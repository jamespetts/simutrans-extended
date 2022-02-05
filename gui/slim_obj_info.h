/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SLIM_OBJ_INFO_H
#define GUI_SLIM_OBJ_INFO_H


#include "gui_frame.h"
#include "components/gui_label.h"
#include "components/gui_obj_view_t.h"
#include "../vehicle/pedestrian.h"

/**
 * For trivial objects that cannot be owned (pedestrian, tree, rock, flock of birds etc.)
 */
class slim_obj_info_t : public gui_frame_t
{
	obj_view_t view;
	gui_label_buf_t label;

	uint16 old_date=65535;  // update and sticky flag
	bool stickable = false; // has sticky and locked

	void set_windowsize();

public:
	slim_obj_info_t(const obj_t* obj);

	bool has_sticky() const OVERRIDE { return stickable; }

	void draw(scr_coord pos, scr_size size) OVERRIDE;
};


#endif
