/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_VEHICLE_CARGOINFO_H
#define GUI_COMPONENTS_GUI_VEHICLE_CARGOINFO_H


#include "gui_container.h"
#include "gui_aligned_container.h"
#include "../../convoihandle_t.h"
#include "../../vehicle/vehicle.h"
#include "../../dataobj/schedule.h"


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


#endif
