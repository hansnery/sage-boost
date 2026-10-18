"""Read-only native render-event contract audit; no game code is executed.

Full bodies are compared with the local reference. Only verified absolute
data operands and external branch targets are normalized. This establishes
the four routine bodies, not native caller/SEH/lifetime equivalence.
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
from capstone import x86

from port_sites import port
from verify_sites import Image
from test_fx_lifecycle import fnv1a

BASELINE = Path('C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat')
# Reference identities only. No installation or target-address constants.
SPECS = (
    ('constructor', 0x117CF0, 167, (0x13, 0x34, 0x44, 0x4D, 0x56), ((0x98, 2),)),
    ('destructor', 0x117DA0, 5, (), ((1, 3),)),
    ('begin_callback', 0x11F340, 128, (9,), ()),
    ('end_callback', 0x11F3C0, 12, (1,), ()),
)


def normalize(image, index, rva, symbols):
    name, _, size, absolute, branches = SPECS[index]
    raw = bytes(image.mem[rva:rva+size])
    normalized = bytearray(raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    decoded, data, external, starts, internal = 0, [], [], set(), []
    lo = image.base+rva
    for ins in md.disasm(raw, lo):
        decoded += ins.size
        starts.add(ins.address)
        offset = ins.address-lo
        if ins.disp_size == 4 and image.base <= (ins.disp & 0xFFFFFFFF) < image.base+len(image.mem):
            assert any(op.type == x86.X86_OP_MEM and not op.mem.base and not op.mem.index for op in ins.operands)
            data.append(offset+ins.disp_offset)
            normalized[data[-1]:data[-1]+4] = b'\0'*4
        if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
            op = ins.operands[0]
            if op.type == x86.X86_OP_IMM:
                if lo <= op.imm < lo+size:
                    internal.append(op.imm)
                else:
                    assert ins.size == 5 and ins.bytes[0] in (0xE8, 0xE9)
                    target = op.imm-image.base
                    assert target in symbols
                    external.append((offset+1, symbols.index(target)))
                    normalized[offset+1:offset+5] = b'\0'*4
            else:
                assert op.type == x86.X86_OP_REG
                assert (index, ins.mnemonic, op.reg) in (
                    (0, 'call', x86.X86_REG_EDI),
                    (2, 'call', x86.X86_REG_ESI),
                    (3, 'jmp', x86.X86_REG_EAX),
                )
        assert not any(op.type == x86.X86_OP_MEM and op.mem.segment for op in ins.operands)
    assert decoded == size and all(p in starts for p in internal), name
    assert tuple(data) == absolute and tuple(external) == branches, name
    return bytes(normalized), raw


def verify(reference, target):
    originals = [s[1] for s in SPECS]
    found = []
    for index, spec in enumerate(SPECS):
        if index == 1:
            # The five-byte tail alone is not a unique signature. Establish
            # its complete constructor/padding context first.
            rva = found[0]+0xB0
            assert bytes(target.mem[found[0]+167:rva]) == b'\xCC'*9
            found.append(rva)
        else:
            _, hits = port(reference, spec[1], target, lengths=(spec[2],))
            assert len(hits) == 1, f'{spec[0]} not unique'
            found.append(hits[0])
    for index, spec in enumerate(SPECS):
        old = normalize(reference, index, originals[index], originals)
        new = normalize(target, index, found[index], found)
        assert old[0] == new[0], f'{spec[0]} body differs'
        print(f'{spec[0]}: {spec[2]} bytes, SHA256 {hashlib.sha256(new[1]).hexdigest()}')
    def operand(index, offset):
        return struct.unpack_from('<I', target.mem, found[index]+offset)[0]-target.base
    iat = operand(0, 0x13)
    imports = {imp.address-target.base: (entry.dll.lower(), imp.name)
               for entry in target.pe.DIRECTORY_ENTRY_IMPORT for imp in entry.imports}
    assert imports.get(iat) == (b'msvcr71.dll', b'strncpy')
    fragments = [operand(0, p) for p in SPECS[0][3][1:]]
    assert fragments == [fragments[0]+n for n in (0, 4, 8, 12)]
    prefix = bytes(target.mem[fragments[0]:fragments[0]+13])
    assert prefix == b'SceneAnalyst\0'
    slots = [operand(2, 9), operand(3, 1)]
    assert slots[0] != slots[1]
    for slot in slots:
        assert 0 <= slot <= len(target.mem)-4
        assert any(section.VirtualAddress <= slot < section.VirtualAddress+section.Misc_VirtualSize
                   and section.Characteristics & 0x80000000
                   and not section.Characteristics & 0x20000000
                   for section in target.pe.sections)
    print('Verified CRT import, local event prefix, distinct writable callback slots and complete branch targets')
    return found, slots


def refusal_checks(reference, target, found):
    ctor, dtor, begin, end = found
    def data(index, off):
        return struct.unpack_from('<I', target.mem, found[index]+off)[0]
    mutations = [
        (ctor, b'\x90'),
        (ctor+0xA5, b'\x10'),  # RET 16 instead of RET 12.
        (ctor+0x13, struct.pack('<I', data(0, 0x13)+4)),
        (end+1, struct.pack('<I', data(2, 9))),  # Aliased callback slots.
        (begin+9, struct.pack('<I', target.base+ctor)),  # Executable slot.
        (data(0, 0x34)-target.base, b'X'),
        (ctor+0x98, struct.pack('<i', end-(ctor+0x9C))),
        (dtor+1, struct.pack('<i', begin-(dtor+5))),
        (target.text_end-1024, bytes(target.mem[ctor:ctor+167])),  # Duplicate signature.
    ]
    for rva, replacement in mutations:
        original = bytes(target.mem[rva:rva+len(replacement)])
        target.mem[rva:rva+len(replacement)] = replacement
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                verify(reference, target)
        except AssertionError:
            pass
        else:
            raise AssertionError(f'altered native event contract accepted at {rva:#x}')
        finally:
            target.mem[rva:rva+len(original)] = original
    print(f'Native event verifier: {len(mutations)} altered-code/data/uniqueness refusals passed', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--execute', action='store_true', help='run isolated verified bodies in the native fixture')
    parser.add_argument('--self-test', action='store_true', help='exercise refusals on private in-memory copies')
    args = parser.parse_args()
    reference, target = Image(args.baseline), Image(args.image)
    found, _ = verify(reference, target)
    if args.self_test:
        refusal_checks(reference, target, found)
    if args.execute:
        with tempfile.TemporaryDirectory(prefix='sage-event-contract-') as folder:
            manifest = Path(folder)/'contract.txt'
            manifest.write_text('\n'.join(f'{rva} {spec[2]} {fnv1a(bytes(target.mem[rva:rva+spec[2]]))}'
                                          for rva, spec in zip(found, SPECS))+'\n', encoding='ascii')
            exe = Path(__file__).resolve().parent.parent/'perf/build/test_native_events.exe'
            raise SystemExit(subprocess.call([str(exe), str(args.image), str(manifest), str(args.image.parent/'msvcr71.dll')]))
