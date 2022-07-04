/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "obj_base_desc.h"
#include "../network/checksum.h"
#include "../simworld.h"

sint64 obj_desc_transport_infrastructure_t::get_base_maintenance() const
{
	return world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_base_maintenance(), infrastructure);
}

sint64 obj_desc_transport_infrastructure_t::get_maintenance() const
{
	return world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_maintenance(), infrastructure);
}

sint64 obj_desc_transport_infrastructure_t::get_base_price() const
{
	return world() ? world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_base_price(), infrastructure) : obj_desc_transport_related_t::get_base_price();
}

sint64 obj_desc_transport_infrastructure_t::get_value() const
{
	return world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_value(), infrastructure);
}

sint64 obj_desc_transport_infrastructure_t::get_base_way_only_cost() const
{
	return world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_base_way_only_cost(), infrastructure);
}

sint64 obj_desc_transport_infrastructure_t::get_way_only_cost() const
{
	return world()->get_inflation_adjusted_price(world()->get_current_month(), obj_desc_transport_related_t::get_way_only_cost(), infrastructure);
}



void obj_desc_timelined_t::calc_checksum(checksum_t *chk) const
{
	chk->input(intro_date);
	chk->input(retire_date);
}


void obj_desc_transport_related_t::calc_checksum(checksum_t *chk) const
{
	obj_desc_timelined_t::calc_checksum(chk);
	chk->input(base_maintenance);
	chk->input(base_cost);
	chk->input(wtyp);
	chk->input(topspeed);
	chk->input(topspeed-topspeed_gradient_1);
	chk->input(topspeed-topspeed_gradient_2);
	chk->input(axle_load);
	chk->input(way_only_cost);
}
