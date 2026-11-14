---
status: draft
verified: ex-15 @ 235a9eddf plus paired reload fixture @ fa85a59b8 and documentation changes (2026-11-14)
---
# Consist recombination

**Covers:** dataobj/schedule.h and dataobj/schedule_entry.h (couple/uncouple flags, target ids,
unique entry ids), dataobj/consist_order_t.* (consist orders and slot matching), simconvoi.* (the
runtime: `convoi_t::hat_gehalten`, `process_consist_order`, `commit_recombined_consist`,
`resolve_uncouple_target`, `form_divided_portion`, `resolve_paired_exchange`,
`execute_paired_exchange`, `commit_paired_exchange`), simhalt.* (laid-over pool and loading convoys),
vehicle/vehicle.* (slot matching and loose recipients), and tests/pending-runtime. Signalling and
reservation consequences: [signals-and-blocks](signals-and-blocks.md). Design and feature status:
[ex-15/recombination-design](ex-15/recombination-design.md),
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md). Cargo and passenger continuity:
[ex-15/recombination-design/cargo-continuity](ex-15/recombination-design/cargo-continuity.md). Path
explorer: [ex-15/yh-path-explorer](ex-15/yh-path-explorer.md). Tests:
[scripting-and-tests](scripting-and-tests.md).

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
available vehicles, enforces `convoi_t::check_validity()` coupling (head, adjacent and tail),
deduplicates a vehicle already used in an earlier slot, and accepts the result only when it is
complete and powered. It does not backtrack; the paired exchange uses the joint solver instead.

`convoi_t::commit_recombined_consist` moves, substitutes, adds and removes vehicles, unloads the
displaced vehicles that become loose stock, and creates a laid-over convoy for the rest. For a
division, the discharge of the split candidate is deferred until `form_divided_portion` fails, so a
promoted portion keeps its load. The commit unreserves the receiving convoy's reservations and calls
`convoi_t::reserve_own_tiles()`; `convoi_t::form_divided_portion` removes the promoted portion from
and re-adds it to `welt->sync`.

`convoi_t::commit_paired_exchange` handles both sides of a pair atomically: only vehicles that leave
both final consists are discharged and laid out as loose stock; direct transfers keep their load; a
laid-over donor emptied by the exchange is deregistered and destroyed. A changed convoy waits out
`shunting_time_seconds`; an unchanged consist is not detached or repopulated. A changed counterpart
waits in `LOADING` under the same lock so that its new consist loads after the delay.

An emptied pool donor on the single-order paths (targetless assembly and one-sided receive) can remain
in the halt's laid-over list until the monthly sweep; this is an open bug in
[known-bugs](known-bugs.md).

## States, once-only execution and saved state

An absent counterpart is waited for in `convoi_t::AWAITING_RECOMBINATION`, deliberately without a
timeout. A direct mutual wait resolves because either convoy's step re-resolves the pair. Longer wait
cycles are not detected.

The executing convoy records the stop in both convoys' `convoi_t::last_stop_id` for a paired exchange;
a promoted portion records it in its own. A later loading pass therefore computes `first_run == false`
and does not execute the operation again. No new saved field is added by any recombination path: the
consists, `last_stop_id`, `arrival_time`, `wait_lock`, `reverse_schedule` and vehicle positions are
written by the existing `convoi_t` and `vehicle_t` save methods, and `AWAITING_RECOMBINATION` is
written by `convoi_t::rdwr`. Savegame and network versioning are unchanged. Determinism follows the
ordinary convoy rules; see [sync-and-determinism](sync-and-determinism.md) and
[savegame-versioning](savegame-versioning.md).

## Platform, reservations and call-on

The reservation consequences are in [signals-and-blocks](signals-and-blocks.md) and
[block-reservations](signals-and-blocks/block-reservations.md). In summary: reservations are per
convoy handle with no per-portion identity, every commit releases and re-reserves the changed
convoy's tiles, and a train cannot reserve through a tile held by a standing convoy. A joining
portion can therefore enter an occupied block or platform only up to the standing train's tile, under
permissive working with the call-on aspect, and only from a stand.

Call-on is implemented as general permissive working: at a stand at a permissive signal under
absolute block, track circuit block or cab signalling with `no_junctions_to_next_signal`, a failed
reservation sets `roadsign_t::call_on`, switches the train to `drive_by_sight` and re-runs
`rail_vehicle_t::block_reserver` with `from_call_on = true`. It admits the train to the occupied
block up to the standing train's tile. The planned use of call-on for shunting movements into an
occupied platform or yard as part of a recombination is not yet implemented: the recombination runtime
does not request call-on and does not move a convoy into an occupied platform, and `SHUNTING` is only
a timed pause. Call-on-based coupling movements remain part of the platform/call-on work.

## Tests

`tests/pending-runtime` covers the runtime through the public script interface: the division
baseline cases in `cases/`, the paired-exchange cases in `pair-cases/`, the division
save/network reload case in `reload/`, and the paired-exchange save/network reload case in
`pair-reload/`. The harnesses and runners are described in
[scripting-and-tests](scripting-and-tests.md).

## Open questions

This feature set is incomplete. The runtime design, the resolved decisions and the remaining open
questions are maintained in [ex-15/recombination-design](ex-15/recombination-design.md) and
[ex-15/schedule-and-consists](ex-15/schedule-and-consists.md). The main outstanding items are
freight/passenger continuity (including the intended loose-stock preloading and retrospective
loading credit), wait-cycle detection beyond a direct pair, reversal-aware picking, the per-portion
validity gate, call-on/platform shunting movements, and the GUI authoring pre-check.

Should the term "convoy"/"convoi" be phased out entirely in the code and this knowledge base as well
as the UI?

## Provenance

Code-verified against ex-15 @ 235a9eddf plus paired reload fixture @ fa85a59b8 and documentation changes. The detailed
design history and forum provenance are in [ex-15/recombination-design](ex-15/recombination-design.md).
