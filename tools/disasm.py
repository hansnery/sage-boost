#!/usr/bin/env python
"""Research helper (not shipped): disassembly and cross-references for any
supported game image.

    python tools/disasm.py <image> <rva> [--len N]   disassemble N bytes
    python tools/disasm.py <image> <rva> --callers  rel32 calls/jumps to the RVA
    python tools/disasm.py <image> <va> --refs      code that contains the abs32 VA
    python tools/disasm.py <image> TEXT --string    VAs of an ASCII string, and their refs

RVAs and VAs are hex (0x...). Every listed site is printed as an RVA.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from verify_sites import Image  # noqa: E402


def disasm(img: Image, rva: int, length: int) -> list[str]:
    out = []
    pos, end = rva, rva + length
    while pos < end:
        ins = next(img.md.disasm(bytes(img.mem[pos:pos + 16]), img.base + pos, 1), None)
        if ins is None:
            out.append(f"{pos:#010x}  {img.mem[pos]:02x}                       (db)")
            pos += 1
            continue
        out.append(f"{pos:#010x}  {ins.bytes.hex(' '):<24} {ins.mnemonic} {ins.op_str}")
        pos += ins.size
    return out


def callers(img: Image, target: int) -> list[tuple[int, str]]:
    hits = []
    buf = img.mem
    for lo, hi in img.exec_ranges:
        hi = min(hi, len(buf) - 1)
        for pos in range(lo, hi - 5):
            op = buf[pos]
            if op in (0xE8, 0xE9):
                rel = struct.unpack_from("<i", buf, pos + 1)[0]
                if pos + 5 + rel == target:
                    hits.append((pos, "call" if op == 0xE8 else "jmp"))
            elif op == 0x0F and 0x80 <= buf[pos + 1] <= 0x8F and pos + 6 <= hi:
                rel = struct.unpack_from("<i", buf, pos + 2)[0]
                if pos + 6 + rel == target:
                    hits.append((pos, "jcc"))
    return hits


def refs(img: Image, va: int) -> list[int]:
    needle = struct.pack("<I", va)
    hits = []
    for lo, hi in img.exec_ranges:
        pos = img.mem.find(needle, lo, hi)
        while pos != -1:
            hits.append(pos)
            pos = img.mem.find(needle, pos + 1, hi)
    return hits


def insn_at(img: Image, operand_rva: int) -> str:
    """The instruction whose bytes contain operand_rva (decoded from up to 8 bytes back)."""
    for back in range(1, 9):
        start = operand_rva - back
        ins = next(img.md.disasm(bytes(img.mem[start:start + 16]), img.base + start, 1), None)
        if ins is not None and start + ins.size > operand_rva and ins.size >= back + 4:
            return f"{start:#010x}  {ins.mnemonic} {ins.op_str}"
    return f"{operand_rva:#010x}  (operand)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path)
    ap.add_argument("addr")
    ap.add_argument("--len", type=lambda s: int(s, 0), default=0x80)
    ap.add_argument("--callers", action="store_true")
    ap.add_argument("--refs", action="store_true")
    ap.add_argument("--string", action="store_true")
    args = ap.parse_args()
    img = Image(args.image)

    if args.string:
        needle = args.addr.encode() + b"\0"
        pos = img.mem.find(needle)
        while pos != -1:
            if pos == 0 or img.mem[pos - 1] == 0:
                va = img.base + pos
                print(f"string at VA {va:#010x}")
                for r in refs(img, va):
                    print("   ", insn_at(img, r))
            pos = img.mem.find(needle, pos + 1)
        return 0

    value = int(args.addr, 0)
    if args.refs:
        for r in refs(img, value):
            print(insn_at(img, r))
    elif args.callers:
        for pos, kind in callers(img, value):
            print(f"{pos:#010x}  {kind}")
    else:
        print("\n".join(disasm(img, value, args.len)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
