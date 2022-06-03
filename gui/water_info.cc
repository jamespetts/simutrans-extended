/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "water_info.h"

#include "../simworld.h"
#include "../simcity.h"
#include "../descriptor/skin_desc.h"
#include "../utils/simstring.h"

water_info_t::water_info_t(const char *name, const koord3d &pos) :
	gui_frame_t(translator::translate(name), NULL),
	view(pos, scr_size(max(96, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8)+LINEASCENT))
{
	k=pos;
	set_table_layout(1,0);
	set_alignment(ALIGN_CENTER_H);
	add_table(3,1);
	{
		img_intown.set_image(skinverwaltung_t::intown->get_image_id(0), true);
		img_intown.set_rigid(false);
		img_intown.set_visible(false);
		add_component(&img_intown);
		add_component(&label);
		new_component<gui_fill_t>();
	}
	end_table();
	add_component(&view);
	update();
}


void water_info_t::draw(scr_coord pos, scr_size size)
{
	if( stadt_t *city = world()->get_city( k.get_2d() ) ){
		if (strcmp(old_name, city->get_name()) != 0) {
			update();
		}
	}
	else {
		if (inside_city) {
			update();
		}
	}
	gui_frame_t::draw(pos, size);
	pos += scr_size(D_V_SPACE + 2, D_TITLEBAR_HEIGHT + 2);
	display_proportional_clip_rgb(pos.x + view.get_pos().x, pos.y+view.get_pos().y, k.get_2d().get_fullstr(), ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), true);
}

void water_info_t::map_rotate90(sint16 new_ysize)
{
	view.map_rotate90(new_ysize);
}

void water_info_t::update()
{
	if (stadt_t *city = world()->get_city(k.get_2d())) {
		inside_city = true;
		tstrncpy(old_name, city->get_name(), sizeof(old_name));
		label.buf().printf(old_name);
	}
	else {
		label.buf().printf( translator::translate(k.z > world()->get_groundwater() ? "Lake" : "Open water") );
		inside_city = false;
	}
	label.update();
	img_intown.set_visible(inside_city);

	const scr_coord_val titlebar_min_width = D_GADGET_WIDTH*3+2+2+D_H_SPACE+proportional_string_width(get_name());

	reset_min_windowsize();
	set_windowsize(scr_size(max(get_min_windowsize().w, titlebar_min_width),get_min_windowsize().h));
}

