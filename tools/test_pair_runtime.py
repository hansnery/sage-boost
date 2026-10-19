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
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run = subprocess.run([str(args.executable.resolve())], capture_output=True, text=True, check=True)
    assert "paired runtime adapter: PASS (0 failures)" in run.stdout
    assert "FAIL" not in run.stdout
    pattern = (r"adapter paired=(\d) regular=(\d+) jitter=(\d): (\d+) ordinary, (\d+) extra, "
               r"(\d+) us, (\d+) shortened waits, (\d+) late, stop=([^\n]+)")
    rows = []
    for match in re.finditer(pattern, run.stdout):
        values = list(map(int, match.groups()[:8]))
        row = dict(zip(["paired", "regular_cost_us", "jitter", "ordinary_updates", "extra_draws",
                        "virtual_elapsed_us", "shortened_waits", "late_predictions"], values))
        assert row["ordinary_updates"] == 1800
        assert 59390000 <= row["virtual_elapsed_us"] <= 59410000
        assert row["extra_draws"] == (1800 if row["paired"] else 0)
        assert row["late_predictions"] == 0 and match.group(9) == "none"
        rows.append(row)
    assert len(rows) == 3
    assert "3 probes, 3 failed, resting=three paired recovery probes" in run.stdout
    recovery = re.search(r"adapter recovery: (\d+) extra draws after the spike, 1 late, (\d+) us", run.stdout)
    assert recovery and int(recovery.group(1)) > 100
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
             "perf/tests/test_fps.c", "tools/test_pair_runtime.py"]
    suite = None
    if args.suite_log:
        log = args.suite_log.read_text(errors="replace")
        for marker in ["OK: config tests passed", "paired runtime adapter: PASS (0 failures)",
                       "OK: catch-up tests passed", "OK: light tests passed"]:
            assert marker in log, marker
        assert "FAIL tests/" not in log and "mingw32-make: ***" not in log
        suite = {"path": str(args.suite_log), "sha256": sha(args.suite_log),
                 "required_markers_present": True, "failure_markers": False}
    result = {
        "version": "0.9.11-beta.14",
        "kind": "production adapter correctness test with controlled virtual timing",
        "gameplay_target_verified": False,
        "plugin_sha256": sha(ROOT / "perf/build/bfme2perf.asi"),
        "fixture_sha256": sha(args.executable),
        "sources": {p: sha(ROOT / p) for p in paths},
        "integer_object_audit": objects,
        "full_suite_log": suite,
        "closed_loop_cases": rows,
        "raw_output": run.stdout,
        "coverage": [
            "Production fps/catch-up/pairpace object files, not alternate implementations",
            "One pacing owner, real saved wait-threshold adapter and exact ordinary update count",
            "Held view/particle updates, unchanged camera, fresh shadow calls, preserved reflection option",
            "Pause, shell, minimization, screenshots, placement, scrolling and frames without a wait",
            "Native period change and logic-random, engine-counter, client-frame and camera failure guards",
            "One transient recovery with a more expensive ordinary reference draw",
            "Persistent expensive draws: at most five draws, three failed bounded probes, then rest",
        ],
        "limits": [
            "No game, game entry point, native update, GPU scene, Present or multiplayer peer executes.",
            "Draw costs and the timeline are controlled fixture inputs; these rates are not measured gameplay FPS.",
            "The hook adapter is called through its C interface; unchanged assembly/site behavior is checked by the full existing hook/site suite.",
            "Shadow invocation is checked here; pixel correctness relies on the unchanged rendering path and prior GPU qualification.",
            "Matching-beta multiplayer, real game speed/input behavior and the settled heavy-scene 59/s target still require gameplay capture.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Production paired adapter passed; six integer objects audited; gameplay FPS unverified.")


if __name__ == "__main__":
    main()
