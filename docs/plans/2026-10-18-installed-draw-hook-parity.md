# Installed draw-hook parity implementation plan

**Goal:** Test the bulk GPU path with the same Draw/DIP hook metadata and
installer that the ASI uses, to investigate beta.11's 30 FPS gameplay result.

**Architecture:** Keep verified native bodies in private RAM. Copy the
production site definitions into the runtime fixture and apply `pf_install`
to its native Draw body. Leave the controlled outer scene unchanged. This
is a continuation of the approved bulk-path audit, executed inline.

**Constraints:** No game launch, reference edits, asset redistribution,
installed changes, scheduler changes or claim of gameplay FPS.

- [x] In `perf/tests/bench_dispatch.c`, copy the production definitions for
  EVENT_BEGIN, EVENT_END, DRAW, INST_MESH and INST_DIP when PF_RUNTIME_BULK
  is defined, before `pf_timing_open`.
- [x] In `perf/tests/bench_native_leader.c`, apply the real Draw entry and
  indirect DIP hooks with `pf_install` on the verified private Draw body.
  Pass its discovered trampoline and indirect displacement to timing.c.
  Require exactly one match and the expected native offsets for each hook.
  Leave the native Triangles-to-Draw call intact, so the real entry
  trampoline and caller's return address execute in the GPU tests.
- [x] In `perf/tests/bench_native_group_gpu.c`, restore the fixture's native
  loop bytes and use `pf_bulk_install` for its entry and loop. Call the
  installed entry so its actual stolen-prologue trampoline executes too.
- [x] Adapt `perf/tests/test_native_gpu_faults.c` to select the proxy through
  the native device slot when using the installed indirect call. The old
  fixture wrote a function pointer where the real hook needs a vtable
  displacement; this failed immediately under the production metadata.
- [x] Run `test-runtime-bulk-gpu` and `test-runtime-group-fallback` for both
  local executable families. Preserve output, state, lifetime, failure and
  negative controls. If the paths fail, isolate the first divergence before
  changing production code. If they pass, record this ruled-out discrepancy
  and continue the performance investigation without an installation.

Final result: both local images pass on native D3D9 and DXVK, lit and shadow.
Eight combined cases, 384 fallback cases, 496 injected GPU failures,
56 broken recovery controls and 24 broken queue controls pass. No
production or installed file changed. See the linked audit/manifest in
STATUS.md. The remaining 60 FPS work is still open.
