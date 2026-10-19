# Native source capture into worker packets

Execute inline within the continuing development authorization.

- [x] Add an offline numeric command format and producer adapter over the
  existing verified native matrix bodies, with producer-thread assertions.
- [x] Extend the worker fixture with optional capture/replay hooks and
  worker-only effect-handle resolution, retaining the direct baseline.
- [x] Validate both local images on lit/shadow GPU workloads, with native
  verifier refusals, provider/scratch poisoning and wrong-setter controls.
- [x] Retain capture-inclusive timings and hashes, run the unchanged-path
  regression, document remaining native/resource boundaries, commit/push.

The full plugin suite passes. Final array indexing was normalized to a flat
command array; both resulting harnesses are byte-identical to the tested
ones except PE timestamp/checksum fields, recorded in the report. No
production code, installed file, stable branch or release changed.

## Consumed native pose work and overlap

- [x] Reuse our verified native decoder loader in a separate general-register
  adapter; feed decoded next endpoints into actual rendered bone positions.
- [x] Compare serial worker preparation/draw against producer preparation
  with serial or bounded pipelined copies, retaining complete GPU timings.
- [x] Add decoded-versus-raw and stale-pose image controls; run warm cursor
  advances zero/one, both executable variants, lit/shadow and repeat orders.
- [x] Re-run the original matrix and batch worker variants; preserve results
  and all input/log hashes in the native-pose worker report.
- [x] Document the 0.75-1.94 ms synthetic overlap benefit and the unqualified
  live source/resource/device boundaries. No runtime promotion or install.

This optional test-only extension passes 144 pose packet image/state cases,
18 deliberate output faults, 24 alias and 24 capacity/recovery controls.
The unchanged worker variants pass another 44 image/state cases. The full
production suite previously passed at base commit `da85d4e`; it was not
repeated for this extension. Installed beta.13 and INI hashes match.

## Window/device owner handoff

- [x] Verify local native creation bodies and flag paths using read-only
  `tools/verify_device_owner.py`; record both images and altered contracts.
- [x] Add optional `bench_window_pose_worker.exe` over the existing worker,
  with producer-owned initialization, MULTITHREADED creation and message-aware
  collection. Require creation/window identity and exact pre-handoff images.
- [x] Extend the runner with `--window-owner`; execute both lit/shadow paths,
  stale-data faults and original-variant regressions. Preserve complete
  timings, hashes and the unresolved Reset/destruction limitations.

Native creation contracts pass both images and 12 altered-code refusals.
Owner-mode GPU runs pass 96 image/state cases, 16 corrupted-output cases,
four missing-flag refusals and all ordered window-message checks. Three old
worker variants pass another 68 image/state cases and ten deliberate output
faults with unchanged reference hashes. Installed beta.13/INI hashes match.
No full-suite rerun or production change; the shared fixture's own instancing
checks execute in each GPU process. Resource teardown and separate live
frame/render bookkeeping are the next integration boundary.

## Reset/resource generations

- [x] Extend the existing owner fixture with closed admission, full-queue
  draining, effect lost/reset callbacks and default-pool resource recreation.
- [x] Keep lifecycle calls and final device release on the window owner;
  release fixture/cache references and require zero final device references.
- [x] Reject stale resource generations, exercise injected failed Reset
  results and fail deliberate early-teardown/omitted-generation controls.
- [x] Run both executable variants on DXVK and native D3D9, retaining the
  original image/state references across three real resets per process.
- [x] Re-run the owner and original batch worker regressions with unchanged
  reference hashes. These checks pass another 44 image/state cases.
- [x] Complete the full plugin regression suite and preserve its log/hash.
- [x] Record source/input/log hashes, unchanged installed beta/INI hashes and
  the remaining native lifecycle boundaries for the development commit.

Eight lifecycle processes pass 240 exact image/state cases, 24 real resets,
24 injected failure results, 24 stale-generation and 48 closed-admission
refusals. All 56 deliberate fault processes reach their expected checks.
This is fixture-owned teardown with a joined/restarted worker, not a live
native Reset hook, arbitrary resource registry or actual lost-device test.
Separate frame/render bookkeeping and native lifecycle integration remain
the next steps; no runtime change or installation is justified by this test.
