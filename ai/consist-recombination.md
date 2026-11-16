---
status: draft
verified: ex-15 @ 2debf73ed (2026-11-15, consist-order check, facing-neutral picking and failure warning); ex-15 @ 71f192a65 (2026-11-15, contact point carried behind the rear vehicle); ex-15 @ b501f13f3 (2026-11-15, choose-signal resolvability pre-check, shared-contact reservation handoff and pool-donor destruction); ex-15 @ 117c75d17 (2026-11-15, coupling-point assertions and one-sided receive layout); ex-15 @ ed6d940be (2026-11-15, shared contact tile and paired layout); ex-15 @ 0692167b6 (2026-11-15, choose-signal call-on core and scripted cases); ex-15 @ 97029f453 (2026-11-14, remainder); paired reload fixture @ fa85a59b8; ex-15 @ 03bf5306e (2026-11-16, committed-target hold corrected and both scripted cases); ex-15 @ 9fdefa153 (2026-11-16, orientation choice, reversal delay, facing case and substitution discharge)
---
# Consist recombination

**Covers:** dataobj/schedule.h and dataobj/schedule_entry.h (couple/uncouple flags, target ids,
unique entry ids), dataobj/consist_order_t.* (consist orders and slot matching), simconvoi.* (the
runtime: `convoi_t::hat_gehalten`, `process_consist_order`, `commit_recombined_consist`,
`resolve_uncouple_target`, `form_divided_portion`, `resolve_paired_exchange`,
`execute_paired_exchange`, `commit_paired_exchange`, `can_approach_coupling_target`,
`report_coupling_hold`, `get_recombination_contact_pos`), simhalt.* (laid-over pool and loading
convoys), vehicle/rail_vehicle.* (`hand_over_contact_reservation`), vehicle/vehicle.* (slot matching
and loose recipients). Signalling and reservation consequences: [signals-and-blocks](signals-and-blocks.md).
Design and feature status: [ex-15/recombination-design](ex-15/recombination-design.md),
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md). Cargo and passenger continuity:
[ex-15/recombination-design/cargo-continuity](ex-15/recombination-design/cargo-continuity.md). Path
explorer: [ex-15/yh-path-explorer](ex-15/yh-path-explorer.md). Scripting and tests:
[scripting-and-tests/consist-recombination](scripting-and-tests/consist-recombination.md). Shared-contact
reservation handoff: [reservation-handoff](consist-recombination/reservation-handoff.md) — read when
changing `rail_vehicle_t::hand_over_contact_reservation`, the reciprocal approach link, or the shared
contact tile.

## Terms

- **Consist.** The ordered list of vehicles a convoy runs with. The project intends to replace the
  term "convoy" with "consist" in the UI by the completion of work on version 15 (ex-15).
- **Consist recombination.** Any operation that changes the vehicles in one or more consists at a
  stop: a locomotive change, a multiple unit splitting or joining, a through coach attached or
  detached, a pick-up goods train dropping or collecting loose wagons, or an assembly from the
  stop's laid-over pool. The two common patterns are a division (one convoy into two portions) and a
  paired exchange (two convoys exchanging vehicles); they are not the whole subject.
- **Consist order (end-state order).** The ordered vehicle slots a schedule entry requires, stored in
  `schedule_t::orders` keyed by the entry's `unique_entry_id`. Each slot has prioritised
  alternatives: a specific `vehicle_desc_t` or a rule over vehicle attributes.
- **Couple entry / uncouple entry.** A schedule entry carrying the `couple` or `uncouple` flag. Its
  target id names a line or a convoy according to `couple_target_is_line_or_cnv` /
  `uncouple_target_is_line_or_cnv`; its target unique-entry id names the counterpart entry.
- **Receiver / target.** In a paired exchange, the convoy whose entry carries `couple` is the
  receiver and the convoy whose entry carries `uncouple` is the target.
- **Joining convoy.** The convoy a one-sided receive draws vehicles from.
- **Mover.** The convoy that travels to the other convoy's contact tile under a committed
  choose-signal approach. The other convoy is the approach target.
- **Call-on.** A movement authority that admits a train into an occupied block or platform tile. In a
  coupling movement it admits the mover into the target's contact tile while the target keeps the
  tile's reservation.
- **Approach link.** The reciprocal pair `convoi_t::recombination_approach_partner` and
  `convoi_t::recombination_approach_is_mover`, recorded when the choose signal adopts the route to
  the target. It authorises the mover's call-on entry and is cleared when the coupling is executed.
- **Reservation holder.** The convoy handle stored in `schiene_t` for a rail tile. A tile has at most
  one holder.
- **Coupling point.** The position at which the mover's front meets the target's rear vehicle: the
  target rear vehicle's `steps` value minus that vehicle's descriptor length in vehicle steps.
- **Split-off portion.** The new convoy a division forms from the source convoy's displaced
  vehicles.
- **Laid-over pool / loose stock.** Convoys in `LAYOVER` at the stop; their vehicles are loose stock
  available to a targetless order and, for the receiver only, to a paired exchange.
- **Discharge boundary.** A vehicle that remains in a scheduled consist keeps its load; a vehicle that
  becomes loose stock is discharged at the stop. The decision, including the intended loose-stock
  preloading and retrospective loading credit, is in
  [cargo-continuity](ex-15/recombination-design/cargo-continuity.md).

## What the player authors

The player writes the operation into the schedule and the schedule entry's consist order:

- `couple` with a target: the convoy receives vehicles from the named line or convoy. If the target
  entry carries `uncouple`, the two entries form a paired exchange; otherwise the receive is
  one-sided.
- `uncouple` with a target: the convoy divides; the split-off portion takes the target schedule.
  `uncouple` with target id 0 is a plain uncouple, which releases the displaced vehicles as loose
  stock without waiting for a recipient.
- Neither flag with a consist order: targetless assembly from the convoy's own vehicles and the
  stop's laid-over pool. (The depot branch in `process_consist_order` is not reached: the only call
  site passes no depot.)
- An entry carrying both flags is refused: it is not a division and not a valid pair.

The consist order is authored on the schedule entry. Typical uses are a locomotive change at an
electrification boundary, a through coach that moves between two services, a multiple unit split or
join, and a pick-up goods train that drops loaded wagons for a later service or collects wagons left
at a yard. The schedule-order rules and the feature status are in
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md).

The entry's window checks the list it shows. `consist_order_t::check` evaluates the whole list against
the vehicle types the order may draw on and reports the first problem with the affected slot (the
window numbers slots as positions, counting from the front of the train): an empty list, a slot with
no alternative, a slot whose rules match no vehicle, a break in the coupling chain, a first or last
vehicle that cannot be at its end of the train, or no vehicle providing power.
`consist_order_frame_t::update_order_check` shows that verdict in colour with an explanation. For a
couple or uncouple entry it also names the counterpart and states that each side of a divided or
joined train must be able to run on its own, and it always states that the list runs from the front
of the train to the rear in the direction it travels. The verdict is advisory: it
never blocks an edit, a save or the order. A named vehicle is assumed to be available, because a
couple entry may draw it from another train, and the check cannot know which way round a joining
vehicle will be. When the runtime cannot carry an order out, the convoy posts one warning per stop
and entry with `convoi_t::report_consist_order_failed`.

## Runtime paths

`convoi_t::hat_gehalten` enters the recombination block on the first loading pass at a stop when the
current entry has a consist order or a couple/uncouple flag. `convoi_t::process_recombination_at_stop`
first resolves a possible pair; if the entry is not a pair it runs the ordinary path.

### Targetless assembly

`convoi_t::process_current_entry_consist_order` calls `convoi_t::process_consist_order` with no joining
convoy. The available vehicles are the convoy's own plus the stop's laid-over convoys, filtered by
waytype. (The depot branch in `process_consist_order` is dead: `hat_gehalten` always passes no depot.)
The order is matched against those vehicles and committed with `convoi_t::commit_recombined_consist`.
This is also the path of a plain uncouple (`uncouple` with target id 0): the displaced vehicles are
discharged and laid out as loose stock.

### One-sided receive

For a `couple` entry, `schedule_t::get_couple_target` looks for the named convoy or a convoy of the
named line among the convoys loading at the stop. A present target becomes the `joining_convoy`;
`process_consist_order` then draws from both consists. The joining convoy does not run its own order
as part of the operation. A target that exists elsewhere is waited for in
`convoi_t::AWAITING_RECOMBINATION`; a target that does not resolve is reported once with
`convoi_t::report_broken_couple_target`. If the joining convoy is emptied it is deregistered from the
layover pool and destroyed with `convoi_t::self_destruct`.

### Division

For an `uncouple` entry (without `couple`), `convoi_t::resolve_uncouple_target` resolves the target
line or convoy and its schedule entry. The source entry's order runs with a split-candidate handle, so
the displaced vehicles are collected into a newly created laid-over convoy rather than an existing
one (`commit_recombined_consist` with the split candidate). `convoi_t::form_divided_portion` applies
the target entry's order to that convoy; only a complete, powered result is promoted. A promoted
portion joins the target line or receives a copy of a lineless target convoy's schedule, starts at the
target entry in the direction given by `uncouple_target_sch_is_reversed`, and enters `LOADING` under
the `shunting_time_seconds` delay. A plain, unresolved, unsatisfiable or unpowered division leaves
the displaced vehicles as loose stock. The source's retained portion takes the existing commit state:
`SHUNTING` when the consist changed, otherwise it stays in `LOADING`.

### Paired exchange

A receiver entry (`couple`) and a target entry (`uncouple`) at the same stop, each naming the other,
form a pair. `convoi_t::resolve_paired_exchange` validates the reciprocity, the halt, the waytype and
the orders; a present counterpart makes the pair ready, an absent one is waited for in
`AWAITING_RECOMBINATION`. `convoi_t::execute_paired_exchange` solves both end-state orders jointly and
`convoi_t::commit_paired_exchange` replaces both consists as one operation. The solver rules are in
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md): a canonical universe of both
forward-oriented consists plus the receiver's eligible laid-over pool, per-slot candidate priority,
`convoi_t::check_validity()` coupling, power in both results, pairwise-disjoint final consists, the
multi-owner access rule, the smallest possible pool draw, and a 16,384 candidate-placement compute
limit. Failure is all-or-nothing: neither consist changes, one warning is posted, the counterpart is
released from `AWAITING_RECOMBINATION`, and both convoys continue.

## Matching and commit

`convoi_t::process_consist_order` matches one order with a heuristic pass and a pass-over retry at
the failed slot: it fills slots in order, tries the vehicle already in the slot first, then the
available vehicles, enforces the coupling predicates (head, adjacent and tail) in the orientation
chosen for each vehicle, deduplicates a vehicle already used in an earlier slot, and accepts the
result only when it is complete and powered. It does not backtrack; the paired exchange uses the
joint solver instead.

The single-order matcher (`convoi_t::process_consist_order`) and the joint solver
(`paired_exchange_solver_t`) choose an orientation for every vehicle they place. A candidate is
tried in its current `is_reversed` orientation first; if it is bidirectional and that orientation
does not fit, the other way round is tried. The chosen orientation is recorded per slot. The
single-order matcher returns it in a vector parallel to the final consist, and the joint solver
stores it per assigned slot. `convoi_t::commit_recombined_consist` and
`convoi_t::commit_paired_exchange` apply the chosen value with `vehicle_t::set_reversed`, so a
bidirectional vehicle can be turned during the shunt. For each bidirectional vehicle whose
orientation changes, the commit adds `settings_t::get_unit_reverse_time()` when the vehicle is
powered or `settings_t::get_turntable_reverse_time()` when it is hauled to the shunting delay. The
editor's list verdict is facing-neutral because it cannot know which vehicle another consist will
supply or which way round that vehicle will be; the runtime resolves that choice.

`convoi_t::commit_recombined_consist` moves, substitutes, adds and removes vehicles, unloads the
displaced vehicles that become loose stock (including a vehicle substituted out of the consist),
and creates a laid-over convoy for the rest. For a
division, the discharge of the split candidate is deferred until `form_divided_portion` fails, so a
promoted portion keeps its load. When the receiver is the shared-contact mover and its route ends at
the joining convoy's contact tile, the commit records both consists' vehicle positions before
detaching them and lays the final groups out on those slots with `vehicle_t::set_layout_position`: the
joining convoy's remainder in the front slots, the receiver's final consist behind it and loose stock
behind that. The joining convoy releases and re-takes its reservations around the repositioning. The commit
unreserves the receiving convoy's reservations and calls `convoi_t::reserve_own_tiles()`;
`convoi_t::form_divided_portion` removes the promoted portion from and re-adds it to `welt->sync`.

`convoi_t::commit_paired_exchange` handles both sides of a pair atomically: only vehicles that leave
both final consists are discharged and laid out as loose stock; direct transfers keep their load; a
laid-over donor emptied by the exchange is deregistered and destroyed. A changed convoy waits out
`shunting_time_seconds`; an unchanged consist is not detached or repopulated. A changed counterpart
waits in `LOADING` under the same lock so that its new consist loads after the delay. When the
receiver reached the target's contact tile under a committed approach, the commit lays the final
consists out on the tiles the pre-exchange vehicles occupied, front convoy first and loose stock
behind both, using `vehicle_t::set_layout_position`; each changed convoy releases its previous
reservations with `convoi_t::unreserve_route()` and re-reserves with
`convoi_t::reserve_own_tiles()` after positioning. Without that physical order, each changed consist
is placed at its former rear vehicle position.

A vehicle detached for layout has no convoy until it is attached to its final or loose consist, so
the commit clears every detached vehicle's leading and last flags before moving it and sets those
flags on the re-formed consists. An emptied laid-over donor on any path is deregistered and
destroyed at the commit rather than at the monthly sweep.

## States, once-only execution and saved state

An absent counterpart is waited for in `convoi_t::AWAITING_RECOMBINATION`, deliberately without a
timeout. A direct mutual wait resolves because either convoy's step re-resolves the pair. Longer wait
cycles are not detected.

The executing convoy records the stop in both convoys' `convoi_t::last_stop_id` for a paired exchange;
a promoted portion records it in its own. A later loading pass therefore computes `first_run == false`
and does not execute the operation again. The consists, `last_stop_id`, `arrival_time`, `wait_lock`,
`reverse_schedule` and vehicle positions are written by the existing `convoi_t` and `vehicle_t` save
methods, and `AWAITING_RECOMBINATION` is written by `convoi_t::rdwr`. The reciprocal coupling
approach link (`convoi_t::recombination_approach_partner` and
`convoi_t::recombination_approach_is_mover`) is saved; the coupling-point offset
`convoi_t::recombination_contact_steps` is transient and recomputed.
Determinism follows the ordinary convoy rules; see [sync-and-determinism](sync-and-determinism.md)
and [savegame-versioning](savegame-versioning.md).

## Platform, reservations and call-on

The reservation consequences are in [signals-and-blocks](signals-and-blocks.md) and
[block-reservations](signals-and-blocks/block-reservations.md). In summary: reservations are per
convoy handle with no per-portion identity, every commit releases and re-reserves the changed
convoy's tiles, and a train cannot reserve through a tile held by a standing convoy. A joining
portion can therefore enter an occupied block or platform only up to the standing train's tile, under
call-on and only from a stand.

Call-on has two forms. Ordinary permissive working applies at a stand at a permissive signal under
absolute block, track circuit block or cab signalling with `no_junctions_to_next_signal`: a failed
reservation sets `roadsign_t::call_on`, switches the train to `drive_by_sight` and re-runs
`rail_vehicle_t::block_reserver` with `from_call_on = true`, admitting the train to the occupied
block up to the standing train's tile.

Choose-signal call-on is target-specific and intrinsic to every choose signal. For an entry carrying
`couple`, or the reciprocal `uncouple`, `convoi_t::get_recombination_target_state()` distinguishes a
target that is not yet committed, a target that has passed the last signal before the stop, and a
target stationary at the stop. A committed target holds the mover at the choose signal. An available
target first passes `convoi_t::can_approach_coupling_target()`: the choose signal tests, without
changing state, that the configured operation can produce valid powered consists. A paired exchange
runs the joint solver; a one-sided receive runs the single-order matcher. A failure keeps the signal
at danger and posts one warning with `convoi_t::report_coupling_hold()`, naming both owners and the
reason, so a mover whose check fails is not routed to the target. The verdict is cached
on `convoi_t::recombination_precheck_signature` and recomputed when either consist or the stop's
laid-over pool changes. While the target is committed, every reservation pass keeps the mover held:
the recorded approach link alone does not count as a selected route, because
`rail_vehicle_t::has_selected_coupling_target_route` also requires the current route to end at the
partner's contact point. An available target that passes the check causes the choose signal to route
to that target's contact tile; the mover is admitted under call-on into that tile and applies the
coupling-point stop, while the target keeps the reservation.
`convoi_t::is_recombination_contact_tile` requires the mover's `call_on` flag, the reciprocal
approach link and a route ending at the target's contact tile; `vehicle_t::hop` sets the tile's
`steps_next` to the coupling-point offset computed by
`convoi_t::get_recombination_contact_ground`. `has_call_on` is display-only, and call-on always
switches the train to `drive_by_sight`. `rail_vehicle_t::hand_over_contact_reservation()` transfers the
tile's reservation to the convoy that remains on it when the holder's last vehicle leaves, and
suppresses the ordinary release when another convoy still occupies a tile it holds. The two branches,
the approach link's lifetime and the window in which the transfer can run are in
[reservation-handoff](consist-recombination/reservation-handoff.md). The contact
point is carried back through every tile the rear vehicle's length covers, so a rear vehicle that
straddles a tile boundary is coupled to in the tile containing its rear end. `SHUNTING` is a timed
pause, not a movement.

## Tests

The scripting and testing material — fixture layout, harnesses, every case and assertion, the
runners, the cadence and the planned coverage — is in
[scripting-and-tests/consist-recombination](scripting-and-tests/consist-recombination.md).

## Open questions

This feature set is incomplete. The runtime design, the resolved decisions and the remaining open
questions are maintained in [ex-15/recombination-design](ex-15/recombination-design.md) and
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md). The main outstanding items are
freight/passenger continuity (including the intended loose-stock preloading and retrospective
loading credit), wait-cycle detection beyond a direct pair, and the off-map pool package. The
runtime chooses a joining vehicle's facing for a bidirectional vehicle: a candidate is tried in its
current orientation first, then the other way round. An order is not limited by the orientation in
which another consist presents that vehicle. Whether the runtime should turn a vehicle only where
the stop has a reversal facility remains open; the runtime turns it during the shunt and charges
the existing unit or turntable reversal time. The shared-contact reservation handoff's
release-suppression half is scripted by `contact-reservation`. The transfer half has no
scripted case: it covers a target that starts departing after the choose signal adopts the approach
but before the mover's first recombination pass. The mechanism and the window are in
[reservation-handoff](consist-recombination/reservation-handoff.md#the-transfer-window); whether the
branch is a defensive case is the open design question.

Should the term "convoy"/"convoi" be phased out entirely in the code and this knowledge base as well
as the UI?

## Provenance

Choose-signal resolvability pre-check and pool-donor destruction verified against ex-15 @ b501f13f3
(2026-11-15); the contact point carried behind the rear vehicle and the handoff predicate against
ex-15 @ 71f192a65 (2026-11-15); choose-signal call-on code and its scripted cases against ex-15 @
0692167b6 (2026-11-15); remaining content against ex-15 @ 97029f453 (2026-11-14); paired reload
fixture @ fa85a59b8; the committed-target hold correction and the `committed-target` and
`contact-reservation` cases against ex-15 @ 03bf5306e (2026-11-16); the orientation choice, reversal
delay and `facing` case against ex-15 @ 9fdefa153 (2026-11-16). The detailed design history and
forum provenance are in [ex-15/recombination-design](ex-15/recombination-design.md).
