//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//


//
// Tests for the depot maintenance and overhaul read model exposed to scripts
//


// Builds one road vehicle in a depot and reads the maintenance fields that the
// vehicle_slot_x snapshot and the convoy methods expose. A vehicle that has
// just been built has travelled no distance, has completed no overhaul and is
// in the month in which it was built, so every due test is false. The test
// therefore checks the field wiring and the initial values, not the runtime
// maintenance behaviour: that is covered by the maintenance group of the
// pending-runtime fixture (tests/pending-runtime/maintenance-cases).
function test_maintenance_vehicle_read_model()
{
	local pl = player_x(0)
	local public_pl = player_x(1)
	local way_desc = way_desc_x.get_available_ways(wt_road, st_flat)[0]

	ASSERT_TRUE(way_desc != null)
	ASSERT_EQUAL(command_x.build_way(public_pl, coord3d(0, 0, 0), coord3d(0, 1, 0), way_desc, true), null)
	ASSERT_EQUAL(command_x.build_depot(pl, coord3d(0, 0, 0), get_depot_by_wt(wt_road)), null)

	local the_depot = depot_x(0, 0, 0)
	ASSERT_TRUE(the_depot.is_valid())
	ASSERT_EQUAL(the_depot.get_convoy_list().len(), 0)

	local vehicle_desc = vehicle_desc_x.get_available_vehicles(wt_road)[0]
	ASSERT_TRUE(vehicle_desc != null)
	ASSERT_TRUE(the_depot.append_vehicle(pl, convoy_x(0), vehicle_desc))

	ASSERT_EQUAL(the_depot.get_convoy_list().len(), 1)
	local cnv = the_depot.get_convoy_list()[0]

	// A convoy standing in a depot is in the INITIAL state, for which the
	// service wait is not defined.
	ASSERT_EQUAL(cnv.get_state(), convoy_state_initial)
	ASSERT_EQUAL(cnv.get_service_wait_ticks(), -1)

	local slots = cnv.get_vehicle_snapshot()
	ASSERT_EQUAL(slots.len(), 1)
	local slot = slots[0]

	// Distance counters and overhaul history of a newly built vehicle.
	ASSERT_EQUAL(slot.km_since_last_maintenance, 0)
	ASSERT_EQUAL(slot.km_since_last_overhaul, 0)
	ASSERT_EQUAL(slot.overhaul_count, 0)
	ASSERT_TRUE(slot.availability >= 1)
	ASSERT_TRUE(slot.availability <= 100)
	ASSERT_TRUE(slot.overhaul_cost >= 0)

	// Deferral and storage flags default to false.
	ASSERT_EQUAL(slot.do_not_overhaul, false)
	ASSERT_EQUAL(slot.do_not_auto_upgrade, false)
	ASSERT_EQUAL(slot.mothballed, false)

	// A new vehicle has covered no distance, and being in its first month
	// satisfies neither the distance nor the months maintenance interval.
	ASSERT_EQUAL(slot.maintenance_needed, false)
	ASSERT_EQUAL(slot.overhaul_needed, false)

	// The convoy-level tests agree with the per-vehicle tests.
	ASSERT_EQUAL(cnv.needs_maintenance(), false)
	ASSERT_EQUAL(cnv.needs_maintenance_urgently(), false)
	ASSERT_EQUAL(cnv.needs_overhaul(), false)

	ASSERT_TRUE(cnv.destroy(pl))

	// clean up
	local remover = command_x(tool_remover)
	ASSERT_EQUAL(remover.work(public_pl, tile_x(0, 0, 0)), null)
	ASSERT_EQUAL(remover.work(public_pl, tile_x(0, 0, 0)), null)
	ASSERT_EQUAL(remover.work(public_pl, tile_x(0, 1, 0)), null)
	RESET_ALL_PLAYER_FUNDS()
}
