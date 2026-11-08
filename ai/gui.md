---
status: draft
verified: ex-15 @ 8109f6dab (settings-tab convention; 2026-11-08)
---
# GUI

**Covers:** gui/ (incl. gui/components/), gui/simwin.*, gui/gui_theme.*, themes.src/,
simutrans/themes/ (assets); launch/menu glue simmenu/simticker/simmesg (root →
[simulation-core](simulation-core.md)); drawing primitives → [rendering](rendering.md).

Read when touching windows, frames, GUI components, themes, or any user-interface work; for
image-free dialog-geometry verification see [gui-layout-dump](gui/layout-dump.md), and for the
rendered-pixel screenshot feedback loop see [gui-visual-feedback](gui/visual-feedback.md).

## Component model

- `gui_component_t` (components/gui_component.h) is the base of every widget. It holds a
  parent-relative `pos` and `size`; flags `visible`, `rigid` (invisible but reserves space)
  and `focusable`; and virtual `draw(scr_coord offset)`, `infowin_event(const event_t*)`,
  `getroffen` (hit test), `get_min_size`/`get_max_size`, `get_client`, `is_marginless`.
  `draw` is pure virtual; `align_to` positions one component relative to another.
- Geometry is parent-relative: a component's absolute screen position is its `pos` plus every
  ancestor container's `pos` plus the window's screen position.

## Containers and table layout

- `gui_container_t` (components/gui_container.h) owns `vector_tpl<gui_component_t*> components`
  and tracks `comp_focus`.
- `gui_aligned_container_t` emulates an HTML table: `set_table_layout(columns,rows)`,
  `add_table`, `add_component(comp[,span])`, `end_table`, `take_component`/`new_component`
  (ownership), theme-derived margins/spacing, alignment flags, `force_equal_columns`.
  `set_size` derives column widths and row heights from children's min sizes, distributes
  extra space only into cells whose `get_max_size()` is unbounded, then recursively assigns
  each child's `pos`/`size`. Spanned cells use an internal `placeholder`.
- **Draw order is reverse child order.** `gui_container_t::draw` visits
  `components[n-1] … components[0]`, so **index 0 is drawn last (topmost)** and also receives
  mouse events first (`infowin_event` scans forward). There is no z-index field. The focused
  component is drawn last of all.
- **One component instance, one parent.** `add_component` stores the bare pointer and each
  parent overwrites the child's `pos`/`size` on resize, so adding the same widget (e.g. a
  shared note label) to two containers corrupts geometry in one view — use a separate
  instance per container [CODE ex-15 @ d6bf8f3e7: gui/prices_frame.cc `note_table`/`note_chart`].

## Frames, windows and widgets

- `gui_frame_t` (gui_frame.h) derives (protected) from `gui_aligned_container_t` and adds a
  title bar (`D_TITLEBAR_HEIGHT`), `windowsize`/`min_windowsize`, resize modes, owner colour,
  and `rdwr()`/`get_rdwr_id()` for persistence. Its own `pos` is set to `(0, titlebar height)`.
- The window manager is gui/simwin.*: `create_win`/`destroy_win`/`top_win`/`modal_dialogue`;
  `magic_numbers` identify singleton and per-player windows; `wintype` flags are `w_info`,
  `w_do_not_delete`, `w_no_overlap`, `w_time_delete`; `win_get_magic` finds an open window by
  id; `rdwr_all_win` persists savable windows.
- components/ holds the widget library: buttons, labels, checkboxes, combo boxes, scroll
  panes, tab panels, charts, list rows, the convoy assembler, and so on.

## Settings dialogue tabs

`settings_frame_t` (gui/settings_frame.cc, gui/settings_stats.*) has the base tabs
General, Display, Economy, Routing, Extended, Costs and Climate Control. The Extended
tab has sub-tabs General Ex. and Passengers. Extended-specific settings belong in the
Extended tab, not in the base tabs: General Ex. for world, physics and maintenance
settings, and Passengers for passenger generation, comfort, catering and related
revenue settings. The `INIT_*` and `READ_*` sequences are paired by type (numbers and
buttons have separate iterators), so adding a control requires the matching reader in
the same relative order.

## Actions

- Widgets derive from `gui_action_creator_t` and hold `action_listener_t*` listeners. On
  interaction they call `call_listeners(value_t)`, dispatching
  `action_triggered(creator, value)`; returning true stops further listeners. Frames implement
  `action_listener_t` to handle their own widgets. (components/gui_action_creator.h,
  components/action_listener.h)

## Theme

- `gui_theme_t` (gui_theme.h) holds static colours, element sizes, margins/spacing and the
  stretchable skin tiles. The `D_*` macros (`D_BUTTON_WIDTH`, `D_MARGIN_LEFT`, `D_H_SPACE`,
  `D_TITLEBAR_HEIGHT`, …) are the sanctioned way to size and position dialogue elements for a
  scalable interface; raw pixel constants are discouraged. `themes_init` reads `theme.tab`;
  `init_gui_defaults`/`init_gui_from_images` populate sizes from skin images.

## Tooling

- [gui-layout-dump](gui/layout-dump.md) — read when verifying dialog geometry (positions,
  overlap, clipping, z-order) without a rendered image. Design; not yet implemented.
- [gui-visual-feedback](gui/visual-feedback.md) — read when iterating dialog design with real
  screenshots: `SIMUTRANS_UI_AUTOMATION` build gates, env vars, run recipe, loop procedure.

## Window persistence (save/restore)

- `rdwr_all_win(loadsave_t*)` (gui/simwin.cc) saves and restores open windows. It runs at the
  very end of `karte_t::rdwr` (after `active_player_nr`) and is NOT part of
  `karte_t::rdwr_gamestate`: window state is client-local UI state and never enters the desync
  hash. The load side runs only when `env_t::restore_UI` is set (karte_t::load) [CODE].
- A window is saved iff its `gui_frame_t::get_rdwr_id()` returns anything but `magic_reserved`.
  Record layout: id, position, `wintype` byte, sticky/locked/rollup flags, then the window's own
  `rdwr()` (its window size and content). Records are written in `wins` order (z-order) and
  terminated by `magic_none`. The block is gated at Extended 15; a record may carry its own
  revision gate where its content changed — such records are skipped entirely for older save
  targets (example: the consist-order window and rule editor records) [CODE].
- Restore (the load side of `rdwr_all_win`) switches on the id: each case default-constructs the
  window, `create_win` places it, then `w->rdwr(file)` fills it. `rdwr()` may create its own real
  window and destroy the dummy it was called on (the `magic_schedule_rdwr_dummy` /
  `magic_line_schedule_rdwr_dummy` pattern); the sticky/rollup flags are then applied to the
  window created last [CODE].
- Two magic styles. A window keyed by a live-object pointer (convoy/line schedule windows, keyed
  `(ptrdiff_t)cnv->get_schedule()` / `(ptrdiff_t)line.get_rep()`) serialises its target handle
  (`convoi_t::rdwr_convoihandle_t` / `simline_t::rdwr_linehandle_t`) and re-binds after loading.
  A static singleton serialises under a reserved "rdwr dummy" id
  (`magic_consist_order_rdwr_dummy`, "only used to load/save") and re-keys the restored instance
  with `win_set_magic`, or directly under its live id (the rule editor) [CODE].
- Restore runs strictly in save order, so a window whose `rdwr()` binds to another window's
  state must be saved after its target: `rdwr_all_win` writes the consist-order window record
  (which binds to its owner schedule window's editing copy) and the rule editor record (which
  binds to the consist-order window's working order) after all other windows, irrespective of
  z-order [CODE].
- Failure policy on restore: when the owner window, entry or halt cannot be resolved, the
  window destroys itself with a `dbg->error` (drop-on-failure) rather than half-binding. Note
  `destroy_win` deletes synchronously outside event handling and defers via the kill list inside
  event handling/draw, so a self-destroying `rdwr()`/`update()` must touch nothing afterwards [CODE].
- Commit-path rule: windows edit working copies (the schedule GUIs' editing copies, the
  consist-order window's working `consist_order_t`); only the commit path sends tools, and
  restore must never mutate synced state → [sync-and-determinism](sync-and-determinism.md) [CODE].
- Separate mechanism: `rdwr_win_settings` (gui/simwin.cc, called from the environment settings)
  keeps a window position/size memory keyed by `guess_magic_number`; that is settings.xml state,
  not savegame state [CODE].

## Open questions

- Cross-window stacking, draw order and hit-testing in gui/simwin.* — document.
- Conventions of the list-frame/*_stats pattern (many `*list_frame_t` + `*_stats_t` pairs).
- themes.src → skin_desc pipeline (→ [data-and-pak](data-and-pak.md)).
- Is a subfolder split warranted (components vs. frames vs. lists)? Decide during a breadth pass.
