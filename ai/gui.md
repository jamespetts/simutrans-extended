---
status: stub
verified: none
---
# GUI

**Covers:** gui/ (incl. gui/components/), gui/simwin.*, gui/gui_theme.*, themes.src/, simutrans/themes/ (assets), simmenu/simticker/simmesg (root → [simulation-core](simulation-core.md)), display coupling → [rendering](rendering.md).

## Seed facts

- Largest directory in the codebase, and the divergence between branches is GUI-heavy: gui/ + gui/components/ make up a large share of master→ex-15 changed files [CODE].
- Hubs: `simwin.h`, `gui_frame.h`, `components/gui_label.h`, `components/gui_button.h`, `components/action_listener.h` — all widely included [CODE].
- ex-15 GUI hotspots: `schedule_gui.cc`, `consist_order_gui.cc`, `vehiclelist_frame.cc`, `convoi_detail_t.cc`, `gui_convoy_assembler.cc` [CODE].

## Planned sections

- Window management: simwin, magic windows, window lifecycle/persistence (saved in savegames? verify → [savegame-versioning](savegame-versioning.md)).
- Frame & component patterns; the action_listener idiom.
- gui_theme & themes.src pipeline; skin descriptors (→ [data-and-pak](data-and-pak.md)).
- List frames/stats pattern (many *list_frame/*_stats pairs).
- ex-15 GUI work: schedule GUI, consist order GUI, convoy assembler → [ex-15](ex-15.md).
- Known-fragile areas (root artefact: "ex-15-failed-attempt-at-fixing-co-gui" branch as historical lead).

## Open questions

- L2 split warranted early (components/ vs. frames vs. lists)? Decide during breadth pass.
