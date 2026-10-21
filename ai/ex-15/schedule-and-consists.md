---
status: reviewed
verified: ex-15 @ c6adde69b
---
# ex-15 feature status: schedules, consists & re-combination

**Covers:** dataobj/schedule.{cc,h}, dataobj/schedule_entry.h, dataobj/consist_order_t.{cc,h},
gui/schedule_gui.*, gui/consist_order_gui.*, gui/components/gui_schedule_item.*, simconvoi.cc
(schedule/consist logic), path_explorer.{cc,h}, simhalt.cc (layover registry).

Per-feature registry for the 15.x programme. Design intent sources: forum thread 17852 (post URLs
inline); parent doc: [ex-15](../ex-15.md). Untagged claims are [CODE] at the frontmatter value.

Y/H-shaped journey traversal — the agreed design and rationale: [yh-path-explorer](yh-path-explorer.md);
read when touching cross-schedule path exploration, split/join journey-time estimation, or the
explorer's treatment of consist orders.

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

- **Couple (joining)** — data + GUI target selection (couple_target_selector, line/convoy modes;
  bt_couple sets and clears the `couple` flag) +
  `schedule_t::get_couple_target` + absorption: the arriving convoy draws vehicles from the joining
  convoy or the halt's laid-over pool via `convoi_t::process_consist_order`; an emptied joining
  convoy self-destructs. Missing: no two-party synchronisation; failure is silently ignored
  (`// TODO: Consider what happens if we fail` in `hat_gehalten`); no waiting/retry when the target
  has not arrived.
- **Consist orders** — data model: `consist_order_t` (end-state for one entry, keyed by
  unique_entry_id; exactly one order per entry
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202940.html]) →
  `consist_order_element_t` (slot: catg_index, tag fields, prioritised alternatives) →
  `vehicle_description_element` (a specific vehicle, or rules: engine_type, min/max range,
  catering, classes, power, tractive effort, topspeed, weight, axle load, capacity, running/fixed
  cost, fuel, staff, drivers). Hierarchy per
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg202971.html]. The former
  `rule_flags` preference system was removed in Extended 15.2 (serialised but never evaluated;
  pre-15.2 values are read and discarded on load). Solver:
  `convoi_t::process_consist_order` (single-pass heuristic matching with one pass-over retry) +
  `commit_recombined_consist` (add/remove/substitute/move vehicles; displaced vehicles force-unloaded
  then collected into a new layover convoy). Matching: `vehicle_t::matches_consist_order_element`
  (rules evaluated except preferences; enforces can_lead/can_follow). SHUNTING state with
  `shunting_time_seconds` setting. GUI `consist_order_frame_t`: order overview, consist copier
  (`set_convoy_order`, reversed-consist handling), vehicle picker with filters; edits apply
  per schedule entry from line and convoy schedule windows (singleton re-targets between
  them, saving unsaved edits first). Rule-based alternatives are edited in the
  `consist_rule_editor_t` singleton (per-line limit checkboxes whose unchecked state means
  unconstrained, OK/Cancel commit onto a working copy, any-traction option, goods category
  incl. an any-category wildcard (`consist_order_element_t::any_catg_index`, skipped in
  matching and in `parse_orders`' carried-category tables), named minimum-class dropdown,
  per-alternative empty toggle, and pool matchability validation that greys OK with a
  reason when no vehicle can satisfy a checked line)
  — INCOMPLETE: no tag controls (the tag fields are retained for instance-level portion identity;
  design pending — see "Vehicle tags" below); no window save/restore (get_rdwr_id and the
  simwin.cc restore are commented out). Livery copying in set_convoy_order explicitly unsupported.
- **Shunting completion** — a convoy completing a consist order passes through `check_departure`
  when its shunting delay expires (handled like LAYOVER), so minimum loading and spacing slots are
  honoured rather than departing immediately on the delay expiring.
- **Left-over/loose vehicles** — displaced vehicles become a new layover convoy in situ (blank
  route, reserves own tiles, empty schedule, no line); pool at stops via
  `haltestelle_t::get_laid_over`, consumed by process_consist_order; mothballed vehicles excluded.
  Gaps: no GUI listing of vehicles available to couple; emptied layover convoys linger until
  `new_month` self-destruct; SHUNTING can overwrite LAYOVER without deregistering (stale
  laid_over entry); `// TODO: Add code to find a suitable home depot`.
- **Trigger/couple GUI wiring** — schedule-window arming covers send_trigger,
  conditional_depart_before/after_wait, cond_trigger_is_line_or_cnv, clear_stored_triggers_on_dep,
  trigger_one_only, couple, uncouple (Line/Consist target modes, convoy-as-target listing, and
  enablement gating that preserves stored values while disarmed); those runtime paths are
  reachable in play subject to the runtime gaps below. Still absent or unwired: tag controls,
  consist-order window persistence, loose-vehicle pool listing, livery copying; the copier's sort
  controls are present but never sort (one sort-option entry; the ascending/descending toggle only
  flips state), and its name/home-depot filters are unimplemented.
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
  join/split cannot be discovered or time-estimated. **Design agreed and documented
  in [yh-path-explorer](yh-path-explorer.md)**; it must satisfy the SIMD-compatible layout
  requirements and staged implementation position (design SIMD-compatible → scalar implementation
  verified → SIMD immediately after; design-time gate on dense vectorisability) recorded in
  [simd-applicability](../simd-applicability.md) [RECOLLECTION:2026-09-19].
- **Two-party timed coupling; dead-lock avoidance** — no arrival synchronisation (a named target
  that has not arrived must be waited for; see "Coupling semantics" below), no two-sided
  evaluation of the receiver's and the target's orders as one exchange, and no validation of
  couple/uncouple target graphs (mutual waits, circular targets, platform blockage).
- **Freight/passenger continuity across splits/joins** — displaced vehicles are force-unloaded at
  the stop; no transfer of cargo between portions, no re-routing correction for cargo on moved
  vehicles ("passengers stay on the through portion" is not modelled).
- **Call-on-based coupling movements into occupied platforms** — call-on signalling exists for
  other purposes but is not connected to coupling; SHUNTING is only a timed pause.
- **Vehicle-tag enforcement** — `vehicle_t::tags` and the order tag fields are serialised-only;
  zero runtime callers, no GUI. The fields are retained as the instance-level coupling-identity
  mechanism for portion working; the design is open (see "Vehicle tags" below).
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
6. Solver completeness; GUI wiring for the remaining unwired controls (tags,
   window persistence, loose-vehicle pool, livery, copier sort).

## Vehicle tags — retained; design pending

`vehicle_t::tags` (uint16 per vehicle instance, serialised with the vehicle) and the order fields
(per slot: `clear_all_tags`, `tags_required`, `tags_to_set`; per order: `tags_to_clear`) are
serialised but never evaluated, and no GUI sets them. They are retained as the intended mechanism
for **instance-level coupling identity** in portion working: through cargo is bound to the
instance, descriptor-level rules cannot distinguish the branch portion from an identical vehicle
in the pool, and displaced vehicles are force-unloaded [RECOLLECTION:2026-10-18]. In the
exchange the target's order can mark the vehicles it releases (`tags_to_set`) and the receiver's
order require those marks (`tags_required`). Design questions open:

1. Tag source: player-set per vehicle, set by the order at coupling, or both.
2. `tags_required` scope: its comment covers loose vehicles, but the joining convoy's vehicles pass
   through the same matcher, and the in-position/substitution path needs gating as well for the
   tags to protect through vehicles from displacement.
3. Whether the solver additionally needs a tag-independent rule never to displace a vehicle
   carrying cargo bound beyond the coupling stop.
4. Lifecycle: replacement creates a new instance (tag lost?); overhaul, mothballing, sale and
   transfer.
5. GUI: vehicle detail, convoy detail, loose-vehicle pool listing.
6. Multiplayer scope: whether tag meanings are per player and whether another player's order may
   match them.
7. Explorer/runtime agreement: `parse_orders` derives the carried-category tables without reading
   tags, so a tag-gated slot the runtime cannot fill would still be recorded as a through journey;
   the explorer may read only saved schedule and operating data, not live pool contents.

## Multi-player consists — ownership and access model

Multi-player consists are intended (through carriages, hired traction, wagon pooling)
[RECOLLECTION:2026-10-18]. What exists:

- Per-vehicle ownership: each vehicle carries its own `obj_t` owner (`owner_n`, serialised per
  vehicle by `obj_t::rdwr`; `PLAYER_UNOWNED` when unowned). Consist-order operations preserve it:
  `add_vehicle`, `move_vehicle`, `substitute_vehicle` and `remove_vehicle_at` never touch it, and
  load only repairs null owners (`convoi_t::finish_rd`, depot load, `start_convoi`). The convoy
  owner is separate and may differ by design.
- Movement access is decided by the **leading vehicle's owner**, not the convoy owner and not each
  vehicle: only the leading vehicle runs `hop_check` ("the leading vehicle will do all the
  checks"); `rail_vehicle_t::can_enter_tile` asserts `leading`; route search uses `front()` as the
  driver. `vehicle_t::check_access` allows a way if the leader's owner is the public service, or
  the way is public right of way, unowned, or owned by the leader's owner, or the leader is
  unowned, or the way belongs to the same owner as the way the leader is on, or its owner grants
  access to the leader's owner. Consequences: access rights change when the leader changes
  (reversal, re-combination); access is an entry test only; a public-service leader bypasses all
  checks. Halt access on route registration uses the convoy owner instead.
- Finance is booked by convoy, not by vehicle: revenue, running costs, way tolls and monthly fixed
  costs go to the convoy owner; overhaul/mothball to the vehicle owner; depot sale value to the
  depot owner; the replacer builds the replacement owned by the convoy owner. Vehicle and convoy
  statistics counters move only on build, sale and destroy, not on a consist-order move.
- Coupling-target selection is not gated by access: the GUI lists every lineless convoy registered
  at the halt (registration itself is owner-filtered) plus lines via `get_loading_convois()`.
  Copying a convoy's order requires mutual access (`gui/consist_order_gui.cc`) — a precedent for
  gating coupling itself.
- Company removal destroys the vehicles in its convoys regardless of owner
  (`complete_liquidation` self-destructs the convoys it owns; `convoi_t::destroy` deletes every
  vehicle), so a foreign vehicle in a liquidating company's consist is deleted; overhaul
  re-liveries a foreign vehicle into the operating convoy's scheme (`update_livery`).
- Open: a settlement model (who pays and receives what when a vehicle runs in another company's
  consist — hire or through-carriage payments); the access policy for mixed consists (keep
  leader-based rights, or use the convoy owner's rights and admit foreign vehicles by a separate
  rule); GUI display of per-vehicle ownership.

## Coupling semantics and reliability gaps

Intended coupling semantics [RECOLLECTION:2026-10-19]: a couple order may draw on loose vehicles at
the current stop (the halt's laid-over pool) and on vehicles released by a consist at the stop
under that consist's own uncouple order, falling back to the pool only insofar as the target does
not supply enough. Taking vehicles out of a consist requires an uncouple order on that consist; a
plain uncouple naming no receiver suffices. A named target that has not arrived is waited for, not
substituted, with a timeout and a player message (duration and post-timeout behaviour open). A
consist that only loses vehicles and needs none from the counterpart does not wait: its released
vehicles become loose at the stop.

The two-sided specification exists in the data model: the receiver's entry carries `couple` + the
target + its own order; the target's entry carries `uncouple` (receiver and target entry optional)
+ its own order. What is absent is the runtime that evaluates both orders against the pre-exchange
consists and commits both as one exchange; today only the receiver's order runs, pulling vehicles
out of the target directly.

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
eligibility check that reads it at pickup is not yet written: `process_consist_order` still draws
on the halt's laid-over pool without consulting the recipient, so the setting has no gameplay
effect yet [CODE]. The design's list of permitted lines/convoys is implemented as one selection;
the later tag design (see "Vehicle tags") supersedes the vectors but is itself unimplemented. The
consist-order window lists the Same/Any entries plus the halt's lines or consists, with a
Line/Consist filter; a specific recipient no longer registered at the halt (or hidden by a filter
switch) resets to the default [CODE].

- **A convoy target is resolved by id alone and dismembered wherever it is.** The line mode
  resolves against `halt->get_loading_convois()` (convoys present and loading), but the convoy mode
  is `cnv.set_id(entry->target_id_couple)` with no position or state check. When the receiving
  convoy's order runs, the target's vehicles are removed from it and repositioned onto the
  receiver; the target's route and reservations are not cleaned up, and if emptied it
  self-destructs. The GUI list is `halt->registered_convoys` (convoys serving the halt, not
  necessarily present), so this is reachable in play; registered as an open bug in
  [known-bugs](../known-bugs.md).
- **A specified target that cannot be resolved is silently replaced by the pool.** An unresolved
  target makes `get_couple_target` return an unbound handle, so `process_consist_order` takes the
  no-joining-convoy branch and assembles from the halt's laid-over pool: a missing partner changes
  the order's meaning rather than making it wait. Order failure is otherwise silently ignored
  (`// TODO: Consider what happens if we fail` in `hat_gehalten`); no waiting state exists.
- **Depot-entry orders never execute.** `process_consist_order` has one call site (`hat_gehalten`),
  always with `dep == nullptr`, so its depot branch (`access_convoy_list`, `get_vehicle_list`) is
  dead; the schedule GUI hides the settings tab containing the consist-order button on depot
  entries.

## Open questions

- "Empty slot" semantics (vehicle_description_element defaults empty=true; GUI produces
  empty slots via the per-slot toggle but not rule-based slots).
- Depot AWAITING_TRIGGER transitions (before-wait → ENTERING_DEPOT, no-flag → LEAVING_DEPOT):
  naming suggests inverted ordering; intended semantics unresolved — left open deliberately
  [RECOLLECTION:2026-09-07].
- `convoi_t::turn_at_skipped_depot` walks at most the entry count, so three or more consecutive
  skipped depots between the stop and the turning point can exceed the bound and leave the duplicate
  end-stop visit; schedules of exactly 2 entries are excluded by its 3+ guard.
- Conditional skip is documented as having no effect at waypoints, but the non-depot skip branch of
  `convoi_t::advance_schedule` has no waypoint check, so a stored flag on a waypoint is still
  honoured when the consist is empty.
