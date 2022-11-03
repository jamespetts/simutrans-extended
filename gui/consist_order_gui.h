/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CONSIST_ORDER_GUI_H
#define GUI_CONSIST_ORDER_GUI_H


#include "gui_frame.h"
#include "simwin.h"
#include "linelist_stats_t.h"
#include "components/gui_aligned_container.h"
#include "components/gui_button.h"
#include "components/gui_colorbox.h"
#include "components/gui_combobox.h"
#include "components/gui_convoy_formation.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_schedule_item.h"
#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_tab_panel.h"

#include "../tpl/slist_tpl.h"

#include "../convoihandle_t.h"
#include "../halthandle_t.h"
#include "../player/simplay.h"

#include "../dataobj/schedule.h"
#include "../dataobj/consist_order_t.h"

class consist_order_t;

struct own_vehicle_t
{
	uint32 count = 0;
	const vehicle_desc_t* veh_type=nullptr;
};


class vehicle_scrollitem_t : public gui_label_t, public gui_scrolled_list_t::scrollitem_t
{
	own_vehicle_t own_veh;

	gui_label_buf_t label;
	gui_vehicle_bar_t colorbar, colorbar_edge;

public:
	vehicle_scrollitem_t(own_vehicle_t own_veh);

	void draw(scr_coord offset) OVERRIDE;

	char const* get_text() const OVERRIDE { return own_veh.veh_type==nullptr ? "Vehicle not found" : own_veh.veh_type->get_name(); }

	const vehicle_desc_t* get_vehicle() const { return own_veh.veh_type; }

	using gui_component_t::infowin_event;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, LINESPACE); }

	//static bool compare(const gui_component_t *a, const gui_component_t *b);
};

// Show vehicle image with 7 parameters that related to the constist order
class gui_simple_vehicle_spec_t : public gui_aligned_container_t
{
	const vehicle_desc_t* veh_type = nullptr;
	sint8 player_nr=1;

public:

	enum {
		SPEC_PAYLOADS,
		SPEC_RANGE,
		SPEC_POWER,
		SPEC_TRACTIVE_FORCE,
		SPEC_BRAKE_FORCE,
		SPEC_SPEED,
		SPEC_WEIGHT,
		SPEC_AXLE_LOAD,
		SPEC_RUNNING_COST,
		SPEC_FIXED_COST,
		MAX_VEH_SPECS
	};

	gui_simple_vehicle_spec_t() { }
	void set_player_nr(sint8 player_nr) { this->player_nr=player_nr; }
	void set_vehicle(const vehicle_desc_t* desc) { veh_type = desc; init_table(); }
	void init_table();
};


class gui_vehicle_description_t : public gui_aligned_container_t, private action_listener_t
{
	consist_order_t *order;
	uint32 order_element_index;
	uint32 description_index;
	button_t bt_up, bt_down, bt_remove;
	//button_t bt_edit; // TODO

public:
	gui_vehicle_description_t(consist_order_t *order, sint8 player_nr, uint32 order_element_index, uint32 description_index);

	bool action_triggered(gui_action_creator_t *comp, value_t) OVERRIDE;
};


class cont_order_overview_t : public gui_aligned_container_t
{
	consist_order_t *order;
	sint8 player_nr=-1; // Required for player color in vehicle images
	uint32 order_element_index = -1;

	uint32 old_count=0; // reflesh flag

public:
	cont_order_overview_t(consist_order_t *order);

	void set_element(uint32 element_idx, sint8 player_nr) {
		this->player_nr = player_nr;
		if (order_element_index != element_idx) {
			order_element_index = element_idx;
			init_table();
		}
	}

	void init_table();

	void draw(scr_coord offset) OVERRIDE;
};


class consist_order_frame_t : public gui_frame_t , private action_listener_t
{
	uint16 unique_entry_id=-1;
	player_t* player;
	schedule_t *schedule;

	uint8 old_entry_count = 0;
	sint64 old_vehicle_assets; // init in init_table()

	halthandle_t halt;

	consist_order_t order;

	cbuffer_t title_buf;
	gui_schedule_entry_number_t halt_number;
	gui_label_buf_t lb_halt;

	gui_scrolled_list_t scl;

	button_t bt_new, bt_delete;

	// [ORDER]
	gui_aligned_container_t cont_order;
	cont_order_overview_t cont_order_overview;
	gui_scrollpane_t scrolly_order;
	uint32 old_order_count=0;
	void update_order_list(sint32 reselect_index=-1);

	// filter (common)
	uint8 filter_catg=goods_manager_t::INDEX_NONE;
	gui_combobox_t freight_type_c;

	// [VEHICLE PICKER]
	const vehicle_desc_t* selected_vehicle = nullptr;
	slist_tpl<own_vehicle_t> own_vehicles;
	button_t bt_add_vehicle, bt_add_vehicle_limit_vehicle, bt_sort_order_veh, bt_show_hide_vehicle_filter;
	gui_label_t lb_open_vehicle_filter;
	gui_simple_vehicle_spec_t veh_specs;
	gui_aligned_container_t cont_picker_frame, cont_vehicle_filter;
	gui_scrolled_list_t scl_vehicles;
	void update_vehicle_info();
	void build_vehicle_list(); // also update convoy list

	// [CONVOY COPIER]
	convoihandle_t selected_convoy = convoihandle_t();
	slist_tpl<convoihandle_t> own_convoys;
	// filter
	gui_label_t lb_open_convoy_filter;
	button_t bt_filter_halt_convoy, bt_filter_single_vehicle, bt_show_hide_convoy_filter;
	gui_aligned_container_t cont_convoy_filter;
	//
	button_t bt_sort_order_cnv, bt_copy_convoy, bt_copy_convoy_limit_vehicle, bt_convoy_detail;
	gui_label_buf_t lb_vehicle_count;
	gui_line_label_t line_label;
	gui_convoi_images_t img_convoy;
	gui_convoy_formation_t formation;
	gui_scrollpane_t scrollx_formation;
	gui_aligned_container_t cont_convoy_copier;
	gui_scrolled_list_t scl_convoys;
	void update_convoy_info();

	gui_tab_panel_t tabs;

	void init_table();
	void update();

	// add an empty order to orders
	void append_new_order();

public:
	consist_order_frame_t(player_t* player=NULL, schedule_t *schedule=NULL, uint16 unique_entry_id=-1);

	void init(player_t* player, schedule_t *schedule, uint16 unique_entry_id);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	const char * get_help_filename() const OVERRIDE { return "consist_order.txt"; }

	koord3d get_weltpos(bool) OVERRIDE;

	bool is_weltpos() OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;

	void set_convoy(convoihandle_t cnv = convoihandle_t());

	void rdwr(loadsave_t *file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_consist_order; }
};

#endif
