# Runtime bulk instancer implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

Execute inline: the execution subskills are unavailable and delegation is
not authorized. Standing user authorization covers development/commit/push.

**Goal:** Put native leader eligibility, checked submission and the bulk
record loop in the production instancer, then test those implementations
through native rendering. Keep installation gated on the complete runtime
bridge, failure policy and significant measured improvement.

**Architecture:** `instbulk.h` exposes configuration with verified sites,
qualification, bulk advancement/cancellation and transaction outcomes.
`inst.c` owns the group and its captured stream, defers only the verified
final native skeletal DIP, and calls shared `instsubmit.o`. Offline adapters
delegate to this implementation; link-time wrappers observe transactions
for tests. Only stage-failure injection may replace the stage entry in its
dedicated fixture; normal rendering tests compile the unmodified source.

- [x] Move leader capture/eligibility, skeletal submission and the bulk
  loop into production code, disabled until configured by a verified hook.
  Propagate incomplete/incoherent outcomes; never use ExitProcess here.
- [x] Replace the checked offline engine with delegates and link-time
  observation. Keep deliberate stage corruption in the fixture only.
- [x] Check native/GPU controls on both images, joined insertion/queue
  controls and the full suite. Preserve references, scope counts, callback
  fallback, fresh shadows, model splits and verification-window behavior.
- [x] Record evidence, internal version and limits; commit and push.
- [ ] Integrate actual hook installation, aggregate timing and runtime
  abandonment/Reset/failure policy; measure before installation/gameplay.

No game/WorldBuilder launch, reference-repo writes, installed-file changes,
camera/logic/input/network changes, FP-control changes or FPS claim here.
