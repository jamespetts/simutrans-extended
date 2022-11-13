/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "vehicle_detail.h"
#include "../descriptor/vehicle_desc.h"


vehicle_detail_t::vehicle_detail_t(const vehicle_desc_t *v) :
	gui_frame_t("")
{
	veh = v;

	gui_frame_t::set_name("vehicle_details");
	init_table();
}


void vehicle_detail_t::init_table()
{
	if (veh == NULL) {
		destroy_win(this);
	}
	remove_all();
	set_table_layout(1,0);
	new_component<gui_label_t>(veh->get_name());
	new_component<gui_image_t>(veh->get_image_id(ribi_t::dir_south, veh->get_freight_type()), 0, ALIGN_NONE, true);
	reset_min_windowsize();
}

bool vehicle_detail_t::action_triggered(gui_action_creator_t*, value_t)
{
	return true;
}
