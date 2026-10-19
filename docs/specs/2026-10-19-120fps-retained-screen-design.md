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
