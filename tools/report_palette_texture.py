"""Summarize a complete, successful eight-case palette texture screen."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]


def summarize(text):
    cases = []
    backend = None
    case = None
    assert 'benchmark refused:' not in text and 'mingw32-make: ***' not in text
    for line in text.splitlines():
        fields = line.split(',')
        if line.startswith('D3D9 backend:'):
            backend = 'dxvk' if 'palette_texture_dxvk' in line else 'native'
        if line.startswith('PALETTE_TEXTURE_SUPPORT,'):
            assert backend
            support = dict(zip(fields[1::2], map(int, fields[2::2])))
            case = {'backend': backend, 'support': support, 'images': [], 'samples': []}
            cases.append(case)
        if line.startswith('PALETTE_IMAGE,'):
            assert case is not None
            case['images'].append(dict(zip(fields[1::2], map(int, fields[2::2]))))
        if line.startswith('PALETTE_CONTROLS,'):
            assert case is not None
            case['controls'] = dict(zip(fields[1::2], fields[2::2]))
        if line.startswith('PALETTE_SAMPLE,'):
            assert case is not None and len(fields) == 5
            case['samples'].append({'round': int(fields[1]), 'constant_complete_ms': float(fields[2]),
                                    'texture_complete_ms': float(fields[3]), 'saving_ms': float(fields[4])})
        if line.startswith('PALETTE_SUMMARY,'):
            assert case is not None
            case['printed_summary'] = dict(zip(fields[1::2], fields[2::2]))
    assert len(cases) == 8
    expected = [(backend, bones, mode) for backend in ['native', 'dxvk']
                for bones in [6, 20] for mode in ['lit', 'shadow']]
    for case, identity in zip(cases, expected):
        controls = case['controls']
        assert (case['backend'], case['support']['bones'], controls['mode']) == identity
        assert int(controls['bones']) == identity[1] and int(controls['phases']) == 3
        assert int(controls['texture_draws']) == 288
        assert int(controls['constant_draws']) == (576 if identity[1] == 6 else 2016)
        assert len(case['images']) == 6
        for index, image in enumerate(case['images']):
            assert image['match'] == index % 2 and image['covered'] > 1000
            assert image['bad'] == 0 if image['match'] else image['bad'] > 100
        rows = case['samples']
        assert [r['round'] for r in rows] == list(range(21))
        assert all(abs(r['constant_complete_ms'] - r['texture_complete_ms'] - r['saving_ms']) < 0.00002 for r in rows)
        savings = sorted(r['saving_ms'] for r in rows)
        summary = case.pop('printed_summary')
        assert summary['mode'] == identity[2] and int(summary['bones']) == identity[1]
        assert abs(float(summary['complete_saved_median_ms']) - statistics.median(savings)) < 0.00002
        case['summary'] = {'samples': 21,
                           'constant_complete_median_ms': statistics.median(r['constant_complete_ms'] for r in rows),
                           'texture_complete_median_ms': statistics.median(r['texture_complete_ms'] for r in rows),
                           'paired_saving_median_ms': statistics.median(savings),
                           'paired_saving_p10_ms': savings[2],
                           'paired_saving_p90_ms': savings[18]}
    return cases


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=Path, required=True)
    parser.add_argument('--excluded-log', type=Path)
    args = parser.parse_args()
    cases = summarize(args.log.read_text(errors='replace'))
    rows = [dict(backend=c['backend'], bones=c['support']['bones'], mode=c['controls']['mode'], **r)
            for c in cases for r in c.pop('samples')]
    with args.samples.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    files = [args.log, *[ROOT/p for p in [
        'perf/tests/bench_palette_texture.c', 'tools/palette_texture_shaders.py',
        'perf/shaders/inst.hlsl', 'perf/build/palette_texture_shaders.h',
        'perf/build/bench_palette_texture_6.exe', 'perf/build/bench_palette_texture_20.exe',
        'perf/tests/bench_material_queue.c', 'perf/tests/material_fixture.c',
        'perf/tests/bench_dispatch.c', 'perf/tests/test_inst.c',
        'perf/build/palette_texture_dxvk/d3d9.dll']]]
    result = {'cases': cases, 'selected_input_hashes': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
              'installed_changes': False, 'gameplay_fps_measured': False,
              'limits': [
                  'Direct constant-upload baseline, not an end-to-end installed-plugin comparison.',
                  'Shared synthetic geometry and fixed group/resource lifetimes.',
                  'No native FX loop/source callbacks, visibility, UI, Present or scheduling.',
                  'Staging, upload and final completion charged; neither is pure GPU time.',
                  'Lit and shadow are separate complete workloads; savings cannot be added.',
              ]}
    if args.excluded_log:
        excluded = args.excluded_log.read_text(errors='replace')
        assert 'value==entry_sampler[i]' in excluded and 'benchmark refused:' in excluded
        result['excluded_run'] = {
            'log': str(args.excluded_log),
            'sha256': hashlib.sha256(args.excluded_log.read_bytes()).hexdigest(),
            'reason': 'Initial run stopped on DXVK vertex sampler restoration; its timings are excluded.',
            'fix': 'Explicitly restore the candidate-owned vertex texture and six sampler states; charge that path for restoration.',
        }
        source = args.excluded_log.with_suffix('.c')
        if source.exists():
            result['excluded_run']['source_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for case in cases:
        print(case['backend'], case['support']['bones'], case['controls']['mode'], case['summary'])


if __name__ == '__main__':
    main()
