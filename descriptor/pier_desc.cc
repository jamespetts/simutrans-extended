/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_desc.h"
#include "../network/checksum.h"

const slope_t::type pier_desc_t::alt_tool_icon=40;
const slope_t::type pier_desc_t::rotation_select_image=41;
const slope_t::type pier_desc_t::low_waydeck_image=43;
const slope_t::type pier_desc_t::parapet[]={44,49,50,52};
const slope_t::type pier_desc_t::auto_tool_cursor_image=79;
const slope_t::type pier_desc_t::auto_tool_icon_image=80;

slope_t::type pier_desc_t::get_above_slope(uint8 rotation) const{
    switch (rotation&3) {
    case 0: return above_slope;
    case 1: return slope_t::rotate270(above_slope);
    case 2: return slope_t::rotate180(above_slope);
    case 3: return slope_t::rotate90(above_slope);
    }
}

image_id pier_desc_t::get_background(slope_t::type slope, uint8 rotation, uint8 season) const{
	const image_t *image = NULL;
	rotation=img_rotation(rotation);
	if(season && number_of_seasons == 2) {
		image = get_child<image_list_t>(5)->get_image(slope + 81 * rotation);
	}
	if(image == NULL) {
		image = get_child<image_list_t>(2)->get_image(slope + 81 * rotation);
	}
	return image != NULL ? image->get_id() : IMG_EMPTY;
}

image_id pier_desc_t::get_foreground(slope_t::type slope, uint8 rotation, uint8 season) const{
	const image_t *image = NULL;
	rotation=img_rotation(rotation);
	if(season && number_of_seasons == 2) {
		image = get_child<image_list_t>(6)->get_image(slope + 81 * rotation);
	}
	if(image == NULL) {
		image = get_child<image_list_t>(3)->get_image(slope + 81 * rotation);
	}
	return image != NULL ? image->get_id() : IMG_EMPTY;
}

void pier_desc_t::calc_checksum(checksum_t *chk) const{
	obj_desc_transport_infrastructure_t::calc_checksum(chk);
	chk->input(above_way_ribi);
	chk->input(below_way_ribi);
	chk->input(above_slope);
	chk->input(auto_group);
	chk->input(auto_height);
	chk->input((uint32)(base_mask & 0xFFFFFFFF));
	chk->input((uint32)(base_mask >> 32));
	chk->input((uint32)(support_mask & 0xFFFFFFFF));
	chk->input((uint32)(support_mask >> 32));
	chk->input((uint32)(middle_mask & 0xFFFFFFFF));
	chk->input((uint32)(middle_mask >> 32));
	chk->input(sub_obj_mask);
	chk->input(deck_obj_mask);
	chk->input(max_weight);
	chk->input(number_of_seasons);
	chk->input(pier_weight);
	chk->input(drag_ribi);
	chk->input(above_way_supplement);
	chk->input(rotational_symmetry);
	chk->input(topspeed);
	chk->input(axle_load);
	for(uint8 slope=0; slope<81; slope++){
		for(uint8 rotation = 0; rotation < 4; rotation++){
			chk->input(get_background(slope,rotation,0)!=IMG_EMPTY);
		}
	}
}
