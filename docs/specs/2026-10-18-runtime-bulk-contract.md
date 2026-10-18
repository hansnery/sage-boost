# Runtime discovery for guarded bulk rendering

The bulk renderer now has a C runtime finder for the native group, mesh,
draw, effect and event boundaries used by the joined offline fixture. It
uses six unique wildcard roots, 36 hashed regions, 85 dependency identities
and 280 relocation fields. All sites come from the mapped executable.
No native body or asset is saved or linked into the plugin.

This is a prerequisite for the runtime bulk engine. `bulkcontract.c` is
currently linked into its private-image test only. Beta **0.9.11-beta.6**
contains the shared-verifier extension; no bulk hook is enabled or installed.
The installed ASI/INI remain unchanged. No new timing or gameplay result is
claimed: the latest combined offline screen remains 6.17-7.96 ms saved per
regular/extra pair, and last measured gameplay remains about 30.18/s.

## Contract and loader behavior

The graph joins the existing independently audited group, mesh and FX
contracts. Shared state and function identities are deduplicated. It checks
the 25 native routine bodies, two CRT jump thunks, the eight-entry draw jump
table, literal values, event labels and event-prefix bytes. External routine
dependencies outside those bodies receive identity/section checks, not full
body fingerprints. Dynamic source implementations remain outside this proof.

The shared verifier now accepts bodies up to 4,096 bytes, bounded pointer
addends and optional semantic-region spans/writable requirements. A pointer
to a field must resolve to the same containing region as other accesses.
Jump-table targets must retain their exact offsets into the verified draw
body. Every region's complete span must fit its required section and remain
disjoint from other identities. Fingerprinted data uses its actual code/data
role; existing insertion/sort metadata retains zero addends and prior roles.

The event prefix needs a 16-byte readable span for four word reads, with 13
bytes of string contents fingerprinted. Mutable engine data is never hashed
as if constant. Discovery writes only its output structure, which is cleared
on refusal, and never writes the mapped executable.

The first private-image run exposed an AotR loader detail:
`OriginalFirstThunk` is zero. Its only import table is overwritten during
loading, so looking for surviving import-name RVAs cannot work at runtime.
The final finder obtains the already-loaded `msvcr71.dll` and compares each
discovered IAT value with its named export. It does not load a DLL in the
game. The fixture loads the user's local CRT and binds those four private
IAT slots, modeling loader resolution without starting the executable.

## Verification

Both local game images pass discovery from private mappings at a different
base address. The test rebases only audited absolute fields, including the
draw table, and checks every discovered identity against the independent
Python audits. It also checks the exported group/mesh/draw/effect/callback
sites and the three group instruction boundaries.

Each image passes **451 no-write refusal controls**:

- 35 body/literal mutations; the pointer-only jump table is covered below.
- All 280 dependency fields redirected outside their required regions.
- All 120 nonzero dependency addends shifted by one byte while still
  targeting code or readable data, detecting inconsistent interior offsets.
- Six duplicated signature roots, writable-section removal, eight missing
  or wrong resolved imports, and an image-bound truncation.

Four additional synthetic region controls distinguish a valid interior
pointer from a region extending one byte beyond its section, non-writable
storage, an out-of-range addend and a zero-sized region. Failed generic
discovery must preserve its output buffer.

Discovery still succeeds after the actual queue-insertion and queue-sorting
entry hooks are installed into each private mapping. These tests execute
the finder and patch installation, not the native bulk-render group. The
existing insertion/sort execution suites separately exercise their handlers.

The full plugin suite passes, including both images' insertion/sorting
entry, ownership, callback and refusal checks. One insertion test's expected
diagnostic changed because a crossing body is now rejected earlier during
dependency-span validation. Its no-write refusal requirement is unchanged.
Both verifier objects contain no x87/SIMD or floating-point control
instructions. Preserve the [check summary and source/build hashes](../benchmarks/2026-10-18-runtime-bulk-contract-checks.json).

Built beta.6 ASI SHA256:
`9c8b59837d01735e0b8bd5f094785836ca406b37a4f6c75719d8fd4506dd5e54`.

## Runtime integration order and remaining work

Call `pf_bulk_find` before installing any event, mesh, draw or effect hook
whose bytes occur in this graph. Queue insertion/sorting may be installed
first, as verified above. Retain the discovered pointers for the guarded
group bridge and final leader-draw identity check; never rescan the already
modified native graph as though it were pristine.

Next move the checked bulk engine and bridge into production. Preserve
native group/method/pass limits and callback/verification exclusions. Close
resource ownership across temporary unbinding: in particular, audit
`capture()` releasing its `GetStreamSource` reference while storing the raw
`g.prev_vb` pointer for a later restore. Verify the case where the device
holds the only remaining reference, rather than relying on the fixture's
extra buffer owner. Persistent GPU errors, device loss and ordinary replay
failure handling also remain open. No intermediate gameplay test is asked
for this discovery-only prerequisite.

```sh
# From perf/ using the documented i686 toolchain:
mingw32-make test-bulkcontract
mingw32-make test
```
