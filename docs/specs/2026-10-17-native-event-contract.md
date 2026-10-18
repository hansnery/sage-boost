# Native render-event contract

Read-only verification plus isolated native execution on both local AotR
`game.dat` and `game820.dat`. No game initialization, scene, loop or Present
runs. The reference remains read only and no EA bytes are stored in the repo.

Four complete bodies (312 bytes total) match the local 1.06 reference after
normalizing only inspected absolute data operands and external branch
targets. The constructor is located by a unique full-body wildcard pattern;
its destructor requires the verified constructor/padding context. Callback
routines are uniquely located and all call/jump targets are checked.

| Body | Bytes | Reference RVA |
|---|---:|---:|
| Event constructor | 167 | 0x117CF0 |
| Event destructor | 5 | 0x117DA0 |
| Begin callback wrapper | 128 | 0x11F340 |
| End callback wrapper | 12 | 0x11F3C0 |

These are research identities, never installation addresses. The verifier
also checks the MSVCR71 `strncpy` import, the exact local event prefix,
contiguous prefix operands and two distinct writable non-executable
callback slots. It does **not** assume that the live callback slots are null.

Each target passes nine refusal controls: altered entry, stack cleanup,
import binding, aliased callback slots, an executable callback-slot address,
changed prefix, wrong constructor/destructor branch targets and a duplicated
constructor signature. Mutations occur in private Python memory only.
An initial control mistakenly used the prefix as a supposedly read-only
address; its section is writable, so accepting that address was expected.
The final control uses an executable section and verifies rejection.

The native helper reads only those verified bodies into private executable
pages, checks their hashes again, and connects them to the user's CRT and
controlled callbacks. The temporary manifest holds numeric offsets/hashes
only and is removed. No image entrypoint or other game function executes.

**80 cases per image pass**, covering all independent begin/end callback
combinations; null, empty, ordinary, long and high-byte labels; null, empty,
ordinary and long groups. Checks cover constructor return value, destructor
callback result, exact bounded buffer writes, truncation, signed character
widening, callback arguments/counts and surrounding canaries. An integer-only
call trampoline checks stack cleanup and EBX/ESI/EDI preservation.

The result supports a narrow condition for further work: with both callback
slots null, these routines only prepare the event's local buffers and take
the null callback branches. When either slot is active, individual events
remain observable and must use the original path. This does not yet prove
that the enclosing rendering loop never consumes the local buffers elsewhere,
or that skipping their construction preserves method ownership and native
SEH cleanup. No production event is omitted by this work.

Reproduce in the documented toolchain from `perf/`:

```sh
mingw32-make test-native-events
```

This is contract groundwork for the
[combined material/accounting experiment](2026-10-17-material-bulk-accounting-screen.md),
not another speed or gameplay measurement. The native group bridge, source
callbacks, resource lifetime and engine binding caches remain open gates.
