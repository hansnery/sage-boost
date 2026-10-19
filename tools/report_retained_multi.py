"""Summarize a successful four-case retained multi-extra make target log."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]


def summarize(text):
    cases = []
    case = None
    names = [('native', 'lit'), ('native', 'shadow'), ('dxvk', 'lit'), ('dxvk', 'shadow')]
    for line in text.splitlines():
        fields = line.split(',')
        if line.startswith('MULTI_CONTROL,phase,1,'):
            assert len(cases) < 4
            backend, mode = names[len(cases)]
            case = {'backend': backend, 'mode': mode, 'controls': [], 'samples': []}
            cases.append(case)
        if line.startswith('MULTI_CONTROL,'):
            assert case is not None
            case['controls'].append(dict(zip(fields[1::2], map(int, fields[2::2]))))
        if line.startswith('MULTI_SAMPLE,'):
            assert case is not None and len(fields) == 11
            case['samples'].append(dict(zip(
                ['extras', 'round', 'base_regular', 'retained_regular', 'base_extras',
                 'retained_extras', 'base_cycle', 'retained_cycle', 'base_complete', 'retained_complete'],
                [int(fields[1]), int(fields[2]), *map(float, fields[3:])])))
    assert len(cases) == 4
    assert 'benchmark refused' not in text and 'mingw32-make: ***' not in text
    for case in cases:
        assert [x['phase'] for x in case['controls']] == [1, 2, 3]
        assert all(x['stale_changed'] > 100 for x in case['controls'])
        case['summary'] = []
        assert len(case['samples']) == 42
        for extras in [1, 3]:
            rows = [r for r in case['samples'] if r['extras'] == extras]
            assert [r['round'] for r in rows] == list(range(21))
            cpu = sorted(r['base_cycle'] - r['retained_cycle'] for r in rows)
            complete = sorted(r['base_complete'] - r['retained_complete'] for r in rows)
            case['summary'].append({'extras': extras, 'samples': 21,
                                    'cpu_cycle_saving_median_ms': statistics.median(cpu),
                                    'completion_cycle_saving_median_ms': statistics.median(complete),
                                    'completion_cycle_saving_p10_ms': complete[2]})
    return cases


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--samples', type=Path, required=True)
    args = p.parse_args()
    cases = summarize(args.log.read_text(errors='replace'))
    flat = [dict(backend=c['backend'], mode=c['mode'], **r) for c in cases for r in c.pop('samples')]
    with args.samples.open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(flat[0]))
        writer.writeheader()
        writer.writerows(flat)
    files = [args.log, ROOT/'perf/tests/bench_retained_multi.c', ROOT/'perf/build/bench_retained_multi.exe']
    result = {'cases': cases, 'hashes': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
              'installed_changes': False, 'gameplay_fps_measured': False,
              'limits': ['Controlled synthetic scene, borrowed lifetimes and narrow state mask.',
                         'No native effect callbacks, scene traversal, UI, Present or scheduler.',
                         'Original retained fixture baseline; not an end-to-end beta.17 comparison.',
                         'Lit and shadow are alternative full workloads; do not add their savings.']}
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    for case in cases:
        print(case['backend'], case['mode'], case['summary'])


if __name__ == '__main__':
    main()
