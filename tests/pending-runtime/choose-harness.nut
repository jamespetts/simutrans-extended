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
local LAYOUT_STEP_LIMIT = 500

// Vehicle steps per car unit. The script API reports vehicle positions in
// vehicle steps and vehicle lengths in car units. This mirrors
// VEHICLE_STEPS_PER_CARUNIT in simunits.h.
local VEHICLE_STEPS_PER_CARUNIT = 16
// Vehicle steps per tile. This mirrors VEHICLE_STEPS_PER_TILE in simunits.h.
local VEHICLE_STEPS_PER_TILE = 256

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
	target_front_pos = null,
	target_rear_pos = null,
	target_front_steps = 0,
	target_rear_steps = 0,
	target_contact_pos = null,
	target_contact_steps = 0,
	pair_contact_seen = false,
	layout_steps = 0,
	last_target_rear_steps = -1,
	target_settle_count = 0,
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


function pos_text(p)
{
	return p == null ? "<none>" : "(" + p.x + "," + p.y + ")"
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


// The coupling point at a standing convoy's rear: the rear vehicle's front
// position minus its length, in the tile that contains it. The fixture's trains
// run east, so an offset behind the tile start lies in the tile to the west.
// This mirrors convoi_t::get_recombination_contact_point.
function contact_slot(standing)
{
	local slots = standing.get_vehicle_snapshot()
	local rear = slots[slots.len() - 1]
	local offset = rear.steps - rear.desc.get_length() * VEHICLE_STEPS_PER_CARUNIT
	local pos = rear.pos
	while (offset <= 0)
	{
		pos = coord3d(pos.x - 1, pos.y, pos.z)
		offset += VEHICLE_STEPS_PER_TILE
	}
	if (offset >= VEHICLE_STEPS_PER_TILE)
	{
		offset = VEHICLE_STEPS_PER_TILE - 1
	}
	return { pos = pos, steps = offset }
}


// The coupling point is the standing convoy's rear vehicle's front position
// minus that vehicle's length, in vehicle steps within the tile that contains
// it. This mirrors convoi_t::get_recombination_contact_ground. Returns 0 while
// the mover is still moving, -1 after reporting a mismatch, and 1 when the
// mover has stopped at the coupling point.
function check_coupling_stop(mover, standing, what)
{
	if (mover.get_speed() != 0)
	{
		return 0
	}
	local contact = contact_slot(standing)
	local front = mover.get_vehicle_snapshot()[0]
	if (!check(front.steps == contact.steps,
		what + " stopped at steps " + front.steps + " in the contact tile " + pos_text(contact.pos)
		+ ", expected the coupling point " + contact.steps))
	{
		return -1
	}
	return 1
}


function slot_matches(slot, desc)
{
	return slot != null && slot.desc != null && desc != null && slot.desc.is_equal(desc)
}


// The fixture's trains run east, so a slot with a smaller x is behind one with
// a larger x. Within one tile the smaller steps value is behind.
function slot_is_behind(a, b)
{
	if (a.pos.x != b.pos.x)
	{
		return a.pos.x < b.pos.x
	}
	return a.steps < b.steps
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
	if (case_name == "target-absent-occupied" || case_name == "straddling-contact" || case_name == "committed-target")
	{
		// The receiver needs two platform tiles; keep the loop a valid alternative.
		// The straddling case needs a straight two-tile platform so that the long
		// target's rear end lies in a straight tile, not on a corner. The
		// committed-target case needs the target to stay driving inside the halt
		// while the receiver reaches the choose signal.
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
	if (case_name == "straddling-contact" || case_name == "committed-target")
	{
		local extended_halt = halt_x.get_halt(coord3d(6, 3, 0), pl)
		if (!check(extended_halt != null && extended_halt.get_name() == main_halt.get_name(), "the extended loop platform belongs to a separate halt")) { return false }
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


// The receiver's order cannot be completed from the two consists: the named
// vehicle is in neither and the pool is not available to a named one-sided
// receive. The choose signal must hold the receiver at the signal.
function commit_receiver_unresolvable_schedule(pl, receiver, target_id)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_c]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the unresolvable receiver schedule")) { return false }
	return true
}


// A paired counterpart: the target's loop entry uncouples to the receiver and
// names the receiver's entry in return. It therefore waits in
// AWAITING_RECOMBINATION instead of running a division.
function commit_pair_target_schedule(pl, target, receiver_id, order)
{
	local entries = [make_entry(LOOP_POS, TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].uncouple = true
	entries[0].uncouple_target_is_line_or_cnv = true
	entries[0].target_id_uncouple = receiver_id
	entries[0].target_unique_entry_uncouple = RECEIVER_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(TARGET_ENTRY_ID, order)
	if (!check_tool(target.change_schedule(pl, schedule), "commit the paired target schedule")) { return false }

	local back = target.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the paired target schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].uncouple == true, "paired target schedule read-back: the uncouple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_uncouple == receiver_id, "paired target schedule read-back: target id is " + back.entries[0].target_id_uncouple + ", expected " + receiver_id)) { return false }
	return true
}


// The receiver side of a pair: its entry couples to the target and names the
// target's entry in return.
function commit_pair_receiver_schedule(pl, receiver, target_id, order)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id
	entries[0].target_unique_entry_couple = TARGET_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, order)
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the paired receiver schedule")) { return false }

	local back = receiver.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the paired receiver schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].couple == true, "paired receiver schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_couple == target_id, "paired receiver schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + target_id)) { return false }
	return true
}


// The straddling case needs the target to stop at the east end of a straight
// two-tile platform, so that its long rear vehicle's rear end lies in the
// straight tile behind its front tile rather than on a corner.
function commit_straddling_target_schedule(pl, target)
{
	local entries = [make_entry(coord3d(6, 3, 0), TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].minimum_loading = 100
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(target.change_schedule(pl, schedule), "commit the straddling target schedule")) { return false }
	return true
}


// The receiver side of a single-vehicle contact fixture: its order restates its
// own locomotive, so the receive changes nothing and the contact stop can be
// observed without a commit.
function commit_receiver_single_schedule(pl, receiver, target_id, desc)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[desc]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the single-vehicle receiver schedule")) { return false }
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
	// The order restates the receiver's own two vehicles, so it is satisfiable
	// from the two consists and changes nothing. An unsatisfiable order is a
	// separate case (unresolvable-order), which must hold at the signal.
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_b], [fixture.coach_b]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the receiver convoy schedule")) { return false }

	local back = receiver.get_schedule()
	if (!check(back != null && back.entries.len() == 2, "the receiver schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected 2")) { return false }
	if (!check(back.entries[0].couple == true, "receiver schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[0].target_id_couple == target_id, "receiver schedule read-back: target id is " + back.entries[0].target_id_couple + ", expected " + target_id)) { return false }
	if (!check(back.entries[0].ignore_choose == (ignore_choose ? true : false), "receiver schedule read-back: ignore_choose is not " + (ignore_choose ? "set" : "clear"))) { return false }
	return true
}


// The layout case needs a target that stays loading and available. Its entry
// carries no couple order, so it never waits for another convoy, and it demands
// full loading so that the empty fixture stop keeps it in the loading state.
function commit_layout_target_schedule(pl, target)
{
	local entries = [make_entry(LOOP_POS, TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].ignore_choose = true
	entries[0].minimum_loading = 100
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(target.change_schedule(pl, schedule), "commit the layout target schedule")) { return false }
	return true
}


// The receiver takes both target vehicles. Its own two vehicles become loose
// stock and the emptied target is destroyed. The loose stock must be laid out
// behind the receiver rather than at the target's former slots.
function commit_receiver_take_all_schedule(pl, receiver, target_id)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_a], [fixture.coach_a]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the receiver take-all schedule")) { return false }
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


// The reservation case's target stays loading indefinitely: it demands full
// loading at an empty stop and has no timed wait, so it never departs while the
// case observes the shared contact tile.
function commit_contact_target_schedule(pl, target)
{
	local entries = [make_entry(LOOP_POS, TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].minimum_loading = 100
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(target.change_schedule(pl, schedule), "commit the contact target schedule")) { return false }
	return true
}


// The reservation case's receiver restates its own consist, so the receive
// changes nothing at the contact stop and the receiver departs normally. Its
// departure is what leaves the shared contact tile while the target still holds
// it.
function commit_contact_receiver_schedule(pl, receiver, target_id)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_b], [fixture.coach_b]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the contact receiver schedule")) { return false }
	return true
}


// The committed-target case's target runs to the far end of the extended loop
// platform and stays there loading. The receiver, started after the target has
// passed the choose signal, reaches the signal while the target is still driving
// through the halt.
function commit_committed_target_schedule(pl, target)
{
	local entries = [make_entry(coord3d(6, 3, 0), TARGET_ENTRY_ID), make_entry(EAST_POS, TARGET_NEXT_ENTRY_ID)]
	entries[0].minimum_loading = 100
	local schedule = schedule_x(wt_rail, entries)
	if (!check_tool(target.change_schedule(pl, schedule), "commit the committed target schedule")) { return false }
	return true
}


function commit_committed_receiver_schedule(pl, receiver, target_id)
{
	local entries = [make_entry(MAIN_POS, RECEIVER_ENTRY_ID), make_entry(EAST_POS, RECEIVER_NEXT_ENTRY_ID)]
	entries[0].couple = true
	entries[0].couple_target_is_line_or_cnv = true
	entries[0].target_id_couple = target_id

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_order([[fixture.loco_b], [fixture.coach_b]]))
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the committed receiver schedule")) { return false }
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

	local receiver = create_depot_convoy(pl, case_name == "straddling-contact" ? [fixture.loco_c] : [fixture.loco_b, fixture.coach_b], "receiver convoy")
	if (receiver == null) { return false }
	fixture.receiver_id = receiver.id

	local target = create_depot_convoy(pl, case_name == "straddling-contact" ? [fixture.loco_a, fixture.loco_b] : [fixture.loco_a, fixture.coach_a], "target convoy")
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

	if (case_name == "target-present" || case_name == "target-present-ignore-choose" || case_name == "moving-block" || case_name == "multiple-choose-signals" || case_name == "target-unreachable" || case_name == "one-sided-layout" || case_name == "unresolvable-order")
	{
		if (case_name == "one-sided-layout")
		{
			if (!commit_layout_target_schedule(pl, target)) { return false }
			if (!commit_receiver_take_all_schedule(pl, receiver, target.id)) { return false }
		}
		else if (case_name == "unresolvable-order")
		{
			if (!commit_target_schedule(pl, target, hold.id)) { return false }
			if (!commit_receiver_unresolvable_schedule(pl, receiver, target.id)) { return false }
		}
		else
		{
			if (!commit_target_schedule(pl, target, hold.id)) { return false }
			if (!commit_receiver_schedule(pl, receiver, target.id, case_name == "target-present-ignore-choose")) { return false }
		}
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-target"
		return true
	}

	if (case_name == "straddling-contact")
	{
		// The target is one long vehicle whose rear end lies in the tile behind
		// its front tile. The receiver must stop at the coupling point in that
		// behind tile, not enter the target's front tile.
		if (!commit_straddling_target_schedule(pl, target)) { return false }
		if (!commit_receiver_single_schedule(pl, receiver, target.id, fixture.loco_c)) { return false }
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-target"
		return true
	}

	if (case_name == "pair-layout" || case_name == "pair-unresolvable")
	{
		local target_order = case_name == "pair-layout" ? make_order([[fixture.loco_b]]) : make_order([[fixture.loco_a]])
		local receiver_order = case_name == "pair-layout"
			? make_order([[fixture.loco_a], [fixture.coach_a]])
			: make_order([[fixture.loco_c]])
		if (!commit_pair_target_schedule(pl, target, receiver.id, target_order)) { return false }
		if (!commit_pair_receiver_schedule(pl, receiver, target.id, receiver_order)) { return false }
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

	if (case_name == "contact-reservation")
	{
		// The target stays loading. The receiver restates its own consist and
		// departs, leaving the shared contact tile while the target still holds
		// its reservation.
		if (!commit_contact_target_schedule(pl, target)) { return false }
		if (!commit_contact_receiver_schedule(pl, receiver, target.id)) { return false }
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-target"
		return true
	}

	if (case_name == "committed-target")
	{
		// The target leads out of the depot. The receiver follows as soon as the
		// depot tile is clear, so it reaches the choose signal while the target is
		// still running between that signal and the platform.
		if (!commit_committed_target_schedule(pl, target)) { return false }
		if (!commit_committed_receiver_schedule(pl, receiver, target.id)) { return false }
		if (!start_convoy(pl, target, "target convoy")) { return false }
		fixture.phase = "wait-committed-start-receiver"
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
		local target_slots = target.get_vehicle_snapshot()
		local rear_steps = target_slots[target_slots.len() - 1].steps
		// Let the standing target settle before the receiver starts: the
		// coupling point is computed from the target's rear position, so a
		// target that is still braking would record a stale point.
		if (target.get_speed() != 0 || rear_steps != fixture.last_target_rear_steps)
		{
			fixture.last_target_rear_steps = rear_steps
			fixture.target_settle_count = 0
			return
		}
		++fixture.target_settle_count
		if (fixture.target_settle_count < 10)
		{
			return
		}
		print("RUNTIME CASE " + case_name + ": target is loading at its scheduled platform")
		fixture.target_front_pos = target_slots[0].pos
		fixture.target_front_steps = target_slots[0].steps
		fixture.target_rear_pos = target_slots[target_slots.len() - 1].pos
		fixture.target_rear_steps = target_slots[target_slots.len() - 1].steps
		local contact = contact_slot(target)
		fixture.target_contact_pos = contact.pos
		fixture.target_contact_steps = contact.steps
		print("RUNTIME CASE " + case_name + ": target contact tile " + pos_text(contact.pos) + " at steps " + contact.steps)
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
		if (case_name == "target-unreachable")
		{
			fixture.phase = "watch-unreachable"
		}
		else if (case_name == "unresolvable-order" || case_name == "pair-unresolvable")
		{
			fixture.phase = "watch-unresolvable"
		}
		else if (case_name == "pair-layout")
		{
			fixture.phase = "watch-pair-layout"
		}
		else if (case_name == "contact-reservation")
		{
			fixture.phase = "watch-contact-hold"
		}
		else
		{
			fixture.phase = "watch-approach"
		}
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
	// The claimed mover enters the contact tile and stops at the coupling
	// point inside it.
	local target_slots = target.get_vehicle_snapshot()
	local rear = target_slots[target_slots.len() - 1].pos
	local contact = { pos = fixture.target_contact_pos, steps = fixture.target_contact_steps }
	local before_rear = coord3d(rear.x - 1, rear.y, rear.z)
	if (same_pos(receiver.get_pos(), contact.pos)
		|| (case_name == "moving-block" && same_pos(receiver.get_pos(), before_rear)))
	{
		if (!check(case_name == "moving-block" || fixture.call_on_seen || fixture.drive_by_sight_seen, "the choose signal never displayed call-on; receiver is in " + depict_working_method(receiver.get_working_method()))) { return }
		if (!check(fixture.drive_by_sight_seen, "the receiver never ran in drive by sight after call-on")) { return }
		if (case_name == "one-sided-layout")
		{
			// The receive commits after arrival, so continue observing until the
			// target has gone and the final consist can be checked.
			fixture.phase = "watch-layout"
			return
		}
		if (case_name != "moving-block")
		{
			local coupling = check_coupling_stop(receiver, target, "the receiver")
			if (coupling != 1)
			{
				// Still running towards the coupling point, or the check failed.
				return
			}
		}
		print("RUNTIME CASE " + case_name + ": receiver reached " + choose_position(receiver))
		pass()
		return
	}

	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("timed out waiting for the receiver to stop behind the target: receiver " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", contact tile (" + contact.pos.x + "," + contact.pos.y + "), signal " + depict_aspect(fixture.signal_state))
	}
}


// The receiver has taken both target vehicles. The emptied target is destroyed,
// and the receiver's former vehicles become loose stock. The final consist
// occupies the target's former slots, and the loose stock must be laid out
// behind the receiver rather than overlapping the target's former slots.
function observe_watch_layout()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid())
	{
		fail("the receiver disappeared during the one-sided layout observation")
		return
	}
	local receiver_slots = receiver.get_vehicle_snapshot()
	if (target.is_valid())
	{
		++fixture.layout_steps
		if (fixture.layout_steps > LAYOUT_STEP_LIMIT)
		{
			fail("timed out waiting for the one-sided receive to commit: receiver has " + receiver_slots.len() + " vehicles at " + choose_position(receiver) + " in " + depict_state(receiver.get_state()) + "; target at " + choose_position(target) + " in " + depict_state(target.get_state()) + " with " + target.get_vehicle_snapshot().len() + " vehicles")
		}
		return
	}
	if (!check(receiver_slots.len() == 2, "the receiver has " + receiver_slots.len() + " vehicles after the receive, expected 2")) { return }
	if (!check(slot_matches(receiver_slots[0], fixture.loco_a), "the receiver's first vehicle is not the target's locomotive")) { return }
	if (!check(slot_matches(receiver_slots[1], fixture.coach_a), "the receiver's second vehicle is not the target's coach")) { return }
	if (!check(same_pos(receiver_slots[0].pos, fixture.target_front_pos) && receiver_slots[0].steps == fixture.target_front_steps,
		"the receiver's front is at " + pos_text(receiver_slots[0].pos) + " steps " + receiver_slots[0].steps
		+ ", expected the target's former front slot " + pos_text(fixture.target_front_pos) + " steps " + fixture.target_front_steps)) { return }
	if (!check(same_pos(receiver_slots[1].pos, fixture.target_rear_pos) && receiver_slots[1].steps == fixture.target_rear_steps,
		"the receiver's second vehicle is at " + pos_text(receiver_slots[1].pos) + " steps " + receiver_slots[1].steps
		+ ", expected the target's former rear slot " + pos_text(fixture.target_rear_pos) + " steps " + fixture.target_rear_steps)) { return }

	local loose = null
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid() && cnv.get_state() == convoy_state_layover && cnv.get_vehicle_snapshot().len() == 2)
		{
			loose = cnv
			break
		}
	}
	if (!check(loose != null, "no two-vehicle laid-over convoy found after the receive")) { return }
	local loose_slots = loose.get_vehicle_snapshot()
	if (!check(slot_matches(loose_slots[0], fixture.loco_b), "the loose convoy's first vehicle is not the receiver's former locomotive")) { return }
	if (!check(slot_matches(loose_slots[1], fixture.coach_b), "the loose convoy's second vehicle is not the receiver's former coach")) { return }
	if (!check(slot_is_behind(loose_slots[0], receiver_slots[1]), "the loose stock is not behind the receiver's rear vehicle")) { return }
	if (!check(slot_is_behind(loose_slots[1], loose_slots[0]), "the loose convoy's second vehicle is not behind its first")) { return }
	print("RUNTIME CASE " + case_name + ": one-sided receive laid out with loose stock behind the receiver at " + pos_text(loose_slots[0].pos))
	pass()
}


// The receiver's order cannot be completed from the two consists. The choose
// signal must keep the receiver at the signal, at danger, with no call-on and
// no entry into the target's platform.
function observe_watch_unresolvable()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared while the coupling pre-check refused the approach")
		return
	}
	observe_signal()
	if (fixture.signal_state == call_on)
	{
		fail("the choose signal issued call-on for an unsatisfiable coupling")
		return
	}
	if (receiver.get_pos().y == LOOP_POS.y)
	{
		fail("the receiver entered the target platform despite an unsatisfiable coupling: " + choose_position(receiver))
		return
	}
	if (receiver_held_at_signal())
	{
		print("RUNTIME CASE " + case_name + ": receiver is held at the choose signal because the coupling cannot be completed")
		pass()
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not settle at the choose signal with an unsatisfiable coupling: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


// A solvable paired exchange reaches contact. The commit then lays out the
// target's final consist on the front slots, the receiver's final consist on
// the slots behind it, and the loose stock on the remaining slots.
function observe_watch_pair_layout()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared during the paired layout observation")
		return
	}
	observe_signal()

	if (!fixture.pair_contact_seen)
	{
		if (target.get_vehicle_snapshot().len() == 1)
		{
			// The commit ran in the same step as the arrival.
			fixture.pair_contact_seen = true
			fixture.layout_steps = 0
			return
		}
		if (same_pos(receiver.get_pos(), fixture.target_contact_pos) && receiver.get_speed() == 0)
		{
			print("RUNTIME CASE " + case_name + ": receiver reached the contact tile " + pos_text(fixture.target_contact_pos) + " at steps " + receiver.get_vehicle_snapshot()[0].steps)
			fixture.pair_contact_seen = true
			fixture.layout_steps = 0
			return
		}
		++fixture.approach_steps
		if (fixture.approach_steps > APPROACH_STEP_LIMIT)
		{
			fail("the receiver did not reach the paired target's contact tile: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", contact tile " + pos_text(fixture.target_contact_pos))
		}
		return
	}

	++fixture.layout_steps
	if (fixture.layout_steps > LAYOUT_STEP_LIMIT)
	{
		fail("timed out waiting for the paired exchange to commit: receiver has " + receiver.get_vehicle_snapshot().len() + " vehicles, target has " + target.get_vehicle_snapshot().len() + " vehicles")
		return
	}
	local receiver_slots = receiver.get_vehicle_snapshot()
	local target_slots = target.get_vehicle_snapshot()
	if (target_slots.len() != 1 || receiver_slots.len() != 2)
	{
		return
	}
	if (!check(slot_matches(target_slots[0], fixture.loco_b), "the target's final consist does not hold the receiver's locomotive")) { return }
	if (!check(same_pos(target_slots[0].pos, fixture.target_front_pos) && target_slots[0].steps == fixture.target_front_steps,
		"the target's final locomotive is at " + pos_text(target_slots[0].pos) + " steps " + target_slots[0].steps
		+ ", expected the target's former front slot " + pos_text(fixture.target_front_pos) + " steps " + fixture.target_front_steps)) { return }
	if (!check(slot_matches(receiver_slots[0], fixture.loco_a), "the receiver's first vehicle is not the target's locomotive")) { return }
	if (!check(same_pos(receiver_slots[0].pos, fixture.target_rear_pos) && receiver_slots[0].steps == fixture.target_rear_steps,
		"the receiver's front is at " + pos_text(receiver_slots[0].pos) + " steps " + receiver_slots[0].steps
		+ ", expected the target's former rear slot " + pos_text(fixture.target_rear_pos) + " steps " + fixture.target_rear_steps)) { return }
	if (!check(slot_matches(receiver_slots[1], fixture.coach_a), "the receiver's second vehicle is not the target's coach")) { return }
	if (!check(slot_is_behind(receiver_slots[1], receiver_slots[0]), "the receiver's second vehicle is not behind its first")) { return }

	local loose = null
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid() && cnv.get_state() == convoy_state_layover && cnv.get_vehicle_snapshot().len() == 1)
		{
			loose = cnv
			break
		}
	}
	if (!check(loose != null, "no one-vehicle laid-over convoy found after the paired exchange")) { return }
	local loose_slots = loose.get_vehicle_snapshot()
	if (!check(slot_matches(loose_slots[0], fixture.coach_b), "the loose convoy does not hold the receiver's former coach")) { return }
	if (!check(slot_is_behind(loose_slots[0], receiver_slots[1]), "the loose stock is not behind the receiver's rear vehicle")) { return }
	print("RUNTIME CASE " + case_name + ": paired exchange laid out with the loose stock behind the receiver")
	pass()
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
	local contact = contact_slot(receiver)
	if (same_pos(target.get_pos(), contact.pos))
	{
		if (!check(fixture.call_on_seen || fixture.drive_by_sight_seen, "the choose signal never displayed call-on for the later target")) { return }
		if (!check(fixture.drive_by_sight_seen, "the later target never ran in drive by sight after call-on")) { return }
		local coupling = check_coupling_stop(target, receiver, "the later target")
		if (coupling != 1)
		{
			// Still running towards the coupling point, or the check failed.
			return
		}
		print("RUNTIME CASE " + case_name + ": later target reached the receiver contact tile at " + choose_position(target))
		pass()
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("timed out waiting for the later target to stop behind the receiver: target " + choose_position(target) + " " + depict_state(target.get_state()) + ", contact tile (" + contact.pos.x + "," + contact.pos.y + "), signal " + depict_aspect(fixture.signal_state))
	}
}


// The reservation holder of a tile, or 0 when the tile is not reserved.
function contact_holder_id(pos)
{
	local way = tile_x(pos.x, pos.y, pos.z).get_way(wt_rail)
	if (way == null)
	{
		return 0
	}
	local holder = way.get_reserved_convoi()
	return holder != null && holder.is_valid() ? holder.id : 0
}


// The receiver has stopped at the coupling point with the target ahead of it in
// the same tile. The target reserved the tile first, so the target must still be
// the holder while both consists occupy it.
function observe_watch_contact_hold()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared while the receiver approached the contact tile")
		return
	}
	observe_signal()
	if (same_pos(receiver.get_pos(), fixture.target_contact_pos))
	{
		local coupling = check_coupling_stop(receiver, target, "the receiver")
		if (coupling != 1)
		{
			// Still running towards the coupling point, or the check failed.
			return
		}
		local holder = contact_holder_id(fixture.target_contact_pos)
		if (!check(holder == target.id, "the contact tile is held by convoy " + holder + " while both consists stand in it, expected the target " + target.id)) { return }
		print("RUNTIME CASE " + case_name + ": receiver stopped at the coupling point with the target holding the contact tile")
		fixture.layout_steps = 0
		fixture.phase = "watch-contact-departure"
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver did not stop in the contact tile: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", contact tile " + pos_text(fixture.target_contact_pos))
	}
}


// The receiver departs after the no-change receive. When its last vehicle leaves
// the shared contact tile, the target still occupies and holds the tile, so the
// release must be suppressed and the target must remain the holder.
function observe_watch_contact_departure()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!target.is_valid())
	{
		fail("the target disappeared while the receiver left the contact tile")
		return
	}
	if (!receiver.is_valid())
	{
		fail("the receiver disappeared before it left the contact tile")
		return
	}
	foreach (slot in receiver.get_vehicle_snapshot())
	{
		if (same_pos(slot.pos, fixture.target_contact_pos))
		{
			++fixture.layout_steps
			if (fixture.layout_steps > LAYOUT_STEP_LIMIT)
			{
				fail("the receiver did not leave the contact tile: front " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", entry " + receiver.get_current_entry_id())
			}
			return
		}
	}
	local holder = contact_holder_id(fixture.target_contact_pos)
	if (!check(holder == target.id, "the contact tile reservation is held by convoy " + holder + " after the receiver left it, expected the target " + target.id)) { return }
	if (!check(target.get_state() == convoy_state_loading, "the target is in " + depict_state(target.get_state()) + " after the receiver left the contact tile, expected loading")) { return }
	print("RUNTIME CASE " + case_name + ": the target kept the contact tile reservation after the receiver left it")
	pass()
}


// Start the receiver only after the target has left the depot tile. Two convoys
// started from the same depot tile in one step overlap on that tile.
function observe_wait_committed_start_receiver()
{
	local target = target_convoy()
	if (!target.is_valid())
	{
		fail("the committed target disappeared before it left the depot")
		return
	}
	++fixture.travel_steps
	if (fixture.travel_steps > TRAVEL_STEP_LIMIT)
	{
		fail("the committed target did not leave the depot: " + choose_position(target) + " " + depict_state(target.get_state()))
		return
	}
	// Start the receiver as soon as the target has left the depot tile. The
	// receiver then follows to the choose signal while the target runs through
	// the halt, so the committed hold lasts several observation steps.
	local tp = target.get_pos()
	if (same_pos(tp, DEPOT_POS))
	{
		return
	}
	print("RUNTIME CASE " + case_name + ": committed target left the depot at " + choose_position(target) + "; starting the receiver")
	if (!start_convoy(player_x(0), receiver_convoy(), "receiver convoy")) { return }
	fixture.phase = "wait-committed-hold"
}


// The receiver has reached the choose signal while the target is still running
// between the signal and the platform. The choose signal must hold the receiver
// and record the reciprocal approach with the receiver as the mover.
function observe_wait_committed_hold()
{
	local receiver = receiver_convoy()
	local target = target_convoy()
	if (!receiver.is_valid() || !target.is_valid())
	{
		fail("a convoy disappeared while the receiver approached the choose signal")
		return
	}
	observe_signal()
	if (receiver.get_state() == convoy_state_waiting_for_clearance && receiver.get_pos().x >= SIGNAL_POS.x - 1)
	{
		local p = receiver.get_pos()
		if (!same_pos(fixture.last_pos, p))
		{
			fixture.last_pos = p
			fixture.stationary_steps = 0
			return
		}
		++fixture.stationary_steps
		if (fixture.stationary_steps < 1)
		{
			return
		}
		if (!check(target.get_state() == convoy_state_driving, "the target is in " + depict_state(target.get_state()) + " at the choose signal hold, expected driving between the signal and the platform")) { return }
		if (!check(fixture.signal_state != call_on, "the choose signal issued call-on for a committed target")) { return }
		local partner = receiver.get_recombination_approach_partner()
		if (!check(partner != null && partner.is_valid() && partner.id == target.id, "the receiver's approach partner is not the committed target")) { return }
		if (!check(receiver.is_recombination_approach_mover(), "the receiver is not the mover in the committed-target approach")) { return }
		local reciprocal = target.get_recombination_approach_partner()
		if (!check(reciprocal != null && reciprocal.is_valid() && reciprocal.id == receiver.id, "the target's reciprocal approach partner is not the receiver")) { return }
		if (!check(!target.is_recombination_approach_mover(), "the target is the mover in the committed-target approach")) { return }
		print("RUNTIME CASE " + case_name + ": receiver held at the choose signal while the committed target runs to the platform")
		fixture.approach_steps = 0
		fixture.phase = "watch-committed-approach"
		return
	}
	++fixture.approach_steps
	if (fixture.approach_steps > APPROACH_STEP_LIMIT)
	{
		fail("the receiver never waited at the choose signal: " + choose_position(receiver) + " " + depict_state(receiver.get_state()) + ", signal " + depict_aspect(fixture.signal_state))
	}
}


// Wait for the committed target to become available at the platform and record
// its coupling point, then use the ordinary shared-contact observations.
function observe_watch_committed_approach()
{
	local target = target_convoy()
	if (!target.is_valid())
	{
		fail("the committed target disappeared before it reached the platform")
		return
	}
	observe_signal()
	if (!convoy_is_loading(target))
	{
		++fixture.approach_steps
		if (fixture.approach_steps > APPROACH_STEP_LIMIT)
		{
			fail("the committed target did not start loading: " + choose_position(target) + " " + depict_state(target.get_state()))
		}
		return
	}
	local contact = contact_slot(target)
	fixture.target_contact_pos = contact.pos
	fixture.target_contact_steps = contact.steps
	print("RUNTIME CASE " + case_name + ": committed target is loading; contact tile " + pos_text(contact.pos) + " at steps " + contact.steps)
	fixture.approach_steps = 0
	fixture.phase = "watch-approach"
}


function observe()
{
	if (fixture.phase == "wait-target") { observe_wait_target(); return }
	if (fixture.phase == "wait-target-blocked") { observe_wait_target_blocked(); return }
	if (fixture.phase == "wait-blocker") { observe_wait_blocker(); return }
	if (fixture.phase == "watch-blocked") { observe_watch_blocked(); return }
	if (fixture.phase == "wait-occupier-main") { observe_wait_occupier_main(); return }
	if (fixture.phase == "watch-approach") { observe_watch_approach(); return }
	if (fixture.phase == "watch-contact-hold") { observe_watch_contact_hold(); return }
	if (fixture.phase == "watch-contact-departure") { observe_watch_contact_departure(); return }
	if (fixture.phase == "wait-committed-start-receiver") { observe_wait_committed_start_receiver(); return }
	if (fixture.phase == "wait-committed-hold") { observe_wait_committed_hold(); return }
	if (fixture.phase == "watch-committed-approach") { observe_watch_committed_approach(); return }
	if (fixture.phase == "watch-layout") { observe_watch_layout(); return }
	if (fixture.phase == "watch-unresolvable") { observe_watch_unresolvable(); return }
	if (fixture.phase == "watch-pair-layout") { observe_watch_pair_layout(); return }
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








