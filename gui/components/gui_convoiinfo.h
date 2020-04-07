/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_CONVOIINFO_H
#define GUI_COMPONENTS_GUI_CONVOIINFO_H


#include "../../display/scr_coord.h"
#include "gui_container.h"
#include "gui_speedbar.h"
#include "../../convoihandle_t.h"


/**
 * Convoi info stats, like loading status bar
 * One element of the vehicle list display
 */
class gui_convoiinfo_t : public gui_world_component_t
{
private:
	/**
	* Handle Convois to be displayed.
	*/
	convoihandle_t cnv;

	gui_speedbar_t filled_bar;

public:
	/**
	* @param cnv, the handler for the Convoi to be displayed.
	*/
	gui_convoiinfo_t(convoihandle_t cnv);

	bool infowin_event(event_t const*) OVERRIDE;

	/**
	* Draw the component
	*/
	void draw(scr_coord offset);
};

#endif
