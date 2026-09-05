---
status: stub
verified: none
---
# GUI

**Covers:** gui/ (~290 files incl. gui/components/), gui/simwin.*, gui/gui_theme.*, themes.src/, simutrans/themes/ (assets), simmenu/simticker/simmesg (root → [simulation-core](simulation-core.md)), display coupling → [rendering](rendering.md).

## Seed facts

- Largest directory: ~290 files, ~67k lines (master) [CODE master @ 3b70dd4b3].
- Divergence is GUI-heavy: gui/ + gui/components/ ≈ 26% of master→ex-15 changed files [CODE].
- Hubs: `simwin.h` (79 includers), `gui_frame.h` (72), `components/gui_label.h` (70), `components/gui_button.h` (60), `components/action_listener.h` (56) [CODE ex-15 @ b06e8fa14].
- ex-15 GUI hotspots: `schedule_gui.cc` (102 commits), `consist_order_gui.cc` (92), `vehiclelist_frame.cc` (33), `convoi_detail_t.cc` (33), `gui_convoy_assembler.cc` (25) [CODE ex-15 @ b06e8fa14].

## Planned sections

- Window management: simwin, magic windows, window lifecycle/persistence (saved in savegames? verify → [savegame-versioning](savegame-versioning.md)).
- Frame & component patterns; the action_listener idiom.
- gui_theme & themes.src pipeline; skin descriptors (→ [data-and-pak](data-and-pak.md)).
- List frames/stats pattern (many *list_frame/*_stats pairs).
- ex-15 GUI work: schedule GUI, consist order GUI, convoy assembler → [ex-15](ex-15.md).
- Known-fragile areas (root artefact: "ex-15-failed-attempt-at-fixing-co-gui" branch as historical lead).

## Open questions

- L2 split warranted early (components/ vs. frames vs. lists)? Decide during breadth pass.
