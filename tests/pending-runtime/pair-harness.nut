//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//
// Shared world builder and assertions for the paired H exchange runtime cases
// under tests/pending-runtime/pair-cases and for the reload scenarios in the
// pair-reload, pair-passenger-loose-reload, pair-freight-through-reload and
// pair-freight-loose-reload directories, which hold a committed exchange
// across a save and load.
//
// A case selects itself by setting ::case_kind before including this file. The
// runner scripts/run-pending-runtime-tests.sh stages the case's scenario.nut,
// this file and empty-16x16.sve in the pakset scenario directory and runs one
// game process per case, so every case starts from the empty world. The reload
// cases are staged instead by scripts/run-pending-runtime-reload-tests.ps1 or
// .sh, which additionally start a client so that the server saves, sends and
// loads its state.
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
// The cargo cases add a loading stop at (3,2) with a platform extension at
// (2,2), and a delivery halt at (14,2) with platforms at (11,2) to (14,2). The
// delivery halt lies beyond the halt walking radius (half the station coverage
// setting), so a passenger packet waits for a vehicle instead of walking there.
// A freight case also builds two consumer industries: one whose factory
// coverage includes the junction halt, and one whose coverage includes the
// delivery halt. The case injects its cargo at the loading stop with
// halt_x.add_cargo once the loading convoy has left the depot.
//

map.file = "empty-16x16.sve"

scenario.short_description = "Paired H exchange runtime case: " + ::case_kind
scenario.author = "Simutrans-Extended automated tests"
scenario.version = "0.1"

local case_name = ::case_kind

// The runner's stall watchdog uses log growth. Engine per-step logging is
// suppressed at low debug levels, so print a heartbeat that does not depend on
// the engine log level.
local heartbeat_steps = 0

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

// Cargo case entries: the receiver loads at the loading stop and, in the loose
// case, collects its released carriage at a second junction entry and unloads
// at a second delivery entry. The through case loads the carriage on the
// target before the junction.
local RECEIVER_LOAD_ENTRY_ID = 41
local RECEIVER_COLLECT_ENTRY_ID = 42
local RECEIVER_FINAL_ENTRY_ID = 43
local TARGET_LOAD_ENTRY_ID = 51

// Observation step limits. These are counts of scenario observations, not
// game-time quantities. The travel limit is wide because the first convoy may
// wait while the second is still in the depot; the departure limit covers the
// configured shunting delay.
local TRAVEL_STEP_LIMIT = 6000
local EXCHANGE_STEP_LIMIT = 1000
// The reload runner's server advances one simulation step per observation and
// the target's run to the junction takes longer in game time than the
// fast-forward runner reaches in the same observation count. The reload cases
// therefore use this wider limit for the wait for the counterpart; a non-cargo
// reload also uses it for the exchange.
local RELOAD_EXCHANGE_STEP_LIMIT = 8000
local DEPARTURE_STEP_LIMIT = 3000
// The reload runner sets shunting_time_seconds to 300 so that the case has not
// advanced while the client joins; the post-reload departure limit must cover
// that delay. A cargo reload also uses this value for its exchange wait,
// because the goods loading dwell at the loading stop is of the same order as
// that delay.
local RELOAD_DEPARTURE_STEP_LIMIT = 16000
local PREPARATION_STEP_LIMIT = 3000
// Cargo cases observe a loaded convoy through the junction, along the delivery
// branch and back to the junction for a collection.
local CARGO_STEP_LIMIT = 6000

// Cargo fixture stops. LOAD_POS is on the trunk before the junction and
// LOAD_EXTENSION_POS is its westward platform extension, so both vehicles of a
// two-vehicle consist stand at the platform. DELIVER_POS is beyond the east
// stop; the delivery halt covers DELIVER_PLATFORM_POSITIONS so every vehicle of
// a three-vehicle consist unloads there.
local LOAD_POS = coord3d(3, 2, 0)
local LOAD_EXTENSION_POS = coord3d(2, 2, 0)
local DELIVER_POS = coord3d(14, 2, 0)
local DELIVER_PLATFORM_POSITIONS = [coord3d(11, 2, 0), coord3d(12, 2, 0), coord3d(13, 2, 0)]
// Passenger packets are delivered to the delivery station building, which is
// the destination position the halt checks. Freight is delivered to a consumer
// industry: the junction consumer receives goods whose destination is the
// junction halt, and the delivery consumer receives goods carried onward.
local DELIVER_STOP_POS = coord(14, 2)
local JUNCTION_CONSUMER_POS = coord3d(8, 5, 0)
local JUNCTION_CONSUMER_ZIELPOS = coord(8, 5)
local DELIVER_CONSUMER_POS = coord3d(12, 5, 0)
local DELIVER_CONSUMER_ZIELPOS = coord(12, 5)

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
	cargo = null,
	cargo_amount = 0,
	cargo_class = 0,
	cargo_consumer_name = "",
	cargo_steps = 0,
	cargo_revenue_before_final = 0,
	cargo_junction_pending = false,
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


function halt_at(pos)
{
	return halt_x.get_halt(pos, player_x(0))
}


// Build a consumer industry. The parameter string means: ignore climate,
// ignore region, rotation 1, a large initial production, then the industry
// name. The pick-up goods fixture uses the same string.
function build_factory(pl, pos, name)
{
	return command_x(tool_build_factory).work(pl, pos, "1111024000," + name)
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
	if (is_cargo_case())
	{
		if (!check_tool(command_x.build_way(pl, EAST_POS, DELIVER_POS, rail, false), "build the delivery branch")) { return false }
	}

	if (!check_tool(command_x.build_depot(pl, coord3d(1, 2, 0), depot_desc_for(wt_rail)), "build rail depot")) { return false }
	local depot = depot_x(1, 2, 0)
	if (!check(depot != null, "depot not found after building")) { return false }
	fixture.depot = depot

	local station = null
	if (is_cargo_case() && !is_passenger_cargo_case())
	{
		// A freight-enabled stop can still have no storage for freight (a bare
		// goods siding has capacity zero). Choose the available freight stop with
		// the largest storage capacity, as the pick-up goods fixture does.
		foreach (st in building_desc_x.get_available_stations(building_desc_x.station, wt_rail, fixture.cargo))
		{
			if (st.enables_freight() && st.get_capacity() > 0
				&& (station == null || st.get_capacity() > station.get_capacity()))
			{
				station = st
			}
		}
		if (!check(station != null, "no freight stop with storage capacity is available")) { return false }
	}
	else
	{
		station = building_desc_x.get_available_stations(building_desc_x.station, wt_rail, good_desc_x.passenger)[0]
		if (!check(station != null, "no rail station available")) { return false }
	}
	if (!check_tool(station_builder(pl, MAIN_POS, station), "build main junction platform")) { return false }
	if (!check_tool(station_builder(pl, LOOP_POS, station), "build loop junction platform")) { return false }
	if (!check_tool(station_builder(pl, EAST_POS, station), "build east stop")) { return false }
	if (!check_tool(station_builder(pl, NORTH_POS, station), "build north stop")) { return false }
	if (is_cargo_case())
	{
		if (!check_tool(station_builder(pl, LOAD_POS, station), "build the cargo loading platform")) { return false }
		if (!check_tool(station_builder(pl, LOAD_EXTENSION_POS, station), "build the cargo loading platform extension")) { return false }
		if (!check_tool(station_builder(pl, DELIVER_POS, station), "build the cargo delivery platform")) { return false }
		foreach (pos in DELIVER_PLATFORM_POSITIONS)
		{
			if (!check_tool(station_builder(pl, pos, station), "build a cargo delivery platform extension")) { return false }
		}
	}

	local main_halt = halt_x.get_halt(MAIN_POS, pl)
	local loop_halt = halt_x.get_halt(LOOP_POS, pl)
	if (!check(main_halt != null && loop_halt != null, "a junction platform has no halt")) { return false }
	if (!check(main_halt.get_name() == loop_halt.get_name(), "the junction main and loop platforms have different stop names, so they are separate halts and cannot form a pair")) { return false }

	if (!is_cargo_case()) { return true }

	local load_halt = halt_at(LOAD_POS)
	if (!check(load_halt != null && halt_at(LOAD_EXTENSION_POS) != null
		&& load_halt.get_name() == halt_at(LOAD_EXTENSION_POS).get_name(), "the cargo loading platform and its extension are separate halts, so a two-vehicle consist is only partly at the platform")) { return false }

	if (!is_passenger_cargo_case())
	{
		// The junction consumer must cover the junction halt but not the loading
		// halt: a packet whose destination position is a factory in the loading
		// halt's list would be consumed there instead of boarding.
		if (!check_tool(build_factory(pl, JUNCTION_CONSUMER_POS, fixture.cargo_consumer_name), "build the junction consumer " + fixture.cargo_consumer_name)) { return false }
		if (!check(main_halt.get_factory_list().len() > 0, "the junction consumer does not cover the junction halt")) { return false }
		if (!check(load_halt.get_factory_list().len() == 0, "the junction consumer covers the loading halt, so goods bound for the junction would be consumed before boarding")) { return false }
		if (!check_tool(build_factory(pl, DELIVER_CONSUMER_POS, fixture.cargo_consumer_name), "build the delivery consumer " + fixture.cargo_consumer_name)) { return false }
		if (!check(halt_at(DELIVER_POS).get_factory_list().len() > 0, "the delivery consumer does not cover the delivery halt")) { return false }
		if (!check(load_halt.get_factory_list().len() == 0, "the delivery consumer covers the loading halt")) { return false }
	}

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


// Cargo case names. A cargo case loads one carriage before the junction and
// asserts what happens to its load when the paired exchange moves or releases
// that carriage. The three cargo reload cases repeat a cargo shape with a save
// and load between the exchange and the carriage's delivery; case_shape() maps
// such a name to the non-reload shape that selects the fixture, the schedules
// and the observations. Every other case returns its own name.
function case_shape()
{
	if (case_name == "pair-passenger-loose-reload") { return "pair-passenger-loose" }
	if (case_name == "pair-freight-through-reload") { return "pair-freight-through" }
	if (case_name == "pair-freight-loose-reload") { return "pair-freight-loose" }
	return case_name
}


// True for the cases that print READY, hold while a network client joins, and
// re-assert the committed exchange after the server loads the save written by
// the join. The fresh offline load of that save runs the same code.
function is_reload_case()
{
	return case_name == "pair-reload" || case_name == "pair-passenger-loose-reload"
		|| case_name == "pair-freight-through-reload" || case_name == "pair-freight-loose-reload"
}


function is_cargo_case()
{
	local shape = case_shape()
	return shape == "pair-passenger-through" || shape == "pair-passenger-loose"
		|| shape == "pair-freight-through" || shape == "pair-freight-loose"
}


// In a loose cargo case the receiver's own carriage is released at the junction
// and collected on the receiver's return visit. In a through cargo case the
// target's carriage is transferred directly to the receiver.
function is_loose_cargo_case()
{
	local shape = case_shape()
	return shape == "pair-passenger-loose" || shape == "pair-freight-loose"
}


function is_passenger_cargo_case()
{
	local shape = case_shape()
	return shape == "pair-passenger-through" || shape == "pair-passenger-loose"
}


// Collect the powered vehicles with no cargo capacity and the trailers that
// carry one good. A locomotive with no cargo capacity cannot take any of the
// injected packets, so every packet reaches a carriage whose load the case
// asserts.
function cargo_stock_lists(good)
{
	local locos = []
	local trailers = []
	foreach (desc in vehicle_desc_x.get_available_vehicles(wt_rail))
	{
		if (desc.needs_electrification() || desc.get_successors().len() > 0) { continue }
		if (desc.get_power() > 0)
		{
			if (desc.get_total_capacity() == 0 && desc.can_be_first() && desc.can_be_last())
			{
				locos.append(desc)
			}
			continue
		}
		if (!desc.can_be_last() || desc.get_total_capacity() == 0) { continue }
		local freight = desc.get_freight()
		if (freight != null && freight.is_equal(good)) { trailers.append(desc) }
	}
	return { locos = locos, trailers = trailers }
}


// The best carriage pair for one good. The two carriages must couple to their
// own locomotive, to each other, and to the receiver's locomotive in the
// collected order [loco, coach_b, coach_a]. The loading consist must fit the
// loading halt's two platform tiles: a vehicle length is in car units and a
// tile holds 16 of them. The pair with the largest capacity for the case's
// cargo class wins.
function best_cargo_pair(good, passenger)
{
	local lists = cargo_stock_lists(good)
	local locos = lists.locos
	local trailers = lists.trailers
	if (locos.len() == 0 || trailers.len() == 0) { return null }

	local result = null
	for (local distinct = 1; distinct >= 0 && result == null; distinct--)
	{
		foreach (a in trailers)
		{
			foreach (b in trailers)
			{
				if (distinct == 1 && a.is_equal(b)) { continue }
				if (!coupons(b, a)) { continue }

				local pair_capacity = 0
				local pair_class = 0
				if (passenger)
				{
					// Use the lowest passenger class with room for two packets.
					pair_class = -1
					for (local c = 0; c < 5; c++)
					{
						local class_capacity = min(a.get_capacity(c), b.get_capacity(c))
						if (class_capacity < 2) { continue }
						pair_class = c
						pair_capacity = class_capacity
						break
					}
					if (pair_class < 0) { continue }
				}
				else
				{
					pair_capacity = min(a.get_capacity(0), b.get_capacity(0))
				}
				if (pair_capacity < 2) { continue }
				if (result != null && pair_capacity <= result.capacity) { continue }

				local this_loco_a = null
				local this_loco_b = null
				foreach (loco in locos)
				{
					if (!coupons(loco, a) || !coupons(loco, b)) { continue }
					if (loco.get_length() + a.get_length() > 32) { continue }
					if (this_loco_a == null)
					{
						this_loco_a = loco
						continue
					}
					if (!loco.is_equal(this_loco_a))
					{
						this_loco_b = loco
						break
					}
				}
				if (this_loco_a == null) { continue }
				if (this_loco_b == null)
				{
					// One locomotive descriptor can serve both consists when it also
					// couples to the target's carriage.
					if (!coupons(this_loco_a, b) || this_loco_a.get_length() + b.get_length() > 32) { continue }
					this_loco_b = this_loco_a
				}

				result = {
					loco_a = this_loco_a,
					loco_b = this_loco_b,
					coach_a = a,
					coach_b = b,
					cargo_class = pair_class,
					capacity = pair_capacity
				}
			}
		}
	}
	return result
}


// Select a consumer industry input good that an available freight wagon can
// carry. A consumer with no output is preferred, because it cannot generate
// cargo of its own. Among the no-output candidates the good with the largest
// couplable carriage wins, so that a freight fare stays visible after the
// financial history converts cents to whole simcents.
function select_freight_goods()
{
	local silent_name = ""
	local silent_good = null
	local silent_capacity = 0
	local any_name = ""
	local any_good = null
	foreach (name, desc in factory_desc_x.get_list())
	{
		local no_output = desc.get_outputs().len() == 0
		foreach (input in desc.get_inputs())
		{
			if (input.good == null) { continue }
			local pair = best_cargo_pair(input.good, false)
			// Two packets of at least two units must fit one carriage, so that the
			// fare stays visible after the financial history converts cents.
			if (pair == null || pair.capacity < 4) { continue }
			if (no_output)
			{
				local better = pair.capacity > silent_capacity
				if (!better && pair.capacity == silent_capacity && silent_name != "")
				{
					// Deterministic choice between equally large candidates.
					better = name < silent_name
				}
				if (better)
				{
					silent_capacity = pair.capacity
					silent_name = name
					silent_good = input.good
				}
			}
			if (any_good == null)
			{
				any_name = name
				any_good = input.good
			}
		}
	}
	if (silent_good != null)
	{
		fixture.cargo = silent_good
		fixture.cargo_consumer_name = silent_name
		return true
	}
	if (!check(any_good != null, "no consumer industry input has a couplable freight wagon with room for two packets")) { return false }
	fixture.cargo = any_good
	fixture.cargo_consumer_name = any_name
	return true
}


// Commit the selected cargo vehicles and the packet amount. Two packets must
// fit the carrying carriage. A freight load must also be large enough that the
// fare remains visible after the financial history converts cents to whole
// simcents; a passenger load is kept small so the loading dwell stays short.
function select_cargo_stock()
{
	local pair = best_cargo_pair(fixture.cargo, is_passenger_cargo_case())
	if (!check(pair != null, "no pair of rail carriages with the required couplings is available")) { return false }

	fixture.loco_a = pair.loco_a
	fixture.loco_b = pair.loco_b
	fixture.coach_a = pair.coach_a
	fixture.coach_b = pair.coach_b
	fixture.cargo_class = pair.cargo_class
	if (is_passenger_cargo_case())
	{
		fixture.cargo_amount = min(pair.capacity / 2, 8)
	}
	else
	{
		// At least two units, so the fare remains visible after the financial
		// history converts cents to whole simcents; the cap keeps the loading
		// dwell short.
		fixture.cargo_amount = max(2, min(pair.capacity / 2, 8))
		if (!check(2 * fixture.cargo_amount <= pair.capacity, "the selected carriage cannot hold two packets of " + fixture.cargo_amount + " units")) { return false }
	}
	print("RUNTIME CASE " + case_name + ": cargo " + fixture.cargo.get_name() + ", class " + pair.cargo_class
		+ ", carriage capacity " + pair.capacity + ", packet amount " + fixture.cargo_amount)
	return true
}


function make_entry(pos, unique_entry_id)
{
	local entry = schedule_entry_x(pos, 0, 0)
	entry.unique_entry_id = unique_entry_id
	return entry
}


// Slot specifications are arrays of alternatives. A descriptor is a specific
// vehicle alternative; a table with min_topspeed is a rule alternative. A table
// with optional_slot set contributes no alternative and makes the slot optional.
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
				if ("optional_slot" in spec && spec.optional_slot)
				{
					check(alternatives.len() == 0, "an optional-empty slot cannot also have alternatives")
					slot.optional = true
					continue
				}
				has_rule = true
				// A rule alternative must be enforced; the element default is a
				// wildcard alternative.
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


// An order whose slots declare their own cargo category. The schedule's
// carried-category tables (built by schedule_t::parse_orders), which the path
// explorer reads when it builds connexions for a lineless convoy, are derived
// from the order slots, so a slot left at the consist_order_element_x default
// category would make the service appear not to carry the case's cargo.
function make_cargo_order(descs)
{
	local order = consist_order_x()
	local slots = []
	foreach (desc in descs)
	{
		local slot = consist_order_element_x()
		local alternative = vehicle_description_x()
		alternative.specific_vehicle = desc
		local freight = desc.get_freight()
		if (freight != null)
		{
			slot.catg = freight.get_catg_index()
		}
		slot.alternatives = [alternative]
		slots.append(slot)
	}
	order.slots = slots
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
	if (is_cargo_case())
	{
		return [[fixture.loco_a], [fixture.coach_b]]
	}
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
		// loco_c is not present. The solver must omit the optional slot without
		// moving either mandatory vehicle or applying a shunting delay.
		return [[fixture.loco_a], [{ optional_slot = true }], [fixture.coach_a]]
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
	if (is_cargo_case())
	{
		return [[fixture.loco_b]]
	}
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
		return [[fixture.loco_b], [{ optional_slot = true }], [fixture.coach_b]]
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
	if (is_cargo_case())
	{
		return [fixture.loco_a, fixture.coach_b]
	}
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
	if (is_cargo_case())
	{
		return [fixture.loco_b]
	}
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
	if (is_cargo_case())
	{
		// The receiver's own carriage leaves both cargo case shapes: the loose
		// case keeps the loaded one there for collection, and the through case's
		// carriage is empty.
		return [fixture.coach_a]
	}
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
// after the load; the fingerprints re-assert their final consists. A cargo
// reload also stores the cargo descriptor's name and the packet amount, so that
// the post-load phases can read the carriage load and the cargo name.
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
		leftovers = leftover_fingerprints,
		cargo_name = is_cargo_case() ? fixture.cargo.get_name() : "",
		cargo_amount = is_cargo_case() ? fixture.cargo_amount : 0,
		cargo_class = is_cargo_case() ? fixture.cargo_class : 0
	}
}


// Persists the committed exchange and holds the case until the runner has
// joined a client, the server has loaded the save written by the join, and
// resume_game() has restored the fixture.
function hold_for_reload()
{
	remember_pair_reload_state()
	fixture.phase = "awaiting-reload"
	print("RUNTIME CASE " + case_name + " READY")
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


// Re-asserts the cargo that the case held at the reload point. A directly
// transferred carriage is still on the receiver with its whole load. A released
// carriage is still laid over at the junction with only the packet or goods
// that continue beyond the junction: the passenger or goods whose journey ended
// at the junction left the carriage before the hold. For freight, the laid-over
// convoy keeps the fare it booked for the goods delivered at the junction.
function check_cargo_restored()
{
	if (!is_cargo_case())
	{
		return true
	}
	if (is_loose_cargo_case())
	{
		local loose = laid_over_consist()
		if (!check(loose.len() == 1, "the junction holds " + loose.len() + " non-empty laid-over convoys after the reload, expected the released carriage")) { return false }
		local load = consist_total_cargo(loose[0])
		if (!check(load == fixture.cargo_amount, "the released carriage holds " + load + " units after the reload, expected the " + fixture.cargo_amount + " units bound beyond the junction")) { return false }
		if (!is_passenger_cargo_case())
		{
			if (!check(convoy_total_revenue(loose[0]) > 0, "the goods delivered at the junction before the reload did not keep their booked fare")) { return false }
		}
		return true
	}
	local load = consist_total_cargo(receiver_convoy())
	return check(load == fixture.cargo_amount, "the transferred carriage holds " + load + " units after the reload, expected the " + fixture.cargo_amount + " units it carried into the junction")
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


// The cargo receiver stops at the loading stop before the junction. In the
// loose case it also returns to the junction for the carriage it released and
// then carries it to the delivery halt again.
function commit_cargo_receiver_schedule(pl, receiver, target)
{
	local loose = is_loose_cargo_case()
	local entries = []
	if (loose)
	{
		entries.append(make_entry(LOAD_POS, RECEIVER_LOAD_ENTRY_ID))
	}
	entries.append(make_entry(MAIN_POS, RECEIVER_ENTRY_ID))
	entries.append(make_entry(DELIVER_POS, RECEIVER_NEXT_ENTRY_ID))
	if (loose)
	{
		entries.append(make_entry(MAIN_POS, RECEIVER_COLLECT_ENTRY_ID))
		entries.append(make_entry(DELIVER_POS, RECEIVER_FINAL_ENTRY_ID))
	}

	local pair_index = loose ? 1 : 0
	entries[pair_index].couple = true
	entries[pair_index].couple_target_is_line_or_cnv = true
	entries[pair_index].target_id_couple = target.id
	entries[pair_index].target_unique_entry_couple = TARGET_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(RECEIVER_ENTRY_ID, make_cargo_order([fixture.loco_a, fixture.coach_b]))
	if (loose)
	{
		schedule.orders.rawset(RECEIVER_COLLECT_ENTRY_ID, make_cargo_order([fixture.loco_a, fixture.coach_b, fixture.coach_a]))
	}
	if (!check_tool(receiver.change_schedule(pl, schedule), "commit the receiver convoy schedule")) { return false }

	local back = receiver.get_schedule()
	if (!check(back != null && back.entries.len() == entries.len(), "the receiver schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected " + entries.len())) { return false }
	if (!check(back.entries[pair_index].unique_entry_id == RECEIVER_ENTRY_ID, "the receiver schedule has no pair entry " + RECEIVER_ENTRY_ID + " at position " + pair_index)) { return false }
	if (!check(back.entries[pair_index].couple == true, "receiver schedule read-back: the couple flag is not set")) { return false }
	if (!check(back.entries[pair_index].uncouple == false, "receiver schedule read-back: the uncouple flag is set")) { return false }
	if (!check(back.entries[pair_index].target_id_couple == target.id, "receiver schedule read-back: target id is " + back.entries[pair_index].target_id_couple + ", expected " + target.id)) { return false }
	if (!check(back.entries[pair_index].target_unique_entry_couple == TARGET_ENTRY_ID, "receiver schedule read-back: target entry is not " + TARGET_ENTRY_ID)) { return false }
	if (!check(RECEIVER_ENTRY_ID in back.orders, "receiver schedule read-back: no order at entry " + RECEIVER_ENTRY_ID)) { return false }
	if (loose)
	{
		if (!check(back.entries[3].unique_entry_id == RECEIVER_COLLECT_ENTRY_ID, "the receiver schedule has no collection entry " + RECEIVER_COLLECT_ENTRY_ID + " fourth")) { return false }
		if (!check(RECEIVER_COLLECT_ENTRY_ID in back.orders, "receiver schedule read-back: no order at entry " + RECEIVER_COLLECT_ENTRY_ID)) { return false }
	}
	return true
}


// The cargo target stops at the loading stop before the junction in the through
// case, so its own carriage is loaded before it reaches the pair entry. The
// loose case leaves the loading to the receiver.
function commit_cargo_target_schedule(pl, receiver, target)
{
	local loose = is_loose_cargo_case()
	local entries = []
	if (!loose)
	{
		entries.append(make_entry(LOAD_POS, TARGET_LOAD_ENTRY_ID))
	}
	entries.append(make_entry(LOOP_POS, TARGET_ENTRY_ID))
	entries.append(make_entry(NORTH_POS, TARGET_NEXT_ENTRY_ID))

	local pair_index = loose ? 0 : 1
	entries[pair_index].uncouple = true
	entries[pair_index].uncouple_target_is_line_or_cnv = true
	entries[pair_index].target_id_uncouple = receiver.id
	entries[pair_index].target_unique_entry_uncouple = RECEIVER_ENTRY_ID

	local schedule = schedule_x(wt_rail, entries)
	schedule.orders.rawset(TARGET_ENTRY_ID, make_cargo_order([fixture.loco_b]))
	if (!loose)
	{
		// The loading entry declares the carriage the target carries there, so
		// the schedule's carried-category tables include the case's cargo for
		// the path explorer. The order restates the consist and changes nothing.
		schedule.orders.rawset(TARGET_LOAD_ENTRY_ID, make_cargo_order([fixture.loco_b, fixture.coach_b]))
	}
	if (!check_tool(target.change_schedule(pl, schedule), "commit the target convoy schedule")) { return false }

	local back = target.get_schedule()
	if (!check(back != null && back.entries.len() == entries.len(), "the target schedule has " + (back == null ? 0 : back.entries.len()) + " entries, expected " + entries.len())) { return false }
	if (!check(back.entries[pair_index].unique_entry_id == TARGET_ENTRY_ID, "the target schedule has no pair entry " + TARGET_ENTRY_ID + " at position " + pair_index)) { return false }
	if (!check(back.entries[pair_index].uncouple == true, "target schedule read-back: the uncouple flag is not set")) { return false }
	if (!check(back.entries[pair_index].couple == false, "target schedule read-back: the couple flag is set")) { return false }
	if (!check(back.entries[pair_index].target_id_uncouple == receiver.id, "target schedule read-back: target id is " + back.entries[pair_index].target_id_uncouple + ", expected " + receiver.id)) { return false }
	if (!check(back.entries[pair_index].target_unique_entry_uncouple == RECEIVER_ENTRY_ID, "target schedule read-back: target entry is not " + RECEIVER_ENTRY_ID)) { return false }
	if (!check(TARGET_ENTRY_ID in back.orders, "target schedule read-back: no order at entry " + TARGET_ENTRY_ID)) { return false }
	if (!loose && !check(TARGET_LOAD_ENTRY_ID in back.orders, "target schedule read-back: no order at the loading entry " + TARGET_LOAD_ENTRY_ID)) { return false }
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

	if (is_cargo_case())
	{
		if (is_passenger_cargo_case())
		{
			fixture.cargo = good_desc_x.passenger
		}
		else if (!select_freight_goods())
		{
			return false
		}
		if (!select_cargo_stock()) { return false }
		if (!build_infrastructure(pl)) { return false }

		local cargo_receiver = create_depot_convoy(pl, receiver_consist(), "receiver convoy")
		if (cargo_receiver == null) { return false }
		fixture.receiver_id = cargo_receiver.id
		local cargo_target = create_depot_convoy(pl, target_consist(), "target convoy")
		if (cargo_target == null) { return false }
		fixture.target_id = cargo_target.id
		if (!commit_cargo_receiver_schedule(pl, cargo_receiver, cargo_target)) { return false }
		if (!commit_cargo_target_schedule(pl, cargo_receiver, cargo_target)) { return false }
		// The loose case starts the target first, so the receiver meets a waiting
		// counterpart on its arrival at the junction. The exchange then commits
		// during that arrival pass, before the receiver's own deferred unload
		// could repair the junction-transfer packet's route. The through case
		// starts the receiver first, so the target can load before the junction.
		fixture.first_kind = is_loose_cargo_case() ? "target" : "receiver"
		fixture.phase = "start-first"
		return true
	}

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


// The total load aboard a convoy, from the public vehicle snapshot.
function consist_total_cargo(cnv)
{
	local total = 0
	foreach (slot in cnv.get_vehicle_snapshot())
	{
		total += slot.cargo
	}
	return total
}


// The vehicle of the given descriptor that carries the stated amount, or null.
// The loose cases use this rather than a fixed consist position, because the
// collected carriage and the receiver's own carriage can share a descriptor.
function consist_vehicle_with_cargo(cnv, desc, amount)
{
	foreach (slot in cnv.get_vehicle_snapshot())
	{
		if (vehicle_matches(slot.desc, desc) && slot.cargo == amount) { return slot }
	}
	return null
}


// The delivery position the case's packets carry: a station tile for
// passengers and a consumer industry tile for freight.
function delivery_zielpos()
{
	return is_passenger_cargo_case() ? DELIVER_STOP_POS : DELIVER_CONSUMER_ZIELPOS
}


// The load of the first vehicle of the given descriptor, or -1 when the convoy
// does not hold one. Used in failure messages.
function consist_cargo_of(cnv, desc)
{
	foreach (slot in cnv.get_vehicle_snapshot())
	{
		if (vehicle_matches(slot.desc, desc)) { return slot.cargo }
	}
	return -1
}


// Total booked revenue over the convoy's whole history table. Month indices
// shift during a run, so the cases compare totals rather than one month.
function convoy_total_revenue(cnv)
{
	local total = 0
	foreach (value in cnv.get_revenue())
	{
		total += value
	}
	return total
}


// Inject one cargo packet at the loading stop. The packet's next transfer is
// the halt at next_transfer_pos, its ultimate destination is the halt at
// destination_pos, and its delivery position is destination_zielpos: a factory
// tile for freight and a station tile for passengers.
function inject_packet(next_transfer_pos, destination_pos, destination_zielpos, amount)
{
	local load_halt = halt_at(LOAD_POS)
	local destination = halt_at(destination_pos)
	local next_transfer = halt_at(next_transfer_pos)
	if (!check(load_halt != null && destination != null && next_transfer != null, "a cargo fixture halt is missing")) { return false }
	local ok = load_halt.add_cargo(fixture.cargo, amount, destination, next_transfer, world.get_time().ticks, load_halt, destination_zielpos, fixture.cargo_class)
	return check_tool(ok, "inject " + amount + " units of cargo at the loading stop")
}


// Injects the cargo the case carries, once the loading convoy has left the
// depot. The loose case puts two packets in the receiver's carriage: one whose
// next transfer is the exchange stop, and one whose continuation lies beyond
// it. The through case puts one packet in the target's carriage; its next
// transfer is the north stop on the donor's own schedule, and the exchange
// transfers the carriage to an owner that serves its ultimate destination.
function inject_case_cargo(loose)
{
	if (is_passenger_cargo_case())
	{
		if (loose)
		{
			if (!inject_packet(MAIN_POS, DELIVER_POS, DELIVER_STOP_POS, fixture.cargo_amount)) { return false }
			return inject_packet(DELIVER_POS, DELIVER_POS, DELIVER_STOP_POS, fixture.cargo_amount)
		}
		return inject_packet(NORTH_POS, DELIVER_POS, DELIVER_STOP_POS, fixture.cargo_amount)
	}
	if (loose)
	{
		// The first packet is delivered at the junction when the wagon is left
		// loose; the second is carried beyond the junction under the through
		// wagon model.
		if (!inject_packet(MAIN_POS, MAIN_POS, JUNCTION_CONSUMER_ZIELPOS, fixture.cargo_amount)) { return false }
		return inject_packet(MAIN_POS, DELIVER_POS, DELIVER_CONSUMER_ZIELPOS, fixture.cargo_amount)
	}
	return inject_packet(NORTH_POS, DELIVER_POS, DELIVER_CONSUMER_ZIELPOS, fixture.cargo_amount)
}


function cargo_step(what)
{
	++fixture.cargo_steps
	// The reload runner sets shunting_time_seconds to 300, so the first
	// post-load cargo phase must outlast the remaining shunting delay as well as
	// the run to the delivery stop.
	local limit = is_reload_case() ? RELOAD_DEPARTURE_STEP_LIMIT : CARGO_STEP_LIMIT
	if (fixture.cargo_steps > limit)
	{
		fail(what + " within " + limit + " observations")
	}
}


// Both cargo shapes load their carriage on the convoy that the case starts
// second: the target in the through case and the receiver in the loose case.
function observe_cargo_injection()
{
	local loader = second_convoy()
	if (!check(loader.is_valid(), "the convoy that must load the cargo is not valid")) { return }
	local snapshot = loader.get_vehicle_snapshot()
	if (!check(snapshot.len() > 0, "the loading convoy has no vehicles")) { return }
	// The depot is at (1,2). Inject once the leading vehicle has left the depot
	// tile, so the routing rebuild caused by the departure has settled and the
	// packets are present before the loading stop.
	if (snapshot[0].pos.x < 2)
	{
		cargo_step("the convoy did not leave the depot to be loaded")
		return
	}
	if (!inject_case_cargo(is_loose_cargo_case())) { return }
	fixture.phase = "detect-exchange"
}


// Asserts the load immediately after the paired exchange. A directly
// transferred carriage keeps its load. A released carriage keeps the packet
// that continues beyond the junction; a passenger packet whose next transfer is
// the junction leaves it, and a goods packet whose destination is the junction
// is delivered there.
function check_cargo_after_exchange()
{
	if (!is_loose_cargo_case())
	{
		local receiver = receiver_convoy()
		if (!check(consist_vehicle_with_cargo(receiver, fixture.coach_b, fixture.cargo_amount) != null,
			"the transferred carriage does not hold the " + fixture.cargo_amount + " units it carried into the junction")) { return false }
		return true
	}

	local loose = laid_over_consist()
	if (!check(loose.len() == 1, "the junction holds " + loose.len() + " non-empty laid-over convoys, expected the released carriage")) { return false }
	print("RUNTIME CASE " + case_name + ": released carriage load " + consist_cargo_of(loose[0], fixture.coach_a) + " of " + fixture.cargo_amount
		+ " bound beyond the junction; receiver load " + consist_total_cargo(receiver_convoy())
		+ ", junction waiting " + halt_at(MAIN_POS).get_freight_to_dest(fixture.cargo, delivery_zielpos()))
	if (!check(consist_vehicle_with_cargo(loose[0], fixture.coach_a, fixture.cargo_amount) != null,
		"the released carriage holds " + consist_cargo_of(loose[0], fixture.coach_a) + " units, not the " + fixture.cargo_amount + " units bound beyond the junction")) { return false }
	if (!is_passenger_cargo_case())
	{
		// The goods bound for the junction were delivered there, so the released
		// convoy booked their fare.
		return check(convoy_total_revenue(loose[0]) > 0, "the released goods wagon did not book the fare for its delivery at the junction")
	}
	return true
}


// Observes the carriage after the exchange. A through carriage is delivered by
// the receiver at the delivery halt. A loose carriage is collected by the
// receiver at the junction; the packet that continues beyond the junction must
// survive the wait and the collection, and the receiver must book the fare when
// it unloads the carriage at the delivery halt.
function observe_cargo()
{
	local receiver = receiver_convoy()
	if (!check(receiver.is_valid(), "the receiver convoy disappeared during the cargo observation")) { return }

	if (fixture.phase == "cargo-through-delivery")
	{
		if (receiver.get_current_entry_id() != RECEIVER_NEXT_ENTRY_ID)
		{
			cargo_step("the receiver did not reach the delivery stop")
			return
		}
		if (consist_total_cargo(receiver) != 0)
		{
			cargo_step("the receiver did not unload the transferred carriage at the delivery stop")
			return
		}
		print("RUNTIME CASE " + case_name + ": delivered carriage load " + consist_total_cargo(receiver)
			+ ", receiver revenue " + convoy_total_revenue(receiver)
			+ ", delivery halt waiting " + halt_at(DELIVER_POS).get_freight_to_dest(fixture.cargo, delivery_zielpos()))
		if (!check(convoy_total_revenue(receiver) > 0, "the delivery of the transferred carriage did not book a fare")) { return }
		pass()
		return
	}

	if (fixture.phase == "cargo-loose-first")
	{
		if (receiver.get_current_entry_id() != RECEIVER_NEXT_ENTRY_ID)
		{
			cargo_step("the receiver did not reach the delivery stop")
			return
		}
		if (is_passenger_cargo_case())
		{
			// The passenger whose next transfer was the junction either boarded
			// the receiver's final consist and was delivered here, or is still
			// waiting at the junction for a later service. A reload case allows a
			// third outcome: the long shunting hold lets the passenger exceed its
			// maximum tolerable wait and leave the junction before the receiver
			// departs.
			local carried = convoy_total_revenue(receiver) > 0
			local main_halt = halt_at(MAIN_POS)
			local waiting = main_halt != null
				&& main_halt.get_freight_to_dest(fixture.cargo, DELIVER_STOP_POS) >= fixture.cargo_amount
			if (!carried && !waiting)
			{
				if (consist_total_cargo(receiver) > 0)
				{
					cargo_step("the receiver's final consist did not deliver or discharge the passenger whose next transfer was the junction")
					return
				}
				if (!is_reload_case())
				{
					fail("the passenger whose next transfer was the junction neither boarded the receiver's final consist nor waited at the junction")
					return
				}
				// The reload runner holds the receiver at the junction for 300
				// game seconds of shunting time, which exceeds the passenger's
				// maximum tolerable wait. Such a passenger leaves the junction
				// before the receiver departs, so only the carriage's through
				// load is collected below.
				print("RUNTIME CASE " + case_name + ": junction-transfer packet left the junction while the receiver waited out the shunting delay")
			}
			else
			{
				print("RUNTIME CASE " + case_name + ": junction-transfer packet "
					+ (carried ? "carried by the receiver's final consist" : "waiting at the junction"))
			}
			fixture.cargo_junction_pending = !carried && waiting
		}
		// The packet whose continuation lies beyond the junction must still be
		// aboard the released carriage after the receiver's first delivery call.
		local loose = laid_over_consist()
		if (!check(loose.len() == 1, "the junction holds " + loose.len() + " non-empty laid-over convoys, expected the released carriage")) { return }
		if (!check(consist_vehicle_with_cargo(loose[0], fixture.coach_a, fixture.cargo_amount) != null,
			"the released carriage lost the cargo bound beyond the junction while the receiver called at the delivery stop")) { return }
		fixture.cargo_steps = 0
		fixture.phase = "cargo-loose-collect"
		return
	}

	if (fixture.phase == "cargo-loose-collect")
	{
		local vehicles = receiver.get_vehicles().len()
		if (vehicles < 3)
		{
			cargo_step("the receiver did not collect the released carriage at the junction")
			return
		}
		if (!check(vehicles == 3, "the receiver holds " + vehicles + " vehicles after the collection, expected 3")) { return }
		// The released carriage brings its through load. A goods packet bound for
		// the junction was already delivered there, while a passenger whose next
		// transfer was the junction boards here if it is still waiting at the
		// junction.
		local expected_load = fixture.cargo_amount
		if (is_passenger_cargo_case() && fixture.cargo_junction_pending)
		{
			expected_load += fixture.cargo_amount
		}
		print("RUNTIME CASE " + case_name + ": collected carriage load " + consist_cargo_of(receiver, fixture.coach_a)
			+ ", receiver load " + consist_total_cargo(receiver) + ", expected " + expected_load)
		if (!check(consist_total_cargo(receiver) == expected_load,
			"the receiver holds " + consist_total_cargo(receiver) + " units after the collection, expected " + expected_load + ": the released carriage's through load was not preserved")) { return }
		fixture.cargo_revenue_before_final = convoy_total_revenue(receiver)
		fixture.cargo_steps = 0
		fixture.phase = "cargo-loose-final"
		return
	}

	if (fixture.phase == "cargo-loose-final")
	{
		if (consist_total_cargo(receiver) != 0)
		{
			cargo_step("the collected carriage did not unload at the delivery stop")
			return
		}
		if (!check(convoy_total_revenue(receiver) > fixture.cargo_revenue_before_final,
			"the delivery of the collected carriage did not book a further fare")) { return }
		pass()
		return
	}

	fail("cargo observation reached phase " + fixture.phase)
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
		if (is_cargo_case())
		{
			// The second convoy carries the case's carriage, so its load is
			// injected while it runs from the depot to the loading stop.
			fixture.cargo_steps = 0
			fixture.phase = "cargo-inject"
			return
		}
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
	if (fixture.exchange_steps > (is_reload_case() ? RELOAD_EXCHANGE_STEP_LIMIT : EXCHANGE_STEP_LIMIT))
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

		// The reload cases stop here, after the commit has been asserted and
		// before the shunting delay expires, so that the runner can join a
		// network client and the saved state can be loaded again.
		if (is_cargo_case())
		{
			if (!check_cargo_after_exchange()) { return }
			if (is_reload_case())
			{
				hold_for_reload()
				return
			}
			fixture.cargo_steps = 0
			if (is_loose_cargo_case())
			{
				fixture.phase = "cargo-loose-first"
			}
			else
			{
				fixture.phase = "cargo-through-delivery"
			}
			return
		}

		if (is_reload_case())
		{
			hold_for_reload()
			return
		}

		fixture.phase = "detect-advance"
		fixture.exchange_steps = 0
		return
	}

	++fixture.exchange_steps
	local exchange_limit = is_reload_case() ? (is_cargo_case() ? RELOAD_DEPARTURE_STEP_LIMIT : RELOAD_EXCHANGE_STEP_LIMIT) : (is_cargo_case() ? CARGO_STEP_LIMIT : EXCHANGE_STEP_LIMIT)
	if (fixture.exchange_steps > exchange_limit)
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
		if (!check_cargo_restored()) { return }
		print("RUNTIME CASE " + case_name + " RESTORED")
		if (is_cargo_case())
		{
			// The cargo cases continue with the carriage's delivery. The first
			// phase must outlast the shunting delay that began before the save.
			fixture.cargo_steps = 0
			fixture.phase = is_loose_cargo_case() ? "cargo-loose-first" : "cargo-through-delivery"
			return
		}
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

	if (fixture.phase == "cargo-inject")
	{
		observe_cargo_injection()
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

	if (fixture.phase == "cargo-through-delivery" || fixture.phase == "cargo-loose-first"
		|| fixture.phase == "cargo-loose-collect" || fixture.phase == "cargo-loose-final")
	{
		observe_cargo()
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
// saved persistent table has been evaluated. A reload case reconstructs the
// fixture identifiers and the cargo it asserts from the persisted state;
// setup() must not run again, because the infrastructure, schedules, loads and
// vehicles are all in the savegame. Observation then re-asserts the committed
// exchange from the loaded world.
function resume_game()
{
	if (!is_reload_case())
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

	if (is_cargo_case())
	{
		fixture.cargo = good_desc_x(state.cargo_name)
		if (!check(fixture.cargo != null, "the persisted cargo " + state.cargo_name + " is not in the loaded pakset")) { return }
		fixture.cargo_amount = state.cargo_amount
		fixture.cargo_class = state.cargo_class
		fixture.cargo_junction_pending = false
		fixture.cargo_revenue_before_final = 0
		fixture.cargo_steps = 0
		if (is_loose_cargo_case())
		{
			// The loose cargo phases identify the released carriage by its
			// descriptor, which must come from the loaded pakset.
			local loose = laid_over_consist()
			if (loose.len() == 1 && loose[0].get_vehicles().len() > 0)
			{
				fixture.coach_a = loose[0].get_vehicles()[0]
			}
		}
	}

	fixture.phase = "restored-pending"
	print("RUNTIME CASE " + case_name + ": saved paired exchange state restored from the savegame")
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
