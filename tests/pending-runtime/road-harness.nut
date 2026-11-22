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

// Vehicle steps per carunit, from simunits.h. A vehicle descriptor's length is
// in carunits.
local VEHICLE_STEPS_PER_CARUNIT = 16

local heartbeat_steps = 0

local fixture = {
	phase = "waiting-for-setup",
	setup_started = false,
	completed = false,
	depot = null,
	convoys = [],
	started = [],
	observations = 0,
	saw_same_tile = false,
	max_on_stop_tile = 0,
	arrived_at_terminal = {},
	second_started = false,
	leading_at_end = false,
	following_at_end = false
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


function road_vehicle_desc()
{
	local list = vehicle_desc_x.get_available_vehicles(wt_road)
	if (list.len() == 0)
	{
		return null
	}
	return list[0]
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


// Creates one convoy in the fixture depot with one vehicle and the given
// schedule. Returns the convoy, or null after reporting a failure.
function make_convoy(pl, desc, entries, what)
{
	local before = convoy_ids()
	if (!check_tool(fixture.depot.append_vehicle(pl, convoy_x(0), desc), "add the vehicle of " + what))
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


// Asserts that two front vehicles on the same tile do not overlap: a vehicle
// whose front is behind another vehicle's front must stay behind that
// vehicle's rear. Vehicles overtaking are skipped: their steps pass the other
// vehicle's front while they are in the other lane, which the snapshot does
// not expose. All fixture vehicles have the same top speed, so the queue cases
// do not overtake.
function check_no_overlap(leader, follower)
{
	if (leader.is_loading() || follower.is_loading())
	{
		// A vehicle loading at a stand is in another lateral position.
		return true
	}
	local a = front_slot(leader)
	local b = front_slot(follower)
	if (a == null || b == null || !same_pos(a.pos, b.pos) || same_pos(a.pos, DEPOT_POS))
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
		+ " overlaps the rear (" + rear + ") of the vehicle of convoy " + leader.id
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
	if (!check(build_any_stop(pl, ROAD_END), "build the terminal at the road end"))
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
	if (!check(build_any_stop(pl, ROAD_END), "build the terminal at the road end"))
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
	if (!check(build_any_stop(pl, TERMINAL_ROAD_END), "build the terminal on the dead end"))
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


function setup()
{
	local pl = player_x(0)
	if (case_name == "queue")
	{
		return setup_queue(pl)
	}
	if (case_name == "through-pass")
	{
		return setup_through_pass(pl)
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
	else if (case_name == "through-pass")
	{
		observe_through_pass()
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
