# tools

Python 3 helpers. None of them ships. Run them from the repository root
(`python tools/<name>.py ...`); each script's docstring has its full usage.
They only read game and mod files, except `bse_patch_carndum.py`, which
rewrites the file it is given. No EA data is stored here: scripts read the
user's own installs at run time.

## Build and verification

| script | what |
|---|---|
| `verify_sites.py` | unlocker: checks every signature against each game's baseline image and regenerates `dll/src/patch_table.c` and `dll/tests/baseline_expect.h` (`--write`). Run by `make test` in `dll/`. Needs `pefile` and `capstone`. |
| `inst_shaders.py` | plugin: generates `perf/src/inst_shaders.h` from `perf/shaders/inst.hlsl` and the local `Shaders.big` (stores only hashes of the game's shaders). |

## Executable research

| script | what |
|---|---|
| `disasm.py` | disassembly, rel32 callers, abs32 references and string lookups in any supported image |
| `bench_native_sort.py` | verifies 38 native render-task sort/ownership bodies, compares exact order/reference/ABI behavior with temporary raw copies, and screens CPU cost while preserving target code spacing. `make test-native-sort` checks both images; `make bench-native-sort` records paired timings. Normal control flow only, no installed hook or FPS claim. See `docs/specs/2026-10-18-native-sort-screen.md`. |
| `bench_native_queue_gpu.py` | joins native owned-record collection, sorting, group/mesh/FX rendering and destruction around the same queued records and real GPU. `make test-native-queue-gpu` checks native D3D9/DXVK lit/fresh shadows and exact-record failure controls; override `AOTR_DAT` for the other image. `make bench-native-queue-gpu` supports `QUEUE_MODE=combined/sort/render` and `QUEUE_ORDER=ascending/descending/random/interleaved`. Collection is modeled with native copy/destruction; no actual vector growth, game startup, installed hook or FPS claim. See `docs/specs/2026-10-18-native-queue-gpu-integration.md`. |
| `xrefs.py` | callers/references of an address, named by the function annotations of the Open-BFME-2 source tree (read only) |
| `port_sites.py` | turns a 1.06 function's first bytes into a wildcard signature that matches another image once; tests in `test_port_sites.py` |
| `bench_native_decode.py` | verifies three complete native animation decoders and screens exact reuse with immutable synthetic streams, warm cursor states, aliasing and broken-cache controls. `make test-native-decode` checks both local images; `make bench-native-decode` records CPU timings. No game startup, production hook or FPS claim. See `docs/specs/2026-10-18-native-decode-screen.md`. |
| `bench_ambient.py` | verifies the full ambient-light sampler against the read-only reference and runs the isolated native cost screen on synthetic data; requires `perf/build/bench_ambient.exe`. No game entry point or scene runs. See `perf/tests/RENDER_SETUP_BENCHMARK.md`. |
| `test_fx_lifecycle.py` | verifies eight complete native FX wrapper bodies, then runs controlled ABI/call-order tests without a game entry point, scene or GPU. `make test-fx-lifecycle` builds the harness and checks both local images; `test_fx_contract.py` tests altered-code refusals with synthetic bytes. See `docs/specs/2026-10-17-native-fx-lifecycle-audit.md`. |
| `test_event_contract.py` | verifies four native diagnostic-event bodies, CRT/data/callback contracts and isolated callback/buffer/ABI behavior. `make test-native-events` checks both local images; no game startup. |
| `test_group_contract.py` | verifies nine native group/SEH-prologue/name/chain/event bodies and runs original-versus-bulk loop, ownership, callback and partial-consumption tests. `make test-native-group` also rejects deliberately broken bridges. Normal control flow only; no native exception-unwind or GPU claim. See `docs/specs/2026-10-17-native-group-contract.md`. |
| `test_mesh_draw_contract.py` | verifies seven native mesh/draw/cleanup bodies, their stack/data/call contracts and final-draw frame identity. `make test-native-mesh` exercises nested callbacks, recursive mesh calls, entry stubs and return wrapping on both local images, plus refusal/broken-gate controls. No game startup, real GPU or native exception-unwind claim. See `docs/specs/2026-10-18-native-mesh-draw-contract.md`. |
| `bench_native_leader.py` | verifies the same local mesh bodies and runs the real GPU/effect fixture through native mesh/draw/cleanup and production timing hooks. `make bench-native-leader` checks native D3D9/DXVK, lit/fresh shadows, callback and verification gates, model splits and earlier DIPs. Group loop and source/state helpers remain controlled; no game startup or installed hook. See `docs/specs/2026-10-18-native-mesh-gpu-screen.md`. |
| `bench_native_group_gpu.py` | joins sixteen verified native group/event/method-chain and mesh/draw/cleanup bodies with actual timing hooks and the local effect/GPU fixture. `make bench-native-group-gpu` compares callback observations, partial groups, ownership, Begin failure and output, then measures serial native/DXVK pairs. Source/state helpers and GPU failure recovery remain open. See `docs/specs/2026-10-18-native-group-gpu-screen.md`. |
| `test_native_gpu_faults.py` | audits one-shot API rejection through a separate device proxy in the joined native/GPU fixture. `make test-native-gpu-faults` records missing output and immediate-state leaks across four backend/workload cases and rejects a broken count oracle. A successful audit detects known defects; it does not mean recovery is fixed. No game startup or installed hook. See `docs/specs/2026-10-18-native-gpu-fault-audit.md`. |
| `make_inst_recovery_source.py` | renames two definitions in our own `inst.c` into an ignored build-only copy, with unique-anchor checks and source/output hashes. The offline recovery engine substitutes checked submission and staged bone replay. `make test-native-gpu-recovery` uses the fault launcher with `--recovery` to require exact counts/state and matching output, including omitted/duplicate replay controls. `make bench-native-gpu-recovery` measures the guarded candidate. No production source or installed hook changes; see `docs/specs/2026-10-18-native-gpu-recovery.md`. |
| `native_fx_state_contract.py`, `bench_native_fx_gpu.py` | verify and join native FX lifecycle/source selection and dirty-state application to the group/mesh GPU fixture. `make test-native-fx-state` checks both local images and altered-body controls; `make test-native-fx-recovery` checks 248 failures with device/effect/native-field snapshots and broken replay/offset controls; `make bench-native-fx-gpu` measures the final candidate serially. Sources/lifetimes remain controlled. See `docs/specs/2026-10-18-native-fx-gpu-integration.md`. |

## Maps, castles and AI bases (`.map`, `.bse`)

A `.bse` (castle or AI base under the game's `bases\`) is a WorldBuilder map:
`EAR\0`, the decoded size, then a RefPack stream of `CkMp` chunks.

| script | what |
|---|---|
| `mapinfo.py` | the reader the others import (`Reader`, `load_map`, `decode`, `refpack`, `BigArchive`); its CLI lists a map's players, teams and scripts (`--scripts`), also for every map in a `.big`. Tests: `test_mapinfo.py`. |
| `refpack_encode.py` | RefPack compressor and `ear_encode()`, the inverse of `mapinfo.decode`, round-trip checked. Tests: `test_refpack_encode.py`. |
| `bse_list.py` | placed objects of a `.bse`/`.map`: template, position, angle, owner, uniqueID |
| `bse_objects.py` | every template name a file mentions, with counts (heuristic; takes base folders too) |
| `bse_farmdiff.py` | AI bases that place farms, against the parent mod's copy of the same base |
| `bse_fielddiff.py` | field-by-field diff of `ObjectsList` and `CastleTemplates` between two castles |
| `bse_verify.py` | checks a rewritten file against its original: RefPack stream, header, chunk list, changed chunks |
| `bse_patch_carndum.py` | the one-off rewrite of Realms' `fortress_carndum.bse` pads (2026-10-14); an example of editing objects and `CastleTemplates` in place. Refuses to write unless the target equals the backup given. |

`CastleTemplates` as read from Realms' and RotWK's castles: u32 key, u32
count, then per entry two u16-length strings (an empty name, the template),
offset x, y, z and angle (floats), priority and phase (u32), then 4 zero
bytes. In every castle checked, the entries are exactly the objects that
carry an `objectBaseName` property (WorldBuilder's "In Base" box, as far as
observed); an object without it is placed on the map but not in the castle.

## Archives and mod data

| script | what |
|---|---|
| `big_find.py` | files whose path contains a substring, in one or more `.big` archives |
| `asset_dat.py` | reads an `asset.dat` (`ALAE`) index: counts, and the entries matching a name |
| `radar_audit.py` | parses a mod's `ini\` (includes, defines, inheritance, module replacement) and records, for every resource building, its resolved `RadarPriority`, `KindOf`, `VisionRange` and filter membership, and which faction reaches it; writes a JSON file |
| `radar_report.py` | reports from that JSON: buildings not on the minimap, duplicates, per-faction lists, paths |

`radar_audit.py`'s faction list (`CHECK`) and `radar_report.py`'s Side lists
are Realms in Exile's split factions; edit them for another mod.
