# Material-chain runtime implementation plan

> Execution: inline after committing the independent recovery qualification.

**Goal:** Apply the qualified follower-loop gate to method counts 1..6 and
report actual coverage in the next substantive beta.

**Architecture:** Preserve native grouping and every other bulk/instancing
guard. Count consumed followers inside `pf_inst_bulk_advance`, separating
regular/extra frames and method counts. Append raw row counters to CSV.

**Constraints:** No new hook or executable address. No logic, network,
input, camera, shadow cadence or graphics-quality changes. No game launch.
Stable branches and installed INI values stay unchanged. A saved-game
60 FPS claim still requires real gameplay evidence.

## Runtime and reporting

- [x] Require the completed material-chain qualification commit first.
- [x] In `perf/src/bulk.c`, replace the count `!= 1` admission condition
  with `(*(const unsigned *)(first + 4) - 1u) > 5u`, rejecting zero and
  out-of-range counts while accepting the six valid native slots.
- [x] Extend `pf_inst_counts` with `unsigned bulk_followers[2][6]`.
  Save the starting record in `pf_inst_bulk_advance`; after consumption,
  increment that method count's regular/extra bucket in take and totals.
  These mean consumed records, not necessarily successful submissions.
- [x] Append twelve `bulk_followers_1..6` / `extra_bulk_followers_1..6`
  raw per-row CSV columns. Test values, absent instrumentation and maximum
  row width. Add screen assertions for exact method/extra attribution.
- [x] Teach `prepare_chain_screen.py` to recognize exactly one supported
  old/new guard, preserving its same-process old-gate comparison.
- [x] Add recovery targets that link the actual production `bulk.o`, not
  the generated switchable gate. Run normal both-image/backend/lit/shadow
  controls with six/twenty bones; deliberate disabled-gate mutations apply
  only to the switchable screen, not production objects.
- [x] Bump main/resource/load-test version to `0.9.11-beta.13`, run the full
  suite and general-register instruction audit, then refresh paired DXVK
  timing on lit and fresh-shadow twenty-bone/six-method workloads.

## Delivery

- [x] Record concrete results, hashes, limitations and live-coverage
  uncertainty in STATUS, BACKLOG and beta.13 release notes.
- [x] Commit explicit paths with the existing co-author trailer and push
  the authorized beta branch. Keep main, stable branch and tags intact.
- [x] If the tested change remains substantial, back up installed beta.11,
  INI and logs/CSVs to a fresh timestamped TEMP directory, copy only the
  tested ASI, verify build/install hashes and unchanged INI bytes, and record
  the installation receipt. Do not launch the game.
- [x] Only then request the user's same heavy-save test. Keep the 60 FPS
  goal active until a settled capture demonstrates it.
