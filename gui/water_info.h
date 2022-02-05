/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_WATER_INFO_H
#define GUI_WATER_INFO_H


#include "gui_frame.h"
#include "components/gui_location_view_t.h"
#include "components/gui_label.h"
#include "components/gui_image.h"

/**
 * An adapter class to display info windows for water tile
 */
class water_info_t : public gui_frame_t
{
	koord3d k;
	gui_image_t img_intown;
	gui_label_buf_t label;
	location_view_t view;

	bool inside_city = false;
	char old_name[256]; ///< Name and old name of the city.

	void update();

public:
	water_info_t(const char *name, const koord3d &pos);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void map_rotate90(sint16) OVERRIDE;
};


#endif
