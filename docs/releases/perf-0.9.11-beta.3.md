# bfme2perf 0.9.11-beta.3

Development build, tested offline but **not installed or published**.
The user wants another gameplay test only after a significant performance
improvement; this correction does not meet that condition.

With ExtraFrameShadows=1 and ShadowInterval=1, the regular frame following
an extra frame now redraws its own shadow map. Previously it reused the
extra frame's map regardless of the requested interval. That could yield
60 unit frames with only about 30 shadow-map redraws per second in steady
one-extra-per-game-frame operation. Full-rate shadows require both draws.
Larger shadow intervals keep the existing reuse behavior.

This fixes cadence correctness, not the heavy-scene 30-FPS limitation.
It can cost more time when extra frames are active. The captured steady
test had no extra frames, so this defect does not explain its 30-FPS result.
Timing safeguards, graphics quality and camera behavior are unchanged.

Validation:

- The new hook regression failed before the fix, then passed: three extra
  frames and four regular frames produce seven shadow redraws at interval 1,
  with each regular frame's own matrix and matching counters.
- Existing interval-2 map/matrix reuse checks still pass.
- Full plugin test suite passes, including local AotR/game820 signatures,
  hook stack/register behavior, load/version, scheduling, instancing,
  shadow-pass verification, catch-up and lights.
- No game launched. Installed beta.2 binary and INI hashes remain unchanged.

The new effect-runtime and ambient-light tests are developer-only screens,
outside the ASI. Their measured gains do not justify a new gameplay test;
see [results](../specs/2026-10-17-render-setup-screening-results.md).
