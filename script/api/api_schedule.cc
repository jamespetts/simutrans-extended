/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "api.h"

/** @file api_schedule.cc exports schedule structure and related functions. */

#include "api_obj_desc_base.h"
#include "../api_class.h"
#include "../api_function.h"
#include "../../bauer/goods_manager.h"
#include "../../dataobj/consist_order_t.h"
#include "../../dataobj/schedule.h"
#include "../../dataobj/translator.h"
#include "../../descriptor/vehicle_desc.h"
#include "../../simhalt.h"
#include "../../simintr.h"
#include "../../simworld.h"

using namespace script_api;


halthandle_t get_halt_from_koord3d(koord3d pos, const player_t *player )
{
	if(  player == NULL  ) {
		return halthandle_t();
	}
	return haltestelle_t::get_halt(pos, player);
}


SQInteger schedule_constructor(HSQUIRRELVM vm) // instance, wt, entries
{
	waytype_t wt = param<waytype_t>::get(vm, 2);
	SQInteger res = set_slot(vm, "waytype", wt, 1);

	if (SQ_SUCCEEDED(res)) {
		sq_pushstring(vm, "entries", -1);
		sq_push(vm, 3); // entries
		res = sq_set(vm, 1);
	}
	if (SQ_SUCCEEDED(res)) {
		// every schedule instance gets its own table of consist orders
		sq_pushstring(vm, "orders", -1);
		sq_newtable(vm);
		res = sq_set(vm, 1);
	}
	if (SQ_SUCCEEDED(res)) {
		// attach a schedule instance
		// the c++ schedule_t instance will be refilled everytime when transfered to the c++ side of things.
		schedule_t* sched = NULL;
		switch(wt) {
			case road_wt:        sched = new truck_schedule_t(); break;
			case track_wt:       sched = new train_schedule_t(); break;
			case water_wt:       sched = new ship_schedule_t(); break;
			case air_wt:         sched = new airplane_schedule_(); break;
			case monorail_wt:    sched = new monorail_schedule_t(); break;
			case tram_wt:        sched = new tram_schedule_t(); break;
			case maglev_wt:      sched = new maglev_schedule_t(); break;
			case narrowgauge_wt: sched = new narrowgauge_schedule_t(); break;
			default:
				sq_raise_error(vm, "Invalid waytype %d", wt);
				return SQ_ERROR;
		}
		attach_instance(vm, 1, sched);
	}
	return res;
}


// ---------------------------------------------------------------------------
// generic field access
//
// Every field is read by name and range-checked against the exact C++ width.
// The functions never truncate or repair input: out-of-range or wrongly typed
// values raise a script error and abort the conversion.
// ---------------------------------------------------------------------------

namespace {

/**
 * Fetches the named slot of the object at @p obj and leaves the value on the
 * top of the stack. Returns false (with a clean stack) if the slot does not
 * exist.
 */
bool get_field(HSQUIRRELVM vm, SQInteger obj, const char* name)
{
	sq_pushstring(vm, name, -1);
	const SQInteger index = obj > 0 ? obj : obj - 1;
	return SQ_SUCCEEDED(sq_get(vm, index));
}

/**
 * Reads an integer field. If the slot is absent or null, @p out is set to
 * @p def. If present, it must be an integer within [lo, hi].
 */
bool read_integer(HSQUIRRELVM vm, SQInteger obj, const char* name, sint64 lo, sint64 hi, sint64 def, sint64 &out, bool *present = NULL)
{
	if (present) { *present = false; }
	if (!get_field(vm, obj, name)) {
		out = def;
		return true;
	}
	if (sq_gettype(vm, -1) == OT_NULL) {
		sq_poptop(vm);
		out = def;
		return true;
	}
	if (present) { *present = true; }
	if (sq_gettype(vm, -1) != OT_INTEGER) {
		sq_poptop(vm);
		sq_raise_error(vm, translator::translate("Schedule field '%s' must be an integer"), name);
		return false;
	}
	SQInteger value = 0;
	sq_getinteger(vm, -1, &value);
	sq_poptop(vm);
	if (value < lo  ||  value > hi) {
		sq_raise_error(vm, translator::translate("Schedule field '%s' is out of range: %lld not in [%lld, %lld]"), name, (long long)value, (long long)lo, (long long)hi);
		return false;
	}
	out = (sint64)value;
	return true;
}

/**
 * Reads a boolean field. Absent or null slots are reported through
 * @p present = false and leave @p out unchanged.
 */
bool read_boolean(HSQUIRRELVM vm, SQInteger obj, const char* name, bool &out, bool &present)
{
	present = false;
	if (!get_field(vm, obj, name)) {
		return true;
	}
	if (sq_gettype(vm, -1) == OT_NULL) {
		sq_poptop(vm);
		return true;
	}
	if (sq_gettype(vm, -1) != OT_BOOL) {
		sq_poptop(vm);
		sq_raise_error(vm, translator::translate("Schedule field '%s' must be a bool"), name);
		return false;
	}
	SQBool value = SQFalse;
	sq_getbool(vm, -1, &value);
	sq_poptop(vm);
	out = value != SQFalse;
	present = true;
	return true;
}

/**
 * Reads a vehicle descriptor slot. A null or absent slot yields NULL. Any
 * other non-instance value is a script error.
 */
const vehicle_desc_t* read_vehicle_desc(HSQUIRRELVM vm, SQInteger obj, const char* name, bool &ok)
{
	ok = true;
	if (!get_field(vm, obj, name)) {
		return NULL;
	}
	if (sq_gettype(vm, -1) == OT_NULL) {
		sq_poptop(vm);
		return NULL;
	}
	if (sq_gettype(vm, -1) != OT_INSTANCE) {
		sq_poptop(vm);
		sq_raise_error(vm, translator::translate("Schedule field '%s' must be a vehicle_desc_x or null"), name);
		ok = false;
		return NULL;
	}
	const vehicle_desc_t *desc = param<const vehicle_desc_t*>::get(vm, -1);
	sq_poptop(vm);
	if (desc == NULL) {
		ok = false;
	}
	return desc;
}

bool set_desc_slot(HSQUIRRELVM vm, const char* name, const vehicle_desc_t* desc)
{
	sq_pushstring(vm, name, -1);
	if (SQ_FAILED(param<const vehicle_desc_t*>::push(vm, desc))) {
		sq_poptop(vm);
		return false;
	}
	// object at -3, name at -2, value at -1
	return SQ_SUCCEEDED(sq_set(vm, -3));
}

} // namespace


// ---------------------------------------------------------------------------
// C++ -> Squirrel
// ---------------------------------------------------------------------------

SQInteger push_vehicle_description(HSQUIRRELVM vm, const vehicle_description_element &e)
{
	SQInteger res = push_instance(vm, "vehicle_description_x");
	if (SQ_FAILED(res)) {
		return res;
	}
	if (!set_desc_slot(vm, "specific_vehicle", e.specific_vehicle)) {
		return SQ_ERROR;
	}
	set_slot(vm, "empty", e.empty);
	set_slot(vm, "engine_type", (SQInteger)e.engine_type);
	set_slot(vm, "min_catering", (SQInteger)e.min_catering);
	set_slot(vm, "max_catering", (SQInteger)e.max_catering);
	set_slot(vm, "must_carry_class", (SQInteger)e.must_carry_class);
	set_slot(vm, "min_range", (SQInteger)e.min_range);
	set_slot(vm, "max_range", (SQInteger)e.max_range);
	set_slot(vm, "min_brake_force", (SQInteger)e.min_brake_force);
	set_slot(vm, "max_brake_force", (SQInteger)e.max_brake_force);
	set_slot(vm, "min_power", (SQInteger)e.min_power);
	set_slot(vm, "max_power", (SQInteger)e.max_power);
	set_slot(vm, "min_tractive_effort", (SQInteger)e.min_tractive_effort);
	set_slot(vm, "max_tractive_effort", (SQInteger)e.max_tractive_effort);
	set_slot(vm, "min_topspeed", (SQInteger)e.min_topspeed);
	set_slot(vm, "max_topspeed", (SQInteger)e.max_topspeed);
	set_slot(vm, "min_weight", (SQInteger)e.min_weight);
	set_slot(vm, "max_weight", (SQInteger)e.max_weight);
	set_slot(vm, "min_axle_load", (SQInteger)e.min_axle_load);
	set_slot(vm, "max_axle_load", (SQInteger)e.max_axle_load);
	set_slot(vm, "min_capacity", (SQInteger)e.min_capacity);
	set_slot(vm, "max_capacity", (SQInteger)e.max_capacity);
	set_slot(vm, "min_running_cost", (SQInteger)e.min_running_cost);
	set_slot(vm, "max_running_cost", (SQInteger)e.max_running_cost);
	set_slot(vm, "min_fixed_cost", (SQInteger)e.min_fixed_cost);
	set_slot(vm, "max_fixed_cost", (SQInteger)e.max_fixed_cost);
	set_slot(vm, "min_fuel_per_km", (SQInteger)e.min_fuel_per_km);
	set_slot(vm, "max_fuel_per_km", (SQInteger)e.max_fuel_per_km);
	set_slot(vm, "min_staff_hundredths", (SQInteger)e.min_staff_hundredths);
	set_slot(vm, "max_staff_hundredths", (SQInteger)e.max_staff_hundredths);
	set_slot(vm, "min_drivers", (SQInteger)e.min_drivers);
	set_slot(vm, "max_drivers", (SQInteger)e.max_drivers);
	return 1;
}


SQInteger push_consist_order_element(HSQUIRRELVM vm, const consist_order_element_t &element)
{
	sq_newarray(vm, 0);
	for (uint32 i = 0; i < element.get_count(); i++) {
		if (SQ_FAILED(push_vehicle_description(vm, element.get_vehicle_description(i)))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		sq_arrayappend(vm, -2);
	}
	// stack: alternatives array
	SQInteger res = push_instance(vm, "consist_order_element_x");
	if (SQ_FAILED(res)) {
		sq_poptop(vm);
		return SQ_ERROR;
	}
	// stack: array, element
	set_slot(vm, "catg", (SQInteger)element.get_catg_index());
	sq_pushstring(vm, "alternatives", -1);
	sq_push(vm, -3);
	if (SQ_FAILED(sq_set(vm, -3))) {
		sq_poptop(vm);
		return SQ_ERROR;
	}
	// remove the alternatives array, leaving the element
	sq_remove(vm, -2);
	return 1;
}


SQInteger push_consist_order(HSQUIRRELVM vm, const consist_order_t &order)
{
	sq_newarray(vm, 0);
	for (uint32 i = 0; i < order.get_count(); i++) {
		if (SQ_FAILED(push_consist_order_element(vm, order.get_order(i)))) {
			sq_poptop(vm);
			return SQ_ERROR;
		}
		sq_arrayappend(vm, -2);
	}
	// stack: slots array
	SQInteger res = push_instance(vm, "consist_order_x");
	if (SQ_FAILED(res)) {
		sq_poptop(vm);
		return SQ_ERROR;
	}
	// stack: array, order
	set_slot(vm, "available_to_id", (SQInteger)order.get_available_to_id());
	set_slot(vm, "available_to_flags", (SQInteger)order.get_available_to_flags());
	sq_pushstring(vm, "slots", -1);
	sq_push(vm, -3);
	if (SQ_FAILED(sq_set(vm, -3))) {
		sq_poptop(vm);
		return SQ_ERROR;
	}
	// remove the slots array, leaving the order
	sq_remove(vm, -2);
	return 1;
}


SQInteger param<schedule_entry_t>::push(HSQUIRRELVM vm, schedule_entry_t const& v)
{
	SQInteger res = push_instance(vm, "schedule_entry_x", v.pos, v.minimum_loading, v.waiting_time_shift);
	if (SQ_FAILED(res)) {
		return res;
	}
	// Standard names first. The Extended aliases and the named flag booleans
	// are derived from these canonical values by schedule_entry_x's _set/_get
	// synchroniser, so pushing them here would only duplicate the same data.
	set_slot(vm, "load", (SQInteger)v.minimum_loading);
	set_slot(vm, "wait", (SQInteger)v.waiting_time_shift);
	set_slot(vm, "spacing_shift", (SQInteger)v.spacing_shift);
	set_slot(vm, "unique_entry_id", (SQInteger)v.unique_entry_id);
	set_slot(vm, "flags", (SQInteger)v.flags);
	set_slot(vm, "condition_bitfield_broadcaster", (SQInteger)v.condition_bitfield_broadcaster);
	set_slot(vm, "condition_bitfield_receiver", (SQInteger)v.condition_bitfield_receiver);
	set_slot(vm, "target_id_condition_trigger", (SQInteger)v.target_id_condition_trigger);
	set_slot(vm, "target_id_couple", (SQInteger)v.target_id_couple);
	set_slot(vm, "target_id_uncouple", (SQInteger)v.target_id_uncouple);
	set_slot(vm, "target_unique_entry_uncouple", (SQInteger)v.target_unique_entry_uncouple);
	set_slot(vm, "target_unique_entry_couple", (SQInteger)v.target_unique_entry_couple);
	set_slot(vm, "reverse", (SQInteger)v.reverse);
	set_slot(vm, "max_speed_kmh", (SQInteger)v.max_speed_kmh);
	return 1;
}


SQInteger param<schedule_t*>::push(HSQUIRRELVM vm, schedule_t* const& v)
{
	return param<const schedule_t*>::push(vm, v);
}


SQInteger param<const schedule_t*>::push(HSQUIRRELVM vm, const schedule_t* const& v)
{
	if (v == NULL) {
		sq_pushnull(vm); return 1;
	}
	SQInteger res = push_instance(vm, "schedule_x", v->get_waytype(), v->entries);
	if (SQ_FAILED(res)) {
		return res;
	}
	const SQInteger schedule_index = sq_gettop(vm);
	// Authoring settings. current_stop is runtime state; it is carried here so
	// that an unchanged read -> commit round trip does not move the convoy back
	// to the first stop.
	set_slot(vm, "bidirectional", v->is_bidirectional());
	set_slot(vm, "mirrored", v->is_mirrored());
	set_slot(vm, "same_spacing_shift", v->is_same_spacing_shift());
	set_slot(vm, "spacing", (SQInteger)v->get_spacing());
	set_slot(vm, "current_stop", (SQInteger)v->get_current_stop());

	// Consist orders, keyed by the unique_entry_id of their entry.
	sq_newtable(vm);
	const SQInteger table_index = sq_gettop(vm);
	for (auto const &order : v->orders) {
		sq_pushinteger(vm, (SQInteger)order.key);
		if (SQ_FAILED(push_consist_order(vm, order.value))) {
			sq_settop(vm, schedule_index);
			return SQ_ERROR;
		}
		// stack: schedule, table, key, value; the table is at table_index
		if (SQ_FAILED(sq_newslot(vm, table_index, false))) {
			sq_settop(vm, schedule_index);
			return SQ_ERROR;
		}
	}
	// stack: schedule, table
	sq_pushstring(vm, "orders", -1);
	sq_push(vm, table_index);
	if (SQ_FAILED(sq_set(vm, schedule_index))) {
		sq_settop(vm, schedule_index);
		return SQ_ERROR;
	}
	sq_settop(vm, schedule_index);
	return 1;
}


// ---------------------------------------------------------------------------
// Squirrel -> C++
// ---------------------------------------------------------------------------

bool read_vehicle_description(HSQUIRRELVM vm, SQInteger obj, vehicle_description_element &out)
{
	bool ok = false;
	const vehicle_desc_t *desc = read_vehicle_desc(vm, obj, "specific_vehicle", ok);
	if (!ok) { return false; }
	out.specific_vehicle = desc;

	bool present = false;
	bool value = false;
	if (!read_boolean(vm, obj, "empty", value, present)) { return false; }
	if (present) { out.empty = value; }

	sint64 i = 0;
	if (!read_integer(vm, obj, "engine_type", 0, vehicle_desc_t::MAX_TRACTION_TYPE, out.engine_type, i)) { return false; }
	out.engine_type = (uint8)i;
	if (!read_integer(vm, obj, "min_catering", 0, 255, out.min_catering, i)) { return false; }
	out.min_catering = (uint8)i;
	if (!read_integer(vm, obj, "max_catering", 0, 255, out.max_catering, i)) { return false; }
	out.max_catering = (uint8)i;
	if (!read_integer(vm, obj, "must_carry_class", 0, 255, out.must_carry_class, i)) { return false; }
	out.must_carry_class = (uint8)i;
	if (!read_integer(vm, obj, "min_range", 0, UINT32_MAX_VALUE, out.min_range, i)) { return false; }
	out.min_range = (uint32)i;
	if (!read_integer(vm, obj, "max_range", 0, UINT32_MAX_VALUE, out.max_range, i)) { return false; }
	out.max_range = (uint32)i;
	if (!read_integer(vm, obj, "min_brake_force", 0, 65535, out.min_brake_force, i)) { return false; }
	out.min_brake_force = (uint16)i;
	if (!read_integer(vm, obj, "max_brake_force", 0, 65535, out.max_brake_force, i)) { return false; }
	out.max_brake_force = (uint16)i;
	if (!read_integer(vm, obj, "min_power", 0, UINT32_MAX_VALUE, out.min_power, i)) { return false; }
	out.min_power = (uint32)i;
	if (!read_integer(vm, obj, "max_power", 0, UINT32_MAX_VALUE, out.max_power, i)) { return false; }
	out.max_power = (uint32)i;
	if (!read_integer(vm, obj, "min_tractive_effort", 0, UINT32_MAX_VALUE, out.min_tractive_effort, i)) { return false; }
	out.min_tractive_effort = (uint32)i;
	if (!read_integer(vm, obj, "max_tractive_effort", 0, UINT32_MAX_VALUE, out.max_tractive_effort, i)) { return false; }
	out.max_tractive_effort = (uint32)i;
	if (!read_integer(vm, obj, "min_topspeed", 0, UINT32_MAX_VALUE, out.min_topspeed, i)) { return false; }
	out.min_topspeed = (uint32)i;
	if (!read_integer(vm, obj, "max_topspeed", 0, UINT32_MAX_VALUE, out.max_topspeed, i)) { return false; }
	out.max_topspeed = (uint32)i;
	if (!read_integer(vm, obj, "min_weight", 0, UINT32_MAX_VALUE, out.min_weight, i)) { return false; }
	out.min_weight = (uint32)i;
	if (!read_integer(vm, obj, "max_weight", 0, UINT32_MAX_VALUE, out.max_weight, i)) { return false; }
	out.max_weight = (uint32)i;
	if (!read_integer(vm, obj, "min_axle_load", 0, UINT32_MAX_VALUE, out.min_axle_load, i)) { return false; }
	out.min_axle_load = (uint32)i;
	if (!read_integer(vm, obj, "max_axle_load", 0, UINT32_MAX_VALUE, out.max_axle_load, i)) { return false; }
	out.max_axle_load = (uint32)i;
	if (!read_integer(vm, obj, "min_capacity", 0, 65535, out.min_capacity, i)) { return false; }
	out.min_capacity = (uint16)i;
	if (!read_integer(vm, obj, "max_capacity", 0, 65535, out.max_capacity, i)) { return false; }
	out.max_capacity = (uint16)i;
	if (!read_integer(vm, obj, "min_running_cost", 0, UINT32_MAX_VALUE, out.min_running_cost, i)) { return false; }
	out.min_running_cost = (uint32)i;
	if (!read_integer(vm, obj, "max_running_cost", 0, UINT32_MAX_VALUE, out.max_running_cost, i)) { return false; }
	out.max_running_cost = (uint32)i;
	if (!read_integer(vm, obj, "min_fixed_cost", 0, UINT32_MAX_VALUE, out.min_fixed_cost, i)) { return false; }
	out.min_fixed_cost = (uint32)i;
	if (!read_integer(vm, obj, "max_fixed_cost", 0, UINT32_MAX_VALUE, out.max_fixed_cost, i)) { return false; }
	out.max_fixed_cost = (uint32)i;
	if (!read_integer(vm, obj, "min_fuel_per_km", 0, UINT32_MAX_VALUE, out.min_fuel_per_km, i)) { return false; }
	out.min_fuel_per_km = (uint32)i;
	if (!read_integer(vm, obj, "max_fuel_per_km", 0, UINT32_MAX_VALUE, out.max_fuel_per_km, i)) { return false; }
	out.max_fuel_per_km = (uint32)i;
	if (!read_integer(vm, obj, "min_staff_hundredths", 0, UINT32_MAX_VALUE, out.min_staff_hundredths, i)) { return false; }
	out.min_staff_hundredths = (uint32)i;
	if (!read_integer(vm, obj, "max_staff_hundredths", 0, UINT32_MAX_VALUE, out.max_staff_hundredths, i)) { return false; }
	out.max_staff_hundredths = (uint32)i;
	if (!read_integer(vm, obj, "min_drivers", 0, UINT32_MAX_VALUE, out.min_drivers, i)) { return false; }
	out.min_drivers = (uint32)i;
	if (!read_integer(vm, obj, "max_drivers", 0, UINT32_MAX_VALUE, out.max_drivers, i)) { return false; }
	out.max_drivers = (uint32)i;
	return true;
}


bool read_consist_order_element(HSQUIRRELVM vm, SQInteger obj, consist_order_element_t &elem)
{
	sint64 catg = -1;
	if (!read_integer(vm, obj, "catg", -1, consist_order_element_t::any_catg_index, -1, catg)) { return false; }

	if (get_field(vm, obj, "alternatives")) {
		if (sq_gettype(vm, -1) != OT_ARRAY) {
			sq_poptop(vm);
			sq_raise_error(vm, "%s", translator::translate("Consist order slot 'alternatives' must be an array"));
			return false;
		}
		sq_pushnull(vm);
		while (SQ_SUCCEEDED(sq_next(vm, -2))) {
			vehicle_description_element ve;
			if (!read_vehicle_description(vm, -1, ve)) { return false; }
			if (ve.specific_vehicle) {
				if (!elem.append_vehicle(ve.specific_vehicle)) {
					sq_raise_error(vm, translator::translate("Consist order slot mixes goods categories: vehicle '%s' contradicts the category fixed by its first alternative"), ve.specific_vehicle->get_name());
					return false;
				}
			}
			else {
				elem.append_rule();
			}
			// Preserve every rule/range field exactly, including on alternatives
			// with a specific vehicle (those fields are stored even if unused).
			elem.access_vehicle_description(elem.get_count() - 1) = ve;
			sq_pop(vm, 2);
		}
		// remove the iterator and the alternatives array
		sq_pop(vm, 2);
	}

	// schedule_t::parse_orders() reads the first alternative's must_carry_class
	// for passenger and mail slots. A passenger or mail slot with no
	// alternatives would therefore index an empty vector and abort, so refuse it
	// here, before the schedule is constructed. Freight slots with no
	// alternatives are valid: they contribute only their category to the
	// carried-category tables.
	if (elem.get_count() == 0  &&  catg >= 0
			&&  ((uint8)catg == goods_manager_t::passengers->get_catg_index()
				|| (uint8)catg == goods_manager_t::mail->get_catg_index())) {
		sq_raise_error(vm, "%s", translator::translate("A passenger or mail consist-order slot needs at least one alternative."));
		return false;
	}

	if (catg >= 0) {
		if (!elem.set_catg_index((uint8)catg)) {
			sq_raise_error(vm, translator::translate("Consist order slot category %d contradicts one of its specific vehicles"), (int)catg);
			return false;
		}
	}
	return true;
}


bool read_consist_order(HSQUIRRELVM vm, SQInteger obj, consist_order_t &order)
{
	sint64 id = 0;
	sint64 flags = consist_order_t::available_to_is_same;
	if (!read_integer(vm, obj, "available_to_id", 0, 65535, 0, id)) { return false; }
	if (!read_integer(vm, obj, "available_to_flags", 0, 255, (sint64)consist_order_t::available_to_is_same, flags)) { return false; }

	if (id == 0  &&  flags == 0) {
		order.set_available_to_any();
	}
	else if (id == 0  &&  flags == consist_order_t::available_to_is_same) {
		order.set_available_to_same();
	}
	else if (id != 0  &&  flags == 0) {
		order.set_available_to((uint16)id, false);
	}
	else if (id != 0  &&  flags == consist_order_t::available_to_is_convoy) {
		order.set_available_to((uint16)id, true);
	}
	else {
		sq_raise_error(vm, translator::translate("Invalid consist order recipient: id %d, flags %d"), (int)id, (int)flags);
		return false;
	}

	if (get_field(vm, obj, "slots")) {
		if (sq_gettype(vm, -1) != OT_ARRAY) {
			sq_poptop(vm);
			sq_raise_error(vm, "%s", translator::translate("Consist order 'slots' must be an array"));
			return false;
		}
		sq_pushnull(vm);
		while (SQ_SUCCEEDED(sq_next(vm, -2))) {
			consist_order_element_t elem;
			if (!read_consist_order_element(vm, -1, elem)) { return false; }
			order.append(elem);
			sq_pop(vm, 2);
		}
		// remove the iterator and the slots array
		sq_pop(vm, 2);
	}
	return true;
}


bool read_orders(HSQUIRRELVM vm, SQInteger obj, schedule_t *sched)
{
	sched->orders.clear();
	if (!get_field(vm, obj, "orders")) {
		return true;
	}
	if (sq_gettype(vm, -1) != OT_TABLE) {
		sq_poptop(vm);
		sq_raise_error(vm, "%s", translator::translate("Schedule field 'orders' must be a table keyed by unique_entry_id"));
		return false;
	}
	sq_pushnull(vm);
	while (SQ_SUCCEEDED(sq_next(vm, -2))) {
		// stack: ..., table, key, value
		if (sq_gettype(vm, -2) != OT_INTEGER) {
			sq_raise_error(vm, "%s", translator::translate("Schedule order keys must be integer unique_entry_ids"));
			return false;
		}
		SQInteger key = 0;
		sq_getinteger(vm, -2, &key);
		if (key <= 0  ||  key > 65535) {
			sq_raise_error(vm, translator::translate("Schedule order key %lld is not a valid unique_entry_id"), (long long)key);
			return false;
		}
		bool found = false;
		for (uint8 i = 0; i < sched->get_count(); i++) {
			if (sched->entries[i].unique_entry_id == (uint16)key) {
				found = true;
				break;
			}
		}
		if (!found) {
			sq_raise_error(vm, translator::translate("Schedule order key %lld does not name an entry of this schedule"), (long long)key);
			return false;
		}
		if (sq_gettype(vm, -1) != OT_INSTANCE) {
			sq_raise_error(vm, translator::translate("Schedule order %lld must be a consist_order_x"), (long long)key);
			return false;
		}
		consist_order_t order;
		if (!read_consist_order(vm, -1, order)) { return false; }
		sched->orders.put((uint16)key, order);
		sq_pop(vm, 2);
	}
	// remove the iterator and the orders table
	sq_pop(vm, 2);
	return true;
}


/**
 * Appends one schedule entry. Returns false and raises a script error on any
 * invalid or ambiguous input.
 */
bool read_entry(HSQUIRRELVM vm, SQInteger obj, schedule_t *sched)
{
	koord3d pos = param<koord3d>::get(vm, obj);

	sint64 load = 0;
	if (!read_integer(vm, obj, "load", 0, 65535, 0, load)) { return false; }
	const sint64 minimum_loading = load;

	sint64 wait_shift = 0;
	if (!read_integer(vm, obj, "wait", -128, 127, 0, wait_shift)) { return false; }
	const sint64 waiting_time_shift = wait_shift;

	sint64 spacing_shift = 0;
	sint64 unique_entry_id = 0;
	sint64 flags = 0;
	sint64 broadcaster = 0;
	sint64 receiver = 0;
	sint64 trigger_target = 0;
	sint64 couple_target = 0;
	sint64 uncouple_target = 0;
	sint64 uncouple_entry = 0;
	sint64 couple_entry = 0;
	sint64 reverse = -1;
	sint64 max_speed = 65535;

	if (!read_integer(vm, obj, "spacing_shift", -32768, 32767, 0, spacing_shift)) { return false; }
	if (!read_integer(vm, obj, "unique_entry_id", 0, 65535, 0, unique_entry_id)) { return false; }
	if (!read_integer(vm, obj, "flags", 0, UINT32_MAX_VALUE, 0, flags)) { return false; }
	if (!read_integer(vm, obj, "condition_bitfield_broadcaster", 0, 65535, 0, broadcaster)) { return false; }
	if (!read_integer(vm, obj, "condition_bitfield_receiver", 0, 65535, 0, receiver)) { return false; }
	if (!read_integer(vm, obj, "target_id_condition_trigger", 0, 65535, 0, trigger_target)) { return false; }
	if (!read_integer(vm, obj, "target_id_couple", 0, 65535, 0, couple_target)) { return false; }
	if (!read_integer(vm, obj, "target_id_uncouple", 0, 65535, 0, uncouple_target)) { return false; }
	if (!read_integer(vm, obj, "target_unique_entry_uncouple", 0, 65535, 0, uncouple_entry)) { return false; }
	if (!read_integer(vm, obj, "target_unique_entry_couple", 0, 65535, 0, couple_entry)) { return false; }
	if (!read_integer(vm, obj, "reverse", -1, 1, -1, reverse)) { return false; }
	if (!read_integer(vm, obj, "max_speed_kmh", 0, 65535, 65535, max_speed)) { return false; }

	// 'flags' is the canonical raw bitfield. schedule_entry_x keeps the named
	// flag values synchronised with it, so reading them here would duplicate
	// the same data.
	const uint32 flag_bits = (uint32)flags;

	if ((flag_bits & schedule_entry_t::wait_for_time)  &&  minimum_loading != 0) {
		sq_raise_error(vm, translator::translate("Schedule entry at (%s) sets both wait_for_time and minimum_loading"), pos.get_str());
		return false;
	}

	if (sched->get_count() >= 254) {
		sq_raise_error(vm, "%s", translator::translate("A schedule cannot hold more than 254 entries"));
		return false;
	}

	grund_t *gr = welt->lookup(pos);
	if (gr == NULL) {
		sq_raise_error(vm, translator::translate("Schedule entry position (%s) is not on the map"), pos.get_str());
		return false;
	}
	if (!sched->is_stop_allowed(gr)) {
		sq_raise_error(vm, translator::translate("Cannot stop at (%s) with a %s schedule"), pos.get_str(), sched->get_schedule_type_name());
		return false;
	}

	if (!sched->append(gr, (uint16)minimum_loading, (uint8)(waiting_time_shift & 0xFF), (sint16)spacing_shift, flag_bits,
			(uint16)broadcaster, (uint16)receiver, (uint16)trigger_target, (uint16)couple_target, (uint16)uncouple_target,
			(uint16)uncouple_entry, (uint16)max_speed, (uint16)couple_entry)) {
		sq_raise_error(vm, translator::translate("Could not append the schedule entry at (%s)"), pos.get_str());
		return false;
	}
	schedule_entry_t &entry = sched->entries.back();
	entry.reverse = (sint8)reverse;

	if (unique_entry_id != 0) {
		// Reject duplicates instead of silently renumbering either entry.
		for (uint8 i = 0; i + 1 < sched->get_count(); i++) {
			if (sched->entries[i].unique_entry_id == (uint16)unique_entry_id) {
				sq_raise_error(vm, translator::translate("Duplicate unique_entry_id %d in schedule"), (int)unique_entry_id);
				return false;
			}
		}
		entry.unique_entry_id = (uint16)unique_entry_id;
	}
	return true;
}


schedule_t* script_api::param<schedule_t*>::get(HSQUIRRELVM vm, SQInteger index)
{
	// get waytype
	waytype_t wt = invalid_wt;
	get_slot(vm, "waytype", wt, index);

	// get instance pointer
	schedule_t* sched = get_attached_instance<schedule_t>(vm, index, param<schedule_t*>::tag());
	if (sched == NULL) {
		return NULL;
	}

	const SQInteger obj = index > 0 ? index : sq_gettop(vm) + index + 1;

	sched->entries.clear();
	sched->orders.clear();

	// entries
	if (get_field(vm, obj, "entries")) {
		if (sq_gettype(vm, -1) != OT_ARRAY) {
			sq_poptop(vm);
			sq_raise_error(vm, "%s", translator::translate("Schedule field 'entries' must be an array"));
			return NULL;
		}
		sq_pushnull(vm);
		while (SQ_SUCCEEDED(sq_next(vm, -2))) {
			if (!read_entry(vm, -1, sched)) {
				return NULL;
			}
			sq_pop(vm, 2);
		}
		// remove the iterator and the entries array
		sq_pop(vm, 2);
	}

	// schedule-level authoring settings
	bool value = false;
	bool present = false;
	if (!read_boolean(vm, obj, "bidirectional", value, present)) { return NULL; }
	if (present) { sched->set_bidirectional(value); }
	if (!read_boolean(vm, obj, "mirrored", value, present)) { return NULL; }
	if (present) { sched->set_mirrored(value); }
	if (!read_boolean(vm, obj, "same_spacing_shift", value, present)) { return NULL; }
	if (present) { sched->set_same_spacing_shift(value); }

	sint64 spacing = sched->get_spacing();
	sint64 rebuilt_spacing = 0;
	// The schedule GUI's spacing control uses 0..999 convoys per month; the
	// string parser also accepts only a non-negative decimal. Match that range
	// rather than inventing a wider, signed one.
	if (!read_integer(vm, obj, "spacing", 0, 999, spacing, rebuilt_spacing)) { return NULL; }
	sched->set_spacing((sint16)rebuilt_spacing);

	// current stop is runtime state, not authoring data: preserve it when the
	// script obtained the schedule from a live object, and clamp it to the
	// rebuilt entry list.
	sint64 current_stop = sched->get_current_stop();
	sint64 rebuilt_current_stop = 0;
	if (!read_integer(vm, obj, "current_stop", 0, 255, current_stop, rebuilt_current_stop)) { return NULL; }
	sched->set_current_stop((uint8)rebuilt_current_stop);

	if (!read_orders(vm, obj, sched)) {
		return NULL;
	}
	sched->parse_orders();
	return sched;
}


void* script_api::param<schedule_t*>::tag()
{
	return (void*)&script_api::param<schedule_t*>::get;
}

void export_schedule(HSQUIRRELVM vm)
{
	/**
	 * Schedule entries
	 */
	begin_class(vm, "schedule_entry_x", "coord3d");

#ifdef SQAPI_DOC // document members
	/**
	 * Minimum loading percentage (Standard name).
	 */
	integer load;
	/**
	 * Waiting time setting (Standard name), sint8.
	 */
	integer wait;
	/**
	 * Minimum loading percentage, uint16. The same value as load; assigning
	 * either name updates both.
	 */
	integer minimum_loading;
	/**
	 * Waiting time shift, sint8. The same value as wait; assigning either name
	 * updates both.
	 */
	integer waiting_time_shift;
	/**
	 * Spacing shift, sint16.
	 */
	integer spacing_shift;
	/**
	 * Stable entry identity, uint16. Zero means "allocate a new id".
	 */
	integer unique_entry_id;
	/**
	 * Raw uint32 bitfield of schedule_entry_flag values.
	 */
	integer flags;
	/**
	 * Named flag values. Assigning a name updates the corresponding bit of
	 * flags; assigning flags updates all of these values.
	 */
	integer wait_for_time;
	integer lay_over;
	integer ignore_choose;
	integer force_range_stop;
	integer discharge_payload;
	integer set_down_only;
	integer pick_up_only;
	integer conditional_depart_before_wait;
	integer conditional_depart_after_wait;
	integer conditional_skip;
	integer send_trigger;
	integer cond_trigger_is_line_or_cnv;
	integer clear_stored_triggers_on_dep;
	integer trigger_one_only;
	integer couple;
	integer uncouple;
	integer couple_target_is_line_or_cnv;
	integer uncouple_target_is_line_or_cnv;
	integer uncouple_target_sch_is_reversed;
	integer couple_target_sch_is_reversed;
	/**
	 * Condition bitfields and targets, uint16.
	 */
	integer condition_bitfield_broadcaster;
	integer condition_bitfield_receiver;
	integer target_id_condition_trigger;
	integer target_id_couple;
	integer target_id_uncouple;
	integer target_unique_entry_uncouple;
	integer target_unique_entry_couple;
	/**
	 * Reversal marker: -1 undefined, 0 no, 1 yes.
	 */
	integer reverse;
	/**
	 * Per-entry speed limit in km/h, uint16. 65535 = unlimited.
	 */
	integer max_speed_kmh;
#endif

	/**
	 * Returns halt at this entry position.
	 * @param pl player that wants to use halt here
	 * @return halt instance
	 * @typemask halt_x(player_x)
	 */
	register_method(vm, &get_halt_from_koord3d, "get_halt", true);

	end_class(vm);

	/**
	 * Class holding the schedule
	 */
	create_class(vm, "schedule_x");

	/**
	 * Constructor
	 * @typemask command_x(way_types)
	 */
	register_function(vm, schedule_constructor, "constructor", 3, "xi.");
	sq_settypetag(vm, -1, param<schedule_t*>::tag());

#ifdef SQAPI_DOC // document members
	/**
	 * The list of schedule targets.
	 */
	array< schedule_entry_x > entries;

	/**
	 * Waytype of schedule.
	 */
	way_types waytype;

	/**
	 * Schedule-wide settings.
	 */
	integer bidirectional;
	integer mirrored;
	integer same_spacing_shift;
	integer spacing;
	/**
	 * Current schedule position (runtime state, preserved across round trips).
	 */
	integer current_stop;
	/**
	 * Consist orders, as a table keyed by unique_entry_id.
	 */
	table orders;
#else
	create_slot(vm, "entries", 0);
	create_slot(vm, "waytype", 0);
	create_slot(vm, "bidirectional", false);
	create_slot(vm, "mirrored", false);
	create_slot(vm, "same_spacing_shift", true);
	create_slot(vm, "spacing", 0);
	create_slot(vm, "current_stop", 0);
	create_slot(vm, "orders", 0);
#endif

	end_class(vm);
}
