//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Fixture for the multi-owner access rule at consist assembly. A second
// player's vehicle is left in the junction's laid-over pool; the first
// player's collector then tries to take it. The collector's consist order may
// use the foreign vehicle only when the owner of the first powered vehicle in
// the final consist permits the foreign vehicle's owner.
//
// Run through scripts/run-pending-runtime-tests.sh, which stages this file
// with the case's scenario.nut and empty-16x16.sve in the pakset scenario
// directory. Every case starts from the empty world.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Consist recombination ownership case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

local heartbeat_steps = 0

local TRUNK_POS = coord3d(3, 2, 0)
local JUNCTION_POS = coord3d(5, 2, 0)
local JUNCTION_LOOP_POS = coord3d(5, 3, 0)
local EAST_POS = coord3d(9, 2, 0)
local NORTH_POS = coord3d(6, 0, 0)

// The second player's depot and its connection to the first player's trunk.
// The branch is built by the second player on empty ground; the connector
// joins the first player's trunk at (2, 2).
local SECOND_DEPOT_POS = coord3d(1, 1, 0)
local SECOND_BRANCH_END = coord3d(2, 1, 0)
local SECOND_CONNECTOR_POS = coord3d(2, 2, 0)

local DONOR_POOL_ENTRY_ID = 41
local DONOR_NEXT_ENTRY_ID = 42
local COLLECTOR_ENTRY_ID = 51
local COLLECTOR_NEXT_ENTRY_ID = 52

local TRAVEL_STEP_LIMIT = 6000
local PREPARATION_STEP_LIMIT = 3000

local fixture = {
	phase = "waiting-for-setup",
	setup_started = false,
	completed = false,
	depot0 = null,
	depot2 = null,
	collector_id = 0,
	donor_id = 0,
	retained = null,
	foreign = null,
	donor_removed = false,
	access_revoked = false,
	collector_started = false,
	preparation_steps = 0,
	travel_steps = 0,
	waiting_for = ""
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
}


function check(ok, what)
{
	if (!ok)
	{
		return fail(what)
	}
	return true
}


// A tool call has two success conventions: command_x.work calls return null,
// while the init-phase command wrappers return true. Both are accepted; the
// committed state is read back where the result matters.
function check_tool(result, what)
{
	if (result == null || result == true)
	{
		return true
	}
	return fail(what + ": " + result)
}


function vehicle_matches(desc, expected)
{
	return desc != null && expected != null && desc.is_equal(expected)
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


function make_entry(pos, unique_entry_id)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = unique_entry_id
	return entry
}


// One-slot-per-descriptor end state. Each slot has one specific-vehicle
// alternative.
function make_order(descriptors)
{
	local order = consist_order_x()
	local slots = []
	foreach (desc in descriptors)
	{
		local slot = consist_order_element_x()
		local alternative = vehicle_description_x()
		alternative.specific_vehicle = desc
		slot.alternatives = [alternative]
		slots.append(slot)
	}
	order.slots = slots
	return order
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


// Two different powered, non-electrified rail vehicles that couple. The
// fixture uses the first as the collector's own locomotive and the second as
// the second player's vehicle.
function select_vehicles()
{
	local candidates = []
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.get_power() == 0) { continue }
		if (!desc.can_be_first() || !desc.can_be_last()) { continue }
		if (desc.needs_electrification()) { continue }
		if (desc.get_successors().len() > 0) { continue }
		candidates.append(desc)
	}
	foreach (a in candidates)
	{
		foreach (b in candidates)
		{
			if (!a.is_equal(b) && vehicle_desc_x.is_coupling_allowed(a, b))
			{
				return [a, b]
			}
		}
	}
	foreach (a in candidates)
	{
		foreach (b in candidates)
		{
			if (vehicle_desc_x.is_coupling_allowed(a, b))
			{
				return [a, b]
			}
		}
	}
	return null
}


// Builds the first player's rail network. The layout is the same as the
// division fixture: a trunk with a passing loop at the junction and an east
// branch, so the donor can release its vehicle on the loop platform while the
// collector stops at the main platform of the same halt.
function build_first_player_infrastructure(pl)
{
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	if (!check(rail != null, "no rail way available")) { return false }

	if (!check_tool(command_x.build_way(pl, coord3d(1, 2, 0), coord3d(6, 2, 0), rail, false), "build trunk")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build east branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(6, 0, 0), rail, false), "build north branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 2, 0), coord3d(4, 3, 0), rail, false), "build loop west link")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(7, 3, 0), rail, false), "build loop")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(7, 3, 0), coord3d(7, 2, 0), rail, false), "build loop east link")) { return false }

	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depot_desc_for(wt_rail)), "build first player's depot")) { return false }
	local depot = depot_x(1, 2, 0)
	if (!check(depot != null, "first player's depot not found after building")) { return false }
	fixture.depot0 = depot

	local station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
	if (!check(station != null, "no rail station available")) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, TRUNK_POS, station.get_name()), "build trunk stop")) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, JUNCTION_POS, station.get_name()), "build junction stop")) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, JUNCTION_LOOP_POS, station.get_name()), "build junction loop stop")) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, EAST_POS, station.get_name()), "build east stop")) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, NORTH_POS, station.get_name()), "build north stop")) { return false }

	return true
}


// Creates the second player and gives it enough capital to build its branch,
// depot and vehicles. Player 0 grants the second player access to its ways and
// stops, so the donor can reach the junction.
function create_second_player(pl)
{
	local pl2 = player_x(2)
	if (!pl2.is_valid())
	{
		if (!check(world.create_player(2, 1), "could not create player 2")) { return false }
		pl2 = player_x(2)
	}
	if (!check(pl2.is_valid(), "player 2 is not valid after creation")) { return false }
	if (!check_tool(pl2.book_cash(10000000), "could not give player 2 working capital")) { return false }
	if (!check_tool(pl.set_allow_access_to(pl2, true), "player 0 could not grant player 2 access")) { return false }
	return true
}


function build_second_player_infrastructure(pl2)
{
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	if (!check(rail != null, "no rail way available for player 2")) { return false }

	if (!check_tool(command_x.build_way(pl2, SECOND_DEPOT_POS, SECOND_BRANCH_END, rail, false), "build player 2's branch")) { return false }
	if (!check_tool(command_x.build_way(pl2, SECOND_BRANCH_END, SECOND_CONNECTOR_POS, rail, false), "connect player 2's branch to the trunk")) { return false }
	if (!check_tool(command_x.build_depot(pl2, SECOND_DEPOT_POS, depot_desc_for(wt_rail)), "build player 2's depot")) { return false }
	local depot = depot_x(1, 1, 0)
	if (!check(depot != null, "player 2's depot not found after building")) { return false }
	fixture.depot2 = depot
	return true
}


function create_depot_convoy(depot, pl, descriptors, what)
{
	local before = convoy_ids()
	if (!check_tool(depot.append_vehicle(pl, convoy_x(0), descriptors[0]), "add the first vehicle of the " + what)) { return null }
	local cnv = find_new_convoy(before)
	if (!check(cnv != null, "the " + what + " was not found after adding its first vehicle")) { return null }

	for (local i = 1; i < descriptors.len(); i++)
	{
		if (!check_tool(depot.append_vehicle(pl, cnv, descriptors[i]), "add vehicle " + (i + 1) + " of the " + what)) { return null }
	}

	local vehicles = cnv.get_vehicles()
	if (!check(vehicles.len() == descriptors.len(), "the " + what + " has " + vehicles.len() + " vehicles, expected " + descriptors.len())) { return null }
	for (local i = 0; i < descriptors.len(); i++)
	{
		if (!check(vehicle_matches(vehicles[i], descriptors[i]), "the " + what + " has a different vehicle descriptor at slot " + i)) { return null }
	}
	return cnv
}


function laid_over_carrier(expected)
{
	local halt = halt_x.get_halt(JUNCTION_POS, player_x(0))
	if (halt == null)
	{
		return null
	}
	foreach (loose in halt.get_laid_over_list())
	{
		if (!loose.is_valid()) { continue }
		foreach (desc in loose.get_vehicles())
		{
			if (vehicle_matches(desc, expected))
			{
				return loose
			}
		}
	}
	return null
}


function setup()
{
	local pl = player_x(0)

	if (!build_first_player_infrastructure(pl)) { return false }
	if (!create_second_player(pl)) { return false }
	local pl2 = player_x(2)
	if (!build_second_player_infrastructure(pl2)) { return false }

	local pair = select_vehicles()
	if (!check(pair != null, "no pair of coupling, non-electrified rail vehicles is available")) { return false }
	fixture.retained = pair[0]
	fixture.foreign = pair[1]

	// The donor belongs to player 2. Its order at the loop platform retains its
	// locomotive and releases the foreign vehicle into the stop's pool.
	local donor = create_depot_convoy(fixture.depot2, pl2, [fixture.retained, fixture.foreign], "foreign donor")
	if (donor == null) { return false }
	fixture.donor_id = donor.id

	local donor_schedule = schedule_x(wt_rail, [make_entry(JUNCTION_LOOP_POS, DONOR_POOL_ENTRY_ID), make_entry(EAST_POS, DONOR_NEXT_ENTRY_ID)])
	local donor_order = make_order([fixture.retained])
	// Any collector may take the released vehicle; the ownership rule, not the
	// recipient setting, is the subject of this case.
	donor_order.available_to_flags = 0
	donor_schedule.orders.rawset(DONOR_POOL_ENTRY_ID, donor_order)
	if (!check_tool(donor.change_schedule(pl2, donor_schedule), "commit the foreign donor's schedule")) { return false }

	// The collector belongs to player 0. Its order at the junction needs its own
	// locomotive and the foreign vehicle from the pool.
	local collector = create_depot_convoy(fixture.depot0, pl, [fixture.retained], "collector")
	if (collector == null) { return false }
	fixture.collector_id = collector.id

	local collector_schedule = schedule_x(wt_rail, [make_entry(JUNCTION_POS, COLLECTOR_ENTRY_ID), make_entry(EAST_POS, COLLECTOR_NEXT_ENTRY_ID)])
	collector_schedule.orders.rawset(COLLECTOR_ENTRY_ID, make_order([fixture.retained, fixture.foreign]))
	if (!check_tool(collector.change_schedule(pl, collector_schedule), "commit the collector's schedule")) { return false }

	if (!check_tool(fixture.depot2.start_convoy(pl2, donor), "start the foreign donor")) { return false }
	fixture.phase = "waiting-for-pool"
	return true
}


function junction_progress(what)
{
	++fixture.travel_steps
	if ((fixture.travel_steps % 200) == 0)
	{
		print("RUNTIME CASE " + case_name + ": heartbeat step " + heartbeat_steps + " (" + what + ")")
	}
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		fail("timed out waiting for " + what)
	}
}


function observe()
{
	if (fixture.phase == "waiting-for-pool")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the foreign donor did not leave its vehicle in the junction laid-over list within " + PREPARATION_STEP_LIMIT + " steps")
			return
		}
		local carrier = laid_over_carrier(fixture.foreign)
		if (carrier == null)
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": foreign vehicle laid over at the junction in convoy " + carrier.id)
		local donor = convoy_x(fixture.donor_id)
		if (!check(donor.is_valid(), "the foreign donor is no longer valid before it could be removed")) { return }
		if (!check_tool(donor.destroy(player_x(2)), "remove the foreign donor after it released the vehicle")) { return }
		fixture.phase = "waiting-for-donor-removal"
		return
	}

	if (fixture.phase == "waiting-for-donor-removal")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the foreign donor did not disappear within " + PREPARATION_STEP_LIMIT + " steps")
			return
		}
		if (convoy_x(fixture.donor_id).is_valid())
		{
			return
		}
		fixture.donor_removed = true
		if (case_name == "ownership-denied")
		{
			// The foreign vehicle is already in the pool. Withdraw the
			// permission that let the donor reach the junction, so the
			// collector's final consist would need it to take a vehicle whose
			// owner is no longer permitted.
			if (!check_tool(player_x(0).set_allow_access_to(player_x(2), false), "player 0 could not withdraw player 2's access")) { return }
			fixture.access_revoked = true
		}
		fixture.phase = "starting-collector"
		return
	}

	if (fixture.phase == "starting-collector")
	{
		local collector = convoy_x(fixture.collector_id)
		if (!check(collector.is_valid(), "the collector is no longer valid before it could start")) { return }
		if (!check_tool(fixture.depot0.start_convoy(player_x(0), collector), "start the collector")) { return }
		fixture.collector_started = true
		fixture.travel_steps = 0
		fixture.phase = "observing"
		return
	}

	local collector = convoy_x(fixture.collector_id)
	if (!collector.is_valid())
	{
		fail("the collector disappeared before its junction state could be observed")
		return
	}

	// The junction order runs on the first loading pass at the junction. The
	// collector has finished that pass once its current entry has advanced to
	// the east entry.
	if (collector.get_current_entry_id() != COLLECTOR_NEXT_ENTRY_ID)
	{
		junction_progress("the collector to pass the junction")
		return
	}

	local vehicles = collector.get_vehicles()
	local slots = collector.get_vehicle_snapshot()
	local foreign_aboard = false
	foreach (slot in slots)
	{
		if (vehicle_matches(slot.desc, fixture.foreign))
		{
			foreign_aboard = true
		}
	}

	if (case_name == "ownership-allowed")
	{
		if (!check(fixture.donor_removed, "the foreign donor was not removed before the collector arrived")) { return }
		if (!check(vehicles.len() == 2, "the collector has " + vehicles.len() + " vehicles, expected 2 (its own locomotive and the foreign vehicle)")) { return }
		if (!check(foreign_aboard, "the collector did not take the foreign vehicle from the pool")) { return }
		if (!check(slots[1].owner != null && slots[1].owner.get_name() == player_x(2).get_name(), "the collected vehicle is not owned by player 2")) { return }
		if (!check(laid_over_carrier(fixture.foreign) == null, "the foreign vehicle is still in the junction laid-over list after collection")) { return }
	}
	else
	{
		if (!check(fixture.access_revoked, "the denied case did not withdraw player 2's access")) { return }
		if (!check(vehicles.len() == 1, "the collector has " + vehicles.len() + " vehicles, expected only its own locomotive after the order was refused")) { return }
		if (!check(!foreign_aboard, "the collector took the foreign vehicle without permission")) { return }
		if (!check(laid_over_carrier(fixture.foreign) != null, "the foreign vehicle left the junction laid-over list even though the order was refused")) { return }
	}

	pass()
}


//
// Required scenario callbacks
//

function get_rule_text(pl)
{
	return ttext("Do not touch.")
}


function get_goal_text(pl)
{
	return ttext("Run the selected ownership case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Consist recombination ownership case {name}: {phase}")
	msg.name = case_name
	msg.phase = fixture.phase
	return msg
}


function get_result_text(pl)
{
	return get_info_text(pl)
}


function resume_game()
{
	// The ownership cases do not use the reload runner.
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
		print("RUNTIME CASE " + case_name + ": setup complete; donor convoy " + fixture.donor_id + ", collector convoy " + fixture.collector_id)
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
