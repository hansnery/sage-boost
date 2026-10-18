# Effect runtime screening implementation plan

**Goal:** Find a larger render-setup saving without another diagnostic game test.

**Architecture:** Compare the installed legacy D3DX effect runtime with the
later installed runtime using locally read compiled effects and the existing
state-recorder fixture. Measure actual Begin/BeginPass/Commit/End calls at
the captured group's scale. Separate state-saving overhead from runtime
choice; neither disabling restoration nor changing an import is authorized
for deployment on benchmark evidence alone.

**Tech stack:** i686 C, D3D9/DXVK, installed D3DX9 27/43, QPC.

## Constraints

- No game launch, reference edits, EA asset copies or installed changes.
- Shadows stay full rate; gameplay, camera, FP control and deadlines stay stock.
- User tests only after evidence of a significant improvement. Retain the
  ~3 ms screening threshold; distinguish synthetic work from saved-game FPS.
- Existing beta branch and standing commit/push authorization apply.

## Steps

- [x] Inspect the current capture and scheduler. The reference floor remains
  too expensive; aging away the estimate alone would create an unbounded
  cost guess. Do not loosen this gate as a substitute for measured savings.
- [x] Confirm installed executable imports and the reference material call
  path. Installed AotR imports `d3dx9_27.dll`; Begin uses flags 6.
- [x] Build an offline benchmark for the two effect runtimes and flags 6/7.
  Flags 7 is a cost-isolation control only, never an installable shortcut.
- [x] Compare effect parameter metadata and emitted device commands across
  both runtimes on supported techniques before interpreting runtime timings.
- [x] Measure paired warmed samples on native D3D9 and DXVK; validate calls,
  include alternating material parameters, and report limits openly.
- [x] Promote only if the evidence warrants an implementation with full
  restoration/compatibility proof. Otherwise preserve the screen and move
  to another source-backed candidate without asking for a gameplay test.
  Runtime/state-save savings are too small. Three particle command streams
  also differ. No effect-runtime hook is implemented.
- [x] Follow up on ambient sampling: independently locate and validate the
  local sampler, call only that isolated function with synthetic maps,
  and measure it on AotR/game820. About 0.05 ms per 1,290 calls is too small.
- [x] Correct the discovered full-rate shadow defect: interval 1 must not
  reuse an extra frame's map on the following regular frame. Reproduce
  before fixing, then run the full suite. Beta.3 is built but not installed;
  this is a cadence correction, not a significant performance improvement.
- [x] Document, commit and push completed work; keep installed beta unchanged
  unless a tested substantial improvement is established.
