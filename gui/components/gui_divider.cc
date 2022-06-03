/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_divider.h"
#include "../gui_theme.h"
#include "../../display/simgraph.h"


void gui_divider_t::draw(scr_coord offset)
{
	display_img_stretch( gui_theme_t::divider, scr_rect( get_pos()+offset, get_size() ) );
}


scr_size gui_divider_t::get_min_size() const
{
	return scr_size( min(size.w, gui_theme_t::gui_divider_size.w), gui_theme_t::gui_divider_size.h );
}


scr_size gui_divider_t::get_max_size() const
{
	return scr_size( temp_width<gui_theme_t::gui_divider_size.w ? temp_width : scr_size::inf.w, gui_theme_t::gui_divider_size.h );
}


gui_border_t::gui_border_t(PIXVAL color_)
{
	color = color_;
}

void gui_border_t::draw(scr_coord offset)
{
	offset += pos;
	display_fillbox_wh_clip_rgb(offset.x, offset.y+size.h/2-1, size.w, 1, color, false);
}

scr_size gui_border_t::get_min_size() const
{
	return scr_size(gui_theme_t::gui_divider_size.w, LINEASCENT/2);
}

scr_size gui_border_t::get_max_size() const
{
	return scr_size(scr_size::inf.w, LINEASCENT/2);
}
