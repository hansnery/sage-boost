# Native group batching connected to the runtime

Beta **0.9.11-beta.10** connects the production skeletal bulk loop to the
native renderer. `[Render] BulkRender=1` is enabled by default on the beta
branch and requires `Instancing=1`. Startup discovers the entire native
contract before installing mesh, draw or event hooks. It then installs a
group entry wrapper and the loop bridge only at unique verified signatures,
with the discovered identities checked again. Six timing/instancing hooks
must have installed successfully before activation.

The group wrapper owns the lifetime of pending units and the captured stream
reference. The first eligible native skeletal draw captures its palette and
stages its leader. The bridge consumes compatible records, submits checked
batches and advances only the accepted records in the native loop. Native
material Begin/Pass/End and group ownership remain in the native renderer.
Callbacks, unsupported geometry/method combinations and verification windows
retain the original route. Shadow batches use the current pose.

Aggregate accounting now lives in production `timing.c`. One interval
measures actual batch work, while mesh/scope counts remain per accepted
record. Extra-frame accounting retains its existing separation from regular
frames. The mesh label collapses to the same existing FXShader name.

## Return, unwind and failure behavior

The entry wrapper preserves the native thiscall arguments and nonvolatile
registers, installs a Win32 exception registration below the caller, invokes
the original trampoline, and removes its registration on return. Its handler
releases plugin-owned group state during unwind and returns
`ExceptionContinueSearch`; it neither handles nor resumes native exceptions.
The wrapper remains a lifetime boundary after bulk batching switches off.

No delayed draw is issued after native End/EndPass or during unwind. A normal
return with unsubmitted units, an interrupted call or an incomplete checked
transaction disables instancing for the session and logs the pending work.
The held stream reference is released, so it cannot be carried from that
group into a subsequent device Reset. This is a group-lifetime guarantee;
the plugin does not reset the device or intercept arbitrary driver calls.

Nested rendering first attempts ordinary replay while the outer material is
still current, then disables instancing and uses native rendering. Foreign
threads do not own or advance the optimized group. The loop bridge preserves
integer registers and flags; its verified fallback executes the displaced
LEA before continuing the original loop.

A recoverable API error uses the checked submission core's ordinary replay
and state restoration, then disables instancing. If restoration remains
incoherent or replay is incomplete, the bridge logs the error, operation and
committed/replayed counts, disables optimization and continues the native
loop after accepted records. It does not repeat a committed batch. Such a
failed frame may be incomplete; this is explicitly reported, not called a
successful recovery. Arbitrary persistent driver failure cannot guarantee
normal pixels. No process termination is used by production recovery.

## Verification scope

The private-image installation check discovers before other hooks, installs
the actual six prerequisite hooks, and then installs both production group
patches. Both executable families pass, along with changed-site/identity and
repeat-install refusals. The full native contract retains its existing
451 no-write refusal controls per image.

Seven lifetime cases exercise the production assembly wrapper and actual
instancer ownership against a COM lifetime double: normal return, a real
Windows exception/unwind, early native return, nested rendering, batching
disabled, incoherent restoration and incomplete submission. They require
balanced references, no dead-object use, no late drawing and restoration of
the outer exception chain. Failure outcomes are retained after cleanup.

The combined native/GPU fixture selects the same `bulk.o` wrapper/bridge
used by the ASI and unmodified production `inst.c`. It checks regular/extra
lit and shadow output, native ABI and exception chain, record/tie ordering,
references, callbacks, model splits and verification windows. Its stage
refusal variant uses only the existing controlled stage-entry injection;
link-time observers measure calls without replacing production algorithms.
Executable bodies are read from the user's files into private RAM; no EA
code or assets are stored in the repository.

The full plugin suite passes. Across both executable families, the active
production wrapper/bridge passes 384 fallback cases, 496 injected single-GPU
failures, 56 deliberately broken device-effect controls, eight combined
queue/GPU workloads and 24 broken-record controls. Production hook/core
objects pass the general-register instruction check.

Six serial DXVK runs retain 126 samples (four warmups, 21 AB/BA samples per
run, eight separately fenced regular/extra pairs per sample). Completion
savings for shuffled input are 7.5174/7.8847 ms in lit/shadow run 1 and
7.9241/7.3960 ms in run 2. Ordered input saves 6.0507/6.1158 ms. These combined
measurements replace earlier candidate timings; do not add isolated gains.
The exact production wrapper, bridge and instancer are included. Test-only
submission observers remain linked in the fixture and are absent in the ASI.

[Check and timing manifest](../benchmarks/2026-10-18-runtime-group-hook-checks.json)
preserves source/build/log hashes and distributions;
[all 126 samples](../benchmarks/2026-10-18-runtime-group-hook-samples.csv)
retain regular, extra and fenced-completion measurements. Built ASI SHA256:
`b936df66dbae8cdf0197a5c4c3331328809eed1f2e238a8f8763ee0362428f5f`.

The native source walkers still execute for group setup and the native
leader. Followers already qualified for the existing instancing path have
no additional material-source caching or callback pruning in this change;
the loop removes their event/string/reference dispatch. Arbitrary source
computation and asset lifetime remain outside the controlled fixture's
coverage, and the in-game verification windows keep their original path.

No gameplay result is established here: the last measured saved-game run remains about
30.18 presentations/s. The objective remains at least 59 presentations/s
over a settled 60-second capture, with full-rate fresh shadows and reported
pacing. No simulation, input, network, camera or FP-control changes are made.
