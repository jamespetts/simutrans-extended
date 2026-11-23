---
status: draft
verified: ex-15 @ a201bf056d plus working-tree junction queueing changes
---
# Road vehicle operations

**Covers:** `vehicle/road_vehicle.*`, `vehicle/simroadtraffic.*` (movement),
`vehicle/vehicle.cc` (`vehicle_base_t::get_blocking_vehicle`), `simhalt.*` (halt position
reservation functions), `descriptor/building_desc.*` (terminal layout count), and the road
tile model. Private car generation, occupancy and information links are covered in
[private-cars](road-vehicle-operations/private-cars.md).

Read [implementation](road-vehicle-operations/implementation.md) when checking what the
road-space and stop-stand code does, its measured object sizes, its tests or its remaining
gaps.

The first two work packages below (road space and queueing, and stop stands with terminal
capacity) are implemented on ex-15. Queueing now covers straight road tiles and road junctions
(a road tile with three or four arms), with an exit-clearance rule for junction entry. The
motorway lane package and pulled-over waiting remain planned. The player-visible intent is also
described in [project-roadmap](project-roadmap.md#road-stop-improvements); [ex-15](ex-15.md)
records non-blocking road vehicles as deferred to after 15.x.

## Terms

- **Halt tile**: a `grund_t` for which `grund_t::get_halt()` returns a bound
  `halthandle_t` and which carries a road way (`grund_t::get_weg(road_wt) != NULL`).
- **Lane**: the existing traffic/passing distinction represented by
  `convoi_t::is_overtaking` and `strasse_t::get_overtaking_mode`.
- **Lateral position**: an index from 0 to 3 formed as `lane * 2 + offset`, where lane 0 is
  the traffic lane and lane 1 the passing lane, and offset 0 is the normal position and
  offset 1 the pulled-over or lateral position. Positions 0 and 1 are therefore the traffic
  lane's normal and pulled-over positions; positions 2 and 3 are the passing lane's normal
  and pulled-over positions.
- **Stop stand**: one of the four physical slots on a halt tile, reserved for one road
  vehicle to load or unload through `haltestelle_t::reserve_position`. The halt maps the
  vehicle's direction and lateral position to the stands that vehicle may use.
- **Pulled-over state**: a stopped or waiting road vehicle at a lateral position whose
  offset is 1, that is, position 1 or position 3. It does not occupy position 0, so traffic
  in position 0 can pass without using the opposing carriageway.
- **Through stop**: a stop on a road with traffic in both directions; it has one stand on
  each side.
- **Terminal**: a road stop building whose `building_desc_t::get_all_layouts()` is 4. The
  station-building placement code treats layout count 4 as a terminal and requires a
  single-direction road tile; through stops use 2, 8 or 16 layouts. A terminal's road halt
  tiles have four stands. Adjacent terminals, with or without choose signs, provide more
  than four stands.
- **Vehicle step**: the unit of `vehicle_base_t::get_steps()`; a straight tile's travel
  path is `VEHICLE_STEPS_PER_TILE` (256) steps, a diagonal path is
  `vehicle_base_t::diagonal_vehicle_steps_per_tile` steps, and 16 steps make one carunit.

## Design hierarchy

The proposal is organised from the road space downwards, so that queueing, pull-over,
terminal stands and motorway lanes are instances of one model rather than separate
mechanisms.

1. **Road space.** A road tile has one or more directions of travel. Each direction has up
   to two lanes (traffic and passing), and each lane has a normal and a pulled-over lateral
   position. Within one lateral position, vehicles are ordered by their longitudinal
   `steps` value on the tile. Blocking, following and pulling over are defined only in
   terms of this road space.
2. **Road user.** Player road vehicles and private cars expose the same road-space state:
   direction, lateral position, longitudinal `steps`, length and current speed. A small
   shared helper reads that state from the two existing classes; it does not introduce a
   new deep class hierarchy into the legacy vehicle classes.
3. **Halt.** A halt tile owns a set of stop stands. A stand is a reservation in the road
   space, and loading requires a stand. The halt maps a vehicle's direction and lateral
   position to the stands it may use.
4. **Text.** A text buffer may contain link spans. A generic text component parses the
   spans and reports clicks; vehicle-specific code only supplies the text and targets.
5. **Policy.** The terminal layout rule, the private car occupancy rule and the motorway
   lane-choice rule are policies layered on the four models above.

## Behaviour before this work

This section records the behaviour that the design replaces, so that the reasons for the
change remain legible. What the implemented code now does is in
[implementation](road-vehicle-operations/implementation.md).

### One vehicle per lane per tile

`objlist_t::intern_add_moving` already allows more than one moving object on a tile, so the
tile data structure is not the limit. The limit is a policy in
`vehicle_base_t::get_blocking_vehicle`: a vehicle on the target tile with the same
direction and lane is returned as a blocker regardless of its position within the tile.
`road_vehicle_t::can_enter_tile` and `private_car_t::can_enter_tile` use that result to
decide whether to enter; their tile caps of 250 and 200 objects are safety limits, not the
capacity rule.

The movement code does not check for a leader on the same tile. Once a vehicle is on a
tile, `vehicle_base_t::do_drive` advances it to the end of that tile without checking for
another vehicle ahead in the same lane on the same tile. Allowing several vehicles to share
a tile therefore requires both an entry rule and a movement cap within the tile.

### Stops and capacity

`road_vehicle_t::is_target` accepts a halt tile only when
`haltestelle_t::get_empty_lane(gr, cnv)` is non-zero. `haltestelle_t::get_empty_lane`
returns a two-bit mask: bit 0 is the traffic lane and bit 1 is the passing lane. Bit 1 is
offered only when the road's `overtaking_mode` is `halt_mode`; the mode is set on the road
by the player through the way tooling. `haltestelle_t::tile_t` stores two reservations,
`reservation[0]` and `reservation[1]`. `haltestelle_t::reserve_position` reserves the first
free lane, and `haltestelle_t::find_free_position` uses the same mask. The road branch of
`haltestelle_t::is_reservable` is narrower: it tests only `reservation[0]` and the presence
of a road vehicle, so `road_vehicle_t` may fall back to `find_free_position` and a new route
search even when a second lane is free.

A road vehicle that stops at a halt remains in its travel lane. A following vehicle must
either wait or move to the opposing carriageway to pass; `convoi_t::is_loading()` is the
state used by overtaking decisions while loading, so a loading vehicle is treated as a slow
or stationary obstacle.

A through stop can use one lane in each direction only when the road is in `halt_mode`, so
it can hold two vehicles, one per direction. A dead-end stop has only one direction of
travel, so it can hold one vehicle. The dead-end terminal therefore has half the capacity
of a through stop configured with `halt_mode`.

## Proposed design

### Lateral positions and blocking

The road-space model has lateral positions, but a vehicle does not need to store a full
index in the first two packages. The current traffic/passing lane is already stored in
`convoi_t::tiles_overtaking` and `private_car_t::tiles_overtaking`. The pulled-over state
is derived: a road vehicle is pulled over when it holds a stand mask, the stand is not a
terminal stand 0, and it is loading, in layover, or stopped at the stand. The only new persistent datum for the first two packages is the
four-bit stand mask on `road_vehicle_t`, and it fits existing padding. The motorway package
may need a persistent travel-lane index; add it then, not now.

Vehicles in different lateral positions do not block one another, except where their paths
cross at an intersection or crossing. Vehicles in the same lateral position block or
follow one another according to the spacing rule below.

Position 0 is the normal travel position. A vehicle stopped at a stand uses the lateral
position mapped to that stand. A vehicle waiting behind a stopped vehicle may use a
pulled-over position if the stop layout provides one; the first implementation can defer
that case and leave the waiting vehicle in position 0. The existing `overtaking_base_offsets`
table and the negative offset used for a vehicle being overtaken provide the graphical
offset for position 1. Positions 2 and 3 use the passing-lane offset and an additional
pulled-over offset.

### Following distance within one lateral position

The entry rule and the movement cap use the nearest vehicle ahead in the same lateral
position on the same tile, and a vehicle whose front has crossed into the next tile while
its rear is still on this tile. The rear boundary of that leader is its `steps` value minus
its length in vehicle steps. The follower may not pass that boundary minus a gap. The gap has
three cases:

- coupled vehicles: zero gap, as for the existing convoy length calculation;
- stationary traffic: a standstill gap in metres, converted to vehicle steps using
  `settings_t::get_meters_per_tile()`;
- moving traffic: the standstill gap plus a reaction-time headway, calculated from the
  follower's speed.

The standstill gap and reaction time are real quantities, not arbitrary tuning values. The
exact values should be chosen from road-design or driver-behaviour references and recorded
in the code. `VEHICLE_STEPS_PER_TILE` and `YARDS_PER_VEHICLE_STEP_SHIFT` provide the unit
conversions.

Apply this rule on straight road tiles and on road junctions (T junctions and crossroads).
Bends, diagonal road tiles and level crossings keep the one-vehicle rule. A junction tile uses
the same following rule only between vehicles that enter by the same edge and leave by the
same edge; `vehicle_base_t::get_blocking_vehicle` still refuses entry to a vehicle on a
conflicting path. A vehicle whose route ends on the tile has no exit direction and is treated
as on the same path as any vehicle entering by the same edge.

### Junction entry and exit clearance

A junction tile cannot safely admit a queue without a further rule: a vehicle which stops
inside the junction can block the crossing traffic. A vehicle entering a junction therefore
also requires room on the tile it will enter after the junction. The room is the distance from
the exit tile's entry edge to the nearest vehicle ahead in the same lateral position that
entered the exit tile from the junction. That distance must be at least the entering vehicle's
length plus the standstill gap, so the vehicle can stop behind the queue with its whole body
clear of the junction. A vehicle longer than one tile is capped at one tile's length, because
such a vehicle cannot clear a one-tile junction and must be allowed to pass through.

A vehicle on the exit tile that entered from another arm is treated as blocking, because its
path may conflict with the entering vehicle. Oncoming traffic in the other carriageway and
vehicles beside the lane at a stop stand are ignored. This is the road equivalent of the rule
that a driver must not enter a box junction unless the exit is clear. It prevents the gridlock
case where a vehicle enters a junction, stops behind a queue, and blocks the cross traffic
which the vehicle ahead needs in order to leave.

The entry check belongs in the road-specific `can_enter_tile` functions. The movement cap
belongs in `road_vehicle_t::do_drive` and a new `private_car_t::do_drive` override. The cap
must be re-evaluated after each tile hop, so the road-specific `do_drive` should process at
most one tile boundary per iteration and then recompute the leader. This avoids changing
`vehicle_base_t::do_drive`, which is shared with rail, air and water vehicles.

The implemented rule also caps the follower's speed, not only its position: convoys cap the
desired speed in `convoi_t::calc_acceleration`, and private cars cap `current_speed` in
`private_car_t::sync_step`. A vehicle that can overtake tries to do so before the cap is
applied. A stopped private car calls `private_car_t::try_resume_behind_leader` on every sync
step, so it starts as soon as the gap to the vehicle ahead opens, without waiting for the
1024-tick `private_car_t::sync_step` retry gate. See
[implementation](road-vehicle-operations/implementation.md#following-speed-and-overtake-priority).

### Stop stands and pull-over

Replace the two-lane reservation with four stand reservations:
`haltestelle_t::tile_t::reservation[4]`. The reservation array is not serialised, so the
halt side of this change does not alter the savegame format.

`haltestelle_t::get_empty_lane` becomes a stand mask. A through stop exposes one stand on
each side, so a vehicle approaching in either direction has one valid stand. A terminal
exposes four stands on its road tile. The mask is direction-aware: it contains only the
stands that the approaching vehicle's direction and lateral position may use.
`haltestelle_t::reserve_position` records the chosen stand on the road vehicle, and the
vehicle uses that stand for rendering and for the loading decision.
`haltestelle_t::is_reservable` must use the same mask for road vehicles instead of testing
only `reservation[0]`.

When a vehicle reaches a halt with a reserved stand, it enters the pulled-over state and
loads or unloads at that stand. Traffic in position 0 passes. When no stand is free, the
vehicle waits in position 0 in the first implementation; it blocks the lane, but the stop
is already at capacity. Pulling waiting vehicles over is a later extension because it
requires a rule that prevents a waiting vehicle from loading before it has a stand; that
rule would otherwise require changes in `convoi_t::hat_gehalten` or `convoi_t::laden`.

### Terminal detection

No new descriptor datum is needed. `building_desc_t::get_all_layouts() == 4` is already the
terminal marker: `tool_build_station_t::tool_station_aux` treats layout counts 2, 8 and 16 as
through stations and layout 4 as a terminal, which must stand on a single-direction road or
track. A terminal gets four stands per road halt tile; a normal stop gets one per side.
`haltestelle_t::add_grund` can read the descriptor through the halt tile's `gebaeude_t` and
record the count on `haltestelle_t::tile_t`. No pakset change is needed.

### Motorway lanes

The design generalises the existing two lanes per direction by giving each lane two lateral
positions. On a one-way road, positions 0 and 1 are the traffic lane's normal and lateral
positions and positions 2 and 3 are the passing lane's normal and lateral positions, giving
four lanes in each direction. On a two-way road, a vehicle uses positions 0 and 1 for the
traffic lane and its pulled-over position; positions 2 and 3 belong to the passing lane.
This phase also needs a lane-choice rule for vehicles on a one-way multi-lane road. The
current code uses `tiles_overtaking`, `lane_affinity` and `next_lane` for the two-lane case;
extending that logic is a separate work package.

## Work packages and ordering

1. **Road position and queue core.** Use the existing lane state, add the packed stand mask
   to `road_vehicle_t`, derive the pulled-over state, and add the same-position leader
   search, gap calculation and movement cap. Apply queueing to straight tiles and to road
   junctions, with the junction exit-clearance rule. Add scripted queue and crossroads cases
   and measure the private-car cost.
2. **Stop stands and terminal capacity.** Extend `haltestelle_t::tile_t` to four
   reservations, make `get_empty_lane`, `reserve_position`, `is_reservable` and
   `find_free_position` stand-aware, use the existing layout count 4 as the terminal test,
   and make a loading vehicle use its stand's lateral position. Add through-stop and
   terminal cases.
3. **Motorway lanes.** Extend lane choice to four lateral positions on one-way roads. This
   is the largest and least defined package; it should follow the stop work.

Packages 1 and 2 share the lateral-position model and should be developed together. The
private car occupancy and information-link packages are in
[private-cars](road-vehicle-operations/private-cars.md).

## Serialisation, network and pakset impact

- The stand mask is synced state and must be saved and loaded with the road vehicle. The
  implemented form is a `road_vehicle_t::rdwr_from_convoi` override that calls
  `vehicle_t::rdwr_from_convoi` and then reads or writes the road fields. The current lane
  is already saved in `tiles_overtaking`; no private car lane field is needed before the
  motorway package.
- The stand reservation is transient: `haltestelle_t::tile_t` reservations are not saved.
  The implemented code records the stand when the leading vehicle enters its final halt tile
  and releases it on departure, so a loaded vehicle at a stand keeps the saved mask until the
  convoy departs. See [implementation](road-vehicle-operations/implementation.md).
- `EX_SAVE_MINOR` was incremented from 19 to 20 for the stand mask, the city-car fill mode and
  the city congestion counters. The `gui/settings_stats.cc` revision list already contained
  `20`, so no array change was needed.
- The gap calculation must use integers only. Speeds, lengths and distances can all be
  converted to vehicle steps and yards with the existing integer constants.

## Resource and performance audit

Counts from the ex-15 census at `b53e370` (2026-11-20): 6,048 halts, 8,456 convoys and
16,808 player vehicles. The census does not split road vehicles or count private cars, so a
new counter is needed before the performance work can be sized precisely.

Memory:

- `haltestelle_t::tile_t` holds an 8-byte `grund_t*` and four 2-byte `convoihandle_t`
  reservations, plus a one-byte stand count. The old record used 12 bytes, padded to 16 on
  64-bit; four reservations still fit 16 bytes, but the stand count makes the record 17
  bytes, which the 8-byte alignment pads to 24. The record therefore grows by 8 bytes per
  halt tile on x64.
- A size probe on the built Debug|x64 MSVC configuration gives `road_vehicle_t` 720 bytes and
  `private_car_t` 224; the default (non-`_DEBUG`) configuration gives 712 and 216. Removing
  the four-bit stand mask from `road_vehicle_t` left its size unchanged in both
  configurations, so the mask costs no object memory; it is packed with the existing
  `is_checker` bit. City-car capacity and length are two bytes per descriptor.

Performance:

- City traffic is already a first-class cost: `private_car_t::sync_step` is 18.6% inclusive
  and `private_car_t::hop_check` is 7.4% inclusive on the gargantuan fixture
  [EXECUTION-VERIFIED:2026-09-10] (see [objects](objects.md) and
  [performance](performance.md)).
- Remembering the lane/pulled-over state is not the cost: the byte is in the same cache
  line as the vehicle, needs no allocation or pointer chase, and fits existing padding.
  Placing it in padding avoids shifting later fields and cache-line offsets. The cost is the
  comparison in the blocking test and the extra leader scan, not storage.
- `road_space::following_limit` walks the object list of the vehicle's tile and, when the
  vehicle's next tile differs from it, the next tile's list as well, on every call. A moving
  road vehicle calls it at least once per sync step while it has distance to travel, and
  again after each tile hop. A stopped private car calls it once per sync step in
  `private_car_t::try_resume_behind_leader`; the `hop_check` call at the `ms_traffic_jam`
  gate remains once per 1024 ticks. The worst case is one extra object-list scan per stopped
  private car per sync step. Use the existing object list; add a transient cache only if
  profiling shows the scan is material.
- `get_empty_lane` builds the four-bit mask in one pass. It is called during target selection
  and on arrival, not every step. Four stands can reduce queue length and congestion.
- Junction queueing adds no per-step scan beyond the existing following scan: `allows_queueing`
  now also accepts a junction tile, and `following_limit` runs the same object-list scan there.
  `vehicle_base_t::get_blocking_vehicle` calls `road_space::can_follow` for each vehicle on a
  queueing target tile, so a same-path vehicle that is far enough ahead is skipped and a
  conflicting vehicle is still returned; this happens at a tile entry, not every step.
  `road_space::junction_exit_clear` adds one scan of the exit tile when a vehicle enters a
  junction. The entry check builds the road-user state only when there is a blocker or the
  target tile is a junction, so an ordinary clear tile entry does not pay for it. The rule
  adds no persistent field to any vehicle or tile.
- The private car occupancy rule changes the number of cars directly. The child document
  gives the expected cars per packet for a uniform packet size of 1 to 7.

The exact audit needs a TEST-marked census of moving road users, private cars, object-list
lengths and reservations, then the performance suite; the instrumentation must be reverted.

## Tests

The implemented cases and results are in
[implementation](road-vehicle-operations/implementation.md#tests). Run game tests with `-nomidi`.

## Open questions

- Is this work to be brought into ex-15 now, given that [ex-15](ex-15.md) records
  non-blocking road vehicles as deferred to after 15.x?
- Should pulled-over waiting be added for a vehicle that finds no free stand, instead of the
  current wait in the travel lane?
- Is the four-lane motorway generalisation in the current scope, or a later package?
