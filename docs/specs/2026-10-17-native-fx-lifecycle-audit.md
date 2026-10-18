# Native FX lifecycle contract for the material-cache experiment

## Result

An offline harness now executes eight verified native wrapper bodies from
each local executable, with controlled dependencies and synthetic objects.
All 58 trace cases pass for both `game.dat` and `game820.dat`. The verifier
also rejects 13 synthetic altered-code cases. This establishes part of the
calling contract needed by a cache; it is not an optimization or FPS result.

The 570 bytes of wrapper code are read from the user's executable into
private memory at run time. No game executable, shader or other asset is
saved. The executable entry point, imports, scene and game loop never run.
The harness does not attach to or modify the running game. It needs no GPU.

## Reproduction and checks

From `perf/` with the usual 32-bit toolchain, run `mingw32-make
test-fx-lifecycle`. The target runs the synthetic verifier refusals and both
local executable checks. `AOTR_DAT` and `ROTWK_DAT` select the input images.

`tools/test_fx_lifecycle.py` scans each target using the read-only 1.06
reference. It compares complete normalized bodies, verifies every direct
dependency and virtual slot, and rejects external branches, absolute data
access, image-pointer immediates or partial instructions. The binder's
single verified tail jump is explicitly allowed. Only direct call/tail
fields are relocated. Ordinary constants, including Begin's flags, remain
in the full-body comparison. A temporary manifest contains only numeric
offsets, sizes, hashes and dependency indices; it is removed afterward.

`perf/tests/test_fx_lifecycle.c` independently checks the loaded byte hashes
before execution, redirects dependencies, and makes the copied pages
execute/read only. Synthetic asset/effect objects validate receiver and
argument identity at each virtual call. A general-register trampoline
checks stack cleanup and preservation of EBX, ESI and EDI on every native
entry. It does not modify the x87 control word or MXCSR.

The copied bodies are the effect getter, Begin, BeginPass, dynamic update,
EndPass, End, asset binder and six-channel source walker. The controlled
dependencies are render-state application, texture lookup, the successful
Begin statistics increment and the final source callback invocation.
Those dependency implementations are **not** proved by this harness.

## Contract established

- The getter checks asset initialization and can initialize it before
  returning the effect. Treating it as a raw pointer read omits work.
- Begin clears the caller's pass count first. With valid asset, effect and
  technique it applies pending render state, fetches the effect again,
  sets the technique, applies a non-null parameter block, resolves/binds
  each texture, and runs source binding before D3DX Begin with flags 6.
- SetTechnique and Begin failures return false. Parameter-block and
  texture HRESULTs are ignored by the native wrapper. Successful Begin,
  including a zero-pass result or `S_FALSE`, increments statistics once.
- BeginPass, EndPass and End obtain the effect through two getter calls.
  Their COM HRESULTs are ignored. Dynamic update applies pending render
  state, binds the selected sources, fetches the effect again and commits.
- Selector `0xFFFF` visits all six source channels in order. Selectors
  zero through five visit just that channel. The tests cover multiple
  entries, an empty channel, a missing technique and null links/owner.
  The native walker does not provide a general bounds check for arbitrary
  selectors; a future bridge must accept only its verified selector set.
- Begin retains the effect obtained after render-state application across
  source binding; Dynamic fetches it after binding. A controlled pointer
  swap proves this difference. It demonstrates a contract hazard, **not**
  evidence that normal gameplay swaps effects at that point.
- A complete three-pass sequence preserves source updates, pass calls and
  the single Begin statistics increment, with balanced native stack use.

## Consequences for integration

The fixture's persistent cache cannot become a runtime feature by skipping
the entire native Begin/update sequence. Asset readiness, render-state
application, input callbacks and statistics must still follow the native
contract. A lookup made before those inputs settle can describe the wrong
material. An effect/technique identity change must invalidate any replay.

A hit must also preserve a coherent D3DX lifecycle. Suppressing Begin and
BeginPass while forwarding real CommitChanges or End is not a proved
substitute. Unknown methods, multiple passes, changed geometry, unsupported
state, device loss and failures must fall back before any partial draw.
The tested fixed objects are not a lifetime policy: runtime cache entries
need valid resource ownership, eviction and reset handling, with no stale
mesh, method or declaration-cache pointers.

The reference group loop additionally chains rendering methods, owns a
method reference, prepares the leader mesh, opens diagnostic scopes, walks
passes and mesh chunks, then finishes the mesh and clears the chain on its
successful path. Its Begin-failure path exits without that successful-path
cleanup. This is source-audit evidence; the group loop itself has not been
executed by this harness. Its complete ABI/state/fallback contract remains
an integration obligation, as do per-mesh diagnostics and visibility.

## Performance and installation

The game and launcher remained running during this audit. No CPU/GPU
performance qualification was attempted. The earlier 504 material-cache
samples remain excluded for concurrent game activity. The process guard
must succeed before collecting replacements.

No production source, ASI, installed INI or game process changed. The
complete existing `make test` suite also passes. Build and installation
hashes were checked and remain those recorded in STATUS.md. The target
remains sustained 60 visual FPS with fresh full-rate shadows in the
user's troop-heavy save; the last qualifying capture remains about 30 FPS.
