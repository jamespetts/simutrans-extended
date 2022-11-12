/* This file is part of the Simutrans project under the artistic licence.
* (see licence.txt)
*
* @author: jamespetts, February 2018
*/

#ifndef DATAOBJ_CONSIST_ORDER_T_H
#define DATAOBJ_CONSIST_ORDER_T_H


#include "../bauer/goods_manager.h"
#include "../convoihandle_t.h"
#include "../descriptor/vehicle_desc.h"

class vehicle_desc_t;
class loadsave_t;
class cbuffer_t;

struct vehicle_description_element
{
	/*
	* If a specific vehicle is required in this slot,
	* this is specified. If not, this is a nullptr, and
	* the rules for vehicle selection are used instead.
	* If this is != nullptr, the rules below here are
	* ignored.
	*/
	const vehicle_desc_t* specific_vehicle = nullptr;

	/*
	* If this is set to true, the vehicle slot is empty.
	* This means that it is permissible for the convoy as
	* assembled by this consist order to have no vehicle
	* in this slot.
	*/
	bool empty = true;

	/* Rules hereinafter */

	uint8 engine_type = 0;

	uint8 min_catering = 0;
	uint8 max_catering = 255;

	// If this is 255, then this vehicle may carry
	// any class of mail/passengers.
	uint8 must_carry_class = 255;

	uint32 min_range = 0;
	uint32 max_range = UINT32_MAX_VALUE;

	uint16 min_brake_force = 0;
	uint16 max_brake_force = 65535;
	uint32 min_power = 0;
	uint32 max_power = UINT32_MAX_VALUE;
	uint32 min_tractive_effort = 0;
	uint32 max_tractive_effort = UINT32_MAX_VALUE;
	uint32 min_topspeed = 0;
	uint32 max_topspeed = UINT32_MAX_VALUE;

	uint32 max_weight = UINT32_MAX_VALUE;
	uint32 min_weight = 0;
	uint32 max_axle_load = UINT32_MAX_VALUE;
	uint32 min_axle_load = 0;

	uint16 min_capacity = 0;
	uint16 max_capacity = 65535;

	uint32 max_running_cost = UINT32_MAX_VALUE;
	uint32 min_running_cost = 0;
	uint32 max_fixed_cost = UINT32_MAX_VALUE;
	uint32 min_fixed_cost = 0;

	uint32 max_fuel_per_km = UINT32_MAX_VALUE;
	uint32 min_fuel_per_km = 0;

	uint32 max_staff_hundredths = UINT32_MAX_VALUE;
	uint32 min_staff_hundredths = 0;
	uint32 max_drivers = UINT32_MAX_VALUE;
	uint32 min_drivers = 0;

	/*
	* These rules define which of available
	* vehicles are to be preferred, and do
	* not affect what vehicles will be selected
	* if <=1 vehicles matching the above rules
	* are available
	*
	* The order of the priorities can be customised
	* by changing the position of each of the preferences
	* in the array. The default order is that in which
	* the elements are arranged above.
	*
	* + Where the vehicle is a goods carrying vehicle: otherwise, this is ignored
	*/

	enum rule_flag
	{
		prefer_high_capacity			= (1u << 0),
		prefer_high_power				= (1u << 1),
		prefer_high_tractive_effort		= (1u << 2),
		prefer_high_brake_force			= (1u << 3),
		prefer_high_speed				= (1u << 4),
		prefer_high_running_cost		= (1u << 5),
		prefer_high_fixed_cost			= (1u << 6),
		prefer_high_fuel_consumption	= (1u << 7),
		prefer_high_driver_numbers		= (1u << 8),
		prefer_high_staff_hundredths	= (1u << 9),
		prefer_low_capacity				= (1u << 10),
		prefer_low_power				= (1u << 11),
		prefer_low_tractive_effort		= (1u << 12),
		prefer_low_brake_force			= (1u << 13),
		prefer_low_speed				= (1u << 14),
		prefer_low_running_cost			= (1u << 15),
		prefer_low_fixed_cost			= (1u << 16),
		prefer_low_fuel_consumption		= (1u << 17),
		prefer_low_driver_numbers		= (1u << 18),
		prefer_low_staff_hundredths		= (1u << 19)
	};

	static const uint8 max_rule_flags = 20u;

	uint32 rule_flags[max_rule_flags]
	{ prefer_high_capacity,
		prefer_high_power,
		prefer_high_tractive_effort,
		prefer_high_speed,
		prefer_high_running_cost,
		prefer_high_fixed_cost,
		prefer_high_fuel_consumption,
		prefer_high_driver_numbers,
		prefer_high_staff_hundredths,
		prefer_low_capacity,
		prefer_low_power,
		prefer_low_tractive_effort,
		prefer_low_speed,
		prefer_low_running_cost,
		prefer_low_fixed_cost,
		prefer_low_fuel_consumption,
		prefer_low_driver_numbers,
		prefer_low_staff_hundredths
	};

	void set_vehicle_spec(const vehicle_desc_t *v)
	{
		engine_type  = v->get_engine_type();
		min_catering = v->get_catering_level();
		//must_carry_class
		//min_range
		min_brake_force = v->get_brake_force();
		min_power = v->get_power();
		//min_tractive_effort;
		min_topspeed = v->get_calibration_speed();
		min_capacity = v->get_total_capacity();
	}

	void set_empty(bool yesno) { empty=yesno; }

	bool operator!= (const vehicle_description_element& other) const;
};

class consist_order_element_t
{
	friend class consist_order_t;
protected:
	/*
	* The goods category of the vehicle that must occupy this slot.
	*/
	uint8 catg_index = goods_manager_t::INDEX_NONE;

	/*
	* All vehicle tags are cleared on the vehicle joining at this slot
	* if this is set to true here.
	*/
	bool clear_all_tags = false;

	/*
	* A bitfield of the tags necessary for a loose vehicle to be
	* allowed to couple to this convoy.
	*/
	uint16 tags_required = 0;

	/*
	* A bitfield of the vehicle tags to be set for the vehicle that
	* occupies this slot when it is attached to this convoy by this order.
	*/
	uint16 tags_to_set = 0;

	vector_tpl<vehicle_description_element> vehicle_description;

public:
	uint32 get_count() const { return vehicle_description.get_count(); }

	void append_vehicle(const vehicle_desc_t *v, bool is_specific=true);

	void append_vehicle_description(vehicle_description_element v_elem) { vehicle_description.append(v_elem); }
	void insert_vehicle_description_at(uint32 description_index, vehicle_description_element v_elem) { vehicle_description.insert_at(description_index, v_elem); }
	void remove_vehicle_description_at(uint32 description_index)
	{
		if (description_index >= vehicle_description.get_count()) { return; }
		vehicle_description.remove_at(description_index, false);
	}

	void clear_vehicles()
	{
		vehicle_description.clear();
	}

	void increment_index(uint32 description_index);

	vehicle_description_element &get_vehicle_description(uint32 description_index)
	{
		return vehicle_description.get_element(description_index);
	}

	bool operator!= (const consist_order_element_t& other) const;
};

class consist_order_t
{
	friend class schedule_t;
protected:
	/*
	* The unique ID of the schedule entry to which this consist order refers
	* is stored as the key of the hashtable in which these are stored, and
	* thus need not be stored here.
	*/

	/* The tags that are to be cleared on _all_ vehicles
	* (whether loose or not) in this convoy after the execution
	* of this consist order.
	*/
	uint16 tags_to_clear = 0;

	vector_tpl<consist_order_element_t> orders;

public:

	consist_order_element_t& get_order(uint32 element_number)
	{
		if (element_number > orders.get_count()) { element_number=0; }
		return orders[element_number];
	}

	void rdwr(loadsave_t* file);

	uint32 get_count() const { return orders.get_count(); }

	void append(consist_order_element_t elem)
	{
		orders.append(elem);
		return;
	}

	void remove_order(uint32 element_number)
	{
		orders.remove_at(element_number);
		return;
	}

	// Copy order from specific convoy
	// specific_vehicle option: restrict specific vehicle or copy required specs.
	void set_convoy_order(uint32 element_number, convoihandle_t cnv, bool specific_vehicle=true);

	void sprintf_consist_order(cbuffer_t &buf) const;
	void sscanf_consist_order(const char* ptr);

	bool operator== (const consist_order_t& other) const;
};

#endif
