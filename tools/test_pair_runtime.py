"""Capture the production paired-cadence adapter test; never launch the game."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "perf/build/test_pair_runtime.exe")
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--suite-log", type=Path)
    parser.add_argument("--run-log", type=Path, help="Parse an already completed fixture/suite run")
    parser.add_argument("--sequential-before", type=Path,
                        help="Same cost-variation fixture linked to the previous runtime adapter")
    parser.add_argument("--sequential-before-commit", help="Commit providing the comparison fps.c")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.run_log.read_text(errors="replace") if args.run_log else subprocess.run(
        [str(args.executable.resolve())], capture_output=True, text=True, check=True).stdout
    assert "paired runtime adapter: PASS (0 failures)" in output
    assert "FAIL tests/" not in output and "mingw32-make: ***" not in output
    pattern = (r"adapter paired=(\d) regular=(\d+) jitter=(\d): (\d+) ordinary, (\d+) extra, "
               r"(\d+) us, (\d+) shortened waits, (\d+) late, stop=([^\n]+)")
    rows = []
    for match in re.finditer(pattern, output):
        values = list(map(int, match.groups()[:8]))
        row = dict(zip(["paired", "regular_cost_us", "jitter", "ordinary_updates", "extra_draws",
                        "virtual_elapsed_us", "shortened_waits", "late_predictions"], values))
        assert row["ordinary_updates"] == 1800
        assert 59390000 <= row["virtual_elapsed_us"] <= 59410000
        assert row["extra_draws"] == (1800 if row["paired"] else 0)
        assert row["late_predictions"] == 0 and match.group(9) == "none"
        rows.append(row)
    assert len(rows) == 3
    assert "3 probes, 3 failed, resting=three paired recovery probes" in output
    recovery = re.search(r"adapter recovery: (\d+) extra draws after the spike, 1 late, (\d+) us", output)
    assert recovery and int(recovery.group(1)) > 100
    intermittent = re.search(r"intermittent recovery: (\d+) extras, 1 probe, 0 late, (\d+) us", output)
    assert intermittent and int(intermittent.group(1)) > 1700
    assert 59390000 <= int(intermittent.group(2)) <= 59410000
    assert "recovery budget gaps: no unaffordable draws, later recovery, bounded failures" in output
    adaptive = []
    pattern = (r"adaptive max=(\d+) limit=(\d+) regular=(\d+) cost=(\d+): (\d+) ordinary, "
               r"(\d+) extra, (\d+) us, max gap (\d+) us, (\d+) late")
    for match in re.finditer(pattern, output):
        row = dict(zip(["max_fps", "pair_max_extras", "regular_cost_us", "extra_cost_us",
                        "ordinary_updates", "extra_draws", "virtual_elapsed_us", "gap_max_us",
                        "late_predictions"], map(int, match.groups())))
        assert row["ordinary_updates"] == 1800 and row["late_predictions"] == 0
        assert 59390000 <= row["virtual_elapsed_us"] <= 59410000
        row["modeled_presentations_per_second"] = 1e6 * (
            row["ordinary_updates"] + row["extra_draws"]) / row["virtual_elapsed_us"]
        adaptive.append(row)
    assert len(adaptive) == 6
    assert [r["extra_draws"] for r in adaptive] == [5400, 1800, 3600, 1800, 3600, 1800]
    assert adaptive[0]["gap_max_us"] < 10000
    period_case = re.search(r"adaptive changing-period updates: 6000 ordinary, (\d+) extra, (\d+) us, 0 late", output)
    assert period_case and int(period_case.group(1)) >= 12000
    assert "adaptive second-draw spike/recovery/cost/state guards: checked" in output
    def sequential_rows(log):
        pattern = (r"sequential variable cost case=(\d) phase=(\d): (\d+) extras, (\d+) late, "
                   r"(\d+) us, max gap (\d+) us")
        found = [dict(zip(["case", "fractional_phase", "extra_draws", "late_predictions",
                          "virtual_elapsed_us", "gap_max_us"], map(int, m.groups())))
                 for m in re.finditer(pattern, log)]
        assert [(r["case"], r["fractional_phase"]) for r in found] == [
            (c, p) for c in range(3) for p in range(4)]
        for row in found:
            assert row["late_predictions"] == 0 and 59390000 <= row["virtual_elapsed_us"] <= 59410000
            assert row["gap_max_us"] < 33000
            row["ordinary_updates"] = 1800
            row["modeled_presentations_per_second"] = 1e6 * (1800 + row["extra_draws"]) / row["virtual_elapsed_us"]
        return found
    sequential = sequential_rows(output)
    assert all(r["extra_draws"] > [3300, 4800, 1700][r["case"]] for r in sequential)
    sequential_before = None
    if args.sequential_before:
        assert args.sequential_before_commit
        comparison = args.sequential_before.parent / "fps-before-sequential.c"
        prior_source = subprocess.run(
            ["git", "show", f"{args.sequential_before_commit}:perf/src/fps.c"],
            cwd=ROOT, capture_output=True, check=True).stdout
        assert comparison.read_bytes() == prior_source
        before = args.sequential_before.read_text(errors="replace")
        assert "paired runtime adapter: FAIL (8 failures)" in before
        failures = [line for line in before.splitlines() if "FAIL tests/" in line]
        assert len(failures) == 8 and all(
            line.endswith("recent cost maxima blocked sequential opportunities") for line in failures)
        sequential_before = {"path": str(args.sequential_before),
                             "sha256": sha(args.sequential_before), "cases": sequential_rows(before),
                             "adapter_source_commit": args.sequential_before_commit,
                             "adapter_source_sha256": sha(comparison),
                             "fixture_sha256": sha(args.sequential_before.parent / "test-pair-before-sequential.exe"),
                             "expected_failures": 8}
    objects = {}
    for name in ["fps", "pairpace", "catchup", "config", "main", "stubs"]:
        path = ROOT / f"perf/build/{name}.o"
        dis = subprocess.run([args.objdump, "-d", "--no-show-raw-insn", str(path)],
                             capture_output=True, text=True, check=True).stdout
        instructions = re.findall(r"^[ \t]*[0-9a-f]+:[ \t]+([a-z][a-z0-9]*)\b([^\n]*)",
                                  dis, re.MULTILINE)
        assert instructions
        bad = [(op, operands) for op, operands in instructions
               if op.startswith("f") or op in {"ldmxcsr", "stmxcsr", "xrstor", "xrstors"} or "xmm" in operands]
        assert not bad, (name, bad)
        objects[name] = {"sha256": sha(path), "instructions_checked": len(instructions),
                         "forbidden_fp_instructions": bad}
    paths = ["perf/src/fps.c", "perf/src/fps.h", "perf/src/pairpace.c", "perf/src/pairpace.h",
             "perf/src/catchup.c", "perf/src/catchup.h", "perf/src/config.c", "perf/src/config.h",
             "perf/src/main.c", "perf/src/version.rc", "perf/tests/test_pair_runtime.c",
             "perf/tests/test_pair_sequential.c", "perf/tests/test_load.c",
             "perf/tests/test_fps.c", "perf/tests/test_config.c", "perf/bfme2perf.ini",
             "tools/test_pair_runtime.py", "perf/Makefile"]
    suite = None
    if args.suite_log:
        log = args.suite_log.read_text(errors="replace")
        for marker in ["OK: config tests passed", "paired runtime adapter: PASS (0 failures)",
                       "OK: catch-up tests passed", "OK: light tests passed",
                       "sequential policy: 126745 checks, 0 failures"]:
            assert marker in log, marker
        assert "FAIL tests/" not in log and "mingw32-make: ***" not in log
        suite = {"path": str(args.suite_log), "sha256": sha(args.suite_log),
                 "required_markers_present": True, "failure_markers": False}
    result = {
        "version": re.search(r'#define PERF_VERSION "([^"]+)"',
                             (ROOT / 'perf/src/main.c').read_text()).group(1),
        "kind": "production adapter correctness test with controlled virtual timing",
        "gameplay_target_verified": False,
        "plugin_sha256": sha(ROOT / "perf/build/bfme2perf.asi"),
        "fixture_sha256": sha(args.executable),
        "sources": {p: sha(ROOT / p) for p in paths},
        "integer_object_audit": objects,
        "full_suite_log": suite,
        "closed_loop_cases": rows,
        "intermittent_recovery": {"ordinary_updates": 1800,
                                  "extra_draws": int(intermittent.group(1)),
                                  "probes": 1, "late_predictions": 0,
                                  "virtual_elapsed_us": int(intermittent.group(2))},
        "recovery_budget_gap_cases_passed": True,
        "adaptive_cases": adaptive,
        "sequential_variable_cost_cases": sequential,
        "sequential_previous_adapter": sequential_before,
        "changing_period_case": {"ordinary_updates": 6000,
                                 "extra_draws": int(period_case.group(1)),
                                 "virtual_elapsed_us": int(period_case.group(2))},
        "raw_output": '\n'.join(line for line in output.splitlines()
                                if line.startswith(('adaptive ', 'adapter ', 'intermittent ', 'recovery ',
                                                    'sequential ', 'paired runtime adapter:'))),
        "coverage": [
            "Production fps/catch-up/pairpace object files, not alternate implementations",
            "One pacing owner, real saved wait-threshold adapter and exact ordinary update count",
            "Held view/particle updates, unchanged camera, fresh shadow calls, preserved reflection option",
            "Pause, shell, minimization, screenshots, placement, scrolling and frames without a wait",
            "Native period change and logic-random, engine-counter, client-frame and camera failure guards",
            "One transient recovery with a more expensive ordinary reference draw",
            "Persistent expensive draws: at most five draws, three failed bounded probes, then rest",
            "Adaptive one/two/three draws, MaxFPS ceiling, shared deadline, repeated observations and clock wrap",
            "Second-draw cost/state failures, UI eligibility lost after first draw and later recovery",
            "6000 changing native periods with preserved ordinary cadence",
            "Sequential variable-cost workloads at four fractional phases, each native deadline checked",
            "Pure policy: 1000 fractional phases, native-clock wrap, delayed wakes and one-extra equivalence",
        ],
        "limits": [
            "No game, game entry point, native update, GPU scene, Present or multiplayer peer executes.",
            "Draw costs and the timeline are controlled fixture inputs; these rates are not measured gameplay FPS.",
            "The hook adapter is called through its C interface; unchanged assembly/site behavior is checked by the full existing hook/site suite.",
            "Shadow invocation is checked here; pixel correctness relies on the unchanged rendering path and prior GPU qualification.",
            "Matching-beta multiplayer, real game speed/input behavior and heavy-scene 120 FPS still require gameplay capture.",
            "120 is a controlled timing result. Expensive inputs still fall back to one extra; actual heavy-scene 120 FPS is not achieved.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Production paired adapter passed; six integer objects audited; gameplay FPS unverified.")


if __name__ == "__main__":
    main()
