"""Verify the local native FX group loop and run an isolated bridge fixture.

No game startup or file patching. Only verified bodies enter private RAM;
the temporary manifest contains numeric relocation metadata and hashes.
The fixture proves normal control flow, not native exception unwinding.
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
from test_event_contract import BASELINE, SPECS as EVENTS, verify as verify_events
from test_fx_lifecycle import fnv1a

# Read-only reference identities, not runtime hook addresses.
BODIES = (
    ('group', 0x17438B, 819), ('seh_prolog', 0x629188, 31),
    ('get_name', 0x16A6B0, 14), ('chain_set', 0x1688FF, 83),
    ('chain_clear', 0x168952, 41),
) + tuple((n, r, s) for n, r, s, _, _ in EVENTS)
DEPENDENCIES = (
    ('compare', 0x149A90), ('prepare', 0x14A710), ('render', 0x14A880),
    ('finish', 0x149BB0), ('vector_push', 0x1741EB),
    ('set_transforms', 0x14D417), ('clear_transforms', 0x14D431),
    ('vector_erase', 0x173DF8),
)
# 0..8 native bodies; 9..16 controlled dependencies; 17..18 CRT exports.
CALLS = (
    ((0x06, 1), (0x41, 9), (0x91, 3), (0xC3, 5), (0xCE, 10),
     (0x15C, 17), (0x16B, 2), (0x17C, 2), (0x190, 18), (0x1AB, 5),
     (0x1C3, 11), (0x1D2, 6), (0x1E4, 2), (0x1F5, 2), (0x22E, 5),
     (0x255, 13), (0x273, 14), (0x288, 11), (0x293, 15),
     (0x2A1, 16), (0x2B0, 6), (0x2E6, 12), (0x2EF, 4), (0x300, 6)),
    (), (), (), (), ((0x98, 7),), ((1, 8),), (), (),
)
# Absolute substitutions: handler, labels, sprintf IAT, SAS slot,
# strncpy IAT, prefix fragments, and independent event callback slots.
ABSOLUTE = (
    ((1, 19), (0xB8, 20), (0x156, 21), (0x183, 22), (0x199, 23),
     (0x1FC, 22), (0x20B, 24), (0x212, 25), (0x21C, 23),
     (0x26A, 26), (0x28E, 26)), (), (), (), (),
    ((0x13, 27), (0x34, 28), (0x44, 29), (0x4D, 30), (0x56, 31)),
    (), ((9, 32),), ((1, 33),),
)
LABELS = (b'RenderFXShaderBatch\0', b'Rendering mesh\tFXShader\t\0',
          b'(unnamed)\0', b'MeshFXShader\0', b'Rendering %d meshes\tFXShader\t%s\0')


def operand(image, rva):
    return struct.unpack_from('<I', image.mem, rva)[0]-image.base


def normalize(image, index, rva, symbols):
    size = BODIES[index][2]
    raw = bytes(image.mem[rva:rva+size])
    normalized = bytearray(raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    starts, branches, direct, absolute = set(), [], [], []
    total = 0
    for ins in md.disasm(raw, image.base+rva):
        off = ins.address-image.base-rva
        starts.add(off)
        total += ins.size
        is_branch = ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP)
        if is_branch and ins.operands[0].type == x86.X86_OP_IMM:
            dest = ins.operands[0].imm-image.base
            if rva <= dest < rva+size:
                branches.append(dest-rva)
            else:
                assert ins.size == 5 and ins.bytes[0] in (0xE8, 0xE9)
                assert dest in symbols[:19], 'unknown external target'
                direct.append((off+1, symbols[:19].index(dest)))
                normalized[off+1:off+5] = b'\0'*4
        fields = []
        if ins.disp_size == 4 and image.base <= (ins.disp & 0xFFFFFFFF) < image.base+len(image.mem):
            fields.append(off+ins.disp_offset)
        if not is_branch and ins.imm_size == 4:
            if any(op.type == x86.X86_OP_IMM and image.base <= op.imm < image.base+len(image.mem)
                   for op in ins.operands):
                fields.append(off+ins.imm_offset)
        for field in fields:
            value = operand(image, rva+field)
            assert value in symbols[19:], 'unknown absolute operand'
            absolute.append((field, symbols.index(value, 19)))
            normalized[field:field+4] = b'\0'*4
    assert total == size and all(p in starts for p in branches)
    assert tuple(direct) == CALLS[index], 'changed direct-call role or offset'
    assert tuple(absolute) == ABSOLUTE[index], 'changed data role or offset'
    if index == 0:
        # Six stolen bytes and two continuations must be full instructions;
        # nobody enters the middle of the proposed insertion window.
        assert {0x14F, 0x155, 0x2B7} <= starts
        assert not any(0x14F < p < 0x155 for p in branches)
    return bytes(normalized), raw


def symbols_for(reference, target):
    events, slots = verify_events(reference, target)
    found = []
    for index, (_, original, size) in enumerate(BODIES[:5]):
        # The short pure getter is identified with its surrounding context.
        _, hits = port(reference, original, target, lengths=(max(24, size),))
        assert len(hits) == 1, f'body {index} not unique'
        found.append(hits[0])
    found += events
    for name, original in DEPENDENCIES:
        _, hits = port(reference, original, target)
        assert len(hits) == 1, f'{name} dependency not unique'
        found.append(hits[0])
    group = found[0]
    imports = {imp.address-target.base: (entry.dll.lower(), imp.name)
               for entry in target.pe.DIRECTORY_ENTRY_IMPORT for imp in entry.imports}
    for field, export in ((0x15C, b'_mbscpy'), (0x190, b'_mbscat')):
        thunk = group+field+4+struct.unpack_from('<i', target.mem, group+field)[0]
        assert bytes(target.mem[thunk:thunk+2]) == b'\xFF\x25'
        assert imports.get(operand(target, thunk+2)) == (b'msvcr71.dll', export)
        found.append(thunk)
    handler = operand(target, group+1)
    assert any(lo <= handler < hi for lo, hi in target.exec_ranges)
    found.append(handler)
    for field, label in zip((0xB8, 0x156, 0x183, 0x199, 0x20B), LABELS):
        loc = operand(target, group+field)
        assert bytes(target.mem[loc:loc+len(label)]) == label
        found.append(loc)
    iat = operand(target, group+0x212)
    assert imports.get(iat) == (b'msvcr71.dll', b'sprintf')
    found.append(iat)
    sas = operand(target, group+0x26A)
    assert any(s.VirtualAddress <= sas and sas+4 <= s.VirtualAddress+s.Misc_VirtualSize
               and s.Characteristics & 0x80000000 and not s.Characteristics & 0x20000000
               for s in target.pe.sections)
    found += [sas, operand(target, events[0]+0x13)]
    found += [operand(target, events[0]+p) for p in (0x34, 0x44, 0x4D, 0x56)]
    found += slots
    assert len(found) == 34 and len(set(found)) == 34
    return found


def verify(reference, target):
    old_symbols = symbols_for(reference, reference)
    symbols = symbols_for(reference, target)
    records = []
    for index, (name, _, size) in enumerate(BODIES):
        old = normalize(reference, index, old_symbols[index], old_symbols)
        new = normalize(target, index, symbols[index], symbols)
        assert old[0] == new[0], f'{name}: complete body differs'
        records.append((symbols[index], size, fnv1a(new[1])))
        print(f'{name}: {size} bytes SHA256 {hashlib.sha256(new[1]).hexdigest()}', flush=True)
    return symbols, records


def refusals(reference, target, symbols):
    group = symbols[0]
    mutations = [
        (group+0x14F, b'\x90'),  # Stolen instruction.
        (group+0x32A, b'\x04'),  # Changed normal epilogue/FS write.
        (group+0x331, b'\x0C'),  # RET 12 instead of RET 8.
        (group+0x1BE, b'\x09'),  # Changed mesh event SEH state.
        (group+0x287, b'\x90'),  # Native multi-instance render call.
        (group+0x212, struct.pack('<I', target.base+symbols[27])),  # Wrong CRT import.
        (group+0x28E, struct.pack('<I', target.base+symbols[32])),  # Wrong SAS slot.
        (group+0xC3, struct.pack('<i', symbols[6]-(group+0xC7))),  # Dtor in ctor role.
        (symbols[2]+2, b'\x14'),  # Name getter field.
        (symbols[20], b'X'),
        (target.text_end-2048, bytes(target.mem[group:group+819])),
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
            raise AssertionError(f'altered group contract accepted at {rva:#x}')
        finally:
            target.mem[rva:rva+len(original)] = original
    print(f'Native group verifier: {len(mutations)} refusal controls passed', flush=True)


def write_manifest(path,records):
    """Persist numeric relocation roles and hashes, never image bytes."""
    lines=[]
    for index,record in enumerate(records):
        patches=[(field,0,symbol) for field,symbol in CALLS[index]]
        patches += [(field,1,symbol) for field,symbol in ABSOLUTE[index]]
        lines.append(' '.join(map(str,(*record,len(patches)))))
        lines.extend(' '.join(map(str,patch)) for patch in patches)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    reference, target = Image(args.baseline), Image(args.image)
    symbols, records = verify(reference, target)
    if args.self_test:
        refusals(reference, target, symbols)
    if args.execute:
        with tempfile.TemporaryDirectory(prefix='sage-group-contract-') as folder:
            path = Path(folder)/'contract.txt'
            write_manifest(path,records)
            exe = Path(__file__).resolve().parent.parent/'perf/build/test_native_group.exe'
            command = [str(exe), str(args.image), str(path), str(args.image.parent/'msvcr71.dll')]
            result = subprocess.call(command)
            if result:
                return result
            if args.self_test:
                for fault in range(1, 5):
                    control = subprocess.run(command+[str(fault)], capture_output=True, text=True)
                    assert control.returncode == 1 and 'native group refused' in control.stderr, control
                print('Native group fixture: four deliberately broken bridges rejected', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
