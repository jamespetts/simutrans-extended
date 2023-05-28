/* This file is part of the Simutrans project under the artistic licence.
* (see licence.txt)
*
* @author: jamespetts, February 2018
*/

#include "consist_order_t.h"
#include "../simconvoi.h"

#include "../utils/cbuffer_t.h"

#include "loadsave.h"
#include "translator.h"

#include "../bauer/vehikelbauer.h"
#include "../vehicle/vehicle.h"


void consist_order_element_t::append_vehicle(const vehicle_desc_t *v)
{
	vehicle_description_element v_elem;
	v_elem.specific_vehicle = v;
	vehicle_description.append(v_elem);
}


bool consist_order_element_t::can_connect(const vehicle_desc_t *v, bool to_rear) const
{
	if (!get_count()) {
		// empty slot
		return to_rear ? v->can_lead(NULL) : v->can_follow(NULL);
	}
	uint32 test = vehicle_description.get_count(); //
	uint32 test2 = get_count(); //
	for (uint32 i = 0; i < vehicle_description.get_count(); i++) {
		const vehicle_desc_t *veh_type = vehicle_description[i].specific_vehicle;
		bool can_connect = to_rear ? v->can_follow(veh_type) : v->can_lead(veh_type);
		if (!can_connect) return false;
		if (veh_type) {
			// must be connectable to each other
			can_connect = to_rear ? veh_type->can_lead(v) : veh_type->can_follow(v);
			if (!can_connect) return false;
		}
	}
	return true;
}


bool consist_order_element_t::has_same_vehicle(const vehicle_desc_t *v) const
{
	for (uint32 i = 0; i < get_count(); i++) {
		if (vehicle_description[i].specific_vehicle == v) return true;
	}
	return false;
}

void consist_order_t::set_convoy_order(convoihandle_t cnv)
{
	orders.clear();
	if( !cnv.is_bound() ) { return; }

	// similar code in replace_frame_t::set_vehicles()
	array_tpl<vehicle_t*> veh_tmp_list; // To restore the order of vehicles that are reversing
	const uint8 vehicle_count = cnv->get_vehicle_count();
	veh_tmp_list.resize(vehicle_count, NULL);
	for (uint8 i = 0; i < vehicle_count; i++) {
		vehicle_t* dummy_veh = vehicle_builder_t::build(koord3d(), cnv->get_owner(), NULL, cnv->get_vehicle(i)->get_desc());
		//dummy_veh->set_current_livery(cnv->get_vehicle(i)->get_current_livery()); // Copying liverys schemes is not supported
		dummy_veh->set_reversed(cnv->get_vehicle(i)->is_reversed());
		veh_tmp_list[i] = dummy_veh;
	}
	// If convoy is reversed, reorder it
	if (cnv->is_reversed()) {
		// reverse_order
		bool reversable = convoi_t::get_terminal_shunt_mode(veh_tmp_list, vehicle_count) == convoi_t::change_direction ? true : false;
		convoi_t::execute_reverse_order(veh_tmp_list, vehicle_count, reversable);
	}

	for (uint8 i=0; i < vehicle_count; i++) {
		consist_order_element_t new_elem;
		new_elem.append_vehicle(veh_tmp_list[i]->get_desc());
		orders.append(new_elem);
	}
}


PIXVAL consist_order_t::get_constraint_state_color(uint32 element_number, bool rear_side)
{
	uint16 vechicle_count = 0;
	uint16 ok_count = 0;
	uint16 missing_count = 0;

	for (uint32 i = 0; i<orders[element_number].get_count(); i++) {
		const vehicle_desc_t *veh_type = orders[element_number].get_vehicle_description(i).specific_vehicle;
		if (veh_type) {
			const vehicle_desc_t *next_veh_type = NULL;
			if ((!rear_side && element_number == 0) || (rear_side && element_number ==orders.get_count()-1)) {
				vechicle_count++;
				// edge side constraint
				// => check can be at tail/head.
				const uint8 next_constraint_bits = rear_side ? veh_type->get_basic_constraint_next() : veh_type->get_basic_constraint_prev();
				if (next_constraint_bits & vehicle_desc_t::intermediate_unique) {
					missing_count++;
				}
				else if (rear_side  && (next_constraint_bits & vehicle_desc_t::can_be_tail)) ok_count++;
				else if (!rear_side && (next_constraint_bits & vehicle_desc_t::can_be_head)) ok_count++;
			}
			else {
				// check next element's vehicles
				uint32 next_index = rear_side ? element_number + 1: element_number - 1;
				for (uint32 j = 0; j<orders[next_index].get_count(); j++) {
					vechicle_count++;
					next_veh_type = orders[next_index].get_vehicle_description(j).specific_vehicle;
					if (rear_side && veh_type->can_lead(next_veh_type)) {
						ok_count++;
					}
					if (!rear_side && veh_type->can_follow(next_veh_type)) {
						ok_count++;
					}
				}
			}
		}
	}

	if (!vechicle_count) {
		return 0; // black
	}
	else if (vechicle_count == ok_count) {
		return COL_SAFETY;
	}
	else if (vechicle_count == (ok_count+missing_count)) {
		return COL_CAUTION;
	}
	else if (ok_count>0) {
		return COL_WARNING;
	}
	else if (vechicle_count) {
		return COL_DANGER;
	}
	return 0; // black
}


void consist_order_t::rdwr(loadsave_t* file)
{
	file->rdwr_short(tags_to_clear);

	uint32 orders_count = orders.get_count();
	file->rdwr_long(orders_count);

	if(file->is_saving())
	{
		for(auto order : orders)
		{
			file->rdwr_byte(order.catg_index);
			file->rdwr_bool(order.clear_all_tags);
			file->rdwr_short(order.tags_required);
			file->rdwr_short(order.tags_to_set);

			uint32 vehicle_description_count = order.vehicle_description.get_count();
			file->rdwr_long(vehicle_description_count);

			for(auto vehicle_description : order.vehicle_description)
			{
				// Save each element of the vehicle descriptors
				const char *s = vehicle_description.specific_vehicle ? vehicle_description.specific_vehicle->get_name() : "~BLANK~";
				file->rdwr_str(s);

				file->rdwr_bool(vehicle_description.empty);
				file->rdwr_byte(vehicle_description.engine_type);
				file->rdwr_byte(vehicle_description.min_catering);
				file->rdwr_byte(vehicle_description.max_catering);
				file->rdwr_byte(vehicle_description.must_carry_class);
				file->rdwr_long(vehicle_description.min_range);
				file->rdwr_long(vehicle_description.max_range);
				file->rdwr_short(vehicle_description.min_brake_force);
				file->rdwr_short(vehicle_description.max_brake_force);
				file->rdwr_long(vehicle_description.min_power);
				file->rdwr_long(vehicle_description.max_power);
				file->rdwr_long(vehicle_description.min_tractive_effort);
				file->rdwr_long(vehicle_description.max_tractive_effort);
				file->rdwr_long(vehicle_description.max_topspeed);
				file->rdwr_long(vehicle_description.min_topspeed);
				file->rdwr_long(vehicle_description.max_weight);
				file->rdwr_long(vehicle_description.min_weight);
				file->rdwr_long(vehicle_description.max_axle_load);
				file->rdwr_long(vehicle_description.min_axle_load);
				file->rdwr_short(vehicle_description.min_capacity);
				file->rdwr_short(vehicle_description.max_capacity);
				file->rdwr_long(vehicle_description.max_running_cost);
				file->rdwr_long(vehicle_description.min_running_cost);
				file->rdwr_long(vehicle_description.max_fixed_cost);
				file->rdwr_long(vehicle_description.min_fixed_cost);
				file->rdwr_long(vehicle_description.max_fuel_per_km);
				file->rdwr_long(vehicle_description.min_fuel_per_km);
				file->rdwr_long(vehicle_description.max_staff_hundredths);
				file->rdwr_long(vehicle_description.min_staff_hundredths);
				file->rdwr_long(vehicle_description.max_drivers);
				file->rdwr_long(vehicle_description.min_drivers);

				for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
				{
					file->rdwr_long(vehicle_description.rule_flags[i]);
				}
			}
		}
	}

	if (file->is_loading())
	{
		for (uint32 i = 0; i < orders_count; i++)
		{
			// Load each order
			consist_order_element_t order;

			file->rdwr_byte(order.catg_index);
			file->rdwr_bool(order.clear_all_tags);
			file->rdwr_short(order.tags_required);
			file->rdwr_short(order.tags_to_set);

			uint32 vehicle_description_count = 0;
			file->rdwr_long(vehicle_description_count);

			for(uint32 j = 0; j < vehicle_description_count; j ++)
			{
				vehicle_description_element vehicle_description;

				char vehicle_name[512];
				file->rdwr_str(vehicle_name, 512);
				const vehicle_desc_t* desc = vehicle_builder_t::get_info(vehicle_name);
				if(desc == nullptr)
				{
					desc = vehicle_builder_t::get_info(translator::compatibility_name(vehicle_name));
				}
				vehicle_description.specific_vehicle = desc;

				file->rdwr_bool(vehicle_description.empty);
				file->rdwr_byte(vehicle_description.engine_type);
				file->rdwr_byte(vehicle_description.min_catering);
				file->rdwr_byte(vehicle_description.max_catering);
				file->rdwr_byte(vehicle_description.must_carry_class);
				file->rdwr_long(vehicle_description.min_range);
				file->rdwr_long(vehicle_description.max_range);
				file->rdwr_short(vehicle_description.min_brake_force);
				file->rdwr_short(vehicle_description.max_brake_force);
				file->rdwr_long(vehicle_description.min_power);
				file->rdwr_long(vehicle_description.max_power);
				file->rdwr_long(vehicle_description.min_tractive_effort);
				file->rdwr_long(vehicle_description.max_tractive_effort);
				file->rdwr_long(vehicle_description.max_topspeed);
				file->rdwr_long(vehicle_description.min_topspeed);
				file->rdwr_long(vehicle_description.max_weight);
				file->rdwr_long(vehicle_description.min_weight);
				file->rdwr_long(vehicle_description.max_axle_load);
				file->rdwr_long(vehicle_description.min_axle_load);
				file->rdwr_short(vehicle_description.min_capacity);
				file->rdwr_short(vehicle_description.max_capacity);
				file->rdwr_long(vehicle_description.max_running_cost);
				file->rdwr_long(vehicle_description.min_running_cost);
				file->rdwr_long(vehicle_description.max_fixed_cost);
				file->rdwr_long(vehicle_description.min_fixed_cost);
				file->rdwr_long(vehicle_description.max_fuel_per_km);
				file->rdwr_long(vehicle_description.min_fuel_per_km);
				file->rdwr_long(vehicle_description.max_staff_hundredths);
				file->rdwr_long(vehicle_description.min_staff_hundredths);
				file->rdwr_long(vehicle_description.max_drivers);
				file->rdwr_long(vehicle_description.min_drivers);

				for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
				{
					file->rdwr_long(vehicle_description.rule_flags[i]);
				}

				order.vehicle_description.append(vehicle_description);
			}

			orders.append(order);
		}
	}
}

void consist_order_t::sprintf_consist_order(cbuffer_t &buf) const
{
	buf.append_fixed(tags_to_clear);
	buf.append_fixed(orders.get_count());
	for (auto order : orders)
	{
		buf.append_fixed(order.catg_index);
		buf.append_fixed(order.clear_all_tags == true ? (uint8)1 : (uint8)0);
		buf.append_fixed(order.tags_required);
		buf.append_fixed(order.tags_to_set);
		buf.append_fixed(order.vehicle_description.get_count());
		for(auto desc : order.vehicle_description)
		{
			buf.append(desc.specific_vehicle ? desc.specific_vehicle->get_name() : "NULL");
			buf.append("|");
			buf.append_fixed(desc.empty == true ? (uint8)1 : (uint8)0);
			buf.append_fixed(desc.engine_type);
			buf.append_fixed(desc.min_catering);
			buf.append_fixed(desc.must_carry_class);
			buf.append_fixed(desc.min_range);
			buf.append_fixed(desc.max_range);
			buf.append_fixed(desc.min_brake_force);
			buf.append_fixed(desc.max_brake_force);
			buf.append_fixed(desc.min_power);
			buf.append_fixed(desc.max_power);
			buf.append_fixed(desc.min_tractive_effort);
			buf.append_fixed(desc.max_tractive_effort);
			buf.append_fixed(desc.min_topspeed);
			buf.append_fixed(desc.max_topspeed);
			buf.append_fixed(desc.max_weight);
			buf.append_fixed(desc.min_weight);
			buf.append_fixed(desc.max_axle_load);
			buf.append_fixed(desc.min_axle_load);
			buf.append_fixed(desc.min_capacity);
			buf.append_fixed(desc.max_capacity);
			buf.append_fixed(desc.max_running_cost);
			buf.append_fixed(desc.min_running_cost);
			buf.append_fixed(desc.max_fixed_cost);
			buf.append_fixed(desc.min_fixed_cost);
			buf.append_fixed(desc.max_fuel_per_km);
			buf.append_fixed(desc.min_fuel_per_km);
			buf.append_fixed(desc.max_staff_hundredths);
			buf.append_fixed(desc.min_staff_hundredths);
			buf.append_fixed(desc.max_drivers);
			buf.append_fixed(desc.min_drivers);

			for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
			{
				buf.append_fixed(desc.rule_flags[i]);
			}
		}
	}
}

const char* consist_order_t::sscanf_consist_order(const char* ptr)
{
	const char *p = ptr;
	orders.clear();

	tags_to_clear = cbuffer_t::decode_uint16(p);

	// Retrieve the count
	const uint32 order_count = cbuffer_t::decode_uint32(p);

	// Now use the count to determine the number of orders retrieved
	for(uint32 i = 0;  i < order_count; i ++)
	{
		consist_order_element_t element;

		element.catg_index = cbuffer_t::decode_uint8(p);
		element.clear_all_tags = cbuffer_t::decode_uint8(p);
		element.tags_required = cbuffer_t::decode_uint16(p);
		element.tags_to_set = cbuffer_t::decode_uint16(p);

		uint32 vehicle_description_count = cbuffer_t::decode_uint32(p);
		for(uint32 j = 0; j < vehicle_description_count; j ++)
		{
			vehicle_description_element desc;
			char vehicle_name[256];
			uint8 n = 0;
			while(*p!='|' && *p!='\0')
			{
				vehicle_name[n++] = *p++;
			}
			vehicle_name[n] = '\0';

			const vehicle_desc_t* vehicle_desc = vehicle_builder_t::get_info(vehicle_name);
			if (strcmp(vehicle_name, "NULL"))
			{
				if (vehicle_desc == NULL)
				{
					vehicle_desc = vehicle_builder_t::get_info(translator::compatibility_name(vehicle_name));
				}
				if (vehicle_desc == NULL)
				{
					dbg->warning("consist_order_t::sscanf_consist_order()", "no vehicle found when searching for '%s', but this was not intended to be a blank consist order slot", vehicle_name);
				}
				else
				{
					desc.specific_vehicle = vehicle_desc;
				}
			}
			else
			{
				desc.specific_vehicle = NULL;
			}
			p++;

			desc.empty = cbuffer_t::decode_uint8(p);
			desc.engine_type = cbuffer_t::decode_uint8(p);
			desc.min_catering = cbuffer_t::decode_uint8(p);
			desc.must_carry_class = cbuffer_t::decode_uint8(p);
			desc.min_range = cbuffer_t::decode_uint32(p);
			desc.max_range = cbuffer_t::decode_uint32(p);
			desc.min_brake_force = cbuffer_t::decode_uint16(p);
			desc.max_brake_force = cbuffer_t::decode_uint16(p);
			desc.min_power = cbuffer_t::decode_uint32(p);
			desc.max_power = cbuffer_t::decode_uint32(p);
			desc.min_tractive_effort = cbuffer_t::decode_uint32(p);
			desc.max_tractive_effort = cbuffer_t::decode_uint32(p);
			desc.min_topspeed = cbuffer_t::decode_uint32(p);
			desc.max_topspeed = cbuffer_t::decode_uint32(p);
			desc.max_weight = cbuffer_t::decode_uint32(p);
			desc.min_weight = cbuffer_t::decode_uint32(p);
			desc.max_axle_load = cbuffer_t::decode_uint32(p);
			desc.min_axle_load = cbuffer_t::decode_uint32(p);
			desc.min_capacity = cbuffer_t::decode_uint16(p);
			desc.max_capacity = cbuffer_t::decode_uint16(p);
			desc.max_running_cost = cbuffer_t::decode_uint32(p);
			desc.min_running_cost = cbuffer_t::decode_uint32(p);
			desc.max_fixed_cost = cbuffer_t::decode_uint32(p);
			desc.min_fixed_cost = cbuffer_t::decode_uint32(p);
			desc.max_fuel_per_km = cbuffer_t::decode_uint32(p);
			desc.min_fuel_per_km = cbuffer_t::decode_uint32(p);
			desc.max_staff_hundredths = cbuffer_t::decode_uint32(p);
			desc.min_staff_hundredths = cbuffer_t::decode_uint32(p);
			desc.max_drivers = cbuffer_t::decode_uint32(p);
			desc.min_drivers = cbuffer_t::decode_uint32(p);

			for (uint32 k = 0; k < vehicle_description_element::max_rule_flags; k++)
			{
				desc.rule_flags[k] = cbuffer_t::decode_uint32(p);
			}

			element.vehicle_description.append(desc);
		}
		orders.append(element);
	}

	return p;
}

bool consist_order_t::operator== (const consist_order_t& other) const
{
	if (tags_to_clear == other.tags_to_clear && orders.get_count() == other.orders.get_count())
	{
		// Check each individual element
		for(uint32 i = 0; i < orders.get_count(); i ++)
		{
			const consist_order_element_t this_element = orders.get_element(i);
			const consist_order_element_t other_element = other.orders.get_element(i);
			if (this_element != other_element)
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		return false;
	}
}

bool consist_order_element_t::operator!= (const consist_order_element_t& other) const
{
	if (catg_index == other.catg_index && clear_all_tags == other.clear_all_tags && tags_required == other.tags_required && tags_to_set == other.tags_to_set && vehicle_description.get_count() == other.vehicle_description.get_count())
	{
		// Check each individual vehicle description element
		// ...
		for (uint32 i = 0; i < vehicle_description.get_count(); i++)
		{
			const vehicle_description_element this_element = vehicle_description.get_element(i);
			const vehicle_description_element other_element = other.vehicle_description.get_element(i);
			if (this_element != other_element)
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return true;
	}

}

bool vehicle_description_element::operator!= (const vehicle_description_element& other) const
{
	bool return_value =
		specific_vehicle != other.specific_vehicle ||
		empty != other.empty ||
		engine_type != other.engine_type ||
		min_catering != other.min_catering ||
		max_catering != other.max_catering ||
		must_carry_class != other.must_carry_class ||
		min_range != other.min_range ||
		max_range != other.max_range ||
		min_brake_force != other.min_brake_force ||
		max_brake_force != other.max_brake_force ||
		min_power != other.min_power ||
		max_power != other.max_power ||
		min_tractive_effort != other.min_tractive_effort ||
		max_tractive_effort != other.max_tractive_effort ||
		max_weight != other.max_weight ||
		min_weight != other.min_weight ||
		max_axle_load != other.max_axle_load ||
		min_axle_load != other.min_axle_load ||
		min_capacity != other.min_capacity ||
		max_capacity != other.max_capacity ||
		max_running_cost != other.max_running_cost ||
		min_running_cost != other.min_running_cost ||
		max_fixed_cost != other.max_fixed_cost ||
		min_fixed_cost != other.min_fixed_cost ||
		max_fuel_per_km != other.max_fuel_per_km ||
		min_fuel_per_km != other.min_fuel_per_km ||
		max_staff_hundredths != other.max_staff_hundredths ||
		min_staff_hundredths != other.min_staff_hundredths ||
		max_drivers != other.max_drivers ||
		min_drivers != other.min_drivers;

	for (uint32 i = 0; i < max_rule_flags; i++)
	{
		if (rule_flags[i] != other.rule_flags[i])
		{
			return true;
		}
	}

	return return_value;
}
