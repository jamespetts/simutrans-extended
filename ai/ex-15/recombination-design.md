---
status: draft
verified: ex-15 @ 2debf73ed (2026-11-15, consist-order check, facing-neutral picking and failure warning); ex-15 @ 71f192a65 (2026-11-15, contact point carried behind the rear vehicle); ex-15 @ b501f13f3 (2026-11-15, choose-signal resolvability pre-check, shared-contact reservation handoff and pool-donor destruction); ex-15 @ 117c75d17 (2026-11-15, coupling-point assertions and one-sided receive layout); ex-15 @ ed6d940be (2026-11-15, shared contact tile, approach link and paired layout); ex-15 @ 0692167b6 (2026-11-15, choose-signal call-on core and scripted cases); ex-15 @ 97029f453 (2026-11-14, remainder); design revised 2026-11-07; discharge, Multi-Owner Access and paired runtime 2026-11-13; intended loose-stock treatment and general signalling API 2026-11-14; shared-contact coupling package defined 2026-11-15; ex-15 @ 03bf5306e (2026-11-16, committed-target hold corrected and the release suppression scripted); ex-15 @ 9fdefa153 (2026-11-16, orientation choice, reversal delay and facing case); ex-15 @ bf157b7a7 plus working-tree pick-up goods package (2026-11-17, cargo continuity, through-wagon model, flash-load credit, optional slots and loaded/empty rules); ex-15 @ 93da398e6 (2026-11-18, Multi-Owner Access Rule at assembly); ex-15 @ 9d2b75ecea (2026-11-20, line-level through-pair recording); ex-15 @ b53e370911 (2026-11-20, no-prediction economics subdocument link); ex-15 @ b53e370911 (2026-11-21, passenger and goods systems subdocument link); ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, passenger division and loose-stock rule with runtime cases)
---
# ex-15: consist re-combination design and work programme

**Covers:** the operational requirements, design baseline and implementation ordering for consist
re-combination. Implementation status lives in [schedule-and-consists](schedule-and-consists.md);
cross-schedule journey discovery and timing lives in [yh-path-explorer](yh-path-explorer.md).
Untagged implementation claims are [CODE] at the frontmatter value. Older forum proposals are
design history, not automatically current decisions.

Primary design sources: the 2013 shunting discussion
[FORUM:https://forum.simutrans.com/index.php/topic,11751.0.html], the 2015 operational discussion
[FORUM:https://forum.simutrans.com/index.php/topic,14301.0.html], the detailed 2018 data/runtime
design [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html], and the 2018
path-explorer discussion [FORUM:https://forum.simutrans.com/index.php/topic,17892.0.html].

Sub-documents (read-when):

- [cargo-continuity](recombination-design/cargo-continuity.md) — the
  cargo/passenger continuity and instance-identity decision, its code and realism constraints, and
  its sub-questions; read when working on that decision or on load/unload paths at a divide.
- [remote-implications](recombination-design/remote-implications.md) — the cross-cutting review of
  realism, player cognitive overhead, performance and anomaly/exploit implications (R/C/P/A items);
  read before settling the cargo/instance or fare-booking decisions and before finalising the
  through-journey stages.
- [no-prediction-economics](recombination-design/no-prediction-economics.md) — the economic scope
  and exploit potential of the design that does not fix the vehicle-to-portion assignment before
  boarding; read when assessing the cost of costless rearrangement, the fixed-cost and demand
  effects, or whether a committed formation is required.
- [passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md) — the
  two-system design for passengers and wagon-load goods, the committed formation, the through
  record and the implementation status; read before implementing or changing the recombination
  loading, division or through-fare paths.

## Operational scope

The required operations are [RECOLLECTION:2026-11-03 user statement]:

- pick-up goods trains dropping and collecting wagons during a journey;
- locomotive changes, including replacement from loose stock at player/player and
  electrified/unelectrified boundaries;
- division and combination of multiple units and locomotive-hauled trains;
- through trains leaving carriages at a junction for a later local/branch train;
- road tractors leaving trailers to load and collecting other trailers; and
- canal boats operating with or without unpowered butties.

These are not confined to rail. Pick-up goods and articulated lorries exchanging cabs were already
explicit examples in 2015 [FORUM:https://forum.simutrans.com/index.php/topic,14301.msg141749.html].
The detailed 2018 design also explicitly uses a pick-up goods train to explain why leftover
vehicles are necessary [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].

A recombination can produce two different kinds of group, which must not be conflated
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html]:

1. a portion valid to depart on a schedule; and
2. loose stock that may be wholly unpowered and need not be a driveable convoy. It remains at the
   stop with an empty schedule and no line, occupies space, and can later supply a consist order.

## Design baseline

### Composition at a split — SETTLED

The composition rule is two end states, one per portion
[RECOLLECTION:2026-11-03 user statement; origin:
FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html]:

- the splitting schedule's consist order at the uncouple entry describes the portion that retains
  that schedule (e.g. portion A keeps schedule N); and
- the target schedule's consist order at `target_unique_entry_uncouple` describes the split-off
  portion (e.g. portion B), which adopts the target schedule (`target_id_uncouple`) at that entry.

The split itself is signalled by the entry's `uncouple` flag plus the two target fields; which
portion keeps the original schedule follows from which convoy is executing it (A keeps N because A's
convoy is the one running N). If the target entry has no consist order, the split-off composition is
unspecified and the non-retained vehicles become loose stock — the 2018 default and the current
behaviour for displaced vehicles. More than two portions require multiple uncouple entries at the
same stop [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].

Data-model consistency [CODE ex-15 @ bf447619a]: consist orders live in `schedule_t::orders`, an
inthashtable keyed by `unique_entry_id`, one order per entry, independent of the entry's flags — so
N's uncouple entry carries the flag, both target fields and A's order simultaneously, and P's target
entry carries B's order; `entry_has_consist_order(unique_id)`/`get_consist_order(unique_id)` look up
by id; the schedule GUI allows both on stop entries (only depot entries hide the order UI); all of it
is serialised. The Y division reads those two orders in sequence: `convoi_t::hat_gehalten` applies the
source entry's order to the retained portion and `convoi_t::form_divided_portion` applies the target
entry's order to the split-off portion. A paired exchange reads both convoys' orders together in
`paired_exchange_solver_t` in `simconvoi.cc` and commits both results atomically
(IMPLEMENTED; see [schedule-and-consists](schedule-and-consists.md)).

Residual questions inside the settled rule (runtime detail, not alternative composition rules):
matching precedence when both orders could claim the same vehicle; joint vs sequential solving; and
failure behaviour when the two orders cannot both be satisfied (part of the paired-exchange failure
semantics). The per-portion picker gate is specified from this rule: each portion must be complete
against its own end-state order, with own traction and valid ends at the split faces and at any
rejoin, in the executed orientation.

### Coupling and asynchronous release

A targetless consist order (no couple configuration) assembles from the current consist and the
stop's loose pool. This is the intentional pool-recombination mode, not coupling between two active
consists [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html, 2023 edit;
CODE ex-15 @ 2315dba1b].

The current intended coupling semantics are [RECOLLECTION:2026-10-19]: a couple order may draw on
loose vehicles and on vehicles released at the stop under the target's own uncouple order, falling
back to the pool only insofar as the target does not supply enough. Removing vehicles from an active
consist requires its uncouple order; a plain uncouple naming no receiver is sufficient. A consist
that only releases vehicles and needs none from a counterpart does not wait: the released vehicles
become loose stock. Thus asynchronous detach-now/collect-later operation is distinct from a paired
exchange between two active consists.

For a paired exchange, the receiver has `couple` + target + its order and the target has `uncouple`
(receiver and target entry optional) + its order. The runtime evaluates both orders against the
pre-exchange consists and commits both as one exchange
[RECOLLECTION:2026-10-19]. The implemented rules — reciprocity validation,
`paired_exchange_solver_t`, the receiver-only pool fallback, all-or-nothing failure and the once-only
stop record (the stop is recorded in both convoys' `last_stop_id`, so neither treats the exchange as
a fresh arrival) — are described in [schedule-and-consists](schedule-and-consists.md). A named couple target that has not
arrived is awaited without substitution and deliberately without a timeout; the earlier timeout
proposal is superseded [RECOLLECTION:2026-11-03 user statement].

The 2018 proposal also requires exactly one of two joining convoys to concatenate schedules: the
combined convoy retains the non-concatenating convoy's ID, and both/neither concatenating is an
authoring error. Whether to retain that joining rule belongs to paired-exchange validation
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].

### Freight and passenger continuity

Passengers and cargo are arranged internally into the correct portion at a division; neither need
be assigned to its eventual post-division vehicle at boarding, and alight-and-reboard at the
division boundary is rejected ([yh-path-explorer](yh-path-explorer.md)). The passenger and goods systems
that implement this are in
[passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md); the packet,
revenue and realism constraints are in
[cargo-continuity](recombination-design/cargo-continuity.md).

The discharge boundary is decided from the final consist. A vehicle in a scheduled consist keeps its
load. A detached loaded goods wagon, or a mail van without a TPO (Travelling Post Office; defined
in [passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md)), waiting in
the [loose pool](../consist-recombination.md#terms) also keeps its through cargo; this is the
through-wagon model, and the wagon is the unit of collection
[RECOLLECTION:2026-11-17 user decision]. A passenger vehicle, or a mail vehicle
on a train with a TPO, that becomes loose stock unloads packets whose next transfer or destination
is the stop where it becomes loose; a later-transfer packet stays aboard as a through coach
([consist-recombination](../consist-recombination.md#terms)). Packets for the stop itself are
delivered there. The through-wagon load stays outside halt capacity, overcrowding and waiting-time
rules; on collection the normal halt loader supplies new freight only then, and that new freight is
subject to the halt rules. The through-wagon load receives no loading credit; the credit applies
only to cargo loaded at collection. Route repair, the loading-time credit and the through fare
record are specified in [cargo-continuity](recombination-design/cargo-continuity.md).

### Flash-load credit at collection

Physical preloading is not used: a loose empty wagon stays empty; until collection, freight that has
arrived for it remains in the halt. `convoi_t::hat_gehalten` measures the earliest `arrival_time` of
the cargo actually loaded while processing the collecting convoy. The loading credit is the smaller
of that load's ordinary contribution to `convoi_t::current_loading_time` (the difference between
`calc_current_loading_time` with that amount and with zero) and the time between collection and
`max(wagon_loose_since, earliest_cargo_arrival_time)`. The wagon becomes available at
`collection_loose_since`, the tick at which it entered loose stock, which is later than the
laid-over convoy's `arrival_time` when the wagon joined an existing convoy. Negative intersections
clamp to zero, credits from separate loaded amounts are added only for their respective normal
contributions, and the subtraction from `current_loading_time` is floored at zero. The Same, Any and
named recipient choices do not alter this calculation.

Each vehicle records when it became loose, including when it joins an older laid-over convoy.
The pending collection data are saved because a counterpart in a paired exchange can load after its
shunting interval. In an ordinary asynchronous pool collection the load occurs in the same
stop-processing call, and clearing the pending marker after that calculation prevents a second
credit on subsequent loading passes.
The path explorer is not changed for this credit: routing still describes a loose-stock handover,
not a new class of edge. See [remote-implications](recombination-design/remote-implications.md) for
why late-arriving cargo must not inherit the wagon's entire standing time.

### Multi-player consists: current model

Multi-player consists are intended for through carriages, hired traction and wagon pooling
[RECOLLECTION:2026-10-18]. The 2013 design proposed explicit permission before taking another
player's vehicles, with running costs borne by the convoy owner even when the picked-up vehicle is
a locomotive [FORUM:https://forum.simutrans.com/index.php/topic,11751.msg115948.html].

- Each vehicle retains its own `obj_t` owner (`owner_n`, serialised per vehicle). Consist-order
  add/move/substitute/remove operations preserve it; load only repairs null owners. The convoy owner
  is separate and may differ by design.
- Movement access is decided by the leading vehicle's owner. Only the leader runs `hop_check`;
  `rail_vehicle_t::can_enter_tile` asserts `leading`, and route search uses `front()`.
  `vehicle_t::check_access` admits a public-service leader, public right of way, an unowned way, a
  way owned by the leader's owner, an unowned leader, a way whose owner matches the current way's
  owner, or an explicit access grant. It is an entry test only, and a public-service leader bypasses
  all checks. Access can therefore change on reversal or recombination. Halt access on route
  registration instead uses the convoy owner.
- Revenue, running costs, way tolls and monthly fixed costs go to the convoy owner;
  overhaul/mothball goes to the vehicle owner; depot sale value goes to the depot owner. The
  replacer builds replacements owned by the convoy owner. Vehicle/convoy statistics counters change
  only on build, sale and destruction, not on a consist-order move.
- Coupling-target selection is not access-gated: it lists every lineless convoy registered at the
  halt (registration itself is owner-filtered) plus lines via `get_loading_convois()`. Copying a
  convoy's order does require mutual access (`gui/consist_order_gui.cc`), a precedent for gating
  coupling itself.
- Company removal destroys every vehicle in its convoys regardless of vehicle owner.
- Recombination never changes liveries: through-worked stock keeps its owner's livery, and players
  cannot change other players' vehicles' liveries [RECOLLECTION:2026-11-01 user statement; CODE].

The Multi-Owner Access Rule governs a consist whose vehicles belong to more than one player
[RECOLLECTION:2026-11-13 user statement]. For each final consist after a re-combination, let `L` be
the first vehicle whose `vehicle_desc_t::get_power()` is greater than zero. For every vehicle `v` in
that consist whose owner differs from the owner of `L`, the owner of `L` must permit that owner to
use the ways owned by the owner of `L`: `owner(L)->allows_access_to(owner(v)->get_player_nr())`
(`player_t::allows_access_to`, `player/simplay.h`). The check is at consist assembly and whenever
the first powered vehicle changes, not on every tile. `vehicle_t::check_access` continues to check
the way owner's permission for the leading vehicle's owner on each tile. `paired_exchange_solver_t`
enforces the rule while it searches, and `convoi_t::match_consist_order`, including the branch that
handles optional slots, applies it to the final consist before accepting it. A denied assembly is
reported through the ordinary consist-order failure warning. The schedule pre-check that inspects
every convoy that could serve the counterpart line and every pool vehicle that could be taken,
collects their owners and reports a missing permission by naming both players is not implemented;
the runtime check is on the actual final consist.

### Path exploration

The explorer design records through journeys from saved schedule/order data and saved operating
state, not live convoy positions or loose-pool contents [RECOLLECTION:2026-09-19]. Revised
2026-11-07: a through connexion's time is the actual pair time recorded by the consist that
carries the passengers or goods across the coupling, with the existing accumulated-leg fallback;
the recorded time includes the coupling stop and the routing search consumes the connexion as an
ordinary edge. See
[yh-path-explorer](yh-path-explorer.md). Differing service frequencies and asynchronous collection
were already considered in the 2018 explorer discussion
[FORUM:https://forum.simutrans.com/index.php/topic,17892.msg170661.html]. `convoi_t::laden` records
the through pair at each scheduled arrival in the line's `average_journey_times` for a convoy that is
on a line; a lineless convoy keeps the accumulated-leg fallback.

### Shared-contact coupling movement

The choose-signal call-on core routes a receiver to the target's platform and admits it under
call-on. The shared-contact coupling movement lets the claimed mover enter the target's contact tile
and apply the coupling-point stop. The implemented parts are:

- shared contact tile: `convoi_t::is_recombination_contact_tile` identifies the claimed partner's
  contact tile and requires the mover's `call_on` flag, the reciprocal approach link and a route
  that ends at that tile. `rail_vehicle_t::block_reserver` stops its reservation walk at that tile
  without reserving it, so the target remains the reservation holder, and
  `rail_vehicle_t::can_enter_tile` admits the mover to it.
- contact stop: `convoi_t::get_recombination_contact_ground` computes the coupling-point offset
  (the target rear vehicle's front position minus that vehicle's length, in vehicle steps within
  the tile) from the target's rear vehicle position and length, and `vehicle_t::hop` uses it as the
  moved tile's `steps_next` when the offset was recorded for that hop, instead of the terminal
  half-tile correction. The value is transient (`convoi_t::recombination_contact_steps`) and is not
  saved.
- approach-link persistence: `convoi_t::recombination_approach_partner` and
  `convoi_t::recombination_approach_is_mover` form a reciprocal link, are set at the first choose
  signal commitment, survive a deferred wait, and are saved.
- resolvability pre-check: before the choose signal adopts a route to a stationary target, it calls
  `convoi_t::can_approach_coupling_target`. The check does not change state: a paired exchange runs
  `paired_exchange_solver_t` and a one-sided receive runs `convoi_t::match_consist_order`, both over
  the same vehicles the arrival commit would use. A failure keeps the signal at danger, posts one
  warning with `convoi_t::report_coupling_hold` naming both owners and the reason, and leaves the
  mover waiting at the signal. The verdict is cached on
  `convoi_t::recombination_precheck_signature` until either consist or the stop's laid-over pool
  changes. A failed check therefore prevents contact, so the normal path has no failure after
  physical contact and needs no reverse-out. A target whose consist changes after the route is
  adopted can fail at the arrival commit; that is all-or-nothing.
- committed-target hold: the reciprocal approach link records the claim but is not a selected route.
  `rail_vehicle_t::has_selected_coupling_target_route` requires the current route to end at the
  partner's contact point as well as the link, so a committed target keeps the mover held on every
  reservation pass. The link alone would let the mover treat the route as selected after the first
  pass and follow its scheduled platform instead of waiting.
- paired-exchange layout: `convoi_t::commit_paired_exchange` places the final consists on the
  tiles the pre-exchange vehicles occupied, the convoy in front first and the one behind second,
  with loose stock behind them, and releases each changed convoy's previous reservations before
  re-reserving its occupied tiles (see "Post-exchange layout" below).
- one-sided receive layout: `convoi_t::commit_recombined_consist` applies the same slot record when
  the receiver is the shared-contact mover and its route ends at the joining convoy's contact tile.
  The joining convoy's remainder takes the front slots, the receiver's final consist follows, and
  loose stock takes the remaining slots; the joining convoy releases and re-takes its reservations
  around the repositioning.
- reservation handoff: `rail_vehicle_t::hand_over_contact_reservation` transfers the tile's single
  reservation holder to the convoy that remains on it when the holder's last vehicle leaves, and
  suppresses the ordinary release when another convoy still occupies a tile it holds. The mover
  entered the contact tile without reserving it, so the handoff keeps exactly one holder throughout.
  The two branches and the link lifetime are in
  [reservation-handoff](../consist-recombination/reservation-handoff.md).

The scripted coverage, its assertions and the coverage gap in the holder-to-mover transfer are in
[scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md); the
design question is whether that branch is a defensive case for a target that starts departing after
the approach is adopted. The coupling point is carried back through as many tiles as the rear vehicle's length
requires: `convoi_t::get_recombination_contact_point` adds `VEHICLE_STEPS_PER_TILE` and steps one
tile backward for each tile the rear end falls behind the front tile, so a rear vehicle whose body
crosses a tile boundary is coupled to in the tile that contains its rear end.
`convoi_t::get_recombination_contact_pos` returns that tile for the route target, the contact-tile
test and the two layout tests.

The choose-signal work handoff sometimes calls this package Phase 4; that numbering is specific to
the call-on/coupling work and is unrelated to the phase numbering in
[vps-migration](../vps-migration.md).

### Post-exchange layout

`convoi_t::commit_paired_exchange` records every pre-exchange vehicle's tile, `steps` and direction
(front convoy first, then rear convoy) before detaching the consists. When the two consists are in a
physical order (the mover reached the other convoy's contact tile), the final consists are placed on
those tiles in order: the front convoy's final consist, the rear convoy's final consist, then all
loose stock. A final consist that grows takes the other consist's former tiles; one that shrinks
leaves the freed tiles to the loose stock. `vehicle_t::set_layout_position` sets the tile, `steps`
and direction. The changed convoys release their reservations with `convoi_t::unreserve_route()` and
re-reserve with `convoi_t::reserve_own_tiles()` after positioning. When the consists are not in a
physical order (different platforms, or a laid-over donor supplying a final vehicle), each changed
consist is placed at its former rear vehicle position.

A one-sided receive uses the same slot record. When the receiver is the shared-contact mover and its
route ends at the joining convoy's contact tile, the joining convoy's remainder takes the front
slots, the receiver's final consist follows, and loose stock takes the remaining slots behind it.
The joining convoy releases its reservations before positioning and re-takes them afterwards.
Without the contact order, the receiver's final consist is placed at its former rear vehicle
position as before.

## Implementation order

The agreed ordering constraints are:

- The dividing runtime and the per-portion check are one package. Split composition is settled
  (above); freight continuity is designed with the runtime because that determines whether instance
  marking is needed. The per-portion check is implemented as the editor's verdict on each list,
  written from the composition rule ([schedule-and-consists](schedule-and-consists.md)).
- At UI level the picker's connection filter is facing-neutral and the list verdict is shown. The
  runtime chooses a joining vehicle's facing during matching, so the editor does not need to know
  it ([schedule-and-consists](schedule-and-consists.md)). A future model could gate turning on a
  reversal facility at the stop; whether to simulate to that level is open.
- The through-journey explorer has its own documented stages: baselines, edge generation,
  cross-boundary condition, then transfer status/rebuild triggers/depth limit
  (→ [yh-path-explorer](yh-path-explorer.md)). Its through journeys must not be released before
  the runtime can execute them; splits, joins, two-convoy exchanges, nested splits,
  portion-sensitive discharge, freight continuity, platform handling and the remaining
  consist-order validation are release co-requirements
  [RECOLLECTION:2026-09-19; RECOLLECTION:2026-11-01].

The proposed dependency order for the remaining runtime work is [UNVERIFIED]:

1. Composition is settled. Freight forwarding uses per-vehicle through-departure records, and
   optional-slot, load-state and recipient rules govern pickup. Cargo retains its fare history and
   loose-stock wait time is used only by the documented collection-loading credit.
2. Implement division and schedule hand-over, including reversal, tile re-reservation and the
   per-portion check. (Implemented: the check is the editor's list verdict.)
3. Implement the paired exchange, failure behaviour and deadlock avoidance, while preserving the
   separate plain-uncouple/asynchronous pool-collection path. Recipient eligibility at pickup is
   implemented for the single-order path (see [schedule-and-consists](schedule-and-consists.md)).
   This follows division because the target's uncouple order supplies the paired exchange.
   (The runtime is implemented; the remaining items are the ones listed under Open questions.)
4. Implement the through-journey stages in parallel where useful, but release them only with the runtime and
   freight-continuity work. Resolve asynchronous collection dwell within that package.
5. Complete platform/siding/home-depot and shared-contact call-on handling, solver completeness,
   ownership/access decisions and the remaining authoring validation. The editor's list verdict
   (`consist_order_t::check`, shown by `consist_order_frame_t::update_order_check`) reports the first
   problem with a list and names the position; it is advisory and cannot know a named vehicle's
   availability. `convoi_t::match_consist_order` and `paired_exchange_solver_t` choose a joining
   vehicle's facing, so the editor does not need to know it. The choose-signal call-on core (target
   resolution, committed-target hold, target-platform route and admission) is implemented. The
   approach link and shared contact tile are implemented; the one-sided receive's layout and the
   paired layout are implemented. The resolvability pre-check
   holds the signal and reports instead of contacting a target that cannot resolve, so the normal
   path has no failure after physical contact. The coupling point is carried back through every tile
   the rear vehicle's length covers.
   The scripted cases, their assertions and the remaining coverage gaps (including the unscripted
   holder-to-mover transfer) are in
   [scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md).
   The general signalling Squirrel API's read-only object
   model, construction commands and site-validity and price queries are implemented (`signalbox_x`,
   the signal, signal-descriptor, convoy, way and vehicle-slot getters,
   `command_x.build_signalbox`, the
   signalbox form of `command_x.build_sign_at`, `command_x.can_build_signalbox_at` and
   `command_x.signalbox_get_price`).
   Basic loose-pool storage and visibility already exist; do not recreate them as a prerequisite subsystem.

## Verification fixtures

The planned verification coverage is listed in
[scripting-and-tests/consist-recombination](../scripting-and-tests/consist-recombination.md#planned-coverage);
the realised scripted cases and their assertions are in the same doc.

## Open questions

- **Instance identity and cargo continuity** — the two-system design resolves the passenger/goods
  split, the committed formation and the loose-stock treatment
  ([passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md)). Remaining:
  passenger loading grouping under capacity pressure and the other open questions in
  [cargo-continuity](recombination-design/cargo-continuity.md). Any persistent marking would engage
  the savegame/network rules before implementation.
- **Paired exchange:** the runtime validation, failure reporting, atomic commit, once-only stop record
  and direct mutual-wait resolution are implemented, and a committed exchange survives the loopback
  network join's save/transfer/reload and a fresh offline load (2026-11-14). Remaining: wait-cycle
  detection longer than a direct pair, and the GUI authoring-time pre-check (designed but not
  brought into the knowledge base). The no-timeout decision for an absent named target is settled.
- **Loose-stock loading:** cargo is not moved into a loose wagon before collection. A loaded goods
  wagon, or a mail van without a TPO, retains its existing through packets under the through-wagon
  model, and that existing load receives no loading credit. A passenger vehicle, or a mail vehicle
  on a train with a TPO, follows the through-coach rule in
  [passenger-and-goods-systems](recombination-design/passenger-and-goods-systems.md). The normal
  collection loading pass supplies new freight with the intersection-based credit described under
  "Flash-load credit at collection"; the Same, Any and named recipient modes use the same credit.
  Halt capacity and ordinary waiting rules apply to freight waiting in the halt before collection,
  not to the existing load aboard the wagon. [RECOLLECTION:2026-11-17 user instruction]
- **Recipient model:** each loose vehicle has one recipient, or Same/Any rather than a list
  [RECOLLECTION:2026-11-17 user instruction]. Both pool pickup and the paired receiver honour it.
  The halt Vehicles tab permits bulk edits to Same, Any, a line or a convoy using a
  `tool_change_convoi_t` network command, and states whether the wagon is waiting loaded or empty.
  Explicit Same keeps the resolved releasing line or convoy; Any uses a flag in the existing
  recipient byte and retains that identity so the editor can restore Same without a new save field.
  This does not make an existing legacy Any item recover an identity that was never saved.
  The path explorer does not read the recipient setting when it generates a through connexion, so it
  could offer a journey through a later service that is not allowed to collect the vehicle. Whether to
  exclude such candidates by constructor-time recipient data remains an open question.
- **Asynchronous dwell:** does measured through dwell include the full wait for a later collecting
  service, and how is it reconstructed before measurements exist? Do not silently charge either
  only the global shunting delay or an ordinary second wait plus walking transfer.
- **Ownership/access/settlement:** the Multi-Owner Access Rule is decided: a final consist with
  vehicles from more than one player is permitted only when the owner of the leading powered vehicle
  allows every other vehicle owner (see the multi-player section). Remaining: hire and
  through-carriage settlement, and the treatment of an existing consist when a permission is
  withdrawn.
- **Outgoing-route validation:** must the solver itself validate replacement traction against the
  next leg's way/electrification constraints, or do player-authored orders and normal departure
  checks suffice? The operational requirement does not by itself choose between these.
- **Reversal facility:** the runtime turns a bidirectional vehicle during the shunt at the coupling
  stop and charges the existing unit or turntable reversal time. A future model could
  allow a vehicle to be turned only where the stop has a reversal facility such as a turntable.
  Whether to simulate to that level of operational granularity is open
  [RECOLLECTION:2026-11-16 user statement]. The behaviour is recorded in
  [consist-recombination](../consist-recombination.md#matching-and-commit) and
  [schedule-and-consists](schedule-and-consists.md).
