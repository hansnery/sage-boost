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
| `xrefs.py` | callers/references of an address, named by the function annotations of the Open-BFME-2 source tree (read only) |
| `port_sites.py` | turns a 1.06 function's first bytes into a wildcard signature that matches another image once; tests in `test_port_sites.py` |
| `bench_ambient.py` | verifies the full ambient-light sampler against the read-only reference and runs the isolated native cost screen on synthetic data; requires `perf/build/bench_ambient.exe`. No game entry point or scene runs. See `perf/tests/RENDER_SETUP_BENCHMARK.md`. |

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
