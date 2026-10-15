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


def test_cut_instruction_is_still_masked():
    code = func(0x1234, 0x00DE4364, 7)
    pat = port_sites.mask_prefix(code, BASE, 11, LO, HI)
    assert pat == [0x55, 0x8B, 0xEC, 0xE8, None, None, None, None, 0xA1, None, None]


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("OK: port_sites tests passed")
