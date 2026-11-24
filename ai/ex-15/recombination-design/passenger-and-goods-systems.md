---
status: draft
verified: ex-15 @ 57435c9cb1 plus working-tree passenger loose-unload fix (2026-11-22, implementation, loose-stock rule and runtime cases); ex-15 @ 6998ff08e1 plus working-tree per-operation shunting delay (2026-11-24, division delay); performance @ b14c12b (2026-11-22); code anchors ex-15 @ b53e370911 (design)
---
# ex-15: re-combination - passenger and wagon-load goods systems

**Covers:** the two-system design for consist recombination: the passenger system, the
wagon-load goods system, mail and TPO handling, the committed formation, the through record,
and the implementation status. Design context:
[recombination-design](../recombination-design.md); cargo continuity:
[cargo-continuity](cargo-continuity.md); economic assessment:
[no-prediction-economics](no-prediction-economics.md); terms:
[consist-recombination](../../consist-recombination.md#terms). Design decisions are
`[RECOLLECTION:2026-11-21; loose-stock rule 2026-11-22]`; code anchors are
`[CODE ex-15 @ 57435c9cb1]`.

## Terms

- **Next transfer.** The halt at which a packet leaves its current vehicle
  (`ware_t::get_zwischenziel()`), or its destination when no next transfer is set.
- **Division and portion.** A division splits one consist into two portions: the retained portion
  keeps the source schedule and the split-off portion takes the target schedule. Defined in
  [consist-recombination](../../consist-recombination.md#terms).
- **Valid end state.** A consist arrangement that satisfies the authored consist orders and the
  coupling, power, access and cargo-service constraints. Any such arrangement is allowed; there
  is no contiguity requirement.
- **Loader.** The loading path that decides which packets enter which vehicle, principally
  `haltestelle_t::fetch_goods` and `vehicle_t::load_cargo`.
- **Passenger system.** The system for passengers: the game does not simulate which carriage a
  passenger boards. At a division, the model moves passenger packets between carriages to the
  portion that serves their journey, subject to the passenger's accommodation class, lower-class
  use and overcrowding capacity (the standing capacity reported by
  `vehicle_t::get_overcrowded_capacity`). A passenger vehicle that becomes loose stock unloads
  packets whose next transfer or destination is the stop where it became loose; a packet whose next
  transfer is later stays aboard as a through coach.
- **TPO.** Travelling Post Office: a mail vehicle whose descriptor has a catering or TPO level
  greater than zero. `convoi_t::get_catering_level(goods_manager_t::INDEX_MAIL)` reports the
  highest level on the train. A train with a TPO is a TPO train. [CODE ex-15 @ 57435c9cb1]
- **Through coach.** Defined in [consist-recombination](../../consist-recombination.md#terms). A
  passenger packet, or a mail packet in a convoy that has a TPO, whose next transfer is the
  detaching stop is unloaded there instead.
- **Goods system.** The system for freight, and for mail without a TPO: the wagon is the unit.
  When a recombination lies between the current stop and the goods' next transfer, a wagon is
  loaded for one wagon requirement and joins a portion that serves it at the division. A
  consist order is the end-state order on a schedule entry; the term is defined in
  [consist-recombination](../../consist-recombination.md#terms).
- **Serving portion.** A final portion whose schedule serves a packet's next transfer or, when
  there is no next transfer, its destination. If more than one portion serves it, any serving
  portion may carry the packet.
- **Committed formation.** A vehicle-to-portion assignment for the next division that is fixed
  before loading from the current consist and the authored consist orders, without depending on
  the stop's laid-over pool, a counterpart that has not arrived, or an optional-slot choice that
  could change the formation.
- **Wagon requirement.** The next transfer of the goods in a wagon, or, when the wagon's
  committed portion is known, any next transfer served by that portion. All goods in the wagon
  must satisfy the requirement.
- **Through pool.** The stop's laid-over pool of loaded wagons under the through-wagon model:
  the wagon keeps its load and is collected later. Defined in
  [consist-recombination](../../consist-recombination.md#terms).
- **Through record.** The per-vehicle fare history in `vehicle_t::through_departures`, keyed by
  the boarding halt (`ware_t::last_transfer`), with a departure record of elapsed distance and
  per-player way distances.

## Settled design decisions

- **Passenger system.** The original abstraction applies: passengers are assumed to be in the
  correct carriage and are moved costlessly at each division. The allocation runs through the
  carriages in consist order. When the serving portion's vehicles are full to their overcrowded
  capacity, the remaining passengers are discharged at the division; they are not transferred to
  a portion that does not serve them. There is no priority system: the consist order is the only
  ordering, and the outcome is effectively arbitrary, as in reality. A passenger vehicle that
  becomes loose stock unloads packets whose next transfer is the stop; a packet whose next transfer
  is later stays aboard as a through coach. [RECOLLECTION:2026-11-21; loose-stock rule 2026-11-22]
- **Goods system.** If and only if the consist's schedule contains a consist order between the
  current stop and the goods' next transfer, a wagon is loaded for one wagon requirement. The
  refinement is to allow different destinations in one wagon when the committed formation puts
  all of them on the same serving portion. A wagon that cannot join a serving portion waits in
  the through pool. This mimics pick-up goods working. [RECOLLECTION:2026-11-21]
- **Any valid end state.** A transfer may perform any shunting or recombination that produces a
  valid end state. There is no contiguity constraint. Complex shunts are not priced by duration
  yet; that mechanism is deferred. [RECOLLECTION:2026-11-21]
- **Schedule edits.** A multi-destination wagon whose goods require different portions after an
  edit is force-discharged at the first division where the destinations diverge. The goods become
  ordinary halt cargo. A single-destination wagon whose serving portion disappears is treated as
  waiting in the through pool by the through-pool rule; the case is explained in Open questions.
  [RECOLLECTION:2026-11-21]
- **Mail and TPO.** Mail uses the goods system by default. If a TPO is on the train, all mail on
  the train uses the passenger system. A mail vehicle in a convoy that has a TPO follows the
  passenger division and loose-stock rules; without a TPO it follows the goods through-wagon rule. The TPO's
  capacity and cost are normal vehicle parameters and are outside this system; the existing TPO
  detection is used. [RECOLLECTION:2026-11-21; loose-stock rule 2026-11-22]
- **Through record.** When a passenger or mail packet moves between vehicles at a division, its
  through record is moved or copied to the destination vehicle, with the necessary adjustments
  to keep the fare history correct. [RECOLLECTION:2026-11-21]
- **Pick-up goods UI.** A dedicated pick-up goods authoring UI is deferred. The solver-level
  cargo compatibility is the necessary part; the authoring preset can be added later.
  [RECOLLECTION:2026-11-21]

## System selection and loading

The loader selects the system per packet, not per vehicle:

- passengers: passenger system;
- mail: passenger system if `convoi_t::get_catering_level(goods_manager_t::INDEX_MAIL) > 0`,
  otherwise goods system;
- freight: goods system.

The selection is made in the loading path (`convoi_t::hat_gehalten`, `haltestelle_t::fetch_goods`
and `vehicle_t::load_freight_internal`) and applies to every packet the vehicle may accept.

### Passenger loading

`haltestelle_t::fetch_goods` uses the serving portion's schedule, or the union of the current and
future portions when the formation is not committed:

- if the vehicle has a committed portion, the passenger is accepted only if that portion serves
  the next transfer or destination;
- if the vehicle has no committed portion, the passenger is accepted if any portion the train
  could form serves it;
- the division resolves the actual portion, with the capacity and discharge rule.

`convoi_t::get_committed_serving_schedule` supplies the target schedule for a vehicle assigned to
the split-off portion, so the loader can see the future portion's schedule.

### Goods loading

The goods requirement is computed from the packet's next transfer and the schedule:

- scan the schedule from the current stop to the packet's next transfer in the current direction;
- if no entry in that interval has a consist order (`schedule_t::entry_has_consist_order`) or a
  `couple`/`uncouple` flag, the ordinary loading applies: the wagon may carry mixed goods,
  because no division intervenes before they leave it;
- if an entry does intervene, the wagon carries goods for one wagon requirement only.

If the vehicle has a committed portion that serves several next transfers, the requirement is
the portion's serving set, and the wagon may carry any goods in that set. Otherwise the
requirement is the first packet's next transfer, or its destination when no next transfer is
set.

The loader stops accepting a packet whose next transfer is outside the requirement. The wagon
may be underfilled; that is the real cost of the grouped working. The player can consolidate
goods for the same requirement or use a smaller wagon.

## Committed formation

The committed formation is the next division's vehicle-to-portion assignment for the current
consist. It is computed before loading:

- at the last recombination entry, or at the start of the schedule;
- from the current consist in its forward direction (`convoi_t::get_forward_consist`) and the
  authored consist orders;
- without the laid-over pool, an absent counterpart, or an optional-slot choice that could
  change the formation.

For a division, `convoi_t::match_consist_order` can produce the source portion's vehicles from
the current consist and the source entry's order; the remainder is matched against the target
entry's order for the target portion; the unmatched vehicles are loose. The result is cached on
the convoy as a per-vehicle portion for the next division. It is recomputed when the consist,
schedule, orders, line, direction or target changes, and cleared after the division. It is
derived from saved state and needs no new saved field if the formation is deterministic. If the
formation depends on the pool, a counterpart or an optional-slot choice, it is not committed;
the loader uses the base one-next-transfer rule.

The division's matching and commit path honours the committed formation: the current vehicles
stay in their planned portions; the stop's laid-over pool may fill remaining order slots. If the
formation cannot be honoured, the fallback applies.

## Division

### Goods

At the division, after the final portions are known:

- for each loaded wagon, test each candidate portion with
  `convoi_t::vehicle_cargo_is_served`; the portion must serve the wagon's next transfer;
- the wagon joins a serving portion if one can accept it under the order, coupling, power,
  access and weight checks;
- if more than one serving portion can take it, the solver chooses by the order's slot
  availability and the valid end state; there is no contiguity constraint;
- if no serving portion can take it, the wagon waits in the through pool; it is not carried past
  its next transfer.

The solver may create any valid end state, including a non-contiguous arrangement. The division
waits out the shunting delay for every coupling operation that the source commit and the portion
commit perform ([consist-recombination](../../consist-recombination.md#shunting-delay)), so a
non-contiguous result costs more because it breaks and makes more couplings.

A multi-destination wagon whose goods require different portions is force-discharged at the
first division where they diverge. The discharge uses the ordinary vehicle unload path
(`convoi_t::unload_individual_vehicle_at` and `vehicle_t::unload_cargo`); the goods become halt
cargo and the wagon is emptied. A single-destination wagon whose serving portion no longer exists
waits in the through pool.

### Passengers

At the division, after the final portions are known:

- iterate the carriages that need re-arrangement in consist order;
- for each passenger packet, find the serving portion whose schedule serves its next transfer
  or destination;
- move the packet to a vehicle in that portion if capacity is available under the existing
  class, lower-class and overcrowded rules;
- once the serving portion's vehicles are full to overcrowded capacity, discharge the remaining
  packets at the division; do not transfer them to a portion that does not serve them;
- if more than one portion serves the destination, the consist order chooses the first serving
  portion with capacity; there is no other priority.

The result is deterministic but not merit-based. The through record moves with the packet (see
below). The passenger's `last_transfer` and `arrival_time` are unchanged.

### Loose stock

A passenger vehicle, or a mail vehicle in a convoy that has a TPO, that becomes
[loose stock](../../consist-recombination.md#terms) is not a portion.
`unload_newly_loose_cargo` unloads every packet whose next transfer or destination is the stop
where the vehicle becomes loose, so the passenger or mail packet can continue from the halt; a
packet whose next transfer is later stays aboard as a through coach. The rule applies to a
one-sided receive, a paired exchange, a targetless assembly and a plain uncouple, through
`convoi_t::commit_recombined_consist` and `convoi_t::commit_paired_exchange`.

A division with a resolved target uses `redistribute_passengers_after_division` instead. If the
target does not form a promoted portion, that function discharges the remaining passenger packets
and mail packets on a TPO train from the failed portion at the division.

Goods and non-TPO mail keep the destination-only pass: the wagon is the unit of collection and the
through-wagon model applies ([cargo-continuity](cargo-continuity.md)).

## Through record

When a packet moves from vehicle A to vehicle B at a division:

- find A's `through_departures` entry for the packet's boarding halt;
- copy it to B with `vehicle_t::add_through_departure`, preserving the departure time, overall
  distance and per-player way distances;
- if B already has an identical entry, do not duplicate it; if B has a different entry for the
  same halt, append the source entry;
- `convoi_t::calc_revenue` selects the entry whose departure time corresponds to the packet's
  boarding, not merely the first entry in the vector. The condition
  `td.departure.departure_time >= ware.arrival_time` selects the earliest departure not earlier
  than the packet's arrival, preferring the larger overall distance on a tie;
- `vehicle_t::prune_through_departures` keeps an entry while any packet with that halt and a
  compatible boarding remains, rather than assuming one entry per halt;
- the copy does not change any other packet's record; a packet that did not move keeps its
  vehicle's existing record.

If the source has no record, no record is invented; the ordinary `convoi_t::calc_revenue`
fallback applies. The through record survives save and reload; it is serialised per vehicle in
`vehicle_t::rdwr_from_convoi`.

## Mail and TPO

`convoi_t::get_catering_level(goods_manager_t::INDEX_MAIL)` reports the TPO level. If it is
greater than zero, all mail on the train uses the passenger system: it can be re-arranged between
mail vehicles and portions at a division, with the mail capacity rules and the through record
copy. A loose mail vehicle in a convoy that has a TPO unloads mail whose next transfer or
destination is the stop and keeps later-transfer mail aboard as a through coach. If it is zero, mail uses the goods
system: one wagon requirement per mail van, through pool on failure, and the destination-only pass
on a loose van. The TPO's capacity and cost are the ordinary vehicle parameters; no separate cost model is
introduced.

## Invariants

- A packet is either in exactly one vehicle or at a halt; there is no limbo state.
- A goods wagon's load satisfies its wagon requirement. A multi-destination wagon is allowed only
  while all its destinations are served by the same committed portion.
- A wagon that cannot join a serving portion waits in the through pool; it is never carried past
  its next transfer by the model.
- Passenger capacity, class and overcrowding are enforced. The division does not create seats.
- The passenger discharge is at the division stop and uses the ordinary halt rules.
- A passenger packet, or a mail packet on a TPO train, in a loose passenger or mail vehicle is
  unloaded when its next transfer or destination is the stop; a later-transfer packet stays aboard
  as a through coach.
- A moved packet's fare history is preserved; no other packet's record changes.
- The committed formation is deterministic and derivable from saved state; it is not a
  prediction of a future vehicle path.
- The system is deterministic: consist order, stable tie-breaks and no random choice. The
  apparent arbitrariness of passenger allocation is the consist order, not an RNG.
- No free transshipment or free capacity: the through pool, the wagon fixed cost, the stop
  handling and the portion's vehicles are the costs.

## Implementation status

The runtime work packages are implemented and measured; the open questions below remain. The cases
named below are listed in
[scripting-and-tests/consist-recombination](../../scripting-and-tests/consist-recombination.md).

1. **System selection and TPO detection.** `convoi_t::uses_passenger_system` selects passengers,
   mail on a TPO train, and freight. `mail-tpo` and `mail-no-tpo` cover the selection.
2. **Committed formation.** `convoi_t::ensure_recombination_plan` derives the next division's
   retained, split and loose assignment from the current forward consist and the two orders.
   `convoi_t::ensure_current_division_plan` plans the division being processed. The plan is a
   transient cache; it has no saved field. `goods-division` covers the forward assignment.
3. **Goods grouping.** `haltestelle_t::fetch_goods` and `vehicle_t::load_freight_internal` apply
   the wagon requirement and the serving schedule. `goods-division`,
   `multi-destination-same-portion` and `schedule-edit-through-pool` cover the loading rules.
4. **Solver cargo compatibility.** `convoi_t::get_recombination_available_vehicles`,
   `convoi_t::match_consist_order`, `convoi_t::process_consist_order`,
   `convoi_t::form_divided_portion` and `paired_exchange_solver_t` reject a candidate vehicle whose
   cargo the candidate portion does not serve. The pair freight cases and
   `schedule-edit-divergence` cover the constraint.
5. **Passenger division and loose stock.** `convoi_t::redistribute_passengers_after_division`
   moves packets to a serving portion up to overcrowded capacity and discharges the remainder.
   `unload_newly_loose_cargo` applies the loose-stock rule. `passenger-division`,
   `passenger-capacity`, `plain-uncouple-passenger` and the pair passenger cases cover it.
6. **Through record.** `vehicle_t::copy_through_departure_from` copies a moved packet's record;
   `convoi_t::calc_revenue` and `vehicle_t::prune_through_departures` use it. The pair passenger
   cases cover the passenger through fare; `through-pair` covers the goods line-level journey-time
   recording; the reload cases cover the records across a save and reload.
7. **Schedule edits.** `convoi_t::discharge_divergent_wagons` discharges a multi-destination wagon
   at the first division where its destinations diverge. `schedule-edit-divergence` and
   `schedule-edit-through-pool` cover the two schedule-edit outcomes.
8. **Tests and performance.** The runtime cases above are implemented; the measurements are in
   [Measured performance](#measured-performance) below.

## Measured performance

Measured at ex-15 @ b14c12b on 2026-11-22 [EXECUTION-VERIFIED:2026-11-22]. The no-recombination
path was compared with the parent 41c6a2b using two 120 s `Capture` runs per build on the
canonical profiling fixture (`bb-10-sep-2023.sve`, defined in
[performance](../../performance.md#fixture)), `Profile (server)|x64`, threads 4. The division and
paired-exchange costs were measured with temporary counters in the graphical `Profile|x64` build
on the runtime cases (16x16 map, `-fast_forward`), because the headless `Profile (server)` build
does not complete the `goods-division` scenario's factory construction.

- **No-recombination path.** The fixture has no couple or uncouple entry, so no division or
  paired exchange is ever executed. `convoi_t::ensure_recombination_plan` accounts for
  0.008-0.009% of sampled CPU time and `convoi_t::recombination_between` for 0.017-0.019%;
  `convoi_t::laden` moves from 1.20% to 1.28% and `haltestelle_t::fetch_goods` from 0.36% to
  0.40%, within the 0.01-0.05 percentage-point run-to-run spread. The counters recorded 476,867
  calls, 35,402 scans and 371,140 schedule entries over a 150 s window; the per-convoy, per-stop
  cache returns early on 93% of calls.
- **Division with a loaded goods wagon** (`goods-division`): 3 vehicles available, 1 in the
  retained portion, 2 in the split-off portion; 5 candidate vehicles and 4 cargo checks; 3-4
  microseconds per build and 7 microseconds for the division, which is built twice, by the loader
  before the division and by `convoi_t::ensure_current_division_plan` when it is processed.
- **Paired exchange** (`pair-freight-loose`, `pair-passenger-loose`): 4 vehicles in
  `paired_exchange_solver_t::universe`, 3 order slots and 3 search steps; 5-6 microseconds. A
  control without a recombination entry (`empty-distribution`) spends 0.9 microseconds per scan.
- **Conclusion.** The no-division loading path does not regress measurably, and a division or
  paired exchange costs single-digit microseconds on these 2-4 vehicle consists. A convoy holds
  at most one transient plan, in `convoi_t::recombination_plan`.

## Test coverage

The runtime cases and their assertions are in
[scripting-and-tests/consist-recombination](../../scripting-and-tests/consist-recombination.md).
The network checklist is described in [network](../../network.md). The following areas have
scripted cases:

- goods division and committed formation: `goods-division`;
- multi-destination grouping: `multi-destination-same-portion`;
- divergence discharge and schedule edits: `schedule-edit-divergence`,
  `schedule-edit-through-pool`;
- paired exchange with loaded passenger and freight vehicles: `pair-passenger-through`,
  `pair-passenger-loose`, `pair-freight-through` and `pair-freight-loose`;
- passenger division, capacity and overflow: `passenger-division`, `passenger-capacity`;
- loose passenger next-transfer unload and through coach: `plain-uncouple-passenger`,
  `pair-passenger-loose`;
- nested split and targetless pool collection: `nested-split`, `pool-collection`;
- mail with and without a TPO: `mail-no-tpo`, `mail-tpo`;
- reload and loopback determinism: the division, paired, choose, pick-up and the three loaded
  pair reload cases.

## Open questions

- `convoi_t::calc_revenue` selects the earliest through-record departure not earlier than the
  packet's arrival, preferring the larger overall distance on a tie. A case with several entries
  for the same boarding halt is still unscripted.
- Confirm the fare and handling treatment of passengers discharged at a division: the current
  implementation uses the ordinary halt transfer rules. `passenger-capacity` asserts the discharge;
  it does not assert the fare.
- A one-sided receive with a loaded passenger carriage has no scripted case. The rule is covered
  through a plain uncouple and a paired exchange; the one-sided path itself is unscripted.
- Pick-up goods authoring: the solver-level cargo compatibility is implemented; a dedicated
  authoring UI or preset can be added later.
- Shunting duration pricing: the delay counts every coupling operation that the commit performs
  ([consist-recombination](../../consist-recombination.md#shunting-delay)); any valid end state
  is allowed. The length of a cut and the distance that staff walk between the vehicles are not
  priced.
- The cost of committing a division or paired exchange is measured only for 2-4 vehicle
  consists (3-7 microseconds). A long loaded consist costs more, because
  `convoi_t::match_consist_order` and `paired_exchange_solver_t` test every candidate vehicle's
  packets through `vehicle_cargo_is_served_by`; a 10-20 vehicle loaded consist is unmeasured.

## Provenance

The design anchors were checked against ex-15 @ b53e370911; the implemented runtime and the
runtime cases were checked against ex-15 @ 57435c9cb1 plus the working-tree passenger loose-unload
fix on 2026-11-22. The design decisions are the user's statements of 2026-11-21, with the
loose-stock rule added on 2026-11-22. The cases passed: the 43-case non-pick-up set, the
10 passing pick-up cases (the five cases `cargo-through`, `flash-full`, `flash-late`,
`flash-partial` and `through-pair` fail on the local pakset for the fare-rounding reason recorded
in the testing document), and the three loaded pair reload cases
[EXECUTION-VERIFIED:2026-11-22, Debug x64]. Performance was measured at ex-15 @ b14c12b with
`Profile (server)|x64` on the canonical fixture and `Profile|x64` on the runtime fixtures
[EXECUTION-VERIFIED:2026-11-22]. The economic assessment of the
no-prediction design is in [no-prediction-economics](no-prediction-economics.md). The broader
realism, cognition, performance and anomaly review is in
[remote-implications](remote-implications.md).
