/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_SCHEDULE_ITEM_H
#define GUI_COMPONENTS_GUI_SCHEDULE_ITEM_H


#include "gui_container.h"
#include "gui_label.h"

#include "../../dataobj/translator.h"


class gui_colored_route_bar_t : public gui_component_t
{
	uint8 p_color_idx;
	uint8 style;
	uint8 alert_level=0;
public:
	enum line_style {
		solid = 0,
		thin,
		doubled,
		dashed,
		downward,
		reversed, // display reverse symbol
		none
	};

	gui_colored_route_bar_t(uint8 p_color_idx, uint8 style_ = line_style::solid);

	void draw(scr_coord offset);

	void set_line_style(uint8 style_) { style = style_; };
	void set_color(uint8 color_idx) { p_color_idx = color_idx; };
	// Color the edges of the line according to the warning level.  0=ok(none), 1=yellow, 2=orange, 3=red
	void set_alert_level(uint8 level) { alert_level = level; };

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_schedule_entry_number_t : public gui_container_t
{
	uint8 p_color_idx;
	uint8 style;
	uint8 number;
	gui_label_buf_t lb_number;
public:
	enum number_style {
		halt = 0,
		interchange,
		depot,
		waypoint,
		none
	};

	gui_schedule_entry_number_t(uint8 number, uint8 p_color_idx = 8, uint8 style_ = number_style::halt, scr_size size = scr_size(D_ENTRY_NO_WIDTH, D_ENTRY_NO_HEIGHT));

	void draw(scr_coord offset);

	void init(uint8 number_, uint8 color_idx, uint8 style_ = number_style::halt) { number = number_+1; p_color_idx = color_idx; style = style_; };

	void set_number_style(uint8 style_) { style = style_; };
	void set_color(uint8 color_idx) { p_color_idx = color_idx; };

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_convoy_arrow_t : public gui_component_t
{
	PIXVAL color;
public:

	gui_convoy_arrow_t(PIXVAL color=COL_SAFETY, scr_size size = scr_size(LINESPACE*0.7, LINESPACE));

	void draw(scr_coord offset);

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
