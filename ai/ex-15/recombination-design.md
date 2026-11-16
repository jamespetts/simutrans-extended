---
status: draft
verified: ex-15 @ 2debf73ed (2026-11-15, consist-order check, facing-neutral picking and failure warning); ex-15 @ 71f192a65 (2026-11-15, contact point carried behind the rear vehicle); ex-15 @ b501f13f3 (2026-11-15, choose-signal resolvability pre-check, shared-contact reservation handoff and pool-donor destruction); ex-15 @ 117c75d17 (2026-11-15, coupling-point assertions and one-sided receive layout); ex-15 @ ed6d940be (2026-11-15, shared contact tile, approach link and paired layout); ex-15 @ 0692167b6 (2026-11-15, choose-signal call-on core and scripted cases); ex-15 @ 97029f453 (2026-11-14, remainder); design revised 2026-11-07; discharge, multi-owner access and paired runtime 2026-11-13; intended loose-stock treatment and general signalling API 2026-11-14; shared-contact coupling package defined 2026-11-15; ex-15 @ 03bf5306e (2026-11-16, committed-target hold corrected and the release suppression scripted); ex-15 @ 9fdefa153 (2026-11-16, orientation choice, reversal delay and facing case)
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
entry's order to the split-off portion. A paired exchange reads both convoys' orders together in the
joint solver (`paired_exchange_solver_t` in `simconvoi.cc`) and commits both results atomically
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
[RECOLLECTION:2026-10-19]. The implemented rules — reciprocity validation, the joint solver, the
receiver-only pool fallback, all-or-nothing failure and the once-only stop record (the stop is
recorded in both convoys' `last_stop_id`, so neither treats the exchange as a fresh arrival) — are
described in [schedule-and-consists](schedule-and-consists.md). A named couple target that has not
arrived is awaited without substitution and deliberately without a timeout; the earlier timeout
proposal is superseded [RECOLLECTION:2026-11-03 user statement].

The 2018 proposal also requires exactly one of two joining convoys to concatenate schedules: the
combined convoy retains the non-concatenating convoy's ID, and both/neither concatenating is an
authoring error. Whether to retain that joining rule belongs to paired-exchange validation
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].

### Freight and passenger continuity

Working assumption (2018/2026-09-19): cargo is rearranged internally into the correct portion at a
split and need not have been assigned to its eventual post-split vehicle at boarding;
alight-and-reboard at the boundary is rejected ([yh-path-explorer](yh-path-explorer.md)). The
passenger side of this assumption is REOPENED 2026-11-03 [RECOLLECTION:2026-11-03 user statement].
Code constraints (packet model, per-leg revenue and the `calc_revenue` cross-schedule defect,
convoy-level loading), realism constraints, candidate shapes and all sub-questions:
[cargo-continuity](recombination-design/cargo-continuity.md).

The discharge boundary is decided: a vehicle that remains in a scheduled consist keeps its load,
and a vehicle that becomes loose stock is physically discharged at the stop. The boundary, its
path-explorer consequences and the play-testing caveat are in
[cargo-continuity](recombination-design/cargo-continuity.md). The discharge is intended to be an
accounting step only for a loose vehicle, which is treated as never having been unloaded (see
"Loading while stock is loose" below). [RECOLLECTION:2026-11-13 and 2026-11-14 user statements]

### Loading while stock is loose

The 2013 discussion identified both the need to preload goods wagons before a passing train
collects them and the impossibility of knowing their future service while they remain loose
[FORUM:https://forum.simutrans.com/index.php/topic,11751.msg115948.html]. Preloading means loading
cargo into a loose vehicle at the stop before the convoy that will collect it arrives. The intended
resolution of the loose-stock case is that the discharge boundary is physically applied but treated
as an accounting step only: a loose vehicle is treated as never having been unloaded. Its packets
stay associated with the vehicle, cargo arriving at the stop may be loaded into it in advance, and
its recorded arrival time supplies retrospective loading credit when a convoy collects it: that is,
the collection is charged as the vehicle's loading operation rather than as an extra wait, subject
to the intersection rule in [remote-implications](recombination-design/remote-implications.md) A8
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html]. The path explorer keeps
its current two-case model (a vehicle in a scheduled consist, or a vehicle that becomes loose
stock); these mechanisms adjust the implications of the discharge instead of adding a
loaded-loose-wait case to the explorer. This treatment is not implemented; it is the next work on
this subject and is not a consequence of the discharge boundary itself.
[RECOLLECTION:2026-11-14 user statement]

Whether preloading needs a general mechanism for placing new ware objects into stationary
unscheduled vehicles, or only a bounded rule for loose vehicles awaiting a named collector, is part
of that work.

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

A planned rule governs a consist whose vehicles belong to more than one player
[RECOLLECTION:2026-11-13 user statement]. For each final consist after a re-combination, let `L` be
the first vehicle whose `vehicle_desc_t::get_power()` is greater than zero. For every vehicle `v` in
that consist whose owner differs from the owner of `L`, the owner of `L` must permit that owner to
use the ways owned by the owner of `L`: `owner(L)->allows_access_to(owner(v)->get_player_nr())`
(`player_t::allows_access_to`, `player/simplay.h`). The check is at consist assembly and whenever
the leading powered vehicle changes, not on every tile. `vehicle_t::check_access` continues to check
the way owner's permission for the leading vehicle's owner on each tile. The rule is not current
behaviour. A schedule pre-check must inspect every convoy that could serve the counterpart line and
every pool vehicle that could be taken, collect their owners, and report a missing permission by
naming both players; the runtime check is on the actual final consist.

### Path exploration

The explorer design records through journeys from saved schedule/order data and saved operating
state, not live convoy positions or loose-pool contents [RECOLLECTION:2026-09-19]. Revised
2026-11-07: a through connexion's time is the actual pair time recorded by the consist that
carries the passengers or goods across the coupling, with the existing accumulated-leg fallback;
the recorded time includes the coupling stop and the routing search consumes the connexion as an
ordinary edge. See
[yh-path-explorer](yh-path-explorer.md). Differing service frequencies and asynchronous collection
were already considered in the 2018 explorer discussion
[FORUM:https://forum.simutrans.com/index.php/topic,17892.msg170661.html]. The runtime must record
the through pair across the schedule handover for the actual time to exist; until then the
fallback applies.

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
  the joint solver and a one-sided receive runs the single-order matcher, both over the same
  vehicles the arrival commit would use. A failure keeps the signal at danger, posts one warning
  with `convoi_t::report_coupling_hold` naming both owners and the reason, and leaves the mover
  waiting at the signal. The verdict is cached on
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

1. Composition is settled (2026-11-03). Resolve the cargo/instance and exchange-failure questions
   sufficiently to specify the divided-portion behaviour. Preloading plus retrospective loose-stock
   loading credit is confirmed as the intended treatment (2026-11-14); its exact rule remains open.
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
   availability. The single-order matcher (`convoi_t::process_consist_order`) and the joint solver
   (`paired_exchange_solver_t`) choose a joining vehicle's facing, so the editor does not need to
   know it. The choose-signal call-on core (target resolution, committed-target hold, target-platform
   route and admission) is implemented. The approach link and shared contact tile are implemented;
   the one-sided receive's layout and the paired layout are implemented. The resolvability pre-check
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

- **Instance identity and cargo continuity** — REOPENED 2026-11-03 [RECOLLECTION:2026-11-03 user
  statement]; full question, candidate shapes and sub-questions (including grouped loading under
  capacity pressure): [cargo-continuity](recombination-design/cargo-continuity.md). Any persistent
  marking engages the savegame/network rules before implementation.
- **Paired exchange:** the runtime validation, failure reporting, atomic commit, once-only stop record
  and direct mutual-wait resolution are implemented, and a committed exchange survives the loopback
  network join's save/transfer/reload and a fresh offline load (2026-11-14). Remaining: wait-cycle
  detection longer than a direct pair, and the GUI authoring-time pre-check (designed but not
  brought into the knowledge base). The no-timeout decision for an absent named target is settled.
- **Loose-stock loading:** the recorded direction is preloading plus retrospective loading credit: a
  loose vehicle is treated as never having been unloaded, and its recorded arrival time supplies
  loading credit when a convoy collects it (see "Loading while stock is loose").
  [RECOLLECTION:2026-11-14 user statement] Open details: the exact credit rule (A8 in
  [remote-implications](recombination-design/remote-implications.md) requires the intersection of
  cargo-present and suitable-capacity-available time), whether the mechanism is general or bounded
  to loose vehicles awaiting a named collector, and the interaction with the discharge boundary
  after play testing.
- **Recipient model:** the 2018 proposal allowed a list of lines/convoys eligible to collect loose
  vehicles; the current data model stores one recipient (or Same/Any), and the single-order pickup
  honours it. Decide whether one recipient is sufficient for the required pickup-goods and
  through-carriage operations [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].
  The path explorer does not read the recipient setting when it generates a through connexion, so it
  can offer a journey through a later service that is not allowed to collect the vehicle; the
  explorer's treatment of the recipient needs a decision.
- **Asynchronous dwell:** does measured through dwell include the full wait for a later collecting
  service, and how is it reconstructed before measurements exist? Do not silently charge either
  only the global shunting delay or an ordinary second wait plus walking transfer.
- **Ownership/access/settlement:** the multi-owner access rule is decided: a final consist with
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
