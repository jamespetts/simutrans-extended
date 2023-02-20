/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_VEHICLE_CARGOINFO_H
#define GUI_COMPONENTS_GUI_VEHICLE_CARGOINFO_H


#include "gui_container.h"
#include "gui_aligned_container.h"
#include "gui_button.h"
#include "../../simhalt.h"
#include "../../convoihandle_t.h"
#include "../../vehicle/vehicle.h"
#include "../../dataobj/schedule.h"
#include "../../utils/simstring.h"


// Display building information in one line
// (1) building name, (2) city name, (3) pos
class gui_destination_building_info_t : public gui_aligned_container_t, private action_listener_t
{
	bool is_freight; // For access to the factory

	// Given access to the factory, we need to remember the factory to avoid crashes if the factory is closed.
	// (Also, unlike the case of pos, there is no need to support the rotation of coordinates.)
	fabrik_t *fab=NULL;

	button_t button; // posbutton for the building / access button for the factory

public:
	gui_destination_building_info_t(koord zielpos, bool is_freight);

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};

// Band graph showing loading status based on capacity for each vehicle "accommodation class"
// The color is based on the color of the cargo
class gui_capacity_occupancy_bar_t : public gui_container_t
{
	vehicle_t *veh;
	uint8 a_class;
	bool size_fixed = true;

public:
	gui_capacity_occupancy_bar_t(vehicle_t *v, uint8 accommo_class=0);

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE;

	void set_size_fixed(bool yesno) { size_fixed = yesno; };

	void display_loading_bar(scr_coord_val xp, scr_coord_val yp, scr_coord_val w, scr_coord_val h, PIXVAL color, uint16 loading, uint16 capacity, uint16 overcrowd_capacity);
	void draw(scr_coord offset) OVERRIDE;
};


class gui_vehicle_cargo_info_t : public gui_aligned_container_t
{
	// Note: different from freight_list_sorter_t
	enum filter_mode_t : uint8 {
		hide_detail = 0,
		by_unload_halt,      // (by wealth)
		by_destination_halt, // (by wealth)
		by_final_destination // (by wealth)
	};

	schedule_t * schedule;
	vehicle_t *veh;
	uint16 total_cargo=0;
	uint8 show_loaded_detail = by_unload_halt;

public:
	gui_vehicle_cargo_info_t(vehicle_t *v, uint8 display_loaded_detail);

	void draw(scr_coord offset) OVERRIDE;

	void update();
};

class gui_cargo_info_t : public gui_aligned_container_t
{
public:
	enum sort_mode_t {
		by_amount   = 0,
		by_via      = 1,
		by_origin   = 2,
		by_category = 3,
		SORT_MODES  = 4
	};

	static bool sort_reverse;

private:
	convoihandle_t cnv;


public:
	gui_cargo_info_t(convoihandle_t cnv);
	void init(uint8 depth_from, uint8 depth_to, bool divide_wealth = false, uint8 sort_mode = 0, uint8 catg_index=goods_manager_t::INDEX_NONE, uint8 fare_class=0);

	void build_table();
};

class gui_convoy_cargo_info_t : public gui_aligned_container_t
{
	convoihandle_t cnv;
	uint8 info_depth_from = 0;
	uint8 info_depth_to   = 1;
	bool divide_by_wealth = false;
	bool separate_by_fare = true;
	uint8 sort_mode = 0;

	uint8 old_vehicle_count=1;
	uint16 old_total_cargo=0;

public:
	gui_convoy_cargo_info_t(convoihandle_t cnv = convoihandle_t());

	void update();

	void set_convoy(convoihandle_t c) { cnv = c;  update(); }
	void set_mode(uint8 depth_from, uint8 depth_to, bool divide_goods_wealth=false, bool separate_by_vehicle_fare = true, uint8 sort_mode=0);

	void draw(scr_coord offset) OVERRIDE;
};


#endif
