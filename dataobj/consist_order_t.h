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

	// Note that this is the *minimum* class that the vehicle carries.
	// In other words, if the vehicle carries more than one class,
	// the higher class is ignored.
	// This stipulation is necessary for the path explorer.
	uint8 must_carry_class = 0;

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

	void set_empty(bool yesno) { empty=yesno; }

	bool operator!= (const vehicle_description_element& other) const;
};

class consist_order_element_t
{
	friend class consist_order_t;
protected:
	/*
	* The goods category of the vehicle that must occupy this slot.
	* COMPULSORY
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
	consist_order_element_t() { vehicle_description.clear(); }

	// Wildcard category: skips the category check in matching. Slots with this
	// category contribute nothing to the path-explorer carried-category tables,
	// as the carried set cannot be known in advance.
	static const uint8 any_catg_index = 255;

	uint32 get_count() const { return vehicle_description.get_count(); }

	uint8 get_catg_index() const { return catg_index; }

	void set_catg_index(uint8 catg) { catg_index = catg; }

	void append_vehicle(const vehicle_desc_t *v);

	// Append a rule-based alternative (no specific vehicle: the hard-constraint
	// ranges select the vehicle). Enforced (empty=false) so the rule actually
	// constrains matching; use set_empty afterwards for an optional slot.
	// Engine defaults to "any" (MAX_TRACTION_TYPE, the matcher wildcard).
	void append_rule()
	{
		vehicle_description_element rule;
		rule.specific_vehicle = nullptr;
		rule.empty = false;
		rule.engine_type = vehicle_desc_t::MAX_TRACTION_TYPE;
		vehicle_description.append(rule);
	}

	void remove_vehicle_description_at(uint32 description_index)
	{
		if (description_index >= vehicle_description.get_count()) { return; }
		vehicle_description.remove_at(description_index, false);
	}

	void clear_vehicles()
	{
		vehicle_description.clear();
	}

	vehicle_description_element &access_vehicle_description(uint32 description_index)
	{
		return vehicle_description.get_element(description_index);
	}

	const vehicle_description_element& get_vehicle_description(uint32 description_index) const
	{
		return vehicle_description.get_element(description_index);
	}

	// Returns whether the passed vehicle can be connected to the rear or front of this element
	bool can_connect(const vehicle_desc_t *v, bool to_rear=true) const;

	// True if the vehicle_description has the same vehicles
	bool has_same_vehicle(const vehicle_desc_t *v) const;

	bool operator!= (const consist_order_element_t& other) const;
	bool operator== (const consist_order_element_t& other) const;
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

	/*
	* Transient mutation counter for GUI refresh. Incremented by every
	* consist_order_t-level mutator so views can detect changes (e.g. slot
	* reordering) that do not alter the element count. Never serialised,
	* never transmitted, never compared: it rides along with by-value copies
	* but is only ever compared against itself in the GUI that owns the copy.
	*/
	uint32 mod_count = 0;

public:

	consist_order_element_t& access_order(uint32 element_number)
	{
		return orders[element_number];
	}

	const consist_order_element_t& get_order(uint32 element_number) const
	{
		assert(element_number < orders.get_count());
		if (element_number > orders.get_count()) { element_number = 0; }
		return orders[element_number];
	}

	void rdwr(loadsave_t* file);

	uint32 get_count() const { return orders.get_count(); }

	uint32 get_mod_count() const { return mod_count; }

	// Bump the mutation counter without structural change: rule-editor value
	// edits refresh the GUI views, which watch the counter.
	void touch() { mod_count++; }

	void append(consist_order_element_t elem)
	{
		orders.append(elem);
		mod_count++;
		return;
	}

	void insert_at(uint32 pos, consist_order_element_t elem)
	{
		if (!get_count() || pos >= get_count()) {
			orders.append(elem);
		}
		else {
			orders.insert_at(pos, elem);
		}
		mod_count++;
		return;
	}

	void append_vehicle_at(uint32 element_number, const vehicle_desc_t *v)
	{
		// NOTE: deliberately does not bump mod_count. Appending an
		// alternative description to an existing slot is refreshed by the
		// slot widget itself; creating a new slot changes the element count,
		// which the views already watch. Bumping here would needlessly clear
		// the vehicle-picker selection on every alternative added.
		if (element_number < orders.get_count()) {
			orders[element_number].append_vehicle(v);
		}
		else {
			consist_order_element_t new_elem;
			new_elem.append_vehicle(v);
			orders.append(new_elem);
		}
		return;
	}

	void remove_order(uint32 element_number)
	{
		orders.remove_at(element_number);
		mod_count++;
		return;
	}

	// Move the slot at index "from" so that it ends up at index "to",
	// shifting the slots in between. Out-of-range indices and from == to
	// are ignored. Used by the consist-order GUI slot-reorder buttons.
	void move_element(uint32 from, uint32 to);

	// Copy order from specific convoy
	void set_convoy_order(convoihandle_t cnv);

	// Determine if a combination of vehicles in consecutive order elements is possible
	PIXVAL get_constraint_state_color(uint32 element_number, bool rear_side = true);

	void sprintf_consist_order(cbuffer_t &buf) const;
	const char* sscanf_consist_order(const char* ptr);

	bool operator== (const consist_order_t& other) const;
};

#endif
