"""Verify and exercise local native FX wrappers without starting the game.

Only eight fully matched wrapper bodies are copied to private executable
memory. Every direct call is redirected to another verified wrapper or a
controlled test dependency. No EA bytes are written to disk; the temporary
manifest contains offsets, lengths, hashes and dependency indices only.
"""
from __future__ import annotations

import argparse
import hashlib
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

import capstone
from capstone import x86

from port_sites import port
from verify_sites import Image

ROOT = Path(__file__).resolve().parent.parent
BASELINE = Path('C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat')


@dataclass(frozen=True)
class Function:
    name: str
    reference: int
    size: int
    direct: tuple[int, ...]
    virtual: tuple[int, ...]


# Read-only reference identities, never installable target addresses.
# Indices 0..7 are copied wrappers; 8..11 are controlled dependencies.
FUNCTIONS = (
    Function('get_effect', 0x15148D, 37, (), (0x28, 0x2C)),
    Function('begin', 0x151EE2, 198, (0, 8, 0, 9, 6, 10), (0xE8, 0x12C, 0xD0, 0xFC)),
    Function('begin_pass', 0x151CEE, 44, (0, 0), (0x100,)),
    Function('dynamic', 0x151D1A, 69, (0, 8, 6, 0), (0x104,)),
    Function('end_pass', 0x151D5F, 38, (0, 0), (0x108,)),
    Function('end', 0x151D85, 38, (0, 0), (0x10C,)),
    Function('bind', 0x1514B2, 30, (7,), ()),
    Function('apply_sources', 0x15326D, 116, (11,), ()),
)
DEPENDENCIES = (0x11D930, 0x13270C, 0x129670, 0x1531F2)


def contract(image: Image, rva: int, spec: Function, symbols: list[int]):
    code = bytes(image.mem[rva:rva + spec.size])
    normalized = bytearray(code)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    direct, virtual, jumps, starts = [], [], [], set()
    decoded = 0
    lo = image.base + rva
    for ins in md.disasm(code, lo):
        decoded += ins.size
        starts.add(ins.address)
        off = ins.address - lo
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type == x86.X86_OP_IMM, 'indirect jump'
            address = ins.operands[0].imm
            if lo <= address < lo + spec.size:
                jumps.append(address)
            else:
                assert spec.name == 'bind' and ins.bytes[0] == 0xE9 and ins.size == 5, 'external jump'
                assert address - image.base == symbols[7], 'tail target differs'
                direct.append((off + 1, 7))
                normalized[off + 1:off + 5] = b'\0' * 4
        if ins.group(capstone.CS_GRP_CALL):
            op = ins.operands[0]
            if op.type == x86.X86_OP_IMM:
                assert ins.bytes[0] == 0xE8 and ins.size == 5
                target = op.imm - image.base
                assert target in symbols, 'uncontrolled direct dependency'
                direct.append((off + 1, symbols.index(target)))
                normalized[off + 1:off + 5] = b'\0' * 4
            else:
                assert op.type == x86.X86_OP_MEM
                assert op.mem.base in (x86.X86_REG_EAX, x86.X86_REG_ECX)
                assert not op.mem.index
                virtual.append(op.mem.disp)
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM:
                assert op.mem.base and not op.mem.segment, 'absolute/segmented data access'
            if op.type == x86.X86_OP_IMM and not (
                ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)
            ):
                assert not image.base <= (op.imm & 0xFFFFFFFF) < image.base + len(image.mem), 'image pointer'
    assert decoded == spec.size, 'partial instruction or body'
    assert all(j in starts for j in jumps), 'external or unaligned branch'
    assert tuple(n for _, n in direct) == spec.direct, 'direct-call sequence changed'
    assert tuple(virtual) == spec.virtual, 'virtual-call sequence changed'
    assert ins.mnemonic == 'ret'
    return bytes(normalized), direct, code


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def verify(reference: Image, target: Image):
    originals = [s.reference for s in FUNCTIONS] + list(DEPENDENCIES)
    found = []
    for index, original in enumerate(originals):
        lengths = (FUNCTIONS[index].size,) if index < len(FUNCTIONS) else (24, 32, 48, 64, 96, 128)
        _, hits = port(reference, original, target, lengths=lengths)
        assert len(hits) == 1, f'function/dependency {index} is not unique'
        found.append(hits[0])
    records = []
    for index, spec in enumerate(FUNCTIONS):
        old = contract(reference, spec.reference, spec, originals)
        new = contract(target, found[index], spec, found)
        assert old[:2] == new[:2], f'{spec.name}: full native body differs'
        records.append((found[index], spec.size, fnv1a(new[2]), new[1]))
        print(f'{spec.name}: verified {spec.size} bytes, SHA256 {hashlib.sha256(new[2]).hexdigest()}', flush=True)
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    args = parser.parse_args()
    records = verify(Image(args.baseline), Image(args.image))
    # Temporary numeric metadata only, removed even when the harness fails.
    with tempfile.TemporaryDirectory(prefix='sage-fx-contract-') as folder:
        path = Path(folder)/'contract.txt'
        lines = [str(len(records))]
        for rva, size, digest, calls in records:
            lines.append(f'{rva} {size} {digest} {len(calls)}')
            lines.extend(f'{offset} {target}' for offset, target in calls)
        path.write_text('\n'.join(lines) + '\n', encoding='ascii')
        return subprocess.call([str(ROOT/'perf/build/test_fx_lifecycle.exe'), str(args.image), str(path)])


if __name__ == '__main__':
    raise SystemExit(main())
