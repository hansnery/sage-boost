# Performance DLL, plan 8: instancing unit meshes (Milestone 6)

**Goal:** 0.7.0 draws the meshes of each supported FX group after its first
one with our own instanced vertex shader, behind `[Render] Instancing`
(default 0), with a verify mode that compares both paths on the GPU and
switches off categories that differ. Spec:
`docs/specs/2026-10-13-perf-instancing-design.md`.

**Tech stack:** C (freestanding, 32-bit MinGW-w64, kernel32 only), HLSL
compiled at dev time with `d3dcompiler_47.dll`, Python for the generator,
GNU make from Git Bash.

## Global constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written.
- No code from SAGE-Unlocked or bfme2-accelerator; no EA shader bytecode or
  file is copied, stored or shipped (only hashes of it).
- Every hook found by signature, unique in `game.dat` and `game820.dat`.
- Nothing in `dll/` changes; the game folder's `dinput8.*` are untouched.
- Freestanding build with `-Werror` and `-mgeneral-regs-only`; D3D9 through
  the game's device vtable only, no d3d9 import.
- Conventional commits on `feat/perf-instancing`, pushed as we go, no
  force-push. The game is never launched by the agent.

## Tasks

- [x] 1. Spec and plan (this file). Commit `docs: design instancing for unit meshes`.
- [x] 2. `perf/shaders/inst.hlsl` and `tools/inst_shaders.py`: generate
  `perf/src/inst_shaders.h` from the local `defaultw3d.fxo` (lit with and
  without shadows, shadow map; rigid, 1 and 2 joints; six mappings).
  Commit `feat(perf): instanced vertex shaders for the unit FX variants`.
- [x] 3. `perf/tests/inst_gpu`: off-line GPU comparison of every generated
  variant against its EA original (Windows d3d9 and DXVK). Drop variants
  that fail from the table. Commit `test(perf): GPU check of the instanced shaders`.
- [x] 4. Sites `inst_mesh` and `inst_dip`, the indirect-call patch kind,
  `PF_MAX_PATTERN` 256, stub pairs 16 and 17, site tests. Commit
  `feat(perf): the FX mesh and draw call sites`.
- [x] 5. `perf/src/d3d9vt.h` (vtable slots), `perf/src/inst.c/.h`: the
  group state machine, capture, instanced draw, restore, counts; tests with
  a fake batch function and a fake device. Commit
  `feat(perf): draw FX groups with instancing`.
- [x] 6. Verify mode: off-screen comparison, categories switched off,
  log lines. Commit `feat(perf): verify instancing against the game's draw`.
- [x] 7. CSV columns and per-minute log summary. Commit
  `feat(perf): instancing columns`.
- [x] 8. Config keys `Instancing`, `InstancingVerify`; 0.7.0; INI; install
  (backup to `%TEMP%`); push. Commit `feat(perf): instancing build 0.7.0`.
