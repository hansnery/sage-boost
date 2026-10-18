"""Offline native matrix-source cost screen; never starts the game.

Native bytes are read only from local images into private process memory.
The temporary manifest contains numeric relocation metadata, never code.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import io
import json
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
from capstone import x86
from port_sites import port
from test_fx_lifecycle import BASELINE, fnv1a
from verify_sites import Image

BODIES = (
    ('matrix', 0x14D440, 52), ('inverse', 0x14D474, 87),
    ('inverse_transpose', 0x14D4CB, 87), ('view', 0x14DCC8, 404),
    ('projection', 0x14DE5C, 300), ('world', 0x14EBD4, 404),
    ('matrix_ctor', 0x7671F, 22), ('array_ctor', 0x1423, 34),
    ('row_ctor', 0x47A6A9, 3),
)
IMPORTS = (('D3DXMatrixInverse', 0x62AF38), ('D3DXMatrixTranspose', 0x62AF44))
DATA = (('flags', 0x9EC4F4, 4), ('state', 0x9EE5D8, 0x290),
        ('projection', 0x9EDBF0, 64), ('one', 0x7BB8D8, 4))
DATA_START = len(BODIES) + len(IMPORTS)


def instructions(image, rva, size):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    result = list(md.disasm(bytes(image.mem[rva:rva+size]), image.base+rva))
    assert sum(i.size for i in result) == size, 'partial instruction body'
    return result


def recipe(reference, index):
    _, rva, size = BODIES[index]
    patches, starts, branches = [], set(), []
    functions = [b[1] for b in BODIES] + [i[1] for i in IMPORTS]
    for ins in instructions(reference, rva, size):
        off = ins.address-reference.base-rva
        starts.add(off)
        branch = ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)
        if branch and ins.operands[0].type == x86.X86_OP_IMM:
            dest = ins.operands[0].imm-reference.base
            if rva <= dest < rva+size:
                branches.append(dest-rva)
            else:
                assert ins.size == 5 and ins.bytes[0] in (0xE8, 0xE9)
                assert dest in functions, ('unknown call', hex(dest))
                patches.append((off+1, 0, functions.index(dest), 0))
        fields = []
        if ins.disp_size == 4 and reference.base <= (ins.disp & 0xffffffff) < reference.base+len(reference.mem):
            fields.append(off+ins.disp_offset)
        if not branch and ins.imm_size == 4 and any(
                op.type == x86.X86_OP_IMM and reference.base <= op.imm < reference.base+len(reference.mem)
                for op in ins.operands):
            fields.append(off+ins.imm_offset)
        for field in fields:
            dest = struct.unpack_from('<I', reference.mem, rva+field)[0]-reference.base
            if dest in functions:
                symbol, add = functions.index(dest), 0
            else:
                hits = [(DATA_START+i, dest-start) for i, (_, start, length) in enumerate(DATA)
                        if start <= dest < start+length]
                assert len(hits) == 1, ('unknown data', hex(dest))
                symbol, add = hits[0]
            patches.append((field, 1, symbol, add))
    assert all(b in starts for b in branches), 'branch into instruction'
    return patches


def verify(reference, target):
    symbols = []
    for name, rva, size in BODIES[:6]:
        _, hits = port(reference, rva, target, lengths=(size,))
        assert len(hits) == 1, f'{name}: not unique'
        symbols.append(hits[0])
    # Tiny or folded helpers are identified through verified callers.
    def relative(rva, field):
        return rva+field+4+struct.unpack_from('<i', target.mem, rva+field)[0]
    symbols.append(relative(symbols[0], 13))
    symbols.append(relative(symbols[6], 14))
    symbols.append(struct.unpack_from('<I', target.mem, symbols[6]+2)[0]-target.base)
    symbols.extend((relative(symbols[1], 42), relative(symbols[1], 60)))
    assert all(any(lo <= r < hi for lo, hi in target.exec_ranges) for r in symbols)
    imports = {imp.address-target.base: (entry.dll.lower(), imp.name)
               for entry in target.pe.DIRECTORY_ENTRY_IMPORT for imp in entry.imports}
    for i, (name, _) in enumerate(IMPORTS):
        rva = symbols[len(BODIES)+i]
        assert bytes(target.mem[rva:rva+2]) == b'\xff\x25', 'import thunk'
        slot = struct.unpack_from('<I', target.mem, rva+2)[0]-target.base
        assert imports[slot] == (b'd3dx9_27.dll', name.encode()), 'wrong import dependency'
    symbols += [None]*len(DATA)
    records = []
    for index, (name, old, size) in enumerate(BODIES):
        new = symbols[index]
        patches = recipe(reference, index)
        before = bytearray(reference.mem[old:old+size])
        after = bytearray(target.mem[new:new+size])
        for field, kind, symbol, add in patches:
            value = struct.unpack_from('<I' if kind else '<i', after, field)[0]
            dest = value-target.base if kind else new+field+4+value
            if symbols[symbol] is None:
                symbols[symbol] = dest-add
            assert symbols[symbol]+add == dest, 'inconsistent dependency'
            before[field:field+4] = after[field:field+4] = b'\0'*4
        assert before == after, f'{name}: full body mismatch'
        instructions(target, new, size)
        raw = bytes(target.mem[new:new+size])
        records.append((new, size, fnv1a(raw), patches))
    assert None not in symbols and len(set(symbols)) == len(symbols), 'missing/aliased role'
    for i, (name, old, size) in enumerate(DATA):
        new = symbols[DATA_START+i]
        assert any(s.VirtualAddress <= new and new+size <= s.VirtualAddress+s.Misc_VirtualSize
                   and not s.Characteristics & 0x20000000
                   and (name == 'one' or s.Characteristics & 0x80000000)
                   for s in target.pe.sections), 'invalid data region'
        if name == 'one':
            assert bytes(target.mem[new:new+size]) == bytes(reference.mem[old:old+size]) == struct.pack('<f', 1)
    return records, symbols


def refusals(reference, target, symbols):
    matrix, inverse, _, view = symbols[:4]
    mutations = [
        (matrix+0x32, b'\x04'),       # thiscall cleanup
        (inverse+0x31, b'\x74'),      # singular branch
        (inverse+0x4f, b'\x98'),      # wrong effect setter slot
        (view+9, struct.pack('<I', target.base+symbols[12])),
        (symbols[14], struct.pack('<f', 2)),
        (symbols[6]+2, struct.pack('<I', target.base+symbols[7])),
        (symbols[9]+2, bytes(target.mem[symbols[10]+2:symbols[10]+6])),
        (target.text_end-4096, bytes(target.mem[matrix:matrix+52])),
    ]
    for rva, replacement in mutations:
        old = bytes(target.mem[rva:rva+len(replacement)])
        target.mem[rva:rva+len(replacement)] = replacement
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                verify(reference, target)
        except (AssertionError, KeyError):
            pass
        else:
            raise AssertionError(f'corrupted source contract accepted at {rva:#x}')
        finally:
            target.mem[rva:rva+len(old)] = old
    return len(mutations)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('harness', type=Path)
    parser.add_argument('d3dx', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    reference, target = Image(BASELINE), Image(args.image)
    records, symbols = verify(reference, target)
    rejected = refusals(reference, target, symbols)
    with tempfile.TemporaryDirectory(prefix='sage-matrix-source-') as directory:
        manifest = Path(directory)/'sources.txt'
        lines = []
        for rva, size, digest, patches in records:
            lines.append(f'{rva} {size} {digest} {len(patches)}')
            lines.extend(' '.join(map(str, p)) for p in patches)
        manifest.write_text('\n'.join(lines)+'\n', encoding='ascii')
        run = subprocess.run([str(args.harness.resolve()), str(args.image.resolve()),
                              str(manifest), str(args.d3dx.resolve())],
                             capture_output=True, text=True, check=True)
        broken = subprocess.run([str(args.harness.resolve()), str(args.image.resolve()),
                                 str(manifest), str(args.d3dx.resolve()), 'swap-view'],
                                capture_output=True, text=True)
        assert broken.returncode == 2 and 'equal(captured,expected)' in broken.stderr, broken
    print(run.stdout, end='')
    result = json.loads(run.stdout)
    result['refusal_controls'] = rejected
    result['wrong_provider_control_rejected'] = True
    result['files'] = {name: {'path': str(path.resolve()), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                       for name, path in [('baseline', BASELINE), ('image', args.image),
                                          ('harness', args.harness), ('d3dx', args.d3dx)]}
    result['bodies'] = [{'name': name, 'rva': rva, 'size': size,
                         'sha256': hashlib.sha256(bytes(target.mem[rva:rva+size])).hexdigest()}
                        for (name, _, _), (rva, size, _, _) in zip(BODIES, records)]
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
