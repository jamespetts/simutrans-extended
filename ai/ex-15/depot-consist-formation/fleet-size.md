---
status: draft
verified: none
---
# Depot consist formation: fleet size and withdrawal

Parent: [depot consist formation](../depot-consist-formation.md). This document defines the Target
Fleet Size, dispatch, Withdrawal and automatic adjustment. Tags: `[CODE]` is verified against the
checked-out tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is unresolved.

## Terms used here

The parent document defines [Consist, Convoy, Deployed Convoy, Held Convoy, Target Fleet Size,
Depot Consist Order and Depot Vehicle Pool](../depot-consist-formation.md#terms). In this document:

- **Withdrawal** is the instruction that sends a Deployed Convoy to a depot and holds it there as a
  Held Convoy, reducing the number of Deployed Convoys by one.
- **Load factor** is the ratio of loaded distance to capacity distance. The passenger load factor is
  `LINE_PAX_DISTANCE / LINE_CAPACITY` in tenths of a percent, where `LINE_CAPACITY` is booked only
  for passenger vehicles. The goods load factor is `LINE_PAYLOAD_DISTANCE / LINE_GOODS_CAPACITY`,
  where `LINE_GOODS_CAPACITY` is booked only for freight vehicles. Mail has its own
  `LINE_MAIL_DISTANCE` and is excluded from both.

## Target Fleet Size

### Definition

The Target Fleet Size is the number of Deployed Convoys a line aims to keep running
[PROPOSED]. It is stored per line as `simline_t::target_convoy_count` and defaults to 0. A target
of 0 means that the line has no target; every assembled Consist is started and every Convoy
completing maintenance resumes service.

A Deployed Convoy is a line-bound Convoy that:

- is not withdrawn;
- has at least one vehicle;
- is not in `INITIAL`;
- is not in a state covered by `convoi_t::is_inside_depot_state()` (`ENTERING_DEPOT`,
  `MAINTENANCE`, `OVERHAUL` or `AWAITING_TRIGGER`) [CODE `simconvoi.h`].

A Convoy in `ROUTING_1`, `ROUTE_JUST_FOUND` or `LEAVING_DEPOT` is counted as Deployed because it
is about to re-enter service; counting it prevents a second Consist being dispatched for the same
gap. A Convoy in `NO_ROUTE` is counted as Deployed but the owner receives the existing route
problem report. A Convoy in `LAYOVER` or `LOADING` is Deployed.

The count is a count of Convoys, not vehicles. The length of each Consist is controlled by the
Depot Consist Order, not by the target. This keeps the two controls separate: the target says how
many Convoys the line runs; the order says how long each Consist is.

The target counts Deployed Convoys, not the total fleet. A line with target N and M Held
Convoys has a total fleet of N + M Convoys, plus any Convoy under maintenance. This is the meaning
that makes "send a spare when the deployed count is below the target" work. A player who wants
to stop the line uses the existing withdraw or delete controls; a target of 0 means no target, not
zero service.

### Below the target: dispatch

Every depot departure checks the target [PROPOSED]:

1. Count the line's Deployed Convoys.
2. If the count is below the target, or the target is 0, start the Convoy.
3. If the count is at or above the target, hold the Convoy as a Held Convoy in the depot.

This applies to an Original Convoy completing maintenance as well as to a New Convoy assembled
from the Depot Vehicle Pool. The result is a rotation: when a Deployed Convoy enters a depot,
the count falls and a Held Convoy or a New Convoy can take its place; when the maintenance Convoy
is ready, it becomes a Held Convoy if the line is already at target. The line therefore keeps a
stable number of Convoys in service and a pool of spares in depots.

A Held Convoy is a line-bound `convoi_t` in `INITIAL`, with a valid Consist, schedule and line. It
is not in `under_maintenance`. It pays fixed costs through `convoi_t::new_month()` [CODE
`simconvoi.cc`]. The player can start it manually at any time; a manual start ignores the target.

When a Deployed Convoy enters a depot, the line dispatches a Held Convoy from any depot,
lowest `convoihandle_t` id first, if it can find a route to the Held Convoy's next schedule entry.
If no Held Convoy can run, the line assembles a New Convoy from the Depot Vehicle Pool if an order
is satisfiable. If neither is possible, the line stays below the target until one can.

### Above the target: withdrawal

When the number of Deployed Convoys exceeds the target, the line withdraws Convoys one at a time
until the count is at or below the target [PROPOSED]. A Withdrawal instructs the chosen Convoy to go
to a suitable depot at the next opportunity and to be held there as a Held Convoy. The Convoy
remains Deployed and keeps serving until it enters the depot; it is not teleported or stopped in
place. The count therefore falls when the Convoy enters the depot, not when the Withdrawal is
issued.

A Withdrawal calls the existing maintenance-type `convoi_t::go_to_depot()` path for the chosen
Convoy, so the Convoy keeps its line and schedule and enters the depot as a maintenance visit. The
depot chooses the next depot entry on the Convoy's schedule. Where the schedule has no depot entry,
it uses the existing nearest-suitable-depot search and then `home_depot`. On arrival, the Convoy is
held as a Held Convoy if the line has reached its target; the per-Convoy `withdrawal_pending` flag
records that the hold is intentional, so the Convoy is not immediately dispatched again.

The manual withdraw controls use the same call. The Convoy detail window's **withdraw** button and
the line's **Withdraw All** button currently set `convoi_t::withdraw`, which runs the Convoy empty
and sells it when it becomes empty. Under this design they send the Convoy to the depot for a
maintenance-type visit instead, and it is held there rather than sold.

#### Choosing which Convoy to withdraw

The choice is deterministic and is made from synced state [PROPOSED]. The recommended priority is
lexicographic, from highest to lowest:

1. **A Convoy already due or urgently due for maintenance or overhaul.** It needs a depot visit
   anyway; sending it now removes it from service without a separate diversion. The maintenance
   need is `convoi_t::is_maintenance_urgently_needed()`, `convoi_t::is_maintenance_needed()` and
   `convoi_t::is_overhaul_needed()`. Among these, the most overdue first.
2. **A Convoy that is already scheduled or routed to a depot.** It will leave the deployed count
   without a new instruction; do not divert another Convoy first.
3. **The Convoy farthest from its next scheduled depot.** A Convoy far from the next depot on its
   schedule serves more of its route before it can be withdrawn, so more passengers and cargo reach
   their intended stops instead of being discharged at the depot. The cost is that the line stays
   above target for longer and the Convoy may run further before it reaches the depot. Where the
   schedule has no depot entry, the distance to the nearest suitable depot is used instead. The
   user chose this priority on 2026-11-25
   [RECOLLECTION:2026-11-25 user statement].
4. **The least loaded Convoy.** Its withdrawal removes the least-used capacity. Use the same load
   factor as the automatic adjustment; for a mixed line, use the higher of the passenger and goods
   load factors (see the open questions).
5. **Lowest `convoihandle_t` id.** The final tie-break for determinism.

The priority uses integer values and no floating point. The maintenance flags, the load factor in
tenths of a percent, the scheduled distance in tiles and the Convoy id are all integer. A weighted
score is not recommended because the weights would be arbitrary tuning values; a lexicographic rule
is easier to explain and to reproduce on every peer.

If no Convoy can be withdrawn because all are already in a depot or heading to one, the line stays
above the target until one arrives. This is a temporary condition, not an error.

### Changing the target

Raising the target immediately makes Held Convoys eligible for dispatch, and makes a New Convoy
eligible for assembly. If no Held Convoy or pool vehicles are available, the line stays below the
target until they are.

Lowering the target starts the Withdrawal rule above. The line does not remove or destroy a Convoy;
it withdraws Convoys until the deployed count is at or below the new target. A Convoy already
serving continues to its next safe point, so passengers and cargo are not stranded.

The target is a dispatch and withdrawal target, not an instantaneous hard cap. The line may remain
above the target while a Withdrawal is in progress, and it may remain below the target when no Held
Convoy or vehicles are available.

### Control

The line management window gains a "Target consists" number control and a readout of:

- Deployed Convoys;
- Held Convoys;
- Convoys under maintenance or overhaul;
- the total fleet;
- the monthly fixed cost of the Held Convoys and the Depot Vehicle Pool.

The control commits through a new `TOOL_CHANGE_LINE` case; it must not mutate `simline_t` from the
GUI [CODE]; see [sync and determinism](../../sync-and-determinism.md). The field is saved per line
under the next `EX_SAVE_MINOR`.

## Automatic adjustment of the target

The user decision is to implement automatic adjustment with the feature, not defer it
[RECOLLECTION:2026-11-25 user statement]. The line management window's Fleet tab is the control
surface.

### Load factors

The passenger load factor is `simline_t::get_load_factor_pax_last_month()`,
`get_load_factor_pax_last_two_months()` and `get_load_factor_pax_year()`, in tenths of a percent
from `LINE_PAX_DISTANCE` and `LINE_CAPACITY` [CODE `simline.cc`]. `LINE_CAPACITY` is booked only for
vehicles whose cargo type is `goods_manager_t::passengers`, so it is seat-km and does not mix seats
with tonnes [CODE `simconvoi.cc`].

The goods load factor is `LINE_PAYLOAD_DISTANCE / LINE_GOODS_CAPACITY`. `LINE_GOODS_CAPACITY` is a
line counter booked for freight vehicles, but not for passenger or mail vehicles. It uses
`vehicle_t::get_cargo_max() * vehicle_t::get_cargo_type()->get_weight_per_unit() * distance`, the
same units as `LINE_PAYLOAD_DISTANCE`, so the ratio is a true loaded-fraction on a mixed line. Mail
is excluded because `LINE_MAIL_DISTANCE` measures it separately. The counter is saved under
`EX_SAVE_MINOR` 24; older saves load it as zero and return a goods load factor of zero until a
month of data exists.

The line chart gains a **Freight capacity** curve next to **Freight-km**, with its own colour
(`COL_LIGHT_BROWN`) and translation entry in `simutrans/text/en.tab`. **Seat-km** remains the
passenger capacity curve. The chart's saved filter-button states are migrated so that an older
save's buttons keep their meaning.

The getters are implemented by `simline_t::get_load_factor(first_month, month_count,
payload_type, capacity_type)`. `get_load_factor_pax_last_two_months()` and
`get_load_factor_goods_last_two_months()` aggregate months 1 and 2, the two most recent complete
months.

### Policy

A per-line policy adjusts the Target Fleet Size from observed demand [PROPOSED]:

- `fleet_policy`: off or automatic;
- `target_min` and `target_max`: the bounds the policy may set;
- `add_threshold` and `add_months`: if the load factor exceeds `add_threshold` for `add_months`
  consecutive months, increase the target by 1, up to `target_max`;
- `remove_threshold` and `remove_months`: if the load factor is below `remove_threshold` for
  `remove_months` consecutive months, decrease the target by 1, down to `target_min`;
- hysteresis requires `add_threshold > remove_threshold`; the month counts prevent oscillation.

Increasing the target allows a Held Convoy to be dispatched or a New Convoy to be assembled.
Decreasing the target starts the Withdrawal rule. Neither direction destroys a Convoy in service.

The thresholds and month counts are player-set policy values, not simulated physical quantities.
The demand measurement is the line's load factor, which is a real quantity. The policy therefore
fits the realism principle in [high-level design goals](../../high-level-design-goals.md): it
models a company's fleet management rule, not an arbitrary tuning constant.

### Mixed lines and directionality

- **Mixed lines.** A line that carries passengers and goods has two load factors. The policy should
  use the higher of the two, so that a full passenger service is not reduced because the goods
  capacity is empty, and vice versa. A weighted combined factor would need comparable units; the
  higher-of-two rule is simpler. The separate `LINE_GOODS_CAPACITY` counter makes both factors
  accurate.
- **Directionality.** The load factor is a round-trip average. A line that is full in one direction
  and empty in the other has a moderate average, even though the peak direction needs more capacity.
  The manual target lets the player cover the peak; a directional policy would need per-direction
  statistics, which do not exist. The policy uses the round-trip average for the first version.
- **Window.** The policy uses `get_load_factor_pax_last_two_months()` and
  `get_load_factor_goods_last_two_months()`, which aggregate months 1 and 2 of the financial
  history. The current, incomplete month is excluded. Two months smooth a single unusual month
  without making the policy slow to respond. The window is fixed, not a per-line setting.

### Limitations

- **Consist length.** The policy changes the number of Convoys, not the length of each Consist. A
  player who wants longer Consists in the peak can author optional slots in the Depot Consist Order.
  A demand-driven consist length is a separate future feature.
- **Spare cost.** Every Held Convoy pays fixed costs. The Fleet tab shows the monthly fixed cost of
  the held stock so the player can judge whether the spare capacity is worth it. Mothballing a
  stored vehicle reduces its fixed cost but removes it from the pool; the mothball controls are
  still missing from the interface [CODE]; see
  [economy and vehicles](../economy-and-vehicles.md).
- **Seasonal demand.** A future seasonal demand model can change the target or the thresholds. The
  Target Fleet Size is the interface for that model; the fleet design itself does not model seasons.

## Open questions

- Should the target be per line or per line and direction? The proposal is per line.
- Should mail have its own load factor and automatic policy? The current design measures passengers
  and freight only; mail has separate statistics.
- How should the manual **Withdraw All** control interact with a line that has no Target Fleet Size?
  It sends each Convoy to a depot, but with no target there is nothing to hold it there after the
  maintenance visit. The current proposal is that the per-Convoy `withdrawal_pending` flag holds it
  until the player starts it or raises the target.