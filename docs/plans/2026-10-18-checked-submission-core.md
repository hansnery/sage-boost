# Checked instancing submission implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

The execution subskills are unavailable. Execute inline with the user's
standing authorization; no delegation or further permission is needed.

**Goal:** Move the tested skeletal batch/replay transaction out of the
offline engine into one production module, then use its actual compiled
object in the native/GPU fixture. Preserve explicit information needed by
the forthcoming runtime bridge when restoration or replay fails.

**Architecture:** `instsubmit.c/.h` borrows the caller's held group objects,
captures the original bone palette, checks every setup/restoration call,
and reports committed/replayed units separately from restored state. It
never exits the process or chooses a native exception/device-loss policy.
Existing offline glue supplies native group eligibility and counters.
Broken-control injection moves to the test device proxy, outside the
production transaction. The module is linked into the beta but runtime
activation waits for the group bridge and its failure policy.

**Tech Stack:** general-register i686 C, D3D9 vtables, existing private
native-image/GPU tests on native D3D9 and DXVK.

- [x] Add the production descriptor/result API and batch/pending replay
  implementation. Bound all constant ranges; capture before deferral;
  replay only uncommitted units after successful restoration; never repeat
  a successful instanced draw.
- [x] Replace the offline transaction copy with adapter calls to that
  object. Preserve fault-step observation and move deliberate corruption
  to the fixture's separate device proxy.
- [x] Exercise persistent restoration failures and partial ordinary replay
  failures against the explicit result API. Run both-image native/GPU
  regressions and the full suite serially.
- [x] Record source/build evidence and limits and bump the internal beta.
  Beta.8 is ready for the authorized commit/push. No installation or new
  FPS claim for this prerequisite.
- [ ] Integrate group eligibility, the verified runtime bridge, resource
  cleanup and failure policy; measure the complete guarded candidate before
  requesting the user's saved-game test.

No game/WorldBuilder launch, reference-repository writes, camera/logic/input/
network changes, FP-control changes or installed-INI edits. The goal remains
59+ actual presentations/s over settled 60 seconds with fresh shadows.
