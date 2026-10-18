"""Offline cost screen for terrain ambient sampling; never launches the game.

Verifies the entire local target body against the read-only 1.06 reference,
then passes relocation metadata (never executable bytes) to the native test.
No runtime signature or installable hook is generated.
"""
from __future__ import annotations

import argparse
import hashlib
import subprocess
from pathlib import Path

import capstone
from capstone import x86 as x86

from port_sites import port
from verify_sites import Image

ROOT = Path(__file__).resolve().parent.parent
BASELINE = Path('C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat')
REFERENCE_RVA = 0x27DA6A  # Research identity only; target is scanned, not addressed.
BODY_SIZE = 507


def contract(image: Image, rva: int):
    code = bytes(image.mem[rva:rva + BODY_SIZE])
    normalized = bytearray(code)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    relocations, constants, imports = [], [], []
    decoded = 0
    lo = image.base + rva
    floor_iat = None
    for entry in image.pe.DIRECTORY_ENTRY_IMPORT:
        for symbol in entry.imports:
            if entry.dll.lower() == b'msvcr71.dll' and symbol.name == b'floor':
                floor_iat = symbol.address
    assert floor_iat is not None, 'missing MSVCR71 floor import'
    for ins in md.disasm(code, lo):
        decoded += ins.size
        off = ins.address - lo
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type == x86.X86_OP_IMM
            assert lo <= ins.operands[0].imm < lo + BODY_SIZE, 'external branch'
        if ins.group(capstone.CS_GRP_CALL):
            op = ins.operands[0]
            assert op.type == x86.X86_OP_MEM, 'unexpected call'
            if op.mem.base:
                assert op.mem.base == x86.X86_REG_EAX and op.mem.disp == 0x2C
            else:
                assert op.mem.disp == floor_iat, 'unexpected imported call'
                imports.append(off + ins.disp_offset)
        if ins.disp_size == 4 and not any(
            op.type == x86.X86_OP_MEM and op.mem.base for op in ins.operands
        ):
            address = ins.disp & 0xFFFFFFFF
            assert image.base <= address < image.base + len(image.mem), 'external data'
            field = off + ins.disp_offset
            relocations.append(field)
            normalized[field:field + 4] = b'\0' * 4
            if address != floor_iat:
                constants.append(bytes(image.mem[address-image.base:address-image.base+4]))
    assert decoded == BODY_SIZE and code[-3:] == b'\xc2\x08\x00'
    assert len(relocations) == 9 and len(imports) == 2
    return bytes(normalized), constants, relocations, imports[0], code


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--crt', type=Path, default=Path('C:/Jogos/AgeoftheRing/rotwk/msvcr71.dll'))
    args = parser.parse_args()
    reference, target = Image(args.baseline), Image(args.image)
    _, hits = port(reference, REFERENCE_RVA, target)
    assert len(hits) == 1, 'target sampling function is not unique'
    original = contract(reference, REFERENCE_RVA)
    found = contract(target, hits[0])
    assert original[:2] == found[:2], 'body or constants differ from inspected reference'
    print(f'Verified {args.image.name}: RVA {hits[0]:#x}, {BODY_SIZE} bytes, '
          f'SHA256 {hashlib.sha256(found[4]).hexdigest()}', flush=True)
    command = [str(ROOT/'perf/build/bench_ambient.exe'), str(args.image), str(args.crt),
               str(hits[0]), str(fnv1a(found[4])), str(found[3])]
    command.extend(map(str, found[2]))
    return subprocess.call(command)


if __name__ == '__main__':
    raise SystemExit(main())
