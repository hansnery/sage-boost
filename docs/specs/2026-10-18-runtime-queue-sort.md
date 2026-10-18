# Runtime rendering-queue sorting

Beta **0.9.11-beta.5** adds `[Render] QueueSort=1` alongside the existing
queue insertion optimization. The plugin sorts qualified rendering records
without temporary reference-count traffic, preserving the native ordering
of equal items. Unknown comparators and unsupported ranges retain the
original function. Both switches can be disabled independently.

The complete plugin suite passes. Runtime insertion checks remain unchanged;
sorting passes native comparisons, private-image hooks in both installation
orders, shared-reference cases and no-write refusal controls. Both images
pass lit/fresh-shadow GPU checks on native D3D9 and DXVK, including all 24
corrupted-record controls. This build is not installed: bulk rendering and
its remaining failure/lifetime boundaries still require runtime integration.

## Algorithm and ownership

The new C implementation is integer-only and allocates no memory. It
preserves the verified median pivot selection, partition traversal, right
recursion followed by the left loop, twice-logarithmic depth limit,
16-record insertion threshold and heap fallback. These details preserve
the native tie permutation. It moves all eight words of each 32-byte record.
No native sorting body is cloned for runtime use or included in the plugin.

Only the two verified comparators qualify. Both read the mesh ordering word
at offset C4; one compares the unsigned state-pointer word at offset 310
when the first words match. The C sorter implements those same comparisons
directly. No callback runs during a qualified sort and no mesh/method object
is mutated. The queue's ownership references remain held throughout every
move and temporary copy, then the original erase/destruction releases them.

Preflight checks range order/alignment/length, record method count, non-null
meshes and live holder reference counts with ample overflow headroom.
Unknown comparators or rejected inputs delegate before any record changes.
Empty/single-record ranges preserve the native no-dereference behavior.
The fast path assumes valid owned records, as the native container does;
this is not a general invalid-pointer recovery mechanism. Native callbacks,
ownership changes and exception handling remain in the original fallback.

## Executable contract and entry hook

Insertion's verifier is now shared in `nativecontract.c`. Sorting supplies
three unique signature roots and normalized FNV/CRC fingerprints for 38
complete bodies (2,513 bytes), 93 encoded dependencies and ten external
exception identities. Code/data roles, bounds, repeated identities and
nonoverlap are checked. External exception bodies receive identity/section
checks rather than full body fingerprints. No fixed target addresses are
used. Generate through `python tools/verify_sites.py --perf-sort --write`;
insertion retains `--perf-queue` and unlocker table generation is unchanged.

The verified entry starts with position-independent
`push esi; mov esi,[esp+8]`. The existing patcher replaces those five bytes
and preserves them in the native fallback trampoline. The master switch
gates installation; each feature verifies its executable contract before
patching. No graphics quality, camera, logic, input, network, frame counter,
x87 control-word or MXCSR change is introduced.

## Verification

Both local images pass the existing 936 comparisons against the production
sort: ordinary sorting, forced depth-zero heap execution, threshold edges,
six key distributions, zero/one/six methods, both comparators and split
queues. Exact records/ties, references, untouched objects/tails, guards,
stack, callee-saved registers and the FS exception chain must match.

Private mapped-image tests then exercise insertion/sort installation in
both orders. For each image and order there are **528 installed-entry
cases and 456 forced-heap cases**. Forced-heap cases call the shared
production algorithm directly so its depth can be set to zero; they do
not pass through the entry hook. Those counts include 48 additional cases
with repeated mesh pointers and duplicated/null method holders. Both
features also execute together through an insertion/native-erase control.

Unknown-comparator controls compare the callback sequence, temporary mesh
and method reference counts, and final records against the original sort.
Ten preflight-refusal cases per image require unchanged inputs and exact
arguments at the fallback; a valid case must avoid that fallback. Each
image also passes 105 runtime contract refusals without changing the image:
five body mutations, all 93 dependency fields, missing/ambiguous copies of
all three roots and a truncated image bound. Insertion still passes its
576 direct comparisons and 576 private-entry cases plus 92 refusals/image.

The full `make test` suite passes with beta.5. The additional shared-holder
cases pass in `test-queuesort`; a final build/load and insertion rerun pass
after the startup-log clarification and negative-control adjustment below.
Disassembly of sorting and the shared verifier contains no x87/SSE
instructions; both use the production general-register flags.

The joined GPU fixture invokes both production handlers. Native group/mesh/FX
rendering and the remaining offline bulk candidate still supply its rendering
portion. Eight image/backend/workload cases preserve exact records, ordered
groups, pixels/depth, object/engine/effect/device state and references.
All 24 bad-record controls fail the required exact-record assertion.

The earlier inserted-record control changed a null method pointer to address
1. The new reference preflight dereferenced it before the record oracle,
producing an access violation. The control now changes the method count to
a different valid count. The test still requires the specific record
comparison to reject it; accepting any crash/nonzero exit was not substituted.

## Combined measurements

Six serial DXVK runs use four warmups and 21 alternating-order samples of
eight fenced regular/extra pairs, with advancing material time, 7,200 owned
records and 288 groups. The normal measured path includes the production
sort's reference preflight. Game/WorldBuilder guards pass throughout.

| Completion saving per regular/extra pair | Run 1 | Run 2 |
|---|---:|---:|
| Shuffled, lit | 7.849 ms | 7.388 ms |
| Shuffled, fresh shadows | 7.590 ms | 7.959 ms |
| Ordered, lit | 6.222 ms | — |
| Ordered, fresh shadows | 6.170 ms | — |

Shuffled p10-p90 saving ranges span 6.95-8.73 ms. These results retain the
earlier combined benefit and replace that screen; they are not additive
savings. Actual saved-game queue order, busy extra-frame cost and presentation
pacing remain unmeasured. Last gameplay remains about 30.18 presentations/s.
The roughly 8 ms engineering budget is not proof of sustained 60 FPS.

Preserve [126 timing samples](../benchmarks/2026-10-18-runtime-queue-sort-timing.csv)
and [control summaries and hashes](../benchmarks/2026-10-18-runtime-queue-sort-runs.json).
Built beta.5 ASI SHA256:
`a925172cf19dd7fa950d79d2a969bbb5aedf936572b76a671ebbc1f823cfe137`.
Installed ASI and INI hashes remain unchanged.

## Next integration

Move the guarded bulk renderer into production, preserving native
group/method boundaries, callback/verification exclusions and staged
ownership. Resolve remaining source/resource lifetimes and persistent,
device-loss and ordinary-replay failure behavior. Then install the complete
tested improvement before asking for the user's saved-game capture. No
gameplay test is requested for this intermediate build.

```sh
# From perf/ using the documented i686 toolchain:
mingw32-make test-native-sort test-native-insert test-queuesort
mingw32-make test
mingw32-make test-native-insert-gpu
mingw32-make test-native-insert-gpu AOTR_DAT=C:/Jogos/AgeoftheRing/rotwk/game820.dat
mingw32-make bench-native-insert-gpu
```
