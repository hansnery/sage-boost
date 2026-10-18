# Material-source demand and parameter-write screen

There is a modest offline lead, **not a new gameplay improvement**. Reading
the current numeric effect value and omitting an identical write saves
0.79-1.05 ms per modeled 850-group workload, while retaining changing bone
palettes. This includes the read/comparison cost, but not native typed
callback integration. It does not justify installation on its own. The last
saved-game capture remains 30.0667 presentation calls/s.

## Actual effect demand

The read-only reference's binder initializes annotation requests before
standard semantics. Its AddBinding uses the enclosing root's technique
usage for nested annotations. The existing leaf-usage audit remains valid:
an unused-looking leaf query is not permission to remove that callback.

`bench_effect --sources-only` queries the user's compiled effects directly
through D3DX27 and D3DX43, without saving shader or parameter payloads. It
records hashes and counts for used annotation roots and standard-semantic
requests. Both native D3D9 and DXVK produce 38 techniques per runtime:
152 observations and 1,476 parameter-request records across the four
runtime/backend combinations. There are **zero exact duplicate** requests
where one parameter has both the same annotation address and a recognized
standard semantic. An independently authored effect deliberately contains
one duplicate and one separate semantic; all four combinations detect it.

The default unit technique has 14 annotation roots and six standard
semantic requests: World, View, ViewInverse, Projection, Time and Ambient.
Its shadow technique has three roots and four semantics: World, View,
Projection and Time. Neither requests WorldView or WorldViewProjection, so
caching those combined matrix calculations is not supported for these
techniques. This does not cover every other material.

These are **requests**, not reconstructed native callback lists or observed
gameplay call counts. Struct/array roots can expand, and namespace-specific
rules can select a dynamic channel. The recorded dynamic annotation is -1
when absent/invalid; it must not be treated as the final channel. In
particular the native skeleton resolver explicitly selects channel 1.

## Upload experiment

`--source-writes` adds a separate D3DX27 cost screen for the default unit
and shadow techniques. Numeric arrays/structs expand into fields, except
each skeletal palette stays one twenty-bone raw upload. Object/texture
fields are excluded. There are 28 writes/1,136 bytes per lit group and
seven writes/828 bytes per shadow group. This is a shape-derived fixture:
generic SetValue and SetRawValue are used, not the game's typed setters or
its matrix/light/fog computations. NumJointsPerVertex is set to one.

Each workload processes 850 groups. Four warmups precede 21 rounds, with
mode order reversed on alternating rounds. Isolated writes average 32
workloads per sample; lifecycle modes average eight. GPU fences bracket
samples, with submission and completion measured separately. No geometry,
draw or Present runs, and the process guard requires the game to be closed.

Five modes distinguish costs:

1. Isolated repeated numeric writes, without a Commit. Persistent dirty
   flags make this insufficient as a prediction of per-group savings.
2. All numeric writes, with a changing bone component, followed by the
   real Begin/BeginPass/CommitChanges/EndPass/End lifecycle with flags 6.
3. Preseeded values with no writes and the same lifecycle. This deliberately
   omits pose changes and is only an artificial cost ceiling for the model.
4. Changing bone palettes plus the same lifecycle, omitting static writes.
   This ideal reuse control omits lookup and invalidation costs.
5. Changing bone palettes plus the same lifecycle, with a read-before-write
   filter for numeric fields up to 64 bytes. It calls GetValue every time,
   compares exact bytes, and skips SetValue only on a successful match.
   Larger/failed reads forward the write. No persistent value cache exists.

Static inputs remain constant within each timed workload. The changing
pose uses the same deterministic sequence on all relevant paths. The filter
therefore measures a favorable repetition pattern, not the saved game's
hit rate. All modes are developer-only, with no runtime hook.

## Controls and results

Round-trip reads verify unchanged values, and a changed input must be
observable. Before GPU-state comparisons the fixture executes a full cycle
to clear dirty state, then poisons vertex/pixel constants. Three changed
poses compare all 256 vertex and 32 pixel float registers and shader hashes
inside the selected unit pass. Both ideal static reuse and the filter match
the full-write path. Distinct poses must change the observed state.

An independent parameter write then changes the joint-count value. The
filter reads the new value and restores the requested one; an intentionally
broken unconditional skip produces a different observed state in all three
cases. This prevents a no-op/static-pose test from passing accidentally.
It is not a rendered-pixel or arbitrary device-state equivalence proof.

Median filter savings per modeled workload, milliseconds:

| Backend / repeat | Lit CPU | Lit completed | Shadow CPU | Shadow completed |
|---|---:|---:|---:|---:|
| Native D3D9 | 0.793563 | 0.820037 | 0.796125 | 0.796988 |
| DXVK, first | 1.053700 | 1.040137 | 0.803663 | 0.806913 |
| DXVK, repeat | 0.854262 | 0.856313 | 0.813625 | 0.816312 |

Lit and shadow each use the full 850-group population. **Do not add them**,
double them into a gameplay prediction, or add them to overlapping scope
savings. Isolated upload medians are only about 0.40-0.44 ms lit and
0.105-0.108 ms shadow; the lifecycle comparison captures additional dirty
state/upload consequences that the isolated calls miss.

[126 timing samples](../benchmarks/2026-10-18-material-source-writes.csv),
[152 demand observations](../benchmarks/2026-10-18-material-source-demand.csv),
[parameter-request metadata](../benchmarks/2026-10-18-material-source-parameters.csv)
and [run/source/input hashes](../benchmarks/2026-10-18-material-source-audit.json)
retain the evidence. Earlier isolated/fixed-pose runs are identified as
exploratory and excluded. An initial partial GetValue on a whole array was
refused; the final fixture reads full storage and uses a partial raw upload.

The existing member mode passes again on both backends. The default runtime
comparison still exits **2 with three D3DX27/D3DX43 command differences**,
as before. This is not a compatibility pass or reason to swap D3DX versions.
No production source, installed ASI/INI or stable branch changed.

## Next qualification

The filter is a possible contributor, below the existing standalone
promotion threshold. Check it through actual native typed callbacks and
the joined queue/group/mesh GPU workload before judging its net benefit.
Native setter/getter error equivalence, parameter-block recording and
application, shared effects, arbitrary input churn, lifetime/reset handling
and pixel/state restoration are still open. The current generic calls and
long-lived fixture effect cannot qualify those boundaries. Keep all native
source computations, changing poses, full-rate shadows and deadline guards.

Reproduce with the documented i686 toolchain from `perf/`:

```sh
mingw32-make build/bench_effect.exe
./build/bench_effect.exe C:/Jogos/AgeoftheRing/rotwk --sources-only
./build/bench_effect.exe C:/Jogos/AgeoftheRing/rotwk --source-writes
```

For DXVK use the separate executable/DLL directory described in
`perf/tests/RENDER_SETUP_BENCHMARK.md`. Run each case serially.
