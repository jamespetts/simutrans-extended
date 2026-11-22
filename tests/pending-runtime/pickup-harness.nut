//
// Pick-up goods runtime fixture. All world changes use public script interfaces:
// construction and schedule changes through tools, cargo through halt_x.
//
map.file = "empty-16x16.sve"
scenario.short_description = "Pick-up goods runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

// The runner's stall watchdog uses log growth. Engine per-step logging is
// suppressed at low debug levels, so print a heartbeat that does not depend on
// the engine log level.
local heartbeat_steps = 0
local uses_cargo = case_name != "empty-distribution" && case_name != "recipient-same" &&
	case_name != "passenger-division"
local A = coord3d(3, 2, 0)
local B = coord3d(5, 2, 0)
local B_LOOP = coord3d(5, 3, 0)
local D = coord3d(9, 2, 0)
// The passenger-division destination is beyond the pakset's walking radius
// (station_coverage / 2, eight tiles with the default station_coverage of 16),
// so the halt cannot deliver the passengers to it on foot before the source
// convoy loads them.
local P = coord3d(15, 2, 0)

local f = {
	setup_started = false,
	phase = "setup",
	depot = null,
	loco = null,
	wagon = null,
	brake = null,
	goods = null,
	consumer_name = "",
	consumer_pos = coord(9, 5),
	source_revenue_at_detach = 0,
	donor_id = 0,
	calibration_donor_id = 0,
	calibration_start = 0,
	calibration_donor_destroyed = false,
	donor_travel = 0,
	donor_start_expected = 0,
	release_expected = 0,
	collection_expected = 0,
	collector_id = 0,
	collector_started = false,
	target_convoy_id = 0,
	split_id = 0,
	injected_amount = 0,
	passenger_class = 0,
	start_delay = 0,
	loose_since = 0,
	loose_donor_id = 0,
	released_cargo = 0,
	first_arrival = 0,
	second_arrival = 0,
	last_arrival = 0,
	period = 0,
	third_observations = 0,
	steps = 0
}

function fail(what)
{
	if (f.phase != "failed" && f.phase != "done")
	{
		print("RUNTIME CASE " + case_name + " FAIL: " + what)
		print("RUNTIME CASE " + case_name + " COMPLETE")
	}
	f.phase = "failed"
	return false
}

function pass()
{
	if (f.phase != "done")
	{
		print("RUNTIME CASE " + case_name + " PASS")
		print("RUNTIME CASE " + case_name + " COMPLETE")
	}
	f.phase = "done"
}

function check(ok, what)
{
	return ok ? true : fail(what)
}

function check_tool(result, what)
{
	return (result == null || result == true) ? true : fail(what + ": " + result)
}

function convoy_ids()
{
	local ids = []
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid()) { ids.append(cnv.id) }
	}
	return ids
}

function new_convoy(before)
{
	foreach (cnv in world.get_convoy_list())
	{
		if (cnv.is_valid() && before.find(cnv.id) == null) { return cnv }
	}
	return null
}

function same_desc(a, b)
{
	return a != null && b != null && a.is_equal(b)
}

function same_pos(pos, reference)
{
	return pos.x == reference.x && pos.y == reference.y && pos.z == reference.z
}

function halt_at(pos)
{
	return halt_x.get_halt(pos, player_x(0))
}

function make_entry(pos, id)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = id
	return entry
}

function make_slot(desc, optional = false, load_requirement = 0)
{
	local slot = consist_order_element_x()
	local alternative = vehicle_description_x()
	alternative.specific_vehicle = desc
	alternative.load_requirement = same_desc(desc, f.wagon) ? load_requirement : 0
	// The slot's category drives the schedule's carried-category tables, which
	// the path explorer reads when it builds stop-to-stop connexions. A goods
	// vehicle must declare its goods category; leaving the default would record
	// the slot as carrying no goods and the line would never be routed.
	local freight = desc.get_freight()
	if (freight != null) { slot.catg = freight.get_catg_index() }
	slot.alternatives = [alternative]
	slot.optional = optional
	return slot
}

function make_order(descs, optional_count = 0, load_requirement = 0)
{
	local slots = []
	for (local i = 0; i < descs.len(); i++)
	{
		local optional = i > 0 && i <= optional_count
		slots.append(make_slot(descs[i], optional, load_requirement))
	}
	local order = consist_order_x()
	order.available_to_id = 0
	// Zero flags with a zero id means any line or consist. The default is
	// Same; callers that want it set available_to_flags afterwards.
	order.available_to_flags = 0
	order.slots = slots
	return order
}

function create_convoy(pl, descs)
{
	local before = convoy_ids()
	if (!check_tool(f.depot.append_vehicle(pl, convoy_x(0), descs[0]), "append first vehicle")) { return null }
	local cnv = new_convoy(before)
	if (!check(cnv != null, "new depot convoy is missing")) { return null }
	for (local i = 1; i < descs.len(); i++)
	{
		if (!check_tool(f.depot.append_vehicle(pl, cnv, descs[i]), "append vehicle " + i)) { return null }
	}
	return cnv
}

function put_on_new_line(pl, cnv, schedule)
{
	local before = []
	foreach (line in pl.get_line_list()) { before.append(line.id) }
	if (!check_tool(pl.create_line(wt_rail), "create pickup service line")) { return false }
	local created = null
	foreach (line in pl.get_line_list())
	{
		if (before.find(line.id) == null) { created = line; break }
	}
	if (!check(created != null, "new pickup service line not found")) { return false }
	if (!check_tool(created.change_schedule(pl, schedule), "commit pickup service line")) { return false }
	return check_tool(cnv.set_line(pl, created), "assign convoy to pickup service line")
}

// Create a line with the given schedule but no convoy. A division's target
// only needs the line's schedule; the split-off portion is formed from the
// source convoy's displaced vehicles.
function create_line_with_schedule(pl, schedule)
{
	local before = []
	foreach (line in pl.get_line_list()) { before.append(line.id) }
	if (!check_tool(pl.create_line(wt_rail), "create division target line")) { return null }
	local created = null
	foreach (line in pl.get_line_list())
	{
		if (before.find(line.id) == null) { created = line; break }
	}
	if (!check(created != null, "new division target line not found")) { return null }
	if (!check_tool(created.change_schedule(pl, schedule), "commit division target schedule")) { return null }
	return created
}


// Select a locomotive, a wagon and a brake vehicle that are mutually valid in
// both [loco, wagon, brake] and the shorter [loco, brake] forms. The exact
// types come from the pakset rather than from hard-coded vehicle names.
function select_stock(prefer_cargo_free_loco = false)
{
	local locos = []
	local locos_with_cargo = []
	local trailers = []
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.needs_electrification() || desc.get_successors().len() > 0) { continue }
		if (desc.get_power() > 0)
		{
			if (desc.can_be_first() && desc.can_be_last())
			{
				// The flash-load cases prefer a locomotive with no cargo capacity of
				// its own, so that a collected wagon's load share is measured against
				// the wagon alone. Other cases keep the pakset's own order.
				if (!prefer_cargo_free_loco || desc.get_total_capacity() == 0) { locos.append(desc) }
				else { locos_with_cargo.append(desc) }
			}
		}
		else if (desc.can_be_last())
		{
			trailers.append(desc)
		}
	}
	foreach (desc in locos_with_cargo) { locos.append(desc) }
	foreach (loco in locos)
	{
		foreach (wagon in trailers)
		{
			if (f.goods != null && (wagon.get_freight() == null || !wagon.get_freight().is_equal(f.goods) || wagon.get_total_capacity() == 0)) { continue }
			if (!vehicle_desc_x.is_coupling_allowed(loco, wagon)) { continue }
			foreach (brake in trailers)
			{
				if (!vehicle_desc_x.is_coupling_allowed(wagon, brake)) { continue }
				if (!vehicle_desc_x.is_coupling_allowed(loco, brake)) { continue }
				f.loco = loco
				f.wagon = wagon
				f.brake = brake
				return true
			}
		}
	}
	return fail("no locomotive, optional wagon and rear vehicle combination is available")
}

// Select two powered passenger railcars that can each form a portion on their
// own. The passenger division needs a source order that retains one railcar and
// a target order that takes the other, so both descriptors must be valid as a
// single-vehicle consist. Two distinct descriptors are preferred so that the
// identity assertions can tell the railcars apart; the same descriptor twice is
// accepted when the pakset has no distinct pair.
function select_passenger_stock()
{
	local railcars = []
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.needs_electrification() || desc.get_successors().len() > 0) { continue }
		if (desc.get_power() == 0 || desc.get_total_capacity() == 0) { continue }
		local freight = desc.get_freight()
		if (freight == null || !freight.is_equal(good_desc_x.passenger)) { continue }
		if (!desc.can_be_first() || !desc.can_be_last()) { continue }
		railcars.append(desc)
	}
	for (local distinct = 1; distinct >= 0; distinct--)
	{
		foreach (a in railcars)
		{
			foreach (b in railcars)
			{
				if (distinct == 1 && a.is_equal(b)) { continue }
				if (!vehicle_desc_x.is_coupling_allowed(a, b)) { continue }
				local passenger_class = -1
				for (local c = 0; c < 5; c++)
				{
					if (a.get_capacity(c) > 0 && b.get_capacity(c) > 0) { passenger_class = c; break }
				}
				if (passenger_class < 0) { continue }
				f.loco = a
				f.wagon = b
				f.passenger_class = passenger_class
				f.injected_amount = min(a.get_capacity(passenger_class), b.get_capacity(passenger_class))
				return true
			}
		}
	}
	return fail("no powered passenger railcar pair is available")
}

// Select a consumer industry from the loaded pakset. Only the consumer is
// built: the injected cargo supplies its input, so no producer runs and the
// fixture does not depend on factory production or dispatch.
function select_consumer()
{
	local first = null
	local fallback = null
	foreach (name, desc in factory_desc_x.get_list())
	{
		foreach (input in desc.get_inputs())
		{
			if (input.good == null) { continue }
			local candidate = [name, input.good]
			if (input.good.get_name() == "Kohle") { first = candidate; break }
			if (fallback == null) { fallback = candidate }
		}
		if (first != null) { break }
	}
	local choice = first != null ? first : fallback
	if (!check(choice != null, "no consumer industry input good is available")) { return false }
	f.consumer_name = choice[0]
	f.goods = choice[1]
	return true
}

function build_factory(pl, pos, name)
{
	return command_x(tool_build_factory).work(pl, pos, "1111024000," + name)
}

function build_infrastructure(pl)
{
	local other_player = player_x(2)
	// The second owner only exists to give the through journey a real
	// apportionment target. A scripted world cannot create a player on a
	// network server, so only the offline case divides the ways.
	local divide_ways = case_name == "cargo-through"
	if (divide_ways)
	{
		if (!other_player.is_valid() && !check_tool(world.create_player(2, 1), "create the other way owner")) { return false }
		other_player = player_x(2)
		if (!check(other_player.is_valid(), "other way owner was not activated")) { return false }
		if (!check_tool(other_player.set_allow_access_to(pl, true), "allow the carrier over the other player's ways")) { return false }
		if (!check_tool(pl.set_allow_access_to(other_player, true), "allow the second builder to join the existing track")) { return false }
		if (!check(other_player.allows_access_to(0) && pl.allows_access_to(2), "way-access permissions were not committed")) { return false }
	}
	local rail = way_desc_x.get_available_ways(wt_rail, st_flat)[0]
	// Player 2 owns the B-D end in the through-cargo case, giving the final
	// packet a real second-player apportionment to observe.
	if (!check_tool(command_x.build_way(pl, coord3d(1, 2, 0), coord3d(6, 2, 0), rail, false), "build A-B approach track")) { return false }
	local outward_owner = divide_ways ? other_player : pl
	if (!check_tool(command_x.build_way(outward_owner, coord3d(6, 2, 0), coord3d(9, 2, 0), rail, false), "build outward track")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 2, 0), coord3d(4, 3, 0), rail, false), "build loop west")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(4, 3, 0), coord3d(7, 3, 0), rail, false), "build loop")) { return false }
	if (!check_tool(command_x.build_way(pl, coord3d(7, 3, 0), coord3d(7, 2, 0), rail, false), "build loop east")) { return false }
	local depdesc = null
	foreach (building in building_desc_x.get_building_list(building_desc_x.depot))
	{
		if (building.get_type() == building_desc_x.depot && building.get_waytype() == wt_rail && building.is_available(world.get_time()))
		{
			depdesc = building
			break
		}
	}
	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depdesc), "build depot")) { return false }
	f.depot = depot_x(1, 2, 0)
	local station_goods = uses_cargo ? f.goods : good_desc_x.passenger
	local station_candidates = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, station_goods)
	local station = station_candidates[0]
	if (uses_cargo)
	{
		// A freight-enabled stop can still have no storage for freight (a bare
		// goods siding has capacity 0). Choose the available freight stop with
		// the largest storage capacity instead of the first in the list.
		local best = null
		foreach (st in station_candidates)
		{
			if (st.enables_freight() && st.get_capacity() > 0 && (best == null || st.get_capacity() > best.get_capacity()))
			{
				best = st
			}
		}
		if (!check(best != null, "no freight stop with storage capacity is available")) { return false }
		station = best
	}
	foreach (pos in [A, B, B_LOOP, D])
	{
		local station_owner = divide_ways && pos.x == D.x && pos.y == D.y ? other_player : pl
		if (!check_tool(command_x(tool_build_station).work(station_owner, pos, station.get_name()), "build station " + pos)) { return false }
	}

	// The passenger division runs beyond D. Build the extension only after the
	// D station so that the station is not erected on a junction.
	if (case_name == "passenger-division")
	{
		// The platform buildings are one tile and the selected railcars are
		// longer than one tile in total, so the source consist needs a second
		// platform tile west of A or only its front vehicle loads.
		local a_extension = coord3d(2, 2, 0)
		if (!check_tool(command_x(tool_build_station).work(pl, a_extension, station.get_name()), "build A platform extension")) { return false }
		if (!check(halt_at(a_extension) != null && halt_at(a_extension).get_name() == halt_at(A).get_name(), "the A platform extension did not join the A halt")) { return false }

		if (!check_tool(command_x.build_way(pl, D, P, rail, false), "build D-P passenger extension")) { return false }
		if (!check_tool(command_x(tool_build_station).work(pl, P, station.get_name()), "build station " + P)) { return false }
	}

	// The two B platforms must form one halt so its shared loose-stock pool is
	// available at both platform positions.
	local main_halt = halt_at(B)
	local loop_halt = halt_at(B_LOOP)
	if (!check(main_halt != null && loop_halt != null && main_halt.get_name() == loop_halt.get_name(), "B platforms are not one halt")) { return false }
	if (uses_cargo)
	{
		if (!check_tool(build_factory(pl, coord3d(f.consumer_pos.x, f.consumer_pos.y, 0), f.consumer_name), "build freight consumer " + f.consumer_name)) { return false }
		if (!check(halt_at(D).get_factory_list().len() > 0, "the " + f.consumer_name + " fixture does not cover D")) { return false }
	}
	return true
}

// Remove every waiting and transferring packet from the fixture halts before a
// case injects its own cargo. This keeps the injection independent of any
// cargo that the world or the consumer industry may have produced.
function clear_halts()
{
	local halts = [A, B, D]
	if (case_name == "passenger-division") { halts.append(P) }
	foreach (pos in halts)
	{
		local halt = halt_at(pos)
		if (halt == null) { return fail("no halt at " + pos + " to clear") }
		if (!check_tool(halt.clear_cargo(), "clear cargo at " + pos)) { return false }
	}
	return true
}

// Inject one cargo packet. This is the general halt_x.add_cargo API: the
// arrival time is preserved, so a case can control the packet's age exactly.
// The destination halt defaults to D, the goods cases' consumer stop; the
// passenger division names P instead. The class is the ninth add_cargo
// argument and is zero (the only goods class) unless a case names another.
function inject_cargo(pos, delivery_pos, amount, arrival_time, next_transfer, cargo_class = 0, destination_pos = D)
{
	local halt = halt_at(pos)
	local destination = halt_at(destination_pos)
	if (!check(halt != null && destination != null, "no halt at " + pos + " or " + destination_pos + " for cargo injection")) { return false }
	return check_tool(halt.add_cargo(f.goods, amount, destination, next_transfer, arrival_time, halt, delivery_pos, cargo_class), "inject cargo at " + pos)
}

function loose_wagons()
{
	local wagons = []
	local halt = halt_at(B)
	if (halt != null)
	{
		foreach (cnv in halt.get_laid_over_list())
		{
			if (cnv.is_valid())
			{
				foreach (cnv2 in world.get_convoy_list())
				{
					if (cnv2.is_valid() && cnv2.id == cnv.id)
					{
						local snapshot = []
						try { snapshot = cnv2.get_vehicle_snapshot() }
						catch (e)
						{
							fail("cannot read loose convoy " + cnv2.id + ": " + e)
							return []
						}
						foreach (v in snapshot)
						{
							try { if (same_desc(v.desc, f.wagon)) { wagons.append(v) } }
							catch (e)
							{
								fail("cannot compare loose vehicle descriptors: " + e)
								return []
							}
						}
						break
					}
				}
			}
		}
	}
	return wagons
}

function loose_convoy()
{
	local halt = halt_at(B)
	if (halt == null) { return null }
	foreach (cnv in halt.get_laid_over_list())
	{
		if (cnv.is_valid() && cnv.get_vehicle_snapshot().len() == 1) { return cnv }
	}
	return null
}

function setup_empty_distribution(pl)
{
	local donor_descs = [f.loco]
	for (local i = 0; i < 5; i++) { donor_descs.append(f.wagon) }
	local donor = create_convoy(pl, donor_descs)
	if (donor == null) { return false }
	f.donor_id = donor.id
	local release = make_order([f.loco])
	local schedule = schedule_x(wt_rail, [make_entry(B_LOOP, 11), make_entry(D, 12)])
	schedule.orders.rawset(11, release)
	local entry = schedule.entries[0]
	entry.uncouple = true
	entry.target_id_uncouple = 0
	entry.target_unique_entry_uncouple = 0
	if (!check_tool(donor.change_schedule(pl, schedule), "commit empty release schedule")) { return false }
	if (!check_tool(f.depot.start_convoy(pl, donor), "start empty release convoy")) { return false }

	local collector = create_convoy(pl, [f.loco, f.brake])
	if (collector == null) { return false }
	f.collector_id = collector.id
	local order = make_order([f.loco, f.wagon, f.wagon, f.wagon, f.brake], 3, 2)
	local cs = schedule_x(wt_rail, [make_entry(B, 21), make_entry(D, 22)])
	cs.orders.rawset(21, order)
	// Return the first batch to the yard only after depositing its wagons at D.
	// The second visit can then take the two wagons left at B.
	cs.entries[1].uncouple = true
	cs.entries[1].target_id_uncouple = 0
	cs.orders.rawset(22, make_order([f.loco, f.brake]))
	if (!check_tool(collector.change_schedule(pl, cs), "commit optional collection schedule")) { return false }
	f.phase = "wait-for-empties"
	return true
}

function setup_cargo_through(pl)
{
	if (!clear_halts()) { return false }

	local collector = create_convoy(pl, [f.loco])
	if (collector == null) { return false }
	f.collector_id = collector.id
	local collector_schedule = schedule_x(wt_rail, [make_entry(B, 41), make_entry(D, 42)])
	// The onward service requests only a loaded wagon of this type. An empty
	// wagon of the same descriptor must not be taken for this slot.
	collector_schedule.orders.rawset(41, make_order([f.loco, f.wagon], 1, 1))
	if (!check_tool(collector.change_schedule(pl, collector_schedule), "commit onward cargo schedule")) { return false }
	if (!put_on_new_line(pl, collector, collector_schedule)) { return false }

	local donor = create_convoy(pl, [f.loco, f.wagon])
	if (donor == null) { return false }
	local donor_schedule = schedule_x(wt_rail, [make_entry(A, 31), make_entry(B_LOOP, 32)])
	// Depart once the wagon holds any part of a load. A full load is not
	// required: the case exercises the release, collection and through-cargo
	// rules, not throughput. Spacing is disabled because a convoy on a line
	// that waits for a minimum load otherwise waits for its next spacing slot,
	// which is measured in months.
	donor_schedule.entries[0].minimum_loading = 1
	donor_schedule.spacing = 0
	// A line whose schedule has consist orders is routed by the categories its
	// orders declare, not by its vehicles, so the loading entry must also
	// declare the loaded consist it leaves with.
	donor_schedule.orders.rawset(31, make_order([f.loco, f.wagon]))
	donor_schedule.entries[1].uncouple = true
	donor_schedule.entries[1].target_id_uncouple = 0
	// The ordinary through case uses unrestricted collection. The reload case
	// instead names the collector's line and checks that choice after both loads.
	local release_order = make_order([f.loco])
	if (case_name == "pickup-reload")
	{
		release_order.available_to_id = collector.get_line().id
		release_order.available_to_flags = 0
	}
	donor_schedule.orders.rawset(32, release_order)
	if (!check_tool(donor.change_schedule(pl, donor_schedule), "commit loaded donor schedule")) { return false }
	if (!put_on_new_line(pl, donor, donor_schedule)) { return false }
	f.donor_id = donor.id
	if (!check(donor.get_schedule().entries[0].minimum_loading == 1, "loaded donor did not retain its loading condition at A")) { return false }

	// The packet is placed at A with B as its next transfer. The donor loads it
	// because B is the next stop on its line; no factory production is involved.
	// The through-pool case sends it back to A, so the B-D collector does not
	// serve its destination and must leave the loaded wagon in the pool.
	if (!inject_cargo(A, f.consumer_pos, f.wagon.get_total_capacity(), world.get_time().ticks, halt_at(B))) { return false }
	if (!check_tool(f.depot.start_convoy(pl, donor), "start loaded donor")) { return false }

	f.phase = "wait-for-loaded-loose"
	return true
}

// A goods wagon is loaded at A for D, although the source schedule only runs
// A-B. The target line's schedule B-D is the wagon's committed portion, so the
// loader must read the target schedule when it decides what the wagon may
// carry. At B the source order retains the leading locomotive and the target
// order takes the trailing locomotive and the wagon; the split-off portion
// must be promoted and carry the wagon to D.
function setup_goods_division(pl)
{
	if (!clear_halts()) { return false }

	local source = create_convoy(pl, [f.loco, f.wagon, f.loco])
	if (source == null) { return false }
	f.donor_id = source.id

	// The target is a line with the B-D schedule. A depot convoy is assigned
	// to it so the line is an operating service; the split-off portion joins
	// the same line. The depot convoy is not started.
	local target_schedule = schedule_x(wt_rail, [make_entry(B, 51), make_entry(D, 52)])
	target_schedule.orders.rawset(51, make_order([f.loco, f.wagon]))
	local target_line = create_line_with_schedule(pl, target_schedule)
	if (target_line == null) { return false }
	local target = create_convoy(pl, [f.loco])
	if (target == null) { return false }
	f.target_convoy_id = target.id
	if (!check_tool(target.set_line(pl, target_line), "assign target line convoy")) { return false }

	local source_schedule = schedule_x(wt_rail, [make_entry(A, 41), make_entry(B, 42)])
	source_schedule.orders.rawset(41, make_order([f.loco, f.wagon, f.loco]))
	source_schedule.orders.rawset(42, make_order([f.loco]))
	local entry = source_schedule.entries[1]
	entry.uncouple = true
	entry.target_id_uncouple = target_line.id
	entry.uncouple_target_is_line_or_cnv = false
	entry.target_unique_entry_uncouple = 51
	if (!put_on_new_line(pl, source, source_schedule)) { return false }

	f.injected_amount = f.wagon.get_total_capacity()
	if (!inject_cargo(A, f.consumer_pos, f.injected_amount, world.get_time().ticks, halt_at(D))) { return false }

	f.phase = "wait-before-start"
	f.start_delay = 0
	return true
}

// A passenger railcar is loaded at A for P, although the source schedule only
// runs A-B. The target line's B-P schedule is the loaded railcar's committed
// serving portion, so the loader must read the target schedule. At B the source
// order retains the leading railcar and the target order takes the loaded
// trailing railcar; the split-off portion must be promoted and carry the
// passengers to P. P is twelve tiles from A, beyond the walking radius.
function setup_passenger_division(pl)
{
	if (!clear_halts()) { return false }

	// The target is a line with the B-P schedule. A depot convoy is assigned to
	// it so the line is an operating service; the split-off portion joins the
	// same line. The depot convoy is not started.
	local target_schedule = schedule_x(wt_rail, [make_entry(B, 51), make_entry(P, 52)])
	target_schedule.orders.rawset(51, make_order([f.wagon]))
	local target_line = create_line_with_schedule(pl, target_schedule)
	if (target_line == null) { return false }
	local target = create_convoy(pl, [f.loco])
	if (target == null) { return false }
	f.target_convoy_id = target.id
	if (!check_tool(target.set_line(pl, target_line), "assign target line convoy")) { return false }

	local source = create_convoy(pl, [f.loco, f.wagon])
	if (source == null) { return false }
	f.donor_id = source.id
	local source_schedule = schedule_x(wt_rail, [make_entry(A, 41), make_entry(B, 42)])
	source_schedule.orders.rawset(41, make_order([f.loco, f.wagon]))
	source_schedule.orders.rawset(42, make_order([f.loco]))
	local entry = source_schedule.entries[1]
	entry.uncouple = true
	entry.target_id_uncouple = target_line.id
	entry.uncouple_target_is_line_or_cnv = false
	entry.target_unique_entry_uncouple = 51
	if (!put_on_new_line(pl, source, source_schedule)) { return false }

	// The passengers are injected by the observer once the source convoy has
	// left the depot. Starting a convoy rebuilds the passenger routing; a packet
	// present during that rebuild can be walked to the wrong stop, so it is
	// added only after the rebuild has settled.
	f.phase = "wait-before-start"
	f.start_delay = 0
	return true
}


function target_line_convoy()
{
	foreach (cnv in world.get_convoy_list())
	{
		if (!cnv.is_valid() || cnv.id == f.target_convoy_id || cnv.id == f.donor_id) { continue }
		local slots = cnv.get_vehicle_snapshot()
		if (slots.len() == 2 && same_desc(slots[0].desc, f.loco) && same_desc(slots[1].desc, f.wagon)) { return cnv }
	}
	return null
}


function observe_goods_division()
{
	if (f.phase == "wait-before-start")
	{
		// Let the line routing settle before the source convoy starts, so the
		// injected cargo is not removed as unroutable on the first step.
		if (++f.start_delay < 20) { return }
		local source = convoy_x(f.donor_id)
		if (!check(source.is_valid(), "source convoy disappeared before starting")) { return }
		if (!check_tool(f.depot.start_convoy(player_x(0), source), "start division source convoy")) { return }
		f.phase = "wait-for-load"
		return
	}
	if (f.phase == "wait-for-load")
	{
		local source = convoy_x(f.donor_id)
		if (!source.is_valid()) { fail("source convoy disappeared before loading"); return }
		if (source.get_current_entry_id() == 42)
		{
			f.phase = "wait-for-division"
		}
		return
	}
	if (f.phase == "wait-for-division")
	{
		local portion = target_line_convoy()
		if (portion == null) { return }
		local source = convoy_x(f.donor_id)
		if (!check(source.is_valid(), "source convoy disappeared before the division")) { return }
		local source_slots = source.get_vehicle_snapshot()
		if (!check(source_slots.len() == 1, "the retained portion has " + source_slots.len() + " vehicles, expected one locomotive")) { return }
		if (!check(same_desc(source_slots[0].desc, f.loco), "the retained portion does not hold the source locomotive")) { return }
		local slots = portion.get_vehicle_snapshot()
		if (!check(slots.len() == 2, "the split-off portion has " + slots.len() + " vehicles, expected two")) { return }
		if (!check(same_desc(slots[0].desc, f.loco), "the split-off portion does not start with the trailing locomotive")) { return }
		if (!check(same_desc(slots[1].desc, f.wagon), "the split-off portion does not hold the goods wagon")) { return }
		if (!check(slots[1].cargo == f.injected_amount, "the wagon's load changed at the division: " + slots[1].cargo + " of " + f.injected_amount)) { return }
		f.split_id = portion.id
		f.phase = "wait-for-delivery"
		return
	}
	if (f.phase == "wait-for-delivery")
	{
		local portion = convoy_x(f.split_id)
		if (!check(portion.is_valid(), "the split-off portion disappeared")) { return }
		local slots = portion.get_vehicle_snapshot()
		if (portion.get_current_entry_id() != 52 || slots.len() != 2 || !same_pos(slots[0].pos, D)) { return }
		if (!check(same_desc(slots[1].desc, f.wagon), "the split-off portion no longer holds the wagon at D")) { return }
		if (slots[1].cargo != 0) { return }
		pass()
	}
}

// The split-off portion is the only convoy, other than the source and the
// target line's depot convoy, that holds one vehicle of the loaded railcar's
// descriptor. A loose railcar is found too, so the assertions below distinguish
// a promoted portion by its target entry.
function passenger_split_convoy()
{
	foreach (cnv in world.get_convoy_list())
	{
		if (!cnv.is_valid() || cnv.id == f.target_convoy_id || cnv.id == f.donor_id) { continue }
		local slots = cnv.get_vehicle_snapshot()
		if (slots.len() == 1 && same_desc(slots[0].desc, f.wagon)) { return cnv }
	}
	return null
}

function observe_passenger_division()
{
	if (f.phase == "wait-before-start")
	{
		// Let the line routing settle before the source convoy starts, so the
		// path explorer has built the A-B-P route when the passengers are added.
		if (++f.start_delay < 60) { return }
		local source = convoy_x(f.donor_id)
		if (!check(source.is_valid(), "source convoy disappeared before starting")) { return }
		if (!check_tool(f.depot.start_convoy(player_x(0), source), "start passenger division source convoy")) { return }
		f.start_delay = 0
		f.phase = "wait-for-approach"
		return
	}
	if (f.phase == "wait-for-approach")
	{
		local source = convoy_x(f.donor_id)
		if (!check(source.is_valid(), "source convoy disappeared before reaching A")) { return }
		local snap = source.get_vehicle_snapshot()
		if (!check(snap.len() == 2, "the source consist has " + snap.len() + " vehicles")) { return }
		// The depot is at (1,2) and the A stop at (3,2). Inject once the leading
		// vehicle has left the depot tile, so the routing rebuild caused by the
		// departure has settled and the passengers are present before the stop.
		if (snap[0].pos.x < 2) { return }
		if (!inject_cargo(A, P, f.injected_amount, world.get_time().ticks, halt_at(P), f.passenger_class, P)) { return }
		f.start_delay = 0
		f.phase = "wait-for-load"
		return
	}
	if (f.phase == "wait-for-load")
	{
		local source = convoy_x(f.donor_id)
		if (!source.is_valid()) { fail("source convoy disappeared before loading"); return }
		local snap = source.get_vehicle_snapshot()
		if (source.get_current_entry_id() == 42)
		{
			f.phase = "wait-for-division"
			return
		}
		// The passengers must reach the loading pass. If they leave A while the
		// source consist is still empty, the halt walked or discarded them.
		if (halt_at(A).get_freight_to_dest(f.goods, P) == 0)
		{
			local cargo = 0
			foreach (v in snap) { cargo += v.cargo }
			if (!check(cargo > 0, "the passengers left A before the source loaded them")) { return }
		}
		return
	}
	if (f.phase == "wait-for-division")
	{
		local portion = passenger_split_convoy()
		if (portion == null) { return }
		local source = convoy_x(f.donor_id)
		if (!check(source.is_valid(), "source convoy disappeared before the division")) { return }
		local source_slots = source.get_vehicle_snapshot()
		if (!check(source_slots.len() == 1, "the retained portion has " + source_slots.len() + " vehicles, expected one railcar")) { return }
		if (!check(same_desc(source_slots[0].desc, f.loco), "the retained portion does not hold the source railcar")) { return }
		if (!check(source_slots[0].cargo == 0, "the retained railcar kept " + source_slots[0].cargo + " passengers")) { return }
		local slots = portion.get_vehicle_snapshot()
		if (!check(slots.len() == 1, "the split-off portion has " + slots.len() + " vehicles, expected one")) { return }
		if (!check(same_desc(slots[0].desc, f.wagon), "the split-off portion does not hold the loaded railcar")) { return }
		if (!check(portion.get_current_entry_id() == 51, "the split-off portion is at entry " + portion.get_current_entry_id() + ", expected 51")) { return }
		if (!check(same_pos(slots[0].pos, B), "the split-off portion did not take over at B")) { return }
		if (!check(slots[0].cargo == f.injected_amount, "the railcar's passenger load at the division is " + slots[0].cargo + " of " + f.injected_amount)) { return }
		if (!check(halt_at(B).get_freight_to_dest(f.goods, P) == 0, "passengers were discharged at B")) { return }
		f.split_id = portion.id
		f.phase = "wait-for-delivery"
		return
	}
	if (f.phase == "wait-for-delivery")
	{
		local portion = convoy_x(f.split_id)
		if (!check(portion.is_valid(), "the split-off portion disappeared")) { return }
		local slots = portion.get_vehicle_snapshot()
		if (portion.get_current_entry_id() != 52 || slots.len() != 1 || !same_pos(slots[0].pos, P)) { return }
		if (!check(same_desc(slots[0].desc, f.wagon), "the split-off portion no longer holds the loaded railcar at P")) { return }
		if (slots[0].cargo != 0) { return }
		pass()
	}
}


function observe_cargo_through()
{
	if (f.phase == "wait-for-loaded-loose")
	{
		local wagons = loose_wagons()
		if (wagons.len() == 0) { return }
		if (!check(wagons.len() == 1, "collector started before the loaded wagon was laid over")) { return }
		if (!check(wagons[0].cargo > 0, "the detached wagon lost its through cargo")) { return }
		// The wagon's own load is the evidence: it must be the same load that
		// the collecting service later carries.
		f.released_cargo = wagons[0].cargo
		if (case_name == "pickup-reload")
		{
			local collector_line = convoy_x(f.collector_id).get_line()
			if (!check(collector_line != null && collector_line.is_valid(), "named-recipient collector has no line before the reload")) { return }
			if (!check(!wagons[0].loose_recipient_is_convoy && !wagons[0].loose_recipient_is_same && !wagons[0].loose_recipient_is_any
				&& wagons[0].loose_recipient_id == collector_line.id, "the loose wagon did not retain its named collecting line")) { return }
		}
		local donor = convoy_x(f.donor_id)
		if (!check(donor.is_valid(), "loaded donor disappeared before its booking was checked")) { return }
		f.source_revenue_at_detach = donor.get_revenue()[0]
		if (!check(f.source_revenue_at_detach == 0, "cargo was booked as a completed fare at the intermediate yard")) { return }
		if (!check_tool(donor.destroy(player_x(0)), "remove loaded donor after detachment")) { return }
		f.phase = "remove-loaded-donor"
		return
	}
	if (f.phase == "remove-loaded-donor")
	{
		if (convoy_x(f.donor_id).is_valid()) { return }
		if (case_name == "pickup-reload")
		{
			local wagons = loose_wagons()
			if (!check(wagons.len() == 1 && wagons[0].cargo > 0, "the loaded loose wagon did not survive removal of the first carrier")) { return }
			local loose = loose_convoy()
			if (!check(loose != null, "cannot identify the loaded loose wagon's convoy")) { return }
			f.loose_donor_id = loose.id
			persistent.pickup_reload <- {
				collector_id = f.collector_id,
				loose_donor_id = f.loose_donor_id,
				source_revenue = f.source_revenue_at_detach,
				released_cargo = f.released_cargo,
				goods_name = f.goods.get_name()
			}
			f.phase = "awaiting-reload"
			print("RUNTIME CASE " + case_name + " READY")
			return
		}
		f.phase = "resume-start-collector"
		return
	}
	if (f.phase == "resume-start-collector")
	{
		start_through_collector()
		return
	}
	if (f.phase == "collect-loaded")
	{
		local collector = convoy_x(f.collector_id)
		if (!collector.is_valid()) { fail("onward collector disappeared"); return }
		local snap = collector.get_vehicle_snapshot()
		if (snap.len() == 2 && snap[1].cargo > 0)
		{
			if (!check(same_desc(snap[1].desc, f.wagon), "onward service collected a different vehicle")) { return }
			if (!check(snap[1].cargo == f.released_cargo, "the collected wagon's through load changed between the release and the collection")) { return }
			f.phase = "wait-for-delivery"
		}
		return
	}
	if (f.phase == "wait-for-delivery")
	{
		// The halt holds goods that arrive for a building for its transshipment
		// time before handing them to the factory, so the factory's received
		// statistics lag the delivery. The unloading itself books the fare, so
		// wait for the collector to stand at the destination entry with its load
		// discharged.
		local collector = convoy_x(f.collector_id)
		if (!collector.is_valid()) { fail("onward collector disappeared"); return }
		if (collector.get_current_entry_id() != 42) { return }
		local cargo = 0
		foreach (v in collector.get_vehicle_snapshot()) { cargo += v.cargo }
		if (cargo != 0) { return }
		if (!check(collector.get_revenue()[0] > 0, "delivery did not book the through fare")) { return }
		if (case_name == "through-pair" || case_name == "pickup-reload")
		{
			// The collecting line stores the pair from the halt where the wagon
			// boarded (A) to the halt where the convoy arrived (D). A journey that
			// a vehicle started on another convoy is stored only in the line's
			// table, never in the collecting convoy's own table.
			local from = halt_at(A);
			local to = halt_at(D);
			local line = collector.get_line();
			if (!check(line != null && line.is_valid(), "the collecting convoy has no line")) { return }
			if (!check(line.get_recorded_journey_time(from, to) > 0,
				"the collecting line did not record a positive A-D through pair time")) { return }
			if (!check(collector.get_own_recorded_journey_time(from, to) == -1,
				"the collecting convoy's own table holds the A-D through pair")) { return }
		}
		// Way tolls cannot be observed in this fixture: the saved world stores
		// toll_revenue_percentage as zero, so no apportioned revenue is paid to
		// a way owner however the journey is apportioned.
		pass()
	}
}

function flash_load_share(amount)
{
	// convoi_t::calc_current_loading_time divides the loaded amount by twice the
	// sum of every vehicle's total and overcrowded capacity, and scales the
	// difference between the longest minimum and maximum loading times by that
	// percentage. The collecting consist is the locomotive and the collected
	// wagon, so both contribute.
	local capacity = 2 * ((f.loco.get_total_capacity() + f.loco.get_overcrowded_capacity())
		+ (f.wagon.get_total_capacity() + f.wagon.get_overcrowded_capacity()))
	if (capacity == 0) { return 0 }
	local percentage = (amount * 100) / capacity
	return ((flash_longest_max_loading_time() - flash_longest_min_loading_time()) * percentage) / 100
}

function flash_longest_min_loading_time()
{
	return max(f.loco.get_min_loading_time(), f.wagon.get_min_loading_time())
}

function flash_longest_max_loading_time()
{
	return max(f.loco.get_max_loading_time(), f.wagon.get_max_loading_time())
}

function start_through_collector()
{
	if (!f.collector_started)
	{
		if (!check_tool(f.depot.start_convoy(player_x(0), convoy_x(f.collector_id)), "start onward collector")) { return false }
		f.collector_started = true
	}
	f.phase = "collect-loaded"
	return true
}

function start_flash_donor()
{
	if (!check_tool(f.depot.start_convoy(player_x(0), convoy_x(f.donor_id)), "start empty flash-load donor")) { return false }
	return true
}

function make_flash_donor(pl)
{
	// The donor carries one empty wagon to the yard and leaves it there. It does
	// not stop at A, so the run to the yard is short and its release tick is
	// easy to place. After releasing the wagon it heads back west to A; that
	// keeps it off the eastbound B-D line while it is being removed.
	local donor_schedule = schedule_x(wt_rail, [make_entry(B_LOOP, 51), make_entry(A, 52)])
	donor_schedule.entries[0].uncouple = true
	donor_schedule.entries[0].target_id_uncouple = 0
	donor_schedule.orders.rawset(51, make_order([f.loco]))
	local donor = create_convoy(pl, [f.loco, f.wagon])
	if (donor == null) { return null }
	if (!check_tool(donor.change_schedule(pl, donor_schedule), "commit empty flash-load donor schedule")) { return null }
	if (!put_on_new_line(pl, donor, donor_schedule)) { return null }
	return donor
}

function setup_flash_load(pl)
{
	// One donor run measures the travel time from the depot to the wagon
	// release. The same consist then runs again at a scheduled start, so the
	// release can be placed just after a collector arrival: that gives the
	// longest possible lead to the collecting arrival and keeps the donor clear
	// of the collector's junction at that arrival.
	local calibration = make_flash_donor(pl)
	if (calibration == null) { return false }
	f.calibration_donor_id = calibration.id
	local donor = make_flash_donor(pl)
	if (donor == null) { return false }
	f.donor_id = donor.id

	// The collector runs a B-D loop with no cargo. Its two observed arrival
	// times at B give the looping period. The optional wagon slot accepts an
	// empty wagon, which is the wagon's state at collection time: the halt does
	// not preload a loose wagon.
	local collector = create_convoy(pl, [f.loco])
	if (collector == null) { return false }
	f.collector_id = collector.id
	local collector_schedule = schedule_x(wt_rail, [make_entry(B, 71), make_entry(D, 72)])
	collector_schedule.orders.rawset(71, make_order([f.loco, f.wagon], 1, 2))
	collector_schedule.entries[0].minimum_loading = 0
	collector_schedule.spacing = 0
	if (!check_tool(collector.change_schedule(pl, collector_schedule), "commit flash-load collector schedule")) { return false }
	if (!put_on_new_line(pl, collector, collector_schedule)) { return false }

	if (!clear_halts()) { return false }
	f.calibration_start = world.get_time().ticks
	if (!check_tool(f.depot.start_convoy(pl, calibration), "start flash-load calibration donor")) { return false }
	f.phase = "calibrate-donor"
	return true
}

function observe_flash_load()
{
	local collector = convoy_x(f.collector_id)
	if (f.phase == "calibrate-donor" || f.phase == "calibrate-remove-donor" || f.phase == "calibrate-cleanup")
	{
		// The collector stays in the depot until the calibration wagon has been
		// removed, so the two runs cannot interfere.
		if (f.phase == "calibrate-donor")
		{
			local wagons = loose_wagons()
			if (wagons.len() == 0) { return }
			if (!check(wagons.len() == 1, "the calibration donor released the wrong number of wagons")) { return }
			if (!check(wagons[0].cargo == 0, "the calibration wagon was not empty")) { return }
			local loose = loose_convoy()
			if (!check(loose != null, "cannot identify the calibration wagon's convoy")) { return }
			f.donor_travel = loose.get_arrival_time() - f.calibration_start
			if (!check(f.donor_travel > 0, "the calibration donor travel time is not positive")) { return }
			if (!check_tool(loose.destroy(player_x(0)), "remove calibration wagon")) { return }
			f.phase = "calibrate-remove-donor"
			return
		}
		if (f.phase == "calibrate-remove-donor")
		{
			local calibration = convoy_x(f.calibration_donor_id)
			if (calibration.is_valid())
			{
				if (!f.calibration_donor_destroyed)
				{
					if (!check_tool(calibration.destroy(player_x(0)), "remove calibration donor")) { return }
					f.calibration_donor_destroyed = true
				}
				return
			}
			f.phase = "calibrate-cleanup"
			return
		}
		if (f.phase == "calibrate-cleanup")
		{
			if (convoy_x(f.calibration_donor_id).is_valid() || loose_convoy() != null) { return }
			if (!check_tool(f.depot.start_convoy(player_x(0), collector), "start flash-load collector probe")) { return }
			f.collector_started = true
			f.phase = "measure-first-arrival"
			return
		}
	}

	if (!collector.is_valid()) { fail("flash-load collector disappeared"); return }
	local snap = collector.get_vehicle_snapshot()
	local at_yard = snap.len() > 0 && same_pos(snap[0].pos, B)

	// After the probe, record each later B arrival. The donor may delay the
	// collector, so the collecting arrival is predicted from the last actual
	// arrival before the wagon release, not from the undisturbed probe sequence.
	if (f.phase == "wait-for-donor-start" || f.phase == "release-empty" || f.phase == "wait-for-injection")
	{
		if (at_yard && world.get_time().ticks != f.last_arrival)
		{
			f.last_arrival = world.get_time().ticks
		}
	}

	if (f.phase == "measure-first-arrival")
	{
		if (at_yard)
		{
			f.first_arrival = world.get_time().ticks
			if (!check(f.first_arrival > 0, "the collector's first B arrival has no tick")) { return }
			f.phase = "wait-left-first-stop"
		}
		return
	}
	if (f.phase == "wait-left-first-stop")
	{
		if (!at_yard) { f.phase = "measure-second-arrival" }
		return
	}
	if (f.phase == "measure-second-arrival")
	{
		if (at_yard)
		{
			f.second_arrival = world.get_time().ticks
			f.period = f.second_arrival - f.first_arrival
			f.last_arrival = f.second_arrival
			if (!check(f.period > 0, "flash-load collector period is not positive: " + f.first_arrival + " -> " + f.second_arrival)) { return }
			// Choose the earliest release slot that still leaves time to start the
			// donor. The release is placed just after an arrival, so the wagon is
			// loose for almost the whole period before the collecting arrival.
			local margin = 15000
			local k = 3
			while (f.first_arrival + (k - 1) * f.period + margin - f.donor_travel < f.second_arrival + 1000) { k++ }
			f.release_expected = f.first_arrival + (k - 1) * f.period + margin
			f.donor_start_expected = f.release_expected - f.donor_travel
			f.collection_expected = f.first_arrival + k * f.period
			f.phase = "wait-for-donor-start"
		}
		return
	}
	if (f.phase == "wait-for-donor-start")
	{
		if (world.get_time().ticks < f.donor_start_expected) { return }
		if (!start_flash_donor()) { return }
		f.phase = "release-empty"
		return
	}
	if (f.phase == "release-empty")
	{
		local wagons = loose_wagons()
		if (wagons.len() == 0) { return }
		if (!check(wagons.len() == 1, "the flash-load donor released the wrong number of wagons")) { return }
		if (!check(wagons[0].cargo == 0, "the flash-load donor loaded instead of remaining empty")) { return }
		local loose = loose_convoy()
		if (!check(loose != null, "cannot identify the loose wagon's convoy")) { return }
		f.loose_since = loose.get_arrival_time()
		if (!check(f.loose_since > 0, "the loose wagon has no arrival time")) { return }
		if (!check(loose_wagons().len() == 1, "the collector reached the wagon before the cargo could be injected")) { return }

		// The wagon is collected at the first B arrival after it became loose.
		// The collecting tick is one period after the last actual B arrival, which
		// absorbs any donor-induced delay on the approach to that arrival. The
		// packet's arrival time is placed relative to it. Full credit needs the
		// whole load share of lead, partial credit is aimed at half the share, and
		// the late case at a small interval before the collection.
		local amount = f.wagon.get_total_capacity()
		local share = flash_load_share(amount)
		if (!check(share > 0, "the wagon's loading times give no flash-load share")) { return }
		f.collection_expected = f.last_arrival + f.period
		if (!check(f.collection_expected > f.loose_since, "no collector arrival follows the wagon release")) { return }
		local lead = f.collection_expected - f.loose_since
		local arrival = f.loose_since
		if (case_name == "flash-partial")
		{
			arrival = f.collection_expected - share / 2
		}
		else if (case_name == "flash-late")
		{
			// The collecting arrival can drift by a few thousand ticks either
			// way. A lead of 3000 ticks near the collection keeps the credit
			// small but positive enough to be loaded in the collecting pass.
			arrival = f.collection_expected - 3000
		}
		else if (!check(lead >= share, "the next collector arrival leaves only " + lead + " ticks, less than the " + share + "-tick full credit")) { return }
		if (!check_tool(convoy_x(f.donor_id).destroy(player_x(0)), "remove empty donor after release")) { return }
		// Inject at the release rather than waiting for the predicted collection:
		// the packet's arrival time is the case's declared value, so the credit
		// does not depend on the injection call's exact wall-clock tick.
		local amount = f.wagon.get_total_capacity()
		if (!inject_cargo(B, f.consumer_pos, amount, arrival, halt_at(D))) { return }
		print("RUNTIME CASE " + case_name + ": injected " + amount + " with arrival tick " + arrival
			+ ", loose since " + f.loose_since + ", collection expected at " + f.collection_expected)
		f.phase = "check-flash-credit"
		return
	}
	if (f.phase == "check-flash-credit")
	{
		if (snap.len() != 2 || snap[1].cargo == 0) { return }
		if (!check(same_desc(snap[1].desc, f.wagon), "the flash-load collector took a different vehicle")) { return }
		// Mirror convoi_t::laden: share of the ordinary loading time, capped by
		// the interval from the later of the wagon becoming loose and the
		// packet arriving to the tick at which the vehicle was collected.
		local share = flash_load_share(snap[1].cargo)
		local available_since = max(snap[1].collection_loose_since, snap[1].earliest_cargo_arrival)
		local collected_at = snap[1].collection_time
		local overlap = collected_at > available_since ? collected_at - available_since : 0
		local credit = min(share, overlap)
		local expected = flash_longest_min_loading_time() + share - credit
		local actual = collector.get_current_loading_time()
		if (!check(actual == expected, "flash-load credit: share=" + share + ", overlap=" + overlap
			+ ", expected loading time " + expected + ", got " + actual)) { return }
		if (case_name == "flash-full" || case_name == "flash-reload")
		{
			if (!check(credit == share && share > 0, "full availability did not credit the entire load share")) { return }
		}
		if (case_name == "flash-partial" && !check(credit > 0 && credit < share, "later cargo did not receive a partial credit")) { return }
		if (case_name == "flash-late" && !check(credit < share, "immediately collected cargo received the full loading-time credit")) { return }
		if (case_name == "flash-reload")
		{
			persistent.flash_reload <- {
				collector_id = f.collector_id,
				loose_since = f.loose_since,
				expected_loading_time = actual,
				goods_name = f.goods.get_name()
			}
			f.phase = "awaiting-reload"
			print("RUNTIME CASE " + case_name + " READY")
			return
		}
		print("RUNTIME CASE " + case_name + ": loading time=" + actual + ", credited ticks=" + credit)
		f.phase = "wait-for-flash-delivery"
		return
	}
	if (f.phase == "wait-for-flash-delivery")
	{
		if (collector.get_current_entry_id() != 72) { return }
		local cargo = 0
		foreach (v in snap) { cargo += v.cargo }
		if (cargo != 0) { return }
		if (!check(collector.get_revenue()[0] > 0, "flash-load delivery did not book the fare")) { return }
		pass()
	}
}

function observe_empty_distribution()
{
	if (f.phase == "wait-for-empties")
	{
		if (loose_wagons().len() == 5)
		{
			print("RUNTIME CASE " + case_name + ": five empty wagons released")
			local donor = convoy_x(f.donor_id)
			f.phase = "empty-release-found"
			if (!check(donor.is_valid(), "release convoy disappeared before removal")) { return }
			if (!check_tool(donor.destroy(player_x(0)), "remove release convoy")) { return }
			f.phase = "remove-donor"
		}
		return
	}
	if (f.phase == "remove-donor")
	{
		if (convoy_x(f.donor_id).is_valid()) { return }
		local collector = convoy_x(f.collector_id)
		if (!check_tool(f.depot.start_convoy(player_x(0), collector), "start optional collector")) { return }
		f.phase = "first"
		return
	}
	local cnv = convoy_x(f.collector_id)
	if (!cnv.is_valid()) { fail("optional collector disappeared"); return }
	local entry = cnv.get_current_entry_id()
	local snap = cnv.get_vehicle_snapshot()
	if (f.phase == "first" && entry == 21 && snap.len() == 5)
	{
		if (!check(f.loco.is_equal(snap[0].desc) && f.brake.is_equal(snap[4].desc), "first pickup does not preserve locomotive and rear vehicle")) { return }
		if (!check(loose_wagons().len() == 2, "first optional pickup did not leave two empty wagons")) { return }
		f.phase = "first-away"
		return
	}
	if (f.phase == "first-away" && entry == 22) { f.phase = "second"; return }
	if (f.phase == "second" && entry == 21 && snap.len() == 4)
	{
		if (!check(f.brake.is_equal(snap[3].desc), "rear mandatory vehicle moved ahead of an optional wagon")) { return }
		if (!check(loose_wagons().len() == 0, "second optional pickup did not consume the two remaining empty wagons")) { return }
		f.phase = "second-away"
		return
	}
	if (f.phase == "second-away" && entry == 22) { f.phase = "third"; return }
	if (f.phase == "third" && entry == 21 && snap.len() == 2)
	{
		// D's mandatory order has already released the second pair. The absence
		// of pool wagons must not add anything or displace the rear vehicle.
		if (++f.third_observations > 20)
		{
			if (!check(f.brake.is_equal(snap[1].desc) && loose_wagons().len() == 0, "no available optional wagon changed the yard pickup or the mandated ends")) { return }
			pass()
		}
	}
}

function setup_recipient_same(pl)
{
	// One lineless convoy releases its wagon at the first B entry and collects
	// it at the second. Same must resolve to that releasing convoy, not to the
	// separate laid-over convoy created to hold the wagon.
	local donor = create_convoy(pl, [f.loco, f.wagon])
	if (donor == null) { return false }
	f.donor_id = donor.id
	f.collector_id = donor.id
	f.collector_started = true
	local schedule = schedule_x(wt_rail, [make_entry(B_LOOP, 91), make_entry(D, 92), make_entry(B, 93), make_entry(D, 94)])
	schedule.entries[0].uncouple = true
	schedule.entries[0].target_id_uncouple = 0
	local release = make_order([f.loco])
	// The default recipient is Same: only the convoy that released the wagon
	// may collect it. available_to_flags 0 would be an explicit Any.
	release.available_to_flags = 2
	schedule.orders.rawset(91, release)
	schedule.orders.rawset(93, make_order([f.loco, f.wagon], 0, 2))
	if (!check_tool(donor.change_schedule(pl, schedule), "commit Same recipient schedule")) { return false }
	f.phase = "wait-for-same-release"
	return check_tool(f.depot.start_convoy(pl, donor), "start Same recipient donor")
}

function observe_recipient_same()
{
	if (f.phase == "wait-for-same-release")
	{
		local wagons = loose_wagons()
		if (wagons.len() == 0) { return }
		if (!check(wagons.len() == 1 && wagons[0].cargo == 0, "Same recipient release produced the wrong empty stock")) { return }
		if (!check(wagons[0].loose_recipient_is_convoy && wagons[0].loose_recipient_is_same && !wagons[0].loose_recipient_is_any
			&& wagons[0].loose_recipient_id == f.donor_id, "Same did not retain the releasing convoy identity")) { return }
		f.phase = "wait-for-same-collection"
		return
	}
	if (f.phase == "wait-for-same-collection")
	{
		local donor = convoy_x(f.donor_id)
		if (!check(donor.is_valid(), "the Same recipient convoy disappeared")) { return }
		if (donor.get_current_entry_id() == 93)
		{
			local snap = donor.get_vehicle_snapshot()
			if (snap.len() == 2)
			{
				// A convoy that reversed at the terminus runs with the
				// locomotive at the rear, so the snapshot order need not match
				// the authored order. The collected wagon must simply be back in
				// the consist and gone from the yard pool.
				local has_wagon = false
				local has_loco = false
				foreach (v in snap)
				{
					if (same_desc(v.desc, f.wagon)) { has_wagon = true }
					if (same_desc(v.desc, f.loco)) { has_loco = true }
				}
				if (!check(has_loco && has_wagon, "the collecting convoy did not take the released wagon")) { return }
				if (!check(loose_wagons().len() == 0, "the collected wagon remained in the yard pool")) { return }
				pass()
			}
		}
	}
}

function setup()
{
	local pl = player_x(0)
	if (case_name == "passenger-division")
	{
		f.goods = good_desc_x.passenger
		if (!build_infrastructure(pl)) { return false }
		if (!select_passenger_stock()) { return false }
		return setup_passenger_division(pl)
	}
	if (uses_cargo && !select_consumer()) { return false }
	if (!build_infrastructure(pl)) { return false }
	if (!select_stock(case_name.slice(0, 6) == "flash-" || case_name == "goods-division")) { return false }
	if (case_name == "empty-distribution") { return setup_empty_distribution(pl) }
	if (case_name == "recipient-same") { return setup_recipient_same(pl) }
	if (case_name == "cargo-through" || case_name == "pickup-reload" || case_name == "through-pair") { return setup_cargo_through(pl) }
	if (case_name == "goods-division") { return setup_goods_division(pl) }
	if (case_name.slice(0, 6) == "flash-") { return setup_flash_load(pl) }
	return fail("unknown pick-up goods case " + case_name)
}

function observe()
{
	// flash-reload holds in this phase while the runner joins the client; the
	// observation limit must not count the hold.
	if (case_name == "flash-reload" && f.phase == "awaiting-reload")
	{
		return
	}
	if (case_name == "recipient-same")
	{
		observe_recipient_same()
	}
	else if (case_name == "empty-distribution")
	{
		observe_empty_distribution()
	}
	else if (case_name == "cargo-through" || case_name == "pickup-reload" || case_name == "through-pair")
	{
		if (case_name == "pickup-reload" && f.phase == "awaiting-reload") { return }
		observe_cargo_through()
	}
	else if (case_name == "goods-division")
	{
		observe_goods_division()
	}
	else if (case_name == "passenger-division")
	{
		observe_passenger_division()
	}
	else if (case_name.slice(0, 6) == "flash-")
	{
		observe_flash_load()
	}
	if (f.phase != "failed" && f.phase != "done" && ++f.steps > 15000)
	{
		fail("observation limit exceeded in phase " + f.phase)
	}
}

function resume_game()
{
	if (case_name == "flash-reload")
	{
		if (!("flash_reload" in persistent) || typeof(persistent.flash_reload) != "table")
		{
			fail("flash-load reload save contains no persisted fixture state")
			return
		}
		local state = persistent.flash_reload
		delete persistent.flash_reload
		f.setup_started = true
		f.depot = depot_x(1, 2, 0)
		f.collector_id = state.collector_id
		f.loose_since = state.loose_since
		f.goods = good_desc_x(state.goods_name)
		f.collector_started = true
		local collector = convoy_x(f.collector_id)
		if (!check(collector.is_valid() && f.depot.is_valid(), "flash-load reload object identities were not restored")) { return }
		local snap = collector.get_vehicle_snapshot()
		if (!check(snap.len() == 2 && snap[1].cargo > 0, "flash-load reload lost the collected cargo")) { return }
		if (!check(collector.get_current_loading_time() == state.expected_loading_time,
			"flash-load credit changed across the save/load: expected " + state.expected_loading_time + ", got " + collector.get_current_loading_time())) { return }
		f.loco = snap[0].desc
		f.wagon = snap[1].desc
		f.phase = "wait-for-flash-delivery"
		print("RUNTIME CASE " + case_name + " RESTORED")
		return
	}
	if (case_name != "pickup-reload") { return }
	if (!("pickup_reload" in persistent) || typeof(persistent.pickup_reload) != "table")
	{
		fail("pickup reload save contains no persisted fixture state")
		return
	}
	local state = persistent.pickup_reload
	delete persistent.pickup_reload
	f.setup_started = true
	f.depot = depot_x(1, 2, 0)
	f.collector_id = state.collector_id
	f.loose_donor_id = state.loose_donor_id
	f.source_revenue_at_detach = state.source_revenue
	f.released_cargo = state.released_cargo
	f.goods = good_desc_x(state.goods_name)
	local collector = convoy_x(f.collector_id)
	local loose = convoy_x(f.loose_donor_id)
	if (!check(collector.is_valid() && loose.is_valid() && f.depot.is_valid(), "pickup reload object identities were not restored")) { return }
	local collector_vehicles = collector.get_vehicle_snapshot()
	local loose_vehicles = loose.get_vehicle_snapshot()
	if (!check(collector_vehicles.len() == 1 && loose_vehicles.len() == 1, "pickup reload restored the wrong consist sizes")) { return }
	f.loco = collector_vehicles[0].desc
	f.wagon = loose_vehicles[0].desc
	local collector_line = collector.get_line()
	if (!check(collector_line != null && collector_line.is_valid(), "pickup reload lost the collecting line")) { return }
	if (!check(!loose_vehicles[0].loose_recipient_is_convoy && !loose_vehicles[0].loose_recipient_is_same && !loose_vehicles[0].loose_recipient_is_any
		&& loose_vehicles[0].loose_recipient_id == collector_line.id, "pickup reload lost the named collecting line")) { return }
	if (!check(loose_vehicles[0].cargo > 0, "pickup reload discharged or lost the loaded loose wagon")) { return }
	f.phase = "resume-start-collector"
	print("RUNTIME CASE " + case_name + " RESTORED")
}

function get_rule_text(pl) { return ttext("Do not touch.") }
function get_goal_text(pl) { return ttext("Exercise the pick-up goods runtime.") }
function get_info_text(pl) { return ttext("Pick-up goods runtime fixture.") }
function get_result_text(pl) { return get_info_text(pl) }

function is_scenario_completed(pl)
{
	if (pl != 0) { return 0 }
	++heartbeat_steps
	if ((heartbeat_steps % 200) == 0) { print("RUNTIME CASE " + case_name + ": heartbeat step " + heartbeat_steps) }
	if (!f.setup_started)
	{
		f.setup_started = true
		try { if (!setup()) { return -1 } }
		catch (e) { fail("setup raised an error: " + e); return -1 }
		return 0
	}
	if (f.phase == "failed") { return -1 }
	if (f.phase == "done") { return 100 }
	try { observe() }
	catch (e) { fail("observation raised an error in phase " + f.phase + ": " + e) }
	return f.phase == "done" ? 100 : (f.phase == "failed" ? -1 : 0)
}
