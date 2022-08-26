/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_SPEEDBAR_H
#define GUI_COMPONENTS_GUI_SPEEDBAR_H


#include "gui_component.h"
#include "../../tpl/slist_tpl.h"


class gui_speedbar_t : public gui_component_t
{
private:
	struct info_t {
		PIXVAL color;
		const sint32 *value;
		sint32 last;
	};

	slist_tpl <info_t> values;

	sint32 base;
	bool vertical;

public:
	gui_speedbar_t() { base = 100; vertical = false; }

	void add_color_value(const sint32 *value, PIXVAL color);

	void set_base(sint32 base);

	void set_vertical(bool vertical) { this->vertical = vertical; }

	/**
	 * Draw the component
	 */
	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};

class gui_speedbar_fixed_length_t : public gui_speedbar_t
{
	scr_coord_val fixed_width;
public:
	gui_speedbar_fixed_length_t() : gui_speedbar_t(), fixed_width(10) {}
	scr_size get_min_size() const OVERRIDE { return scr_size(fixed_width, gui_speedbar_t::get_min_size().h); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
	void set_width(scr_coord_val w) OVERRIDE { fixed_width = w; }
};

class gui_tile_occupancybar_t : public gui_component_t
{
private:
	uint32 convoy_length;
	bool insert_mode = false;
	bool incomplete = false;
	sint8 new_veh_length = 0;

	// returns convoy length including extra margin.
	// for some reason, convoy may have "extra margin"
	// this correction corresponds to the correction in convoi_t::get_tile_length()
	uint32 adjust_convoy_length(uint32 total_len, uint8 last_veh_len);
	// these two are needed for adding automatic margin
	uint8 last_veh_length;
	uint8 switched_last_veh_length = -1;

	// specify fill width and color of specified tile
	void fill_with_color(scr_coord offset, uint8 tile_index, uint8 from, uint8 to, PIXVAL color, uint8 length_to_pixel);

public:
	void set_base_convoy_length(uint32 convoy_length, uint8 last_veh_length);

	// 3rd argument - If remove the last vehicle, need to set the value of the previous vehicle length.
	// This is necessary to calculate the correct value of length related adding automatic margin.
	void set_new_veh_length(sint8 new_veh_length, bool insert = false, uint8 new_last_veh_length = 0xFFu);

	void set_assembling_incomplete(bool incomplete);

	void draw(scr_coord offset);

	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, D_LABEL_HEIGHT); }
};


// route progress bar
class gui_routebar_t : public gui_component_t
{
private:
	const sint32 *value;
	const sint32 *reserve_value = 0;
	sint32 base;
	uint8 state;
	PIXVAL reserved_color;

public:
	gui_routebar_t() { base = 100; state = 0; }
	void set_reservation(const sint32 *value, PIXVAL color = color_idx_to_rgb(COL_BLUE));
	void set_reserved_color(PIXVAL color) { reserved_color = color; };
	void set_base(sint32 base);
	void init(const sint32 *value, uint8 state);

	void set_state(uint8 state);

	/**
	 * Draw the component
	 */
	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};

class gui_bandgraph_t : public gui_component_t
{
private:
	sint32 total = 0;
	bool size_fixed;
	struct info_t {
		PIXVAL color;
		const sint32 *value;
		bool cylinder_style;
	};
	slist_tpl <info_t> values;

public:
	gui_bandgraph_t(scr_size size = D_INDICATOR_SIZE, bool size_fixed_ = true) { set_size(size); size_fixed=size_fixed_; }

	void add_color_value(const sint32 *value, PIXVAL color, bool cylinder_style=false);

	void clear() { values.clear(); }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};
#endif
