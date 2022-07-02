/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_CONVOI_BUTTON_H
#define GUI_COMPONENTS_GUI_CONVOI_BUTTON_H


#include "gui_button.h"
#include "action_listener.h"

#include "../../simconvoi.h"
#include "../../simline.h"
#include "../../player/simplay.h"
#include "../../simlinemgmt.h"


/**
 * Button to open convoi window
 */
class gui_convoi_button_t : public button_t, public action_listener_t
{
	convoihandle_t convoi;

public:
gui_convoi_button_t(convoihandle_t convoi) : button_t() {
		this->convoi = convoi;
		if( skinverwaltung_t::open_window ) {
			init(button_t::imagebox_state, NULL);
			set_image(skinverwaltung_t::open_window ? skinverwaltung_t::open_window->get_image_id(0) : skinverwaltung_t::goods->get_image_id(0));
		}
		else {
			init(button_t::posbutton, NULL);
		}
		add_listener(this);
	}

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE {
		convoi->show_info();
		return true;
	}
};

/**
 * Button to open line window
 */
class gui_line_button_t : public button_t, public action_listener_t
{
	linehandle_t line;

public:
	gui_line_button_t(linehandle_t line) : button_t() {
		this->line = line;
		if( skinverwaltung_t::open_window ) {
			init(button_t::imagebox_state, NULL);
			set_image(skinverwaltung_t::open_window ? skinverwaltung_t::open_window->get_image_id(0) : skinverwaltung_t::goods->get_image_id(0));
		}
		else {
			init(button_t::posbutton, NULL);
		}
		add_listener(this);
	}

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE {
		line->get_owner()->simlinemgmt.show_lineinfo(line->get_owner(), line);
		return true;
	}
};

#endif
