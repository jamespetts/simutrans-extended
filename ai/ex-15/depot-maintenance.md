---
status: draft
verified: ex-15 @ 2777c3800a plus working-tree changes (2026-11-24)
---
# ex-15 depot maintenance and vehicle availability

**Covers:** `vehicle/vehicle.*`, `vehicle/air_vehicle.*`, `simconvoi.cc` (maintenance,
depot arrival and departure), `simdepot.*`, `descriptor/vehicle_desc.h`,
`descriptor/reader/vehicle_reader.cc`, `descriptor/writer/vehicle_writer.cc`,
`dataobj/settings.*` (maintenance intervals), `gui/convoi_detail_t.*`,
`gui/convoi_info_t.*`, `gui/depot_frame.*`, `player/finance.h` and
`player/simplay.cc` (maintenance booking). Parent:
[economy-and-vehicles](economy-and-vehicles.md).

Read this when working on routine depot maintenance, availability, maintenance
queues, overhaul timing or overhaul finance. Vehicle wear curves, staff costs,
fuel costs and prices are described in the parent registry. Range stops and
replenishment (refuelling or otherwise restoring a vehicle's range) are part of
the same maintenance feature programme but have their own implementation work:
see [range-stops-replenishment](range-stops-replenishment.md).
Replacement scheduling is also separate: see the vehicle-replacer section in
[economy-and-vehicles](economy-and-vehicles.md).

## Terms

- **Affordability** means that `finance_t::can_afford()` returns true for the
  price of the work. It respects the soft credit limit returned by
  `finance_t::get_soft_credit_limit()` and the `insolvent_purchases_allowed` and
  freeplay settings.
- **Availability** is the percentage of its working life for which a vehicle is
  available for transport service rather than being maintained.
- **Convoy** is the Simutrans name for a consist: the coupled vehicles represented
  by `convoi_t`.
- **Livery** is a vehicle's configured paint scheme.
- **Routine maintenance** is the periodic depot work derived from availability.
- **Overhaul** is the less frequent major work that resets the `km_since_last_overhaul` or
  takeoff (flight cycle) counters, resets availability to its starting value and can apply an automatic upgrade.
- **Urgent maintenance** is the overdue threshold that diverts a convoy to a
  depot instead of waiting for its next scheduled depot entry.
- **Active-service time** is the time that a vehicle spends available to operate
  rather than in storage, maintenance, overhaul or layover. The simulation
  counts driving, loading, waiting for a load or a signal, shunting and
  replenishment as active service; it excludes depot storage, layover,
  maintenance and overhaul. `convoi_t::sync_step()` implements this boundary.

## Settled design

### Availability, wear and maintenance due

- Running cost increases and availability decreases as the distance since the
  last overhaul increases from `availability_decay_start_km` to
  `max_distance_between_overhauls`. Aircraft use
  `availability_decay_start_takeoffs` and `max_takeoffs` for the equivalent
  progression. Both are bounded by descriptor values; there is no separate
  tuning curve to invent.
- Routine maintenance is due after `maintenance_interval_km`, or after the
  `maintenance_interval_months` setting when that descriptor value is zero.
  `simplified_maintenance` suppresses the requirement.
- Urgent maintenance is due at 1.5 times the kilometre interval, or after
  `extended_maintenance_interval_months` when the vehicle uses the months-based
  interval. An urgently overdue convoy stops loading and is sent to a depot at
  its first stop through `convoi_t::emergency_go_to_depot()`. That function
  teleports to a depot when no route exists and sells the convoy when no suitable depot exists.
- Deferring an overhaul is permitted with `do_not_overhaul`. The vehicle then
  remains at maximum running cost and minimum availability; those penalties are
  the intended cost of deferral, not an extra tuning rule.

### Depot visits

- A schedule entry at a depot may carry `conditional_skip`. The entry is skipped
  unless `convoi_t::is_maintenance_needed()` is true. When simplified maintenance
  is disabled, new schedules created from a depot append such an entry automatically.
  A schedule is not required to contain a depot; urgent maintenance covers a convoy without one.
- Arrival at a conditionally skipped depot entry sets
  `maintain_or_overhaul` in the temporary visit flags through
  `convoi_t::enter_depot`. The flag is not stored on the schedule entry.
- A maintenance or overhaul visit keeps the convoy attached to its line and
  resumes at the current schedule position. Only a `store` visit detaches the
  convoy from its line.
- When one vehicle needs routine maintenance, all vehicles in the convoy are
  maintained on that visit. Vehicles due for overhaul are placed in the overhaul
  group unless `do_not_overhaul` is set; the remainder are maintained normally.
- The convoy's maintenance duration is the longest individual vehicle duration,
  not the sum. A convoy larger than the depot capacity receives a proportional
  time multiplier.
- Depot capacity `max_vehicles_under_maintenance` is measured in vehicles. A
  convoy at the head of `depot_t::under_maintenance` always progresses; convoys
  behind it have their countdown frozen while they await attention.
  `depot_t::prioritise_for_maintenance()` moves a convoy to the front and
  `depot_t::depriortise_for_maintenance()` moves it to the back. Those methods
  define the queue policy; only controls that call them are missing.
- Maintenance cannot be interrupted once started. `convoi_t::start()` accepts
  only the stored or routing states, so a convoy under maintenance cannot be
  started through the ordinary depot controls.

### Maintenance duration

The design record contains two worked examples:

- 90% availability after 10 hours in service requires 1 hour in the depot;
- 75% availability after 1 hour in service requires 15 minutes in the depot.

The implemented formula, consistent with both examples, is
`maintenance_time = active_service_time * (100 - availability) / 100` `[CODE]`.

Both examples produce no maintenance time at 100% availability and the full service-time coefficient at 0%. The exact ratio
`active_service_time * (100 - availability) / availability` would produce different answers and is a design change,
not an implementation of the examples `[UNVERIFIED]`. The examples are recorded in the maintenance discussion
[FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html] and the depot-visits section of
[project-roadmap](../project-roadmap.md).

`vehicle_t::maintain()` implements this duration with the per-vehicle
`active_service_time` counter. `convoi_t::sync_step()` adds the sync-step
interval to every vehicle in the convoy while the convoy is in an active state:
driving, loading, waiting for a load or a signal, shunting, replenishing and
schedule waiting. The counter is excluded while the convoy is in `INITIAL`,
`EDIT_SCHEDULE`, `LAYOVER`, `MAINTENANCE` or `OVERHAUL`. `vehicle_t::maintain()`
and `vehicle_t::overhaul()` reset the counter. The counter is serialised in
`vehicle_t::rdwr()` under Extended save revision 21
([savegame-versioning](../savegame-versioning.md)); older saves start at zero.

`convoi_t::enter_depot()` and `convoi_t::ziel_erreicht()` preserve the wait for a
maintenance or overhaul visit. Other depot visits clear it. `vehicle_t::maintain()`
clamps availability to 100 and has no division by availability, so a zero
availability value produces a full service-time wait rather than a division by
zero.

### Overhauls and upgrades

- Each vehicle becomes due for overhaul independently from its own distance or
  takeoff count. There is no convoy-wide or line-wide overhaul-due rule.
- An overhaul resets the overhaul and maintenance counters, resets availability
  through those counters, increments the overhaul count and records the overhaul
  month.
- `initial_overhaul_cost`, `max_overhaul_cost` and
  `overhauls_before_max_cost` define the escalating overhaul price.
  `vehicle_desc_t::get_initial_overhaul_cost()` and
  `vehicle_desc_t::get_max_overhaul_cost()` apply the `vehicle_maintenance`
  price index. `vehicle_t::overhaul()` calculates the price before incrementing
  `overhauls`, so the first overhaul uses `initial_overhaul_cost` and each
  later overhaul uses the next point on the escalation curve. An
  overhaul-specific financial category remains outstanding; the payment is
  currently recorded in `ATV_VEHICLE_MAINTENANCE`.
- An overhaul is a one-off cost
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html]. It
  must not be treated as a recurring monthly fixed cost.
- When `auto_upgrade_index` resolves and `do_not_auto_upgrade` is false, the
  overhaul applies the upgrade and charges the upgrade price instead of the
  ordinary overhaul price. Maintenance history is retained.
- An overhaul updates an owned vehicle to the latest available livery in the
  current livery scheme. A vehicle owned by another player in a mixed-owner
  convoy keeps its livery.
- When the current scheme has no available livery and is obsolete, the livery
  falls back to the first scheme in settings order that has an available livery
  for the vehicle. The existing vehicle builder and convoy assembler use this
  deterministic rule. `vehicle_t::update_livery()` still needs to apply it.

### Maintenance accounting

`player_t::book_vehicle_maintenance()` records a signed amount in
`ATV_VEHICLE_MAINTENANCE` and adjusts the account balance by that amount. Fixed
monthly costs and mothballing charges are passed as negative amounts.
`player_t::book_vehicle_maintenance_one_off()` records a one-off payment without
applying `calc_adjusted_monthly_figure()`. `vehicle_t::overhaul()` passes the
negated overhaul or upgrade price to that method.

If an overhaul or automatic upgrade is unaffordable, `vehicle_t::overhaul()`
returns false without changing the vehicle. A scheduled conditional depot entry
is skipped before routing when an overhaul is unaffordable and
`convoi_t::is_maintenance_urgently_needed()` is false
(`convoi_t::advance_schedule()`). A convoy for which that test is true still
enters the depot, performs no maintenance work and is set to `INITIAL`, so it
remains in the depot until the player can pay.

Fixed costs continue during layover; `convoi_t::new_month()` charges them and
`convoi_t::sync_step()` discounts only salaries while the convoy is in
`LAYOVER`.

## Outstanding design decisions

These change game behaviour and are not settled by the code or the recorded
design. Recommendations are not decisions and are marked `[UNVERIFIED]`.

### Grouping overhauls of vehicles that are nearly due

Overhauls are independently timed per vehicle. The design record contains a
proposal to overhaul vehicles together when their overhaul dates are near, but no
definition of "near", no selected scope and no decision that this mechanic is
wanted. A fixed percentage would be arbitrary unless grounded in a real workshop
scheduling interval or a descriptor value.

The proposed default is to retain independent timing for 15.x and assess the
observed problem after maintenance works correctly `[UNVERIFIED]`. Grouping is a
separate mechanic.

### Economic meaning of simplified maintenance

`simplified_maintenance` currently disables routine maintenance, urgent
diversion and overhaul requirements. The original design proposed two
economically comparable modes and represented availability in the purchase price
of simplified-mode vehicles. That proposal does not specify an internally
consistent formula: its `1 / availability` wording would add one third to a
75%-available vehicle, while its example says one quarter. It also does not say
whether the adjustment applies to resale value or how pre-15 saves, which load
with simplified maintenance enabled, should be handled.

A decision is required between retaining the current setting as a switch that
disables the mechanic without modelling its economic effect, or completing the
availability-adjusted purchase-price model with a new exact formula and a
legacy-save policy. No change should be made under the old description because it is not precise
enough to implement safely. The original material is
[FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html].

### Convoy trigger for replacement on overhaul

`replace_data_t::replacement_time` defines replacement on replenishment,
maintenance, overhaul, immediate, manual and automatic. The unresolved part is
what "on overhaul" means when a convoy has several independently timed vehicles:
any vehicle due, all vehicles due, or a nominated vehicle such as the locomotive
[FORUM:https://forum.simutrans.com/index.php/topic,16980.0.html].

The implementation currently replaces the whole convoy when any vehicle is in
the overhaul group. That is a usable default, but the mode is not exposed in the
graphical user interface. Confirm the any-vehicle rule before exposing the mode,
or select a different rule. Overhaul and replacement execution is otherwise
future work; only the `on_maintenance` trigger condition is corrected now.

### Cargo at a maintenance visit

`depot_t::convoi_arrived()` calls `vehicle_t::discard_cargo()` for every vehicle,
including a vehicle that arrives for routine maintenance or an overhaul. There is
no code that guarantees the convoy is empty before it enters the depot:
`convoi_t::get_depot_when_empty()` applies only to replacement and manual visits,
and `schedule_entry_t::set_down_only` and `schedule_entry_t::discharge_payload`
are explicit schedule flags that the player must set. A scheduled maintenance
visit can therefore arrive loaded and delete its load.

The 2016 design intended that the decision to visit a depot be made when the
convoy skips a depot entry, so that the convoy can be empty when it arrives
[FORUM:https://forum.simutrans.com/index.php/topic,15473.0.html]. No mechanism
implements that advance decision. The choices are to keep the load through the
maintenance visit, to defer the visit until the convoy is empty, or to unload the
load at the stop before the depot. The choice is not settled.

### Resale value and the second-hand market

`vehicle_t::calc_sale_value()` depreciates by age and by whether the vehicle has
been driven; it does not consider wear or overhaul history. The preferred
direction is an in-game second-hand market with player-set prices and a scrap
price fallback, so a wear-adjusted formula is not required now.

### Per-line depot limit

The 2016 design proposed one convoy per line per depot and a visit decision
before arrival. Neither exists, so every convoy of a line can become due
together and leave service together. The available policies are under
investigation.

## Outstanding user-interface work (future session)

- **Maintenance queue controls.** Add depot-window controls that call
  `depot_t::prioritise_for_maintenance()` and
  `depot_t::depriortise_for_maintenance()`. The serialised queue order affects
  wait times, so the controls must commit through a depot tool command.
- **Mothball controls.** Add controls for `vehicle_t::mothball()` and
  `vehicle_t::un_mothball()` for vehicles stored in a depot, committed through a
  depot tool command.
- **Replacement timing controls.** Expose `on_maintenance` and `on_overhaul`
  from `replace_data_t::replacement_time` in `gui/replace_frame.cc`. Leave
  `on_replenish` hidden until the range-stop runtime exists.
- **Fleet overhaul view.** A list of vehicles due for overhaul, optionally in a
  future fleet-management window, would help players manage large fleets.

## Open questions

- How should availability values above 100 be validated? `vehicle_t::maintain()`
  clamps availability to 100, but the descriptor reader does not reject a
  larger value.
- Do the depot trigger states named
  `conditional_depart_before_wait` and `conditional_depart_after_wait` implement
  the recorded before-wait and after-wait meanings? The design meanings are
  explicit; this is an implementation verification, not an open mechanic.
- Can the maintenance countdown and queue survive save/reload exactly? The queue
  and convoy state are serialised and validated, but no behavioural test exists.
- The scheduled-depot edge cases in the Open questions section of
  [schedule-and-consists](schedule-and-consists.md) remain separate defects.

## Resumption order

1. Add behavioural tests for duration, queue progress, urgent diversion,
   affordability and overhaul accounting.
2. Verify save/reload and two-process determinism of the active-service counter,
   queue and overhaul financial state.
3. Apply the livery-scheme fallback in `vehicle_t::update_livery()`.
4. Add the outstanding user-interface work listed above.
5. Decide the cargo-at-maintenance policy and the per-line depot limit.
6. Decide simplified maintenance, overhaul grouping and the replacement trigger
   before exposing those controls.
7. Re-run the checks with vehicles that have moved between consists, so each
   vehicle keeps its own maintenance and active-service history.
