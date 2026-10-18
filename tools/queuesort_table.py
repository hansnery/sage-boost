"""Fingerprint native rendering-task sort semantics and both pure comparators."""
import bench_native_sort
from native_contract_table import run as generate


def run(write):
    return generate(write, bench_native_sort, 'queuesort', 'QS', '--perf-sort',
                    [(30, 48), (8, 29), (9, 47)], {'ENTRY': 30, 'COMPARE_MESH': 8, 'COMPARE_STATE': 9})
