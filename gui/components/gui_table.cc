/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_table.h"
#include "../../display/simgraph.h"
#include "../simwin.h"



void gui_table_cell_t::draw(scr_coord offset)
{
	// background
	display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h, bgcolor, true);

	// border
	if (border_bottom_right) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y+get_size().h-1, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x+ get_size().w, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_label_t::draw(offset);
}

scr_size gui_table_cell_t::get_max_size() const
{
	if (fixed_width) {
		return scr_size(get_min_size().w, scr_size::inf.h);
	}
	return scr_size::inf;
}

void gui_table_header_t::draw(scr_coord offset)
{
	// border
	if (border_top_left) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_table_cell_t::draw(offset);
}


void gui_table_cell_buf_t::draw(scr_coord offset)
{
	// background
	display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h, bgcolor, true);

	// border
	if (border_bottom_right) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y+get_size().h-1, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x+ get_size().w, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_label_buf_t::draw(offset);
}

scr_size gui_table_cell_buf_t::get_max_size() const
{
	if (fixed_width) {
		return scr_size(get_min_size().w, scr_size::inf.h);
	}
	return scr_size::inf;
}

void gui_table_header_buf_t::draw(scr_coord offset)
{
	// border
	if (border_top_left) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_table_cell_buf_t::draw(offset);
}
