/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_COLORBOX_H
#define GUI_COMPONENTS_GUI_COLORBOX_H


#include "gui_component.h"
#include "../../simcolor.h"

// for gui_vehicle_number_t
#include "../../descriptor/vehicle_desc.h"
#include "../../utils/cbuffer_t.h"

#define HALT_WAITING_BAR_MAX_WIDTH 80
#define L_CAPPED_ARROW_WIDTH (5)

/**
 * Draws a simple colored box.
 */
class gui_colorbox_t : public gui_component_t
{
protected:
	PIXVAL color;

	scr_coord_val height = D_INDICATOR_HEIGHT;
	scr_coord_val width = D_INDICATOR_WIDTH;
	bool size_fixed = false;
	bool show_frame = true;
	scr_size padding = scr_size(0, 0);

	scr_size max_size;

	const char * tooltip;

public:
	gui_colorbox_t(PIXVAL c = 0);

	void init(PIXVAL color_par, scr_size size, bool fixed=false, bool show_frame=true) {
		set_color(color_par);
		set_size(size);
		set_size_fixed(fixed);
		set_show_frame(show_frame);
	}

	void draw(scr_coord offset) OVERRIDE;

	void set_color(PIXVAL c)
	{
		color = c;
	}

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;

	void set_size(scr_size size) OVERRIDE { width = size.w; height = size.h; max_size =size; }
	void set_size_fixed(bool yesno) { size_fixed = yesno; }
	void set_show_frame(bool yesno) { show_frame = yesno; }
	void set_padding(scr_size padding) { this->padding = padding; set_size(size + padding + padding); }

	void set_tooltip(const char * t);

	void set_max_size(scr_size s)
	{
		max_size = s;
	}
};


/**
 * Draws a simple right triangle arrow.
 */
class gui_right_pointer_t : public gui_colorbox_t
{
protected:
	uint8 height;

public:
	gui_right_pointer_t(PIXVAL c = SYSCOL_TEXT, uint8 height_ = (LINESPACE*3)>>2);

	void init(PIXVAL color_par, scr_size size) {
		set_color(color_par);
		set_size(size);
	}

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return gui_component_t::size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_operation_status_t : public gui_right_pointer_t
{
	uint8 status = operation_stop;
public:
	gui_operation_status_t(PIXVAL c = SYSCOL_TEXT, uint8 height_ = (LINESPACE*3)>>2);

	enum {
		operation_stop   = 0,
		operation_normal = 1,
		operation_pause  = 2,
		operation_invalid = 255 // nothing to show
	};

	void init(PIXVAL color_par, scr_size size) {
		set_color(color_par);
		set_size(size);
	}

	void set_status(uint8 shape_value) { status = shape_value; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return gui_component_t::size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


/**
 * Draws a colored bar to represent the role and status of the vehicle
 */
class gui_vehicle_bar_t : public gui_component_t
{
protected:
	PIXVAL color;

	uint8 flags_left;
	uint8 flags_right;
	uint8 interactivity;

public:
	gui_vehicle_bar_t(PIXVAL = COL_DANGER, scr_size size=scr_size(VEHICLE_BAR_HEIGHT*4, VEHICLE_BAR_HEIGHT));

	void init(PIXVAL color_par, scr_size size=scr_size(VEHICLE_BAR_HEIGHT*4, VEHICLE_BAR_HEIGHT)) {
		set_color(color_par);
		set_size(size);
	}

	void set_flags(uint8 flags_left_, uint8 flags_right_, uint8 interactivity_);

	void draw(scr_coord offset) OVERRIDE;

	void set_color(PIXVAL c) { color = c; }

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return size; }
};


class gui_vehicle_number_t : public gui_vehicle_bar_t
{
	bool show_frame;
	cbuffer_t buf;

	void init();

public:
	gui_vehicle_number_t(const char* text_=NULL, PIXVAL bgcol = COL_SAFETY, PIXVAL /*textcol*/ = color_idx_to_rgb(COL_WHITE), bool show_frame_ = true) :
		gui_vehicle_bar_t(bgcol) {
		show_frame = show_frame_;
		set_flags(vehicle_desc_t::can_be_head, vehicle_desc_t::can_be_head|vehicle_desc_t::can_be_tail, HAS_POWER | BIDIRECTIONAL);
		set_text(text_);
	}

	void set_text(const char* text_) {
		if (text_) {
			buf.clear();
			buf.append(text_);
		}
		init();
	}
	void draw(scr_coord offset) OVERRIDE;
};


class gui_capacity_bar_t : public gui_colorbox_t
{
	PIXVAL bg_col;
	uint16 capacity;
	uint16 loading;
	bool cylinder_style;

public:
	gui_capacity_bar_t(scr_size size, PIXVAL c = color_idx_to_rgb(COL_GREEN), bool size_fixed=true, bool cylinder_style = true):
		gui_colorbox_t(c),
		cylinder_style(cylinder_style)
	{
		gui_colorbox_t::set_size(size);
		width = size.w; height = size.h;
		set_size_fixed(size_fixed);
		bg_col = color_idx_to_rgb(COL_GREY4);
		capacity = width;
		loading  = capacity;
	}

	void set_value(uint16 capacity, uint16 loading_amount) {
		this->capacity = capacity;
		loading = loading_amount;
	};

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return scr_size(width, height); }
	scr_size get_max_size() const OVERRIDE { return gui_colorbox_t::get_max_size(); }
};


class gui_depotbox_t : public gui_colorbox_t
{
	uint8 width;

public:
	gui_depotbox_t(PIXVAL c = SYSCOL_TEXT, uint8 width = 10);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return gui_component_t::size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_fluctuation_triangle_t : public gui_colorbox_t
{
protected:
	uint8 height;
	sint64 value;

public:
	gui_fluctuation_triangle_t(sint64 value=0, uint8 height_ = (LINESPACE*3)>>2);


	void draw(scr_coord offset) OVERRIDE;

	void set_value(sint64 v) { value = v; }

	scr_size get_min_size() const OVERRIDE { return gui_component_t::size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_capped_arrow_t : public gui_component_t
{
	bool left;
public:
	gui_capped_arrow_t(bool left_arrow = false) { left = left_arrow; }

	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return scr_size(L_CAPPED_ARROW_WIDTH,5); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
