//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//


//
// Tests for lossless schedule and consist-order conversion through the script
// API, and for the read-only runtime observation needed by the dividing
// (Y/H) work.
//


function make_test_road_line(pl)
{
	local road_desc = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	ASSERT_TRUE(road_desc != null)
	ASSERT_EQUAL(command_x.build_way(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), road_desc, true), null)
	ASSERT_TRUE(pl.create_line(wt_road))

	local line = null
	foreach (l in pl.get_line_list()) {
		if (l.get_waytype() == wt_road) {
			line = l
			break
		}
	}
	ASSERT_TRUE(line != null)
	return line
}


// A consist order with one wildcard slot holding one specific vehicle.
function make_test_order(waytype, recipient)
{
	local vehicle = vehicle_desc_x.get_available_vehicles(waytype)[0]
	ASSERT_TRUE(vehicle != null)
	local slot = consist_order_element_x()
	slot.catg = consist_order_element_x.any_catg
	local alt = vehicle_description_x()
	alt.specific_vehicle = vehicle
	slot.alternatives = [alt]
	local order = consist_order_x()
	order.available_to_id = recipient
	order.available_to_flags = 1
	order.slots = [slot]
	return order
}


function make_test_rich_schedule()
{
	local e1 = schedule_entry_x(coord3d(12, 12, 0), 30, -5)
	e1.spacing_shift = -7
	e1.unique_entry_id = 11
	e1.lay_over = true
	e1.ignore_choose = true
	e1.force_range_stop = false
	e1.discharge_payload = true
	e1.set_down_only = false
	e1.pick_up_only = true
	e1.conditional_depart_before_wait = true
	e1.conditional_depart_after_wait = false
	e1.conditional_skip = true
	e1.send_trigger = false
	e1.cond_trigger_is_line_or_cnv = true
	e1.clear_stored_triggers_on_dep = false
	e1.trigger_one_only = true
	e1.couple = false
	e1.uncouple = true
	e1.couple_target_is_line_or_cnv = false
	e1.uncouple_target_is_line_or_cnv = true
	e1.uncouple_target_sch_is_reversed = true
	e1.couple_target_sch_is_reversed = false
	e1.condition_bitfield_broadcaster = 0x8001
	e1.condition_bitfield_receiver = 0x0003
	e1.target_id_condition_trigger = 4
	e1.target_id_couple = 5
	e1.target_id_uncouple = 6
	e1.target_unique_entry_uncouple = 12
	e1.target_unique_entry_couple = 13
	e1.reverse = 1
	e1.max_speed_kmh = 123

	local e2 = schedule_entry_x(coord3d(12, 13, 0), 100, 0)
	e2.spacing_shift = 32767
	e2.unique_entry_id = 12
	e2.reverse = -1
	e2.max_speed_kmh = 65535

	local e3 = schedule_entry_x(coord3d(12, 14, 0), 0, 127)
	e3.spacing_shift = -32768
	e3.unique_entry_id = 13
	e3.reverse = 0
	e3.max_speed_kmh = 1

	local sched = schedule_x(wt_road, [e1, e2, e3])
	sched.bidirectional = true
	sched.mirrored = true
	sched.same_spacing_shift = false
	sched.spacing = 36
	sched.current_stop = 1

	// Portion A end state, keyed by entry 11: two slots, the first a specific
	// vehicle and the second a rule-based alternative.
	local vehicle = vehicle_desc_x.get_available_vehicles(wt_road)[0]
	ASSERT_TRUE(vehicle != null)

	local slot0 = consist_order_element_x()
	slot0.catg = 255 // any category: preserves the wildcard explicitly
	local alt0 = vehicle_description_x()
	alt0.specific_vehicle = vehicle
	slot0.alternatives = [alt0]

	local slot1 = consist_order_element_x() // catg -1: derived from the alternatives (none -> category 2)
	local alt1 = vehicle_description_x()
	alt1.specific_vehicle = null
	alt1.empty = false
	alt1.engine_type = 1
	alt1.min_catering = 1
	alt1.max_catering = 3
	alt1.must_carry_class = 2
	alt1.min_range = 100
	alt1.max_range = 5000
	alt1.min_brake_force = 5
	alt1.max_brake_force = 20
	alt1.min_power = 1000
	alt1.max_power = 900000
	alt1.min_tractive_effort = 10
	alt1.max_tractive_effort = 200000
	alt1.min_topspeed = 50
	alt1.max_topspeed = 200
	alt1.min_weight = 1000
	alt1.max_weight = 90000
	alt1.min_axle_load = 10
	alt1.max_axle_load = 30
	alt1.min_capacity = 10
	alt1.max_capacity = 500
	alt1.min_running_cost = 5
	alt1.max_running_cost = 1000
	alt1.min_fixed_cost = 100
	alt1.max_fixed_cost = 90000
	alt1.min_fuel_per_km = 1
	alt1.max_fuel_per_km = 500
	alt1.min_staff_hundredths = 0
	alt1.max_staff_hundredths = 300
	alt1.min_drivers = 0
	alt1.max_drivers = 3
	slot1.alternatives = [alt1]

	local portion_a = consist_order_x()
	portion_a.slots = [slot0, slot1]
	portion_a.available_to_id = 7
	portion_a.available_to_flags = 1 // convoy

	sched.orders[11] <- portion_a
	return sched
}


function assert_rich_entry(e)
{
	ASSERT_EQUAL(e.minimum_loading, 30)
	ASSERT_EQUAL(e.load, 30)
	ASSERT_EQUAL(e.waiting_time_shift, -5)
	ASSERT_EQUAL(e.wait, -5)
	ASSERT_EQUAL(e.spacing_shift, -7)
	ASSERT_EQUAL(e.unique_entry_id, 11)
	ASSERT_EQUAL(e.wait_for_time, false)
	ASSERT_EQUAL(e.lay_over, true)
	ASSERT_EQUAL(e.ignore_choose, true)
	ASSERT_EQUAL(e.force_range_stop, false)
	ASSERT_EQUAL(e.discharge_payload, true)
	ASSERT_EQUAL(e.set_down_only, false)
	ASSERT_EQUAL(e.pick_up_only, true)
	ASSERT_EQUAL(e.conditional_depart_before_wait, true)
	ASSERT_EQUAL(e.conditional_depart_after_wait, false)
	ASSERT_EQUAL(e.conditional_skip, true)
	ASSERT_EQUAL(e.send_trigger, false)
	ASSERT_EQUAL(e.cond_trigger_is_line_or_cnv, true)
	ASSERT_EQUAL(e.clear_stored_triggers_on_dep, false)
	ASSERT_EQUAL(e.trigger_one_only, true)
	ASSERT_EQUAL(e.couple, false)
	ASSERT_EQUAL(e.uncouple, true)
	ASSERT_EQUAL(e.couple_target_is_line_or_cnv, false)
	ASSERT_EQUAL(e.uncouple_target_is_line_or_cnv, true)
	ASSERT_EQUAL(e.uncouple_target_sch_is_reversed, true)
	ASSERT_EQUAL(e.couple_target_sch_is_reversed, false)
	ASSERT_EQUAL(e.condition_bitfield_broadcaster, 0x8001)
	ASSERT_EQUAL(e.condition_bitfield_receiver, 0x0003)
	ASSERT_EQUAL(e.target_id_condition_trigger, 4)
	ASSERT_EQUAL(e.target_id_couple, 5)
	ASSERT_EQUAL(e.target_id_uncouple, 6)
	ASSERT_EQUAL(e.target_unique_entry_uncouple, 12)
	ASSERT_EQUAL(e.target_unique_entry_couple, 13)
	ASSERT_EQUAL(e.reverse, 1)
	ASSERT_EQUAL(e.max_speed_kmh, 123)
}


function test_schedule_round_trip_rich_entry()
{
	local pl = player_x(0)
	local line = make_test_road_line(pl)
	local sched = make_test_rich_schedule()

	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	local back = line.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 3)
	assert_rich_entry(back.entries[0])

	ASSERT_EQUAL(back.entries[1].minimum_loading, 100)
	ASSERT_EQUAL(back.entries[1].waiting_time_shift, 0)
	ASSERT_EQUAL(back.entries[1].spacing_shift, 32767)
	ASSERT_EQUAL(back.entries[1].unique_entry_id, 12)
	ASSERT_EQUAL(back.entries[1].reverse, -1)
	ASSERT_EQUAL(back.entries[1].max_speed_kmh, 65535)

	ASSERT_EQUAL(back.entries[2].minimum_loading, 0)
	ASSERT_EQUAL(back.entries[2].waiting_time_shift, 127)
	ASSERT_EQUAL(back.entries[2].spacing_shift, -32768)
	ASSERT_EQUAL(back.entries[2].unique_entry_id, 13)
	ASSERT_EQUAL(back.entries[2].reverse, 0)
	ASSERT_EQUAL(back.entries[2].max_speed_kmh, 1)

	ASSERT_EQUAL(back.bidirectional, true)
	ASSERT_EQUAL(back.mirrored, true)
	ASSERT_EQUAL(back.same_spacing_shift, false)
	ASSERT_EQUAL(back.spacing, 36)
	ASSERT_EQUAL(back.current_stop, 1)

	// Consist order for entry 11
	local order = back.orders[11]
	ASSERT_TRUE(order != null)
	ASSERT_EQUAL(order.available_to_id, 7)
	ASSERT_EQUAL(order.available_to_flags, 1)
	ASSERT_EQUAL(order.slots.len(), 2)

	local slot0 = order.slots[0]
	ASSERT_EQUAL(slot0.catg, 255)
	ASSERT_EQUAL(slot0.alternatives.len(), 1)
	ASSERT_TRUE(slot0.alternatives[0].specific_vehicle != null)
	ASSERT_EQUAL(slot0.alternatives[0].specific_vehicle.get_name(), sched.orders[11].slots[0].alternatives[0].specific_vehicle.get_name())

	local slot1 = order.slots[1]
	ASSERT_EQUAL(slot1.catg, 2) // INDEX_NONE, the C++ default for a rule-only slot
	ASSERT_EQUAL(slot1.alternatives.len(), 1)
	local alt = slot1.alternatives[0]
	ASSERT_TRUE(alt.specific_vehicle == null)
	ASSERT_EQUAL(alt.empty, false)
	ASSERT_EQUAL(alt.engine_type, 1)
	ASSERT_EQUAL(alt.min_catering, 1)
	ASSERT_EQUAL(alt.max_catering, 3)
	ASSERT_EQUAL(alt.must_carry_class, 2)
	ASSERT_EQUAL(alt.min_range, 100)
	ASSERT_EQUAL(alt.max_range, 5000)
	ASSERT_EQUAL(alt.min_brake_force, 5)
	ASSERT_EQUAL(alt.max_brake_force, 20)
	ASSERT_EQUAL(alt.min_power, 1000)
	ASSERT_EQUAL(alt.max_power, 900000)
	ASSERT_EQUAL(alt.min_tractive_effort, 10)
	ASSERT_EQUAL(alt.max_tractive_effort, 200000)
	ASSERT_EQUAL(alt.min_topspeed, 50)
	ASSERT_EQUAL(alt.max_topspeed, 200)
	ASSERT_EQUAL(alt.min_weight, 1000)
	ASSERT_EQUAL(alt.max_weight, 90000)
	ASSERT_EQUAL(alt.min_axle_load, 10)
	ASSERT_EQUAL(alt.max_axle_load, 30)
	ASSERT_EQUAL(alt.min_capacity, 10)
	ASSERT_EQUAL(alt.max_capacity, 500)
	ASSERT_EQUAL(alt.min_running_cost, 5)
	ASSERT_EQUAL(alt.max_running_cost, 1000)
	ASSERT_EQUAL(alt.min_fixed_cost, 100)
	ASSERT_EQUAL(alt.max_fixed_cost, 90000)
	ASSERT_EQUAL(alt.min_fuel_per_km, 1)
	ASSERT_EQUAL(alt.max_fuel_per_km, 500)
	ASSERT_EQUAL(alt.min_staff_hundredths, 0)
	ASSERT_EQUAL(alt.max_staff_hundredths, 300)
	ASSERT_EQUAL(alt.min_drivers, 0)
	ASSERT_EQUAL(alt.max_drivers, 3)

	// A deliberate modification and a second commit must be lossless too.
	local modified = line.get_schedule()
	modified.entries[0].max_speed_kmh = 9
	modified.entries[0].target_unique_entry_couple = 21
	ASSERT_EQUAL(line.change_schedule(pl, modified), true)
	local again = line.get_schedule()
	ASSERT_EQUAL(again.entries[0].max_speed_kmh, 9)
	ASSERT_EQUAL(again.entries[0].target_unique_entry_couple, 21)
	ASSERT_EQUAL(again.entries[0].minimum_loading, 30)
	ASSERT_EQUAL(again.entries[0].waiting_time_shift, -5)
	ASSERT_EQUAL(again.entries[0].spacing_shift, -7)
	ASSERT_EQUAL(again.entries[0].lay_over, true)
	ASSERT_EQUAL(again.entries[0].uncouple, true)
	ASSERT_EQUAL(again.orders[11].slots.len(), 2)

	// clean up
	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_schedule_standard_entry_constructor()
{
	local e = schedule_entry_x(coord3d(3, 4, 5), 55, -6)
	ASSERT_EQUAL(e.x, 3)
	ASSERT_EQUAL(e.y, 4)
	ASSERT_EQUAL(e.z, 5)
	ASSERT_EQUAL(e.load, 55)
	ASSERT_EQUAL(e.wait, -6)
	// The Extended names are aliases of the Standard names, not separate slots.
	ASSERT_EQUAL(e.minimum_loading, 55)
	ASSERT_EQUAL(e.waiting_time_shift, -6)
	ASSERT_EQUAL(e.unique_entry_id, 0)
	ASSERT_EQUAL(e.reverse, -1)
	ASSERT_EQUAL(e.max_speed_kmh, 65535)
}


function test_schedule_rejects_invalid_input()
{
	local pl = player_x(0)
	local line = make_test_road_line(pl)

	local sched = schedule_x(wt_road, [
		schedule_entry_x(coord3d(12, 12, 0), 0, 0),
		schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	])
	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	// duplicate ids
	{
		local bad = schedule_x(wt_road, [
			schedule_entry_x(coord3d(12, 12, 0), 0, 0),
			schedule_entry_x(coord3d(12, 13, 0), 0, 0)
		])
		bad.entries[0].unique_entry_id = 5
		bad.entries[1].unique_entry_id = 5
		local caught = false
		try { line.change_schedule(pl, bad) } catch (e) { caught = true }
		ASSERT_TRUE(caught)
		ASSERT_EQUAL(line.get_schedule().entries.len(), 2)
	}

	// order key that names no entry
	{
		local bad = schedule_x(wt_road, [
			schedule_entry_x(coord3d(12, 12, 0), 0, 0),
			schedule_entry_x(coord3d(12, 13, 0), 0, 0)
		])
		bad.entries[0].unique_entry_id = 7
		bad.entries[1].unique_entry_id = 8
		local order = consist_order_x()
		local slot = consist_order_element_x()
		slot.catg = 0
		order.slots = [slot]
		bad.orders[9] <- order
		local caught = false
		try { line.change_schedule(pl, bad) } catch (e) { caught = true }
		ASSERT_TRUE(caught)
	}

	// wait_for_time together with a non-zero load
	{
		local bad = schedule_x(wt_road, [
			schedule_entry_x(coord3d(12, 12, 0), 50, 0),
			schedule_entry_x(coord3d(12, 13, 0), 0, 0)
		])
		bad.entries[0].wait_for_time = true
		local caught = false
		try { line.change_schedule(pl, bad) } catch (e) { caught = true }
		ASSERT_TRUE(caught)
	}

	// a slot that contradicts its specific vehicle's category
	{
		local bad = schedule_x(wt_road, [
			schedule_entry_x(coord3d(12, 12, 0), 0, 0),
			schedule_entry_x(coord3d(12, 13, 0), 0, 0)
		])
		bad.entries[0].unique_entry_id = 7
		bad.entries[1].unique_entry_id = 8
		local vehicle = null
		foreach (v in vehicle_desc_x.get_available_vehicles(wt_road)) {
			if (v.get_freight() != null) {
				vehicle = v
				break
			}
		}
		ASSERT_TRUE(vehicle != null)
		local vehicle_catg = vehicle.get_freight().get_catg_index()
		local wrong_catg = vehicle_catg == 0 ? 1 : 0
		local slot = consist_order_element_x()
		local alt = vehicle_description_x()
		alt.specific_vehicle = vehicle
		slot.alternatives = [alt]
		slot.catg = wrong_catg
		local order = consist_order_x()
		order.slots = [slot]
		bad.orders[7] <- order
		local caught = false
		try { line.change_schedule(pl, bad) } catch (e) { caught = true }
		ASSERT_TRUE(caught)
	}

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


// Builds a road on row 13 from x = 30 to x = 36 and creates a road line, so
// that schedule entries can be placed at x = 33. The row is clear of the rows
// used by the other schedule tests, and the fixture is wider than 36 tiles.
function make_wide_test_road_line(pl)
{
	local road_desc = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	ASSERT_TRUE(road_desc != null)
	ASSERT_EQUAL(command_x.build_way(pl, coord3d(30, 13, 0), coord3d(36, 13, 0), road_desc, true), null)

	local existing = {}
	foreach (l in pl.get_line_list()) {
		existing[l.id] <- true
	}

	ASSERT_TRUE(pl.create_line(wt_road))

	local line = null
	foreach (l in pl.get_line_list()) {
		if (!(l.id in existing)) {
			line = l
			break
		}
	}
	ASSERT_TRUE(line != null)
	return line
}


// The schedule string marks the end of its entries with the text "33", so an
// entry whose x coordinate is 33 must survive the string round trip, together
// with every entry after it and the consist orders. Each test goes through
// sprintf_schedule (the writer), the line change tool and sscanf_schedule (the
// reader).

function test_schedule_string_x33_first_without_orders()
{
	local pl = player_x(0)
	local line = make_wide_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(33, 13, 0), 0, 0)
	e0.unique_entry_id = 11
	local e1 = schedule_entry_x(coord3d(34, 13, 0), 0, 0)
	e1.unique_entry_id = 12
	local sched = schedule_x(wt_road, [e0, e1])

	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	local back = line.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 2)
	ASSERT_EQUAL(back.entries[0].x, 33)
	ASSERT_EQUAL(back.entries[0].unique_entry_id, 11)
	ASSERT_EQUAL(back.entries[1].x, 34)
	ASSERT_EQUAL(back.entries[1].unique_entry_id, 12)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(30, 13, 0), coord3d(36, 13, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_schedule_string_x33_first_with_order()
{
	local pl = player_x(0)
	local line = make_wide_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(33, 13, 0), 0, 0)
	e0.unique_entry_id = 11
	local e1 = schedule_entry_x(coord3d(34, 13, 0), 0, 0)
	e1.unique_entry_id = 12
	local sched = schedule_x(wt_road, [e0, e1])
	sched.orders[11] <- make_test_order(wt_road, 7)

	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	local back = line.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 2)
	ASSERT_EQUAL(back.entries[0].x, 33)
	ASSERT_EQUAL(back.entries[1].x, 34)
	ASSERT_TRUE(11 in back.orders)
	ASSERT_EQUAL(back.orders[11].available_to_id, 7)
	ASSERT_EQUAL(back.orders.len(), 1)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(30, 13, 0), coord3d(36, 13, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_schedule_string_x33_middle_multi_digit_order()
{
	local pl = player_x(0)
	local line = make_wide_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(31, 13, 0), 0, 0)
	e0.unique_entry_id = 11
	local e1 = schedule_entry_x(coord3d(33, 13, 0), 0, 0)
	e1.unique_entry_id = 65534
	local e2 = schedule_entry_x(coord3d(34, 13, 0), 0, 0)
	e2.unique_entry_id = 12
	local sched = schedule_x(wt_road, [e0, e1, e2])
	sched.orders[65534] <- make_test_order(wt_road, 9)

	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	local back = line.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 3)
	ASSERT_EQUAL(back.entries[0].x, 31)
	ASSERT_EQUAL(back.entries[1].x, 33)
	ASSERT_EQUAL(back.entries[1].unique_entry_id, 65534)
	ASSERT_EQUAL(back.entries[2].x, 34)
	ASSERT_TRUE(65534 in back.orders)
	ASSERT_EQUAL(back.orders[65534].available_to_id, 9)
	ASSERT_EQUAL(back.orders.len(), 1)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(30, 13, 0), coord3d(36, 13, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_schedule_string_x33_last_without_orders()
{
	local pl = player_x(0)
	local line = make_wide_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(31, 13, 0), 0, 0)
	e0.unique_entry_id = 11
	local e1 = schedule_entry_x(coord3d(32, 13, 0), 0, 0)
	e1.unique_entry_id = 12
	local e2 = schedule_entry_x(coord3d(33, 13, 0), 0, 0)
	e2.unique_entry_id = 13
	local sched = schedule_x(wt_road, [e0, e1, e2])

	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	local back = line.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 3)
	ASSERT_EQUAL(back.entries[0].x, 31)
	ASSERT_EQUAL(back.entries[1].x, 32)
	ASSERT_EQUAL(back.entries[2].x, 33)
	ASSERT_EQUAL(back.entries[2].unique_entry_id, 13)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(30, 13, 0), coord3d(36, 13, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_convoy_vehicle_snapshot()
{
	local pl = player_x(0)
	RESET_ALL_PLAYER_FUNDS()
	local rail_desc = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	ASSERT_EQUAL(command_x.build_way(pl, coord3d(13, 1, 0), coord3d(13, 3, 0), rail_desc, true), null)
	ASSERT_EQUAL(command_x.build_depot(pl, coord3d(13, 1, 0), get_depot_by_wt(wt_rail)), null)

	local depot = depot_x(13, 1, 0)
	ASSERT_TRUE(depot != null)

	local list = vehicle_desc_x.get_available_vehicles(wt_rail)
	local loco = null
	local coach = null
	foreach (a in list) {
		if (!a.can_be_first()) { continue }
		foreach (b in list) {
			if (vehicle_desc_x.is_coupling_allowed(a, b) && b.can_be_last()) {
				loco = a
				coach = b
				break
			}
		}
		if (loco != null) { break }
	}
	ASSERT_TRUE(loco != null)
	ASSERT_TRUE(coach != null)

	ASSERT_TRUE(depot.append_vehicle(pl, convoy_x(0), loco))
	local cnv = depot.get_convoy_list()[0]
	ASSERT_TRUE(cnv != null)
	ASSERT_TRUE(depot.append_vehicle(pl, cnv, coach))

	ASSERT_EQUAL(depot.get_convoy_list().len(), 1)
	ASSERT_EQUAL(cnv.get_vehicles().len(), 2)

	local snapshot = cnv.get_vehicle_snapshot()
	ASSERT_EQUAL(snapshot.len(), 2)
	ASSERT_EQUAL(snapshot[0].index, 0)
	ASSERT_EQUAL(snapshot[0].desc.get_name(), loco.get_name())
	ASSERT_EQUAL(snapshot[1].index, 1)
	ASSERT_EQUAL(snapshot[1].desc.get_name(), coach.get_name())
	ASSERT_EQUAL(snapshot[0].owner.nr, pl.nr)
	ASSERT_EQUAL(snapshot[0].cargo, 0)
	ASSERT_EQUAL(snapshot[1].cargo, 0)
	ASSERT_TRUE(snapshot[0].capacity >= 0)
	ASSERT_TRUE(snapshot[1].capacity >= 0)
	// the snapshot reports the total capacity of the descriptor
	ASSERT_TRUE(snapshot[1].capacity > 0)

	// A depot convoy has no schedule position and is in the initial state.
	ASSERT_EQUAL(cnv.get_current_stop(), -1)
	ASSERT_EQUAL(cnv.get_current_entry_id(), 0)
	ASSERT_EQUAL(cnv.get_state(), convoy_state_initial)

	// Destroying a convoy that is still in its depot (state
	// convoi_t::INITIAL) runs convoi_t::destroy(), which unreserves the route
	// through the route-unreserve worker threads. This world has only a few
	// ways, fewer than the per-worker slice size, which used to underflow the
	// worker's range end and make the scan run past the end of the way list.
	ASSERT_EQUAL(cnv.destroy(pl), true)
	ASSERT_EQUAL(depot.get_convoy_list().len(), 0)

	// The convoy is gone: remove the now-empty depot and the track, then check
	// that neither vehicles nor ways are left charging maintenance.
	ASSERT_EQUAL(command_x(tool_remover).work(pl, coord3d(13, 1, 0)), null)
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(13, 1, 0), coord3d(13, 3, 0), "" + wt_rail), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_convoy_state_enum_values()
{
	// get_state() must have one stable constant per convoi_t::states value.
	local states = [
		convoy_state_initial,
		convoy_state_edit_schedule,
		convoy_state_routing_1,
		convoy_state_routing_2,
		convoy_state_dummy5,
		convoy_state_no_route,
		convoy_state_driving,
		convoy_state_loading,
		convoy_state_waiting_for_clearance,
		convoy_state_waiting_for_clearance_one_month,
		convoy_state_can_start,
		convoy_state_can_start_one_month,
		convoy_state_self_destruct,
		convoy_state_waiting_for_clearance_two_months,
		convoy_state_can_start_two_months,
		convoy_state_leaving_depot,
		convoy_state_entering_depot,
		convoy_state_reversing,
		convoy_state_out_of_range,
		convoy_state_emergency_stop,
		convoy_state_route_just_found,
		convoy_state_no_route_too_complex,
		convoy_state_waiting_for_loading_three_months,
		convoy_state_waiting_for_loading_four_months,
		convoy_state_replenishing,
		convoy_state_maintenance,
		convoy_state_overhaul,
		convoy_state_awaiting_trigger,
		convoy_state_layover,
		convoy_state_shunting,
		convoy_state_awaiting_recombination
	]
	ASSERT_EQUAL(states.len(), 31)
	local seen = {}
	foreach (state in states) {
		ASSERT_TRUE(state >= 0)
		ASSERT_TRUE(state <= 30)
		ASSERT_FALSE(state in seen)
		seen[state] <- true
	}
	ASSERT_TRUE(convoy_state_loading != convoy_state_driving)
	ASSERT_TRUE(convoy_state_layover != convoy_state_shunting)
	ASSERT_TRUE(convoy_state_shunting != convoy_state_awaiting_recombination)
	ASSERT_TRUE(convoy_state_loading != convoy_state_layover)
}


function test_halt_laid_over_list()
{
	local pl = player_x(0)
	local road_desc = way_desc_x.get_available_ways(wt_road, st_flat)[0]
	local station_desc = building_desc_x.get_available_stations(building_desc_x.station, wt_road, good_desc_x.passenger)[0]
	ASSERT_TRUE(station_desc != null)
	ASSERT_EQUAL(command_x.build_way(pl, coord3d(14, 10, 0), coord3d(14, 11, 0), road_desc, true), null)
	ASSERT_EQUAL(command_x(tool_build_station).work(pl, coord3d(14, 10, 0), station_desc.get_name()), null)

	local halt = halt_x.get_halt(coord3d(14, 10, 0), pl)
	ASSERT_TRUE(halt != null)
	local list = halt.get_laid_over_list()
	ASSERT_EQUAL(list.get_count(), 0)
	local seen = 0
	foreach (cnv in list) {
		ASSERT_TRUE(cnv != null)
		++seen
	}
	ASSERT_EQUAL(seen, 0)

	ASSERT_EQUAL(command_x(tool_remover).work(player_x(1), coord3d(14, 10, 0)), null)
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(14, 10, 0), coord3d(14, 11, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


function test_schedule_y_style_pair()
{
	local pl = player_x(0)
	local line_n = make_test_road_line(pl)

	// second line on the same road: portion B's target
	local before = []
	foreach (l in pl.get_line_list()) {
		before.append(l.id)
	}
	ASSERT_TRUE(pl.create_line(wt_road))
	local line_p = null
	foreach (l in pl.get_line_list()) {
		if (before.find(l.id) == null) {
			line_p = l
			break
		}
	}
	ASSERT_TRUE(line_p != null)

	// Target schedule: entry 21 is the handover point, entry 22 the branch.
	local p0 = schedule_entry_x(coord3d(12, 12, 0), 0, 0)
	p0.unique_entry_id = 21
	local p1 = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	p1.unique_entry_id = 22
	local sched_p = schedule_x(wt_road, [p0, p1])

	// Portion B end state: one optional rule-based slot.
	local b_slot = consist_order_element_x()
	b_slot.catg = 255
	local b_alt = vehicle_description_x()
	b_alt.empty = false
	b_alt.engine_type = 1
	b_alt.min_power = 100
	b_slot.alternatives = [b_alt]
	local portion_b = consist_order_x()
	portion_b.slots = [b_slot]
	sched_p.orders[21] <- portion_b
	ASSERT_EQUAL(line_p.change_schedule(pl, sched_p), true)

	// Source schedule: entry 12 uncouples, naming target line P and entry 21.
	local n0 = schedule_entry_x(coord3d(12, 12, 0), 0, 0)
	n0.unique_entry_id = 11
	local n1 = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	n1.unique_entry_id = 12
	n1.uncouple = true
	n1.uncouple_target_is_line_or_cnv = false
	n1.target_id_uncouple = line_p.id
	n1.target_unique_entry_uncouple = 21
	n1.uncouple_target_sch_is_reversed = true
	local n2 = schedule_entry_x(coord3d(12, 14, 0), 0, 0)
	n2.unique_entry_id = 13
	local sched_n = schedule_x(wt_road, [n0, n1, n2])

	// Portion A end state: one specific vehicle.
	local a_slot = consist_order_element_x()
	a_slot.catg = 255
	local a_alt = vehicle_description_x()
	a_alt.specific_vehicle = vehicle_desc_x.get_available_vehicles(wt_road)[0]
	a_slot.alternatives = [a_alt]
	local portion_a = consist_order_x()
	portion_a.slots = [a_slot]
	sched_n.orders[12] <- portion_a
	ASSERT_EQUAL(line_n.change_schedule(pl, sched_n), true)

	local back_n = line_n.get_schedule()
	local back_p = line_p.get_schedule()
	ASSERT_EQUAL(back_n.entries[1].uncouple, true)
	ASSERT_EQUAL(back_n.entries[1].uncouple_target_is_line_or_cnv, false)
	ASSERT_EQUAL(back_n.entries[1].target_id_uncouple, line_p.id)
	ASSERT_EQUAL(back_n.entries[1].target_unique_entry_uncouple, 21)
	ASSERT_EQUAL(back_n.entries[1].uncouple_target_sch_is_reversed, true)
	ASSERT_EQUAL(back_n.orders[12].slots.len(), 1)

	local found = false
	foreach (e in back_p.entries) {
		if (e.unique_entry_id == 21) {
			found = true
		}
	}
	ASSERT_TRUE(found)
	ASSERT_EQUAL(back_p.orders[21].slots.len(), 1)
	ASSERT_EQUAL(back_p.orders[21].slots[0].alternatives[0].engine_type, 1)
	ASSERT_EQUAL(back_p.orders[21].slots[0].alternatives[0].min_power, 100)

	ASSERT_TRUE(line_p.destroy(pl))
	ASSERT_TRUE(line_n.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


// Regression coverage for schedule_t::sscanf_schedule(): the target of a
// schedule change is a copy of the live schedule, so adding, changing and
// deleting a consist order must replace the copy's orders rather than leaving
// stale ones behind.
function test_schedule_order_edit_delete_line()
{
	local pl = player_x(0)
	local line = make_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(12, 12, 0), 0, 0)
	e0.unique_entry_id = 11
	local e1 = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	e1.unique_entry_id = 12
	local e2 = schedule_entry_x(coord3d(12, 14, 0), 0, 0)
	e2.unique_entry_id = 13
	local sched = schedule_x(wt_road, [e0, e1, e2])
	sched.orders[11] <- make_test_order(wt_road, 7)
	ASSERT_EQUAL(line.change_schedule(pl, sched), true)

	// Add an order for an existing entry.
	local back = line.get_schedule()
	ASSERT_TRUE(11 in back.orders)
	back.orders[12] <- make_test_order(wt_road, 9)
	ASSERT_EQUAL(line.change_schedule(pl, back), true)
	back = line.get_schedule()
	ASSERT_EQUAL(back.orders.len(), 2)
	ASSERT_EQUAL(back.orders[11].available_to_id, 7)
	ASSERT_EQUAL(back.orders[12].available_to_id, 9)

	// Change an existing order.
	back.orders[11].available_to_id = 15
	back.orders[11].available_to_flags = 0
	back.orders[11].slots[0].catg = consist_order_element_x.any_catg
	ASSERT_EQUAL(line.change_schedule(pl, back), true)
	back = line.get_schedule()
	ASSERT_EQUAL(back.orders[11].available_to_id, 15)
	ASSERT_EQUAL(back.orders[11].available_to_flags, 0)
	ASSERT_EQUAL(back.orders[11].slots[0].catg, consist_order_element_x.any_catg)
	ASSERT_EQUAL(back.orders[12].available_to_id, 9)

	// Delete an existing order.
	delete back.orders[11]
	ASSERT_EQUAL(line.change_schedule(pl, back), true)
	back = line.get_schedule()
	ASSERT_FALSE(11 in back.orders)
	ASSERT_TRUE(12 in back.orders)
	ASSERT_EQUAL(back.orders.len(), 1)
	ASSERT_EQUAL(back.orders[12].available_to_id, 9)
	ASSERT_EQUAL(back.entries.len(), 3)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


// The same regression on a real convoy schedule. The convoy tool rebuilds its
// target from the convoy's live schedule, which is the case that used to keep
// a deleted or changed order.
function test_schedule_order_edit_delete_convoy()
{
	local pl = player_x(0)
	// The previous tests clean up after themselves, so zero maintenance is a
	// valid precondition and a valid postcondition here too.
	RESET_ALL_PLAYER_FUNDS()
	local rail_desc = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	ASSERT_TRUE(rail_desc != null)
	ASSERT_EQUAL(command_x.build_way(pl, coord3d(11, 12, 0), coord3d(11, 14, 0), rail_desc, true), null)
	ASSERT_EQUAL(command_x.build_depot(pl, coord3d(11, 12, 0), get_depot_by_wt(wt_rail)), null)

	local depot = depot_x(11, 12, 0)
	ASSERT_TRUE(depot != null)

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
	ASSERT_TRUE(loco != null)
	ASSERT_TRUE(coach != null)

	ASSERT_TRUE(depot.append_vehicle(pl, convoy_x(0), loco))
	local cnv = depot.get_convoy_list()[0]
	ASSERT_TRUE(cnv != null)

	// Appending must keep the convoy's current livery scheme index.
	local livery = cnv.get_livery_scheme_index()
	ASSERT_TRUE(depot.append_vehicle(pl, cnv, coach))
	ASSERT_EQUAL(cnv.get_vehicles().len(), 2)
	ASSERT_EQUAL(cnv.get_livery_scheme_index(), livery)

	local e0 = schedule_entry_x(coord3d(11, 12, 0), 0, 0)
	e0.unique_entry_id = 21
	local e1 = schedule_entry_x(coord3d(11, 13, 0), 0, 0)
	e1.unique_entry_id = 22
	local e2 = schedule_entry_x(coord3d(11, 14, 0), 0, 0)
	e2.unique_entry_id = 23
	local sched = schedule_x(wt_rail, [e0, e1, e2])
	sched.current_stop = 1
	sched.orders[21] <- make_test_order(wt_rail, 7)
	ASSERT_EQUAL(cnv.change_schedule(pl, sched), true)

	local back = cnv.get_schedule()
	ASSERT_TRUE(back != null)
	ASSERT_EQUAL(back.entries.len(), 3)
	ASSERT_TRUE(21 in back.orders)
	ASSERT_EQUAL(back.orders[21].available_to_id, 7)
	ASSERT_EQUAL(cnv.get_current_stop(), 1)
	ASSERT_EQUAL(cnv.get_current_entry_id(), 22)

	// Add.
	back.orders[22] <- make_test_order(wt_rail, 9)
	ASSERT_EQUAL(cnv.change_schedule(pl, back), true)
	back = cnv.get_schedule()
	ASSERT_EQUAL(back.orders.len(), 2)
	ASSERT_EQUAL(back.orders[21].available_to_id, 7)
	ASSERT_EQUAL(back.orders[22].available_to_id, 9)
	ASSERT_EQUAL(cnv.get_current_stop(), 1)

	// Change.
	back.orders[21].available_to_id = 15
	back.orders[21].slots[0].catg = consist_order_element_x.any_catg
	ASSERT_EQUAL(cnv.change_schedule(pl, back), true)
	back = cnv.get_schedule()
	ASSERT_EQUAL(back.orders[21].available_to_id, 15)
	ASSERT_EQUAL(back.orders[21].slots[0].catg, consist_order_element_x.any_catg)
	ASSERT_EQUAL(back.orders[22].available_to_id, 9)
	ASSERT_EQUAL(cnv.get_current_stop(), 1)

	// Delete.
	delete back.orders[21]
	ASSERT_EQUAL(cnv.change_schedule(pl, back), true)
	back = cnv.get_schedule()
	ASSERT_FALSE(21 in back.orders)
	ASSERT_TRUE(22 in back.orders)
	ASSERT_EQUAL(back.orders.len(), 1)
	ASSERT_EQUAL(back.entries.len(), 3)
	ASSERT_EQUAL(cnv.get_current_stop(), 1)
	ASSERT_EQUAL(cnv.get_livery_scheme_index(), livery)

	// The convoy is in its depot and has never departed.
	ASSERT_EQUAL(cnv.get_state(), convoy_state_initial)

	// Destroy the depot convoy, then the now-empty depot and the track. At this
	// point the world holds three rail ways, fewer than the per-worker slice
	// size, so every worker's slice is empty and the whole list is scanned. An
	// unsigned underflow in that range calculation used to make the scan run
	// past the end of the way list.
	ASSERT_EQUAL(cnv.destroy(pl), true)
	ASSERT_EQUAL(depot.get_convoy_list().len(), 0)
	ASSERT_EQUAL(command_x(tool_remover).work(pl, coord3d(11, 12, 0)), null)
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(11, 12, 0), coord3d(11, 14, 0), "" + wt_rail), null)
	RESET_ALL_PLAYER_FUNDS()
}


// The Standard names load/wait and the Extended names minimum_loading/
// waiting_time_shift are aliases of one value, and the raw flags field and the
// named flag booleans are kept synchronised. The last assignment wins; there
// is no stored precedence.
function test_schedule_alias_and_flags_edits()
{
	local e = schedule_entry_x(coord3d(3, 4, 5), 10, -2)
	ASSERT_EQUAL(e.load, 10)
	ASSERT_EQUAL(e.minimum_loading, 10)
	ASSERT_EQUAL(e.wait, -2)
	ASSERT_EQUAL(e.waiting_time_shift, -2)

	e.load = 40
	ASSERT_EQUAL(e.minimum_loading, 40)
	e.minimum_loading = 60
	ASSERT_EQUAL(e.load, 60)
	e.wait = 5
	ASSERT_EQUAL(e.waiting_time_shift, 5)
	e.waiting_time_shift = -9
	ASSERT_EQUAL(e.wait, -9)

	e.flags = (1 << 1) | (1 << 12) // lay_over | uncouple
	ASSERT_EQUAL(e.lay_over, true)
	ASSERT_EQUAL(e.uncouple, true)
	ASSERT_EQUAL(e.ignore_choose, false)
	e.lay_over = false
	ASSERT_EQUAL(e.flags, 1 << 12)
	e.uncouple = false
	e.load = 0
	e.wait_for_time = true
	ASSERT_EQUAL(e.flags, 1)

	// The conversion rejects a committed entry that combines wait_for_time with
	// a non-zero minimum loading, so clear that flag and restore the load for
	// the round trip. lay_over carries a flag through the commit.
	e.wait_for_time = false
	e.load = 60
	e.lay_over = true
	ASSERT_EQUAL(e.flags, 1 << 1)

	// Commit the entry that was edited through both naming conventions.
	local pl = player_x(0)
	local line = make_test_road_line(pl)
	e.x = 12
	e.y = 12
	e.z = 0
	e.unique_entry_id = 1
	local other = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	other.unique_entry_id = 2
	ASSERT_EQUAL(line.change_schedule(pl, schedule_x(wt_road, [e, other])), true)

	local back = line.get_schedule()
	ASSERT_EQUAL(back.entries[0].load, 60)
	ASSERT_EQUAL(back.entries[0].minimum_loading, 60)
	ASSERT_EQUAL(back.entries[0].wait, -9)
	ASSERT_EQUAL(back.entries[0].waiting_time_shift, -9)
	ASSERT_EQUAL(back.entries[0].flags, 1 << 1)
	ASSERT_EQUAL(back.entries[0].wait_for_time, false)
	ASSERT_EQUAL(back.entries[0].lay_over, true)
	ASSERT_EQUAL(back.entries[0].uncouple, false)

	// An unchanged commit stays lossless.
	ASSERT_EQUAL(line.change_schedule(pl, back), true)
	local again = line.get_schedule()
	ASSERT_EQUAL(again.entries[0].flags, 1 << 1)
	ASSERT_EQUAL(again.entries[0].load, 60)
	ASSERT_EQUAL(again.entries[0].minimum_loading, 60)
	ASSERT_EQUAL(again.entries[0].wait, -9)
	ASSERT_EQUAL(again.entries[0].waiting_time_shift, -9)

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


// A slot with no alternatives is accepted for every goods category and is
// preserved through the schedule string round trip. Such a slot declares its
// category and matches no vehicle at run time. The category and class tables
// that schedule_t::parse_orders derives must not index an empty alternatives
// list, so this change must neither abort nor discard the order. A slot that
// may be left empty is a different case: it holds an alternative whose empty
// flag is set.
function test_schedule_zero_alternative_slots()
{
	local pl = player_x(0)
	local line = make_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(12, 12, 0), 0, 0)
	e0.unique_entry_id = 7
	local e1 = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	e1.unique_entry_id = 8
	ASSERT_EQUAL(line.change_schedule(pl, schedule_x(wt_road, [e0, e1])), true)

	// A freight category is taken from a road vehicle rather than assumed.
	// vehicle_desc_t::get_freight() is never null (it also returns the
	// passenger or mail descriptor), so the category is compared explicitly.
	local passenger_catg = good_desc_x.passenger.get_catg_index()
	local mail_catg = good_desc_x.mail.get_catg_index()
	local freight_catg = -1
	foreach (v in vehicle_desc_x.get_available_vehicles(wt_road)) {
		local f = v.get_freight()
		if (f != null && f.get_catg_index() != passenger_catg && f.get_catg_index() != mail_catg) {
			freight_catg = f.get_catg_index()
			break
		}
	}
	ASSERT_TRUE(freight_catg >= 2)

	foreach (catg in [passenger_catg, mail_catg, freight_catg]) {
		local sched = line.get_schedule()
		local slot = consist_order_element_x()
		slot.catg = catg
		local order = consist_order_x()
		order.slots = [slot]
		sched.orders[7] <- order

		// The passenger and mail categories reach the derived class tables, so an
		// unguarded read of the first vehicle description would end the process
		// with an index-out-of-bounds fatal error.
		ASSERT_EQUAL(line.change_schedule(pl, sched), true)

		local after = line.get_schedule()
		ASSERT_EQUAL(after.entries.len(), 2)
		ASSERT_TRUE(7 in after.orders)
		ASSERT_EQUAL(after.orders[7].slots.len(), 1)
		ASSERT_EQUAL(after.orders[7].slots[0].catg, catg)
		ASSERT_EQUAL(after.orders[7].slots[0].alternatives.len(), 0)
	}

	// A well-formed order on the same entry is parsed after a slot with no
	// alternatives has been stored, so the guard does not disturb a later
	// derivation of the class tables.
	{
		local sched = line.get_schedule()
		local slot = consist_order_element_x()
		slot.catg = passenger_catg
		local alt = vehicle_description_x()
		alt.specific_vehicle = null
		alt.empty = false
		alt.must_carry_class = 1
		slot.alternatives = [alt]
		local order = consist_order_x()
		order.slots = [slot]
		sched.orders[7] <- order
		ASSERT_EQUAL(line.change_schedule(pl, sched), true)

		local after = line.get_schedule()
		ASSERT_TRUE(7 in after.orders)
		ASSERT_EQUAL(after.orders[7].slots[0].catg, passenger_catg)
		ASSERT_EQUAL(after.orders[7].slots[0].alternatives.len(), 1)
		ASSERT_EQUAL(after.orders[7].slots[0].alternatives[0].must_carry_class, 1)
	}

	// Removing the order re-derives the tables from the remaining orders.
	{
		local sched = line.get_schedule()
		delete sched.orders[7]
		ASSERT_EQUAL(line.change_schedule(pl, sched), true)

		local after = line.get_schedule()
		ASSERT_EQUAL(after.entries.len(), 2)
		ASSERT_FALSE(7 in after.orders)
	}

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}


// The spacing property matches the range of the schedule GUI's spacing
// control and of the schedule string parser: 0..999.
function test_schedule_spacing_range()
{
	local pl = player_x(0)
	local line = make_test_road_line(pl)

	local e0 = schedule_entry_x(coord3d(12, 12, 0), 0, 0)
	local e1 = schedule_entry_x(coord3d(12, 13, 0), 0, 0)
	ASSERT_EQUAL(line.change_schedule(pl, schedule_x(wt_road, [e0, e1])), true)
	local original = line.get_schedule().spacing

	foreach (bad_value in [-1, 1000, 12345]) {
		local bad = line.get_schedule()
		bad.spacing = bad_value
		local caught = false
		try { line.change_schedule(pl, bad) } catch (err) { caught = true }
		ASSERT_TRUE(caught)
		ASSERT_EQUAL(line.get_schedule().spacing, original)
	}

	foreach (good_value in [0, 999]) {
		local good = line.get_schedule()
		good.spacing = good_value
		ASSERT_EQUAL(line.change_schedule(pl, good), true)
		ASSERT_EQUAL(line.get_schedule().spacing, good_value)
	}

	ASSERT_TRUE(line.destroy(pl))
	ASSERT_EQUAL(command_x(tool_remove_way).work(pl, coord3d(12, 12, 0), coord3d(12, 14, 0), "" + wt_road), null)
	RESET_ALL_PLAYER_FUNDS()
}
