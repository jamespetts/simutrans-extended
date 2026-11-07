---
status: draft
verified: ex-15 @ f4f6fd2b5 (through-journey path-explorer changes; design revised 2026-11-07)
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
is serialised. Only the runtime that reads the two orders together is absent (`// TODO: Implement
logic for dividing`; `get_uncouple_target` TODO).

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
(receiver and target entry optional) + its order. The intended runtime evaluates both orders
against the pre-exchange consists and commits both as one exchange
[RECOLLECTION:2026-10-19]. The present one-sided receiver implementation is incomplete. A named
couple target that has not arrived is awaited without substitution and deliberately without a
timeout; the earlier timeout proposal is superseded [RECOLLECTION:2026-11-03 user statement].

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

### Loading while stock is loose

The 2013 discussion identified both the need to preload goods wagons before a passing train
collects them and the impossibility of knowing their future service while they remain loose
[FORUM:https://forum.simutrans.com/index.php/topic,11751.msg115948.html]. The 2018 proposal was
therefore not to load loose vehicles, but to store their arrival time and retrospectively adjust
loading time when another convoy collects them
[FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html]. Whether that remains the
chosen mechanism is open below. Do not assume that a general system for routing new ware objects
into stationary unscheduled vehicles is required.

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
- Company removal currently destroys every vehicle in its convoys regardless of vehicle owner.
- Recombination never changes liveries: through-worked stock keeps its owner's livery, and players
  cannot change other players' vehicles' liveries [RECOLLECTION:2026-11-01 user statement; CODE].

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

## Implementation order

The agreed ordering constraints are:

- Dividing runtime design and implementation are one package. Split composition is settled (above,
  2026-11-03); freight continuity is designed with the runtime because that determines whether
  instance marking is needed. The per-portion picker gate is specified from the composition rule and
  ships with the runtime [RECOLLECTION:2026-11-01 user statement].
- At UI level, implement reversal-aware picking first, then the per-portion gate with the dividing
  package [RECOLLECTION:2026-11-01 user statement].
- The through-journey explorer has its own documented stages: baselines, edge generation,
  cross-boundary condition, then transfer status/rebuild triggers/depth limit
  (→ [yh-path-explorer](yh-path-explorer.md)). Its through journeys must not be released before
  the runtime can execute them; splits, joins, two-convoy exchanges, nested splits,
  portion-sensitive discharge, freight continuity, platform handling and the remaining
  consist-order validation are release co-requirements
  [RECOLLECTION:2026-09-19; RECOLLECTION:2026-11-01].

The proposed dependency order for the remaining runtime work is [UNVERIFIED]:

1. Composition is settled (2026-11-03). Resolve the cargo/instance, loose-loading and
   exchange-failure questions sufficiently to specify the dividing runtime. Confirm whether
   retrospective loose-stock loading credit remains intended.
2. Implement division and schedule hand-over, including reversal, tile re-reservation and the
   per-portion picker gate.
3. Implement the paired exchange, failure behaviour and deadlock avoidance, while preserving the
   separate plain-uncouple/asynchronous pool-collection path. Complete recipient eligibility at
   pickup. This follows division because the target's uncouple order supplies the paired exchange.
4. Implement the through-journey stages in parallel where useful, but release them only with the runtime and
   freight-continuity work. Resolve asynchronous collection dwell within that package.
5. Complete platform/siding/home-depot and call-on handling, solver completeness, ownership/access
   decisions and the remaining authoring validation. Basic loose-pool storage and visibility already
   exist; do not recreate them as a prerequisite subsystem.

## Verification fixtures

Exercise at least: a multi-stop pick-up goods train; locomotive replacement with unchanged hauled
vehicles, including an electrification boundary and a cross-player case; multiple-unit division and
rejoining in both orientations; a locomotive-hauled split with a non-driveable loose remainder;
through coaches collected later by a branch service; a tractor exchanging loading trailers; and a
motor boat with zero, one and several butties. Include pool contention, broken/absent targets,
save/reload and server/client determinism checks [RECOLLECTION:2026-11-03 operational scope;
UNVERIFIED fixture composition].

## Open questions

- **Instance identity and cargo continuity** — REOPENED 2026-11-03 [RECOLLECTION:2026-11-03 user
  statement]; full question, candidate shapes and sub-questions (including grouped loading under
  capacity pressure): [cargo-continuity](recombination-design/cargo-continuity.md). Any persistent
  marking engages the savegame/network rules before implementation.
- **Paired exchange:** exact validation, failure reporting, commit behaviour and deadlock
  detection/avoidance. The no-timeout decision for an absent named target is settled.
- **Loose-stock loading:** retain retrospective loading-time credit, model actual loading while
  stationary, or use another abstraction? If actual loading is required, specify how cargo can know
  the future service and how this differs from an ordinary layover, which forbids loading.
- **Recipient model:** the 2018 proposal allowed a list of lines/convoys eligible to collect loose
  vehicles; the current data model stores one recipient (or Same/Any). Decide whether one recipient
  is sufficient for the required pickup-goods and through-carriage operations before completing the
  eligibility check at pickup [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg170134.html].
- **Asynchronous dwell:** does measured through dwell include the full wait for a later collecting
  service, and how is it reconstructed before measurements exist? Do not silently charge either
  only the global shunting delay or an ordinary second wait plus walking transfer.
- **Ownership/access/settlement:** retain leader-owner way access or use convoy-owner/operator
  access with separate permission for foreign vehicles; define hire/through-carriage settlement and
  protect foreign vehicles on company removal.
- **Outgoing-route validation:** must the solver itself validate replacement traction against the
  next leg's way/electrification constraints, or do player-authored orders and normal departure
  checks suffice? The operational requirement does not by itself choose between these.
