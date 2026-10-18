# Instancing dispatch benchmark implementation plan

> **For agentic workers:** Execute this plan task by task. The suggested
> superpowers execution subskills are not installed; use the available
> native build and test workflow directly.

**Goal:** Establish whether eliminating compatible-member dispatch can save
enough time to justify another gameplay test.

**Architecture:** An offline native D3D9 harness reuses the existing fake
objects, real instancing implementation and locally loaded game shaders.
Compare the current per-member hook path with an optimistic bulk-staging
prototype that retains the same draws. Keep the prototype out of the ASI.

**Tech stack:** i686 GCC, C, existing x86 hook stubs, D3D9 and DXVK, QPC.

## Global constraints

- No game/WorldBuilder launch, installed changes or reference-repo edits.
- No EA bytes/assets saved or committed; shaders read from the local install.
- Preserve all production gameplay, camera, FP and deadline safeguards.
- User wants another test only after a significant improvement. Screening
  threshold: repeatable savings around 3 ms per representative regular-frame
  workload, with correctness evidence; synthetic speed alone is not a claim
  of saved-game FPS or sufficient grounds for deployment.
- A bulk loop without retained detailed markers is an optimistic bound. If
  even that falls short, reject it before adding runtime signatures/hooks.

## Task 1: Offline comparison

- [x] Add a separately built benchmark using the existing GPU test fixture.
- [x] Exercise production enter/exit stubs, detail markers and instancing.
- [x] Add a test-only bulk helper with the same staging and flush behavior,
  leaving production compilation unchanged.
- [x] Check pixels, draw/mesh counts and method references before timing.
- [x] Alternate order over repeated samples; fence GPU work and report CPU
  submission and GPU-completed time separately. Include a Detail=0 control.
- [x] Match the trace's approximate instanced mesh/group scale and document
  synthetic geometry, cache, material and shader coverage limitations.
- [x] Run native D3D9 and DXVK; missing resources must fail the benchmark.

## Task 2: Decision and regression checks

- [x] If savings are small, record rejection and leave the installed ASI.
  If material, first prove the installed caller ABI, references, scope
  accounting and fallback boundaries before proposing a runtime patch.
- [x] Run the existing plugin suite after any shared-source changes.
- [x] Record reproducible commands and measured results in the repository.
- [x] Update STATUS/BACKLOG, commit explicit paths and push the beta branch
  under the user's standing authorization. No stable release or tags.

## Outcome

Completed: synthetic colour/shadow output and state checks pass on native
D3D9 and DXVK, as does the full plugin suite. Final paired median savings
are 1.4-1.7 ms with Detail=1. The standalone runtime shortcut is deferred;
no target-loop hook, deployment or gameplay test request is justified.
See `../specs/2026-10-17-dispatch-benchmark-results.md` and its raw samples.
