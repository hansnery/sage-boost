"""Read-only native D3D9 creation contract; no game bytes are saved."""
import argparse
import contextlib
import hashlib
import io
import json
import struct
from pathlib import Path

import capstone
from capstone import x86
from port_sites import port, mask_code
from verify_sites import Image

BASELINE = Path('C:/repositories/Open-BFME-2/baselines/bfme2/workshop-vanilla-1.06/files/game.dat')
CREATE_RVA, CREATE_SIZE = 0x1246C0, 460
INIT_RVA = 0x1245A0


def instructions(image, rva):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    result = list(decoder.disasm(bytes(image.mem[rva:rva+CREATE_SIZE]), image.base+rva))
    assert sum(i.size for i in result) == CREATE_SIZE
    assert result[-1].mnemonic == 'ret'
    return result


def verify(reference, target):
    _, hits = port(reference, CREATE_RVA, target, lengths=(CREATE_SIZE,))
    assert len(hits) == 1, 'creation body absent or ambiguous'
    rva = hits[0]
    old, new = instructions(reference, CREATE_RVA), instructions(target, rva)
    assert len(old) == len(new)
    assert mask_code(bytes(reference.mem[CREATE_RVA:CREATE_RVA+CREATE_SIZE]),
                     reference.base+CREATE_RVA, reference.base, reference.base+len(reference.mem)) == \
        mask_code(bytes(target.mem[rva:rva+CREATE_SIZE]), target.base+rva,
                  target.base, target.base+len(target.mem)), 'normalized body differs'
    starts = {i.address-target.base-rva for i in new}
    globals_ = {}
    calls = []
    for a, b in zip(old, new):
        off = b.address-target.base-rva
        assert not b.mnemonic.startswith(('fldcw', 'fnstcw', 'fstcw', 'ldmxcsr', 'stmxcsr', 'fxrstor', 'xrstor'))
        if b.group(capstone.CS_GRP_JUMP):
            assert b.operands[0].type == x86.X86_OP_IMM
            dest = b.operands[0].imm-target.base-rva
            assert dest in starts and dest == a.operands[0].imm-reference.base-CREATE_RVA
        if b.group(capstone.CS_GRP_CALL):
            if b.operands[0].type == x86.X86_OP_IMM:
                assert a.operands[0].imm == reference.base+INIT_RVA
                _, dependency = port(reference, INIT_RVA, target)
                assert len(dependency) == 1 and b.operands[0].imm == target.base+dependency[0]
                calls.append({'offset': off, 'dependency_rva': dependency[0]})
            else:
                assert b.operands[0].type == x86.X86_OP_MEM
                calls.append({'offset': off, 'vtable_byte_offset': b.operands[0].mem.disp})
        fields = []
        if a.disp_size == 4 and reference.base <= a.disp < reference.base+len(reference.mem):
            fields.append((a.disp_offset, b.disp_offset))
        if a.imm_size == 4 and not (a.group(capstone.CS_GRP_JUMP) or a.group(capstone.CS_GRP_CALL)):
            if any(p.type == x86.X86_OP_IMM and reference.base <= p.imm < reference.base+len(reference.mem)
                   for p in a.operands):
                fields.append((a.imm_offset, b.imm_offset))
        for af, bf in fields:
            av = struct.unpack_from('<I', a.bytes, af)[0]-reference.base
            bv = struct.unpack_from('<I', b.bytes, bf)[0]-target.base
            assert 0 <= bv < len(target.mem)
            assert globals_.setdefault(av, bv) == bv, 'inconsistent global identity'
    assert len(set(globals_.values())) == len(globals_), 'aliased native globals'
    assert [c['offset'] for c in calls if c.get('vtable_byte_offset') == 0x40] == [0x146, 0x1B2]
    # This complete, normalized instruction sequence derives the initial
    # software/hardware flag, optional pure bit and optional FPU-preserve bit.
    # No native body is emitted: semantic instructions below have no game data.
    flag_ops = [(i.address-target.base-rva, i.mnemonic, i.op_str) for i in new
                if 0x62 <= i.address-target.base-rva < 0xA2]
    ops = {off: (mnemonic, operands) for off, mnemonic, operands in flag_ops}
    for off, expected in {
        0x62: ('cmp', 'dword ptr [esp + 0xcc], 0xfffe0101'),
        0x6D: ('sbb', 'eax, eax'), 0x6F: ('and', 'al, 0xe0'),
        0x71: ('add', 'eax, 0x40'), 0x74: ('test', 'al, 0x40'),
        0x7D: ('test', 'byte ptr [esp + 0x26], 0x10'),
        0x84: ('or', 'al, 0x10'), 0x9B: ('or', 'al, 2'),
    }.items():
        assert ops[off] == expected
    assert globals_[0x9EDA1C] == struct.unpack_from('<I', target.mem, rva+0x122)[0]-target.base
    assert globals_[0x9EDA1C] == struct.unpack_from('<I', target.mem, rva+0x181)[0]-target.base
    # Values follow directly from the verified operations and branch guards.
    possibilities = sorted({vp | pure | preserve for vp in (0x20, 0x40)
                            for pure in ((0, 0x10) if vp == 0x40 else (0,))
                            for preserve in (0, 2)})
    assert all(not (value & 4) for value in possibilities)
    return {'rva': rva, 'size': CREATE_SIZE,
            'sha256': hashlib.sha256(bytes(target.mem[rva:rva+CREATE_SIZE])).hexdigest(),
            'calls': calls, 'possible_creation_flags': possibilities,
            'multithreaded_flag_set': False,
            'flags_global_rva': globals_[0x9EDA1C],
            'single_threaded_global_rva': globals_[0x9EDA94],
            'device_global_rva': globals_[0x9EDA34]}


def refusals(reference, target):
    info = verify(reference, target)
    # Flag constant, branch, global identity and each CreateDevice slot.
    for off in (0x73, 0x79, 0x85, 0x9D, 0x148, 0x1B4):
        at = info['rva']+off
        before = target.mem[at]
        target.mem[at] ^= 1
        rejected = False
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                verify(reference, target)
        except (AssertionError, ValueError):
            rejected = True
        finally:
            target.mem[at] = before
        assert rejected, f'altered creation contract accepted at {off:x}'
    return 6


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('images', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reference = Image(BASELINE)
    report = {'baseline': str(BASELINE),
              'baseline_sha256': hashlib.sha256(BASELINE.read_bytes()).hexdigest(),
              'images': [], 'game_launched': False, 'runtime_changed': False}
    for path in args.images:
        target = Image(path)
        info = verify(reference, target)
        info.update(path=str(path), image_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    refusal_controls=refusals(reference, target))
        report['images'].append(info)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(json.dumps(report, indent=2))
