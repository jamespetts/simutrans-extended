---
status: draft
verified: ex-15 @ b2bfc8d plus working-tree lateral rendering, reversal stand and following-traffic changes
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
queueing, and stop stands with terminal capacity. The motorway lane package and pulled-over
waiting are not implemented.

## Road space and queueing

`vehicle/road_vehicle.cc` holds the shared rules in the namespace `road_space`:

- `road_space::allows_queueing` accepts a road tile whose unmasked ribi is straight
  (`ribi_t::is_straight`) and which is neither an intersection nor a crossing
  (`strasse_t::is_crossing`). Corners, T junctions, crossroads and diagonal tiles keep the
  one-vehicle-per-lane rule.
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
  only road users in the same lateral position and travelling in the same direction. A
  vehicle whose front is on the same tile constrains the follower whenever that front is
  ahead of the follower's front; the limit is the vehicle's rear boundary (its front minus
  its length) less the gap. A vehicle which is already overlapping the follower is
  therefore still a blocker: the follower waits instead of driving through it. The limit
  is clamped to zero because `-1` means "no constraint"; a negative value would otherwise
  let the follower drive to the end of its tile when the gap grows, as it does when a
  stopped queue starts moving and the reaction distance replaces the standstill gap.
- `following_limit` also looks ahead into the tile returned by
  `vehicle_base_t::get_pos_next`. A vehicle whose front has crossed into the next tile
  still has its rear on the follower's tile while its front is shorter than its length,
  and it constrains the follower in the same way. A vehicle on the next tile is considered
  only if its recorded direction shows that it entered through the shared tile edge, so
  that both vehicles' step coordinates start at that edge.
  `road_space::step_scale` returns the length of one step on a tile in 512ths of a
  straight step; a diagonal tile's step covers
  `vehicle_base_t::get_diagonal_multiplier()` 512ths.
- `road_space::get_user_state` gives a leading convoy vehicle the whole convoy length,
  because the convoy's rear is that vehicle's front minus the convoy length. A trailing
  vehicle gives its own length instead, which is the rear of the part of the convoy on its
  tile. Each vehicle of a convoy is therefore a rear boundary of the convoy's body, not a
  separate road user with the full convoy length.
- `road_space::can_follow` is the entry test: the follower's front enters at step 0, so an
  accepted leader's rear boundary must be more than one gap ahead. Only a private car or a
  convoy's leading vehicle can clear a blocker; a trailing convoy vehicle's own length does
  not prove that the convoy's rear is clear.
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

On the built `Debug|x64` MSVC configuration, `sizeof(road_vehicle_t)` is 720 bytes,
`sizeof(private_car_t)` 224 and `sizeof(haltestelle_t::tile_t)` 24. On the default
(non-`_DEBUG`) configuration the vehicle classes are 712 and 216, and the halt record is still
24. Removing the stand mask from `road_vehicle_t` left its size unchanged in both
configurations, so the four-bit mask costs no object memory; it is packed with the existing
`is_checker` bit. `convoihandle_t` holds a `uint16`: the old record used 8 bytes for the
pointer and 4 bytes for two reservations, 12 bytes padded to 16. Four reservations use 16
bytes, which still fits that padding, but the added one-byte `stands` count makes the record
17 bytes, which the 8-byte alignment pads to 24. `haltestelle_t::tile_t` therefore grows by 8
bytes per halt tile on x64. The rendering-only lateral blends add up to 16 bytes to
`road_vehicle_t` and up to 12 bytes to `private_car_t`; the size probe has not been rerun,
so the vehicle figures above predate those fields.

## Tests

`tests/pending-runtime/road-harness.nut` and `tests/pending-runtime/road-cases/` provide the
`road` group, run with `PENDING_RUNTIME_GROUPS=road` (or by case name). It contains twelve cases:

- `single-run`: one convoy crosses a straight road to its destination without a false block or
  stall.
- `moving-gap`: a fast follower catches a slow leader near the end of the route, where road
  overtaking is refused. While both fronts are on one tile, the gap in
  vehicle steps must be at least the standstill gap plus the 1.5 s reaction distance at the
  follower's speed, converted at the fixture's 125 m per tile, less a small observation
  tolerance. At 50 km/h the bound is about 45 vehicle steps; the earlier 1000x scaling error
  produced a gap of a few steps and fails it. While the follower is on the tile immediately
  behind the leader's tile, the follower's front must not pass the leader's rear boundary
  expressed in the follower's tile coordinates. The case also requires both convoys to reach
  the destination.
- `overtake`: the fastest and slowest available road vehicle descriptors are selected in one
  pass from the pakset; the fast vehicle starts second and must arrive first, which can only
  happen if it overtook the slow vehicle without a deadlock or head-on conflict.
- `junction`: two convoys leave separate depots on an east-west trunk and a north branch and
  cross a T junction from different arms; both must reach the trunk-end terminal.
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

The twelve road cases pass with the Debug x64 build and `-nomidi -nosound -fast_forward`
(about 44 s with six jobs). The standard Squirrel suite passes (81 of 81). The `division`,
`pair`, `choose` and `ownership` pending-runtime groups pass (37 cases). `save-reload-stand`
passes on the server after the join and in the fresh offline load.

The reaction distance also has a compile-time test: the two `static_assert`s on
`road_space::reaction_gap_mm` in `vehicle/road_vehicle.cc`.

There is no scripted case for private car occupancy or the information-window links; see
[private-cars](private-cars.md).

Run all game/binary tests with `-nomidi`. Use `scripts/run-perf-suite.ps1` for a performance
comparison; the suite needs a Profile build and a recorded baseline and was not run for this
change.

## Known limitations

- The following scan covers a vehicle whose front is on the follower's own tile or on the
  immediately following tile. A vehicle longer than one tile whose front is two or more
  tiles ahead can still have its rear on the follower's tile without being seen. Road
  vehicle sprites are normally much shorter than a tile, so this needs a pakset descriptor
  with an unusually large `length`.
- The leader scan is one object-list scan per moving road user per tile crossing, plus a
  second scan of the next tile. The performance suite has not been run; if profiling shows
  the scan matters, a transient per-tile leader cache is the next step.

## Not implemented

- The motorway four-lateral-position lane choice.
- Pulled-over waiting when no stand is free.
