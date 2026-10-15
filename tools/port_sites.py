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


def mask_prefix(code: bytes, va: int, n: int, lo: int, hi: int) -> list[int | None]:
    """The first n bytes of mask_code(code), so an instruction cut at n is
    still decoded (code must extend past n)."""
    return mask_code(code, va, lo, hi)[:n]


def _masked(img: Image, rva: int, n: int, lo: int, hi: int) -> list[int | None]:
    return mask_prefix(bytes(img.mem[rva:rva + n + 15]), img.base + rva, n, lo, hi)


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
        hits = _find_image(dst, _masked(src, rva, n, lo, hi))
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
            print("   ", pattern_text(_masked(src, rva, n, lo, hi)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
