# Installed hook parity after the beta.11 gameplay result

The first native GPU fixture used a zero-initialized site table. Its Draw
hook therefore wrapped the return address instead of using the installed
entry probe. Its DIP hook continued at a fixed function pointer instead of
reading the native device's vtable. The bulk group wrapper was called
directly, bypassing the installed stolen-prologue trampoline. These were
test-coverage gaps, not established production defects.

Runtime GPU fixtures now copy the production Draw, DIP, mesh and event
definitions. The actual signature installer hooks the privately relocated
Draw body, and `pf_bulk_install` hooks the private group's entry and loop.
The native Triangles-to-Draw call stays intact. The fixture checks unique
matches, hook offsets, the entry trampoline and the DIP displacement.
Executable bodies remain in private RAM; no game file is modified.

The fault fixture initially failed because it replaced the indirect-call
displacement with its proxy function pointer. It now supplies that proxy
through the native device slot, exercising live vtable resolution. This
correction affects only the fixture; the ASI already uses the displacement.

The combined GPU controls pass with these actual installed paths, including
leader deferral and bulk follower consumption. This rules out these hook
differences for the tested workload. It does not establish live bulk
coverage or saved-game performance, and no production binary is changed.

Both local executable families pass on native D3D9 and DXVK, with lit and
shadow workloads: eight combined cases, 384 zero/partial fallback cases,
496 injected GPU failures, 56 broken recovery controls and 24 broken queue
controls. Each case also executes the existing 136 native group controls.
The [validation manifest](../benchmarks/2026-10-18-installed-hook-parity.json)
records source, executable, capture and log hashes. No new speedup timings
were collected; the previous synthetic timing results are not new gains.

## Further evidence in the existing gameplay capture

The 98..157 interval averages 3,110.73 shadow mesh markers/frame. Its
shadow instancing classifications total 3,238.51/frame when the separately
reported 141.80 leaders are included. The 127.78 surplus is consistent with
bulk leaders being included in both `meshes` and `leaders`: ordinary
leaders are counted only in `leaders`. This is evidence against the entire
bulk path being dormant, not a direct consumed-record measurement. CSV
values are rounded per row, and native marker/pass accounting and
verification can differ; do not present it as an exact coverage percentage.

The same interval's measured shadow instanced-submission cost is only
0.2014 ms/frame. Shadow renderOneObject's 2.6118 ms includes rendering and
skeleton work; it is not all lighting. Its pre-render and environment-build
parts are 0.2927 and 0.1178 ms respectively. The log reports zero scene and
dynamic lights tested per object, explaining zero LightReuse hits/misses.
These overlapping scopes do not support finding several milliseconds by
optimizing instanced submission or empty light lists alone.

The actual result remains 30.0667 presentation calls/s. Next experiments
must address the remaining CPU rendering work and use more representative
bone counts/material diversity; the previous fixture uses six bones while
the live verification log includes twenty-bone unit models. No new gameplay
request or diagnostic-only installation follows from this audit.
