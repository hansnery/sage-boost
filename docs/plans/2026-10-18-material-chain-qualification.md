# Material-chain qualification implementation plan

> Execution: inline, continuing the user-authorized performance investigation.

**Goal:** Qualify the measured follower-loop gate change under partial
staging, failed GPU operations and native method-chain exit behavior.

**Architecture:** Reuse the real native group, mesh and FX bodies and the
existing fault proxy. Share the controlled owning-chain fixture with the
screen; add child callbacks that change and restore real GPU render state.
Keep the candidate gate in a developer-only generated object until qualified.

**Constraints:** No game launch, reference checkout edits, shipped assets,
camera/logic/input/network changes or floating-point control changes. Keep
the installed beta.11 and its INI unchanged during qualification. Controlled
children are not a claim of complete native subclass coverage.

## Qualification fixture

- [x] Extract the owning chain helpers from `bench_chain_screen.c` to
  `native_chain_fixture.h`; preserve the existing screen behavior.
- [x] Add optional case/setup callbacks to `test_native_gpu_faults.c` and
  an overridable extra-state snapshot in `test_native_group_fallback.c`.
- [x] Add `test_chain_recovery.c`: include the existing fallback fixture,
  use `chain_records(count)` for lengths 2..6, set child phases in Begin/
  BeginPass/EndPass/End, and save/set/restore `D3DRS_COLORWRITEENABLE` in
  nested child order. Include links, phases, references and GPU state in
  `cq_snapshot(unsigned *out,unsigned capacity)` for immediate recovery.
- [x] Compare original native rendering with optimized rendering for every
  inherited GPU failure and partial-staging case, including poisoned next
  slots and a subsequent group. Require no missing pixels, stale scopes,
  state changes, reference leaks or ABI failures. Require consumed followers
  in clean optimized runs so a disabled gate cannot pass accidentally.
- [x] Exercise failed native Begin and the following successful group;
  compare native retained links rather than imposing extra cleanup.
- [x] Build six- and twenty-bone variants; run both executable images,
  native D3D9 and DXVK, lit and shadow, serially with the game/launcher guard.
- [x] Reject deliberate count, batch replay, partial replay, palette restore,
  child-link and disabled-gate corruptions. Record source/binary/log hashes
  and results without persisting game bytes.
- [x] Document limits and commit/push the independently reviewable evidence.

## Promotion checkpoint

Only after qualification, change the production guard from count `== 1` to
the validated range `1..6`, preserving full native method-array boundaries.
Version and test changes belong to a separate plugin commit. Require the
full suite, instruction audit, GPU qualification and a fresh timing check
before any installation. Synthetic savings never establish saved-game FPS.
