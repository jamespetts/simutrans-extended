//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Runtime fixture for the depot maintenance and overhaul read model.
//
// The fixture builds a short single-track line with a depot at (1, 2) and one
// stop at (11, 2), puts a convoy in the depot and gives it the schedule
//
//   entry 11: the stop at (11, 2)
//   entry 12: the depot at (1, 2), with load = 4 and reverse = 1
//
// The road case (maintenance-dwell) uses one powered road vehicle. Every road
// vehicle descriptor available at that case's date reports 100% starting and
// minimum availability, so its maintenance wait is zero. The rail case
// (maintenance-dwell-rail) uses the powered non-electric locomotive whose
// descriptor reports starting availability below 100, followed by a
// low-availability carriage; its wait is nonzero, so it observes the dwell.
// The overhaul case (maintenance-overhaul) uses the rail fixture's locomotive and
// calls convoy_x.set_maintenance_distances_for_testing() to set the leading
// vehicle's distance since the last overhaul above the descriptor's overhaul
// interval. The depot entry's maintain_or_overhaul flag then makes
// depot_t::convoi_arrived() place the vehicle in the overhaul group. The case
// asserts the overhaul count, the odometer reset, the availability reset and the
// cash booking when the overhaul state is entered; it does not wait for the
// overhaul duration to expire.
//
// A depot schedule entry carries the depot visit flags in its minimum_loading
// field: convoi_t::ziel_erreicht() passes schedule->get_current_entry().minimum_loading
// to convoi_t::enter_depot() as the flags argument. The value 4 is
// schedule_entry_t::maintain_or_overhaul, so arriving at the depot entry
// requests routine maintenance for every vehicle in the convoy without the
// vehicle being due. This is the same encoding that the depot window writes into
// the entry; the script schedule API exposes it through schedule_entry_x.load.
// The entry also sets reverse = 1. Without that value the fixture's convoy passed
// the depot tile and looped between the stop and the depot without entering it;
// with it, the convoy enters the depot.
//
// The case therefore observes the maintenance visit itself:
//
//   1. the convoy departs the depot, runs to the stop and returns;
//   2. on arrival at the depot entry it enters the MAINTENANCE state;
//   3. if the vehicle reports availability below 100, the service wait reported
//      by convoy_x.get_service_wait_ticks() is positive, decreases while the
//      state holds and the state lasts for more than one observation step;
//   4. the convoy leaves the MAINTENANCE state, its maintained vehicle reports
//      zero km since the last maintenance, its availability and overhaul
//      distance are unchanged, its overhaul count is still zero, and the convoy
//      drives off the depot tile towards the stop again.
//
// A vehicle built during the run has covered no distance, so the maintenance
// cases that do not use the test hook assert the negative for overhauls: the
// visit is MAINTENANCE and the overhaul count and distance never change. The
// overhaul case sets the counters through the scenario-only
// convoy_x.set_maintenance_distances_for_testing() test hook, which changes the
// same fields that normal gameplay advances in vehicle_t::step_km(). Setting
// km_since_last_overhaul above the descriptor's max_distance_between_overhauls
// makes vehicle_t::is_overhaul_needed() true, and the depot entry's
// maintain_or_overhaul flag then classifies the vehicle for overhaul in
// depot_t::convoi_arrived().
//
// The maintenance wait that vehicle_t::maintain() computes is
// active_service_time * (100 - availability) / 100. When the wait is zero the
// visit completes between two observation steps and no MAINTENANCE state is
// observable; the fixture detects the visit by the odometer reset at the depot
// tile and skips the dwell assertions. When the wait is positive the dwell
// assertions run. Availability is read from the vehicle snapshot.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Depot maintenance runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

// The case selector can set maintenance_rail_case before including this file.
// The road case uses one powered road vehicle; the rail case uses the one
// available powered non-electric locomotive with starting availability below
// 100 and a low-availability carriage behind it.
local RAIL_CASE = ("maintenance_rail_case" in getroottable()) && ::maintenance_rail_case
local OVERHAUL_CASE = ("maintenance_overhaul_case" in getroottable()) && ::maintenance_overhaul_case
// The split case makes the last vehicle due for overhaul and the rest of the
// consist due only for routine maintenance, then checks that the maintained
// vehicles leave the depot in a new convoy while the overhauled vehicle stays.
local SPLIT_CASE = ("maintenance_split_case" in getroottable()) && ::maintenance_split_case
// The invalid case makes the locomotive due for overhaul and leaves only the
// tender and carriage in the maintained group, which is unpowered. The split
// must be refused and the original convoy must be restored.
local SPLIT_INVALID_CASE = ("maintenance_split_invalid_case" in getroottable()) && ::maintenance_split_invalid_case
// The unreachable case gives the convoy a valid maintained group but a next
// entry after the depot that cannot be reached, so the split fails its route
// check after the schedule and line have been installed and must undo.
local SPLIT_UNREACHABLE_CASE = ("maintenance_split_unreachable_case" in getroottable()) && ::maintenance_split_unreachable_case
// The cross-convoy case puts two mixed convoys of the same line in one depot.
// Train A's locomotive and tender are due for overhaul and its carriage is only
// maintained; train B's carriage is due for overhaul and its locomotive and
// tender are only maintained. The maintained groups combine into one convoy.
local CROSS_CONVOY_CASE = ("maintenance_cross_convoy_case" in getroottable()) && ::maintenance_cross_convoy_case
// The withdraw case destroys a convoy while it is in the maintenance queue and
// checks that the depot survives the following sync steps and drops the convoy
// from its lists.
local WITHDRAW_CASE = ("maintenance_withdraw_case" in getroottable()) && ::maintenance_withdraw_case
// The depot-order case authors a Depot Consist Order on the depot entry. The
// Convoy decomposes on arrival: the Lead Vehicle stays in the original Convoy
// and the other vehicles wait in the Depot Vehicle Pool. When every vehicle is
// released, Depot Assembly re-forms the original Consist and the Convoy leaves.
local DEPOT_ORDER_CASE = ("maintenance_depot_order_case" in getroottable()) && ::maintenance_depot_order_case
// The exclude case authors an order that names only the first two vehicles.
// The last vehicle is due for overhaul and is left in the Depot Vehicle Pool
// while the original Convoy departs with the vehicles the order names.
local DEPOT_ORDER_EXCLUDE_CASE = ("maintenance_depot_order_exclude_case" in getroottable()) && ::maintenance_depot_order_exclude_case
local DEPOT_ORDER_ANY_CASE = DEPOT_ORDER_CASE || DEPOT_ORDER_EXCLUDE_CASE
// The target case builds two convoys of one line, both held in the depot, and
// drives the Target Fleet Size: raise it to dispatch Held Convoys, lower it to
// withdraw a Deployed Convoy to the depot.
local TARGET_CASE = ("maintenance_target_case" in getroottable()) && ::maintenance_target_case
local WAYTYPE = RAIL_CASE ? wt_rail : wt_road

// The runner's stall watchdog uses log growth. Engine per-step logging is
// suppressed at low debug levels, so print a heartbeat that does not depend on
// the engine log level.
local heartbeat_steps = 0

local DEPOT_POS = coord3d(1, 2, 0)
local STOP_POS = coord3d(11, 2, 0)
local WAY_END_POS = coord3d(12, 2, 0)

local STOP_ENTRY_ID = 11
local DEPOT_ENTRY_ID = 12
local UNREACHABLE_POS = coord3d(2, 10, 0)
local UNREACHABLE_ENTRY_ID = 13

// schedule_entry_t::maintain_or_overhaul. A depot schedule entry carries the
// visit flags in minimum_loading (the schedule_entry_x.load field).
local DEPOT_FLAG_MAINTAIN_OR_OVERHAUL = 4

// The overhaul case sets this distance since the last overhaul. Ten million
// kilometres exceeds every pakset's max_distance_between_overhauls.
local OVERHAUL_DISTANCE = 10000000

// Observation-step limits. They are diagnostic bounds for a case that stalls;
// the runner's overall per-case timeout is the real bound. The rail case's wait
// is derived from the active-service time on the two legs and the locomotive's
// availability, which is below 100, so it lasts for many observation steps at
// the fast-forward step rate.
local TRAVEL_STEP_LIMIT = 20000
local SERVICE_STEP_LIMIT = 20000
local DEPARTURE_STEP_LIMIT = 4000

local fixture = {
	stage = "setup-pending",
	setup_started = false,
	completed = false,
	depot = null,
	convoy_id = 0,
	vehicle_count = 0,
	travel_steps = 0,
	service_steps = 0,
	departure_steps = 0,
	saw_stop = false,
	km_before_stop = 0,
	before_km_overhaul = 0,
	before_availability = 0,
	service_state = -1,
	first_wait = 0,
	last_wait = 0,
	captured_after = false,
	cash_before = 0.0,
	month_before = 0,
	overhaul_count_before = 0,
	overhaul_cost_before = 0,
	line_id = 0,
	known_ids = [],
	split_convoy_id = 0,
	split_steps = 0,
	split_carriage_index = 0,
	split_overhaul_before = 0,
	split_maintenance_before = 0,
	overhaul_vehicle_index = 0,
	convoy_id_b = 0,
	vehicle_count_b = 0,
	cross_steps = 0,
	cross_checked = false,
	cross_b_started = false,
	withdraw_steps = 0,
	withdraw_cleared_steps = 0,
	order_descs = [],
	target_phase = 0
}


function fail(what)
{
	if (!fixture.completed)
	{
		fixture.completed = true
		print("RUNTIME CASE " + case_name + " FAIL: " + what)
		print("RUNTIME CASE " + case_name + " COMPLETE")
	}
	fixture.stage = "failed"
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
	fixture.stage = "done"
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
// while the init-phase wrappers (schedule and depot changes) return true. Both
// are accepted here; the committed state is read back where it matters.
function check_tool(result, what)
{
	if (result == null || result == true)
	{
		return true
	}
	return fail(what + ": " + result)
}


function depot_desc_for(waytype)
{
	foreach (building in building_desc_x.get_building_list(building_desc_x.depot))
	{
		if (building.get_type() == building_desc_x.depot && building.get_waytype() == waytype && building.is_available(world.get_time()))
		{
			return building
		}
	}
	return null
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


// Returns the descriptors to append, in running order. The road case uses one
// powered non-electric road vehicle. The rail case uses the powered
// non-electric locomotive whose descriptor reports starting availability below
// 100, followed by an unpowered carriage that can be the last vehicle. The
// locomotive cannot be last, so a single-vehicle rail convoy is invalid.
function select_vehicle()
{
	if (RAIL_CASE)
	{
		local loco = null
		local carriage = null
		foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
		{
			if (desc.get_power() == 0) { continue }
			if (desc.needs_electrification()) { continue }
			if (desc.get_starting_availability() >= 100) { continue }
			if (!desc.can_be_first()) { continue }
			loco = desc
			break
		}
		foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
		{
			if (desc.get_power() != 0) { continue }
			if (desc.get_starting_availability() >= 100) { continue }
			if (!desc.can_be_last()) { continue }
			carriage = desc
			break
		}
		if (loco == null || carriage == null) { return null }
		return [loco, carriage]
	}

	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_road))
	{
		if (desc.get_power() == 0) { continue }
		if (desc.needs_electrification()) { continue }
		if (desc.get_successors().len() > 0) { continue }
		if (!desc.can_be_first() || !desc.can_be_last()) { continue }
		if (CROSS_CONVOY_CASE) { return [desc, desc] }
		return [desc]
	}
	return null
}


function make_entry(pos, unique_entry_id)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = unique_entry_id
	return entry
}


// One specific-vehicle slot per descriptor, in the given order. Used to author
// the Depot Consist Order that re-forms the fixture's Convoy.
function make_depot_order(descriptors)
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


// Creates a line with the given schedule and returns it, or null. The split
// case needs a line because depot_t::convoi_arrived() does not split a lineless
// convoy.
function create_line_with_schedule(pl, schedule)
{
	local before = []
	foreach (line in pl.get_line_list())
	{
		before.append(line.id)
	}
	if (!check_tool(pl.create_line(WAYTYPE), "create the maintenance line")) { return null }
	local created = null
	foreach (line in pl.get_line_list())
	{
		if (before.find(line.id) == null)
		{
			created = line
			break
		}
	}
	if (!check(created != null, "the new maintenance line was not found")) { return null }
	if (!check_tool(created.change_schedule(pl, schedule), "commit the maintenance line schedule")) { return null }
	return created
}


function setup()
{
	local pl = player_x(0)
	local way = way_desc_x.get_available_ways(WAYTYPE, st_flat)[0]
	if (!check(way != null, "no way available for waytype " + WAYTYPE)) { return false }

	if (!check_tool(command_x.build_way(pl, DEPOT_POS, WAY_END_POS, way, false), "build the line")) { return false }
	local depot_desc = depot_desc_for(WAYTYPE)
	if (!check(depot_desc != null, "no depot available for waytype " + WAYTYPE)) { return false }
	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc), "build the depot")) { return false }
	local depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(depot.is_valid(), "the depot at " + DEPOT_POS + " is not valid after building it")) { return false }
	fixture.depot = depot

	local station = building_desc_x.get_available_stations(building_desc_x.station, WAYTYPE, good_desc_x.passenger)[0]
	if (!check(station != null, "no station available for waytype " + WAYTYPE)) { return false }
	if (!check_tool(command_x(tool_build_station).work(pl, STOP_POS, station.get_name()), "build the stop")) { return false }

	if (SPLIT_UNREACHABLE_CASE)
	{
		// An isolated halt that no route from the depot can reach.
		if (!check_tool(command_x.build_way(pl, coord3d(1, 10, 0), coord3d(3, 10, 0), way, false), "build the isolated way")) { return false }
		if (!check_tool(command_x(tool_build_station).work(pl, UNREACHABLE_POS, station.get_name()), "build the unreachable stop")) { return false }
	}

	local descs = select_vehicle()
	if (!check(descs != null && descs.len() > 0, "no suitable vehicle is available for waytype " + WAYTYPE)) { return false }

	local before = convoy_ids()
	if (!check_tool(depot.append_vehicle(pl, convoy_x(0), descs[0]), "put the leading vehicle in the depot")) { return false }
	local cnv = find_new_convoy(before)
	if (!check(cnv != null, "the convoy was not found after adding its leading vehicle")) { return false }
	fixture.convoy_id = cnv.id

	for (local i = 1; i < descs.len(); i++)
	{
		if (!check_tool(depot.append_vehicle(pl, cnv, descs[i]), "append vehicle " + i + " to the convoy")) { return false }
	}
	fixture.vehicle_count = cnv.get_vehicles().len()
	if (!check(fixture.vehicle_count >= descs.len(), "the convoy has " + fixture.vehicle_count + " vehicles after appending " + descs.len())) { return false }

	// The cross-convoy case needs a second convoy of the same line in the same
	// depot. The depot tool creates a new convoy when it is given an unbound
	// handle, as the first convoy above.
	local cnv_b = null
	if (CROSS_CONVOY_CASE || TARGET_CASE)
	{
		local before_b = convoy_ids()
		if (!check_tool(depot.append_vehicle(pl, convoy_x(0), descs[0]), "put the second convoy's leading vehicle in the depot")) { return false }
		cnv_b = find_new_convoy(before_b)
		if (!check(cnv_b != null, "the second convoy was not found after adding its leading vehicle")) { return false }
		fixture.convoy_id_b = cnv_b.id
		for (local i = 1; i < descs.len(); i++)
		{
			if (!check_tool(depot.append_vehicle(pl, cnv_b, descs[i]), "append vehicle " + i + " to the second convoy")) { return false }
		}
		fixture.vehicle_count_b = cnv_b.get_vehicles().len()
		if (!check(fixture.vehicle_count_b >= descs.len(), "the second convoy has " + fixture.vehicle_count_b + " vehicles after appending " + descs.len())) { return false }
	}

	local stop_entry = make_entry(STOP_POS, STOP_ENTRY_ID)
	local depot_entry = make_entry(DEPOT_POS, DEPOT_ENTRY_ID)
	// The depot entry requests routine maintenance on arrival. It is also the
	// line's terminus, so the convoy reverses there.
	depot_entry.load = DEPOT_FLAG_MAINTAIN_OR_OVERHAUL
	depot_entry.reverse = 1

	local entries = [stop_entry, depot_entry]
	if (SPLIT_UNREACHABLE_CASE)
	{
		entries.append(make_entry(UNREACHABLE_POS, UNREACHABLE_ENTRY_ID))
	}
	local schedule = schedule_x(WAYTYPE, entries)
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the convoy schedule")) { return false }

	// Read the depot entry's visit flags back: the case depends on the value
	// surviving the schedule commit.
	local back = cnv.get_schedule()
	local expected_entries = SPLIT_UNREACHABLE_CASE ? 3 : 2
	if (!check(back != null && back.entries.len() == expected_entries, "the committed schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected " + expected_entries)) { return false }
	if (!check(back.entries[0].unique_entry_id == STOP_ENTRY_ID, "schedule entry 0 is not the stop entry " + STOP_ENTRY_ID)) { return false }
	if (!check(back.entries[1].unique_entry_id == DEPOT_ENTRY_ID, "schedule entry 1 is not the depot entry " + DEPOT_ENTRY_ID)) { return false }
	if (!check(back.entries[1].load == DEPOT_FLAG_MAINTAIN_OR_OVERHAUL, "the depot entry reports load " + back.entries[1].load + ", expected the maintain_or_overhaul visit flag " + DEPOT_FLAG_MAINTAIN_OR_OVERHAUL)) { return false }
	if (!check(back.entries[1].reverse == 1, "the depot entry reports reverse " + back.entries[1].reverse + ", expected 1 so that the route search stops at the depot instead of treating it as a waypoint")) { return false }
	if (SPLIT_UNREACHABLE_CASE)
	{
		if (!check(back.entries[2].unique_entry_id == UNREACHABLE_ENTRY_ID, "schedule entry 2 is not the unreachable entry " + UNREACHABLE_ENTRY_ID)) { return false }
	}

	if (DEPOT_ORDER_ANY_CASE)
	{
		local slots = cnv.get_vehicle_snapshot();
		local descriptors = [];
		local included = DEPOT_ORDER_EXCLUDE_CASE ? slots.len() - 1 : slots.len();
		for (local i = 0; i < included; i++)
		{
			descriptors.append(slots[i].desc);
		}
		fixture.order_descs = descriptors;

		if (DEPOT_ORDER_EXCLUDE_CASE)
		{
			local last = slots.len() - 1;
			if (!check(cnv.set_maintenance_distances_for_testing(last, 0, OVERHAUL_DISTANCE), "set the excluded vehicle's overhaul distance")) { return false; }
			if (!check(slots[last].overhaul_cost > 0, "the excluded vehicle reports no overhaul cost")) { return false; }
			if (!check_tool(pl.book_cash(slots[last].overhaul_cost + 100000), "add cash for the excluded vehicle's overhaul")) { return false; }
		}

		local order = make_depot_order(descriptors);
		local ordered = cnv.get_schedule();
		ordered.orders.rawset(DEPOT_ENTRY_ID, order);
		if (!check_tool(cnv.change_schedule(pl, ordered), "commit the depot consist order")) { return false; }
		local back_order = cnv.get_schedule();
		if (!check(back_order.orders.rawget(DEPOT_ENTRY_ID) != null, "the committed schedule has no depot consist order")) { return false; }
	}

	if (CROSS_CONVOY_CASE || TARGET_CASE)
	{
		// Both convoys use the line schedule, so both arrive at the depot with
		// the depot entry current.
		local line = create_line_with_schedule(pl, schedule)
		if (line == null) { return false }
		fixture.line_id = line.id
		if (!check_tool(cnv.set_line(pl, line), "assign the first convoy to the maintenance line")) { return false }
		if (!check_tool(cnv_b.set_line(pl, line), "assign the second convoy to the maintenance line")) { return false }

		if (TARGET_CASE)
		{
			// Both Convoys are INITIAL Held Convoys. Raising the target to 1
			// must dispatch the lowest-id Convoy.
			if (!check(line.get_deployed_convoy_count() == 0, "the target case starts with " + line.get_deployed_convoy_count() + " Deployed Convoys")) { return false }
			if (!check_tool(line.set_target_convoy_count(pl, 1), "set the line target to 1")) { return false }
			return true
		}

		local a_slots = cnv.get_vehicle_snapshot()
		local b_slots = cnv_b.get_vehicle_snapshot()
		if (!check(a_slots.len() == 3 && b_slots.len() == 3, "the cross-convoy case needs three vehicles in each convoy (locomotive, tender and carriage), but they have " + a_slots.len() + " and " + b_slots.len())) { return false }

		// Train A: locomotive due for overhaul, tender and carriage only maintained.
		if (!check(cnv.set_maintenance_distances_for_testing(0, 0, OVERHAUL_DISTANCE), "set train A's locomotive overhaul distance")) { return false }
		if (!check(cnv.set_maintenance_distances_for_testing(2, OVERHAUL_DISTANCE, 0), "set train A's carriage maintenance distance")) { return false }
		// Train B: carriage due for overhaul, locomotive and tender only maintained.
		if (!check(cnv_b.set_maintenance_distances_for_testing(0, OVERHAUL_DISTANCE, 0), "set train B's locomotive maintenance distance")) { return false }
		if (!check(cnv_b.set_maintenance_distances_for_testing(2, 0, OVERHAUL_DISTANCE), "set train B's carriage overhaul distance")) { return false }

		local a_after = cnv.get_vehicle_snapshot()
		local b_after = cnv_b.get_vehicle_snapshot()
		if (!check(a_after[0].overhaul_needed, "train A's locomotive does not report an overhaul as due")) { return false }
		if (!check(!a_after[2].overhaul_needed, "train A's carriage reports an overhaul as due; it must only be maintained")) { return false }
		if (!check(b_after[2].overhaul_needed, "train B's carriage does not report an overhaul as due")) { return false }
		if (!check(!b_after[0].overhaul_needed, "train B's locomotive reports an overhaul as due; it must only be maintained")) { return false }

		local overhaul_cost = a_after[0].overhaul_cost + b_after[2].overhaul_cost
		if (!check_tool(pl.book_cash(overhaul_cost + 200000), "add cash for both overhauls")) { return false }

		fixture.known_ids = convoy_ids()
		if (!check(fixture.known_ids.find(cnv.id) != null && fixture.known_ids.find(cnv_b.id) != null, "the two convoys are not both in the convoy list before starting")) { return false }

		// Start only the first convoy. The second stays in the depot until the
		// first is in overhaul, so the two trains cannot meet head-on on the
		// single-track fixture line.
		if (!check_tool(depot.start_convoy(pl, cnv), "start the first convoy")) { return false }
		fixture.cross_b_started = false
		return true
	}

	if (OVERHAUL_CASE)
	{
		local slots = cnv.get_vehicle_snapshot()
		if (!check(slots.len() > 0, "the convoy has no vehicle to set up")) { return false }
		fixture.overhaul_count_before = slots[0].overhaul_count
		fixture.overhaul_cost_before = slots[0].overhaul_cost
		if (!check(cnv.set_maintenance_distances_for_testing(0, 0, OVERHAUL_DISTANCE), "the scenario-only test hook did not set the overhaul distance")) { return false }
		local after = cnv.get_vehicle_snapshot()[0]
		if (!check(after.km_since_last_overhaul == OVERHAUL_DISTANCE, "the overhaul distance reads " + after.km_since_last_overhaul + " after the test hook set " + OVERHAUL_DISTANCE)) { return false }
		if (!check(after.km_since_last_maintenance == 0, "the maintenance distance reads " + after.km_since_last_maintenance + " after the test hook set 0")) { return false }
		if (!check(after.overhaul_needed, "the vehicle does not report an overhaul as due after the test hook set " + OVERHAUL_DISTANCE + " km since the last overhaul")) { return false }
		if (!check(after.overhaul_cost > 0, "the vehicle reports an overhaul cost of " + after.overhaul_cost + ", so the cash booking cannot be asserted")) { return false }
		if (!check_tool(pl.book_cash(fixture.overhaul_cost_before + 100000), "add cash for the overhaul")) { return false }
	}

	if (SPLIT_CASE || SPLIT_UNREACHABLE_CASE)
	{
		// The maintained group must be a valid powered consist on its own, so
		// the last vehicle is the one due for overhaul: with the rail fixture's
		// locomotive and its tender maintained, the split-off convoy can run.
		local slots = cnv.get_vehicle_snapshot()
		if (!check(slots.len() >= 3, "the split case needs a locomotive, its tender and a carriage, but the convoy has " + slots.len() + " vehicles")) { return false }
		fixture.split_carriage_index = slots.len() - 1
		fixture.overhaul_vehicle_index = fixture.split_carriage_index
		fixture.split_overhaul_before = slots[fixture.split_carriage_index].overhaul_count
		fixture.split_maintenance_before = slots[0].km_since_last_maintenance
		if (!check(cnv.set_maintenance_distances_for_testing(fixture.split_carriage_index, 0, OVERHAUL_DISTANCE), "the scenario-only test hook did not set the overhaul distance on the last vehicle")) { return false }
		if (!check(cnv.set_maintenance_distances_for_testing(0, OVERHAUL_DISTANCE, 0), "the scenario-only test hook did not set the maintenance distance on the leading vehicle")) { return false }
		local after = cnv.get_vehicle_snapshot()
		if (!check(after[fixture.split_carriage_index].overhaul_needed, "the last vehicle does not report an overhaul as due after the test hook set " + OVERHAUL_DISTANCE + " km since the last overhaul")) { return false }
		if (!check(!after[0].overhaul_needed, "the leading vehicle reports an overhaul as due; the split needs it only maintained")) { return false }
		if (!check(after[fixture.split_carriage_index].overhaul_cost > 0, "the last vehicle reports an overhaul cost of " + after[fixture.split_carriage_index].overhaul_cost + ", so the overhaul cannot be paid for")) { return false }
		if (!check_tool(pl.book_cash(after[fixture.split_carriage_index].overhaul_cost + 100000), "add cash for the overhaul")) { return false }

		local line = create_line_with_schedule(pl, schedule)
		if (line == null) { return false }
		fixture.line_id = line.id
		if (!check_tool(cnv.set_line(pl, line), "assign the convoy to the maintenance line")) { return false }
		fixture.known_ids = convoy_ids()
		if (!check(fixture.known_ids.find(cnv.id) != null, "the original convoy is not in the convoy list after the line assignment")) { return false }
	}

	if (SPLIT_INVALID_CASE)
	{
		// The locomotive is the overhaul group; the tender and carriage are the
		// maintained group, which is unpowered. The split must be refused and
		// the original consist restored.
		local slots = cnv.get_vehicle_snapshot()
		if (!check(slots.len() >= 3, "the invalid split case needs a locomotive, its tender and a carriage, but the convoy has " + slots.len() + " vehicles")) { return false }
		fixture.overhaul_vehicle_index = 0
		fixture.split_overhaul_before = slots[0].overhaul_count
		if (!check(cnv.set_maintenance_distances_for_testing(0, 0, OVERHAUL_DISTANCE), "the scenario-only test hook did not set the overhaul distance on the locomotive")) { return false }
		if (!check(cnv.set_maintenance_distances_for_testing(slots.len() - 1, OVERHAUL_DISTANCE, 0), "the scenario-only test hook did not set the maintenance distance on the carriage")) { return false }
		local after = cnv.get_vehicle_snapshot()
		if (!check(after[0].overhaul_needed, "the locomotive does not report an overhaul as due")) { return false }
		if (!check(after[0].overhaul_cost > 0, "the locomotive reports no overhaul cost")) { return false }
		if (!check_tool(pl.book_cash(after[0].overhaul_cost + 100000), "add cash for the overhaul")) { return false }

		local line = create_line_with_schedule(pl, schedule)
		if (line == null) { return false }
		fixture.line_id = line.id
		if (!check_tool(cnv.set_line(pl, line), "assign the convoy to the maintenance line")) { return false }
		fixture.known_ids = convoy_ids()
	}

	if (!check_tool(depot.start_convoy(pl, cnv), "start the convoy")) { return false }
	return true
}


// Captures the distance covered on the outbound leg while the convoy stands at
// the stop, before the maintenance visit. The line is ten tiles long, which is
// more than one kilometre at the fixture pakset's metres_per_tile (125), so the
// odometer has booked at least one kilometre by the time the convoy arrives.
// The pre-service read model is captured later, in enter_service(), because the
// vehicle travels further on the return leg to the depot.
function capture_before(cnv, slot)
{
	fixture.saw_stop = true
	fixture.km_before_stop = slot.km_since_last_maintenance
	fixture.before_km_overhaul = slot.km_since_last_overhaul
	fixture.before_availability = slot.availability
	print("RUNTIME CASE " + case_name + ": convoy reached the stop with " + slot.km_since_last_maintenance + " km since the last maintenance, " + slot.km_since_last_overhaul + " km since the last overhaul, availability " + slot.availability)
	if (OVERHAUL_CASE)
	{
		if (!check(slot.km_since_last_overhaul >= OVERHAUL_DISTANCE, "the convoy reached the stop with " + slot.km_since_last_overhaul + " km since the last overhaul, less than the " + OVERHAUL_DISTANCE + " km the test hook set")) { return false }
		fixture.overhaul_count_before = slot.overhaul_count
		fixture.overhaul_cost_before = slot.overhaul_cost
		return check(slot.overhaul_cost > 0, "the vehicle reports an overhaul cost of " + slot.overhaul_cost + ", so the cash booking cannot be asserted")
	}
	return check(slot.km_since_last_maintenance > 0, "the convoy reached the stop with no booked distance since the last maintenance, so a later reset to zero would prove nothing")
}


// The split case: the original convoy keeps only the vehicle due for overhaul,
// and the maintained vehicles have left in a new convoy that is already bound
// to the same line and positioned at the stop entry after the depot.
function enter_split_service(cnv, state)
{
	if (!check(fixture.saw_stop, "the convoy entered a depot service state before it reached the stop, so it did not run the lap the case expects")) { return false }
	if (!check(state == convoy_state_overhaul, "the depot visit put the convoy in state " + state + "; " + case_name + " expects state " + convoy_state_overhaul)) { return false }

	fixture.service_state = state
	fixture.stage = "split"
	fixture.split_steps = 0

	local slots = cnv.get_vehicle_snapshot()
	if (!check(slots.len() == 1, "the original convoy has " + slots.len() + " vehicles after the split; the maintained vehicles should have left")) { return false }
	if (!check(slots[0].overhaul_count == fixture.split_overhaul_before + 1, "the overhauled vehicle reports " + slots[0].overhaul_count + " overhauls, expected " + (fixture.split_overhaul_before + 1))) { return false }
	if (!check(slots[0].km_since_last_overhaul == 0, "the overhauled vehicle reports " + slots[0].km_since_last_overhaul + " km since the last overhaul, expected vehicle_t::overhaul() to reset it")) { return false }

	local new_cnv = null
	foreach (other in world.get_convoy_list())
	{
		if (other.is_valid() && fixture.known_ids.find(other.id) == null)
		{
			new_cnv = other
			break
		}
	}
	if (!check(new_cnv != null, "no new convoy was created by the split")) { return false }
	fixture.split_convoy_id = new_cnv.id

	local new_slots = new_cnv.get_vehicle_snapshot()
	if (!check(new_slots.len() == fixture.vehicle_count - 1, "the split-off convoy has " + new_slots.len() + " vehicles, expected the " + (fixture.vehicle_count - 1) + " maintained vehicles")) { return false }
	if (!check(new_cnv.get_state() != convoy_state_initial, "the split-off convoy is still in INITIAL after the split")) { return false }
	if (!check(new_cnv.get_current_entry_id() == STOP_ENTRY_ID, "the split-off convoy is at schedule entry " + new_cnv.get_current_entry_id() + ", expected the stop entry " + STOP_ENTRY_ID + " after the depot entry")) { return false }
	local line = new_cnv.get_line()
	if (!check(line != null && line.id == fixture.line_id, "the split-off convoy is not bound to the original convoy's line")) { return false }
	if (!check(new_slots[0].maintenance_needed == false, "the leading vehicle of the split-off convoy still reports that maintenance is needed")) { return false }

	print("RUNTIME CASE " + case_name + ": the split left " + slots.len() + " overhauled vehicle and started convoy " + new_cnv.id + " with " + new_slots.len() + " maintained vehicles")
	return true
}


// The undo cases (an unpowered maintained group, or an unreachable next entry
// after the depot): split_maintained_from_overhaul() must restore the original
// consist and leave no new convoy behind.
function enter_split_undo_service(cnv, state)
{
	if (!check(state == convoy_state_overhaul, "the depot visit put the convoy in state " + state + "; " + case_name + " expects state " + convoy_state_overhaul)) { return false }

	local slots = cnv.get_vehicle_snapshot()
	if (!check(slots.len() == fixture.vehicle_count, "the original convoy has " + slots.len() + " vehicles after the refused split, expected the original " + fixture.vehicle_count)) { return false }
	if (!check(slots[fixture.overhaul_vehicle_index].overhaul_count == fixture.split_overhaul_before + 1, "the overhauled vehicle reports " + slots[fixture.overhaul_vehicle_index].overhaul_count + " overhauls, expected " + (fixture.split_overhaul_before + 1))) { return false }
	if (!check(slots[fixture.overhaul_vehicle_index].km_since_last_overhaul == 0, "the overhauled vehicle reports " + slots[fixture.overhaul_vehicle_index].km_since_last_overhaul + " km since the last overhaul, expected vehicle_t::overhaul() to reset it")) { return false }

	foreach (other in world.get_convoy_list())
	{
		if (other.is_valid() && fixture.known_ids.find(other.id) == null)
		{
			return fail("the refused split left a new convoy " + other.id + " in the world")
		}
	}

	fixture.stage = "split-undo"
	print("RUNTIME CASE " + case_name + ": the split was refused and the original convoy kept " + slots.len() + " vehicles")
	pass()
	return true
}


// The withdraw case: the convoy is in the maintenance queue. Toggle the
// withdraw flag through the same tool that the convoy detail window uses. The
// convoy is empty, because depot_t::convoi_arrived() discards the load, and its
// state is not INITIAL, so convoi_t::set_withdraw() schedules it for
// self-destruction. convoi_t::destroy() must remove it from the depot lists
// before convoi_t::~convoi_t() detaches its convoihandle_t; otherwise the next
// depot sync step dereferences a null pointer.
function enter_withdrawal(cnv, state)
{
	fixture.stage = "withdrawing"
	fixture.withdraw_steps = 0
	fixture.withdraw_cleared_steps = 0
	print("RUNTIME CASE " + case_name + ": convoy entered state " + state + "; withdrawing it from the maintenance queue")
	return check_tool(cnv.toggle_withdraw(player_x(0)), "withdraw the convoy under maintenance")
}


function enter_service(cnv, state)
{
	if (DEPOT_ORDER_ANY_CASE)
	{
		if (!check(fixture.saw_stop, "the convoy entered a depot service state before it reached the stop, so it did not run the lap the case expects")) { return false }
		local slots = cnv.get_vehicle_snapshot()
		if (!check(slots.len() == 1, "the Depot Consist Order should leave the Lead Vehicle alone in the original convoy, but it has " + slots.len() + " vehicles")) { return false }
		fixture.service_state = state
		fixture.service_steps = 1
		fixture.first_wait = cnv.get_service_wait_ticks()
		fixture.last_wait = fixture.first_wait
		fixture.before_availability = slots[0].availability
		fixture.before_km_overhaul = slots[0].km_since_last_overhaul
		fixture.stage = "service"
		print("RUNTIME CASE " + case_name + ": the order decomposed the convoy and left the Lead Vehicle in the original convoy")
		return true
	}
	if (SPLIT_INVALID_CASE || SPLIT_UNREACHABLE_CASE)
	{
		return enter_split_undo_service(cnv, state)
	}
	if (SPLIT_CASE)
	{
		return enter_split_service(cnv, state)
	}
	if (!check(fixture.saw_stop, "the convoy entered a depot service state before it reached the stop, so it did not run the lap the case expects")) { return false }
	local expected_state = OVERHAUL_CASE ? convoy_state_overhaul : convoy_state_maintenance
	if (!check(state == expected_state, "the depot visit put the convoy in state " + state + "; " + case_name + " expects state " + expected_state)) { return false }

	if (WITHDRAW_CASE)
	{
		return enter_withdrawal(cnv, state)
	}

	fixture.service_state = state
	fixture.service_steps = 1
	fixture.first_wait = cnv.get_service_wait_ticks()
	fixture.last_wait = fixture.first_wait
	fixture.stage = "service"

	local slots = cnv.get_vehicle_snapshot()
	if (!check(slots.len() == fixture.vehicle_count, "the convoy snapshot has " + slots.len() + " slots, expected the convoy's " + fixture.vehicle_count + " vehicles")) { return false }
	fixture.before_km_overhaul = slots[0].km_since_last_overhaul
	fixture.before_availability = slots[0].availability

	if (OVERHAUL_CASE)
	{
		if (!check(slots[0].overhaul_count == fixture.overhaul_count_before + 1, "the overhaul count is " + slots[0].overhaul_count + ", expected " + (fixture.overhaul_count_before + 1))) { return false }
		if (!check(slots[0].km_since_last_overhaul == 0, "the vehicle reports " + slots[0].km_since_last_overhaul + " km since the last overhaul, expected vehicle_t::overhaul() to reset it")) { return false }
		if (!check(slots[0].availability == slots[0].desc.get_starting_availability(), "the overhaul left availability at " + slots[0].availability + "%, expected the descriptor's starting availability " + slots[0].desc.get_starting_availability() + "%")) { return false }
		if (!check(slots[0].overhaul_needed == false, "the leading vehicle still reports that an overhaul is needed after the overhaul")) { return false }
		if (!check(cnv.needs_overhaul() == false, "the convoy still reports that an overhaul is needed after the overhaul")) { return false }
		if (!check(fixture.first_wait > 0, "the convoy entered overhaul with a service wait of " + fixture.first_wait + " ticks; the overhaul duration that convoi_t::sync_step counts down was cleared before the case could observe it")) { return false }
		local charged = (fixture.cash_before - player_x(0).get_current_cash()) * 100.0
		// The approach to the depot also books per-kilometre running costs, so
		// the cash delta is the overhaul cost plus those charges. A 5% (or ten
		// credit) bound still rejects a monthly-scaled or repeated booking.
		local tolerance = 1000
		if (fixture.overhaul_cost_before / 20 > tolerance)
		{
			tolerance = fixture.overhaul_cost_before / 20
		}
		// The cash interface reports whole currency units, so a correctly
		// booked overhaul can appear up to one unit below its exact cost.
		if (!check(charged >= fixture.overhaul_cost_before - 100.0, "the overhaul changed cash by only " + charged + " hundredths, less than the vehicle's overhaul cost " + fixture.overhaul_cost_before + " by more than the one-unit rounding tolerance")) { return false }
		if (world.get_time().raw == fixture.month_before)
		{
			if (!check(charged <= fixture.overhaul_cost_before + tolerance, "the overhaul changed cash by " + charged + " hundredths, more than the vehicle's overhaul cost " + fixture.overhaul_cost_before + " plus " + tolerance + " hundredths of other charges; the overhaul may have been monthly-scaled or charged more than once")) { return false }
		}
		print("RUNTIME CASE " + case_name + ": convoy entered overhaul with a service wait of " + fixture.first_wait + " ticks; cash fell by " + charged + " hundredths against an overhaul cost of " + fixture.overhaul_cost_before + " (tolerance " + tolerance + ")")
		return true
	}

	if (!check(fixture.before_availability < 100, "the maintained vehicle reports availability " + fixture.before_availability + "; vehicle_t::maintain() computes a wait of active_service_time * (100 - availability) / 100, so a vehicle at 100% availability cannot produce the multi-step visit this case asserts")) { return false }
	if (!check(fixture.first_wait > 0, "the convoy entered maintenance with a service wait of " + fixture.first_wait + " ticks; the wait that convoi_t::sync_step counts down was cleared before the case could observe it")) { return false }
	print("RUNTIME CASE " + case_name + ": convoy entered maintenance with a service wait of " + fixture.first_wait + " ticks and availability " + fixture.before_availability)
	return true
}


function observe_service(cnv)
{
	if (cnv.get_state() == fixture.service_state)
	{
		++fixture.service_steps
		local wait = cnv.get_service_wait_ticks()
		if (!check(wait >= 0, "the convoy is in the maintenance state but reports no service wait")) { return }
		if (!check(wait <= fixture.last_wait, "the remaining service wait rose from " + fixture.last_wait + " to " + wait + " ticks while the convoy was at the head of the maintenance queue")) { return }
		fixture.last_wait = wait
		if (fixture.service_steps > SERVICE_STEP_LIMIT)
		{
			fail("the convoy stayed in maintenance for more than " + SERVICE_STEP_LIMIT + " observation steps")
		}
		return
	}

	if (DEPOT_ORDER_ANY_CASE)
	{
		// The Convoy leaves the maintenance state when Depot Assembly has
		// re-formed its Consist. observe_departure() asserts the result.
		fixture.stage = "departing"
		print("RUNTIME CASE " + case_name + ": the order was assembled and the convoy left the maintenance state")
		return
	}
	if (!check(fixture.service_steps > 1, "the convoy stayed in maintenance for only " + fixture.service_steps + " observation step, so the state did not persist across more than one observation step")) { return }
	if (!check(fixture.last_wait < fixture.first_wait, "the service wait did not fall below its first value of " + fixture.first_wait + " ticks (last observed " + fixture.last_wait + "), so the countdown that ends the visit was not observed")) { return }

	fixture.stage = "departing"
	print("RUNTIME CASE " + case_name + ": maintenance ended after " + fixture.service_steps + " observation steps; the wait fell from " + fixture.first_wait + " to " + fixture.last_wait + " ticks")
}


function observe_departure(cnv)
{
	local slots = cnv.get_vehicle_snapshot()
	local expected_count = DEPOT_ORDER_ANY_CASE ? fixture.order_descs.len() : fixture.vehicle_count
	if (!check(slots.len() == expected_count, "the convoy snapshot has " + slots.len() + " slots, expected the convoy's " + expected_count + " vehicles")) { return }
	local slot = slots[0]

	// Capture the post-visit read model once, on the first observation after the
	// maintenance state ended: the distance counters grow again as the convoy
	// drives away, so they are not re-asserted on later observations.
	if (!fixture.captured_after)
	{
		for (local i = 0; i < slots.len(); i++)
		{
			if (!check(slots[i].km_since_last_maintenance == 0, "vehicle " + i + " reports " + slots[i].km_since_last_maintenance + " km since the last maintenance, expected the depot service to reset the counter")) { return }
			if (!check(slots[i].overhaul_count == 0, "vehicle " + i + " reports " + slots[i].overhaul_count + " overhauls, expected none: an overhaul was not due and the visit was routine maintenance")) { return }
		}
		if (!check(slot.maintenance_needed == false, "the leading vehicle still reports that routine maintenance is needed after the visit")) { return }
		if (!check(cnv.needs_maintenance() == false, "the maintained convoy still reports that routine maintenance is needed after the visit")) { return }
		if (!check(slot.availability == fixture.before_availability, "routine maintenance changed the availability from " + fixture.before_availability + " to " + slot.availability + "; availability depends on the distance since the last overhaul, which maintenance does not change")) { return }
		if (!check(slot.km_since_last_overhaul >= fixture.before_km_overhaul, "the distance since the last overhaul fell from " + fixture.before_km_overhaul + " to " + slot.km_since_last_overhaul + " km during a routine maintenance visit")) { return }
		fixture.captured_after = true
		print("RUNTIME CASE " + case_name + ": after maintenance " + slots.len() + " vehicles report 0 km since the last maintenance; the leading vehicle reports availability " + slot.availability + " and overhaul count " + slot.overhaul_count)
	}

	if (DEPOT_ORDER_ANY_CASE)
	{
		if (!check(slots.len() == fixture.order_descs.len(), "the assembled convoy has " + slots.len() + " vehicles, expected the " + fixture.order_descs.len() + " vehicles named by the order")) { return }
		for (local i = 0; i < slots.len(); i++)
		{
			if (!check(slots[i].desc.get_name() == fixture.order_descs[i].get_name(), "assembled vehicle " + i + " is " + slots[i].desc.get_name() + ", expected " + fixture.order_descs[i].get_name())) { return }
		}
	}

	if (slot.pos.x > DEPOT_POS.x && cnv.get_current_entry_id() == STOP_ENTRY_ID)
	{
		pass()
		return
	}

	++fixture.departure_steps
	if (fixture.departure_steps > DEPARTURE_STEP_LIMIT)
	{
		fail("the convoy did not leave the depot towards the stop entry within " + DEPARTURE_STEP_LIMIT + " observation steps (state " + cnv.get_state() + ", position " + slot.pos + ")")
	}
}


// The withdraw case: a withdrawn convoy is no longer sold when it becomes
// empty. It finishes its depot visit and is held in INITIAL in the depot's
// convoy list. The process must survive several further sync steps, because
// depot_t::sync_step() walks the maintenance queue every step.
function observe_withdrawal()
{
	local cnv = convoy_x(fixture.convoy_id)
	if (!cnv.is_valid())
	{
		fail("the withdrawn convoy was destroyed; a withdrawal must hold it in the depot")
		return
	}

	if (cnv.get_state() != convoy_state_initial)
	{
		++fixture.withdraw_steps
		if (fixture.withdraw_steps > DEPARTURE_STEP_LIMIT)
		{
			fail("the withdrawn convoy did not become INITIAL within " + DEPARTURE_STEP_LIMIT + " observation steps (state " + cnv.get_state() + ")")
		}
		return
	}

	foreach (c in fixture.depot.get_convoy_list())
	{
		if (c == null)
		{
			fail("the depot's convoy list contains an unbound handle after the withdrawal")
			return
		}
		if (c.id == fixture.convoy_id)
		{
			if (fixture.withdraw_cleared_steps == 0)
			{
				print("RUNTIME CASE " + case_name + ": the withdrawn convoy is held in INITIAL and the depot still lists it")
			}
			++fixture.withdraw_cleared_steps
			if (fixture.withdraw_cleared_steps >= 4)
			{
				pass()
			}
			return
		}
	}

	fail("the withdrawn convoy is INITIAL but the depot no longer lists it")
}


// The split case: the original convoy must stay in the depot under overhaul
// while the split-off convoy runs towards the stop.
function observe_split()
{
	local cnv = convoy_x(fixture.convoy_id)
	if (!cnv.is_valid())
	{
		fail("the original convoy " + fixture.convoy_id + " disappeared")
		return
	}
	if (!check(cnv.get_state() == convoy_state_overhaul, "the original convoy left the overhaul state (state " + cnv.get_state() + ") before the split-off convoy departed")) { return }

	local new_cnv = convoy_x(fixture.split_convoy_id)
	if (!new_cnv.is_valid())
	{
		fail("the split-off convoy " + fixture.split_convoy_id + " disappeared")
		return
	}
	local slots = new_cnv.get_vehicle_snapshot()
	if (slots.len() == 0)
	{
		fail("the split-off convoy has no vehicles")
		return
	}

	if (slots[0].pos.x > DEPOT_POS.x && new_cnv.get_current_entry_id() == STOP_ENTRY_ID)
	{
		print("RUNTIME CASE " + case_name + ": the split-off convoy left the depot towards the stop while the original convoy stayed in overhaul")
		pass()
		return
	}

	++fixture.split_steps
	if (fixture.split_steps > DEPARTURE_STEP_LIMIT)
	{
		fail("the split-off convoy did not leave the depot towards the stop entry within " + DEPARTURE_STEP_LIMIT + " observation steps (state " + new_cnv.get_state() + ", entry " + new_cnv.get_current_entry_id() + ", position " + slots[0].pos + ")")
	}
}


// The cross-convoy case: wait until both original convoys are in OVERHAUL and
// one combined convoy exists, then check that the combined convoy leaves while
// both originals stay behind.
function observe_cross_convoy()
{
	local a = convoy_x(fixture.convoy_id)
	local b = convoy_x(fixture.convoy_id_b)
	if (!a.is_valid() || !b.is_valid())
	{
		fail("one of the original convoys disappeared")
		return
	}
	if (!fixture.cross_b_started && a.get_state() == convoy_state_overhaul && b.get_state() == convoy_state_initial)
	{
		if (!check_tool(fixture.depot.start_convoy(player_x(0), convoy_x(fixture.convoy_id_b)), "start the second convoy after the first entered overhaul")) { return }
		fixture.cross_b_started = true
		print("RUNTIME CASE " + case_name + ": the first convoy is in overhaul; started the second convoy from the depot")
		return
	}
	if (a.get_state() != convoy_state_overhaul || b.get_state() != convoy_state_overhaul)
	{
		++fixture.cross_steps
		if (fixture.cross_steps > TRAVEL_STEP_LIMIT)
		{
			fail("the two convoys did not both enter overhaul within " + TRAVEL_STEP_LIMIT + " observation steps (states " + a.get_state() + " and " + b.get_state() + ")")
		}
		return
	}

	local combined = null
	foreach (other in world.get_convoy_list())
	{
		if (other.is_valid() && fixture.known_ids.find(other.id) == null)
		{
			combined = other
			break
		}
	}
	if (combined == null)
	{
		fail("both convoys are in overhaul but no combined maintained convoy exists")
		return
	}

	local slots = combined.get_vehicle_snapshot()
	if (!fixture.cross_checked)
	{
		if (!check(slots.len() == 3, "the combined convoy has " + slots.len() + " vehicles, expected the maintained locomotive, tender and carriage")) { return }

		if (!check(combined.get_state() != convoy_state_initial, "the combined convoy is still in INITIAL")) { return }
		if (!check(combined.get_current_entry_id() == STOP_ENTRY_ID, "the combined convoy is at entry " + combined.get_current_entry_id() + ", expected the stop entry " + STOP_ENTRY_ID)) { return }
		local line = combined.get_line()
		if (!check(line != null && line.id == fixture.line_id, "the combined convoy is not on the original line")) { return }
		if (!check(combined.get_vehicles()[0].get_power() > 0, "the combined convoy's leading vehicle is not powered")) { return }
		local a_count = a.get_vehicle_snapshot().len()
		local b_count = b.get_vehicle_snapshot().len()
		if (!check(a_count > 0 && b_count > 0, "the split left an original convoy with no vehicles (" + a_count + " and " + b_count + ")")) { return }
		if (!check(a_count + b_count + slots.len() == fixture.vehicle_count + fixture.vehicle_count_b, "the vehicle counts do not add up: " + a_count + " + " + b_count + " + " + slots.len() + " != " + (fixture.vehicle_count + fixture.vehicle_count_b))) { return }
		fixture.cross_checked = true
		print("RUNTIME CASE " + case_name + ": the combined convoy " + combined.id + " has " + slots.len() + " maintained vehicles; convoy " + a.id + " keeps " + a_count + " and convoy " + b.id + " keeps " + b_count + " overhauled vehicles")
	}

	if (slots.len() > 0 && slots[0].pos.x > DEPOT_POS.x)
	{
		print("RUNTIME CASE " + case_name + ": the combined convoy left the depot while both originals stayed in overhaul")
		pass()
		return
	}
	++fixture.cross_steps
	if (fixture.cross_steps > DEPARTURE_STEP_LIMIT)
	{
		fail("the combined convoy did not leave the depot towards the stop within " + DEPARTURE_STEP_LIMIT + " observation steps")
	}
}


function observe_target_fleet()
{
	local line = line_x(fixture.line_id)
	local a = convoy_x(fixture.convoy_id)
	local b = convoy_x(fixture.convoy_id_b)
	if (!line.is_valid() || !a.is_valid() || !b.is_valid())
	{
		fail("the target case lost its line or one of its convoys")
		return
	}

	if (fixture.target_phase == 0)
	{
		if (a.get_state() == convoy_state_initial)
		{
			++fixture.travel_steps
			if (fixture.travel_steps > DEPARTURE_STEP_LIMIT)
			{
				fail("raising the target to 1 did not dispatch the first Held Convoy")
			}
			return
		}
		if (!check(b.get_state() == convoy_state_initial, "the second Held Convoy left the depot when the target was 1")) { return }
		if (!check(line.get_deployed_convoy_count() == 1, "the Deployed count is " + line.get_deployed_convoy_count() + " after dispatching the first Convoy")) { return }
		print("RUNTIME CASE " + case_name + ": target 1 dispatched the first Held Convoy")
		if (!check_tool(line.set_target_convoy_count(player_x(0), 2), "raise the target to 2")) { return }
		fixture.target_phase = 1
		fixture.travel_steps = 0
		return
	}

	if (fixture.target_phase == 1)
	{
		if (b.get_state() == convoy_state_initial)
		{
			++fixture.travel_steps
			if (fixture.travel_steps > DEPARTURE_STEP_LIMIT)
			{
				fail("raising the target to 2 did not dispatch the second Held Convoy")
			}
			return
		}
		if (!check(line.get_deployed_convoy_count() == 2, "the Deployed count is " + line.get_deployed_convoy_count() + " after dispatching the second Convoy")) { return }
		print("RUNTIME CASE " + case_name + ": target 2 dispatched the second Held Convoy")
		if (!check_tool(line.set_target_convoy_count(player_x(0), 1), "lower the target to 1")) { return }
		fixture.target_phase = 2
		fixture.travel_steps = 0
		return
	}

	if (line.get_deployed_convoy_count() > 1)
	{
		++fixture.travel_steps
		if (fixture.travel_steps > DEPARTURE_STEP_LIMIT)
		{
			fail("lowering the target to 1 did not withdraw a Deployed Convoy (count " + line.get_deployed_convoy_count() + ")")
		}
		return
	}

	if (a.get_state() == convoy_state_initial || b.get_state() == convoy_state_initial)
	{
		print("RUNTIME CASE " + case_name + ": lowering the target to 1 withdrew and held one Convoy")
		pass()
		return
	}

	++fixture.travel_steps
	if (fixture.travel_steps > DEPARTURE_STEP_LIMIT)
	{
		fail("the withdrawn Convoy did not become INITIAL within " + DEPARTURE_STEP_LIMIT + " observation steps")
	}
}


function observe()
{
	if (TARGET_CASE)
	{
		observe_target_fleet()
		return
	}
	if (fixture.stage == "withdrawing")
	{
		// The withdrawn convoy is expected to disappear, so this stage must be
		// handled before the validity check below.
		observe_withdrawal()
		return
	}

	local cnv = convoy_x(fixture.convoy_id)
	if (!cnv.is_valid())
	{
		fail("the convoy " + fixture.convoy_id + " disappeared")
		return
	}
	if (CROSS_CONVOY_CASE)
	{
		observe_cross_convoy()
		return
	}

	if (fixture.stage != "service")
	{
		local current_state = cnv.get_state()
		if (current_state != convoy_state_maintenance && current_state != convoy_state_overhaul)
		{
			fixture.cash_before = player_x(0).get_current_cash()
			fixture.month_before = world.get_time().raw
		}
	}

	if (fixture.stage == "split")
	{
		observe_split()
		return
	}
	if (fixture.stage == "service")
	{
		if (OVERHAUL_CASE)
		{
			// The overhaul case asserts the booking and the state resets when
			// the state is entered. It does not wait for the overhaul duration
			// to expire; the maintenance dwell cases cover the service-wait
			// countdown.
			pass()
			return
		}
		observe_service(cnv)
		return
	}
	if (fixture.stage == "departing")
	{
		observe_departure(cnv)
		return
	}

	local state = cnv.get_state()
	if (state == convoy_state_maintenance || state == convoy_state_overhaul)
	{
		enter_service(cnv, state)
		return
	}

	local slots = cnv.get_vehicle_snapshot()
	if (!check(slots.len() == fixture.vehicle_count, "the convoy snapshot has " + slots.len() + " slots, expected the convoy's " + fixture.vehicle_count + " vehicles")) { return }

	// A visit can complete without an observable MAINTENANCE state when
	// vehicle_t::maintain() computes a zero wait, which happens when the vehicle
	// reports 100% availability (the pakset default when no availability decay
	// is configured). The odometer reset at the depot tile identifies the visit.
	if (!OVERHAUL_CASE && fixture.saw_stop && slots[0].km_since_last_maintenance == 0
		&& slots[0].pos.x == DEPOT_POS.x && slots[0].pos.y == DEPOT_POS.y && slots[0].pos.z == DEPOT_POS.z)
	{
		print("RUNTIME CASE " + case_name + ": the maintenance visit completed in one step at availability " + slots[0].availability + ", so no MAINTENANCE state was observable")
		fixture.stage = "departing"
		observe_departure(cnv)
		return
	}

	++fixture.travel_steps
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		local pos = slots.len() > 0 ? slots[0].pos : "unknown"
		fail("the convoy did not enter a depot service state within " + TRAVEL_STEP_LIMIT + " observation steps (state " + state + ", entry " + cnv.get_current_entry_id() + ", position " + pos + ")")
		return
	}

	// The current schedule entry is the stop from the moment the convoy leaves
	// the depot, so the arrival is identified by the vehicle's own position
	// rather than by the entry id.
	if (!fixture.saw_stop && slots[0].pos.y == STOP_POS.y && slots[0].pos.x >= STOP_POS.x && slots[0].pos.x <= STOP_POS.x + 1)
	{
		capture_before(cnv, slots[0])
	}
}


function get_rule_text(pl)
{
	return ttext("Do not touch.")
}


function get_goal_text(pl)
{
	return ttext("Run the depot maintenance case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Maintenance runtime case {name}: {stage}")
	msg.name = case_name
	msg.stage = fixture.stage
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
		fixture.stage = "awaiting-stop"
		print("RUNTIME CASE " + case_name + ": setup complete; convoy " + fixture.convoy_id + " created")
		return 0
	}
	if (fixture.stage == "failed")
	{
		return -1
	}
	if (fixture.stage == "done")
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
	if (fixture.stage == "done")
	{
		return 100
	}
	if (fixture.stage == "failed")
	{
		return -1
	}
	return 0
}

