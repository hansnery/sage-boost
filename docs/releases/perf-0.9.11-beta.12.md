# bfme2perf 0.9.11-beta.12 — development build

Unsampled groups can keep bulk rendering while an instancing verification
window is open. Groups selected for verification still follow the native
path, with the same schedule and quotas.

The policy regression, native/GPU output checks and full plugin suite pass.
This fix has not demonstrated a substantial gain in the troop-heavy save.
Beta.11 remains installed; no stable release or new gameplay test is requested.

[Evidence and limitations](../specs/2026-10-18-verification-window-bulk.md)
