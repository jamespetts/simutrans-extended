//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Shared world builder and assertions for the choose-signal call-on cases under
// tests/pending-runtime/choose-cases.
//
// A case selects itself by setting ::case_kind before including this file. The
// runner scripts stage the case's scenario.nut, this file and the map named by
// the case in the pakset scenario directory and runs one game process per case.
//
// The track layout is the two-platform junction from pair-harness.nut with a
// choose signal and signalbox added in the throat:
//
//   depot (1,2) -- trunk -- choose signal (3,2) -- junction (4,2)
//                                                   |         |
//                                            main platform  loop platform
//                                                (5,2)         (5,3)
//
// Both platforms belong to one halt. A convoy leaving the depot eastwards
// passes the choose signal before the junction. A target convoy parked on the
// loop platform is therefore reachable from the choose signal, and a receiver
// whose schedule entry names it must be routed to the loop platform and
// admitted under call-on.
//
// Every observation uses only the public script interface.
//

local case_name = ::case_kind

map.file = case_name == "moving-block" ? "empty-16x16-1992.sve" : "empty-16x16.sve"

scenario.short_description = "Choose-signal call-on runtime case: " + case_name
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local MAIN_POS = coord3d(5, 2, 0)
local LOOP_POS = coord3d(5, 3, 0)
local UNREACHABLE_POS = coord3d(5, 4, 0)
local UNREACHABLE_SIGN_POS = coord3d(4, 4, 0)
local EAST_POS = coord3d(9, 2, 0)
local NORTH_POS = coord3d(6, 0, 0)
local DEPOT_POS = coord3d(1, 2, 0)
local SIGNAL_POS = coord3d(3, 2, 0)
local SECOND_SIGNAL_POS = coord3d(2, 2, 0)
local BOX_POS = coord3d(3, 3, 0)

local RECEIVER_ENTRY_ID = 11
local RECEIVER_NEXT_ENTRY_ID = 12
local TARGET_ENTRY_ID = 21
local TARGET_NEXT_ENTRY_ID = 22
local OCCUPIER_ENTRY_ID = 31
local OCCUPIER_NEXT_ENTRY_ID = 32

// Observation step limits. These are counts of scenario observations, not
// game-time quantities.
local TRAVEL_STEP_LIMIT = 6000
local APPROACH_STEP_LIMIT = 3000
local HOLD_OBSERVATIONS = 40

local fixture = {
	phase = "setup",
	setup_started = false,
	completed = false,
	depot = null,
	receiver_id = 0,
	target_id = 0,
	occupier_id = 0,
	hold_id = 0,
	signal_box = null,
	signal_box_desc = null,
	choose_desc = null,
	signal_state = null,
	call_on_seen = false,
	drive_by_sight_seen = false,
	travel_steps = 0,
	approach_steps = 0,
	last_pos = null,
	stationary_steps = 0,
	loco_a = null,
	loco_b = null,
	loco_c = null,
	coach_a = null,
	coach_b = null
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


function same_pos(a, b)
{
	return a != null && b != null && a.x == b.x && a.y == b.y && a.z == b.z
}


function choose_position(cnv)
{
	if (cnv == null || !cnv.is_valid())
	{
		return "<invalid>"
	}
	local p = cnv.get_pos()
	return "(" + p.x + "," + p.y + ")"
}


function depict_aspect(aspect)
{
	switch (aspect)
	{
		case danger: return "danger"
		case clear: return "clear"
		case caution: return "caution"
		case preliminary_caution: return "preliminary caution"
		case advance_caution: return "advance caution"
		case clear_no_choose: return "clear (no choose)"
		case caution_no_choose: return "caution (no choose)"
		case preliminary_caution_no_choose: return "preliminary caution (no choose)"
		case advance_caution_no_choose: return "advance caution (no choose)"
		case call_on: return "call on"
	}
	return "aspect " + aspect
}


function depict_working_method(method)
{
	switch (method)
	{
		case drive_by_sight: return "drive by sight"
		case time_interval: return "time interval"
		case absolute_block: return "absolute block"
		case token_block: return "token block"
		case track_circuit_block: return "track circuit block"
		case cab_signalling: return "cab signalling"
		case moving_block: return "moving block"
		case one_train_staff: return "one train staff"
		case time_interval_with_telegraph: return "time interval with telegraph"
	}
	return "working method " + method
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


// Select a signalbox and a choose signal that the box can control. The moving
// block case requires a choose signal whose descriptor uses moving_block.
function select_choose_signal_and_box()
{
	local boxes = building_desc_x.get_building_list(building_desc_x.signalbox).filter(
		@(idx, b) b.is_signalbox() && b.is_available(world.get_time())
			&& b.get_capacity() >= (case_name == "multiple-choose-signals" ? 2 : 1))
	if (!check(boxes.len() > 0, "no available signalbox with capacity")) { return false }

	local signals = sign_desc_x.get_available_signs(wt_rail).filter(
		@(idx, s) s.is_choose_sign() && s.get_allow_underground() != 1
			&& (case_name == "moving-block" ? s.get_working_method() == moving_block : s.get_working_method() != moving_block))
	if (!check(signals.len() > 0, case_name == "moving-block" ? "no available moving-block choose signal" : "no available choose signal")) { return false }

	foreach (s in signals)
	{
		foreach (b in boxes)
		{
			if (s.get_signal_group() > 0 && (b.get_clusters() & s.get_signal_group()) != 0)
			{
				fixture.choose_desc = s
				fixture.signal_box_desc = b
				return true
			}
		}
	}
	return check(false, "no available choose signal with a matching signalbox")
}


function build_signalbox(pl)
{
	local positions = [BOX_POS, coord3d(3, 4, 0), coord3d(2, 3, 0), coord3d(1, 3, 0), coord3d(0, 3, 0), coord3d(0, 0, 0), coord3d(1, 0, 0), coord3d(2, 0, 0), coord3d(3, 0, 0)]
	foreach (pos in positions)
	{
		for (local rotation = 0; rotation < 4; rotation++)
		{
			if (command_x.can_build_signalbox_at(pl, pos, fixture.signal_box_desc, rotation) != null)
			{
				continue
			}
			if (!check_tool(command_x.build_signalbox(pl, pos, fixture.signal_box_desc, rotation), "build signalbox")) { return null }
			local box = tile_x(pos.x, pos.y, pos.z).find_object(mo_signalbox)
			if (!check(box != null, "signalbox not found after building")) { return null }
			return box
		}
	}
	fail("no valid signalbox site was found near the junction")
	return null
}


function build_infrastructure(pl)
{
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	if (!check(rail != null, "no rail way available")) { return false }

	if (!check_tool(command_x.build_way(pl, DEPOT_POS, coord3d(6, 2, 0), rail, false), "build trunk")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build east branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(6, 0, 0), rail, false), "build north branch")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 2, 0), coord3d(4, 3, 0), rail, false), "build loop west link")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(7, 3, 0), rail, false), "build loop")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(7, 3, 0), coord3d(7, 2, 0), rail, false), "build loop east link")) { return false }
	if (case_name == "target-unreachable")
	{
		if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(4, 4, 0), rail, false), "build third platform west link")) { return false }
		if (!check_tool(command_x.build_way(pl, coord3d(4, 4, 0), coord3d(7, 4, 0), rail, false), "build third platform track")) { return false }
		if (!check_tool(command_x.build_way(pl, coord3d(7, 4, 0), coord3d(7, 3, 0), rail, false), "build third platform east link")) { return false }
	}


	if (!check_tool(command_x.build_depot(pl, DEPOT_POS, depot_desc_for(wt_rail)), "build rail depot")) { return false }
	local depot = depot_x(DEPOT_POS.x, DEPOT_POS.y, DEPOT_POS.z)
	if (!check(depot != null, "depot not found after building")) { return false }
	fixture.depot = depot

	local station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
	if (!check(station != null, "no rail station available")) { return false }
	if (!check_tool(station_builder(pl, MAIN_POS, station), "build main junction platform")) { return false }
	if (!check_tool(station_builder(pl, LOOP_POS, station), "build loop junction platform")) { return false }
	if (case_name == "target-absent-occupied")
	{
		// The receiver needs two platform tiles; keep the loop a valid alternative.
		if (!check_tool(station_builder(pl, coord3d(6, 3, 0), station), "extend the free loop platform")) { return false }
	}
	if (case_name == "target-unreachable")
	{
		if (!check_tool(station_builder(pl, UNREACHABLE_POS, station), "build third junction platform")) { return false }
	}
	if (!check_tool(station_builder(pl, EAST_POS, station), "build east stop")) { return false }
	if (!check_tool(station_builder(pl, NORTH_POS, station), "build north stop")) { return false }

	local main_halt = halt_x.get_halt(MAIN_POS, pl)
	local loop_halt = halt_x.get_halt(LOOP_POS, pl)
	if (!check(main_halt != null && loop_halt != null, "a junction platform has no halt")) { return false }
	if (!check(main_halt.get_name() == loop_halt.get_name(), "the junction main and loop platforms have different stop names, so they are separate halts")) { return false }
	if (case_name == "target-unreachable")
	{
		local third_halt = halt_x.get_halt(UNREACHABLE_POS, pl)
		if (!check(third_halt != null && third_halt.get_name() == main_halt.get_name(), "the third platform belongs to a separate halt")) { return false }
	}

	if (!select_choose_signal_and_box()) { return false }
	local box = build_signalbox(pl)
	if (box == null) { return false }
	fixture.signal_box = box
	if (!check(box.can_add_signal(fixture.choose_desc), "the signalbox cannot control the selected choose signal")) { return false }
	try {
		if (!check_tool(command_x.build_sign_at(pl, SIGNAL_POS, fixture.choose_desc, box), "build choose signal")) { return false }
	}
	catch (e) {
		return fail("build choose signal raised: " + e + "; group=" + fixture.choose_desc.get_signal_group() + " wm=" + fixture.choose_desc.get_working_method() + " boxclusters=" + fixture.signal_box_desc.get_clusters() + " boxcap=" + fixture.signal_box_desc.get_capacity())
	}
	local signal = tile_x(SIGNAL_POS.x, SIGNAL_POS.y, SIGNAL_POS.z).find_object(mo_signal)
	if (!check(signal != null, "choose signal not found after building")) { return false }

	if (case_name == "multiple-choose-signals")
	{
		if (!check(box.can_add_more_signals(), "the signalbox cannot control a second choose signal")) { return false }
		if (!check_tool(command_x.build_sign_at(pl, SECOND_SIGNAL_POS, fixture.choose_desc, box), "build second choose signal")) { return false }
		if (!check(tile_x(SECOND_SIGNAL_POS.x, SECOND_SIGNAL_POS.y, SECOND_SIGNAL_POS.z).find_object(mo_signal) != null, "second choose signal not found after building")) { return false }
	}

	return true
}


function coupons(a, b)
{
	return vehicle_desc_x.is_coupling_allowed(a, b)
}


// Collect rail vehicles that can run on non-electrified track and have no fixed
// successor. Three powered descriptors and two unpowered coaches are chosen so
// that every coupling the cases use is allowed.
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

	foreach (a in powered)
	{
		foreach (b in powered)
		{
			if (a.is_equal(b)) { continue }
			foreach (c in powered)
			{
				if (c.is_equal(a) || c.is_equal(b)) { continue }
				if (!coupons(b, c)) { continue }
				foreach (ca in unpowered)
				{
					foreach (cb in unpowered)
					{
						if (ca.is_equal(cb)) { continue }
						if (!coupons(a, ca) || !coupons(b, cb)) { continue }
						if (!coupons(c, ca)) { continue }
						fixture.loco_a = a
						fixture.loco_b = b
						fixture.loco_c = c
						fixture.coach_a = ca
						fixture.coach_b = cb
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
// vehicle alternative.
function make_order(slots)
{
	local order = consist_order_x()
	local result = []
	foreach (slot_spec in slots)
	{
		local slot = consist_order_element_x()
		local alternatives = []
		foreach (spec in slot_spec)
		{
			local alternative = vehicle_description_x()
			alternative.specific_vehicle = spec
			alternatives.append(alternative)
		}
		slot.alternatives = alternatives
		result.append(slot)
	}
	order.slots = result
	return order
}


function create_depot_convoy_at(pl, depot, descriptors, what)
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


function create_depot_convoy(pl, descriptors, what)
{
	return create_depot_convoy_at(pl, fixture.depot, descriptors, what)
}


// A convoy with a couple entry whose target is not at the stop waits in
// AWAITING_RECOMBINATION without a timeout. The hold convoy stays in the depot
// and supplies that absent target.
function commit_target_schedule(pl, target, hold_id)
{
	local target_platform = case_name == "target-unreachable" ? UNREACHABLE_POS : LOOP_POS
	local entries = [make_entry(target_platform, TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].ignore_choose = true
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = hold_id
	if (case_name == "choose-reload")
	{
		// Keep the target loading while the reload fixture holds the receiver.
		entries[0].wait_for_time = 1
		entries[0].waiting_time_shift = 120
	}

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(TARGET_ENTRY_ID, make_order([[fixture.loco_a]]))
	if (!check_tool(target.change_schedule(pl, schedule), "commit the target convoy schedule")) { return false }

	local back = target.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the target schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].couple == true, "target schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_couple == hold_id, "target schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + hold_id)) { return false }
	if (!check(back.entries[0].ignore_choose == true, "target schedule read-back: ignore_choose is not set")) { return false }
	return true
}


function commit_hold_schedule(pl, hold)
{
	local entries = [make_entry(NORTH_POS, 41), make_entry(EAST_POS, 42)]
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(hold.change_schedule(pl, schedule), "commit the hold convoy schedule")) { return false }
	return true
}


function commit_blocker_schedule(pl, blocker)
{
	local entries = [make_entry(LOOP_POS, OCCUPIER_ENTRY_ID), make_entry(EAST_POS, OCCUPIER_NEXT_ENTRY_ID)]
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(blocker.change_schedule(pl, schedule), "commit the blocker convoy schedule")) { return false }
	return true
}


function commit_occupier_schedule(pl, occupier, pos, entry_id, next_entry_id, hold_id)
{
	local entries = [make_entry(pos, entry_id), make_entry(EAST_POS, next_entry_id)]
	entries[0].ignore_choose = true
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = hold_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(entry_id, make_order([[fixture.loco_c]]))
	if (!check_tool(occupier.change_schedule(pl, schedule), "commit the occupier convoy schedule")) { return false }
	return true
}


function commit_receiver_schedule(pl, receiver, target_id, ignore_choose)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id
	if (ignore_choose)
	{
		entries[0].ignore_choose = true
	}

	local schedule = schedule_x(wt_rail, entries)
	// The order names a vehicle that is in neither consist. The receive must
	// therefore leave both consists unchanged, so the signalling assertions are
	// not obscured by the recombination commit.
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_c]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the receiver convoy schedule")) { return false }

	local back = receiver.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the receiver schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].couple == true, "receiver schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_couple == target_id, "receiver schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + target_id)) { return false }
	if (!check(back.entries[0].ignore_choose == (ignore_choose ? true : false), "receiver schedule read-back: ignore_choose is not " + (ignore_choose ? "set" : "clear"))) { return false }
	return true
}


function commit_late_target_schedule(pl, target, receiver)
{
	local entries = [make_entry(LOOP_POS, TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = receiver.id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(TARGET_ENTRY_ID, make_order([[fixture.loco_a]]))
	if (!check_tool(target.change_schedule(pl, schedule), "commit the late target convoy schedule")) { return false }

	local back = target.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the late target schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].couple == true, "late target schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_couple == receiver.id, "late target schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + receiver.id)) { return false }
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


function occupier_convoy()
{
	return convoy_x(fixture.occupier_id)
}


function choose_signal()
{
	return sign_x(SIGNAL_POS.x, SIGNAL_POS.y, SIGNAL_POS.z)
}


function observe_signal()
{
	local signal = choose_signal()
	if (signal == null)
	{
		return
	}
	local aspect = signal.get_state()
	fixture.signal_state = aspect
	if (aspect == call_on)
	{
		fixture.call_on_seen = true
	}
}


function convoy_is_loading(cnv)
{
	return cnv.is_valid() && cnv.get_state() == convoy_state_loading
}


function wait_for_entry(cnv, entry_id, what)
{
	++fixture.travel_steps
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		fail("timed out waiting for the " + what + " to reach entry " + entry_id + ": " + choose_position(cnv) + ", entry " + cnv.get_current_entry_id() + ", " + depict_state(cnv.get_state()))
		return false
	}
	if (cnv.get_current_entry_id() == entry_id && (convoy_is_loading(cnv) || cnv.get_state() == convoy_state_layover || cnv.get_state() == convoy_state_awaiting_recombination))
	{
		return true
	}
	return false
}


function start_convoy(pl, cnv, what)
{
	if (!check_tool(fixture.depot.start_convoy(pl, cnv), "start the " + what)) { return false }
	fixture.travel_steps = 0
	return true
}


function setup()
{
	local pl = player_x(0)

	if (!build_infrastructure(pl)) { return false }
	if (!check(select_descriptors(), "no set of three powered and two unpowered rail vehicles with the required couplings is available")) { return false }

	local receiver = create_depot_convoy(pl, [fixture.loco_b, fixture.coach_b], "receiver convoy")
	if (receiver == null) { return false }
	fixture.receiver_id = receiver.id

	local target = create_depot_convoy(pl, [fixture.loco_a, fixture.coach_a], "target convoy")
	if (target == null) { return false }
	fixture.target_id = target.id

	local hold = create_depot_convoy(pl, [fixture.loco_c], "hold convoy")
	if (hold == null) { return false }
	fixture.hold_id = hold.id
	if (case_name == "target-platform-blocked" || case_name == "choose-reload")
	{
		if (!commit_blocker_schedule(pl, hold)) { return false }
	}
	else if (!commit_hold_schedule(pl, hold))
	{
		return false
	}

	if (case_name == "target-present" || case_name == "target-present-ignore-choose" || case_name == "moving-block" || case_name == "multiple-choose-signals" || case_name == "target-unreachable")
	{
		if (!commit_target_schedule(pl, target, hold.id)) { return false }
		if (!commit_receiver_schedule(pl, receiver, target.id, case_name == "target-present-ignore-choose")) { return false }
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-target"
		return true
	}

	if (case_name == "target-absent")
	{
		// The named target exists but stays in the depot, so no target is
		// present at the stop. Both platforms are free; the receiver keeps its
		// scheduled platform and no call-on is issued.
		if (!commit_receiver_schedule(pl, receiver, target.id, false)) { return false }
		if (!start_convoy(pl, receiver, "receiver convoy")) { return false }
		fixture.phase = "watch-free-platform"
		return true
	}

	if (case_name == "target-absent-ignore-choose" || case_name == "target-absent-occupied")
	{
		local occupier = create_depot_convoy(pl, [fixture.loco_a], "occupier convoy")
		if (occupier == null) { return false }
		fixture.occupier_id = occupier.id
		if (!commit_occupier_schedule(pl, occupier, MAIN_POS, OCCUPIER_ENTRY_ID, OCCUPIER_NEXT_ENTRY_ID, hold.id)) { return false }
		if (!commit_receiver_schedule(pl, receiver, target.id, case_name == "target-absent-ignore-choose")) { return false }
		if (!start_convoy(pl, occupier, "occupier convoy")) { return false }
		fixture.phase = "wait-occupier-main"
		return true
	}

	if (case_name == "target-platform-blocked" || case_name == "choose-reload")
	{
		fixture.occupier_id = hold.id
		if (!commit_target_schedule(pl, target, hold.id)) { return false }
		if (!commit_receiver_schedule(pl, receiver, target.id, false)) { return false }
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-target-blocked"
		return true
	}

	if (case_name == "target-arrives-after")
	{
		if (!commit_receiver_schedule(pl, receiver, hold.id, false)) { return false }
		if (!commit_late_target_schedule(pl, target, receiver)) { return false }
		// The target is not at the stop yet. The receiver selects a free
		// platform and passes the choose signal. The target is started later and
		// becomes the mover.
		if (!start_convoy(pl, receiver, "receiver convoy")) { return false }
		fixture.phase = "wait-receiver-pass"
		return true
	}

	return fail("unknown case " + case_name)
}


function observe_wait_target()
{
	local target = target_convoy()
	if (!target.is_valid())
	{
		fail("the target convoy disappeared before reaching the loop platform")
		return
	}
	observe_signal()
	if (wait_for_entry(target, TARGET_ENTRY_ID, "target convoy"))
	{
		print("RUNTIME CASE " + case_name + ": target is loading at its scheduled platform")
		if (case_name == "target-unreachable")
		{
			local one_way = null
			foreach (desc in sign_desc_x.get_available_signs(wt_rail))
			{
				if (desc.is_one_way() && desc.get_signal_group() == 0)
				{
					one_way = desc
					break
				}
			}
			if (!check(one_way != null, "no available one-way rail sign")) { return }
			if (!check_tool(command_x.build_sign_at(player_x(0), UNREACHABLE_SIGN_POS, one_way), "build the one-way rail sign")) { return }
			// The corner connects north and east. The first construction selects
			// north; repeating it selects east and prohibits the receiver's step
			// from this tile towards the target platform at (5,4).
			if (!check_tool(command_x.build_sign_at(player_x(0), UNREACHABLE_SIGN_POS, one_way), "turn the one-way rail sign east")) { return }
		}
		if (!start_convoy(player_x(0), receiver_convoy(), "receiver convoy")) { return }
		fixture.phase = case_name == "target-unreachable" ? "watch-unreachable" : "watch-approach"
	}
}


function observe_wait_target_blocked()
{
	local target = target_convoy()
	if (!target.is_valid())
	{
		fail("the target convoy disappeared before it could be blocked")
		return
	}
	observe_signal()
	if (wait_for_entry(target, TARGET_ENTRY_ID, "target convoy"))
	{
		print("RUNTIME CASE " + case_name + ": target is waiting at the loop platform; starting the blocker")
		if (!start_convoy(player_x(0), occupier_convoy(), "blocker convoy")) { return }
		fixture.phase = "wait-blocker"
	}
}


function observe_wait_blocker()
{
	local blocker = occupier_convoy()
	if (!blocker.is_valid())
	{
		fail("the blocker convoy disappeared before it reached the loop approach")
		return
	}
	observe_signal()
	local p = blocker.get_pos()
	if (p.x > SIGNAL_POS.x && p.y >= 2 && blocker.get_state() == convoy_state_driving)
	{
		print("RUNTIME CASE " + case_name + ": blocker is standing at " + choose_position(blocker) + " on the target platform approach")
		fixture.last_pos = null
		fixture.stationary_steps = 0
		if (!start_convoy(player_x(0), receiver_convoy(), "receiver convoy")) { return }
		fixture.phase = "watch-blocked"
		return
	}
	++fixture.travel_steps
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		fail("the blocker did not reach the target platform approach: " + choose_position(blocker) + " " + depict_state(blocker.get_state()))
	}
}


function observe_watch_blocked()
{
	local receiver = receiver_convoy()
	if (!receiver.is_valid())
	{
		fail("the receiver convoy disappeared while the target platform was blocked")
		return
	}
	observe_signal()
	if (fixture.signal_state == call_on)
	{
		fail("the choose signal issued call-on while an unrelated convoy blocked the target platform approach")
		return
	}
	if (receiver_held_at_signal())
	{
		if (case_name == "choose-reload")
		{
			print("RUNTIME CASE " + case_name + ": receiver is held at the choose signal with the approach link bound")
			persistent.reload <- {
				receiver_id = fixture.receiver_id,
				target_id = fixture.target_id,
				blocker_id = fixture.occupier_id
			}
			fixture.phase = "awaiting-reload"
			print("RUNTIME CASE " + case_name + " READY")
			return
		}
		print("RUNTIME CASE " + case_name + ": receiver is held at the choose signal at " + depict_aspect(fixture.signal_state) + " while the target approach is blocked")
		pass()
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not settle at the choose signal while the target approach was blocked: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


function observe_wait_occupier_main()
{
	local occupier = occupier_convoy()
	if (!occupier.is_valid())
	{
		fail("the occupier convoy disappeared before reaching the main platform")
		return
	}
	observe_signal()
	if (wait_for_entry(occupier, OCCUPIER_ENTRY_ID, "occupier convoy"))
	{
		print("RUNTIME CASE " + case_name + ": occupier is loading at the main platform")
		if (!start_convoy(player_x(0), receiver_convoy(), "receiver convoy")) { return }
		fixture.phase = case_name == "target-absent-ignore-choose" ? "watch-ignore-choose-absent" : "watch-free-platform"
	}
}


function receiver_held_at_signal()
{
	local receiver = receiver_convoy()
	if (!receiver.is_valid())
	{
		return false
	}
	local p = receiver.get_pos()
	if (receiver.get_state() != convoy_state_waiting_for_clearance)
	{
		return false
	}
	if (p.y != SIGNAL_POS.y || p.x > SIGNAL_POS.x + 1)
	{
		return false
	}

	if (!same_pos(fixture.last_pos, p))
	{
		fixture.last_pos = p
		fixture.stationary_steps = 0
		return false
	}
	++fixture.stationary_steps
	return fixture.stationary_steps >= HOLD_OBSERVATIONS
}


// The target has passed the choose signal but is held before the loop platform
function observe_watch_approach()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared during the approach")
		return
	}
	observe_signal()
	if (!fixture.drive_by_sight_seen && receiver.get_working_method() == drive_by_sight)
	{
		fixture.drive_by_sight_seen = true
		print("RUNTIME CASE " + case_name + ": receiver is running in " + depict_working_method(receiver.get_working_method()) + " at " + choose_position(receiver))
	}
	// The receiver must stop in the tile before the target's rear vehicle.
	local target_slots = target.get_vehicle_snapshot()
	local rear = target_slots[target_slots.len() - 1].pos
	local expected = coord3d(rear.x - 1, rear.y, rear.z)
	if (same_pos(receiver.get_pos(), expected))
	{
		if (!same_pos(fixture.last_pos, expected))
		{
			fixture.last_pos = expected
			fixture.stationary_steps = 0
		}
		else
		{
			++fixture.stationary_steps
		}
		if (fixture.stationary_steps >= HOLD_OBSERVATIONS)
		{
			if (!check(case_name == "moving-block" || fixture.call_on_seen, "the choose signal never displayed call-on; receiver is in " + depict_working_method(receiver.get_working_method()))) { return }
			if (!check(receiver.get_working_method() == drive_by_sight, "the receiver is in " + depict_working_method(receiver.get_working_method()) + " after call-on, expected drive by sight")) { return }
			print("RUNTIME CASE " + case_name + ": receiver stopped at " + choose_position(receiver) + " behind the target rear at " + choose_position(target))
			pass()
			return
		}
	}
	else
	{
		fixture.last_pos = null
		fixture.stationary_steps = 0
	}

	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("timed out waiting for the receiver to stop behind the target: receiver " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", target rear (" + rear.x + "," + rear.y + "), signal " + depict_aspect(fixture.signal_state))
	}
}


function observe_watch_unreachable()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared while the target platform was unreachable")
		return
	}
	observe_signal()
	if (receiver.get_pos().y == UNREACHABLE_POS.y || fixture.signal_state == call_on)
	{
		fail("the receiver entered the unreachable target platform or was called on: " + choose_position(receiver) + ", aspect " + depict_aspect(fixture.signal_state))
		return
	}
	if (receiver_held_at_signal())
	{
		print("RUNTIME CASE " + case_name + ": receiver is held at the choose signal with the target platform unreachable")
		pass()
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not settle at the choose signal with an unreachable target: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


function observe_watch_free_platform()
{
	local receiver = receiver_convoy()
	if (!receiver.is_valid())
	{
		fail("the receiver convoy disappeared during the free-platform approach")
		return
	}
	observe_signal()
	if (fixture.signal_state == call_on)
	{
		fail("the choose signal issued call-on with no target present")
		return
	}


	local expected_platform = case_name == "target-absent-occupied" ? LOOP_POS : MAIN_POS
	if (receiver.get_pos().y == expected_platform.y && receiver.get_pos().x >= expected_platform.x
		&& receiver.get_current_entry_id() == RECEIVER_ENTRY_ID)
	{
		if (!check(fixture.signal_state != call_on, "the choose signal issued call-on with no target present")) { return }
		print("RUNTIME CASE " + case_name + ": receiver reached its free platform at " + choose_position(receiver) + " with no call-on")
		pass()
		return
	}

	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not settle on its free scheduled platform: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


function observe_watch_ignore_choose_absent()
{
	local receiver = receiver_convoy()
	if (!receiver.is_valid())
	{
		fail("the receiver convoy disappeared while waiting at the choose signal")
		return
	}
	observe_signal()
	if (fixture.signal_state == call_on)
	{
		fail("the choose signal issued call-on with ignore_choose and no target present")
		return
	}
	if (receiver.get_pos().y == 3)
	{
		fail("the receiver was diverted to the loop platform despite ignore_choose")
		return
	}
	if (receiver_held_at_signal())
	{
		print("RUNTIME CASE " + case_name + ": receiver stayed on the scheduled approach at " + choose_position(receiver) + " with ignore_choose")
		pass()
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not settle on the scheduled approach: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


function observe_wait_receiver_pass()
{
	local receiver = receiver_convoy()
	if (!receiver.is_valid())
	{
		fail("the receiver convoy disappeared before it passed the choose signal")
		return
	}
	observe_signal()
	if (receiver.get_current_entry_id() == RECEIVER_ENTRY_ID
		&& (convoy_is_loading(receiver) || receiver.get_state() == convoy_state_awaiting_recombination))
	{
		print("RUNTIME CASE " + case_name + ": receiver stopped at " + choose_position(receiver) + " with no target present")
		if (!start_convoy(player_x(0), target_convoy(), "target convoy")) { return }
		fixture.travel_steps = 0
		fixture.approach_steps = 0
		fixture.last_pos = null
		fixture.stationary_steps = 0
		fixture.phase = "watch-late-target"
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not reach a platform: " + choose_position(receiver) + " " + depict_state(receiver.get_state()))
	}
}


// The receiver is already standing at a platform. The target, started later,
// must become the mover and be routed to the receiver's platform under
// call-on.
function observe_watch_late_target()
{
	local target = target_convoy()
	local receiver = receiver_convoy()
	if (!target.is_valid() || !receiver.is_valid())
	{
		fail("a convoy disappeared while the later target was approaching")
		return
	}
	observe_signal()
	if (!fixture.drive_by_sight_seen && target.get_working_method() == drive_by_sight)
	{
		fixture.drive_by_sight_seen = true
		print("RUNTIME CASE " + case_name + ": later target is running in " + depict_working_method(target.get_working_method()) + " at " + choose_position(target))
	}
	local receiver_slots = receiver.get_vehicle_snapshot()
	local rear = receiver_slots[receiver_slots.len() - 1].pos
	local expected = coord3d(rear.x - 1, rear.y, rear.z)
	if (same_pos(target.get_pos(), expected))
	{
		if (!same_pos(fixture.last_pos, expected))
		{
			fixture.last_pos = expected
			fixture.stationary_steps = 0
		}
		else
		{
			++fixture.stationary_steps
		}
		if (fixture.stationary_steps >= HOLD_OBSERVATIONS)
		{
			if (!check(fixture.call_on_seen, "the choose signal never displayed call-on for the later target")) { return }
			if (!check(target.get_working_method() == drive_by_sight, "the later target is in " + depict_working_method(target.get_working_method()) + " after call-on, expected drive by sight")) { return }
			print("RUNTIME CASE " + case_name + ": later target stopped at " + choose_position(target) + " behind the standing receiver")
			pass()
			return
		}
	}
	else
	{
		fixture.last_pos = null
		fixture.stationary_steps = 0
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("timed out waiting for the later target to stop behind the receiver: target " + choose_position(target) + " " + depict_state(target.get_state()) + ", receiver rear (" + rear.x + "," + rear.y + "), signal " + depict_aspect(fixture.signal_state))
	}
}


function observe()
{
	if (fixture.phase == "wait-target") { observe_wait_target(); return }
	if (fixture.phase == "wait-target-blocked") { observe_wait_target_blocked(); return }
	if (fixture.phase == "wait-blocker") { observe_wait_blocker(); return }
	if (fixture.phase == "watch-blocked") { observe_watch_blocked(); return }
	if (fixture.phase == "wait-occupier-main") { observe_wait_occupier_main(); return }
	if (fixture.phase == "watch-approach") { observe_watch_approach(); return }
	if (fixture.phase == "watch-unreachable") { observe_watch_unreachable(); return }
	if (fixture.phase == "watch-free-platform") { observe_watch_free_platform(); return }
	if (fixture.phase == "watch-ignore-choose-absent") { observe_watch_ignore_choose_absent(); return }
	if (fixture.phase == "wait-receiver-pass") { observe_wait_receiver_pass(); return }
	if (fixture.phase == "watch-late-target") { observe_watch_late_target(); return }
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
	return ttext("Run the selected choose-signal call-on case and assert its end state.")
}


function get_info_text(pl)
{
	local msg = ttext("Choose-signal runtime case {name}: {phase}")
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
	if (case_name != "choose-reload")
	{
		return
	}
	if (!("reload" in persistent) || typeof(persistent.reload) != "table")
	{
		fail("the savegame holds no persisted reload state; it must be saved while the receiver waits at the signal")
		return
	}

	local state = persistent.reload
	// Drop the state so that a later save cannot restore a stale pre-reload
	// state.
	delete persistent.reload
	fixture.receiver_id = state.receiver_id
	fixture.target_id = state.target_id
	fixture.occupier_id = state.blocker_id
	fixture.last_pos = null
	fixture.stationary_steps = 0

	local receiver = convoy_x(fixture.receiver_id)
	local target = convoy_x(fixture.target_id)
	if (!check(receiver.is_valid(), "the receiver named by the persisted state is not in the loaded world")) { return }
	if (!check(target.is_valid(), "the target named by the persisted state is not in the loaded world")) { return }

	local partner = receiver.get_recombination_approach_partner()
	if (!check(partner != null && partner.is_valid() && partner.id == fixture.target_id, "the receiver's approach partner was not restored")) { return }
	if (!check(receiver.is_recombination_approach_mover(), "the receiver is not the mover in the restored approach")) { return }

	local reciprocal = target.get_recombination_approach_partner()
	if (!check(reciprocal != null && reciprocal.is_valid() && reciprocal.id == fixture.receiver_id, "the target's reciprocal approach partner was not restored")) { return }
	if (!check(!target.is_recombination_approach_mover(), "the target is the mover in the restored approach")) { return }

	if (!check(receiver.get_state() == convoy_state_waiting_for_clearance, "the receiver is not waiting for clearance after the reload")) { return }
	local p = receiver.get_pos()
	if (!check(p.y == SIGNAL_POS.y && p.x <= SIGNAL_POS.x + 1, "the receiver is not held at the choose signal after the reload")) { return }

	fixture.phase = "restored-pending"
	print("RUNTIME CASE " + case_name + " RESTORED")
}


function is_scenario_completed(pl)
{
	if (pl != 0)
	{
		return 0
	}
	if (fixture.phase == "awaiting-reload")
	{
		return 0
	}
	if (fixture.phase == "restored-pending")
	{
		pass()
		return 100
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








