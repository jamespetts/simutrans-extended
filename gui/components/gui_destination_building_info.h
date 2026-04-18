/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_DESTINATION_BUILDING_INFO_H
#define GUI_COMPONENTS_GUI_DESTINATION_BUILDING_INFO_H


#include "gui_aligned_container.h"
#include "gui_button.h"
#include "../../simfab.h" // show_info


 // Display building information in one line
 // (1) building name, (2) city name, (3) pos
class gui_destination_building_info_t : public gui_aligned_container_t, private action_listener_t
{
	bool is_freight; // For access to the factory

	// Given access to the factory, we need to remember the factory to avoid crashes if the factory is closed.
	// (Also, unlike the case of pos, there is no need to support the rotation of coordinates.)
	fabrik_t *fab = NULL;

	button_t button; // posbutton for the building / access button for the factory

public:
	gui_destination_building_info_t(koord zielpos, bool is_freight);

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};


#endif
