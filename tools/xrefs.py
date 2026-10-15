#!/usr/bin/env python
"""Research helper (not shipped): who calls / references an address in game.dat.

Resolves each hit to the enclosing reversed function using the
"@0xRVA (NNNB)" / "@0xRVA NNNB" annotations in the upstream source tree.

    python tools/xrefs.py 0x0033A3F4 0x00DBE9B0 ...   (code RVAs or data VAs)
"""

from __future__ import annotations

import re
import struct
import sys
from bisect import bisect_right
from pathlib import Path

import pefile

GAME = Path(r"C:\repositories\Open-BFME-2\baselines\bfme2\workshop-vanilla-1.06\files\game.dat")
CODE = Path(r"C:\repositories\Open-BFME-2\Code")
BASE = 0x400000

ANNOT = re.compile(r"@\s*0x([0-9A-Fa-f]{8})\s*\(?\s*(\d+)\s*B")


def load_index() -> list[tuple[int, int, str]]:
    funcs: dict[int, tuple[int, str]] = {}
    for p in CODE.rglob("*.cpp"):
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in ANNOT.finditer(text):
            start, size = int(m.group(1), 16), int(m.group(2))
            if 0x1000 <= start < 0x7C0000 and size > 0:
                funcs.setdefault(start, (size, str(p.relative_to(CODE))))
    return sorted((s, sz, f) for s, (sz, f) in funcs.items())


def main() -> None:
    pe = pefile.PE(str(GAME))
    img = pe.get_memory_mapped_image()
    text = pe.sections[0]
    t0, t1 = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    index = load_index()
    starts = [s for s, _, _ in index]

    def where(rva: int) -> str:
        i = bisect_right(starts, rva) - 1
        if i >= 0:
            s, sz, f = index[i]
            if rva < s + sz:
                return f"in 0x{s:08X} ({sz}B) {f}"
        return "in unclaimed code"

    for arg in sys.argv[1:]:
        target = int(arg, 16)
        print(f"== 0x{target:08X}")
        if target < 0x800000:  # code RVA: rel32 calls/jumps
            for i in range(t0, t1 - 5):
                op = img[i]
                if op in (0xE8, 0xE9):
                    if i + 5 + struct.unpack_from("<i", img, i + 1)[0] == target:
                        print(f"   {'call' if op == 0xE8 else 'jmp '} @0x{i:08X} {where(i)}")
            needle = struct.pack("<I", BASE + target)
        else:
            needle = struct.pack("<I", target)
        pos = img.find(needle)
        while pos != -1:
            kind = "code" if t0 <= pos < t1 else "data"
            print(f"   abs  @0x{pos:08X} [{kind}] {where(pos) if kind == 'code' else ''}")
            pos = img.find(needle, pos + 1)


if __name__ == "__main__":
    main()
