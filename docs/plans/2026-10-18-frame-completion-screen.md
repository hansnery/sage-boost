# Frame completion comparison implementation plan

**Goal:** Determine whether per-frame GPU drains distort the comparison
between native bulk rendering and the independent command worker.

**Architecture:** Extend the existing fixed-fixture benchmark with an
optional completion policy. Keep the CPU command queue synchronous at each
frame boundary, but allow two, four or eight frames of GPU work before a
mandatory final drain. Compare native bulk, direct command capture and the
merged-start worker under both policies. This is the first control required
by the completion-boundary audit, not a live game pipeline.

**Tech stack:** Existing WinLibs i686 C/D3D9 fixtures and Python runner.
Implementation proceeds inline under the standing development authorization.

## Constraints

- No runtime source, installed files, INI, stable branch or EA asset changes.
- Every measured batch must finish its GPU work before the timer stops.
- Preserve every intermediate checked frame in its own GPU surface, then
  read and compare all surfaces after the final drain. Keep fresh pose and
  material phases, native state restoration, reference checks and ownership.
- Mode changes require a drained batch. Maximum deferred batch size is eight.
- Preserve the existing per-frame-drained results as a separate control.
- Do not infer gameplay FPS or multiplayer compatibility from this fixture.

## Implementation and verification

- [x] Add `perf/tests/group_completion.h` with bounded saved surfaces/results,
  completion accounting, frame entry/finish guards and a GPU-owner flush.
  `gc_completion_enter(mode)` refuses a mode change with pending GPU work;
  `gc_completion_finish(request)` preserves checked output before reuse;
  `gc_completion_flush()` drains before collecting any saved results.
- [x] Add conditional `GC_FLUSH` handling and the header to
  `perf/tests/bench_group_consumer.c`. Existing builds retain their current
  drain and readback paths. Add `bench_group_completion.exe` to `perf/Makefile`.
- [x] Validate sequences of 2/4/8 frames against separately rendered native
  reference images/states. Fault 16 corrupts only an intermediate saved
  frame; the final frame remains correct. Fault 17 attempts an ownership-mode
  change with pending GPU work. Both must be rejected.
- [x] Time six rotated combinations: native/direct/worker, each with per-frame
  or per-batch drains. Normalize total completion to a regular/extra pair,
  preserve raw total timings and check actual drain/frame counts.
- [x] Extend `tools/bench_group_consumer.py` with `--completion`, explicit
  column metadata, sequence results and fault expectations. Keep the prior
  reports and checks unchanged. Run the DXVK pilot before the full matrix.
- [x] Build with the documented Git Bash toolchain. Run both local images,
  lit/shadow and native/DXVK, sequentially behind the game/launcher guard.
  Repeat the prior frame-start path as a regression. Preserve failed pilots
  and exclude interrupted/contaminated timing runs.
- [x] Record results, source/input hashes, limits and the resulting next
  decision in the spec, STATUS and failure record. Commit explicit paths and
  push `codex/perf-beta`. No install unless separate runtime evidence exists.

Commands (from the checkout; all shell invocations use RTK):

```powershell
rtk "C:\Program Files\Git\bin\bash.exe" --norc --noprofile -c 'export PATH=/c/tools/winlibs-i686/mingw32/bin:/usr/bin:$PATH; cd /c/repositories/sage-boost/perf && rtk mingw32-make build/bench_group_completion.exe'
rtk python tools/bench_group_consumer.py C:/Jogos/AgeoftheRing/rotwk --image C:/Jogos/AgeoftheRing/rotwk/game.dat --completion --pilot --output perf/build/group-completion-pilot.json
```

The full runs add the second `--image .../game820.dat`, omit `--pilot`, and
repeat with `--backend native`. Expected output includes every intermediate
image/state comparison, detected faults, complete sample rows and balanced
queued references. A positive timing result only authorizes investigation
of the next ownership boundary; it is not the 60 FPS success criterion.
