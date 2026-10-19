"""Run the paired-cadence policy model and preserve explicitly modeled results."""
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
    parser.add_argument("--executable", type=Path, default=ROOT / "perf/build/test_pairpace.exe")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--objdump", default="objdump")
    parser.add_argument("--object", type=Path, default=ROOT / "perf/build/pairpace.o")
    args = parser.parse_args()
    run = subprocess.run([str(args.executable.resolve())], capture_output=True, text=True, check=True)
    lines = [json.loads(line) for line in run.stdout.splitlines() if line.strip()]
    summary = lines.pop()
    assert summary["summary"] and summary["failures"] == 0
    assert len(lines) == 22
    indexed = {(r["case"], r["mode"]): r for r in lines}
    for name in ["aggregate_anchor_10819", "aggregate_anchor_12849", "anchor_with_2ms_jitter"]:
        old = indexed[name, "legacy_admission_model"]
        new = indexed[name, "paired_cadence_model"]
        assert old["regular"] == new["regular"] == 1818
        assert old["extras"] == 0 and new["extras"] == 1818
    heavy = indexed["heavier_aggregate_anchor", "paired_cadence_model"]
    assert heavy["regular"] == 1818 and heavy["extras"] == 909
    assert indexed["overload", "paired_cadence_model"]["extras"] == 0
    good = indexed["repayment_control", "paired_cadence_model"]
    no_repay = indexed["repayment_control", "fault_no_repayment"]
    reanchored = indexed["repayment_control", "fault_reanchor_every_frame"]
    assert no_repay["extras"] < good["extras"] / 100
    assert reanchored["regular"] < good["regular"] - 20
    for row in lines:
        row["modeled_presentations_per_second"] = (row["regular"] + row["extras"]) / 60
    paths = ["perf/src/pairpace.c", "perf/src/pairpace.h", "perf/src/fps.c",
             "perf/tests/test_pairpace.c", "tools/test_pairpace.py"]
    installed = Path("C:/Jogos/AgeoftheRing/rotwk/mss")
    disassembly = subprocess.run([args.objdump, "-d", str(args.object.resolve())],
                                 capture_output=True, text=True, check=True).stdout
    instructions = re.findall(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+([a-z][a-z0-9]*)\b([^\n]*)",
                              disassembly, re.MULTILINE)
    assert instructions
    def forbidden(instruction):
        op, operands = instruction
        return op.startswith("f") or op in {"ldmxcsr", "stmxcsr", "xrstor", "xrstors"} or "xmm" in operands
    assert forbidden(("fldcw", "(%eax)")) and forbidden(("movss", "%xmm0,%xmm1"))
    assert not forbidden(("mov", "%eax,%ebx"))
    bad_instructions = [i for i in instructions if forbidden(i)]
    assert not bad_instructions, bad_instructions
    result = {
        "kind": "closed-loop scheduling policy model, not a performance benchmark",
        "target_verified": False,
        "runtime_integrated": False,
        "checks": summary,
        "sources": {p: sha(ROOT / p) for p in paths},
        "executable_sha256": sha(args.executable),
        "integer_object_audit": {"sha256": sha(args.object), "instructions_checked": len(instructions),
                                 "forbidden_fp_instructions": bad_instructions, "matcher_controls_passed": True},
        "installed_hashes": {n: sha(installed / n) for n in ["bfme2perf.asi", "bfme2perf.ini"]
                             if (installed / n).exists()},
        "settings": {"predicted_borrow_us": 4000, "reserve_us": 1250,
                     "native_clock_quantum_us": 1000},
        "limits": [
            "No game, GPU, Present or multiplayer peer executes. All frame costs are controlled inputs.",
            "Capture anchors combine aggregates and retained estimates from different samples; they are not a trace replay or FPS forecast.",
            "The real legacy planner executes, but its adapter, backoff and learned-cost history are not a full legacy runtime replay.",
            "The extra-cost estimate stays fixed, including after injected spikes. Runtime estimator and backoff integration remain to test.",
            "The modeled regular cost excludes any unaccounted outer-loop, timer or hook overhead.",
            "A predicted 4 ms borrow is not a hard bound on unexpectedly slow GPU/OS completion; actual spike debt reaches 9 ms here.",
            "Native 33 ms quantization means 30.303 nominal regular updates/s. The controller follows that period rather than imposing exact 30 Hz.",
            "Fresh-shadow output, camera/state guards, runtime sites, pause/loading gates and matching-beta synchronization need integration qualification.",
        ],
        "development_findings": [
            "A 2 ms allowance retained only every other extra frame with the 2 ms jitter workload; the tested candidate uses 4 ms.",
            "Using only 250 us reserve ignored native-clock fractional phase; use 1000 us quantization reserve plus 250 us overhead.",
            "The first no-repayment fault expected immediate logic slowdown, but retaining unpaid debt instead starves extra draws. Both that fault and reanchoring every frame now have distinct oracles.",
            "Carry small debt across native-period changes; resetting it on each network adjustment could repeatedly forgive overruns.",
        ],
        "rows": lines,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Paired-cadence policy: {summary['checks']} checks passed; 22 rows; gameplay unverified.")


if __name__ == "__main__":
    main()
