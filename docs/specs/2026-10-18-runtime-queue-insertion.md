# Runtime rendering-queue insertion

Beta **0.9.11-beta.4** puts the spare-capacity insertion optimization into
the plugin. `[Render] QueueInsert=1` enables it after executable verification;
zero keeps the original function. The master switch still applies. This is
the first runtime portion of the combined insertion/sort/render candidate.
Sorting and bulk rendering remain offline implementations, so this build
is not installed and no gameplay test is requested.

The complete plugin suite passes. Both executable variants pass 576 native
comparisons through the exact production handler and another 576 through
the actual entry hook in private images. Each image also passes 92 runtime
refusal cases without any image changes. Combined GPU controls pass on
native D3D9 and DXVK for both images, lit and freshly rendered shadows;
all 24 corrupted-record controls are rejected.

## Runtime boundary

The finder locates the entry using a unique 48-byte wildcard signature.
It traverses encoded calls and absolute operands to verify 33 complete
function bodies (2,148 bytes), with normalized FNV and CRC hashes. Repeated
dependencies must resolve consistently, code/data section roles must match,
all bodies must fit within the image/sections, and distinct symbols must
not overlap. The thirteen external exception/heap dependencies receive
identity/section checks; their bodies are not replaced or fully hashed.
Metadata contains no fixed target addresses or copied executable bodies.
Generate it through `python tools/verify_sites.py --perf-queue --write`;
this mode verifies both local variants and leaves unlocker tables alone.

After verification, the existing patcher replaces the entry's five-byte
`mov eax,imm32` with a jump to the general-register handler. The trampoline
preserves that instruction and returns to the original SEH setup. No caller,
logic, network, frame counter, camera or floating-point control state changes.

The handler preserves the three queue-selection branches and the exact
32-byte record layout. It fills a spare slot, initializes unused holders,
adds the mesh/method references and consumes the incoming by-value mesh
holder once. Full queues, oversized method stacks, destination/input aliases
and other rejected preconditions delegate to the original entry. Allocation,
growth and their exception behavior remain in the original implementation.

## Verification

The private-image test reads the user's executable into RAM without loading
imports, executing its entry point or starting the game. It rebases the
verified absolute operands to the private address, then exercises the actual
installed entry and its original trampoline. Heap allocation/free callbacks
are controlled. The reference path uses a separate unhooked native copy.

The existing 576 comparisons cover eight record counts, three starting
capacities, zero through seven methods and distinct/duplicate/null method
patterns. They compare exact records, queue lengths and capacities, mesh
and method references, unchanged source objects, unused storage, allocation
balance, stack, callee-saved registers and the exception-frame chain. Extra
controls cover final-holder deletion, null-mesh rejection and input aliases.
The handler fixture's corrupted-record execution must fail its independent
record oracle. The installed-entry fixture additionally refuses a second
installation.

Runtime refusal cases alter full-body instructions, each of 81 encoded
dependency fields, root uniqueness, section/image bounds and a repeated
helper's identity. Both finding and attempted installation must refuse, clear
the output entry and preserve a hash of the complete private image.

`make test` now includes the runtime finder/entry checks when the images are
present, alongside existing plugin/configuration/hook/load tests. The load
test confirms the new version/settings and refusal in a non-game process.
The production object is built with general-register flags; disassembly
contains no x87/SSE instructions. No installed ASI or INI has changed.

## Combined performance

The GPU fixture now calls the exact production insertion handler. The
sorting/bulk rendering portion remains the previously qualified fixture.
These results replace the previous combined screen; they are not additional
savings or a measurement of the installed plugin.

| DXVK completion saving per regular/extra pair | Run 1 | Run 2 |
|---|---:|---:|
| Shuffled, lit | 7.538 ms | 7.697 ms |
| Shuffled, fresh shadows | 7.779 ms | 7.800 ms |
| Ordered, lit | 6.139 ms | — |
| Ordered, fresh shadows | 6.128 ms | — |

Each serial run uses the game/WorldBuilder process guard, four warmups and
21 alternating-order samples of eight separately fenced pairs. The workload
contains 7,200 owned records across 288 groups, with advancing material time
and changing shuffled input. Shuffled p10-p90 saving ranges span 6.97-8.90 ms.
Actual saved-game queue order and busy extra-frame costs remain unknown.
The roughly 8 ms engineering budget and actual 60 FPS/pacing target are
still unproven. Last gameplay evidence remains about 30.18 presentations/s.

Preserve [126 timing samples](../benchmarks/2026-10-18-runtime-queue-insertion-timing.csv)
and [verification summaries and hashes](../benchmarks/2026-10-18-runtime-queue-insertion-runs.json).
The built ASI SHA256 is
`3dd6207681f93389f34e40c412f46988e4b31fb9a365c1989c107fa302f9212c`.

## Remaining work

Integrate guarded native-order sorting and bulk rendering into the runtime.
Preserve native group/method ownership and callback/verification exclusions;
qualify remaining source/resource lifetimes and persistent/device-loss/replay
failure behavior before installing the complete candidate. These insertion
tests do not establish native exception recovery for those other features.
Only a subsequent user gameplay capture can demonstrate the FPS goal.

```sh
# From perf/ using the documented i686 toolchain:
mingw32-make test-queueinsert test-native-insert
mingw32-make test
mingw32-make test-native-insert-gpu
mingw32-make test-native-insert-gpu AOTR_DAT=C:/Jogos/AgeoftheRing/rotwk/game820.dat
mingw32-make bench-native-insert-gpu
```
