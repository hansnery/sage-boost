# Performance DLL: frame timing and skeleton updates on other cores

Status: design approved (2026-10-13), not implemented. Amended the same day:
the DLL loads as a Miles plugin (`mss\bfme2perf.asi`) and the faction
unlocker is left untouched.

## Goal

Make big battles in Realms in Exile (and then Age of the Ring) run smoother
on multi-core PCs, without changing what any player sees or what the game
simulates, so a player with the DLL can play with players without it.

The first version does two things, in this order:

1. **Timing.** Measure where a frame's time goes, with the skeleton update
   (animation poses) timed on its own.
2. **Skeleton pre-pass.** If the skeleton update is a large enough share,
   compute the poses of the units on screen on other cores before the game
   draws them, with bit-identical results.

Out of scope for now (later work, see the end): changes to game logic that
need every player on the same version (target search, pathfinding, the sync
check), a render thread, batching identical units, and the
off-screen/LOD work from the earlier survey.

## Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written; it is read and
  read-only scripts run against it.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT). Their
  READMEs and their general approach may be studied.
- `game.dat` and other EA assets are never redistributed. The DLL ships alone
  and changes nothing on disk.
- No hardcoded absolute addresses: every hook is found by signature in the
  running executable.

## Context

- Target executable: Age of the Ring's `game.dat` (ROTWK 2.01 plus the mod's
  edits), which Realms in Exile runs on. The decompilation is BFME2 1.06, so
  every function it names has to be found again in `game.dat` (Phase R).
- The game is single-threaded apart from audio, the asset manager, the mouse
  and GameSpy. Logic runs at 5 Hz and the client at 30 FPS.
- The user's game folder has DXVK's `d3d9.dll` and the faction unlocker's
  `dinput8.dll`. DXVK already moves part of the driver work to its own thread.

### What the survey found

- **Skinning is already on the GPU.** At load, the engine converts legacy W3D
  materials to `DefaultW3D.fx` ("FXShader conversion" messages in `game.dat`).
  The FX draw path (`MeshClass::Render_With_FX_Material`, 1.06 RVA
  `0x0014A880`) hands the skeleton and bone list to the shader (the shader
  parameter source; `rva0014D3E7`), and `defaultw3d.fxo` / `normalmapped.fxo`
  contain `WorldBones`, `NumJointsPerVertex` and `MaxSkinningBones`. The CPU
  path `DX8SkinFVFCategoryContainer::Render` is only a fallback. Realms' art
  has 23,507 skinned meshes with legacy materials and 36 with FX materials;
  both end up on the FX path.
- **The pose is computed lazily.** `Animatable3DObjClass::Update_Sub_Object_Transforms`
  (and `HLodClass::Update_Sub_Object_Transforms`, which also updates the
  sub-objects) computes the pivots of the object's own `HTreeClass` copy from
  the current animation (`Base_Update`, `Anim_Update`, `Blend_Update`,
  `Combo_Update`), then marks the hierarchy valid. It runs the first time
  anything needs the pose: usually the draw, sometimes a bone query
  (`Get_Bone_Transform`) from effects or attachments.
- **Animation progress depends on the frame's sync time.** For a single
  animation, `Single_Anim_Progress` sets the frame from `WW3D::Get_Sync_Time()`
  and stores it, so a second call in the same frame gives the same frame.
- **Shared animation data has caches.** Animations (`HAnimClass` and its
  channels) are shared by every unit that plays them. Channel lookups write
  caches, for example `TimeCodedBitChannelClass::Get_Bit`'s `CachedIdx`.
  That one is only a search hint (any valid index gives the same answer),
  but every write the pose path makes to shared data has to be audited.
- **Two dependencies cross objects.** An object slaved to another one
  (`_bfme_a3o_v0`) copies the master's finished pose, updating the master
  first. A sub-object updates its container first.
- **Where the draw decides what is on screen.** `RTS3DScene::Customized_Render`
  runs `Visibility_Check` (sets each top-level object's visible flag), then
  `On_Frame_Update` for the update list, draws the terrain, and then draws
  each object in `RenderList` that `Is_Really_Visible()`. A reflection pass
  runs the same function with back-face culling inverted.

## Project shape

- New folder `perf/` next to `dll/` in this repository, with its own
  makefile, INI, log, tests and version. Same toolchain as the unlocker:
  C, 32-bit MinGW, `-ffreestanding -nostdlib`.
- It reuses copies of the unlocker's signature scanner, hook stubs
  (`hooks.h`), logger and INI reader, kept in `perf/`. Nothing in `dll/` is
  changed and the two DLLs share no state at run time.
- The faction unlocker is a separate project and stays as it is: no code
  change, and its `dinput8.dll` and `dinput8.ini` are never replaced or
  renamed.
- Output: `bfme2perf.asi`, a normal DLL with the Miles plugin extension.
  Its settings are in `bfme2perf.ini` next to it.

## Loading

The game's audio library, Miles (`mss32.dll`), loads every `*.asi`, `*.flt`
and `*.m3d` plugin in its redist folder (the game folder's `mss\`, set with
`AIL_set_redist_directory`) when the game calls `AIL_startup`, and calls
each plugin's `RIB_Main`. The folder already holds `mssmp3.asi`,
`mssdsp.flt`, `msseax.m3d` and `msssoft.m3d`.

So the DLL is installed as `mss\bfme2perf.asi`:

- It exports `RIB_Main`, registers no audio provider and returns success.
- In `DllMain` it pins itself (`GetModuleHandleExW` with
  `GET_MODULE_HANDLE_EX_FLAG_PIN`), so Miles unloading its plugins can't
  unload it.
- It installs the game-code hooks from `DllMain`, like the unlocker, with
  no `LoadLibrary` under the loader lock. The worker threads are created
  later, on the first pre-pass.
- No file is replaced, no export is forwarded, and it works the same with
  or without the faction unlocker. Removing the file uninstalls it.

If the game starts with audio off, Miles doesn't start and the DLL doesn't
load; the game runs as without it.

The rejected options were:

- a hook in the faction unlocker (the user's choice: separate projects);
- taking the `dinput8.dll` name (that's the unlocker's file);
- a proxy for a small system DLL the game imports, such as `netapi32.dll`
  or `winmm.dll` (every export has to be forwarded, Windows versions differ,
  and other modules in the process would load the proxy too);
- a separate launcher (it conflicts with Realms' own launcher).

## Phase R: research (read-only)

Done on Age of the Ring's `game.dat`, with `game820.dat` (clean 2.01) for
comparison. Output: a findings section in this spec, with each site's
signature and the address it was found at.

| # | Question |
|---|---|
| R0 | Loading: the redist folder the game passes to `AIL_set_redist_directory`; whether Miles loads an `.asi` that registers nothing, what it does with its `RIB_Main` return value, and whether it unloads plugins at `AIL_shutdown`; when `AIL_startup` runs relative to the first frame. Confirmed in game with a stub `.asi` that only writes a log line. |
| R1 | Find in `game.dat`: the client frame (`GameEngine::update` or its frame body), `GameLogic::update`, the scene render (`RTS3DScene::Customized_Render`), `Visibility_Check`, and both `Update_Sub_Object_Transforms` (`Animatable3DObjClass`, `HLodClass`). Each one needs a signature found exactly once. |
| R2 | The pre-pass hook point: after `Visibility_Check` and the `On_Frame_Update` loop, before the first object in `RenderList` is drawn, only when not in the reflection pass. Either a call site inside `Customized_Render` or the first `renderOneObject` call of the frame. |
| R3 | How to tell from the outside whether an object is top-level, visible (`Is_Really_Visible`), an `HLodClass` / `Animatable3DObjClass`, slaved, its motion mode, its animation pointers and whether its hierarchy is valid: vtable slots and field offsets in 2.01. |
| R4 | The audit: every function the pose path can reach for a `SINGLE_ANIM` or `BASE_POSE` HLod and its sub-objects, and every write to memory that isn't the object's own (reference counts, allocator, channel caches, globals, statics). Each write is classed as harmless, covered by grouping by animation, or a reason to keep that case on the main thread. |
| R5 | The floating-point settings the main thread runs with (x87 control word, SSE MXCSR) when the pose path runs. |
| R6 | Whether Granny models (`TheGrannyRenderObjSystem`) appear in Realms or AotR at all; if they do, they stay on the main thread. |

### R0 findings (in game, 2026-10-13)

Plugin 0.1.0 (log only) in `rotwk\mss\`, Realms in Exile through its
launcher, `Test Replay` played from the menu.

- The game runs with `rotwk` as its working directory and passes the
  relative `MSS\` to `AIL_set_redist_directory` (`game.dat` 0x00460DCF),
  then calls `AIL_startup`. It skips both when GlobalData+0x99C is 0 (audio
  off). `AIL_startup` loads every `*.mix`, `*.asi`, `*.m3d` and `*.flt` in
  that folder with `LoadLibraryA` and calls `_RIB_Main@8` with `up_down=1`
  (`mss32.dll` 0x211027B3).
- Loaded from `C:\Jogos\AgeoftheRing\rotwk\MSS\bfme2perf.asi` into
  `game.dat`, 2,323 ms after process start. `d3d9.dll` was not loaded yet;
  the faction unlocker's `dinput8.dll` and `mss32.dll` were.
- `RIB_Main` calls: one, `provider=2 up_down=1`, at 2,323 ms. No
  `up_down=0` call at exit.
- Pinned: yes. The game behaved as usual.
- Consequence for plan 3: the hooks can be installed from `DllMain`, before
  the graphics device exists and before the first frame.

### R1 findings (2026-10-13)

Found with `tools/port_sites.py` from the 1.06 RVAs in the decompilation's
comments. Each 1.06 signature matched exactly once in both 2.01 and AotR at
48 and 96 bytes too, and AotR's first 0x400 bytes of every function are
identical to 2.01's. Each signature below is AotR's own first 24 bytes,
masked, and matches exactly once in AotR's `game.dat` and in `game820.dat`.

| Function | 1.06 RVA | 2.01 / AotR RVA | Signature (24 bytes) |
|---|---|---|---|
| `GameEngine::update` | 0x00225DA9 | 0x002325A0 | `55 8B EC 51 53 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28 8B 0D ?? ?? ??` |
| `Win32GameEngine::update` | 0x00041EC3 | 0x0004181F | `56 8B F1 E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 57 8B 3D` |
| `GameLogic::update(int)` | 0x0024555A | 0x0022E4E8 | `B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 74 01 00 00 53 56 8B F1 57 8D 4D D8` |
| `setFPMode` | 0x00040EA9 | 0x00040809 | `FF 15 ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 25 FF FC FE FF 0D 00 00 02 00 68 00` |
| `RTS3DScene::Customized_Render` | 0x0007054F | 0x0006FE84 | `55 8B EC 83 EC 0C 53 56 33 F6 57 8B F9 89 B7 F0 07 00 00 89 B7 F8 07 00` |
| `RTS3DScene::Visibility_Check` | 0x00071245 | 0x00070B83 | `B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 60 53 56 8B F1 8D 9E BC 00 00 00 57` |
| `RTS3DScene::renderOneObject` | 0x0006FB59 | 0x0006F48E | `B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC 94 02 00 00 53 56 8B F1 57 33 FF 8D` |
| `HLodClass::Update_Sub_Object_Transforms` | 0x0019D640 | 0x0019CD70 | `83 EC 4C 53 55 56 57 8B D9 E8 ?? ?? ?? ?? 33 C9 39 8B 20 01 00 00 89 4C` |
| `Animatable3DObjClass::Update_Sub_Object_Transforms` | 0x001A5900 | 0x001A5050 | `83 EC 30 56 8B F1 8B 8E FC 00 00 00 85 C9 74 71 80 B9 F4 00 00 00 00 75` |
| `MeshClass::Render_With_FX_Material` | 0x0014A880 | 0x0014A1F0 | `6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 81 EC 00` |

Notes:

- `GameEngine::update` is the first call in `Win32GameEngine::update`
  (vtable slot 10); the decompilation pins its 1.06 body at 0x00225DA9.
- `GameLogic::update`, `Visibility_Check` and `renderOneObject` start with
  `mov eax, imm32; call` (the exception-frame prologue), so an entry hook
  there steals those 10 bytes and re-creates the call.
- AotR's `Animatable3DObjClass::Update_Sub_Object_Transforms` reads the
  slave master at `+0xFC` and the master's `IsTreeValid` at `+0xF4`, the same
  offsets as 1.06 (R3 checks the rest).
- Since AotR's sites equal 2.01's, the plugin also runs on clean 2.01; BFME2
  1.06 has the same functions at other RVAs, found by the 1.06 signatures.

### R2 findings (2026-10-13)

AotR's `Customized_Render` (0x0006FE84, `edi` = scene `this`, `[ebp+8]` =
`RenderInfoClass &`) differs from Zero Hour's in the details:

1. `[this+0x7EC]` is the pass mode. Only mode 0 calls `Visibility_Check`
   (scene vtable +0x6C, argument the camera from `rinfo`).
2. Only mode 0 walks the update list (sentinel `this+0x78`, first node
   `[this+0x7C]`; node `+4` next, `+0xC` interface pointer, object = pointer
   − 8 when non-zero). Class ID (object vtable +0x0C) 4 is the terrain and is
   kept for later; every other object gets `On_Frame_Update` (vtable +0x34)
   unless one of two global bytes is set: 0x00DD3445 (1.06 0x00DEDA05,
   `bfmeCameraProjectionOverride`) or 0x00DD1E44 (1.06 0x00DEC404,
   `WW3D::Is_Currently_Rendering_Shadow_Map()`). This is BFME2's replacement
   for Zero Hour's mirror-pass test.
3. The terrain is drawn (vtable +0x30, `Render(rinfo)`) when one was found.
4. If `[this+0x12C]` (terrain only) is set or the mode is 4, the object loop
   is skipped. Otherwise it walks `RenderList` (sentinel `this+0xF0`, first
   node `[this+0xF4]`, same node layout), skips class 4, asks object vtable
   +0x180 whether to draw, and draws through `renderOneObject` (0x0006F48E)
   or the object's own `Render` (vtable +0x30), depending on the mode.

**Hook site: 0x0006FFFA**, `8B 9F F4 00 00 00` (`mov ebx, [edi+0xF4]`, the
`RenderList` loop's first node), reached once per scene pass after the
visibility check, the update loop and the terrain. No branch lands inside
the 6 bytes. Signature, unique in `game.dat` and `game820.dat` (`|` marks
the site):

`80 BF 2C 01 00 00 00 0F 85 ?? ?? ?? ?? 83 BF EC 07 00 00 04 0F 84 ?? ?? ?? ?? | 8B 9F F4 00 00 00 E9 ?? ?? ?? ?? 8B 73 0C 8B 5B 04`

The stub runs the pre-pass only when `[edi+0x7EC] == 0` and both global
bytes are 0, then executes the stolen `mov ebx, [edi+0xF4]` and jumps back
to 0x00070000. The two byte addresses are read from the code at run time,
from the update loop's flag test (unique in `game.dat`, at 0x0006FF37):

`80 3D ?? ?? ?? ?? 00 75 1A 80 3D ?? ?? ?? ?? 00 75 11 8B 4B 0C`

(the first `??` run is the projection override, the second the shadow-map
flag).

### R3 findings (2026-10-13)

From AotR's bodies of `Animatable3DObjClass::Update_Sub_Object_Transforms`
(0x001A5050), `HLodClass::Update_Sub_Object_Transforms` (0x0019CD70),
`Customized_Render` and the two classes' vtables. The `Animatable3DObjClass`
offsets equal 1.06's.

| What | AotR |
|---|---|
| HLod vtable | RVA 0x007EC7F0 (the entry pointing at 0x0019CD70, minus 42 slots) |
| `Class_ID` | vtable +0x0C; HLod returns 0x19 (25, WW3D's `CLASSID_HLOD`); 4 is the terrain |
| `Validate_Transform` | vtable +0x50 |
| `Update_Sub_Object_Transforms` | vtable +0xA8 (slot 42) |
| `Get_HTree` | vtable +0xE8 (slot 58), `mov eax, [ecx+0xF8]` |
| `Is_Really_Visible` | vtable +0x180 (slot 96), `([this+0x10] & 0x6000) == 0x6000` |
| Transform | `this+0x18` (`Matrix3D`) |
| `IsTreeValid` | byte `this+0xF4` |
| `HTree` | `this+0xF8` (the object's own copy) |
| slave master | `this+0xFC` |
| `CurMotionMode` | `this+0x100`: 1 `BASE_POSE`, 2 `SINGLE_ANIM`, 3 `DOUBLE_ANIM`, 4 `MULTIPLE_ANIM` (jump table); 0 or above 4 does nothing |
| `SINGLE_ANIM` | motion `+0x104`, frame `+0x108`; `+0x110` ≠ 0 (not manual) first calls `Single_Anim_Progress` (0x001A4A70) |
| `DOUBLE_ANIM` | motion0 `+0x104`, frame0 `+0x108`, motion1 `+0x10C`, frame1 `+0x110`, percentage `+0x114` |
| `MULTIPLE_ANIM` | combo `+0x104` |
| HLod LODs | count `this+0x120`, array `this+0x128`, entry stride 0x28: model count `+0x10`, model array `+0x04`, model stride 0x14: render object `+0x00`, pivot index `+0x04` |
| HLod additional models | count `this+0x148` |

Behaviour that changes the design:

- **No container dependency.** The base `Update_Sub_Object_Transforms`
  that the animatable update calls first (0x00688AC0) is a bare `ret`, so
  an object never updates its container. The slave master is the only
  dependency between top-level objects.
- **HLods update their sub-objects themselves.** After its own pose, the
  HLod sets each LOD model's transform from its pivot (vtable +0x54, +0x19C,
  +0x5C) and calls the model's `Update_Sub_Object_Transforms`, so the whole
  tree is one unit of work.
- **The pose math uses SSE.** The HLod's pivot-to-matrix code is
  `movss`/`mulss`, so MXCSR matters as much as the x87 control word (R5).
- The update functions do the mode switch themselves; the pre-pass only
  needs `Class_ID == 25`, `Is_Really_Visible`, `IsTreeValid == 0`, slave
  master 0 and the motion mode.

### R4 findings (2026-10-13)

A recursive-descent walk of AotR's code from both update functions (direct
calls followed, jump tables decoded), every memory write listed and
classified, with the decompilation as the guide for the virtual calls into
the animation classes.

| Path | AotR functions | Writes outside the object's own memory | Verdict |
|---|---|---|---|
| `BASE_POSE` | 0x001A3F40 → `HTree::Base_Update` 0x00162130 | none (own `HTree` pivots, `IsTreeValid`) | pre-pass |
| `SINGLE_ANIM`, frame | `Single_Anim_Progress` 0x001A4A70 → `Compute_Current_Frame` 0x001A4770 | none (own frame `+0x108`, sync time `+0x114`); reads the animation's frame count and rate through its vtable, and the global sync time | pre-pass |
| `SINGLE_ANIM`, pose | 0x001A3F70 picks by animation vtable +0x54: `HTree` 0x00163310, or 0x00162440 with the per-skeleton cursor buffer (0x00160850) | the cursor buffer is the skeleton's own, reallocated through `MemoryPool::_Free` / `_Allocate` when the animation changes; the visibility bit channel's `CachedIdx` (a member of the shared animation) | pre-pass, grouped by animation |
| `DOUBLE_ANIM` | `Blend_Update` 0x001A3FE0 → 0x00163E40 | as `SINGLE_ANIM`, for two animations | main thread (first version) |
| `MULTIPLE_ANIM` | `Combo_Update` 0x001A4020 → 0x001650B0 | non-atomic reference counts on shared objects (`add [eax+4], ±1` via 0x00195E10, `add [eax+0x1C], ±1` at 0x0016548A / 0x00195EE0) | main thread |
| slave | 0x001A5050 first branch → `HTree::Slave_Update` 0x00160540 | the master's pose, if stale | main thread |

Details:

- **The animation data is read-only in the pose path.** BFME2's compressed
  animations keep their per-lookup cache in locals (`cache = 0x0FFFFFFF`
  for every call, `hcanim.cpp` `Get_Translation`/`Get_Orientation`). Its
  stream channels (encodings 1 and 2) either decode from the shared header
  into locals or continue from a record in the caller's cursor buffer; the
  decoders only read their `state` argument, the channel's `Data`,
  `Initial` and the filter table. The one shared write is
  `TimeCodedBitChannelClass::Get_Bit`'s `CachedIdx`, a search hint (any
  valid index gives the same answer); grouping by animation keeps it on
  one thread anyway.
- **The allocator is thread-safe.** `MemoryPool::_Allocate` (0x0002FE60) and
  `_Free` (0x0002FED0) are reached through function pointers that
  `game.dat` fills at startup with `GetProcAddress` on its own exports
  (0x00DC5E44 and 0x00DC5E3C); their callees take critical sections and use
  TLS. Allocation order changes addresses, but those already differ between
  players.
- **Still to check before Milestone 2 enables anything:** the three virtual
  calls the HLod makes on each of its sub-objects besides
  `Update_Sub_Object_Transforms` (vtable +0x54, +0x19C, +0x5C), for the
  sub-object classes Realms' units use (`MeshClass` and nested HLods). The
  walk can't follow them without the sub-objects' vtables. Plan 4 starts
  with this.

### R5 and R6 findings (2026-10-13)

- **x87.** `setFPMode` (0x00040809) is `_fpreset()`, then
  `_controlfp((_statusfp() & 0xFFFEFCFF) | 0x20000, 0x30300)` from
  `msvcr71.dll`: 24-bit precision, round to nearest. `game.dat` has 527
  `fldcw`, mostly the float-to-int conversions, which restore the word.
- **SSE.** `game.dat` contains no `ldmxcsr` or `stmxcsr`; MXCSR is whatever
  `msvcr71.dll` (`_fpreset`, `_controlfp`) and the graphics layer leave.
  The pose path uses SSE (R3), so the workers copy both words from the main
  thread at each pre-pass (`fnstcw` / `stmxcsr` there, `fldcw` / `ldmxcsr`
  in the worker), as the design says, instead of assuming values.
- **Granny (R6).** Not used: no `.gr2` file in Realms' folder or in any of
  the 46 `.big` archives (AotR and Realms), no "granny" string in
  `game.dat`, no `granny2.dll`. Nothing to keep off the workers.
- **`profile-high.csv`.** Written at exit by the game's built-in profile
  library (writer at 0x006DBCC6): a header, then one row per high-level
  counter with one column per recorded frame range. The user's file has
  only the header because nothing records: `Profile::StartRange` records a
  range only when an active pattern from the `profile` debug-command group
  matches its name. Those commands can probably be given through a
  `.dbgcmd` file, as the Realms WorldBuilder setup does; if so, the
  built-in counters can cross-check Milestone 1's numbers. Not needed for
  the plan.

## Milestone 1: timing

Hooks (entry and exit, no change in behaviour):

- the client frame, `GameLogic::update` and the scene render;
- `Update_Sub_Object_Transforms`, counted and timed, split into calls during
  the scene render and calls elsewhere.

Timing uses `QueryPerformanceCounter`. Nested calls (an HLod updating its
sub-objects) are counted once, at the outermost call.

`bfme2perf_timing.csv`, next to the DLL in `mss\`, gets one row per second with the
second's averages: frame time, logic tick, render, skeleton update during the
draw, skeleton update elsewhere, animated objects on screen, objects handed to
the workers and objects left to the main thread (both 0 in Milestone 1), and
milliseconds per animated object on screen. Old files are overwritten at game
start; the first row is a header.

As built in 0.2.0 (plan 3):

- "The scene render" is `RTS3DScene::Render` (1.06 0x00071E40, 2.01 / AotR
  0x0007177E, signature `55 8B EC 83 EC 28 53 56 8B 35 ?? ?? ?? ?? 85 F6 57
  8B F9 89 7D E4 0F 84`), which runs `Customized_Render` and `Flush`, so the
  draw calls are included. The frame is `GameEngine::update`, which excludes
  the frame limiter's wait.
- Only calls on the main thread (the first to run `GameEngine::update`)
  are timed; other threads pass straight through.
- "Animated objects on screen" is counted as the outermost skeleton
  updates during the render: the objects whose pose the draw computed.
  Objects whose pose was already valid (static ones) aren't counted;
  walking `RenderList` from the plugin would mean calling game virtuals
  every frame, which the timing build avoids.
- Columns (ms values are per-frame averages over the second):

| Column | Meaning |
|---|---|
| `time_s` | seconds since the first frame, at the row's start |
| `frames` | client frames in the row |
| `frame_avg_ms`, `frame_max_ms` | time inside `GameEngine::update` |
| `logic_ms` | `GameLogic::update`, all its phases in the frame |
| `render_ms` | `RTS3DScene::Render` |
| `skel_render_ms` | outermost skeleton updates during the render |
| `skel_other_ms` | outermost skeleton updates elsewhere |
| `anim_on_screen` | outermost skeleton updates during the render, per frame |
| `to_workers`, `main_thread` | 0 in Milestone 1 |
| `skel_us_per_obj` | `skel_render` time per counted object, in microseconds |

A column whose hook isn't installed is left empty.

### Milestone 1 results (2026-10-13, `Test Replay`)

Plugin 0.2.0, Realms in Exile (`-mod ...realms_nightly`), DXVK at
3840x2160 with FIFO presentation, the faction unlocker loaded too. All five
hooks applied at their R1 RVAs; no crash; DXVK's log has only its usual
unknown-format warnings and the unlocker logged its normal exit summary.

The CSV covers 615 s: about 40 s of menus and loading, then 575 s of the
replay at the 30 FPS cap (17,498 frames in all). That is about 9.6 minutes
of the replay's roughly 11.3; either playback stopped early or it runs a
little faster than 5 logic frames per second.

| Period | Frame (avg) | FPS (avg / min) | Logic | Render | Skeleton in render | Skeleton elsewhere | Other | Animated objects | µs per object |
|---|---|---|---|---|---|---|---|---|---|
| All play (563 s) | 19.4 ms | 30.0 / 1 | 18.1% | 57.5% | 4.1% | 0.2% | 24.4% | 121 (max 323) | 5.5 |
| Top 10% by frame time (55 s, after the first 10 s) | 34.5 ms | 27.6 / 21 | 16.5% | 64.5% | 5.2% | 0.2% | 19.0% | 273 | 6.6 |
| Top 10% by animated objects (56 s) | 33.4 ms | 28.2 / 21 | 16.2% | 65.0% | 5.2% | 0.2% | 18.7% | 281 | 6.2 |

Percentages are shares of the frame time; "Other" is the frame outside
`GameLogic::update` and `RTS3DScene::Render` (client and UI updates,
particles, audio, the network, `Present`). The heaviest stretch is from
about minute 7 of the replay (`time_s` 420–560): 28–34 ms of work per
frame against the 33.3 ms budget, so FPS dips to the 20s there. Over the
whole replay, 31 of 553 seconds averaged more than 33.3 ms. The worst
single frames (1.1 s, 0.4 s) are right after loading.

Checks: logic + render never exceed the frame, skeleton time never exceeds
render time, and skeleton time tracks the animated-object count closely
(correlation 0.985); render time does far less (0.35). The only rows longer
than a second are loading frames.

**Decision gate: not passed.** The skeleton update during the render is
about 5% of the frame in the heavy periods (1.7–1.8 ms of 33–34 ms), against
the 10% guide. Moving it to other cores would save at most about 1.5 ms per
frame here. The render without the skeleton updates is the big part (about
59%, 20 ms per frame in the heavy periods), then logic (16%, 5.4–5.7 ms)
and the rest of the frame (19%).

What a bigger battle would change: at about 6.5 µs per animated object,
1,000 on screen would cost about 6.5 ms in skeleton updates. Render and
logic grow with the army too (draw calls; target search and pathfinding,
which grow faster than linearly), so the skeleton share would likely stay
below 10%, and logic's share would rise. A big-battle replay should confirm
this before Milestone 2 is dropped for good.

Measurement notes:

- The frame time includes `Present`. With FIFO presentation that may block,
  which would show up in "Other"; the frame limiter's wait is not included.
- `time_s` is truncated from each row's start, and each row ends at the first
  frame after a full second, so rows run slightly long and about every 50 s
  one second number is skipped. Cosmetic; the fix is to close rows on whole
  seconds since the first frame (`first_t0 + n * freq`) instead of one second
  after the previous row.
- The hook overhead (two QPC reads and the stubs, well under 1 µs per call)
  is included in the skeleton times; with about 300 objects it's under
  0.1 ms per frame.
- This replay is small (the user against two AIs, at most 323 animated
  objects on screen).

**Next:** split the render to find where its 20 ms go before choosing the
next change. Time `Visibility_Check`, `Customized_Render` and `Flush`
separately, and each scene pass (normal, shadow map, reflection) by its
pass mode and flags (R2). The candidates the split would point to are
fewer draw calls (batching), cheaper passes (shadows, water reflection at
4K) or a render thread, which the design kept as the riskiest option. The
logic share points to the lockstep work (target search, pathfinding) in
"Later work".

**Decision gate.** The user plays a big-army replay with the plain game plus
the DLL in timing-only mode. If the skeleton update during the draw is a
small share of the frame (as a guide, under 10%), Milestone 2 stops and the
design is revisited with the numbers.

## Milestone 1b: render breakdown (amendment, 2026-10-13)

Milestone 1 showed the render, not the skeleton update, is most of the
frame. The user chose to put Milestone 2 on hold and first split the
render. Plugin 0.3.0 keeps every 0.2.0 column and adds the split; it still
only measures.

### Research (read-only)

- **The game names its own render sections.** `W3DDisplay::draw` and the
  scene code wrap each section in a `BFME2ScopedRenderEvent` (a debug
  marker object on the stack: constructor 1.06 0x00117CF0, AotR
  0x00117690, `thiscall (label, group, colour)`, `ret 0xC`, does nothing
  for a null label; destructor AotR 0x00117740, a lone `jmp` to the marker's
  end function). The labels found in AotR include, at frame level,
  `UpdateShadowMap`, `UpdateWaterReflection`, `RenderViews`, `RenderUI`;
  inside the scene `RenderTerrain` (with roads, floors, scorches, tracks,
  props and more), `RenderWater`, `RenderParticles`, `RenderTrees`,
  `RenderShrubs`, `RenderDecalShadows`, `RenderVolumeShadows`,
  `RenderStaticSortLists`, `RenderSmudges`, `RenderBuffs`; and per batch
  `RenderFXShaderBatch`, `MeshFXShader`, `MeshDX8Render`. Timing the
  constructor and destructor pairs gives the breakdown by the game's own
  names.
- **The shadow-map pass** is `W3DShadowMapManager::UpdateShadowMap`
  (1.06 0x0007DC01, AotR 0x0007D5C9), inside the `UpdateShadowMap` marker.
  **The water reflection** is inside `UpdateWaterReflection`. The camera
  projection override byte (R2) has no writer that anything calls, so it
  doesn't identify a pass.
- **`Present`** is inside `DX8Wrapper::End_Scene(bool flip)` (1.06
  0x00122BE0, AotR 0x001225E0), reached from `WW3D::End_Render`. Timing it
  from the game's side, for calls with `flip` set, separates the vsync wait
  from the work without touching DXVK.
- **Draw calls** all go through `BfmeDrawOps::Draw` (1.06 0x001203C0, AotR
  0x0011FD40), which `Draw_Triangles`, `Draw_Strip` and the non-indexed
  forms tail into. Counting its calls counts draw calls through the W3D
  wrapper.
- **Inside the scene render**: `Visibility_Check` (0x00070B83) runs inside
  `Customized_Render` (0x0006FE84); `RTS3DScene::Flush` (1.06 0x00071606,
  AotR 0x00070F44, `ret 4`) holds the mesh renderer flush, the sorting
  renderer flush and the water, shadow, particle and static-sort markers.

Every site's signature is unique in AotR's `game.dat` and `game820.dat`,
and no branch lands inside a stolen range. The marker destructor's 5 bytes
are a `jmp rel32`; its trampoline jumps to the decoded target instead of
copying it, and its signature starts in the end of the constructor.

### What 0.3.0 adds

- Wrapped (timed): `Visibility_Check`, `Customized_Render`,
  `RTS3DScene::Flush`, `DX8Wrapper::End_Scene` (counted as `present` only
  with `flip` set).
- Probed (seen at entry only, never wrapped): the marker constructor and
  destructor, `BfmeDrawOps::Draw`.
- Markers are matched by their `this` (the stack object); a constructor
  with a null label is ignored. Each label's time is inclusive (nested
  markers count in both).
- CSV columns added after the 0.2.0 ones, all per-frame averages: `vis_ms`,
  `cust_ms` (includes `vis`), `flush_ms`, `present_ms`, `shadow_ms`,
  `reflect_ms`, `views_ms`, `ui_ms`, `terrain_ms`, `water_ms`,
  `particles_ms`, `trees_ms`, `shrubs_ms`, `decal_shadows_ms`,
  `volume_shadows_ms`, `static_sort_ms`, `fx_mesh_ms`, `dx8_mesh_ms`,
  `draws` (per frame), `rest_ms`.
- `rest_ms` is the frame minus the parts that don't overlap: the logic,
  the four frame-level markers and `present`, each counted only when no
  other of them is open.
- `bfme2perf_scopes.csv`, once per minute: every label seen, with calls
  and milliseconds per frame, so labels without a column still show up.
- **Rows close on whole seconds since the first frame**, so `time_s` no
  longer skips numbers (only a frame longer than a second, such as
  loading, leaves a gap).
- Hook groups: the frame (required), logic, render, skeleton, scene
  (visibility, `Customized_Render`, `Flush`), present, markers (both),
  draws. A group whose signature is missing turns off only its columns.

Overhead: the marker and draw probes run thousands of times per frame; each
costs the stub and a few comparisons, and the markers two clock reads. The
per-label times include that.

### Milestone 1b results (stress test, 2026-10-13)

Plugin 0.3.0, Realms in Exile, a live skirmish (Lindon with TestGollum,
"an insane amount of units" spawned), 3840x2160, DXVK with FIFO. All 12
hooks applied; the game ran 870 s of play (about 14.5 minutes) and ended
normally (the CSV goes on into the menus, the unlocker logged its exit
summary, DXVK's log has no errors). Copies are in
`%TEMP%\bfme2perf-0.3.0-stress-1542`.

| Period | Frame | FPS avg / min | Logic | Shadow map | Water reflection | Views | UI | Present | Skeleton | Rest | Draws per frame | Animated objects |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| All play (870 s) | 41.0 ms | 23.5 / 1 | 11% | 28% | 18% | 28% | 3% | 0.0% | 5.0% | 12% | 11,281 | 398 (max 893) |
| Top 10% by frame time | 67.6 ms | 14.8 / 12 | 9% | 28% | 23% | 28% | 2% | 0.0% | 4.7% | 10% | 18,669 | 646 |
| Top 10% by animated objects | 61.8 ms | 16.3 / 12 | 9% | 31% | 23% | 27% | 2% | 0.0% | 6.2% | 8% | 22,268 | 822 |

Inside the scene renders (all three passes, top 10% by animated objects):
`RTS3DScene::Render` 49.5 ms, of which `Flush` (the draw-out) 47.0 ms,
`Customized_Render` 2.5 ms (visibility check 1.5 ms). Particles 1.1 ms
(3.6 ms in the heaviest frames), terrain 0.3 ms, static sort lists 0.2 ms,
decal shadows 0.2 ms; water, trees, shrubs and volume shadows under
0.1 ms each. In the heaviest minute the scopes table shows
`RenderFXShaderBatch` (no column of its own) at 21.7 ms and 1,691 calls per
frame, and about 14,700 per-mesh FX markers per frame (about 4,800 per
pass).

How it scaled from the light `Test Replay` run (top 10% by animated
objects): animated objects 281 to 822 (2.9x), render 21.7 to 49.5 ms
(2.3x), logic 5.4 to 5.3 ms per frame (unchanged; the spawned units were
probably idle, so this run doesn't load the simulation the way a real
battle does), skeleton 1.7 to 3.8 ms, at 5.2% to 6.2% of the frame. Draws
track the animated objects (correlation 0.97), as do the shadow pass
(0.90) and the render (0.85).

**Decision gate: still not passed** (6.2% at 893 animated objects on
screen against 10%).

**The limit is the CPU, not the GPU or vsync**: `present` is 0.01 ms. Each
frame draws the units three times, into the shadow map, into the water
reflection and in the main view, at about 22,000 draw calls per frame at
the peak. Nearly all the render time is spent submitting them (`Flush`),
which with DXVK includes the D3D9 front end on the game's thread.

Measurement notes:

- **The per-mesh markers aren't the `MeshFXShader` and `MeshDX8Render`
  labels the columns expect.** The game builds them at run time as
  `Rendering mesh<TAB>FXShader<TAB><model>` in reused buffers. So
  `fx_mesh_ms` and `dx8_mesh_ms` stay at 0, and the per-mesh rows in the
  scopes CSV carry the name of the first model seen in each buffer (their
  call counts and times are right; their names are not). Fix: classify by
  the `Rendering mesh<TAB>FXShader` / `<TAB>DX8Render` prefix, don't cache
  those by pointer, and add a column for `RenderFXShaderBatch`.
- **The probes cost something at this scale.** About 22,000 draw probes
  and 16,000 marker pairs (each with two clock reads) per frame: probably
  2–3 ms of a 62 ms frame, inside the times of the sections they sit in.
  Fix: an INI switch to turn off the per-mesh markers and the draw count,
  so a run can measure the overhead directly.
- No impossible values: logic + render never exceed the frame, visibility
  never exceeds `Customized_Render`, `Customized_Render` + `Flush` never
  exceed the render, and `rest` is never negative.

**Targets, by expected gain** (all are client-only: rendering doesn't feed
the simulation, so a player with the DLL can play with players without it):

1. **The water reflection pass** (23% of a heavy frame, 14–16 ms). It
   re-renders the units into a texture every frame. Updating it every other
   frame saves about half (7–8 ms at the peak), with a one-frame-old
   reflection as the only visible change; leaving units out of the
   reflection would save most of it, with a bigger visual change.
2. **The shadow-map pass** (28–31%, about 19 ms). Updating it every other
   frame saves about half, but moving units' shadows would lag a frame,
   which is more visible than a stale reflection; culling shadow casters
   outside the view would save less but change nothing on screen.
3. **The number of draw calls** (about 22,000 per frame, 4,800 FX meshes
   per pass). Batching identical units (the same model and animation
   frame) would help all three passes at once. It's the biggest gain and
   the biggest job.

The logic (9–11%) and the skeleton update (5–6%) come after these.

**Next:** first confirm the pass costs with the game's own settings (water
reflections and shadows off, one at a time, same stress setup), which
needs no code. Then build the reflection update interval as the first
optimisation (a `[Render] ReflectionInterval` setting, default 1 = off):
the smallest change with the clearest gain and the least visible effect.

## Milestone 2: water reflection interval (amendment, 2026-10-13)

The skeleton pre-pass is put off (see "Later: skeleton pre-pass" below):
the gate failed twice, at 5.2% of the frame in `Test Replay` and 6.2% at
893 animated objects in the stress test. Milestone 2 is now the first
change that alters the game: `[Render] ReflectionInterval=N` redraws the
water reflection only every Nth frame. The user chose it over first
checking the in-game settings.

### What the reflection pass does (read-only research)

`W3DDisplay::draw` (AotR around 0x00049C47) runs, inside the
`UpdateWaterReflection` marker:

1. `0x0007F695(camera)` on the water object (`[0x00DC7A40]`): whether a
   reflection is needed this frame (1.06 `0x0007FD89`, the per-entry
   visibility test). It runs **before** the marker and isn't touched.
2. `call 0x0007F25A(scene, camera)` at **0x00049E5D**, the only caller of
   that function. `0x0007F25A` (1.06 `0x0007F92A`) is a guard: it returns
   unless the terrain object and its `+0x37C0` are present, then jumps to
   the body `0x0007F1AC` (1.06 `0x0007F87C`, `thiscall`, `ret 8`, void).

The body, in order:

- `0x0007EA2B(camera, false)` (1.06 `rva0007F0FB`): takes the DX8 thread
  lock, builds the mirrored reflection camera in the water object
  (`+0xFC`), turns D3D clip planes off if they're on (render state 0x98),
  raises the camera projection override byte (`0x00DD3445`) and applies
  the reflection camera (0x00135530). With `false` the byte stays raised.
  It bumps two DX8 statistics counters and releases the lock.
- Sets the render target to the reflection texture (`+0xF8`, 0x001201D0),
  `WW3D::Begin_Render` (clears it), `WW3D::Render(scene, reflection
  camera)` (the whole scene through `RTS3DScene::Render`), and
  `WW3D::End_Render(false)` (no flip).
- Lowers the override byte and restores the default render target
  (0x0011DBF0).

Side effects outside the reflection texture, and why skipping is safe:

- **No per-frame object updates.** `Customized_Render` skips the
  `On_Frame_Update` loop while the override byte is set, and the byte is
  set for the whole reflection render. So particle emitters and the other
  update-list objects are updated by the main view only, with or without
  the reflection.
- **`WW3D::FrameCount`** (`0x00DD1E20`) rises by one per `End_Render`. Its
  readers (the skin and mesh renderers, `ParticleBufferClass::Render_Line`)
  compare it to tell one pass from the next; each remaining pass still ends
  with its own `End_Render`, so each still sees a new value. Only how fast
  it rises changes.
- **Render state.** The pass leaves the default render target, the
  override byte down and the cached render states invalidated
  (`End_Render`), as it found the frame. The clip-plane switch (0x98) is
  only written here, through the render-state cache; skipped frames just
  don't turn clip planes off, and nothing found turns them on.
- **Visibility flags and skeleton poses** set while drawing the reflection
  are recomputed by the main view's own `Visibility_Check` and pose
  updates.
- **The reflection texture** belongs to the water object and is written
  only by this pass; the water draw (`RenderWater`) samples it. Skipping
  leaves the last reflection in it, and the reflection camera (`+0xFC`)
  keeps matching it.
- **Client-only.** All of this is drawing; nothing here feeds the
  simulation, so players with and without the DLL stay in sync.

### Design

- **Hook:** the `call` at 0x00049E5D becomes a call to the entry stub
  (a *call hook*: `E8` to the stub, the stub continues at the old callee).
  On a run frame `pf_enter` continues at `0x0007F25A`, unchanged; on a skip
  frame it continues at a `ret 8` thunk, which pops the same two arguments
  and returns to the same place. The hook is installed only when
  `ReflectionInterval` > 1, so 1 leaves the game's bytes untouched.
  Signature (unique in `game.dat` and `game820.dat`, site at offset 22):
  `FF B7 04 01 00 00 8B 0D ?? ?? ?? ?? FF 35 ?? ?? ?? ?? C6 45 FC 02 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC E8`.
- **Schedule:** the first call runs; after that one call in N runs and the
  N − 1 between are skipped. A call more than 250 ms after the previous
  one also runs (after loading, alt-tab, a device reset or a resolution
  change, which all stall the frame), so a reflection texture that was
  recreated is redrawn at once. Only main-thread calls are ever skipped.
- **Not detected:** a camera jump (a minimap click) or a texture recreated
  without a stall. Detecting them would mean reading the water object's
  and the camera's fields every frame; instead staleness is capped at N − 1
  frames (at most 3 frames, about 0.1 s at 30 FPS, with N = 4), after which
  the reflection is right again.
- **What the user may notice:** with N = 2 the reflection in the water is
  one frame (about 33 ms) behind the camera and the units; when the camera
  moves fast or jumps, the reflection briefly lags. Nothing else changes.
- **Settings:** `[Render] ReflectionInterval` 1–4, default 1 (unchanged
  game); values outside clamp to 1 or 4 and are logged. The log names the
  setting and whether the hook went in.
- **CSV:** `reflect_skipped` (skipped updates in that second) is appended,
  with `fx_batch_ms` (below); `reflect_ms` stays the marker's time, so it
  drops on skipped frames.

### Measurement fixes in the same build (0.4.0)

- Per-mesh markers go by their kind: labels starting `Rendering mesh<TAB>FXShader`
  count to `fx_mesh_ms`, `Rendering mesh<TAB>DX8Render` to `dx8_mesh_ms`;
  in the scopes CSV they're one row per kind (`Rendering mesh FXShader`),
  never cached by pointer. Tabs in labels become spaces and commas
  semicolons. A new appended column `fx_batch_ms` times
  `RenderFXShaderBatch`.
- `[Timing] Detail=1` (default) keeps everything; `Detail=0` doesn't
  install the draw counter and ignores per-mesh markers (no clock reads, no
  stack entry), so a run can measure their overhead. The section markers
  stay.

## Milestone 3: the shadow map (amendment, 2026-10-13)

The shadow-map pass was the biggest cost in the stress test (28–31% of
the heaviest frames, about 19 ms). The user asked for two switches: a cull
of shadow casters that can't affect the view, and `ShadowInterval=N`.
They test after both are built.

### How the game builds the shadow map (read-only research)

- **The manager** (`W3DShadowMapManager`, AotR `[0x00DC7A38]`, created by
  `setTimeOfDay`) owns two cameras: `[+0x00]`, the camera the pass renders
  with (0x408 bytes, a `CameraClass` subclass), and `[+0x04]`, a working
  `CameraClass`. Plus the render target (`+0x14`), an optional
  post-process target (`+0x18`) and its depth buffer (`+0x1C`).
- **The light camera is fitted to the view on every camera change.**
  `W3DView::setCameraTransform` (1.06 0x0008BE6B) calls
  `0x0007D37D(view camera)` (1.06 0x0007D9B5), which calls the fit
  `0x0007C276` (1.06 0x0007C8AE). The fit places the working camera,
  intersects its frustum's corners with the terrain, builds the hull of the
  ground the view can see, and sets the render camera's orthographic view
  plane, clip planes and transform around it. The same call hands the
  light camera to the terrain's `updateCenter`, so the terrain loads what
  every camera needs.
- **Casters are already culled to that frustum.** `UpdateShadowMap`
  (0x0007D5C9) renders the whole scene through `WW3D::Render(scene,
  rinfo(render camera))` with the shadow-map flag raised and a `Shadow.fx`
  `_WriteColor` method pushed; `RTS3DScene::Render` runs `Visibility_Check`
  with the light camera, so objects outside the fitted light frustum aren't
  drawn. A caster just off-screen whose shadow falls into the view is
  inside it, which is what the fit is for.
- **The shaders get the shadow map each frame from scratch.** Inside the
  `RenderViews` marker `W3DDisplay::draw` calls `0x0007BA74` (1.06
  `rva0007C18E`), which builds the world-to-shadow matrix from the render
  camera (`projection * view`, scaled to texture space) and passes it with
  the shadow texture to `SetShadowMapInfo(0, &matrix, texture)` (the call
  at **0x0007BB37**). After the views, `0x0007AF59` sets shadow map 0
  back to an identity matrix and no texture.

So **the cull the user described is already what the game does**: the
light frustum is refit to the view footprint every camera move and the
shadow pass culls against it. What is left is the slack between the hull
and the light-space rectangle around it. Trimming that needs, per object
and per frame, the object's bounds pushed along the light direction against
the view frustum: floating-point geometry the plugin avoids, plus game
virtuals called from inside the pass; and it changes which casters are
drawn, so "no visual change" couldn't be guaranteed without far more work.
**No cull switch is built.** The cost of the pass is drawing again every unit
mesh the view can see (about 4,800 FX meshes in the stress test, each a draw
call with its own skinning constants), plus the skeleton updates, which are
counted in the shadow pass because it is the first pass of the frame. To
measure this, 0.5.0 counts the per-mesh markers in each pass.

### `[Render] ShadowInterval=N`

What else the pass does, and why skipping it is safe once the matrix is
handled:

- `On_Frame_Update` isn't run in the shadow pass (`Customized_Render`
  skips it while the shadow-map byte is set), so no per-frame object update
  depends on it.
- Visibility flags and skeleton poses set in the pass are recomputed by the
  passes after it; `WW3D::FrameCount` still rises with each remaining
  `End_Render`; the flag, the rendering method and the render target are
  restored at the end of the pass.
- The render targets are written only by this pass and read only through
  shadow map 0. Releasing and re-acquiring them (size change, reset) also
  happens only inside the pass, so a skipped frame never sees a released
  texture; the next run does the reset.
- **The matrix must stay with the map it describes.** The render camera
  is refit whenever the view camera moves, and `0x0007BA74` builds the
  matrix from it every frame. So on a skipped frame the matrix from the
  last run is put back in place of the new one, just before
  `SetShadowMapInfo` is called; the texture argument is untouched (the
  same object every frame), and so are its reference counts.

Design:

- Two call hooks, installed together or not at all (group `shadow`):
  - `UpdateShadowMap`'s only call, at **0x00049DF5** in `W3DDisplay::draw`
    (`thiscall`, one argument, `ret 4`); on a skip frame `pf_enter`
    continues at a `ret 4` thunk. Signature (site at offset 16):
    `FF 35 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? C6 45 FC 01 E8 ?? ?? ?? ?? 8D 8D A8 FE FF FF 88 5D FC E8`.
  - The `SetShadowMapInfo` call at **0x0007BB37** in `0x0007BA74` (never
    skipped): after a run it saves the 64-byte matrix the call receives;
    after a skip it copies the saved one over it. Signature (site at
    offset 12): `8B 0D ?? ?? ?? ?? 8D 45 C4 50 6A 00 E8 ?? ?? ?? ?? 5E 83 C5 78 C9 C3`.
  Both are unique in `game.dat` and `game820.dat`; no branch lands inside
  either `call`. With `ShadowInterval=1` (the default) neither is installed.
- The schedule is the reflection's: the first call runs, then one in N; a
  call more than 250 ms after the last one runs too.
- **What may look different with N = 2:** units' shadows trail them by one
  frame (about 33 ms), and when the camera scrolls, a strip at the leading
  edge of the screen can be without shadows for one frame (the stored map
  covers the old view). Shadows of anything that isn't moving stay exactly
  in place, because the matrix and the map always match.
- Client-only: nothing here feeds the simulation.
- CSV, appended: `shadow_skipped` (per second), and per frame
  `shadow_meshes`, `reflect_meshes`, `view_meshes` (per-mesh markers
  inside each pass, counted at any `Detail`).

### Result and 0.5.1 (2026-10-13)

`Test Replay`, the same stretch with both intervals at 2 and at 1: frame
work 26.7 → 20.7 ms in the heaviest part (−22%), all of it from the shadow
pass (10.7 → 4.7 ms); views, logic and skeleton unchanged; exactly half the
redraws skipped. FPS stayed at the game's 30 cap. The map has no reflective
water, so `ReflectionInterval` did nothing there.

The user saw building ghosts' shadows trail the cursor. 0.5.1 draws the
shadow map every frame while a building is being placed:

- `InGameUI::placeBuildAvailable` stores the template being placed at
  `+0x53C` (`m_pendingPlaceType`, NULL when nothing is placed) and
  `setScrolling` keeps `m_isScrolling` at `+0x7F8` (same in 1.06, AotR and
  ROTWK 2.01). `TheInGameUI` (AotR VA 0x00DE4830) is read from two callers
  of `TheInGameUI->placeBuildAvailable(0, 0)` (vtable +0xDC), which must
  agree; the offsets come from the operands in `placeBuildAvailable` and
  `setScrolling`. All four signatures are unique in `game.dat` and
  `game820.dat`. Nothing is hooked or called: the shadow gate reads two
  fields on the main thread.
- A held frame runs the shadow pass and restarts the interval from it.
  CSV, appended: `shadow_full_rate` (held redraws per second).
- Scrolling: `[Render] ShadowFullWhileScrolling=1` holds the full rate
  while the camera scrolls too. Default 0: players scroll constantly in
  battles, so it would give back the saving exactly where it matters, for
  a one-frame strip at the screen edge.

## Milestone 4: batching identical units (amendment, 2026-10-13)

### How the FX mesh path draws a unit

All unit meshes go through one function, the FX shader batch renderer
(AotR 0x00173BEB, `thiscall`, two arguments: a sorted array of 32-byte
records `{ mesh, method count, rendering methods[...] }`):

1. **Groups.** Consecutive records form a group while the meshes pass the
   game's "same batch" test (0x00149430: same geometry and material) and
   share the same chain of `FXShader::RenderingMethod`s. The array is sorted
   beforehand (introsort at 0x00173F23), so identical meshes are already
   adjacent; sorting is not a lever.
2. **Per group** (marker `RenderFXShaderBatch`): the method chain is linked
   (0x0016815D), the first mesh's light environment is set
   (0x0014A080 → `DX8Wrapper::Set_Light_Environment`), then
   `method->Begin(&passes)` (slot 2). `RenderingMethod::Begin` binds the
   material's textures (`ID3DXEffect::SetTexture`) and calls
   `ID3DXEffect::Begin(&passes, 6)`: shader and sampler state are not
   saved, render states are, and `End` restores them.
3. **Per pass** `BeginPass` (slot 3), then the meshes in chunks of
   `maxInstances` = `[[[mesh+0xC4]+0xBC]+0x24]`:
   - chunk of 1 (marker `Rendering mesh\tFXShader\t<name>`): the per-mesh
     render 0x0014A1F0 sets the mesh's world/bone transforms and other SAS
     values, commits and draws: one draw call per mesh per pass;
   - chunk of N > 1 (`Rendering %d meshes`): each mesh's 48-byte world
     matrix goes into an array handed to the SAS object (0x0014CD78), and one
     draw covers N meshes. This is the game's own instancing: the geometry
     is built with N copies, copy i bound to joint i, so it only works for
     rigid meshes that were replicated at load. Units are skinned or carry
     per-unit bones and always take the chunk-of-1 path.
4. `EndPass` (slot 5), `End` (slot 6), the light globals are restored
   (0x00149550) and the chain unlinked (0x001681B0).

### Where the time goes (0.3.0 stress test, Detail=1, per heavy frame)

| minute | groups | meshes drawn | group time | per-mesh time | per group | per mesh |
|---|---|---|---|---|---|---|
| 3:00 | 677 | 20,730 | 19.2 ms | 14.4 ms | 7.0 µs | 0.69 µs |
| 13:00 | 2,294 | 8,661 | 20.5 ms | 10.9 ms | 4.2 µs | 1.25 µs |

The FX batches are about a third of the 62 ms stress frame (shadow,
reflection and view passes together). Of that, per-mesh work is
0.7–1.3 µs per mesh (constants, commit, draw, through DXVK), and the
per-group work (method link, light environment, effect
`Begin`/`BeginPass`/`EndPass`/`End` with its render-state save and restore)
is 4–7 µs per group. The game's instanced path is nearly unused (under 2
meshes per frame).

### Options

1. **Merge neighbouring groups that use the same rendering method.** When
   group k+1 has the same single method object, a single pass and the same
   light environment as group k, the `EndPass`/`End` of k and the
   `Begin`/`BeginPass` of k+1 are redundant. Saving: up to the per-group
   cost of every merged group (4–7 µs each); the number of such pairs is
   unknown: methods are per material, so neighbouring groups may rarely
   share one. Risk: render states set between the two (for example by the
   per-mesh render) are no longer reset by `End`/`Begin` for the second
   group; textures and lights are equal by the merge rule. Needs the four
   method slots wrapped (vtable patch, pass-through elsewhere) and the
   batch function wrapped to flush a deferred `End`. Effort: medium.
   Client-only and render-only.
2. **Sort identical meshes together:** already done by the game.
3. **Real instancing of units:** per-instance bone palettes (stream
   frequency instancing or packed palettes) with replacement skinning
   shaders written by us, reproducing house color, lighting, shadow-map
   sampling and fog of the EA shaders, plus a new draw path in place of the
   per-mesh render. Saving: most of the per-mesh time (roughly 7–11 ms of a
   heavy stress frame). Risk: high (every unit's look depends on the new
   shaders); effort: large (weeks), and it can't be checked without
   playing. Not in this milestone.

### Decision

Measure option 1 first, because its value depends on one number the
existing data doesn't have: how many neighbouring groups share their
method and light environment. **0.6.0** adds that count (no change to
rendering):

- A probe on the per-group setup call (0x00173CB8, `call 0x0014A080`,
  signature `68 ?? ?? ?? ?? 8D 8D 8C FD FF FF E8 ?? ?? ?? ?? 8B 0B C6 45 FC 01
  E8 ?? ?? ?? ?? 8B 06 68 FF FF 00 00`, site at offset 22; unique in
  `game.dat` and `game820.dat`, nothing branches into it). It reads, from
  the registers saved by the stub: the first mesh (ECX), the record (EBX),
  the method (ESI) and the group end (`[EBP+8]`), and the mesh's light
  environment (`[mesh+0xC8]`). Read-only.
- A group is single-pass when its mesh markers equal its record count.
  Neighbours in the same batch (no other marker in between, same pass) are
  counted as mergeable when both have one method, the same method object,
  and the first is single-pass; `fx_merge` also needs the same light
  environment, `fx_merge_anylight` doesn't.
- CSV, appended, per frame: `fx_groups`, `fx_merge`, `fx_merge_anylight`.
- `[Render] Batching=0|1` is read and logged; 1 does nothing yet.

Go/no-go for the merge (0.6.x behind `Batching=1`): `fx_merge` at least a
quarter of `fx_groups` in a heavy battle, which is worth about
`fx_merge × 5 µs` (several ms at 2,000 groups). Below that, it's not worth
the visual risk and batching moves to option 3 as its own project.

### B1 result: the merge is no-go (2026-10-13)

0.6.0 skirmish stress test (Númenor, unlimited money, 751 s in game, up to
1,119 animated units on screen; both intervals 2, Detail=0):

| | groups per frame | `fx_merge` | `fx_merge_anylight` |
|---|---|---|---|
| whole game | 474 | 0 (0%) | 27 (5.6%) |
| heaviest 10% of seconds | 1,433 | 0 (0%) | 112 (7.8%) |
| 10% of seconds with most groups | 1,545 | 0 (0%) | 190 (12.3%) |

No two neighbouring groups ever share a light environment (the CSV
rounds per-frame counts; `fx_merge` never reached 1% of the groups in any
second): every object has its own, so the merge rule never holds. Ignoring
the lights, which would light the second group with the first group's
lights, reaches 5.6–12%, worth about 0.6–1 ms in the heaviest frames.
Both are far below the 25% gate. **B2 is not built.** `Batching` stays a
read-and-logged key.

The same run against the 0.3.0 stress test, at similar load (seconds with
300–500 animated units on screen): frame work 42.5 → 33.2 ms, 23.1 →
26.0 FPS; shadow 11.9 → 5.6 ms, reflection 7.7 → 4.4 ms. At 700–900 units
the frame is equal (59.6 vs 59.4 ms): the halved passes are offset by
logic (5.3 → 10.4 ms) and the main view (16.2 → 20.7 ms) in this bigger
game. In its heaviest 10% of seconds the main view is 19.5 ms of 51.5:
the largest cost, then the shadow pass, logic, reflection and particles.

## Milestone 5: more than 30 FPS, safe in multiplayer (research, 2026-10-13)

Goal: render extra frames on the local client only, with logic, its
schedule, the commands and the network traffic exactly as stock, so a
player with the plugin stays in sync with players without it.
bfme2-accelerator's "logic spread" and render thread moved where frames
land relative to logic ticks; online that showed as teleporting units and
animations out of step. Everything below avoids touching that schedule.

### How the frame loop works (AotR RVAs; to be found by signature)

- **The cap.** `GameEngine::execute`'s wait (around 0x0023A196): after each
  `update`, the frame time is `1000 / (m_maxFPS × time scale)` ms
  (`m_maxFPS` = GameEngine +0x0C), and the loop spins
  `Sleep(0)`/`timeGetTime` (the `Sleep` call at 0x0023A1DD) until it has
  passed. `m_maxFPS` comes from `GameData.ini` `FramesPerSecondLimit` (30)
  and is then set by the network (below). A flag skips the wait while the
  time scale isn't 1 or logic is behind. Minimized:
  `Win32GameEngine::update` (0x0004181F) loops `IsIconic` + `Sleep(5)`.
- **Logic ticks are counted in client frames, not timed.**
  `GameEngine::update` (0x002325A0) increments the frame-in-tick counter
  (+0x34) and runs the logic tick when it passes a hard-coded 6, then
  resets it; +0x38 = `BfmeClientFramesPerSecond / BfmeLogicFramesPerSecond`
  = 30 / 5, and the interpolation ratio (+0x3C) = counter / 6, clamped to
  0..1 (0x0023256F). So running `update` more often runs logic faster: in
  single player the game speeds up, online the network has to hold it back.
- **`_bfme_shouldSkipClientFrame`** (1.06 0x00225BA6): once the 6 frames of
  a tick are used and the network says the next tick's commands aren't
  ready, client frames are skipped (no client update, no draw).
- **Per-frame client systems.** `BfmeClientFramesPerSecond` (30) converts
  seconds to client frames all over the client: radar blinks and beacons,
  notices and floating text, formation previews, drawable fades and
  blends, `TheW3DFrameLengthInMsec`, half-rate timers computed at startup.
  Camera motion and particle ages use `WW3D::Get_Sync_Time()` (time-based).
  **Logic reads it too** (castle packing, script actions, rates, some
  drawable fades started from logic, `SlowDeathBehavior` with
  `getFramesPerSecondLimit`), and several values derived from it are
  computed once at startup. It can't be changed to 60 safely.

### Multiplayer

- **The frame rate is negotiated.** In the Zero Hour code BFME2 inherits,
  each peer reports its achieved frame rate; the run-ahead update takes the
  minimum (at least 5, at most `FramesPerSecondLimit`), and a run-ahead
  command sets every peer's `m_maxFPS` to it at an agreed logic frame.
  BFME2's version is to be confirmed by signature in F0. So the stock
  frame rate is shared state: if extra frames made a peer's stock frames
  late, its reported rate would drop and **slow the game for everyone**.
  They must only use idle time and never delay a stock frame.
- **Client frames are checked against logic.** Every client update runs
  inside `BFMEDesyncCheck`: with `-verifyClientCRC` on the command line the
  logic CRC is taken before and after and a mismatch is reported. Client
  code that draws a logic random value desyncs; some client code does
  (`GetGameLogicRandomValue` in living-world client code), which is why
  extra frames must not run the client update.
- `GameCRCMismatch` is the in-game message when peers' CRCs differ;
  `-deepCRC`/`-binaryDeepCRC` exist for deeper checks.

### Design: render-only frames in the limiter's idle time

- Hook the limiter's `Sleep` call (a 6-byte `call [Sleep]`, rewritten as a
  call to our stub plus a nop). Each time the stock loop waits, the stub
  decides: if an extra frame is due (`[Render] MaxFPS`, 60 or 120) and its
  measured cost fits before the stock deadline with a margin, draw one;
  otherwise call `Sleep(0)` as stock. The stock loop re-reads the clock
  after the stub, so its cadence and every stock frame are untouched.
- An extra frame calls only the display draw: no `GameEngine::update`, no
  client update, no message stream, no input, no audio, no network. The
  game's frame counters (logic frame, client frame, +0x34..+0x44) and
  `m_maxFPS` are never written.
- Shadow map and reflection: an extra frame reuses both (the shadow gate's
  stored map with its matrix); only the main view and UI are redrawn.
- What an extra frame shows, by stage:
  - **F1 hold:** the last stock frame drawn again. No visual gain; it
    proves the pipeline (CRC, pacing, Present) is safe.
  - **F2 camera:** the camera extrapolated half a stock frame from its last
    two stock positions (an interpolated camera would need the next frame,
    which would add latency). Scrolling, zoom and rotation look smooth;
    units still step at 30, so they judder when the camera tracks them.
  - **F3 units:** each visible drawable's transform extrapolated the same
    way (positions move linearly within a logic tick, so it's exact there
    and overshoots by at most 1/12 tick at a tick boundary), and the WW3D
    sync time advanced for skeletal animation; both restored after the
    draw. Needs the drawable list and the transform interpolation found
    and proven side-effect-free. Particles, UI animations and decals stay
    at 30.
- **Fall back to stock** (stop extra frames for the session, logged) on:
  a logic random value drawn during an extra frame (probe on
  `GetGameLogicRandomValue`), any change of the logic or client frame
  counter during one, a stock frame finishing late because of one more
  than a few times, the window minimized or inactive, a stock frame cost
  over the budget, the shell (optional), or anything not found by
  signature.
- `[Render] MaxFPS=30` (default: no hook, stock). 60 and 120 opt in.

### Where it helps

An extra frame costs the main view plus UI. From the existing runs, one
extra frame per stock frame (60 FPS) fits in about half the seconds:
almost always under 100 animated units on screen, about two thirds of the
time in the `Test Replay` battle, and never in big battles, where stock
frames already take more than 33 ms. It smooths light and medium scenes
and scrolling; it doesn't make big battles faster.

### Risk

- Desync: low by construction (logic, schedule and traffic untouched),
  verified by `-verifyClientCRC`-style CRC checks around extra frames, the
  logic-random probe and replays.
- Pacing: medium; extra frames must never make a stock frame late (it
  would lower everyone's frame rate online). Budgeted and auto-disabled.
- Visual: F2 judder of units while tracking; F3 small overshoot at tick
  boundaries and 30 FPS particles/UI.

### Effort and milestones

1. **F0, measure (small).** Find the limiter, its `Sleep` call, the display
   draw and the run-ahead frame-rate handling by signature; hook the `Sleep`
   call as a no-op probe; CSV `idle_ms` (time spent waiting per frame) and
   the fraction of stock frames that would have room for an extra frame.
   Confirm the frame-rate negotiation.
2. **F1, hold frames (medium).** `MaxFPS=60`: draw the last frame again in
   idle time, with every fallback; verify CRC, pacing and Present (vsync)
   behaviour. Visually the same.
3. **F2, camera (medium).** Camera extrapolation. First visible gain.
4. **F3, units (large).** Drawable transforms and animation time.

### Testing

- Replays first: the same replay at `MaxFPS=30` and `60` must play to the
  same end; run with `-verifyClientCRC` and check for its report.
- Then a two-PC Network game (desktop with the plugin at 60, laptop
  without): watch for `GameCRCMismatch` or a desync message, teleporting
  units and the game slowing down; compare both logs' frame counts.

### F0/F1 as built (0.8.0-fps, branch `feat/perf-fps`)

Found by signature in `game.dat` and `game820.dat` (all unique):

| What | AotR | Use |
|---|---|---|
| limiter `call [Sleep]` | 0x0023A1DD | hook site (`call [import]` → `call stub; nop`) |
| `TheDisplay` | 0x00DE4418 | from the loading screen's `draw(); Sleep(100)` |
| `W3DDisplay::draw` | 0x0004B788 | vtable slot 12; checked before every extra frame |
| its `updateAverageFPS` call | 0x0004B7B5 | skipped in extra frames (keeps the game's frame-rate history) |
| draw's client frame, W3D time accumulator, frame-limit clock, frame-skip count, init bits, screenshot count | 0x00D98C8C, 0x00DC7580, 0x00DC7700, 0x00D98CB4, 0x00DC7704, 0x00DC7568 | saved and restored around an extra frame |
| `WW3D::Sync` time, previous time | 0x00DD1E0C, 0x00DD1E10 | the same |
| logic random seed | 0x00DA1CA4 | must not change during an extra frame |
| `-verifyClientCRC` flag, `TheGameLogic`, in-game test, `getCRC` | 0x00DE87C5, 0x00DE412C, 0x00041B60, 0x00225886 | with the flag set, every extra frame is CRC-checked |

Each extra frame: draw is called only when the next game frame isn't due
for at least the measured extra-frame cost plus 2 ms (scheduled at
1/2 of the frame for 60, 1/4, 2/4, 3/4 for 120); shadow map and reflection
reused; the draw's own time state restored after. The draw advances W3D
time by client frames elapsed, which is zero in an extra frame, so
animations and particles hold. Off for the session (logged) on: a changed
logic seed, a changed GameEngine word (+0x0C..+0x5F: frame limit,
frame-in-tick counter, ratio, metrics), a changed client frame, a changed
logic CRC with `-verifyClientCRC`, five game frames made late, the draw
not being the checked function, or a missing site. Paused (not off) while
minimized or a screenshot is pending. CSV, appended: `idle_ms`,
`extra_frames`, `extra_ms`, `extra_late`.

What F1 shows: the last game frame drawn again. No visual gain by itself;
it proves the pipeline (no logic change, no late frames, Present/vsync
behaviour) before anything moves.

### F2/F3 as built (`[Render] FrameSmoothing`, default 1)

The camera isn't rebuilt by the draw: `W3DView::setCameraTransform`
(builds the matrix, `camera->Set_Transform`) is called only from the
view's own code (15 callers, all in W3DView), never from
`W3DDisplay::draw`; extra frames skip the client update, so they render
whatever the camera holds. Instead of finding where the view writes it,
an extra frame sets it itself:

- **Camera (F2, FrameSmoothing=1).** `TheTacticalView` (0x00DE447C) →
  `m_3DCamera` at +0x104 → its transform (RenderObjClass +0x18, Matrix3D).
  Both `camera->Set_Transform` calls in `setCameraTransform` confirm +0x104
  and the vtable offset 0x54 (slot 21). At the first wait of every game
  frame the camera's transform is recorded with that frame's start time;
  an extra frame writes `cur + (cur − prev) × (now − start) / (start − prev
  start)` through the camera's own `Set_Transform` (which also marks its
  frustum stale), draws, checks the camera still holds what was written
  (if the draw changed it, camera smoothing turns off for the session) and
  writes the recorded bytes back. Extrapolation, not interpolation: the
  current frame is already on screen, so going between the last two would
  step backwards.
- **Units (F3, FrameSmoothing=2).** Probes on `HLodClass::Set_Transform`
  (0x0019AE20) and `~HLodClass` (0x0019E6A0); the destructor writes the
  HLod vtable 0x007EC7F0, whose slot 0x54 is that `Set_Transform` (checked
  in both executables). Each game frame's last transform per HLod goes into
  a 4096-entry table keyed by object; the destructor removes it, so a dead
  object is never touched. An extra frame moves only HLods set in both of
  the last two game frames, still holding the recorded transform, and
  moving less than 60 world units per frame (more is a teleport or a
  reused address); then puts every one back and checks it took. CSV:
  `extra_units` (moved per extra frame).
- Animation frames are not advanced: units glide at 60 but their skeletal
  animation, particles and UI still change at 30.
- **Maths.** `fpsmath.c` is the only unit with floating point: SSE scalar
  code (`-msse2 -mfpmath=sse`, no vectorizing), called only from the
  limiter's `Sleep` call, an ABI call boundary where the x87 stack is empty
  and no SSE register is live. It never touches the x87 unit (so the
  game's 24-bit x87 precision is untouched) and saves and restores MXCSR.
  Lerp of all 12 components, then Gram-Schmidt on the axes. Render-only:
  the result is written only into transforms that are put back before the
  game's next frame.
- The checks of F1 still run around every extra frame (logic seed, engine
  words, client frame, `-verifyClientCRC` CRC), and they also cover
  anything the camera or unit writes might disturb.

### With instancing (0.8.0, merged)

Instancing runs in extra frames too. Its hooks draw what the frame's
render pass built: the group's records, the mesh transforms (+0x18) and
the bones from each container's HTree. With FrameSmoothing=2 an extra frame
moves an HLod through `Set_Transform`, which marks its sub-objects dirty;
the HLod's own render in that frame rebuilds them and the HTree before its
meshes reach the FX batch, so instancing copies the moved bones exactly
as it copies the game's in a normal frame. Per extra frame, `fps.c` tells
`inst.c` (`pf_inst_set_extra`): no counts (the `inst_*` columns stay per
game frame) and no verify window or sample (no off-screen draw or GPU
readback in an extra frame).

Merged numbering: sites 16–17 are instancing's (`inst_mesh`, `inst_dip`),
18–21 the extra frames' (`fps_wait`, `fps_avg`, `hlod_set`, `hlod_dtor`);
call-hook kind 2 is `call [edx+disp32]` (instancing), 3 `call [import]`
(the limiter); CSV flags `PF_HAVE_INST` 1024, `PF_HAVE_FPS` 2048, and the
columns 0.6.0's, then `inst_*`/`verify_*`, then `idle_ms` … `extra_units`.

### Where the frame's time goes (0.8.1)

The first 0.8.0 session in a game (MaxFPS=120) drew no extra frames: the
limiter waited about 0.3 ms per frame while `GameEngine::update` took
1.5–6 ms of a 33 ms frame. About 25 ms passed between the frame start
(0x00DE4318) and the limiter's first `Sleep(0)`, outside `update`, so the
idle time was almost always under an extra frame's cost plus the 2 ms
margin. In menus the extra frames all fell at the end of the wait. Static
reading leaves two candidates for those 25 ms: `serviceWindowsOS` (the
message pump that `Win32GameEngine::update` tail-jumps to), and the code
between `update` and the limiter (view and network calls at 0x00239FEF).

0.8.1 measures them. Site 22 `pump` wraps `Win32GameEngine::serviceWindowsOS`
(0x00041A7D, group `PF_GROUP_PUMP`, on with timing). CSV flag
`PF_HAVE_PUMP` 4096 and four columns appended: `waits` (limiter `Sleep`
calls), `wait_ms` (time inside them), `outside_ms` (from `update`'s exit to
the first wait) and `pump_ms`. `outside_ms` minus `pump_ms` is the code
between the pump and the limiter. Extra frames still use only the
limiter's own wait; nothing else waits, so there is no other idle time to
draw in until that gap is known.

### One Sleep per frame (0.8.2)

The 0.8.1 run (MaxFPS=120, 478 in-game seconds) disproved that: `outside_ms`
0.2, `pump_ms` 0.2, `wait_ms` 22.7 per frame (13.0 in the heaviest tenth),
but `idle_ms` 0.1 and no extra frames. `idle_ms` is the span between the
hook's Sleep calls within a frame, so the game's first `Sleep(0)` returned
only near the deadline: the scheduler saw each frame once, 5–20 ms in. It
drew only when that one look was already past an evenly spaced point
(period × k / (extras + 1)) and the frame fit; in menus the look came at
0.3 ms, before the first point, and there was never a second. And the
expected cost followed the maximum and fell only when an extra frame was
drawn: seven extra frames during the load averaged 13.9 ms (one made a
game frame late), after which nothing fit and so nothing lowered it.

0.8.2 paces the extra frames inside the wait call, on the game's own clock
(`timeGetTime`, the one its deadline uses): for each next point it spins
(`pause`, no Sleep) until the point, rechecks every condition and the fit,
draws, and returns to the game's `Sleep` as soon as the next one wouldn't
fit before the deadline less the cost and the 2 ms margin. Of the points
already passed only the latest is drawn, so a heavy frame gets fewer extra
frames rather than two back to back. A spin gives up if the game's clock
hasn't reached the point within that time plus 2 ms of real time. The
expected cost is the recent maximum, falling by 1/32 per game frame toward
the average of what was measured (1/8 per sample); after 30 game frames
without an extra frame the average falls by 1/32 per frame too. The
deadline, the game's frame start and its own loop are unchanged. CSV,
appended: `sleeps` (the limiter's Sleep calls, per row), `budget_ms` (per
game frame: the time left before the deadline at its first wait, which
should be close to `wait_ms`) and `extra_cost_ms` (the expected cost when
the row closed).

### Late frames (0.8.3)

Correction to the above: the game's `Sleep(0)` loop does spin (0.8.2's
`sleeps`: about 4 million calls per second). 0.8.1's `idle_ms` of 0.1 was a
rounding bug: each sub-microsecond gap between calls was converted to µs,
truncated to 0, and only then summed; it is now summed in ticks. What kept
0.8.1 from drawing was the stuck cost estimate. Pacing inside the call is
kept: it spins like the game's own loop, and doesn't depend on the next call
coming in time.

0.8.2 drew about 55–93 extra frames per second in game, then went off at
about t=87 on the fifth late game frame of the session. The five were spread
over 45 s: one during the load, two in the first seconds of the game, two
later. They come from occasional extra frames far slower than the average
(DXVK compiling a pipeline or uploading a texture on first use), which no
estimate predicts. A late frame costs a few ms of one game frame; in
multiplayer it only slightly lowers the frame rate reported to peers. So:

- **Back off.** After a late, extra frames pause for 1 s; a repeat within
  10 s of the previous late doubles it, up to 8 s. The expected cost (the
  decaying recent maximum) is held while paused, so the slow frame's cost
  still sets the headroom when they resume.
- **Grace.** Lates within 10 s after the first game frame, or after a game
  frame gap over 250 ms (a load or a stall), back off but aren't counted.
- **Off for the session** only when lates persist: 3 counted lates within
  10 s (with the backoff that means late again right after each pause), or
  at least 6 counted lates in 60 s that are also over 1% of the game frames
  that had extra frames in that minute. The log line gives the reason.
- **Log.** Each late, at most one line per 2 s: how late, the extra frame's
  cost, the pause and the counted lates in the last minute.

### Effects at the game's rate, late frames, unit jitter (0.8.4)

The user's 0.8.3 test (MaxFPS=120, FrameSmoothing=2): more than 30 FPS for
a while, then 30 again; torches, construction dust and the other effects
ran fast; units mostly right, sometimes jittery. Logs and CSVs are in
`%TEMP%\bfme2perf-0.8.3-run1-0006`.

**Correction to F0/F1.** The W3D sync time does hold in an extra frame, but
not everything in the draw runs on it. Before any pass is drawn,
W3DDisplay's frame (AotR 0x00049CF8, called from `W3DDisplay::draw`) runs:

1. unless the game is paused (GameLogic +0x125), `TheDisplay->updateViews()`
   (vtable +0xA0, 0x0025BF2F): each view's `updateView`, `W3DView::update`
   (0x0008BCF2). Per call it moves the camera for follow, shake and scripted
   moves, refits the shadow light camera and updates the terrain under the
   camera when the terrain asks (terrain +0x37D4). It walks the drawables in
   view (`Drawable::draw`, the draw modules) only when the sync time
   advanced, which it doesn't in an extra frame.
2. `TheFXParticleSystemManager->update()` (vtable +0x28, 0x001F5123), which
   steps every particle system one frame per call. Its own
   once-per-client-frame guard (the client frame against +0x74) is only on
   with GlobalData +0xD45, which two setup paths set and a normal game
   doesn't. **This ran every particle effect up to 4 times per game frame
   at 120.**

The 0.8.3 log also has "the draw changed the camera" at about t=215: the
view update rebuilt the camera inside an extra frame, which turned camera
smoothing off for the rest of the session.

Two hooks (group `hold`, installed with MaxFPS > 30; extra frames stay off
without them):

| Site | AotR / 2.01 | Signature (`|` marks the site) |
|---|---|---|
| `views`: the `call [eax+0xA0]` | 0x00049D35 | `A1 ?? ?? ?? ?? 38 98 25 01 00 00 75 0F 8B 06 8B CE \| FF 90 A0 00 00 00 E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B 01 FF 50 28` |
| `particles`: the update's entry (steals `mov eax, imm32`) | 0x001F5123 | `B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 28 0F 57 C0 53 57 8B F9 F3 0F 11 47 60 A1 ?? ?? ?? ?? 33 DB 38 98 C2 09 00 00 74 14` |

Both are unique in `game.dat` and `game820.dat`; nothing branches into the
stolen bytes; the update is only reached through its two vtables (the W3D
manager's and its base's), never called directly. Indirect call hooks now
take any base register (`FF 90`..`FF 97` but `94`); the stub reads the
register the call had. In an extra frame both return at once (`ret`), on
the main thread only; in the game's frames they pass through. Before each
extra frame, `TheFXParticleSystemManager`'s vtable slot 0x28 (both read from
the `views` signature) must be the hooked function, or extra frames go off.
So particles and the view's own motion stay at the game's 30 per second;
FrameSmoothing still moves the camera and units in extra frames. This also
means extra frames no longer change any particle state (0.8.3's could finish
and delete particle systems early). CSV: `extra_held`, per row (2 per extra
frame).

**Late frames.** 43 lates between t=11 and t=536; off at t=536 by the burst
rule (3 counted lates within 10 s, 3 and 5 ms late). Two kinds:

- About ten extra frames of 70–105 ms (51–85 ms late), between t=195 and
  t=366, while the game's own frames in those seconds took at most 10–18 ms.
  DXVK 2.6.2 uses graphics pipeline libraries on this GPU, so a pipeline
  compile costs milliseconds, not 100. The view update with an extrapolated
  camera (shadow-light refit, terrain update) is the likely cause; it no
  longer runs in extra frames. `extra_max_ms` will show whether they remain.
- About 30 lates of 1–10 ms as the battle grew: extra frames of 13–29 ms
  where the usual was 5–10, against a decaying maximum that forgot a slow
  frame within 0.7 s and a fixed 2 ms margin.

Changes:

- **Expected cost:** the slowest extra frame of the last 64 game frames that
  drew one (about 2 s). After 30 game frames in which none fit, the slowest
  sample gives way to the decaying average, one per frame.
- **Margin:** 3 ms (2 ms, plus 1 ms for `timeGetTime`'s resolution) plus an
  eighth of the expected cost.
- **A late up to 4 ms** pauses 1 s and doesn't escalate. A larger one pauses
  1 s, doubling to 8 s when it comes within 10 s of the previous late.
- **Off for the session:** late again right after the 8 s pause (5 counted
  lates in a row, each within 10 s of the last), or counted lateness over
  1% of the last 60 s (600 ms). Run through these rules, the 0.8.3 lates
  (paired with their CSV seconds) reach at most about 410 ms in a minute and
  no chain longer than 3: extra frames would have stayed on.
- Log: each late line gives the expected cost and the late ms in the last
  minute. CSV, appended: `extra_max_ms` (slowest extra frame), `late_ms`
  (lateness summed), `extra_held`.

**Unit jitter.** After camera smoothing went off (t≈215), units moved at
up to 120 per second while the camera and terrain stepped at 30: judder
whenever the camera moved. Holding the view update removes that cause.
The other is F3's overshoot: `GameEngine::update` counts 1..6 within a
logic tick (ratio c/6; at the 7th the tick runs and the count restarts at
1), so drawables move linearly through each tick's 6 frames and turn or
stop only after the frame with c = 6. Extra frames after that frame go on
along the old segment, up to 3/4 of a frame's step, and the next game frame
snaps back: one game frame in six, at 5 Hz.

**FrameSmoothing=3** (a new value of the existing key, not the default):
units one game frame (33 ms) behind, interpolated. In the game's own draw,
at the particle update's entry (after the view update set this frame's
transforms, before the shadow pass), each unit that moved is set to its
previous transform; when `W3DDisplay::draw` returns it is put back (a new
wrapped hook `display_draw` on 0x0004B788, group `lag`, installed only with
3). Transforms set in between (sub-objects placed from the earlier pose)
aren't recorded as the game's. Extra frames show prev + (cur − prev) × t.
No overshoot, at a cost: models are 33 ms behind the camera, the terrain,
their health bars and selection rings, attached effects and the positions
projectiles leave from. One *logic* tick behind (200 ms) was not built:
models would trail everything drawn from logic positions by 200 ms.
Render objects only, and every one is put back before the draw returns.

### Shadows and reflections in extra frames (0.8.5)

The user saw shadows trail their units. `ShadowInterval=1` redraws the map
on every game frame, but extra frames still reused it, so at 120 FPS
shadows moved at 30 while units moved at up to 120.

`[Render] ExtraFrameShadows=1` (default 0) lets the `UpdateShadowMap` call
run in extra frames instead of returning: the pass draws the scene from the
light camera with the units where F3 put them (the camera F2 moved isn't the
light camera, which stays fitted to the last game frame's view, as on a
game frame that didn't move the camera). Everything else is as on a game
frame that draws the map: the pass restores its flag, rendering method and
render target, the frame's visibility and poses are recomputed by the main
view, and nothing it touches feeds logic; the extra frame's checks (logic
seed, engine words, client frame, `-verifyClientCRC`) still run around it.
The one piece of plugin state it changes is the stored shadow matrix:
after such a pass the extra frame's `SetShadowMapInfo` matrix is stored as
the one matching the map (as after a game frame's redraw), so a later game
frame that skips the redraw (`ShadowInterval` > 1) puts back the matrix of
the map the extra frame drew. The interval schedule only counts game frames.

**Cost.** The extra frame's cost is measured around the whole draw, so the
expected cost (and with it the pacing and the late-frame policy) includes
the shadow pass with no other change. In the 0.8.3 run a shadow pass took
1.5, 3.3 and 4.5 ms per redraw in the light, medium and heavy thirds of the
game, against extra frames of 4.0, 5.9 and 7.6 ms: about 40–60% more per
extra frame, which at these budgets means about one extra frame fewer per
game frame (120 → 90 FPS in light scenes, 90 → 60 in heavy ones).

**Reflections** were reused the same way. `ExtraFrameReflections=1`
(default 0) lets the reflection update run in extra frames; it builds its
mirrored camera from the view camera the extra frame drew with, and
restores the projection override byte and the render target, as on a game
frame. Nothing else changes; the user's maps so far have no reflective
water, so it is untested in game.

CSV, appended: `extra_shadows` and `extra_reflections`, the passes drawn by
extra frames per row (with the option on, one per extra frame).

### Units shaking while they move (0.8.6)

The user's 0.8.5 test: effects at the right speed now, but every moving
unit shakes a little (not teleporting). Logs in
`%TEMP%\bfme2perf-0.8.5-run1-0151` (FrameSmoothing=2, ExtraFrameShadows=1).

**Cause: extra frames were timed from when the game's frame started, not
from when frames are shown.** An extra frame put units (and the camera)
`(now − frame start) / dt` of a game frame's step ahead, with `now` the
moment its draw began. But the game's own frame is presented at the end of
its update, `GameEngine::update` taking a median 4.6 ms (3.5–7.8 ms) in
this run, and an extra frame is presented when its own draw returns, 2.9 ms
(median) after it began. With the old points at 1/4, 2/4 and 3/4 of the
frame, a typical game frame placed the unit, in percent of a step against
the time it reached the screen: 0 % at 4.6 ms, 25 % at 11.2, 50 % at 19.6,
75 % at 27.9 and 100 % at 37.6 (the next game frame): 3.8, 3.0, 3.0 and
2.6 % per ms instead of a steady 3.0, so units sped up and slowed down 30
times a second, by more when the update took longer. The camera had the
same error. The extra-frame count also changed often (median 2 per game
frame, 28 % of the seconds under 1.5): the expected cost, the slowest of
the last 2 s (median 7.3 ms against an average of 2.9), let fewer fit, and
with fewer than three the fixed points left uneven gaps.

Changes:

- **Timed by when frames are shown.** A game frame counts as shown at its
  first wait (right after its draw presented), on a microsecond clock
  (QPC). An extra frame is placed for when it will be shown, its start plus
  the average extra-frame cost: f = (that time − the game frame's) / the
  game's frame time, so the unit is drawn exactly where a steady motion
  between the game's frames puts it, however long the update took. Same
  for the camera and for FrameSmoothing=3.
- **Evenly spaced.** The extra frames of a game frame are planned at its
  first wait: n of them shown at period × j / (n + 1) after it, started the
  average cost earlier, n the most that fit (back to back too). When the
  game frame was shown so late that the even points don't fit before the
  deadline, the same n squeezed into the time left (at least 70 % of the
  even span), else one as late as fits.
- **No flicker in the count.** It drops at once but rises only after the
  larger count fit for 8 game frames in a row.
- A measured cost of 0 µs counts as 1 µs (0 meant "not measured").
- CSV, appended: `gap_avg_ms`, `gap_max_ms` (between shown frames, the
  game's and extra ones), `disp_err_ms` (how far an extra frame's shown
  time was from the one its positions were computed for), `rate_changes`
  (game frames whose extra-frame count changed), and `unit_steps`,
  `unit_uneven` (units moving two frames in a row, and those whose step
  changed by over a quarter: what FrameSmoothing=2 amplifies and 3 doesn't).

What stays at 30 per second: drawables' health bars, selection rings and
attached effects follow the game's positions and step once per game frame.
With FrameSmoothing=2 a moving unit runs up to 3/4 of a step ahead of them
before they catch up; with 3 it trails them by 1 to 1/4 of a step.

### Back at 30 in big battles, and shaking there (0.8.7)

The user switched to FrameSmoothing=3: no shake while extra frames ran,
but later the rate fell to 30 and some units shook. That session ran 0.8.5
(started before 0.8.6 was installed); logs in
`%TEMP%\bfme2perf-0.8.5-run2-0215` (ShadowInterval=1, ExtraFrameShadows=1).

**Why 30: no idle time left, not a shut-off.** No check fired. As the
battle grew, the game's own frame took a median 4–7 ms in minutes 1–4,
12–15 ms in minutes 6–10 and 19–30 ms in minutes 11–19 (up to 214–274
animated objects on screen), leaving 4–14 ms before the deadline, while an
extra frame (with its own shadow pass) took 10–16 ms: extra frames fell
from about 45 per second to 1–10, and 5–11 lates a minute (extra frames
running long under that load) paused them on top. The shadow pass was a
large part of the game's own frame (5–14 ms) since `ShadowInterval=1`
draws it every game frame.

Change: with `ExtraFrameShadows=1`, a game frame right after an extra frame
drew the shadow map reuses that map (a few ms old, with the extra frame's
units, at most a quarter step behind) instead of drawing its own, as a
skipped redraw with the matching matrix; it counts as a redraw for
`ShadowInterval`. While extra frames run, the game's frames save their
shadow pass, which is the time the next extra frame needs. With none, the
game frame draws its own as before.

**Why units shook at 30: frames counted by the limiter's wait.** A game
frame that overruns its 33 ms never calls the limiter's `Sleep`, so it
never reached the wait hook, and the plugin numbered frames there. In
minutes 14 and 18, 29 % and 36 % of game frames didn't wait (5–8 % in
others). Two frames' `Set_Transform` records then shared one number: the
later overwrote the earlier and "the last frame" became two frames back, so
with FrameSmoothing=3 the game's draw showed a unit one step behind in one
frame and two steps behind in the next (FrameSmoothing=2 extrapolated from
a doubled step the same way).

Changes:

- **Frames are numbered by the limiter's frame start** (`[start]`, which
  the limiter sets for every frame, waited or not), read when a unit is
  recorded, at the draw and at the wait.
- **The delay follows the extra frames.** FrameSmoothing=3's one-frame
  delay is full while extra frames are drawn (gaps of up to 8 game frames
  keep it); after 8 game frames without one (heavy load, a pause after a
  late frame) it eases out a quarter step per game frame to none, so units
  are drawn exactly as the game has them, and eases back in when extra
  frames resume. Extra frames go from where the game's last draw showed a
  unit to where the next will. Once extra frames are off for the session
  nothing is delayed at all.
- FrameSmoothing=3 is now the default (code and shipped INI).

### Production clocks flashing on building buttons (0.8.9)

With extra frames on (0.8.5 run, MaxFPS=120), the progress clock on a
building's command buttons (a unit or hero being recruited) flashed and was
hard to see; the one on hero portraits was fine.

**Why: the clock is a one-draw request.** The building's command buttons
are window-system push buttons. Each game frame the control bar's update
asks for the clock on a button: it sets the button data's first byte (the
window's user data, window +0x2C) to 1 (clock) or 2 (remaining-time clock),
with the percentage at +4 and the color at +8. The push button image draw
(`W3DGadgetPushButtonImageDraw`, RVA 0x000A5112) draws the clock through
TheDisplay and then sets the byte back to 0. The extra frame's draw runs
the same window draw, finds no request, and draws the button without its
clock: at 120 FPS the clock was on screen one frame in four. The byte is
set by two small setters (clock and remaining-time clock, called from the
control bar's update) and tested only in this draw (the test sequence
matches once in the image), the only caller of the clock's display call
besides Apt's `TimerOverlay`.

Hero portraits, the spellbook's timers and the Apt command buttons
(`StrategicHUD::CommandButtonMovieClip`) are Apt movies: their
`TimerOverlay` custom render reads a fraction the object keeps
(`AptPlayer` timer map, +0xB8), so they draw the same in every frame. The
Apt player's own advance is in the client update, not its draw, so extra
frames don't move Apt animations either.

Affected, all through the push button clock: units and heroes in a
building's production, upgrades and research in progress, and the
remaining-time clock of recharging powers on command buttons.

**Change: the game's draw notes, extra frames ask again.** A probe on the
image draw's entry (`button_draw`, group `PF_GROUP_BUTTONS`, only with
extra frames on): in the game's own draw it notes each window with a clock
request and its kind, for that game frame (numbered by the limiter's frame
start); in that frame's extra frames, if the window still has the same
button data and the request is clear, it sets the kind back before the
draw, which draws the clock with the percentage and color the control bar
set and clears the request as the game's draw did. After an extra frame's
draw, a request it made that wasn't drawn is cleared, so the game's next
update finds the buttons as its own draw left them. Nothing the logic or
the CRC reads changes: the byte is client UI state, set by the client
update and cleared by the draw. A new game frame drops what the last one
noted; up to 64 buttons per frame.

The site is checked before it is enabled (`pf_btn_find`): the function
reads the button data through a `mov eax, [ecx+off]; ret` getter, tests
the byte at the data's start for 0 and 1, and clears it after drawing;
if any of that doesn't match, the hook isn't installed and the log says
extra frames draw the buttons without their clock. The CSV's new
`extra_clocks` column counts clocks drawn again in extra frames per row:
non-zero while something is in production.

### Still flashing: the command bar's buttons take another draw (0.9.1)

0.9.0 (0.8.9's fix) ran with the hook applied and logged, extra frames in
50 of 58 rows, and `extra_clocks` 0 in every row while the user watched
production clocks flash: no clock was ever noted.

**Why: the hooked function was one of three.** The window system draws a
push button through `W3DGadgetPushButtonDraw` (RVA 0x000A5A3E, the only
function the gadget table and the button's thiscall thunk point to). It
picks one of three draws, all `(window, instance)` cdecl:

- a check-like button (button data +0x20 set): RVA 0x000A4A25;
- otherwise, with window +0x84 set and instance +0x0A bit 0x20 clear:
  0x000A4585;
- else 0x000A5112 (the image draw 0.8.9 hooked).

Each of the three draws the clock the request asks for and clears it
(`mov al, [esi]` / `mov cl, [esi]`, then `mov byte [esi], 0; call
setUserData`), and each is called only from the dispatcher. No clock was
noted at the image draw, so the building buttons' clocks go through one of
the other two; check-like buttons are what Zero Hour's control bar uses
for command buttons, so most likely 0x000A4A25. Either way the hook never
saw them. 0.8.9's
claim that the request is tested only in the image draw came from a
signature with the image draw's registers (`cl`); the other two use `al`.
Those three draws hold the only three `mov byte [esi], 0; call
setUserData` clears in the image, and the display's clock calls
(0x000A422F clock, 0x000A427E remaining time, 0x000A42CD with images) have
no other push-button callers; their other callers are Apt render
callbacks that compute the fraction from fields that persist.

**Change.** The `button_draw` probe is on the dispatcher's entry (the whole
function as its signature, 6 bytes stolen: `push ebp; mov ebp, esp; sub
esp, 0x10`), so every push button draw, whichever of the three, is noted
and asked again. `pf_btn_find` follows its three calls and checks each draw
reads the data through the `mov eax, [ecx+off]; ret` getter (all three the
same offset), tests the byte and clears it; any mismatch leaves the hook
off and says why in the log.

**Proof either way.** Two probes on the control bar's request setters
(`GadgetButtonDrawClock` 0x00329ED8 sets 1, `GadgetButtonDrawInverseClock`
0x00329F0C sets 2; the command bar's production uses the second) count the
requests, compared by window pointer only. New CSV columns at the end of
the row: `clock_requests` (requests), `clocks_noted` (clocks noted at the
game's button draws) and `clock_unseen` (game frames with an extra frame
in which a window the control bar asked a clock for had none noted: not
drawn that frame, or drawn through a path that isn't hooked). The log says
once each when the first clock is noted, when the first is drawn again in
an extra frame, and the first time a request isn't noted.

### Still flashing 90% of the time: the request is for the next draw (0.9.2)

The user ran 0.9.1 (MaxFPS=120, Detail=1; logs in
`%TEMP%\bfme2perf-0.9.1-run1-1033`): clocks still flashed, looked right "a
few times", more often while the camera moved. 804 rows; 154,614 clock
requests, 83,857 noted at the game's draws, `extra_clocks` 280 for 15,171
extra frames. Per second, by extra frames per game frame:

| Extra per game frame | Rows | Requests noted at the game's draw |
|---|---|---|
| about 0 | 353 | 85% |
| about 1 | 211 | 19% |
| about 2 | 37 | 3% |
| about 3 | 38 | 0% |

The more extra frames, the fewer clocks the game's own draw found.

**Why: GameClient::update draws first, then updates the UI.** In
GameClient::update, `TheDisplay->draw()` (RVA 0x00248869) runs before
`TheInGameUI->update()` (0x002488AE, the W3DInGameUI vtable +0x28 jumps to
InGameUI::update 0x002A1F4D), which calls `TheControlBar->update()`
(TheControlBar at 0x00DE7744, vtable +0x28 = 0x0031FC08), whose button
refresh calls the request setters. So a request is made after the frame's
draw, for the next draw: without extra frames that is the next game
frame's. With extra frames the first extra frame drew the clock and
cleared the request; the other extra frames and the next game frame's draw
found none, and 0.9.1 only gave back what the game's draw had found
(nothing). One frame in four showed the clock at 3 extra frames per game
frame; with the camera moving, frames cost more, fewer extra frames fit and
more game draws found the request, so it looked right more often.

**Change: extra frames leave the requests as they found them.** At the
push button draw's entry an extra frame notes each request it finds (and
its kind); after its draw it sets the requests it cleared back. Every
extra frame draws the clock at the percentage the control bar set last,
and the game's next draw finds and clears the requests as it would have
with no extra frames: the UI state after the extra frames is exactly what
the game left. The game's own draws are only counted (`clocks_noted`);
nothing is noted from them any more.
## Milestone 7: three cuts in the shadow-map pass (0.9.3)

From the 2026-10-14 research (`%TEMP%\bfme2perf-research-2026-10-14.md`):
`W3DShadowMapManager::UpdateShadowMap` (0x0007D5C9) draws the scene in the
same pass mode (0) as the main view. It only pushes a shadow rendering method
and raises the shadow-map byte (0x009D1E44). So the whole main-view machinery
runs in the shadow pass, including three pieces of work that change nothing
in the map. Each cut has its own `[Render]` key. Each was proven to leave the
map as the game draws it, and ShadowPassVerify re-checks that in game.

### ShadowBatchIgnoreLight (default 1)

**What the game does.** `FXShaderRenderer::PerformRendering` (0x00173BEB)
ends a group where the next record's mesh fails `Is_Rendering_Identical`
against the group's first. That test (0x00149430; one caller, the call at
0x00173C2B) compares three things by value:

- the light environment (`mesh+0xC8`, `LightEnvironmentClass` equality at
  0x0013F3B0: output ambient and every light);
- the fog colour at `mesh+0x314` (the getter at 0x00148C50; this is the
  "colour vector" of the 0.7.0 notes, not house colour);
- the opacity (vtable +0x64).

Per group, the setup at 0x0014A080 takes the first mesh's light environment,
fog colour, opacity (it becomes `WW3D.OpacityOverride`) and inherited render
data. `renderOneObject` gives an object its own light environment whenever
any of these holds:

- the terrain lightmap is sampled;
- a tint or selection colour is set;
- a point light touches it.

So in the shadow pass, as in the main view, groups break per light
environment. 0.9.0 counted those breaks as `shadow_inst_split`.

**The cut.** The call at 0x00173C2B goes straight to `pf_sp_same_batch`, a
`thiscall` plugin function with no stub, because it runs for every FX record
of every pass. Outside the shadow pass it tail-calls the game's test. Inside,
when the game's test says "different", the function:

1. puts the first mesh's light-environment pointer and fog colour into the
   other mesh;
2. runs the game's test again (opacity, the models' FX flags and the methods
   still decide);
3. puts the other mesh's own values back (integer copies).

Draw order doesn't change; only fewer groups are set up. The inherited
render data was never part of the test, so nothing changes there.

**The light state it leaves (fixed in 0.9.4-lights).** A merged group's setup
stores its first mesh's light environment. The FX flush's unlock writes the
device's lights from the last one stored, and those outlive the pass. 0.9.4
hands the setup the environment the game's own last group would have stored.
See Milestone 9, "The device state the shadow pass leaves".

**Proof.** `make shadow-fx` (`perf/tests/shadow_fx`, dev only) creates every
effect in `rotwk\Shaders.big` with d3dx9_43 and checks three things:

- only `defaultw3d`, `gpuparticle`, `normalmapped`, `terrain` and `vegetation`
  have a `_CreateShadowMap` technique, the one the shadow method swaps in
  (0x0007B1BB → slot 0 at 0x00150E14);
- per `ID3DXEffect::IsParameterUsed`, none of those techniques uses a light,
  ambient or fog parameter, by name, semantic or `SasBindAddress`;
- a recording state manager sees byte-identical device calls (states,
  textures, shaders, every constant) with all light, ambient and fog
  parameters set to two different values.

Every one of those passes also sets `FOGENABLE=0`, `ALPHABLENDENABLE=0`,
`ZFUNC=LESSEQUAL` and `ZWRITEENABLE=1` itself. The tool passes on Windows'
`d3d9.dll` and on DXVK 2.6.2. The `OpacityOverride` the depth shaders read
comes from vtable +0x64, which the test still compares. The 13 effect files
(name, FNV-1a, size, and whether each has the technique) are in
`perf/src/shadowfx_table.h`, and the tool fails if they differ.

**Guard.** On a thread started at load (`fxguard.c`, kernel32 only), the
plugin reads the directory of every `.big` under the game folder (the
working directory) and the `-mod` folder, plus loose `shaders\compiled`
folders there. It hashes every `shaders\compiled\*.fxo` it finds. For the
user's install that is 131 archives in 0.7–1.3 s, so it doesn't run under
the loader lock. Until it reports, the cut leaves the game's test alone. Any
file not in the table, or any missing shadow-capable effect, keeps the cut
off (logged). Age of the Ring and Realms ship no effect of their own.

### NoStencilFlushes (default 1)

**What the game does.** In `flushOccludedObjectsIntoStencil` (0x00070176),
every object whose template's shadow type (`+0x5F2`) has
`SHADOW_VOLUME_NON_SELF_1..3` (0x80, 0x100, 0x200; name table at VA
0x00D99AD8) is drawn between two mesh-renderer flushes (0x00116D80), with a
stencil reference made from those bits:

- occludees at 0x00070257 (`and ecx, 0x380`);
- occluders at 0x00070597 (`shr eax, 3; and eax, 0x70`);
- the rest at 0x0007069D (`shr ebx, 3; and ebx, 0x70`).

Realms uses these types in 80 object files: trolls, mûmakil, ents, eagles,
fell beasts, drakes, chariots and wagons. Such units are never batched or
instanced, and each one cuts the batch of everything queued before it.

**Who reads stencil bits 4–6.**

- Only stencil shadow volumes (`W3DVolumetricShadow`).
- The occluded player-colour silhouettes use the low bits (player indices
  1–15, bit-reversed in 4 bits by 0x0006E3DF) with mask 0x8F.
- The merge decals test `NOTEQUAL 255`, which no reference reaches with or
  without the bits.
- The shadow map's own D24S8 stencil is read by nothing.

**The cut.** Three stub sites steal those 6-byte instructions. When the cut
applies, `pf_enter` zeroes the saved register before the trampoline runs
them, so the game takes its own path for an object without the bits. No
branch lands in any stolen range, in either executable.

- 1: in the shadow pass.
- 2: also in the other passes while `TheWritableGlobalData` (0x009E4364) has
  shadow mapping on (+0x62) and stencil volumes off (+0x60). Both bytes are
  read each time, so with stencil shadows nothing changes.

**Proof.** Without the flushes, a monster's meshes are drawn in the batch's
order instead of on their own. The shadow passes write depth with
`LESSEQUAL`, depth writes on and no blending, so the map is the nearest
depth in any order. `make shadow-fx` also draws 24 overlapping objects, a
quarter of them exact duplicates (equal-depth ties), into an R32F map with a
D24S8 buffer in five orders: 248,448 texels, 0 differ, on both runtimes.

**Value 2 isn't exact, so it isn't the default.** The bits only change the
stencil reference the monster's meshes are drawn with, and with shadow
mapping nothing reads bits 4–6. Order is the problem. The FX queue takes every
sort-level-0 FX mesh, opaque or blended (`MeshClass::Render`), and the flush
sorts it by model pointer only (`mesh+0xC4`). The two flushes cut it into
three windows: what was queued before the monster, the monster, and what
comes after. Without them the three are sorted as one, and meshes cross the
monster's boundaries. `make shadow-fx` models exactly that (8 objects queued
before the monster, 4 for the monster, 12 after, each window sorted by key,
then all 24 sorted as one; 22 of 24 change place). It draws them in colour
with `LESSEQUAL` depth into an A8R8G8B8 target. Results are the same on both
runtimes:

| Case | Texels drawn | Differ |
|---|---|---|
| all opaque, distinct depths | 62,063 | 0 |
| opaque, equal depth across windows (another colour) | 61,340 | 2,105 |
| one translucent mesh queued before the monster | 62,154 | 4,421 |
| the same without depth writes | 61,459 | 468 |
| one translucent mesh in the monster | 61,975 | 814 |

A translucent mesh anywhere in the windows changes the picture where it
overlaps the monster. That holds even when the monster is fully opaque, so
keeping the flushes only for monsters with translucent meshes isn't enough.
An exact version would keep the first flush when the queue already holds a
blended mesh, and keep the second when the monster or anything queued after
it, up to the next flush, is blended. The second test needs a hook on every
mesh queued. Equal-depth surfaces of different colours (two units of one
model on the same spot) would still change. Value 2 therefore stays an
opt-in that isn't exact.

### ShadowSkipParticles (default 1)

**What the game does.** `RTS3DScene::Flush` calls the particle render
forwarder (0x0004C3EA → `TheParticleSystemManager` vtable +0x40, 0x0004C84A)
at 0x000716EE in every mode-0 pass, the shadow pass included. The render's
steps:

1. It returns unless the manager's +0xA8 byte is set. Every mode-0
   `Customized_Render` sets that byte (vtable +0x48, 0x0004C3E2).
2. It clears the byte and zeroes the smudge counter (0x009C7730) and
   `TheSmudgeManager` +0x20.
3. It walks every particle system. Systems named `SMUD` add heat-haze
   smudges for the pass's camera when `TheSmudgeManager` exists, its +4 isn't
   1 and GlobalData +0x25 is set. The rest call their draw module's
   `doParticles` (vtable +0x10).

All six draw-module classes (12 vtables; slots +0x14 and +0x1C shared) test
the shadow-map byte first and return 0. So in the shadow pass the walk draws
nothing and adds 0 to the drawn count (+0x5C). Its deferred lists are cleared
at its end, `0x00D99035` is put back, and the snow render returns there too.

What does persist:

- the cleared +0xA8 byte;
- the smudge count. `RenderSmudges` resets the sets after drawing and copies
  the count into `TheSmudgeManager` +0x20. The main view's heat-haze filter
  (0x000FD5E8) reads that value to decide whether to render to a texture.

**The cut.** The call at 0x000716EE gets a stub. In the shadow pass, when the
manager's +0x40 slot is the checked render, it returns without calling:

- nothing pending: the render would return at once;
- pending, and no smudge can be added: it writes what the walk leaves (the
  byte cleared, both smudge counts 0).

With 2 it does the latter even when smudges are possible; the heat-haze
filter may then decide on the previous pass's count.

**Proof.** The audit above (both executables have the same 12 vtables) and
`test_shadowpass`. With UltraHigh's heat effects the smudge branch can run,
so value 1 keeps the walk: no saving there unless heat effects are off.

### ShadowPassVerify (default 0)

Every 10 s (from 20 s in), on a game frame that redraws the shadow map, the
`shadow` site calls `UpdateShadowMap` twice itself, then skips the game's
call:

- once with one cut held off (rotating over the enabled ones and "all
  three");
- once as configured, in alternating order so the first pass's skeleton
  updates don't bias the timing.

Inside each run, the shadow map is taken from render target 0 at the particle
site (installed for the verify), read back to system memory, and the two
reads are compared:

- texels drawn, not bit-identical, and over the instancing verify's
  tolerance of 2^-16;
- the particle bookkeeping (pending byte, drawn count change, both smudge
  counts).

The log gives both pass times. A cut that differs twice is switched off for
the session.

### Counters (CSV, appended; per frame unless noted)

| Column | What |
|---|---|
| `fx_groups_shadow`, `fx_groups_reflect`, `fx_groups_view` | FX groups by pass (the `fx_group` probe). |
| `sb_tests`, `sb_ends_game`, `sb_ends_now` | The shadow pass's batch tests, the group ends the game's test gives, and the ends left. The difference is the groups merged. |
| `stencil_skip_shadow`, `stencil_skip_other` | Objects drawn without their two flushes. |
| `particles_skipped`, `particles_kept` | Shadow-pass walks (per row). |
| `spv_samples`, `spv_bad` | Verify samples (per row). |

`bfme2perf_scopes.csv` now also lists each label by the pass it ran in (for
example `RenderFXShaderBatch [shadow]` and `RenderParticles [shadow]`), so
the shadow pass's batch and particle time shows directly. The log gives
per-minute totals and each verify variant's average pass time off and on.

### With the rest

- **ShadowInterval:** a skipped redraw draws nothing.
- **ExtraFrameShadows:** an extra frame's shadow pass goes through the same
  cuts. Its map is the one the game would draw from those transforms, so
  0.8.7's reuse of it by the next game frame is unchanged. Counters count
  game frames only.
- **FrameSmoothing 3:** transforms are set before the pass and none of this
  touches them.
- **Shadow instancing (0.9.0):** it sees the groups `PerformRendering` formed,
  now whole models, so it covers more meshes. The leader is still the game's,
  the instancing verify still compares each group, and `shadow_inst_split`
  should fall to near 0.
- **Logic and CRC:** none of these sites is reached from logic.

### Expected saving (heaviest minutes of the 0.8.5 run 2, ShadowInterval=1)

- **ShadowBatchIgnoreLight:** the shadow pass's ~600 groups, at 4–7 µs of
  setup each, fall to about one per model, material and opacity (estimated
  150–250). That is 1.4–3.2 ms, plus the shadow-pass meshes 0.9.0 left as
  "small" groups (24%) becoming instanced, about 0.3–0.5 ms.
- **NoStencilFlushes:** up to 1.5 ms at 1 with many monsters on screen;
  nothing without them. Value 2 adds the main view's share.
- **ShadowSkipParticles:** 0.3–0.7 ms of the 1.5 ms `RenderParticles`, but
  only when no smudge can be added (heat effects off) or with 2.
- **At ShadowInterval=2** about half of each.

ShadowPassVerify measures each cut's saving in the same frame;
`RenderFXShaderBatch [shadow]` and the `sb_*` columns show the batch saving
in normal play.

### Tests (2026-10-14)

- `make test`: all 11 suites pass.
  - **`test_sites`:** the five sites match once in `game.dat` and
    `game820.dat` at the RVAs above. `pf_sp_find` gives the same data in
    both. The particle call goes to the forwarder, and the render is in one
    vtable slot.
  - **`test_hooks`:** a stencil site through the real stub (ECX zeroed in
    the shadow pass, untouched otherwise).
  - **`test_row`, `test_config`, `test_load`:** the 12 columns, the 4 keys,
    and the log lines.
  - **`test_shadowpass` (new):** each cut against fake game objects:
    - the batch test's fields put back, and opacity still splitting;
    - stencil, every pass, mode and shadow-mode combination;
    - particles, every pending, smudge and vtable case;
    - the effect guard on the real game folder (proven), on a mod with its
      own effect or a loose one (off), and with no effects (off);
    - ShadowPassVerify on a hardware device, where an exact cut passes and a
      wrong one goes off after two bad samples. This also runs under DXVK.
- `make shadow-fx` on Windows' `d3d9.dll` and DXVK: OK (above).
- The plugin still imports only KERNEL32, and has floating point only in
  `fpsmath.c`.
- The installed-site mask is now 64 bits wide (`pf_site_mask`, `PF_BIT`). With
  0.9.1's two button-clock sites there are 34 sites; a static assert guards
  the limit.

## Milestone 8: catching up by skipping renders (0.9.4)

In the user's big battles (`%TEMP%\bfme2perf-settings-test-1101\analysis.md`)
the main thread needs 33–37 ms per frame, whatever the graphics settings,
with present at about 0. Logic advances by counting frames (one phase per
client frame, six per 5 Hz logic frame), so every frame over the limiter's
33 ms slows the game: it ran at 84–92 % speed, and online the slowest
client sets everyone's pace. The render (shadow map, reflection, views, UI)
is about 60 % of such a frame.

Catch-up skips the render of a frame when the game has fallen behind its
schedule, and lets the limiter's next wait end at the schedule. Every frame
still runs its whole update. Logic runs the same frames with the same
inputs whether a frame is drawn or not. The reference projects decoupled
logic from frames by time, which broke multiplayer; nothing here changes
when or how often logic runs. Only ideas were taken from them, no code.

### How the frame loop runs (AotR = 2.01 RVAs)

- **`GameEngine::execute`** loops `update` (vtable +0x28), then the
  limiter. The limiter waits only when its byte (0x009E4320) is set:
  GlobalData's FPS-limit switch, cleared in fast mode, when the camera's
  time multiplier is above 1, and for six frames after a frame number the
  engine keeps at 0x009E4308.
  The network's time scale (0x0099F498) can set it too.
- **The wait** (0x0023A196): `period = (int)(1000 / (m_maxFPS × scale))`,
  33 ms at 30 FPS. With `elapsed = now − [start]` (frame start 0x009E4318,
  `timeGetTime`), it loops `Sleep(0)` (the 0.8.0 `fps_wait` site) while
  `elapsed < period`. Then `[start] = now`. A late frame never waits and the
  next frame starts when it ends, so **lateness is never made up**:
  frames take `max(33, work)`.
- **The frame**: `GameEngine::update` runs the client half
  (`GameClient::update`: input, window manager, drawables, then
  `TheDisplay->draw()` at 0x00248869, then `InGameUI` and the control bar,
  display strings, the shell), radar, messages, audio and the network, and
  one logic phase.
- **`W3DDisplay::draw`** (0x0004B788) returns at once while the window is
  minimised. It runs `updateAverageFPS`, terrain LOD, the debug display,
  computes the client frame delta, then (fast-mode path aside) the
  terrain-tracks update and `WW3D::Sync`, the time-multiplier skip, and
  then calls **W3DDisplay's frame** (0x00049CF8, arg `now`), in a loop
  while the client's time is frozen and the camera moves. Screenshots and
  a final call come after it.
- **W3DDisplay's frame**: `BFME_DX8_Thread_Lock`;
  `WW3D::Prepare_Render_New_Frame`; unless paused, `updateViews` (camera,
  drawables' draw modules); the horde draw manager's query; the FX
  particle update; the terrain's tree update (sway, toppling); the shroud
  and taint per-camera updates. Then the **render**:
  1. `UpdateShadowMap`;
  2. the water reflection;
  3. GlobalData +0xD36/+0xB6F tests and `WW3D::Begin_Render`;
  4. the movie path or the scene: the shadow manager's pre/post around
     `drawViews` (`RTS3DScene` render for every view);
  5. static images and the UI: `InGameUI` draw ×3, the window manager's
     draw, `InGameUI`'s floating-animation update (0x002A536C), another
     subsystem's draw;
  6. the letterbox fade, the subtitles, the mouse, the debug display;
  7. `WW3D::End_Render` (present).

  If step 3 fails, the frame goes to its own **no-render path** (0x0004A230):
  `if (byte 0x00998CB0) byte = 0`, then the return value as after a render.
  That byte has three references, all in this function.

### What is skipped

A new probe site, `catchup_render`, is at 0x00049DAB: the start of step 1,
after everything listed before the render. Stolen: `mov eax, [TheGameLogic]`
(5 bytes). Its four branches all land on the site, none inside it; the
signature is unique in `game.dat` and `game820.dat`.

On a skipped frame the stub continues at the no-render path instead of the
trampoline. The path compares with BL, which is 0 there (the function's
`xor ebx, ebx`; the plugin checks the saved EBX and turns catch-up off if
not). The stack and the exception state are the same at both points. So a
skipped frame:

- skips steps 1–7 (shadow map, reflection, scene, UI draw, present);
- keeps everything else in the frame. That includes `updateAverageFPS`
  (the network reports it as the frame rate, so it stays at the frames
  actually run), `WW3D::Sync`, the view update, the FX particle update, the
  trees, the shroud and taint, and every client and UI update outside the
  draw.

`cusites.c` finds what it needs and checks it:

- the no-render path, from the two `jne` after the GlobalData tests (both
  to the same place, shape checked);
- the scene, as pushed for `UpdateShadowMap`, the same global as
  `scene->pass = 0`;
- the scene's update loop in `Customized_Render`, every offset fixed in
  the pattern; its shadow-map byte must equal 0.5.0's;
- the floating-animation update, called on TheInGameUI (the same global
  as `uistate.c`'s), its body checked;
- the limiter's byte and clock, as the wait signature reads them;
- the client's time-running byte, as the draw tests it;
- TheGameLogic, both signatures agreeing; the device, for the notice.

### Logic and the CRC don't read the render (static)

- **The game already runs frames without it.**
  - A minimised client in a LAN or online game keeps running
    `GameEngine::update` (`Win32GameEngine::update` breaks out of its
    iconic loop for network modes). `W3DDisplay::draw` then returns before
    anything, including `Sync`.
  - The frame's own no-render path above.
  - The draw's time-multiplier skip.
  - The network's skipped client frames.

  Logic that read anything the render writes would desync in each case.
- **What the render writes is per client.** Visibility flags come from each
  client's camera. Poses are computed lazily from the animation and the
  sync time, the same values whenever they are first needed (R4).
- **Logic reads only pristine bones** (Zero Hour heritage:
  `Object` → `Drawable::getPristineBonePositions`), cached from the
  model's base pose independent of any draw. It also reads hidden flags
  set by logic and the client.
- **Shroud and terrain.** The logic shroud is the partition manager's
  (logic phase 5); the render only copies it into a texture, and that
  update is before the site anyway. Logic heights come from the map's
  height data, not the render's buffers.
- **Sound.** The listener is set by the view update (before the site);
  object sounds come from logic positions.
- **Writes the skip leaves out**: GPU and render-object state, WW3D's frame
  counter, the byte above. None is read by logic.

### What a skipped frame still advances

Things the render advances once per draw are run by the plugin on a skipped
frame, at the site, as the render would:

- **The scene's update list**, as the main view's `Customized_Render` walks
  it (`On_Frame_Update` on every object, mode 0, with the projection
  override and shadow-map bytes 0). W3D particle buffers emit by the last
  sync step (`Get_Frame_Time`). Skipping them would emit fewer particles,
  and dynamic lights fade one step per call. The terrain (class 4) is left
  out: its update only refreshes what it draws (vertex lighting, background
  tiles).
- **InGameUI's floating 2D animations** (list +0x8D0): position +=
  velocity, velocity × damping, per draw.

Checked and left alone:

- the FX particle update and the view update: before the site, once per
  frame (0.8.4's guard problem is about extra frames, not this);
- the mouse cursor and the letterbox fade: time-based;
- **command-button clock requests** (0.9.2): the control bar sets them
  after the draw, for the next one. A skipped draw leaves them set, and the
  next drawn frame draws the latest. The control bar renews them every
  frame while something is in production, so the only difference is one
  drawn frame showing the last percentage when production ends right after
  a skip;
- **still per draw, not advanced on a skipped frame**:
  - shrub sway (stepped inside the terrain render);
  - the update lists of any scene other than the 3D one;
  - the subtitle layout.

  These run slower by the fraction of renders skipped. That is at most
  half with `MaxSkips=1`, and typically 1 in 7 to 1 in 3 in a big battle.
  (Extra frames make them faster in the same way. The scene's update list
  is held in extra frames since 0.9.4, below.)

### When to skip

`catchup.c`'s decision works on the limiter's own clock and frame start, with
measured costs:

- **The schedule.** Each frame should start one limiter period after the
  previous frame's scheduled start. At `GameEngine::update` the frame's
  lateness is `[start] − schedule`. It is capped at three periods (99 ms;
  more is forgiven). After a stall over 250 ms, or any frame not allowed
  to skip, the schedule restarts from the frame's start (stock pacing).
- **The wait.** While the frame started late, the limiter's wait ends when
  the frame reaches the schedule (`elapsed ≥ period − lateness`). The hook
  sets the saved ESI (the period) to 0; the loop compares, exits, and
  reloads ESI from its frame. No game memory is written. In such a frame no
  extra frames are drawn: the idle time goes to catching up.
- **The render.** At the site, the first time in a frame:
  `late = (now − scheduled start) + render + after − period`, where
  `render` is the measured cost from the site to the draw's end on drawn
  frames, and `after` the time from the draw's end to the frame's end.
  Both are averages over 8 frames, a slow sample counted as at most twice
  the average. The render is **skipped when `late > max(render, 4 ms)`**:
  even skipped, the frame would end behind, so a skipped frame never waits
  and each skip pays back a whole render. That is the fewest skips that
  keep the schedule. They come at a steady spacing (every 7–8 frames for
  36 ms frames), with no flicker between states.
- **Never**:
  - two in a row (`MaxSkips`, 1 by default, up to 3);
  - before 8 renders were measured;
  - when the limiter isn't pacing (fast-forward, script time scale, the
    first frames);
  - outside a game (the shell included, `isInGame`), while paused, while
    the client's time is frozen (cinematic camera);
  - with a screenshot pending;
  - while placing a building or while the camera scrolls (a missing frame
    shows there; the debt is forgiven meanwhile);
  - in a frame after one that drew extra frames;
  - in an extra frame or off the main thread.

### With the rest

- **FrameSmoothing=3**: frames are still numbered by the limiter's frame
  start, which changes once per frame, skipped or not (the catch-up wait
  ends the loop normally). The game's draw still lags and puts back units
  around the (skipped) render. During catch-up no extra frames run, so the
  delay eases out as 0.8.7 does.
- **ShadowInterval, ReflectionInterval**: a skipped frame counts as one that
  reused the stored map (`pf_reflect_frame_skipped`), so the next render
  redraws on schedule. The map shown is never more than interval − 1 game
  frames old. With interval 1 every drawn frame redraws.
- **ExtraFrameShadows**: 0.8.7's reuse of an extra frame's map is dropped at
  a skip (the map is a frame older by then).
- **The shadow matrix**: a skipped frame calls no `SetShadowMapInfo`; the
  stored matrix still matches the stored map.
- **ShadowPassVerify, instancing verify**: both are timed; their samples
  simply fall on drawn frames. Their extra cost is clipped from the render
  estimate by the 2× rule.
- **Multiplayer**: logic, commands, the network and the CRC run every frame
  as stock. The frame rate the network sees (`updateAverageFPS`, still run)
  is the update rate, now back near 30. So a slow client stops slowing the
  others; nothing is faster than the stock schedule on average. The
  network's own time scale changes the period, which the schedule follows.

### The notice

While renders are being skipped and for 2 s after the last one, every
presented frame (extra frames too, so it doesn't flicker) gets a small box
in the top right corner of the back buffer with `[CatchUp] NoticeText` and
`...` (three ASCII dots: the font has no U+2026; dots the text already ends
with are dropped first). The margin from the top and the right is a 30th
of the height; a line too wide for the target at its scale is drawn
smaller. 0.9.4 held it 1 s at the top centre: in the user's first battle
with catch-up, isolated skips a few seconds apart flashed it on and off
about 50 times in 6 minutes (2 s: about 31). It is drawn
at `DX8Wrapper::End_Scene(flip)`'s entry (the `present` site) with two
`IDirect3DDevice9::Clear` calls only:

- a dark box, then the text's pixel runs from a 5×7 font of the plugin's
  own (printable ASCII; anything else draws as '?');
- scaled 1× up to 1079 lines, 2× at 1080, 4× at 2160.

The viewport and the scissor test are put back exactly. Nothing is drawn
unless the current render target is the back buffer. No game UI, message
list or window is touched. The shipped INI says `Recuperando atraso`; the
code's default is `Catching up`.

### Settings (`[CatchUp]`)

| Key | Default | |
|---|---|---|
| `Enabled` | 1 | skip renders when behind; 0 = the game's own pacing |
| `MaxSkips` | 1 | renders skipped in a row, 1..3 |
| `Notice` | 1 | the on-screen notice |
| `NoticeText` | `Catching up` | its text, shown with `...` (shipped INI: `Recuperando atraso`) |
| `NoticeMinSkips` | 6 | renders skipped within 2 s before the notice shows, 1..30 (0.9.9; 1 = every skip) |

It is on by default because every proof above holds statically. It needs
`frame`, `fps_wait` (+`fps_avg`), `display_draw` and `catchup_render`; the
notice also needs `present` and the device. If any is missing, catch-up stays
off and the log says why.

### Counters

CSV, appended (flag `PF_HAVE_CATCHUP` 524288; whole row):

| Column | What |
|---|---|
| `skipped_draws` | renders skipped |
| `logic_frames` | `GameLogic::update` calls: 6 per 5 game frames, in the shell too (not a speed) |
| `game_speed_pct` | `frames` / 30 × 100 (logic takes one step per game frame). Until 0.9.6 it was `logic_frames` / 30 and read 120% at full speed |
| `behind_ms`, `behind_max_ms` | lateness at frame start against the schedule, average and most, over the frames tracked |
| `cu_render_ms`, `cu_after_ms` | the estimates the decision used |
| `waits_cut` | limiter waits ended at the schedule |

The other per-frame columns stay per game frame: a skipped frame counts 0
render time. Drawn frames per row are `frames − skipped_draws`. The log
gets the first skip, and one line a minute while anything was skipped or
cut: frames drawn of run, skips, cut waits, logic frames and speed,
lateness, the estimates and a skipped frame's own cost.

### Tests (2026-10-14)

- `make test`: all 12 suites pass.
- **`test_catchup` (new):**
  - **the decision in a simulated game loop**: the limiter as the game runs
    it, 1 ms clock, 60 s;
  - **the site through the real stub** on a fake frame function: on a
    skip the no-render path runs with EBX intact; the scene's three units
    get one `On_Frame_Update` and the terrain none; the UI animation update
    runs once; the limiter's wait is ended. The next frame renders and the
    plugin touches nothing. No skip and no cut when paused, time frozen,
    not in game, a screenshot pending, extra frames last frame, no limiter,
    placing or scrolling;
  - **the notice**: every glyph defined, the layout at 720p, 1080p and
    2160p, and on a hardware device the box's and text's pixels, nothing
    changed outside the box, the viewport and scissor test put back, and
    nothing drawn into another render target. Also passes with DXVK's
    `d3d9.dll` next to it.

  The simulated loop gives:

  | Load (before + render + after) | off: logic frames/s | on: logic frames/s | on: drawn/s |
  |---|---|---|---|
  | 12 + 22 + 2 ms (36 ms) | 27.8 | 30.3 | 26.2 |
  | 15 + 33 + 2 ms (50 ms) | 20.0 | 29.8 | 15.0 |
  | the same, `MaxSkips=2` | 20.0 | 30.3 | 14.7 |
  | 9 + 18 + 2 ms, +25 ms one frame in six | 27.4 | 30.3 | 25.3 |
  | 13 + 23 + 1.5 ms, +15 ms one frame in six | 25.0 | 30.3 | 21.1 |

  In each, at most one skip in a row; frames that fit (25 ms) skip and cut
  nothing; no skip or cut in frames not allowed. 30.3 is the limiter's
  1000 / 33.
- **`test_sites`**: the site matches once at 0x00049DAB in both executables.
  `pf_cu_find` gives the same data in both, the site is the expected
  instruction, three jumps go to the no-render path and none into the
  stolen bytes.
- **`test_reflect`**: a skipped frame in the shadow and reflection
  schedules.
- **`test_row`, `test_config`, `test_load`**: the 8 columns, the 4 keys (text
  trimmed, non-ASCII as '?', length capped), and the log lines without the
  game.
- The plugin still imports only KERNEL32 and has floating point only in
  `fpsmath.c`.

### Expected effect in the user's battle

Measured there: about 13 ms before the render (logic 4.7–5.7, client
updates), 19–23 ms of render and UI, 1–2 ms after, plus a heavy logic phase
one frame in six. That is the last row of the table. The game should hold
**100 % speed (30 logic frames/s, from 84–92 %)** at about **21–26 drawn
frames per second** (from 25–27.5): a few frames fewer for full speed. In
the heaviest seconds (50–60 ms frames), `MaxSkips=1` draws every other
frame, about 15 per second, and holds 80–100 % speed where the game alone
runs at 55–65 %. Online,
the user's client stops pulling the shared frame rate down. Not measured
in game yet.

### Extra frames hold the scene's update list (0.9.4)

An extra frame runs the game's whole draw. That includes the main view's
`Customized_Render`, which walks the scene's update list and calls every
object's `On_Frame_Update`. W3D particle buffers emit there by the last
sync step: 0 in an extra frame, which holds the sync time. They also
register finished buffers for release. **W3D dynamic lights** step their
fade-in and decay counters once per call. So with MaxFPS=120, light pulses
(fires, explosions, spells) faded up to four times as fast. The FX particle
update and the view update were already held (0.8.4). This list wasn't.

A new probe site, `scene_update` (0x0006FF49, group `hold`), sits at the
loop's per-object `On_Frame_Update` call. Its signature is the whole update
loop, the same bytes catch-up's finder checks, with every offset fixed. It
steals `mov ecx, [ebx+0xC]; lea eax, [ecx-8]`. In an extra frame, on the main
thread, the stub continues 17 bytes on, at `mov ebx, [ebx+4]` (the next
node), so the call is passed by. In game frames it runs as stock. No branch
lands inside the stolen bytes, in either executable.

- The list advances once per game frame: in the game frame's own render, or
  on a frame catch-up skipped, by the plugin's walk (which calls the
  objects directly, not through the site).
- It is installed with the view and particle holds (the `hold` group). Extra
  frames need all three, or they stay off for the session.
- CSV `extra_held` now counts 3 per extra frame when the scene's update list
  has objects: view update, particle update and this list.

Tests:

- `test_hooks`: a fake update-loop step through the real stub. A game frame
  updates the object and takes the next node; an extra frame passes the
  object by, still takes the next node, and counts the hold. The next game
  frame updates it again.
- `test_sites`: the site matches once at 0x0006FF49 in both executables,
  with the call 14 bytes on and the next node 17 bytes on; nothing branches
  into the stolen bytes.

### The extra frames' cost estimate (0.9.6)

The user's 0.9.4 battle (`%TEMP%\bfme2perf-0.9.4-catchup-run1-1251`)
never went above 30 FPS again once the fight was big, even after it calmed
down. With MaxFPS=120 the CSV shows the expected cost falling from
5.7 ms to 31 µs over t=89–112 while no extra frame fit (budget 1.5–11 ms),
then a late game frame after an extra frame of 9.4 ms "expected 31 us"; the
same at t=113–123, and the chain of lates that turned extra frames off for
the session ended with 10–13 ms extra frames expected at 1.7–2.4 ms. The
rest of the run (budget up to 22 ms) drew none.

**Cause.** 0.8.4's "after 30 game frames in which none fit, the slowest
sample gives way to the decaying average" ran on every game frame without
an extra frame that wasn't paused: frames that overran, frames with a few
ms left, frames catch-up held. The average lost 1/32 a frame toward 0, and
the slowest samples followed it: 6 s took 10 ms to about 30 µs.

**Now** only two things change the estimate:

- an extra frame that was drawn (as before: the slowest of the last 64 game
  frames that drew one, and the average);
- a game frame that had room for an extra frame of the *reference* cost but
  not of the expected cost. After 30 of those, one per frame, the average
  eases toward the reference and the slowest sample gives way to the larger
  of the two. Nothing goes below the reference.

Frames without that room (no idle time, catch-up holding, paused, resting,
a screenshot) leave the estimate exactly as it is.

**The reference** is the game's own draw: `W3DDisplay::draw` on game
frames, timed at `display_draw` (now installed with MaxFPS > 30, not only
with FrameSmoothing=3; renders catch-up skipped are left out), an average in
which one slow draw counts at most twice, once 8 have been timed; 10 ms
before that or without the hook; never under 1 ms. It also seeds the
expected cost before any extra frame has been measured. In the 0.9.4 run
the draw from the render site alone was 4–7 ms where extra frames (which
redraw the shadow map) took 6–9 ms, so the reference can be low; hence:

**Guesses.** While the expected cost isn't a measurement (none yet, or its
maximum is a sample that gave way to the reference), a late game frame
after it is logged "not measured yet: not a repeat": it pauses 1 s, doesn't
extend the chain of repeats, and still counts toward the minute's 1%. That
extra frame's cost becomes a floor under the reference, eased back toward
the cost of extra frames that fit afterwards. So the same guess can't make
frame after frame late (`test_fps`: 22 ms extra frames with 20 ms left and
a 3 ms draw give one late, then none).

**Rests instead of off for the session.** 0.8.4's two rules (late again
right after the 8 s pause; over 1% of a minute late) now rest extra frames
1 minute, then 2, then 4, with the chain and the minute's buckets started
over; the 4th time they are off for the session. Checks that fail (the
logic seed, engine counters, client frame, CRC, camera or units not put
back, the draw or particle update replaced) still turn them off at once.
Log: `fps: extra frames rest for 60 s: ...`, `fps: extra frames back after
resting (expected cost N us)`. CSV, appended: `extra_ref_ms` (the
reference when the row closed).

### The game's speed (0.9.6)

The catch-up minute line's speed was `GameLogic::update` calls / 1800, and
that hook fires 6 times per 5 game frames, menus included: about 20% high.
The game's own counter is read instead: `GameLogic::update` steps
TheGameLogic +0x40 once per logic frame (`inc [esi+0x40]` at 0x0022E577, in
both executables, after a three-byte "ready" test of `this` and the
network hold), and `GameEngine::update` divides by
`BfmeLogicFramesPerSecond` (0x0099F608, 5). Both are found by signature
(the counter's call target must be that test's body) and only read. The
minute line gives the logic frames and `frames × 100 / (rate × seconds)`;
without the counter, game frames / 30. CSV, appended: `logic_ticks`, the
counter's steps per row (5 a second at full speed, none in the shell);
`game_speed_pct` is now `frames` / 30.

Tests: `test_fps` (no decay over 20 s of overrun, short and held frames,
then extra frames back without a late; a 40 ms sample gives way only down
to a 6 ms draw; the reference seeds the cost, ignores skipped renders, has
a 1 ms floor; the guess floor; rests of 1, 2, 4 minutes with no extra frame
in them, then off), `test_sites` (the counter and the rate in both
executables), `test_catchup` (ticks per row across a new game), `test_row`.
With 0.9.5's decay rule the first test reads "the expected cost fell to
31 us", as in the user's log.

## Milestone 9: the per-object light environment (0.9.4-lights, merged in 0.9.7)

The 2026-10-14 settings run (`%TEMP%\bfme2perf-settings-test-1101\analysis.md`)
put most of a battle frame in per-object CPU work in the passes, outside the
FX batches. `RTS3DScene::renderOneObject` (AotR 0x0006F48E, `thiscall`,
`ret 0x10`) runs for every visible object in every pass (the shadow-map pass
is pass mode 0 too) and builds the object's light environment each time.
0.9.4-lights times it and cuts the parts that can be cut exactly.

### What the build does (AotR / ROTWK 2.01, read from the code)

The environment is a 0x228-byte `LightEnvironmentClass` on the stack at
`[EBP-0x2A0]`, made by its constructor (0x0013EEF0: zeroes everything; the
destructor at 0x00688AC0 is a bare `ret`), so it starts from zero on every
call. Then:

1. `Reset(centre, ambient)` (0x0013F100): the bounding sphere's centre at +8,
   the ambient (the scene's, or the infantry ambient, plus the terrain
   lightmap sample) at +0x164, the light count at +4 to 0.
2. With a drawable: its tint, selection, base and light colours (0x00270A06
   isn't a pure getter: it calls a global object's virtuals and writes the
   drawable at +0x90) give a tint sum and a scale. The base colour pointer
   is `drawable+0x9C`, never null, so this tinted branch is the only one
   taken.
3. The global lights (scene +0x130, or the infantry lights at +0x150; count
   at +0x160): each light's diffuse is set to the tinted value, the light is
   added and the diffuse put back. Then the ambient is adjusted and "lights
   added" (`[EBP-0x54]`) set.
4. The second material pass (heat vision, stealth) is pushed if any.
5. The scene's light list (sentinel scene +0x90): for every light,
   `Get_Bounding_Sphere` (vtable +0x104); a non-point light (+0xC4 ≠ 0) is
   added, a point light only if the sphere test (0x0006E93D, cdecl,
   `(r1+r2)² > |c1-c2|²`, pure) passes. Then the dynamic lights (sentinel
   +0x118): the same for each one whose enabled byte (+0x144) is set.
6. Nothing added and no lightmap: the scene's default environment (+0x164,
   or the infantry one at +0x5B4). Otherwise `Pre_Render_Update` (0x00140500)
   with the camera's transform (`[[rinfo]]+0x18`): each light in camera space
   at +0x170, the ambient clamped.
7. `rinfo.light_environment` is set (0x0006FD8A), the object's `Render`
   runs, and the pointer is cleared.

`Add_Light` (0x00140250) reads only the environment, the light's fields
+0x10..+0x120 (transform, bits, type, intensity +0xD0, colours +0xD4/+0xE0,
attenuation +0x100/+0x104, spot data +0x10C..+0x11C), `.rdata` constants and
one writable global, the brightness cut-off at VA 0x00DD8E4C. Its point and
spot setup (0x0013F640) uses x87 (`fsqrt`, `fdiv`), so the plugin never
computes a light value itself: it only calls the game's functions or copies
their output. A light's `Get_Transform` (vtable +0x50, 0x0013B320) only walks
a container chain (+0x7C); without one it does nothing.

**Cost, estimated from the code:** about 0.4–0.6 µs per object for the
constructor, the tint, three or four global lights and `Pre_Render_Update`;
about 25–40 ns per light tested in the two lists (list walk, virtual call,
sphere copy, test); about 0.15–0.25 µs per point light added. The counters
below measure it.

**Where the environment goes.** `MeshClass::Render` (0x0014B500) copies it into
the mesh (`Set_Lighting_Environment`, 0x0014B490: by pointer when its +0 byte
is set, else into mesh+0xCC) for FX meshes and static-sorted meshes. Non-FX
meshes test the shadow-map flag first and return in that pass. The FX group
setup (0x0014A080) hands the group's first mesh's environment to
`DX8Wrapper::Set_Light_Environment` (0x001228A0: the device's fixed-function
lights and `D3DRS_AMBIENT`; NULL leaves the device as it was) and to the
effect's light parameters. `Is_Rendering_Identical` compares it by value.

**When lights change.** `Customized_Render` (0x0006FE84) walks the update list
(scene +0x78) calling `On_Frame_Update` (vtable +0x34), which moves and fades
the dynamic lights, before its render-list loop calls `renderOneObject`, and
skips that walk while the shadow-map flag (0x009D1E44) or the inverted-culling
flag the water reflection sets (0x009D3445, Zero Hour's
`ShaderClass::Is_Backface_Culling_Inverted`) is set. So lights are stable within a pass; they change only at
the start of a main-view pass, and the shadow and reflection passes see what
the last main view left.

### LightCull (default 1)

At the light lists' start (0x0006FC16) the plugin decides the lists itself
and continues at the "lights added" test (0x0006FD36):

- Once per pass (a pass starts at the `Customized_Render` hook), it walks
  both lists as the game does: every scene light, every enabled dynamic light,
  each one's type and the sphere its own `Get_Bounding_Sphere` returns.
- For each object, every non-point light and each point light the game's own
  sphere test keeps (called with the same two spheres) is added through the
  game's `Add_Light`, scene lights first, both in list order. "Lights added"
  is set if any was.

**Proof.** The decisions come from the same function on the same values, the
lights go to the same function in the same order, and `Add_Light` doesn't read
the other lights or touch them, so separating the decisions from the adds
changes nothing. What the snapshot takes for granted, that no light changes
during the pass, holds by the order above. A pass is left to the game's loops
(`lt_fallback`) when a light has a container or a bounding-sphere getter
other than `RenderObjClass`'s (0x0006C850), or a list has more than 256
lights. The snapshot calls the getter once per light per pass instead of once
per object: it only computes a cache from the light's own fields.
`ltsites.c` checks the whole loop (0x0006FC16..0x0006FD8D, call
displacements aside) before anything is patched; any difference turns the
cuts off.

### LightReuse (default 1, needs LightCull)

With the kept lights known, an object's environment after the lists depends
only on the environment before them (all 0x228 bytes: centre, ambient, global
lights, tint), the object's sphere, the "lights added" flag, each kept light's
fields, the brightness cut-off and the floating-point modes. A table of 2048
builds (by object, about 2.4 MB) keeps each one with all of those. A light's
fields are compared in full at each snapshot and get a new version when they
change (a table of 1024 lights; when it fills, it starts over and versions
keep growing, so no old build matches). An object whose inputs all match gets
the stored result without the `Add_Light` calls; `Pre_Render_Update` still
runs with this pass's camera. The x87 control word and MXCSR's control bits
are read (never written) into the key. Builds are kept across passes and
frames: the key is the inputs, not the object, so a freed and reused object
can't match wrongly. Only builds with at least one and at most eight kept
lights are stored.

### LightSkipShadow (default 0)

In the shadow-map pass the build is skipped: the global lights loop
(0x0006F8D4) continues at the second material pass check (0x0006F9F6); for an
object without a drawable, Reset and its global lights (0x0006FB11) go
straight to the lists; the lists continue at the default environment's choice
(0x0006FD42). The tint getters still run (one of them writes the drawable).
The object gets the scene's default environment, as the game gives an object
no light reaches.

**What reads it in that pass.** Only the FX path: `Set_Lighting_Environment`
into the mesh, the group setup (`Set_Light_Environment` and the effect's light
parameters, which no `_CreateShadowMap` technique reads: 0.9.3's proof),
`Is_Rendering_Identical` (grouping only; the map is the nearest depth in any
order: 0.9.3's proof), and the static sort list, which re-renders with
static sorting off, so non-FX meshes still return at the shadow-map check.
`Pre_Render_Update`'s output and the light diffuse writes (restored by the
game) leave nothing behind. A mesh's copy is overwritten by its next render
before anything reads it: everything that reads it runs within the same
`RTS3DScene::Flush`. ShadowPassVerify checks it in game as a fourth variant.

What it leaves behind the pass is the device's lights (next section). That
can't be made exact without the build the cut removes, and no static proof
covers every draw that could read it, so the default stays 0.

### The device state the shadow pass leaves (both cuts)

Both LightSkipShadow and 0.9.3's ShadowBatchIgnoreLight change which light
environment the shadow pass's FX groups hand to `Set_Light_Environment`.
This is what of that outlives the pass, read from the code (AotR / ROTWK 2.01;
the premises are checked by `test_sites`).

**Gone before the next pass.**

- *The cached environment pointer* (`DX8Wrapper`, VA 0x00DD3454). Every
  render-target pass ends with `WW3D::End_Render` (0x00116DA0). For the shadow
  map that happens at 0x0007D728, and at 0x0007D79C after the post-process
  step, both before the shadow byte drops at 0x0007D7B1. `End_Render` runs the
  sorting flush, then `End_Scene` (which clears the pointer), then the cache
  invalidation (0x0011F690). The invalidation clears the pointer again, clears
  the pending-state bits and fills the render-state cache with 0x12345678. So
  every later pass starts with NULL, cuts or not. The FX light-parameter
  callbacks (0x0014CEF6..0x0014D354) read through this pointer when a binding
  is applied. `Begin_Rendering` applies bindings after the group setup has
  stored its group's environment; a NULL pointer gives zeros.
- *Effect parameters.* An effect is shared by all passes and keeps a value
  until it is set again. Bindings are recorded per technique, and only for
  parameters that technique uses: `AddBinding` (0x00153459) tests
  `IsParameterUsed` (vtable +0xF8). No `_CreateShadowMap` technique uses a
  light, ambient or fog parameter (0.9.3's proof), so the shadow pass sets
  none of them.
- *The fog colour* (0x00DD3464) *and the value at 0x00DD34B8.* The group setup
  sets both from its first mesh. The group end (0x00149550, at 0x00173ED0)
  puts both back.
- *The render-state cache:* invalidated, as above.

**What stays: the device's lights.** DX8Wrapper's light cache holds four lights
(0x00DD4060, stride 0x68) and their enables (0x00DD4200). The device keeps
what it last applied from that cache: lights 0–3, their enables and
`D3DRS_AMBIENT`. `Set_Light_Environment` (0x001228A0) writes this state only
when the environment is non-NULL, is not the cached one with byte 0 set, and
the light lock (0x00DD3460) is clear. When it writes, it writes all four
lights (count..3 disabled) and the ambient, from the input lights (+0x14,
stride 0x54) and +0x164. `Set_Light` (0x0011D6C0) has no compare; it marks
each light dirty, and the next draw applies them.

FX batches draw only inside the FX flush (0x00174406). The flush sets the
lock, renders the three batch lists (the only callers of
`PerformRendering`), and clears the lock with `Set_Light_Lock(0)`
(0x00124DE0). Clearing the lock clears the cached pointer and calls
`Set_Light_Environment` again with the pointer it had. While the lock is set,
a group setup's call only stores its pointer. So the light state an FX flush
leaves is the environment of the last group's first mesh, written at the
unlock. It survives the invalidation; the next write replaces all of it.

**Who could read it.** Two kinds of reader can see this state before the next
write:

- a draw that lights vertices with the fixed-function pipeline
  (`D3DRS_LIGHTING` on, no vertex shader);
- a sorted-polygon capture. The inserts at 0x0012F850/0x0012FEE0 copy the
  light cache, and the sorting flush restores it.

In the game's code, only `VertexMaterialClass::Apply` (0x0013D080) turns
`D3DRS_LIGHTING` on, from the material's lighting flag. The other writers turn
it off; 0x0011C110 only reads states. The mesh paths write their own
environment first whenever they have one: the DX8 renderer at 0x00146CB4, the
material pass at 0x0014B9A6 and the FX setup at 0x0014A0C1. Every object in a
scene pass has one (`renderOneObject`). Terrain, roads, bridges and water
hand on the render info's environment. In `Customized_Render`'s terrain call
that environment is NULL (0x0006FF83), so the call writes nothing.

What draws first after `UpdateShadowMap` (the water reflection's own pass,
then the main frame) depends on every renderer's materials and effects. That
wasn't audited completely, so a reader is not excluded statically. Such a
reader would see one shadow-casting object's environment instead of another's
(the scene's global lights plus that object's tint and point lights); for
LightSkipShadow, the scene default instead of the object's own.

**ShadowBatchIgnoreLight: made exact (0.9.4).** In 0.9.3 a merged group's setup
stores its first mesh's environment. For the same records, the game would
have stored the first mesh's environment of the last group its own test
makes. When the merged group is the flush's last, the unlock writes different
lights.

The fix hooks the setup call (`fx_group`, 0x00173CB8). There EBX is the
group's first record and [EBP+8] the record past the group, as the scan
signature at 0x00173C14 pins them; the scan sits there in both executables.
When that scan merged a group the game would end, the plugin runs the game's
test over the group's records as the game's scan would, each against the
current group's first. The method compare holds for all of them, since each
matched the first. The plugin then hands the setup's `Set_Light_Environment`
the environment of the last group's first mesh.

Nothing else reads the stored pointer in that pass, because no shadow
technique binds a light parameter. So the pointer after every group, the
unlock's write and everything after it equal the game's. A mesh with an
environment and one without no longer merge, so a setup with no environment
has none to store.

Two counters check it. `sb_light_fixed` counts the setups handed the game's
environment. `sb_light_missed` counts setups whose call didn't come; it is 0
by construction, because that call is every FX group setup's first. Without
the two sites, or with them not where the scan says, ShadowBatchIgnoreLight
stays off.

**Risk for the installed 0.9.3 (`ShadowBatchIgnoreLight=1`).** A difference
needs one of the readers above at the start of the reflection or main view,
before the first environment write there. None was found in the code
checked. If one exists, it would show one element lit by the wrong unit's
light environment (same global lights, another unit's tint or nearby point
light) for one frame's draw. That is low enough that setting it to 0
meanwhile isn't needed.

**LightSkipShadow.** The unlock writes the environment of the last group's
first mesh, which is the scene default instead of the object's own. Making
that exact means building that object's environment, which is the work the
cut removes. So the default stays 0.

`lt_stale_draws` counts the readers that can be detected cheaply: draws
outside the shadow pass between such a write and the next write, when the
device says the draw uses fixed-function lighting (`D3DRS_LIGHTING` on and no
vertex shader; also counted when the device can't answer). A write is a call
the game's own conditions let through: the lock (address from
`Set_Light_Environment`'s head) and the cached-pointer test. Sorted captures
are not counted.

### LightVerify (default 1)

Every 5 s the next 8 objects that reach LightCull are sampled (2: every
object, tests): the plugin computes its result (reused or decided) on a
copy, the game's own loops then run, and at the render info's set the
plugin applies `Pre_Render_Update` to its copy with the same camera and
compares all 0x228 bytes, the environment chosen (local or default) and the
flag. Two differing samples switch LightReuse off (if the sample was a reused
build) or LightCull and LightReuse off.

### Timers and counters (with `[Timing] Enabled=1`)

The wrap of `renderOneObject` (its own return-address stack in `lights.c`;
`pf_exit` hands `roo` back to it) and probes at Reset (both branches), the
render info's set and the two `Render` calls give, by pass (the marker that is
open): calls, total time, time before `Render`, and the light build's time
(Reset to the render info's set), using the processor's time stamp counter,
calibrated against QueryPerformanceCounter per row. Probes on the two lists'
`Add_Light` calls count the lights kept when the game's loops run; the
plugin counts its own. CSV, appended, per frame unless noted:

| Columns | What |
|---|---|
| `lt_calls_P`, `lt_ms_P`, `lt_pre_ms_P`, `lt_env_ms_P` | renderOneObject calls, ms, ms before `Render`, ms in the light build, for P = `shadow`, `reflect`, `views` |
| `lt_scene_P`, `lt_scene_kept_P`, `lt_dyn_P`, `lt_dyn_kept_P` | scene and enabled dynamic lights tested and added |
| `lt_skipped`, `lt_decided`, `lt_reuse_hits`, `lt_reuse_misses` | shadow-pass builds skipped, objects LightCull decided, reuses |
| `lt_fallback`, `lt_verify_samples`, `lt_verify_bad`, `lt_stale_draws` | per row: passes left to the game, LightVerify samples, fixed-function lit draws that could see a skipped build's lights |
| `sb_light_fixed`, `sb_light_missed` | per row: merged groups handed the game's light environment, and those whose setup call didn't come (0) |
| `lt_extra_hits`, `lt_extra_misses` | per row (0.9.7): LightReuse in extra frames; the reuse columns above are game frames' |

The log adds a line a minute with the totals and, by pass, ns per object
(total, before `Render`, light build) and lights tested and kept per object.
The timers cost five hook entries per object (about 0.2–0.3 µs); with
`[Timing] Enabled=0` only the cut sites run.

### Sites (all unique in `game.dat` and `game820.dat`, same RVAs)

| Site | RVA | Stolen | Use |
|---|---|---|---|
| `roo` | 0x0006F48E | `mov eax, imm32` | wrap (timers) |
| `roo_reset` | 0x0006F735 | 7 | build start, drawable (timers) |
| `roo_glights` | 0x0006F8D4 | 8 | LightSkipShadow |
| `roo_reset_nd` | 0x0006FB11 | 7 | LightSkipShadow; build start |
| `roo_lists` | 0x0006FC16 | 6 | LightCull, LightReuse, LightSkipShadow |
| `roo_add_static`, `roo_add_dyn` | 0x0006FC77, 0x0006FD12 | 7 | kept lights (timers) |
| `roo_env` | 0x0006FD8A | 6 | build end; LightVerify |
| `roo_render`, `roo_render2` | 0x0006FE39, 0x0006FE0F | 5 | before `Render` (timers) |
| `lenv_set` | 0x001228A0 | 9 | stale draws (timers); ShadowBatchIgnoreLight's light environment (own group) |

No branch lands inside a stolen range. LightCull and LightReuse also need the
`Customized_Render` hook (installed with them). ShadowBatchIgnoreLight now
also installs `fx_group` (0x00173CB8) and `lenv_set`. 45 sites in all; 47
with catch-up's two since the merge (0.9.7: theirs 34 and 35, these 36–46).

### Expected saving

- **LightCull:** about 20–35 ns per light tested per object per pass. With
  40 lights in range of the lists (a battle with fire), 1,000 objects and two
  building passes, about 2 ms a frame; with 5 lights, about 0.25 ms.
- **LightReuse:** the kept lights' `Add_Light` calls (0.15–0.25 µs each) for
  objects whose inputs repeat: on Ultra the reflection pass after the shadow
  pass (no update between them), and static objects (buildings, trees) from
  frame to frame. It doesn't cover the global lights or `Pre_Render_Update`.
- **LightSkipShadow:** the whole build in the shadow pass, about 0.5–1 µs per
  object: 0.5–1.5 ms a frame on Ultra with 1,000 objects.

The next run's `lt_env_ms_*` and `lt_*_tested`/`lt_*_kept` columns replace
these estimates. Not built: reusing the global-light part too (keyed after the
tint, before the global loop), which would cut the 0.4–0.6 µs per object
that the cuts above leave in the reflection and main-view passes.

### Tests (2026-10-14)

- `make test`: all 12 suites pass.
  - **`test_lights` (new):** fake scenes, lights and game functions against a
    copy of the game's two loops: 400 random scenes give the same
    environment, flag and `Add_Light` calls in the same order; one snapshot
    per pass; a second scene in a pass; the fallbacks (a container, another
    getter); LightReuse hits and every rebuild case (a kept light changed or
    removed, the environment before the lists, the sphere, the flag, the
    cut-off), and a far light's change still reusing; LightSkipShadow's
    three continuations; LightVerify passing on 20 random scenes and
    switching LightCull, or only LightReuse, off after two bad samples; the
    timers by pass; the stale-draw count (terrain's environment, unlit draws,
    passes without skipped builds). Two deliberate faults in `lights.c` (a
    dynamic light's type ignored, the environment left out of the reuse key)
    fail it.
  - **`test_sites`:** the 11 sites and `pf_lt_find`'s data in both
    executables; the sphere test's whole body; the FX group setup's call to
    `Set_Light_Environment`; `Get_Transform`'s container walk.
  - **`test_hooks`:** the `renderOneObject` wrap through the real stubs and
    `pf_exit` (result and stack kept, counted only inside a pass marker), and
    the global-lights site continuing at the skip in the shadow pass.
  - **`test_shadowpass`:** the LightSkipShadow variant on a hardware device:
    held off in one of each sample's two draws, switched off after two bad
    samples.
  - **`test_row`, `test_config`, `test_load`:** the 32 columns, the 4 keys,
    the log lines.
- The device-state follow-up (same day): `make test` passes all 12 suites
  again.
  - **`test_shadowpass`:** a merged run e1 e1 e2 e2 e3 hands the setup e3,
    using the game's test once per record; e1 e2 e1 and value-equal
    environments keep the first's; a mesh without an environment ends the
    group; a different call is left alone and counted as missed, and the fix
    is then spent. The game's own groups, the other passes and missing sites
    are left alone. The `GetRenderState`/`GetVertexShader` slots and
    `D3DRS_LIGHTING` are checked against the SDK.
  - **`test_hooks`:** the same through the real `fx_group` stub (the
    registers and [EBP+8] as the batch has them).
  - **`test_lights`:** stores under the lock leave the earlier lights; the
    unlock's write lasts through the same environment with byte 0 set and
    through NULL, until a real write; with a device, only fixed-function lit
    draws count (lit with no vertex shader, or unknown), and the shader's
    reference is released.
  - **`test_sites`:** the group scan and both sites at its offsets;
    `Set_Light_Environment`'s cached pointer and lock; `PerformRendering`'s
    three callers, each list render's one caller, no data references; the FX
    flush's lock and unlock; `Set_Light_Lock`'s body; `End_Render`'s
    `End_Scene` and invalidation; the invalidation clearing the pointer and
    the pending bits.
  - **`test_row`:** `sb_light_fixed`, `sb_light_missed` appended.

### With catch-up and extra frames (0.9.7)

Merged into the main line with catch-up (Milestone 8) and the 0.9.6 cost
estimate. Lights change in three places now:

- the main view's `Customized_Render` walk, inside a pass, as before;
- catch-up's walk of the same list on a skipped frame (`frame_duties`, at
  `catchup_render`), outside any pass;
- never in an extra frame: `scene_update` passes every `On_Frame_Update`
  by (0.9.4), and extra frames stay off without that hold.

**A skipped render.** LightCull's snapshot is renewed at each pass start.
The first pass after a skip would renew it too, but the snapshot then
rested only on nothing drawing between the walk and that pass. Now the skip
itself drops it: `render_skipped` (timing.c) calls `pf_lt_render_skipped`,
which also drops a pending LightVerify sample. LightReuse's builds are kept.
None is keyed by frame: the key is everything `Add_Light` reads, and a
light's version is its fields compared in full at each snapshot. A light
the walk faded or moved gets a new version, and every build that kept it
misses. A build whose lights and own inputs are the same values (a
building no changed light reaches, or a light faded and back over two
skips) gives what the game gives, so reusing it is exact. The render
object's pointer only picks the slot.

**Extra frames.** Their lights are those the last game frame's main view
left, so every object whose own inputs didn't move (sphere, the
environment before the lists) reuses its build. Moving units interpolated
by FrameSmoothing rebuild, as their sphere differs. The camera doesn't
matter: `Pre_Render_Update` runs on every reuse. In a game frame, the
shadow map and the reflection come before the main view's walk and reuse
what the last main view built; only objects reached by a light the walk
changed rebuild. A light that did change in an extra frame would get a new
version all the same. The reuse columns count game frames only; CSV
`lt_extra_hits`, `lt_extra_misses` (appended, per row) count extra frames.

**The draw's cost (0.9.6).** The reference is `W3DDisplay::draw` timed at
`display_draw` on game frames. The `roo` wrap keeps its own return stack:
`pf_exit` hands it back before it reads the clock or touches the timing
stack, so an object's return can't end the draw's timing or drop a wrapped
call. The cuts make the draw cheaper, and the reference follows, as it
should. What runs only in game frames (the timers' clock reads, about 25 ns
per object; LightVerify's 8 samples per 5 s; the rebuilds after the walk)
puts the reference slightly above an extra frame's cost. It is a floor, so
the error is on the safe side: fewer extra frames, never a late from it.

Tests:

- `test_lights`: after a skip, an object decided before any pass start
  gets the faded and moved lights; the next frame reuses only builds whose
  lights didn't change. Over two skips a light faded and back is reused,
  and a real fade rebuilds. Over a game frame (shadow, reflection, walk,
  views), three extra frames and the next game frame, every result equals
  the game's: 6 of 6 extra-frame hits, and the misses only where the walk
  changed a light. A light changed in an extra frame is rebuilt. Without
  `pf_lt_render_skipped` two checks fail. `fps.c` isn't linked: the test
  says when a frame is extra.
- `test_catchup`: a render skipped through the real stub renews the snapshot
  (LightReuse on): the next object gets the walk's colour, not the
  stored build. It fails without the call.
- `test_hooks`: through the real stubs, ten 3 ms game draws, each with three
  objects through the `roo` wrap inside the views marker, give a reference
  of at least 2.9 ms. All 30 objects are timed and no wrapped call is
  dropped. With an object's return made to end the draw, it reads about
  1 ms and fails.
- `test_row`: the two columns.

## 0.9.8: the camera between the last two game frames

**The report (0.9.7, MaxFPS=120, FrameSmoothing=3).** "When I scroll the
camera, after I stop moving it, it moves a little bit." Motion sickness.

**Why.** Since `abc6e8c` (FrameSmoothing=1) an extra frame set the camera
*ahead* of the last game frame: `camera_ahead` drew
`cur + (cur - prev) * f`, f up to about 0.85 at 120 FPS. When scrolling
stopped, the extra frames of the last moving game frame went up to 0.85 of a
step past where the game stopped, and the next game frame put the camera
back: overshoot, then a jump back. A camera that slows down over a few
frames (the game's own easing, an edge scroll) overshot and came back on
each of them. FrameSmoothing=3 moved units between the last two game frames
but still the camera ahead.

**Why it seemed new.** The math is the same since 0.8.0 (0.8.6 only
changed what f is measured from). What changed was how much of a session
it ran in: until 0.8.4 the view update ran in extra frames, rebuilt the
camera and switched camera smoothing off ("the draw changed the camera",
0.8.3 log); `c44f1d2` (0.8.4) held the view update, so the extrapolation
stayed on. In 0.9.4 and 0.9.5 extra frames turned themselves off for the
session after 6 and 12 minutes (late frames); from `7ec51a2` (0.9.6) they
rest instead and the cost estimate keeps them on: the 0.9.7 session drew
extra frames in 203 of 211 seconds.

**Now.** The camera is never moved ahead, in every FrameSmoothing from 1:

- In the game's own draw, at the particle update (after the view update
  set the camera, the same place FrameSmoothing=3 lags units), the camera
  is set `g_cam_lag` of its step since the last game frame behind:
  `cur - (cur - prev) * lag` (`camera_lag`). When the draw returns the
  game's transform is put back byte for byte (`camera_back`); a draw that
  set the camera itself keeps what it set and camera smoothing goes off.
- An extra frame f of the way to the next game frame shows the camera
  `shown + (cur - shown) * f`, f clamped to 1 (`camera_between`): from what
  the game's draw showed to where the game has it, never further. When
  scrolling stops, `cur == prev`: the game's draw shows `cur` and the extra
  frames don't move it. A change of direction stays between the two game
  positions.
- The delay eases in and out with the units' (a quarter per game frame; out
  after 8 game frames without an extra frame), so without extra frames the
  camera is drawn as the game has it.
- A jump isn't blended: a step over 250 world units on an axis in one game
  frame, or an axis turned over about 25 degrees (`pf_mat_near`), is shown
  as it is (minimap click, a cut). Nor is a frame after one whose camera
  wasn't recorded.
- Without the `display_draw` hook the camera isn't moved at all.

Cost: the camera is 33 ms behind while extra frames run, like the units
with FrameSmoothing=3. Nothing logic, input or the network reads changes:
the transform is the game's again before the draw returns, as for units,
and the extra frames already drew with a moved camera. The view update
(the shadow light's fit, the terrain under the camera) still runs with the
game's camera, before the lag.

Tests (`test_fps`): scroll then stop (at once, and slowing over five
frames) at FrameSmoothing 1, 2 and 3: nothing drawn past where the game
stopped, never back, more than 20 frames between the game's positions; a
change of direction stays between 50 and 100; a jump to 5000 leaves nothing
drawn between; FrameSmoothing=0 and no draw hook draw only the game's
positions; the extra frame's or the game's draw changing the camera turns
smoothing off without stopping extra frames; the delay is -10 with extra
frames and 0 after they stop. On 0.9.7 the first four fail (drawn at 197.5
with the game stopped at 190, then back).

## 0.9.9: the catch-up notice only when catching up is sustained

**The report.** "The NoticeText is showing way too often." Until 0.9.8 the
notice showed for 2 s after every skipped render. In the 0.9.7 session
(211 s) 31 seconds had a skip, all but two of them a single one after one
60-90 ms frame (a hitch): the notice was up about a quarter of the time
for frames nobody would have missed.

**Now.** `pf_cu_notice` (catchup.c, no game memory) keeps the last 30
skips' times on the game's clock. A skip shows the notice when at least
`NoticeMinSkips` (default 6, 1..30) were skipped within the last 2 s;
while shown, half that many (rounded up) keep it, and it stays 2 s after
the last skip that did. So a sustained rate turns it on, a dip doesn't
flicker it, and a stutter's skip or two never shows it. `NoticeMinSkips=1`
is the old behaviour. Skipping itself doesn't look at it.

The default, from the logs: windows of 2 s with at least 6 skips were 0%
of the 0.9.7 session, 4% of the 31-minute 0.9.5 battle and 19% of the
heavy 0.9.4 one (where the game ran at 75-89% speed for minutes).

Tests: `test_catchup` (a skip a second for 30 s, every 2.5 s, one stutter:
never; every other frame: on at the 6th; a skip every 500 ms keeps it
without a flicker; every 1.5 s turns it off within about 3.5 s; a skip
every 350 ms: on without a flicker; 1 and 0 as before; the clock's wrap),
`test_config` (default 6, 0 -> 1, 99 -> 30).

## 0.9.10: the camera is never moved

**The 0.9.9 test (MaxFPS=120, FrameSmoothing=3, ExtraFrameShadows=1,
ReflectionInterval=2).** "The drift is still happening, and it's not a thin
strip that loses its shadow, it's a big black rectangular strip."

**Why the black strip.** The camera the plugin moved is the render
object's transform (`CameraClass::Set_Transform`). The game never moves it
that way: `W3DView::setCameraTransform` refits the shadow map's light camera
to the hull of the ground the view can see and hands it to the terrain's
`updateCenter`, so the shadow map and the terrain's loaded area cover that
camera and little more (see "How the game builds the shadow map"). 0.9.9
drew every game frame from a camera one step behind, and the extra frames
between that and the game's: the ground behind the scroll was outside both,
a band as wide as one frame's scroll. Earlier builds had the same on the
leading edge, in extra frames only and up to 0.85 of a step.

**Why the drift stayed.** Not proven frame by frame (the CSV has no camera
positions), but every camera smoothing so far re-timed the game's own
camera: ahead (0.8.x-0.9.7) it overshot and came back; behind (0.9.9) it
went on gliding for a frame after the game's stopped, on top of whatever
easing the game's camera does itself. The user's "it wasn't happening
before, even with a higher max fps" matches the sessions where camera
smoothing was off: before 0.8.4 the view update in extra frames rebuilt the
camera and the plugin switched camera smoothing off.

**Now.** The plugin never moves the camera, with any FrameSmoothing. Extra
frames draw from the camera the game's last view update set, which is what
the shadow map and the terrain were fitted to, so neither problem can come
from the camera. Unit smoothing (2, 3) is unchanged. FrameSmoothing=1 now
moves nothing (as 0). An extra frame whose draw moved the camera anyway is
put back and extra frames stop ("an extra frame's draw moved the camera").
`camera_lag`, `camera_back`, `camera_between`, `pf_mat_near` are gone.

Tests (`test_fps`): over a scroll that stops and one that slows down, at
FrameSmoothing 0 to 3, every frame drawn (the game's and over 80 extra
ones) shows the camera exactly where the game has it; with FrameSmoothing=3
the camera stays the game's while a unit is drawn between game frames; an
extra frame's draw that moves the camera stops extra frames and the camera
is the game's again.

## Later: skeleton pre-pass (was Milestone 2)

Put off on 2026-10-13: the skeleton update during the render stayed under
the 10% gate (5.2% light, 6.2% at 893 animated objects). The design below
is kept for when the render passes are cheaper and the skeleton share has
grown.

**When.** Once per drawn frame, at the R2 hook point. Not in the reflection
pass: by then the poses are already valid.

**What.** The pre-pass walks `RenderList` and picks each top-level object
that is visible, animated and has an out-of-date hierarchy, and whose motion
mode is `SINGLE_ANIM` or `BASE_POSE` in the first version. It skips:

- slaved objects and objects whose container isn't ready;
- `DOUBLE_ANIM` (blends) and `MULTIPLE_ANIM` (combos), in the first version;
- anything R4 marks as main-thread only.

Skipped objects get their pose on the main thread during the draw, as today.
Each picked object gets one call to the game's own
`Update_Sub_Object_Transforms`, the call its draw would have made in the same
frame, so each object gets the same number of updates as without the DLL.

**Grouping.** Picked objects are grouped by animation pointer, and all of a
group goes to one worker, so two threads never write the same animation's
caches. Groups are dealt out largest first to balance the load.

**Fork and join.** The main thread wakes the workers, takes a share of the
groups itself, and waits until every group is done. Nothing else in the game
runs during the pre-pass, so no game code can see a half-finished pose. The
draw then finds the hierarchies valid and skips the work.

**Floating point.** Before each batch, the workers load the main thread's
x87 control word and MXCSR (R5), so they compute the same bits as the main
thread would.

**Workers.** One fewer than the logical cores, at most 4, created on the
first pre-pass. Between frames they sleep on an event; they never spin. If
creating them fails, the pre-pass stays off for the session and the log says
so.

**Verify mode.** `Verify=1` also copies each picked object's pose after the
workers finish, marks the hierarchy invalid, recomputes it on the main thread
and compares the pivots bit for bit. Any difference is logged with the
object's model name. For replays only, not multiplayer: it costs more than
the pre-pass saves.

## Why multiplayer stays in sync

Each player's game already computes these poses at different moments, in a
different order and for different objects, depending on what is on that
player's screen. So game logic can't depend on when a pose is computed. The
pre-pass keeps the same values (same function, same floating-point
settings) and the same number of updates per object per frame. It changes
only which core does the work and when within the frame.

## Settings: `bfme2perf.ini`

```ini
[Perf]
; Master switch. 0 = load, hook nothing.
Enabled=1
; 1 = write bfme2perf.log on every start.
Log=1

[Timing]
; 1 = write bfme2perf_timing.csv, one row per second.
Enabled=1

[CatchUp]
; 1 = skip a frame's render when the game is behind its schedule (Milestone 8).
Enabled=1
MaxSkips=1
Notice=1
NoticeText=Catching up
NoticeMinSkips=6

[Skeleton]
; Workers for the pose pre-pass. 0 = off (default in the first builds).
; -1 = one fewer than the logical cores. 1..4 = that many. At most 4.
Threads=0
; 1 = recompute every pose on the main thread and compare. Testing only.
Verify=0
```

A missing INI means every setting is at its default.

## Logs and errors

`bfme2perf.log`, next to the DLL in `mss\`: DLL version; whether the
faction unlocker's `dinput8.dll` is loaded too (read only); a hash of the
executable and the game
detected (Realms or AotR, from the command line and the folder); the settings
read; each hook with the address its signature matched, or why it was
skipped; worker count; and, in Verify mode, any differences.

- A signature that is missing or matches more than once turns off every
  feature that needs it; the game starts normally.
- Hooks that depend on each other are installed together or not at all
  (the pre-pass needs the render-scene hook and the update function).
- The DLL can't catch a crash inside a worker. That's why the pre-pass is
  off by default in the first builds and Verify mode exists.

## Testing

**Automated** (in `perf/`, like the unlocker's): the signature scan finds
every R1 site exactly once in `game.dat`, and reports the expected misses
in `game820.dat` and 1.06; the INI parser; the grouping (same animation,
same worker); the `RIB_Main` export.

**Benchmark setups.** The same replay, at normal speed, with DXVK in all of
them. The unlocker's files stay in place; it is switched with its own
`Enabled` setting in `dinput8.ini`.

| Setup | Unlocker `Enabled` | `mss\bfme2perf.asi` |
|---|---|---|
| Plain game | 0 | absent |
| Unlocker only | 1 | absent |
| Performance only | 0 | present |
| Both | 1 | present |

Without the performance DLL there is no CSV; the frame rate comes from
DXVK's HUD. The other two setups write the CSV.

**Replays.** New recordings of big battles against the AI with the current
Realms install, with a higher command-point limit. The Realms data must not
change between measurement rounds: changed data makes old replays stop
playing back. The camera is left where the replay puts it; milliseconds per
animated object on screen stays comparable when the view differs. The first
test run checks whether a replay controls the camera.

The first benchmark replay is `Test Replay.BfME2Replay` in
`%APPDATA%\Age of the Ring\Replays`: `map mp iron foothills`, 3,379 logic
frames (about 11 minutes), the user against two AIs, recorded 2026-10-13
with the current install. The game has no command-line option to play a
replay, so it is started from the Replays menu.

**Same results**, in this order:

1. The replay's own sync check, with the pre-pass on and off.
2. `Verify=1` on the same replay: no differences.
3. A game between the user's PC with the DLL and the laptop without it,
   against the AI, until a big battle has happened.

## Open questions

- Whether replay playback moves the camera (first test run).
- Whether 2.01's pose code matches 1.06's shape closely enough for R3's
  field offsets (Phase R).
- How much skeleton work happens outside the draw (Milestone 1 numbers).
  The pre-pass can't help that part.

## Later work (not built now)

- Blends and combos in the pre-pass, once R4 covers them.
- Changes that need every player on the same version: AttackPriority target
  search, the pathfinder queue, the game-state CRC. They need a version
  guard so mismatched versions can't play together, and a replay CRC
  comparison for every change.
- Re-enabling `Prepare_LOD`, skipping work for off-screen drawables, the
  frame limiter's `Sleep(0)` spin.
