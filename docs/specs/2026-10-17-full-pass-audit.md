# Full-pass CPU audit for the 60-FPS beta

Read-only implementation audit, 2026-10-17. No rendering optimization is
implemented by this document. Shadows remain full rate. The reference repo
was read only; no EA code or assets are incorporated into the plugin.

## Evidence and limits

The preserved beta.1 timing CSV is in
`%TEMP%/bfme2perf-beta1-test-20261017-171637/`. Frame-weighted values for
seconds 55 through 114 (the same interval as the approved design) are:

| Counter | Per game frame |
|---|---:|
| FX groups | 680.22 |
| Adjacent same-method/same-light opportunities (`fx_merge`) | 0 |
| Adjacent same-method opportunities ignoring light (`fx_merge_anylight`) | 3.40 |
| FX batch inclusive time | 6.866 ms |
| FX mesh inclusive time | 3.481 ms |
| Shadow renderOneObject calls | 501.33 |
| Shadow renderOneObject inclusive time | 2.699 ms |
| Shadow light Pre_Render_Update time | 0.242 ms |
| Shadow Set_Light_Environment time | 0.125 ms |
| Shadow same-model/method split mesh count | 1,796.98 |

The difference between batch and mesh time, about 3.384 ms, includes loop,
marker and group setup work. It is not a measurement of effect Begin/End
alone. The scopes overlap; none of these numbers can be added indiscriminately.
These are game-frame measurements. Extra-pass measurements are required
before attributing the extra-frame budget to the same costs.

## Material group merging: no current promotion case

`perf/src/timing.c:fx_group_begin` counts adjacent groups with the same
single rendering method, inferring the previous group's single-pass status
from its mesh markers. The light-equal count is zero in this interval; even
the light-agnostic count is only about 0.50% of groups. This is much less
than the earlier 25% opportunity gate in the main performance design.

`perf/src/inst.c:begin_group` increments `shadow_split` by the following
group's record count when model and method match. This is a count of meshes,
not candidate boundary count or proof of interchangeable state. It does
not establish equal opacity, inherited data, geometry state or effect
parameters. It must not be used as evidence for merging those groups.

`perf/src/shadowpass.c:pf_sp_same_batch` already relaxes the game's group
comparison for light environment and fog in the proved shadow case, keeping
the other original comparisons. `pf_sp_group` and `pf_sp_light_env` preserve
the last original light environment for the later light unlock. Broadening
the comparison beyond this requires a separate proof of every differing
material input, rather than bypassing the comparator wholesale.

Local reference evidence:

- `C:/repositories/Open-BFME-2/Code/Libraries/Source/WWVegas/WW3D2/FXShaderRendererFlushRenderingTasks.cpp`
  describes the sorted task path; no new sorting lever is established.
- `C:/repositories/Open-BFME-2/reverse/attempts/0x0017438b.cpp` is explicitly a
  **banked partial**, not an adopted ABI contract. It shows group setup,
  method Begin, per-pass loops, method End and method-chain cleanup. Treat
  it as a guide for disassembly, not executable specification.
- The already verified AotR path is documented in
  `2026-10-13-perf-dll-design.md`, Milestone 4, and
  `2026-10-13-perf-instancing-design.md`.

Leaving an effect open across group boundaries would omit render-state
restore/reapply operations. Equal effect pointers alone cannot establish
equivalence after per-mesh mutations. A future merge would need complete
state accounting, chain/reference lifetime handling and flush-on-every-exit
behavior. There is no measured benefit here sufficient to justify that risk.

## renderOneObject: do not cache or skip the complete call

`C:/repositories/Open-BFME-2/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DScene.cpp`
contains a `renderOneObject` body labelled present-unmatched. In that
reference path, shroud queries can remove the object from the scene; clear
shroud handling writes a drawable frame field. The routine also handles
ghosts, hidden drawables, custom material passes, tint/selection colours,
infantry lights and balanced material/override stacks. These observations
are warnings to verify against the actual target, not permission to replace
the routine based on the reference source.

Caching its previous draw or calling Render directly would bypass those
effects and potentially keep stale object pointers. It is not a safe
render-only shortcut. `perf/src/lights.c:lists_result` already implements
a narrower cache with exact input environment, sphere, light versions,
threshold and FP-context matching. The selected trace has no kept scene or
dynamic lights in its representative heavy rows. Expanding that cache alone
has no demonstrated route to the missing approximately 8 ms.

## Actionable next candidate: reduce dispatch inside an existing group

Investigate a **group-local instancing fast path** before cross-group
material merging. Keep the game's original material group boundaries,
Begin/BeginPass/EndPass/End and ordering. After the game's leader has drawn
and `capture`/`evaluate` have proved the supported shader, declaration,
geometry and stream state, stage subsequent compatible records directly
through the existing `stage`/`draw_batch` machinery in original order.

Currently `member` skips the mesh render callee, but the game still walks
each record, constructs its mesh event label, enters/exits the event, and
passes through the hook for each member. The partial reference demonstrates
that surrounding loop and event construction. Removing repeated dispatch
for a verified run is a concrete CPU candidate; its savings are **not yet
measured** and may be modest. It must not disable the user's detailed timing
or validation settings as a hidden shortcut.

Before implementation:

1. Verify the actual AotR and game820 loop bodies and every branch into/out
   of the proposed run, then describe those sites in the signature generator.
   Do not install addresses from the reference attempt.
2. Build a native loop harness comparing current per-member processing with
   group-local staging. Include the marker/dispatch cost separately so the
   proposed benefit can be rejected before adding a runtime hook.
3. Account for every method AddRef/Release and scope lifetime. `member`
   currently decrements the skipped callee's method reference; the enclosing
   loop's copies and cleanup remain a separate obligation.
4. Fall back before a differing model/base vertex/method, unsupported layout,
   failing bone stage, verification sample, device transition or multipass
   case. Flush before another model rebinds buffers, as `member` does now.
5. Preserve diagnostic meaning. If equivalent per-mesh accounting cannot
   be retained with Detail=1, reject this route under the current settings
   rather than silently changing the measurement configuration.
6. Require output comparisons and full device-state restoration on native
   D3D9 and DXVK, plus early exits, partial stage failures, reference balance
   and an injected draw failure in the native harness. Keep verification
   samples on the existing stock path until equivalent coverage is proved.

An alternative is a very narrow cache of group setup values with complete
value/version keys and invalidation at all writers. That still must execute
the effect lifecycle unless its state proof is complete. The present trace
does not isolate setup costs well enough to prioritize it over the loop
candidate.

## Decision

Ship the pass-level measurements first, then use the user's same save to
choose the costly pass. Do not promote same-pointer material merging or a
renderOneObject bypass. Prototype and benchmark the existing-group dispatch
candidate independently; stop if its savings are too small. Missing
normalmapped shadow shaders remain a narrow coverage improvement, not the
primary performance hypothesis for this save. No current evidence promises
60 FPS, and native harness gains do not establish in-game frame pacing.
