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
