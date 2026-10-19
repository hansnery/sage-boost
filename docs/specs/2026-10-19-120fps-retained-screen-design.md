# Rendering reuse toward 120 FPS

Hans now explicitly makes 120 FPS the next target and accepts the near-60
result as a milestone. Do not keep requesting another test merely to cross
the older exact 59/s gate. Preserve the installed beta.17, stable releases,
INI preferences, fresh shadows, graphics quality and logic/input/camera
behavior. His standing preapproval covers this offline investigation.

## Choice

1. Screen capture-once/replay-three rendering. Earlier retained-batch tests
   charged capture overhead to one extra. Three extras can amortize that
   overhead; measure the full cycle rather than multiplying an old saving.
2. More scheduler changes cannot make a 40+ ms workload fit a 33 ms cycle.
   The adaptive one/two/three-extra scheduler already exists.
3. Threading the same rendering work or broad material caching has prior
   insufficient/regressing results. Revisit only with a specific changed
   cost model or ownership proof, not as an unmeasured rewrite.

Start with option 1 as a bounded feasibility screen. Reuse the existing
retained GPU fixture, with a separate executable so prior tests stay intact.
Each cycle draws one ordinary frame and one or three extras. The ordinary
frame recaptures all 288 group states, and every extra receives a distinct
fresh palette. Clear and redraw shadow output on every extra. Compare each
extra against the ordinary instanced baseline, test stale poses as a negative
control, and retain the existing reference/count/device-state checks.

Alternate baseline/candidate order with four warmups and 21 measured samples,
eight cycles per sample. Run native D3D9 and DXVK, lit and shadow separately.
Report whole-cycle CPU and fenced completion time, with capture charged once
per cycle, plus per-extra aggregate time. Never sum lit/shadow savings: each
is a full synthetic workload. Keep the process guard active.

The fixture omits native effect callbacks, scene collection, UI and Present.
It borrows synthetic object identities and has only a narrow state mask.
Even a large gain requires lifetime, visibility, material/time invalidation,
engine-state reconciliation and failure-path proofs before runtime use.
No installation or gameplay test follows from this screen alone.

## Results and decision

Both four-case matrices completed after the game closed. Each case has
21 AB/BA samples for one extra and 21 for three, with eight cycles per sample.
All three extra phases match the baseline within the existing tolerance;
the preceding-pose controls fail by more than 100 texels in every phase.
No tolerance was relaxed. Shadow output is cleared and redrawn each time.

Median complete-cycle savings in milliseconds (positive is faster):

| Backend / workload | Setup only, one extra | Setup only, three extras | Full material, one extra | Full material, three extras |
|---|---:|---:|---:|---:|
| Native D3D9 lit | 1.201 | 5.129 | -0.796 | 2.307 |
| Native D3D9 shadow | 1.212 | 4.827 | -0.734 | 2.164 |
| DXVK lit | -0.365 | 2.769 | -2.863 | -2.517 |
| DXVK shadow | 0.201 | 2.453 | -2.234 | -2.106 |

The material extension keeps the local D3DX27 effect Begin/End, exact keys,
full touched-state restoration per replay group, logical mesh accounting,
and Time advancing between cycles. Its baseline includes the prior fixture's
bulk-after-second dispatch. Every ordinary candidate draw recaptures all
288 groups, including key construction and capture costs. Capture misses
retain per-member dispatch; this is deliberately explicit in the report.

The existing material controls also pass: changed materials cannot reuse
old output; changed extra inputs fall back and subsequently warm hits match;
effect End restores state; altered clock keys invalidate; omitted boundary
restoration and poisoned bindings are detected. The timed cycle keeps Time
constant within its extras. Actual per-extra changes could reduce hits further.
The fixture still omits the native FX loop and parameter-source callbacks,
scene construction, UI, Present, arbitrary resources and live ownership.

**Reject runtime promotion.** Three-extra amortization improves the simple
screen, but the stronger comparison loses on the installed DXVK backend.
The native-only gain does not justify an install. No gameplay FPS, 120 FPS
success or end-to-end beta.17 speedup is claimed. Do not add lit/shadow costs.
Do not repeat this route without a specific cheaper correctness-preserving
capture/validation mechanism; additional ordinary cache hits were already
screened separately in the earlier persistent/shared experiments.

Evidence: [setup report](../benchmarks/2026-10-19-retained-multi.json),
[setup samples](../benchmarks/2026-10-19-retained-multi.csv),
[material report](../benchmarks/2026-10-19-material-multi.json),
[material samples](../benchmarks/2026-10-19-material-multi.csv).
The setup run used source `03f1fde`; the material extension's selected source
and binary hashes are in its report. Raw logs remain under `perf/build/`.
Production sources, installed ASI/INI and published releases did not change.
