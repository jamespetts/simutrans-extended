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
	overhaul_cost_before = 0
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

	local stop_entry = make_entry(STOP_POS, STOP_ENTRY_ID)
	local depot_entry = make_entry(DEPOT_POS, DEPOT_ENTRY_ID)
	// The depot entry requests routine maintenance on arrival. It is also the
	// line's terminus, so the convoy reverses there.
	depot_entry.load = DEPOT_FLAG_MAINTAIN_OR_OVERHAUL
	depot_entry.reverse = 1

	local schedule = schedule_x(WAYTYPE, [stop_entry, depot_entry])
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the convoy schedule")) { return false }

	// Read the depot entry's visit flags back: the case depends on the value
	// surviving the schedule commit.
	local back = cnv.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the committed schedule does not have two entries")) { return false }
	if (!check(back.entries[0].unique_entry_id == STOP_ENTRY_ID, "schedule entry 0 is not the stop entry " + STOP_ENTRY_ID)) { return false }
	if (!check(back.entries[1].unique_entry_id == DEPOT_ENTRY_ID, "schedule entry 1 is not the depot entry " + DEPOT_ENTRY_ID)) { return false }
	if (!check(back.entries[1].load == DEPOT_FLAG_MAINTAIN_OR_OVERHAUL, "the depot entry reports load " + back.entries[1].load + ", expected the maintain_or_overhaul visit flag " + DEPOT_FLAG_MAINTAIN_OR_OVERHAUL)) { return false }
	if (!check(back.entries[1].reverse == 1, "the depot entry reports reverse " + back.entries[1].reverse + ", expected 1 so that the route search stops at the depot instead of treating it as a waypoint")) { return false }

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


function enter_service(cnv, state)
{
	if (!check(fixture.saw_stop, "the convoy entered a depot service state before it reached the stop, so it did not run the lap the case expects")) { return false }
	local expected_state = OVERHAUL_CASE ? convoy_state_overhaul : convoy_state_maintenance
	if (!check(state == expected_state, "the depot visit put the convoy in state " + state + "; " + case_name + " expects state " + expected_state)) { return false }

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
		if (!check(charged >= fixture.overhaul_cost_before - 0.5, "the overhaul changed cash by only " + charged + " hundredths, less than the vehicle's overhaul cost " + fixture.overhaul_cost_before)) { return false }
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

	if (!check(fixture.service_steps > 1, "the convoy stayed in maintenance for only " + fixture.service_steps + " observation step, so the state did not persist across more than one observation step")) { return }
	if (!check(fixture.last_wait < fixture.first_wait, "the service wait did not fall below its first value of " + fixture.first_wait + " ticks (last observed " + fixture.last_wait + "), so the countdown that ends the visit was not observed")) { return }

	fixture.stage = "departing"
	print("RUNTIME CASE " + case_name + ": maintenance ended after " + fixture.service_steps + " observation steps; the wait fell from " + fixture.first_wait + " to " + fixture.last_wait + " ticks")
}


function observe_departure(cnv)
{
	local slots = cnv.get_vehicle_snapshot()
	if (!check(slots.len() == fixture.vehicle_count, "the convoy snapshot has " + slots.len() + " slots, expected the convoy's " + fixture.vehicle_count + " vehicles")) { return }
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


function observe()
{
	local cnv = convoy_x(fixture.convoy_id)
	if (!cnv.is_valid())
	{
		fail("the convoy " + fixture.convoy_id + " disappeared")
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

