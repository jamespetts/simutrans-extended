/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_SCHEDULE_ITEM_H
#define GUI_COMPONENTS_GUI_SCHEDULE_ITEM_H


#include "gui_container.h"
#include "gui_label.h"
#include "gui_action_creator.h"

#include "../../dataobj/translator.h"
#include "../../dataobj/koord3d.h"

#include "../../player/simplay.h"


#define L_CONVOY_ARROW_WIDTH (LINESPACE*0.8)
//#define L_CONVOY_ARROW_HEIGHT (LINESPACE)
//#define L_CONVOY_ARROW_SIZE (scr_size(L_CONVOY_ARROW_WIDTH, L_CONVOY_ARROW_HEIGHT))

void display_framed_circle_rgb(scr_coord_val x0, scr_coord_val  y0, int radius, const PIXVAL base_color, const PIXVAL frame_color);


class gui_colored_route_bar_t : public gui_component_t
{
	uint8 alert_level=0;

protected:
	bool flexible_height;
	uint8 style;
	PIXVAL base_color;

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

	gui_colored_route_bar_t(uint8 p_color_idx, uint8 style_ = line_style::solid, bool flexible_height=false);
	gui_colored_route_bar_t(PIXVAL line_color, uint8 style_ = line_style::solid, bool flexible_height=false);

	void draw(scr_coord offset) OVERRIDE;

	void set_line_style(uint8 style_) { style = style_; }
	void set_color(uint8 color_idx) { base_color = color_idx_to_rgb(color_idx - color_idx%8 + 3); }
	void set_color(PIXVAL colval) { base_color = colval; }
	// Color the edges of the line according to the warning level.  0=ok(none), 1=yellow, 2=orange, 3=red
	void set_alert_level(uint8 level) { alert_level = level; }

	void set_flexible_height(bool yesno) { flexible_height=yesno; }

	scr_size get_min_size() const OVERRIDE { return scr_size(D_ENTRY_NO_WIDTH, LINESPACE); }
	scr_size get_max_size() const OVERRIDE { return flexible_height ? scr_size(get_min_size().w, scr_size::inf.h) : get_min_size(); }
};


class gui_waypoint_box_t : public gui_colored_route_bar_t
{
	koord3d entry_pos;
	uint8 player_nr = PUBLIC_PLAYER_NR;
	bool highlight = false;

public:
	gui_waypoint_box_t(PIXVAL line_color, uint8 line_style, koord3d pos3d, waytype_t wt = ignore_wt);

	void set_highlight(bool yesno) { highlight = yesno; }

	void draw(scr_coord offset) OVERRIDE;

	bool infowin_event(event_t const*) OVERRIDE;
};


class gui_schedule_entry_number_t : public gui_container_t, public gui_action_creator_t
{
	uint8 number;
	uint8 p_color_idx;
	uint8 style;
	gui_label_buf_t lb_number;
	koord3d entry_pos;
	bool highlight=false;

public:
	enum number_style {
		halt = 0,
		interchange,
		depot,
		waypoint,
		none
	};

	gui_schedule_entry_number_t(uint8 number, uint8 p_color_idx = 8, uint8 style_ = number_style::halt, scr_size size = scr_size(D_ENTRY_NO_WIDTH, D_ENTRY_NO_HEIGHT), koord3d entry_pos = koord3d::invalid);

	bool infowin_event(event_t const*) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;

	void init(uint8 number_, uint8 color_idx, uint8 style_ = number_style::halt, koord3d pos_ = koord3d::invalid)
	{
		number = number_+1;
		p_color_idx = color_idx;
		style = style_;
		entry_pos = pos_;
	};

	void set_number_style(uint8 style_) { style = style_; }
	void set_color(uint8 color_idx) { p_color_idx = color_idx; }
	void set_highlight(bool yesno)  { highlight = yesno; }

	koord3d get_entry_pos() const { return entry_pos; }

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_convoy_arrow_t : public gui_component_t
{
	PIXVAL color;
	bool reverse;

	scr_size padding = scr_size(2, 0);

public:

	gui_convoy_arrow_t(PIXVAL color=COL_SAFETY, bool reverse=false, scr_size size = scr_size(L_CONVOY_ARROW_WIDTH+4, LINESPACE));

	void draw(scr_coord offset) OVERRIDE;

	void set_padding(scr_size padding) { this->padding = padding; set_size(size + padding + padding); }

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
