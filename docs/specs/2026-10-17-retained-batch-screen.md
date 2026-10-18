# Retained extra-frame batches: offline screen

2026-10-17. Experimental test executables only; no production code, settings,
installation or stable release changes. Full-rate 60 FPS remains unachieved.

## Hypothesis and implementation

The previous dispatch experiment still revisits each material group on an
extra frame. This experiment retains a group's GPU setup from the regular
frame, then draws fresh bone palettes directly in the extra frame. It also
owns the entire replay region, allowing state restoration once at its end
instead of after every instanced chunk. A fresh shadow map is drawn.

The implementation is deliberately confined to a controlled fixture. It
borrows stable synthetic object identities and the existing instancing
shader/declaration caches. All 288 snapshots are recaptured on every
regular frame; that cost is charged to the comparison. No cache hit rate,
game object lifetime, material invariance or safe replacement hook in the
installed executable has been established.

The broad D3DSBT_ALL approach was slow. A second mode captures only the
states used by the fixture, omits bones (uploaded afresh) and keeps state
bound between its draws. See the [reproduction and limits](../../perf/tests/RETAINED_BENCHMARK.md).

## Measurements

Three independent narrow-mask runs, 21 paired samples each, eight workloads
per sample. Positive values are savings in milliseconds per modeled
regular-plus-extra pair, **including the regular capture overhead**:

| Backend / workload | Submission medians, runs 1 / 2 / 3 | Completion medians, runs 1 / 2 / 3 |
|---|---|---|
| Native D3D9, lit | 1.399 / 1.545 / 1.590 | 1.362 / 1.537 / 1.537 |
| Native D3D9, shadow | 1.837 / 1.325 / 1.670 | 1.842 / 1.259 / 1.603 |
| DXVK, lit | 0.509 / 0.664 / 0.542 | 0.231 / 0.370 / 0.269 |
| DXVK, shadow | 0.382 / 0.628 / 0.522 | 0.200 / 0.362 / 0.232 |

The broad-mask control lost 2.99/3.44 ms (native lit/shadow) and 3.70/5.97 ms
(DXVK lit/shadow). Those are paired submission medians from one control run.
Some narrow-mask DXVK samples also regressed; do not present just the
fastest sample or treat the CPU submission gain as GPU throughput.

[All 336 paired samples](../benchmarks/2026-10-17-retained-batches.csv) are
preserved, including the control. Lit and shadow each use the entire
7,200-mesh workload; their savings **cannot be added**. This is not a saved
game replay, a 60-FPS result or a formal bound on in-game savings.

## Correctness evidence

- All final native/DXVK runs pass. Lit output is bit-identical (18,413
  covered pixels). Shadow coverage is equal (13,731 texels); 58 native or
  42 DXVK depths differ, maximum 2 or 1 units of 2^-24 respectively, below
  the existing depth tolerance. The leader now uses instanced arithmetic.
- The stale-pose positive control changes 22,275 lit pixels and 16,712
  shadow texels. This proves the comparison exercises refreshed palettes.
- Method reference counts, all 7,200 meshes/576 retained draws, restored
  shader/stream/declaration state and restored VS/PS float constants pass.
  Cleared/missing snapshots and out-of-range slots are rejected.
- Initial exploratory full-state creation produced an empty retained image;
  explicit Capture after creation corrected the fixture. Those failed
  exploratory runs are not included in the measurements. This observation
  is not a diagnosis of a driver bug.
- Full `perf/` suite passes. Extracted shared dispatch initialization was
  checked by rerunning its lit/shadow benchmark and the retained matrix.

## Decision and next proof obligations

Do not promote this prototype into a beta. On the installed DXVK backend,
the measured completion gain is only about 0.2-0.4 ms. This does not justify
the required object/material lifetime and state-replay risk, nor another
gameplay test under the user's significant-improvement requirement.

Retaining the **whole** render queue could additionally avoid FX binding
and scene reconstruction; neither saving is included or demonstrated here.
Before pursuing that route, prove which work can be omitted, then include
its real callbacks and invalidation cost in an offline harness. A complete
regular-frame visibility/queue capture could observe workload without
forcing an unbudgeted extra draw, but is not implemented by this change.

Required runtime proofs include: references and deletion/reuse; material,
texture, buffer and declaration changes; shader time/animation parameters;
visibility/shroud and pass order; engine-side state-cache reconciliation;
device loss/reset; complete fallback before replaying a partial queue;
exact signature/ABI checks; and unchanged logic/CRC/deadline/camera behavior.
The adjacent single-method merge counters do not settle opportunities for
non-adjacent groups or multi-method stacks.

Microsoft documents state blocks as capturing selected device state and
Apply restoring it: [CreateStateBlock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createstateblock),
[custom state blocks](https://learn.microsoft.com/en-us/windows/win32/direct3d9/state-blocks-save-and-restore-state).
These APIs do not establish the missing engine-level proof above.
