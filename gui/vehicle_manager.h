/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_VEHICLE_MANAGER_H
#define GUI_VEHICLE_MANAGER_H


#include "simwin.h"
#include "gui_frame.h"
#include "../simdepot.h"
#include "components/gui_button.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_textinput.h"
#include "components/gui_table.h"
#include "components/sortable_table_vehicle.h"
#include "components/sortable_table_header.h"

class vehicle_desc_t;

class vehicle_manager_t : public gui_frame_t, action_listener_t
{
	depot_t *depot;

	// update triggars
	uint32 old_vhicles;  // stored vehicles
	uint32 old_listed_vehicles; // waytype total in the world
	uint32 old_selections; // selected vehicles
	sint32 last_selected=-1; // last selected vehicle
	uint32 old_month; // Update flag when the month changes

public:

	enum {
		da_scrap,
		da_listing,
		da_cancel_listing,
		da_buy
	};
	uint8 dep_action=da_scrap;

private:
	//static sort_mode_t sortby;
	//static const char* sort_text[vehicle_row_t::MAX_COLS]; // ?

	button_t
		bt_mode_scrap,
		bt_mode_sell,
		bt_mode_cancel_listing,
		bt_mode_buy,
		bt_sel_all,
		bt_reset,
		bt_execute;
	gui_label_buf_t
		lb_vehicle_count,
		lb_total_cost;

	char name[256], old_name[256];
	gui_textinput_t name_input;

	gui_sort_table_header_t table_header;
	gui_aligned_container_t cont_sortable;

	gui_scrolled_list_t scrolly;
	gui_scrollpane_t scroll_sortable;

	/// refill the list of vehicle info elements
	void update_list();

	void init_table();
	void init_mode_buttons();

	// calculate total selected vehicles cost and update the label
	void calc_total_cost();

	// execute vechile trade command
	void scrap_vehicles();
	void offer_vehicles_for_sale();
	void cancel_vehicles_for_sale();

	// buy vehicles from other players
	void takeover_vehicles();

	/// Renames the depot to the name given in the text input field
	void rename_depot();

public:
	vehicle_manager_t(depot_t* depot);

	void reset_depot_name();

	const char *get_help_filename() const OVERRIDE {return "vehicle_manager.txt"; }

	bool action_triggered(gui_action_creator_t *comp, value_t v) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	koord3d get_weltpos(bool) OVERRIDE;
	bool is_weltpos() OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_vehicle_manager + depot->get_owner_nr(); }
};


#endif
