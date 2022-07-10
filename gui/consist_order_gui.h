/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CONSIST_ORDER_GUI_H
#define GUI_CONSIST_ORDER_GUI_H


#include "gui_frame.h"
#include "simwin.h"
#include "components/gui_aligned_container.h"
#include "components/gui_button.h"
#include "components/gui_combobox.h"
#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_label.h"

#include "../convoihandle_t.h"
#include "../halthandle_t.h"

#include "../dataobj/schedule.h"
#include "../dataobj/consist_order_t.h"

class consist_order_t;

class consist_order_frame_t : public gui_frame_t , private action_listener_t
{
	uint16 unique_entry_id;
	player_t* player;
	schedule_t *schedule;

	uint8 old_entry_count = 0;

	halthandle_t halt;

	consist_order_t order;

	cbuffer_t title_buf;

	gui_scrolled_list_t scl;

	button_t bt_new, bt_edit, bt_delete;


	uint8 selected;

	const vehicle_desc_t* selected_vehicle = nullptr;

	void init_table();

public:
	consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 unique_entry_id);
	void init(player_t* player, schedule_t *schedule, uint16 unique_entry_id);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	const char * get_help_filename() const OVERRIDE { return "consist_order.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void set_convoy(convoihandle_t cnv = convoihandle_t());

	void update();

	//void rdwr(loadsave_t *file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_consist_order; }
};

#endif
