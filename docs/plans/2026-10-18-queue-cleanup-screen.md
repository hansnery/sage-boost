# Queue cleanup CPU screen

**Goal:** Determine whether native rendering-queue destruction can supply a
meaningful part of the remaining frame budget before any runtime work.

**Architecture:** Reuse the insertion fixture's verified native bodies in
private RAM. Compare full native erase with an independently written loop
that releases a record directly only when no holder can reach zero. Keep
the native record destructor for all other records. This is an offline
normal-return screen; exception/context qualification remains open.

**Constraints:** No ASI/install/INI/reference edits, no game launch, no EA
bytes persisted. Native order is method slots 7..2 followed by mesh slot 0;
record bytes are not zeroed. Use general registers only.

- [x] Add `perf/tests/bench_queue_cleanup.c`, reusing the native insertion
  loader. For each record, qualify mesh refs >1, method refs >6, and no
  mesh/method alias before changing anything. Otherwise call native dtor.
  Clear `last` only after every record, as native full-range erase does.
- [x] Compare empty/small/large queues, 0/1/3/6 methods, repeated/null
  holders, mesh/method aliases and last-owner callbacks. Compare queue
  bounds, record bytes, every reference count and callback order/state.
  Require a deliberate wrong-release control to fail.
- [x] Run both verified executable families with four warmups and 21
  alternating paired samples, eight 7,680-record clears per sample.
  Restore fixture ownership outside each timed clear. Preserve raw samples.
- [x] Use timings to decide whether full native/GPU integration is worth
  doing. A small isolated saving is not a reason to install or request
  another gameplay test; the actual 60 FPS objective stays unchanged.

Result: 192 controls per run pass with exact callback event arrays; all
three wrong-release controls fail as expected. Median savings of
0.0836-0.0926 ms per 7,680-record clear are insufficient for promotion.
See [results](../specs/2026-10-18-workload-screens.md).
