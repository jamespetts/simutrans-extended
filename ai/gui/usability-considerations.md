---
status: draft
verified: ex-15 @ 97029f453
---
# Interface usability considerations

**Covers:** the shared interface code named in the entries below (`gui/tool_selector.*`,
`gui/settings_stats.*`, `gui/welt.*`, `gui/simwin.*`, `gui/schedule_list.*`, `gui/line_item.*`,
`gui/halt_info.*`, `gui/components/gui_numberinput.*`, `gui/components/gui_label.*`,
`gui/components/gui_chart.*`, `gui/components/gui_convoy_assembler.*`), the supporting
`simline.*`, `simmenu.cc`, `simtool-dialogs.h`, `simworld.cc`, `simdepot.cc` and
`dataobj/environment.*`, and the `pak128.Britain-Ex` files that define the British pakset's
toolbars and object lists. No entry is implemented.

Read when planning first-run, learning, discoverability or interface-explanation work, or
when triaging a usability report.

## Source and scope

This list comes from a transcript of a YouTube livestream in which Master Hellish played
Simutrans-Standard with the Britain pakset (the `pak128.Britain-Ex` data set in `simutrans/`).
The transcript is undated; it refers to Farming Simulator 22. It is a third-party usability
report, not a defect report: the speaker is an experienced transport-game player, and the
session lasted about three hours. The transcript is the source for the observations below and
is not reproduced here.

The speaker's conclusion was that almost every problem he found was interface, not simulation,
and that the game was difficult to start rather than difficult to master. This document therefore
covers presentation and discoverability only. It does not propose changes to balance, economics
or simulation.

Simutrans-Extended is a fork of Standard Simutrans ([project-overview](../project-overview.md)),
and its version constants track base 123.2 (`simversion.h`). The GUI code named here is inherited
from Standard. Each entry states whether it is a shared fix, which belongs on `master`
first and is then merged into the active development branch, or a new feature, which belongs on
the active development branch, following the branch rule in [AGENTS.md](../../AGENTS.md).

## Already addressed in Extended

These complaints from the transcript do not need work; the current Extended code already
provides the described behaviour. Verified against the checkout named in the frontmatter.

| Complaint | Current behaviour |
|---|---|
| No preview of the map before starting a game | `welt_gui_t` creates a `gui_map_preview_t` (`map_preview`) and `welt_gui_t::update_preview` fills it from the map's height data. |
| The line management window is a bare list with no useful columns | `schedule_list_gui_t` provides line selection, convoy counts, load factors (the proportion of vehicle capacity in use), per-line finance history, sort modes and per-line charts. `line_scrollitem_t::draw` adds status colours and status icons. |
| Charts run right-to-left | `env_t::left_to_right_graphs` defaults to true (`dataobj/environment.cc`) and `gui_chart_t::draw` uses it. |
| Waiting passengers are shown as `+ 0 - 172` with no legend | `halt_info_t::update_components` uses `skinverwaltung_t::pax_evaluation_icons` with tooltips. |
| No cargo filter when choosing wagons, and no way to copy a convoy | `gui_convoy_assembler_t` filters by goods category and name, and `depot_frame_t` has a copy-convoy control. |
| "Income" mixes revenue and operating cost | `line_cost_t` separates `LINE_REVENUE`, `LINE_OPERATIONS`, `LINE_PROFIT`, `LINE_REFUNDS` and `LINE_WAYTOLL`. |
| An invalid schedule gives no warning | `depot_frame_t::action_triggered` checks `schedule_t::is_editing_finished` and displays a warning before starting a convoy. |

## Proposed changes

### Make the toolbar scroll controls real

**Current behaviour.** `tool_selector_t::draw` draws `gui_theme_t::arrow_button_left_img`,
`arrow_button_right_img`, `arrow_button_up_img` and `arrow_button_down_img` at the edges when
the tools do not fit. `tool_selector_t::infowin_event` handles dragging and icon selection but
does not test the arrow rectangles, so a click on an arrow selects the icon underneath it. The
function also does not handle `IS_WHEELUP` or `IS_WHEELDOWN`.

**Proposed change.** Test the arrow rectangles before the icon hit test and move
`tool_icon_disp_start` by one icon per click. Handle the two wheel events by the same amount.
Keep the existing drag behaviour.

**Evidence.** After selecting a theme with larger icons, the speaker lost toolbar buttons off the
edge of the screen and did not know the toolbar could scroll. He found the drag behaviour by
accident. The arrows are the only visible indication that the toolbar can scroll, and they do
not respond.

**Priority and effort.** High / small. Shared fix: `master` first. Also recorded as a priority 3
bug in [known-bugs](../known-bugs.md).

### Translate and explain settings labels

**Current behaviour.** The `INIT_NUM`, `INIT_BOOL`, `INIT_COST`, `INIT_LB` and `INIT_COLOR`
macros in `gui/settings_stats.h` create a label and call `gui_label_t::set_text_pointer` with a
configuration key such as `frames_per_second` or `max_route_steps`. `gui_label_t::set_text_pointer`
does not translate; only `gui_label_t::set_text` does. Many of these keys have no entry in
`simutrans/text/en.tab` or `simutrans/pak128.Britain-Ex/text/en.tab`, so the settings dialogue
displays the key itself. A number input adds only the generic "enter a value between %i and %i"
tooltip from `gui_numberinput_t::draw`.

**Proposed change.** Make the macros call `gui_label_t::set_text` and add human-readable English
entries for each key to `simutrans/text/en.tab`, translating the entries through the Simutranslator
workflow described in [translations](../translations.md). Add a tooltip parameter to the macros
for settings whose meaning is not obvious from the label. Prioritise the General and Extended
tabs and the new-game dialogue.

**Evidence.** The speaker repeatedly says the tooltips are useless and give only a range, and
that settings assume knowledge the player does not have.

**Priority and effort.** High / medium: the code change is small, but the translation and
tooltip writing are substantial and mechanical. Shared fix: `master` first.

### Put the existing help behind a visible control

**Current behaviour.** `welt_gui_t::get_help_filename` returns `new_world.txt`.
`simutrans/text/en/new_world.txt` explains map number, map size, median citizens per city,
tourist attractions, the timeline and the other fields in the new-game dialogue. The help is
reachable through the title-bar help button, which `create_win` enables when
`gui_frame_t::get_help_filename` returns a file name, or by pressing F1 through
`interaction_t::interactive_event`. The new-game dialogue has no visible "Help" control.

**Proposed change.** Add a visible "Help" button to `welt_gui_t` that calls
`help_frame_t::open_help_on("new_world.txt")`. Consider opening the same page on first run.

**Evidence.** The speaker asks what the map settings mean and says the tooltips assume knowledge.
He never finds the help page.

**Priority and effort.** High / small. New feature: active development branch.

### Provide a tutorial or getting-started scenario

**Current behaviour.** The tutorial button is not constructed; the code for it in
`banner_t::banner_t` is commented out. The Britain pakset contains no tutorial scenario. The
speaker reports that the Standard tutorial was broken when he tried it: text was missing and it
did not appear playable.

**Proposed change.** Provide a short tutorial scenario for the Britain pakset, and either
re-enable the tutorial button in `banner_t::banner_t` or add a "Getting started" control that
opens an introductory help page. The scenario should cover building a road stop (a bus stop or
lorry stop), buying a vehicle, creating a line, and reading the line window.

**Evidence.** The speaker's central complaint is the difficulty of learning the interface; he
says the game is difficult to start rather than difficult to master.

**Priority and effort.** High / large. New feature and content: active development branch.

### Make the status-bar balance open the finances window

**Current behaviour.** `win_display_flush` draws the active player's name and balance as text in
the status bar. There is no hit test, so clicking the balance does nothing.

**Proposed change.** Hit-test the balance text in the main mouse event path and open
`money_frame_t` for the active player when it is clicked.

**Evidence.** The speaker clicks the balance twice and expects the finances window.

**Priority and effort.** Medium / small. New feature: active development branch.

### Suppress the repeated player-activation message

**Current behaviour.** `karte_t::switch_active_player` has no early return when the requested
player is already active. Selecting the current company therefore emits "Now active as %s" and
re-runs `exit()` and `init()` on the currently selected tool.

**Proposed change.** Return early when the resolved player equals `active_player_nr`, leaving the
selected tool and the message log unchanged.

**Evidence.** The speaker selects his own company and sees a message of the form
"Now active as <player name>" with his own name, then asks what he was before.

**Priority and effort.** Medium / small. Shared fix: `master` first. Also recorded as a priority 4
bug in [known-bugs](../known-bugs.md).

### Add the line list to the list-of-lists dialogues

**Current behaviour.** `simtool-dialogs.h` defines `dialog_list_halt_t`, `dialog_list_convoi_t`,
`dialog_list_depot_t`, `dialog_list_vehicle_t`, `dialog_list_signalbox_t`, `dialog_list_town_t`,
`dialog_list_goods_t`, `dialog_list_factory_t`, `dialog_list_curiosity_t` and
`dialog_list_label_t`, but no line-list dialogue. The Britain pakset's `LISTTOOLS` sub-toolbar
(`toolbar[13]` in `simutrans/pak128.Britain-Ex/config/menuconf.tab`) contains only the existing
dialogues.

**Proposed change.** Add a `dialog_list_line_t` that opens `schedule_list_gui_t` for the active
player, register it in `tool_t::read_menu`, and add it to the pakset's list toolbar.

**Evidence.** The speaker opens the list-of-lists and asks why the line list is not part of it.

**Priority and effort.** Medium / small for the engine, plus a pakset toolbar edit. New feature:
active development branch.

### Explain the pin control

**Current behaviour.** The title-bar pin toggles the `sticky` flag in `check_pos_win`
(`gui/simwin.cc`). A sticky window cannot be dragged (`check_pos_win` skips the drag path) and
survives `destroy_all_win(false)`, which is the path used by Backspace. The control has no
tooltip.

**Proposed change.** Give the control a tooltip that states both effects, for example "Lock
window position and keep the window when Backspace closes others".

**Evidence.** The speaker pins a window, finds that it still closes when another window opens,
and says he does not know what the pin does.

**Priority and effort.** Low / small. New feature: active development branch.

### Identify the failing convoy in the start message

**Current behaviour.** `depot_t::start_convoi` displays the translation of
"Noch kein Fahrzeug mit Fahrplan vorhanden", which reads "Give the Vehicle(s) (a) schedule(s),
before giving it(them) the go order!". The message does not name the convoy or its line, and
does not distinguish "no line assigned" from "line assigned but its schedule is incomplete".

**Proposed change.** Name the convoy and, where a line is assigned, the line. State the next
action: open the line management window and add stops, or open the convoy's individual schedule.
Keep the message translatable through a single `translator::translate` call.

**Evidence.** The speaker assigns a line, tries to start the convoy, receives the generic message
and cannot tell why the game refuses.

**Priority and effort.** Medium / small. Shared fix: `master` first.

### Add a reset control to the new-game dialogue

**Current behaviour.** `welt_gui_t` edits the `settings_t` object passed to its constructor;
`banner_t::banner_t` passes `&env_t::default_settings`. Once a field is changed, the previous
value is not retained. The settings dialogue has a "Simuconf.tab" control
(`settings_frame_t::action_triggered`, `revert_to_default`) that re-reads the configuration
files and replaces the whole settings object, but the new-game dialogue has no equivalent, and
no control in it is labelled as restoring defaults.

**Proposed change.** Add a clearly labelled reset control to the new-game dialogue. Decide
whether it restores only the map-generation fields shown in the dialogue or all settings, and
document the choice. The reparse path in `settings_frame_t::action_triggered` can be reused if
the wider scope is chosen.

**Evidence.** The speaker changes the city and industry counts, cannot restore the defaults, and
says that a control labelled "Default" in the Standard interface does not reset the new-game
settings.

**Priority and effort.** Medium / small. New feature: active development branch.

### Explain line colours and status icons

**Current behaviour.** `simline_t::recalc_status` sets `state` and `state_color`.
`line_scrollitem_t::draw` uses `simline_t::get_state_color` for the row text and draws status
icons with tooltips. The colour meanings exist only in a comment above `simline_t::recalc_status`;
the line management window has no legend.

**Proposed change.** Add a legend or a row tooltip that states what each colour and icon means,
including "last month's income was negative", "no convoys", "no vehicle has moved", "some
vehicles are overcrowded" and "at least one vehicle is obsolete".

**Evidence.** The speaker asks whether the list is colour-coded by profitability and cannot tell
what the colours mean.

**Priority and effort.** Medium / small. New feature: active development branch.

## Pakset contributions

These are data changes in `simutrans/pak128.Britain-Ex`, not engine changes, and should be
coordinated with the pakset maintainer.

- Regroup `toolbar[8]` in `config/menuconf.tab`. `toolbar[8]` currently expands the `ways(1,0)`
  and `ways(1,1)` menu configuration directives into a single undivided list of every road type,
  followed by bridges, piers, tunnels, signs, stops and depots. The engine supports nested
  toolbars through `toolbar_t` and `tool_t::read_menu`; separate sub-toolbars for road types,
  stops, depots and signs would reduce the list length.
- Distinguish or remove duplicate-looking station extension buildings, such as the two bus
  terminals that the speaker cannot tell apart.
- Provide sound effects for the pakset, or state in the pakset documentation that it has none.

## Longer-term options

- Add a search field or a basic/advanced filter to the settings dialogue. The dialogue now has
  several tabs and a large number of controls; the speaker says he does not remember enabling
  the setting that produced the large object list. This is a design change, not a small fix, and
  is not scheduled.
- Make the main toolbar wrap to more than one row instead of scrolling. `tool_selector_t`
  already supports multiple rows for secondary toolbars through `env_t::toolbar_max_width` and
  the `allow_break` parameter of `tool_selector_t::add_tool`, but the main toolbar is forced to
  one row.

## Open questions

- Is the toolbar arrow defect also present in current Standard Simutrans, or has Standard
  changed the control since the version used in the transcript? Only the Extended checkout has
  been verified here.
- Should autosave default to a non-zero interval for new installations? `env_t::autosave`
  exists but defaults to 0, so the speaker's complaint that the game does not autosave remains
  true by default. Enabling it writes files without an explicit player action, so this is a
  policy decision.
- Which fields should the new-game reset control restore, and from which source: a snapshot
  taken when the dialogue opens, or a fresh read of `simuconf.tab`?
- Should the main toolbar wrap rather than scroll, given the reduction in map viewport height?
- Does the pakset maintainer want the road toolbar regrouped, given that it changes icon
  positions that existing players may have learned?
