"""Counterexamples for the read-only gameplay capture report."""
import copy
import csv
import tempfile
import unittest
from pathlib import Path

from perf_capture import BIN_COLUMNS, BULK_COLUMNS, COUNT_COLUMNS, analyze, load_rows


def capture(seconds=60):
    rows = []
    for second in range(100, 101 + seconds):
        row = dict.fromkeys(COUNT_COLUMNS + BIN_COLUMNS, '0')
        row.update(time_s=str(second), frames='30', present_calls='60',
                   present_gaps='60', extra_frames='30', extra_shadows='30',
                   extra_shadow_samples='30', extra_profile_valid='30')
        row[BIN_COLUMNS[2]] = '60'
        rows.append(row)
    return rows


class CaptureTests(unittest.TestCase):
    def test_regular_and_extra_calls_count_once(self):
        result = analyze(capture(), 100, 60)
        self.assertEqual(result['presentation_calls'], 3600)
        self.assertEqual(result['presentation_calls_per_nominal_second'], 60)
        self.assertTrue(result['window']['at_least_60_seconds'])
        self.assertEqual(result['pacing']['p95']['upper_ms'], 16.667)
        self.assertTrue(result['shadows']['extra_counts_match_each_row'])

    def test_bursty_sixty_is_not_uniform_pacing(self):
        rows = capture()
        for row in rows:
            row[BIN_COLUMNS[2]] = '0'
            row[BIN_COLUMNS[0]] = row[BIN_COLUMNS[5]] = '30'
        result = analyze(rows, 100, 60)
        self.assertEqual(result['presentation_calls_per_nominal_second'], 60)
        self.assertEqual(result['pacing']['p50']['upper_ms'], 8.333)
        self.assertEqual(result['pacing']['p95']['upper_ms'], 33.334)
        self.assertEqual(result['pacing']['fraction_over_25_ms'], 0.5)

    def test_aggregate_histogram_not_average_of_row_percentiles(self):
        rows = capture()
        rows[0].update(present_calls='6000', present_gaps='6000')
        rows[0][BIN_COLUMNS[2]] = '0'
        rows[0][BIN_COLUMNS[6]] = '6000'
        result = analyze(rows, 100, 60)
        self.assertEqual(result['pacing']['p50']['upper_ms'], 50)

    def test_overflow_percentile_is_not_zero(self):
        rows = capture()
        for row in rows:
            row[BIN_COLUMNS[2]], row[BIN_COLUMNS[-1]] = '0', '60'
        result = analyze(rows, 100, 60)
        self.assertEqual(result['pacing']['p95']['lower_exclusive_ms'], 1000)
        self.assertIsNone(result['pacing']['p95']['upper_ms'])

    def test_no_presentations_has_no_percentile(self):
        rows = capture()
        for row in rows:
            row['present_calls'] = row['present_gaps'] = row[BIN_COLUMNS[2]] = '0'
        result = analyze(rows, 100, 60)
        self.assertIsNone(result['pacing']['p50'])
        self.assertIsNone(result['pacing']['fraction_over_25_ms'])

    def test_mismatched_histogram_rejected(self):
        rows = capture()
        rows[3][BIN_COLUMNS[0]] = '1'
        with self.assertRaisesRegex(ValueError, 'histogram'):
            analyze(rows, 100, 60)

    def test_repeated_gaps_reset_rejected(self):
        rows = capture()
        for row in rows:
            row['present_gaps'] = row[BIN_COLUMNS[2]] = '59'
        with self.assertRaisesRegex(ValueError, 'missing initial'):
            analyze(rows, 100, 60)

    def test_missing_second_or_end_boundary_rejected(self):
        for removed in (20, -1):
            rows = capture()
            del rows[removed]
            with self.assertRaisesRegex(ValueError, 'boundary|consecutive'):
                analyze(rows, 100, 60)

    def test_duplicate_or_reversed_times_rejected(self):
        for replacement in ('109', '108'):
            rows = capture()
            rows[10]['time_s'] = replacement
            with self.assertRaisesRegex(ValueError, 'increasing'):
                analyze(rows, 100, 60)

    def test_short_window_is_explicit(self):
        self.assertFalse(analyze(capture(59), 100, 59)['window']['at_least_60_seconds'])

    def test_shadow_misses_cannot_cancel_between_rows(self):
        rows = capture()
        rows[0]['extra_shadows'], rows[1]['extra_shadows'] = '29', '31'
        result = analyze(rows, 100, 60)
        self.assertEqual(result['shadows']['extra_draw_calls'], 1800)
        self.assertFalse(result['shadows']['extra_counts_match_each_row'])

    def test_no_extras_does_not_prove_extra_shadow_coverage(self):
        rows = capture()
        for row in rows:
            for key in ('extra_frames', 'extra_shadows', 'extra_shadow_samples',
                        'extra_profile_valid'):
                row[key] = '0'
        self.assertFalse(analyze(rows, 100, 60)['shadows']['extra_counts_match_each_row'])

    def test_missing_negative_and_fractional_counts_rejected(self):
        for value in ('', '-1', '1.5'):
            rows = capture()
            rows[5]['present_calls'] = value
            with self.assertRaisesRegex(ValueError, 'present_calls'):
                analyze(rows, 100, 60)

    def test_invalid_profile_and_verification_errors_reported(self):
        rows = capture()
        rows[2]['extra_profile_invalid'] = '1'
        rows[2]['verify_bad'] = '1'
        result = analyze(rows, 100, 60)
        self.assertFalse(result['shadows']['extra_counts_match_each_row'])
        self.assertEqual(result['verification_failures']['verify_bad'], 1)

    def test_no_mutation_and_no_terminal_row_in_metrics(self):
        rows = capture()
        rows[-1]['present_calls'] = '99999'
        original = copy.deepcopy(rows)
        self.assertEqual(analyze(rows, 100, 60)['presentation_calls'], 3600)
        self.assertEqual(rows, original)

    def test_bulk_counts_are_raw_and_normalized_by_the_matching_frames(self):
        rows = capture(2)
        for row in rows:
            row.update(dict.fromkeys(BULK_COLUMNS, '0'))
        rows[0].update(frames='10', bulk_followers_1='10', bulk_followers_2='90',
                       extra_frames='5', extra_bulk_followers_6='50')
        rows[1].update(frames='30', bulk_followers_2='300', extra_frames='15',
                       extra_bulk_followers_6='350')
        rows[-1]['bulk_followers_1'] = '99999'  # Boundary row is not included.
        original = copy.deepcopy(rows)
        result = analyze(rows, 100, 2)['bulk_coverage']
        self.assertTrue(result['available'])
        self.assertEqual(result['regular']['consumed_followers'], 400)
        self.assertEqual(result['regular']['consumed_followers_per_frame'], 10)
        self.assertEqual(result['regular']['multi_method_followers'], 390)
        self.assertEqual(result['regular']['multi_method_fraction'], 0.975)
        self.assertEqual(result['extra']['consumed_followers_per_frame'], 20)
        self.assertEqual(result['extra']['consumed_followers_by_method_count']['6'], 400)
        self.assertEqual(rows, original)

    def test_legacy_and_blank_bulk_counters_are_unknown_not_zero(self):
        self.assertFalse(analyze(capture(), 100, 60)['bulk_coverage']['available'])
        rows = capture()
        for row in rows:
            row.update(dict.fromkeys(BULK_COLUMNS, ''))
        for instrumented in (0, 1):
            result = analyze(rows, 100, 60)['bulk_coverage']
            self.assertFalse(result['available'])
            self.assertEqual(result['instrumented_rows'], instrumented)
            self.assertNotIn('regular', result)
            rows[0].update(dict.fromkeys(BULK_COLUMNS, '0'))

    def test_zero_bulk_count_is_distinct_from_no_extra_frames(self):
        rows = capture()
        for row in rows:
            row.update(dict.fromkeys(BULK_COLUMNS, '0'))
            row['extra_frames'] = '0'
        result = analyze(rows, 100, 60)['bulk_coverage']
        self.assertTrue(result['available'])
        self.assertEqual(result['regular']['consumed_followers_per_frame'], 0)
        self.assertIsNone(result['regular']['multi_method_fraction'])
        self.assertIsNone(result['extra']['consumed_followers_per_frame'])

    def test_incomplete_or_invalid_bulk_counts_are_rejected(self):
        for value in (None, '', '-1', '1.5'):
            rows = capture()
            for row in rows:
                row.update(dict.fromkeys(BULK_COLUMNS, '0'))
            if value is None:
                del rows[1]['bulk_followers_3']
            else:
                rows[1]['bulk_followers_3'] = value
            with self.assertRaisesRegex(ValueError, 'bulk'):
                analyze(rows, 100, 60)

    def test_truncated_and_duplicate_header_csv_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'timing.csv'
            path.write_text('time_s,frames\n0\n')
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                load_rows(path)
            path.write_text('time_s,time_s\n0,0\n')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                load_rows(path)
            rows = capture()
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            self.assertEqual(load_rows(path), rows)


if __name__ == '__main__':
    unittest.main()
