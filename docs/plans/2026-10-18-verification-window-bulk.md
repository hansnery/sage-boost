# Preserve bulk rendering for unsampled verification groups

**Goal:** Remove the two-second blanket bulk-render exclusion while retaining
the existing verification schedule, quotas, native sample path and failure policy.

**Architecture:** Share one read-only sample-selection predicate between
`evaluate` and `bulk_eligible`. At the leader's captured DIP all fields used
by selection are known. Only a group actually selected for verification
must retain its native leader and per-member path. Unsampled groups may use
the already qualified bulk path. Leave window clocks, quotas and settings intact.

**Scope:** Production changes are limited to `perf/src/inst.c` and the next
beta version identifiers. No camera, logic, shadow cadence or INI changes.
Reference bodies remain read-only and private to offline fixtures.

- [x] Add a production-source policy regression test through the existing
  ownership fixture. Cross extra/regular, verification/window on/off, total
  and shadow sample limits, category/bonelist quotas and bonelist priority.
  Assert unchanged selected mode and accounting; unsampled eligible groups
  retain bulk. Run against the old implementation and require a failure.
- [x] Factor `verify_group_selected(void)` from evaluate's existing decision.
  Have evaluate and bulk_eligible use it without changing any counters or
  sample timing. Run the exhaustive policy test and existing instancing tests.
- [x] Extend native/GPU controls with an open window whose category quota is
  exhausted. Require native selected samples, bulk unsampled groups, equal
  pixels/engine state/references and unchanged verification counts. Exercise
  both images, native D3D9/DXVK and fresh shadows.
- [x] Measure the complete old/new native queue/group path with a forced
  unsampled open window; keep insertion/sort/geometry and inputs identical.
  Include six- and twenty-bone cases, recording CPU and completion separately.
  This isolates the window penalty; it does not establish gameplay duty cycle.
- [x] Evaluate promotion: the measured gain does not warrant installation.
  The beta development version is bumped and the full suite passes. Keep
  beta.11 installed; commit/push the correction and evidence. The following
  deployment gate remains conditional for a future combined improvement:
  if the measured gain warrants promotion, bump the beta version, run
  the full suite and required fallback/failure regressions, commit/push, then
  back up and install with unchanged INI and matched hashes. Otherwise retain
  the evidence without installing. Final 60 FPS proof still requires gameplay.

Results: 23,040 policy states; twelve native/GPU control runs; eight timing
runs and 168 samples. Separate timing runs show drift in unchanged work.
The live capture fills all six samples within one reporting row per window,
so it does not support the forced benchmark's duty cycle. The next larger
lead is shadow fragmentation; opacity is a three-factor product in the
public reference, and no runtime opacity change is qualified or implemented.
See [evidence and limits](../specs/2026-10-18-verification-window-bulk.md).
