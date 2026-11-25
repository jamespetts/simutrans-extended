---
status: draft
verified: none
---
# Depot consist formation: replacement integration

Parent: [depot consist formation](../depot-consist-formation.md). This document defines how the
existing vehicle replacer becomes a special case of a Depot Consist Order. Tags: `[CODE]` is
verified against the checked-out tree; `[PROPOSED]` is planned behaviour; `[OPEN]` is unresolved.

## The existing replacer

`replace_data_t` (dataobj/replace_data.h) holds the player-facing replacement recipe: a list of
replacement `vehicle_desc_t` pointers, the `replacement_time` enum, `retain_in_depot`,
`use_home_depot`, `allow_using_existing_vehicles`, `autostart` and `number_of_convoys` [CODE].
`gui/replace_frame.cc` is the interface [CODE]. `convoi_t::replace_now()` (simconvoi.cc) executes
the recipe: it reuses matching vehicles already in the Convoy, takes matching vehicles from the
home depot with `depot_t::find_oldest_newest()`, upgrades in place when possible, buys new stock
with `depot_t::buy_vehicle()`, sells or retains the old vehicles, rebuilds the Convoy in place,
recalculates categories and clears `replace_data_t` [CODE].

The design keeps the recipe and the interface [PROPOSED]. The player-facing behaviour remains a
whole-Consist replacement: the player selects the new vehicles and the trigger, and the game
performs the purchase, sale and assembly.

## How the replacer maps onto Depot Assembly

When the replacer fires, it supplies a one-shot Depot Consist Order for the Original Convoy's next
departure [PROPOSED]. The order is built from `replace_data_t::get_replacing_vehicles()`: one slot
per replacement vehicle, in order, each a specific `vehicle_desc_t`.

The replacement runs before the maintenance and overhaul classification, as now, so the new Consist
is classified and maintained normally. The Original Convoy keeps its identity. The Lead Vehicle may
change; the `convoi_t` object persists.

The one-shot order is evaluated by the same Depot Assembly engine. When `replace_now()` has already
rebuilt the Consist in place and the order matches it, the Original Convoy departs without further
decomposition. When retained stock or pool vehicles are needed, the order waits until they are
Available Vehicles.

The old vehicles follow the existing rules [CODE `convoi_t::replace_now()` in `simconvoi.cc`]:

- vehicles are sold unless `replace_data_t::get_retain_in_depot()` is true;
- retained vehicles go to `depot_t::vehicles` with a recipient for the same line or Convoy;
- `replace_data_t::get_allow_using_existing_vehicles()` lets the assembly take matching vehicles
  from the Depot Vehicle Pool before buying new. `depot_t::find_oldest_newest()` currently takes
  any matching loose vehicle; the replacement path must apply the same Release Time and recipient
  checks as Depot Assembly, so it does not take a vehicle reserved to another line or still in
  works;
- the automatic upgrade selected by `vehicle_t::get_auto_upgrade()` is still applied in place and
  charges the overhaul-equivalent wait.

## Timing values

`replace_data_t::replacement_time` keeps its meaning [PROPOSED]:

- `on_maintenance` applies the one-shot order on the next maintenance visit, including a visit made
  only for overhauls.
- `on_overhaul` applies it when at least one vehicle is due for an overhaul.
- `immediate`, `manual` and `automatic` send the Convoy to its home depot and apply the order on
  arrival.
- `on_replenish` remains deferred with the
  [range-stop runtime](../range-stops-replenishment.md).

## Policy choices

### Precedence with a schedule order

A Convoy's depot entry may also have a schedule Depot Consist Order. The recommendation is that
the replacement order wins for that visit and the depot reports that the schedule order was not
applied [PROPOSED]. The replacement window flags the conflict and explains that the replacement
takes priority. The schedule order remains on the schedule for later visits.

The alternative is to refuse the combination at authoring time. That is clearer but prevents a
player from using a replacement at a depot that is also part of a line's regular formation policy.
The choice is recorded in [decisions-and-plan](decisions-and-plan.md).

### Autostart and the Target Fleet Size

`replace_data_t::get_autostart()` wins over the Target Fleet Size: a player-initiated replacement
starts the replaced Convoy even if the target is met [PROPOSED]. The target governs automatic
spares, not a direct player instruction. The alternative is to hold the replaced Convoy when the
target is met, which would surprise a player who has just asked for a replacement.

### Whole-Consist scope

`replace_data_t` describes one complete replacement Consist, so the replacement is whole-Consist.
Per-vehicle replacement is not implemented [CODE]. A player who wants to replace one vehicle can
use a Depot Consist Order that keeps the other vehicles and names the new type for one slot. The
replacement window can link to that editor.

## Persistence and shared data

The one-shot order must persist until the Consist departs. The recommendation is to keep
`replace_data_t` until departure and add a per-Convoy `replacement_awaiting_formation` flag,
rather than store a second `consist_order_t` [PROPOSED]. `replace_now()` must not clear
`replace_data_t` while the flag is set, and `depot_t::convoi_arrived()` must skip the replacement
block while the flag is set so that a later visit does not repeat the purchase and sale. On
departure, clear both.

`replace_data_t` can be shared by several Convoys (`replace_data_t::number_of_convoys`). Deferring
`clear_replace()` until departure keeps the shared object alive for the other Convoys. The
per-Convoy flag must be cleared on departure, sale or destruction, and the shared count must be
decremented exactly once. A Convoy that is destroyed while the flag is set must release its
reference like any other replacement Convoy.

The saved fields and the save revision are in
[remote-and-persistence](remote-and-persistence.md).

## Interface

The replacement window remains the familiar whole-Consist replacement interface [PROPOSED]. It
gains:

- a short note that a replacement is a special case of a Depot Consist Order;
- a warning when the Convoy's current depot entry also has a schedule order, because the
  replacement order takes precedence;
- a link to the depot order editor for a player who wants to keep some vehicles.

## Open questions

- Does the replacement order use the schedule order's recipient setting for retained vehicles, or
  always the same line or Convoy? The recommendation is the same line or Convoy, because the
  replacement has no recipient field.
- Should a replacement that is waiting for retained stock be cancelled if the player sells the
  retained stock? The order remains unsatisfied and the depot reports the missing slot; the player
  can clear the replacement or supply another vehicle.
- Should the replacement window offer a "keep these vehicles" mode that creates a Depot Consist
  Order directly, rather than a whole-Consist replacement? That is a UI improvement, not a
  requirement for the first version.
- Does the replacement's `use_home_depot` flag interact with a line-level Target Fleet Size that
  holds the replaced Convoy as a spare in the home depot? The replacement still happens at the
  home depot; the target check applies at departure.
- Should `on_replenish` be re-expressed as a condition on a Depot Consist Order once the range-stop
  runtime exists? The current enum remains; the mapping is future work.
