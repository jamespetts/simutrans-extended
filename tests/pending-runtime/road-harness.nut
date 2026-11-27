//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Road-vehicle operations runtime cases: same-tile following (queue), a vehicle
// passing a loading vehicle at a through stop, and terminal stand capacity.
//
// A case selects itself by setting ::case_kind before including this file. The
// runner scripts/run-pending-runtime-tests.sh stages the case's scenario.nut,
// this file and empty-16x16.sve in the pakset scenario directory and runs one
// game process per case, so every case starts from the empty world.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Road vehicle operations runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

local DEPOT_POS = coord3d(1, 2, 0)
local ROAD_END = coord3d(12, 2, 0)
local MID_STOP = coord3d(10, 2, 0)
local STOP_POS = coord3d(5, 2, 0)
local TERMINAL_ROAD_END = coord3d(7, 2, 0)
local REVERSE_WEST_STOP = coord3d(3, 2, 0)
local REVERSE_EAST_STOP = coord3d(7, 2, 0)
local REVERSE_EAST_DEPOT = coord3d(12, 2, 0)

// The moving-gap case uses a short road so that the follower meets the leader
// near the end of its route, where road overtaking is refused.
local GAP_ROAD_END = coord3d(14, 2, 0)
local GAP_MID_STOP = coord3d(12, 2, 0)

// The overtake case needs a long straight so that the faster vehicle catches
// the slower one well before the end of the route.
local LONG_ROAD_END = coord3d(14, 2, 0)
local LONG_MID_STOP = coord3d(12, 2, 0)

// The junction case uses an east-west trunk at y = 5 and a north branch at x = 7.
local JUNCTION_TRUNK_END = coord3d(14, 5, 0)
local JUNCTION_POS = coord3d(7, 5, 0)
local JUNCTION_BRANCH_END = coord3d(7, 1, 0)
local JUNCTION_DEPOT_POS = coord3d(1, 5, 0)
local JUNCTION_SECOND_STOP = coord3d(3, 5, 0)

// The crossroads case uses an east-west road at y = 5 and a north-south road at
// x = 7. Each arm ends in a depot, with a return stop inside it.
local CROSSROADS_POS = coord3d(7, 5, 0)
local CROSSROADS_WEST_END = coord3d(1, 5, 0)
local CROSSROADS_EAST_END = coord3d(14, 5, 0)
local CROSSROADS_NORTH_END = coord3d(7, 1, 0)
local CROSSROADS_SOUTH_END = coord3d(7, 9, 0)
local CROSSROADS_WEST_DEPOT = coord3d(1, 5, 0)
local CROSSROADS_EAST_DEPOT = coord3d(14, 5, 0)
local CROSSROADS_NORTH_DEPOT = coord3d(7, 1, 0)
local CROSSROADS_SOUTH_DEPOT = coord3d(7, 9, 0)
local CROSSROADS_WEST_STOP = coord3d(3, 5, 0)
local CROSSROADS_EAST_STOP = coord3d(12, 5, 0)
local CROSSROADS_NORTH_STOP = coord3d(7, 3, 0)
local CROSSROADS_SOUTH_STOP = coord3d(7, 7, 0)

// The junction-curve case has two adjacent junctions. Junction A at (5,5) is
// entered from the south by the first convoy, which turns east towards
// junction B at (6,5). B's east arm leads into a bend at (7,5) whose other arm
// runs south. The second convoy starts south of the bend and runs north
// through it, then west through B and A. The approaches to A and to the bend
// are both four tiles, so the two convoys reach the junction entries at about
// the same time and each one's exit tile then holds the other one.
local CURVE_A = coord3d(5, 5, 0)
local CURVE_B = coord3d(6, 5, 0)
local CURVE_BEND = coord3d(7, 5, 0)
local CURVE_WEST_END = coord3d(2, 5, 0)
local CURVE_B_NORTH_END = coord3d(6, 3, 0)
local CURVE_A_SOUTH_DEPOT = coord3d(5, 9, 0)
local CURVE_C_SOUTH_DEPOT = coord3d(7, 9, 0)
local CURVE_SOUTH_STOP = coord3d(7, 7, 0)

// The through-pass-oncoming case uses a two-way road between two depots, with
// a through stop in the middle, a second stop west of it for the westbound
// stream's first entry, and a terminal on a branch at each end.
local ONCOMING_ROAD_WEST = coord3d(1, 2, 0)
local ONCOMING_ROAD_EAST = coord3d(14, 2, 0)
local ONCOMING_STOP = coord3d(5, 2, 0)
local ONCOMING_WEST_STOP = coord3d(2, 2, 0)
local ONCOMING_EAST_BRANCH = coord3d(12, 2, 0)
local ONCOMING_EAST_TERMINAL = coord3d(12, 0, 0)
local ONCOMING_WEST_BRANCH = coord3d(3, 2, 0)
local ONCOMING_WEST_TERMINAL = coord3d(3, 0, 0)
local ONCOMING_STREAM_CONVOYS = 10

// Vehicle steps per carunit and per tile, from simunits.h. A vehicle
// descriptor's length is in carunits; a tile boundary is at the tile's step
// count, which is 256 on a straight tile.
local VEHICLE_STEPS_PER_CARUNIT = 16
local VEHICLE_STEPS_PER_TILE = 256

// The fixture pakset, pak128.Britain-Ex, sets meters_per_tile to 125. The
// moving-gap lower bound is expressed at that scale; the road group is run
// against that pakset only.
local FIXTURE_METERS_PER_TILE = 125

local heartbeat_steps = 0

local fixture = {
	phase = "waiting-for-setup",
	setup_started = false,
	completed = false,
	depot = null,
	second_depot = null,
	convoys = [],
	started = [],
	observations = 0,
	saw_same_tile = false,
	max_on_stop_tile = 0,
	arrived_at_terminal = {},
	second_started = false,
	leading_at_end = false,
	following_at_end = false,
	// moving-gap
	saw_moving_pair = false,
	min_gap_seen = -1,
	min_gap_speed = 0,
	same_tile_observations = 0,
	// overtake
	slow_arrival = -1,
	fast_arrival = -1,
	// stop-release and terminal-turnover
	holder_cleared = false,
	// reverse-through
	reversal_seen = false,
	reverse_westbound = false,
	stand_taken = false,
	holder_released_seen = false,
	fifth_started = false,
	holder_seen = false,
	four_seen = false,
	distinct_on_terminal = {},
	// through-pass-oncoming
	stream = [],
	holder_ready = false,
	stream_started = false,
	passer_started = false,
	cross_seen = false,
	holder_held = false,
	distinct_oncoming = {},
	// crossroads
	crossroads_destinations = [],
	crossroads_west_ids = [],
	crossroads_arrived = {},
	crossroads_max_on_junction = 0,
	crossroads_max_west_on_junction = 0,
	// junction-curve
	curve_destinations = [],
	curve_arrived = {},
	// trigger-wait
	trigger_seen = false,
	trigger_steps = 0,
	trigger_arrival_ticks = 0,
	trigger_wait_ticks = 0,
	trigger_left = false
}


function fail(what)
{
	if (!fixture.completed)
	{
		fixture.completed = true
		print("RUNTIME CASE " + case_name + " FAIL: " + what)
		print("RUNTIME CASE " + case_name + " COMPLETE")
	}
	fixture.phase = "failed"
	return false
}


function pass()
{
	if (!fixture.completed)
	{
		fixture.completed = true
		print("RUNTIME CASE " + case_name + " PASS")
		print("RUNTIME CASE " + case_name + " COMPLETE")
	}
	fixture.phase = "done"
	return true
}


function check(ok, what)
{
	if (!ok)
	{
		return fail(what)
	}
	return true
}


function check_tool(result, what)
{
	if (result == null || result == true)
	{
		return true
	}
	return fail(what + ": " + result)
}


function pos_text(pos)
{
	return "(" + pos.x + "," + pos.y + "," + pos.z + ")"
}


function same_pos(a, b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z
}


function on_depot_tile(pos)
{
	foreach (d in depot_x.get_depot_list(player_x(0), wt_road))
	{
		if (same_pos(d.get_pos(), pos))
		{
			return true
		}
	}
	return false
}


function convoy_ids()
{
	local ids = []
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid())
		{
			ids.append(cnv.id)
		}
	}
	return ids
}


function find_new_convoy(before)
{
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid() && before.find(cnv.id) == null)
		{
			return cnv
		}
	}
	return null
}


function depot_desc_for(waytype)
{
	local list = building_desc_x.get_building_list(building_desc_x.depot)
	foreach (building in list)
	{
		if (building.get_type() == building_desc_x.depot && building.get_waytype() == waytype && building.is_available(world.get_time()))
		{
			return building
		}
	}
	return null
}


function make_entry(pos, unique_entry_id, load)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = unique_entry_id
	entry.load = load
	return entry
}


// An entry with a maximum waiting time. The shift selects the wait as
// ticks_per_world_month >> (16 - shift); a larger shift waits longer.
function make_entry_wait(pos, unique_entry_id, load, wait_shift)
{
	local entry = make_entry(pos, unique_entry_id, load)
	entry.wait = wait_shift
	return entry
}


function road_vehicle_desc()
{
	local list = vehicle_desc_x.get_available_vehicles(wt_road)
	if (list.len() == 0)
	{
		return null
	}
	return list[0]
}


// The slowest and fastest road vehicle descriptors available in the current
// era, as [slow, fast]. A single pass is used because the script VM limits the
// work per call.
function slowest_and_fastest_descriptors()
{
	local list = vehicle_desc_x.get_available_vehicles(wt_road)
	if (list.len() == 0)
	{
		return null
	}
	local slow = list[0]
	local fast = list[0]
	foreach (d in list)
	{
		if (d.get_topspeed() < slow.get_topspeed())
		{
			slow = d
		}
		if (d.get_topspeed() > fast.get_topspeed())
		{
			fast = d
		}
	}
	return [slow, fast]
}


// The number of vehicle steps that the following rule keeps clear behind a
// moving leader for a follower at @p kmh. This mirrors road_space::following_gap:
// a one-metre standstill gap plus a 1.5 s reaction distance, converted from
// millimetres with the fixture's metres per tile and rounded up.
function moving_gap_steps(kmh)
{
	local millimetres = 1000 + (kmh * 1500 * 1000) / 3600
	local millimetres_per_tile = FIXTURE_METERS_PER_TILE * 1000
	return (millimetres * 256 + millimetres_per_tile - 1) / millimetres_per_tile
}


// Builds any passenger road stop at pos, trying the available station
// descriptors and rotations. Returns true on success.
function build_any_stop(pl, pos)
{
	local stops = building_desc_x.get_available_stations(building_desc_x.station, wt_road, good_desc_x.passenger)
	if (stops.len() == 0)
	{
		return false
	}
	foreach (stop in stops)
	{
		foreach (rotation in [0, 2, 8, 1, 4])
		{
			if (command_x.build_station(pl, pos, stop, rotation) == null)
			{
				return true
			}
		}
	}
	return false
}


// Builds a stop at pos that accepts traffic in both directions (not a
// terminal). A terminal only fits a single-direction road, so it is rejected
// by building on a straight road tile that has traffic in both directions;
// this helper is used where the test needs a through stop.
function build_through_stop(pl, pos)
{
	return build_any_stop(pl, pos)
}


// Builds a terminal (a road stop building with four layouts, and therefore
// four stands) at pos. The building descriptor's layout count is exposed as
// is_terminus(), so the correct descriptor is selected rather than assumed.
function build_terminal(pl, pos)
{
	local stops = building_desc_x.get_available_stations(building_desc_x.station, wt_road, good_desc_x.passenger)
	foreach (stop in stops)
	{
		if (!stop.is_terminus())
		{
			continue
		}
		foreach (rotation in [0, 2, 8, 1, 4])
		{
			if (command_x.build_station(pl, pos, stop, rotation) == null)
			{
				return true
			}
		}
	}
	return false
}


// Creates one convoy in @p depot with one vehicle and the given schedule.
// Returns the convoy, or null after reporting a failure.
function make_convoy_at(depot, pl, desc, entries, what)
{
	local before = convoy_ids()
	if (!check_tool(depot.append_vehicle(pl, convoy_x(0), desc), "add the vehicle of " + what))
	{
		return null
	}
	local cnv = find_new_convoy(before)
	if (!check(cnv != null, "the " + what + " convoy was not found after adding its vehicle"))
	{
		return null
	}
	local schedule = schedule_x(wt_road, entries)
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the schedule of " + what))
	{
		return null
	}
	return cnv
}


function make_convoy(pl, desc, entries, what)
{
	return make_convoy_at(fixture.depot, pl, desc, entries, what)
}


function front_slot(cnv)
{
	if (!cnv.is_valid())
	{
		return null
	}
	local slots = cnv.get_vehicle_snapshot()
	if (slots.len() == 0)
	{
		return null
	}
	return slots[0]
}


function length_steps(slot)
{
	return slot.desc.get_length() * VEHICLE_STEPS_PER_CARUNIT
}


// convoi_t::REVERSING in simconvoi.h. The overlap check below compares step
// positions on the assumption that both convoys face the same way; a convoy
// reversing at a dead end does not.
local CONVOI_STATE_REVERSING = 17


// Asserts that a follower does not overlap the vehicle in front: a vehicle
// whose front is behind another vehicle's front must stay behind that vehicle's
// rear, both when the two fronts share a tile and when the follower is on the
// tile immediately behind the leader's tile. Vehicles on the passing lane are
// skipped: their step coordinates describe the other lane.
function check_no_overlap(leader, follower)
{
	if (leader.is_loading() || follower.is_loading() || leader.is_in_depot() || follower.is_in_depot())
	{
		// A vehicle loading at a stand is in another lateral position, and a
		// convoy in a depot is not on the road at all.
		return true
	}
	local a = front_slot(leader)
	local b = front_slot(follower)
	if (a == null || b == null || on_depot_tile(a.pos) || on_depot_tile(b.pos))
	{
		return true
	}
	if (a.reversed || b.reversed)
	{
		// A convoy that has reversed at a dead end faces the other way. The
		// step ordering below assumes that both vehicles face the same way, so
		// it cannot compare their bodies after a reversal.
		return true
	}
	if (leader.get_state() == CONVOI_STATE_REVERSING || follower.get_state() == CONVOI_STATE_REVERSING
		|| leader.is_reverse_schedule() || follower.is_reverse_schedule())
	{
		// The convoys have reversed at the route end. The former follower is
		// now the leading vehicle in the return direction, so the step
		// ordering below no longer describes this pair.
		return true
	}
	if (a.pulled_over && !b.pulled_over)
	{
		// The leader is beside the travel lane.
		return true
	}
	if (a.overtaking || b.overtaking)
	{
		// The two vehicles are in different lanes. Their step coordinates are
		// not comparable while either is on the passing lane.
		return true
	}
	// The queue and moving-gap fixtures run east, so a follower one tile west of
	// the leader is at the tile boundary behind it. The leader's rear, in the
	// follower's tile coordinates, is the tile length plus its steps, less its
	// length. The follower's front must not pass that rear.
	if (a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.pos.x == b.pos.x + 1)
	{
		local rear = VEHICLE_STEPS_PER_TILE + a.steps - length_steps(a)
		if (!check(b.steps <= rear,
			"vehicle of convoy " + follower.id + " at steps " + b.steps
			+ " on " + pos_text(b.pos) + " overlaps the rear (" + rear
			+ ") of the vehicle of convoy " + leader.id + " on the next tile "
			+ pos_text(a.pos)))
		{
			return false
		}
		return true
	}
	if (!same_pos(a.pos, b.pos))
	{
		return true
	}
	if (b.steps > a.steps)
	{
		// The follower is level with or ahead of the leader; only possible
		// while overtaking.
		return true
	}
	local rear = a.steps - length_steps(a)
	if (!check(b.steps <= rear,
		"vehicle of convoy " + follower.id + " at steps " + b.steps
		+ " (state " + follower.get_state() + ")"
		+ " overlaps the rear (" + rear + ") of the vehicle of convoy " + leader.id
		+ " (state " + leader.get_state() + ")"
		+ " on " + pos_text(a.pos)))
	{
		return false
	}
	return true
}


function setup_queue(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	// The destination must accept two convoys in the same direction, so it is a
	// terminal with four stands rather than a through stop with one stand on
	// each side. A second halt gives the schedule two entries.
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, MID_STOP), "build the mid-road stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	local a = make_convoy(pl, desc, [make_entry(ROAD_END, 11, 0), make_entry(MID_STOP, 12, 0)], "leading")
	local b = make_convoy(pl, desc, [make_entry(ROAD_END, 13, 0), make_entry(MID_STOP, 14, 0)], "following")
	if (a == null || b == null)
	{
		return false
	}
	fixture.convoys = [a, b]
	// Start both in the same step: the following convoy has to enter the
	// depot exit tile while the leading convoy is still on it.
	if (!check_tool(fixture.depot.start_convoy(pl, a), "start the leading convoy"))
	{
		return false
	}
	if (!check_tool(fixture.depot.start_convoy(pl, b), "start the following convoy"))
	{
		return false
	}
	fixture.started = [true, true]
	return true
}


function observe_queue()
{
	if (!check_no_overlap(fixture.convoys[0], fixture.convoys[1]))
	{
		return
	}
	local a = front_slot(fixture.convoys[0])
	local b = front_slot(fixture.convoys[1])
	if (a == null || b == null)
	{
		return
	}
	if ((fixture.observations % 100) == 0)
	{
		print("RUNTIME CASE " + case_name + ": leading at " + pos_text(a.pos) + " steps " + a.steps
			+ " state " + fixture.convoys[0].get_state() + "; following at " + pos_text(b.pos)
			+ " steps " + b.steps + " state " + fixture.convoys[1].get_state())
	}
	if (same_pos(a.pos, b.pos) && !same_pos(a.pos, DEPOT_POS))
	{
		fixture.saw_same_tile = true
	}
	if (same_pos(a.pos, ROAD_END))
	{
		fixture.leading_at_end = true
	}
	if (same_pos(b.pos, ROAD_END))
	{
		fixture.following_at_end = true
	}
	if (fixture.saw_same_tile && fixture.leading_at_end && fixture.following_at_end)
	{
		print("RUNTIME CASE " + case_name + ": both convoys reached " + pos_text(ROAD_END)
			+ "; following gap held on a shared tile")
		pass()
	}
}


function setup_single_run(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, MID_STOP), "build the mid-road stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	local cnv = make_convoy(pl, desc, [make_entry(ROAD_END, 71, 0), make_entry(MID_STOP, 72, 0)], "single-run")
	if (cnv == null)
	{
		return false
	}
	fixture.convoys = [cnv]
	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the single-run convoy"))
	{
		return false
	}
	return true
}


function observe_single_run()
{
	local cnv = fixture.convoys[0]
	local slot = front_slot(cnv)
	if (slot == null)
	{
		return
	}
	if ((fixture.observations % 100) == 0)
	{
		print("RUNTIME CASE " + case_name + ": convoy at " + pos_text(slot.pos) + " steps " + slot.steps
			+ " speed " + cnv.get_speed() + " state " + cnv.get_state())
	}
	if (same_pos(slot.pos, ROAD_END))
	{
		print("RUNTIME CASE " + case_name + ": convoy reached " + pos_text(ROAD_END))
		pass()
	}
}


// A convoy waits at a stop for a departure trigger that is never sent. The
// receiver bit is set and no broadcaster sends it, so the convoy must remain in
// AWAITING_TRIGGER. Before the fix for GitHub issue 701, the first
// convoi_t::sync_step() after arrival aborted with "Wrong state 27".
// The trigger-timeout case uses the same fixture with a maximum waiting time:
// it must leave AWAITING_TRIGGER once that wait has expired and continue to the
// road end.
local TRIGGER_TIMEOUT_SHIFT = 8

function setup_trigger_wait(pl)
{
	return setup_trigger_case(pl, 0)
}

function setup_trigger_timeout(pl)
{
	return setup_trigger_case(pl, TRIGGER_TIMEOUT_SHIFT)
}

function setup_trigger_case(pl, wait_shift)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the trigger stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	local stop_entry = make_entry_wait(STOP_POS, 71, 0, wait_shift)
	stop_entry.conditional_depart_before_wait = true
	stop_entry.condition_bitfield_receiver = 1
	local cnv = make_convoy(pl, desc, [stop_entry, make_entry(ROAD_END, 72, 0)], case_name)
	if (cnv == null)
	{
		return false
	}
	fixture.convoys = [cnv]
	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the " + case_name + " convoy"))
	{
		return false
	}
	return true
}


function observe_trigger_wait()
{
	local cnv = fixture.convoys[0]
	if (cnv == null || !cnv.is_valid())
	{
		return
	}
	local state = cnv.get_state()
	if (!fixture.trigger_seen)
	{
		if (state == convoy_state_awaiting_trigger)
		{
			fixture.trigger_seen = true
			fixture.trigger_steps = 0
			print("RUNTIME CASE " + case_name + ": convoy is awaiting a trigger at " + pos_text(cnv.get_pos()))
			return
		}
		local slot = front_slot(cnv)
		if (slot != null && slot.pos.x > STOP_POS.x)
		{
			fail("the convoy passed the trigger stop without entering AWAITING_TRIGGER (state " + state + ")")
		}
		return
	}
	++fixture.trigger_steps
	if (state != convoy_state_awaiting_trigger)
	{
		fail("the convoy left AWAITING_TRIGGER after " + fixture.trigger_steps + " observations (state " + state + ")")
		return
	}
	if (fixture.trigger_steps >= 100)
	{
		print("RUNTIME CASE " + case_name + ": convoy remained in AWAITING_TRIGGER for " + fixture.trigger_steps + " observations")
		pass()
	}
}


// The trigger-timeout case configures a maximum wait and sends no trigger. The
// convoy must stay in AWAITING_TRIGGER until the wait from its arrival has
// expired, then leave and continue to the road end.
function observe_trigger_timeout()
{
	local cnv = fixture.convoys[0]
	if (cnv == null || !cnv.is_valid())
	{
		return
	}
	local state = cnv.get_state()
	local now = world.get_time()

	if (!fixture.trigger_seen)
	{
		if (state == convoy_state_awaiting_trigger)
		{
			fixture.trigger_seen = true
			fixture.trigger_steps = 0
			fixture.trigger_arrival_ticks = cnv.get_arrival_time()
			fixture.trigger_wait_ticks = now.ticks_per_month >> (16 - TRIGGER_TIMEOUT_SHIFT)
			print("RUNTIME CASE " + case_name + ": convoy is awaiting a trigger at " + pos_text(cnv.get_pos())
				+ "; arrival " + fixture.trigger_arrival_ticks + ", configured wait " + fixture.trigger_wait_ticks + " ticks")
			return
		}
		local slot = front_slot(cnv)
		if (slot != null && slot.pos.x > STOP_POS.x)
		{
			fail("the convoy passed the trigger stop without entering AWAITING_TRIGGER (state " + state + ")")
		}
		return
	}

	if (!fixture.trigger_left)
	{
		if (state == convoy_state_awaiting_trigger)
		{
			++fixture.trigger_steps
			if (fixture.trigger_steps > 20000)
			{
				fail("the convoy remained in AWAITING_TRIGGER for " + fixture.trigger_steps + " observations, beyond the configured wait of " + fixture.trigger_wait_ticks + " ticks")
			}
			return
		}
		local elapsed = now.ticks - fixture.trigger_arrival_ticks
		if (!check(elapsed >= fixture.trigger_wait_ticks,
			"the convoy left AWAITING_TRIGGER after " + elapsed + " ticks, before the configured " + fixture.trigger_wait_ticks + " ticks"))
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": convoy left AWAITING_TRIGGER after " + elapsed
			+ " ticks (configured " + fixture.trigger_wait_ticks + ") and is in state " + state)
		fixture.trigger_left = true
		return
	}

	observe_single_run()
}


function setup_moving_gap(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, GAP_ROAD_END, road, true), "build the moving-gap road"))
	{
		return false
	}
	if (!check(build_terminal(pl, GAP_ROAD_END), "build the terminal at the moving-gap road end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, GAP_MID_STOP), "build the moving-gap mid stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local descs = slowest_and_fastest_descriptors()
	if (!check(descs != null, "no road vehicle descriptor is available"))
	{
		return false
	}
	local slow = descs[0]
	local fast = descs[1]
	if (!check(fast.get_topspeed() > slow.get_topspeed(),
		"the fastest road vehicle (" + fast.get_topspeed() + " km/h) is not faster than the slowest ("
		+ slow.get_topspeed() + " km/h)"))
	{
		return false
	}
	local leader = make_convoy(pl, slow, [make_entry(GAP_ROAD_END, 81, 0), make_entry(GAP_MID_STOP, 82, 0)], "slow leader")
	local follower = make_convoy(pl, fast, [make_entry(GAP_ROAD_END, 83, 0), make_entry(GAP_MID_STOP, 84, 0)], "fast follower")
	if (leader == null || follower == null)
	{
		return false
	}
	fixture.convoys = [leader, follower]
	if (!check_tool(fixture.depot.start_convoy(pl, leader), "start the slow leader"))
	{
		return false
	}
	fixture.started = [true, false]
	return true
}


function observe_moving_gap()
{
	local leader = fixture.convoys[0]
	local follower = fixture.convoys[1]
	local a = front_slot(leader)
	local b = front_slot(follower)
	if (a == null || b == null)
	{
		return
	}
	if (!fixture.second_started)
	{
		// Give the leader a head start, then start the fast follower so that it
		// catches the leader near the end of the route, where overtaking is
		// refused.
		if (a.pos.x >= 10 && leader.get_speed() > 0)
		{
			fixture.second_started = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), follower), "start the fast follower"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": leader at " + pos_text(a.pos) + " speed "
				+ leader.get_speed() + "; starting the fast follower")
		}
		return
	}
	// The return leg begins once both convoys have reached the terminal. The
	// overlap and gap assertions describe the eastbound following pair only;
	// after the reversal the former follower is the leading vehicle.
	if (same_pos(a.pos, GAP_ROAD_END))
	{
		fixture.leading_at_end = true
	}
	if (same_pos(b.pos, GAP_ROAD_END))
	{
		fixture.following_at_end = true
	}
	if (!(fixture.leading_at_end && fixture.following_at_end))
	{
		if (!check_no_overlap(leader, follower))
		{
			return
		}
		if (same_pos(a.pos, b.pos) && !same_pos(a.pos, GAP_ROAD_END) && !leader.is_in_depot() && !follower.is_in_depot()
			&& !b.overtaking && leader.get_speed() > 0 && follower.get_speed() > 0
			&& b.steps <= a.steps && a.steps > 0)
		{
			++fixture.same_tile_observations;
			local gap = a.steps - length_steps(a) - b.steps;
			local kmh = follower.get_speed();
			fixture.saw_moving_pair = true;
			// Give the follower a settling period: the first observation on the
			// leader's tile can be before the following-speed rule has had a
			// step to act, or while an overtake attempt is being refused.
			if (fixture.same_tile_observations > 20)
			{
				// The follower may legitimately travel faster than the leader while
				// the gap is large; the rule is about what happens when it closes.
				// Check the speed only when the gap is near the equilibrium gap at
				// the leader's speed, where the follower should have settled.
				local leader_equilibrium = moving_gap_steps(leader.get_speed());
				if (gap < leader_equilibrium * 2)
				{
					// The follower must slow down to the leader's speed rather than
					// accelerate up to its own top speed and be stopped by the position
					// cap at the next tile boundary. The earlier behaviour kept the
					// follower's calculated speed at 50-80 km/h while it was physically
					// held behind a 5 km/h leader.
					if (!check(kmh <= leader.get_speed() + 15,
						"following vehicle at " + kmh + " km/h behind a " + leader.get_speed()
						+ " km/h leader with a gap of " + gap + " steps on " + pos_text(a.pos)
						+ ", more than 15 km/h above the leader's speed"))
					{
						return
					}
				}
				if (kmh <= leader.get_speed() + 15)
				{
					// A follower travelling at the leader's speed must also have
					// settled near the equilibrium gap. A much larger gap is the
					// regression where the follower waits for room for its top speed.
					if (!check(gap <= leader_equilibrium * 2 + 8,
						"following vehicle kept " + gap + " steps behind the leader on "
						+ pos_text(a.pos) + ", more than twice the equilibrium gap of "
						+ leader_equilibrium + " steps at the leader's speed"))
					{
						return
					}
				}
			}
			if (kmh >= 25)
			{
				local required = moving_gap_steps(kmh);
				// The leader moves on between its entry check and this observation,
				// so allow a small tolerance. The earlier 1000x scaling error gave
				// a gap of a few steps at every speed.
				if (!check(gap >= required - 8,
					"moving follower at " + kmh + " km/h kept " + gap + " steps behind the leader's rear on "
					+ pos_text(a.pos) + ", below the reaction bound of " + required + " steps"))
				{
					return
				}
			}
			if (fixture.min_gap_seen < 0 || gap < fixture.min_gap_seen)
			{
				fixture.min_gap_seen = gap
				fixture.min_gap_speed = kmh
			}
		}
		else
		{
			fixture.same_tile_observations = 0
		}
	}
	if ((fixture.observations % 100) == 0)
	{
		print("RUNTIME CASE " + case_name + ": leader at " + pos_text(a.pos) + " steps " + a.steps
			+ " speed " + leader.get_speed() + "; follower at " + pos_text(b.pos) + " steps " + b.steps
			+ " speed " + follower.get_speed())
	}
	if (same_pos(a.pos, GAP_ROAD_END))
	{
		fixture.leading_at_end = true;
	}
	if (same_pos(b.pos, GAP_ROAD_END))
	{
		fixture.following_at_end = true;
	}
	if (fixture.saw_moving_pair && fixture.leading_at_end && fixture.following_at_end)
	{
		print("RUNTIME CASE " + case_name + ": both convoys reached " + pos_text(GAP_ROAD_END)
			+ "; smallest moving gap " + fixture.min_gap_seen + " steps at " + fixture.min_gap_speed + " km/h")
		pass()
	}
}


function setup_through_pass(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_any_stop(pl, STOP_POS), "build the through stop"))
	{
		return false
	}
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The stopping vehicle waits for a full load, which never arrives on the
	// empty fixture map, so it holds the stop for the observation window. The
	// passing vehicle runs through the stop tile to the terminal.
	local stopping = make_convoy(pl, desc, [make_entry(STOP_POS, 21, 100), make_entry(MID_STOP, 22, 0)], "stopping")
	local passing = make_convoy(pl, desc, [make_entry(ROAD_END, 23, 0), make_entry(MID_STOP, 24, 0)], "passing")
	if (stopping == null || passing == null)
	{
		return false
	}
	fixture.convoys = [stopping, passing]
	if (!check_tool(fixture.depot.start_convoy(pl, stopping), "start the stopping convoy"))
	{
		return false
	}
	fixture.started = [true, false]
	return true
}


function observe_through_pass()
{
	local stopping = fixture.convoys[0]
	local passing = fixture.convoys[1]
	local a = front_slot(stopping)
	local b = front_slot(passing)
	if (a == null || b == null)
	{
		return
	}
	if (!fixture.second_started)
	{
		// Wait until the stopping vehicle is loading at the stop before
		// starting the passing vehicle.
		if (same_pos(a.pos, STOP_POS) && stopping.is_loading())
		{
			fixture.second_started = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), passing), "start the passing convoy"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": stopping convoy is loading at " + pos_text(STOP_POS)
				+ "; starting the passing convoy")
		}
		return
	}
	if ((fixture.observations % 100) == 0)
	{
		print("RUNTIME CASE " + case_name + ": passing convoy at " + pos_text(b.pos) + " steps " + b.steps
			+ " state " + passing.get_state() + "; stopping convoy at " + pos_text(a.pos) + " steps " + a.steps
			+ " state " + stopping.get_state())
	}
	if (same_pos(a.pos, STOP_POS) && same_pos(b.pos, STOP_POS) && !same_pos(b.pos, DEPOT_POS))
	{
		fixture.saw_same_tile = true
	}
	if (fixture.saw_same_tile && !same_pos(b.pos, STOP_POS))
	{
		print("RUNTIME CASE " + case_name + ": passing convoy crossed the stop tile while the stopping convoy loaded")
		pass()
	}
}


function setup_overtake(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, LONG_ROAD_END, road, true), "build the overtake road"))
	{
		return false
	}
	if (!check(build_terminal(pl, LONG_ROAD_END), "build the terminal at the overtake road end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, LONG_MID_STOP), "build the overtake mid stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local descs = slowest_and_fastest_descriptors()
	if (!check(descs != null, "no road vehicle descriptor is available"))
	{
		return false
	}
	local slow = descs[0]
	local fast = descs[1]
	if (!check(fast.get_topspeed() > slow.get_topspeed(),
		"the fastest road vehicle (" + fast.get_topspeed() + " km/h) is not faster than the slowest ("
		+ slow.get_topspeed() + " km/h)"))
	{
		return false
	}
	local slow_cnv = make_convoy(pl, slow, [make_entry(LONG_ROAD_END, 91, 0), make_entry(LONG_MID_STOP, 92, 0)], "slow vehicle")
	local fast_cnv = make_convoy(pl, fast, [make_entry(LONG_ROAD_END, 93, 0), make_entry(LONG_MID_STOP, 94, 0)], "fast vehicle")
	if (slow_cnv == null || fast_cnv == null)
	{
		return false
	}
	fixture.convoys = [slow_cnv, fast_cnv]
	if (!check_tool(fixture.depot.start_convoy(pl, slow_cnv), "start the slow vehicle"))
	{
		return false
	}
	fixture.started = [true, false]
	return true
}


function observe_overtake()
{
	local slow_cnv = fixture.convoys[0]
	local fast_cnv = fixture.convoys[1]
	local a = front_slot(slow_cnv)
	local b = front_slot(fast_cnv)
	if (a == null || b == null)
	{
		return
	}
	if (!fixture.second_started)
	{
		if (a.pos.x >= 4 && slow_cnv.get_speed() > 0)
		{
			fixture.second_started = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), fast_cnv), "start the fast vehicle"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": slow vehicle at " + pos_text(a.pos)
				+ "; starting the fast vehicle")
		}
		return
	}
	if (fixture.fast_arrival < 0 && !b.overtaking && (b.pos.x < a.pos.x || (same_pos(a.pos, b.pos) && b.steps <= a.steps))
		&& !same_pos(a.pos, LONG_ROAD_END))
	{
		// While the fast vehicle is in the traffic lane behind the slow one, it
		// must not overlap it. While overtaking, the step coordinates describe
		// the other lane and are not comparable.
		if (!check_no_overlap(slow_cnv, fast_cnv))
		{
			return
		}
	}
	if (fixture.slow_arrival < 0 && same_pos(a.pos, LONG_ROAD_END))
	{
		fixture.slow_arrival = fixture.observations;
		print("RUNTIME CASE " + case_name + ": slow vehicle reached " + pos_text(LONG_ROAD_END)
			+ " at observation " + fixture.slow_arrival)
	}
	if (fixture.fast_arrival < 0 && same_pos(b.pos, LONG_ROAD_END))
	{
		fixture.fast_arrival = fixture.observations;
		print("RUNTIME CASE " + case_name + ": fast vehicle reached " + pos_text(LONG_ROAD_END)
			+ " at observation " + fixture.fast_arrival)
	}
	if (fixture.slow_arrival >= 0 && fixture.fast_arrival >= 0)
	{
		if (!check(fixture.fast_arrival < fixture.slow_arrival,
			"the fast vehicle reached the destination after the slow vehicle (fast " + fixture.fast_arrival
			+ ", slow " + fixture.slow_arrival + "), so it did not overtake"))
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": fast vehicle overtook the slow vehicle and arrived first")
		pass()
	}
}


function setup_junction(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, JUNCTION_DEPOT_POS, JUNCTION_TRUNK_END, road, true), "build the east-west trunk"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, JUNCTION_POS, JUNCTION_BRANCH_END, road, true), "build the north branch"))
	{
		return false
	}
	if (!check(build_terminal(pl, JUNCTION_TRUNK_END), "build the trunk-end terminal"))
	{
		return false
	}
	if (!check(build_through_stop(pl, JUNCTION_SECOND_STOP), "build the second stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, JUNCTION_DEPOT_POS, depot_desc_for(wt_road)), "build the west depot"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, JUNCTION_BRANCH_END, depot_desc_for(wt_road)), "build the north depot"))
	{
		return false
	}
	fixture.depot = depot_x(JUNCTION_DEPOT_POS.x, JUNCTION_DEPOT_POS.y, JUNCTION_DEPOT_POS.z)
	fixture.second_depot = depot_x(JUNCTION_BRANCH_END.x, JUNCTION_BRANCH_END.y, JUNCTION_BRANCH_END.z)
	if (!check(fixture.depot != null && fixture.second_depot != null, "a junction depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	local from_west = make_convoy_at(fixture.depot, pl, desc,
		[make_entry(JUNCTION_TRUNK_END, 101, 0), make_entry(JUNCTION_SECOND_STOP, 102, 0)], "west convoy")
	local from_north = make_convoy_at(fixture.second_depot, pl, desc,
		[make_entry(JUNCTION_TRUNK_END, 103, 0), make_entry(JUNCTION_SECOND_STOP, 104, 0)], "north convoy")
	if (from_west == null || from_north == null)
	{
		return false
	}
	fixture.convoys = [from_west, from_north]
	if (!check_tool(fixture.depot.start_convoy(pl, from_west), "start the west convoy"))
	{
		return false
	}
	if (!check_tool(fixture.second_depot.start_convoy(pl, from_north), "start the north convoy"))
	{
		return false
	}
	fixture.started = [true, true]
	return true
}


function observe_junction()
{
	local west = fixture.convoys[0]
	local north = fixture.convoys[1]
	local a = front_slot(west)
	local b = front_slot(north)
	if (a == null || b == null)
	{
		return
	}
	if ((fixture.observations % 100) == 0)
	{
		print("RUNTIME CASE " + case_name + ": west convoy at " + pos_text(a.pos) + " state " + west.get_state()
			+ "; north convoy at " + pos_text(b.pos) + " state " + north.get_state())
	}
	if (same_pos(a.pos, JUNCTION_TRUNK_END))
	{
		fixture.leading_at_end = true;
	}
	if (same_pos(b.pos, JUNCTION_TRUNK_END))
	{
		fixture.following_at_end = true;
	}
	if (fixture.leading_at_end && fixture.following_at_end)
	{
		print("RUNTIME CASE " + case_name + ": both convoys passed the junction and reached "
			+ pos_text(JUNCTION_TRUNK_END))
		pass()
	}
}


function setup_crossroads(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, CROSSROADS_WEST_END, CROSSROADS_EAST_END, road, true), "build the east-west road"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, CROSSROADS_NORTH_END, CROSSROADS_SOUTH_END, road, true), "build the north-south road"))
	{
		return false
	}
	if (!check(build_through_stop(pl, CROSSROADS_WEST_STOP), "build the west return stop"))
	{
		return false
	}
	if (!check(build_through_stop(pl, CROSSROADS_EAST_STOP), "build the east return stop"))
	{
		return false
	}
	if (!check(build_through_stop(pl, CROSSROADS_NORTH_STOP), "build the north return stop"))
	{
		return false
	}
	if (!check(build_through_stop(pl, CROSSROADS_SOUTH_STOP), "build the south return stop"))
	{
		return false
	}
	local depot_positions = [CROSSROADS_WEST_DEPOT, CROSSROADS_EAST_DEPOT, CROSSROADS_NORTH_DEPOT, CROSSROADS_SOUTH_DEPOT]
	local depots = []
	foreach (pos in depot_positions)
	{
		if (!check_tool(command_x.build_depot(pl, pos, depot_desc_for(wt_road)), "build a crossroads depot"))
		{
			return false
		}
		local depot = depot_x(pos.x, pos.y, pos.z)
		if (!check(depot != null, "a crossroads depot was not found after building"))
		{
			return false
		}
		depots.append(depot)
	}
	fixture.depot = depots[0]
	fixture.second_depot = depots[1]
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// Two convoys leave the west depot in the same step. They follow the same
	// path through the junction, so they must be able to share the junction
	// tile. The other three approach from the remaining arms and must cross
	// without deadlocking against the westbound pair.
	local west_one = make_convoy_at(depots[0], pl, desc,
		[make_entry(CROSSROADS_EAST_STOP, 201, 0), make_entry(CROSSROADS_WEST_STOP, 202, 0)], "first west convoy")
	local west_two = make_convoy_at(depots[0], pl, desc,
		[make_entry(CROSSROADS_EAST_STOP, 203, 0), make_entry(CROSSROADS_WEST_STOP, 204, 0)], "second west convoy")
	local from_east = make_convoy_at(depots[1], pl, desc,
		[make_entry(CROSSROADS_WEST_STOP, 205, 0), make_entry(CROSSROADS_EAST_STOP, 206, 0)], "east convoy")
	local from_north = make_convoy_at(depots[2], pl, desc,
		[make_entry(CROSSROADS_SOUTH_STOP, 207, 0), make_entry(CROSSROADS_NORTH_STOP, 208, 0)], "north convoy")
	local from_south = make_convoy_at(depots[3], pl, desc,
		[make_entry(CROSSROADS_NORTH_STOP, 209, 0), make_entry(CROSSROADS_SOUTH_STOP, 210, 0)], "south convoy")
	if (west_one == null || west_two == null || from_east == null || from_north == null || from_south == null)
	{
		return false
	}
	fixture.convoys = [west_one, west_two, from_east, from_north, from_south]
	fixture.crossroads_destinations = [CROSSROADS_EAST_STOP, CROSSROADS_EAST_STOP,
		CROSSROADS_WEST_STOP, CROSSROADS_SOUTH_STOP, CROSSROADS_NORTH_STOP]
	fixture.crossroads_west_ids = [west_one.id, west_two.id]
	local starts = [ [depots[0], west_one], [depots[0], west_two], [depots[1], from_east],
		[depots[2], from_north], [depots[3], from_south] ]
	foreach (start in starts)
	{
		if (!check_tool(start[0].start_convoy(pl, start[1]), "start a crossroads convoy"))
		{
			return false
		}
		fixture.started.append(true)
	}
	return true
}


function observe_crossroads()
{
	local on_junction = 0
	local west_on_junction = 0
	for (local i = 0; i < fixture.convoys.len(); i++)
	{
		local slot = front_slot(fixture.convoys[i])
		if (slot == null)
		{
			continue
		}
		if (same_pos(slot.pos, CROSSROADS_POS))
		{
			on_junction++
			if (fixture.crossroads_west_ids.find(fixture.convoys[i].id) != null)
			{
				west_on_junction++
			}
		}
		if (same_pos(slot.pos, fixture.crossroads_destinations[i])
			&& !(fixture.convoys[i].id in fixture.crossroads_arrived))
		{
			fixture.crossroads_arrived[fixture.convoys[i].id] <- true
			print("RUNTIME CASE " + case_name + ": convoy " + fixture.convoys[i].id
				+ " reached " + pos_text(slot.pos))
		}
	}
	if (on_junction > fixture.crossroads_max_on_junction)
	{
		fixture.crossroads_max_on_junction = on_junction
	}
	if (west_on_junction > fixture.crossroads_max_west_on_junction)
	{
		fixture.crossroads_max_west_on_junction = west_on_junction
	}
	if ((fixture.observations % 200) == 0)
	{
		print("RUNTIME CASE " + case_name + ": on junction " + on_junction
			+ " (westbound " + west_on_junction + "), arrived " + fixture.crossroads_arrived.len()
			+ " of " + fixture.convoys.len())
	}
	if (fixture.crossroads_arrived.len() < fixture.convoys.len())
	{
		return
	}
	if (!check(fixture.crossroads_max_west_on_junction >= 2,
		"the two westbound convoys never shared the junction tile; maximum westbound on the junction was "
		+ fixture.crossroads_max_west_on_junction))
	{
		return
	}
	if (!check(fixture.crossroads_max_on_junction >= 2,
		"the junction never held more than one convoy at a time"))
	{
		return
	}
	print("RUNTIME CASE " + case_name + ": all " + fixture.convoys.len()
		+ " convoys crossed the junction; up to " + fixture.crossroads_max_on_junction
		+ " shared it, " + fixture.crossroads_max_west_on_junction + " of them westbound")
	pass()
}


function setup_junction_curve(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	// The west arm and the two junctions.
	if (!check_tool(command_x.build_way(pl, CURVE_WEST_END, CURVE_B, road, true), "build the west arm to junction B"))
	{
		return false
	}
	// The south arm of junction A.
	if (!check_tool(command_x.build_way(pl, CURVE_A, CURVE_A_SOUTH_DEPOT, road, true), "build the south arm of junction A"))
	{
		return false
	}
	// The third arm of junction B and the bend C.
	if (!check_tool(command_x.build_way(pl, CURVE_B, CURVE_B_NORTH_END, road, true), "build the north arm of junction B"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, CURVE_B, CURVE_BEND, road, true), "build the bend approach from junction B"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, CURVE_BEND, CURVE_C_SOUTH_DEPOT, road, true), "build the south road from the bend"))
	{
		return false
	}
	if (!check(build_terminal(pl, CURVE_WEST_END), "build the west terminal"))
	{
		return false
	}
	if (!check(build_through_stop(pl, CURVE_SOUTH_STOP), "build the south stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, CURVE_A_SOUTH_DEPOT, depot_desc_for(wt_road)), "build the junction A depot"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, CURVE_C_SOUTH_DEPOT, depot_desc_for(wt_road)), "build the bend depot"))
	{
		return false
	}
	fixture.depot = depot_x(CURVE_A_SOUTH_DEPOT.x, CURVE_A_SOUTH_DEPOT.y, CURVE_A_SOUTH_DEPOT.z)
	fixture.second_depot = depot_x(CURVE_C_SOUTH_DEPOT.x, CURVE_C_SOUTH_DEPOT.y, CURVE_C_SOUTH_DEPOT.z)
	if (!check(fixture.depot != null && fixture.second_depot != null, "a junction-curve depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The second entry of each schedule is behind the first one on the way
	// back, so the outbound leg is the path through both junctions. The case
	// finishes when the first entry is reached.
	local from_south = make_convoy_at(fixture.depot, pl, desc,
		[make_entry(CURVE_SOUTH_STOP, 301, 0), make_entry(CURVE_B, 302, 0)], "junction convoy")
	local through_bend = make_convoy_at(fixture.second_depot, pl, desc,
		[make_entry(CURVE_WEST_END, 303, 0), make_entry(CURVE_BEND, 304, 0)], "bend convoy")
	if (from_south == null || through_bend == null)
	{
		return false
	}
	fixture.convoys = [from_south, through_bend]
	fixture.curve_destinations = [CURVE_SOUTH_STOP, CURVE_WEST_END]
	if (!check_tool(fixture.depot.start_convoy(pl, from_south), "start the junction convoy"))
	{
		return false
	}
	if (!check_tool(fixture.second_depot.start_convoy(pl, through_bend), "start the bend convoy"))
	{
		return false
	}
	fixture.started = [true, true]
	return true
}


function observe_junction_curve()
{
	for (local i = 0; i < fixture.convoys.len(); i++)
	{
		local slot = front_slot(fixture.convoys[i])
		if (slot == null)
		{
			continue
		}
		if (same_pos(slot.pos, fixture.curve_destinations[i])
			&& !(fixture.convoys[i].id in fixture.curve_arrived))
		{
			fixture.curve_arrived[fixture.convoys[i].id] <- true
			print("RUNTIME CASE " + case_name + ": convoy " + fixture.convoys[i].id
				+ " reached " + pos_text(slot.pos))
		}
	}
	if ((fixture.observations % 200) == 0)
	{
		local a = front_slot(fixture.convoys[0])
		local b = front_slot(fixture.convoys[1])
		if (a != null && b != null)
		{
			print("RUNTIME CASE " + case_name + ": junction convoy at " + pos_text(a.pos)
				+ " speed " + fixture.convoys[0].get_speed() + "; bend convoy at "
				+ pos_text(b.pos) + " speed " + fixture.convoys[1].get_speed()
				+ "; arrived " + fixture.curve_arrived.len() + " of " + fixture.convoys.len())
		}
	}
	if (fixture.curve_arrived.len() < fixture.convoys.len())
	{
		return
	}
	print("RUNTIME CASE " + case_name + ": both convoys left the adjacent junctions and reached their destinations")
	pass()
}


function setup_terminal(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, TERMINAL_ROAD_END, road, true), "build the terminal road"))
	{
		return false
	}
	// Only a terminal (a road stop building with four layouts) can be built on
	// the single-direction dead end, so a successful build identifies one.
	if (!check(build_terminal(pl, TERMINAL_ROAD_END), "build the terminal on the dead end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the second stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	for (local i = 0; i < 5; i++)
	{
		local cnv = make_convoy(pl, desc, [make_entry(TERMINAL_ROAD_END, 31 + i, 100), make_entry(STOP_POS, 41 + i, 0)], "terminal convoy " + (i + 1))
		if (cnv == null)
		{
			return false
		}
		fixture.convoys.append(cnv)
	}
	foreach (cnv in fixture.convoys)
	{
		if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start a terminal convoy"))
		{
			return false
		}
		fixture.started.append(true)
	}
	return true
}


function observe_terminal()
{
	local on_tile = 0
	foreach (cnv in fixture.convoys)
	{
		local slot = front_slot(cnv)
		if (slot != null && same_pos(slot.pos, TERMINAL_ROAD_END))
		{
			on_tile++
			if (!(cnv.id in fixture.arrived_at_terminal))
			{
				fixture.arrived_at_terminal[cnv.id] <- true
			}
		}
	}
	if (on_tile > fixture.max_on_stop_tile)
	{
		fixture.max_on_stop_tile = on_tile
	}
	if (!check(on_tile <= 4, on_tile + " convoys are on the terminal tile, but a terminal has four stands"))
	{
		return
	}
	// Four stands must all be usable: four convoys reach the terminal, and the
	// fifth waits off the tile.
	local count = 0
	foreach (id, seen in fixture.arrived_at_terminal)
	{
		count++
	}
	if (count >= 4 && on_tile == 4)
	{
		print("RUNTIME CASE " + case_name + ": four convoys are on the terminal tile; the fifth waits; maximum seen "
			+ fixture.max_on_stop_tile)
		pass()
	}
}


function setup_stop_release(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the through stop"))
	{
		return false
	}
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The holder waits for a full load and departs when its maximum wait
	// expires. The second vehicle is started only after the holder has left,
	// so that its route to the stop finds the released stand.
	local holder = make_convoy(pl, desc,
		[make_entry_wait(STOP_POS, 111, 100, 8), make_entry(ROAD_END, 112, 0)], "stand holder")
	local second = make_convoy(pl, desc,
		[make_entry(STOP_POS, 113, 100), make_entry(ROAD_END, 114, 0)], "second vehicle")
	if (holder == null || second == null)
	{
		return false
	}
	fixture.convoys = [holder, second]
	if (!check_tool(fixture.depot.start_convoy(pl, holder), "start the stand holder"))
	{
		return false
	}
	fixture.started = [true, false]
	return true
}


function observe_stop_release()
{
	local holder = fixture.convoys[0]
	local second = fixture.convoys[1]
	local a = front_slot(holder)
	local b = front_slot(second)
	if (a == null || b == null)
	{
		return
	}
	if (!fixture.holder_seen)
	{
		if (same_pos(a.pos, STOP_POS) && holder.is_loading() && a.stand_mask != 0)
		{
			fixture.holder_seen = true
			print("RUNTIME CASE " + case_name + ": holder is loading at the stand at " + pos_text(STOP_POS)
				+ " (stand mask " + a.stand_mask + ")")
		}
		return
	}
	if (!fixture.holder_cleared)
	{
		// The holder must have left the stop tile itself: while it is still on
		// the tile its stand reservation has not yet been released.
		if (!same_pos(a.pos, STOP_POS) && !holder.is_loading())
		{
			fixture.holder_cleared = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), second), "start the second vehicle after the stand was released"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": holder left the stop; starting the second vehicle")
		}
		return
	}
	if (same_pos(b.pos, STOP_POS) && second.is_loading() && b.stand_mask != 0)
	{
		print("RUNTIME CASE " + case_name + ": second vehicle is loading at the released stand at "
			+ pos_text(STOP_POS) + " (stand mask " + b.stand_mask + ")")
		pass()
	}
}


// A through stop whose schedule entry reverses. The vehicle must keep its
// stand while it is still on the stop tile, including through the reversal
// wait, and must then leave the stop in the opposite direction. The
// westbound variant approaches the stop heading west, where the vehicle stops
// at the middle of the halt tile rather than at its end.
function setup_reverse_through(pl)
{
	local westbound = case_name == "reverse-through-westbound"
	local second_stop = westbound ? REVERSE_EAST_STOP : REVERSE_WEST_STOP
	local depot_pos = westbound ? REVERSE_EAST_DEPOT : DEPOT_POS
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the reversing stop"))
	{
		return false
	}
	if (!check(build_through_stop(pl, second_stop), "build the stop beyond the reversing stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, depot_pos, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(depot_pos.x, depot_pos.y, depot_pos.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The maximum wait ends the stop; no passengers are generated in the
	// empty world, so the vehicle would otherwise wait indefinitely.
	local stop_entry = make_entry_wait(STOP_POS, 111, 100, 8)
	stop_entry.reverse = 1
	local cnv = make_convoy(pl, desc,
		[stop_entry, make_entry(second_stop, 112, 0)], "reversing vehicle")
	if (cnv == null)
	{
		return false
	}
	fixture.convoys = [cnv]
	fixture.reverse_westbound = westbound
	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the reversing vehicle"))
	{
		return false
	}
	fixture.started = [true]
	return true
}


function observe_reverse_through()
{
	local cnv = fixture.convoys[0]
	local a = front_slot(cnv)
	if (a == null)
	{
		return
	}
	if (same_pos(a.pos, STOP_POS) && cnv.is_loading())
	{
		if (a.stand_mask == 0)
		{
			fail("the reversing vehicle is loading at the stop without holding its stand")
			return
		}
		if (!fixture.holder_seen)
		{
			fixture.holder_seen = true
			print("RUNTIME CASE " + case_name + ": loading at the reversing stop with stand mask "
				+ a.stand_mask)
		}
		return
	}
	// After loading, the vehicle waits at the stop for the reversal. The stand
	// must remain held while it is still on the stop tile so that it stays
	// drawn at the kerb.
	if (fixture.holder_seen && same_pos(a.pos, STOP_POS) && a.speed == 0 && !cnv.is_loading())
	{
		if (a.stand_mask == 0)
		{
			fail("the reversing vehicle lost its stand while still on the stop tile")
			return
		}
		if (!a.pulled_over)
		{
			// Following traffic, including private cars, passes a vehicle only
			// when this flag says that it is beside the travel lane.
			fail("the reversing vehicle is not reported as pulled over while waiting at its stand")
			return
		}
		if (!fixture.reversal_seen)
		{
			fixture.reversal_seen = true
			print("RUNTIME CASE " + case_name + ": holding stand " + a.stand_mask
				+ " while waiting to reverse")
		}
		return
	}
	local left_the_stop = fixture.reverse_westbound ? a.pos.x > STOP_POS.x : a.pos.x < STOP_POS.x
	if (fixture.reversal_seen && left_the_stop)
	{
		print("RUNTIME CASE " + case_name + ": reversed away from the stop at " + pos_text(a.pos))
		pass()
	}
}


function setup_terminal_turnover(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, TERMINAL_ROAD_END, road, true), "build the terminal road"))
	{
		return false
	}
	if (!check(build_terminal(pl, TERMINAL_ROAD_END), "build the terminal on the dead end"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the second stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	for (local i = 0; i < 5; i++)
	{
		local cnv = make_convoy(pl, desc,
			[make_entry_wait(TERMINAL_ROAD_END, 121 + i, 100, 10), make_entry(STOP_POS, 131 + i, 0)], "terminal convoy " + (i + 1))
		if (cnv == null)
		{
			return false
		}
		fixture.convoys.append(cnv)
	}
	// Start the first four; the fifth waits in the depot until a stand is
	// released.
	for (local i = 0; i < 4; i++)
	{
		if (!check_tool(fixture.depot.start_convoy(pl, fixture.convoys[i]), "start terminal convoy " + (i + 1)))
		{
			return false
		}
		fixture.started.append(true)
	}
	fixture.started.append(false)
	return true
}


function observe_terminal_turnover()
{
	local stood = 0
	local on_tile = 0
	for (local i = 0; i < fixture.convoys.len(); i++)
	{
		local slot = front_slot(fixture.convoys[i])
		if (slot != null && same_pos(slot.pos, TERMINAL_ROAD_END))
		{
			on_tile++
			if (slot.stand_mask != 0)
			{
				stood++
				if (!(fixture.convoys[i].id in fixture.distinct_on_terminal))
				{
					fixture.distinct_on_terminal[fixture.convoys[i].id] <- true
				}
			}
		}
	}
	if (stood > fixture.max_on_stop_tile)
	{
		fixture.max_on_stop_tile = stood
	}
	if ((fixture.observations % 200) == 0)
	{
		local text = "RUNTIME CASE " + case_name + ": on tile " + on_tile + ", stood " + stood + ":"
		for (local i = 0; i < fixture.convoys.len(); i++)
		{
			local s = front_slot(fixture.convoys[i])
			if (s != null)
			{
				text = text + " [" + i + " " + pos_text(s.pos) + " state " + fixture.convoys[i].get_state()
					+ " mask " + s.stand_mask + " speed " + fixture.convoys[i].get_speed() + "]"
			}
		}
		print(text)
	}
	if (!check(stood <= 4, stood + " convoys hold stands on the terminal tile, but a terminal has four stands"))
	{
		return
	}
	if (!fixture.four_seen)
	{
		if (stood == 4)
		{
			fixture.four_seen = true
			print("RUNTIME CASE " + case_name + ": four convoys hold the four terminal stands")
		}
		return
	}
	if (!fixture.fifth_started)
	{
		// Wait until a stand is free, then start the fifth convoy. A vehicle
		// may be on the terminal tile in the travel lane without holding a
		// stand, so the count that matters is the stood count.
		if (stood < 4)
		{
			fixture.fifth_started = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), fixture.convoys[4]), "start the fifth terminal convoy after a stand was released"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": a stand was released; starting the fifth convoy ("
				+ on_tile + " vehicles on the tile, " + stood + " at stands)")
		}
		return
	}
	local fifth = fixture.convoys[4]
	local slot = front_slot(fifth)
	if (slot != null && same_pos(slot.pos, TERMINAL_ROAD_END) && fifth.is_loading() && slot.stand_mask != 0)
	{
		local count = 0
		foreach (id, seen in fixture.distinct_on_terminal)
		{
			count++
		}
		print("RUNTIME CASE " + case_name + ": the fifth convoy took the released stand; "
			+ count + " distinct convoys used the terminal stands, at most " + fixture.max_on_stop_tile + " at once")
		pass()
	}
}


function setup_save_reload_stand(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, DEPOT_POS, ROAD_END, road, true), "build the straight road"))
	{
		return false
	}
	if (!check(build_through_stop(pl, STOP_POS), "build the through stop"))
	{
		return false
	}
	if (!check(build_terminal(pl, ROAD_END), "build the terminal at the road end"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_road)), "build the road depot"))
	{
		return false
	}
	fixture.depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(fixture.depot != null, "the road depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The vehicle waits for a full load with a long maximum wait, so that the
	// join's save and reload happen while it still holds the stand. It departs
	// naturally after the reload and runs to the terminal.
	local holder = make_convoy(pl, desc,
		[make_entry_wait(STOP_POS, 141, 100, 12), make_entry(ROAD_END, 142, 0)], "reload holder")
	if (holder == null)
	{
		return false
	}
	fixture.convoys = [holder]
	if (!check_tool(fixture.depot.start_convoy(pl, holder), "start the reload holder"))
	{
		return false
	}
	return true
}


function observe_save_reload_stand()
{
	local cnv = fixture.convoys[0]
	local slot = front_slot(cnv)
	if (slot == null)
	{
		return
	}
	if (fixture.phase == "ready")
	{
		// Wait for resume_game() to restore the state.
		return
	}
	if (fixture.phase == "restored")
	{
		if (same_pos(slot.pos, ROAD_END))
		{
			print("RUNTIME CASE " + case_name + ": restored vehicle reached " + pos_text(ROAD_END))
			pass()
		}
		return
	}
	if (same_pos(slot.pos, STOP_POS) && cnv.is_loading() && slot.stand_mask != 0)
	{
		persistent.reload <- { kind = case_name, convoy_id = cnv.id, stand_mask = slot.stand_mask }
		fixture.phase = "ready"
		print("RUNTIME CASE " + case_name + " READY: vehicle " + cnv.id + " is loading at stand mask "
			+ slot.stand_mask + " at " + pos_text(STOP_POS))
	}
}


// Called by scenario_t::rdwr after a savegame has been loaded. The reload case
// reconstructs the vehicle from the persisted id and checks that the saved
// stand mask and pulled-over state survived the load.
function resume_game()
{
	if (case_name != "save-reload-stand")
	{
		return
	}
	if (!("reload" in persistent) || typeof(persistent.reload) != "table")
	{
		fail("the savegame holds no persisted reload state; it must be saved while the vehicle holds the stand")
		return
	}
	local state = persistent.reload
	delete persistent.reload

	fixture.setup_started = true
	fixture.phase = "restored"
	local cnv = convoy_x(state.convoy_id)
	if (!check(cnv.is_valid(), "the convoy " + state.convoy_id + " named by the persisted state is not in the loaded world"))
	{
		return
	}
	fixture.convoys = [cnv]
	local slot = front_slot(cnv)
	if (!check(slot != null && same_pos(slot.pos, STOP_POS), "the restored vehicle is not at the stop " + pos_text(STOP_POS)))
	{
		return
	}
	if (!check(slot.stand_mask == state.stand_mask,
		"the restored stand mask " + slot.stand_mask + " differs from the saved mask " + state.stand_mask))
	{
		return
	}
	if (!check(slot.pulled_over, "the restored vehicle holds a stand but is not pulled over"))
	{
		return
	}
	print("RUNTIME CASE " + case_name + " RESTORED: vehicle " + state.convoy_id + " is loading at stand mask "
		+ slot.stand_mask + " at " + pos_text(STOP_POS))
}


function setup_through_pass_oncoming(pl)
{
	local road = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	if (!check(road != null, "no flat road is available"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, ONCOMING_ROAD_WEST, ONCOMING_ROAD_EAST, road, true), "build the two-way road"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, ONCOMING_EAST_BRANCH, ONCOMING_EAST_TERMINAL, road, true), "build the east terminal branch"))
	{
		return false
	}
	if (!check_tool(command_x.build_way(pl, ONCOMING_WEST_BRANCH, ONCOMING_WEST_TERMINAL, road, true), "build the west terminal branch"))
	{
		return false
	}
	if (!check(build_terminal(pl, ONCOMING_EAST_TERMINAL), "build the east terminal"))
	{
		return false
	}
	if (!check(build_terminal(pl, ONCOMING_WEST_TERMINAL), "build the west terminal"))
	{
		return false
	}
	if (!check(build_through_stop(pl, ONCOMING_STOP), "build the through stop"))
	{
		return false
	}
	if (!check(build_through_stop(pl, ONCOMING_WEST_STOP), "build the westbound stream stop"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, ONCOMING_ROAD_WEST, depot_desc_for(wt_road)), "build the eastbound depot"))
	{
		return false
	}
	if (!check_tool(command_x.build_depot(pl, ONCOMING_ROAD_EAST, depot_desc_for(wt_road)), "build the westbound depot"))
	{
		return false
	}
	fixture.depot = depot_x(ONCOMING_ROAD_WEST.x, ONCOMING_ROAD_WEST.y, ONCOMING_ROAD_WEST.z)
	fixture.second_depot = depot_x(ONCOMING_ROAD_EAST.x, ONCOMING_ROAD_EAST.y, ONCOMING_ROAD_EAST.z)
	if (!check(fixture.depot != null && fixture.second_depot != null, "an oncoming-case depot was not found after building"))
	{
		return false
	}
	local desc = road_vehicle_desc()
	if (!check(desc != null, "no road vehicle is available"))
	{
		return false
	}
	// The holder waits for a full load, which never arrives, so it holds the
	// through stop and stays pulled over for the whole observation window.
	local holder = make_convoy_at(fixture.depot, pl, desc,
		[make_entry(ONCOMING_STOP, 151, 100), make_entry(ONCOMING_EAST_TERMINAL, 152, 0)], "eastbound holder")
	// The passer starts behind the holder and crosses the stop tile on its way
	// to the east terminal.
	local passer = make_convoy_at(fixture.depot, pl, desc,
		[make_entry(ONCOMING_EAST_TERMINAL, 153, 0), make_entry(ONCOMING_WEST_STOP, 154, 0)], "eastbound passer")
	if (holder == null || passer == null)
	{
		return false
	}
	// The westbound stream leaves the far depot, passes the through stop
	// without stopping and stows briefly at the west stop before turning to
	// the west terminal. Ten convoys give a platoon long enough to cover the
	// passer's crossing of the stop tile.
	for (local i = 0; i < ONCOMING_STREAM_CONVOYS; i++)
	{
		local cnv = make_convoy_at(fixture.second_depot, pl, desc,
			[make_entry(ONCOMING_WEST_STOP, 161 + i, 0), make_entry(ONCOMING_WEST_TERMINAL, 181 + i, 0)], "westbound stream " + (i + 1))
		if (cnv == null)
		{
			return false
		}
		fixture.stream.append(cnv)
	}
	fixture.convoys = [holder, passer]
	if (!check_tool(fixture.depot.start_convoy(pl, holder), "start the eastbound holder"))
	{
		return false
	}
	fixture.started = [true, false]
	return true
}


function observe_through_pass_oncoming()
{
	local holder = fixture.convoys[0]
	local passer = fixture.convoys[1]
	local a = front_slot(holder)
	local b = front_slot(passer)
	if (a == null || b == null)
	{
		return
	}
	if (!fixture.holder_ready)
	{
		if (same_pos(a.pos, ONCOMING_STOP) && holder.is_loading() && a.pulled_over)
		{
			fixture.holder_ready = true
			print("RUNTIME CASE " + case_name + ": holder is loading, pulled over at " + pos_text(ONCOMING_STOP))
		}
		return
	}
	if (!fixture.stream_started)
	{
		fixture.stream_started = true
		foreach (cnv in fixture.stream)
		{
			if (!check_tool(fixture.second_depot.start_convoy(player_x(0), cnv), "start a westbound stream convoy"))
			{
				return
			}
		}
		print("RUNTIME CASE " + case_name + ": westbound stream started (" + fixture.stream.len() + " convoys)")
		return
	}
	if (!fixture.passer_started)
	{
		// Start the passer when the leading westbound convoy is about to
		// reach the stop, so that the platoon covers the stop while the
		// passer crosses it. The stream travels west, so the leading convoy
		// has the smallest x among the moving convoys that have left the
		// depot.
		local lead_x = -1
		foreach (cnv in fixture.stream)
		{
			local s = front_slot(cnv)
			if (s != null && cnv.get_speed() > 0 && !on_depot_tile(s.pos)
				&& (lead_x < 0 || s.pos.x < lead_x))
			{
				lead_x = s.pos.x
			}
		}
		if (lead_x >= 0 && lead_x <= 9)
		{
			fixture.passer_started = true
			if (!check_tool(fixture.depot.start_convoy(player_x(0), passer), "start the eastbound passer"))
			{
				return
			}
			print("RUNTIME CASE " + case_name + ": westbound lead reached x " + lead_x + "; starting the eastbound passer")
		}
		return
	}
	// The holder must keep the stand and the pulled-over position while the
	// passer crosses.
	if (same_pos(b.pos, ONCOMING_STOP) && holder.is_loading() && a.pulled_over)
	{
		fixture.holder_held = true
	}
	local oncoming_here = 0
	foreach (cnv in fixture.stream)
	{
		local s = front_slot(cnv)
		if (s != null && same_pos(s.pos, ONCOMING_STOP))
		{
			if (!(cnv.id in fixture.distinct_oncoming))
			{
				fixture.distinct_oncoming[cnv.id] <- true
			}
			oncoming_here++
		}
	}
	if (same_pos(b.pos, ONCOMING_STOP))
	{
		if (b.overtaking)
		{
			fail("the eastbound passer moved into the opposite carriageway (overtaking) on the stop tile")
			return
		}
		if (oncoming_here > 0)
		{
			fixture.cross_seen = true
			if ((fixture.observations % 100) == 0 || !fixture.saw_same_tile)
			{
				print("RUNTIME CASE " + case_name + ": passer on the stop tile with " + oncoming_here
					+ " westbound convoy(s) oncoming; passer overtaking false")
			}
			fixture.saw_same_tile = true
		}
	}
	if ((fixture.observations % 200) == 0)
	{
		print("RUNTIME CASE " + case_name + ": passer at " + pos_text(b.pos) + " state " + passer.get_state()
			+ " overtaking " + b.overtaking + "; oncoming at the stop " + oncoming_here
			+ "; distinct oncoming seen " + fixture.distinct_oncoming.len())
	}
	if (same_pos(b.pos, ONCOMING_EAST_TERMINAL))
	{
		fixture.following_at_end = true
	}
	if (fixture.following_at_end)
	{
		local distinct = fixture.distinct_oncoming.len()
		if (!check(fixture.cross_seen, "the passer reached the terminal without sharing the stop tile with a westbound convoy"))
		{
			return
		}
		if (!check(fixture.holder_held, "the holder was not loading and pulled over while the passer crossed the stop"))
		{
			return
		}
		if (!check(distinct >= 3, "only " + distinct + " distinct westbound convoys were seen at the stop, so the stream was not continuous"))
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": passer crossed the stop with oncoming traffic; "
			+ distinct + " distinct westbound convoys used the stop tile")
		pass()
	}
}


function setup()
{
	local pl = player_x(0)
	if (case_name == "queue")
	{
		return setup_queue(pl)
	}
	if (case_name == "single-run")
	{
		return setup_single_run(pl)
	}
	if (case_name == "trigger-wait")
	{
		return setup_trigger_wait(pl)
	}
	if (case_name == "trigger-timeout")
	{
		return setup_trigger_timeout(pl)
	}
	if (case_name == "moving-gap")
	{
		return setup_moving_gap(pl)
	}
	if (case_name == "overtake")
	{
		return setup_overtake(pl)
	}
	if (case_name == "junction")
	{
		return setup_junction(pl)
	}
	if (case_name == "crossroads")
	{
		return setup_crossroads(pl)
	}
	if (case_name == "junction-curve")
	{
		return setup_junction_curve(pl)
	}
	if (case_name == "stop-release")
	{
		return setup_stop_release(pl)
	}
	if (case_name == "reverse-through" || case_name == "reverse-through-westbound")
	{
		return setup_reverse_through(pl)
	}
	if (case_name == "terminal-turnover")
	{
		return setup_terminal_turnover(pl)
	}
	if (case_name == "save-reload-stand")
	{
		return setup_save_reload_stand(pl)
	}
	if (case_name == "through-pass")
	{
		return setup_through_pass(pl)
	}
	if (case_name == "through-pass-oncoming")
	{
		return setup_through_pass_oncoming(pl)
	}
	if (case_name == "terminal-stands")
	{
		return setup_terminal(pl)
	}
	return fail("unknown road runtime case: " + case_name)
}


function observe()
{
	++fixture.observations
	if (fixture.observations > 20000)
	{
		fail("timed out after " + fixture.observations + " observations in phase " + fixture.phase)
		return
	}
	if (case_name == "queue")
	{
		observe_queue()
	}
	else if (case_name == "single-run")
	{
		observe_single_run()
	}
	else if (case_name == "trigger-wait")
	{
		observe_trigger_wait()
	}
	else if (case_name == "trigger-timeout")
	{
		observe_trigger_timeout()
	}
	else if (case_name == "moving-gap")
	{
		observe_moving_gap()
	}
	else if (case_name == "overtake")
	{
		observe_overtake()
	}
	else if (case_name == "junction")
	{
		observe_junction()
	}
	else if (case_name == "crossroads")
	{
		observe_crossroads()
	}
	else if (case_name == "junction-curve")
	{
		observe_junction_curve()
	}
	else if (case_name == "stop-release")
	{
		observe_stop_release()
	}
	else if (case_name == "reverse-through" || case_name == "reverse-through-westbound")
	{
		observe_reverse_through()
	}
	else if (case_name == "terminal-turnover")
	{
		observe_terminal_turnover()
	}
	else if (case_name == "save-reload-stand")
	{
		observe_save_reload_stand()
	}
	else if (case_name == "through-pass")
	{
		observe_through_pass()
	}
	else if (case_name == "through-pass-oncoming")
	{
		observe_through_pass_oncoming()
	}
	else if (case_name == "terminal-stands")
	{
		observe_terminal()
	}
}


function get_goal_text(pl)
{
	return ttext("Run the selected road-vehicle operations runtime case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Road vehicle operations runtime case {name}: {phase}")
	msg.name = case_name
	msg.phase = fixture.phase
	return msg
}


function get_result_text(pl)
{
	return get_info_text(pl)
}


function is_scenario_completed(pl)
{
	if (pl != 0)
	{
		return 0
	}
	++heartbeat_steps
	if ((heartbeat_steps % 200) == 0)
	{
		print("RUNTIME CASE " + case_name + ": heartbeat step " + heartbeat_steps)
	}
	if (!fixture.setup_started)
	{
		fixture.setup_started = true
		local ok = false
		try {
			ok = setup()
		}
		catch (e) {
			fail("setup raised an error: " + e)
			return -1
		}
		if (!ok)
		{
			return -1
		}
		fixture.phase = "observing"
		print("RUNTIME CASE " + case_name + ": setup complete")
		return 0
	}
	if (fixture.phase == "failed")
	{
		return -1
	}
	if (fixture.phase == "done")
	{
		return 100
	}
	try {
		observe()
	}
	catch (e) {
		fail("observation raised an error: " + e)
		return -1
	}
	if (fixture.phase == "done")
	{
		return 100
	}
	if (fixture.phase == "failed")
	{
		return -1
	}
	return 0
}
