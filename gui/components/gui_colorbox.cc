/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_colorbox.h"
#include "../gui_theme.h"
#include "../../display/simgraph.h"

#include "../simwin.h"

gui_colorbox_t::gui_colorbox_t(PIXVAL c)
{
	color = c;
	tooltip = NULL;
	max_size = scr_size(scr_size::inf.w, height);
}


scr_size gui_colorbox_t::get_min_size() const
{
	return scr_size(width, height);
}


scr_size gui_colorbox_t::get_max_size() const
{
	return scr_size(size_fixed ? width : max_size.w, height);
}


void gui_colorbox_t::set_tooltip(const char * t)
{
	if (t == NULL) {
		tooltip = NULL;
	}
	else {
		tooltip = t;
	}
}


void gui_colorbox_t::draw(scr_coord offset)
{
	offset += pos;
	offset += padding;

	if (show_frame) {
		display_colorbox_with_tooltip(offset.x, offset.y, width, height, color, true, tooltip);
	}
	else {
		display_fillbox_wh_clip_rgb(offset.x, offset.y, width, height, color, true);
	}
}



gui_right_pointer_t::gui_right_pointer_t(PIXVAL c, uint8 height_)
{
	height = height_;
	color = c;
	tooltip = NULL;
	gui_component_t::set_size(scr_size(height, height));
}

void gui_right_pointer_t::draw(scr_coord offset)
{
	offset += pos;
	display_right_pointer_rgb(offset.x, offset.y, height, color, true);
}


gui_operation_status_t::gui_operation_status_t(PIXVAL c, uint8 height_)
{
	height = height_;
	color = c;
	tooltip = NULL;
	gui_component_t::set_size(GOODS_COLOR_BOX_SIZE);
}

void gui_operation_status_t::draw(scr_coord offset)
{
		offset += pos;
		switch (status)
		{
			case operation_stop:
				display_fillbox_wh_clip_rgb(offset.x+1, offset.y+1, height-2, height-2, color, true);
				break;
			case operation_pause:
				display_fillbox_wh_clip_rgb(offset.x+1,   offset.y+1, height/3, height-2, color, true);
				display_fillbox_wh_clip_rgb(offset.x+1+height/3*2, offset.y+1, height/3, height-2, color, true);
				break;
			case operation_normal:
				display_right_pointer_rgb(offset.x, offset.y, height, color, true);
				break;
			case operation_invalid:
			default:
				break;
		}
}


gui_vehicle_bar_t::gui_vehicle_bar_t(PIXVAL c, scr_size size)
{
	color = c;
	set_size(size);
}

void gui_vehicle_bar_t::set_flags(uint8 flags_left_, uint8 flags_right_, uint8 interactivity_)
{
	flags_left = flags_left_;
	flags_right=flags_right_;
	interactivity=interactivity_;
}

void gui_vehicle_bar_t::draw(scr_coord offset)
{
	offset += pos;
	display_veh_form_wh_clip_rgb(offset.x,          offset.y, (size.w+1)/2, size.h, color, true, false, flags_left,  interactivity);
	display_veh_form_wh_clip_rgb(offset.x+size.w/2, offset.y, (size.w+1)/2, size.h, color, true, true,  flags_right, interactivity);
}


void gui_vehicle_number_t::init()
{
	const int bar_width_half = (proportional_string_width(buf)+7+LINEASCENT+4)/4*2+2;
	set_size(scr_size(max(bar_width_half*2, 42), LINEASCENT+4));
}

void gui_vehicle_number_t::draw(scr_coord offset)
{
	offset += pos;
	if (show_frame) {
		display_veh_form_wh_clip_rgb(offset.x,          offset.y, size.w/2, size.h, color_idx_to_rgb(COL_WHITE), false, false, flags_left, interactivity);
		display_veh_form_wh_clip_rgb(offset.x+size.w/2, offset.y, size.w/2, size.h, color_idx_to_rgb(COL_WHITE), false, true,  flags_right, interactivity);
	}
	display_veh_form_wh_clip_rgb(offset.x+1,        offset.y+1, size.w/2-1, size.h-2, color, false, false, flags_left, interactivity);
	display_veh_form_wh_clip_rgb(offset.x+size.w/2, offset.y+1, size.w/2-1, size.h-2, color, false, true,  flags_right, interactivity);
	display_proportional_clip_rgb(offset.x+(size.w - proportional_string_width(buf))/2, offset.y+2, buf, ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), false);
}


void gui_capacity_bar_t::draw(scr_coord offset)
{
	if (height < 2) { return; }
	offset += gui_colorbox_t::pos;
	if (show_frame) {
		display_colorbox_with_tooltip(offset.x, offset.y, width, height, bg_col, true, tooltip);
	}
	else {
		display_fillbox_wh_clip_rgb(offset.x+1, offset.y+1, width-2, height-2, bg_col, true);
	}

	if (capacity) {
		scr_coord_val colored_width = ((width-2)*min(capacity,loading)+capacity-1)/capacity;
		scr_coord_val overcrowded_width = loading > capacity ? min(width-2, ((width-2)*(loading-capacity) + loading-capacity - 1) / loading) : 0;

		if (cylinder_style) {
			display_cylinderbar_wh_clip_rgb(offset.x + 1, offset.y + 1, colored_width, height - 2, color, true);
		}
		else {
			display_fillbox_wh_clip_rgb(offset.x + 1, offset.y + 1, colored_width, height - 2, color, true);
		}
		if (overcrowded_width) {
			if (cylinder_style) {
				display_cylinderbar_wh_clip_rgb(offset.x+width-1- overcrowded_width, offset.y + 1, overcrowded_width, height - 2, SYSCOL_OVERCROWDED, true);
			}
			else {
				display_fillbox_wh_clip_rgb(offset.x+width-1- overcrowded_width, offset.y + 1, overcrowded_width, height - 2, SYSCOL_OVERCROWDED, true);
			}
		}
	}
}

gui_depotbox_t::gui_depotbox_t(PIXVAL c, uint8 w)
{
	width = w;
	color = c;
	tooltip = NULL;
	gui_component_t::set_size(scr_size(w, w));
}

void gui_depotbox_t::draw(scr_coord offset)
{
	offset += pos;
	display_depot_symbol_rgb(offset.x, offset.y, width, color, true);
}


gui_fluctuation_triangle_t::gui_fluctuation_triangle_t(sint64 value_, uint8 height_)
{
	value = value_;
	height = min(GOODS_COLOR_BOX_HEIGHT,height_);
	gui_component_t::set_size(scr_size(height, height));
}

void gui_fluctuation_triangle_t::draw(scr_coord offset)
{
	offset += pos;
	display_fluctuation_triangle_rgb(offset.x, offset.y, height, false, value);
}


void gui_capped_arrow_t::draw(scr_coord offset)
{
	offset += pos;
	display_fillbox_wh_clip_rgb(offset.x+left, offset.y+1, L_CAPPED_ARROW_WIDTH-1, 3, color_idx_to_rgb(COL_WHITE), false);
	display_fillbox_wh_clip_rgb(offset.x+2,    offset.y,   1, 5, color_idx_to_rgb(COL_WHITE), false);
	if (left) {
		display_fillbox_wh_clip_rgb(offset.x, offset.y+2, 1, 1, color_idx_to_rgb(COL_WHITE), false);
	}
	else {
		display_fillbox_wh_clip_rgb(offset.x+ L_CAPPED_ARROW_WIDTH-1, offset.y+2, 1, 1, color_idx_to_rgb(COL_WHITE), false);
	}
}
