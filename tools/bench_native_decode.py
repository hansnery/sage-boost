"""Screen exact reuse of three native animation decoders on synthetic streams.

Reads local images only. Code stays in private process memory; the temporary
manifest contains RVAs, sizes, hashes and one filter-table relocation per body.
No game initialization or asset loading runs. This is not an FPS benchmark.
"""
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
from test_fx_lifecycle import BASELINE, fnv1a
from verify_sites import Image

# Read-only research identities, never installed target addresses.
FUNCTIONS = ((0x1B2210, 250, 1), (0x1B230A, 326, 3), (0x1B2450, 329, 4))


def contract(image, rva, size):
    raw = bytes(image.mem[rva:rva + size])
    normalized = bytearray(raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    starts, jumps, fields = set(), [], []
    decoded = 0
    for ins in md.disasm(raw, image.base + rva):
        off = ins.address - image.base - rva
        starts.add(off)
        decoded += ins.size
        assert not ins.group(capstone.CS_GRP_CALL), 'unexpected decoder call'
        assert ins.mnemonic not in ('fldcw', 'fnstcw', 'ldmxcsr', 'stmxcsr'), 'FP control access'
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type == x86.X86_OP_IMM
            jumps.append(ins.operands[0].imm - image.base - rva)
        for operand in ins.operands:
            if operand.type != x86.X86_OP_MEM:
                continue
            assert not operand.mem.segment or (
                operand.mem.segment == x86.X86_REG_ES and bytes(ins.bytes) == b'\xa5'
            ), 'unexpected segmented access'
            if not operand.mem.base:
                assert ins.mnemonic == 'movss' and operand.mem.index and operand.mem.scale == 4
                assert ins.disp_size == 4
                fields.append(off + ins.disp_offset)
        if ins.imm_size == 4 and not ins.group(capstone.CS_GRP_JUMP):
            assert not any(op.type == x86.X86_OP_IMM and
                           image.base <= (op.imm & 0xffffffff) < image.base + len(image.mem)
                           for op in ins.operands), 'unexpected image immediate'
    assert decoded == size and ins.mnemonic == 'ret' and ins.op_str == '0x14'
    assert all(j in starts for j in jumps) and len(fields) == 1
    field = fields[0]
    table = struct.unpack_from('<I', raw, field)[0] - image.base
    assert any(s.VirtualAddress <= table and table + 1024 <= s.VirtualAddress + s.Misc_VirtualSize
               and s.Characteristics & 0x80000000 and not s.Characteristics & 0x20000000
               for s in image.pe.sections), 'filter table region'
    normalized[field:field + 4] = b'\0' * 4
    return bytes(normalized), field, table


def verify(reference, target):
    records, tables = [], []
    for rva, size, components in FUNCTIONS:
        _, hits = port(reference, rva, target, lengths=(size,))
        assert len(hits) == 1, 'complete decoder not unique'
        old, _, _ = contract(reference, rva, size)
        new, field, table = contract(target, hits[0], size)
        assert new == old, 'complete decoder changed'
        raw = bytes(target.mem[hits[0]:hits[0] + size])
        records.append((hits[0], size, fnv1a(raw), field, components))
        tables.append(table)
        print('decoder', components, 'bytes', size, 'sha256', hashlib.sha256(raw).hexdigest(), flush=True)
    assert len(set(tables)) == 1, 'different filter-table identities'
    return records


def refusals(reference, target):
    records = verify(reference, target)
    rejected = 0
    # Change an instruction in each complete body and then its table identity.
    for rva, size, _, field, _ in records:
        for offset in (size - 2, field):
            old = target.mem[rva + offset]
            target.mem[rva + offset] = old ^ 1
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    verify(reference, target)
            except (AssertionError, ValueError):
                rejected += 1
            finally:
                target.mem[rva + offset] = old
    assert rejected == 6
    print('Six altered-body/table controls refused', flush=True)
    return records


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image', type=Path)
    p.add_argument('executable', type=Path)
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--samples', type=int, default=21)
    a = p.parse_args()
    assert 0 <= a.samples <= 101
    reference, target = Image(BASELINE), Image(a.image)
    records = refusals(reference, target) if a.self_test else verify(reference, target)
    print('image_sha256', hashlib.sha256(a.image.read_bytes()).hexdigest(), flush=True)
    print('exe_sha256', hashlib.sha256(a.executable.read_bytes()).hexdigest(), flush=True)
    with tempfile.TemporaryDirectory(prefix='sage-native-decode-') as folder:
        manifest = Path(folder) / 'manifest.txt'
        manifest.write_bytes(('\n'.join(' '.join(map(str, r)) for r in records) + '\n').encode())
        command = [str(a.executable.resolve()), str(a.image.resolve()), str(manifest)]
        result = subprocess.call(command + [str(a.samples), '0'])
        if result:
            return result
        if a.self_test:
            for mode, label in ((1, 'missing state key'), (2, 'missing target frame'), (3, 'corrupt cached output')):
                bad = subprocess.run(command + ['0', str(mode)], capture_output=True, text=True)
                assert bad.returncode == 1 and 'decoder screen refused' in bad.stderr, bad
                print('Oracle rejects', label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
