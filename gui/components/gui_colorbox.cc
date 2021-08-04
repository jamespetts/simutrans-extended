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
	if (show_frame) {
		display_colorbox_with_tooltip(offset.x, offset.y, width, height, color, true, tooltip);
	}
	else {
		display_fillbox_wh_clip_rgb(offset.x, offset.y, width, height, color, true);
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
