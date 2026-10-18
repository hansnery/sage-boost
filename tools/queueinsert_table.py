"""Fingerprint the insertion contract without emitting native function bodies."""
import bench_native_insert
from native_contract_table import run as generate


def run(write):
    return generate(write, bench_native_insert, 'queueinsert', 'QI', '--perf-queue',
                    [(0, 48)], {'ENTRY': 0})
