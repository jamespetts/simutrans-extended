/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_OBJ_INFO_H
#define GUI_OBJ_INFO_H


#include "../simdebug.h"
#include "../obj/simobj.h"
#include "base_info.h"
#include "components/action_listener.h"
#include "components/gui_obj_view_t.h"

/**
 * An adapter class to display info windows for things (objects)
 */
class obj_infowin_t : public base_infowin_t, public action_listener_t
{
protected:
	obj_view_t view;

public:
	obj_infowin_t(const obj_t* obj);

	obj_t const* get_obj() const { return view.get_obj(); }

	koord3d get_weltpos(bool) OVERRIDE { return get_obj()->get_pos(); }

	bool is_weltpos() OVERRIDE;

	/**
	 * Handles a click on a link in the text area. Coordinate links of the form
	 * "(x,y,z)" move the viewport and the cursor.
	 */
	bool action_triggered(gui_action_creator_t *comp, value_t v) OVERRIDE;

	// refill buffer
	void fill_buffer();

	/**
	* Draw new component. The values to be passed refer to the window
	* i.e. It's the screen coordinates of the window where the
	* component is displayed.
	*/
	void draw(scr_coord pos, scr_size size) OVERRIDE;
};


#endif
