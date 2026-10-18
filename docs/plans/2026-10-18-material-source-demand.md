# Material-source demand audit

**Goal:** Identify the live unit effects' requested native material sources
and possible duplicate annotation/standard-semantic bindings before spending
time on a source cache. This audit cannot establish saved-game FPS.

**Architecture:** Add a read-only mode to the existing local D3DX effect
fixture. Query actual parameter annotations and standard semantics, retaining
only hashes and counts. Compare top-level requests for each technique using
the native binder's parent usage rule. Do not use unreliable leaf usage as
a pruning gate. Control the duplicate detector with an independently authored
effect containing both an annotation and a semantic on one parameter.

**Files:** `perf/tests/effect_source_audit.h` contains classification and
queries; `bench_effect.c` selects `--sources-only`; the Makefile tracks the
new header. Evidence and limits go in `docs/specs/` and `docs/benchmarks/`.

- [x] Add the explicit audit mode without changing default/member behavior.
  `source_control(create_effect_fn, unsigned)` requires one detected duplicate
  and a separate semantic-only binding. `audit_sources(ID3DXEffect *,
  unsigned, const char *)` emits used-root family/shape/dynamic-set metadata
  and exact duplicate-request counts, never shader or parameter payloads.
- [x] Build `build/bench_effect.exe`, then run `--sources-only` against the
  local effects under D3DX27 and D3DX43 on native D3D9 and DXVK, serially and
  guarded against a running game. Require the authored controls to pass.
- [x] Preserve raw metadata and executable/source/input hashes. Inspect the
  default unit and shadow technique demand before selecting native bodies
  for the next cost screen. Source requests are not measured callback counts.
- [x] Screen generic numeric parameter uploads separately with
  `--source-writes`: expand array/structure fields, retain each skeletal
  array as one twenty-bone upload, and exclude object/texture fields.
  Require round-trip and changed-value controls. Measure 21 samples after
  four warmups: isolated writes use 32 workloads of 850 groups; paired
  lifecycle modes use eight. Include actual Begin/Pass/Commit/End calls
  both with updates and with already seeded values, so persistent dirty
  flags cannot hide the cost of repeated writes after a real commit.
  The zero-write control is not a runtime candidate. This isolates upload
  granularity; it does not execute or time native source computations.
- [x] Extend the lifecycle comparison to changing twenty-bone palettes and
  both ideal static reuse and a live GetValue/byte-compare filter. Before
  timing, clear effect dirty state, poison device constants and compare
  in-pass shader hashes/constants. Change an effect parameter independently;
  require the filter to recover it and an unconditional skip to differ.
- [x] Record whether duplicate binding or matrix-source reuse has enough
  evidence to pursue. Leave installed beta.11/settings/stable unchanged;
  commit/push the useful audit, never label it an FPS improvement.

Result: no exact duplicate requests among 152 technique/runtime/backend
observations. The default unit technique does not request the combined
world-view/world-view-projection semantics. The upload filter saves
0.79-1.05 ms per modeled 850-group workload with changed poses, including
live getter/comparison cost. This is an offline lead below the standalone
promotion threshold, not a new installed feature. Native typed callbacks,
parameter blocks, error equivalence and whole rendering still need proof.
