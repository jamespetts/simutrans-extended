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


// Out-of-line definition required by C++14: any_catg_index is odr-used where it is
// passed to a const-reference parameter (e.g. vector_tpl::append in the rule editor),
// which clang does not fold away as a constant expression.
const uint8 consist_order_element_t::any_catg_index;


bool consist_order_element_t::set_catg_index(uint8 catg)
{
	if (catg != any_catg_index) {
		for (uint32 i = 0; i < vehicle_description.get_count(); i++) {
			const vehicle_desc_t *v = vehicle_description[i].specific_vehicle;
			if (v && v->get_freight_type()->get_catg_index() != catg) {
				return false;
			}
		}
	}
	catg_index = catg;
	return true;
}


bool consist_order_element_t::append_vehicle(const vehicle_desc_t *v)
{
	const uint8 v_catg = v->get_freight_type()->get_catg_index();
	if (vehicle_description.get_count()) {
		// The slot's category is fixed by its first alternative.
		if (catg_index != any_catg_index  &&  catg_index != v_catg) {
			return false;
		}
	}
	else {
		catg_index = v_catg;
	}
	vehicle_description_element v_elem;
	v_elem.specific_vehicle = v;
	vehicle_description.append(v_elem);
	return true;
}


// Whether v may stand next to "other" in a consist, in the direction the order
// is written: behind "other" when to_rear is set, in front of it otherwise. A
// consist order records vehicle types but not their facing, so the editor
// cannot tell which way round a bidirectional vehicle will be: accept it when
// it connects either way round. A null "other" means the element has no other
// vehicle, so v stands at the end of the train and must suit that end.
static bool vehicle_fits(const vehicle_desc_t *v, const vehicle_desc_t *other, bool to_rear)
{
	if (!v)
	{
		return false;
	}
	if (!other)
	{
		return to_rear ? v->can_lead(nullptr) : v->can_follow(nullptr);
	}
	// The neighbour's own end does not depend on which way round v is.
	if (!(to_rear ? other->can_lead(v) : other->can_follow(v)))
	{
		return false;
	}
	if (to_rear ? v->can_follow(other) : v->can_lead(other))
	{
		return true;
	}
	// The other way round, v's other end meets this neighbour.
	return v->is_bidirectional() && (to_rear ? v->can_lead(other) : v->can_follow(other));
}


bool consist_order_element_t::can_connect(const vehicle_desc_t *v, bool to_rear) const
{
	if (!get_count())
	{
		// empty slot: the vehicle stands at the end of the train
		return vehicle_fits(v, nullptr, to_rear);
	}
	for (uint32 i = 0; i < vehicle_description.get_count(); i++)
	{
		if (!vehicle_fits(v, vehicle_description[i].specific_vehicle, to_rear)) return false;
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
	mod_count++;
	if( !cnv.is_bound() ) { return; }

	// similar code in replace_frame_t::set_vehicles()
	array_tpl<vehicle_t*> veh_tmp_list; // To restore the order of vehicles that are reversing
	const uint8 vehicle_count = cnv->get_vehicle_count();
	veh_tmp_list.resize(vehicle_count, NULL);
	for (uint8 i = 0; i < vehicle_count; i++) {
		vehicle_t* dummy_veh = vehicle_builder_t::build(koord3d(), cnv->get_owner(), NULL, cnv->get_vehicle(i)->get_desc());
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
	mod_count++;
}


void consist_order_t::move_element(uint32 from, uint32 to)
{
	const uint32 count = orders.get_count();
	if (from >= count || to >= count || from == to) {
		return;
	}
	consist_order_element_t elem = orders[from];
	orders.remove_at(from);
	orders.insert_at(to, elem);
	mod_count++;
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


// The descriptors a slot can be filled with, in the order the matcher tries
// them. A named vehicle is always available. A rule is resolved against the
// pool; a slot whose alternative is permitted to be empty accepts any vehicle
// of the pool. When the pool is empty a rule cannot be judged, which the
// caller reports rather than treating as a failure.
static void collect_slot_candidates(const consist_order_element_t &element, const vector_tpl<const vehicle_desc_t*> &pool, vector_tpl<const vehicle_desc_t*> &out, bool &unjudged)
{
	const uint8 catg = element.get_catg_index();
	for (uint32 i = 0; i < element.get_count(); i++)
	{
		const vehicle_description_element &vde = element.get_vehicle_description(i);
		if (vde.specific_vehicle)
		{
			if (!out.is_contained(vde.specific_vehicle))
			{
				out.append(vde.specific_vehicle);
			}
			continue;
		}
		if (pool.empty())
		{
			// Nothing to test a rule against.
			unjudged = true;
			continue;
		}
		for (auto const desc : pool)
		{
			if ((vde.empty || vehicle_t::desc_matches_rule(desc, catg, vde)) && !out.is_contained(desc))
			{
				out.append(desc);
			}
		}
	}
}


// Whether "rear" may stand immediately in front of "front", in the direction
// the order is written. These are the descriptor rules the runtime matcher
// applies (convoi_t::consist_slot_couples): an articulated vehicle only couples
// to its own group, and an ordinary vehicle couples unless the touching ends
// are marked unconnectable.
static bool descriptors_couple(const vehicle_desc_t *rear, const vehicle_desc_t *front)
{
	return rear->can_lead(front) && front->can_follow(rear);
}


// Advisory reachability for an order that permits slots to be omitted. The
// runtime matcher is authoritative; this check tests whether candidates can
// connect across any run of optional slots and retains the existing end and
// power checks.
static consist_order_check_t check_order_with_optional(const consist_order_t &order, const vector_tpl<const vehicle_desc_t*> &pool)
{
	consist_order_check_t result;
	const uint32 count = order.get_count();
	vector_tpl<vector_tpl<const vehicle_desc_t*> > candidates;
	bool unjudged = false;
	for (uint32 i = 0; i < count; i++)
	{
		vector_tpl<const vehicle_desc_t*> slot;
		collect_slot_candidates(order.get_order(i), pool, slot, unjudged);
		if (slot.empty() && !order.get_order(i).is_optional())
		{
			if (unjudged)
			{
				result.unjudged = true;
				return result;
			}
			result.reason = order.get_order(i).get_count() == 0
				? consist_order_check_t::slot_without_vehicle
				: consist_order_check_t::slot_without_available_vehicle;
			result.slot = i;
			return result;
		}
		candidates.append(slot);
	}
	if (unjudged)
	{
		result.unjudged = true;
		return result;
	}

	vector_tpl<vector_tpl<const vehicle_desc_t*> > forward;
	vector_tpl<vector_tpl<const vehicle_desc_t*> > backward;
	forward.set_count(count);
	backward.set_count(count);
	for (uint32 i = 0; i < count; i++)
	{
		bool front_is_possible = true;
		for (auto desc : candidates[i])
		{
			bool reachable = false;
			sint32 previous = (sint32)i - 1;
			for (; previous >= 0 && !reachable; previous--)
			{
				for (auto before : forward[previous])
				{
					if (descriptors_couple(before, desc))
					{
						reachable = true;
						break;
					}
				}
				if (!order.get_order(previous).is_optional())
				{
					front_is_possible = false;
					break;
				}
			}
			if (front_is_possible && desc->can_follow(nullptr))
			{
				reachable = true;
			}
			if (reachable && !forward[i].is_contained(desc))
			{
				forward[i].append(desc);
			}
		}
		if (forward[i].empty() && !order.get_order(i).is_optional())
		{
			result.reason = front_is_possible && i == 0
				? consist_order_check_t::front_cannot_lead
				: consist_order_check_t::slot_does_not_connect;
			result.slot = i;
			return result;
		}
	}
	for (uint32 reverse = count; reverse-- > 0;)
	{
		bool rear_is_possible = true;
		for (auto desc : candidates[reverse])
		{
			bool reachable = false;
			for (uint32 next = reverse + 1; next < count && !reachable; next++)
			{
				for (auto after : backward[next])
				{
					if (descriptors_couple(desc, after))
					{
						reachable = true;
						break;
					}
				}
				if (!order.get_order(next).is_optional())
				{
					rear_is_possible = false;
					break;
				}
			}
			if (rear_is_possible && desc->can_lead(nullptr))
			{
				reachable = true;
			}
			if (reachable && !backward[reverse].is_contained(desc))
			{
				backward[reverse].append(desc);
			}
		}
		if (backward[reverse].empty() && !order.get_order(reverse).is_optional())
		{
			result.reason = rear_is_possible && reverse + 1 == count
				? consist_order_check_t::rear_cannot_be_last
				: consist_order_check_t::slot_does_not_connect;
			result.slot = reverse;
			return result;
		}
	}
	for (uint32 i = 0; i < count; i++)
	{
		for (auto desc : forward[i])
		{
			if (desc->get_power() > 0 && backward[i].is_contained(desc))
			{
				return result;
			}
		}
	}
	result.reason = consist_order_check_t::no_power;
	return result;
}


consist_order_check_t consist_order_t::check(const vector_tpl<const vehicle_desc_t*> &pool) const
{
	for (uint32 i = 0; i < orders.get_count(); i++)
	{
		if (orders[i].is_optional())
		{
			return check_order_with_optional(*this, pool);
		}
	}

	consist_order_check_t result;
	const uint32 count = orders.get_count();
	if (count == 0)
	{
		result.reason = consist_order_check_t::empty_list;
		return result;
	}

	// The candidate vehicles of every slot. A rule that cannot be judged leaves
	// the order unjudged as a whole: the chain and the ends depend on it.
	vector_tpl<vector_tpl<const vehicle_desc_t*> > candidates;
	bool unjudged = false;
	for (uint32 i = 0; i < count; i++)
	{
		vector_tpl<const vehicle_desc_t*> slot;
		collect_slot_candidates(orders[i], pool, slot, unjudged);
		if (slot.empty())
		{
			if (unjudged)
			{
				result.unjudged = true;
				return result;
			}
			result.reason = orders[i].get_count() == 0
				? consist_order_check_t::slot_without_vehicle
				: consist_order_check_t::slot_without_available_vehicle;
			result.slot = i;
			return result;
		}
		candidates.append(slot);
	}
	if (unjudged)
	{
		result.unjudged = true;
		return result;
	}

	// forward[i]: the candidates of slot i that can follow some candidate of
	// slot i-1 (for slot 0, those that can be at the front of the train).
	vector_tpl<vector_tpl<const vehicle_desc_t*> > forward;
	{
		vector_tpl<const vehicle_desc_t*> front;
		for (auto const desc : candidates[0])
		{
			if (desc->can_follow(nullptr))
			{
				front.append(desc);
			}
		}
		if (front.empty())
		{
			result.reason = consist_order_check_t::front_cannot_lead;
			result.slot = 0;
			return result;
		}
		forward.append(front);
	}
	for (uint32 i = 1; i < count; i++)
	{
		vector_tpl<const vehicle_desc_t*> reachable;
		for (auto const desc : candidates[i])
		{
			for (auto const previous : forward[i - 1])
			{
				if (descriptors_couple(previous, desc))
				{
					reachable.append(desc);
					break;
				}
			}
		}
		if (reachable.empty())
		{
			result.reason = consist_order_check_t::slot_does_not_connect;
			result.slot = i;
			return result;
		}
		forward.append(reachable);
	}

	// backward_rev[k]: the candidates of the slot k places from the rear that
	// can be followed by some candidate of the slot behind them (for the last
	// slot, those that can be at the rear of the train).
	vector_tpl<vector_tpl<const vehicle_desc_t*> > backward_rev;
	{
		vector_tpl<const vehicle_desc_t*> rear;
		for (auto const desc : candidates[count - 1])
		{
			if (desc->can_lead(nullptr))
			{
				rear.append(desc);
			}
		}
		if (rear.empty())
		{
			result.reason = consist_order_check_t::rear_cannot_be_last;
			result.slot = count - 1;
			return result;
		}
		backward_rev.append(rear);
	}
	for (uint32 k = 1; k < count; k++)
	{
		const uint32 i = count - 1 - k;
		vector_tpl<const vehicle_desc_t*> reachable;
		for (auto const desc : candidates[i])
		{
			for (auto const next : backward_rev[k - 1])
			{
				if (descriptors_couple(desc, next))
				{
					reachable.append(desc);
					break;
				}
			}
		}
		backward_rev.append(reachable);
	}

	// A candidate can be part of a complete train only when it is reachable
	// from both ends; power must come from such a candidate, not from a vehicle
	// that could never be placed.
	bool has_power = false;
	for (uint32 i = 0; i < count && !has_power; i++)
	{
		for (auto const desc : forward[i])
		{
			if (desc->get_power() > 0 && backward_rev[count - 1 - i].is_contained(desc))
			{
				has_power = true;
				break;
			}
		}
	}
	if (!has_power)
	{
		result.reason = consist_order_check_t::no_power;
		return result;
	}

	return result;
}


void vehicle_description_element::rdwr(loadsave_t *file)
{
	// The rule preference flags were removed in Extended 15.2 (they were serialised
	// but never evaluated at runtime). Pre-15.2 files (Extended 15 revision 0-1) still
	// carry 20 uint32 values per alternative, read and discarded below.
	const bool old_rule_flags_shape = file->get_extended_version() == 15 && file->get_extended_revision() < 2;
	// Number of flag values per alternative in the pre-15.2 shape.
	const uint32 old_rule_flag_count = 20;

	if (file->is_saving())
	{
		const char *s = specific_vehicle ? specific_vehicle->get_name() : "~BLANK~";
		file->rdwr_str(s);
	}
	else
	{
		char vehicle_name[512];
		file->rdwr_str(vehicle_name, 512);
		const vehicle_desc_t* desc = vehicle_builder_t::get_info(vehicle_name);
		if(desc == nullptr)
		{
			desc = vehicle_builder_t::get_info(translator::compatibility_name(vehicle_name));
		}
		specific_vehicle = desc;
	}

	file->rdwr_bool(empty);
	file->rdwr_byte(engine_type);
	file->rdwr_byte(min_catering);
	file->rdwr_byte(max_catering);
	file->rdwr_byte(must_carry_class);
	file->rdwr_long(min_range);
	file->rdwr_long(max_range);
	file->rdwr_short(min_brake_force);
	file->rdwr_short(max_brake_force);
	file->rdwr_long(min_power);
	file->rdwr_long(max_power);
	file->rdwr_long(min_tractive_effort);
	file->rdwr_long(max_tractive_effort);
	file->rdwr_long(max_topspeed);
	file->rdwr_long(min_topspeed);
	file->rdwr_long(max_weight);
	file->rdwr_long(min_weight);
	file->rdwr_long(max_axle_load);
	file->rdwr_long(min_axle_load);
	file->rdwr_short(min_capacity);
	file->rdwr_short(max_capacity);
	file->rdwr_long(max_running_cost);
	file->rdwr_long(min_running_cost);
	file->rdwr_long(max_fixed_cost);
	file->rdwr_long(min_fixed_cost);
	file->rdwr_long(max_fuel_per_km);
	file->rdwr_long(min_fuel_per_km);
	file->rdwr_long(max_staff_hundredths);
	file->rdwr_long(min_staff_hundredths);
	file->rdwr_long(max_drivers);
	file->rdwr_long(min_drivers);

	if (file->is_loading())
	{
		load_requirement = load_any;
	}
	if (file->is_version_ex_atleast(15, 17))
	{
		file->rdwr_byte(load_requirement);
	}

	if (old_rule_flags_shape) {
		// Zeros for the removed preference flags (see above), so that older
		// builds can still read saves written for an older target revision.
		for(uint32 i = 0; i < old_rule_flag_count; i ++)
		{
			uint32 zero = 0;
			file->rdwr_long(zero);
		}
	}
}


void consist_order_t::rdwr(loadsave_t* file)
{
	// The vehicle tag fields were removed in Extended 15.4 (they were serialised
	// but never evaluated at runtime). Pre-15.4 files (Extended 15 revision 0-3)
	// still carry them, read and discarded below; zeros are written for older save
	// targets so that older builds can still read saves written for those targets.
	// (The removed pre-15.2 rule preference flags are handled inside
	// vehicle_description_element::rdwr.)
	const bool old_tag_shape = file->get_extended_version() == 15 && file->get_extended_revision() < 4;

	if (old_tag_shape)
	{
		uint16 tags_to_clear = 0;
		file->rdwr_short(tags_to_clear);
	}

	uint32 orders_count = orders.get_count();
	file->rdwr_long(orders_count);

	if (file->is_version_ex_atleast(15, 3))
	{
		file->rdwr_short(available_to_id);
		file->rdwr_byte(available_to_flags);
	}
	else if (file->is_loading())
	{
		// Pre-15.3 saves have no recipient: default to the same line or
		// consist that released the vehicles, matching the member defaults.
		available_to_id = 0;
		available_to_flags = available_to_is_same;
	}

	if(file->is_saving())
	{
		for(auto order : orders)
		{
			file->rdwr_byte(order.catg_index);
			if (file->is_version_ex_atleast(15, 17))
			{
				file->rdwr_bool(order.optional);
			}

			if (old_tag_shape)
			{
				// Zeros for the removed tag fields (see above).
				bool clear_all_tags = false;
				uint16 tags_required = 0;
				uint16 tags_to_set = 0;
				file->rdwr_bool(clear_all_tags);
				file->rdwr_short(tags_required);
				file->rdwr_short(tags_to_set);
			}

			uint32 vehicle_description_count = order.vehicle_description.get_count();
			file->rdwr_long(vehicle_description_count);

			for(auto &vehicle_description : order.vehicle_description)
			{
				vehicle_description.rdwr(file);
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
			order.optional = false;
			if (file->is_version_ex_atleast(15, 17))
			{
				file->rdwr_bool(order.optional);
			}

			if (old_tag_shape)
			{
				// Discard the removed tag fields (see above).
				bool clear_all_tags = false;
				uint16 tags_required = 0;
				uint16 tags_to_set = 0;
				file->rdwr_bool(clear_all_tags);
				file->rdwr_short(tags_required);
				file->rdwr_short(tags_to_set);
			}

			uint32 vehicle_description_count = 0;
			file->rdwr_long(vehicle_description_count);

			for(uint32 j = 0; j < vehicle_description_count; j ++)
			{
				vehicle_description_element vehicle_description;
				vehicle_description.rdwr(file);
				order.vehicle_description.append(vehicle_description);
			}

			orders.append(order);
		}
	}
}

void consist_order_t::sprintf_consist_order(cbuffer_t &buf) const
{
	buf.append_fixed(orders.get_count());
	buf.append_fixed(available_to_id);
	buf.append_fixed(available_to_flags);
	for (auto order : orders)
	{
		buf.append_fixed(order.catg_index);
		buf.append_fixed((uint8)(order.optional ? 1 : 0));
		buf.append_fixed(order.vehicle_description.get_count());
		for(auto desc : order.vehicle_description)
		{
			buf.append(desc.specific_vehicle ? desc.specific_vehicle->get_name() : "NULL");
			buf.append("|");
			buf.append_fixed(desc.empty == true ? (uint8)1 : (uint8)0);
			buf.append_fixed(desc.load_requirement);
			buf.append_fixed(desc.engine_type);
			buf.append_fixed(desc.min_catering);
			buf.append_fixed(desc.max_catering);
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
		}
	}
}

const char* consist_order_t::sscanf_consist_order(const char* ptr)
{
	const char *p = ptr;
	orders.clear();

	// Retrieve the count
	const uint32 order_count = cbuffer_t::decode_uint32(p);

	available_to_id = cbuffer_t::decode_uint16(p);
	available_to_flags = cbuffer_t::decode_uint8(p);

	// Now use the count to determine the number of orders retrieved
	for(uint32 i = 0;  i < order_count; i ++)
	{
		consist_order_element_t element;

		element.catg_index = cbuffer_t::decode_uint8(p);
		element.optional = cbuffer_t::decode_uint8(p) != 0;

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
			desc.load_requirement = cbuffer_t::decode_uint8(p);
			desc.engine_type = cbuffer_t::decode_uint8(p);
			desc.min_catering = cbuffer_t::decode_uint8(p);
			desc.max_catering = cbuffer_t::decode_uint8(p);
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

			element.vehicle_description.append(desc);
		}
		orders.append(element);
	}

	return p;
}

bool consist_order_t::operator== (const consist_order_t& other) const
{
	if (available_to_id == other.available_to_id && available_to_flags == other.available_to_flags && orders.get_count() == other.orders.get_count())
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
	if (catg_index == other.catg_index && optional == other.optional && vehicle_description.get_count() == other.vehicle_description.get_count())
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

bool consist_order_element_t::operator== (const consist_order_element_t& other) const
{
	return !(other != *this);
}

bool vehicle_description_element::operator!= (const vehicle_description_element& other) const
{
	bool return_value =
		specific_vehicle != other.specific_vehicle ||
		empty != other.empty ||
		load_requirement != other.load_requirement ||
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

	return return_value;
}
