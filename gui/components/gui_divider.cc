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
