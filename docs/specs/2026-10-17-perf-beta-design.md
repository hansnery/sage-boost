# Performance beta: extra-frame recovery and render throughput

Status: proposed for review. The user selected the focused beta and larger
rendering investigation on 2026-10-17, skipping settings-only experiments.
Baseline `bb072a9` is clean and pushed on `main` and `feat/perf-dll`.
Development is isolated on `codex/perf-beta`; no installation or publication
is part of this design review.

## Evidence and limits

The installed 0.9.10 settings request 120 FPS, extra-frame shadows,
instancing and verification. Logs were copied before inspection to
`%TEMP%/sage-perf-review-66fcf4e14f8b403f836b70c046b12362`.
The log's modification time is 2026-10-17. Whether this was the multiplayer
match in the user's report is not confirmed. The user's report establishes
successful online play, not every outstanding multiplayer test case.

In the timing CSV, select rows with `anim_on_screen > 20` and `frames >= 15`.
There are 1,146 such rows; all report zero extra frames. Weighting the
per-frame measurements by each row's frame count gives approximately:

- Render: 9.300 ms; skeleton within render: 0.425 ms.
- Shadow pass: 4.093 ms; main views: 4.925 ms; reflection: 0.575 ms.
- Terrain: 0.289 ms; FX meshes: 2.161 ms.
- Available limiter budget: 18.645 ms; expected extra cost: 30.614 ms.
- Regular-draw reference cost: 11.278 ms.

Scopes overlap and must not be added together. These samples are a scene
filter, not proof of network mode or a controlled before/after benchmark.
Regular render cost is not a guarantee of the next extra frame's cost.

The log's final instancing summary reports 5,175,233 mesh fallbacks from
declaration-cache exhaustion. `inst.c` has 256 entries and refuses all new
declaration identities after filling them. This is a confirmed throughput
limitation; the resulting time savings remain unmeasured.

The final extra-frame lateness event measured 30,614 us after a guessed
cost. In `fps.c`, `late_frame` raises `g_learned_us` on that path;
`reference_cost` uses it as a floor, while its only reduction requires a
successful extra frame. Recovery cannot lower the estimate below that
floor. A floor above the available budget can therefore prevent its own
recovery. The trace is consistent with this mechanism; reproduce it in
the scheduler harness before claiming the match's cause is established.

## First beta: bounded recovery and cache replacement

Use plugin version `0.9.11-beta.1` (numeric Windows resource 0,9,11,1).
Keep the unlocker and the established public package unchanged.

### Extra-frame recovery

First add a deterministic regression reproducing a guessed 30,614 us
draw followed by sustained cheap regular draws and sufficient idle time.
Also retain the existing test in which extra draws remain expensive:
recovering from a transient must not produce a repeated-lateness loop.

Permit a single recovery probe after 300 consecutive eligible game frames
where no extra was drawn, regular-draw measurements are available, and a
probe based on those measurements fits the existing scheduler and margin.
Reset eligibility on catch-up holds, safety shutdown, pause/rest, minimized
window, screenshot activity, unavailable budget or a successful extra.
Do not lower the learned floor globally just because time passed.

The probe's temporary estimate is the greater of twice the regular-draw
reference and the existing minimum estimate. It must fit before the game's
deadline with the existing margin and slack. Only one extra is allowed in
the probe frame. A timely probe can reseed the old cost history and learned
floor with its measured cost. A late probe raises the floor, uses existing
pause accounting, and counts toward repeated-lateness escalation even if
its cost was guessed. Never bypass a permanent safety shutdown.

This policy deliberately may not probe when twice the regular draw cannot
fit. It favors multiplayer deadlines over forcing a higher FPS number.
Tests must cover transient recovery, persistently expensive extras,
interrupted eligibility, timer wraparound and unchanged safety shutdowns.

Add per-row counts of game frames blocked by cost, catch-up, rest/pause,
or safety shutdown, plus probe attempts/successes/lates and the learned
floor. Count once per game frame, not on every busy-wait iteration.
Document which reasons can overlap and preserve existing CSV column order
by appending fields. Extend row-format and scheduler tests.

### Declaration cache

Keep memory bounded. Replace the permanent-full behavior with least
recently used replacement of the existing 256 entries. Hold references on
both original and generated declarations while cached; release the old
references before reusing an entry. Include unusable declarations in the
bounded cache to avoid reparsing them for every mesh.

Eviction must occur only between completed groups, never while a pending
batch still refers to that entry. Audit `capture`, `flush` and device
transitions before implementing replacement. Preserve state restoration
and ordinary rendering fallback when declaration creation fails.

Record cache hits, misses, evictions and creation failures so testing can
distinguish improved coverage from excessive declaration recreation. Do
not simply enlarge the table and postpone exhaustion.

Exercise more than 256 distinct declarations, repeated hot declarations,
unsupported declarations, creation failures and reference balancing. The
render harness must still compare output and restored device state.

## Larger rendering investigation

Prioritize shadow and mesh work, keeping each behavioral experiment in a
separate subsequent beta so its effect is measurable.

1. Measure after cache repair. Compare shadow/main-view time and instancing
   fallback reasons in comparable scenes. Do not assume every avoided
   fallback translates into a proportional FPS gain.
2. Audit the unrecognized shaders reported by hashes `CF3571C7` and
   `94092159`. Read local assets only. Add an independently authored
   instanced variant only after identifying its roles and obtaining equal
   output in the offline shader harness. Keep unsupported paths stock.
3. Investigate shadow groups split at model boundaries. Current logs count
   roughly 1.76 million such splits, but that is opportunity data, not proof
   they can merge. Establish material, geometry, bone, stream and device
   state equivalence before proposing any cross-group batching change.
4. Reassess skeleton work after those improvements. In this trace it is
   about 4.6% of render time; eliminating it entirely would save only about
   0.43 ms per regular frame on average. Worker scheduling is not justified
   by this evidence. The historical worker proposal also loads x87/MXCSR,
   which current repository rules prohibit; do not implement it unchanged.

Each larger experiment needs its own concrete implementation design once
the audit establishes a safe change. Do not implement speculative scene
reordering or animation threads merely to use more CPU cores.

## Invariants and verification

- Visual rendering only. No changes to game-frame cadence, network, input,
  logic random draws, CRC-visible state, frame counters or frame limits.
- Never move the camera; never touch x87 control word or MXCSR.
- New executable hook sites require unique verified wildcard signatures.
- No changes to `dll/`, reference repos, installed INIs or EA assets.
- No copied implementation from SAGE-Unlocked or bfme2-accelerator.
- Run the complete `perf/` test suite and the offline instancing GPU check.
  Report skipped GPU checks as skipped, never as passing coverage.
- User tests gameplay: comparable SP and MP scenes, heavy battles,
  scrolling, shadows, loading transitions and sustained online play.
  Compare presented frames, extra-frame lateness, game speed and visual
  correctness. Higher FPS alone does not establish success.

## Release isolation

Keep `main` and `feat/perf-dll` on the pushed baseline during development.
Build artifacts and notes identify the prerelease version and source
commit. A beta package contains only the ASI and its INI template; existing
user INIs are preserved. Stable and beta ASIs must never coexist under
different names in `mss`, because Miles loads every ASI there.

Before publication, verify the Realms launcher's actual opt-in channel and
rollback semantics. An earlier release through Launcher Beta does not by
itself prove per-component channel isolation. Publication, deployment and
tags require the user's request. Keep a known-good stable download and
back up the installed binary, INI and logs before an authorized deployment.

## Next implementation step

Review this design, then write the exact implementation plan for the first
beta. Larger rendering audits can proceed read-only while the first beta
is developed, but must not silently expand its code scope.
