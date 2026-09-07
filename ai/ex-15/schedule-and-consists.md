---
status: reviewed
verified: ex-15 @ 91d9b252e
---
# ex-15 feature status: schedules, consists & re-combination

**Covers:** dataobj/schedule.{cc,h}, dataobj/schedule_entry.h, dataobj/consist_order_t.{cc,h},
gui/schedule_gui.*, gui/consist_order_gui.*, gui/components/gui_schedule_item.*, simconvoi.cc
(schedule/consist logic), path_explorer.{cc,h}, simhalt.cc (layover registry).

Per-feature registry for the 15.x programme. Design intent sources: forum thread 17852 (post URLs
inline); parent doc: [ex-15](../ex-15.md). Untagged claims are [CODE] at the frontmatter value.

## Data structures — IMPLEMENTED (deviates from the 2018 plan)

- `schedule_entry_t` (dataobj/schedule_entry.h): `pos`; `flags` (uint32, enum `schedule_entry_flag`);
  `minimum_loading` (uint16 — doubles as depot-command carrier via `depot_flag`: delete_entry /
  store / maintain_or_overhaul); `spacing_shift`; `unique_entry_id` (uint16, stable identity via
  `schedule_t::get_next_free_unique_id`); `condition_bitfield_broadcaster` / `_receiver`;
  `target_id_condition_trigger` / `target_id_couple` / `target_id_uncouple`;
  `target_unique_entry_uncouple`; `waiting_time_shift`; `reverse`; `max_speed_kmh` (per-entry speed
  limit, 65535 = none).
- Flags: wait_for_time, lay_over, ignore_choose, force_range_stop, conditional_depart_before_wait,
  conditional_depart_after_wait, conditional_skip, send_trigger, cond_trigger_is_line_or_cnv,
  clear_stored_triggers_on_dep, trigger_one_only, couple, uncouple, couple_target_is_line_or_cnv,
  uncouple_target_is_line_or_cnv, uncouple_target_sch_is_reversed, discharge_payload,
  set_down_only, pick_up_only.
- Deviations from the 2018 design
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170109.html,
  https://forum.simutrans.com/index.php/topic,17852.msg193643.html]: `unique_entry_id` replaces the
  planned 8-bit target-entry index; flags widened to 32-bit; loading-control flags added (Dec 2020 /
  Jan 2023 revisions); consist orders live on `schedule_t::orders` (inthashtable keyed by
  unique_entry_id) rather than line/convoy level — entries are copied by value, heavyweight orders
  are kept apart; the planned whole-schedule "list of referring schedules" does NOT exist.
- Savegame/network: all new entry/order data gated on extended ≥ 15 (`schedule_t::rdwr`,
  `consist_order_t::rdwr`, `haltestelle_t::rdwr` laid_over list, `convoi_t::rdwr`
  conditions_bitfield). Network path = schedule string serialisation
  (`sprintf_schedule`/`sscanf_schedule`, including orders) via tool_change_convoi/line/depot;
  no dedicated network command for consist orders.

## IMPLEMENTED features

- **Per-entry speed limit** — `max_speed_kmh`, enforced in `convoi_t::calc_move`; GUI controls
  (bt_speed_limit/numimp_speed_limit). Forum reported an inverted-UI bug Jul 2022
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg200904.html] — re-verify.
- **Layover** — flag + `convoi_t::LAYOVER` state + enter/exit_layover; requires a stop with a
  `layover_enable` building (`haltestelle_t::can_lay_over`, building_desc FLAG_LAYOVER_ENABLE);
  halt-side `laid_over` registry; `min_layover_overhead_seconds` setting; entering layover forces
  full discharge; salary time discounted (`last_salary_point_ticks`). GUI control gated on
  can_lay_over; "[LO]" stop-name prefix. Gaps: mail/goods aboard at layover (open in forum
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193643.html]); no warning UI when
  facilities are removed after a schedule is set.
- **Conditional skip** — depot entries skipped when `!convoi_t::is_maintenance_needed()`; non-depot
  entries skipped when loading level is 0 (`convoi_t::advance_schedule`). Set only programmatically
  (auto-appended home-depot entry in `create_schedule`; tool_generic 'l'); no GUI control.
- **Ignore choose sign** — `rail_vehicle_t::activate_choose_signal`, `road_vehicle_t::hop_check`;
  GUI control + "[IC]" prefix + skin.
- **Trigger runtime** — `convoi_t::AWAITING_TRIGGER` state; receiver bitfield matching in the step
  logic; `simline_t::propagate_triggers` (with trigger_one_only: earliest-arriving satisfied convoy
  first); clear_stored_triggers_on_dep handling. GUI sets only the bitfield numbers (inputs select
  condition numbers 0–15 — the 16 designed conditions) and a line selector.
- **Loading-control flags** (discharge_payload / set_down_only / pick_up_only) — enforced in
  `hat_gehalten`/unload paths and in the path explorer. Added Jan 2023
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202997.html].
- **Depot commands in schedule entries** (depot_flag) — executed in `depot_t::convoi_arrived`.
- **Spacing/timing rework** — `schedule_t::spacing` in 12ths of departures/month; per-entry
  spacing_shift; departure-slot queueing (`haltestelle_t::get_queue_pos`); whole-schedule ETA/ETD
  projection (`convoi_t::book_departure_time`).
- **Times history / departure boards** — `times_history_map` (TIMES_HISTORY_SIZE 3),
  gui_times_history_t (contributor: suitougreentea). Post-2018-plan addition.
- **Path-explorer consist-order integration** — `schedule_t::parse_orders` derives
  catg/min-class carried-to/from tables; `compartment_t::schedule_flags` fragmentation with
  "invalid zones" across consist-order points; extra transfer time after layover/discharge stops.
  Within a single schedule only (see ABSENT below).

## PARTIAL features

- **Couple (joining)** — data + GUI target selection (couple_target_selector, line/convoy modes) +
  `schedule_t::get_couple_target` + absorption: the arriving convoy draws vehicles from the joining
  convoy or the halt's laid-over pool via `convoi_t::process_consist_order`; an emptied joining
  convoy self-destructs. Missing: nothing sets the `couple` flag (GUI sets only target IDs), so the
  path is unreachable in play; no two-party synchronisation; failure is silently ignored
  (`// TODO: Consider what happens if we fail` in `hat_gehalten`); no waiting/retry when the target
  has not arrived.
- **Consist orders** — data model: `consist_order_t` (end-state for one entry, keyed by
  unique_entry_id; exactly one order per entry
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202940.html]) →
  `consist_order_element_t` (slot: catg_index, tag fields, prioritised alternatives) →
  `vehicle_description_element` (a specific vehicle, or rules: engine_type, min/max range,
  catering, classes, power, tractive effort, topspeed, weight, axle load, capacity, running/fixed
  cost, fuel, staff, drivers; plus rule_flags preferences). Hierarchy per
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202971.html]. Solver:
  `convoi_t::process_consist_order` (single-pass heuristic matching with one pass-over retry;
  in-code FIXME: the GUI stores slots in the wrong container shape) +
  `commit_recombined_consist` (add/remove/substitute/move vehicles; displaced vehicles force-unloaded
  then collected into a new layover convoy). Matching: `vehicle_t::matches_consist_order_element`
  (rules evaluated except preferences; enforces can_lead/can_follow). SHUNTING state with
  `shunting_time_seconds` setting. GUI `consist_order_frame_t`: order overview, consist copier
  (`set_convoy_order`, reversed-consist handling), vehicle picker with filters — INCOMPLETE: slot
  reorder buttons stubbed, no rule-based-alternative controls (specific vehicles only), no tag
  controls, "slot may be empty" commented out, window rdwr disabled, opens only from line schedule
  windows. Consist-order editing for lineless convoys is intended but unbuilt (the runtime fully
  supports convoy-level orders) [RECOLLECTION:2026-09-07]. rule_flags preferences serialised but
  never evaluated; keep-or-remove undecided [RECOLLECTION:2026-09-07]. `max_catering` omitted from
  string serialisation (network/tool round-trip resets it). Livery copying in set_convoy_order
  explicitly unsupported.
- **Defect (user-confirmed [RECOLLECTION:2026-09-07])**: a convoy completing a consist order goes
  SHUNTING → ROUTING_1 → advance_schedule without passing `check_departure` — it departs
  immediately after the shunting delay, ignoring minimum loading and spacing slots
  → [known-bugs](../known-bugs.md).
- **Left-over/loose vehicles** — displaced vehicles become a new layover convoy in situ (blank
  route, reserves own tiles, empty schedule, no line); pool at stops via
  `haltestelle_t::get_laid_over`, consumed by process_consist_order; mothballed vehicles excluded.
  Gaps: no GUI listing of vehicles available to couple; emptied layover convoys linger until
  `new_month` self-destruct; SHUNTING can overwrite LAYOVER without deregistering (stale
  laid_over entry); `// TODO: Add code to find a suitable home depot`.
- **Trigger/couple GUI wiring** — no setters exist for send_trigger,
  conditional_depart_before/after_wait, cond_trigger_is_line_or_cnv, clear_stored_triggers_on_dep,
  trigger_one_only, couple, uncouple → those runtime paths are unreachable in normal play. All are
  still planned for 15.x, awaiting GUI wiring [RECOLLECTION:2026-09-07]; gui/schedule_gui.h carries
  a "UI TODO" for exactly these. Polarity defect → [known-bugs](../known-bugs.md).
- **Range stops & replenishment** — range checks exist (`convoi_t::min_range`/calc_min_range,
  OUT_OF_RANGE in prepare_for_routing, depot-finding range check). Replenishment pieces exist but
  are disconnected: `vehicle_t::replenish()` (REPLENISHING state, replenishment_seconds .dat),
  building `replenish_enable` flag, `km_since_last_replenish`, replace_data on_replenish — no
  callers/triggers anywhere; force_range_stop flag is display-only ("[RS]", img_refuel). In scope
  for 15.x: the connecting logic is outstanding, and more sophisticated integration with
  re-combination/consist orders is likely needed [RECOLLECTION:2026-09-07]. Range-stop algorithm
  designed [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193779.html]; dynamic
  (fuel-consumption-derived) range rejected
  [FORUM:https://forum.simutrans.com/index.php/topic,14991.msg193647.html].

## ABSENT — core outstanding 15.x work [RECOLLECTION:2026-09-07]

- **Uncouple/dividing runtime** — `// TODO: Implement logic for dividing` in `hat_gehalten`;
  `get_uncouple_target` absent (`// TODO` in schedule.h); target_id_uncouple /
  target_unique_entry_uncouple / uncouple_target_sch_is_reversed are stored, GUI-editable,
  serialised and compared in `schedule_t::matches`, but never read at runtime. The only splitting
  is incidental (consist-order displaced vehicles → layover convoy, no schedule).
- **Schedule adoption/concatenation for divided portions** — no mechanism sets a split-off
  portion's schedule to the target line/convoy schedule at the target entry.
- **Y/H-shaped path-explorer traversal** — the explorer never reads couple/uncouple targets;
  connexions are computed within one schedule only (linear/cyclic + mirrored). Journeys through a
  join/split cannot be discovered or time-estimated.
- **Two-party timed coupling; dead-lock avoidance** — no arrival synchronisation, no validation of
  couple/uncouple target graphs (mutual waits, circular targets, platform blockage).
- **Freight/passenger continuity across splits/joins** — displaced vehicles are force-unloaded at
  the stop; no transfer of cargo between portions, no re-routing correction for cargo on moved
  vehicles ("passengers stay on the through portion" is not modelled).
- **Call-on-based coupling movements into occupied platforms** — call-on signalling exists for
  other purposes but is not connected to coupling; SHUNTING is only a timed pause.
- **Vehicle-tag enforcement** — `vehicle_t::tags` and the order tag fields are serialised-only;
  zero runtime callers, no GUI.
- **Whole-schedule referring list** (warn players editing a schedule others point to) — absent.

## Most difficult outstanding work (assessment, user-confirmed scope)

1. Dividing runtime with schedule hand-over: create a convoy mid-stop from a live consist, assign
   the target schedule at the right entry (incl. the reversed case), re-reserve tiles for both
   portions, keep signalling/reservations consistent, network-deterministically.
2. Y/H path-explorer traversal with journey times across concatenated schedules.
3. Two-party timed coupling (probably via the existing trigger system), failure behaviour,
   dead-lock detection/avoidance.
4. Freight continuity across splits/joins (fares/apportionment in unload_cargo/load_cargo,
   ware_t routing data).
5. Platform occupancy by dormant consists (home-depot finding, sidings, call-on movements).
6. Solver completeness + GUI/solver data-shape FIXME; GUI wiring for the unwired flags.

## Open questions

- "Empty slot" semantics (vehicle_description_element defaults empty=true; GUI cannot produce
  rule-based/empty slots).
- Depot AWAITING_TRIGGER transitions (before-wait → ENTERING_DEPOT, no-flag → LEAVING_DEPOT):
  naming suggests inverted ordering; intended semantics unresolved — left open deliberately
  [RECOLLECTION:2026-09-07].
- Rule-based alternatives / rule_flags preferences: keep or remove? Undecided
  [RECOLLECTION:2026-09-07].
