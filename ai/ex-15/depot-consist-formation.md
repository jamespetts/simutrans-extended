---
status: draft
verified: none
---
# ex-15: depot consist formation

**Covers:** the design for changing the consist of a `convoi_t` at a depot. This document proposes
behaviour and changes no code. Current code facts and reuse points are in
[code-baseline](depot-consist-formation/code-baseline.md). Persistence, network, determinism,
performance and the test plan are in
[remote-and-persistence](depot-consist-formation/remote-and-persistence.md). The Target Fleet
Size and withdrawal are in [fleet-size](depot-consist-formation/fleet-size.md); the interface is in
[interface](depot-consist-formation/interface.md). The replacement integration is in
[replacement-integration](depot-consist-formation/replacement-integration.md).

Read this before implementing a Depot Consist Order, a Depot Vehicle Pool, a Target Fleet Size, or
the replacement integration. It generalises the automatic release rule described in
[depot maintenance](depot-maintenance.md#releasing-maintained-vehicles-from-overhauling-convoys).

Tags: `[CODE]` marks a fact verified against the checked-out code. `[PROPOSED]` marks a rule this
design proposes and the code does not implement. `[OPEN]` marks a question that needs a user
decision or further investigation.

The per-vehicle depot maintenance queue, the Depot Consist Order on a depot entry, Decomposition
and the Original Convoy arrangement are implemented on ex-15 [CODE]. The New Convoy arrangement,
the Target Fleet Size, the Fleet tab and the replacement integration remain [PROPOSED].

## Terms

- **Consist** is the ordered list of vehicles in a `convoi_t`, from the front of the Convoy to the
  rear in the direction of travel.
- **Convoy** is the `convoi_t` object that owns a Consist, a schedule, a line membership and
  statistics.
- **Depot Consist Order** is the `consist_order_t` stored in `schedule_t::orders` for a depot
  schedule entry. It describes the Consist that is to leave the depot.
- **Blank Depot Consist Order** is a depot entry with no `consist_order_t` in `schedule_t::orders`.
- **Decomposition** is the operation at a maintenance or overhaul depot visit that removes every
  vehicle except the Lead Vehicle from the arriving Convoy and places the removed vehicles in the
  Depot Vehicle Pool.
- **Lead Vehicle** is the first vehicle of `convoi_t::get_forward_consist()` for the arriving
  Convoy. It remains in the Original Convoy during Decomposition.
- **Original Convoy** is the `convoi_t` that arrived at the depot and retains its Lead Vehicle.
- **Depot Vehicle Pool** is `depot_t::vehicles`, the depot's loose vehicle list, together with each
  vehicle's availability state and recipient.
- **Recipient** is the line or Convoy that may take a vehicle released by an order. It is stored in
  `vehicle_t::loose_recipient_id` and `vehicle_t::loose_recipient_flags`. The default is the line
  or Convoy that released the vehicle.
- **Available Vehicle** is a vehicle in the Depot Vehicle Pool whose maintenance or overhaul work
  is complete, that is not mothballed, and whose Recipient permits the assembling Convoy to use it.
- **Release Time** is the game tick at which a vehicle's maintenance or overhaul work completes and
  the vehicle becomes an Available Vehicle.
- **Depot Assembly** is forming a Consist from Available Vehicles and, where the order permits, the
  Lead Vehicle, then starting the resulting Convoy.
- **Held Convoy** is a line-bound `convoi_t` in state `INITIAL` that has been assembled but is held
  in a depot because the line's Target Fleet Size is met.
- **Deployed Convoy** is a line-bound Convoy that is running on the line: its state is not
  `INITIAL`, `convoi_t::is_inside_depot_state()` returns false, it is not withdrawn, and it has at
  least one vehicle. A Convoy in a depot is not Deployed.
- **Target Fleet Size** is the number of Deployed Convoys a line aims to keep in service. It is
  stored per line; 0 means that the line has no target.
- **Maintenance-Only Group** is the set of vehicles in a Convoy visit that
  `vehicle_t::maintain()` handles rather than `vehicle_t::overhaul()`.
- **Overhaul Group** is the set of vehicles in a Convoy visit that `vehicle_t::overhaul()` handles.

## Purpose

The design gives players one general mechanism for changing which vehicles form a `convoi_t` when
it visits a depot. The same mechanism covers:

- releasing maintenance-only vehicles from a Convoy whose other vehicles are overhauled;
- combining vehicles from two or more Convoys of a line;
- forming a spare Consist from stored vehicles;
- converting a fleet gradually to newer stock;
- pooling vehicles between lines at one depot;
- responding to seasonal demand by changing the number of Consists in service.

The design is an alternative to a vehicle-for-vehicle exchange between two depot Convoys. It does
not need a persistent vehicle identity or a promise to restore a previous formation. Each vehicle
keeps its own maintenance counters and ownership, and a Consist is formed from whatever the Depot
Vehicle Pool contains when the order can be satisfied. The rationale is in
[decisions-and-plan](depot-consist-formation/decisions-and-plan.md#rationale).

## The proposed model

### The depot order is the departure specification

A depot schedule entry may carry a Depot Consist Order. The order describes the Consist that is to
leave the depot, front to rear in the direction of travel. It uses the existing consist-order data
model: ordered slots, prioritised specific-vehicle and rule-based alternatives, wildcard
alternatives, optional slots and the `available_to` recipient setting
[CODE `dataobj/consist_order_t.h`; `dataobj/schedule.cc`].

The order applies to a maintenance or overhaul visit only. A `store` or `delete_entry` visit
neither decomposes nor assembles a Consist. The order is stored with the schedule and travels in
the schedule string through the existing `TOOL_CHANGE_CONVOI` and `TOOL_CHANGE_LINE` commit paths
[CODE `simconvoi.cc`; `simtool.cc`]. A depot order is a separate use of the data model from a
stop-level order: `convoi_t::hat_gehalten()` does not evaluate it, and the path explorer must not
treat it as a through journey [PROPOSED].

The schedule window shows a **Departure consist** tab on depot entries. The tab holds the
consist-order button and a status line that reads "unchanged" or "order set"; it replaces the stop
settings tabs on a depot entry [CODE `gui/schedule_gui.cc`]. The interface design is in
[interface](depot-consist-formation/interface.md).

### Blank order means no re-arrangement

When the Depot Consist Order is blank, the entire Convoy stays together through the visit. This
includes a visit in which some vehicles are in the Overhaul Group and others in the
Maintenance-Only Group.

This supersedes `release_maintained_vehicles()` in `depot_t::convoi_arrived()`
[CODE `simdepot.cc`]. That function releases the Maintenance-Only Group whenever a mixed visit
occurs. Under this design, the group is released only when a Depot Consist Order asks for it.

The change is deliberate. It gives the player one rule to learn: a depot entry with no order leaves
the Convoy alone, and a depot entry with an order re-forms it. The automatic rule's two-Convoy
heuristic and its open policy questions are removed. The existing `maintenance-split`,
`maintenance-split-invalid`, `maintenance-split-unreachable` and `maintenance-cross-convoy` cases
must be rewritten to author orders, and the corresponding section of
[depot maintenance](depot-maintenance.md) must be replaced.

### Decomposition

On arrival at a maintenance or overhaul visit with a non-blank Depot Consist Order [CODE]:

1. The replacement block, if any, runs first; see Replacement below.
2. `depot_t::convoi_arrived()` classifies the vehicles into the Overhaul Group and the
   Maintenance-Only Group, including the existing grouping percentage rule, and applies
   `vehicle_t::overhaul()` or `vehicle_t::maintain()` to each vehicle.
3. The Lead Vehicle remains in the Original Convoy. The Lead Vehicle is the first vehicle of
   `convoi_t::get_forward_consist()`, not necessarily `convoi_t::front()` when the Convoy is
   reversed [CODE `simconvoi.cc`].
4. Every other vehicle is removed from the Original Convoy with
   `convoi_t::remove_vehicle_at()` and appended to `depot_t::vehicles`.
5. Each vehicle, including every moved vehicle and every vehicle that stays in the Original
   Convoy, joins the depot's per-vehicle maintenance queue. Its Release Time is set when it reaches
   the head of the queue, as the current tick plus its own maintenance or overhaul duration. The
   depot works on up to `building_desc_t::get_max_vehicles_under_maintenance()` vehicles at once
   [CODE `depot_t::is_awaiting_attention()` counts vehicles, but per Convoy; `simdepot.cc`]. A
   Convoy's wait is until its last vehicle is released.
6. The Original Convoy's state is `OVERHAUL` when at least one of its remaining vehicles was
   overhauled and `MAINTENANCE` otherwise. Its `wait_lock` covers the Lead Vehicle's own duration
   and any replacement shunting or in-place-upgrade wait, but the depot does not let it leave until
   every vehicle of its Consist is released.
7. Each moved vehicle's `loose_recipient_id` and `loose_recipient_flags` are set from the order's
   `available_to` setting. The default `available_to_is_same` resolves to the Original Convoy's
   line, or to the Original Convoy when it is lineless [CODE `dataobj/consist_order_t.h`;
   `simconvoi.cc`].
8. The Original Convoy is registered in `depot_t::under_maintenance` with the existing
   `depot_t::register_for_maintenance()` call.

The moved vehicles' overhaul and upgrade costs are booked to their owners by
`vehicle_t::overhaul()` when the work is applied. Routine maintenance has no separate charge; the
fixed and running costs cover it. Their counters are reset by `vehicle_t::maintain()` and
`vehicle_t::overhaul()`. A maintenance or overhaul visit discards the Consist's
cargo in `depot_t::convoi_arrived()` before any of this [CODE `simdepot.cc`], so each vehicle is
empty when it enters the Depot Vehicle Pool, and the assembled Convoy loads normally at its next
stop.

### Release and availability

A moved vehicle is an Available Vehicle when its Release Time has been reached, it is not
mothballed, and its recipient setting permits the assembling Convoy to take it [PROPOSED].

The depot must also handle a pool vehicle that becomes due for routine maintenance while it is
stored. A distance-based counter and the aircraft takeoff counter do not advance while the vehicle
is in a depot, but the months-based routine-maintenance interval can make a stored vehicle due
[CODE `vehicle/vehicle.cc`]. A vehicle can also have been overdue for an overhaul when it was
stored. When such a vehicle is selected for assembly, the depot applies the work that is due,
books the cost to the vehicle's owner, and sets its Release Time before the Consist can depart. A
vehicle that is due for an overhaul it cannot afford is not available; the depot reports the
reason.

A vehicle whose work is in progress can be scrapped, but cannot be listed on the secondhand market
or moved to another Convoy or depot [PROPOSED]. Scrapping is the current sell path: it deletes the
vehicle immediately and abandons the remaining work. Secondhand listing and moves wait until the
Release Time. The depot window shows the vehicle's state and release date; the sale options are
described in [interface](depot-consist-formation/interface.md).

### Depot Assembly

The depot attempts Depot Assembly when a trigger occurs. The Original Convoy arrangement is
implemented; the New Convoy arrangement is proposed [CODE for the Original Convoy arrangement]:

- a vehicle's Release Time is reached;
- a Convoy arrives at the depot with a non-blank order;
- a vehicle is added to or removed from the Depot Vehicle Pool;
- the order is edited;
- the line's Target Fleet Size changes;
- a Held Convoy should be dispatched.

The attempt runs in the single-threaded depot step. For each pending order, in a deterministic
order (line id, then convoy id), the solver tries two arrangements:

1. **Original Convoy arrangement.** The Lead Vehicle fills slot 0 of the order, and Available
   Vehicles fill the remaining slots. The Original Convoy is the departing Convoy. It waits until
   every chosen vehicle is an Available Vehicle.
2. **New Convoy arrangement.** Available Vehicles alone satisfy the order, and the line's Target
   Fleet Size requires another Deployed Convoy. A New Convoy is created from the Depot Vehicle
   Pool. The Original Convoy remains in the depot with its Lead Vehicle.

The Original Convoy's `wait_lock` can reach zero before the other chosen vehicles are Available
Vehicles. The depot holds the Convoy in its maintenance state until the full Consist is assembled;
it does not let `convoi_t::step()` set `ROUTING_1` while the order is pending. The depot window
reports the Convoy as awaiting assembly.

The solver enforces the existing rules [CODE `convoi_t::match_consist_order`;
`vehicle_t::matches_consist_order_element`;
`convoi_t::check_validity()`; `convoi_t::is_unpowered()`]:

- the Convoy is not withdrawn and has at least one vehicle;
- slot matching, alternatives in their stored priority order, wildcard alternatives and optional
  slots;
- coupling validity between adjacent vehicles, in the chosen orientation;
- at least one powered vehicle and a non-zero calculated maximum speed;
- a vehicle that can be at the front with a cab, and a vehicle that can be at the rear;
- the Convoy length and `waytype_t` limits;
- the Multi-Owner Access Rule (defined in
  [recombination design](recombination-design.md#multi-player-consists-current-model)): the owner
  of the first powered vehicle must permit every other vehicle owner to use its ways
  (`player_t::allows_access_to()`);
- a route from the depot to the next schedule entry (`vehicle_t::calc_route()`).

A partial Consist is not dispatched. A variable-length Consist is expressed with optional slots:
the mandatory slots set the minimum length and the optional slots allow a longer Consist when more
vehicles are available. `consist_order_t::check()` already provides an advisory list-level verdict
for the editor; the runtime check must report the same first problem, once per trigger rather than
every step [CODE `dataobj/consist_order_t.h`].

If neither arrangement is valid, nothing is assembled. The depot reports the first missing or
invalid slot, or the reason the arrangement cannot run, to the Convoy's owner. The report names the
depot, the Convoy and the slot. A Convoy whose order cannot be satisfied stays in the depot; this
is visible rather than silent.

### Departure and identity

When the Original Convoy departs, it keeps its `convoi_t` id, name, line, schedule,
`home_depot`, statistics, `replace_data_t`, `withdraw` flag and livery scheme (defined in
[depot maintenance](depot-maintenance.md#terms)). It is the same object that arrived [PROPOSED].
The Lead Vehicle is in slot 0. It departs only when every vehicle in its Consist is released, so a
blank-order visit also waits for the whole Consist's per-vehicle work.

When a New Convoy departs, it is created with `depot_t::add_convoi(false)`. It receives
[PROPOSED]:

- a new `convoi_t` id and a name from its leading vehicle, following the existing depot assembly
  convention;
- the Original Convoy's line, owner and livery scheme;
- a copy of the schedule advanced one entry past the depot, using the schedule-copy rules already
  used by the held-Convoy code (`copy_schedule_past_depot()` in `simdepot.cc`);
- a fresh set of Convoy statistics, and the `ATV_CONVOIS` booking that `depot_t::add_convoy()`
  callers make.

A lineless Original Convoy produces a lineless New Convoy with the copied schedule. A line-bound
Original Convoy produces a line-bound New Convoy, and the copied schedule is replaced by the line
schedule when `simline_t::add_convoy()` applies it.

Both arrangements start with `convoi_t::start(dep)` rather than `depot_t::start_convoi()`, so that
automatic assembly has no window or depot-frame side effects [CODE `simconvoi.cc`; `simdepot.cc`].
The depot first advances an Original Convoy's schedule past the depot entry, as
`depot_t::start_convoi()` does, and moves the Convoy out of `MAINTENANCE` or `OVERHAUL` into a
state that `convoi_t::start()` accepts. The departure removes the Convoy from
`depot_t::access_convoy_list()`. `simline_t::add_convoy()` may toggle `start_reversed` for a
bidirectional line; the departure reasserts the schedule's direction afterwards, as
`create_held_convoy()` does [CODE `simdepot.cc`]. Depot Assembly adds no separate waiting time;
the Release Times and the replacement shunting delay account for the visit.

If the order excludes the Lead Vehicle, the Original Convoy cannot depart. It stays in the depot
with its Lead Vehicle, and a New Convoy may depart if the Target Fleet Size allows. The depot
window reports that the Lead Vehicle is waiting for a matching order. This is the leftover problem
in which a locomotive is overhauled while its carriages are not, or the reverse; the player must
re-form the remaining vehicles. The depot window makes the waiting Lead Vehicle visible, and the
player can sell it or edit the order.

### Target Fleet Size

A line may store a Target Fleet Size: the number of Deployed Convoys it aims to keep in service
[PROPOSED]. The field is `simline_t::target_convoy_count`, with default 0. 0 means that the line
has no target.

The target governs both directions:

- **Below the target.** When a Deployed Convoy enters a depot, the count drops. The line dispatches
  a Held Convoy from any depot, lowest `convoihandle_t` id first, if it can find a route to its next
  schedule entry. If no Held Convoy can run, the line assembles a New Convoy from the Depot Vehicle
  Pool if an order is satisfiable. If neither is possible, the line stays below the target.
- **Above the target.** When the number of Deployed Convoys exceeds the target, the line withdraws
  Convoys one at a time until the count is at or below the target. A withdrawal sends the chosen
  Convoy to the next depot on its schedule, through the maintenance-type
  `convoi_t::go_to_depot()` call. Where the schedule has no depot entry, it falls back to the
  nearest suitable depot and then `home_depot`. The Convoy remains Deployed and keeps serving until
  it enters the depot; it is not teleported or stopped in place. The manual withdraw controls use
  the same call. The choice of Convoy is a deterministic priority rule; see
  [fleet-size](depot-consist-formation/fleet-size.md).

Every depot departure checks the target, including an Original Convoy completing maintenance. If
starting the Convoy would make the number of Deployed Convoys exceed the target, the Convoy is
held as a Held Convoy instead. Holding an Original Convoy moves it from `MAINTENANCE` or `OVERHAUL`
to `INITIAL`, removes it from `depot_t::under_maintenance`, and advances its schedule past the
depot entry, so it becomes a normal Held Convoy. If the number is below the target, the Convoy
starts. This is the maintenance cover mechanism: when a Deployed Convoy enters a depot, the count
drops, and a Held Convoy or a New Convoy can take its place.

A Held Convoy is a line-bound `convoi_t` in `INITIAL`, with a valid Consist, schedule and line. It
is not in `under_maintenance`. It pays fixed costs through `convoi_t::new_month()` [CODE
`simconvoi.cc`].

The target is a dispatch and withdrawal target, not an instantaneous hard cap. A Convoy that is
already serving continues to its next safe point, so passengers and cargo are not stranded. The
line may remain above the target while a withdrawal is in progress.

The line management window shows the deployed, held and under-maintenance counts and a control for
the target. The control commits through a new `TOOL_CHANGE_LINE` case and the field is saved per
line under the next `EX_SAVE_MINOR`. The detailed semantics, the withdrawal priority, the counting
rules and the automatic adjustment are in
[fleet-size](depot-consist-formation/fleet-size.md).

### Replacement

Replacement remains the player-facing `replace_data_t` recipe and its `gui/replace_frame.cc`
interface. When the replacer fires, it supplies a one-shot Depot Consist Order for the Original
Convoy's next departure, and the same Depot Assembly engine evaluates it. The replacement runs
before the maintenance and overhaul classification, keeps the Original Convoy's identity, and
preserves the existing sale, retention, existing-stock and in-place-upgrade rules. The detailed
integration is in
[replacement-integration](depot-consist-formation/replacement-integration.md).

### Pooling, seasonal demand and fleet conversion

The default recipient is the same line; the player can set a vehicle's recipient to Any in the
depot window to pool it for any line using that depot. Seasonal demand can change the Target Fleet
Size, and fleet conversion uses prioritised alternatives in the order. The detailed rules are in
[interface](depot-consist-formation/interface.md) and
[fleet-size](depot-consist-formation/fleet-size.md).

## Decisions and plan

The decisions taken, the remaining decisions, the implementation order, the documentation impact
and the remaining open questions are in
[decisions-and-plan](depot-consist-formation/decisions-and-plan.md).
