---
status: draft
verified: ex-15 @ 6a9e8d963; ex-15 @ f41b00399f (2026-11-25, safe REPLENISHING completion transition)
---
# ex-15 range stops and replenishment

**Covers:** `vehicle/vehicle.*`, `vehicle/air_vehicle.*`, `simconvoi.cc`
(range checks and `REPLENISHING`), `dataobj/schedule_entry.h`,
`dataobj/schedule.cc`, `gui/schedule_gui.*`, `descriptor/vehicle_desc.h`,
`descriptor/building_desc.h` and `path_explorer.cc` (forced-stop through
boundary). Parent: [schedule-and-consists](schedule-and-consists.md).

Read this before implementing automatic range-stop detection, replenishment
dwell, replenishment facilities or replacement on replenishment. Simutrans
vocabulary is defined in the [glossary](../glossary.md).

## Terms

- **Convoy** is the Simutrans object represented by `convoi_t`.
- **Range** is the maximum distance a vehicle can travel before it must be
  replenished, expressed by `vehicle_desc_t::get_range()` in kilometres.
- **Replenishment** is refuelling or otherwise restoring the vehicle's range. Its
  duration is `vehicle_desc_t::get_replenishment_seconds()`.
- **Automatic range stop** is a scheduled halt selected because the vehicle
  cannot reach a later halt before exceeding its range.
- **Forced range stop** is a halt marked `schedule_entry_t::force_range_stop`,
  requiring replenishment there whether or not the automatic calculation selects
  it.
- **Replenishment facility** is a stop containing a building whose descriptor has
  `building_desc_t::FLAG_REPLENISH_ENABLE`.

## Settled design

- Range is static and comes from the vehicle descriptor. Deriving it from
  simulated fuel consumption is rejected by design
  [FORUM:https://forum.simutrans.com/index.php/topic,14991.msg193647.html].
- Automatic range stops are selected by accumulating distance since the last
  replenishment or depot visit. A stop becomes a range stop when passing it
  would leave the convoy unable to reach the following stop within its range.
  The worked example is in
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193779.html].
- A forced range stop is always treated as a range stop. The schedule window
  exposes the forced flag on halt entries and disables it on waypoints
  (non-stop schedule points) and depot entries.
- Replenishment may overlap loading and unloading. Its duration can extend the
  minimum stop dwell rather than cancelling ordinary loading; ships and aircraft
  with long loading times may therefore absorb it without extra delay
  [FORUM:https://forum.simutrans.com/index.php/topic,16980.msg161553.html].
- A depot visit replenishes the vehicles. `vehicle_t::maintain()` and
  `vehicle_t::overhaul()` reset `km_since_last_replenish`, and the counters are
  saved with the vehicle.
- Automatic range stops and forced range stops have distinct schedule
  indications. The recorded design proposes black for an automatic stop and dark
  red for a forced stop
  [FORUM:https://forum.simutrans.com/index.php/topic,17852.msg193779.html].
- Replacement with `replace_data_t::on_replenish` is intended to occur when the
  convoy reaches its replenishment stop. The timing value is saved; the trigger
  is part of this runtime work.

## Current implementation

### Range values and the out-of-range guard

- `convoi_t::calc_min_range()` sets `convoi_t::min_range` to the smallest non-zero
  descriptor range among the convoy's vehicles. A convoy of only zero-range
  vehicles has `min_range` zero and no range restriction.
- `convoi_t::prepare_for_routing()` skips waypoints to find the next halt or
  depot, adds the straight-line distance from the last stop to the current
  position, and compares that distance with `min_range`. Exceeding the range sets
  `convoi_t::OUT_OF_RANGE` and reports a vehicle problem.
- That guard is not the automatic range-stop system. It detects that the next
  scheduled halt is already too far away; it does not select a replenishment
  stop, measure remaining range from `km_since_last_replenish`, or add a stop.
  It also compares the full descriptor range with distance and therefore
  overstates the range remaining to a part-used vehicle.
- `vehicle_t::step_km()` increments `km_since_last_replenish` with the other
  distance counters. No range-selection code reads it. A remaining-range
  calculation must use `vehicle_desc_t::get_range()` less
  `km_since_last_replenish` for each vehicle.

### Schedule authoring and routing

- `force_range_stop` is stored in `schedule_entry_t`, serialised in the schedule,
  editable in `gui/schedule_gui.cc`, and exposed to scripts as
  `schedule_entry_x.force_range_stop`.
- The schedule entry icon displays only `force_range_stop`. The schedule window
  header separately displays a refuelling icon when the selected line or convoy
  has a non-zero `min_range`. That icon does not identify which entries are
  automatic range stops.
- The convoy runtime does not read `force_range_stop`; it has no replenishment
  effect.
- The consist-recombination through-journey predicate does read it. Both
  `path_explorer_t::compartment_t::through_predicate_allows()` and
  `collect_downstream_stops()` stop a through journey at a coupling entry carrying
  `force_range_stop`, in the same way as `lay_over` and `discharge_payload`. This
  is a path-explorer boundary, not replenishment execution.

### Facility support and runtime

- `building_desc_t::FLAG_REPLENISH_ENABLE` and
  `building_desc_t::replenish_enable()` exist. No halt method queries the flag.
- `vehicle_t::replenish()` sets the convoy state to `REPLENISHING`, resets
  `km_since_last_replenish`, and sets `wait_lock` to the descriptor's
  replenishment time. It has no callers. The convoy information window displays
  a `REPLENISHING` status and the remaining wait.
- Calling `vehicle_t::replenish()` separately for several vehicles would overwrite
  `wait_lock` each time. The runtime needs one convoy dwell value derived from the
  applicable vehicles, analogous to the longest-vehicle rule for maintenance.
- The ordinary convoy loader does not start replenishment. No code checks
  facilities or requests replenishment. `convoi_t::step()` handles the return
  from replenishment to loading and departure; the runtime still has to call
  `vehicle_t::replenish()` from the stop logic.
- The completion path is safe: when the replenishment dwell expires,
  `convoi_t::step()` sets the state to `LOADING` before calling `laden()`, so the
  ordinary loading and departure rules apply. If `laden()` starts another
  replenishment dwell, the step returns without falling through to
  `SELF_DESTRUCT`. The runtime must still supply one convoy dwell value derived
  from the applicable vehicles, as noted above.
- `replace_data_t::on_replenish` has no trigger code.

### Verification

The active scripted test suite checks the `force_range_stop` field and the
`OUT_OF_RANGE` and `REPLENISHING` state constants. It does not exercise automatic
selection, forced-stop display, facility detection, replenishment dwell,
out-of-range recovery, replacement on replenishment or save/reload.

## Outstanding design decisions

### Facility requirement and failure behaviour

`replenish_enable` exists, so replenishment at a stop is intended to depend on a
facility. Two cases remain unspecified:

- whether a forced range stop at a halt without a replenishment facility is
  rejected in the schedule editor, ignored with a warning, or still creates a
  timed stop without restoring range;
- what an automatic selection does when no facility is reachable within range.
  The convoy cannot satisfy the range requirement, so the result needs an
  explicit failure state and player report rather than an arbitrary long stop.

No option has been selected. The mechanism must report the actual reason: a stop
not equipped for replenishment and a convoy unable to reach any suitable facility
are different conditions.

### Which vehicles replenish at a range stop

The stop is triggered by the vehicle with the least remaining range, but that
does not decide whether every vehicle in the convoy replenishes there or only
vehicles below a threshold. Replenishing all vehicles matches the rule that a
maintenance visit maintains the whole convoy, but consumes additional time
without a cost in the current model. Replenishing only vehicles that need it
would require a separate threshold rule.

The proposed default is to replenish all vehicles in the convoy `[UNVERIFIED]`.
That keeps one stop event and one dwell calculation, and resets the whole
convoy's remaining-range state at the same physical facility.

### Replacement at a replenishment stop

`replace_data_t::on_replenish` is documented as replacing when the convoy reaches
its next replenishment stop. Current replacement execution requires a depot:
`convoi_t::replace_now()` obtains its vehicle source and destinations through the
home depot and `depot_t::append_vehicle()`. A replenishment stop need not be a
depot, so the intended location and mechanism of replacement are unresolved.

The choices are:

- perform the replacement at the range stop, which requires a separate
  vehicle-exchange path that does not depend on a depot;
- mark the convoy for replacement on replenishment and perform the exchange at a
  later depot visit.

This must be decided before connecting the trigger. It changes when players can
replace stock and whether range stops can replace depots as fleet-change points.

### Forced range stops and through journeys

The path explorer currently treats `force_range_stop` as a boundary for
through-journey candidates. The replenishment design however permits loading and
unloading during replenishment and does not require passengers, mail or goods to
be discharged. If they can remain aboard, blocking a through-journey record at
the stop would understate a valid through journey.

The options are:

- remove `force_range_stop` from the through-journey predicate if replenishment
  does not discharge the payload;
- retain the boundary and make forced range stops discharge the payload, which
  adds a cargo behaviour not present in the original replenishment proposal.

The runtime decision must agree with the path explorer, otherwise routes will be
recorded that the convoy cannot execute or valid through journeys will be
omitted.

### Presentation of automatic stops on a shared line

Automatic stops are derived per convoy because vehicles with different ranges
serve the same line. The schedule window edits a line or convoy schedule, so a
line-level display cannot identify the automatic stops for each assigned convoy
without choosing which convoy's ranges to use or aggregating them.

The choices are to show automatic stops only for concrete convoys, or to show a
line-level advisory based on the most restrictive assigned convoy. The schedule
entry itself remains shared; automatic status cannot be saved onto it as if it
were a forced stop.

## Implementation work requiring no further design

- Find or derive automatic range stops from the current convoy's remaining range
  and the schedule's successive halt and depot distances. After re-combination,
  recompute for each new convoy from the vehicles' saved counters. Each portion
  then stops where its own least-remaining vehicle requires, not where the
  original convoy would have stopped.
- Add a halt facility query for `replenish_enable` and use it for automatic
  selection and forced-stop validation.
- Store or cache the automatic selection without adding persistent savegame data
  unless a future decision requires it. Forced state is already persisted.
- Add a distinct schedule indicator for automatic range stops; retain the forced
  indicator.
- At arrival, derive the replenishment duration from the vehicles that require
  it, preserve ordinary loading and waiting, and return to the normal departure
  path.
- Reset `km_since_last_replenish` only for the vehicles replenished at that stop,
  as selected by the replenishment-scope decision.
- Add behavioural tests for automatic selection, forced stops, mixed-range
  consists, facility failure, dwell, re-combination, save/reload and replacement.

## Open questions

- A halt can contain several stop buildings. A facility query must decide whether
  one building with `replenish_enable` makes the whole halt suitable for every
  convoy, or whether suitability also depends on the halt tile served. The
  building flag itself carries no waytype or tile restriction.
- When should a derived automatic-stop selection be discarded? It must be
  refreshed after a schedule edit, a vehicle change, a consist re-combination,
  and a load that removes or restores range, but the cache lifetime is not yet
  selected.
- If replacement is deferred until a depot visit, does that visit retain the
  convoy's line and current schedule position in the same way as scheduled
  maintenance? The replacement-specific departure position is not recorded.

## Resumption order

1. Decide facility failure, replenishment scope, replacement-at-stop behaviour,
   through-journey semantics and automatic-stop presentation.
2. Add remaining-range and facility queries plus the automatic stop calculation.
3. Add schedule indication and tests for selection.
4. Connect replenishment dwell and the safe departure transition.
5. Connect replacement on replenishment after its location decision.
6. Verify re-combination, save/reload and two-process determinism.
