# Performance DLL, plan 2: Phase R research (R1–R6)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** answer R1–R6 of `docs/specs/2026-10-13-perf-dll-design.md` on Age
of the Ring's `game.dat`, so plan 3 (timing) can be written with real sites.

**Architecture:** one read-only Python tool, `tools/port_sites.py`, that
turns a function's first bytes in BFME2 1.06 into a wildcard signature
(call targets and absolute addresses masked) and finds it in ROTWK 2.01 and
Age of the Ring's `game.dat`. The rest is disassembly with the existing
`tools/disasm.py`, written up as findings sections in the spec. No DLL code.

**Tech Stack:** Python 3, `pefile`, `capstone` (as `tools/verify_sites.py`).

## Global Constraints

- Nothing under `C:\repositories\Open-BFME-2\` is written; it is read and read-only scripts run against it.
- Read-only outside this repository: nothing is written to `C:\Jogos\AgeoftheRing\` or `C:\Program Files (x86)\Realms in Exile Nightly\`.
- No code from SAGE-Unlocked (GPL-3) or bfme2-accelerator (MIT).
- No game file or EA asset is copied into the repository; tests build their own synthetic inputs.
- No hardcoded absolute addresses in the DLL: every site recorded here comes with a signature that matches exactly once.
- Conventional commits; branch `feat/perf-dll`.

## Inputs

| Name | Path |
|---|---|
| 1.06 exe | `C:\repositories\Open-BFME-2\baselines\bfme2\workshop-vanilla-1.06\files\game.dat` |
| 2.01 exe | `C:\Jogos\AgeoftheRing\rotwk\game820.dat` |
| AotR exe | `C:\Jogos\AgeoftheRing\rotwk\game.dat` |
| Decompilation | `C:\repositories\Open-BFME-2\Code` (1.06 RVAs in comments) |

## Starting points (1.06 RVAs, from the decompilation's comments)

| Function | 1.06 RVA | Source file |
|---|---|---|
| `GameLogic::update(int phase)` | 0x0024555A | `GameLogic/System/GameLogicInit.cpp` |
| `Win32GameEngine::update` | 0x00041EC3 | `Main/Win32GameEngineUpdate.cpp` |
| `setFPMode` | 0x00040EA9 | `GameClient/DisplayMovieFrameRva0025CF2A.cpp` |
| `RTS3DScene::Customized_Render` | 0x0007054F | `W3DDevice/GameClient/RTS3DSceneVisibilityCheck.cpp` (comment) |
| `RTS3DScene::Visibility_Check` | 0x00071245 | `W3DDevice/GameClient/RTS3DSceneVisibilityCheck.cpp` |
| `RTS3DScene::renderOneObject` | 0x0006FB59 | `W3DDevice/GameClient/RTS3DSceneFlushTranslucent.cpp` (comment) |
| `HLodClass::Update_Sub_Object_Transforms` | 0x0019D640 | `WW3D2/HLodUpdateSubObjectTransforms.cpp` |
| `Animatable3DObjClass::Update_Sub_Object_Transforms` | 0x001A5900 | `WW3D2/animobj.cpp` (comment) |
| `MeshClass::Render_With_FX_Material` | 0x0014A880 | `WW3D2/MeshClassRenderWithFXMaterial.cpp` |

`GameEngine::update` (the frame body that `Win32GameEngine::update` calls)
has no RVA in the comments; Task 2 finds it as the call target inside
`Win32GameEngine::update`.

---

### Task 1: the signature porter

**Files:**
- Create: `tools/port_sites.py`
- Test: `tools/test_port_sites.py`

**Interfaces:**
- Consumes: `verify_sites.Image` (`.mem` indexed by RVA, `.base`, `.exec_ranges`, `.pe`), `verify_sites.find_all(buf, start, end, pat)`.
- Produces:
  - `mask_code(code: bytes, va: int, lo: int, hi: int) -> list[int | None]`: the bytes of `code` (decoded as x86-32 at `va`) with `None` for the 4-byte target of every `call`/`jmp`/`jcc` rel32 and for every 4-byte immediate or displacement whose value is in `[lo, hi)`. Bytes of an undecodable tail are kept.
  - `pattern_text(pat: list[int | None]) -> str`: `"55 8B EC ?? ?? ?? ??"` style, the format `verify_sites.parse_pattern` reads.
  - `find_in(buf: bytes, pat: list[int | None]) -> list[int]`: every offset in `buf` where `pat` matches.
  - `mask_prefix(code: bytes, va: int, n: int, lo: int, hi: int) -> list[int | None]`: the first `n` bytes of `mask_code(code, …)`; `code` extends past `n`, so an instruction cut at `n` is still masked. `port` and the CLI mask `n + 15` bytes through it. (Added after the first run: 48-byte patterns that cut an instruction failed while 96-byte ones matched.)
  - `port(src: Image, rva: int, dst: Image, lengths=(24, 32, 48, 64, 96, 128)) -> tuple[int, list[int]]`: the first length whose masked pattern matches `dst`'s executable sections exactly once, and the hit list; if none is unique, the longest length and its hits.
  - CLI: `python tools/port_sites.py <src> <dst> name=0xRVA ... [--pattern]` prints one line per name: `name 0xSRC -> 0xDST len=N`, or `name 0xSRC -> ambiguous [0x..., ...] len=N`, or `name 0xSRC -> missing`; with `--pattern`, the masked pattern on the next line.

- [x] **Step 1: Write the failing test** `tools/test_port_sites.py`

```python
"""Tests for tools/port_sites.py on synthetic code (no game files)."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import port_sites  # noqa: E402

BASE = 0x00400000
LO, HI = BASE, BASE + 0x01000000


def func(call_rel: int, abs_va: int, imm: int) -> bytes:
    return (b"\x55\x8b\xec"                          # push ebp; mov ebp, esp
            + b"\xe8" + struct.pack("<i", call_rel)  # call rel32
            + b"\xa1" + struct.pack("<I", abs_va)    # mov eax, [abs32]
            + b"\x05" + struct.pack("<I", imm)       # add eax, imm32 (not an address)
            + b"\x0f\x84" + struct.pack("<i", 16)    # je rel32
            + b"\x5d\xc3")                           # pop ebp; ret


def test_masks_branches_and_addresses_only():
    pat = port_sites.mask_code(func(0x1234, 0x00DE4364, 7), BASE + 0x1000, LO, HI)
    assert pat[:3] == [0x55, 0x8B, 0xEC]
    assert pat[3] == 0xE8 and pat[4:8] == [None] * 4
    assert pat[8] == 0xA1 and pat[9:13] == [None] * 4
    assert pat[13] == 0x05 and pat[14:18] == [7, 0, 0, 0]
    assert pat[18:20] == [0x0F, 0x84] and pat[20:24] == [None] * 4
    assert pat[24:] == [0x5D, 0xC3]


def test_pattern_text_round_trips_through_verify_sites():
    import verify_sites
    pat = port_sites.mask_code(func(1, 0x00DE4364, 7), BASE, LO, HI)
    parsed, site = verify_sites.parse_pattern(port_sites.pattern_text(pat))
    assert parsed == pat and site == 0


def test_find_in_buffer_ignores_masked_bytes():
    pat = port_sites.mask_code(func(0x1234, 0x00DE4364, 7), BASE, LO, HI)
    buf = b"\xcc" * 50 + func(-0x999, 0x00DF0000, 7) + b"\xcc" * 50 + func(5, 0x00DE0000, 8)
    assert port_sites.find_in(buf, pat) == [50]


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("OK: port_sites tests passed")
```

- [x] **Step 2: Run it to see it fail**

Run: `python tools/test_port_sites.py`
Expected: `ModuleNotFoundError: No module named 'port_sites'`.

- [x] **Step 3: Write `tools/port_sites.py`**

```python
"""Research helper (not shipped): find a 1.06 function in another image.

The function's first bytes become a wildcard signature: the 4-byte targets
of rel32 branches and every 4-byte immediate or displacement that points
into the image are masked. The signature grows until it matches the other
image's executable sections exactly once.

    python tools/port_sites.py <src> <dst> name=0xRVA ... [--pattern]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import capstone
from capstone import x86 as cx86

sys.path.insert(0, str(Path(__file__).parent))
from verify_sites import Image, find_all  # noqa: E402

_MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_MD.detail = True
_REL_BRANCH = {capstone.CS_GRP_JUMP, capstone.CS_GRP_CALL}


def mask_code(code: bytes, va: int, lo: int, hi: int) -> list[int | None]:
    pat: list[int | None] = list(code)
    for ins in _MD.disasm(code, va):
        off = ins.address - va
        if any(ins.group(g) for g in _REL_BRANCH) and ins.size >= 5 and \
                ins.operands and ins.operands[0].type == cx86.X86_OP_IMM:
            for i in range(off + ins.size - 4, off + ins.size):
                pat[i] = None
            continue
        if ins.disp_size == 4 and lo <= (ins.disp & 0xFFFFFFFF) < hi:
            for i in range(off + ins.disp_offset, off + ins.disp_offset + 4):
                pat[i] = None
        if ins.imm_size == 4:
            for op in ins.operands:
                if op.type == cx86.X86_OP_IMM and lo <= (op.imm & 0xFFFFFFFF) < hi:
                    for i in range(off + ins.imm_offset, off + ins.imm_offset + 4):
                        pat[i] = None
    return pat


def pattern_text(pat: list[int | None]) -> str:
    return " ".join("??" if b is None else f"{b:02X}" for b in pat)


def find_in(buf: bytes, pat: list[int | None]) -> list[int]:
    return find_all(buf, 0, len(buf), pat)


def _find_image(img: Image, pat: list[int | None]) -> list[int]:
    hits: list[int] = []
    for start, end in img.exec_ranges:
        hits += find_all(img.mem, start, end, pat)
    return hits


def _image_range(img: Image) -> tuple[int, int]:
    return img.base, img.base + img.pe.OPTIONAL_HEADER.SizeOfImage


def port(src: Image, rva: int, dst: Image,
         lengths=(24, 32, 48, 64, 96, 128)) -> tuple[int, list[int]]:
    lo, hi = _image_range(src)
    hits: list[int] = []
    n = lengths[-1]
    for n in lengths:
        pat = mask_code(bytes(src.mem[rva:rva + n]), src.base + rva, lo, hi)
        hits = _find_image(dst, pat)
        if len(hits) == 1:
            return n, hits
    return n, hits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("sites", nargs="+", help="name=0xRVA")
    ap.add_argument("--pattern", action="store_true")
    args = ap.parse_args()
    src, dst = Image(Path(args.src)), Image(Path(args.dst))
    lo, hi = _image_range(src)
    for item in args.sites:
        name, _, rva_text = item.partition("=")
        rva = int(rva_text, 16)
        n, hits = port(src, rva, dst)
        if len(hits) == 1:
            print(f"{name} {rva:#010x} -> {hits[0]:#010x} len={n}")
        elif hits:
            print(f"{name} {rva:#010x} -> ambiguous [{', '.join(f'{h:#010x}' for h in hits[:8])}] len={n}")
        else:
            print(f"{name} {rva:#010x} -> missing")
        if args.pattern:
            print("   ", pattern_text(mask_code(bytes(src.mem[rva:rva + n]), src.base + rva, lo, hi)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [x] **Step 4: Run the test to see it pass**

Run: `python tools/test_port_sites.py`
Expected: `OK: port_sites tests passed`.

- [x] **Step 5: Smoke test on the real images**

Run:

```bash
python tools/port_sites.py "C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat" C:/Jogos/AgeoftheRing/rotwk/game820.dat findSkirmishSide=0x002AC4D4
```

Expected: `findSkirmishSide 0x002ac4d4 -> 0x002acfa3 len=…` (the 2.01 RVA
plan S2 already established).

- [x] **Step 6: Commit**

```bash
git add tools/port_sites.py tools/test_port_sites.py
git commit -m "feat(tools): port 1.06 function signatures to 2.01 and AotR"
```

### Task 2: R1, the sites in `game.dat`

**Files:**
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` (an "R1 findings" section after "R0 findings")

**Interfaces:**
- Consumes: `tools/port_sites.py` CLI; `tools/disasm.py <image> <rva> [--len N]`.
- Produces: a table, used by plan 3, with one row per function: name, 1.06 RVA, 2.01 RVA, AotR RVA, signature length, and the signature (the AotR-unique pattern from `--pattern`, run with AotR as `src` once its RVA is known).

- [x] **Step 1: Port the starting points to 2.01 and AotR**

```bash
V106="C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat"
SITES="logic_update=0x0024555A win32_engine_update=0x00041EC3 setFPMode=0x00040EA9 customized_render=0x0007054F visibility_check=0x00071245 render_one_object=0x0006FB59 hlod_usot=0x0019D640 anim3d_usot=0x001A5900 render_fx=0x0014A880"
python tools/port_sites.py "$V106" C:/Jogos/AgeoftheRing/rotwk/game820.dat $SITES
python tools/port_sites.py "$V106" C:/Jogos/AgeoftheRing/rotwk/game.dat $SITES
```

- [x] **Step 2: Resolve every `ambiguous` or `missing` row by hand**

For each, disassemble the 1.06 function (`python tools/disasm.py "$V106" 0xRVA --len 0x80`)
and find a distinctive anchor: a string it pushes (`disasm.py <img> TEXT --string`),
a caller whose own signature is unique (`--callers`), or a vtable slot.
Locate the same anchor in AotR's `game.dat` and confirm by disassembling
both bodies side by side (same call sequence, same field offsets). Record
the anchor used in the table's notes column.

- [x] **Step 3: Find `GameEngine::update`**

Disassemble AotR's `Win32GameEngine::update` (its RVA from Step 1) and take
the target of its first `call` that isn't an import; confirm it is the frame
body (it calls the logic update through `TheGameLogic`'s vtable slot +0x34
and the client update). Port it back to 2.01 with `port_sites.py <AotR> <2.01>`.

- [x] **Step 4: Make every AotR signature unique in AotR itself**

```bash
python tools/port_sites.py C:/Jogos/AgeoftheRing/rotwk/game.dat C:/Jogos/AgeoftheRing/rotwk/game.dat --pattern <name=0xAOTR_RVA ...>
```

Each line must map an RVA to itself. Copy the patterns into the table.

- [x] **Step 5: Write the R1 findings section** (table plus notes; every value from the tool output, none from expectations) **and commit**

```bash
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: record the performance plugin's sites in game.dat (R1)"
```

### Task 3: R2, the pre-pass hook point

**Files:**
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` ("R2 findings")

**Interfaces:**
- Consumes: AotR RVAs of `customized_render`, `visibility_check`, `render_one_object` from Task 2.
- Produces: one hook site inside AotR's `Customized_Render`: its RVA, its original instruction bytes, a signature unique in `game.dat`, how the reflection pass is detected there (the `ShaderClass::Is_Backface_Culling_Inverted` read), and which register or stack slot holds the scene `this`.

- [x] **Step 1: Disassemble AotR's `Customized_Render` in full**

`python tools/disasm.py C:/Jogos/AgeoftheRing/rotwk/game.dat 0x<customized_render> --len 0x400`

Mark: the `Visibility_Check` call (through the scene vtable), the
`On_Frame_Update` loop over the update list, the terrain `Render` call, the
start of the `RenderList` loop and its first `renderOneObject` call.

- [x] **Step 2: Pick the hook site**

The first instruction after the terrain render and before the `RenderList`
loop that is at least 5 bytes long (or a `call` that can be redirected),
reached once per scene pass. Record the bytes, the scene `this` location and
how the code reads the back-face-culling flag.

- [x] **Step 3: Write "R2 findings" and commit**

```bash
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: record the skeleton pre-pass hook point (R2)"
```

### Task 4: R3, object layout in AotR

**Files:**
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` ("R3 findings")

**Interfaces:**
- Consumes: AotR RVAs of `anim3d_usot`, `hlod_usot`, `visibility_check` from Task 2.
- Produces: in AotR, the offsets of `IsTreeValid`, `HTree`, the slave master, `CurMotionMode`, `ModeAnim.Motion`, `ModeInterp.Motion0/1`, `ModeCombo.AnimCombo`, `Container`, the visible/hidden bits `Is_Really_Visible` reads, and the vtable slot of `Update_Sub_Object_Transforms`, `Class_ID` and `Get_HTree`; the `RenderList` head in the scene object and the list node layout.

- [x] **Step 1: Compare the 1.06 and AotR bodies of `Animatable3DObjClass::Update_Sub_Object_Transforms`**

Disassemble both (`--len 0x140`). 1.06 reads `+0xFC` (slave), `+0xF8`
(HTree), `+0xF4` (IsTreeValid), `+0x100` (CurMotionMode), `+0x104`
(ModeAnim.Motion). Record the AotR offset of each one from the same
instruction in the AotR body.

- [x] **Step 2: The same for `HLodClass::Update_Sub_Object_Transforms`** (sub-object array and count) and for the `RenderList` walk in `Visibility_Check` (list head, node `next` and object fields, the `Set_Visible` bit).

- [x] **Step 3: Write "R3 findings" and commit**

```bash
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: record the animated-object layout in game.dat (R3)"
```

### Task 5: R4, the audit of the pose path

**Files:**
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` ("R4 findings")

**Interfaces:**
- Consumes: AotR RVAs and offsets from Tasks 2 and 4.
- Produces: the list of every function reachable from `Update_Sub_Object_Transforms` for `BASE_POSE` and `SINGLE_ANIM` HLods, each write outside the object's own memory, and its class: harmless, covered by grouping by animation, or main-thread only. The verdict decides which motion modes the pre-pass may take.

- [x] **Step 1: List the callees**

Starting from AotR's `hlod_usot` and `anim3d_usot`, follow every direct
`call` (and the virtual calls whose targets are fixed by the vtables in
R3), depth first, recording each function's RVA and size with `disasm.py`.
Stop at imports and record them by name.

- [x] **Step 2: Classify every store**

For each function, list each instruction that writes memory (`mov [..], …`,
`inc/dec [..]`, `fstp [..]`, `rep stos`, calls to the allocator). Mark what
it points at: the object or its HTree copy (own), an `HAnimClass` or its
channel (shared per animation), a global (`[abs32]`), or unknown.

- [x] **Step 3: Write "R4 findings" with the verdict and commit**

```bash
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: audit what the pose path writes (R4)"
```

### Task 6: R5, R6 and the built-in profile file

**Files:**
- Modify: `docs/specs/2026-10-13-perf-dll-design.md` ("R5 and R6 findings")

**Interfaces:**
- Consumes: AotR RVA of `setFPMode` from Task 2.
- Produces: the x87 control word the game sets, whether `game.dat` touches MXCSR (`ldmxcsr`/`stmxcsr`) anywhere; whether Granny models appear in Realms or AotR; what writes `profile-high.csv` and whether it can give per-frame timings.

- [x] **Step 1: x87 and SSE settings**

Disassemble AotR's `setFPMode` and record the control-word value it loads
(`fldcw`, or `_controlfp` with its arguments). Search the executable sections
for `ldmxcsr` / `stmxcsr` (bytes `0F AE /2` and `0F AE /3`) with capstone
and record each site's enclosing function.

- [x] **Step 2: Granny**

Search Realms' `art` folder and the AotR `.big` archives' file lists for
`.gr2` files (`tools/` already reads BIGs: `mapinfo.BigArchive`), and
`game.dat` for references to `granny2.dll`. Record the counts.

- [x] **Step 3: `profile-high.csv`**

Find the string `profile-high.csv` (or `profile-` and `.csv`) in `game.dat`
and the code that references it (`disasm.py <img> TEXT --string`). Record
what triggers the write and what the columns are.

- [x] **Step 4: Write "R5 and R6 findings" and commit**

```bash
git add docs/specs/2026-10-13-perf-dll-design.md
git commit -m "docs: record FP settings, Granny use and the profile file (R5, R6)"
```
