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

## Independent packet consumer

Keep the production frame/timing/extra-frame hooks on the producer. Reuse
the existing context-based `pf_submit_capture`/`pf_submit_batch` engine in a
worker-owned consumer instead of transferring the native instancer and
timing globals to the worker. This removes the fixture-only ownership
workaround before attempting a live capture bridge.

- [x] Add `perf/tests/packet_consumer.h` with private shader/declaration/
  instance-buffer ownership and direct checked draws from copied palettes.
  Material effects remain owned by the GPU consumer; no native object or
  callback is invoked from the worker.
- [x] Add an optional consumer mode to `bench_batch_worker.c` and
  `bench_worker_reset.c`; the unchanged hook-based path remains the oracle.
  Producer preparation uses actual frame/render timing stubs. During an
  untimed worker pause inside an open effect, exercise producer frame hooks
  and require unchanged worker/global-extra state and balanced counters.
- [x] Require exact original images/state, fresh palettes/material/matrices,
  independent frame counters, all reset/generation controls and zero final
  device references on both executable variants and both D3D backends.
- [x] Run existing worker regressions, retain logs/hashes and document the
  remaining native capture/Begin/result and full-scene integration work.

This changes the consumer architecture rather than making the existing
single-thread globals concurrent. The fixture's single material family and
fixed resources still do not qualify a general live renderer.

Both images/backends pass 240 image/state cases, 24 real resets, eight final
zero-reference releases and 72 fault processes. Old reset/owner/batch paths
preserve another 104 image/state cases. No consumer timing is collected and
no production source or installed file changes. The full suite previously
passed at `9121da8`; affected optional paths were rerun for this extension.

## Native group result boundary

Execute inline. Extend the existing verified group/GPU fixture rather than
starting another synthetic group loop. Preserve the actual effect Begin
HRESULT and returned pass count before native control flow continues.

- [x] Split the independent consumer's checked group draw from its frame
  loop in `perf/tests/packet_consumer.h`; retain the old worker regression.
- [x] Add `perf/tests/bench_group_consumer.c`, executing native group calls
  on the producer and copied palette draws on a GPU worker. Begin requests
  return actual results; native EndPass/End publish one owned group packet.
  Keep original native bulk drawing as the independent image reference.
- [x] Exercise alternating effect identities, changed material/pose,
  failed Begin, copied-buffer poisoning and native method reference balance.
  Compare direct capture with the worker including waits and GPU completion.
- [x] Use both local executable variants and both graphics backends, retain
  logs/hashes, and select the next integration step from measured cost.
  Do not install a synchronous group bridge merely because it is correct.

Both images/backends pass 96 candidate image/state cases, 48 ordinary/bulk
reference images and 32 deliberate faults. Lit pixels and captured state are
exact; shadow depth differs by at most 1/2^24, within the existing tolerance
of 256/2^24. The original packet consumer preserves 60 exact image/state
cases, six resets and two zero-reference releases; 18 deliberate faults are
rejected. Both material state-filter workloads also pass.

All 96 paired timing samples regress; workload medians add 13.37-17.84 ms
per regular/extra pair against the native bulk prototype. Reject this
synchronous handoff. The next experiment must avoid per-group waits while
preserving actual effect results, explicit caller-state restoration and
owned resources. Fixed single-pass callbacks do not establish the full
native FX lifecycle or a live render bridge. No installation follows.

The full plugin regression suite passes without skips. Final source, binary,
input and log hashes are recorded in the group report; installed beta.13 and
INI hashes remain unchanged.

## Producer effect evaluation with batched device commands

Execute inline under the continuing development authorization. The rejected
per-group reply path stays available as a regression. Keep native method
control and real effect evaluation on the producer; move only recorded
device-state calls and copied palette drawing to the GPU owner. Use two
owned slots, publishing at most sixteen groups per batch. This is a measured
alternative to per-group replies, not a claim that general asynchronous
driver failures can already be propagated to native callers.

- [x] Add `perf/tests/group_commands.h`: a bounded effect-state manager that
  copies callback arguments, retains queued COM resources and records group
  save/draw/restore commands. Return allocation/unsupported-command failure
  to the real effect call; never predict Begin's returned pass count.
- [x] Extend optional `bench_group_consumer.c` under `PF_GROUP_COMMANDS`,
  preserving the existing build. Evaluate effect callbacks on the producer,
  copy palettes there and publish completed batches to the existing queue.
  Use the existing explicit render-state/texture restore block for flags=6.
  Split the packet consumer's already-loaded draw from its effect setter so
  the worker never touches the producer's effect object.
- [x] Audit every public IDirect3DDevice9 vtable entry during untimed correctness
  runs: any producer device call inside effect evaluation fails immediately.
  Remove audit wrappers before timing all modes, including the native bulk
  baseline. Deliberately inject a forbidden device call to prove the audit.
- [x] Compare native bulk, direct command capture/replay and worker batches
  with complete regular/extra GPU pairs. Require old pixel/state references,
  copied-buffer poisoning, real Begin replies, alternating effects and
  balanced native/queued resource ownership. Reject stale palettes, wrong
  resource identity, omitted restoration and command overflow.
- [x] Run both images/backends sequentially through an extended runner,
  preserving raw timings, input/log hashes and failed pilots. Re-run the
  original group/packet paths after shared helpers change. Document measured
  outcome and remaining native resource/result boundaries before commit/push.
  Install only if later runtime work supplies substantive improvement proof.

Completed: 96 candidate image/state cases, 27,600 individual restoration
checks and 64 fault processes pass. The audit also covers state-block
Capture/Apply, which exposed automatic saving despite a custom manager.
Begin(flags=7) plus explicit queued restoration passes this fixture; it does
not establish original flags=6 failure equivalence or deferred driver-error
propagation. Native D3D9 saves a median 0.10-0.73 ms per pair, while DXVK
regresses 2.31-3.08 ms. No runtime promotion or installation follows.

Regression checks pass: the original group path retains its 24 image/state
cases and eight faults; the packet consumer retains 60 cases, six resets,
two zero-reference device releases and eighteen faults. Both material-filter
workloads and the full plugin suite pass without skips. Source, binary,
input and log hashes are preserved in
`../benchmarks/2026-10-18-group-command-batches.json`. Installed beta.13 and
INI hashes remain unchanged.
