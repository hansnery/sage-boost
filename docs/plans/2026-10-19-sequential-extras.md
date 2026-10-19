# Sequential Extra Admission Implementation Plan

**Goal:** Use newly available headroom after each extra draw while retaining
conservative per-draw admission and ordinary-update cadence.

**Architecture:** Add `pf_pair_limit_sequential` and `pf_pair_plan_sequential`
alongside the existing pure policy, then use them in the opt-in runtime path.
Existing claim/deadline/eligibility and complete-render checks remain intact.

**Tech Stack:** C, WinLibs i686, existing production-linked virtual-clock tests.
Execute inline under Hans's standing preapproval; no delegation needed.

- [x] Review captured costs and reproduce the upfront-reservation limitation
  with an explicitly controlled model; do not call it captured gameplay.
- [ ] Add focused policy tests for the 12/7/12 ms case, every fractional clock
  phase, constant-expensive work, new expensive samples, late wakes, sequence
  and clock wrap, and equivalence with the old one-extra path.
- [ ] Implement the two policy functions in `perf/src/pairpace.{c,h}`. Choose
  an opportunity ceiling using averages, but leave `pf_pair_claim` unchanged.
  Guard all integer bounds and prohibit repeated quota enlargement.
- [ ] Integrate in `perf/src/fps.c`; extend `test_pair_runtime.c` with actual
  adapter cost variation, first/second stalls, eligibility loss and recovery.
  Bump the plugin/resource version only when integrating the qualified change.
- [ ] Run focused policy/runtime tests, the full plugin suite and integer
  instruction audit. Preserve failures and report modeled pacing/counts.
- [ ] Commit/push explicit changes. Decide whether the evidence justifies a
  local beta with PairMaxExtras=3; preserve all existing INI values and back up
  all installation state before any authorized deployment. Verify hashes and
  the shortcut without launching the game. Public releases remain unchanged.
