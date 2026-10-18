# Instancing stream ownership implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

The execution subskills are unavailable; execute inline under the user's
standing authorization. No delegation or extra approval is required.

**Goal:** Keep a captured stream buffer alive until its last restoration,
including the bulk renderer's deferred leader and cancellation paths.

**Architecture:** Retain the successful GetStreamSource reference with an
explicit group owner. Release it on terminal state transitions, after all
pending draws are resolved. Keep retained fixture snapshots separate from
that ownership token so copying a group does not duplicate ownership.

**Tech Stack:** i686 C, COM reference-count fixture, native D3D9/DXVK checks.

## Global constraints

No game/WorldBuilder launch, no installed-file changes for this prerequisite.
Read-only Open-BFME-2. No logic/input/network/camera or FP-control changes.
Keep the full 59+ actual presentations/s goal open until gameplay proves it.

## Implementation and checks

- [x] Add `perf/tests/test_inst_ownership.c` with a device owning the only
  buffer reference. Its tombstone object records use after destruction
  without freeing memory; require the current production capture/batch to
  fail the ownership assertion before changing `perf/src/inst.c`.
- [x] Add `release_stream_owner()` and transfer the successful getter's
  reference into the group owner. Acquire only after other capture gates.
  Release after normal completion, rejection, model split, early group end,
  initialization and session-failed fallback. Never release pending data
  before draw/replay resolves it.
- [x] Update offline bulk completion/cancellation to use the same owner
  release. Verify two consecutive batches, null streams, refused captures,
  multiple leader draws, group replacement and reset without leaks or
  dead-object access.
- [x] Exercise a real D3D9/DXVK stream binding and object lifetime, plus the
  existing native/GPU recovery/fallback checks and full plugin suite. The
  ownership fixture supplies deterministic destruction checks; GPU tests
  supply actual API state/output evidence.
- [x] Bump the internal beta build and record evidence and limitations.
  Beta.7 is ready for the authorized commit/push with the full suite passing.
- [ ] Continue moving the checked submission/bulk bridge into runtime;
  do not request a saved-game test for this ownership prerequisite alone.

Results: `../specs/2026-10-18-instancing-stream-ownership.md` and its evidence
manifest. Normal completion/refusal GPU tests require release without
initialization masking a leak. Only the unfinished-group case uses init.
