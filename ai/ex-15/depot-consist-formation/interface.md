---
status: draft
verified: none
---
# Depot consist formation: interface

Parent: [depot consist formation](../depot-consist-formation.md). This document defines the
Recipient, sale and per-line depot limit controls, the Fleet tab, the Convoys tab Maintenance
column, and the interface design process. Tags: `[CODE]` is verified against the checked-out
tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is unresolved.

## Recipient, sale and the per-line depot limit

These three controls are separate from the Target Fleet Size and are often confused with it.

### Recipient

Each vehicle in the Depot Vehicle Pool has one Recipient, stored in `vehicle_t::loose_recipient_id`
and `vehicle_t::loose_recipient_flags` [CODE `vehicle/vehicle.h`]. The Recipient is the line or
Convoy that may take the vehicle:

- **Same line** (default): any Convoy of the line that released the vehicle may take it. This is
  what makes the two-train combination work: the carriages released by one train are available to
  the other train of the same line.
- **Any**: any line using the depot may take the vehicle. This is opt-in cross-line pooling.
- **Named line or Convoy**: only that line or Convoy may take the vehicle.

The Depot Consist Order's `available_to` setting controls the Recipient of the vehicles that the
order releases [CODE `dataobj/consist_order_t.h`]. The player can change a vehicle's Recipient in
the depot pool view. The Recipient does not change the vehicle's owner or livery.

### Sale, scrap, secondhand and move during work

The selling mechanism is planned to split into two operations [PROPOSED]:

- **Scrap**: sell the vehicle immediately for its current scrap value. This is the current sell
  behaviour: `depot_t::sell_vehicle()` books `vehicle_t::calc_sale_value()` and deletes the vehicle.
  Scrap is allowed while the vehicle's maintenance or overhaul work is in progress; the remaining
  work is abandoned.
- **Secondhand listing**: offer the vehicle to other players at a player-set price. This is a
  planned feature. It is allowed only when the vehicle is an Available Vehicle, whose Release Time
  has been reached, because a buyer must not receive a vehicle that is still in works.

The current depot window already distinguishes the full-value case: when the nominal cost equals the
resale value, `depot_frame_t::set_resale_value()` labels the button "Refund" and returns the vehicle
for a full refund. That is the return-to-manufacturer undo for a new vehicle that has never left its
depot, and it remains unchanged [CODE `gui/depot_frame.cc`].

Moving a vehicle to another Convoy or depot while its work is in progress remains refused; the work
state is not transferable. An Available Vehicle can be moved. A scrap sale during work deletes the
vehicle and therefore also removes any pending order that named it; the order becomes unsatisfiable
and is reported.

### Per-line depot limit

`simline_t::max_convoys_at_depot` limits how many of a line's Convoys may hold a maintenance place
at one depot at the same time [CODE `simline.cc`]. It is not the Target Fleet Size and it is not the
depot's physical vehicle capacity:

- The Target Fleet Size controls how many Convoys the line runs.
- The per-line depot limit controls how many of the line's Convoys may be in one depot for
  maintenance at once. The default is 1.
- The depot's `building_desc_t::get_max_vehicles_under_maintenance()` controls how many vehicles the
  depot can work on at once and lengthens the visit when a Convoy is larger.

A Held Convoy and the Depot Vehicle Pool do not count against the per-line depot limit, because
they are not seeking a maintenance place. The maintenance-cover example with two Convoys of one line
needs a per-line limit of at least 2 when the depot entry is conditional; otherwise the second
Convoy skips the depot. The control to change the limit is missing and is part of the Fleet tab
work.

## Interface

### Fleet tab

The line management window gains a **Fleet** tab immediately after the existing **Convoys** tab
[PROPOSED]. It shows, from top to bottom:

1. **Target and policy controls.** The Target Fleet Size number, the automatic policy switch,
   `target_min`, `target_max`, the add/remove thresholds and month counts, and the manual override.
   The controls commit through `TOOL_CHANGE_LINE`.
2. **Fleet summary.** Deployed, Held, under-maintenance and total Convoys; the monthly fixed cost of
   the Held Convoys and the Depot Vehicle Pool; the current passenger and goods load factors. State
   the value used by the automatic policy.
3. **Depots served by the line.** One row per depot that the line's schedule visits or that holds
   one of the line's Convoys. A depot without a Depot Consist Order is greyed out with a mouseover:
   "This depot has no departure consist order. Add one on the depot's schedule entry to re-form
   trains here." Convoys under maintenance at a greyed-out depot are listed under it, because they
   are still part of the line's fleet even though the depot cannot re-form them.
4. **Depot detail.** For a depot with a Depot Consist Order, list the Available Vehicles and the
   vehicles under maintenance that match the order's slots, with the slot each could fill. Show the
   Held Convoys at that depot. Show whether the order is currently satisfiable and, if not, the
   first missing slot.
5. **Maximum Consists that can run now.** A statement of the number of complete Consists the line
   could deploy at present from the current Deployed Convoys plus the complete Consists that the
   Depot Assembly solver can form from each depot's Available Vehicles and under-maintenance stock.
   The count is capped by the number of satisfiable orders and by the target; show the uncapped
   number and the target so the player can see which limit applies. The calculation uses the same
   greedy assembly rule as the runtime, so it is a practical maximum, not an optimal packing.
6. **Diagnostics.** The reasons a Convoy cannot be dispatched or withdrawn, the deposit's queue
   position, and the monthly cost of the current spare capacity.

### Convoys tab

The existing **Convoys** tab gains a **Maintenance** column [PROPOSED]. For each `convoi_t` on the
line it shows the compact state and, where relevant, the remaining work:

- `DEPLOYED`;
- `ENTERING DEPOT`;
- `MAINTENANCE` with the remaining wait and queue position;
- `OVERHAUL` with the remaining wait and queue position;
- `AWAITING TRIGGER`;
- `HELD` (an `INITIAL` Convoy) with the depot and the reason it is held;
- `REPLENISHING` or `SHUNTING` when those states apply;
- `NO ROUTE` with the existing problem report.

The column is read-only. It answers "where is each train in the maintenance cycle?" without opening
each Convoy's detail window.

### Learnability

The current consist-order editor is powerful but assumes the player already understands slots,
alternatives, rules, optional slots and recipients. A player who only wants to change which
carriages a locomotive hauls should not need to learn the rule editor. The depot formation feature
must have a simple default and a clear explanation of the one rule that matters:

> A depot entry with no departure consist leaves the train as it is. A depot entry with a
> departure consist breaks the train up on arrival and forms a new train from the vehicles in the
> depot when the order can be satisfied.

The default is a Blank Depot Consist Order, so a player who ignores the feature sees the current
behaviour. The new controls are opt-in. The schedule window's depot entry shows "Unchanged" or
"Formed from order" with an Edit button; the stop-level order keeps a different label.

### UI design process

The detail of the Fleet and Convoys tabs, the depot pool view and the diagnostics is a separate UI
design task [RECOLLECTION:2026-11-25 user instruction]. It is to be done last, by a subagent, to a
high standard. The subagent should treat this document as the information architecture and produce:

- a legible, well-laid-out, inviting design that fits the existing Simutrans window style;
- controls grouped by task, with the target and policy first, the fleet summary second and the
  depot detail third;
- an explanation of every non-obvious value, especially the difference between the target, the
  per-line depot limit and the depot's vehicle capacity;
- the additional information a player needs: overhaul due dates, maintenance queue position, the
  age and value of spare stock, the monthly cost of spares, the load factor history, and the
  reason a depot is greyed out;
- a check that every control has a tooltip and an in-game help link, and that all strings go
  through `translator::translate`.

## Open questions

- Should the depot pool view label the current sell action "Scrap" until the secondhand market
  exists, or offer both scrap and listing immediately? The proposal uses the current sell path as
  scrap and adds listing when the market exists.
- Should the Fleet tab's "maximum Consists that can run now" count only complete Consists, or also
  the maximum if the player lengthens or shortens the orders? The proposal counts complete Consists
  under the current orders.
- Should the replacement window's "replace instead of overhaul" and the Depot Consist Order share
  a single authoring screen eventually? The proposal keeps them separate.
- Which additional fleet values should the Fleet tab show: vehicle age, value, overhaul due date,
  load-factor history, monthly spare cost, or depot distance? The UI subagent decides the final set
  within the information architecture in this document.