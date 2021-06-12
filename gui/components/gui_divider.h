/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_DIVIDER_H
#define GUI_COMPONENTS_GUI_DIVIDER_H


#include "gui_component.h"
#include "../gui_theme.h"

/**
 * A horizontal divider line
 */
class gui_divider_t : public gui_component_t
{
	scr_coord_val temp_width = gui_theme_t::gui_divider_size.w;
public:
	// TODO remove later
	void init( scr_coord xy, scr_coord_val width, scr_coord_val height = D_DIVIDER_HEIGHT ) {
		temp_width = width;
		set_pos( xy );
		set_size( scr_size( width, height ) );
	}

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};

#endif
