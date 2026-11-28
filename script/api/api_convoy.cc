/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "api.h"

/** @file api_convoy.cc exports convoy related functions. */

#include "get_next.h"
#include "api_obj_desc_base.h"
#include "../api_class.h"
#include "../api_function.h"
#include "../../simconvoi.h"
#include "../../simhalt.h"
#include "../../simline.h"
#include "../../simtool.h"
#include "../../simworld.h"
#include "../../vehicle/vehicle.h"
#include "../../vehicle/road_vehicle.h"
#include "../../vehicle/rail_vehicle.h"
#include "../../display/viewport.h"
#include "../../dataobj/environment.h"
#include "../../network/network_socket_list.h"

// for convoy tools
#include "../../simmenu.h"
#include "../../dataobj/schedule.h"

using namespace script_api;


waytype_t get_convoy_wt(convoi_t* cnv)
{
	if (cnv  &&  cnv->get_vehicle_count() > 0) {
		return cnv->front()->get_waytype();
	}
	return invalid_wt;
}


vector_tpl<sint64> const& get_convoy_stat(convoi_t* cnv, sint32 INDEX)
{
	static vector_tpl<sint64> v;
	v.clear();
	if (cnv  &&  0<=INDEX  &&  INDEX<convoi_t::MAX_CONVOI_COST) {
		for(uint16 i = 0; i < MAX_MONTHS; i++) {
			v.append(cnv->get_stat_converted(i, (convoi_t::convoi_cost_t)INDEX));
		}
	}
	return v;
}


vector_tpl<vehicle_desc_t const*> const& convoi_get_vehicles(convoi_t* cnv)
{
	static vector_tpl<vehicle_desc_t const*> v;
	v.clear();
	if (cnv) {
		for(uint16 i=0; i<cnv->get_vehicle_count(); i++) {
			v.append(cnv->get_vehicle(i)->get_desc());
		}
	}
	return v;
}


sint32 convoy_get_kmh(convoi_t const* cnv)
{
	return cnv ? speed_to_kmh(cnv->get_akt_speed()) : -1;
}

// Remaining wait in simulation ticks for a convoy that a depot is servicing
// (routine maintenance, overhaul or replenishment), or -1 in any other state.
// The value is convoi_t::wait_lock, which convoi_t::sync_step() reduces by the
// step's tick length; those three states act when it has reached zero, so it
// reaches zero when the service ends.
sint32 convoy_get_service_wait_ticks(convoi_t *cnv)
{
	if (cnv == NULL) {
		return -1;
	}
	switch (cnv->get_state()) {
		case convoi_t::MAINTENANCE:
		case convoi_t::OVERHAUL:
		case convoi_t::REPLENISHING:
			return cnv->get_wait_lock();
		default:
			return -1;
	}
}

// A scenario script runs on the server only. This test hook mutates saved
// vehicle state directly instead of travelling as a network command, so a
// connected client would not apply it and would desynchronise. Refuse the call
// rather than create a silent desync. A client that joins later receives the
// server's complete saved state, so mutating the world is safe while no client
// is connected.
static bool script_may_change_vehicle_maintenance()
{
	return !env_t::networkmode
		|| (env_t::server
			&& socket_list_t::get_connected_clients() == 0
			&& socket_list_t::get_playing_clients() == 0);
}

// Scenario-only test hook. Sets the distance counters that decide when routine
// maintenance and overhauls are due. The counters are ordinary saved state;
// normal gameplay advances them in vehicle_t::step_km(). Returns true when the
// convoy and vehicle exist and the call is allowed.
bool convoy_set_maintenance_distances_for_testing(convoi_t *cnv, sint32 index, uint32 km_maintenance, uint32 km_overhaul)
{
	if (cnv == NULL || index < 0 || (uint32)index >= cnv->get_vehicle_count() || !script_may_change_vehicle_maintenance()) {
		return false;
	}
	vehicle_t *v = cnv->get_vehicle((uint16)index);
	if (v == NULL) {
		return false;
	}
	v->set_maintenance_distances_for_testing(km_maintenance, km_overhaul);
	return true;
}

// Scenario-only test hook. Makes routine maintenance due now, using either the
// kilometre interval or the months-based interval that the descriptor and
// settings select. urgent selects the overdue threshold. Returns true when the
// convoy and vehicle exist, the call is allowed, and the current date permits
// the chosen interval to be set.
bool convoy_set_maintenance_due_for_testing(convoi_t *cnv, sint32 index, bool urgent)
{
	if (cnv == NULL || index < 0 || (uint32)index >= cnv->get_vehicle_count() || !script_may_change_vehicle_maintenance()) {
		return false;
	}
	vehicle_t *v = cnv->get_vehicle((uint16)index);
	if (v == NULL) {
		return false;
	}
	return v->set_maintenance_due_for_testing(urgent);
}

// Returns the value that
// convoi_t::get_average_journey_times_this_convoy_only() holds for the pair
// (from, to), in tenths of minutes, or -1 if the table holds no such pair.
// This is the convoy's own average_journey_times table, not the line table that
// convoi_t::get_average_journey_times() returns when the convoy has a line.
sint32 convoy_get_own_recorded_journey_time(convoi_t *cnv, halthandle_t from, halthandle_t to)
{
	if (cnv == NULL || !from.is_bound() || !to.is_bound()) { return -1; }
	const average_tpl<uint32> *average = cnv->get_average_journey_times_this_convoy_only().access(id_pair(from.get_id(), to.get_id()));
	if (average == NULL) { return -1; }
	const uint32 value = average->get_average();
	return value > (uint32)SINT32_MAX_VALUE ? SINT32_MAX_VALUE : (sint32)value;
}


vector_tpl<convoihandle_t> const* generic_get_convoy_list(HSQUIRRELVM vm, SQInteger index)
{
	uint16 id;
	bool use_world;
	if (SQ_SUCCEEDED(get_slot(vm, "halt_id", id, index))) {
		halthandle_t halt;
		halt.set_id(id);
		if (halt.is_bound()) {
			return &halt->registered_convoys;
		}
	}
	if (SQ_SUCCEEDED(get_slot(vm, "line_id", id, index))) {
		linehandle_t line;
		line.set_id(id);
		if (line.is_bound()) {
			return &line->get_convoys();
		}
	}
	if (SQ_SUCCEEDED(get_slot(vm, "use_world", use_world, index))  &&  use_world) {
		return &welt->convoys();
	}
	sq_raise_error(vm, "Invalid convoy list.");
	return NULL;
}

SQInteger generic_get_convoy_count(HSQUIRRELVM vm)
{
	vector_tpl<convoihandle_t> const* list = generic_get_convoy_list(vm, 1);
	return list ? param<uint32>::push(vm, list->get_count()) : SQ_ERROR;
}

SQInteger generic_get_next_convoy(HSQUIRRELVM vm)
{
	vector_tpl<convoihandle_t> const* list = generic_get_convoy_list(vm, 1);
	return list ? generic_get_next(vm, list->get_count()) : SQ_ERROR;
}

SQInteger generic_get_convoy_by_index(HSQUIRRELVM vm)
{
	vector_tpl<convoihandle_t> const* list = generic_get_convoy_list(vm, 1);
	sint32 index = param<sint32>::get(vm, 2);
	convoihandle_t cnv;
	if (list) {
		 cnv = (0<=index  &&  (uint32)index < list->get_count()) ?  (*list)[index] : convoihandle_t();
		 return push_instance(vm, "convoy_x",  cnv.is_bound() ? cnv.get_id() :  0);
	}
	return SQ_ERROR;
}

uint32 calc_max_kmh(uint32 power, uint32 weight, sint32 speed_limit)
{
	// FIXME
	return 0;
// 	return speed_to_kmh(convoi_t::calc_max_speed(power, weight, kmh_to_speed(speed_limit)) );
}

uint32 kmh_to_tiles_per_month(uint32 kmh)
{
	return welt->speed_to_tiles_per_month( kmh_to_speed(kmh));
}

call_tool_init convoy_set_name(convoi_t *cnv, const char* name)
{
	return command_rename(cnv->get_owner(), 'c', cnv->self.get_id(), name);
}

call_tool_init convoy_set_line(convoi_t *cnv, player_t *player, linehandle_t line)
{
	if (!line.is_bound()) {
		return "Invalid line handle provided";
	}
	// see depot_frame_t::apply_line() and convoi_t::call_convoi_tool()
	cbuffer_t buf;
	buf.printf("%c,%u,%i", 'l', cnv->self.get_id(), line->get_handle().get_id());

	return call_tool_init(TOOL_CHANGE_CONVOI | SIMPLE_TOOL, buf, 0, player);
}

call_tool_init convoy_generic_tool(convoi_t *cnv, player_t *player, uint8 cnvtool)
{
	char c = (char)cnvtool;
	if (strchr("wx", c) == NULL) {
		return "Invalid convoy tool called";
	}
	// see convoi_t::call_convoi_tool()
	cbuffer_t buf;
	buf.printf("%c,%u", c, cnv->self.get_id());

	return call_tool_init(TOOL_CHANGE_CONVOI | SIMPLE_TOOL, buf, 0, player);
}

bool convoy_is_schedule_editor_open(convoi_t *cnv)
{
	return cnv->get_state() == convoi_t::EDIT_SCHEDULE;
}

bool convoy_is_followed(convoi_t const *cnv)
{
	return cnv ? welt->get_viewport()->get_follow_convoi().get_id() == cnv->self.get_id() : false;
}

void convoy_set_followed(convoi_t const *cnv)
{
	if (cnv) {
		welt->get_viewport()->set_follow_convoi(cnv->self);
	}
}

bool convoy_is_loading(convoi_t *cnv)
{
	return cnv->get_state() == convoi_t::LOADING;
}

/**
 * Current state as a value of the convoy_states enum.
 */
sint32 convoy_get_state(convoi_t *cnv)
{
	return cnv ? cnv->get_state() : convoi_t::INITIAL;
}

/**
 * Index of the current entry in the convoy's schedule, or -1 if it has none.
 */
sint32 convoy_get_current_stop(convoi_t *cnv)
{
	if (cnv  &&  cnv->get_schedule()  &&  !cnv->get_schedule()->empty()) {
		return cnv->get_schedule()->get_current_stop();
	}
	return -1;
}

/**
 * unique_entry_id of the current entry, or 0 if it has none.
 */
sint32 convoy_get_current_entry_id(convoi_t *cnv)
{
	if (cnv  &&  cnv->get_schedule()  &&  !cnv->get_schedule()->empty()) {
		return cnv->get_schedule()->get_current_entry().unique_entry_id;
	}
	return 0;
}

/**
 * Working method of a rail-family convoy, or -1 for a convoy without a rail vehicle at the front.
 */
sint32 convoy_get_working_method(convoi_t *cnv)
{
	rail_vehicle_t *rv = cnv ? dynamic_cast<rail_vehicle_t *>(cnv->front()) : NULL;
	return rv ? (sint32)rv->get_working_method() : -1;
}

/**
 * The convoy this convoy is approaching to couple with, or an unbound handle.
 */
convoihandle_t convoy_get_recombination_approach_partner(convoi_t *cnv)
{
	return cnv ? cnv->get_recombination_approach_partner() : convoihandle_t();
}

/**
 * True when this convoy is the mover in a committed coupling approach.
 */
bool convoy_is_recombination_approach_mover(convoi_t *cnv)
{
	return cnv && cnv->is_recombination_approach_mover();
}

/**
 * Snapshot of the convoy's vehicles in running order. Each entry is a
 * vehicle_slot_x value object, so no vehicle pointer outlives the call.
 */
SQInteger convoy_get_vehicle_snapshot(HSQUIRRELVM vm)
{
	convoi_t *cnv = param<convoi_t*>::get(vm, 1);
	if (cnv == NULL) {
		sq_pushnull(vm);
		return 1;
	}
	sq_newarray(vm, 0);
	for (uint16 i = 0; i < cnv->get_vehicle_count(); i++) {
		vehicle_t *v = cnv->get_vehicle(i);
		if (v == NULL) {
			continue;
		}
		if (SQ_FAILED(push_instance(vm, "vehicle_slot_x"))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		// stack: array, slot
		set_slot(vm, "index", (SQInteger)i);
		sq_pushstring(vm, "desc", -1);
		if (SQ_FAILED(param<const vehicle_desc_t*>::push(vm, v->get_desc()))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		if (SQ_FAILED(sq_set(vm, -3))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		sq_pushstring(vm, "owner", -1);
		if (SQ_FAILED(param<player_t*>::push(vm, v->get_owner()))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		if (SQ_FAILED(sq_set(vm, -3))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		set_slot(vm, "reversed", v->is_reversed());
		set_slot(vm, "cargo", (SQInteger)v->get_total_cargo());
		set_slot(vm, "capacity", (SQInteger)v->get_cargo_max());
		set_slot(vm, "loose_recipient_id", (SQInteger)v->get_loose_recipient_id());
		set_slot(vm, "loose_recipient_is_convoy", v->get_loose_recipient_is_convoy());
		set_slot(vm, "loose_recipient_is_same", v->get_loose_recipient_is_same());
		set_slot(vm, "loose_recipient_is_any", v->is_loose_recipient_any());
		sint64 collection_loose_since = 0;
		sint64 collection_time = 0;
		v->get_collection_credit_times(collection_loose_since, collection_time);
		set_slot(vm, "collection_loose_since", (SQInteger)collection_loose_since);
		set_slot(vm, "collection_time", (SQInteger)collection_time);
		sint64 earliest_cargo_arrival = -1;
		for (uint8 c = 0; c < v->get_number_of_classes(); c++) {
			for (const ware_t &ware : v->get_cargo(c)) {
				if (earliest_cargo_arrival < 0 || ware.arrival_time < earliest_cargo_arrival) {
					earliest_cargo_arrival = ware.arrival_time;
				}
			}
		}
		set_slot(vm, "earliest_cargo_arrival", (SQInteger)earliest_cargo_arrival);
		set_slot(vm, "pos", v->get_pos());
		set_slot(vm, "route_index", (SQInteger)v->get_route_index());
		set_slot(vm, "steps", (SQInteger)v->get_steps());
		set_slot(vm, "steps_next", (SQInteger)v->get_steps_next());
		// The convoy's current speed in km/h; every slot of one convoy reports
		// the same value.
		set_slot(vm, "speed", (SQInteger)speed_to_kmh(cnv->get_akt_speed()));
		// The four-bit stop-stand mask for a road vehicle, or zero when it holds
		// no stand. Every other vehicle reports zero.
		const road_vehicle_t *rv = dynamic_cast<const road_vehicle_t *>(v);
		set_slot(vm, "stand_mask", (SQInteger)(rv != NULL ? rv->get_stand_mask() : 0));
		// True for a road vehicle at a stop stand beside the travel lane: while
		// loading, while laid over, or while stopped at the stand, which
		// includes the wait for a reversal. Any other vehicle reports false.
		set_slot(vm, "pulled_over", rv != NULL && rv->is_pulled_over());
		// True when a road convoy is travelling in the passing lane (overtaking).
		// Any other vehicle type reports false.
		set_slot(vm, "overtaking", v->get_waytype() == road_wt && cnv->is_overtaking());
		// Maintenance and overhaul read model. The distances are in km since the
		// respective work last completed, availability is the percentage that
		// vehicle_t::get_availability() reports, and overhaul_cost is the amount
		// that vehicle_t::overhaul() would book at the vehicle's current overhaul
		// count, including an automatic upgrade where vehicle_t::get_auto_upgrade()
		// selects one. The two _needed flags are the per-vehicle tests that
		// depot_t::convoi_arrived() uses to split a convoy's vehicles into the
		// maintenance and overhaul groups.
		set_slot(vm, "km_since_last_maintenance", (SQInteger)v->get_km_since_last_maintenance());
		set_slot(vm, "km_since_last_overhaul", (SQInteger)v->get_km_since_last_overhaul());
		set_slot(vm, "availability", (SQInteger)v->get_availability());
		set_slot(vm, "overhaul_count", (SQInteger)v->get_overhaul_count());
		set_slot(vm, "overhaul_cost", (SQInteger)v->get_overhaul_or_upgrade_cost());
		set_slot(vm, "do_not_overhaul", v->get_do_not_overhaul());
		set_slot(vm, "do_not_auto_upgrade", v->get_do_not_auto_upgrade());
		set_slot(vm, "mothballed", v->get_is_mothballed());
		set_slot(vm, "maintenance_needed", v->is_maintenance_needed());
		set_slot(vm, "overhaul_needed", v->is_overhaul_needed());
		sq_arrayappend(vm, -2);
	}
	return 1;
}

call_tool_init convoy_change_schedule(convoi_t *cnv, player_t *player, schedule_t *sched)
{
	if (sched) {
		cbuffer_t buf;
		// make a copy, and perform validation on it
		schedule_t *copy = sched->copy();
// 		copy->make_valid();
		if (copy->get_count() >= 2) {
			// build param string (see convoi_info_t::apply_schedule and convoi_t::call_convoi_tool())
			buf.printf("g,%u,", cnv->self.get_id());
			copy->sprintf_schedule(buf);
			if (!tool_param_fits_network((const char*)buf)) {
				delete copy;
				return tool_param_too_large_msg();
			}
		}
		else {
			delete copy;
			return "Invalid schedule provided: less than two entries remained after removing doubles";
		}
		delete copy;
		return call_tool_init(TOOL_CHANGE_CONVOI | SIMPLE_TOOL, buf, 0, player);
	}
	return "Invalid schedule provided";
}

void export_convoy(HSQUIRRELVM vm, bool scenario)
{
	/**
	 * Implements iterator to iterate through lists of convoys.
	 *
	 * Usage:
	 * @code
	 * local list = world.get_convoy_list_x()
	 * foreach(cnv in list) {
	 *     ... // cnv is an instance of the convoy_x class
	 * }
	 * @endcode
	 *
	 * @see world::get_convoy_list, halt_x::get_convoy_list, line_x::get_convoy_list
	 */
	begin_class(vm, "convoy_list_x", 0);

	/**
	 * Meta-method to be used in foreach loops. Do not call them directly.
	 */
	register_function(vm, generic_get_next_convoy,     "_nexti",  2, "x o|i");
	/**
	 * Meta-method to be used in foreach loops. Do not call them directly.
	 * @typemask convoy_x()
	 */
	register_function(vm, generic_get_convoy_by_index, "_get",    2, "xi");
	/**
	 * Returns number of convoys in the list.
	 * @typemask integer()
	 */
	register_function(vm, generic_get_convoy_count, "get_count",  1, "x");

	end_class(vm);

	/**
	 * Class to access a convoy.
	 * Player vehicles are convoys, which themselves consist of individual vehicles (trucks, trailers, ...).
	 */
	begin_class(vm, "convoy_x", "extend_get,ingame_object");
	/**
	 * @returns if object is still valid.
	 */
	export_is_valid<convoi_t*>(vm); //register_function("is_valid")
	/**
	 * Does convoy needs electrified ways?
	 * @returns true if this is the case
	 */
	register_method(vm, &convoi_t::needs_electrification, "needs_electrification");
	/**
	 * Name of convoy.
	 * @returns name
	 */
	register_method(vm, &convoi_t::get_name,     "get_name");
	/**
	 * Sets convoy name.
	 * @ingroup rename_func
	 */
	register_method(vm, &convoy_set_name, "set_name", true);
	/**
	 * Position of convoy.
	 * @returns pos
	 */
	register_method(vm, &convoi_t::get_pos,      "get_pos");
	/**
	 * Owner of convoy.
	 * @returns owner, which is instance of player_x
	 */
	register_method(vm, &convoi_t::get_owner, "get_owner");
	/**
	 * Returns array of goods categories that can be transported by this convoy.
	 * @returns array
	 */
	register_method(vm, &convoi_t::get_goods_catg_index, "get_goods_catg_index");
	/**
	 * Returns waytype of convoy.
	 * @returns waytype
	 * @see way_types
	 */
	register_method(vm, &get_convoy_wt, "get_waytype", true);
	/**
	 * Schedule of this convoy.
	 */
	register_method(vm, &convoi_t::get_schedule, "get_schedule");
	/**
	 * Get monthly statistics of capacity.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_capacity",          freevariable<sint32>(convoi_t::CONVOI_CAPACITY), true);
// 	/**
// 	 * Get monthly statistics of number of transported goods.
// 	 * @returns array, index [0] corresponds to current month
// 	 */
// 	register_method_fv(vm, &get_convoy_stat, "get_transported_goods", freevariable<sint32>(convoi_t::CONVOI_TRANSPORTED_GOODS), true );
	/**
	 * Get monthly statistics of revenue.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_revenue",           freevariable<sint32>(convoi_t::CONVOI_REVENUE), true );
	/**
	 * Get monthly statistics of running costs.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_cost",              freevariable<sint32>(convoi_t::CONVOI_OPERATIONS), true );
	/**
	 * Get monthly statistics of profit.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_profit",            freevariable<sint32>(convoi_t::CONVOI_PROFIT), true );
	/**
	 * Get monthly statistics of traveled distance.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_traveled_distance", freevariable<sint32>(convoi_t::CONVOI_DISTANCE), true );
	/**
	 * Get monthly statistics of income/loss due to way tolls.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_convoy_stat, "get_way_tolls",         freevariable<sint32>(convoi_t::CONVOI_WAYTOLL), true );
	/**
	 * @returns lifetime traveled distance of this convoy
	 */
	register_method(vm, &convoi_t::get_total_distance_traveled, "get_distance_traveled_total");
	/**
	 * @returns the line the convoy belongs to, null if there is no line
	 */
	register_method(vm, &convoi_t::get_line, "get_line");
	/**
	 * Assigns the convoy to the given line.
	 * @param player
	 * @param line
	 * @ingroup game_cmd
	 */
	register_method(vm, convoy_set_line, "set_line", true);
	/**
	 * @returns returns an array containing the vehicle_desc_x 's of the vehicles of this convoy
	 */
	register_method(vm, convoi_get_vehicles, "get_vehicles", true);
	/**
	 * @returns current speed of convoy
	 */
	register_method(vm, convoy_get_kmh, "get_speed", true);
	/**
	 * @returns get current loading limit: waiting for full load gives 100
	 */
	register_method(vm, &convoi_t::get_loading_limit, "get_loading_limit");
	/**
	 * @returns get current loading level
	 */
	register_method(vm, &convoi_t::get_loading_level, "get_loading_level");
	/**
	 * @returns total loading time assigned at the current stop in simulation ticks
	 */
	register_method(vm, &convoi_t::get_current_loading_time, "get_current_loading_time");
	/**
	 * @returns arrival time at the current stop in simulation ticks
	 */
	register_method(vm, &convoi_t::get_arrival_time, "get_arrival_time");
	/**
	 * @returns gets location of home depot
	 */
	register_method(vm, &convoi_t::get_home_depot, "get_home_depot");
	/**
	 * @returns whether convoi has obsolete vehicles
	 */
	register_method(vm, &convoi_t::has_obsolete_vehicles, "has_obsolete_vehicles");
	/**
	 * Toggle the flag 'withdraw convoy'
	 * @ingroup game_cmd
	 */
	register_method_fv(vm, convoy_generic_tool, "toggle_withdraw", freevariable<uint8>('w'), true );
	/**
	 * @returns the flag 'withdraw convoy'
	 */
	register_method(vm, &convoi_t::get_withdraw, "is_withdrawn");
	/**
	 * @returns true if convoy is in depot
	 */
	register_method(vm, &convoi_t::in_depot, "is_in_depot");
	/**
	 * @returns true if convoy is currently waiting (for way clearance)
	 */
	register_method(vm, &convoi_t::is_waiting, "is_waiting");
	/**
	 * @returns true if the camera is following this convoy.
	 */
	register_method(vm, convoy_is_followed, "is_followed", true);
	/**
	 * Centers the camera on this convoy and follows it.
	 * @ingroup game_cmd
	 */
	register_method(vm, convoy_set_followed, "set_followed", true);
	/**
	 * @returns current state, a value of the convoy_states enum
	 */
	register_method(vm, convoy_get_state, "get_state", true);
	/**
	 * @returns index of the current schedule entry, or -1 without a schedule
	 */
	register_method(vm, convoy_get_current_stop, "get_current_stop", true);
	/**
	 * @returns unique_entry_id of the current schedule entry, or 0
	 */
	register_method(vm, convoy_get_current_entry_id, "get_current_entry_id", true);
	/**
	 * @returns the working method of a rail-family convoy, or -1 for a non-rail convoy
	 */
	register_method(vm, convoy_get_working_method, "get_working_method", true);
	/**
	 * @returns the convoy this convoy is approaching to couple with, or null
	 */
	register_method(vm, &convoy_get_recombination_approach_partner, "get_recombination_approach_partner", true);
	/**
	 * @returns true if this convoy is the mover in a committed coupling approach
	 */
	register_method(vm, &convoy_is_recombination_approach_mover, "is_recombination_approach_mover", true);
	/**
	 * @returns true if the convoy is running its schedule backwards
	 */
	register_method(vm, &convoi_t::get_reverse_schedule, "is_reverse_schedule");
	/**
	 * @returns array of vehicle_slot_x snapshots in running order
	 */
	register_function(vm, convoy_get_vehicle_snapshot, "get_vehicle_snapshot", 1, param<convoi_t*>::typemask());
	/**
	 * @returns true if any vehicle in this consist needs routine maintenance
	 */
	register_method(vm, &convoi_t::is_maintenance_needed, "needs_maintenance");
	/**
	 * @returns true if any vehicle in this consist is overdue for routine
	 * maintenance, which sends the convoy to a depot urgently
	 */
	register_method(vm, &convoi_t::is_maintenance_urgently_needed, "needs_maintenance_urgently");
	/**
	 * @returns true if any vehicle in this consist needs an overhaul
	 */
	register_method(vm, &convoi_t::is_overhaul_needed, "needs_overhaul");
	/**
	 * @returns remaining service wait in ticks for a convoy in the maintenance,
	 * overhaul or replenishing state, or -1 in any other state
	 */
	register_method(vm, convoy_get_service_wait_ticks, "get_service_wait_ticks", true);
	if (scenario) {
		/**
		 * Scenario-only test hook. Sets the distance counters that decide when
		 * routine maintenance and overhauls are due. These are ordinary saved
		 * vehicle fields; normal gameplay advances them in vehicle_t::step_km().
		 * The call is refused in a network game while a client is connected.
		 * @param index position in the convoy, 0 = front
		 * @param km_maintenance kilometres since the last completed routine maintenance
		 * @param km_overhaul kilometres since the last completed overhaul
		 * @returns true on success
		 * @note Only available in scenario mode.
		 * @ingroup scen_only
		 */
		register_method(vm, &convoy_set_maintenance_distances_for_testing, "set_maintenance_distances_for_testing", true);
		/**
		 * Scenario-only test hook. Makes routine maintenance due now, using the
		 * descriptor's kilometre interval when it has one and the saved
		 * last_maintenance_month otherwise.
		 * The call is refused in a network game while a client is connected.
		 * @param index position in the convoy, 0 = front
		 * @param urgent true to use the overdue threshold that diverts the convoy
		 * @returns true on success
		 * @note Only available in scenario mode.
		 * @ingroup scen_only
		 */
		register_method(vm, &convoy_set_maintenance_due_for_testing, "set_maintenance_due_for_testing", true);
	}
	/**
	 * @returns the convoy's livery scheme index
	 */
	register_method(vm, &convoi_t::get_livery_scheme_index, "get_livery_scheme_index");
	/**
	 * @returns true if convoy is currently waiting (for way clearance)
	 */
	register_method(vm, convoy_is_loading, "is_loading", true);
	/**
	 * Destroy the convoy.
	 * The convoy will be marked for destroying, it will be destroyed when the simulation continues.
	 * Call @ref sleep to be sure that the convoy is destroyed before script continues.
	 * @ingroup game_cmd
	 */
	register_method_fv(vm, convoy_generic_tool, "destroy", freevariable<uint8>('x'), true);
	/**
	 * @returns returns true if the schedule of the convoy is currently being edited.
	 */
	register_method(vm, convoy_is_schedule_editor_open, "is_schedule_editor_open", true);
	/**
	 * @returns returns the number of station tiles covered by the convoy.
	 */
	register_method(vm, &convoi_t::get_tile_length, "get_tile_length");
	/**
	 * Returns the average journey time that this convoy has recorded in its own
	 * table for the journey from @p from to @p to, in tenths of minutes. The
	 * convoy's own table is separate from the table of the line to which the
	 * convoy is assigned: a journey that a vehicle started on another convoy is
	 * recorded in the table of the line that collected the vehicle, not in the
	 * convoy's own table, so this method returns -1 for that pair. The method
	 * reads the convoy's own table and does not change it.
	 * @typemask integer(halt_x, halt_x)
	 * @param from the halt where the journey started
	 * @param to the halt where the journey ended
	 * @returns the average time in tenths of minutes, or -1 if the convoy's own
	 * table has no record for the pair
	 */
	register_method(vm, &convoy_get_own_recorded_journey_time, "get_own_recorded_journey_time", true);
	/**
	 * Change schedule of convoy.
	 * Schedule should not contain doubled entries and more than two entries.
	 * This might make the convoy lose its line.
	 * @ingroup game_cmd
	 */
	register_method(vm, convoy_change_schedule, "change_schedule", true);

#define STATIC
	/**
	 * Static method to compute the potential max speed of a convoy
	 * with the given parameters.
	 * @param power       total power of convoy
	 * @param weight      weight of convoy
	 * @param speed_limit speed limit induced by convoy's vehicles
	 * @returns max speed
	 */
	STATIC register_method(vm, &calc_max_kmh, "calc_max_speed");
	/**
	 * Static method to convert speed (from km per hour) to tiles per month.
	 * @param speed
	 * @returns tile per month
	 */
	STATIC register_method(vm, &kmh_to_tiles_per_month, "speed_to_tiles_per_month");

	end_class(vm);
}
