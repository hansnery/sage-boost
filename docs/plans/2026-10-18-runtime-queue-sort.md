# Runtime rendering-queue sort plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Execution stays inline under the user's standing authorization. Those
> auxiliary skills are not installed; no delegation or new approval is needed.

**Goal:** Integrate the sorting portion of the combined 60 FPS candidate,
preserving exact record/tie order and native behavior outside its contract.
Queue insertion is already integrated at cfd22d1. No standalone installation
or gameplay request for this intermediate step.

**Design:** An independently written integer-only introsort uses raw 32-byte
record moves. Match the verified median/partition/right-recursion rules,
depth limit, 16-record final insertion threshold and heap fallback. Only the
two verified pure comparators qualify. Preflight owned record references;
unknown comparators/unsupported ranges delegate to the native trampoline.
No global replacement of constructors, assignment or destruction. Native
sorting bodies are never copied into the plugin or cloned for runtime use.

## Implementation

- [x] Extract the insertion verifier's hash/dependency checks into a shared
  runtime contract verifier supporting multiple unique signature roots.
  Keep the insertion contract/refusal results unchanged.
- [x] Generate the sort contract from the 38 verified bodies, three roots
  and distinct external dependency identities. Use symbolic indices and
  hashes only; extend `verify_sites.py` with `--perf-sort [--write]`.
- [x] Implement record sorting, known-comparator dispatch and native fallback.
  Compare exact records/references/ABI against native ordinary and forced
  heap paths, including threshold boundaries, tied keys and split queues.
- [x] Exercise a real five-byte entry hook in private mapped images, with
  refusal/no-write controls and unknown-comparator fallback. Verify both
  hook installation orders coexist with queue insertion.
- [x] Add `QueueSort=1`, log activation/refusal, bump beta.5, and include
  runtime checks in `make test`. Preserve installed INI and stable branches.
- [x] Join the actual production sort to the insertion/GPU fixture, run
  correctness/negative controls and measure serial paired workloads.
- [x] Record evidence/limits, commit and push the beta branch. Continue with
  bulk-render runtime integration and remaining device/replay/lifetime gates;
  sustained 59+ actual presentations/s still requires the user's capture.

## Constraints and evidence

Read-only Open-BFME-2/reference images; never launch the game/WorldBuilder.
No x87/MXCSR, graphics reduction, camera movement, logic/network/input or
frame-counter changes. General-register production code. Do not replace
native ordering with merely sorted output: require byte-identical ties.
Native sort has a cdecl three-argument entry whose first five bytes are
position-independent `push esi; mov esi,[esp+8]`. Runtime discovery uses
signatures and complete body checks, never these research RVAs.

Existing native sort tests supply 936 comparisons per executable plus
changed-body/corrupt-copy controls. Reuse them against the real production
algorithm. Retain full insertion checks while sharing its verifier. GPU
tests must still compare owned records, pixels/depth, ordered groups,
engine/effect/device state and references with fresh shadows.
