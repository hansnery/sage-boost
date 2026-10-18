# Runtime Queue Insertion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.
> Execution here stays inline under the user's standing authorization. Those
> auxiliary skills are not installed; no delegation or new approval is needed.

**Goal:** Put the proved spare-capacity task insertion into the beta plugin,
with a runtime verifier and an ordinary-code fallback. This is one part of
the combined 60 FPS candidate, not completion of the FPS goal.

**Architecture:** Generate a root signature, normalized body hashes and
dependency metadata from the existing 33-body insertion contract. A C
resolver checks the loaded image before a five-byte entry hook is installed.
The general-register handler only fills available storage; all other paths
call the original function through its existing-style entry trampoline.

**Tech Stack:** C/Win32, WinLibs i686 GCC, existing PE scanner/patcher,
Python pefile/capstone verification, native private-memory test fixtures.

## Global Constraints

- Stable branches, installed ASI and INI stay unchanged in this step.
- Do not launch the game or WorldBuilder or modify Open-BFME-2.
- No game logic, network, input, CRC, frame-counter or camera changes.
- No x87 control-word or MXCSR changes; general registers in the handler.
- Unique wildcard signature plus full normalized code/dependency checks.
- No original executable body or asset in generated files; hashes and
  relocation metadata, with a short root signature only.
- Native growth, oversized stacks and destination aliases use original code.
- Commit/push the beta branch after checks; no gameplay request for this
  queue-only step. Integrate sort/batching and qualify the whole candidate.

## Task 1: Generated runtime contract and finder

Files: create `perf/src/queueinsert.h`, `perf/src/queueinsert.c`, generated
`perf/src/queueinsert_table.h`; add the generator entry to
`tools/verify_sites.py` with a focused helper in `tools/queueinsert_table.py`.

Interfaces:

```c
const char *pf_queue_insert_find(const pf_image *image, unsigned *entry);
int pf_queue_insert_install(const pf_image *image, const char **why);
```

- [x] Generate from `bench_native_insert.BODIES`, `recipe` and dependency
  identities, never fixed target RVAs. Use a short wildcard entry signature
  and FNV/CRC of complete normalized bodies. Visit every dependency and
  require consistent identities, executable/data roles and nonoverlap.
- [x] Add the focused command to the required generator entry:
  `python tools/verify_sites.py --perf-queue --write`. It must verify both
  installed image variants and leave unlocker tables untouched.
- [x] Test the C finder against privately mapped images with relocated
  absolute operands. Mutating instructions, dependency targets, section
  roles, root uniqueness or body bounds must refuse before patching.

## Task 2: Handler and real entry installation

Interface for native fixtures:

```c
typedef void (__attribute__((thiscall)) *pf_queue_add_fn)(void *,void *,const void *);
void pf_queue_insert_original(pf_queue_add_fn original);
void __attribute__((thiscall)) pf_queue_insert_add(void *,void *,const void *);
```

- [x] Move the already-tested direct-append behavior into the runtime C
  module. Its original callback is mandatory; unsupported cases delegate
  without consuming the caller's mesh holder first.
- [x] Reuse `pf_install` with the verified root, a five-byte `mov eax,imm`
  prologue and the thiscall handler. Verify the complete closure before any
  code write. Log scanner/refusal/patch failure through the caller.
- [x] Run the existing 576 native insertion comparisons against this exact
  runtime handler, including native growth and alias/deletion controls.
  Preserve the deliberately corrupted-record test as an independent oracle.
- [x] Exercise the installed entry in a private mapped image using the
  existing native closure with controlled heap callbacks. Compare records,
  references and ABI before/after hooking; verify unknown bytes stay unpatched.

## Task 3: Beta configuration and validation

Files: `perf/src/main.c`, `perf/src/config.[ch]`, `perf/src/version.rc`,
`perf/bfme2perf.ini`, `perf/Makefile`, focused tests and project status docs.

- [x] Add `[Render] QueueInsert=1`, independently switchable to zero. Call
  installation only after the master/version gates and before native code
  it depends on is patched. Preserve all existing INI values on any future
  deployment; this step does not deploy.
- [x] Bump the beta version in all three required locations. Add this module
  to the plugin and its finder/native behavior checks to `make test`.
- [x] Run `make test-native-insert`, the runtime finder/hook tests, combined
  GPU correctness and the complete `make test`. Re-measure the actual
  runtime handler in the joined workload serially, with the game closed.
- [x] Update status/backlog with exact scope, verification and installation
  limits. Commit explicit paths with the required trailer and push
  `codex/perf-beta`. Keep the full saved-game 60 FPS goal active.
