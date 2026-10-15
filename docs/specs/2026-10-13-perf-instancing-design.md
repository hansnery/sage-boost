# Performance DLL: instancing unit meshes (Milestone 6)

Status: designed and built 2026-10-13; off-line and device tests pass, not
yet run in the game (the user tests at the end). Branch
`feat/perf-instancing`, plugin 0.7.0, installed with `Instancing=0`. The
main spec is `2026-10-13-perf-dll-design.md`; this file only adds to it.

## Goal

Draw many copies of the same unit mesh with fewer draw calls, in the main
view, the shadow map and the water reflection, behind
`[Render] Instancing=0|1` (default 0: no hook is installed). Client-only and
render-only: nothing here touches logic, its schedule or the network, so a
player with the plugin stays in sync with players without it.

## What the research found

### How a unit mesh is drawn today

`FXShaderRenderer::PerformRendering` (AotR 0x00173BEB, spec Milestone 4)
walks records sorted by mesh model. A *group* is a run of records whose
meshes pass `Is_Rendering_Identical` (AotR 0x00149430) and share the same
rendering methods. That test compares, by value, the light environment
(`mesh+0xC8`), a colour vector and the opacity scalar; it doesn't compare
models (the sort puts equal models next to each other).

Per group, the game sets **once, from the first mesh**: the light
environment (`DX8Wrapper::Set_Light_Environment`), the fog colour, the
opacity scalar and the "inherited render data" (house colour and the like),
then `Begin`/`BeginPass` the method's effect. Then for each mesh,
`MeshClass::Render_With_FX_Material` (AotR 0x0014A1F0):

- skinned (`model+0x18` & 0x400): the SAS skeleton source gets
  `(Container->Get_HTree(), &geometry->boneMap, geometry->boneCount)`
  (`geometry = model+0xBC`, bone map a `vector<short>` at `+0x14`, count at
  `+0x20`); the world matrix isn't used by the skinned shaders;
- rigid: the world matrix is the mesh's `Transform` (`mesh+0x18`, a
  `Matrix3D`), unless the model is ALIGNED/ORIENTED (camera-facing);
- `method->slot4(1)` (commit), `Begin_Rendering(mesh+0x300)` (vertex and
  index buffers), `Draw_Triangles`, then the skeleton source is cleared.

So **inside one group, the only per-mesh inputs are the bones (skinned) or
the world matrix (rigid).** Everything else the shaders read is the same for
every mesh of the group, by the game's own construction.

`DX8Wrapper::Draw_Triangles` calls `BfmeDrawOps::Draw` (AotR 0x0011FD40),
which applies the pending render state and calls
`IDirect3DDevice9::DrawIndexedPrimitive` through the game's device pointer
(`call [edx+0x148]` at 0x0011FEC5).

### The bones are a straight copy

An HTree (`Name[16]`, pivot count `+0x10`, pivots `+0x14`, stride 0x58)
keeps each pivot's world transform at pivot `+0x30` as a rotation
quaternion and a translation (28 bytes). The SAS source uploads exactly
that, plus a zero, as the shader's `WorldBones[i] = { Rotation,
Translation_Zero }` (2 registers per bone). So the plugin copies 8 dwords
per bone and does no floating-point work on the CPU.

### The shaders (studied, not copied)

All unit materials end up in `shaders\compiled\defaultw3d.fxo` in
`rotwk\Shaders.big` (Age of the Ring and Realms don't override it). Its
shaders are shader model 1.1 and 2.0, chosen per pass by parameters
(`NumJointsPerVertex`, shadows, texture-coordinate mapping, …). The
high-quality technique uses two vertex shader families and the shadow map
a third:

| Family | EA variants | Inputs | Bones |
|---|---|---|---|
| lit, shadow map on | 18 (`vs_2_0`) | position(s), normal(s), blend indices/weight, uv0, uv1, colour | `WorldBones[90]` |
| lit, shadow map off | 18 (`vs_2_0`) | same | `WorldBones_L[32]` |
| shadow-map depth | 3 (`vs_1_1`) | position(s), blend indices/weight, uv0, colour | `WorldBones[90]` |

Each family has rigid, 1-joint and 2-joint variants, and the lit ones six
texture-coordinate mappings (none, `TexCoordTransform_0`, texture
animation 0, `TexCoordTransform_1`, and the two combinations). All lighting
is per vertex: ambient, three directional lights with specular and one
point light, then fog, shroud and cloud coordinates, the shadow-map
coordinates and the first light's share (used by the pixel shader to darken
shadowed pixels). **The pixel shaders read no per-object constant** (only
the shadow-map texel size and the fog colour, both per group).

## Design

### Keep the game's pixel shader, replace only the vertex shader

For a group the plugin can handle, the first mesh (the *leader*) is drawn
by the game as usual. That leaves the device fully set up for the group:
the game's pixel shader, textures, samplers, render states, vertex and
index buffers, and every vertex shader constant (lights, camera, fog,
material, the preshader results). The other meshes are not drawn by the
game; their bones (or world matrices) are copied into an instance array,
and drawn in as few draws as fit, with:

- **our vertex shader**, equivalent to the game's variant but reading each
  instance's bones from an instance region of the constant registers;
- a vertex declaration = the game's + one `FLOAT1 TEXCOORD7` element from
  an extra stream: each instance's base register in that region;
- stream-frequency instancing (`D3DSTREAMSOURCE_INDEXEDDATA | n` on the
  mesh's streams, `INSTANCEDATA | 1` on ours);
- the leader's exact `DrawIndexedPrimitive` arguments.

Afterwards the game's vertex shader, declaration, stream and frequencies
are put back exactly. The pixel side is untouched, so house colour, shadow
sampling, shroud, cloud, fog blending, alpha and opacity are drawn by the
game's own pixel shader.

Our vertex shaders keep the game's register layout for everything they
share (they read the constants the game already uploaded) and put the
instances in the largest range of registers the game's variant doesn't use
for anything else. Instances per draw = that range ÷ registers per
instance:

| Family | Instance range | Per instance | Instances per draw |
|---|---|---|---|
| lit + shadow, skinned | c0–c179 (90 bones) | 2 × bones | 90 ÷ bones (4 for a 22-bone body) |
| lit, skinned | c91–c255 to c99–c255 (157–165 registers) | 2 × bones | 78–82 ÷ bones |
| shadow map, skinned | c0–c179 | 2 × bones | 90 ÷ bones |
| any rigid | c22–c44 up to c255 (212–234 registers) | 3 (world rows) | 70–78 |

`D3DCAPS9.MaxVertexShaderConst` must be at least 256 (DXVK: 256);
otherwise instancing stays off.

### Shaders: written from scratch, generated per variant

`perf/shaders/inst.hlsl` is one source written for this plugin (no EA
code), with the family, joints and mappings as defines. `tools/inst_shaders.py`
(dev-time only) reads the user's local `defaultw3d.fxo`, takes each EA
variant's register numbers from its constant table and the order of its
preshader outputs, compiles our HLSL with Windows' `d3dcompiler_47.dll` to
`vs_2_0`, and writes `perf/src/inst_shaders.h`: for each variant, a hash of
the EA bytecode (FNV-1a, 32 bits, plus its length), our bytecode, the
instance range and the bone limit. No EA bytecode is stored or shipped;
the hash only recognises the shader the game bound.

Recognising the variant at run time: the bound vertex shader's bytecode
(`GetFunction`) is hashed once per shader object and looked up. **Any
shader not in the table means "not instanced".**

### Hooks (both found by signature, unique in `game.dat` and `game820.dat`)

1. `inst_mesh`, a call hook on the chunk-of-one `Render_With_FX_Material`
   call in `PerformRendering` (0x00173DAD; signature of 227 bytes from
   0x00173CD5, site at offset 216, which pins every frame offset read).
   From the saved registers: the mesh (ECX), the method (by value at
   `[esp+4]`, `AddRef`ed by the caller and released by the callee), the
   current record (EDI), the group's first record (EBX); from the frame:
   the group's end `[EBP+8]`, the pass `[EBP-0x14]`, the pass count
   `[EBP-0x24]`, the game's own instance limit `[EBP-0x2C]`. A mesh the
   plugin draws continues at a `ret 8` thunk after the plugin releases the
   method reference, as the callee would have.
2. `inst_dip`, the `call [edx+0x148]` (`DrawIndexedPrimitive`) in
   `BfmeDrawOps::Draw` (0x0011FEC5, 6 bytes, rewritten as `call stub;
   nop`; signature of 75 bytes from 0x0011FE82, site at offset 67). While a
   leader is drawn it records the device, the draw's arguments, the bound
   vertex shader and declaration and the stream frequencies, then
   continues at `[edx+0x148]` as the original did. Otherwise it only
   passes through.

The patcher gets one new kind for the second hook (an indirect call
replaced by a direct call plus a `nop`).

### Which groups are instanced

A group is a candidate when it has at least 3 meshes, its method has one
pass (multi-pass methods could depend on equal depth between passes) and
the game's own instancing doesn't apply (`maxInstances` ≤ 1). Then, after
the leader is drawn:

- exactly one `DrawIndexedPrimitive` came from the leader, with no stream
  frequency set;
- the bound vertex shader is a supported variant whose category isn't
  switched off by the verify mode;
- the declaration has no `TEXCOORD7` and a free stream for ours.

Each following mesh joins when it has the leader's model and base vertex
offset and, if skinned, a valid HTree, the geometry's skeleton flag clear
and every bone index inside the HTree. A mesh that doesn't join is drawn
by the game, and the rest of that group too (its draw changed the bound
buffers).

### Verify mode (`InstancingVerify=1`)

Every 5 seconds a verify window opens for up to 6 groups (2 per
category). In a sampled group every mesh is drawn by the game (the screen
stays stock), and at its last mesh the plugin:

1. saves the render target, depth buffer, viewport and scissor rectangle;
2. draws the group's meshes after the leader through the game's own
   `Render_With_FX_Material` into a 512×512 off-screen target with its own
   depth buffer, cleared to black and far;
3. draws the same meshes with the instanced path into a second target;
4. restores everything, reads both back and compares them as bytes.

A pixel is *covered* when either image differs from the clear colour, and
*bad* when a channel differs by more than 24 of 255. A sample fails when
more than 2% of the covered pixels are bad; an empty sample doesn't count.
A category (one EA vertex shader variant) is switched off for the session
at its second failed sample. Off-screen surfaces are created and released
inside each sample, so nothing in the default pool is held across a
device reset.

### Safety

- `Instancing=0`: neither hook is installed.
- Any D3D call that fails while setting up (shader, declaration, buffer)
  turns instancing off for the session, logged; a failure in a group's
  draw falls back to the game for the rest of that group.
- Device reset and alt-tab: the plugin's objects are a vertex buffer in
  the managed pool, shaders and declarations, none of which a reset
  invalidates; a new device pointer drops every cached object.
- Only the thread that first reaches either hook (the render thread) is
  ever handled; any other passes through.
- No floating point in the plugin: bones and matrices are copied as
  dwords; instance base offsets are integer bit patterns of small floats.

### Counting (CSV columns appended at the end of each row, per frame)

`inst_meshes` (drawn by the instanced path), `inst_draws` (instanced draw
calls), `inst_groups`, then the meshes left to the game in candidate and
non-candidate groups, by reason: `inst_small` (group under 3),
`inst_multipass`, `inst_game_inst` (the game's own instancing),
`inst_no_shader` (vertex shader not in the table or no shader),
`inst_disabled` (category switched off by verify), `inst_mismatch` (mesh
differs from its leader or the leader's draw wasn't captured), and
`inst_leaders`. Then `verify_samples` and `verify_bad` (whole row).

The log lists every unsupported shader hash the first time it's seen
(capped), each verify sample of the first few per category and every
failed one, and a per-minute summary per category.

0.8.1: a mesh of another model part-way through a group used to send the
rest of the group to the game. Now, with at least three meshes left
(itself included), it leads them: the instanced meshes so far are drawn,
the game draws it, its draw is captured and the meshes after it are
checked against it, exactly as for a group's first mesh. With fewer left
they stay `inst_mismatch`. The per-minute summary adds a line splitting
`inst_mismatch` by kind (method with under two references, geometry
without its own vertex buffer, camera-facing, bad bone map, leader not
exactly one draw, device state at capture, model change with too few
left) and the number of splits.

## Expected saving

From the 0.3.0/0.6.0 stress tests (spec, Milestones 1b and 4): per heavy
frame about 9,600 FX mesh draws in three passes, in about 1,430 groups
(6.7 meshes per group), at 0.7–1.3 µs of per-mesh work each (commit,
buffers, draw through DXVK). The plugin keeps every group's setup and the
leader, and replaces the other meshes (about 85% of them, ~8,200) by a
copy of their bones plus a share of one instanced draw per 4–70 meshes.
At an estimated 0.15–0.3 µs per instanced mesh, that removes roughly
0.5–1 µs from each: **about 4–8 ms off a 51 ms heavy frame** if most
groups are covered, less if common shaders turn out unsupported. The CSV
columns measure the coverage directly.

## What may look different

- Tiny floating-point differences in vertex positions and lighting (our
  shader computes the same formulas in a different instruction order). The
  verify mode measures them; they should be well under one 8-bit step.
- A material whose look depends on per-mesh state we didn't find would
  look like its group's first mesh. The verify mode is meant to catch that
  and switch the category off.

## Not covered (left to the game)

`normalmapped.fxo`'s high-quality shaders (tangent frame, or not a unit
family; Low quality is covered since 0.9.4) and every other effect,
camera-facing meshes, groups
under 3 meshes, multi-pass methods, the game's own instanced path, and
anything that isn't drawn by `PerformRendering` (terrain, particles,
trees, the DX8 fallback path).

## Testing

- `perf/tests`: the site signatures, the patcher's new kind, the CSV
  columns, the INI keys and the instancing state machine against a fake
  batch function and a fake device.
- `perf/tests/inst_gpu`: an off-line GPU check (not shipped) that creates a
  hidden D3D9 device, draws synthetic meshes with each EA vertex shader
  variant read from the user's local `Shaders.big` (one draw per instance)
  and with ours (one instanced draw), and compares every vertex output
  through a debug pixel shader. Run against Windows' `d3d9.dll` and
  against DXVK's. Variants that don't match aren't put in the table.
- `perf/tests/test_inst.c` (part of `make test`): the instancing state
  machine on a hidden D3D9 device, driven exactly like the hooked call
  sites, with fake meshes, models and HTrees and a fake
  `Render_With_FX_Material` that draws with the game's own skinned shader.
- In game (the user): a stress test with `Instancing=1` and
  `InstancingVerify=1`, then optionally the same with `Instancing=0`.

### Results before the game test (2026-10-13)

- Generator: 39 of the 57 `defaultw3d.fxo` vertex shaders have a
  replacement (lit with and without the shadow map, the shadow-map depth
  pass; each rigid, 1- and 2-joint, with every texture mapping). The 18
  medium-quality `vs_1_1` lit shaders are refused (different preshader).
  `shadow.fxo` has no skinned shader, so units in the shadow pass use
  `defaultw3d`'s depth technique, which is covered.
- `make inst-gpu`: all 39 match the game's shaders on Windows' `d3d9.dll`
  and on DXVK's (the user's 2.x build), over 333 compared outputs; worst
  difference 0.00028 (one 8-bit step is 0.0039). This also shows that
  stream-frequency instancing with `vs_2_0` shaders works on DXVK.
- `test_inst` on both: the instanced frame equals the stock one (no pixel
  off by more than 2 of 255), method references balance, the game's vertex
  shader, declaration and frequencies are put back, the reasons add up
  (small, multi-pass, game instancing, a different model mid-group), and a
  verify sample reports a match (largest difference 0–1 of 255).
- The plugin still imports only KERNEL32 and contains no x87/SSE
  instruction. Both new sites match once in `game.dat` and `game820.dat`
  at 0x00173DAD and 0x0011FEC5.

### The bone list and its length (0.8.8)

In the user's games most refused meshes were "bone map: list length
differs from the count" (0.8.3, by the end: 36.5 million meshes refused
against 22.1 million instanced; 0.8.5 run 2: 17.7 against 6.7 million),
always a count of 1 or 2 with a list of 2–15. Correction to "How a unit
mesh is drawn today": the skeleton source call is
`(HTree, &geometry->boneMap, geometry +0x20)` (AotR 0x0014A4A4–0x0014A4BE,
setter 0x0014CD48 storing them at the source's `+0xC8`, `+0xCC`, `+0xD0`),
and the effect parameter handlers it feeds show what each is:

- `NumJointsPerVertex` (handler 0x0014D48D): `SetInt` with the third
  value, geometry `+0x20`. **It is the joints per vertex**, 1 or 2, which
  selects the 1- or 2-joint shader variant; not a bone count.
- `NumJoints` (handler 0x0014D8C9): `SetInt` with the bone list's length,
  `(end − begin) / 2`.
- The bone uploads (0x0014DA2x for `WorldBones`, quaternion and
  translation; 0x0014DC62 and 0x0014E04A for the matrix forms) all loop
  over every entry of the list, `WorldBones[i] = pivot[boneMap[i]]`, after
  checking that the list fits the parameter's elements and that the last
  index is below the HTree's pivot count; none reads `+0x20`.

So vertex blend indices select entries of the whole list, which is what
the plugin already uploads per instance (two registers per entry, the
instance's base in `TEXCOORD7`); only the check was wrong. Since 0.8.8 a
skinned leader is accepted when its list has entries (at most 128), its
`+0x20` is 1 or 2 and equal to the bound variant's joints (else a new
mismatch kind: "joints per vertex not 1 or 2" or "joints unlike the bound
shader's"); the existing limits stay (the list must fit the game's palette,
`WorldBones[90]` or `WorldBones_L[32]`, and two instances must fit a draw).

Safety net: groups whose list length differs from the joints are verified
on their own. Until 60 of them have been sampled, other groups get at most
half of each verify window; two bad samples switch off only these groups of
that variant (the variant's other groups keep instancing). `test_inst` now
draws a 6-entry list with 1 joint per vertex on the real device: the
instanced frame equals the stock one, and the verify sample matches
(largest difference 1 of 255).

Expected effect: in the heaviest minutes of 0.8.5 run 2 a frame had about
1,260 mismatch meshes (some 950 of them bone map) against 370 instanced;
moving about 850 of them (one leader per group stays with the game) saves
the per-mesh draw cost measured in Milestone 4 (0.7–1.3 µs each), about
0.6–1.1 ms of a 12.8 ms render.

**Device state.** The other mismatch that grew late in a game (0.8.3: 0 for
5 minutes, then 9.4 million). Every path that counted as "device state" is
now its own kind in the log: shader skinning unlike the mesh's, vertex
declaration not usable, declaration cache full, stream frequency set,
stream not readable. The likely one is the declaration cache: it had 64
entries and refused every group once full. It now has 256, and both caches
(declarations and shaders) hold a reference on the game's object, so a
released object's address can't come back as another one while it is
cached.

**No shader.** Only three vertex shaders were ever logged as not in the
table (`CF3571C7`, `A2B3863E`, `94092159`, the normal-mapped ones), so they
are all of "no shader": 0.38 of about 110 million meshes in the 0.8.3
game (0.3 %), 0.81 of 49 million in 0.8.5 run 2 (1.7 %). Not worth their
own shaders yet; the summary now counts meshes per unknown shader.

CSV, appended: `inst_bonelist` (instanced meshes per frame whose list length
differs from the joints, also counted in `inst_meshes`). Log, each minute:
the same total, the bone-map and device-state kinds, meshes per unknown
shader, and per variant the verify results of these groups.

## The shadow-map pass (0.9.0)

The user's 0.8.5 run 2 (`%TEMP%\bfme2perf-0.8.5-run2-0215`, ShadowInterval=1)
spent about 10 ms of a 38.7 ms frame in `UpdateShadowMap` in its heaviest
tenth of seconds (render 22.5 ms), with about 2,030 FX meshes a frame in the
shadow pass against 1,930 in the main view. This section is what the shadow
pass does with unit meshes, what the instancing already did there, and what
0.9.0 adds.

### How the shadow pass draws units (static, AotR = 2.01)

- **The pass.** `W3DShadowMapManager::UpdateShadowMap` (0x0007D5C9, the
  callee of the `shadow` site 0x00049DF5) builds a `RenderInfoClass` for the
  light camera (manager `+0x00`), pushes an override material-pass object
  into it (vtable 0x007DCB44), raises WW3D's "rendering the shadow map"
  byte (0x00DD1E44), sets the render target to the shadow texture's level 0
  (`+0x14`) with its own depth buffer (`+0x1C`), clears both
  (`WW3D::Begin_Render`, colour 1.0), draws the whole scene with
  `WW3D::Render(scene, rinfo)`, runs the optional post-process target
  (`+0x18`), calls `End_Render(false)`, restores the default render target
  and lowers the byte.
- **The render target** is `D3DFMT_R32F` (created through 0x0011DAC0 with
  format 0x72 at 0x0007D419), square, of side `[0x00D9952C]` (1024 as
  initialised); the depth
  buffer is a `D3DFMT_D24S8` surface of the same side
  (`CreateDepthStencilSurface` at 0x0007D4CC). The map holds depth as
  colour.
- **Which meshes.** `RTS3DScene::Render` runs `Visibility_Check` with the
  light camera and draws each visible object through `renderOneObject`,
  which computes its light environment as in the main view.
  `MeshClass::Render` (0x0014B500) returns at once for a mesh without an FX
  material while the byte is set (0x0014B773: nothing goes into the shadow
  map through the DX8 path); FX meshes take the same path as in the main
  view: the light environment from the `RenderInfo`, the FX shader's
  methods for the current pass, and a record queued in the FX batch
  (0x0017430D). So **the shadow pass's unit meshes go through the same
  `PerformRendering` grouping** (`Is_Rendering_Identical`: light
  environment, colour vector and opacity by value, the same methods) and
  through the same chunk-of-one `Render_With_FX_Material` call, where
  `inst_mesh` sees them.
- **The shaders** are `defaultw3d.fxo`'s shadow-map technique: vertex
  shaders 4, 5, 6 (`vs_1_1`, rigid, 1-joint, 2-joint) and pixel shaders 0–3
  (`ps_2_0`). 0 and 1 write `z/w` (`oT1.x`) to every channel; 2 also kills
  the pixel where the vertex alpha × opacity is under 0.376; 3 does the same
  with the texture's alpha. The vertex shaders read `WorldBones[90]` (c0–c179)
  or `World`, `View` (c190), `Projection` (c194, the light camera's) and
  `TexCoordTransform_0` (c198), plus preshader results: opacity ×
  `OpacityOverride` (c180), the texture offsets (c181, c187) and a
  `BlendMode == 2` flag (c189: additive meshes are moved off the map, they
  cast no shadow). The pixel shaders read no constant. **Per mesh only the
  bones or the world matrix change**, as in the main view; the lights and
  the colour vector the game groups by aren't read at all.
- `SetShadowMapInfo` (the `shadow_info` site) feeds the receivers in the
  main view; nothing of it is used while the map is drawn. Render states
  come from the technique's pass (`Begin`/`BeginPass`), the same for every
  mesh of a group; the instanced draw changes only the vertex shader, the
  declaration, stream `top + 1` and the frequencies, and puts them back.

### What 0.7.0 to 0.8.8 already did there

The three shadow-map vertex shaders are in the table since 0.7.0, and the
pass reaches the same hooks, so **the shadow pass has been instanced all
along**. In the 0.8.5 run the variants of shaders 4 and 5 were about half of
the instanced meshes (4.0 of 8.0 million in the per-variant log lines),
with 309 and 334 verify samples, none bad. Two things were missing:

- The verify drew them into an 8-bit A8R8G8B8 target: the depth as an 8-bit
  colour, judged with a tolerance of 24/255, about 9% of the depth range,
  which can't show that a shadow map is equal.
- Nothing told the passes apart: `inst_*` mixed the shadow map, the
  reflection and the view, and the per-variant mesh counts in the log
  included extra frames (the totals didn't).

From the CSV and the log, about 13% of the shadow pass's meshes were
instanced in that run (all passes: 11%); the rest were refused like the
main view's: "mismatch" 61% (three quarters of it the bone-list length
check that 0.8.8 removed, most of the rest "device state"), "small" (groups
under 3) 24%.

### What 0.9.0 adds

- **The pass is known.** `passflag.c` finds the shadow-map byte from three
  signatures, `UpdateShadowMap` raising it, lowering it and
  `MeshClass::Render` testing it; each matches once in `game.dat` and
  `game820.dat` and all three must name the same byte (RVA 0x009D1E44).
  `inst.c` reads it at the start of every group. Without it, a group whose
  leader bound a shadow-map shader counts as the shadow pass.
- **`[Render] ShadowInstancing`** (default 1, only with `Instancing=1`): 0
  leaves every mesh of the shadow map to the game (reason
  `shadow_inst_off`) and keeps the other passes instanced.
- **The verify draws in the shadow map's own format.** At a sample the
  bound render target's format is read (`IDirect3DSurface9::GetDesc`); for
  R32F both off-screen targets are R32F, cleared to 1.0 like the game's map,
  with a D24S8 depth buffer, and compared as depths: each texel is turned
  into fixed point with 24 fractional bits from its IEEE bits (no floating
  point in the plugin, `depthcmp.h`), covered where either isn't 1.0, bad
  where the two differ by more than 2^-16 of the depth range (an 8-bit step
  is 2^-8). Other formats are compared as before.
- **Categories per pass.** A verify category is a game shader in one pass,
  so two bad samples in the shadow map switch off that shader there only;
  the shadow pass, which comes first in a frame, gets at most 3 of a
  window's 6 samples.
- **Counters.** CSV, appended: the shadow pass's share of every `inst_*`
  column (`shadow_inst_meshes` … `shadow_inst_bonelist`, same order and
  units, `shadow_verify_samples` and `shadow_verify_bad` per row), then
  `shadow_inst_off` (left to the game by `ShadowInstancing=0`),
  `shadow_inst_split` (meshes of shadow-pass groups that continue the
  previous group's model and method, i.e. groups the game split by light,
  colour or opacity) and `shadow_inst_ms` (time in the shadow pass's
  instanced draws). Log, each minute: one line with the same for the shadow
  pass, and the per-variant lines per pass ("shadow-map pass"). The
  per-variant mesh counts now leave extra frames out, like the totals.

### Refused in the shadow pass

The main view's rules apply unchanged (each refused mesh keeps its reason,
now counted per pass): groups under 3, multi-pass methods, the game's own
instancing, shaders not in the table (the normal-mapped materials'
`normalmapped.fxo` shadow shaders; about 1–2% of all meshes), a mesh unlike
its leader, a leader that isn't exactly one draw, too many bones. Not built:

- **Merging across the game's groups.** The depth shaders don't read the
  light environment or the colour vector, so groups the game split only
  for those could be drawn as one. The 0.6.0 count says it's small: under
  12% of groups even share their method with the previous one, and a merge
  would save only the leader's draw of each (the game still sets up every
  group), well under 0.1 ms.   `shadow_inst_split` measures it in game before
  anything is built. Opacity can't be ignored (the alpha test reads it).
  0.9.3 instead lets the game itself merge them
  (`ShadowBatchIgnoreLight`, main spec Milestone 7). That saves each merged
  group's setup, not only its leader's draw.
- Groups of 2: one instanced draw instead of one game draw saves nothing.

### With ShadowInterval, ExtraFrameShadows and the reused map

- `ShadowInterval` > 1: a skipped redraw draws nothing, so there is nothing
  to instance; the columns are per game frame and so fall with the redraws.
- `ExtraFrameShadows=1`: an extra frame's shadow pass is instanced like a
  game frame's (the same hooks), but neither counted nor verified
  (`pf_inst_set_extra`). F3's moved units are drawn from their HLods'
  HTrees, which the HLod's render rebuilt from the moved transform before
  the meshes reach the FX batch; the instanced copy reads those pivots as
  the game's draw does, and F3 puts the transforms back after the draw.
- 0.8.7's reuse of an extra frame's map by the next game frame: that game
  frame draws no shadow pass (counted 0); the map it shows was drawn,
  instanced, by the extra frame.

### Expected saving

In the 0.8.5 run's heaviest tenth, per frame: about 2,030 shadow-pass
meshes, of which about 260 instanced (half of the 517 instanced in all
passes). With 0.8.8's bone-list change, about 940 more of them (the
shadow pass's share of the 2,090 bone-list refusals) become instanceable,
less one leader per group (groups average about 7 meshes): about 800 more
instanced, about 1,060 in all (52%). At the per-mesh cost measured in
Milestone 4 (0.7–1.3 µs, against an estimated 0.15–0.3 µs instanced), that is
**about 0.5–1.2 ms of the 10 ms `shadow_ms`**, 0.3–0.9 ms of it new since
0.8.5. The rest of the pass is per-group work (about 550 groups at 4–7 µs:
effect begin/end, the light environment, the method chain), the light-camera
visibility check and the skeleton updates, which the shadow pass pays for
as the frame's first pass. `ShadowInstancing=0` against 1 on the same replay
measures it directly; the `shadow_inst_*` columns give the coverage.

### Tests (2026-10-14)

- `make test`: all suites pass. `test_sites`: the three flag signatures
  match once in both executables at RVA 0x009D1E44. `test_config`: the key
  (default 1, 0, junk). `test_row`: the 18 new columns.
- `test_inst`, new: the game's shadow-map shader (5, 1-joint) and depth
  pixel shader (2) drawing nine skinned meshes into an R32F map with a
  depth buffer, with the shadow flag raised: the instanced map equals the
  game's (13,793 texels drawn, 13,539 bit-identical, largest difference
  2/2^24), everything is counted as the shadow pass; the same meshes as two
  groups of one model count 5 as split; `ShadowInstancing=0`, with and
  without the flag, leaves all nine to the game; a verify sample is taken in
  R32F and passes (48,805 texels, one edge texel over the tolerance). Plus
  the depth compare against the floats it stands for, and the new vtable
  slot and struct.
- `make inst-gpu` adds, for the three shadow-map variants, the pass as the
  game draws it: each of pixel shaders 0, 2 and 3 (with an alpha texture),
  R32F, depth test on, the game's shader one draw per instance against ours
  in one draw. On Windows' `d3d9.dll` and on DXVK the covered texels are the
  same in every case (alpha-tested ones too), the rigid variant is
  bit-identical, the skinned ones differ by at most 5/2^24.

## Low shader quality (0.9.4)

In the user's settings test on all-Low (2026-10-14, test 4), instancing
fell to nothing: the log named three vertex shaders "not in the table". The
game drew about 4,100 draws per frame, against 480 with Ultra shaders and
shadows and water off. Hashing every shader in `Shaders.big` identifies
them:

| Hash (size) | Effect, shader | What it is | Meshes in that session |
|---|---|---|---|
| `61D2DB05` (5,612) | `defaultw3d.fxo` 170 | lit, `vs_1_1`, 1 joint, no texture mapping | 651,406 |
| `531D2A8E` (6,188) | `defaultw3d.fxo` 171 | the same, 2 joints | 419,724 |
| `9D297B91` (4,040) | `normalmapped.fxo` 30 | the normal-mapped material's Low technique, rigid | 660 |

None of them is a high-quality normal-mapped shader. `CF3571C7`
(`normalmapped` 0: `vs_1_1`, world only, not a unit family), `94092159`
(1, its skinned twin) and `A2B3863E` (26: `vs_2_0` with tangent and
binormal inputs, which the hook doesn't supply as instance data) stay
refused, as before.

**Two new families.**

- **4, lit low** (`defaultw3d` 169–186, all 18): family 2's lighting
  (ambient, three directional lights with specular, the point light,
  vertex colour, house colour), shroud and every texture mapping, without
  fog. `oD1.w` is 1. `WorldBones_L` holds 32 bones.
- **5, normal-mapped low** (`normalmapped` 30 rigid, 31 one joint):
  diffuse only. The output is `(Σ lights · DiffuseColor + ambient) / 2` with
  `OpacityOverride` as alpha, the shroud coordinates and vertex fog in
  `oFog`. There's no vertex colour and no texture mapping.

Both keep the game's register layout. The instance range is the largest
range the variant leaves free (c26 or c27 up to c255 for rigid, c87 or c88
up to c255 for skinned), and three world rows or two registers per bone, as
for the other families. Each group already shares `DiffuseColor` (its
material) and `OpacityOverride` (the game's batch test compares opacity),
so the leader's constants hold for every instance. The generator adds
`DiffuseColor` and `OpacityOverride` to the roles and reads
`normalmapped.fxo` after `defaultw3d.fxo` (table ids 1000 + shader index).
The 39 earlier variants compile to the same bytecode as before.

**GPU harness.** All 59 variants pass on Windows' `d3d9.dll` and on DXVK.
The 20 new ones match on every output with differences of at most 1e-6.
`oFog` can't be read by a `ps_2_0` shader, so for family 5 the harness also
draws black with fixed-function vertex fog onto a white fog colour (red =
1 − fog). Every covered texel is fogged, and the game's shader and ours
agree to within 1e-6 (1,574 and 7,647 texels).

**Expected saving.** On Low, `inst_meshes` goes from 0 back to the share
seen with Ultra shaders: draws per frame from about 4,100 towards 500. Test 3
against test 4 put the CPU side at about 0.5 ms per frame. The verify mode
covers the new categories like the others, so the in-game check is a Low
session with `InstancingVerify=1`: no "not in the table" lines for these
three, and no verify failures.
