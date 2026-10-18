# Native FX group loop and batching boundary

This is an offline control-flow proof for the combined material/accounting
candidate. It does not implement or install a performance feature, measure
FPS, or qualify a retained cache for the game.

## Verified code and execution boundary

`tools/test_group_contract.py` compares nine complete local native bodies
with the read-only 1.06 reference. It locates each body or its established
context uniquely, preserves internal branches and all frame operands, and
checks external call roles, CRT imports, diagnostic strings and data slots.
Only the explicitly checked external targets and absolute operands are
normalized. Indirect-call instructions remain in the full-body comparison.

The reference identities below are research metadata, never installable
addresses. Both local AotR `game.dat` and `game820.dat` pass.

| Body | Reference RVA | Bytes |
|---|---:|---:|
| PerformRendering | 0x17438B | 819 |
| SEH prologue | 0x629188 | 31 |
| Model-name getter | 0x16A6B0 | 14 |
| Set method chain | 0x1688FF | 83 |
| Clear method chain | 0x168952 | 41 |
| Event constructor | 0x117CF0 | 167 |
| Event destructor | 0x117DA0 | 5 |
| Begin callback wrapper | 0x11F340 | 128 |
| End callback wrapper | 0x11F3C0 | 12 |

Total: 1,300 bytes. The group body SHA-256 on both targets is
`abfa3769f5cf81e2f6e9e3627e8e00a1ce42c1778de932dbe35d186c075194af`.
The tool reports every other body hash. Eleven mutations cover an altered
insertion instruction, FS restoration, RET size, event cleanup state,
multi-instance call, CRT import, SAS slot, constructor role, getter field,
label and duplicate group signature. All are refused on private copies.

The C fixture copies only verified bodies into private pages. Every
remaining call is redirected to a controlled dependency or a verified local
CRT export. No PE entry point, scene, network loop or GPU work runs. The
manifest contains offsets, lengths, hashes and relocation indices; no EA
code or asset is saved. The reference and installed files are read only.

## Batching insertion point

Relative to the verified group entry, offset `0x14F` begins the six-byte
`lea eax,[ebp-0x134]` before the single-mesh label is assembled. The original
loop has already computed the native chunk end, but has not constructed
the mesh event or acquired its owning method temporary. The group event
and group-owned method reference are still alive, with cleanup state 1.

The private prototype retains one or two original members, then handles eligible
remaining records. It sets the chunk end to the first unconsumed record and
resumes at offset `0x2B7`; the native iterator, EndPass, End, mesh finish,
method-chain clearing and final reference release continue normally. A
refusal executes the stolen instruction and resumes at offset `0x155`.
The stub preserves general registers and flags; it uses no FP instructions.

The replacement here records logical draws only. It is not a GPU cache or
production instancing helper. It is restricted to one pass, native chunk
size 1, one method, and both native diagnostic callbacks null. Active
callbacks retain every original event. The pure 14-byte model-name getter
has no writes or calls; skipping its label-only uses does not omit a getter
side effect. The enclosing loop is checked as a complete body.

## Native fixture results

Each image passes 10,752 cases (expanded on October 18 from 5,376 to cover
both boundaries), comparing the original native body with the
private bridge and checking explicit counts and native frame state:

- Empty and 1/2/3/7/25-member ranges; native chunks of 1, 3 and 25.
- Zero, one and two passes; Begin success and failure.
- All four independent BeginEvent/EndEvent callback combinations; named,
  unnamed and empty-name models, using the real event bodies and local CRT.
- Group splits caused by mesh equality, method identity or method count;
  three-method chains and zero-method groups.
- Full batching, refusal before consuming anything, partial consumption
  followed by ordinary rendering, and an unsupported record within a run.
- Logical draw order, pass calls, preparation/finish calls, chain calls,
  final references and chain contents agree exactly. A controlled last-owner
  case also exercises the native virtual destruction branch.
- Native event cleanup states, restored FS exception chain, stack balance
  and callee-saved registers agree. Begin failure keeps the native behavior
  of bypassing successful-path finish/chain clearing.

Four deliberately broken bridges must fail the fixture: a missing logical
draw, a leaked temporary reference, bypassing the active-callback gate, and
advancing past an unconsumed record. These supplement the verifier's eleven
altered-code/data controls.

## Limits and next integration work

The proof covers normal control flow and cleanup state, **not exception
unwinding through the original C++ handler**. An unexpected exception fails
the fixture. Source callback computation, native mesh preparation/rendering,
geometry allocation and GPU operations are controlled dependencies. The
existing GPU fixture separately proves its synthetic output, but the two
fixtures together do not prove a complete runtime cache.

The combined candidate still needs source/input freshness, resource
ownership and reset/eviction handling, reconciliation with the game's
cached device state, and partial GPU-failure handling. In particular,
native per-mesh rendering establishes world/skeleton context before dynamic
source channel 1. A key read before that work cannot simply be assumed to
describe the current mesh. The fixed material fixture prepares its own
inputs; it does not yet reproduce those native source computations.

The reference DX8 initialization resolves BeginEvent/EndEvent only when
`D3DPERF_GetStatus` is present and returns nonzero, and shutdown clears the
slots. This supports checking both live slots before omitting events; it
does not prove that either slot is null in a particular game session.

The standalone dispatch screen remains below its promotion threshold.
Stronger clock/state controls subsequently disqualified the material cache.
The October 18 [leader batching screen](2026-10-18-leader-batch-screen.md)
is near the research threshold, but requires a distinct native mesh/draw
contract and GPU integration. This logical-loop proof does not establish
safe suppression of a leader's draw or justify a new gameplay request.
Last qualifying gameplay remains about 30.18 presentations/s; sustained
60 FPS with fresh full-rate shadows remains unproved.

Reproduce from `perf/` with the documented 32-bit toolchain:

```sh
mingw32-make test-native-group
```

No production source, plugin version, installed ASI or INI changed.
