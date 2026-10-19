# Palette Texture Screen Implementation Plan

**Goal:** Measure whether full-precision palette textures remove enough
large-skeleton batch overhead to justify deeper 120 FPS work.

**Architecture:** Generate only our own vs_3_0 shader bytecode from the
existing HLSL. A standalone GPU fixture compares direct constant batches
with a fresh texture upload and larger batches under equal surrounding work.

**Tech Stack:** WinLibs i686 C, D3D9/DXVK, D3DCompiler47, Python.
Execute inline under Hans's standing preapproval; no delegation is needed.

## Constraints

No production/install/reference changes, EA bytecode output, game launch,
graphics reduction or stale shadows. Preserve pixel/depth tolerances.

## Task 1: generated shader and device support

- [x] Add `tools/palette_texture_shaders.py`; import the existing shader
  classifier/compiler, identify local variants 5 and 476, and compile only
  our HLSL with `IR[index]` replaced by a texture helper. Write the generated
  header to ignored `perf/build/`, with EA identity hashes and our bytecode.
- [x] Add `perf/tests/bench_palette_texture.c` and Makefile dependencies.
  Require vs_3_0, A32B32G32R32F vertex/dynamic texture support, successful
  dynamic allocation, and successful vertex sampling/draw setup.

## Task 2: correctness and timed comparison

- [x] Stage 288 rows of 25 complete palettes, applying identical pose
  production on both paths. Constants draw chunks of `ir_count/(2*BONES)`;
  texture mode uploads once and draws all 25 units in each group.
- [x] Validate at least three moving phases in lit and fresh-shadow passes,
  reject stale uploaded palettes, restore device/vertex sampler state and
  account for every mesh/draw. Fail immediately on any device error.
- [x] Run six- and twenty-bone native/DXVK cases serially with the guard.
  Use four warmups and 21 AB/BA samples of eight fenced frames each. Charge
  CPU palette staging/upload and final completion; record all samples.
- [x] Preserve any unsupported/failing cases and the exact source/binary/log
  hashes. Interpret only after controls pass; no installation from this test.
- [x] Update STATUS and the design with the decision; commit/push explicit
  source/report paths. Continue toward native integration only for a useful
  measured gain against the direct constant baseline.

Final decision: no runtime promotion. All eight correctness cases pass, but
DXVK regresses by about 0.79-1.26 ms/frame despite fewer calls. The native
gains do not qualify a backend switch. The first DXVK sampler-restoration
failure is preserved and excluded; explicit owned-state restoration resolves
it without weakening checks. Installed beta.17 and INI remain untouched.
