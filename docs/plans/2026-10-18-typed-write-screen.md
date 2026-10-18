# Typed effect-write screen

**Goal:** Decide whether the generic numeric-write saving survives the setter
families used by the native material sources, before building a runtime hook.

**Architecture:** A separate developer executable reuses the effect fixture's
local asset reader, D3D9 device and timing helpers. It uses SetInt, SetFloat,
SetVector and SetMatrixTranspose for exactly matching scalar/vector/matrix
descriptors, forwarding all other numeric shapes. It retains changing raw
bone palettes. No native source computation or installed code changes.

**Constraints:** Native D3D9 and DXVK runs are serial and guarded against the
game. No asset payloads leave memory. Preserve installed beta.11 and its INI.
Do not treat these timings as gameplay FPS or a full native-path result.

- [x] Add `PF_EFFECT_MAIN` to bench_effect.c so the new executable can reuse
  its existing helpers without changing default behavior. Build the new
  `build/bench_typed_effect.exe` with the existing TEST_CFLAGS and -O2.
- [x] Add `typed_write(ID3DXEffect *, typed_input *, int filter, int recording)`.
  Read with the matching typed getter and compare exact bytes. Forward on
  getter failure, descriptor mismatch, raw palettes, unsupported shapes and
  parameter-block recording. Qualification happens before timed loops and
  does not establish a production descriptor-cache lifetime policy.
- [x] Compare output constants/shaders for full writes versus filtered
  writes over three changing poses, external parameter changes and poisoned
  GPU constants. Require an intentionally unconditional skip to differ.
- [x] Check parameter-block recording/application and typed conversion:
  identical current values still need recording; a GetInt conversion of a
  fractional float must not authorize omitting SetInt. Compare invalid-handle
  setter HRESULTs with the forwarded path. Keep negative controls explicit.
- [x] Measure 21 paired samples after four warmups, full versus filtered
  typed writes through complete effect lifecycles, 850 groups per workload,
  changing twenty-bone palettes, alternating execution order. Record both
  submission and completion. Run native D3D9 then DXVK; repeat only if needed
  to resolve an ambiguous result.
- [x] Preserve hashes, counts, controls, sample data and limitations. Update
  STATUS/BACKLOG and commit/push explicit paths. Do not install this fixture.

Result: reject standalone promotion. Final lit savings are 0.60-0.68 ms per
modeled workload, with shadow deltas straddling zero. Thirty nonzero typed
round trips per backend, including asymmetric matrices, also pass. Native
source computations, actual recording-state tracking and full GPU/lifetime
qualification remain outside this screen. No installed change or FPS claim.
