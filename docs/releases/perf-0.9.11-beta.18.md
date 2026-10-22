# sage-boost Beta.18 - recovery update

Performance plugin 0.9.11-beta.18, bundled with faction unlocker 0.5.0.

- Fixes two cases where a brief slowdown could keep the game drawing fewer
  frames even after the workload became lighter.
- Keeps the familiar approximately 60 FPS pacing mode as the default, with
  moving shadows updated on every rendered frame. Results depend on your
  PC and the battle; this update has not demonstrated higher FPS in our
  troop-heavy test scene.

Restart the game after updating. Your saved launcher settings still apply.
Choose a frame limit of 60 or higher to use the extra frames. The default
pacing mode still targets approximately 60 FPS when 120 is selected;
experimental 90/120 pacing requires a separate advanced INI opt-in and is
not enabled by this update.

For multiplayer testing, use the same sage-boost beta on every PC.
Multiplayer validation remains incomplete. Set Experimental > PairPacing
to 0 if you need to return to the earlier pacing behavior.

## Also included from Beta.14

- Smoother large battles: the original beta improved our very heavy Ultra
  High test scene from around 30 FPS to around 55 FPS. Results vary by PC
  and battle.
- Fresh moving shadows on every rendered frame.
