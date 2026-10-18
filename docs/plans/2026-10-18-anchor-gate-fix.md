# Correct the native mesh anchor eligibility check

**Goal:** Recover batching rejected by beta.10 without skipping a mesh that
needs the native relative world transform. Keep the 60 FPS objective open.

**Evidence:** The beta.10 saved-game capture has 1,769 presentation calls
over seconds 50..109 (29.4833/s), no extra frames, and about 7,127 rejected
meshes per frame. `lead()` rejects every non-null mesh +0x310 value and
incorrectly labels the rejection a model change. The verified native mesh
body uses this field as a pointer to an anchor pointer: null, null pointee
and self anchor all take its identity-world branch. A different anchor
takes the relative-transform branch and must remain native.

This is a bug fix within the approved batching design. Execute inline under
the existing authorization; no new feature or scheduler policy is proposed.

- [x] Add failing cases to `perf/tests/test_inst_ownership.c` for null,
  null-pointee and self anchors at ordinary leader/member admission; retain
  a valid different-anchor refusal with pending-batch cleanup.
- [x] Give runtime native/GPU fixtures a mixture of all three identity
  cases in `perf/tests/bench_native_leader.c`. Confirm beta.10 fails their
  expected batching counts. These fixtures execute the verified native
  branch as well as production hooks, with fresh regular/extra shadows.
- [x] Add one integer-only helper in `perf/src/inst.c`:

  ```c
  static int identity_skin_anchor(const unsigned char *mesh)
  {
      const unsigned char *const *anchor =
          *(const unsigned char *const *const *)(mesh + 0x310);
      return !anchor || !*anchor || *anchor == mesh;
  }
  ```

  Use it at leader/member admission, bulk match/advance and final-DIP
  qualification. Rigid meshes do not use this native skeletal branch.
  Keep different-anchor meshes native and report a distinct mismatch reason.
- [x] Bump beta.11 in main.c, version.rc and the load-test expectation.
  Run ownership, actual native/GPU fallback and combined checks on both
  executable families, then the full suite and instruction inspection.
- [x] Preserve the gameplay failure report and final validation hashes.
  Measure the corrected complete path serially with identity anchors.
- [x] Commit/push and install only the tested substantive fix with backup
  and unchanged installed INI. Actual saved-game performance remains
  unproved and needs a new capture.

Never launch the game/WorldBuilder, change the read-only reference, weaken
deadline margins, move the camera, alter simulation/network/input or touch
FP control state. Tests read EA bodies into private RAM only.
