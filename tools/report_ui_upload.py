"""Validate and summarize the complete native/DXVK UI upload screen."""
import argparse
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import statistics
import struct

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = [(n, g) for n in (64, 256, 1024) for g in (1, 16, 64)]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse(text, expected_backend):
    require('benchmark failed:' not in text and 'benchmark refused:' not in text,
            'Failed or refused benchmark')
    lines = text.splitlines()
    require(lines.count('UI_UPLOAD_PASS') == 1 and lines[-1] == 'UI_UPLOAD_PASS',
            'Missing or repeated completion marker')
    cases = []
    backend = None
    current = None
    for fields in csv.reader(io.StringIO(text)):
        if not fields:
            continue
        if fields[0] == 'UI_BACKEND':
            require(backend is None and len(fields) == 3 and fields[1] == expected_backend,
                    'Wrong or repeated backend')
            backend = fields[2]
        elif fields[0] == 'UI_CONTROLS':
            require(backend is not None and len(fields) == 25, 'Malformed controls')
            controls = dict(zip(fields[1::2], map(int, fields[2::2])))
            current = {'backend': expected_backend, 'controls': controls, 'samples': []}
            cases.append(current)
        elif fields[0] == 'UI_SAMPLE':
            require(current is not None and len(fields) == 7, 'Malformed sample')
            n, g, round_number = map(int, fields[1:4])
            require((n, g) == (current['controls']['requests'], current['controls']['group']),
                    'Sample belongs to another case')
            a, b, saving = map(float, fields[4:])
            require(all(math.isfinite(v) for v in (a, b, saving)) and a > 0 and b > 0,
                    'Invalid timing')
            require(abs(a - b - saving) < 0.000002, 'Inconsistent saving')
            current['samples'].append({'round': round_number, 'direct_ms': a,
                                       'staged_ms': b, 'saving_ms': saving})
        else:
            require(fields == ['UI_UPLOAD_PASS'], 'Unexpected benchmark output')
    require(len(cases) == len(EXPECTED), 'Incomplete case matrix')
    for case, (n, g) in zip(cases, EXPECTED):
        c = case['controls']
        draws = n // g + int(n > 833 and 833 % g != 0)
        expected = dict(requests=n, group=g, direct_locks=n, staged_locks=draws,
                        draws=draws, wraps=int(n > 833), phases=3, exact_images=3,
                        nonempty_images=3, stale_rejected=3, state_fault_rejected=3,
                        restoration_checks=12)
        require(c == expected, 'Wrong control or geometry counts')
        rows = case['samples']
        require([r['round'] for r in rows] == list(range(21)), 'Missing or repeated sample')
        savings = sorted(r['saving_ms'] for r in rows)
        case['summary'] = {
            'samples': 21, 'frames_per_path_per_sample': 8,
            'direct_complete_median_ms': statistics.median(r['direct_ms'] for r in rows),
            'staged_complete_median_ms': statistics.median(r['staged_ms'] for r in rows),
            'paired_saving_median_ms': statistics.median(savings),
            'paired_saving_p10_ms': savings[2], 'paired_saving_p90_ms': savings[18],
            'positive_pairs': sum(v > 0 for v in savings),
        }
    require(backend is not None, 'Missing backend')
    return backend, cases


def refusal_checks(text):
    """Corrupt a real successful record; none may qualify as evidence."""
    sample = next(line for line in text.splitlines() if line.startswith('UI_SAMPLE,'))
    fields = sample.split(',')
    nonfinite = ','.join(fields[:4] + ['nan'] + fields[5:])
    inconsistent = ','.join(fields[:6] + ['12345'])
    invalid = {
        'missing_completion': text.replace('UI_UPLOAD_PASS', ''),
        'explicit_failure': text + '\nbenchmark failed: injected\n',
        'missing_sample': text.replace(sample + '\n', '', 1),
        'duplicate_sample': text.replace(sample, sample + '\n' + sample, 1),
        'nonfinite_timing': text.replace(sample, nonfinite, 1),
        'inconsistent_saving': text.replace(sample, inconsistent, 1),
        'failed_image_control': text.replace('exact_images,3', 'exact_images,0', 1),
        'failed_stale_control': text.replace('stale_rejected,3', 'stale_rejected,0', 1),
        'failed_state_control': text.replace('state_fault_rejected,3', 'state_fault_rejected,0', 1),
    }
    for name, changed in invalid.items():
        try:
            parse(changed, 'native')
        except ValueError:
            continue
        raise ValueError(f'Report accepted invalid evidence: {name}')
    return list(invalid)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def machine(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    require(data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0', 'Not a PE input')
    return struct.unpack_from('<H', data, pe+4)[0]


def native_module_path(reported):
    path = Path(reported)
    # The i686 fixture prints System32. A 64-bit Python reader must hash the
    # SysWOW64 image that Windows actually maps into the 32-bit process.
    if machine(path) == 0x14c:
        return path
    windows = Path(os.environ['SystemRoot'])
    require(str(path).casefold() == str(windows / 'system32' / 'd3d9.dll').casefold(),
            'Unexpected native module location')
    path = windows / 'SysWOW64' / 'd3d9.dll'
    require(machine(path) == 0x14c, 'Native backend is not i686')
    return path


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--native-log', type=Path, required=True)
    parser.add_argument('--dxvk-log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    args = parser.parse_args()
    native_text = args.native_log.read_text()
    native_dll, native = parse(native_text, 'native')
    dxvk_dll, dxvk = parse(args.dxvk_log.read_text(), 'dxvk')
    refused = refusal_checks(native_text)
    require(machine(Path(dxvk_dll)) == 0x14c, 'DXVK backend is not i686')
    native_path = native_module_path(native_dll)
    files = [args.native_log, args.dxvk_log, native_path, Path(dxvk_dll),
             *[ROOT / p for p in ('perf/tests/bench_ui_upload.c', 'perf/tests/bench_guard.h',
                                  'perf/Makefile', 'perf/build/bench_ui_upload.exe',
                                  'tools/report_ui_upload.py')]]
    for output in (args.output, args.samples):
        require(output.resolve().is_relative_to(ROOT / 'docs' / 'benchmarks'),
                'Output must be within this checkout docs/benchmarks')
        require(all(output.resolve() != p.resolve() for p in files), 'Output overwrites input')
    require(args.output.resolve() != args.samples.resolve(), 'Outputs must differ')
    cases = native + dxvk
    rows = [dict(backend=c['backend'], requests=c['controls']['requests'],
                 group=c['controls']['group'], **r) for c in cases for r in c.pop('samples')]
    result = {
        'cases': cases, 'selected_input_sha256': {str(p): digest(p) for p in files},
        'native_module': {'loader_reported_path': native_dll,
                          'wow64_resolved_hash_path': str(native_path)},
        'report_refusal_controls': refused,
        'installed_changes': False, 'gameplay_fps_measured': False,
        'limits': [
            'Generated fixed-function UI geometry/textures; no EA assets or native callbacks executed.',
            'No measured live UI request/batch distribution; tested counts are a parameter sweep.',
            'Identical draw boundaries/order; this screens vertex upload strategy only.',
            'Fresh generation, extra CPU copy, state restoration and GPU completion are timed.',
            'No scene, shadows, logic, Present or pacing; these timings cannot be converted to game FPS.',
            'Native D3D9 and DXVK results are separate workloads; do not combine them.',
            'Positive screens require native boundary/resource qualification and live frequency evidence.',
        ],
    }
    with args.samples.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    result['samples_sha256'] = digest(args.samples)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for case in cases:
        c, s = case['controls'], case['summary']
        print(f"{case['backend']:6s} {c['requests']:4d}/{c['group']:2d}: "
              f"paired median {s['paired_saving_median_ms']:+.6f} ms "
              f"p10/p90 {s['paired_saving_p10_ms']:+.6f}/{s['paired_saving_p90_ms']:+.6f}")
    print(f'Rejected {len(refused)} corrupted evidence controls; {len(rows)} paired samples.')


if __name__ == '__main__':
    main()
