"""Run the native frame-rate setter in private fixture memory, never the game.

This is a correctness check, not an FPS benchmark. No image bytes are written.
Reports retain source/input hashes, operand metadata and native output values.
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
from verify_sites import Image

# Read-only BFME2 research identity. No fixed address enters an install path.
REFERENCE_RVA, SIZE = 0x238895, 191
NAMES = ('logic_hz', 'client_hz', 'logic_frames_per_ms', 'ms_per_logic_frame',
         'logic_rate_real', 'seconds_per_logic_frame', 'ms_per_client_frame',
         'client_frames_per_ms', 'client_rate_real', 'seconds_per_client_frame')
ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def region(image, rva, size, writable):
    return any(s.VirtualAddress <= rva and rva + size <= s.VirtualAddress + s.Misc_VirtualSize
               and not s.Characteristics & 0x20000000
               and s.Characteristics & 0x40000000
               and (writable is None or bool(s.Characteristics & 0x80000000) == writable)
               for s in image.pe.sections)


def contract(image, rva):
    raw = bytes(image.mem[rva:rva + SIZE])
    normalized = bytearray(raw)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    instructions = list(md.disasm(raw, image.base + rva))
    assert sum(i.size for i in instructions) == SIZE
    assert instructions[-1].mnemonic == 'ret' and not instructions[-1].op_str
    starts = {i.address for i in instructions}
    reads, writes, branches = [], [], []
    for ins in instructions:
        assert not ins.group(capstone.CS_GRP_CALL), 'unexpected call'
        assert ins.mnemonic not in ('fldcw', 'fnstcw', 'ldmxcsr', 'stmxcsr', 'fxrstor', 'xrstor')
        if ins.group(capstone.CS_GRP_JUMP):
            assert ins.operands[0].type == x86.X86_OP_IMM and ins.operands[0].imm in starts
            branches.append(ins.mnemonic)
        for operand in ins.operands:
            if operand.type != x86.X86_OP_MEM:
                continue
            mem = operand.mem
            assert not mem.segment and not mem.index
            if mem.base:
                assert mem.base == x86.X86_REG_ESP and mem.disp == 8
                continue
            assert operand.size == 4 and ins.disp_size == 4
            field = ins.address - image.base - rva + ins.disp_offset
            target = (mem.disp & 0xffffffff) - image.base
            assert target % 4 == 0
            if operand.access & capstone.CS_AC_WRITE:
                assert operand.access == capstone.CS_AC_WRITE and region(image, target, 4, True)
                writes.append((field, target))
            else:
                # The reference's .rdata is writable in the PE header. Read
                # access, exact values and separation from writes are checked.
                assert operand.access == capstone.CS_AC_READ and region(image, target, 4, None)
                reads.append((field, target))
            normalized[field:field + 4] = b'\0' * 4
    assert branches == ['jle'] and len(reads) == 3 and len(writes) == 10
    base = min(target for _, target in writes)
    assert sorted(target for _, target in writes) == list(range(base, base + 40, 4)), 'output identities'
    assert region(image, base, 40, True)
    assert len({target for _, target in reads}) == 3
    assert all(not base <= target < base + 40 for _, target in reads)
    for (_, target), expected in zip(reads, (0.001, 1000.0, 1.0)):
        assert bytes(image.mem[target:target + 4]) == struct.pack('<f', expected), 'wrong conversion constant'
    patches = [(field, slot) for slot, (field, _) in enumerate(reads)]
    patches += [(field, 3 + (target - base) // 4) for field, target in writes]
    return bytes(normalized), patches, base, reads


def verify(reference, target):
    _, hits = port(reference, REFERENCE_RVA, target, lengths=(SIZE,))
    assert len(hits) == 1, 'full native rate body not unique'
    old, old_patches, old_globals, _ = contract(reference, REFERENCE_RVA)
    new, patches, globals_rva, reads = contract(target, hits[0])
    assert old == new and old_patches == patches, 'complete native contract changed'
    initial = bytes(target.mem[globals_rva:globals_rva + 40])
    assert initial == bytes(reference.mem[old_globals:old_globals + 40]), 'initial rate block changed'
    raw = bytes(target.mem[hits[0]:hits[0] + SIZE])
    return {'rva': hits[0], 'size': SIZE, 'fnv1a': fnv1a(raw),
            'body_sha256': hashlib.sha256(raw).hexdigest(), 'patches': patches,
            'globals_rva': globals_rva, 'initial_words': list(struct.unpack('<10I', initial)),
            'constant_rvas': [target for _, target in reads]}


def preflight_faults(reference, target, record):
    rva = record['rva']
    writes = [(field, slot) for field, slot in record['patches'] if slot >= 3]
    first, second = writes[:2]
    alterations = (
        ('changed_return_instruction', rva + SIZE - 1, b'\x90'),
        ('wrong_conversion_constant', record['constant_rvas'][0], struct.pack('<f', 0.002)),
        ('aliased_output', rva + first[0], bytes(target.mem[rva + second[0]:rva + second[0] + 4])),
    )
    results = []
    for label, where, value in alterations:
        before = bytes(target.mem[where:where + len(value)])
        try:
            target.mem[where:where + len(value)] = value
            try:
                verify(reference, target)
            except AssertionError as error:
                results.append({'fault': label, 'rejected': True, 'reason': str(error)})
            else:
                raise RuntimeError('preflight accepted ' + label)
        finally:
            target.mem[where:where + len(value)] = before
    assert verify(reference, target) == record
    return results


def process(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=20)
    return {'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}


def check_image(reference, path, executable):
    initial_hash = sha(path)
    target = Image(path)
    record = verify(reference, target)
    refused = preflight_faults(reference, target, record)
    with tempfile.TemporaryDirectory(prefix='sage-native-rates-') as folder:
        manifest = Path(folder) / 'manifest.txt'
        lines = [f"{record['rva']} {SIZE} {record['fnv1a']} {len(record['patches'])}"]
        lines += [f'{field} {slot}' for field, slot in record['patches']]
        manifest.write_bytes(('\n'.join(lines) + '\n').encode('ascii'))
        command = [str(executable.resolve()), str(path.resolve()), str(manifest)]
        positive = process(command + ['0'])
        assert positive['returncode'] == 0 and 'PASS,13,512,ABI,0' in positive['stdout'], positive
        rows = []
        for line in positive['stdout'].splitlines():
            if not line.startswith('RATE,'):
                continue
            parts = line.split(',')
            assert len(parts) == 13
            words = [int(word, 16) for word in parts[3:]]
            values = words[:2] + [struct.unpack('<f', struct.pack('<I', word))[0] for word in words[2:]]
            rows.append({'requested_logic': int(parts[1]), 'requested_client': int(parts[2]),
                         'native_output': dict(zip(NAMES, values)), 'raw_words': parts[3:]})
        assert len(rows) == 13
        faults = []
        for mode, label in ((1, 'stale_client_rate'), (2, 'stale_duration_scale'), (3, 'stale_client_step')):
            result = process(command + [str(mode)])
            assert result['returncode'] == 1 and 'rate check refused' in result['stderr'], result
            faults.append({'fault': label, **result})
    assert sha(path) == initial_hash, 'input image changed during check'
    initial_words = record['initial_words']
    configured_words = [int(word, 16) for word in rows[0]['raw_words']]
    differences = [{'field': NAMES[i], 'startup_word': f'{before:08x}', 'configured_word': f'{after:08x}'}
                   for i, (before, after) in enumerate(zip(initial_words, configured_words)) if before != after]
    assert [item['field'] for item in differences] == ['logic_frames_per_ms', 'client_frames_per_ms']
    assert all(configured_words[i] == initial_words[i] + 1 for i in (2, 7))
    initial_scale = struct.unpack('<f', struct.pack('<I', initial_words[2]))[0]
    modeled_durations = []
    for ms in (200, 250, 1000):
        entries = []
        for label, hz, scale in (
            ('startup_5hz', 5, initial_scale),
            ('setter_5hz', 5, rows[0]['native_output']['logic_frames_per_ms']),
            ('setter_10hz', 10, rows[1]['native_output']['logic_frames_per_ms']),
        ):
            ticks = math.ceil(ms * scale)
            entries.append({'mode': label, 'ticks': ticks, 'elapsed_ms': ticks * 1000 // hz})
        modeled_durations.append({'requested_ms': ms, 'results': entries})
    print(f'{path.name}: 13 cases + 512 reconfiguration calls pass; 6 negative controls rejected; '
          'two startup constants differ from recomputed 5/30', flush=True)
    return {'path': str(path.resolve()), 'sha256': initial_hash, 'contract': record,
            'preflight_faults': refused, 'positive': positive, 'rows': rows, 'output_faults': faults,
            'startup_vs_configured_baseline': differences,
            'duration_rounding_from_native_factors': {
                'scope': 'arithmetic model using executed setter factors; native integer parser not executed',
                'examples': modeled_durations}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, action='append', required=True)
    parser.add_argument('--executable', type=Path, default=ROOT / 'perf/build/test_native_rates.exe')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sources = ['tools/test_native_rates.py', 'tools/port_sites.py', 'tools/verify_sites.py',
               'tools/test_fx_lifecycle.py', 'perf/tests/test_native_rates.c']
    hashes = {p: sha(ROOT / p) for p in sources}
    executable_hash = sha(args.executable)
    reference_hash = sha(BASELINE)
    reference = Image(BASELINE)
    images = [check_image(reference, path, args.executable) for path in args.image]
    assert all(sha(ROOT / p) == value for p, value in hashes.items())
    assert sha(args.executable) == executable_hash and sha(BASELINE) == reference_hash
    report = {
        'schema': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
        'purpose': 'native rate-setter correctness only; no runtime integration or FPS measurement',
        'reference': {'path': str(BASELINE), 'sha256': reference_hash},
        'executable': {'path': str(args.executable.resolve()), 'sha256': executable_hash},
        'source_sha256': hashes, 'images': images,
        'duration_rounding_model': {
            'scope': 'integer arithmetic illustration; native integer INI parser not executed',
            'milliseconds': 250, 'baseline': {'hz': 5, 'ticks': 2, 'elapsed_ms': 400},
            'uncompensated_beta': {'hz': 10, 'ticks': 3, 'elapsed_ms': 300}},
        'limits': ['no real game or network executed', 'no startup/load-order verification in installed images',
                   'no full gameplay-time compensation', 'no measured frame time or FPS', 'no files installed'],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Report:', args.output)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
