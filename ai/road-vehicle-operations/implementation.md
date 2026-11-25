---
status: draft
verified: ex-15 @ 82b143fc38 plus working-tree junction clearance changes; ex-15 @ f41b00399f (2026-11-25, trigger-wait road case and test counts)
---
# Road vehicle operations: implementation

**Covers:** the implemented part of [road-vehicle-operations](../road-vehicle-operations.md): the
`road_space` following rules, the four stop stands, the stand mask save format, the measured
object sizes and the tests. Private car occupancy and information links are in
[private-cars](private-cars.md).

Read this when checking what the road-space and stop-stand code does, its object sizes, its
tests or its remaining gaps. The design, current behaviour and resource audit are in
[road-vehicle-operations](../road-vehicle-operations.md).

The first two work packages of the parent document are implemented on ex-15: road space and
queueing, and stop stands with terminal capacity. Queueing covers straight road tiles and road
junctions; the junction exit-clearance rule described below prevents a vehicle from stopping in
a junction and blocking the cross traffic. The motorway lane package and pulled-over waiting
are not implemented.

## Road space and queueing

`vehicle/road_vehicle.cc` holds the shared rules in the namespace `road_space`:

- `road_space::allows_queueing` accepts a road tile whose unmasked ribi is straight
  (`ribi_t::is_straight`) or has three or four arms (`ribi_t::is_threeway`). It rejects a level
  crossing (`strasse_t::is_crossing`), a diagonal road (`strasse_t::is_diagonal`) and a bend.
  `road_space::is_junction` identifies the subset with three or four arms; that subset also
  requires the exit-clearance rule.
- `road_space::following_gap` computes the gap in vehicle steps from a standstill gap of one
  metre (`STANDSTILL_GAP_MM`) plus, when the vehicle ahead is moving, a perception and
  reaction time of 1.5 seconds (`REACTION_TIME_MS`) at the follower's speed.
  `road_space::reaction_gap_mm` converts km/h and milliseconds to millimetres as
  `speed * REACTION_TIME_MS * 1000 / 3600`. A 50 km/h follower therefore keeps about 20.8 m,
  and two `static_assert`s in `road_vehicle.cc` fail the build if that value leaves the range
  20 m to 21 m. `settings_t::get_meters_per_tile()` converts the total to vehicle steps and the
  result is rounded up, so a real gap never becomes zero.
- `road_space::following_limit` returns the furthest position, in vehicle steps, that a
  road user's front may reach on its tile, or `-1` when nothing constrains it. It compares
  only road users in the same lateral position and on the same path through the tile. The
  path comparison requires the same entry direction (the direction of travel onto the tile)
  and the same exit direction (the direction of travel off it). A vehicle
  which has reached the end of its route has no exit direction; it is treated as on the same
  path as a vehicle entering by the same edge, so a vehicle can enter a terminal tile behind a
  vehicle already standing there. A vehicle whose front is on the same tile constrains the
  follower whenever that front is ahead of the follower's front; the limit is the vehicle's
  rear boundary (its front minus its length) less the gap. A vehicle which is already
  overlapping the follower is therefore still a blocker: the follower waits instead of driving
  through it. The limit is clamped to zero because `-1` means "no constraint"; a negative value
  would otherwise let the follower drive to the end of its tile when the gap grows, as it does
  when a stopped queue starts moving and the reaction distance replaces the standstill gap.
- `following_limit` also looks ahead into the tile returned by
  `vehicle_base_t::get_pos_next`. A vehicle whose front has crossed into the next tile
  still has its rear on the follower's tile while its front is shorter than its length,
  and it constrains the follower in the same way. A vehicle on the next tile is considered
  only if it entered through the shared tile edge, so that both vehicles' step coordinates
  start at that edge. A vehicle loading at a stop stand is skipped only when its whole body
  is inside the next tile; while its rear is still at the boundary it continues to cap the
  follower, so a vehicle cannot overlap it as it enters its stand.
  `road_space::step_scale` returns the length of one step on a tile in 512ths of a
  straight step; a diagonal tile's step covers
  `vehicle_base_t::get_diagonal_multiplier()` 512ths. The same conversion is used for a
  vehicle turning on a cardinal junction tile, because such a vehicle also uses the diagonal
  step count.
- `road_space::get_user_state` gives a leading convoy vehicle the whole convoy length,
  because the convoy's rear is that vehicle's front minus the convoy length. A trailing
  vehicle gives its own length instead, which is the rear of the part of the convoy on its
  tile. Each vehicle of a convoy is therefore a rear boundary of the convoy's body, not a
  separate road user with the full convoy length.
- `road_space::can_follow` is the entry test: the follower's front enters at step 0, so an
  accepted leader's rear boundary must be more than one gap ahead. It also requires the same
  path (entry and exit direction) and lateral position. `vehicle_base_t::get_blocking_vehicle`
  calls it for each vehicle on a queueing target tile, so a same-path vehicle which is far
  enough ahead does not block entry, while a vehicle on a conflicting path still does. Only a
  private car or a convoy's leading vehicle can clear a blocker; a trailing convoy vehicle's
  own length does not prove that the convoy's rear is clear.
- `road_space::drive_until_limit` performs one base `vehicle_base_t::do_drive` call for at
  most one tile boundary. `road_vehicle_t::do_drive` and the new `private_car_t::do_drive`
  repeat it after each hop and recompute the leader, so a vehicle cannot pass the leader while
  the hop carries it forward. A non-leading vehicle of a convoy is moved by the distance that
  the leading vehicle reports, as before.
- `private_car_t::sync_step` passes the accumulated distance in `road_user_t::weg_next` to
  `private_car_t::do_drive`, then reduces `weg_next` by the returned distance, or clears it
  when the returned distance is not smaller. `private_car_t::do_drive` clears `weg_next` only
  when at least one whole vehicle step remains after the movement loop, because that means
  the car was held behind a leader or at a tile boundary; clearing the whole of the unspent
  distance stops it being travelled in one call when the obstruction moves. A remainder
  smaller than one vehicle step is left in `weg_next`, so a car whose speed gives less than
  one vehicle step of distance per `private_car_t::sync_step` call accumulates distance until
  it can move.

### Junction entry and exit clearance

`road_space::junction_exit_clear` is called by `road_vehicle_t::apply_following_rule` and
`private_car_t::apply_following_rule` when the target tile is a junction. It scans the tile the
vehicle will enter after the junction and measures the distance from its entry edge to the
nearest vehicle ahead in the same lateral position which entered that tile from the junction.
The distance must be at least the entering vehicle's length plus `road_space::following_gap`
evaluated at the vehicle's maximum speed with the leader treated as moving. The maximum speed
is `convoi_t::get_min_top_speed()` for a convoy and `citycar_desc_t::get_topspeed()` for a
private car; the larger of that value and the current speed is used. This uses the same
reaction-time gap rule as `road_space::following_limit` while the vehicle moves, so a vehicle
admitted to the junction cannot be held inside it by a leader that was already ahead at entry.
For a vehicle longer than one tile, the requirement is capped at one tile's length: such a
vehicle cannot clear a one-tile junction, so an uncapped requirement could never be satisfied.

The check evaluates `road_space::allows_queueing(exit)` before scanning the tile. If it is
false, the exit tile is a level crossing, a diagonal road tile, a bend or a dead end, and it
does not admit a following vehicle in one lateral position, so a vehicle on the exit tile on
the same path as the entering vehicle blocks entry regardless of its position. A bend, a
diagonal road tile and a level crossing have two road arms, so a vehicle which is not on the
entering vehicle's path is coming the other way; `vehicle_base_t::get_blocking_vehicle`, which
decides which vehicles block entry to a tile, permits a vehicle travelling the other way to
share the tile, so that vehicle does not block entry. If
`road_space::allows_queueing(exit)` is true, the scan skips a vehicle beside the lane at a stop
stand and skips oncoming traffic whose entry direction is the reverse of the entering vehicle's
exit direction, while a vehicle on the exit tile which entered from another arm is treated as
blocking because its path may conflict with the entering vehicle. A vehicle which entered from
the junction is converted from its tile's step scale to straight steps with
`road_space::step_scale` before its rear is compared with the required distance; this handles a
vehicle turning on a cardinal junction tile.

The check is made at junction entry, not every step. If the exit tile fills after a vehicle has
entered the junction, the ordinary following rule still prevents an overlap; the exit-clearance
rule prevents the common gridlock case where a vehicle deliberately stops in the junction and
blocks the crossing traffic that the vehicle ahead needs in order to leave.

### One-way dead ends

`private_car_t::can_enter_tile` refuses entry to a one-way dead end when
`strasse_t::get_overtaking_mode()` is `oneway_mode` and the target tile has no road arm other
than the one back to the car's current tile. The refusal does not apply when `get_pos()` equals
`pos_next_next`, which marks a U-turn back towards the current tile. The U-turn is the only way
out of the dead end, so refusing entry would leave the car waiting at the entrance to the dead
end indefinitely.

### Following speed and overtake priority

`road_space::following_limit` records the nearest vehicle ahead in the same lateral position
in a `road_space::leader_t`. The record holds the distance from the follower's front to the
leader's rear in vehicle steps, the leader's speed in internal speed units, whether the
leader is moving, and a `convoihandle_t` for a convoy leader. The handle is unbound when the
leader is a private car. The next-tile scan also records a leader whose rear is entirely on
the next tile; that leader does not cap the follower's position on the current tile, but it is
still the nearest leader for the speed rule.

`road_vehicle_t::do_drive` stores the record on the leading vehicle. `convoi_t::calc_acceleration`
reads it and applies `road_space::safe_following_speed`. That function takes the equilibrium gap
at the leader's speed from `road_space::following_gap`, so a stopped leader uses the standstill
gap and a moving leader uses the standstill gap plus the leader's reaction distance. The
follower may use any surplus over that equilibrium gap to close it over one reaction time, so
its safe speed is the leader's speed plus the surplus divided by the reaction time. The safe
speed rises continuously as the gap opens and falls as the follower closes it. A convoy caps
`convoi_t::akt_speed_soll` at the safe speed when that is below its own desired speed. The
position cap in `road_space::following_limit` remains the last resort that prevents an overlap.

If the safe speed is below the desired speed and the convoy is not already overtaking, the
convoy calls `convoi_t::can_overtake` once for a convoy leader before applying the cap. The
call uses `convoi_t::akt_speed_soll_unlimited` in the overtake decision. If it starts an
overtake, the cap is not applied: a vehicle that can pass does not slow down first. The call is
made once per following episode; `convoi_t::following_cap_active` records that it has been made
and is cleared when the convoy loses the leader or starts overtaking. A private car leader does
not trigger this proactive call; the ordinary overtake decision in
`road_vehicle_t::can_enter_tile` still handles it at a tile boundary. While the convoy is
following, that decision is evaluated at every tile boundary, so it keeps looking for an
opportunity to pass.

`private_car_t` applies the same rule to city traffic. `private_car_t::do_drive` calls
`road_space::drive_until_limit`, so it also receives the `road_space::leader_t` and sets
`overtaker_t::following_speed` to `road_space::safe_following_speed`.
`private_car_t::sync_step` clamps `current_speed` to `overtaker_t::following_speed` before the
car moves. After `private_car_t::do_drive` it calls `private_car_t::accelerate(delta_t)` unless
the car was held with a whole vehicle step of distance accumulated: a car held by an obstruction
does not accelerate, while a car with only a sub-step remainder is still accumulating distance
at a low speed and does accelerate.
`private_car_t::accelerate` spreads the per-tile increment `max_speed >> 2`, where `max_speed` is
`desc->get_topspeed()`, over the ticks that a tile takes at the car's top speed. A tile is
`1 << YARDS_PER_TILE_SHIFT` yards and the top speed is in yards per tick, so the tile takes
`(1 << YARDS_PER_TILE_SHIFT) / max_speed` ticks; the `delta_t` argument is also in ticks. Each
call therefore adds `(max_speed >> 2) * delta_t / tile_ticks`, with a minimum of one speed unit,
and then applies the way limit, the vehicle top speed and `overtaker_t::following_speed`. A car
that has been held at a low speed therefore accelerates as soon as the safe speed rises.
`private_car_t::hop_check` clears the limit when the car restarts. `private_car_t::can_overtake`
uses the speed the car would use without the following rule,
`max(current_speed, min(desc->get_topspeed(), kmh_to_speed(str->get_max_speed())))`, in the speed
difference, distance and time calculations, so a car that has slowed behind a leader can still
overtake. `private_car_t::can_enter_tile` applies the following rule only after the overtake
decision, as `road_vehicle_t::can_enter_tile` does, so the overtake gets the first chance.

A car with `private_car_t::current_speed == 0` does not run `private_car_t::do_drive`, so nothing
in the movement code refreshes `overtaker_t::following_speed` while it waits. The stopped branch
of `private_car_t::sync_step` therefore calls `private_car_t::try_resume_behind_leader` on every
sync step. That function builds the same `road_space::user_t` as `private_car_t::do_drive`, calls
`road_space::following_limit` for the leader on the car's tile and the next one, and returns
`false` when there is no leader, when the position limit still holds the car, or when
`road_space::safe_following_speed` is zero. Otherwise it sets
`overtaker_t::following_speed`, resets `private_car_t::ms_traffic_jam`, sets `current_speed` to
the restart speed 48 and lets the normal movement code run in the same step. A car stopped at the
end of its tile needs `private_car_t::hop_check` to move, and that call is expensive, so the
function resumes a car at a tile end only when the gap has just opened and leaves later attempts
to the retry gate described next. The gate is the `ms_traffic_jam >> 10` boundary test in
`private_car_t::sync_step`; `ms_traffic_jam` is a count of ticks, so the gate opens once every
1024 ticks. Without the per-step evaluation, `overtaker_t::following_speed` keeps the zero value
from the last `private_car_t::do_drive` call, and a car held behind a vehicle could start only
when the gate opened, regardless of how soon the vehicle ahead moved.

Two changes keep `convoi_t::can_overtake` usable after the following rule has reduced the
convoy's current speed. The speed difference and the overtake distance use
`max(akt_speed, akt_speed_soll_unlimited)` instead of `akt_speed` alone. The oncoming-traffic
phase, the second `while` loop in `convoi_t::can_overtake`, is skipped only when
`strasse_t::get_overtaking_mode()` returns `oneway_mode`, because a one-way road has no facing
traffic. A `halt_mode` road is a two-way road and still runs the phase. The loading branch's
route scan also stops at the end of the route instead of reading past it. The in-congestion
distance calculation stores `convoi_t::get_max_power_speed()` in `own_max_power_speed` and
returns false when that is not greater than `other_speed`, so the division by their difference
cannot be a division by zero.

The speed rule calls `road_space::following_gap` once per road convoy and once per private car
per step while a leader is in the current or next tile, and performs a few integer
multiplications and divisions. The private car rule reuses the leader record that
`private_car_t::do_drive` already obtains, so it adds no scan for a moving car.
`private_car_t::accelerate` adds one integer multiplication and one division per moving car per
step. A stopped car calls `road_space::following_limit` once per step, which scans its own tile
and the next one. That adds one scan per stopped car per step to the existing
`private_car_t::hop_check` call at the 1024-tick gate, which does the same scan plus the private
car route lookup and `private_car_t::can_enter_tile`. The performance suite has not been run for
the stopped-car rule; the `road` pending-runtime group, the standard automated suite and the
network smoke test are the automated coverage.

The lateral position of a road vehicle is the existing traffic or passing lane
(`convoi_t::is_overtaking`, `private_car_t::is_overtaking`) plus the pulled-over state.
`road_vehicle_t::is_pulled_over` is true when the leading vehicle holds a stand bit, the
stand is not a terminal stand 0 (which is the normal travel position), and the convoy is
loading (`convoi_t::is_loading`), laid over (`convoi_t::LAYOVER`) or stopped at the stand
(the leading vehicle has reached its position limit within the tile and the convoy is
stationary). The last condition
covers the wait at the stop after loading, including the wait for a reversal, when the convoy
is no longer loading but the vehicle is still beside the lane. A vehicle that is pulled over does not
block traffic in position 0: `road_space::following_limit` skips it, and both
`road_vehicle_t::apply_following_rule` and `private_car_t::apply_following_rule` clear such a
vehicle as a blocker.

## Stop stands and terminal capacity

`haltestelle_t::tile_t` holds `reservation[4]` and the number of stands on the tile
(`stands`). `haltestelle_t::add_grund` sets `stands` to 4 when a road halt tile's `gebaeude_t`
has a `building_desc_t::get_all_layouts()` value of 4. Every other halt tile keeps the default
value of 2; the field is read only for road halt tiles.

The stand indices are:

| index | meaning |
|---|---|
| 0 | traffic lane, normal position (terminal only) |
| 1 | traffic lane, pulled over |
| 2 | passing lane, normal position (terminal only) |
| 3 | passing lane, pulled over (terminal only) |

A through stop offers exactly one stand to a vehicle: index 0 for a vehicle travelling north or
east and index 1 for one travelling south or west. The two directions therefore never share a
stand. A terminal offers all four.

`haltestelle_t::reserve_position` chooses the lowest free stand from `get_empty_lane`, stores it
in `reservation[s]`, and records the bit on the leading vehicle with
`road_vehicle_t::set_stand_mask` if it holds no stand yet. `unreserve_position` clears the bit
and the vehicle's mask. The reservation is made when the leading vehicle enters the last tile
of its route and that tile is a halt (`road_vehicle_t::enter_tile`), because normal road route
finding does not call `reserve_position`; route finding only checks that a stand is free. The
same function records the halt in `target_halt` when the reservation succeeds.
`release_target_reservations` frees the halt positions when the next route is prepared, but
the vehicle keeps its stand mask while it is still on the halt tile. This matters at a stop
where the convoy reverses: the next route is prepared as soon as loading ends, and the convoy
then waits at the stop for the reversal delay. `road_vehicle_t::leave_tile` clears the mask
when the vehicle's next position is a different tile, so the mask survives a final hop within
the halt tile but is cleared on departure. Road convoys keep the same leading vehicle through
a reversal because `convoi_t::execute_reverse_order` does not swap vehicles when
`get_terminal_shunt_mode` returns `wye` (the mode in which the convoy turns without
re-ordering its vehicles), which it does for every non-rail waytype; the stand mask therefore
stays on the leading vehicle. Without the release, ordinary road convoys left
the reservation behind and a through stop lost its stand for the rest of the game.
`get_empty_lane` ignores the reserving vehicle itself and drops a stand that another vehicle
occupies in the same lateral position.

A vehicle that finds no free stand waits in the travel lane or at its previous tile;
pulled-over waiting is not implemented. A terminal stand 0 is the normal travel position and
does not pull the vehicle over.

### Lateral drawing

`road_vehicle_t::get_screen_offset` and `private_car_t::get_screen_offset` draw the lateral
lane position at a blend between the current lane and the target lane.
`road_space::advance_lateral_blend` moves the blend at most as far laterally as the vehicle
moves forward: `LANE_CHANGE_BLEND_STEPS` (192 vehicle steps) covers a full lane offset, and
`PULL_IN_BLEND_STEPS` (64 steps) covers the through-stop pull-in. The overtaking offset uses
`overtaking_base_offsets`; a vehicle being overtaken uses
`road_space::OVERTAKEN_LATERAL_LEVEL` (-51 of 256, a fifth of the full lane offset in the
opposite direction). A stop stand
uses `vehicle_base_t::add_pulled_over_offset`, which is a quarter of the
`driveleft_base_offsets` separation (`PULLED_OVER_OFFSET_DIVISOR` is 4); terminal stands 2
and 3 also include the passing-lane offset. The pull-in blend starts
`PULL_IN_APPROACH_STEPS` (128 steps) before a through stop; a terminal's blend starts when
its stand is reserved. `road_vehicle_t::update_lateral_blend` runs on the leading vehicle
from `convoi_t::sync_step`; `private_car_t::update_lateral_blend` runs from
`private_car_t::do_drive`. Both blends are advanced by the forward distance travelled, so a
vehicle never appears to slide sideways while standing still. The stand mask remains set
while the vehicle is on the halt tile (see above), so a vehicle that reverses at a stop stays
drawn at the stand through the reversal wait and starts to fade out only after it leaves the
tile. The blends are rendering-only: they are not saved, not synced and do not affect
movement.

The stand mask is saved by the `road_vehicle_t::rdwr_from_convoi` override, which calls
`vehicle_t::rdwr_from_convoi` and then reads or writes one byte behind
`loadsave_t::is_version_ex_atleast(15, 20)`. `EX_SAVE_MINOR` is 20. The `revision_ex` array in
`gui/settings_stats.cc` already contains `20`, so no array change was needed.

## Measured sizes

On the built `Debug|x64` MSVC configuration, `sizeof(private_car_t)` is 232 bytes and
`sizeof(haltestelle_t::tile_t)` is 24. The earlier 224-byte private car figure predated the
rendering-only lateral blends. The following-speed storage replaced the unused
`overtaker_t::max_power_speed` field, so it does not enlarge `private_car_t`. The
`road_space::leader_t` cached on `road_vehicle_t` is 8 bytes (a 16-bit gap, a 16-bit speed, a
bool and a `convoihandle_t`); `sizeof(road_vehicle_t)` has not been re-measured for this
change. Removing the stand mask from `road_vehicle_t` left its size unchanged in both
configurations, so the four-bit mask costs no object memory; it is packed with the existing
`is_checker` bit. `convoihandle_t` holds a `uint16`: the old record used 8 bytes for the
pointer and 4 bytes for two reservations, 12 bytes padded to 16. Four reservations use 16
bytes, which still fits that padding, but the added one-byte `stands` count makes the record
17 bytes, which the 8-byte alignment pads to 24. `haltestelle_t::tile_t` therefore grows by 8
bytes per halt tile on x64.

## Tests

`tests/pending-runtime/road-harness.nut` and `tests/pending-runtime/road-cases/` provide the
`road` group, run with `PENDING_RUNTIME_GROUPS=road` (or by case name). It contains fifteen
cases:

- `single-run`: one convoy crosses a straight road to its destination without a false block or
  stall.
- `trigger-wait`: a convoy arrives at a through stop whose entry has a non-zero
  `condition_bitfield_receiver` and the `conditional_depart_before_wait` schedule flag (the
  "wait for trigger" option), with no convoy or line sending a matching trigger. It must stay in
  `AWAITING_TRIGGER` for 100 observations without the `convoi_t::sync_step()` abort reported as
  [GitHub issue 701](https://github.com/jamespetts/simutrans-extended/issues/701).
- `moving-gap`: a fast follower catches a slow leader near the end of a 14-tile road. The
  follower starts when the leader is at x = 10, so the remaining route is too short for
  `convoi_t::can_overtake` and the follower must follow. Once both fronts have been on the
  same tile for more than 20 observations, the follower's speed must be within 15 km/h of the
  leader's speed when the gap is near the equilibrium, and a follower at the leader's speed
  must have settled within twice the equilibrium gap plus eight steps. This catches both the
  old behaviour, where the desired speed stayed at the follower's top speed, and a queue that
  waits for room for the top speed instead of closing up. While both are on the same tile and
  the follower is in the traffic lane, the gap in vehicle steps must be at least the standstill
  gap plus the 1.5 s reaction distance at the follower's speed, converted at the fixture's
  125 m per tile, less a small observation tolerance. At 50 km/h the bound is about 45 vehicle
  steps; the earlier 1000x scaling error produced a gap of a few steps and fails it. The case
  also requires both convoys to reach the destination. The overlap checks are skipped after
  either convoy has reversed at the terminal.
- `overtake`: the fastest and slowest available road vehicle descriptors are selected in one
  pass from the pakset; the fast vehicle starts second and must arrive first, which can only
  happen if it overtook the slow vehicle without a deadlock or head-on conflict. While the
  fast vehicle is still in the traffic lane behind the slow one and has not arrived, it must
  not overlap it; the check is skipped while either vehicle is on the passing lane.
- `junction`: two convoys leave separate depots on an east-west trunk and a north branch and
  cross a T junction from different arms; both must reach the trunk-end terminal.
- `crossroads`: five convoys approach an unsignalised crossroads from four arms. Two leave the
  west depot in the same step and must share the junction tile at some observation, which
  exercises same-direction junction queueing; the other three approach from the remaining arms
  and exercise the conflicting-path and exit-clearance rules. All five must reach their
  destinations. The run reached at least two westbound convoys on the junction tile and
  passed.
- `junction-curve`: two adjacent junctions. A convoy enters the first junction from the south
  and turns east into the second, whose east arm leads into a bend. A second convoy comes north
  through the bend and continues west through both junctions. The first convoy's junction entry
  is checked against the bend, which holds the second convoy; the second convoy's junction
  entry is checked against the first junction, which holds the first convoy. Both must reach
  their destinations.
- `queue`: two convoys leave one depot in the same step and follow one another on a straight
  road. The following vehicle must stay behind the leading vehicle's rear whenever both are
  on one tile and when the follower is on the tile immediately behind the leader's tile, and
  both must reach the destination.
- `stop-release`: a holder loads at a through stop and departs; a second vehicle then starts,
  reaches the stop, loads and holds the released stand. The saved stand mask must be non-zero
  so that the case proves the stand was reused, not merely that the second vehicle passed.
- `reverse-through`: a vehicle loads at a through stop whose schedule entry has its `reverse`
  flag set. It must hold its stand while loading, keep a non-zero stand mask and report
  `pulled_over` while it waits at the stop for the reversal, and then leave the stop in the
  opposite direction. The case fails
  if the stand is released when the next route is prepared while the vehicle is still on the
  stop tile.
- `reverse-through-westbound`: the same checks with a westbound approach, where the vehicle
  stops at the middle of the halt tile rather than at its end.
- `terminal-stands`: four convoys occupy a terminal while a fifth waits; the number of held
  stands never exceeds four.
- `terminal-turnover`: four convoys hold the terminal's four stands, one departs, and the
  waiting fifth convoy then takes a stand. At most four stands are held at once and five
  distinct convoys use the terminal.
- `through-pass`: a vehicle waiting for a full load at a through stop holds the stop while a
  second convoy passes through the same tile in position 0.
- `through-pass-oncoming`: an eastbound holder waits for a full load at the through stop and
  stays pulled over; ten westbound convoys form a stream through the stop tile while a second
  eastbound convoy crosses it. The case asserts that the holder is loading and pulled over,
  that a westbound convoy shares the stop tile with the passer, that the passer's `overtaking`
  state is false throughout the crossing (it stays out of the opposite carriageway), that at
  least three distinct westbound convoys use the stop tile, and that the passer reaches the
  east terminal. On the fixture run eight distinct westbound convoys used the stop tile.

The harness selects a real terminal by filtering station descriptors with
`building_desc_x.is_terminus()` (the descriptor's `get_all_layouts() == 4` marker) instead of
accepting any station; a through stop can otherwise be built on a dead end. It counts stands
with the `stand_mask` snapshot field rather than tile occupancy.

`tests/pending-runtime/road-reload/scenario.nut` adds `save-reload-stand`, which is not in the
`road` group because it uses the reload runner. The vehicle holds a stand, the runner joins a
loopback client so that the server saves and reloads its state, and `resume_game()` checks that
the restored vehicle is still at the stop, holds the same `stand_mask` and reports
`pulled_over`. The vehicle then departs and reaches the terminal; the preserved join save is
loaded offline and repeats the checks. Run it with:

```
scripts\run-pending-runtime-reload-tests.ps1 -CaseName save-reload-stand
  -ScenarioName pending-runtime-road-save-reload-stand
  -Selector tests\pending-runtime\road-reload\scenario.nut
  -Harness tests\pending-runtime\road-harness.nut -HarnessName road-harness
  -Exe simutrans\Simutrans-Extended-Debug.exe
```

The fifteen road cases pass with the Debug x64 build and `-nomidi -nosound -fast_forward`
(45 s with eight jobs in the 2026-11-25 run). The cases do not directly create a one-way dead end,
so the one-way dead-end exception has no dedicated scripted case. The standard Squirrel suite
passes (82 of 82). The network smoke test passes with 1049 per-step private-car route hashes
identical between two runs. The `division`, `pair`, `choose` and `ownership` pending-runtime
groups pass (37 cases). `save-reload-stand` passes on the server after the join and in the fresh
offline load.

The reaction distance also has a compile-time test: the two `static_assert`s on
`road_space::reaction_gap_mm` in `vehicle/road_vehicle.cc`.

There is no scripted case for private car occupancy or the information-window links; see
[private-cars](private-cars.md). There is also no scripted case that creates and drives a
private car for the following-speed rule, so the per-step stopped-car evaluation and
`private_car_t::accelerate` have no runtime assertion. The network smoke test
(`scripts/run-smoke-tests.sh` with `tests/demo.sve`) exercises city traffic, including the
per-step private-car route hash, and the standard automated suite passes.

Run all game/binary tests with `-nomidi`. Use `scripts/run-perf-suite.ps1` for a performance
comparison; the suite needs a Profile build and a recorded baseline and was not run for this
change.

## Known limitations

- A private car's acceleration uses the existing per-tile increment `max_speed >> 2`, applied
  over the number of ticks a tile takes at the car's top speed by `private_car_t::accelerate`.
  It is not the convoy physics; the safe speed limits the result.
- A private car stopped at the end of its tile makes one entry attempt when the gap ahead opens.
  If a sign, a crossing or a vehicle in another lane refuses entry, later attempts wait for the
  1024-tick gate in `private_car_t::sync_step` rather than one per step.
- An overtake that ends while the overtaking vehicle is still alongside the overtaken vehicle
  relies on the existing `other_lane_blocked` check in `road_vehicle_t::enter_tile` to keep it
  in the passing lane. The harness's overlap assertion skips vehicles on the passing lane, so
  it does not verify the lateral separation during that phase.
- The following scan covers a vehicle whose front is on the follower's own tile or on the
  immediately following tile. A vehicle longer than one tile whose front is two or more
  tiles ahead can still have its rear on the follower's tile without being seen. Road
  vehicle sprites are normally much shorter than a tile, so this needs a pakset descriptor
  with an unusually large `length`.
- The leader scan is one object-list scan per moving road user per tile crossing, plus a
  second scan of the next tile. The performance suite has not been run; if profiling shows
  the scan matters, a transient per-tile leader cache is the next step. The junction
  exit-clearance scan and the `road_space::can_follow` calls in
  `vehicle_base_t::get_blocking_vehicle` also run at tile entries rather than every step, and
  have not been measured separately.
- The junction exit-clearance rule checks only the tile immediately after the junction. Two
  adjacent junctions can still gridlock if the exit tile of the first is the second junction
  and that second junction's own exit is blocked. The rule is exact only when the junction's
  exit tile is not itself a junction; the adjacent-junction lookahead is an open question in
  [road-vehicle-operations](../road-vehicle-operations.md#open-questions).
- A vehicle which is already inside a junction can still be blocked by the exit tile filling
  after it entered, for example by a vehicle entering the exit tile from another arm. It
  remains subject to the ordinary following rule and cannot overlap the queue, but it may
  block cross traffic until the exit queue moves. The entry check requires room for the
  vehicle's length plus the following gap, so a leader that was already ahead at entry does not
  hold it inside the junction.

## Not implemented

- The motorway four-lateral-position lane choice.
- Pulled-over waiting when no stand is free.
