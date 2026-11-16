---
status: draft
verified: ex-15 @ 6a9e8d963
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
replenishment (refuelling or otherwise restoring a vehicle's range) are
separate work: see [range-stops-replenishment](range-stops-replenishment.md).
Replacement scheduling is also separate: see the vehicle-replacer section in
[economy-and-vehicles](economy-and-vehicles.md).

## Terms

- **Availability** is the percentage of its working life for which a vehicle is
  available for transport service rather than being maintained.
- **Convoy** is the Simutrans name for a consist: the coupled vehicles represented
  by `convoi_t`.
- **Livery** is a vehicle's configured paint scheme.
- **Routine maintenance** is the periodic depot work whose duration is derived
  from availability.
- **Overhaul** is the less frequent major work that resets the `km_since_last_overhaul` or
  takeoff (flight cycle) counters, resets availability to its starting value and can apply an automatic upgrade.
- **Urgent maintenance** is the overdue threshold that diverts a convoy to a
  depot instead of waiting for its next scheduled depot entry.
- **Active-service time** is the time that a vehicle spends available to operate
  rather than in storage, maintenance, overhaul or layover. Its precise
  accounting boundary is an open design question below.

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

The formula consistent with both examples is
`maintenance_time = active_service_time * (100 - availability) / 100` `[UNVERIFIED]`.

Both examples produce no maintenance time at 100% availability and the full service-time coefficient at 0%. The exact ratio
`active_service_time * (100 - availability) / availability` would produce different answers and is a design change,
not an implementation of the examples `[UNVERIFIED]`. The examples are recorded in the maintenance discussion
[FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html] and the depot-visits section of
[project-roadmap](../project-roadmap.md).

The current implementation does not implement this duration:

- `vehicle_t::maintain()` calculates
  `(welt->get_ticks() - last_maintenance_time) * 100 / get_availability()`.
  This uses elapsed wall-clock time rather than active-service time and does not
  match either worked example.
- After `depot_t::convoi_arrived()` sets the wait through `maintain()` or
  `overhaul()`, `convoi_t::enter_depot()` clears `wait_lock`. Its caller
  `convoi_t::drive_to()` clears it again after `enter_depot()` returns.

The separate defects are recorded in [known-bugs](../known-bugs.md). They must
be corrected together: preserving the current calculated wait would preserve the
wrong duration.

### Overhauls and upgrades

- Each vehicle becomes due for overhaul independently from its own distance or
  takeoff count. There is no convoy-wide or line-wide overhaul-due rule.
- An overhaul resets the overhaul and maintenance counters, resets availability
  through those counters, increments the overhaul count and records the overhaul
  month.
- `initial_overhaul_cost`, `max_overhaul_cost` and
  `overhauls_before_max_cost` define the escalating overhaul price. An
  overhaul-specific financial category is intended for 15.x
  [RECOLLECTION:2026-09-07]; the current booking uses the ordinary
  vehicle-maintenance category.
- An overhaul is a one-off capital cost
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html]. It
  must not be treated as a recurring monthly fixed cost and must use the normal
  inflation-adjustment rule for vehicle maintenance prices.
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
monthly costs and mothballing charges are passed as negative amounts. An
overhaul payment is therefore required to be a negative amount.

The current overhaul path passes a positive price. It also applies
`calc_adjusted_monthly_figure()` through
`finance_t::book_vehicle_maintenance_with_bits()` and uses descriptor values that
are not inflation-adjusted. This credits money instead of charging it, scales a
one-off payment as though it were monthly, and bypasses the project's inflation
rule. The defect and its three consequences are recorded in
[known-bugs](../known-bugs.md).

## Outstanding design decisions

These change game behaviour and are not settled by the code or the recorded
design. Recommendations are not decisions and are marked `[UNVERIFIED]`.

### Measurement of active-service time

The relation between service time and maintenance duration is settled by the
worked examples. The unresolved question is how to measure service time. The
roadmap permits either direct measurement or an estimate from distance and
average speed; it does not select one.

A direct per-vehicle accumulator is the proposed default `[UNVERIFIED]`. It
must preserve each vehicle's history when vehicles move between consists. The likely
boundary is that active-service time includes driving, loading, traffic waiting,
schedule waiting, shunting and ordinary replenishment, and excludes depot
storage, layover, maintenance and overhaul `[UNVERIFIED]`.

Direct measurement requires persistent per-vehicle state. A savegame/network
proposal and version decision are therefore required before implementation, per
[savegame-versioning](../savegame-versioning.md) and
[sync-and-determinism](../sync-and-determinism.md). Estimating from distance and
average speed avoids that state but is less faithful after speed changes and
after vehicles move between consists; no reason to prefer it has been recorded.

### Affordability of an automatic overhaul

No recorded rule says what happens when an automatic overhaul or automatic
upgrade price is unaffordable. The choices are to charge the account regardless,
defer the operation with a warning, or stop the convoy at the depot until the
player acts.

The proposed default is to defer the selected operation, charge nothing, issue a
rate-limited warning and retain the existing overdue penalties `[UNVERIFIED]`.
Work on other vehicles should continue rather than blocking the depot queue. An
unaffordable auto-upgrade should defer rather than silently substitute an
ordinary overhaul, unless the user decides otherwise.

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
or select a different rule.

## Implementation work requiring no new mechanic

- Correct and test maintenance duration, including retention of the wait through
  depot entry and departure.
- Correct overhaul sign, one-off accounting, inflation adjustment and financial
  category.
- Apply the existing deterministic livery-scheme fallback in
  `vehicle_t::update_livery()`.
- Add maintenance queue controls in the depot window, calling the existing
  priority methods.
- Add `MAINTENANCE`, `OVERHAUL` and `REPLENISHING` status text to the convoy
  information window. Replenishment has its own runtime work; only the display is
  listed here.
- Add graphical controls for the already implemented mothball/unmothball
  mechanic.
- Decide whether to expose the existing maintenance and overhaul replacement
  timings after the convoy-trigger decision above.
- Add behavioural maintenance tests. The active Squirrel scripted test suite currently checks
  maintenance-related schedule fields and convoy-state constants, but does not
  exercise maintenance duration, availability, queue progress, urgent diversion
  or overhaul accounting.

## Open questions

- Does the active-service boundary include time spent waiting for a load or a
  signal, or only time with a booked driver? The salary accumulator in
  `convoi_t::sync_step()` treats driving, loading and waiting as active and
  discounts layover, maintenance and overhaul. Reusing that boundary is the
  proposal, not a recorded decision.
- How should a zero availability value be handled? The current duration formula
  would divide by zero; the intended duration formula would make every service
  interval a maintenance interval. Descriptor validation and failure behaviour
  need a separate decision only if zero is permitted.
- Do the depot trigger states named
  `conditional_depart_before_wait` and `conditional_depart_after_wait` implement
  the recorded before-wait and after-wait meanings? The design meanings are
  explicit; this is an implementation verification, not an open mechanic.
- Can the maintenance countdown and queue survive save/reload exactly? The queue
  and convoy state are serialised and validated, but no behavioural test exists.
- The scheduled-depot edge cases recorded in the Open questions section of
  [schedule-and-consists](schedule-and-consists.md) remain separate defects in
  the schedule layer.

## Resumption order

1. Decide active-service measurement, overhaul affordability and simplified
   maintenance. Grouping and replacement triggering can follow before their
   respective user interfaces are added.
2. Add a behavioural maintenance fixture and reproduce the duration and
   accounting defects.
3. Repair duration, accounting, inflation and livery fallback with regression
   tests.
4. Add queue controls and convoy status text.
5. Add the service-time state if direct measurement is selected; present the
   savegame/network change before implementation.
6. Verify save/reload and two-process determinism.
7. Re-run the checks with vehicles that have moved between consists, so each vehicle keeps its own maintenance and service history.
