---
status: draft
verified: none
---
# Depot consist formation: code baseline

Parent: [depot consist formation](../depot-consist-formation.md). This document records the
current code that the design reuses and the refactors it needs. Persistence, network, determinism,
performance, failure modes and the test plan are in
[remote-and-persistence](remote-and-persistence.md). Tags: `[CODE]` is verified against the
checked-out tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is unresolved.

## Current code baseline

### Consist orders and schedule storage

- `consist_order_t` (dataobj/consist_order_t.h) holds an ordered list of
  `consist_order_element_t` slots. Each slot has a goods category, an `optional` flag, and a
  prioritised list of `vehicle_description_element` alternatives. An alternative is either a
  specific `vehicle_desc_t` or a rule over vehicle attributes. `vehicle_description_element::empty`
  makes a rule alternative a wildcard. `available_to_id` and `available_to_flags` select who may
  take vehicles that the order releases [CODE].
- `consist_order_t::check()` evaluates a whole order against a pool of vehicle types and reports the
  first problem: empty list, empty slot, rule with no matching vehicle, broken coupling chain,
  front cannot lead, rear cannot be last, or no power [CODE].
- Orders live in `schedule_t::orders`, an `inthashtable` keyed by
  `schedule_entry_t::unique_entry_id`, one per entry. They are saved by `schedule_t::rdwr()` and
  travel in the schedule string through `sprintf_schedule()`/`sscanf_schedule()` [CODE
  dataobj/schedule.cc].
- `consist_order_frame_t` (gui/consist_order_gui.cc) edits an order. It includes a consist copier
  (`consist_order_t::set_convoy_order()`), a vehicle picker, rule-based alternatives and a
  list-level verdict [CODE].
- `schedule_gui_t` hides the settings tab containing `bt_consist_order` on non-station entries
  [CODE gui/schedule_gui.cc]. Depot entries therefore cannot author an order through the current
  GUI.

### Matching and commit

- `convoi_t::match_consist_order()` (simconvoi.cc) matches an order against a vector of available
  vehicles and a forward-oriented shadow of the current Convoy. It returns the final Consist and
  each vehicle's orientation, and enforces slot matching, coupling, power, optional slots and the
  Multi-Owner Access Rule (defined in
  [recombination design](../recombination-design.md#multi-player-consists-current-model)) [CODE].
- `vehicle_t::matches_consist_order_element()` (vehicle/vehicle.cc) tests a vehicle against one slot's
  alternatives and enforces `can_lead`/`can_follow` [CODE].
- `convoi_t::process_consist_order()` (simconvoi.cc) obtains available vehicles, calls
  `match_consist_order()`, and commits with `convoi_t::commit_recombined_consist()`. It accepts a
  `depot_t*` parameter. `convoi_t::get_recombination_available_vehicles()` already reads
  `dep->access_convoy_list()` and `dep->get_vehicle_list()` when the parameter is non-null [CODE].
  The `access_convoy_list()` part includes other Convoys in the depot, including Held Convoys. The
  design draws only from the loose list `depot_t::vehicles`, so the depot solver must not reuse
  that part unchanged.
- The depot branch is dead: the only live call site is `convoi_t::hat_gehalten()`, which passes
  `nullptr`. `convoi_t::process_current_entry_consist_order()` also passes `nullptr` [CODE
  `simconvoi.cc`]. The data and matching code are therefore the main reuse point.
- `convoi_t::commit_recombined_consist()` moves vehicles with `convoi_t::add_vehicle()`,
  `convoi_t::remove_vehicle_at()` and `convoi_t::substitute_vehicle()`, applies the chosen
  orientations with `vehicle_t::set_reversed()`, and collects displaced vehicles into loose
  convoys at a stop [CODE]. Depot formation needs a depot variant that appends displaced vehicles
  to `depot_t::vehicles` instead of a layover convoy.

### Depot arrival, maintenance and the queue

- `depot_t::convoi_arrived()` (simdepot.cc) discards cargo, classifies each vehicle with the
  file-local `classify_vehicle_for_maintenance()`, applies the grouping percentage, runs the
  replacement block, stores each vehicle's duration, applies `vehicle_t::overhaul(context)` or
  `vehicle_t::maintain()`, sets the Convoy state to `OVERHAUL` or `MAINTENANCE`, registers the
  Convoy with `register_for_maintenance()`, queues its vehicles with
  `depot_t::queue_convoy_maintenance()`, sets an initial positive `wait_lock` when any vehicle has
  a non-zero duration, and appends the Convoy to `convois` [CODE].
- `vehicle_t::get_maintenance_duration()` returns
  `active_service_time * (100 - availability) / 100`. `vehicle_t::maintain()` applies the work and
  resets `active_service_time`, `km_since_last_maintenance` and `last_maintenance_month`; it no
  longer sets the Convoy state or `wait_lock` [CODE vehicle/vehicle.cc].
- `vehicle_t::get_overhaul_duration()` returns
  `(welt->ticks_per_world_month * desc->get_overhaul_month_tenths()) / 10`.
  `vehicle_t::overhaul(convoi_t *context)` applies the work, resets the counters, books the
  overhaul or upgrade cost, increments `overhauls`, applies an automatic upgrade and updates the
  livery; it no longer sets the Convoy state or `wait_lock`. `context` supplies the Convoy whose
  name and livery scheme are updated when the vehicle is loose [CODE vehicle/vehicle.cc].
- `convoi_t::remove_vehicle_at()` calls `v->set_convoi(NULL)` [CODE `simconvoi.cc`];
  `depot_t::remove_vehicle()` then appends the detached vehicle to `depot_t::vehicles`. The new
  duration and apply split means a detached vehicle can still be overhauled or maintained.
- `depot_t::register_for_maintenance()` appends the Convoy to `under_maintenance` [CODE
  simdepot.cc]. The capacity count and the Release Times are per vehicle, in
  `depot_t::maintenance_queue`; see [per-vehicle work](#per-vehicle-work-and-the-depot-queue).
- `depot_t::sync_step()` drops unbound or departed queue entries, completes jobs whose release
  time has been reached, starts waiting jobs while the depot has free maintenance places, and steps
  each Convoy in `under_maintenance` [CODE `simdepot.cc`].
- `convoi_t::sync_step()` asks the depot for `convoy_maintenance_complete()` and
  `convoy_maintenance_remaining()` while the Convoy is in `MAINTENANCE` or `OVERHAUL`; it does not
  count `wait_lock` down itself in those states [CODE `simconvoi.cc`].
- `depot_t::convois` is appended on arrival and removed by `remove_convoi()`, which is called from
  `sell_convoy()`, `start_convoi()` and `convoi_t::destroy()` [CODE `simdepot.cc`; `simconvoi.cc`].
  The automatic maintenance completion path reaches `convoi_t::step()`'s `ROUTE_JUST_FOUND` case,
  which calls `depot_t::start_convoi(self, false)`; that call removes the Convoy from `convois` and
  `under_maintenance`, and `start_convoi()` clears the Convoy's vehicle jobs. A Convoy that cannot
  start remains in the depot by design.
- `depot_t::convoi_arrived()` no longer calls `release_maintained_vehicles()` for a mixed visit.
  A blank order leaves the Convoy together; a non-blank order decomposes it and Depot Assembly
  re-forms it. See [Depot Consist Order execution](#depot-consist-order-execution).
- Maintenance and overhaul completion happens in `convoi_t::step()`: when `wait_lock <= 0`, it
  advances past the depot entry if that is still current and sets `ROUTING_1` [CODE
  `simconvoi.cc`]. The design's Target Fleet Size check and Depot Assembly must intercept this
  transition; otherwise an Original Convoy leaves the depot without the departure check. The
  natural hook is `depot_t::sync_step()`: before it lets the jobs of a Convoy complete, it checks
  the target and the pending assembly. If the target is met, it converts the Convoy to a Held
  Convoy. If the order is still waiting for other vehicles, it holds the Convoy in its maintenance
  state. In neither case does it let `convoi_t::step()` set `ROUTING_1`.

### Depot Consist Order execution

- `depot_t::convoi_arrived()` reads the current depot entry's order with
  `schedule_t::entry_has_consist_order()` and `schedule_t::get_consist_order()` [CODE].
  A blank order (no entry in `schedule_t::orders`, or a `consist_order_t` with no slots) leaves the
  Convoy together. A non-blank order triggers Decomposition.
- Decomposition takes the Lead Vehicle from `convoi_t::get_forward_consist()` and removes every
  other vehicle with `convoi_t::remove_vehicle()`. The moved vehicles are appended to
  `depot_t::vehicles`, receive a `loose_recipient` from the order's `available_to` setting, and
  receive a depot job with their already-computed duration. The Lead Vehicle remains the only
  vehicle of the Original Convoy [CODE `simdepot.cc`].
- `depot_t::awaiting_assembly` holds the Convoys whose Lead Vehicle is released but whose order is
  not yet satisfied. `convoy_maintenance_complete()` returns false for a Convoy in that list, so
  `convoi_t::sync_step()` does not let it leave [CODE].
- `depot_t::try_assemble_depot_orders()` runs from `depot_t::sync_step()`. For each held Convoy it
  builds the available list from `depot_t::vehicles` whose jobs are complete or absent, calls
  `convoi_t::match_consist_order()` with the Convoy's forward consist as the shadow, and requires
  the first chosen vehicle to be the Lead Vehicle [CODE].
- `depot_t::try_assemble_convoy()` appends the chosen pool vehicles to the Original Convoy with
  `convoi_t::add_vehicle()`, applies the chosen orientations, sets the leading and last flags,
  calls `convoi_t::check_validity()`, clears the Convoy's depot jobs and sets `wait_lock` to zero
  [CODE]. A failed match or validity check leaves the Convoy in the depot and reports once.
- `depot_t::try_form_new_convoy()` implements the New Convoy arrangement. It matches the order with
  an empty shadow against the available pool, creates a Convoy with `depot_t::add_convoi(false)`,
  moves the chosen vehicles, copies the source schedule past the depot with
  `copy_schedule_past_depot()`, sets the line, livery and name, checks the route and starts the
  Convoy with `convoi_t::start()` [CODE]. `depot_t::hold_original_convoy()` advances the source past
  the depot entry, sets it to `INITIAL` and removes its jobs and queue entry, so the source becomes
  a Held Convoy [CODE].
- `schedule_gui_t` shows a **Departure consist** tab on depot entries, with a
  `bt_depot_consist_order` button that opens the same `consist_order_frame_t` as the stop-level
  order [CODE `gui/schedule_gui.cc`].
- `release_maintained_vehicles()` and its split/held-Convoy helpers are removed. The former
  `maintenance-split`, `maintenance-split-invalid`, `maintenance-split-unreachable` and
  `maintenance-cross-convoy` cases are replaced by `depot-order` and `depot-order-exclude`
  [CODE `tests/pending-runtime/maintenance-cases/`].

### Depot vehicle pool and primitives

- `depot_t::vehicles` is the loose vehicle list. `depot_t::rdwr()` saves it with
  `rdwr_vehicle()`, so its order is saved [CODE simdepot.cc].
- `depot_t::new_month()` charges the monthly fixed cost of every loose, non-mothballed vehicle to
  the depot owner with `book_vehicle_maintenance()` [CODE `simdepot.cc`]. A vehicle inside a Convoy
  pays its fixed cost through `convoi_t::new_month()` [CODE `simconvoi.cc`]. The charge follows the
  owner of the depot or Convoy, as it does now.
- `depot_t::add_convoi()`, `depot_t::append_vehicle()`, `depot_t::remove_vehicle()`,
  `depot_t::remove_vehicles_to_end()`, `depot_t::sell_vehicle()`, `depot_t::buy_vehicle()` and
  `depot_t::find_oldest_newest()` are the vehicle-editing primitives [CODE simdepot.cc].
  `append_vehicle()` sets the vehicle position to the depot and adds it to the Convoy;
  `remove_vehicle()` appends the detached vehicle to `depot_t::vehicles` without changing its
  position; `sell_vehicle()` books the sale value and deletes the vehicle [CODE].
- `vehicle_t::loose_recipient_id` and `vehicle_t::loose_recipient_flags` already record which line
  or Convoy may collect a loose vehicle. `get_recombination_available_vehicles()` checks them for
  the laid-over pool [CODE simconvoi.cc; dataobj/consist_order_t.h].
- `depot_t::is_deletable()` refuses deletion while `vehicles`, `convois` or `under_maintenance`
  contain stock [CODE simdepot.cc].

### Convoy identity and departure

- `convoi_t::start(dep)` accepts only `INITIAL`, `ROUTING_1` and `ROUTE_JUST_FOUND`; it sets the
  home depot, adds the Convoy to the world sync list after a membership check, resets
  `no_load`, sets `ROUTING_1`, recalculates the metrics, and updates the line's category index
  [CODE `simconvoi.cc`]. An Original Convoy in `MAINTENANCE` or `OVERHAUL` must be moved to one of
  those accepted states before the depot starts it; the depot hook performs that transition when
  the Consist is assembled.
- `convoi_t::front()` is the array element at index 0; `convoi_t::get_forward_consist()` de-reverses
  the array without changing the vehicles' `is_reversed` flags [CODE simconvoi.cc]. The Lead
  Vehicle for the design is the first element of `get_forward_consist()`.
- `convoi_t::destroy()` awaits the path explorer and removes the Convoy from the depot's `convois`
  and `under_maintenance` lists before the destructor detaches its handle [CODE simconvoi.cc;
  simdepot.cc]. Any new vehicle or schedule deletion in the assembly path must use the same await
  rule.
- `simline_t::line_managed_convoys` is the line's Convoy list. It is not saved; `convoi_t::finish_rd()`
  rebuilds it. `simline_t::start_reversed` is the alternating start direction for a bidirectional
  line [CODE simline.h; simline.cc].
- `simline_t::has_depot_maintenance_slot()` counts a Convoy whose position is the depot tile unless
  it is a completed `INITIAL` held Convoy, and a Convoy whose current schedule entry is the depot
  [CODE `simline.cc`]. The design keeps this as an arrival policy; the Depot Vehicle Pool and Held
  Convoys do not count. A newly started Convoy whose front is still on the depot tile counts
  transiently and stops counting when it moves off the tile.

### Replacement

- `replace_data_t` (dataobj/replace_data.h) holds the replacement vehicle descriptors, the
  `replacement_time` enum, `retain_in_depot`, `use_home_depot`,
  `allow_using_existing_vehicles`, `autostart` and `number_of_convoys` [CODE].
- `convoi_t::replace_now()` (simconvoi.cc) reuses matching vehicles from the current Convoy, takes
  matching vehicles from the home depot with `depot_t::find_oldest_newest()`, upgrades in place
  when possible, buys new stock with `depot_t::buy_vehicle()`, sells or retains the old vehicles,
  rebuilds the Convoy in place, recalculates categories and clears `replace_data_t` [CODE].
- `depot_t::convoi_arrived()` runs the replacement block before the maintenance loops when
  `get_replace_at()` is `on_maintenance` or `on_overhaul` and the timing condition holds [CODE
  simdepot.cc].

### Line statistics

- `simline_t::get_load_factor_pax_last_month()`, `get_load_factor_pax_last_two_months()` and
  `get_load_factor_pax_year()` return a passenger load factor in tenths of a percent [CODE
  `simline.cc`]. Their guard is `!goods_catg_index.is_contained(goods_manager_t::INDEX_PAS)`, so a
  line without passengers returns 0.
- `LINE_CAPACITY` is booked only for vehicles whose cargo type is `goods_manager_t::passengers`
  [CODE `simconvoi.cc`]. It is seat-km, not a mixture of seats and tonnes.
- `LINE_GOODS_CAPACITY` is booked for freight vehicles, but not for passenger or mail vehicles. It
  uses the vehicle's capacity, the cargo weight per unit and the distance, the same units as
  `LINE_PAYLOAD_DISTANCE` [CODE `simconvoi.cc`]. The goods load factors use
  `LINE_PAYLOAD_DISTANCE / LINE_GOODS_CAPACITY`; see [fleet-size](fleet-size.md).
- Mail is measured separately by `LINE_MAIL_DISTANCE` and is excluded from both load factors.
- `simline_t::get_service_frequency()` computes a service frequency from the Convoys for which
  `convoi_t::in_depot()` is false [CODE `simline.cc`]. `convoi_t::in_depot()` returns true only for
  `INITIAL`, so a Convoy in `MAINTENANCE` or `OVERHAUL` is included by this test [CODE
  `simconvoi.h`].

## Reuse map

| Requirement | Existing entity | Gap |
|---|---|---|
| Describe the departing Consist | `consist_order_t`, `schedule_t::orders` | None for the first version; the data model already supports slots, alternatives, wildcards, optional slots and recipients. |
| Match an order against vehicles | `convoi_t::match_consist_order()`, `vehicle_t::matches_consist_order_element()`, `consist_order_t::check()` | The matcher is a `convoi_t` member with a current-Convoy shadow; a New Convoy has no current Convoy. A pool-only solver is needed, or a temporary candidate Convoy. |
| Read the depot pool | `depot_t::get_vehicle_list()` | The design draws only from the loose list. `get_recombination_available_vehicles()` also reads `access_convoy_list()`, which would dismantle other Convoys; the depot solver must not use that branch unchanged. Add release-time, recipient and due-work checks. |
| Detach vehicles | `convoi_t::remove_vehicle_at()`, `depot_t::vehicles.append()` | Set position, leading/last flags, recipient and release time; clear `cnv`. |
| Create a Convoy | `depot_t::add_convoi()` | Set schedule, line, name, livery, statistics booking and target check. |
| Copy the schedule past the depot | `copy_schedule_past_depot()` in simdepot.cc | File-local today; reuse or expose. It rejects a one-entry schedule. |
| Start a Convoy without GUI effects | `convoi_t::start(dep)` | Reuse; `depot_t::start_convoi()` has window and depot-frame side effects and is not suitable for automatic assembly. |
| Route check | `vehicle_t::calc_route()` | Reuse the call `depot_t::start_convoi()` makes. |
| Ownership | `player_t::allows_access_to()`, `consist_owners_permitted()` | The matcher already applies the Multi-Owner Access Rule; the depot solver must use the same helper. |
| Fixed costs | `depot_t::new_month()`, `convoi_t::new_month()` | Reuse; no new charge. |
| Recipients | `vehicle_t::loose_recipient_id`, `_flags` | Reuse; add depot-pool pickup checks. |
| Replacement | `replace_data_t`, `convoi_t::replace_now()` | Convert the replacement vehicle list into a one-shot order and defer the order's clear until departure. |
| Line target | None | New `simline_t::target_convoy_count`, a new `TOOL_CHANGE_LINE` case, a control and a save field. |
| Depot order UI | `consist_order_frame_t` | Show the settings tab on depot entries; add a depot pool/held view and diagnostics. |

## Required refactors

### Work duration without a Convoy

The current `vehicle_t::maintain()` and `vehicle_t::overhaul()` both set the Convoy state and
`wait_lock` and both dereference `cnv`. A detached vehicle has `cnv == NULL`. The refactor must
separate three things:

1. Computing the duration (from `active_service_time`, availability, or the overhaul month tenths).
2. Applying the work to the vehicle (counter resets, cost booking, overhaul count and upgrade).
3. Assigning the duration to the Convoy `wait_lock` or to the vehicle's Release Time.

`vehicle_t::overhaul(convoi_t *context)` reads `context->front()` and
`context->get_livery_scheme_index()` to rename the Convoy and update the livery when an automatic
upgrade applies [CODE `vehicle/vehicle.cc`]. `context` is the arriving Convoy, so a loose vehicle
can be overhauled with no Convoy by passing NULL; the livery update is then skipped. The
blank-order path now uses the same per-vehicle queue and produces the same total wait as a queue of
the Consist's vehicles rather than a multiplied whole-Convoy wait.

### Per-vehicle job state

`vehicle_t` holds:

- `sint64 depot_release_time`: the tick at which the work completes. 0 means no pending depot job.
- `uint8 depot_job_state`: `depot_job_none`, `depot_job_waiting`, `depot_job_in_progress` or
  `depot_job_complete`.
- `uint32 depot_queue_index`: the vehicle's position in the depot queue, used to rebuild the queue
  after a load without relying on pointer order.
- `sint64 depot_job_duration`: the length of the job in ticks, stored before `maintain()` resets
  `active_service_time`.

All four fields are saved in `vehicle_t::rdwr_from_convoi()` under `EX_SAVE_MINOR` 24, with
defaults 0 for older saves. A vehicle inside a Convoy may have a pending job while the Convoy is
under maintenance; `depot_t::rebuild_maintenance_queue()` restores the queue from the saved fields
after a load. A vehicle in `depot_t::vehicles` whose work is due is queued immediately unless it is
mothballed; the pool-side due-work check is not yet wired into `depot_t::sync_step()`.

The alternative is a depot-side list of `(vehicle_t*, release_time, state, queue_index, duration)`.
It avoids four fields on every vehicle but needs matching on load, because `vehicle_t` has no
persistent id. The per-vehicle fields are simpler and travel with the vehicle if the player moves
it; they are the implemented form.

### Per-vehicle work and the depot queue

Depot maintenance capacity is counted per vehicle, not per Convoy. Each vehicle in a maintenance
or overhaul visit receives a job on `vehicle_t`:

- `depot_job_state`: `depot_job_none`, `depot_job_waiting`, `depot_job_in_progress` or
  `depot_job_complete`;
- `depot_release_time`: the absolute tick when the job completes;
- `depot_queue_index`: the deterministic position in the depot queue;
- `depot_job_duration`: the length of the job in ticks.

`depot_t::maintenance_queue` holds the vehicles with jobs in queue order. The depot starts work on
up to `building_desc_t::get_max_vehicles_under_maintenance()` vehicles at a time
(`depot_t::start_depot_jobs()`), marks jobs complete at their release time
(`depot_t::complete_depot_jobs()`), and projects the release times of waiting jobs with one free
time per maintenance place (`depot_t::reproject_depot_jobs()`). A Convoy leaves when every one of
its vehicles is complete (`depot_t::convoy_maintenance_complete()`), whether or not a Depot Consist
Order decomposed it.

The queue is rebuilt after a load by `depot_t::rebuild_maintenance_queue()`, which scans
`depot_t::vehicles` and the vehicles of every Convoy in `convois`, sorts by `depot_queue_index`,
and repairs `next_maintenance_queue_index`. `depot_t::validate_maintenance_queue()` calls it from
`karte_t::load()`. The per-vehicle fields and the next index are saved under `EX_SAVE_MINOR` 24.

A vehicle in the Depot Vehicle Pool that becomes due for maintenance or overhaul is worked on as
soon as it becomes due, unless it is mothballed. The pool-side due-work check is not yet wired into
`depot_t::sync_step()`; the current phase queues vehicles that arrive in a Convoy. Mothballing has
no interface in the current code and is deferred.

### A pool-only solver

`convoi_t::match_consist_order()` can match an order against an arbitrary `available_vehicles` vector,
but it is a `convoi_t` member and uses the current Convoy's forward shadow for orientation and
displacement. A New Convoy has no current Convoy. The recommended implementation is a
`depot_consist_solver_t` that:

- takes the order, the Depot Vehicle Pool and, optionally, the Lead Vehicle;
- reuses `vehicle_t::matches_consist_order_element()`, the coupling predicates, the power and cab
  checks, the Multi-Owner Access Rule helper and the orientation helpers from `simconvoi.cc`;
  `consist_owners_permitted()` is file-local to `simconvoi.cc`, so a solver in another translation
  unit needs an exposed equivalent or must live in `simconvoi.cc`;
- returns the final Consist and orientations without changing state;
- uses a deterministic search order and the existing 16,384-candidate limit from
  `paired_exchange_solver_t` (an implementation limit, not a game quantity);
- is called by the commit path, which sets each vehicle's depot position and leading/last flags,
  moves the vehicles and starts the Convoy. `depot_t::append_vehicle()` performs the position and
  flag work for a Convoy; a direct `convoi_t::add_vehicle()` path must do the same.

Reusing `match_consist_order()` with a temporary empty `convoi_t` is possible but would misuse the
`convoi_t`'s displacement semantics and the Convoy's own vehicles. A separate solver is clearer.

### Path explorer

`schedule_t::parse_orders()` derives `catg_carried_from`, `catg_carried_to`,
`passenger_min_class_carried_from/to` and `mail_min_class_carried_from/to` from every entry that
has an order [CODE dataobj/schedule.cc]. A Depot Consist Order starts a Consist at the depot; it is
not a stop through-journey boundary and has no through passengers or goods. The function must skip
depot entries, or the path explorer can record invalid through connexions or a false category
change at the depot. This is a required change in `dataobj/schedule.cc`.


Persistence, network, determinism, performance, failure modes and the test plan are in
[remote-and-persistence](remote-and-persistence.md).
