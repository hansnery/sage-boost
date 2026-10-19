# Launcher profiles

`realms-beta14.ini` is the Realms Launcher Beta template for the tested
0.9.11-beta.14 performance binary. It starts from the previous published
template, preserves every existing value, and adds QueueInsert, QueueSort,
BulkRender and PairPacing enabled. Existing launcher overrides still apply.

The generic `perf/bfme2perf.ini` remains a conservative standalone template
with PairPacing off. Do not replace an installed user's INI with either
template; local deployments preserve their settings and add missing keys.

The launcher bundle is `0.5.0.1-perf.0.9.11-beta.14`, containing faction
unlocker 0.5.0 and performance plugin 0.9.11-beta.14. The numeric bundle
revision is necessary because the current launcher compares only four
leading numeric version components. Changing the performance suffix alone
would not advertise an update. Neither binary is relabeled by this revision.

Release staging uses the prior published unlocker DLL and its exact INI,
plus the tested ASI and this template. Only the four component files ship;
the game executable and assets are never included.
