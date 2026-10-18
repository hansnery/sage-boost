# Native render-task sort screen

Removing temporary reference-count traffic from the native render-task
sort saves **0.51-2.04 ms** for the one-method synthetic workload, depending
on input order. Random keys among 128 values save **1.44 / 1.41 ms** in two
serial runs. This is a promising separate CPU target, not a measured game
frame, an installed optimization or evidence of 60 FPS.

The earlier group/GPU candidate still saves 2.68-2.93 ms per modeled
regular/extra pair. These screens have not been combined. Do not add their
numbers, double the sort result or convert either result into gameplay FPS.
The last gameplay capture remains about 30.18 presentations/s.

## Boundary and candidate

The native FX range flusher sorts 32-byte records before rendering and
erasing them. Records own one mesh and six nullable method holders.
Out-of-line copy, assignment and destruction repeatedly update references
during sorting. The verified comparators only read the mesh ordering word
and, for the second comparator, a secondary state address.

The offline launcher verifies 38 complete native bodies, totaling 2,513
bytes, against the read-only reference. It finds the root and comparators
through unique wildcard matches, follows direct dependencies, checks full
normalized instructions, internal branch boundaries and dependency roles,
then passes only numeric relocation records and hashes to the harness.

The harness reads those bodies into two private memory regions. Their
relative positions match the local target image. Only verified bodies are
populated, relocated and made executable; no executable entry point or game
initialization runs, and no EA bytes are saved to disk. Initial exploratory
timings with one page per function are excluded because that layout could
distort instruction-cache and TLB costs.

The reference executes the native sort, owning-record operations and CRT
helpers. The candidate executes the same native sorting instructions and
comparators, but substitutes independently written eight-word copies for
record copy/assignment and a no-op for temporary destruction. Thus the
screen preserves the native tie permutation rather than choosing another
sorting algorithm. The candidate is never linked into the plugin.

## Correctness and limits

Both local executable images pass **936 original/candidate cases** each:

- 900 single-range cases span 0-3,072 records, threshold boundaries, six
  key distributions, zero/one/six method holders, both comparators and
  ordinary introsort or its forced depth-zero heap path.
- 36 complete workloads sort consecutive ranges of 3,072, 1,536 and 3,072
  records. Output is checked after every range, including untouched tails.
- Every comparison requires byte-identical records and tie order, sorted
  output, unchanged mesh fields and final reference counts, intact guards,
  preserved callee-saved registers/stack and restored FS exception chain.
- Four altered-instruction contracts and a deliberately corrupt record-copy
  candidate are rejected per image.

The first heap-path test incorrectly assumed that the introsort loop alone
sorts short ranges. Native code leaves ranges of at most 16 records for the
root's final insertion phase. The harness now calls that phase after the
depth-zero loop; no sortedness check or output difference was waived.

Exception/termination dependencies deliberately fail-stop. This establishes
normal control flow only, with fixture-owned objects and verified pure
comparators. It does not qualify exception unwinding, real queue lifetimes,
other users of these owning-record operations or an installed hook. No
production function is globally replaced. A runtime candidate needs a
verified, bounded sort context and must retain ordinary behavior elsewhere.

## Measurements

Each sample averages eight complete three-range workloads, totaling 7,680
records per repetition. Both modes pay for the same input reset. Four
warmups precede 21 alternating-order sample pairs per distribution/method
count. Two runs execute serially with the game/WorldBuilder process guard.
The method-count-one medians below are milliseconds per repetition; saved
time is the median of paired differences, not a difference of medians.

| Input order | Native, run 1 / 2 | Candidate, run 1 / 2 | Saving, run 1 / 2 |
|---|---:|---:|---:|
| Random among 128 keys | 2.133 / 2.107 | 0.695 / 0.696 | 1.437 / 1.411 |
| Ascending with ties | 0.754 / 0.736 | 0.227 / 0.226 | 0.527 / 0.511 |
| Descending with ties | 1.013 / 0.991 | 0.274 / 0.274 | 0.739 / 0.718 |
| All keys equal | 2.586 / 2.576 | 0.547 / 0.548 | 2.038 / 2.028 |
| Organ-pipe pattern | 1.197 / 1.161 | 0.353 / 0.355 | 0.844 / 0.806 |
| Random 32-bit keys | 2.250 / 2.246 | 0.768 / 0.795 | 1.479 / 1.451 |

These counts approximate the captured total mesh workload, but its actual
queue split, key order, reference topology and memory locality are unknown.
Zero and six methods provide sensitivity controls; they are not evidence
of the game's method mix. Real queue collection, rendering, erasure and
runtime guards are outside the timed region. This is CPU elapsed time,
without a GPU or presentation.

Preserve [756 timing pairs](../benchmarks/2026-10-18-native-sort-screen.csv)
and [36 summaries, limits and hashes](../benchmarks/2026-10-18-native-sort-runs.json).
Production, installed ASI, INI and stable branches are unchanged.

Next join sorting to native queue/group rendering and the guarded GPU
candidate, preserving exact command/output order, ownership and fallback.
Measure their combined cost before proposing runtime integration. There
is no new gameplay test request from this isolated result.

```sh
# From perf/ with the documented i686 toolchain
mingw32-make test-native-sort
mingw32-make bench-native-sort
```
