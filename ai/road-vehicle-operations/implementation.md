---
status: draft
verified: ex-15 @ 2ec587aa34; implementation on branch road-vehicle-operations @ ef449777d1
---
# Road vehicle operations: implementation

**Covers:** the implemented part of [road-vehicle-operations](../road-vehicle-operations.md): the
`road_space` following rules, the four stop stands, the stand mask save format, the measured
object sizes and the tests. Private car occupancy and information links are in
[private-cars](private-cars.md).

Read this when checking what the road-space and stop-stand code does, its object sizes, its
tests or its remaining gaps. The design, current behaviour and resource audit are in
[road-vehicle-operations](../road-vehicle-operations.md).

The first two work packages of the parent document are implemented on branch
`road-vehicle-operations`: road space and queueing, and stop stands with terminal capacity.
The motorway lane package and pulled-over waiting are not implemented.

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
- `road_space::following_limit` scans the tile's object list for the nearest road vehicle
  ahead in the same travel direction and lateral position, and returns its rear boundary
  (`steps` minus its length in vehicle steps) minus the gap. A vehicle of the same convoy, a
  vehicle in another lateral position and an oncoming vehicle are ignored.
- `road_space::can_follow` is the entry test: the follower's front enters at step 0, so the
  leader's rear boundary must be more than one gap ahead.
- `road_space::drive_until_limit` performs one base `vehicle_base_t::do_drive` call for at
  most one tile boundary. `road_vehicle_t::do_drive` and the new `private_car_t::do_drive`
  repeat it after each hop and recompute the leader, so a vehicle cannot pass the leader while
  the hop carries it forward. A non-leading vehicle of a convoy is moved by the distance that
  the leading vehicle reports, as before.

The lateral position of a road vehicle is the existing traffic or passing lane
(`convoi_t::is_overtaking`, `private_car_t::is_overtaking`) plus the pulled-over state.
`road_vehicle_t::is_pulled_over` is true when the leading vehicle holds a stand bit and the
convoy is loading (`convoi_t::is_loading`) or laid over (`convoi_t::LAYOVER`), except at a
terminal stand 0, which is the normal travel position. A vehicle that is pulled over does not
block traffic in position 0: both `road_vehicle_t::apply_following_rule` and
`private_car_t::apply_following_rule` clear such a vehicle as a blocker.

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
same function records the halt in `target_halt` when the reservation succeeds, so that
`road_vehicle_t::release_target_reservations` frees the stand on departure; without that,
ordinary road convoys left the reservation behind and a through stop lost its stand for the
rest of the game. `get_empty_lane` ignores the reserving vehicle itself and drops a stand that
another vehicle occupies in the same lateral position.

A vehicle that finds no free stand waits in the travel lane or at its previous tile;
pulled-over waiting is not implemented. A terminal stand 0 is the normal travel position and
does not pull the vehicle over.

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
bytes per halt tile on x64.

## Tests

`tests/pending-runtime/road-harness.nut` and `tests/pending-runtime/road-cases/` provide the
`road` group, run with `PENDING_RUNTIME_GROUPS=road` (or by case name). It contains ten cases:

- `single-run`: one convoy crosses a straight road to its destination without a false block or
  stall.
- `moving-gap`: a fast follower catches a slow leader near the end of the route, where road
  overtaking is refused. While both fronts are on one tile and both are moving, the gap in
  vehicle steps must be at least the standstill gap plus the 1.5 s reaction distance at the
  follower's speed, converted at the fixture's 125 m per tile, less a small observation
  tolerance. At 50 km/h the bound is about 45 vehicle steps; the earlier 1000x scaling error
  produced a gap of a few steps and fails it. The case also requires both convoys to reach the
  destination.
- `overtake`: the fastest and slowest available road vehicle descriptors are selected in one
  pass from the pakset; the fast vehicle starts second and must arrive first, which can only
  happen if it overtook the slow vehicle without a deadlock or head-on conflict.
- `junction`: two convoys leave separate depots on an east-west trunk and a north branch and
  cross a T junction from different arms; both must reach the trunk-end terminal.
- `queue`: two convoys leave one depot in the same step and follow one another on a straight
  road. The following vehicle must stay behind the leading vehicle's rear whenever both are on
  one tile, and both must reach the destination.
- `stop-release`: a holder loads at a through stop and departs; a second vehicle then starts,
  reaches the stop, loads and holds the released stand. The saved stand mask must be non-zero
  so that the case proves the stand was reused, not merely that the second vehicle passed.
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

Results on 2026-11-22 with the Debug x64 build and `-nomidi -nosound -fast_forward`: all ten
road cases passed (about 50 s with six jobs); the standard Squirrel suite passed (81 of 81);
the `division`, `pair`, `choose` and `ownership` pending-runtime groups passed (37 cases); and
`save-reload-stand` passed on the server after the join (READY after ~36 s, client joined
after ~16 s, PASS after ~98 s, 10,448 checklist pairs with no mismatch) and in the fresh
offline load (RESTORED after ~16 s, PASS).

The reaction distance also has a compile-time test: the two `static_assert`s on
`road_space::reaction_gap_mm` in `vehicle/road_vehicle.cc`.

There is no scripted case for private car occupancy or the information-window links; see
[private-cars](private-cars.md).

Run all game/binary tests with `-nomidi`. Use `scripts/run-perf-suite.ps1` for a performance
comparison; the suite needs a Profile build and a recorded baseline and was not run for this
change.

## Known limitations

- The following scan sees only vehicles whose front is on the same tile. A long
  leader whose front has crossed into the next tile can still have its rear on the
  follower's tile; the entry check prevents the follower entering the leader's tile, but a
  small visual overlap across the tile boundary is possible.
- The leader scan is one object-list scan per moving road user per tile crossing. The
  performance suite has not been run; if profiling shows the scan matters, a transient
  per-tile leader cache is the next step.

## Not implemented

- The motorway four-lateral-position lane choice.
- Pulled-over waiting when no stand is free.
