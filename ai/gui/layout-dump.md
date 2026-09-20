---
status: draft
verified: master @ d6717f8f1
---
# GUI layout dump diagnostic

**Covers:** temporary TEST-only instrumentation of `gui/components/gui_container.cc` and
`gui/gui_frame.cc` that emits a text layout report; scratch output under `ai/temp/`.
Parent topic: [gui](../gui.md).

Use this when a text-only (non-vision) model must reason about GUI geometry during
dialog/widget work — what sits where, what overlaps what, what is clipped, and the z-order —
or when a deterministic, greppable alternative to screenshots is needed for layout
verification.

## Why draw-based, not a tree walk

The widget tree is not held in one place: composites such as `gui_scrollpane_t` (holds a
child `comp`) and `gui_tab_panel_t` (holds a tab list) own children without being
`gui_container_t`s [CODE master @ d6717f8f1]. Instrumenting `gui_container_t::draw` captures
every composite's children for free, and its traversal order is the true draw order.

## Geometry it relies on

The tool only reports the geometry defined in [gui](../gui.md): components hold
parent-relative `pos`/`size`; the absolute position is the sum of ancestor `pos` values plus
the window's screen position; and draw/event order follows reverse child order (index 0 is
topmost). The tool must not redefine these.

## Design (temporary; not yet implemented)

- Enable via env var `SIMUTRANS_GUI_LAYOUT_DUMP=<path>`; optional
  `SIMUTRANS_GUI_LAYOUT_DUMP_N=<count>` (default 5) limits how many window draws are dumped.
- In `gui_container_t::draw`: file-static state (path, `FILE*`, recursion depth, blocks
  remaining, recorded rects). At depth 0, open the file once and begin a block. While
  dumping, bypass the clip early-continue so clipped/off-screen children are still visited.
  Print one record per visible child immediately before its `c->draw(screen_pos)`;
  increment/decrement depth around the recursive call. When depth returns to 0, emit the
  summary and decrement the block counter.
- In `gui_frame_t::draw`: emit a `WINDOW name=... rect=...` header and seed the parent rect
  used for clipping checks.
- Component type names come from `typeid(*c).name()`.

Output line:
`D<depth> #<draw_index> type=<name> abs=(x y w h) min=(w h) max=(w h) vis= rigid= focusable= marginless= table=<c>x<r> focus=<0|1>`

Summary sections: `WINDOW`, `OVERLAPS n` then `OVERLAP #a<->#b area= sibling=<0|1>`, and
`CLIP #i exceeds parent`.

## Procedure

1. Add the TEST-marked instrumentation; build the SDL client.
2. Set the env var (path under `ai/temp/`), launch with a fixture savegame and pakset, open
   the target dialog(s).
3. Read the dump; reason from the rectangles, hierarchy, and overlap/clip report.
4. Revert all instrumentation: AGENTS.md hard rule 9 requires testing-only changes to carry a
   `TEST` comment and to be removed before finishing.

## Limitations

- RTTI must be enabled for `typeid` names; no `-fno-rtti` is set in the current build files
  [CODE master @ d6717f8f1].
- Only on-screen content of conditionally-drawn composites is present (e.g. `gui_tab_panel_t`
  draws only its active tab).
- The focused component is drawn separately at the end of `gui_container_t::draw`, so its
  output position differs.
- It does not open dialogs itself; the user still opens the dialog under inspection.

## Open questions

- Is RTTI enabled on every supported toolchain and backend? Verify before relying on
  `typeid` names.
