---
status: draft
verified: ex-15 @ 8ad14f9615 (2026-11-24, maintenance article and status, revenue classification, cargo policy, 2016 depot-visit intent, early decision and last-halt discharge); livery fallback @ 6656e7dce0 (2026-11-24); maintenance test hook and overhaul runtime case @ d921be1fa0 (2026-11-24); working tree 2026-11-24 (per-line depot limit, consist overhaul grouping, simplified-maintenance wear bypass and purchase-price compensation, replacement delay); working tree 2026-11-25 (automatic release of maintained vehicles, cross-convoy combination, sync-list fix)
---
# ex-15 depot maintenance and vehicle availability

**Covers:** `vehicle/vehicle.*`, `vehicle/air_vehicle.*`, `simconvoi.cc` (maintenance,
depot arrival and departure), `simdepot.*`, `simline.*` (per-line maintenance settings),
`descriptor/vehicle_desc.h`,
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
- **Livery scheme** is a named set of liveries with introduction and retirement
  dates, represented by `livery_scheme_t`.
- **Depot maintenance place** is the right of one convoy of a line to use a depot's
  maintenance capacity at a time. `simline_t::max_convoys_at_depot` sets how many of a
  line's convoys may hold a place at each depot.
- **Overhaul group** is the set of vehicles that `depot_t::convoi_arrived()` classifies for
  `vehicle_t::overhaul()` on one depot visit.
- **Overhaul grouping percentage** is the fraction of its own overhaul interval that a
  vehicle must have covered to join an overhaul visit caused by another vehicle in the same
  convoy. It is stored per line and as a global setting.
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
  A vehicle that is not yet due but has covered at least the line's overhaul
  grouping percentage of its own interval joins the overhaul group; see
  Overhauls and upgrades.
- The convoy's maintenance duration is the longest individual vehicle duration,
  not the sum. A convoy larger than the depot capacity receives a proportional
  time multiplier.
- Depot capacity `max_vehicles_under_maintenance` is measured in vehicles. A
  convoy at the head of `depot_t::under_maintenance` always progresses; convoys
  behind it have their countdown frozen while they await attention.
  `depot_t::prioritise_for_maintenance()` moves a convoy to the front and
  `depot_t::depriortise_for_maintenance()` moves it to the back. Those methods
  define the queue policy; only controls that call them are missing.
- A line stores `simline_t::max_convoys_at_depot`, the maximum number of its
  convoys that may hold a depot maintenance place at the same depot at the same time.
  `convoi_t::advance_schedule()` checks `simline_t::has_depot_maintenance_slot()`
  before committing to a conditional depot entry, and
  `convoi_t::turn_at_skipped_depot()` uses the same test. A convoy counts
  against the limit while its current schedule entry is that depot tile (it is
  approaching the depot) or while its position is that depot tile (it is
  maintained, overhauled or held for an unaffordable overhaul). This count is
  conservative: a convoy that has already decided to skip the entry still
  counts until its schedule moves past it. A convoy that finds no place
  skips the entry and remains in service. Urgent maintenance bypasses the limit.
  The value defaults to 1 and is saved per line under Extended save revision 23.
  The control to change it is deferred.
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
  takeoff count. There is no convoy-wide or line-wide rule that makes a vehicle
  due; the grouping rule below only decides which vehicles join a visit.
- When a convoy enters a depot with `maintain_or_overhaul` and at least one
  vehicle is due, `depot_t::convoi_arrived()` also places every other vehicle in
  the overhaul group when it has covered at least the line's
  `overhaul_grouping_percentage` of its own overhaul interval (or takeoff count
  for aircraft), unless `do_not_overhaul` is set or its owner cannot afford the
  overhaul. The test is `vehicle_t::is_overhaul_nearly_due()`, overridden by
  `air_vehicle_t::is_overhaul_nearly_due()`. The percentage defaults to 90, is
  read from `settings_t::overhaul_grouping_percentage` and simuconf for new
  lines and lineless convoys, and is saved per line under Extended save revision
  23. 0 disables grouping. Grouping applies only within one convoy, not across a
  line or the player's fleet.
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
- On an overhaul visit, a replacement configured with
  `replace_data_t::on_overhaul` runs before the overhaul loop when the overhaul
  group is non-empty. `convoi_t::replace_now()` replaces the whole convoy,
  because `replace_data_t` describes one complete replacement consist; the
  overhaul group is the trigger, not the set of vehicles replaced. The visit
  waits for the longer of `settings_t::shunting_time_seconds` and the longest
  overhaul duration of any vehicle that `convoi_t::replace_now()` upgraded in
  place, because an in-place upgrade is overhaul-equivalent work. `on_maintenance`
  remains a trigger for any maintenance visit. The `on_overhaul` mode is not
  exposed in `gui/replace_frame.cc`.
- When `auto_upgrade_index` resolves and `do_not_auto_upgrade` is false, the
  overhaul applies the upgrade and charges the upgrade price instead of the
  ordinary overhaul price. The upgrade charge is also revenue expenditure.
  Maintenance history is retained. The upgrade keeps the overhaul duration,
  because it runs inside `vehicle_t::overhaul()`.
- An automatic upgrade is not accounted for as a replacement transaction: the
  old vehicle is not sold and the upgrade price is not split between revenue and
  capital. The whole charge is revenue expenditure. A replacement model is not
  planned for 15.x [RECOLLECTION:2026-11-24 user statement].
- An overhaul updates an owned vehicle to the latest available livery in the
  current livery scheme. In a convoy whose vehicles belong to more than one
  player, a vehicle owned by a player other than the convoy's owner keeps its
  livery.
- When the scheme selected by `convoi_t::get_livery_scheme_index()` has no
  livery available for the vehicle and the scheme is no longer available (that
  is, `livery_scheme_t::is_available()` returns false),
  `vehicle_t::update_livery()` calls
  `livery_scheme_t::get_latest_available_livery()` for each scheme in the order
  returned by `settings_t::get_livery_schemes()` and uses the first non-null
  result. If every scheme returns null, it uses the `"default"` livery.
  `vehicle_builder_t::build()` and
  `gui_convoy_assembler_t::get_latest_available_livery_image_id()` use the same
  search order.

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

The pending-runtime `maintenance-overhaul` case sets the overhaul distance through
`convoy_x.set_maintenance_distances_for_testing()`, then asserts that the visit
increments the overhaul count, resets `km_since_last_overhaul`, returns availability
to the descriptor's starting value, and reduces cash by the vehicle's `overhaul_cost`
within a bound that allows the running costs booked on the final approach. The
scenario-only hook and the case are recorded in
[scripting-and-tests](../scripting-and-tests.md) [EXECUTION-VERIFIED:2026-11-24].

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

### Simplified maintenance

The project decision for 15.x is to keep `simplified_maintenance` as a switch and
to make it internally consistent. When the setting is true:

- `convoi_t::is_maintenance_needed()`,
  `convoi_t::is_maintenance_urgently_needed()` and
  `convoi_t::is_overhaul_needed()` return false.
- `convoi_t::create_schedule()` and the depot line-creation path do not append
  the automatic conditional depot entry.
- `vehicle_t::get_availability()` and `air_vehicle_t::get_availability()` return
  the descriptor's starting availability; `vehicle_t::get_running_cost()`
  returns the descriptor's base running cost; and
  `vehicle_t::is_wear_affecting_vehicle()` returns false. `vehicle_t::step_km()`
  still advances the distance counters, so the getters would resume the
  accumulated wear if the setting were later turned off.
- `vehicle_desc_t::get_value()` multiplies the inflation-adjusted price by
  `100 / starting_availability`, rounded up. This is the cost-balancing
  compensation: a vehicle that is available for only part of its working life
  costs as much as the number of vehicles needed to cover that life. The
  multiplier applies to purchase, sale and asset value because all three read
  `get_value()`. It does not apply to `vehicle_desc_t::get_upgrade_price()`.
- The forum text says that a 75%-available vehicle costs 25% more, but its
  stated `1 / availability` formula produces 33.3% more. The stated formula is
  implemented; the example is arithmetically inconsistent. The original
  material is
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html].
- Pre-15 saves load with `simplified_maintenance` forced true. Their existing
  vehicles are valued by the multiplier whenever `vehicle_desc_t::get_value()`
  is called, so their sale and asset values change. This is the legacy-save
  policy.

The mode is therefore cheaper to operate but more expensive to buy. It is not
precisely economically comparable to full maintenance, and play-testing is
needed to confirm that the formula balances.

### Releasing maintained vehicles from overhauling convoys

The grouping rule above applies within one convoy at arrival. This rule applies
across two convoys of the same line. In the recorded case, train A's locomotive
and train B's carriages are due for overhaul, while A's carriages and B's
locomotive need only routine maintenance. The maintenance-only vehicles finish
earlier. The rule forms a serviceable train from those vehicles and starts it
while the overhaul sets remain in the depot. This does not shorten the overhaul;
it releases the maintenance-only vehicles instead of leaving them idle.

`depot_t::convoi_arrived()` calls `release_maintained_vehicles()` after the
replacement block and after `vehicle_t::maintain()`/`vehicle_t::overhaul()` have
been applied, and before `register_for_maintenance()`. The rule is:

- The arriving convoy must have a non-empty overhaul set and a non-empty
  maintenance-only set. It must be line-bound, have a non-empty schedule, must
  not have been replaced on this visit and must not be held for an unaffordable
  overhaul.
- If another convoy of the same line has maintenance or overhaul work due and is
  routed to the same depot, the arriving maintenance-only vehicles are held for
  it. The held vehicles are placed in a new line-bound convoy in state
  `INITIAL` in the depot, with a copy of the schedule advanced one entry past
  the depot. This is the **held convoy**. It is an ordinary stored convoy and is
  saved by the existing format.
- When the second convoy arrives, its maintenance-only vehicles are added to the
  held convoy. The rule tries the new vehicles at the front, then appended, and
  starts the first arrangement that is powered, coupling-valid, has a front cab
  and can find a route to the next schedule entry. The start uses
  `convoi_t::start()`, not `depot_t::start_convoi()`, so it has no window or
  depot-frame effects.
- A trailer moves only when its coupled leader moves. The test is
  `vehicle_desc_t::get_trailer()` on the leading vehicle, so a steam tender, an
  articulated bus section and a tram section stay with their powered unit.
- The two original convoys keep their overhaul sets, line, `wait_lock`,
  schedule position and depot registration. They advance past the depot entry
  when their overhauls finish. The combined convoy starts at the first arrival's
  next stop; the original convoys are one schedule entry out of phase.
- The overhaul set is not validated. If the leftover vehicles cannot form a
  train after the overhaul, the player must re-form them. This is deliberate for
  the recorded locomotive/carriages case: validating the overhaul set would
  refuse the split.
- The per-line limit is unchanged. The recorded case needs
  `simline_t::max_convoys_at_depot` to be at least 2 when the depot entry is
  conditional, because the second convoy otherwise skips the depot. The
  runtime case uses a plain depot entry, so it works with the default of 1.

The rule is covered by `maintenance-split`, `maintenance-split-invalid`,
`maintenance-split-unreachable` and `maintenance-cross-convoy` in
`tests/pending-runtime/maintenance-cases/`. The invalid and unreachable cases
cover the undo path. The cross-convoy case covers the two-train combination.

The composition of consists from non-identical rolling stock, the treatment of
the leftover overhaul sets, and combinations of more than two convoys are not
settled. They are recorded as open decisions below.

## Decisions, deferrals and open questions

This section records the decisions taken for 15.x, the work intentionally
deferred, and the remaining open questions. Recommendations are not decisions
and are marked `[UNVERIFIED]`.

### Early decision for scheduled maintenance visits (implemented; schedule-index deferred)

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
schedule-index byte. The project maintainer has deferred that byte until player
feedback from a completed 15.x shows that schedules with more than one
conditional depot need it. This is an intentional deferral, not an open design
question; the last-halt set-down remains the protection.

The per-line limit that decides whether a convoy may take a depot maintenance
place is recorded in the Depot visits section above.

### Composition of consists from non-identical rolling stock (open)

The automatic release and combination rules assume that the maintenance-only
vehicles from two convoys can be fitted together and that a trailer stays with
its leader. They do not define:

- how a consist may be composed from different but compatible vehicle types;
- whether the game should check the leftover overhaul set, or automatically
  re-form it after the overhaul;
- how more than two convoys, or several possible partners, should be paired.

These are open design questions. They may require a fuller vehicle-management
system. The current rule handles the two-convoy case and leaves the leftover
overhaul sets to the player.

### Resale value and the second-hand market (open)

`vehicle_t::calc_sale_value()` depreciates by age and by whether the vehicle has
been driven; it does not consider wear or overhaul history. The preferred
direction is an in-game second-hand market with player-set prices and a scrap
price fallback, so a wear-adjusted formula is not required now.

## Outstanding user-interface work (future session)

- **Per-line depot maintenance limit control.** The value is stored and
  enforced, but no control exists. Add a line-management control that commits
  through `TOOL_CHANGE_LINE`; the tool case is not yet present.
- **Per-line overhaul grouping percentage control.** The value is stored and
  defaults from simuconf, but no control exists. Add one that commits through
  `TOOL_CHANGE_LINE`.
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
- Does the `100 / starting_availability` purchase-price compensation balance?
  The recorded formula and the recorded 75% example disagree; the formula is
  implemented, but no play-test has compared simplified and realistic modes.
- Does the per-line depot limit behave correctly when a convoy is held in
  `INITIAL` for an unaffordable overhaul? The convoy counts against the limit
  and urgent maintenance bypasses it, but no runtime case tests the interaction.
- Does the 90% grouping rule produce the intended number of overhauls in a mixed
  consist? The rule is implemented, but no runtime case sets two vehicles at
  different distances.
- A mirrored schedule that reverses at a stop keeps the early decision taken in
  the incoming direction; whether that needs a reset on the direction change is
  not yet verified.
- The scheduled-depot edge cases in the Open questions section of
  [schedule-and-consists](schedule-and-consists.md) remain separate defects.

## Resumption order

1. Add behavioural tests for the per-line depot limit, the 90% grouping rule,
   the replacement delay, duration, queue progress, urgent diversion and
   affordability.
2. Verify save/reload and two-process determinism of the new line and settings
   fields, the active-service counter, the queue and the overhaul financial
   state.
3. Add the outstanding user-interface work listed above, including the deferred
   per-line controls.
4. Re-run the checks with vehicles that have moved between consists, so each
   vehicle keeps its own maintenance and active-service history.
5. Reconsider a cross-convoy overhaul exchange and a schedule-index byte only
   after player feedback from a completed 15.x.
