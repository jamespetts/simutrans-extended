---
status: draft
verified: master @ d6717f8f1
---
# GUI

**Covers:** gui/ (incl. gui/components/), gui/simwin.*, gui/gui_theme.*, themes.src/,
simutrans/themes/ (assets); launch/menu glue simmenu/simticker/simmesg (root →
[simulation-core](simulation-core.md)); drawing primitives → [rendering](rendering.md).

Read when touching windows, frames, GUI components, themes, or any user-interface work; for
image-free dialog-geometry verification see [gui-layout-dump](gui/layout-dump.md).

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
  overlap, clipping, z-order) without a rendered image.

## Open questions

- Which windows are saved/restored (which `get_rdwr_id` values are non-reserved), and how that
  interacts with savegame versioning? Verify against gui/simwin.* → [savegame-versioning](savegame-versioning.md).
- Cross-window stacking, draw order and hit-testing in gui/simwin.* — document.
- Conventions of the list-frame/*_stats pattern (many `*list_frame_t` + `*_stats_t` pairs).
- themes.src → skin_desc pipeline (→ [data-and-pak](data-and-pak.md)).
- Is a subfolder split warranted (components vs. frames vs. lists)? Decide during a breadth pass.
