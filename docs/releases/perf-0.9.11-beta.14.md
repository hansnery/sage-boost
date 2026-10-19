# Smoother large battles — sage-boost Beta

Performance plugin 0.9.11-beta.14, bundled with faction unlocker 0.5.0.

- Improves frame pacing and allows more frames to be drawn in demanding battles.
  In my very heavy Ultra High test scene, performance rose from around 30 FPS
  to around 55 FPS. Results depend on your PC and the battle.
- Keeps moving shadows updated on every rendered frame.
- Recovers more reliably after brief slowdowns, so a temporary stall is less
  likely to leave the game drawing fewer frames for the rest of the match.

Restart the game after updating. Your settings saved in the launcher still
apply. If you previously selected a 30 FPS limit, choose 60 or higher to use
the extra frames. This beta's paired pacing targets approximately 60 FPS;
selecting 120 does not enable 120 FPS during paired gameplay yet.

For multiplayer testing, use the same sage-boost beta on every PC. This new
performance mode has not yet completed multiplayer validation. If needed,
set Experimental > PairPacing to 0 to return to the previous pacing behavior.
