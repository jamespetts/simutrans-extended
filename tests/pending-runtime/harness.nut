//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Shared world builder and assertions for the scheduled division (Y) runtime
// cases under tests/pending-runtime.
//
// A case selects itself by setting ::case_kind before including this file. The
// runner scripts/run-pending-runtime-tests.sh stages the case's scenario.nut,
// this file and empty-16x16.sve in the pakset scenario directory and runs one
// game process per case, so every case starts from the empty world. The reload
// case is run by scripts/run-pending-runtime-reload-tests.sh instead, which
// also joins a network client to the running case and loads the save written
// by that join in a further, offline process.
//
// Track runs east from the depot at (1,2) through the trunk stop (3,2) and the
// junction stop (5,2) to the east stop (9,2). A branch leaves the trunk at
// (6,2) for the north stop (6,0). A passing loop from (4,2) through the loop
// platform (5,3) to (7,2) gives the junction a second platform on a separate
// track, so a vehicle laid over on one platform does not block the other.
// Target schedules use the junction and north stops.
//
// The source convoy leaves the depot and runs its schedule. At the entry that
// carries the uncouple flag, convoi_t::hat_gehalten runs the source entry's
// consist order and, when the target resolves, forms the split-off portion
// from the vehicles that order displaced. Every observation below uses only
// the public script interface.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Y division runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.3"

local case_name = ::case_kind

local TRUNK_POS = coord3d(3, 2, 0)
local JUNCTION_POS = coord3d(5, 2, 0)
local JUNCTION_LOOP_POS = coord3d(5, 3, 0)
local EAST_POS = coord3d(9, 2, 0)
local NORTH_POS = coord3d(6, 0, 0)

local TRUNK_ENTRY_ID = 11
local UNCOUPLE_ENTRY_ID = 12
local EAST_ENTRY_ID = 13

local TARGET_ENTRY_ID = 21
local NEXT_TARGET_ENTRY_ID = 22
local PREVIOUS_TARGET_ENTRY_ID = 20

// Entry identities of the separate convoy that releases the pool vehicle in the
// pool-collection case. Its schedule is independent of the source and target
// schedules, so its identities need only be unique within that schedule.
local POOL_RELEASE_ENTRY_ID = 31
local POOL_EAST_ENTRY_ID = 32

// A source entry that returns to the junction after an intermediate stop needs
// a wider travel budget than a direct run. The value is an observation step
// count, not a game-time quantity.
local TRAVEL_STEP_LIMIT = 6000

// The division runs during the first loading pass at the junction stop, so it
// appears within a few observation steps of the source entering LOADING there.
// The wait-lock duration depends on the shunting_time_seconds setting, whose
// tick count depends on the frame rate, so the limit is a wide margin used only
// to report a missing division before the runner's overall timeout.
local JUNCTION_STEP_LIMIT = 300

// The pool-collection case prepares its laid-over vehicle with a separate
// convoy before it starts the source. The limit covers that convoy's run to
// the junction and the removal of the convoy afterwards.
local PREPARATION_STEP_LIMIT = 3000

// After the handover the split-off portion waits out the shunting delay before
// its schedule advances. This limit is a wide margin above the observation
// count that delay can produce and is small enough to report a portion that
// never advances.
local DEPARTURE_STEP_LIMIT = 1000

// The reload case holds the split-off portion at target entry 21 until the
// runner has joined a network client and the saved state has been loaded
// again. scripts/run-pending-runtime-reload-tests.sh sets
// shunting_time_seconds to 600, and the fixture's pakset (pak128.Britain-Ex)
// sets meters_per_tile to 125, so seconds_to_ticks (simunits.h) makes the
// shunting delay 109267 simulation ticks. With the runner's 100 frames per
// second and four frames per simulation step the network server advances 40
// simulation ticks per step, so the delay lasts about 109 real seconds and
// produces about 2700 scenario observations. The larger limit covers that wait
// plus the advance to the next target entry.
local RELOAD_DEPARTURE_STEP_LIMIT = 8000

local fixture = {
	phase = "waiting-for-setup",
	setup_started = false,
	completed = false,
	depot = null,
	convoy_id = 0,
	portion_id = 0,
	target_line_id = 0,
	target_convoy_id = 0,
	broken_line_id = 0,
	release_convoy_id = 0,
	release_convoy_removed = false,
	retained = null,
	split = null,
	pool = null,
	uncouple_entry_id = UNCOUPLE_ENTRY_ID,
	uncouple_index = 1,
	reached_junction = false,
	pool_prepared = false,
	pool_donor_id = 0,
	saw_last_entry_forward = false,
	loose_stock_seen = false,
	waiting_for = "",
	travel_steps = 0,
	junction_steps = 0,
	preparation_steps = 0,
	departure_steps = 0
}


function fail(what)
{
	if (!fixture.completed)
	{
		fixture.completed = true
		print("RUNTIME CASE " + case_name + " FAIL: " + what)
		if (case_name == "forward")
		{
			print("Y DIVISION FAIL: " + what)
		}
		print("RUNTIME CASE " + case_name + " COMPLETE")
		if (case_name == "forward")
		{
			print("Y DIVISION COMPLETE")
		}
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
		if (case_name == "forward")
		{
			print("Y DIVISION PASS")
		}
		print("RUNTIME CASE " + case_name + " COMPLETE")
		if (case_name == "forward")
		{
			print("Y DIVISION COMPLETE")
		}
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
// while the init-phase command wrappers (schedule, depot and line changes)
// return true. A failure is raised as a script error, or returned as an error
// string by a work call. Both success values are accepted here, and the
// committed state is read back below, where a rejected change shows.
function check_tool(result, what)
{
	if (result == null || result == true)
	{
		return true
	}
	return fail(what + ": " + result)
}


function line_ids(pl)
{
	local ids = []
	foreach (line in pl.get_line_list())
	{
		ids.append(line.id)
	}
	return ids
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


function find_new_line(pl, before)
{
	foreach (line in pl.get_line_list())
	{
		if (before.find(line.id) == null)
		{
			return line
		}
	}
	return null
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


// True when both references identify the same vehicle descriptor. The script
// API's vehicle_desc_x.get_name() is not reliable for identification here, so
// descriptor identity is compared with is_equal().
function vehicle_matches(desc, expected)
{
	return desc != null && expected != null && desc.is_equal(expected)
}


function convoy_has_no_line(cnv)
{
	local line = cnv.get_line()
	return line == null || !line.is_valid()
}


function source_convoy()
{
	return convoy_x(fixture.convoy_id)
}


// Returns [retained, split-off] vehicle descriptors, or null. Both vehicles
// must have nonzero get_power() and must satisfy can_be_first() and
// can_be_last(), so that each can form a one-vehicle convoy. They must not
// require electrification because the fixture does not choose its way
// descriptor by electrification. Descriptors with a fixed successor are
// excluded because depot.append_vehicle adds the successor as well, which
// would change the consist the case authors. Two different descriptors are
// preferred so that the portions can be distinguished by descriptor identity;
// two examples of one descriptor are accepted, in which case the portions can
// be distinguished by convoy identity, line membership and vehicle count.
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
		if (vehicle_desc_x.is_coupling_allowed(a, a))
		{
			return [a, a]
		}
	}
	return null
}


// A vehicle with zero power that couples behind leader. Descriptors with a
// fixed successor are excluded because depot.append_vehicle adds the successor
// as well, which would change the consist the case authors.
function select_unpowered_follower(leader)
{
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.get_power() != 0) { continue }
		if (desc.needs_electrification()) { continue }
		if (!desc.can_be_last()) { continue }
		if (desc.get_successors().len() > 0) { continue }
		if (!vehicle_desc_x.is_coupling_allowed(leader, desc)) { continue }
		return desc
	}
	return null
}


// Any rail vehicle that couples behind leader. Reusing any descriptor,
// including the leader's own descriptor, is permitted: each vehicle is a
// separate instance, and the donor convoy's distinct identity records which
// instance was released.
function select_follower(leader)
{
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.needs_electrification()) { continue }
		if (!desc.can_be_last()) { continue }
		if (desc.get_successors().len() > 0) { continue }
		if (!vehicle_desc_x.is_coupling_allowed(leader, desc)) { continue }
		return desc
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


function station_builder(pl, pos, desc)
{
	return command_x(tool_build_station).work(pl, pos, desc.get_name())
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


function build_infrastructure(pl)
{
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	if (!check(rail != null, "no rail way available")) { return false }

	// Trunk (1,2) -> (6,2); the N schedule continues east (6,2) -> (9,2);
	// the P schedule continues north (6,2) -> (6,0). The passing loop
	// (4,2) -> (4,3) -> (7,3) -> (7,2) gives the junction two platforms on
	// separate tracks, so a vehicle laid over on the loop platform does not
	// block a convoy that stops at the main platform.
	if (!check_tool(command_x.build_way(pl, coord3d(1, 2, 0), coord3d(6, 2, 0), rail, false), "build trunk")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build N branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(6, 0, 0), rail, false), "build P branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 2, 0), coord3d(4, 3, 0), rail, false), "build loop west link")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(7, 3, 0), rail, false), "build loop")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(7, 3, 0), coord3d(7, 2, 0), rail, false), "build loop east link")) { return false }

	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depot_desc_for(wt_rail)), "build rail depot")) { return false }
	local depot = depot_x(1, 2, 0)
	if (!check(depot != null, "depot not found after building")) { return false }
	fixture.depot = depot

	local station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
	if (!check(station != null, "no rail station available")) { return false }
	if (!check_tool(station_builder(pl, TRUNK_POS, station), "build trunk stop")) { return false }
	if (!check_tool(station_builder(pl, JUNCTION_POS, station), "build junction stop")) { return false }
	if (!check_tool(station_builder(pl, JUNCTION_LOOP_POS, station), "build junction loop stop")) { return false }
	if (!check_tool(station_builder(pl, EAST_POS, station), "build east stop")) { return false }
	if (!check_tool(station_builder(pl, NORTH_POS, station), "build north stop")) { return false }

	return true
}


// Creates one depot convoy from the given descriptors in order and reads the
// resulting consist back. Returns the convoy handle, or null on failure.
function create_depot_convoy(pl, descriptors, what)
{
	local before = convoy_ids()
	if (!check_tool(fixture.depot.append_vehicle(pl, convoy_x(0), descriptors[0]), "add the first vehicle of the " + what)) { return null }
	local cnv = find_new_convoy(before)
	if (!check(cnv != null, "the " + what + " was not found after adding its first vehicle")) { return null }

	for (local i = 1; i < descriptors.len(); i++)
	{
		if (!check_tool(fixture.depot.append_vehicle(pl, cnv, descriptors[i]), "add vehicle " + (i + 1) + " of the " + what)) { return null }
	}

	local vehicles = cnv.get_vehicles()
	if (!check(vehicles.len() == descriptors.len(), "the " + what + " has " + vehicles.len() + " vehicles, expected " + descriptors.len())) { return null }
	for (local i = 0; i < descriptors.len(); i++)
	{
		if (!check(vehicle_matches(vehicles[i], descriptors[i]), "the " + what + " has a different vehicle descriptor at slot " + i)) { return null }
	}
	return cnv
}


function source_consist_descriptors()
{
	return [fixture.retained, fixture.split]
}


function needs_target_line()
{
	return case_name != "plain-uncouple" && case_name != "lineless-target"
}


function expected_target_entry_ids()
{
	if (case_name == "reversed-target")
	{
		return [PREVIOUS_TARGET_ENTRY_ID, TARGET_ENTRY_ID]
	}
	return [TARGET_ENTRY_ID, NEXT_TARGET_ENTRY_ID]
}


function target_order_descriptors()
{
	if (case_name == "unsatisfiable-target")
	{
		// Two slots require two vehicles of this descriptor, but the source
		// supplies only one. The order therefore cannot be completed without
		// requiring another descriptor that the pakset may not provide.
		return [fixture.split, fixture.split]
	}
	if (case_name == "pool-collection")
	{
		return [fixture.split, fixture.pool]
	}
	return [fixture.split]
}


function check_target_line_readback()
{
	local line = line_x(fixture.target_line_id)
	if (!check(line.is_valid(), "target line P no longer exists after its schedule was committed")) { return false }
	local schedule = line.get_schedule()
	if (!check(schedule != null, "target line P has no schedule")) { return false }

	local ids = expected_target_entry_ids()
	if (!check(schedule.entries.len() == ids.len(), "target line P schedule has " + schedule.entries.len() + " entries, expected " + ids.len())) { return false }
	for (local i = 0; i < ids.len(); i++)
	{
		if (!check(schedule.entries[i].unique_entry_id == ids[i], "target line P schedule entry " + i + " has id " + schedule.entries[i].unique_entry_id + ", expected " + ids[i])) { return false }
	}

	if (case_name == "target-no-order")
	{
		if (!check(!(TARGET_ENTRY_ID in schedule.orders), "target line P entry " + TARGET_ENTRY_ID + " has a consist order; this case requires it to have none")) { return false }
		return true
	}

	if (!check(TARGET_ENTRY_ID in schedule.orders, "target line P schedule has no order at entry " + TARGET_ENTRY_ID)) { return false }
	local order = schedule.orders[TARGET_ENTRY_ID]
	local expected = target_order_descriptors()
	if (!check(order.slots.len() == expected.len(), "target line P order at entry " + TARGET_ENTRY_ID + " has " + order.slots.len() + " slots, expected " + expected.len())) { return false }
	for (local i = 0; i < expected.len(); i++)
	{
		if (!check(order.slots[i].alternatives.len() == 1, "target line P order slot " + i + " has " + order.slots[i].alternatives.len() + " alternatives, expected 1")) { return false }
		local desc = order.slots[i].alternatives[0].specific_vehicle
		if (!check(vehicle_matches(desc, expected[i]), "target line P order slot " + i + " does not match the expected vehicle descriptor")) { return false }
	}
	return true
}


function create_target_line(pl)
{
	local before = line_ids(pl)
	if (!check(pl.create_line(wt_rail), "create target line P")) { return false }
	local line = find_new_line(pl, before)
	if (!check(line != null, "target line P not found after creation")) { return false }
	fixture.target_line_id = line.id

	local entries = []
	if (case_name == "reversed-target")
	{
		entries.append(make_entry(NORTH_POS, PREVIOUS_TARGET_ENTRY_ID))
		entries.append(make_entry(JUNCTION_POS, TARGET_ENTRY_ID))
	}
	else
	{
		entries.append(make_entry(JUNCTION_POS, TARGET_ENTRY_ID))
		entries.append(make_entry(NORTH_POS, NEXT_TARGET_ENTRY_ID))
	}
	local schedule = schedule_x(wt_rail, entries)
	if (case_name != "target-no-order")
	{
		schedule.orders.rawset(TARGET_ENTRY_ID, make_order(target_order_descriptors()))
	}

	if (!check_tool(line.change_schedule(pl, schedule), "commit target line P schedule")) { return false }
	return check_target_line_readback()
}


// Creates a line and destroys it immediately. The source schedule then names
// the destroyed line's id, which no object binds, so the target is broken.
function destroy_broken_line(pl)
{
	local before = line_ids(pl)
	if (!check(pl.create_line(wt_rail), "create the line used as the broken target")) { return false }
	local line = find_new_line(pl, before)
	if (!check(line != null, "line used as the broken target not found after creation")) { return false }
	fixture.broken_line_id = line.id
	if (!check_tool(line.destroy(pl), "destroy the line used as the broken target")) { return false }
	if (!check(!line_x(fixture.broken_line_id).is_valid(), "the line used as the broken target is still valid after destroying it")) { return false }
	return true
}


function create_target_convoy(pl)
{
	local cnv = create_depot_convoy(pl, [fixture.retained], "lineless target convoy")
	if (cnv == null) { return false }
	fixture.target_convoy_id = cnv.id

	local schedule = schedule_x(wt_rail, [make_entry(JUNCTION_POS, TARGET_ENTRY_ID), make_entry(NORTH_POS, NEXT_TARGET_ENTRY_ID)])
	schedule.orders.rawset(TARGET_ENTRY_ID, make_order([fixture.split]))
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the lineless target convoy schedule")) { return false }

	if (!check(cnv.is_valid(), "the lineless target convoy is no longer valid after its schedule was committed")) { return false }
	if (!check(convoy_has_no_line(cnv), "the lineless target convoy is assigned to a line")) { return false }
	if (!check(cnv.is_in_depot(), "the lineless target convoy left the depot, but this case requires it to remain there")) { return false }

	local back = cnv.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the lineless target convoy has no two-entry schedule")) { return false }
	if (!check(back.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the lineless target convoy schedule has no entry " + TARGET_ENTRY_ID + " first")) { return false }
	if (!check(back.entries[1].unique_entry_id == NEXT_TARGET_ENTRY_ID, "the lineless target convoy schedule has no entry " + NEXT_TARGET_ENTRY_ID + " second")) { return false }
	if (!check(TARGET_ENTRY_ID in back.orders, "the lineless target convoy has no order at entry " + TARGET_ENTRY_ID)) { return false }
	local order = back.orders[TARGET_ENTRY_ID]
	if (!check(order.slots.len() == 1, "the lineless target convoy order has " + order.slots.len() + " slots, expected 1")) { return false }
	local desc = order.slots[0].alternatives[0].specific_vehicle
	if (!check(vehicle_matches(desc, fixture.split), "the lineless target convoy order does not match the displaced vehicle descriptor")) { return false }
	return true
}


// Creates the separate convoy that releases the pool vehicle at the junction
// in the pool-collection case. Its first entry retains its leading vehicle and
// displaces the pool vehicle, which becomes loose stock at the junction. The
// convoy is then started; observe_pool_collection() removes it once the pool
// vehicle is laid over.
function create_pool_donor(pl)
{
	local cnv = create_depot_convoy(pl, [fixture.retained, fixture.pool], "pool release convoy")
	if (cnv == null) { return false }
	fixture.release_convoy_id = cnv.id

	local schedule = schedule_x(wt_rail, [make_entry(JUNCTION_LOOP_POS, POOL_RELEASE_ENTRY_ID), make_entry(EAST_POS, POOL_EAST_ENTRY_ID)])
	schedule.orders.rawset(POOL_RELEASE_ENTRY_ID, make_order([fixture.retained]))
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the pool release convoy schedule")) { return false }

	local back = cnv.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the pool release convoy has no two-entry schedule")) { return false }
	if (!check(back.entries[0].unique_entry_id == POOL_RELEASE_ENTRY_ID, "the pool release convoy schedule has no entry " + POOL_RELEASE_ENTRY_ID + " first")) { return false }
	if (!check(back.entries[1].unique_entry_id == POOL_EAST_ENTRY_ID, "the pool release convoy schedule has no entry " + POOL_EAST_ENTRY_ID + " second")) { return false }
	if (!check(POOL_RELEASE_ENTRY_ID in back.orders, "the pool release convoy has no order at entry " + POOL_RELEASE_ENTRY_ID)) { return false }
	local order = back.orders[POOL_RELEASE_ENTRY_ID]
	if (!check(order.slots.len() == 1, "the pool release convoy order has " + order.slots.len() + " slots, expected 1")) { return false }
	local desc = order.slots[0].alternatives[0].specific_vehicle
	if (!check(vehicle_matches(desc, fixture.retained), "the pool release convoy order does not match the retained vehicle descriptor")) { return false }

	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the pool release convoy")) { return false }
	return true
}


function source_entry_ids()
{
	if (case_name == "reversed-source")
	{
		return [UNCOUPLE_ENTRY_ID, EAST_ENTRY_ID]
	}
	return [TRUNK_ENTRY_ID, UNCOUPLE_ENTRY_ID, EAST_ENTRY_ID]
}


function source_target_id()
{
	if (case_name == "plain-uncouple") { return 0 }
	if (case_name == "broken-target") { return fixture.broken_line_id }
	if (case_name == "lineless-target") { return fixture.target_convoy_id }
	return fixture.target_line_id
}


function source_target_entry()
{
	if (case_name == "plain-uncouple") { return 0 }
	return TARGET_ENTRY_ID
}


function build_source_entries()
{
	local entries = []
	if (case_name == "reversed-source")
	{
		entries.append(make_entry(JUNCTION_POS, UNCOUPLE_ENTRY_ID))
		entries.append(make_entry(EAST_POS, EAST_ENTRY_ID))
	}
	else
	{
		entries.append(make_entry(TRUNK_POS, TRUNK_ENTRY_ID))
		entries.append(make_entry(JUNCTION_POS, UNCOUPLE_ENTRY_ID))
		entries.append(make_entry(EAST_POS, EAST_ENTRY_ID))
	}
	return entries
}


function commit_source_schedule(pl, cnv)
{
	local entries = build_source_entries()
	local uncouple_entry = entries[fixture.uncouple_index]
	uncouple_entry.uncouple = true
	uncouple_entry.uncouple_target_is_line_or_cnv = (case_name == "lineless-target")
	uncouple_entry.uncouple_target_sch_is_reversed = (case_name == "reversed-target")
	uncouple_entry.target_id_uncouple = source_target_id()
	uncouple_entry.target_unique_entry_uncouple = source_target_entry()

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(fixture.uncouple_entry_id, make_order([fixture.retained]))
	if (case_name == "reversed-source")
	{
		// The convoy starts at the last entry. On departing that entry, the
		// mirrored schedule selects reverse traversal, so the next entry visited
		// is the junction uncouple entry.
		schedule.mirrored = true
		schedule.current_stop = 1
	}

	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the source convoy schedule")) { return false }

	local back = cnv.get_schedule()
	local ids = source_entry_ids()
	if (!check(back != null && back.entries.len() == ids.len(), "the source convoy schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected " + ids.len())) { return false }
	for (local i = 0; i < ids.len(); i++)
	{
		if (!check(back.entries[i].unique_entry_id == ids[i], "source schedule entry " + i + " has id " + back.entries[i].unique_entry_id + ", expected " + ids[i])) { return false }
	}

	local live_uncouple = back.entries[fixture.uncouple_index]
	if (!check(live_uncouple.uncouple == true, "source schedule read-back: the uncouple flag is not set at entry " + fixture.uncouple_entry_id)) { return false }
	if (!check(live_uncouple.target_id_uncouple == source_target_id(), "source schedule read-back: target id is " + live_uncouple.target_id_uncouple + ", expected " + source_target_id())) { return false }
	if (!check(live_uncouple.target_unique_entry_uncouple == source_target_entry(), "source schedule read-back: target entry is " + live_uncouple.target_unique_entry_uncouple + ", expected " + source_target_entry())) { return false }
	if (!check(live_uncouple.uncouple_target_is_line_or_cnv == (case_name == "lineless-target"), "source schedule read-back: target kind flag is wrong at entry " + fixture.uncouple_entry_id)) { return false }
	if (!check(live_uncouple.uncouple_target_sch_is_reversed == (case_name == "reversed-target"), "source schedule read-back: target reversal flag is wrong at entry " + fixture.uncouple_entry_id)) { return false }

	if (!check(fixture.uncouple_entry_id in back.orders, "source schedule read-back: no order at entry " + fixture.uncouple_entry_id)) { return false }
	local order = back.orders[fixture.uncouple_entry_id]
	if (!check(order.slots.len() == 1, "source schedule read-back: order at entry " + fixture.uncouple_entry_id + " has " + order.slots.len() + " slots, expected 1")) { return false }
	local alt = order.slots[0].alternatives[0].specific_vehicle
	if (!check(vehicle_matches(alt, fixture.retained), "source schedule read-back: the order at entry " + fixture.uncouple_entry_id + " does not match the retained vehicle descriptor")) { return false }

	if (case_name == "reversed-source")
	{
		if (!check(back.mirrored == true, "source schedule read-back: the source schedule is not mirrored")) { return false }
		if (!check(back.current_stop == 1, "source schedule read-back: current_stop is " + back.current_stop + ", expected 1")) { return false }
	}

	return true
}


function setup()
{
	local pl = player_x(0)

	if (!build_infrastructure(pl)) { return false }

	if (case_name == "pool-collection")
	{
		// The pool vehicle is laid over on the loop platform; the source stops
		// at the main platform. Both platform tiles must belong to one halt, or
		// the source's consist order cannot see the laid-over vehicle.
		local main_halt = halt_x.get_halt(JUNCTION_POS, pl)
		local loop_halt = halt_x.get_halt(JUNCTION_LOOP_POS, pl)
		if (!check(main_halt != null && loop_halt != null, "the junction platforms have no halt")) { return false }
		if (!check(main_halt.get_name() == loop_halt.get_name(), "the junction main and loop platforms have different stop names, so they are separate halts and the source cannot see the laid-over pool vehicle")) { return false }
	}

	local pair = select_vehicles()
	if (!check(pair != null, "no pair of independently powered rail vehicles that can run on non-electrified track is available")) { return false }
	fixture.retained = pair[0]

	if (case_name == "unpowered-target")
	{
		fixture.split = select_unpowered_follower(fixture.retained)
		if (!check(fixture.split != null, "no unpowered rail vehicle that can be coupled behind the retained vehicle is available")) { return false }
		if (!check(fixture.split.get_power() == 0, "the selected displaced vehicle is powered; this case requires zero power")) { return false }
	}
	else
	{
		fixture.split = pair[1]
	}

	if (case_name == "pool-collection")
	{
		fixture.pool = select_follower(fixture.split)
		if (!check(fixture.pool != null, "no rail vehicle that can be coupled behind the displaced vehicle is available for the laid-over pool")) { return false }
	}

	fixture.uncouple_entry_id = UNCOUPLE_ENTRY_ID
	if (case_name == "reversed-source")
	{
		fixture.uncouple_index = 0
	}
	else
	{
		fixture.uncouple_index = 1
	}

	if (needs_target_line())
	{
		if (!create_target_line(pl)) { return false }
	}
	if (case_name == "broken-target")
	{
		if (!destroy_broken_line(pl)) { return false }
	}
	if (case_name == "lineless-target")
	{
		if (!create_target_convoy(pl)) { return false }
	}

	local cnv = create_depot_convoy(pl, source_consist_descriptors(), "source convoy")
	if (cnv == null) { return false }
	fixture.convoy_id = cnv.id

	if (!commit_source_schedule(pl, cnv)) { return false }

	if (case_name == "pool-collection")
	{
		// The source starts only after observe_pool_collection() has seen the
		// pool vehicle laid over, so the laid-over vehicle is still present when
		// the source reaches the junction.
		if (!create_pool_donor(pl)) { return false }
		return true
	}

	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the source convoy")) { return false }
	return true
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


function target_line_convoys()
{
	local convoys = []
	local line = line_x(fixture.target_line_id)
	if (!line.is_valid())
	{
		return convoys
	}
	foreach (cnv in line.get_convoy_list())
	{
		if (cnv.is_valid())
		{
			convoys.append(cnv)
		}
	}
	return convoys
}


function world_vehicle_total()
{
	local total = 0
	foreach (cnv in world.get_convoy_list())
	{
		if (!cnv.is_valid()) { continue }
		total += cnv.get_vehicles().len()
	}
	return total
}


// Returns the first convoy that carries vehicles and is not named in
// allowed_ids. Empty convoys are ignored: an emptied pool donor is left at the
// stop by the existing commit code and is not part of this case's assertion.
function find_populated_convoy_other_than(allowed_ids)
{
	foreach (cnv in world.get_convoy_list())
	{
		if (!cnv.is_valid()) { continue }
		if (cnv.get_vehicles().len() == 0) { continue }
		if (allowed_ids.find(cnv.id) == null)
		{
			return cnv
		}
	}
	return null
}


function travel_step()
{
	++fixture.travel_steps
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		local src = source_convoy()
		fail("timed out waiting for the source convoy to reach the uncouple entry: entry " + src.get_current_entry_id() + ", state " + src.get_state())
	}
}


function junction_progress(what)
{
	++fixture.junction_steps
	if (fixture.junction_steps > JUNCTION_STEP_LIMIT)
	{
		local src = source_convoy()
		local detail = fixture.loose_stock_seen ? "; the displaced vehicle was seen in the junction laid-over list" : ""
		fail("timed out waiting for " + what + " within " + JUNCTION_STEP_LIMIT + " steps at the uncouple entry (source entry " + src.get_current_entry_id() + ", state " + src.get_state() + ")" + detail)
	}
}


function departure_step(what)
{
	++fixture.departure_steps
	local limit = (case_name == "reload") ? RELOAD_DEPARTURE_STEP_LIMIT : DEPARTURE_STEP_LIMIT
	if (fixture.departure_steps > limit)
	{
		local portion = convoy_x(fixture.portion_id)
		fail(what + " did not advance within " + limit + " observation steps (entry " + portion.get_current_entry_id() + ", state " + portion.get_state() + ")")
	}
}


// Stores the identifiers of the divided state that survive a savegame load.
// The base scenario save() in simutrans/script/script_base.nut serialises the
// persistent table into the savegame, and scenario_t::rdwr evaluates the
// resulting root-table assignment before it calls resume_game() on loading.
// Only scalars are stored: vehicle descriptors, convoy handles and the depot
// are reconstructed from the loaded world in resume_game(). The reload case
// calls this once, when the forward handover has been asserted and the case is
// about to wait for the save to be reloaded.
function remember_reload_state()
{
	persistent.reload <- {
		kind = case_name,
		convoy_id = fixture.convoy_id,
		portion_id = fixture.portion_id,
		target_line_id = fixture.target_line_id
	}
}


// Performs one observation step of the common wait for the source convoy to
// reach the uncouple entry and enter its loading pass. Returns true when the
// source is in that state, so that the caller can run the junction assertion.
function source_waiting_at_uncouple()
{
	local src = source_convoy()
	local entry = src.get_current_entry_id()
	local state = src.get_state()
	if (entry != fixture.uncouple_entry_id)
	{
		if (fixture.reached_junction)
		{
			fail("the source convoy left entry " + fixture.uncouple_entry_id + " before " + fixture.waiting_for + ": now entry " + entry + ", state " + state)
			return false
		}
		travel_step()
		return false
	}
	if (!fixture.reached_junction)
	{
		fixture.reached_junction = true
		print("RUNTIME CASE " + case_name + ": source convoy reached the uncouple entry " + fixture.uncouple_entry_id)
	}
	if (state != convoy_state_loading)
	{
		travel_step()
		return false
	}
	return true
}


function check_forward_formation(portion)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the original source convoy is no longer valid")) { return false }
	if (!check(portion.is_valid(), "the split-off portion is no longer valid")) { return false }
	if (!check(portion.id != src.id, "the split-off portion has the source convoy's identity")) { return false }

	// Retained portion: schedule N, one vehicle, still at the split entry.
	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1, "the source convoy has " + source_vehicles.len() + " vehicles, expected 1")) { return false }
	if (!check(vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy has a different vehicle descriptor in its retained slot")) { return false }
	local source_slots = src.get_vehicle_snapshot()
	if (!check(source_slots.len() == 1, "the source convoy snapshot has " + source_slots.len() + " slots, expected 1")) { return false }
	if (!check(source_slots[0].index == 0, "the retained vehicle is at slot " + source_slots[0].index + ", expected slot 0")) { return false }
	if (!check(vehicle_matches(source_slots[0].desc, fixture.retained), "the source convoy snapshot has a different vehicle descriptor at slot 0")) { return false }

	local source_schedule = src.get_schedule()
	if (!check(source_schedule != null && source_schedule.entries.len() == 3, "the source convoy no longer runs schedule N")) { return false }
	if (!check(source_schedule.entries[1].unique_entry_id == UNCOUPLE_ENTRY_ID, "schedule N of the source convoy has no entry " + UNCOUPLE_ENTRY_ID)) { return false }
	if (!check(src.get_current_entry_id() == UNCOUPLE_ENTRY_ID, "the source convoy is at entry " + src.get_current_entry_id() + ", expected entry " + UNCOUPLE_ENTRY_ID)) { return false }
	if (!check(source_schedule.entries[1].target_id_uncouple == fixture.target_line_id, "the source entry " + UNCOUPLE_ENTRY_ID + " does not name target line P")) { return false }
	if (!check(source_schedule.entries[1].target_unique_entry_uncouple == TARGET_ENTRY_ID, "the source entry " + UNCOUPLE_ENTRY_ID + " does not name target entry " + TARGET_ENTRY_ID)) { return false }

	// Split-off portion: the only convoy of target line P, one vehicle,
	// scheduled at the target entry, in forward traversal.
	local line_p = line_x(fixture.target_line_id)
	if (!check(line_p.is_valid(), "target line P no longer exists")) { return false }
	local on_line = target_line_convoys()
	if (!check(on_line.len() == 1, "target line P has " + on_line.len() + " convoys, expected exactly 1")) { return false }
	if (!check(on_line[0].id == portion.id, "the unique convoy of target line P is not the inspected portion")) { return false }

	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the split-off portion is not assigned to target line P")) { return false }

	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1, "the split-off portion has " + portion_vehicles.len() + " vehicles, expected 1")) { return false }
	if (!check(vehicle_matches(portion_vehicles[0], fixture.split), "the split-off portion has a different vehicle descriptor in its only slot")) { return false }
	local portion_slots = portion.get_vehicle_snapshot()
	if (!check(portion_slots.len() == 1, "the split-off portion snapshot has " + portion_slots.len() + " slots, expected 1")) { return false }
	if (!check(portion_slots[0].index == 0, "the split-off vehicle is at slot " + portion_slots[0].index + ", expected slot 0")) { return false }
	if (!check(vehicle_matches(portion_slots[0].desc, fixture.split), "the split-off portion snapshot has a different vehicle descriptor at slot 0")) { return false }

	local portion_schedule = portion.get_schedule()
	if (!check(portion_schedule != null && portion_schedule.entries.len() == 2, "the split-off portion does not run the target schedule")) { return false }
	if (!check(portion_schedule.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the target schedule of the split-off portion has no entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.get_current_entry_id() == TARGET_ENTRY_ID, "the split-off portion is at entry " + portion.get_current_entry_id() + ", expected target entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.is_reverse_schedule() == false, "the split-off portion traverses the target schedule in reverse")) { return false }
	if (!check(TARGET_ENTRY_ID in portion_schedule.orders, "the target schedule of the split-off portion has no order at entry " + TARGET_ENTRY_ID)) { return false }
	local portion_order = portion_schedule.orders[TARGET_ENTRY_ID]
	if (!check(portion_order.slots.len() == 1, "the target schedule order has " + portion_order.slots.len() + " slots, expected 1")) { return false }
	local portion_order_desc = portion_order.slots[0].alternatives[0].specific_vehicle
	if (!check(vehicle_matches(portion_order_desc, fixture.split), "the target schedule order for the split-off portion does not match the split-off vehicle descriptor")) { return false }
	if (!check(portion.get_state() == convoy_state_loading, "the split-off portion is in state " + portion.get_state() + ", expected the loading state " + convoy_state_loading)) { return false }

	// No released vehicle may be left behind as loose stock at the junction.
	if (!check(laid_over_carrier(fixture.split) == null, "the split-off vehicle remains in the junction laid-over list as loose stock")) { return false }

	local total = world_vehicle_total()
	if (!check(total == 2, "the map holds " + total + " vehicles, expected the original two")) { return false }
	local unexpected = find_populated_convoy_other_than([src.id, portion.id])
	if (!check(unexpected == null, "the map holds an unexpected populated convoy in addition to the source and the portion")) { return false }

	return true
}


function observe_forward()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	// The reload case stops here after the first forward formation until the
	// runner has joined a network client and the saved state has been loaded
	// again. The portion must still be at the target entry then, because the
	// restored state is re-asserted at that entry.
	if (fixture.phase == "awaiting-reload")
	{
		local portion = convoy_x(fixture.portion_id)
		if (!portion.is_valid())
		{
			fail("the split-off portion disappeared before the saved state was reloaded")
			return
		}
		local entry = portion.get_current_entry_id()
		if (entry != TARGET_ENTRY_ID)
		{
			fail("the split-off portion advanced from target entry " + TARGET_ENTRY_ID + " to entry " + entry + " before the saved state was reloaded; shunting_time_seconds must outlast the client join")
			return
		}
		return
	}

	// After the load, resume_game() has reconstructed the fixture. The divided
	// state is re-asserted here, once, through the same public API checks as the
	// forward case, and the portion is then allowed to advance.
	if (fixture.phase == "restored-pending")
	{
		local portion = convoy_x(fixture.portion_id)
		if (!portion.is_valid())
		{
			fail("the split-off portion " + fixture.portion_id + " named by the saved state is not valid after the load")
			return
		}
		if (!check_forward_formation(portion))
		{
			return
		}
		fixture.phase = "post_formation"
		print("RUNTIME CASE " + case_name + " RESTORED")
		return
	}

	if (fixture.phase == "post_formation")
	{
		observe_forward_advance()
		return
	}

	local on_line = target_line_convoys()
	if (on_line.len() > 1)
	{
		fail("target line P has " + on_line.len() + " convoys; expected exactly one split-off portion")
		return
	}
	if (on_line.len() == 1)
	{
		if (check_forward_formation(on_line[0]))
		{
			fixture.portion_id = on_line[0].id
			if (case_name == "reload")
			{
				remember_reload_state()
				fixture.phase = "awaiting-reload"
				print("RUNTIME CASE " + case_name + " READY")
			}
			else
			{
				fixture.phase = "post_formation"
				print("RUNTIME CASE " + case_name + ": split-off portion formed at target entry " + TARGET_ENTRY_ID + "; observing departure")
			}
		}
		return
	}

	if (laid_over_carrier(fixture.split) != null)
	{
		fixture.loose_stock_seen = true
	}

	fixture.waiting_for = "a split-off portion on target line P"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


// Runs after check_forward_formation() has asserted the handover at target
// entry 21. The split-off portion must continue as an ordinary convoy: it
// stays valid, stays on target line P with one vehicle, and its schedule
// advances from the target entry to the next target entry. The advance proves
// that the new convoy takes part in the normal convoy step and that loading,
// departure and routing start; physical arrival at the next stop is not
// awaited. The source convoy must stay valid with its single retained vehicle,
// but it is free to continue along schedule N.
function observe_forward_advance()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared while the split-off portion was departing")
		return
	}
	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1 && vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy no longer holds exactly the retained vehicle descriptor")) { return }

	local portion = convoy_x(fixture.portion_id)
	if (!portion.is_valid())
	{
		fail("the split-off portion was destroyed before its schedule advanced from target entry " + TARGET_ENTRY_ID + " to entry " + NEXT_TARGET_ENTRY_ID)
		return
	}
	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the split-off portion left target line P before its schedule advanced to entry " + NEXT_TARGET_ENTRY_ID)) { return }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the split-off portion no longer holds exactly the split-off vehicle descriptor")) { return }

	local entry = portion.get_current_entry_id()
	if (entry == TARGET_ENTRY_ID)
	{
		departure_step("the split-off portion leaving target entry " + TARGET_ENTRY_ID)
		return
	}
	if (entry == NEXT_TARGET_ENTRY_ID)
	{
		if (!check(portion.is_reverse_schedule() == false, "the split-off portion traverses the target schedule in reverse after advancing")) { return }
		pass()
		return
	}
	fail("the split-off portion reached entry " + entry + " (state " + portion.get_state() + ") instead of advancing from target entry " + TARGET_ENTRY_ID + " to entry " + NEXT_TARGET_ENTRY_ID)
}


// The target did not resolve or did not produce a usable portion: the source
// keeps only the retained vehicle and the displaced vehicle stands at the
// junction as loose stock. The checks below require that positive end state;
// the case does not pass merely because no division appeared.
function check_loose_stock_end_state(loose)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the source convoy is no longer valid")) { return false }
	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1, "the source convoy has " + source_vehicles.len() + " vehicles after its order, expected 1")) { return false }
	if (!check(vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy no longer holds the retained vehicle descriptor")) { return false }
	if (!check(src.get_current_entry_id() == fixture.uncouple_entry_id, "the source convoy is at entry " + src.get_current_entry_id() + ", expected the uncouple entry " + fixture.uncouple_entry_id)) { return false }

	if (!check(loose.is_valid(), "the laid-over convoy carrying the displaced vehicle is no longer valid")) { return false }
	if (!check(loose.get_state() == convoy_state_layover, "the convoy carrying the displaced vehicle is in state " + loose.get_state() + ", expected the laid-over state " + convoy_state_layover)) { return false }
	if (!check(convoy_has_no_line(loose), "the convoy carrying the displaced vehicle is assigned to a line; it must remain loose stock")) { return false }
	local loose_vehicles = loose.get_vehicles()
	if (!check(loose_vehicles.len() == 1, "the laid-over convoy has " + loose_vehicles.len() + " vehicles, expected only the displaced vehicle")) { return false }
	if (!check(vehicle_matches(loose_vehicles[0], fixture.split), "the laid-over convoy holds a different vehicle descriptor from the displaced vehicle")) { return false }

	if (fixture.target_line_id != 0)
	{
		local line = line_x(fixture.target_line_id)
		if (!check(line.is_valid(), "target line P no longer exists")) { return false }
		local on_line = target_line_convoys()
		if (!check(on_line.len() == 0, "target line P has " + on_line.len() + " convoys, expected none: the target must not produce a portion")) { return false }
	}
	else
	{
		if (!check(line_ids(player_x(0)).len() == 0, "the plain-uncouple case created a line, but it must have none")) { return false }
	}

	if (case_name == "broken-target")
	{
		if (!check(!line_x(fixture.broken_line_id).is_valid(), "the broken target line " + fixture.broken_line_id + " is still valid")) { return false }
	}
	if (case_name == "target-no-order")
	{
		local line = line_x(fixture.target_line_id)
		local schedule = line.get_schedule()
		if (!check(schedule != null && !(TARGET_ENTRY_ID in schedule.orders), "target line P entry " + TARGET_ENTRY_ID + " has a consist order; this case requires it to have none")) { return false }
	}
	if (case_name == "unsatisfiable-target")
	{
		local line = line_x(fixture.target_line_id)
		local schedule = line.get_schedule()
		if (!check(schedule != null && TARGET_ENTRY_ID in schedule.orders, "target line P lost its unsatisfiable order at entry " + TARGET_ENTRY_ID)) { return false }
		local order = schedule.orders[TARGET_ENTRY_ID]
		if (!check(order.slots.len() == 2, "target line P entry " + TARGET_ENTRY_ID + " has " + order.slots.len() + " order slots, expected 2")) { return false }
	}

	local total = world_vehicle_total()
	if (!check(total == 2, "the map holds " + total + " vehicles, expected the retained vehicle and the displaced vehicle")) { return false }
	local unexpected = find_populated_convoy_other_than([src.id, loose.id])
	if (!check(unexpected == null, "the map holds an unexpected populated convoy in addition to the source and the loose convoy")) { return false }

	return true
}


function observe_loose_stock()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	local loose = laid_over_carrier(fixture.split)
	if (loose != null)
	{
		fixture.loose_stock_seen = true
		if (check_loose_stock_end_state(loose))
		{
			pass()
		}
		return
	}

	fixture.waiting_for = "the displaced vehicle to become loose stock"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


// The pool-collection case prepares the laid-over vehicle with a separate
// convoy before it starts the source. The preparation phases are:
//
//   waiting-for-pool             the release convoy has not yet displaced the
//                                pool vehicle at the junction;
//   removing-release-convoy      the pool vehicle is laid over; destroy the
//                                convoy that released it so that it cannot block
//                                the junction or return along the single track;
//   waiting-for-release-removal  wait for that convoy to disappear;
//   starting-source              start the source convoy;
//   observing                    wait for the promoted portion.
function observe_pool_collection()
{
	if (fixture.phase == "waiting-for-pool")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the pool release convoy did not leave the pool vehicle in the junction laid-over list within " + PREPARATION_STEP_LIMIT + " steps")
			return
		}
		local carrier = laid_over_carrier(fixture.pool)
		if (carrier == null)
		{
			return
		}
		fixture.pool_prepared = true
		fixture.pool_donor_id = carrier.id
		print("RUNTIME CASE " + case_name + ": pool vehicle laid over at the junction in convoy " + carrier.id)
		fixture.phase = "removing-release-convoy"
		return
	}

	if (fixture.phase == "removing-release-convoy")
	{
		local donor = convoy_x(fixture.release_convoy_id)
		if (!check(donor.is_valid(), "the pool release convoy is no longer valid before it could be removed")) { return }
		fixture.phase = "waiting-for-release-removal"
		if (!check_tool(donor.destroy(player_x(0)), "remove the pool release convoy after it released the pool vehicle")) { return }
		return
	}

	if (fixture.phase == "waiting-for-release-removal")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the pool release convoy did not disappear within " + PREPARATION_STEP_LIMIT + " steps")
			return
		}
		if (convoy_x(fixture.release_convoy_id).is_valid())
		{
			return
		}
		fixture.release_convoy_removed = true
		fixture.phase = "starting-source"
		return
	}

	if (fixture.phase == "starting-source")
	{
		fixture.phase = "observing"
		fixture.travel_steps = 0
		fixture.junction_steps = 0
		print("RUNTIME CASE " + case_name + ": pool vehicle prepared; starting the source convoy")
		if (!check_tool(fixture.depot.start_convoy(player_x(0), source_convoy()), "start the source convoy after the pool vehicle was laid over")) { return }
		return
	}

	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	local on_line = target_line_convoys()
	if (on_line.len() > 1)
	{
		fail("target line P has " + on_line.len() + " convoys; expected exactly one split-off portion")
		return
	}
	if (on_line.len() == 1)
	{
		if (check_pool_formation(on_line[0]))
		{
			pass()
		}
		return
	}

	if (!fixture.pool_prepared)
	{
		fail("the source convoy is running before the pool vehicle was laid over")
		return
	}

	if (laid_over_carrier(fixture.split) != null || laid_over_carrier(fixture.pool) != null)
	{
		fixture.loose_stock_seen = true
	}

	fixture.waiting_for = "the split-off portion to collect the laid-over vehicle"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


function check_pool_formation(portion)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the original source convoy is no longer valid")) { return false }
	if (!check(fixture.pool_prepared, "the target order collected no laid-over vehicle: the pool vehicle was never laid over at the junction")) { return false }
	if (!check(fixture.release_convoy_removed, "the pool release convoy was not removed before the source reached the junction")) { return false }
	if (!check(portion.id != fixture.pool_donor_id, "the promoted portion has the pool donor convoy's identity")) { return false }

	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1, "the source convoy has " + source_vehicles.len() + " vehicles, expected 1")) { return false }
	if (!check(vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy no longer holds the retained vehicle descriptor")) { return false }
	if (!check(src.get_current_entry_id() == UNCOUPLE_ENTRY_ID, "the source convoy is at entry " + src.get_current_entry_id() + ", expected the uncouple entry " + UNCOUPLE_ENTRY_ID)) { return false }

	local line_p = line_x(fixture.target_line_id)
	if (!check(line_p.is_valid(), "target line P no longer exists")) { return false }
	local on_line = target_line_convoys()
	if (!check(on_line.len() == 1, "target line P has " + on_line.len() + " convoys, expected exactly 1")) { return false }
	if (!check(on_line[0].id == portion.id, "the unique convoy of target line P is not the inspected portion")) { return false }
	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the promoted portion is not assigned to target line P")) { return false }

	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 2, "the promoted portion has " + portion_vehicles.len() + " vehicles, expected the displaced vehicle and the laid-over vehicle")) { return false }
	if (!check(vehicle_matches(portion_vehicles[0], fixture.split), "the promoted portion has a different vehicle descriptor first from the displaced vehicle")) { return false }
	if (!check(vehicle_matches(portion_vehicles[1], fixture.pool), "the promoted portion has a different vehicle descriptor second from the laid-over vehicle")) { return false }
	local slots = portion.get_vehicle_snapshot()
	if (!check(slots.len() == 2, "the promoted portion snapshot has " + slots.len() + " slots, expected 2")) { return false }
	if (!check(slots[0].index == 0 && slots[1].index == 1, "the promoted portion snapshot slots are not in running order")) { return false }
	if (!check(vehicle_matches(slots[0].desc, fixture.split), "the promoted portion snapshot has a different vehicle descriptor at slot 0")) { return false }
	if (!check(vehicle_matches(slots[1].desc, fixture.pool), "the promoted portion snapshot has a different vehicle descriptor at slot 1")) { return false }

	local portion_schedule = portion.get_schedule()
	if (!check(portion_schedule != null && portion_schedule.entries.len() == 2, "the promoted portion does not run the target schedule")) { return false }
	if (!check(portion_schedule.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the target schedule of the promoted portion has no entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.get_current_entry_id() == TARGET_ENTRY_ID, "the promoted portion is at entry " + portion.get_current_entry_id() + ", expected target entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.is_reverse_schedule() == false, "the promoted portion traverses the target schedule in reverse")) { return false }
	if (!check(portion.get_state() == convoy_state_loading, "the promoted portion is in state " + portion.get_state() + ", expected the loading state " + convoy_state_loading)) { return false }

	if (!check(laid_over_carrier(fixture.split) == null, "the displaced vehicle remains in the junction laid-over list as loose stock")) { return false }
	if (!check(laid_over_carrier(fixture.pool) == null, "the laid-over vehicle remains at the junction after the target order collected it")) { return false }

	local total = world_vehicle_total()
	if (!check(total == 3, "the map holds " + total + " vehicles, expected the retained vehicle and the two promoted vehicles")) { return false }
	local unexpected = find_populated_convoy_other_than([src.id, portion.id])
	if (!check(unexpected == null, "a populated convoy other than the source and the promoted portion exists")) { return false }

	if (fixture.pool_donor_id != 0)
	{
		local donor = convoy_x(fixture.pool_donor_id)
		if (donor.is_valid() && donor.get_vehicles().len() != 0)
		{
			return fail("the pool donor convoy still carries " + donor.get_vehicles().len() + " vehicles after the target order collected the laid-over vehicle")
		}
	}
	return true
}


function check_reversed_formation(portion)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the original source convoy is no longer valid")) { return false }
	if (!check(portion.is_valid(), "the split-off portion is no longer valid")) { return false }

	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1 && vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy does not hold exactly the retained vehicle descriptor")) { return false }
	if (!check(src.get_current_entry_id() == UNCOUPLE_ENTRY_ID, "the source convoy is at entry " + src.get_current_entry_id() + ", expected entry " + UNCOUPLE_ENTRY_ID)) { return false }
	local source_schedule = src.get_schedule()
	if (!check(source_schedule != null, "the source convoy has no schedule")) { return false }
	if (!check(source_schedule.entries[fixture.uncouple_index].uncouple_target_sch_is_reversed == true, "the source entry does not request reverse traversal of the target schedule")) { return false }

	local line_p = line_x(fixture.target_line_id)
	if (!check(line_p.is_valid(), "target line P no longer exists")) { return false }
	local on_line = target_line_convoys()
	if (!check(on_line.len() == 1 && on_line[0].id == portion.id, "target line P does not hold exactly the inspected portion")) { return false }
	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the split-off portion is not assigned to target line P")) { return false }

	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the split-off portion does not hold exactly the split-off vehicle descriptor")) { return false }
	local portion_schedule = portion.get_schedule()
	if (!check(portion_schedule != null && portion_schedule.entries.len() == 2, "the split-off portion does not run the two-entry target schedule")) { return false }
	if (!check(portion_schedule.entries[0].unique_entry_id == PREVIOUS_TARGET_ENTRY_ID, "the target schedule of the split-off portion has no entry " + PREVIOUS_TARGET_ENTRY_ID + " first")) { return false }
	if (!check(portion_schedule.entries[1].unique_entry_id == TARGET_ENTRY_ID, "the target schedule of the split-off portion has no entry " + TARGET_ENTRY_ID + " second")) { return false }
	if (!check(portion.get_current_entry_id() == TARGET_ENTRY_ID, "the split-off portion is at entry " + portion.get_current_entry_id() + ", expected target entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.is_reverse_schedule() == true, "the split-off portion does not traverse the target schedule in reverse")) { return false }
	if (!check(portion.get_state() == convoy_state_loading, "the split-off portion is in state " + portion.get_state() + ", expected the loading state " + convoy_state_loading)) { return false }

	if (!check(laid_over_carrier(fixture.split) == null, "the split-off vehicle remains in the junction laid-over list as loose stock")) { return false }
	local total = world_vehicle_total()
	if (!check(total == 2, "the map holds " + total + " vehicles, expected the retained vehicle and the split-off vehicle")) { return false }
	return true
}


function observe_reversed_target()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	if (fixture.phase == "post_formation")
	{
		observe_reversed_advance()
		return
	}

	local on_line = target_line_convoys()
	if (on_line.len() > 1)
	{
		fail("target line P has " + on_line.len() + " convoys; expected exactly one split-off portion")
		return
	}
	if (on_line.len() == 1)
	{
		if (check_reversed_formation(on_line[0]))
		{
			fixture.portion_id = on_line[0].id
			fixture.phase = "post_formation"
			print("RUNTIME CASE " + case_name + ": split-off portion formed at target entry " + TARGET_ENTRY_ID + " in reverse traversal; observing the reverse advance")
		}
		return
	}

	if (laid_over_carrier(fixture.split) != null)
	{
		fixture.loose_stock_seen = true
	}

	fixture.waiting_for = "a split-off portion on target line P traversing the target schedule in reverse"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


// The portion adopts target entry 21 in reverse traversal. Advancing the
// reversed schedule decrements the entry index, so entry 21 must be followed by
// entry 20.
function observe_reversed_advance()
{
	local portion = convoy_x(fixture.portion_id)
	if (!portion.is_valid())
	{
		fail("the split-off portion was destroyed before its reversed schedule advanced from target entry " + TARGET_ENTRY_ID + " to entry " + PREVIOUS_TARGET_ENTRY_ID)
		return
	}
	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the split-off portion left target line P before its reversed schedule advanced")) { return }
	if (!check(portion.is_reverse_schedule() == true, "the split-off portion stopped traversing the target schedule in reverse before it advanced")) { return }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the split-off portion no longer holds exactly the split-off vehicle descriptor")) { return }

	local entry = portion.get_current_entry_id()
	if (entry == TARGET_ENTRY_ID)
	{
		departure_step("the split-off portion leaving target entry " + TARGET_ENTRY_ID + " in reverse traversal")
		return
	}
	if (entry == PREVIOUS_TARGET_ENTRY_ID)
	{
		pass()
		return
	}
	fail("the split-off portion reached entry " + entry + " (state " + portion.get_state() + ") instead of decrementing from target entry " + TARGET_ENTRY_ID + " to entry " + PREVIOUS_TARGET_ENTRY_ID)
}


function find_copied_schedule_convoy()
{
	foreach (cnv in world.get_convoy_list())
	{
		if (!cnv.is_valid()) { continue }
		if (cnv.id == fixture.convoy_id || cnv.id == fixture.target_convoy_id) { continue }
		local schedule = cnv.get_schedule()
		if (schedule == null || schedule.entries.len() != 2) { continue }
		if (schedule.entries[0].unique_entry_id != TARGET_ENTRY_ID) { continue }
		if (schedule.entries[1].unique_entry_id != NEXT_TARGET_ENTRY_ID) { continue }
		return cnv
	}
	return null
}


function check_lineless_formation(portion)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the original source convoy is no longer valid")) { return false }
	if (!check(portion.is_valid(), "the promoted portion is no longer valid")) { return false }
	if (!check(portion.id != fixture.target_convoy_id, "the promoted portion has the lineless target convoy's identity")) { return false }

	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1 && vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy does not hold exactly the retained vehicle descriptor")) { return false }
	if (!check(src.get_current_entry_id() == UNCOUPLE_ENTRY_ID, "the source convoy is at entry " + src.get_current_entry_id() + ", expected entry " + UNCOUPLE_ENTRY_ID)) { return false }

	if (!check(convoy_has_no_line(portion), "the promoted portion is assigned to a line, but a lineless target must not assign one")) { return false }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the promoted portion does not hold exactly the displaced vehicle descriptor")) { return false }
	local portion_schedule = portion.get_schedule()
	if (!check(portion_schedule != null && portion_schedule.entries.len() == 2, "the promoted portion does not run the copied two-entry schedule")) { return false }
	if (!check(portion_schedule.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the copied schedule has no entry " + TARGET_ENTRY_ID + " first")) { return false }
	if (!check(portion_schedule.entries[1].unique_entry_id == NEXT_TARGET_ENTRY_ID, "the copied schedule has no entry " + NEXT_TARGET_ENTRY_ID + " second")) { return false }
	if (!check(portion.get_current_entry_id() == TARGET_ENTRY_ID, "the promoted portion is at entry " + portion.get_current_entry_id() + ", expected target entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.is_reverse_schedule() == false, "the promoted portion traverses the copied schedule in reverse")) { return false }
	if (!check(portion.get_state() == convoy_state_loading, "the promoted portion is in state " + portion.get_state() + ", expected the loading state " + convoy_state_loading)) { return false }
	if (!check(TARGET_ENTRY_ID in portion_schedule.orders, "the copied schedule has no order at entry " + TARGET_ENTRY_ID)) { return false }
	local order = portion_schedule.orders[TARGET_ENTRY_ID]
	if (!check(order.slots.len() == 1, "the copied schedule order has " + order.slots.len() + " slots, expected 1")) { return false }
	local order_desc = order.slots[0].alternatives[0].specific_vehicle
	if (!check(vehicle_matches(order_desc, fixture.split), "the copied schedule order does not match the displaced vehicle descriptor")) { return false }

	local target = convoy_x(fixture.target_convoy_id)
	if (!check(target.is_valid(), "the lineless target convoy is no longer valid")) { return false }
	if (!check(convoy_has_no_line(target), "the lineless target convoy acquired a line")) { return false }
	if (!check(target.is_in_depot(), "the lineless target convoy left the depot")) { return false }
	local target_vehicles = target.get_vehicles()
	if (!check(target_vehicles.len() == 1 && vehicle_matches(target_vehicles[0], fixture.retained), "the lineless target convoy no longer holds its original vehicle descriptor")) { return false }
	local target_schedule = target.get_schedule()
	if (!check(target_schedule != null && target_schedule.entries.len() == 2, "the lineless target convoy lost its two-entry schedule")) { return false }
	if (!check(target_schedule.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the lineless target convoy schedule changed at entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(target_schedule.entries[1].unique_entry_id == NEXT_TARGET_ENTRY_ID, "the lineless target convoy schedule changed at entry " + NEXT_TARGET_ENTRY_ID)) { return false }

	if (!check(laid_over_carrier(fixture.split) == null, "the split-off vehicle remains in the junction laid-over list as loose stock")) { return false }
	local total = world_vehicle_total()
	if (!check(total == 3, "the map holds " + total + " vehicles, expected the retained vehicle, the portion and the target convoy's vehicle")) { return false }
	local unexpected = find_populated_convoy_other_than([src.id, portion.id, target.id])
	if (!check(unexpected == null, "a populated convoy other than the source, the portion and the target exists")) { return false }
	return true
}


function observe_lineless_target()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	if (fixture.phase == "post_formation")
	{
		observe_lineless_advance()
		return
	}

	local portion = find_copied_schedule_convoy()
	if (portion != null)
	{
		if (check_lineless_formation(portion))
		{
			fixture.portion_id = portion.id
			fixture.phase = "post_formation"
			print("RUNTIME CASE " + case_name + ": promoted portion runs a copy of the lineless target convoy's schedule; observing departure")
		}
		return
	}

	if (laid_over_carrier(fixture.split) != null)
	{
		fixture.loose_stock_seen = true
	}

	fixture.waiting_for = "a promoted portion that runs a copy of the lineless target convoy's schedule"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


function observe_lineless_advance()
{
	local portion = convoy_x(fixture.portion_id)
	if (!portion.is_valid())
	{
		fail("the promoted portion was destroyed before its copied schedule advanced from target entry " + TARGET_ENTRY_ID + " to entry " + NEXT_TARGET_ENTRY_ID)
		return
	}
	if (!check(convoy_has_no_line(portion), "the promoted portion acquired a line while running its copied schedule")) { return }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the promoted portion no longer holds exactly the displaced vehicle descriptor")) { return }

	local entry = portion.get_current_entry_id()
	if (entry == TARGET_ENTRY_ID)
	{
		departure_step("the promoted portion leaving target entry " + TARGET_ENTRY_ID)
		return
	}
	if (entry == NEXT_TARGET_ENTRY_ID)
	{
		pass()
		return
	}
	fail("the promoted portion reached entry " + entry + " (state " + portion.get_state() + ") instead of advancing from target entry " + TARGET_ENTRY_ID + " to entry " + NEXT_TARGET_ENTRY_ID)
}


// The source runs a mirrored schedule. The convoy starts at the last entry,
// departs it in the forward direction, and then traverses the schedule in
// reverse towards the junction uncouple entry.
function observe_reversed_source()
{
	local src = source_convoy()
	if (!src.is_valid())
	{
		fail("the source convoy disappeared before the junction state could be observed")
		return
	}

	if (src.get_current_entry_id() == EAST_ENTRY_ID && !src.is_reverse_schedule() && !fixture.saw_last_entry_forward)
	{
		fixture.saw_last_entry_forward = true
		print("RUNTIME CASE " + case_name + ": source convoy is at the last entry " + EAST_ENTRY_ID + " in forward traversal")
	}

	local on_line = target_line_convoys()
	if (on_line.len() > 1)
	{
		fail("target line P has " + on_line.len() + " convoys; expected exactly one split-off portion")
		return
	}
	if (on_line.len() == 1)
	{
		if (check_reversed_source_formation(on_line[0]))
		{
			pass()
		}
		return
	}

	if (laid_over_carrier(fixture.split) != null)
	{
		fixture.loose_stock_seen = true
	}

	fixture.waiting_for = "a split-off portion on target line P from a source traversing its mirrored schedule in reverse"
	if (!source_waiting_at_uncouple())
	{
		return
	}
	junction_progress(fixture.waiting_for)
}


function check_reversed_source_formation(portion)
{
	local src = source_convoy()
	if (!check(src.is_valid(), "the source convoy is no longer valid")) { return false }
	if (!check(fixture.saw_last_entry_forward, "the source convoy did not reach the last entry " + EAST_ENTRY_ID + " in forward traversal before the uncouple entry")) { return false }
	if (!check(src.is_reverse_schedule() == true, "the source convoy reached the uncouple entry without traversing its mirrored schedule in reverse")) { return false }
	local source_schedule = src.get_schedule()
	if (!check(source_schedule != null && source_schedule.mirrored == true, "the source convoy does not run a mirrored schedule")) { return false }
	if (!check(source_schedule.current_stop == 0, "the source convoy is at schedule index " + source_schedule.current_stop + ", expected the junction entry at index 0")) { return false }
	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1 && vehicle_matches(source_vehicles[0], fixture.retained), "the source convoy does not hold exactly the retained vehicle descriptor")) { return false }
	if (!check(src.get_current_entry_id() == UNCOUPLE_ENTRY_ID, "the source convoy is at entry " + src.get_current_entry_id() + ", expected entry " + UNCOUPLE_ENTRY_ID)) { return false }

	local line_p = line_x(fixture.target_line_id)
	if (!check(line_p.is_valid(), "target line P no longer exists")) { return false }
	local on_line = target_line_convoys()
	if (!check(on_line.len() == 1 && on_line[0].id == portion.id, "target line P does not hold exactly the inspected portion")) { return false }
	local portion_line = portion.get_line()
	if (!check(portion_line != null && portion_line.is_valid() && portion_line.id == fixture.target_line_id, "the split-off portion is not assigned to target line P")) { return false }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1 && vehicle_matches(portion_vehicles[0], fixture.split), "the split-off portion does not hold exactly the split-off vehicle descriptor")) { return false }
	local portion_schedule = portion.get_schedule()
	if (!check(portion_schedule != null && portion_schedule.entries.len() == 2, "the split-off portion does not run the target schedule")) { return false }
	if (!check(portion_schedule.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the target schedule of the split-off portion has no entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.get_current_entry_id() == TARGET_ENTRY_ID, "the split-off portion is at entry " + portion.get_current_entry_id() + ", expected target entry " + TARGET_ENTRY_ID)) { return false }
	if (!check(portion.is_reverse_schedule() == false, "the split-off portion traverses the target schedule in reverse")) { return false }
	if (!check(portion.get_state() == convoy_state_loading, "the split-off portion is in state " + portion.get_state() + ", expected the loading state " + convoy_state_loading)) { return false }

	if (!check(laid_over_carrier(fixture.split) == null, "the split-off vehicle remains in the junction laid-over list as loose stock")) { return false }
	local total = world_vehicle_total()
	if (!check(total == 2, "the map holds " + total + " vehicles, expected the retained vehicle and the split-off vehicle")) { return false }
	return true
}


function observe()
{
	if (case_name == "forward" || case_name == "reload")
	{
		observe_forward()
	}
	else if (case_name == "reversed-target")
	{
		observe_reversed_target()
	}
	else if (case_name == "lineless-target")
	{
		observe_lineless_target()
	}
	else if (case_name == "pool-collection")
	{
		observe_pool_collection()
	}
	else if (case_name == "reversed-source")
	{
		observe_reversed_source()
	}
	else
	{
		observe_loose_stock()
	}
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
	return ttext("Run the selected scheduled division case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Y division runtime case {name}: {phase}")
	msg.name = case_name
	msg.phase = fixture.phase
	return msg
}


function get_result_text(pl)
{
	return get_info_text(pl)
}


// Called by scenario_t::rdwr after the script has been loaded again and the
// saved persistent table has been evaluated. The reload case reconstructs the
// fixture from the loaded world; setup() must not run again, because the
// infrastructure, schedules and vehicles are all in the savegame. Observation
// then re-asserts the divided state from observe_forward().
function resume_game()
{
	if (case_name != "reload")
	{
		return
	}
	if (!("reload" in persistent) || typeof(persistent.reload) != "table")
	{
		fail("the savegame holds no persisted reload state; it must be saved after the forward division has formed")
		return
	}

	local state = persistent.reload
	// Drop the state so that a later save cannot restore a stale
	// pre-departure state.
	delete persistent.reload

	fixture.setup_started = true
	fixture.reached_junction = true
	fixture.convoy_id = state.convoy_id
	fixture.portion_id = state.portion_id
	fixture.target_line_id = state.target_line_id

	if (!check(state.kind == case_name, "the persisted state belongs to case " + state.kind + ", not " + case_name)) { return }

	local src = source_convoy()
	if (!check(src.is_valid(), "the source convoy " + fixture.convoy_id + " named by the persisted state is not in the loaded world")) { return }
	local source_vehicles = src.get_vehicles()
	if (!check(source_vehicles.len() == 1, "the restored source convoy has " + source_vehicles.len() + " vehicles, expected 1")) { return }
	fixture.retained = source_vehicles[0]

	local portion = convoy_x(fixture.portion_id)
	if (!check(portion.is_valid(), "the split-off portion " + fixture.portion_id + " named by the persisted state is not in the loaded world")) { return }
	local portion_vehicles = portion.get_vehicles()
	if (!check(portion_vehicles.len() == 1, "the restored split-off portion has " + portion_vehicles.len() + " vehicles, expected 1")) { return }
	fixture.split = portion_vehicles[0]

	if (!check(line_x(fixture.target_line_id).is_valid(), "target line P " + fixture.target_line_id + " named by the persisted state is not in the loaded world")) { return }

	local depot = depot_x(1, 2, 0)
	if (!check(depot.is_valid(), "the depot at (1, 2, 0) is not in the loaded world")) { return }
	fixture.depot = depot

	fixture.phase = "restored-pending"
	print("RUNTIME CASE " + case_name + ": saved division state restored from the savegame")
}


function is_scenario_completed(pl)
{
	if (pl != 0)
	{
		return 0
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
		if (case_name == "pool-collection")
		{
			fixture.phase = "waiting-for-pool"
		}
		else
		{
			fixture.phase = "observing"
		}
		print("RUNTIME CASE " + case_name + ": setup complete; source convoy " + fixture.convoy_id + " created")
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
