/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CURIOSITYLIST_FRAME_T_H
#define GUI_CURIOSITYLIST_FRAME_T_H


#include "simwin.h"
#include "../simcity.h"
#include "gui_frame.h"
#include "components/action_listener.h"
#include "components/gui_combobox.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_button.h"
#include "components/gui_combobox.h"


/**
 * Curiosity list window
 */
class curiositylist_frame_t : public gui_frame_t, private action_listener_t
{
private:
	gui_combobox_t sortedby, region_selector;
	button_t sort_order;
	button_t filter_within_network, bt_cancel_cityfilter;
	gui_scrolled_list_t scrolly;
	gui_aligned_container_t list;
	gui_label_buf_t lb_target_city;

	stadt_t *filter_city;
	uint32 attraction_count;

	void fill_list();

	static char name_filter[256];
	char last_name_filter[256];
	gui_textinput_t name_filter_input;

public:
	curiositylist_frame_t(stadt_t *filter_city = NULL);

	const char *get_help_filename() const OVERRIDE {return "curiositylist_filter.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE { fill_list(); }

	void rdwr(loadsave_t* file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_curiositylist; }

	void set_cityfilter(stadt_t *city);
};

#endif
