"""Execute isolated native duration parsers with explicit fixture dependencies.

No game entry/imports/assets run. Numeric scanning, ceil, integral conversion
and exception reporting are substituted; conversion bodies and rate setter
come from the local image. This is correctness evidence, not an FPS test.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import struct
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import capstone
from capstone import x86
from port_sites import port
from test_fx_lifecycle import BASELINE, fnv1a
from test_native_rates import ROOT, region, sha, verify as verify_rates
from verify_sites import Image

# Offline reference identities; no installed addresses or persisted code.
BODIES = ((0x338b0a, 38), (0x338b30, 70), (0x338b76, 71), (0x339761, 119))


def cstring(image, rva):
    end = image.mem.find(b'\0', rva, rva + 512)
    assert end >= rva
    return bytes(image.mem[rva:end])


def contract(image, rva, size, kind, scale_rva):
    raw = bytes(image.mem[rva:rva + size])
    normal = bytearray(raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    insns = list(md.disasm(raw, image.base + rva))
    assert sum(i.size for i in insns) == size and insns[-1].mnemonic == 'ret' and not insns[-1].op_str
    starts = {i.address for i in insns}
    patches, dependencies = [], {}
    calls = (0, 2) if kind == 0 else (0, 1, 6, 7, 8) if kind == 3 else (0, 1, 6)
    direct_count = 0
    data_seen = set()
    pushed = 0
    imports = {i.address: i.name for d in image.pe.DIRECTORY_ENTRY_IMPORT for i in d.imports}
    for ins in insns:
        off = ins.address - image.base - rva
        assert ins.mnemonic not in ('fldcw', 'fnstcw', 'ldmxcsr', 'stmxcsr', 'fxrstor', 'xrstor')
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type == x86.X86_OP_IMM and ins.operands[0].imm in starts
        if ins.group(capstone.CS_GRP_CALL) and ins.operands[0].type == x86.X86_OP_IMM:
            assert ins.size == 5 and direct_count < len(calls)
            symbol = calls[direct_count]
            direct_count += 1
            target = ins.operands[0].imm - image.base
            assert any(a <= target < b for a, b in image.exec_ranges)
            patches.append((off + 1, 0, symbol))
            dependencies[symbol] = target
            normal[off + 1:off + 5] = b'\0' * 4
        for op in ins.operands:
            if op.type == x86.X86_OP_MEM:
                assert not op.mem.segment and not op.mem.index
                if op.mem.base:
                    assert op.mem.base in (x86.X86_REG_ESP, x86.X86_REG_EBP, x86.X86_REG_EAX, x86.X86_REG_ECX)
                    continue
                assert ins.disp_size == 4
                target = (op.mem.disp & 0xffffffff) - image.base
                field = off + ins.disp_offset
                if ins.mnemonic == 'call':
                    assert imports.get(target + image.base) == b'ceil'
                    symbol = 5
                elif ins.mnemonic == 'fadd':
                    assert bytes(image.mem[target:target + 4]) == struct.pack('<f', 2**32)
                    assert region(image, target, 4, None)
                    symbol = 4
                else:
                    assert ins.mnemonic in ('fld', 'fmul') and target == scale_rva
                    symbol = 3
                assert symbol not in data_seen
                data_seen.add(symbol)
                dependencies[symbol] = target
                patches.append((field, 1, symbol))
                normal[field:field + 4] = b'\0' * 4
            elif op.type == x86.X86_OP_IMM and ins.mnemonic == 'push' and ins.imm_size == 4:
                target = (op.imm & 0xffffffff) - image.base
                assert kind == 3 and pushed < 2 and region(image, target, 4, None)
                symbol = 9 + pushed
                pushed += 1
                field = off + ins.imm_offset
                patches.append((field, 1, symbol))
                dependencies[symbol] = target
                normal[field:field + 4] = b'\0' * 4
    assert direct_count == len(calls)
    assert data_seen == ({3} if kind == 0 else {3, 4, 5})
    assert pushed == (2 if kind == 3 else 0)
    return bytes(normal), patches, dependencies


def verify(reference, target):
    ref_rates, rates = verify_rates(reference, reference), verify_rates(reference, target)
    records = []
    for kind, (ref_rva, size) in enumerate(BODIES):
        _, hits = port(reference, ref_rva, target, lengths=(size,))
        old, old_patches, old_deps = contract(reference, ref_rva, size, kind, ref_rates['globals_rva'] + 8)
        qualified = []
        for hit in hits:
            try:
                new, patches, deps = contract(target, hit, size, kind, rates['globals_rva'] + 8)
                assert old == new and old_patches == patches, 'native parser changed'
            except AssertionError:
                continue
            qualified.append((hit, patches, deps))
        assert len(qualified) == 1, 'complete parser and duration dependency not unique'
        hit, patches, deps = qualified[0]
        if kind == 3:
            assert cstring(reference, old_deps[9]) == cstring(target, deps[9]), 'overflow format changed'
        raw = bytes(target.mem[hit:hit + size])
        records.append({'kind': kind, 'rva': hit, 'size': size, 'fnv1a': fnv1a(raw),
                        'body_sha256': hashlib.sha256(raw).hexdigest(),
                        'pattern_matches': hits, 'patches': patches, 'dependencies': deps})
    for symbol in (0, 3):
        assert len({r['dependencies'][symbol] for r in records}) == 1
    for symbol in (1, 4, 5, 6):
        assert len({r['dependencies'][symbol] for r in records[1:]}) == 1
    records.append({'kind': 4, **{k: rates[k] for k in ('rva', 'size', 'fnv1a', 'body_sha256')},
                    'patches': [(field, 1, 20 + slot) for field, slot in rates['patches']]})
    return records


def negative_preflight(reference, target, records):
    changes = [(f'changed_return_{r["kind"]}', r['rva'] + r['size'] - 1, b'\x90') for r in records[:4]]
    field = next(f for f, _, s in records[0]['patches'] if s == 3)
    changes.append(('wrong_duration_global', records[0]['rva'] + field,
                    struct.pack('<I', target.base + records[0]['dependencies'][3] + 4)))
    changes.append(('wrong_unsigned_bias', records[1]['dependencies'][4], struct.pack('<f', 2**31)))
    results = []
    for label, where, value in changes:
        saved = bytes(target.mem[where:where + len(value)])
        try:
            target.mem[where:where + len(value)] = value
            try:
                verify(reference, target)
            except AssertionError as e:
                results.append({'fault': label, 'rejected': True, 'reason': str(e)})
            else:
                raise RuntimeError('accepted ' + label)
        finally:
            target.mem[where:where + len(value)] = saved
    assert verify(reference, target) == records
    return results


def oracle(stdout):
    factors, rows, floats = {}, [], []
    for line in stdout.splitlines():
        parts = line.split(',')
        if parts[0] == 'FACTOR':
            factors[int(parts[1])] = struct.unpack('<f', struct.pack('<I', int(parts[2], 16)))[0]
        elif parts[0] == 'INTEGER':
            mode, ms, u32, u16, u8, thrown, doubled, ok16, ok8 = map(int, parts[1:])
            expected = math.ceil(ms * factors[mode])
            assert u32 == expected and u16 == expected % 65536, ('integer oracle', parts, expected)
            assert thrown == (expected > 255)
            if not thrown:
                assert u8 == expected
            if mode == 0:
                assert doubled == 2 * expected and ok16 == (2 * expected <= 65535) and ok8 == (2 * expected <= 255)
            rows.append({'mode': mode, 'milliseconds': ms, 'u32': u32, 'u16': u16,
                         'u8': None if thrown else u8, 'byte_threw': bool(thrown),
                         'compensated_u32': doubled if mode == 0 else None,
                         'compensated_u16_fits': bool(ok16) if mode == 0 else None,
                         'compensated_u8_fits': bool(ok8) if mode == 0 else None})
        elif parts[0] == 'REAL':
            mode, input_word, result_word = int(parts[1]), int(parts[2], 16), int(parts[3], 16)
            value = struct.unpack('<f', struct.pack('<I', input_word))[0]
            expected_word = struct.unpack('<I', struct.pack('<f', value * factors[mode]))[0]
            assert result_word == expected_word, ('real oracle', parts, hex(expected_word))
            floats.append({'mode': mode, 'input': value, 'result_word': parts[3]})
    assert len(factors) == 4 and len(rows) == 100 and len(floats) == 32
    assert 'PASS,ABI,0' in stdout
    return factors, rows, floats


def check(reference, path, executable):
    initial_hash = sha(path)
    target = Image(path)
    records = verify(reference, target)
    refusals = negative_preflight(reference, target, records)
    with tempfile.TemporaryDirectory(prefix='sage-native-duration-') as folder:
        manifest = Path(folder) / 'manifest.txt'
        lines = []
        for r in records:
            lines.append(f"{r['rva']} {r['size']} {r['fnv1a']} {len(r['patches'])}")
            lines += [' '.join(map(str, p)) for p in r['patches']]
        manifest.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))
        command = [str(executable.resolve()), str(path.resolve()), str(manifest)]
        def run(mode):
            p = subprocess.run(command + [str(mode)], capture_output=True, text=True, timeout=30)
            return {'returncode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}
        positive = run(0)
        assert positive['returncode'] == 0, positive
        factors, rows, floats = oracle(positive['stdout'])
        faults = []
        for mode, label in ((1, 'floor_instead_of_ceil'), (2, 'scale_before_rounding'), (3, 'narrow_overflow_write')):
            result = run(mode)
            rejected = False
            if result['returncode'] == 1 and 'duration check refused' in result['stderr']:
                rejected = True
            elif result['returncode'] == 0:
                try:
                    oracle(result['stdout'])
                except AssertionError:
                    rejected = True
            assert rejected, (label, result)
            faults.append({'fault': label, 'rejected': rejected, **result})
    assert sha(path) == initial_hash
    print(f'{path.name}: native duration outputs and compensation pass; 9 negative controls rejected', flush=True)
    return {'path': str(path.resolve()), 'sha256': initial_hash, 'contracts': records,
            'preflight_faults': refusals, 'factors': factors, 'integer_cases': rows,
            'real_cases': floats, 'positive': positive, 'output_faults': faults}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image', action='append', type=Path, required=True)
    p.add_argument('--executable', type=Path, default=ROOT / 'perf/build/test_native_durations.exe')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    files = ['tools/test_native_durations.py', 'tools/test_native_rates.py', 'tools/port_sites.py',
             'tools/verify_sites.py', 'tools/test_fx_lifecycle.py', 'perf/tests/test_native_durations.c']
    hashes = {f: sha(ROOT / f) for f in files}
    exe_hash, ref_hash = sha(a.executable), sha(BASELINE)
    reference = Image(BASELINE)
    images = [check(reference, path, a.executable) for path in a.image]
    assert all(sha(ROOT / f) == h for f, h in hashes.items())
    assert sha(a.executable) == exe_hash and sha(BASELINE) == ref_hash
    report = {'schema': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
              'purpose': 'native duration correctness and legacy quantization; no FPS claim',
              'source_sha256': hashes, 'reference': {'path': str(BASELINE), 'sha256': ref_hash},
              'executable': {'path': str(a.executable.resolve()), 'sha256': exe_hash}, 'images': images,
              'substitutions': ['numeric token getters/scanners', 'positive finite mathematical ceil',
                                'exact integral x87 result conversion without control-word changes',
                                'byte-overflow exception reporting via fixture longjmp'],
              'limits': ['no text tokenization, MSVC exception unwinding or live object storage qualification',
                         'no installed hook, game startup, multiplayer or gameplay executed',
                         'narrow overflow is refused by the candidate; actual field coverage unresolved']}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Report:', a.output)


if __name__ == '__main__':
    main()
