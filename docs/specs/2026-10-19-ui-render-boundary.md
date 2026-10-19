# UI rendering: audit before considering reuse

The 120 FPS target needs cheaper complete frames as well as beta.18's extra
draw opportunities. In the preserved beta.16 capture's 1193..1252-second
interval, 1,505 extras average 6.774 ms including the outer draw checks.
Their RenderUI scopes average 1.428 ms. This makes UI a worthwhile profiling
lead, but does not show that 1.428 ms is redundant or assign it wholly to APT.

## Native evidence

`tools/audit_ui_boundary.py` reads the existing Open-BFME-2 reference at
`ff1544d9` and both local ROTWK/AotR executable images. It never runs native
code, modifies an executable, or outputs EA code/assets. Five complete
function bodies match uniquely under relocation masks in each image.
The APT wrapper's three direct child targets bind to the verified pre-draw,
core and post-draw bodies. Ten owned-memory body-alteration controls refuse
the changed code. Source/image/catalog hashes and resolved call targets are
in the [report](../benchmarks/2026-10-19-ui-boundary.json).

The confirmed boundaries show why a cache around the complete UI draw is
not qualified:

- APT PreDraw has two conditional `timeGetTime` call sites, updates its
  observed elapsed time and sets the UI animation clock. PostDraw restores
  the previous engine clock and resets rendering state.
- The APT core can invoke virtual update slot `0x28` and clears its dirty
  flag. A render call therefore is not a side-effect-free bitmap request.
- The UI wrapper calls pre-draw, per-view operations and window repainting.
  Reference children include placement/radius cursors, floating text and
  world animations. Skipping the wrapper can omit required visible work.
- The reference APT rendering path also has mask/transform changes and
  custom-control callbacks. The same native game update does not by itself
  establish identical UI output or unchanged callback state.

The full child graph and virtual targets are not all bound across versions.
The report explicitly distinguishes the five verified bodies and three
verified APT child bindings from the further reference-source leads. This
is sufficient to reject unconditional whole-boundary reuse; it does not
qualify an optimization below that boundary.

## Decision and next measurement

Do not cache or suppress complete RenderUI/APT calls, lower their refresh
rate, or install a beta based on treating the entire UI scope as saved time.
Preserve beta.18 and its settings as the local test baseline.

The next useful measurement separates the APT traversal/geometry submission,
custom controls and window repaint from UI updates and clock/state handling.
Any candidate must retain those side effects, use complete changing-input
keys and owned resources, preserve transparency/masks/order, and beat the
current path with capture/restoration and GPU completion included. The
existing full UI timing cannot establish which smaller boundary pays off.

## Reproduce

Run `python tools/audit_ui_boundary.py --image <game.dat> --image <game820.dat>
--capture <copied timing CSV> --output <report.json>`. The reference defaults
to the existing `C:/repositories/Open-BFME-2` checkout; never clone or modify
it. The captured interval is deliberately fixed for reproducibility and
must contain all 60 rows and matching UI-sample/extra counts. Output inside
the reference checkout or over an input is refused.

No game was launched, no plugin or installed setting changed, and this audit
establishes no new FPS result. It prevents reviving a superficially attractive
but unqualified whole-HUD cache while identifying the missing measurement.
