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
| `native_contract_table.py`, `queueinsert_table.py`, `queuesort_table.py` | plugin: helpers for `verify_sites.py --perf-queue/--perf-sort [--write]`; verify both configured images and generate unique signatures, full normalized hashes and symbolic dependency metadata. Do not touch unlocker tables. |
| `test_queueinsert.py` | checks the runtime queue finder and actual installed entry in private mapped images. `make test-queueinsert` covers altered bodies/dependencies, ambiguous signatures and bounds, then native ownership/ABI/growth comparisons. The game is never launched. |
| `test_queuesort.py` | `make test-queuesort` checks native-order C sorting through private-image entry hooks, both insertion/sort installation orders, shared holders, unknown-comparator callback observations, preflight guards and executable refusals. Forced-heap cases call the same production algorithm directly. No game startup. |
| `bulkcontract_table.py`, `test_bulkcontract.py` | `verify_sites.py --perf-bulk [--write]` joins the native group/mesh/FX/event graph into runtime fingerprints. `make test-bulkcontract` tests the actual C finder against relocated private images, region/addend/import refusals and coexistence with queue hooks. The bulk finder is not yet linked into the ASI. No game startup or new FPS evidence. |

The C-only `make test-inst-ownership` target checks captured stream reference
ownership with deterministic COM lifetimes and is included in `make test`.
`make test-inst-ownership-gpu` checks real buffer destruction and restoration
on native D3D9 and DXVK using the local shaders. See
`docs/specs/2026-10-18-instancing-stream-ownership.md`; neither starts the game.

`make test-instsubmit` links the production checked submission object and
checks persistent GPU rejection and partial ordinary replay results. The
native/GPU recovery targets also link this same object. Runtime activation
still needs the group bridge and failure policy; see
`docs/specs/2026-10-18-checked-submission-core.md`.

Since beta.9, the checked native/GPU adapter delegates leader eligibility,
bulk advancement and cancellation to production `inst.c`. Normal combined
queue tests use the unmodified source; only the stage-refusal fixture
renames the stage entry for controlled rejection. Link-time wrappers observe
submission calls without substituting their algorithm. Runtime hook
installation remains pending; see `docs/specs/2026-10-18-runtime-bulk-instancer.md`.

Beta.10 connects these operations to production `bulk.c` (native group
entry/loop bridge and unwind cleanup) and production aggregate accounting.
`make test-runtime-bulk-gpu` runs the combined insertion/sort/native/GPU
checks through that wrapper; `make test-runtime-group-fallback` adds stage
refusal and injected API errors. `make test-bulk-install` verifies actual
private-image patch installation after the prerequisite hooks, and
`make test-bulk-lifetime` exercises return, real Windows unwind and failed
transaction cleanup. All are offline; none launches the game. See
`docs/specs/2026-10-18-runtime-group-hook.md`.

## Gameplay capture checks

For a local development beta, `launch_perf_beta.ps1` provides a user-invoked
launch path that checks the expected ASI and INI SHA-256 values before opening
the selected mod. Pass `-GameDir`, `-ModDir`, `-ExpectedPerfSha256` and
`-ExpectedIniSha256`; add `-CheckOnly` to validate without launching anything.
The game and launchers must be closed. The helper starts the existing
`lotrbfme2ep1.exe -mod ...` directly and never installs files or changes
settings, language or launcher state. Use it after installation checks have
passed: the regular launcher's repair step can replace a development ASI
with its published version and recreate the INI from its release template.
An agent must only execute CheckOnly; the user performs the actual game run.

After the user closes the game, copy its log, INI and CSVs to a new capture
folder before reading. `perf_capture.py` reads a copied timing CSV and reports
one explicitly selected interval; it never launches or modifies the game:

```powershell
python tools/perf_capture.py C:/path/to/capture/bfme2perf_timing.csv --start-s 100 --seconds 60
python tools/test_perf_capture.py
```

For beta.13 captures, the report also sums raw consumed followers by method
count, separately for regular and extra frames. Per-frame values divide the
pooled counts by the corresponding total frame count. Absent legacy columns
or blank instrumentation are reported as unavailable, not as zero coverage.
These counts exclude leaders and do not establish successful draws, unique
units, a fraction of all eligible records, or a saved-game speedup.

Choose the same settled troop-heavy scene, not the fastest window. The row at
`start + seconds` must exist; the final shutdown row can be partial. Missing
seconds, missing counters and inconsistent histograms are refused. The report
uses completed flip-enabled EndScene calls (not `frames + extra_frames`) and
pools the exclusive histogram bins to give median/p95 bounds. Overflow means
greater than one second, never zero. CSV timestamps quantize the interval;
exact display refresh/GPU completion is outside this instrumentation.

The report includes extra-shadow calls, completed shadow samples, invalid
profiles and verification errors. No numeric field declares the overall goal
complete: confirm the loaded version/settings, runtime log, same scene and
visual behavior separately. Sixteen tests cover bursty 60 FPS,
short/incomplete intervals, counter corruption and absent/mismatched shadows.

## Executable research

| script | what |
|---|---|
| `disasm.py` | disassembly, rel32 callers, abs32 references and string lookups in any supported image |
| `bench_native_matrix_sources.py` | verifies nine native matrix callback/provider/constructor bodies and D3DX27 import roles, then checks outputs and screens computation using a recording effect sink. Build `perf/build/bench_native_matrix_sources.exe`, then pass `<game.dat> <harness.exe> <d3dx9_27.dll> [--output report.json]`. Both images and changed-provider/contract controls are supported. No game startup, real effect storage, GPU work, runtime cache or FPS claim. |
| `bench_native_sort.py` | verifies 38 native render-task sort/ownership bodies, compares exact order/reference/ABI behavior with the production integer-only C sorter, and screens CPU cost while preserving target code spacing. `make test-native-sort` checks both images; `make bench-native-sort` records paired timings. Private reference code has fail-stop exception handlers. See `docs/specs/2026-10-18-runtime-queue-sort.md` for runtime integration and limits; no gameplay FPS claim. |
| `bench_native_queue_gpu.py` | joins native owned-record collection, sorting, group/mesh/FX rendering and destruction around the same queued records and real GPU. `make test-native-queue-gpu` checks native D3D9/DXVK lit/fresh shadows and exact-record failure controls; override `AOTR_DAT` for the other image. `make bench-native-queue-gpu` supports `QUEUE_MODE=combined/sort/render` and `QUEUE_ORDER=ascending/descending/random/interleaved`. Collection is modeled with native copy/destruction; no actual vector growth, game startup, installed hook or FPS claim. See `docs/specs/2026-10-18-native-queue-gpu-integration.md`. |
| `bench_native_insert.py` | verifies 33 native insertion/growth/erase bodies and compares a spare-capacity direct append with native ownership, queue selection and growth fallback. `make test-native-insert` checks both images; `make bench-native-insert` records CPU timings. Heap callbacks are controlled; exceptions fail-stop. |
| `bench_queue_cleanup.py` | screens full native queue erase against an independently written release loop using the insertion loader. Checks records, references and exact observed callback sequence/state, with a broken-release control; `make bench-queue-cleanup` records paired CPU timings. Normal return only; the measured saving was too small for runtime promotion. See `docs/specs/2026-10-18-workload-screens.md`. |
| `bench_native_insert_gpu.py` | adds actual native task insertion and vector erase to the joined sort/group/mesh/FX GPU workload. `make test-native-insert-gpu` checks native D3D9/DXVK lit/fresh shadows and three exact-record negative controls; override `AOTR_DAT` for the second image. `make bench-native-insert-gpu` supports `INSERT_MODE=combined/sort/render/insert` and `INSERT_ORDER=ascending/descending/random/interleaved`. No installed hook or FPS claim. See `docs/specs/2026-10-18-native-insertion-gpu-screen.md`. |
| `test_native_group_fallback.py` | tests ordinary replay after zero/partial bulk consumption through native group/mesh/FX and real GPU. `make test-native-group-fallback` covers native D3D9/DXVK lit/fresh shadows, partial staging writes, following groups and seven negative controls; override `AOTR_DAT` for the second image. Ordinary-replay GPU errors and runtime integration remain open. See `docs/specs/2026-10-18-native-group-fallback.md`. |
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
| `make_inst_recovery_source.py` | beta.9: renames only the stage entry in our own `inst.c` into an ignored copy, with unique-anchor checks and hashes, for controlled stage refusal/partial-write injection. Normal GPU tests compile unmodified `inst.c`; all checked paths call production `instsubmit.o`. `make test-native-gpu-recovery` requires counts/state and matching output, including missing/duplicate device-effect controls. Runtime hook installation remains pending; see `docs/specs/2026-10-18-runtime-bulk-instancer.md`. |
| `native_fx_state_contract.py`, `bench_native_fx_gpu.py` | verify and join native FX lifecycle/source selection and dirty-state application to the group/mesh GPU fixture. `make test-native-fx-state` checks both local images and altered-body controls; `make test-native-fx-recovery` checks 248 failures with device/effect/native-field snapshots and broken replay/offset controls; `make bench-native-fx-gpu` measures the final candidate serially. Sources/lifetimes remain controlled. See `docs/specs/2026-10-18-native-fx-gpu-integration.md`. |
| `prepare_chain_screen.py` | generates an ignored copy of our bulk dispatcher with only its one-method gate made switchable. Optional `build/bench_chain_screen.exe` and `_20.exe` targets compare installed/candidate gates in one process through native group/mesh/FX and GPU paths. Native chain walkers use controlled owning children; complete method-array boundaries remain intact. No production change, arbitrary native-child/failure qualification or FPS claim. See `docs/specs/2026-10-18-material-chain-bulk.md`. |
| `test_chain_recovery.py` | runs the optional `build/test_chain_recovery.exe` / `_20.exe` fixtures with verified native manifests; `--fault 1..7` selects deliberate replay/state corruption. Chains of two through six methods preserve nonempty output, references, lifecycle, effect/device state and following groups under GPU errors and partial staging. Child callbacks are controlled and the gate remains developer-only. See `docs/specs/2026-10-18-material-chain-recovery.md`. |

Beta.13's `build/test_chain_runtime.exe` and `build/test_chain_runtime_20.exe`
use the same recovery launcher but link the actual production dispatcher.
The switchable `bench_chain_screen_20.exe` retains old/new gate comparisons
and verifies the twelve consumed-follower CSV counters' attribution. See
`docs/specs/2026-10-18-material-chain-runtime.md` for production checks and
fresh paired measurements; the installed INI does not need modification.

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
