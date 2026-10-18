# bfme2perf 0.9.11-beta.13

The fast follower path now supports native material chains with up to six
methods. Previously, these groups could defer their leader while their
followers still paid the native per-mesh wrapper cost. Material boundaries,
geometry, shadow cadence and simulation timing are preserved.

This beta includes the beta.12 fix that keeps unsampled groups eligible
while a verification window is open. New timing CSV columns report actual
consumed followers by method count, separately for normal and extra frames.
No INI change is needed.

The full suite, both-image/backend GPU recovery checks and instruction
audit pass. Fresh paired synthetic timings save about 3.4 ms for lit units
and 2.6 ms for fresh shadows per normal/extra pair. These are separate
workloads; they are not a saved-game FPS result or a guarantee of 60 FPS.

Use the same troop-heavy save and camera position for the next gameplay
test, with FrameView running for at least 60 settled seconds. The new log
and CSV can establish actual presentation rate, pacing and chain coverage.
Keep the existing shadow and INI settings.

See [implementation, measurements and limits](../specs/2026-10-18-material-chain-runtime.md).

Installed on the development machine from pushed commit `5e94796` on
2026-10-18, with the prior beta.11 and its INI/logs/CSVs backed up. Tested
and installed hashes match, and INI bytes are unchanged. The game was not
launched. [Installation receipt](../benchmarks/2026-10-18-material-chain-install.json).
