# UI vertex upload batching screen

The native UI boundary audit excludes whole-HUD reuse. A narrower candidate
changes vertex uploads below those update, clock and custom-control boundaries.
The reference `W3DAptRenderer::BeginTriangleList` (238 bytes) batches draws,
but locks the GPU buffer for each geometry request. Its existing batch flush
retains order and uses a 20,000-vertex ring with DISCARD/NOOVERWRITE behavior.

Choose owned CPU staging followed by one upload at each existing draw batch.
Keeping a GPU buffer locked across arbitrary UI callbacks risks drawing while
locked. Whole-HUD caching skips required work. CPU staging adds a copy but
preserves the existing flush/state boundaries and has a measurable cost.

First build an offline D3D9 screen, with generated geometry/textures only.
The direct path fills the locked GPU buffer for each request. The candidate
fills owned CPU storage, then copies it once at the same batch boundary.
Both generate fresh vertices each frame, issue identical draws and preserve
texture/blend/stencil transitions and the 20,000-vertex capacity boundary.
This models upload strategy; it does not execute native UI callbacks or
establish that the game's requests have the tested distribution.

Test 64/256/1,024 requests per frame and 1/16/64 requests per state batch.
Use 24 vertices per request so the largest cases cross the native capacity.
Require exact nonempty image equality across advancing phases, deliberately
stale geometry and wrong-state negative controls, equal draw counts and
restored device state. Charge CPU generation/copy, uploads, state restoration
and final GPU completion. Alternate A/B order over 21 paired samples and
report medians and tails for native D3D9 and the installed DXVK separately.

The fixture must refuse a running game, WorldBuilder or launcher, use a
hidden owned test window, and never launch the game. No plugin, user setting,
game asset or reference-checkout write is part of this screen. A positive
result still needs native boundary/resource qualification and measured live
request counts before installation; a small or negative result is recorded
without another gameplay test. Do not convert fixture timing into game FPS.

## Result and decision (2026-10-19)

All eighteen final cases pass exact images over three advancing phases,
stale-geometry and wrong-texture rejection, draw/lock/rollover counts and
restoration of every changed render state, FVF, texture and stream binding.
The standalone executable builds with warnings treated as errors. Both
guarded runs exit successfully; no game or launcher was started.

On the installed DXVK, the 1,024-request cases with batches of 16/64 save
0.207/0.191 ms at the paired median. Their p10 savings are negative
(-0.147/-0.343 ms); every DXVK case has a negative p10. Even the
single-request control, which removes no locks, fluctuates substantially.
Native D3D9 saves 0.276/0.299 ms in those large batched cases, but that is a
different backend and does not establish a benefit for Hans's installation.

This is insufficient evidence for runtime integration or another gameplay
test. The measured heavy extra-frame UI scope was 1.428 ms, and only an
unknown part of it belongs to this APT upload path. Live request counts and
batch sizes are also unknown. Do not assign these synthetic savings to that
scope, add them to other experiments, or forecast FPS from them. The
Begin/End/flush/resource-lifetime boundaries would still need native
qualification before any runtime implementation.

[Final report](../benchmarks/2026-10-19-ui-upload.json) and
[378 paired samples](../benchmarks/2026-10-19-ui-upload-samples.csv).
The report rejects nine corrupted evidence controls and records source,
executable and backend hashes, resolving the native i686 DLL through WOW64.
The first successful matrix is retained in ignored
`perf/build/ui-upload-*-initial.log` with its source/executable; final runs
add explicit stencil-function/pass and FVF restoration assertions. Those
earlier samples are not mixed into the final report.

Beta.18, the user's INI and public releases remain unchanged. Further work
toward 120 should target a larger measured cost or the scheduling behavior
of beta.18, rather than install this small, noisy upload screen.

## Reproduce

Build `make build/bench_ui_upload.exe` in `perf/`. Run that executable with
`native`, then with the absolute installed i686 DXVK `d3d9.dll` path, saving
each log separately. `make bench-ui-upload` runs both in sequence. The
fixture refuses an active game, WorldBuilder or launcher, creates only a
hidden owned window, and uses generated geometry/textures.

From the checkout root run `python tools/report_ui_upload.py --native-log
perf/build/ui-upload-native.log --dxvk-log perf/build/ui-upload-dxvk.log
--output docs/benchmarks/2026-10-19-ui-upload.json --samples
docs/benchmarks/2026-10-19-ui-upload-samples.csv`. The report requires all
cases and controls and only writes within this checkout's benchmark folder.
