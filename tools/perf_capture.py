"""Report a chosen interval from a COPIED bfme2perf timing CSV (read only).

Usage: python tools/perf_capture.py CAPTURE/bfme2perf_timing.csv --start-s 100
       [--seconds 60]

Select the interval from the user's settled gameplay, never the fastest rows.
The row at start + seconds must exist, because the final shutdown row can be
partial. Missing seconds are refused rather than treated as one-second rows.
This reports completed flip-enabled EndScene calls, not monitor refresh or
successful GPU/display completion. It never declares the overall goal met.
"""
import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path


BOUNDS_US = (8333, 12500, 16667, 20000, 25000, 33334,
             50000, 100000, 200000, 500000, 1000000)
BIN_COLUMNS = tuple(f'present_gap_le_{bound}us' for bound in BOUNDS_US) + (
    'present_gap_over_1000000us',)
VERIFY_COLUMNS = ('verify_bad', 'shadow_verify_bad', 'spv_bad', 'lt_verify_bad')
COUNT_COLUMNS = ('frames', 'present_calls', 'present_gaps', 'extra_frames',
                 'extra_shadows', 'extra_shadow_samples', 'extra_profile_valid',
                 'extra_profile_invalid', 'shadow_skipped') + VERIFY_COLUMNS
BULK_COLUMNS = tuple(f'{prefix}bulk_followers_{methods}'
                     for prefix in ('', 'extra_') for methods in range(1, 7))
REGULAR_TIMES = ('frame_avg_ms', 'logic_ms', 'render_ms', 'present_ms',
                 'shadow_ms', 'reflect_ms', 'views_ms', 'ui_ms', 'vis_ms',
                 'cust_ms', 'flush_ms', 'fx_batch_ms', 'fx_mesh_ms',
                 'skel_render_ms', 'terrain_ms')
EXTRA_TIMES = (('extra_ms', 'extra_frames', True),
               ('extra_profile_total_ms', 'extra_profile_valid', False),
               ('extra_shadow_total_ms', 'extra_shadow_samples', False),
               ('extra_reflect_total_ms', 'extra_reflect_samples', False),
               ('extra_views_total_ms', 'extra_views_samples', False),
               ('extra_ui_total_ms', 'extra_ui_samples', False))


def count(row, key):
    value = row.get(key)
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]+', value):
        raise ValueError(f"row {row.get('time_s', '?')}: missing/invalid {key}")
    return int(value)


def load_rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream, strict=True)
        header = reader.fieldnames
        if not header or len(header) != len(set(header)):
            raise ValueError('missing or duplicate CSV header')
        rows = list(reader)
    if any(None in row or any(value is None for value in row.values()) for row in rows):
        raise ValueError('incomplete CSV row or unexpected extra fields')
    return rows


def quantile(bins, percent):
    total = sum(bins)
    if not total:
        return None
    rank = (total * percent + 99) // 100
    cumulative = 0
    for index, size in enumerate(bins):
        cumulative += size
        if cumulative >= rank:
            return {
                'lower_exclusive_ms': BOUNDS_US[index - 1] / 1000 if index else None,
                'upper_ms': BOUNDS_US[index] / 1000 if index < len(BOUNDS_US) else None,
            }
    raise AssertionError('unreachable percentile rank')


def bulk_coverage(rows, totals):
    """Pool raw follower counts; older captures and blank fields are unknown."""
    present = [set(BULK_COLUMNS).intersection(row) for row in rows]
    if not any(present):
        return {'available': False, 'reason': 'columns absent in this capture'}
    if any(len(keys) != len(BULK_COLUMNS) for keys in present):
        raise ValueError('incomplete bulk follower columns')
    measured = []
    for row in rows:
        values = [row[key] for key in BULK_COLUMNS]
        if all(value == '' for value in values):
            continue
        measured.append({key: count(row, key) for key in BULK_COLUMNS})
    report = {'available': len(measured) == len(rows),
              'instrumented_rows': len(measured), 'selected_rows': len(rows)}
    if not report['available']:
        report['reason'] = 'instrumentation absent in some or all selected rows'
        return report
    for mode, prefix, denominator in (
            ('regular', '', totals['frames']),
            ('extra', 'extra_', totals['extra_frames'])):
        counts = {str(methods): sum(row[f'{prefix}bulk_followers_{methods}']
                                   for row in measured)
                  for methods in range(1, 7)}
        consumed = sum(counts.values())
        multi = consumed - counts['1']
        report[mode] = {
            'consumed_followers_by_method_count': counts,
            'consumed_followers': consumed,
            'multi_method_followers': multi,
            'multi_method_fraction': multi / consumed if consumed else None,
            'frame_denominator': denominator,
            'consumed_followers_per_frame': consumed / denominator if denominator else None,
        }
    report['limits'] = [
        'Consumed follower records across render passes; not unique units or successful draws.',
        'Deferred leaders are excluded; no total eligible-record denominator is recorded.',
        'Regular per-frame values include updates whose render was skipped.',
        'Method counts measure use of the path, not time saved or FPS gained.',
    ]
    return report


def pooled_time(rows, column, denominator, per_sample):
    """CSV averages need count weighting; *_total_ms already contains sums."""
    total = 0.0
    samples = measured = 0
    for row in rows:
        if column not in row or denominator not in row:
            continue
        n = count(row, denominator)
        value = row[column]
        if value == '':
            if n:
                continue  # Unknown instrumentation is not a zero-cost sample.
            ms = 0.0
        else:
            try:
                ms = float(value)
            except (TypeError, ValueError) as error:
                raise ValueError(f'invalid timing {column}') from error
            if not math.isfinite(ms) or ms < 0:
                raise ValueError(f'invalid timing {column}')
        if n == 0 and ms != 0:
            raise ValueError(f'{column}: nonzero time without samples')
        measured += 1
        samples += n
        total += ms * n if per_sample else ms
    result = {'available': measured == len(rows), 'measured_rows': measured,
              'selected_rows': len(rows), 'sample_count_column': denominator,
              'input_is_per_sample_average': per_sample}
    if result['available']:
        result.update(samples=samples, summed_ms=total,
                      mean_ms=total / samples if samples else None)
    else:
        result['reason'] = 'missing columns or unmeasured timing in selected rows'
    return result


def frame_costs(rows):
    return {
        'regular': {column: pooled_time(rows, column, 'frames', True)
                    for column in REGULAR_TIMES},
        'extra': {column: pooled_time(rows, column, denominator, per_sample)
                  for column, denominator, per_sample in EXTRA_TIMES},
        'limits': [
            'Regular fields are weighted by ordinary updates, including any skipped render.',
            'Extra outer time uses all drawn extras; profiles use their own valid sample counts.',
            'Nested scopes overlap: do not add render, shadow, views, FX, and mesh times.',
            'Present scope is CPU call duration; it does not isolate GPU completion or display latency.',
            'Row-average precision limits reconstructed sums; no individual frame timeline exists.',
            'Admitted extras are selected by scheduling; refused-frame costs remain unknown.',
            'These means do not predict 120 FPS, admission decisions, or future-frame deadlines.',
        ],
    }


def analyze(rows, start, seconds):
    if start < 0 or seconds <= 0:
        raise ValueError('start must be nonnegative and seconds positive')
    times = [count(row, 'time_s') for row in rows]
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('CSV times must be strictly increasing')
    end = start + seconds
    if start not in times or end not in times:
        raise ValueError('missing start/end boundary; final row may be partial')
    selected = [row for row, time in zip(rows, times) if start <= time < end]
    if [count(row, 'time_s') for row in selected] != list(range(start, end)):
        raise ValueError('selected interval must have consecutive one-second rows')
    parsed = [{key: count(row, key) for key in COUNT_COLUMNS + BIN_COLUMNS}
              for row in selected]
    for time, row in zip(range(start, end), parsed):
        if sum(row[key] for key in BIN_COLUMNS) != row['present_gaps']:
            raise ValueError(f'row {time}: histogram does not match present_gaps')
        if not 0 <= row['present_calls'] - row['present_gaps'] <= 1:
            raise ValueError(f'row {time}: present_calls and present_gaps disagree')
    totals = {key: sum(row[key] for row in parsed) for key in COUNT_COLUMNS}
    if totals['present_calls'] - totals['present_gaps'] > 1:
        raise ValueError('more than one missing initial presentation gap')
    bins = [sum(row[key] for row in parsed) for key in BIN_COLUMNS]
    gaps = totals['present_gaps']
    calls = totals['present_calls']
    extras = totals['extra_frames']
    shadow_counts_match = bool(extras) and all(
        row['extra_frames'] == row['extra_shadows'] == row['extra_shadow_samples']
        == row['extra_profile_valid'] and row['extra_profile_invalid'] == 0
        for row in parsed)
    return {
        'window': {'start_s': start, 'end_s_exclusive': end,
                   'nominal_seconds': seconds, 'at_least_60_seconds': seconds >= 60},
        'presentation_calls': calls,
        'presentation_calls_per_nominal_second': calls / seconds,
        'nominal_rate_at_least_59': calls >= 59 * seconds,
        # The CSV timestamps are whole seconds at frame boundaries, not exact
        # QPC endpoints. Contiguous labels bound the endpoint difference by 1 s.
        'rate_bounds_from_timestamp_quantization': {
            'lower': calls / (seconds + 1),
            'upper': calls / (seconds - 1) if seconds > 1 else None,
        },
        'per_row_calls': {'min': min(row['present_calls'] for row in parsed),
                          'max': max(row['present_calls'] for row in parsed)},
        'regular_frame_updates': totals['frames'],
        'extra_frames': extras,
        'extra_draws_per_regular_update': extras / totals['frames'] if totals['frames'] else None,
        'pacing': {
            'gaps': gaps, 'histogram_counts': dict(zip(BIN_COLUMNS, bins)),
            'p50': quantile(bins, 50), 'p95': quantile(bins, 95),
            'fraction_over_25_ms': sum(bins[5:]) / gaps if gaps else None,
            'fraction_over_50_ms': sum(bins[7:]) / gaps if gaps else None,
        },
        'shadows': {
            'extra_draw_calls': totals['extra_shadows'],
            'extra_completed_pass_samples': totals['extra_shadow_samples'],
            'valid_extra_profiles': totals['extra_profile_valid'],
            'invalid_extra_profiles': totals['extra_profile_invalid'],
            'regular_shadow_skips': totals['shadow_skipped'],
            'extra_counts_match_each_row': shadow_counts_match,
        },
        'verification_failures': {key: totals[key] for key in VERIFY_COLUMNS},
        'bulk_coverage': bulk_coverage(selected, totals),
        'frame_costs': frame_costs(selected),
        'limits': [
            'Completed flip-enabled EndScene calls; not measured display refresh or GPU success.',
            'Row timestamps quantize duration; reported FPS uses the nominal interval.',
            'Percentiles are pooled histogram bounds, not exact gaps or averaged row percentiles.',
            'The first gap may begin before the selected interval.',
            'Matching shadow counts do not prove correct pixels or per-frame correspondence.',
            'Confirm the saved-game scene, loaded version/settings, visual output and runtime log.',
            'Simulation/network/input/camera invariants require separate evidence.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--start-s', type=int, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    try:
        source_hash = hashlib.sha256(args.csv.read_bytes()).hexdigest()
        report = analyze(load_rows(args.csv), args.start_s, args.seconds)
        if hashlib.sha256(args.csv.read_bytes()).hexdigest() != source_hash:
            raise ValueError('CSV changed during analysis; use a stable copied capture')
        report['source'] = {'path': str(args.csv.resolve()), 'sha256': source_hash}
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(2, f'capture not usable: {error}\n')
    print(json.dumps(report, indent=2, allow_nan=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
