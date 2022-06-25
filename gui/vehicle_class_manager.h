/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_VEHICLE_CLASS_MANAGER_H
#define GUI_VEHICLE_CLASS_MANAGER_H


#include "gui_frame.h"
#include "simwin.h"
#include "components/gui_aligned_container.h"
#include "components/gui_scrollpane.h"
#include "components/gui_button.h"
#include "components/gui_label.h"
#include "components/gui_colorbox.h"
#include "components/gui_combobox.h"
#include "components/gui_tab_panel.h"
#include "components/gui_vehicle_capacitybar.h"
#include "components/action_listener.h"
#include "../convoihandle_t.h"

#include "../vehicle/vehicle.h"

#include "../utils/cbuffer_t.h"
#include "../tpl/slist_tpl.h"

#include "../bauer/goods_manager.h"

class vehicle_desc_t;

struct accommodation_t
{
	uint8 catg_index=goods_manager_t::INDEX_NONE;
	uint8 accommo_class=0;
	const char* name{};

	bool is_match(accommodation_t a)
	{
		if (a.accommo_class == accommo_class && !strcmp(a.name,name) && a.accommo_class==accommo_class ) {
			return true;
		}
		return false;
	}

	void set_accommodation(const vehicle_desc_t *veh, uint8 a_class)
	{
		catg_index = veh->get_freight_type()->get_catg_index();
		accommo_class = a_class;
		name = veh->get_accommodation_name(a_class);
	}
};

struct accommodation_info_t
{
	accommodation_t accommodation;
	uint16 capacity;
	uint16 count; // total compartments
	uint8 assingned_class;
	uint8 min_comfort;
	uint8 max_comfort;

	static int compare(const accommodation_info_t & l, const accommodation_info_t &r) {
		int comp = l.accommodation.catg_index - r.accommodation.catg_index;
		if (comp==0) {
			comp = l.assingned_class - r.assingned_class;
		}
		return comp;
	}
};


class accommodation_summary_t
{
	slist_tpl<accommodation_info_t> accommo_list;
public:
	void clear() { accommo_list.clear(); }
	void add_vehicle(vehicle_t *veh);
	void add_convoy(convoihandle_t cnv);
	void add_line(linehandle_t line);

	const slist_tpl<accommodation_info_t>& get_accommodations() const { return accommo_list; }
};


class gui_accommodation_fare_manager_t : public gui_aligned_container_t
{
	accommodation_summary_t accommodations;
	linehandle_t line;
	convoihandle_t cnv;

public:
	gui_accommodation_fare_manager_t(linehandle_t line);
	gui_accommodation_fare_manager_t(convoihandle_t cnv);

	void update();

	void set_line(linehandle_t line = linehandle_t());

	using gui_aligned_container_t::get_min_size;
	using gui_aligned_container_t::get_max_size;
};

class gui_cabin_fare_changer_t : public gui_aligned_container_t, private action_listener_t
{
private:
	vehicle_t *vehicle;
	uint8 cabin_class;

	gui_fluctuation_triangle_t up_or_down;
	button_t bt_up, bt_down;
	gui_label_buf_t lb_assigned_fare;

	uint8 old_assingned_fare;


public:
	gui_cabin_fare_changer_t(vehicle_t *v, uint8 original_class);
	void draw(scr_coord offset) OVERRIDE;
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


class gui_accommo_fare_changer_t : public gui_aligned_container_t, private action_listener_t
{
private:
	convoihandle_t cnv;
	linehandle_t line;
	accommodation_t acm;
	uint8 current_fare;

	button_t bt_up, bt_down;

	void change_convoy_fare_class(convoihandle_t cnv, uint8 target_fare);

public:
	gui_accommo_fare_changer_t(convoihandle_t cnv, accommodation_t acm, uint8 current_fare_class=0);
	gui_accommo_fare_changer_t(linehandle_t line, accommodation_t acm, uint8 current_fare_class=0);
	void init(uint8 current_fare_class);
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


class vehicle_class_manager_t : public gui_frame_t , private action_listener_t
{
	convoihandle_t cnv;

	cbuffer_t title_buf;

	gui_label_buf_t lb_total_max_income, lb_total_running_cost, lb_total_maint;
	button_t bt_init_fare;
	gui_convoy_loading_info_t capacity_info;

	gui_tab_panel_t tabs;

	gui_aligned_container_t cont_by_vehicle;
	gui_accommodation_fare_manager_t cont_by_accommo;
	gui_scrollpane_t scrolly_by_vehicle, scrolly_by_accommo;

	// update triggar
	uint8 old_vehicle_count = 0;
	uint8 old_month = 0;

	void init(convoihandle_t cnv);

public:
	vehicle_class_manager_t(convoihandle_t cnv);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	const char * get_help_filename() const OVERRIDE {return "vehicle_class_manager.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void update_list();

	// recalc income and update its label.
	void recalc_income();

	// this constructor is only used during loading
	vehicle_class_manager_t();

	void rdwr(loadsave_t *file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_class_manager; }
};

#endif
