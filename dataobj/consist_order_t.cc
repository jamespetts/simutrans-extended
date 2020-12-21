/* This file is part of the Simutrans project under the artistic licence.
* (see licence.txt)
*
* @author: jamespetts, February 2018
*/

#include "consist_order_t.h"

#include "../utils/cbuffer_t.h"

#include "loadsave.h"
#include "translator.h"

#include "../descriptor/vehicle_desc.h"
#include "../bauer/vehikelbauer.h"

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

				for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
				{
					file->rdwr_short(vehicle_description.rule_flags[i]); 
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

				for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
				{
					file->rdwr_short(vehicle_description.rule_flags[i]); 
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
		buf.append_bool(order.clear_all_tags);
		buf.append_fixed(order.tags_required);
		buf.append_fixed(order.tags_to_set);
		buf.append_fixed(order.vehicle_description.get_count()); 
		for(auto desc : order.vehicle_description)
		{
			buf.append(desc.specific_vehicle ? desc.specific_vehicle->get_name() : "NULL"); 
			buf.append_bool(desc.empty);
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
			for(uint32 i = 0; i < vehicle_description_element::max_rule_flags; i ++)
			{
				buf.append_fixed(desc.rule_flags[i]); 
			}
		}
	}
}

void consist_order_t::sscanf_consist_order(const char* ptr)
{
	const char *p = ptr;
	orders.clear();

	tags_to_clear = cbuffer_t::decode_uint16(p);

	// Retrieve the count
	uint32 order_count = cbuffer_t::decode_uint32(p);

	// Now use the count to determine the number of orders retrieved
	for(uint32 i = 0;  i < order_count; i ++)
	{
		consist_order_element_t element;
		
		element.catg_index = cbuffer_t::decode_uint8(p); 
		element.clear_all_tags = cbuffer_t::decode_bool(p);
		element.tags_required = cbuffer_t::decode_uint16(p); 
		element.tags_to_set = cbuffer_t::decode_uint16(p); 

		uint32 vehicle_description_count = cbuffer_t::decode_uint32(p); 
		for(uint32 j = 0; j < vehicle_description_count; j ++)
		{
			vehicle_description_element desc;
			char vehicle_name[256];
			uint8 n = 0;
			while(*p!='|')
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
			for (uint32 k = 0; k < vehicle_description_element::max_rule_flags; k++)
			{
				desc.rule_flags[k] = cbuffer_t::decode_uint16(p); 
			}

			element.vehicle_description.append(desc);
		}
		orders.append(element);
	}
}