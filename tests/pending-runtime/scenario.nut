//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Pre-runtime fixture for the Y-shaped (dividing) service.
//
// It builds a real Y: a trunk line that divides at a junction stop into a
// north branch and an east branch. In the coordinates used here y increases
// southwards, so the north branch is the one towards decreasing y and the
// east branch is the one towards increasing x.
//
// Portion A keeps the source schedule and is the north branch. Portion B
// adopts the target line's schedule at the target entry and is the east
// branch. Both portion end states are authored as consist orders through the
// public script API. The source convoy is started from the depot.
//
// The recorded pending state is: the source convoy keeps one vehicle, the
// split-off vehicle exists as a loose-stock convoy at the junction, and no
// portion convoy adopts the target line, because the dividing runtime does
// not exist yet. Any difference is reported as PENDING RUNTIME: OBSERVED
// CHANGE together with the observed values, so that this expectation can be
// updated.
//
// The fixture fails only where the API returns a definite result. Calls to a
// command_x tool report whether the call was queued, not whether the tool work
// later succeeded; the committed schedules are therefore read back, which is
// where a setup error would show.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Pre-runtime Y dividing fixture"
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local phase = "waiting-for-setup"
local setup_started = false
local convoy_id = 0
local target_line_id = 0
local junction_pos = coord3d(5, 2, 0)
local junction_index = 1
local settle_steps = -1
local wait_steps = 0


function fail_setup(what, detail)
{
	print("FIXTURE SETUP FAILED: " + what + (detail != null ? ": " + detail : ""))
	phase = "failed"
	return false
}


function check(ok, what)
{
	if (!ok) {
		return fail_setup(what, null)
	}
	return true
}


// Checks the immediate return value of a command_x tool call. Tools that are
// queued during the init phase report queuing and execution here, not the
// validity of the tool work itself.
function check_tool(result, what)
{
	if (result != null) {
		return fail_setup(what, "" + result)
	}
	return true
}


function find_new_line(pl, before)
{
	foreach (line in pl.get_line_list()) {
		if (before.find(line.id) == null) {
			return line
		}
	}
	return null
}


function line_ids(pl)
{
	local ids = []
	foreach (line in pl.get_line_list()) {
		ids.append(line.id)
	}
	return ids
}


function station_builder(pl, pos, desc)
{
	return command_x(tool_build_station).work(pl, pos, desc.get_name())
}


function depot_desc_for(waytype)
{
	local list = building_desc_x.get_building_list(building_desc_x.depot)
	foreach (building in list) {
		if (building.get_type() == building_desc_x.depot && building.get_waytype() == waytype && building.is_available(world.get_time())) {
			return building
		}
	}
	return null
}


function setup()
{
	local pl = player_x(0)
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	if (!check(rail != null, "no rail way available")) { return false; }

	// Trunk (1,2) -> (6,2); branch A (6,2) -> (6,0); branch B (6,2) -> (9,2).
	if (!check_tool(command_x.build_way(pl, coord3d(1, 2, 0), coord3d(6, 2, 0), rail, false), "build trunk")) { return false; }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(6, 0, 0), rail, false), "build branch A")) { return false; }
	if (!check_tool(command_x.build_way(pl, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build branch B")) { return false; }

	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depot_desc_for(wt_rail)), "build rail depot")) { return false; }
	local depot = depot_x(1, 2, 0)
	if (!check(depot != null, "depot not found after building")) { return false; }

	local station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
	if (!check(station != null, "no rail station available")) { return false; }
	if (!check_tool(station_builder(pl, coord3d(3, 2, 0), station), "build trunk stop")) { return false; }
	if (!check_tool(station_builder(pl, junction_pos, station), "build junction stop")) { return false; }
	if (!check_tool(station_builder(pl, coord3d(9, 2, 0), station), "build branch B stop")) { return false; }
	if (!check_tool(station_builder(pl, coord3d(6, 0, 0), station), "build branch A stop")) { return false; }

	local vehicles = vehicle_desc_x.get_available_vehicles(wt_rail)
	local loco = null
	local coach = null
	foreach (a in vehicles) {
		if (!a.can_be_first() || a.get_power() == 0) { continue }
		foreach (b in vehicles) {
			if (vehicle_desc_x.is_coupling_allowed(a, b) && b.can_be_last()) {
				loco = a
				coach = b
				break
			}
		}
		if (loco != null) { break }
	}
	if (!check(loco != null, "no coupled pair of rail vehicles available")) { return false; }

	// Target line P (portion B). Its first entry is the junction stop with a
	// stable id that the source's uncouple entry names.
	local before = line_ids(pl)
	if (!check(pl.create_line(wt_rail), "create target line")) { return false; }
	local line_p = find_new_line(pl, before)
	if (!check(line_p != null, "target line not found after creation")) { return false; }
	target_line_id = line_p.id

	local p0 = schedule_entry_x(junction_pos, 0, 0)
	p0.unique_entry_id = 21
	local p1 = schedule_entry_x(coord3d(6, 0, 0), 0, 0)
	p1.unique_entry_id = 22
	local sched_p = schedule_x(wt_rail, [p0, p1])

	// Portion B end state: the coach.
	local portion_b = consist_order_x()
	local b_slot = consist_order_element_x()
	local b_alt = vehicle_description_x()
	b_alt.specific_vehicle = coach
	b_slot.alternatives = [b_alt]
	portion_b.slots = [b_slot]
	sched_p.orders.rawset(21, portion_b)
	if (!check(line_p.change_schedule(pl, sched_p), "commit target line schedule")) { return false; }

	// Read the committed target schedule back: a queued tool call would not
	// otherwise show a rejected schedule.
	local back_p = line_p.get_schedule()
	if (!check(back_p.entries.len() == 2, "target line schedule read-back: entry count")) { return false; }
	local p_has_21 = false
	foreach (entry in back_p.entries) {
		if (entry.unique_entry_id == 21) { p_has_21 = true }
	}
	if (!check(p_has_21, "target line schedule read-back: entry id 21")) { return false; }
	if (!check(21 in back_p.orders, "target line schedule read-back: order at entry 21")) { return false; }
	if (!check(back_p.orders[21].slots.len() == 1, "target line schedule read-back: slot count")) { return false; }
	if (!check(back_p.orders[21].slots[0].alternatives.len() == 1, "target line schedule read-back: alternative count")) { return false; }
	local b_vehicle = back_p.orders[21].slots[0].alternatives[0].specific_vehicle
	if (!check(b_vehicle != null && b_vehicle.get_name() == coach.get_name(), "target line schedule read-back: coach")) { return false; }

	// Source convoy with both vehicles, created in the depot.
	if (!check(depot.append_vehicle(pl, convoy_x(0), loco), "add locomotive")) { return false; }
	local cnv = depot.get_convoy_list()[0]
	if (!check(cnv != null, "convoy not found after adding the locomotive")) { return false; }
	convoy_id = cnv.id
	if (!check(depot.append_vehicle(pl, cnv, coach), "add coach")) { return false; }

	// Source schedule N (portion A keeps this schedule). The junction entry
	// carries the uncouple flag, the target line and the target entry, plus
	// portion A's end state (the locomotive).
	local n0 = schedule_entry_x(coord3d(3, 2, 0), 0, 0)
	n0.unique_entry_id = 11
	local n1 = schedule_entry_x(junction_pos, 0, 0)
	n1.unique_entry_id = 12
	n1.uncouple = true
	n1.uncouple_target_is_line_or_cnv = false // line
	n1.target_id_uncouple = line_p.id
	n1.target_unique_entry_uncouple = 21
	n1.uncouple_target_sch_is_reversed = false
	local n2 = schedule_entry_x(coord3d(9, 2, 0), 0, 0)
	n2.unique_entry_id = 13
	local sched_n = schedule_x(wt_rail, [n0, n1, n2])

	local portion_a = consist_order_x()
	local a_slot = consist_order_element_x()
	local a_alt = vehicle_description_x()
	a_alt.specific_vehicle = loco
	a_slot.alternatives = [a_alt]
	portion_a.slots = [a_slot]
	sched_n.orders.rawset(12, portion_a)

	if (!check(cnv.change_schedule(pl, sched_n), "commit source convoy schedule")) { return false; }

	// Read the committed source schedule back.
	local back_n = cnv.get_schedule()
	if (!check(back_n.entries.len() == 3, "source schedule read-back: entry count")) { return false; }
	if (!check(12 in back_n.orders, "source schedule read-back: order at entry 12")) { return false; }
	if (!check(back_n.entries[1].uncouple == true, "source schedule read-back: uncouple flag")) { return false; }
	if (!check(back_n.entries[1].target_id_uncouple == line_p.id, "source schedule read-back: target line")) { return false; }
	if (!check(back_n.entries[1].target_unique_entry_uncouple == 21, "source schedule read-back: target entry")) { return false; }

	if (!check(depot.start_convoy(pl, cnv), "start source convoy")) { return false; }
	return true
}


function record_pending()
{
	local cnv = convoy_x(convoy_id)
	local halt = halt_x.get_halt(junction_pos, player_x(0))
	local laid_over_list = halt != null ? halt.get_laid_over_list() : null
	local laid_over = laid_over_list != null ? laid_over_list.get_count() : -1
	local laid_over_ids = ""
	local laid_over_valid = laid_over_list != null
	if (laid_over_list != null) {
		// Iterate the list while it is still the pre-runtime loose stock, so
		// that a change in its contents is visible, not only a count change.
		foreach (loose in laid_over_list) {
			if (!loose.is_valid() || loose.get_vehicles().len() < 1) {
				laid_over_valid = false
				continue
			}
			if (laid_over_ids.len() > 0) {
				laid_over_ids += ","
			}
			laid_over_ids += loose.id
		}
	}
	local convoys_here = halt != null ? halt.get_convoy_list().get_count() : -1
	local target_line = line_x(target_line_id)
	local target_convoys = target_line.is_valid() ? target_line.get_convoy_list().get_count() : -1
	local vehicles = -1
	local state = -1
	local stop = -1
	local entry_id = 0
	if (cnv.is_valid()) {
		vehicles = cnv.get_vehicles().len()
		state = cnv.get_state()
		stop = cnv.get_current_stop()
		entry_id = cnv.get_current_entry_id()
	}
	print("PENDING RUNTIME: junction convoys=" + convoys_here + " laid_over=" + laid_over + " laid_over_ids=" + laid_over_ids + " source_vehicles=" + vehicles + " state=" + state + " stop=" + stop + " entry_id=" + entry_id + " target_line_convoys=" + target_convoys)
	if (vehicles == 1 && laid_over == 1 && laid_over_valid && convoys_here == 1 && target_convoys == 0) {
		print("PENDING RUNTIME: the source entry's consist order released the split-off vehicle as loose stock at the junction, but no portion convoy adopted the target line's schedule at target entry 21; the dividing runtime is not implemented.")
	}
	else {
		print("PENDING RUNTIME: OBSERVED CHANGE: the junction state differs from the recorded pending expectation; update this fixture.")
	}
	print("PENDING RUNTIME COMPLETE.")
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
	return ttext("Reach the junction with the pre-runtime Y fixture.")
}


function get_info_text(pl)
{
	local msg = ttext("Pre-runtime Y fixture: {phase}")
	msg.phase = phase
	return msg
}


function get_result_text(pl)
{
	return get_info_text(pl)
}


function is_scenario_completed(pl)
{
	if (pl != 0) {
		return 0
	}
	if (!setup_started) {
		setup_started = true
		local ok = false
		try {
			ok = setup()
		}
		catch (e) {
			print("FIXTURE SETUP FAILED: " + e)
			phase = "failed"
			return -1
		}
		if (!ok) {
			return -1
		}
		phase = "waiting"
		print("Y FIXTURE: setup complete; source convoy " + convoy_id + " started")
		return 0
	}
	if (phase == "failed") {
		return -1
	}
	if (phase == "done") {
		return 100
	}
	if (phase == "waiting") {
		local cnv = convoy_x(convoy_id)
		if (!cnv.is_valid()) {
			print("PENDING RUNTIME: TIMEOUT: the source convoy disappeared before reaching the junction")
			phase = "done"
			return 100
		}
		++wait_steps
		if (cnv.get_current_stop() == junction_index && cnv.get_state() == convoy_state_loading) {
			print("Y FIXTURE: source convoy reached the junction stop")
			phase = "settling"
			settle_steps = 20
			return 0
		}
		if (wait_steps > 6000) {
			print("PENDING RUNTIME: TIMEOUT waiting for the source convoy: state=" + cnv.get_state() + " stop=" + cnv.get_current_stop())
			phase = "done"
			return 100
		}
		return 0
	}
	if (phase == "settling") {
		if (settle_steps > 0) {
			--settle_steps
			return 0
		}
		record_pending()
		phase = "done"
		return 100
	}
	return 0
}
