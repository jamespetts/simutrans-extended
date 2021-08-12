/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
* Track infowindow buttons //Ves
 */

#include "schiene_info.h"
#include "../boden/wege/schiene.h" // The rest of the dialog
#include "../vehicle/vehicle.h"

#include "../simmenu.h"
#include "../simworld.h"
#include "../display/viewport.h"

schiene_info_t::schiene_info_t(schiene_t* const s) :
	obj_infowin_t(s),
	sch(s)

{
	bool rail_track = (sch->get_desc()->get_wtyp() == monorail_wt || sch->get_desc()->get_wtyp() == maglev_wt || sch->get_desc()->get_wtyp() == tram_wt || sch->get_desc()->get_wtyp() == narrowgauge_wt) && sch->get_desc()->get_wtyp() != air_wt;
	bool runway = sch->get_desc()->get_wtyp() == air_wt && sch->get_desc()->get_styp() == type_runway;

	if (rail_track || runway)
	{
		reserving_vehicle_button.init(button_t::posbutton, NULL, scr_coord(D_MARGIN_LEFT, get_windowsize().h - 26 - (sch->get_textlines() * LINESPACE)));
		reserving_vehicle_button.set_tooltip("goto_vehicle");
		reserving_vehicle_button.add_listener(this);
		add_component(&reserving_vehicle_button);
	}
}


bool schiene_info_t::action_triggered( gui_action_creator_t *comp, value_t)
{
	if (comp == &reserving_vehicle_button)
	{
		welt->get_viewport()->set_follow_convoi(sch->get_reserved_convoi());
	}
	return true;
}

/*
* Draw new component. The values to be passed refer to the window
* i.e. It's the screen coordinates of the window where the
* component is displayed.
*/

void schiene_info_t::draw(scr_coord pos, scr_size size)
{
	buf.clear();
	fill_buffer();
//	textarea.recalc_size();



	gui_frame_t::draw(pos, size);

	if (sch->is_reserved() || sch->is_reserved_directional() || sch->is_reserved_priority())
	{
		reserving_vehicle_button.set_visible(true);
	}
	else
	{
		reserving_vehicle_button.set_visible(false);
	}
}
