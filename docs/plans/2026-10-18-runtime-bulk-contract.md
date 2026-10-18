# Runtime bulk-render contract implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

The named execution subskills are unavailable in this environment. Execute
inline under the user's standing authorization; do not delegate.

**Goal:** Give the bulk renderer a runtime verifier for the native group,
mesh, effect and draw boundaries already exercised by the offline fixture.

**Architecture:** Extend the existing fingerprint verifier with dependency
addends and optional region bounds/roles. Generate one joined contract from
the existing read-only reference audits, including the draw jump table and
immutable literals. Exercise the actual runtime finder against relocated
private images and altered dependencies before connecting a rendering hook.

**Tech Stack:** i686 C, Python/Capstone/pefile, existing signature patcher.

## Global constraints

Read-only Open-BFME-2. Never launch the game or WorldBuilder. No native code
or assets persisted, no fixed runtime addresses, no changes to logic,
network, input, camera or floating-point control state. Keep the installed
binary/INI and stable branches unchanged until the complete candidate is
qualified. Runtime discovery must precede hooks which alter verified bodies.

## Task 1: Joined executable contract

Files: `perf/src/nativecontract.[ch]`, `perf/src/bulkcontract.[ch]`,
`perf/src/bulkcontract_table.h`, `tools/bulkcontract_table.py`,
`tools/verify_sites.py`, `perf/tests/test_bulkcontract.c`,
`tools/test_bulkcontract.py`, `perf/Makefile`.

- [x] Add optional region spans/writable requirements and relocation addends
  to the shared verifier. Existing insertion/sort metadata defaults to zero
  addends and its existing roles. Bound-check before pointer reads.
- [x] Generate the joined graph from the existing group, mesh and FX
  audits. Deduplicate common dependencies. Verify literals by hashes,
  mutable data by full section span, and jump entries by draw-relative
  instruction offsets. Emit only signatures, numbers and hashes.
- [x] Implement `pf_bulk_find(const pf_image *, pf_bulk_sites *)`, clearing
  all outputs on refusal and returning only signature-derived sites.
- [x] Map each local image privately; rebase verified absolute fields once.
  Require exact discovered positions, then change every dependency and
  representative bytes in every body/literal and require no-write refusal.
  Include missing/ambiguous roots, changed jump targets, truncated regions,
  conflicting identities and a relocated image base.
- [x] Run `make test-bulkcontract` and `make test` (which includes insertion
  and sorting entry/refusal tests) after building the verifier change. Check disassembly
  for floating-point instructions. Record tested source/build hashes.

## Task 2: Runtime integration handoff

- [x] Record the verified sites, exact installation ordering and remaining
  source-lifetime/device-failure guards. Commit/push the reviewed change.
  This verifier is a prerequisite, not evidence of gameplay FPS or a reason
  to request an intermediate saved-game test.
- [ ] Continue into the production bulk engine and actual bridge. Preserve
  native method/group limits, callback/verification exclusions, pending
  staged ownership and coherent recovery. Install only the complete tested
  improvement; the final goal still requires 59+ actual presentations/s.
