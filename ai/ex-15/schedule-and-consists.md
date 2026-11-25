---
status: draft
verified: ex-15 @ 6a9e8d963 (2026-11-16, range-stop and replenishment status); ex-15 @ 2debf73ed (2026-11-15, consist-order check, facing-neutral picking and failure warning); ex-15 @ 71f192a65 (2026-11-15, contact point carried behind the rear vehicle); ex-15 @ b501f13f3 (2026-11-15, choose-signal resolvability pre-check, recipient eligibility at pickup, shared-contact reservation handoff); ex-15 @ 117c75d17 (2026-11-15, one-sided receive layout); ex-15 @ ed6d940be (2026-11-15, shared contact tile and paired layout); ex-15 @ 0692167b6 (2026-11-15, choose-signal call-on core and scripted cases); ex-15 @ 97029f453 (2026-11-14, remainder); discharge boundary and paired runtime 2026-11-13; script interface ex-15 @ 957d5ced8 plus working-tree changes; ex-15 @ 03bf5306e (2026-11-16, committed-target hold corrected and both scripted cases); ex-15 @ 9fdefa153 (2026-11-16, orientation choice, substitution discharge and 35-case test set); ex-15 @ bf157b7a7 plus working-tree pick-up goods package (2026-11-17, optional slots, loaded/empty rules, collection credit and revision 16/17 fields); ex-15 @ 93da398e6 (2026-11-18, Multi-Owner Access Rule in `convoi_t::match_consist_order`); ex-15 @ 9d2b75ecea (2026-11-20, line-level through-pair recording); ex-15 @ e74fb897d0 (2026-11-20, committed through-pair script accessors and test case); ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, passenger and mail continuity across splits and loose stock); ex-15 @ 6998ff08e1 plus working-tree per-operation shunting delay (2026-11-24); ex-15 @ 1578e7a689 plus working-tree standing-order and deferred-unload fix (2026-11-25, rear-portion-order case); ex-15 @ f41b00399f (2026-11-25, AWAITING_TRIGGER sync-step handling)
---
# ex-15 feature status: schedules, consists & re-combination

**Covers:** dataobj/schedule.{cc,h}, dataobj/schedule_entry.h, dataobj/consist_order_t.{cc,h},
gui/schedule_gui.*, gui/consist_order_gui.*, gui/components/gui_schedule_item.*, simconvoi.cc
(schedule/consist logic), path_explorer.{cc,h}, simhalt.cc (layover registry).

Per-feature registry for the 15.x programme. Design intent sources: forum thread 17852 (post URLs
inline); parent doc: [ex-15](../ex-15.md). Untagged claims are [CODE] at the frontmatter value.

Operational requirements, design decisions, ownership and implementation ordering:
[recombination-design](recombination-design.md); read before planning or changing the runtime.
Through-journey traversal across couplings — the agreed design and rationale: [yh-path-explorer](yh-path-explorer.md);
read when touching cross-schedule path exploration, split/join journey-time estimation, or the
explorer's treatment of consist orders.

## Data structures — IMPLEMENTED

- `schedule_entry_t` (dataobj/schedule_entry.h): `pos`; `flags` (uint32, enum `schedule_entry_flag`);
  `minimum_loading` (uint16 — doubles as depot-command carrier via `depot_flag`: delete_entry /
  store / maintain_or_overhaul); `spacing_shift`; `unique_entry_id` (uint16, stable identity via
  `schedule_t::get_next_free_unique_id`); `condition_bitfield_broadcaster` / `_receiver`;
  `target_id_condition_trigger` / `target_id_couple` / `target_id_uncouple`;
  `target_unique_entry_uncouple` / `target_unique_entry_couple`; `waiting_time_shift`; `reverse`;
  `max_speed_kmh` (per-entry speed limit, 65535 = none).
- Flags: wait_for_time, lay_over, ignore_choose, force_range_stop, conditional_depart_before_wait,
  conditional_depart_after_wait, conditional_skip, send_trigger, cond_trigger_is_line_or_cnv,
  clear_stored_triggers_on_dep, trigger_one_only, couple, uncouple, couple_target_is_line_or_cnv,
  uncouple_target_is_line_or_cnv, uncouple_target_sch_is_reversed, couple_target_sch_is_reversed,
  discharge_payload, set_down_only, pick_up_only.
- Storage model: consist orders live on `schedule_t::orders` (inthashtable keyed by
  `unique_entry_id`), one per entry; entries are copied by value and heavyweight orders are kept
  apart. There is no whole-schedule list of referring schedules.
- Savegame/network: all new entry/order data gated on extended ≥ 15 (`schedule_t::rdwr`,
  `consist_order_t::rdwr`, `haltestelle_t::rdwr` laid_over list, `convoi_t::rdwr`
  conditions_bitfield). Network path = schedule string serialisation
  (`sprintf_schedule`/`sscanf_schedule`, including orders) via tool_change_convoi/line/depot;
  no dedicated network command for consist orders. Vehicle-tag fields in saves from before
  Extended 15.4 (`vehicle_t::tags`, per-slot tag flags, per-order `tags_to_clear`) are read and
  discarded on load; older save targets are written with zeros.
  `consist_order_element_t::optional` is one bool per slot and
  `vehicle_description_element::load_requirement` is one byte per alternative; both use the same
  read/write condition, `is_version_ex_atleast(15, 17)`, and default to mandatory and any load state
  in older files. Schedule network tool strings carry both fields. The three pending collection-credit
  fields (`collection_credit_pending`, `collection_loose_since` and `collection_time`) are one bool
  and two sint64 tick values in `vehicle_t::rdwr_from_convoi`, also read and written under Extended 15
  revision 17. Per-vehicle through-leg distance records use the revision 16 block.
- Script interface: `schedule_x` contains all entry fields, schedule-level authoring settings,
  `current_stop` and an `orders` table keyed by `unique_entry_id`. Its consist-order objects cover
  ordered slots, prioritised specific/rule-based alternatives and recipient settings. Conversion
  uses the existing line/convoy tools, not direct live-schedule mutation, and rejects the same
  oversized parameter strings that the GUI rejects. The depot script wrappers include the required
  livery-index field and preserve a bound convoy's current livery scheme.
  `schedule_t::sscanf_schedule` clears the copied schedule's old orders before rebuilding them and
  accepts multi-digit order keys; `schedule_t::parse_orders` keeps those keys at 16 bits. The
  schedule-string format is unchanged.
- Scripted test setup and outcomes for the division, paired-exchange and choose-signal cases,
  including their save/network reloads, are in
  [scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md).

## IMPLEMENTED features

- **Per-entry speed limit** — `max_speed_kmh`, enforced in `convoi_t::calc_acceleration` (the
  wanted speed is min()'d against the entry limit); GUI controls
  (bt_speed_limit/numimp_speed_limit).
- **Layover** — flag + `convoi_t::LAYOVER` state + enter/exit_layover; requires a stop with a
  `layover_enable` building (`haltestelle_t::can_lay_over`, building_desc FLAG_LAYOVER_ENABLE);
  halt-side `laid_over` registry; `min_layover_overhead_seconds` setting; entering layover forces
  full discharge; salary time discounted (`last_salary_point_ticks`). GUI control gated on
  can_lay_over; "[LO]" stop-name prefix. Gaps: mail/goods aboard at layover (open in forum
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193643.html]); no warning UI when
  facilities are removed after a schedule is set.
- **Conditional skip** — depot entries skipped when `!convoi_t::is_maintenance_needed()`; non-depot
  entries skipped when loading level is 0 (`convoi_t::advance_schedule`), except at reversing stops
  (a mirrored schedule end or an entry with the reverse flag set), where the option has no effect
  (`schedule_t::is_reversing_stop`). GUI control ("Conditional skip" checkbox in schedule entry
  settings 1; halt and depot entries; disabled with stored value preserved at waypoints and at
  reversing station stops).
- **Ignore choose sign** — `rail_vehicle_t::activate_choose_signal`, `road_vehicle_t::hop_check`;
  GUI control + "[IC]" prefix + skin.
- **Trigger runtime** — `convoi_t::AWAITING_TRIGGER` state; `convoi_t::sync_step()` treats it as a
  stationary state, while loading and trigger matching run in `convoi_t::step()`; receiver bitfield
  matching in the step logic; `simline_t::propagate_triggers` (with trigger_one_only: earliest-arriving
  satisfied convoy first); clear_stored_triggers_on_dep handling. GUI sets only the bitfield numbers
  (inputs select condition numbers 0–15 — the 16 designed conditions) and a line selector.
  The pending-runtime `trigger-wait` case keeps a convoy in this state and asserts that it remains
  there.
- **Loading-control flags** (discharge_payload / set_down_only / pick_up_only) — enforced in
  `hat_gehalten`/unload paths and in the path explorer
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202997.html].
- **Depot commands in schedule entries** (depot_flag) — executed in `depot_t::convoi_arrived`.
- **Spacing/timing rework** — `schedule_t::spacing` in 12ths of departures/month; per-entry
  spacing_shift; departure-slot queueing (`haltestelle_t::get_queue_pos`); whole-schedule ETA/ETD
  projection (`convoi_t::book_departure_time`).
- **Times history / departure boards** — `times_history_map` (TIMES_HISTORY_SIZE 3),
  gui_times_history_t (contributor: suitougreentea).
- **Path-explorer consist-order integration** — `schedule_t::parse_orders` derives
  catg/min-class carried-to/from tables; `compartment_t::schedule_flags` fragmentation with
  "invalid zones" across consist-order points; extra transfer time after layover/discharge stops.
  Within a single schedule only (see ABSENT below).
- **Y division (uncouple)** — an `uncouple` entry without `couple` divides the convoy when it first
  halts there, provided the entry has a consist order. `convoi_t::resolve_uncouple_target` resolves
  `target_id_uncouple` by the path explorer's rules: without `uncouple_target_is_line_or_cnv` the
  id names a line; with that flag it names a convoy, whose line supplies the schedule when the
  convoy is on a line, and whose own schedule is used when it is lineless. The target entry
  (`target_unique_entry_uncouple`) must exist in that schedule, carry a consist order and lie at
  the halt the convoy has stopped at, checked with `haltestelle_t::get_halt` for the target's
  owner. When the target resolves, the source entry's order runs as usual, and its displaced
  vehicles form a newly created laid-over convoy of their own (`commit_recombined_consist` with the
  split-candidate handle). The displaced group is created in the order in which its vehicles stood
  in the consist being divided, front of travel first; the commit applies the ordering when it
  creates the group. `convoi_t::form_divided_portion` applies the target entry's order to that
  convoy and promotes it only when the result is complete and powered. A promoted portion joins
  the target line (`convoi_t::set_line`) or receives a copy of the lineless target convoy's
  schedule, starts at the target entry in the direction given by
  `uncouple_target_sch_is_reversed`, is removed from the halt's lay-over registry
  (`haltestelle_t::remove_laid_over`), inherits the source's `arrival_time`, takes current-position
  `last_stop_pos` values, is removed from and re-added to `welt->sync`, and enters `LOADING` with
  the `shunting_time_seconds` wait lock. `commit_recombined_consist` also defers the portion's
  stop unload until `form_divided_portion` assigns the target schedule, so the unload uses the
  assigned target schedule and through cargo stays aboard. The portion departs in the direction
  its target schedule gives: if that is the direction its vehicles already face, it departs
  directly in that direction; otherwise the ordinary departure logic reverses the order or drives
  from the rear cab as `convoi_t::get_terminal_shunt_mode` allows. `last_stop_id` is set to the
  current halt, so the target entry's order runs once at this stop. A plain uncouple (target id
  0), an unresolved or deleted target, a target entry without an order, an unsatisfiable order and
  an unpowered result all leave the displaced vehicles as ordinary loose stock. An entry carrying
  both `couple` and `uncouple` is excluded from this path. `commit_recombined_consist` defers the
  discharge of a displacement collected into a split candidate: `form_divided_portion` discharges the
  portion only when the target order cannot be completed or the result is unpowered, so a promoted
  portion keeps its load (the decided discharge boundary). A vehicle displaced by substitution keeps
  its load but is not priced as a through journey
  ([cargo-continuity](recombination-design/cargo-continuity.md)).
- **Paired H exchange (couple + uncouple)** — two convoys exchange vehicles at one stop as one
  operation. The receiver's current entry carries `couple`; the target's current entry at the same
  stop carries `uncouple`. Each entry names the other convoy or line; the counterpart's optional
  `target_unique_entry_couple`/`target_unique_entry_uncouple` reference, when set, must name the
  other entry. Both entries have a consist order. `convoi_t::resolve_paired_exchange`
  requires that the two entries name each other, lie at the same `haltestelle_t` and use the same
  waytype; it refuses both flags on an entry that names the other side, a self target, `lay_over` on
  either entry, a missing order, an ambiguous match at the stop, and a counterpart entry that does not
  reciprocate or lies at another stop. A counterpart that is present
  but not yet waiting, and one that is not at the stop, is awaited:
  `convoi_t::AWAITING_RECOMBINATION` has no timeout. The file-local `paired_exchange_solver_t` in
  `simconvoi.cc` searches the two entries' consist orders (each order states the required end state
  of its convoy) jointly over the canonical universe of both forward-oriented pre-exchange consists
  and, for the receiver only, the stop's eligible laid-over vehicles. It enforces slot matching,
  the `convoi_t::check_validity()` coupling predicates, power in both results, pairwise-disjoint
  final consists, and the Multi-Owner Access Rule (every vehicle's owner must be granted access by
  the owner of the first powered vehicle in that final consist). The receiver may draw on the
  laid-over pool only after a solve without it has failed, and the search starts with the smallest
  pool draw. The search stops after 16,384 candidate placements; this is an implementation
  computation limit, not a game quantity, and exhaustion refuses the exchange.
  `convoi_t::commit_paired_exchange` commits both final consists as one operation: only vehicles
  that leave both final consists retain through cargo and are laid out as loose stock under the
  releasing order's recipient setting; vehicles transferred directly between the two consists keep their
  load; a laid-over donor emptied by the exchange is deregistered and destroyed. When the receiver
  reached the target's rear tile under a committed approach, the final consists are laid out on the
  tiles the pre-exchange vehicles occupied (front convoy first, loose stock behind both) with
  `vehicle_t::set_layout_position`; each changed convoy releases its previous reservations before
  re-reserving. A changed convoy waits out `shunting_time_seconds`; the counterpart waits in
  `LOADING` under the same lock so that its new consist loads after the delay. Failure is
  all-or-nothing: neither consist changes, one warning is posted, the counterpart is released from
  `AWAITING_RECOMBINATION`, and both convoys continue on their own schedules. Before a choose signal
  routes the receiver to the target, `convoi_t::can_approach_coupling_target` runs the same joint
  solver without committing; a failure holds the receiver at the signal, posts
  `convoi_t::report_coupling_hold` and never reaches contact.
- **Passenger and mail continuity across splits and loose stock** — `convoi_t::hat_gehalten`
  defers its ordinary unload at a recombination entry until the final consists are known.
  `convoi_t::redistribute_passengers_after_division` moves passenger packets and mail packets on a
  train with a TPO (Travelling Post Office; defined in
  [passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md)) to a serving
  portion up to overcrowded capacity, then discharges the remainder at the division. A passenger
  vehicle, or a mail vehicle on a train with a TPO, that becomes loose stock unloads packets whose
  next transfer or destination is the stop where it becomes loose; later-transfer packets stay
  aboard as a through coach ([consist-recombination](../consist-recombination.md#terms)). Goods and
  non-TPO mail keep the through-wagon model. Directly
  transferred vehicles keep their load, and per-vehicle through records supply the fare history.
  See [passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md) and
  [cargo-continuity](recombination-design/cargo-continuity.md). Passenger load-time portion grouping
  and portion-class boarding rules remain open.

## PARTIAL features

- **Couple (joining)** — data + GUI target selection (couple_target_selector, line/convoy modes;
  bt_couple sets and clears the `couple` flag) +
  `schedule_t::get_couple_target` + absorption: the arriving convoy draws vehicles from the joining
  convoy or the halt's laid-over pool via `convoi_t::process_consist_order`; an emptied joining
  convoy self-destructs. Target resolution is co-location-gated in both modes (only convoys present
  at the halt and loading) and yields four states [CODE ex-15 @ 2315dba1b]: not configured (the
  order assembles from the stop's pool — the intentional targetless mode), present (exchange),
  absent (waited for in `convoi_t::AWAITING_RECOMBINATION`, deliberately without a timeout), and
  missing — a broken target, reported and left unexecuted, never pool-assembled. An incomplete
  couple order (flag with no target selected) is removed at commit with a notice naming the stops.
  Missing: the one-sided receive is the fallback only when the counterpart entry does not carry
  `uncouple`, which selects the paired exchange instead (see IMPLEMENTED features); an entry carrying
  both flags is refused. A one-sided receive whose consist order fails with a present target remains
  unexecuted; `convoi_t::process_current_entry_consist_order` reports the failure once per stop with
  `convoi_t::report_consist_order_failed`. `convoi_t::match_consist_order`, including the branch
  that handles optional slots, also enforces the Multi-Owner Access Rule on the final consist: the
  owner of the first powered vehicle must permit every other vehicle owner to use its ways. A denied
  assembly fails like any other unmatched order. A target that is stationary when the convoy is at a
  choose signal is tested by the resolvability pre-check, which holds the convoy and reports the
  reason instead of routing it to the target. Detection of wait cycles longer than a direct pair is
  absent.
- **Consist orders** — data model: `consist_order_t` (end-state for one entry, keyed by
  unique_entry_id; exactly one order per entry
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202940.html]) →
  `consist_order_element_t` (slot: catg_index, prioritised alternatives) →
  `vehicle_description_element` (a specific vehicle, or rules: engine_type, min/max range,
  catering, classes, power, tractive effort, topspeed, weight, axle load, capacity, running/fixed
  cost, fuel, staff, drivers). `consist_order_element_t::optional` controls whether the whole slot
  may be omitted at any position; later mandatory slots shift forward. A rule alternative's
  `vehicle_description_element::load_requirement` restricts the actual vehicle to loaded or empty.
  The separate existing `empty` member makes a rule alternative a wildcard; it does not omit a slot.
  Hierarchy per
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202971.html]. Alternatives carry no
  preference flags; priority is list order. `rule_flags` values in saves from before Extended
  15.2 are read and discarded on load. Solver:
  `convoi_t::process_consist_order` (single-pass heuristic matching with one pass-over retry) +
  `commit_recombined_consist` (add/remove/substitute/move vehicles and collect displaced vehicles
  into layover convoys). Vehicles not promoted to a schedule retain through cargo, while cargo for
  the stop is delivered there. The shared destination, forward and explicit-discharge rules are in
  [cargo-continuity](recombination-design/cargo-continuity.md). A split candidate is delivered only
  after the target schedule has been assigned. Matching:
  `vehicle_t::matches_consist_order_element`
  (rules evaluated except preferences; enforces can_lead/can_follow). SHUNTING state with the delay
  in [consist-recombination](../consist-recombination.md#shunting-delay). GUI
  `consist_order_frame_t`: order overview, consist copier
  (`set_convoy_order`, reversed-consist handling), vehicle picker with filters; edits apply
  per schedule entry from line and convoy schedule windows (singleton re-targets between
  them, saving unsaved edits first). Rule-based alternatives are edited in the
  `consist_rule_editor_t` singleton (per-line limit checkboxes whose unchecked state means
  unconstrained, OK/Cancel commit onto a working copy, any-traction option, goods category
  incl. an any-category wildcard (`consist_order_element_t::any_catg_index`, skipped in
  matching and in `parse_orders`' carried-category tables), named minimum-class dropdown,
  separately labelled wildcard and optional-slot toggles, loaded/empty requirement selection, and
  pool matchability validation that greys OK with a reason when no vehicle can satisfy a checked
  line). Picker authoring gates: per-slot goods-category consistency across a slot's alternatives
  is IMPLEMENTED — the picker only offers vehicles of the target slot's category
  (`is_filtered`), `consist_order_element_t::append_vehicle`/`set_catg_index` refuse category
  conflicts, and the rule editor offers only the established category and "Any" per slot.
  Behaviour-relevant because the value is read at runtime by rule matching
  (`vehicle_t::desc_matches_rule` tests the slot's category; specific-vehicle alternatives
  bypass it) and by `schedule_t::parse_orders`' carried-category tables feeding the path
  explorer [CODE]. The picker's connection filter
  (`consist_order_element_t::can_connect`) accepts a bidirectional vehicle that connects in
  either facing, because an order records vehicle types but not which way round each vehicle
  will be when the train is formed. A list-level verdict is IMPLEMENTED: `consist_order_t::check`
  evaluates the whole list against a pool of vehicle types (the player's fleet plus anything
  buyable new) and reports the first problem with the affected slot, which the window numbers as a
  position from the front of the train: an empty list; a slot with no alternative; a slot whose rule
  alternatives match no vehicle in the pool; a break in the coupling chain between two adjacent
  slots; a first vehicle that cannot be at the front; a last vehicle that cannot be at the rear; or
  no vehicle that provides power. A named vehicle is assumed to be available, because the order may
  draw it from another train.
  `consist_order_frame_t::update_order_check` shows the verdict in colour under the slot list with
  an explanation, names the counterpart of a couple or uncouple entry, states that each side of a
  divided or joined train must be able to run on its own, and states that the list runs from the
  front of the train to the rear in the direction it travels. The verdict is advisory: it marks
  the list and never blocks an edit or a save. `convoi_t::get_forward_consist` de-reverses the array
  for matching without changing the vehicles' `is_reversed` flags. `convoi_t::match_consist_order`
  and `paired_exchange_solver_t` try each candidate in its current `is_reversed` orientation first
  and, when it is bidirectional, the other way round if that does not fit. The chosen orientation
  is applied by `convoi_t::commit_recombined_consist` and `convoi_t::commit_paired_exchange` with
  `vehicle_t::set_reversed`; each bidirectional vehicle whose orientation changes adds
  `settings_t::get_unit_reverse_time()` (powered) or `settings_t::get_turntable_reverse_time()`
  (hauled) to the shunting delay. The editor's verdict is facing-neutral because it cannot know
  which vehicle another consist will supply or which way round that vehicle will be. The
  verdict also cannot tell whether the two lists of a divide can both be satisfied from the same
  vehicles; that is decided by `convoi_t::match_consist_order` or `paired_exchange_solver_t` from
  the vehicles actually available.
- **Shunting completion** — a convoy completing a consist order passes through `check_departure`
  when its shunting delay expires (handled like LAYOVER), so minimum loading and spacing slots are
  honoured rather than departing immediately on the delay expiring.
- **Left-over/loose vehicles** — displaced vehicles become a new layover convoy in place (blank
  route, reserves own tiles, empty schedule, no line); pool at stops via
  `haltestelle_t::get_laid_over`, consumed by process_consist_order; mothballed vehicles excluded
  from the pool but stored alongside. Player visibility: the stop information window shows a
  layover icon with the number of loose vehicles (absent when none; mothballed not counted) and
  the stop details window has a "Vehicles" tab listing each stored vehicle with its owner
  (mothballed marked). Deleting a stop evacuates the pool: every vehicle goes to the nearest
  depot of the right type owned by the vehicle's owner, charged to that owner at 1.1x the
  straight-line distance at the vehicle's per-km running cost (integer arithmetic; booked as
  distance-based running cost); with no such depot the vehicle is sold at its current value. The
  affected owners each receive a pop-up dialogue (on their own client) and a player-targeted
  message. Runs inside `haltestelle_t::destroy` on every peer (skipped at world teardown).
  A new loose convoy's home depot is the nearest depot suitable for its first vehicle
  (`depot_t::find_nearest_depot`; `koord3d::invalid` when the owner has none). Targetless consist
  orders can already generate loose stock; play-testing of the visibility UI and evacuation is
  deferred until the fuller runtime is available. Gaps: none outstanding
  here — emptied joining convoys destroy promptly (`new_month`'s vehicle-less sweep acts as a
  backstop) and the layover registry is kept consistent (see "Layover registry consistency"
  below).
- **Trigger/couple GUI wiring** — schedule-window arming covers send_trigger,
  conditional_depart_before/after_wait, cond_trigger_is_line_or_cnv, clear_stored_triggers_on_dep,
  trigger_one_only, couple, uncouple (Line/Consist target modes, convoy-as-target listing, and
  enablement gating that preserves stored values while disarmed); those runtime paths are
  reachable in play subject to the runtime gaps below. Consist-order window and rule-editor
  persistence is implemented ([gui](../gui.md)); the list-level verdict and the picker's
  facing-neutral connection filter described above are implemented as well. The couple
  target-entry picker and
  the "Target schedule is reversed" controls for both sides are present, enabled only for
  mirrored target schedules; target changes clear the stored entry id and direction. They are
  compile-verified but have not been exercised in an interactive game. They are additional UI
  work beyond the schedule/consist UI described above.
- **Range stops & replenishment — ABSENT runtime; scaffold present.** Range descriptors, the
  out-of-range guard, saved `km_since_last_replenish`, schedule authoring, the `replenish_enable`
  building flag, `REPLENISHING` and `replace_data_t::on_replenish` exist. Automatic selection,
  facility lookup, runtime dwell and the replacement trigger are not connected. Detailed current
  code state, design decisions and resumption order:
  [range-stops-replenishment](range-stops-replenishment.md) — read before implementing automatic
  range-stop detection, replenishment dwell, facilities or replacement on replenishment. Dynamic
  (fuel-consumption-derived) range remains rejected by design
  [FORUM:https://forum.simutrans.com/index.php/topic,14991.msg193647.html].

## ABSENT — core outstanding 15.x work [RECOLLECTION:2026-09-07]

- **Through-journey path-explorer traversal** — the connexion-rebuild generation reads couple/uncouple
  targets and records through connexions as direct candidates whose time is the actual recorded
  through pair (defined in [yh-path-explorer](yh-path-explorer.md#definitions)), with the existing
  accumulated-leg fallback. There is no routing-search substitution.
  The Y division supplies the runtime divider and the schedule handover. A convoy that is on a line
  records the actual through pair at each arrival in `convoi_t::laden`, into the line's
  `average_journey_times`, so a through connexion takes the recorded pair when the destination
  linkage's table holds one. A lineless convoy records no through pair, so its through connexions
  are timed from the accumulated legs. The recording does not affect revenue, which uses the
  vehicle's `through_departures` record. The recording's case is `through-pair`, in
  [scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md).
  Design:
  [yh-path-explorer](yh-path-explorer.md). `target_unique_entry_couple` and
  `couple_target_sch_is_reversed` are fields of `schedule_entry_t`. The
  connexion-rebuild generation, the cross-boundary predicate, the depth setting and the GUI controls are
  implemented. A temporary verification hook on
  `month-boundary.sve` passed seven modes: uncouple, one-sided couple, reversed target, mirrored
  source reverse pass, nested couplings (record present at depth 64, absent at depth 1), broken
  target (no record) and blocked coupling (no record), plus a repeated-refresh determinism check.
  The hook is not in the code. The category/class consist-order predicate, cross-process
  determinism of saves containing couplings and interactive GUI operation remain untested. The
  work must satisfy the routing-search engine requirements in
  [path-explorer-optimisation](../path-explorer-optimisation.md) [RECOLLECTION:2026-09-19;
  revised 2026-11-07].
- **Wait-cycle detection beyond a direct pair** — a convoy whose couple entry names an absent
  target waits in `convoi_t::AWAITING_RECOMBINATION` without a timeout
  [RECOLLECTION:2026-11-03 user statement]. A direct pair at one stop resolves because either
  convoy's step re-resolves and executes the exchange. A longer cycle (A waits for B, B for C,
  C for A) is not detected and jams; circular target graphs are refused only where the pair
  reciprocity validation reaches them. Platform blockage is accepted congestion.
- **Choose-signal call-on for coupling movements** — the choose-signal target decision, target-platform
  route and call-on admission are implemented. A committed target holds the mover at the signal.
  Before the signal routes to a stationary target it calls `convoi_t::can_approach_coupling_target`,
  which solves the configured operation without changing state: `paired_exchange_solver_t` or
  `convoi_t::match_consist_order`. A failure keeps the signal at danger, posts one warning through
  `convoi_t::report_coupling_hold` and holds the receiver; the verdict is cached until either
  consist or the stop's laid-over pool changes. The shared contact tile, the coupling-point
  calculation, the reservation handoff (`rail_vehicle_t::hand_over_contact_reservation`), the
  one-sided receive's layout and the paired layout are implemented. Scripted coverage, the case
  lists and the remaining gaps (including the unscripted holder-to-mover transfer) are in
  [scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md).
  SHUNTING is a timed pause, not a movement.
- **Whole-schedule referring list** (warn players editing a schedule others point to) — absent.

## Coupling semantics and reliability gaps

The intended paired/asynchronous semantics, ownership model and outstanding design decisions are
in [recombination-design](recombination-design.md). Implementation-only reliability facts follow.

### Leftover-vehicle recipients

`consist_order_t::available_to_id`/`available_to_flags` (serialised under
`is_version_ex_atleast(15,3)`) record who may take the vehicles that the order releases at the
stop. Zero id and zero flags mean any line or consist; zero id with `available_to_is_same` (the
default) means only the line that released them, or the convoy itself if it is lineless; a
non-zero id names a specific line, or a specific convoy when `available_to_is_convoy` is set. The
default follows the written design: "It should be possible to specify in the consist orders a
list of convoys/lines for which the spare vehicles would be available to be coupled..., the
default being only the same convoy/line that loaded them"
[FORUM:https://forum.simutrans.com/index.php?topic=17852.msg170134]. "Same" is resolved at
release time (the releasing convoy's line, else the convoy), so it remains correct if a schedule
is copied between a line and a convoy; the resolved id/kind is copied to each released vehicle's
`loose_recipient_id`/`loose_recipient_flags` (zero means no named recipient) [CODE]. The
eligibility check at pickup is implemented for `process_consist_order`: a laid-over vehicle is
available only when its recorded recipient is unset ("Any"), names this convoy, or names this
convoy's line. The paired exchange applies the same rule to the receiver's pool draw. One
recipient may be specified, not a list. The
consist-order window lists the Same/Any entries plus the halt's lines or consists, with a
Line/Consist filter; a specific recipient not registered at the halt (or hidden by a filter
switch) resets to the default [CODE]. A target order applied to a split-off portion resolves "Same"
before the portion is given its target line (`convoi_t::form_divided_portion` calls
`process_consist_order` first and `convoi_t::set_line` afterwards), so released vehicles can name
the split-off convoy rather than the line it is about to join.

- **Zero-id semantics** [CODE ex-15 @ 13ba17597]: 0 means "no specific target" by design for
  `consist_order_t::available_to_id` (`is_available_to_any`/`is_available_to_same`, branched on in
  `process_consist_order`) and for `target_id_uncouple` (a plain uncouple names no receiver), but
  NOT for `target_id_couple`, where it is merely "no selection" (the GUI's "---" entry is an
  inactive placeholder; the field comment makes the target integral). The intentional pool mode is
  an order WITHOUT couple configuration; a broken target (id 0 or a deleted id — one `is_bound()`
  check distinguishes it from an absent one) is reported per visit and left unexecuted.
- **Reference sweeps on deletion** [CODE ex-15 @ 2315dba1b]: convoy and line destructors sweep
  every schedule for consist-order references to the deleted object and remove them — a couple
  order keeps its flag and becomes a broken-target order, an uncouple order becomes a plain
  uncouple, trigger targets and leftover-vehicle recipients are cleared — reporting the affected
  players in a message-style window (`consist_change_news`, `gui/messagebox.h`) and the message
  log. Losing the last convoy of a surviving line warns the referring players instead of removing
  anything. This is what prevents a stale id binding to a new object that reuses the id slot.
- **Depot-entry orders never execute.** `process_consist_order` has one call site (`hat_gehalten`),
  always with `dep == nullptr`, so its depot branch (`access_convoy_list`, `get_vehicle_list`) is
  dead; the schedule GUI hides the settings tab containing the consist-order button on depot
  entries. The design in [depot consist formation](depot-consist-formation.md) proposes to
  activate that branch with a Depot Consist Order and a Depot Vehicle Pool.

### Layover registry consistency

The `laid_over` pool must mirror exactly the convoys in the LAYOVER state standing at the stop:
a stale entry would let a consist order at the stop draw on a remote running convoy (the pool
draw filters only MAINTENANCE/OVERHAUL/REPLENISHING/SHUNTING states). The invariant is
maintained by the state transitions — the SHUNTING transition and departures from LAYOVER and
SHUNTING call `exit_layover()` (which guards an unbound halt); `convoi_t::destroy()` deregisters
(the state is not a reliable guide at that point, so removal is attempted unconditionally); the
emptied joining convoy in `process_consist_order` deregisters before `self_destruct()` (an
emptied convoy has no position for destroy() to find the stop by) and is destroyed promptly —
and repaired at load (`karte_t::load`, beside the maintenance-queue pairing): LAYOVER convoys
re-register at their stop and `haltestelle_t::validate_laid_over()` drops unbound entries and
entries whose convoy is not in layover at that stop. Pool iteration is defensive about
unbound/stale handles regardless.

## Open questions

- Player-initiated livery copying — a convenience feature for copying the livery of one of a
  player's own consists to their other consists [RECOLLECTION:2026-11-01 user statement].
  Unsettled: whether it copies the
  livery scheme (setting the targets' scheme index and re-applying) or the per-vehicle liveries,
  and how the target consists are selected. The ownership rule in
  [recombination-design](recombination-design.md) gives its semantics on mixed consists (own
  vehicles only, with the warning). Unrelated to re-combination, which never changes liveries.
- Split composition: SETTLED — two end-state orders, one per portion (see
  [recombination-design](recombination-design.md)). Instance identity at a division remains open
  (same doc).
- Reversal facility: the runtime turns a bidirectional vehicle during the shunt at the coupling
  stop and charges the existing unit or turntable reversal time. A future model could
  allow a vehicle to be turned only where the stop has a reversal facility such as a turntable.
  Whether to simulate to that level of operational granularity is open
  [RECOLLECTION:2026-11-16 user statement].
- Depot AWAITING_TRIGGER transitions (before-wait → ENTERING_DEPOT, no-flag → LEAVING_DEPOT):
  naming suggests inverted ordering; intended semantics unresolved — left open deliberately
  [RECOLLECTION:2026-09-07].
- `convoi_t::turn_at_skipped_depot` walks at most the entry count, so three or more consecutive
  skipped depots between the stop and the turning point can exceed the bound and leave the duplicate
  end-stop visit; schedules of exactly 2 entries are excluded by its 3+ guard.
- Conditional skip is documented as having no effect at waypoints, but the non-depot skip branch of
  `convoi_t::advance_schedule` has no waypoint check, so a stored flag on a waypoint is honoured
  when the consist is empty.
