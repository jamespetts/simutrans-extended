//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Shared world builder and assertions for the paired H exchange runtime cases
// under tests/pending-runtime/pair-cases.
//
// A case selects itself by setting ::case_kind before including this file. The
// runner scripts/run-pending-runtime-tests.sh stages the case's scenario.nut,
// this file and empty-16x16.sve in the pakset scenario directory and runs one
// game process per case, so every case starts from the empty world.
//
// Track runs east from the depot at (1,2) through the trunk to (6,2); a branch
// continues east to the east stop (9,2) and another runs north to the north stop
// (6,0). A passing loop from (4,2) through the loop platform (5,3) to (7,2)
// gives the junction stop two platforms on separate tracks, so two convoys can
// stand at it at once. Both platforms must belong to one halt for the pair to
// resolve.
//
// Two lineless convoys are created in the depot. The receiver's pair entry at
// the main platform (5,2) carries the couple flag; the target's pair entry at
// the loop platform (5,3) carries the uncouple flag. Each names the other
// convoy and the other entry's unique id, and each entry has an end-state
// consist order. The case starts one convoy first and waits until it is in
// AWAITING_RECOMBINATION at its pair entry, then starts the other. The other
// executes the exchange. Every observation below uses only the public script
// interface.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Paired H exchange runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

local MAIN_POS = coord3d(5, 2, 0)
local LOOP_POS = coord3d(5, 3, 0)
local EAST_POS = coord3d(9, 2, 0)
local NORTH_POS = coord3d(6, 0, 0)

local RECEIVER_ENTRY_ID = 11
local RECEIVER_NEXT_ENTRY_ID = 12
local TARGET_ENTRY_ID = 21
local TARGET_NEXT_ENTRY_ID = 22

local DONOR_ENTRY_ID = 31
local DONOR_EAST_ENTRY_ID = 32

// Observation step limits. These are counts of scenario observations, not
// game-time quantities. The travel limit is wide because the first convoy may
// wait while the second is still in the depot; the departure limit covers the
// configured shunting delay.
local TRAVEL_STEP_LIMIT = 6000
local EXCHANGE_STEP_LIMIT = 1000
local DEPARTURE_STEP_LIMIT = 3000
// The reload runner sets shunting_time_seconds to 600 so that the case has not
// advanced while the client joins; the post-reload departure limit must cover
// that delay.
local RELOAD_DEPARTURE_STEP_LIMIT = 16000
local PREPARATION_STEP_LIMIT = 3000

local fixture = {
	phase = "waiting-for-setup",
	setup_started = false,
	completed = false,
	depot = null,
	receiver_id = 0,
	target_id = 0,
	donor_id = 0,
	donor_removed = false,
	first_kind = "receiver",
	first_waited = false,
	receiver_advanced = false,
	target_advanced = false,
	loco_a = null,
	loco_b = null,
	loco_c = null,
	coach_a = null,
	coach_b = null,
	pool_coach = null,
	rule_min_topspeed = 0,
	steps = 0,
	travel_steps = 0,
	exchange_steps = 0,
	departure_steps = 0,
	preparation_steps = 0,
	after_reload = false,
	reload_expected = null
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


function check_tool(result, what)
{
	if (result == null || result == true)
	{
		return true
	}
	return fail(what + ": " + result)
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


function vehicle_matches(desc, expected)
{
	return desc != null && expected != null && desc.is_equal(expected)
}


function consist_matches(cnv, expected)
{
	if (!cnv.is_valid())
	{
		return false
	}
	local vehicles = cnv.get_vehicles()
	if (vehicles.len() != expected.len())
	{
		return false
	}
	for (local i = 0; i < expected.len(); i++)
	{
		if (!vehicle_matches(vehicles[i], expected[i]))
		{
			return false
		}
	}
	return true
}


function consist_name(expected)
{
	local names = ""
	foreach (desc in expected)
	{
		if (names.len() > 0) { names += ", " }
		names += desc.get_name()
	}
	return "[" + names + "]"
}


function convoy_name(cnv)
{
	if (cnv == null || !cnv.is_valid())
	{
		return "<invalid convoy>"
	}
	return consist_name(cnv.get_vehicles())
}


function depict_state(state)
{
	switch (state)
	{
		case convoy_state_driving: return "driving"
		case convoy_state_loading: return "loading"
		case convoy_state_shunting: return "shunting"
		case convoy_state_awaiting_recombination: return "awaiting recombination"
		case convoy_state_layover: return "layover"
		case convoy_state_can_start: return "can start"
		case convoy_state_initial: return "initial"
		case convoy_state_routing_1: return "routing 1"
		case convoy_state_routing_2: return "routing 2"
		case convoy_state_no_route: return "no route"
		case convoy_state_reversing: return "reversing"
		case convoy_state_leaving_depot: return "leaving depot"
	}
	return "state " + state
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

	if (!check_tool(command_x.build_way(pl, coord3d(1, 2, 0), coord3d(6, 2, 0), rail, false), "build trunk")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build east branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(6, 0, 0), rail, false), "build north branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 2, 0), coord3d(4, 3, 0), rail, false), "build loop west link")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(7, 3, 0), rail, false), "build loop")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(7, 3, 0), coord3d(7, 2, 0), rail, false), "build loop east link")) { return false }

	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depot_desc_for(wt_rail)), "build rail depot")) { return false }
	local depot = depot_x(1, 2, 0)
	if (!check(depot != null, "depot not found after building")) { return false }
	fixture.depot = depot

	local station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
	if (!check(station != null, "no rail station available")) { return false }
	if (!check_tool(station_builder(pl, MAIN_POS, station), "build main junction platform")) { return false }
	if (!check_tool(station_builder(pl, LOOP_POS, station), "build loop junction platform")) { return false }
	if (!check_tool(station_builder(pl, EAST_POS, station), "build east stop")) { return false }
	if (!check_tool(station_builder(pl, NORTH_POS, station), "build north stop")) { return false }

	local main_halt = halt_x.get_halt(MAIN_POS, pl)
	local loop_halt = halt_x.get_halt(LOOP_POS, pl)
	if (!check(main_halt != null && loop_halt != null, "a junction platform has no halt")) { return false }
	if (!check(main_halt.get_name() == loop_halt.get_name(), "the junction main and loop platforms have different stop names, so they are separate halts and cannot form a pair")) { return false }

	return true
}


function coupons(a, b)
{
	return vehicle_desc_x.is_coupling_allowed(a, b)
}


// Collect rail vehicles that can run on non-electrified track and have no fixed
// successor (a fixed successor would make the depot append more vehicles than
// the case authors). Three powered descriptors (a, b, c) and two unpowered
// coaches are chosen so that every coupling the cases use is allowed.
function select_descriptors()
{
	local powered = []
	local unpowered = []
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.needs_electrification()) { continue }
		if (desc.get_successors().len() > 0) { continue }
		if (desc.get_power() > 0)
		{
			if (desc.can_be_first() && desc.can_be_last())
			{
				powered.append(desc)
			}
		}
		else if (desc.can_be_last())
		{
			unpowered.append(desc)
		}
	}
	if (powered.len() < 3 || unpowered.len() < 2)
	{
		return false
	}

	// Prefer a triple whose lowest-topspeed member can be excluded by a
	// min_topspeed rule that the other two satisfy.
	foreach (a in powered)
	{
		foreach (b in powered)
		{
			if (a.is_equal(b)) { continue }
			foreach (c in powered)
			{
				if (c.is_equal(a) || c.is_equal(b)) { continue }
				if (a.get_topspeed() >= b.get_topspeed() || a.get_topspeed() >= c.get_topspeed()) { continue }
				if (!coupons(b, c)) { continue }
				foreach (ca in unpowered)
				{
					foreach (cb in unpowered)
					{
						if (ca.is_equal(cb)) { continue }
						if (!coupons(a, ca) || !coupons(b, cb)) { continue }
						if (!coupons(a, cb) || !coupons(b, ca)) { continue }
						if (!coupons(c, ca)) { continue }
						if (!coupons(ca, cb)) { continue }
						fixture.loco_a = a
						fixture.loco_b = b
						fixture.loco_c = c
						fixture.coach_a = ca
						fixture.coach_b = cb
						fixture.rule_min_topspeed = min(b.get_topspeed(), c.get_topspeed())
						return true
					}
				}
			}
		}
	}
	return false
}


function make_entry(pos, unique_entry_id)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = unique_entry_id
	return entry
}


// Slot specifications are arrays of alternatives. A descriptor is a specific
// vehicle alternative; a table with min_topspeed is a rule alternative.
function make_order(slots)
{
	local order = consist_order_x()
	local result = []
	foreach (slot_spec in slots)
	{
		local slot = consist_order_element_x()
		local has_rule = false
		local alternatives = []
		foreach (spec in slot_spec)
		{
			local alternative = vehicle_description_x()
			if (typeof(spec) == "table")
			{
				has_rule = true
				// A rule alternative must be enforced; the element default is an
				// optional (wildcard) slot.
				alternative.empty = false
				if ("min_topspeed" in spec) { alternative.min_topspeed = spec.min_topspeed }
				if ("min_power" in spec) { alternative.min_power = spec.min_power }
			}
			else
			{
				alternative.specific_vehicle = spec
			}
			alternatives.append(alternative)
		}
		if (has_rule)
		{
			// A rule-only slot must accept every goods category, otherwise the
			// element's default category would filter the rule.
			slot.catg = 255
		}
		slot.alternatives = alternatives
		result.append(slot)
	}
	order.slots = result
	return order
}


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


function receiver_consist()
{
	return [fixture.loco_a, fixture.coach_a]
}


function target_consist()
{
	if (case_name == "conflict-backtrack")
	{
		return [fixture.loco_b, fixture.loco_c]
	}
	return [fixture.loco_b, fixture.coach_b]
}


function receiver_order()
{
	if (case_name == "symmetric-swap" || case_name == "symmetric-swap-target-first")
	{
		return [[fixture.loco_b], [fixture.coach_a]]
	}
	if (is_one_way_transfer_case())
	{
		return [[fixture.loco_a], [fixture.coach_b]]
	}
	if (case_name == "already-at-end-state")
	{
		return [[fixture.loco_a], [fixture.coach_a]]
	}
	if (case_name == "conflict-backtrack")
	{
		return [[{ min_topspeed = fixture.rule_min_topspeed }], [fixture.coach_a]]
	}
	if (case_name == "unsatisfiable")
	{
		// The named vehicle exists in the pakset but is in neither consist and
		// in no laid-over convoy, so the receiver's order cannot be completed.
		return [[fixture.loco_c], [fixture.coach_a]]
	}
	if (case_name == "pool-fallback")
	{
		return [[fixture.loco_a], [fixture.coach_a], [fixture.coach_b], [fixture.pool_coach]]
	}
	fail("no receiver order is defined for case " + case_name)
	return [[fixture.loco_a]]
}


function target_order()
{
	if (case_name == "symmetric-swap" || case_name == "symmetric-swap-target-first")
	{
		return [[fixture.loco_a], [fixture.coach_b]]
	}
	if (is_one_way_transfer_case())
	{
		return [[fixture.loco_b]]
	}
	if (case_name == "already-at-end-state")
	{
		return [[fixture.loco_b], [fixture.coach_b]]
	}
	if (case_name == "conflict-backtrack")
	{
		return [[fixture.loco_b]]
	}
	if (case_name == "unsatisfiable")
	{
		return [[fixture.loco_b], [fixture.coach_b]]
	}
	if (case_name == "pool-fallback")
	{
		return [[fixture.loco_b]]
	}
	fail("no target order is defined for case " + case_name)
	return [[fixture.loco_b]]
}


function receiver_final()
{
	if (case_name == "symmetric-swap" || case_name == "symmetric-swap-target-first")
	{
		return [fixture.loco_b, fixture.coach_a]
	}
	if (is_one_way_transfer_case())
	{
		return [fixture.loco_a, fixture.coach_b]
	}
	if (case_name == "already-at-end-state" || case_name == "unsatisfiable")
	{
		return [fixture.loco_a, fixture.coach_a]
	}
	if (case_name == "conflict-backtrack")
	{
		return [fixture.loco_c, fixture.coach_a]
	}
	if (case_name == "pool-fallback")
	{
		return [fixture.loco_a, fixture.coach_a, fixture.coach_b, fixture.pool_coach]
	}
	return null
}


function target_final()
{
	if (case_name == "symmetric-swap" || case_name == "symmetric-swap-target-first")
	{
		return [fixture.loco_a, fixture.coach_b]
	}
	if (is_one_way_transfer_case())
	{
		return [fixture.loco_b]
	}
	if (case_name == "already-at-end-state" || case_name == "unsatisfiable")
	{
		return [fixture.loco_b, fixture.coach_b]
	}
	if (case_name == "conflict-backtrack")
	{
		return [fixture.loco_b]
	}
	if (case_name == "pool-fallback")
	{
		return [fixture.loco_b]
	}
	return null
}


// The vehicles expected to become loose stock at the junction after the
// exchange, in the order they are recorded in the new laid-over convoy.
function expected_leftovers()
{
	if (is_one_way_transfer_case())
	{
		return [fixture.coach_a]
	}
	if (case_name == "conflict-backtrack")
	{
		return [fixture.loco_a]
	}
	return []
}


// The reload case uses the same one-way transfer as one-way-to-receiver: the
// receiver takes the target's coach and the receiver's own coach becomes loose
// stock at the junction. Both cases share the same orders, final consists and
// expected leftovers.
function is_one_way_transfer_case()
{
	return case_name == "one-way-to-receiver" || case_name == "pair-reload"
}


// A vehicle descriptor's script identity is not stable across a savegame load:
// vehicle_desc_x.get_name() is unreliable on ex-15 (see ai/known-bugs.md), and
// descriptors are reconstructed from the loaded pakset. The reload case
// therefore identifies the expected final consists by a fingerprint of scalar
// descriptor properties. The pre-reload phase asserts the exact descriptors
// with is_equal(); the post-reload phase asserts that the same fingerprints
// survived the load.
function descriptor_fingerprint(desc)
{
	if (desc == null)
	{
		return "<null>"
	}
	return desc.get_power() + ":"
		+ desc.get_topspeed() + ":"
		+ desc.get_weight() + ":"
		+ desc.get_length() + ":"
		+ (desc.needs_electrification() ? 1 : 0) + ":"
		+ (desc.can_be_first() ? 1 : 0) + ":"
		+ (desc.can_be_last() ? 1 : 0)
}


function fingerprint_sequence(descriptors)
{
	local result = []
	foreach (desc in descriptors)
	{
		result.append(descriptor_fingerprint(desc))
	}
	return result
}


function consist_matches_fingerprints(cnv, expected)
{
	if (!cnv.is_valid())
	{
		return false
	}
	local vehicles = cnv.get_vehicles()
	if (vehicles.len() != expected.len())
	{
		return false
	}
	for (local i = 0; i < expected.len(); i++)
	{
		if (descriptor_fingerprint(vehicles[i]) != expected[i])
		{
			return false
		}
	}
	return true
}


// Stores the identifiers and expected final consists of the committed
// exchange. Only scalars, strings and arrays are stored: the base scenario
// save() serialises the persistent table and evaluates it again on load, so
// descriptor handles cannot be persisted. The identifiers locate the convoys
// after the load; the fingerprints re-assert their final consists.
function remember_pair_reload_state()
{
	local receiver_final_fingerprints = fingerprint_sequence(receiver_final())
	local target_final_fingerprints = fingerprint_sequence(target_final())
	local leftover_fingerprints = fingerprint_sequence(expected_leftovers())
	fixture.reload_expected = {
		receiver = receiver_final_fingerprints,
		target = target_final_fingerprints,
		leftovers = leftover_fingerprints
	}
	persistent.reload <- {
		kind = case_name,
		receiver_id = fixture.receiver_id,
		target_id = fixture.target_id,
		receiver_final = receiver_final_fingerprints,
		target_final = target_final_fingerprints,
		leftovers = leftover_fingerprints
	}
}


function exchange_applied_reload()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		return false
	}
	if (receiver.get_state() == convoy_state_awaiting_recombination || target.get_state() == convoy_state_awaiting_recombination)
	{
		return false
	}
	if (!consist_matches_fingerprints(receiver, fixture.reload_expected.receiver))
	{
		return false
	}
	if (!consist_matches_fingerprints(target, fixture.reload_expected.target))
	{
		return false
	}
	return true
}


function check_leftovers_reload()
{
	local expected = fixture.reload_expected.leftovers
	local loose = laid_over_consist()
	if (expected.len() == 0)
	{
		return check(loose.len() == 0, "the junction holds " + loose.len() + " non-empty laid-over convoys after the reload, expected none: " + (loose.len() > 0 ? convoy_name(loose[0]) : ""))
	}
	if (!check(loose.len() == 1, "the junction holds " + loose.len() + " non-empty laid-over convoys after the reload, expected the one holding the released vehicles")) { return false }
	if (!check(consist_matches_fingerprints(loose[0], expected), "the laid-over convoy after the reload holds " + convoy_name(loose[0]) + ", which does not match the persisted released vehicles")) { return false }
	return check(loose[0].get_state() == convoy_state_layover, "the laid-over convoy is in " + depict_state(loose[0].get_state()) + " after the reload, expected the layover state")
}


function commit_receiver_schedule(pl, receiver, target)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target.id
	entries[0].target_unique_entry_couple = TARGET_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order(receiver_order()))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the receiver convoy schedule")) { return false }

	local back = receiver.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the receiver schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].unique_entry_id == RECEIVER_ENTRY_ID, "the receiver schedule has no entry " + RECEIVER_ENTRY_ID + " first")) { return false }
	if (!check(back.entries[1].unique_entry_id == RECEIVER_NEXT_ENTRY_ID, "the receiver schedule has no entry " + RECEIVER_NEXT_ENTRY_ID + " second")) { return false }
	if (!check(back.entries[0].couple == true, "receiver schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].uncouple == false, "receiver schedule read-back: the uncouple flag is set")) { return false }
	if (!check(back.entries[0].target_id_couple == target.id, "receiver schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + target.id)) { return false }
	if (!check(back.entries[0].target_unique_entry_couple == TARGET_ENTRY_ID, "receiver schedule read-back: target entry is not " + TARGET_ENTRY_ID)) { return false }
	if (!check(RECEIVER_ENTRY_ID in back.orders, "receiver schedule read-back: no order at entry " + RECEIVER_ENTRY_ID)) { return false }
	return true
}


function commit_target_schedule(pl, receiver, target)
{
	local entries = [make_entry(LOOP_POS, TARGET_ENTRY_ID), make_entry(NORTH_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].uncouple = true
	entries[0].uncouple_target_is_line_or_cnv = true
	entries[0].target_id_uncouple = receiver.id
	entries[0].target_unique_entry_uncouple = RECEIVER_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(TARGET_ENTRY_ID, make_order(target_order()))
	if (!check_tool(target.change_schedule(pl, schedule), "commit the target convoy schedule")) { return false }

	local back = target.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the target schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].unique_entry_id == TARGET_ENTRY_ID, "the target schedule has no entry " + TARGET_ENTRY_ID + " first")) { return false }
	if (!check(back.entries[1].unique_entry_id == TARGET_NEXT_ENTRY_ID, "the target schedule has no entry " + TARGET_NEXT_ENTRY_ID + " second")) { return false }
	if (!check(back.entries[0].uncouple == true, "target schedule read-back: the uncouple flag is not set")) { return false }
	if (!check(back.entries[0].couple == false, "target schedule read-back: the couple flag is set")) { return false }
	if (!check(back.entries[0].target_id_uncouple == receiver.id, "target schedule read-back: target id is " + back.entries[0].target_id_uncouple + ", expected " + receiver.id)) { return false }
	if (!check(back.entries[0].target_unique_entry_uncouple == RECEIVER_ENTRY_ID, "target schedule read-back: target entry is not " + RECEIVER_ENTRY_ID)) { return false }
	if (!check(TARGET_ENTRY_ID in back.orders, "target schedule read-back: no order at entry " + TARGET_ENTRY_ID)) { return false }
	return true
}


// Creates the separate convoy that releases the pool coach at the loop
// platform in the pool-fallback case. Its order uses the "any" recipient so
// that the receiver is allowed to take the released coach.
function create_pool_donor(pl)
{
	local cnv = create_depot_convoy(pl, [fixture.loco_c, fixture.pool_coach], "pool release convoy")
	if (cnv == null) { return false }
	fixture.donor_id = cnv.id

	local entries = [make_entry(LOOP_POS, DONOR_ENTRY_ID), make_entry(EAST_POS, DONOR_EAST_ENTRY_ID)]
	local schedule = schedule_x(wt_rail, entries)
	local order = make_order([[fixture.loco_c]])
	order.available_to_id = 0
	order.available_to_flags = 0
	schedule.orders.rawset(DONOR_ENTRY_ID, order)
	if (!check_tool(cnv.change_schedule(pl, schedule), "commit the pool release convoy schedule")) { return false }

	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the pool release convoy")) { return false }
	return true
}


function setup()
{
	local pl = player_x(0)

	if (!build_infrastructure(pl)) { return false }

	if (!check(select_descriptors(), "no set of three powered and two unpowered rail vehicles with the required couplings and topspeeds is available")) { return false }

	if (case_name == "pool-fallback")
	{
		// A coach that couples behind the coaches already in use; it is the
		// vehicle the receiver must collect from the laid-over pool.
		foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
		{
			if (desc.get_power() != 0 || desc.needs_electrification()) { continue }
			if (desc.get_successors().len() > 0 || !desc.can_be_last()) { continue }
			if (desc.is_equal(fixture.coach_a) || desc.is_equal(fixture.coach_b)) { continue }
			if (!coupons(fixture.coach_b, desc)) { continue }
			fixture.pool_coach = desc
			break
		}
		if (!check(fixture.pool_coach != null, "no third unpowered coach that couples behind the second coach is available")) { return false }
	}

	local receiver = create_depot_convoy(pl, receiver_consist(), "receiver convoy")
	if (receiver == null) { return false }
	fixture.receiver_id = receiver.id

	local target = create_depot_convoy(pl, target_consist(), "target convoy")
	if (target == null) { return false }
	fixture.target_id = target.id

	if (!commit_receiver_schedule(pl, receiver, target)) { return false }
	if (!commit_target_schedule(pl, receiver, target)) { return false }

	if (case_name == "symmetric-swap" || case_name == "one-way-to-receiver" || case_name == "pair-reload" || case_name == "already-at-end-state" || case_name == "conflict-backtrack" || case_name == "unsatisfiable" || case_name == "pool-fallback")
	{
		fixture.first_kind = "receiver"
	}
	else if (case_name == "symmetric-swap-target-first")
	{
		fixture.first_kind = "target"
	}
	else
	{
		return fail("unknown case " + case_name)
	}

	if (case_name == "pool-fallback")
	{
		if (!create_pool_donor(pl)) { return false }
		return true
	}

	fixture.phase = "start-first"
	return true
}


function receiver_convoy()
{
	return convoy_x(fixture.receiver_id)
}


function target_convoy()
{
	return convoy_x(fixture.target_id)
}


function first_convoy()
{
	return fixture.first_kind == "receiver" ? receiver_convoy() : target_convoy()
}


function second_convoy()
{
	return fixture.first_kind == "receiver" ? target_convoy() : receiver_convoy()
}


function first_pair_entry_id()
{
	return fixture.first_kind == "receiver" ? RECEIVER_ENTRY_ID : TARGET_ENTRY_ID
}


function second_pair_entry_id()
{
	return fixture.first_kind == "receiver" ? TARGET_ENTRY_ID : RECEIVER_ENTRY_ID
}


function pair_entry_ids()
{
	return [RECEIVER_ENTRY_ID, TARGET_ENTRY_ID]
}


function next_entry_ids()
{
	return [RECEIVER_NEXT_ENTRY_ID, TARGET_NEXT_ENTRY_ID]
}


function at_any_pair_entry(cnv)
{
	local entry = cnv.get_current_entry_id()
	return entry == RECEIVER_ENTRY_ID || entry == TARGET_ENTRY_ID
}


function laid_over_carrier(expected)
{
	local halt = halt_x.get_halt(MAIN_POS, player_x(0))
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


function laid_over_consist()
{
	local halt = halt_x.get_halt(MAIN_POS, player_x(0))
	local consists = []
	if (halt == null)
	{
		return consists
	}
	foreach (loose in halt.get_laid_over_list())
	{
		if (!loose.is_valid()) { continue }
		if (loose.get_vehicles().len() > 0)
		{
			consists.append(loose)
		}
	}
	return consists
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


function wait_first()
{
	local first = first_convoy()
	if (!first.is_valid())
	{
		fail("the " + fixture.first_kind + " convoy disappeared before reaching its pair entry")
		return
	}
	if (first.get_current_entry_id() == first_pair_entry_id() && first.get_state() == convoy_state_awaiting_recombination)
	{
		fixture.first_waited = true
		print("RUNTIME CASE " + case_name + ": " + fixture.first_kind + " convoy is waiting at entry " + first_pair_entry_id())
		local second = second_convoy()
		if (!check(second.is_valid(), "the second convoy is not valid before it is started")) { return }
		if (!check_tool(fixture.depot.start_convoy(player_x(0), second), "start the " + (fixture.first_kind == "receiver" ? "target" : "receiver") + " convoy")) { return }
		fixture.phase = "detect-exchange"
		return
	}
	if (first.get_current_entry_id() != first_pair_entry_id())
	{
		++fixture.travel_steps
		if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
		{
			fail("timed out waiting for the " + fixture.first_kind + " convoy to reach its pair entry: entry " + first.get_current_entry_id() + ", " + depict_state(first.get_state()))
		}
		return
	}
	// At the pair entry but not yet waiting: the exchange may already have run
	// with the second convoy if the schedule was edited, or the convoy may be
	// loading before the resolver runs. Keep observing.
	++fixture.exchange_steps
	if (fixture.exchange_steps > EXCHANGE_STEP_LIMIT)
	{
		fail("timed out waiting for the " + fixture.first_kind + " convoy at its pair entry to wait for the counterpart: " + depict_state(first.get_state()))
	}
}


// True when neither convoy is still waiting and both carries its expected final
// consist. This is the post-exchange state for both a successful exchange and
// an all-or-nothing refusal (whose final consults equal the pre-exchange ones).
function exchange_applied()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid()) { return false }
	if (receiver.get_state() == convoy_state_awaiting_recombination || target.get_state() == convoy_state_awaiting_recombination) { return false }
	if (!consist_matches(receiver, receiver_final()) || !consist_matches(target, target_final())) { return false }
	return true
}


function check_leftovers()
{
	local expected = expected_leftovers()
	local loose = laid_over_consist()
	if (expected.len() == 0)
	{
		if (!check(loose.len() == 0, "the junction holds " + loose.len() + " non-empty laid-over convoys, expected none: " + (loose.len() > 0 ? convoy_name(loose[0]) : ""))) { return false }
		return true
	}
	if (!check(loose.len() == 1, "the junction holds " + loose.len() + " non-empty laid-over convoys, expected the one holding the released vehicles")) { return false }
	if (!check(consist_matches(loose[0], expected), "the laid-over convoy holds " + convoy_name(loose[0]) + ", expected " + consist_name(expected))) { return false }
	if (!check(loose[0].get_state() == convoy_state_layover, "the laid-over convoy is in " + depict_state(loose[0].get_state()) + ", expected the layover state")) { return false }
	return true
}


function detect_exchange()
{
	if (exchange_applied())
	{
		local receiver = receiver_convoy()
		local target = target_convoy()
		// Descriptor names are not printed: vehicle_desc_x.get_name() is not
		// reliable for identification in scripted tests (see ai/known-bugs.md).
		print("RUNTIME CASE " + case_name + ": receiver convoy " + receiver.id + " holds " + receiver.get_vehicles().len() + " vehicles; target convoy " + target.id + " holds " + target.get_vehicles().len() + " vehicles")

		if (!check_leftovers()) { return }

		local allowed = [fixture.receiver_id, fixture.target_id]
		foreach (cnv in laid_over_consist())
		{
			allowed.append(cnv.id)
		}
		local total = world_vehicle_total()
		local expected_total = receiver_final().len() + target_final().len() + expected_leftovers().len()
		if (!check(total == expected_total, "the map holds " + total + " vehicles, expected " + expected_total)) { return }
		local unexpected = find_populated_convoy_other_than(allowed)
		if (!check(unexpected == null, "the map holds an unexpected populated convoy holding " + convoy_name(unexpected))) { return }

		// The shunting delay is a real dwell, so the changed convoy must be
		// waiting in SHUNTING or LOADING rather than departing at once.
		if (!check(receiver.get_state() != convoy_state_awaiting_recombination && target.get_state() != convoy_state_awaiting_recombination, "a convoy is still awaiting recombination after the exchange")) { return }
		if (case_name == "already-at-end-state")
		{
			if (!check(receiver.get_state() != convoy_state_shunting && target.get_state() != convoy_state_shunting, "an already-satisfied pair entered the shunting state; an unchanged consist must not take the shunting delay")) { return }
		}

		// The reload case stops here, after the commit has been asserted and
		// before the shunting delay expires, so that the runner can join a
		// network client and the saved state can be loaded again.
		if (case_name == "pair-reload")
		{
			remember_pair_reload_state()
			fixture.phase = "awaiting-reload"
			print("RUNTIME CASE " + case_name + " READY")
			return
		}

		fixture.phase = "detect-advance"
		fixture.exchange_steps = 0
		return
	}

	++fixture.exchange_steps
	if (fixture.exchange_steps > EXCHANGE_STEP_LIMIT)
	{
		local receiver = receiver_convoy()
		local target = target_convoy()
		fail("timed out waiting for the exchange: receiver entry " + receiver.get_current_entry_id() + " (" + depict_state(receiver.get_state()) + ") " + convoy_name(receiver) + ", target entry " + target.get_current_entry_id() + " (" + depict_state(target.get_state()) + ") " + convoy_name(target))
	}
}


function detect_advance()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared before both schedules advanced past the pair entries")
		return
	}

	// Each convoy is observed independently: the two schedules are cyclic and
	// the receiver may return to its pair entry before the target reaches its
	// next stop, so a simultaneous observation is not required.
	local receiver_entry = receiver.get_current_entry_id()
	local target_entry = target.get_current_entry_id()
	if (!fixture.receiver_advanced && receiver_entry == RECEIVER_NEXT_ENTRY_ID)
	{
		local receiver_ok = fixture.after_reload ? consist_matches_fingerprints(receiver, fixture.reload_expected.receiver) : consist_matches(receiver, receiver_final())
		if (!check(receiver_ok, "the receiver consist changed at its next entry: " + convoy_name(receiver))) { return }
		fixture.receiver_advanced = true
		print("RUNTIME CASE " + case_name + ": receiver advanced to entry " + RECEIVER_NEXT_ENTRY_ID)
	}
	if (!fixture.target_advanced && target_entry == TARGET_NEXT_ENTRY_ID)
	{
		local target_ok = fixture.after_reload ? consist_matches_fingerprints(target, fixture.reload_expected.target) : consist_matches(target, target_final())
		if (!check(target_ok, "the target consist changed at its next entry: " + convoy_name(target))) { return }
		fixture.target_advanced = true
		print("RUNTIME CASE " + case_name + ": target advanced to entry " + TARGET_NEXT_ENTRY_ID)
	}
	if (fixture.receiver_advanced && fixture.target_advanced)
	{
		pass()
		return
	}
	++fixture.exchange_steps
	local departure_limit = fixture.after_reload ? RELOAD_DEPARTURE_STEP_LIMIT : DEPARTURE_STEP_LIMIT
	if (fixture.exchange_steps > departure_limit)
	{
		fail("the convoys did not both advance past the pair entries within " + departure_limit + " observations (receiver entry " + receiver_entry + " " + depict_state(receiver.get_state()) + ", target entry " + target_entry + " " + depict_state(target.get_state()) + ")")
	}
}


function observe_pool_preparation()
{
	if (fixture.phase == "waiting-for-pool")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the pool release convoy did not leave the pool coach in the junction laid-over list within " + PREPARATION_STEP_LIMIT + " observations")
			return
		}
		local carrier = laid_over_carrier(fixture.pool_coach)
		if (carrier == null)
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": pool coach laid over at the junction in convoy " + carrier.id)
		fixture.phase = "removing-donor"
		return
	}

	if (fixture.phase == "removing-donor")
	{
		local donor = convoy_x(fixture.donor_id)
		if (!check(donor.is_valid(), "the pool release convoy is no longer valid before it could be removed")) { return }
		fixture.phase = "waiting-for-donor-removal"
		if (!check_tool(donor.destroy(player_x(0)), "remove the pool release convoy after it released the pool coach")) { return }
		return
	}

	if (fixture.phase == "waiting-for-donor-removal")
	{
		++fixture.preparation_steps
		if (fixture.preparation_steps > PREPARATION_STEP_LIMIT)
		{
			fail("the pool release convoy did not disappear within " + PREPARATION_STEP_LIMIT + " observations")
			return
		}
		if (convoy_x(fixture.donor_id).is_valid())
		{
			return
		}
		fixture.donor_removed = true
		print("RUNTIME CASE " + case_name + ": pool coach prepared; starting the first pair convoy")
		fixture.phase = "start-first"
		return
	}
}


function observe()
{
	if (fixture.phase == "waiting-for-pool" || fixture.phase == "removing-donor" || fixture.phase == "waiting-for-donor-removal")
	{
		observe_pool_preparation()
		return
	}

	if (fixture.phase == "awaiting-reload")
	{
		local receiver = receiver_convoy()
		local target = target_convoy()
		if (!check(receiver.is_valid() && target.is_valid(), "a convoy disappeared before the saved exchange state was reloaded")) { return }
		if (!check(exchange_applied_reload(), "the exchanged state changed before the saved state was reloaded")) { return }
		if (!check(receiver.get_current_entry_id() == RECEIVER_ENTRY_ID && target.get_current_entry_id() == TARGET_ENTRY_ID, "a convoy advanced past its pair entry before the saved state was reloaded; shunting_time_seconds must outlast the client join")) { return }
		return
	}

	if (fixture.phase == "restored-pending")
	{
		if (!check(exchange_applied_reload(), "the restored convoys do not carry the persisted final consists")) { return }
		if (!check_leftovers_reload()) { return }
		local total = world_vehicle_total()
		local expected_total = fixture.reload_expected.receiver.len() + fixture.reload_expected.target.len() + fixture.reload_expected.leftovers.len()
		if (!check(total == expected_total, "the restored map holds " + total + " vehicles, expected " + expected_total)) { return }
		print("RUNTIME CASE " + case_name + " RESTORED")
		fixture.phase = "detect-advance"
		fixture.exchange_steps = 0
		return
	}

	if (fixture.phase == "start-first")
	{
		local first = first_convoy()
		if (!check(first.is_valid(), "the first convoy is not valid before it is started")) { return }
		if (!check_tool(fixture.depot.start_convoy(player_x(0), first), "start the " + fixture.first_kind + " convoy")) { return }
		fixture.travel_steps = 0
		fixture.phase = "wait-first"
		return
	}

	if (fixture.phase == "wait-first")
	{
		wait_first()
		return
	}

	if (fixture.phase == "detect-exchange")
	{
		detect_exchange()
		return
	}

	if (fixture.phase == "detect-advance")
	{
		detect_advance()
		return
	}

	fail("observation reached phase " + fixture.phase)
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
	return ttext("Run the selected paired exchange case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Paired exchange runtime case {name}: {phase}")
	msg.name = case_name
	msg.phase = fixture.phase
	return msg
}


function get_result_text(pl)
{
	return get_info_text(pl)
}


// Called by scenario_t::rdwr after the script has been loaded again and the
// saved persistent table has been evaluated. The pair reload case reconstructs
// the fixture identifiers from the persisted state; setup() must not run again,
// because the infrastructure, schedules and vehicles are all in the savegame.
// Observation then re-asserts the committed exchange from the loaded world.
function resume_game()
{
	if (case_name != "pair-reload")
	{
		return
	}
	if (!("reload" in persistent) || typeof(persistent.reload) != "table")
	{
		fail("the savegame holds no persisted pair reload state; it must be saved after the paired exchange has committed")
		return
	}

	local state = persistent.reload
	// Drop the state so that a later save cannot restore a stale pre-departure
	// state.
	delete persistent.reload

	fixture.setup_started = true
	fixture.after_reload = true
	fixture.receiver_id = state.receiver_id
	fixture.target_id = state.target_id
	fixture.reload_expected = {
		receiver = state.receiver_final,
		target = state.target_final,
		leftovers = state.leftovers
	}

	if (!check(state.kind == case_name, "the persisted state belongs to case " + state.kind + ", not " + case_name)) { return }
	if (!check(receiver_convoy().is_valid(), "the receiver convoy " + fixture.receiver_id + " named by the persisted state is not in the loaded world")) { return }
	if (!check(target_convoy().is_valid(), "the target convoy " + fixture.target_id + " named by the persisted state is not in the loaded world")) { return }

	fixture.phase = "restored-pending"
	print("RUNTIME CASE " + case_name + ": saved paired exchange state restored from the savegame")
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
		if (case_name == "pool-fallback")
		{
			fixture.phase = "waiting-for-pool"
		}
		print("RUNTIME CASE " + case_name + ": setup complete; receiver convoy " + fixture.receiver_id + ", target convoy " + fixture.target_id)
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
