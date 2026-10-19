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
