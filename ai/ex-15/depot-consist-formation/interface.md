---
status: draft
verified: none
---
# Depot consist formation: interface

Parent: [depot consist formation](../depot-consist-formation.md). This document defines the
Recipient, sale and per-line depot limit controls, the Fleet tab, the Consists tab Maintenance
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

### Depot schedule entry

A depot entry in the schedule window shows a **Departure consist** tab instead of the stop
settings tabs [CODE `gui/schedule_gui.cc`]. The tab has the **Modify consist** button, which opens
the existing `consist_order_frame_t`, and a status line that reads "unchanged" when the order is
blank and "order set" when it is not. The window title and tooltip identify the order as the
depot's departure consist, not a stop-level recombination [PROPOSED for the final wording].

### Fleet tab

The line management window has a **Fleet** tab immediately after the **Consists** tab [CODE
`gui/schedule_list.cc`]. It shows, from top to bottom:

1. **Target and policy controls.** The Target Fleet Size number and a **Set** button; the automatic
   policy switch; `fleet_target_min`, `fleet_target_max`, the add/remove thresholds in tenths of a
   percent and the add/remove month counts; and a **Set policy** button. The controls commit through
   `TOOL_CHANGE_LINE` cases `'n'` and `'p'`. The policy uses the last two complete months; the
   window is not a control.
2. **Fleet summary.** Deployed, Held, under-maintenance and total Convoys; the monthly fixed cost of
   the Held Convoys and the Depot Vehicle Pool. The passenger and goods load factors for last month
   and for the two-month policy window, and the higher value that the automatic policy uses. The
   line chart's **Freight capacity** curve shows the goods denominator over time.
3. **Depots served by the line.** One row per depot that the line's schedule visits. The row shows
   whether the depot has a Depot Consist Order, how many of the line's Convoys are under maintenance
   there, how many pool vehicles it holds, how many of those are available, and the estimated number
   of complete Consists the order can form. A depot without an order is greyed out with a mouseover:
   "This depot has no departure consist order. Add one on the depot's schedule entry to re-form
   trains here."
4. **Maximum Consists that can run now.** The Deployed count plus the Held count plus the estimated
   pool Consists from each depot with an order. The estimate is the smallest number of available
   vehicles matching any one slot, capped by the number that the pool can supply; it is a practical
   greedy count, not an optimal packing.
5. **Schedule spacing controls.** The line's frequency, the optional frequency-to-fleet
   synchronisation, the same-shift setting, and one shift input per timed stop. The behaviour,
   formula and cross-schedule trace are in
   [fleet and schedule-spacing synchronisation](fleet-spacing-sync.md).
6. **Diagnostics.** The reasons a Convoy cannot be dispatched or withdrawn, the depot queue position
   and the monthly cost of the current spare capacity are not yet shown. The per-slot depot detail
   and the Consists tab **Maintenance** column remain [PROPOSED].

### Consists tab

The existing **Consists** tab gains a **Maintenance** column [PROPOSED]. For each `convoi_t` on the
line it shows the compact state and, where relevant, the remaining work:

- `DEPLOYED`;
- `ENTERING DEPOT`;
- `MAINTENANCE` with the remaining wait, the vehicle queue position and how many of the Convoy's
  vehicles are still unreleased;
- `OVERHAUL` with the same per-vehicle information;
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

The detail of the Fleet and Consists tabs, the depot pool view and the diagnostics is a separate UI
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