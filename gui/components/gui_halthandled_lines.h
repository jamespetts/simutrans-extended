/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_HALTHANDLED_LINES_H
#define GUI_COMPONENTS_GUI_HALTHANDLED_LINES_H


#include "gui_aligned_container.h"
#include "gui_label.h"

#include "../../display/simgraph.h" //LINEASCENT

#include "../../simhalt.h"

#define L_COLOR_LABEL_PADDING_X (LINEASCENT/3)

class gui_color_label_t : public gui_label_buf_t
{
	PIXVAL bg_color;
	uint8 style;

public:
	gui_color_label_t(const char* text = NULL, PIXVAL text_color = SYSCOL_TEXT, PIXVAL bg_color = color_idx_to_rgb(COL_WHITE), uint8 style=0) :
		gui_label_buf_t(text_color)
	{
		this->bg_color = bg_color;
		this->style = style;
		buf().append(text);
		update();
		set_fixed_width(gui_label_buf_t::get_min_size().w);
		set_size(gui_label_buf_t::get_min_size()+scr_size(L_COLOR_LABEL_PADDING_X*2,0));
	}

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_halthandled_lines_t : public gui_aligned_container_t
{
	halthandle_t halt;

	uint16 old_connex = 0;

	void update_table();

public:
	gui_halthandled_lines_t(halthandle_t halt);

	void draw(scr_coord offset) OVERRIDE;
};

#endif
