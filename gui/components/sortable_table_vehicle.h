/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_SORTABLE_TABLE_VEHICLE_H
#define GUI_COMPONENTS_SORTABLE_TABLE_VEHICLE_H


#include "sortable_table.h"
#include "gui_aligned_container.h"
#include "gui_label.h"
#include "gui_scrolled_list.h"
#include "gui_image.h"
#include "gui_table.h"
#include "../../display/scr_coord.h"
#include "../../vehicle/vehicle.h"
#include "../../utils/cbuffer_t.h"

#include "gui_scrolled_list.h"

#include "gui_chart.h"
#include "gui_container.h"


class vehicle_side_image_cell_t : public text_cell_t
{
	image_id side_image;
	scr_coord remove_offset;

public:
	vehicle_side_image_cell_t(image_id img, const char* text = NULL);

	void draw(scr_coord offset) OVERRIDE;
};


class vehicle_class_cell_t : public values_cell_t
{
	const vehicle_desc_t* veh_type;

public:
	vehicle_class_cell_t(const vehicle_desc_t* desc);

	void set_width(scr_coord_val new_width) OVERRIDE;

	const uint8 get_type() const OVERRIDE { return cell_values; }

	void draw(scr_coord offset) OVERRIDE;
};

class engine_type_cell_t : public value_cell_t
{
public:
	engine_type_cell_t(vehicle_desc_t::engine_t et);
};


class vehicle_status_bar_t : public values_cell_t
{
	const vehicle_desc_t* veh_type;
	PIXVAL l_color, r_color;

public:
	vehicle_status_bar_t(const vehicle_desc_t* desc);

	void set_width(scr_coord_val new_width) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};


class vehicle_desc_row_t : public gui_sort_table_row_t
{
public:
	enum sort_mode_t {
		VD_NAME,
		VD_STATUSBAR,
		VD_COST,
		VD_ENGINE_TYPE,
		VD_POWER,
		VD_TRACTIVE_FORCE,
		VD_SPEED,
		VD_FREIGHT_TYPE,
		VD_CAPACITY,
		VD_WEIGHT,
		VD_AXLE_LOAD,
		VD_INTRO_DATE,
		VL_RETIRE_DATE,
		MAX_COLS
	};

	static int sort_mode;
	static bool sortreverse;
	// false=show name, true=show side view
	static bool side_view_mode;

	// 1=fuel filer on, 2=freight type fiter on
	enum {
		VL_NO_FILTER = 0,
		VL_FILTER_FUEL = 1 << 0,
		VL_FILTER_FREIGHT = 1 << 1,
		VL_FILTER_UPGRADABLE = 1 << 2
	};
	static uint8 filter_flag;
	uint8 old_filter_flag=0;

private:
	const vehicle_desc_t* veh_type=NULL;
	cbuffer_t tooltip_buf;

	void update_highlight();

public:
	vehicle_desc_row_t(const vehicle_desc_t* desc);

	char const* get_text() const OVERRIDE { return veh_type->get_name(); }

	bool infowin_event(event_t const* ev) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;

	static bool compare(const gui_component_t* aa, const gui_component_t* bb);
};


#endif
