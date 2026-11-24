---
status: draft
verified: ex-15 @ b53e370911 (code anchors; economic assessment is analysis); ex-15 @ 6998ff08e1 plus working-tree per-operation shunting delay (2026-11-24, shunt cost anchor)
---
# ex-15: re-combination - economics of the no-prediction design

**Covers:** the economic scope and exploit potential of the design in which the runtime does
not know at boarding which vehicle will carry a packet to which portion, and the
costless-rearrangement assumption at a split. Design context:
[recombination-design](../recombination-design.md); cargo and passenger rules:
[cargo-continuity](cargo-continuity.md); broader review: [remote-implications](remote-implications.md);
terms: [consist-recombination](../../consist-recombination.md#terms). Code claims carry
`[CODE ex-15 @ b53e370911]`; the economic assessment is `[UNVERIFIED]` analysis until measured
against a representative [pakset](../../glossary.md) case. The design decisions that resolve the
grouping, fallback and passenger allocation questions are recorded in
[passenger-and-goods-systems](passenger-and-goods-systems.md); this note remains the economic
assessment, and the pick-up goods UI and shunting duration pricing are deferred there.

## Terms

- **Consist and portion.** A consist is the ordered list of vehicles a `convoi_t` runs with. A
  portion is one of the groups a consist divides into at a recombination: the retained group
  keeps the source schedule and the split-off group takes the target schedule. The consist
  orders are the end-state orders on the schedule entries. The terms are defined in
  [consist-recombination](../../consist-recombination.md#terms).
- **Packet.** A `ware_t` consignment of passengers, mail or goods travelling together with a
  common origin, destination and next transfer, and, for passengers and mail, a common class.
- **Laid-Over Pool.** Convoys in `LAYOVER` at a stop; their vehicles are loose stock available to
  a consist order. Defined in [consist-recombination](../../consist-recombination.md#terms).
- **Layover Staff Discount.** The existing rule that a convoy in `LAYOVER` does not accrue staff
  salary for the laid-over time; `convoi_t` advances `convoi_t::last_salary_point_ticks` while
  the convoy is laid over.
- **No-Prediction Design.** The design in which the runtime does not know at boarding which
  physical vehicle will go to which portion. The portion is resolved at the recombination from
  the then-current consist, the consist orders and the stop's laid-over pool, and the model
  assumes the correct assignment at the split.
- **Costless Rearrangement.** The model-level assumption that a packet can be assigned to the
  portion whose schedule serves its next transfer or destination at a split, without simulating
  the physical movement of the passenger or the wagon. For passengers the design treats the
  passenger as having boarded the correct portion. For freight the assumption is the subject of
  this note, because the wagon cannot be moved without physical work.
- **Economic Exploit.** A player obtains an in-game economic benefit without paying the real cost
  that the modelled railway would incur. The benefit can be an avoided cost, additional revenue
  or a competitive advantage.
- **Multi-Owner Access Rule.** In a final consist with vehicles from more than one player, the
  owner of the first powered vehicle must permit every other vehicle owner. Defined in
  [recombination-design](../recombination-design.md).
- **Loader.** The loading path that decides which packets enter which vehicle, principally
  `haltestelle_t::fetch_goods` and `vehicle_t::load_cargo`.
- **Committed Formation.** A vehicle-to-portion assignment fixed before loading from the authored
  consist order and the current consist, so that the loader can know which portion each wagon or
  passenger vehicle will travel on. The committed formation is the chosen design for goods; the
  implementation is in [passenger-and-goods-systems](passenger-and-goods-systems.md).

## The economic question

The no-prediction design does not create money directly. The player still pays for the train,
the wagons, the track, the staff and the fuel. The economic question is not whether the model
can move a packet for free in its own representation. It is which real costs the model omits,
how large those costs are, and whether the player can obtain the benefit without paying them.
[UNVERIFIED]

That distinction matters because the per-operation costs are often much smaller than they look,
while the network-level effects can be much larger. The fare model reinforces this: freight
fares are distance-based, and the dwell (the time a convoy spends at a stop) affects staff time,
vehicle opportunity cost (the revenue the vehicle could have earned elsewhere), catering and
Travelling Post Office (TPO) revenue, and demand through journey time, rather than the freight
fare per tonne-kilometre. [UNVERIFIED]

## Code anchors for the cost and fare model

- `goods_desc_t::get_total_fare` computes a distance-based fare from the goods descriptor's
  scaled fare stages. It applies class and catering or TPO modifiers to passengers and mail, but
  no multiplier for journey speed or time to the base fare. [CODE ex-15 @ b53e370911]
- `convoi_t::calc_revenue` uses the packet's distance and, where available, the per-vehicle
  record in `vehicle_t::through_departures`. It does not multiply freight revenue by a
  journey-time or speed factor. [CODE ex-15 @ b53e370911]
- `settings_t::shunting_time_seconds` is the configured time of one shunt: half is fixed and half
  is charged per coupling operation
  ([consist-recombination](../../consist-recombination.md#shunting-delay)). `convoi_t` discounts
  staff time while laid over by advancing `convoi_t::last_salary_point_ticks`.
  [CODE ex-15 @ 6998ff08e1 plus working tree]
- Vehicles pay fixed monthly costs through `vehicle_t::get_fixed_cost`. A wagon used as storage
  continues to pay that cost. [CODE ex-15 @ b53e370911]
- Stop storage capacity is `haltestelle_t::get_capacity`. Cargo outside the stop does not count
  against it. [CODE ex-15 @ b53e370911]

These anchors bound the direct per-operation value of avoiding a shunt or a transshipment. The
larger effects are fixed-cost avoidance and demand, considered below. [UNVERIFIED]

## Exploit mechanisms and economic quantities

The mechanisms below are the ways in which the no-prediction design can produce a benefit
without the corresponding physical cost. The materiality column states when the mechanism is
large enough to matter; it is analysis, not a measured result. [UNVERIFIED]

| Mechanism | Cost avoided | Economic quantity | When material | Mitigation with real grounding |
|---|---|---|---|---|
| Sorting at the split | Extra shunting beyond a single cut; yard; staff; vehicle occupancy | Shunting time, staff salary, vehicle opportunity cost, yard capital and maintenance | Non-contiguous or interleaved portions, or repeated splits. A division into two contiguous portions has almost no extra sorting. | Any valid end state is allowed; the shunting delay charges every coupling operation the commit performs, so an extra cut adds time. |
| Transshipment at a stop | Goods facility, handling time, stop capacity | Building capital and maintenance, staff time, stop capacity | Busy junctions where a goods shed or warehouse would otherwise be required | Require a goods-enabled stop and use the ordinary unload and load path. The ordinary stop handling path is the cost. |
| Storage in a loose wagon | Warehouse or siding capacity, staff, track occupancy | Building capital and maintenance, wagon fixed cost, layover staff discount | Where cheap wagons are used as storage; otherwise the wagon fixed cost is the natural limit | Keep the wagon fixed cost; do not give loaded wagons the full layover staff discount. |
| Capacity and overbooking | Extra wagons and capacity | Vehicle capital and running cost, demand revenue | Elastic demand with excess set down at the junction | Enforce portion capacity at loading; excess waits at the origin. The ordinary loading path is the cost. |
| Empty stock and another player's stock | Positioning and hire | Vehicle capital and hire, running cost | Multiplayer; potentially large | Hire or settlement (payment for the use of another player's vehicle; see [recombination-design](../recombination-design.md)), or player-ordered positioning. The Multi-Owner Access Rule is permission, not payment. |
| Through-service advertising | The physical service and its cost | Demand and revenue, path-explorer route choice ([yh-path-explorer](../yh-path-explorer.md)) | Network scale; can be the largest effect | Through connexions only for operable services; recorded through journey time; a committed formation for the guaranteed service. |
| Fare splitting and catering | The correct through fare | Fare difference, catering and TPO revenue | Where legs are booked at couplings or catering is applied retroactively | The scheduled-through fare rule (a journey using a scheduled through movement is priced as a through fare; see [cargo-continuity](cargo-continuity.md)) and the `vehicle_t::through_departures` record. |
| Service-pattern distortion | Separate services, connections and yards | Vehicle-hours, staff, facilities, demand | Systemic; the main long-run effect | Real split, shunting and facility costs; path-explorer journey times. |

## Degree of impact

The mechanisms differ in how often they occur and how large their economic effect is. The
ranking below is by economic effect, not frequency. [UNVERIFIED]

1. **Per-operation shunting and staff costs are small.** A one-operation shunt at the default
   `settings_t::shunting_time_seconds` (60 seconds) is a small fraction of a month's staff cost
   and a vehicle's fixed cost; a re-marshal of several coupling operations takes a multiple of it.
   Avoiding it directly is not a major source of money. The base
   freight fare is distance-based, so the direct fare effect of dwell is absent. [UNVERIFIED;
   code anchors above]
2. **Facility and storage costs are fixed and can be significant.** Avoiding a goods shed,
   warehouse or marshalling yard saves capital and maintenance. A busy junction can justify such
   a building; the no-prediction design lets the player avoid it if the physical requirement is
   not enforced. [UNVERIFIED]
3. **Demand and service-pattern effects are the largest.** The path explorer records the through
   connexion and its journey time, and the player can attract traffic that would otherwise
   transfer or use a separate service. The player captures the through traffic without providing
   the physical sorting. The gain is additional revenue and vehicle utilisation, not the avoided
   shunt. [UNVERIFIED]
4. **Multiplayer stock use is potentially large but separate.** A mixed-owner consist lets the
   convoy owner collect revenue while the vehicle owner receives no hire. The Multi-Owner Access
   Rule is permission, not payment. This is a fairness issue independent of the no-prediction
   design, but recombination makes it easier to use. [UNVERIFIED]

## Mitigations with real grounding

The strongest low-side-effect mitigations use existing physical mechanics rather than new
charges. [UNVERIFIED]

- **Route the rearrangement through the ordinary stop unload and load path.** The goods pay the
  existing handling time, stop capacity, waiting rules and facility requirements. The side effect
  is that goods may not flow freely, which is the physical constraint.
- **Require a goods-enabled stop for transshipment.** The side effect is that some stops cannot
  transship, which is realistic.
- **Allow any valid end state and price complex shunts in duration when that mechanism is
  added.** The current design allows any valid end state; the extra cut is a real cost that the
  deferred duration-pricing mechanism will charge.
- **Keep the wagon fixed cost while stored, and remove the full layover staff discount for
  loaded wagons.** The wagon already pays its fixed cost; the layover staff discount is the
  questionable part. Loaded wagons need security or handling, so the discount is not realistic
  for them. The side effect is that storage costs more.
- **Make the path explorer's through connexions conservative and feasible.** The player can
  use a committed formation to obtain the guaranteed through service. The side effect is less
  demand for uncertain services, which is the correct signal.
- **Add hire or settlement for mixed ownership.** The side effect is multiplayer complexity, but
  the alternative is an uncosted use of another player's capital.

The committed formation is the chosen design for goods. It eliminates the avoidable cost
because the loader knows the portion before loading, and the split has nothing to correct.
It is a design change, not a low-side-effect patch: it requires the loader to know and use the
committed formation, the split to honour it, and a saved or deterministically derived assignment,
and it must define what happens when the committed formation is disrupted after loading.
[UNVERIFIED]

## Open questions

- What are the measured magnitudes in a representative pakset case: one busy junction with a
  goods shed and a yard, a set of through freight flows, and the cost of the alternative
  separate services? The assessment above identifies mechanisms and relative magnitudes; the
  pakset figures are not yet measured.
- Do the underfill and through-pool costs of the committed formation and the goods grouping
  remain acceptable in a representative case, or do they need a different loading or storage
  rule?
- Is a standing or demurrage charge (a charge for standing wagons) needed beyond the wagon fixed
  cost and the removal of the layover staff discount for loaded wagons? If so, it must be
  grounded in a real standing charge, not an arbitrary tuning value.
- Should the path explorer's through connexions carry the portion's capacity and exclude services
  that cannot form? The capacity question is also open in
  [yh-path-explorer](../yh-path-explorer.md).
- Which booking model applies at a coupling: the existing `convoi_t::calc_revenue` schedule
  lookup or mileage apportionment (apportioning revenue by distance travelled)? The
  scheduled-through fare rule is settled; the mechanism is not.
- What settlement applies when a consist uses another player's vehicles or traction?

## Provenance

The code anchors were checked against ex-15 @ b53e370911. The economic assessment is analysis,
not a measured result; no representative pakset figures have been collected. The broader
realism, cognition, performance and anomaly review remains in
[remote-implications](remote-implications.md).
