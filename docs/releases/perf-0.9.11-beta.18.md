# Performance beta.18: more extra-frame opportunities

This local development beta continues toward 120 FPS. With
`PairPacing=1`, `MaxFPS=120` and `PairMaxExtras=3`, a cheaply completed extra
frame can leave room for a second or third. Previously, reserving the recent
worst-case cost for every extra upfront could discard those opportunities.

Every extra still needs enough time for its conservative measured cost
before it starts. It redraws fresh shadows, preserves reflection settings
and uses the existing state checks. Expensive scenes can still fit only one
extra. `PairMaxExtras=1`, the default, retains the previous behavior.

Controlled tests exercise the actual runtime adapter with variable costs,
fractional clock phases, stalls, lost eligibility, state faults and recovery.
These are scheduling tests, not measured gameplay FPS. Heavy-scene 120 FPS,
real game speed/input behavior and matching-beta multiplayer remain unverified.

This is not a launcher publication. Public Stable and beta.14 remain unchanged.
See the [design and qualification](../specs/2026-10-19-sequential-extras-design.md).
