---
status: draft
verified: ex-15 @ 2777c3800a plus working-tree changes (2026-11-24); revenue classification, cargo policy and 2016 depot-visit intent @ 872055d188 (2026-11-24); early decision and last-halt discharge @ working tree (2026-11-24)
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
- A maintenance or overhaul visit discards the load. `depot_t::convoi_arrived()`
  calls `vehicle_t::discard_cargo()` for every vehicle. This is correct: the
  vehicle must be empty for maintenance, and an urgently overdue convoy cannot
  continue in service with its load
  [RECOLLECTION:2026-11-24 user statement]. The general depot rule is that a
  vehicle entering a depot leaves its ordinary route and its cargo is lost
  [FORUM:https://forum.simutrans.com/index.php/topic,13206.0.html].
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
  later overhaul uses the next point on the escalation curve. The payment is
  recorded in `ATV_VEHICLE_MAINTENANCE`; see Maintenance accounting.
- An overhaul is revenue expenditure ([cost-of-capital](../cost-of-capital.md))
  and is a one-off cost
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html]. It
  must not be treated as a recurring monthly fixed cost or as capital
  expenditure.
- When `auto_upgrade_index` resolves and `do_not_auto_upgrade` is false, the
  overhaul applies the upgrade and charges the upgrade price instead of the
  ordinary overhaul price. The upgrade charge is also revenue expenditure.
  Maintenance history is retained.
- An automatic upgrade is not accounted for as a replacement transaction: the
  old vehicle is not sold and the upgrade price is not split between revenue and
  capital. The whole charge is revenue expenditure. A replacement model is not
  planned for 15.x [RECOLLECTION:2026-11-24 user statement].
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
negated overhaul or upgrade price to that method. An overhaul is revenue
expenditure: the payment reduces `ATV_OPERATING_PROFIT` and does not enter the
Asset Ledger or the Tax Written-Down Pool
([cost-of-capital](../cost-of-capital.md)). Unlike a way replacement, there is
no vehicle classifier that splits the charge between revenue and capital.

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

### Advance decision for scheduled maintenance visits

The 2016 design records a requirement, not a complete algorithm. Its stated purpose
is to avoid convoys dumping cargo on a depot visit
[FORUM:https://forum.simutrans.com/index.php/topic,15473.0.html]:

- the decision to visit a depot, and the reservation of a maintenance spot at that
  depot, is made each time the convoy skips the depot on its schedule, rather than
  when it arrives;
- each depot automatically services one convoy per line at a time;
- each depot has a maximum number of convoys that it can service at once;
- all depot visits use the same rules, including a manual Go To Depot and a future
  Upgrade order;
- Jamespetts noted that consideration would also need to be given to a mechanism
  that stops goods, passengers and mail bound for a stop beyond the depot from
  loading onto the convoy.

The thread does not define the exact decision point or the reservation mechanism; its
author deferred a separate discussion of depot visit behaviour.

The early decision is implemented:

- The decision is stored in three spare bits of the byte that holds
  `is_choosing` and `last_stop_was_depot` in `convoi_t`:
  `conditional_depot_present`, `conditional_depot_decided` and
  `conditional_depot_visit`. The bits add no size to `convoi_t`.
- `convoi_t::reset_conditional_depot_state()` runs when the schedule is set,
  when a line schedule update is applied, and when the convoy enters a depot. It
  records whether the schedule has a conditional depot and clears the decision.
- `convoi_t::update_conditional_depot_state()` runs at every halt before loading.
  The first halt after the reset evaluates, once, the same predicate as the late
  decision: `convoi_t::is_maintenance_needed()` and either
  `convoi_t::is_maintenance_urgently_needed()` or
  `convoi_t::can_afford_overhaul_visit()`. The result is binding for the next
  conditional depot. Urgent maintenance overrides a skip in
  `convoi_t::advance_schedule()` and `convoi_t::turn_at_skipped_depot()`.
- `haltestelle_t::fetch_goods()` walks the schedule from the current stop. If the
  decision is to visit, the walk stops at the first `conditional_skip` depot entry
  and refuses the packet. Cargo whose next transfer or destination is before the
  depot still loads. The filter applies only when the loading schedule is the
  convoy's own schedule; a split-off portion loaded against a target schedule is
  not covered.
- `convoi_t::is_last_halt_before_depot()` identifies the last halt before a depot.
  While the decision is to visit, `convoi_t::unload_individual_vehicle_at()` is
  called with `force_discharge`, so the convoy is emptied at that halt and arrives
  at the depot empty. This is the safety net for cargo loaded before the decision
  and for a later conditional depot that the state does not track.
- The state is saved as one byte under Extended save revision 22. Older saves start
  undecided and evaluate at the next halt.

The decision covers only the next conditional depot. A schedule may contain more
than one; later entries keep the late decision in
`convoi_t::advance_schedule()`. Tracking a specific depot would need a
schedule-index byte; whether that is worth the memory is an open question.

The reservation requirements (one convoy per line per depot, and the depot's
maximum number of convoys) are not implemented; see Per-line depot limit below.

### Resale value and the second-hand market

`vehicle_t::calc_sale_value()` depreciates by age and by whether the vehicle has
been driven; it does not consider wear or overhaul history. The preferred
direction is an in-game second-hand market with player-set prices and a scrap
price fallback, so a wear-adjusted formula is not required now.

### Per-line depot limit

The 2016 design proposed one convoy per line per depot and a visit decision
before arrival, as described under Advance decision for scheduled maintenance
visits above. The visit decision is implemented; the per-line limit is not, so
every convoy of a line can become due together and leave service together. The
available policies are under investigation.

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
- Should the early conditional-depot decision track a specific depot with a
  schedule-index byte? The current state covers only the next conditional depot.
  A second conditional depot therefore keeps the late decision and relies on the
  last-halt discharge if it is visited. A mirrored schedule that reverses at a
  stop also keeps the decision taken in the incoming direction; whether that needs
  a reset on the direction change is not yet verified.
- The scheduled-depot edge cases in the Open questions section of
  [schedule-and-consists](schedule-and-consists.md) remain separate defects.

## Resumption order

1. Add behavioural tests for duration, queue progress, urgent diversion,
   affordability and overhaul accounting.
2. Verify save/reload and two-process determinism of the active-service counter,
   queue and overhaul financial state.
3. Apply the livery-scheme fallback in `vehicle_t::update_livery()`.
4. Add the outstanding user-interface work listed above.
5. Decide whether to add a schedule-index byte to track a specific conditional
   depot; decide the per-line depot limit.
6. Decide simplified maintenance, overhaul grouping and the replacement trigger
   before exposing those controls.
7. Re-run the checks with vehicles that have moved between consists, so each
   vehicle keeps its own maintenance and active-service history.
