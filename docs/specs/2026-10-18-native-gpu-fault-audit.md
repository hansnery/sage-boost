# Batch submission fault audit

The current joined native batching candidate has reproducible failure-path
defects. A rejected instanced draw drops already staged meshes; several
state-setting and restoration failures leave incorrect output or device
state while instancing remains enabled. **This audit does not implement
recovery or qualify the candidate for installation.**

Across native D3D9/DXVK and lit/fresh-shadow workloads, each case set has
56 one-shot rejected calls: fourteen operation positions, in either batch,
in a regular or extra frame. Every set detects 28 changed-output cases and
28 immediate-state leaks; 52 cases leave instancing enabled despite the
detected error. Four rejected-DIP cases disable it but lose staged units.
These are controlled counterexamples, not evidence that these errors occur
in the user's game or cause its approximately 30 FPS cadence.

## Injection and independent checks

The fixture uses the previous sixteen verified native group/mesh/event/
cleanup bodies and real local effect/GPU path. It supplies the instancer
with a separate COM-compatible proxy that forwards to the real device after
replacing its `this` argument. The driver object's vtable is not modified.
General-register forwarding thunks are our own code in private RAM. The
selected API operation returns `D3DERR_INVALIDCALL` once **before** invoking
the original method. Other calls execute the original backend.

Original and candidate clean images match with all 25 meshes positioned in
separate screen regions. A zero-filled alternative stream buffer makes
failed instance-buffer binding observable instead of accidentally leaving
the correct offsets bound. Shader, declaration, stream bindings/frequencies
and all vertex constants are captured independently before and immediately
after each batch. Checking at batch exit detects damage that the next batch
or an outer state block could otherwise hide.

Successful draw forwarding and actual indexed-stream frequency provide the
submitted-instance count; ordinary native DIPs are counted independently.
Tests also check pixels/depth, native attempt counts, suppressed leaders,
method references, scope balance, stack/register ABI and FS chain. A
deliberately corrupted instance-count oracle is rejected for all four
backend/workload combinations. The existing 136 native group comparison
cases and verification-window control run before each audit.

An earlier attempt to substitute the real device's public vtable crashed
before producing any fault sample. It was removed: driver implementations
can have private layout requirements beyond the public COM interface. Two
unseeded proxy runs are also excluded from the final observations.

## Operation positions

| Step | Operation |
|---:|---|
| 1 | Read the vertex constants that the batch will overwrite |
| 2-4 | Bind the instanced shader, declaration and offset stream |
| 5-6 | Set indexed and per-instance stream frequencies |
| 7 | Upload instance palettes |
| 8 | Issue DrawIndexedPrimitive |
| 9-10 | Restore both stream frequencies |
| 11-13 | Restore stream binding, declaration and shader |
| 14 | Restore overwritten vertex constants |

The proxy checks this call order against the one-stream fixture rather
than silently treating an unexpected call as the intended fault.

Examples from DXVK lit regular frames:

- Rejecting the first batch's DIP leaves only the last ten native unit
  draws. The first fifteen units, including the deferred leader, are lost;
  75 pixels differ. Instancing is then disabled.
- Rejecting the second batch's DIP leaves the first fifteen instanced units
  but loses the remaining ten; 57 pixels differ.
- Rejecting the first instance-stream binding changes 75 pixels while
  instancing remains active and the API submission count still says 25.
  Submission counts alone cannot prove correct output.
- Rejecting the first constant snapshot or its restoration leaves 588
  constant words different immediately after that batch. Output may still
  match in that frame, and instancing remains enabled. Final screenshots
  alone cannot prove state restoration.

The code clears `g.count` before drawing and retains no replay list. It
ignores setup/restore HRESULTs and checks only the DIP result, so disabling
the session afterward cannot recover already skipped native draws. The
same draw-batch routine is used by ordinary follower instancing, although
this audit specifically measures the deferred-leader candidate.

## API scope and next work

Microsoft documents `D3DERR_INVALIDCALL` for a failed indexed draw.
[DrawIndexedPrimitive documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawindexedprimitive).
Device loss is a separate problem: rendering calls can report success while
their work is silently discarded.
[Lost-device behavior](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices).
The injected rejection occurs before forwarding and proves no behavior for
persistent failures, failures after a state mutation, lost devices, resets
or ambiguous submission outcomes.

Recovery must track staged ownership and which draws actually committed,
check setup before submission, and restore both device state and the
effect/engine's corresponding state before replaying unsubmitted work.
Already successful draws must not be replayed after a restoration error.
The controlled source/state helpers still do not establish full native
cache/lifetime behavior. Add a recovery candidate to these cases and require
matching output/state/counts; do not interpret today's detected-bug assertions
as a passing recovery test.

[224 observations](../benchmarks/2026-10-18-native-gpu-faults.csv) and
[operation/source/executable/log metadata](../benchmarks/2026-10-18-native-gpu-faults-run.json)
are preserved. This instrumented audit is correctness-only and makes no
performance claim. No production source, installed binary/INI, stable
branch or reference repository changed. The approximately 3 ms screen
remains separate evidence; full-rate 60 FPS remains unachieved.

The full plugin suite and all four joined native/GPU regression cases pass
after the shared fixture changes. Built beta.3, installed 0.9.10 and installed
INI hashes are unchanged. The audit's success means the counterexamples
were detected; it must not be described as successful failure recovery.

Reproduce from `perf/` with the documented i686 toolchain and local assets:

```sh
mingw32-make test-native-gpu-faults
```
