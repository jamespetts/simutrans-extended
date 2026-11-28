---
status: draft
verified: ex-15 @ ea93b6f8ef
---
# GUI

**Covers:** gui/ (incl. gui/components/), gui/simwin.*, gui/gui_theme.*, themes.src/,
simutrans/themes/ (assets); launch/menu glue simmenu/simticker/simmesg (root →
[simulation-core](simulation-core.md)); drawing primitives → [rendering](rendering.md).

Read when touching windows, frames, GUI components, themes, or any user-interface work; for
image-free dialog-geometry verification see [gui-layout-dump](gui/layout-dump.md), for the
rendered-pixel screenshot feedback loop see [gui-visual-feedback](gui/visual-feedback.md), and
for planned accessibility, first-run and explanation work see
[usability-considerations](gui/usability-considerations.md).

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
- `modal_dialogue` runs its own event loop and, when given a `karte_t*`, calls
  `karte_t::sync_step()` and `karte_t::step()` on that world. It does not call them after the
  modal window has been closed by an action in `check_pos_win`, while another window is open
  over the modal, or while the world is being destroyed or is already stepping. This keeps a
  new-map or load action from replacing the map in the middle of a step.
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

## Tooltips

- `tool_t::build_tooltip(tooltip_t&, const player_t*)` (simmenu.h) adds the lines of a
  tool's tooltip. The default implementation adds `get_tooltip(player)` as one line, so
  unconverted tools keep their existing text. `tool_build_way_t::build_tooltip`
  (simtool.cc) adds the name, way cost, forge cost, total, maintenance, speed, axle
  load, durability and restrictions of the way (a transport line such as a track or
  road) selected by the tool. `tool_build_bridge_t`, `tool_build_tunnel_t`,
  `tool_build_wayobj_t`, `tool_build_station_t`, `tool_build_depot_t`,
  `tool_build_pier_t`, `tool_build_pier_auto_t`, `tool_build_roadsign_t`,
  `tool_signalbox_t` and `tool_headquarter_t` override it with their own cost,
  maintenance and capability lines. `tool_build_pier_auto_t` is the elevated
  support tool; its tooltip adds the phrase "elevated support" to the pier name.
- `tooltip_t` (gui/tooltip.h) holds ordered `tooltip_line_t` values. Each line has a
  `tooltip_symbol_t`, an optional custom `image_id` and translated text. Helpers:
  `add`, `add_icon`, `add_value` and `add_money`. `add_money` takes the amount in
  Simucents and displays it as a positive amount ([glossary](glossary.md#money));
  `add_value` takes an integer and an optional unit. Each helper returns the appended
  line, so a caller can set the line's layout fields in the same statement.
- `tooltip_line_t` has three layout fields. `indent` is an indentation level; the
  renderer uses 24 pixels per level. `title` draws a full-width separator below the
  line and extra space after it. `total` draws half-width separators above and below
  the line and extra space on both sides. Capital costs use `indent = 1` for the
  component lines and `total = true` for the total, so the total is visibly a
  composite of the lines above it and is separated from the lines below it. This
  applies where a tooltip shows capital cost components and their total.
- `tooltip_t::draw` (gui/tooltip.cc) draws a speech bubble: a rounded filled body with
  a one-pixel border, a highlight line, a one-pixel shadow and a triangular tail
  pointing at the anchor. The tail is drawn on the side nearest the anchor. The
  renderer clamps the bubble to the screen, wraps text at spaces and aligns
  continuation lines under the text column. The renderer leaves three pixels between
  lines and six pixels after a title line. A total line has six pixels before it and
  twelve pixels after it when another line follows. The body uses
  `env_t::tooltip_color`, the border a darker blend, and the text
  `env_t::tooltip_textcolor`. The `modern.tab` and `theme-large.tab` theme tabs set
  `tooltip_background_color` to `#D6ECF7`, a light blue from the theme palette.
- `win_set_tooltip` has two overloads. The `tooltip_t` overload registers a delayed
  tooltip for the tool strip (the strip of tool icons drawn by `tool_selector_t`).
  The `const char*` overload wraps the text in a one-line `tooltip_t` and registers
  the same delayed form for button, label and other widget tooltips. A line created
  by the one-line form has `TOOLTIP_SYMBOL_NONE`, so its text starts at the left
  padding, and its tail points at the mouse position.
  `win_set_static_tooltip` accepts either form and registers the immediate tooltip
  used by the drag estimate (the floating cost box shown while dragging a way
  route); the static tooltip follows the mouse. `win_display_flush` (gui/simwin.cc)
  draws `tooltip_t::draw` output inside its existing clip; the delayed form also
  follows the delay and duration rules.
- Speed and weight lines show `Unlimited` when the descriptor value is 999 or more.
  `tooltip_add_limited_value` (simtool.cc) applies this rule to way, bridge, tunnel,
  way object, pier, elevated support and road sign tooltips.
- Tooltip maintenance lines use `tooltip_maint` through
  `tooltip_maintenance_label` (simtool.cc). `tooltip_maint` displays `Maintenance`;
  when the active language does not define it, the helper falls back to the
  `Maintenance` key. The `Maintenance` key itself stays paired with
  `Inf. maintenance` for the finance window and any other caller that uses it.
- `tool_build_way_t::mark_tiles` (simtool.cc) builds the drag estimate from
  `way_cost_breakdown_t`, returned by `way_builder_t::calc_costs` (bauer/wegbauer.cc).
  The breakdown has `way`, `forge`, `land`, `trees`, `objects`, `terraforming`,
  `structures` and `total` fields. The drag tooltip always shows way, forge, land and
  trees, and adds the other lines only when nonzero. The total is the same value used
  by the affordability check in `tool_build_way_t::do_work`. The bridge, tunnel, way
  object and road sign drag estimates use the same bubble; the bridge and road sign
  estimates build their own component lists because they do not use `calc_costs`.
- The line symbols that have dedicated theme images are theme `menu` images
  registered in `simskin.cc` as `TooltipCost`, `TooltipForge`,
  `TooltipMaintenance`, `TooltipSpeed`, `TooltipWeight`, `TooltipDurability`,
  `TooltipLand`, `TooltipTrees`, `TooltipStructures`, `TooltipRestriction`,
  `TooltipTotal` and `TooltipDistance`. Each theme source directory that has a
  skin `.dat` file contains `tooltip-symbols.png`, and that `.dat` maps the
  eleven names other than `TooltipDistance`, namely `TooltipCost`,
  `TooltipForge`, `TooltipMaintenance`, `TooltipSpeed`, `TooltipWeight`,
  `TooltipDurability`, `TooltipTrees`, `TooltipStructures`,
  `TooltipRestriction`, `TooltipTotal` and `TooltipLand`, to cells of the sheet.
  The cost symbol is a gold coin with a low-contrast cent sign. The restriction
  symbol is a red warning triangle; its visible area is slightly larger than the
  24-pixel round symbols, so it remains legible in a large theme.
  `TooltipDistance` falls back to `skinverwaltung_t::travel_time`,
  `TooltipSpeed` to `skinverwaltung_t::fastforwardsymbol`, `TooltipLand` to
  `skinverwaltung_t::intown`, `TooltipTotal` to
  `skinverwaltung_t::tooltip_cost` and `TooltipRestriction` to cell 2 of
  `skinverwaltung_t::alerts`; a theme that defines none of the images shows no
  symbol for that line. The time, passenger, mail, goods and power symbols use
  `skinverwaltung_t::service_frequency`, `skinverwaltung_t::passengers`,
  `skinverwaltung_t::mail`, `skinverwaltung_t::goods` and
  `skinverwaltung_t::electricity` respectively.

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

## Usability considerations

[usability-considerations](gui/usability-considerations.md) records accessibility, first-run and
explanation considerations: real toolbar scroll controls, translated settings labels and tooltips,
a visible route to the existing help text, a tutorial, a clickable balance, line-list and
new-game-dialogue controls, and the line-status legend. It also records which complaints from
the source usability report are already addressed in Extended.

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
