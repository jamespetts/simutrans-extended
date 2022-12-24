/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_TABLE_H
#define GUI_COMPONENTS_GUI_TABLE_H


#include "gui_component.h"
#include "gui_label.h"
#include "gui_table.h"
#include "../../simcolor.h"
#include "../gui_theme.h"
#include "gui_button.h"


/**
 * Draw the table cell
 *  - background color can be specified. The default color is the color set in the theme.
 *  - can set the bottom and right borders. (default=hidden, the default color is set in the theme.)
 *   --  use the header version if need top and left border.
 *  - height is flexible rather than fixed label height.
 *  - a little bigger than the label because it requires padding for the background.
 *  - does not support line breaks...
 *  - there is no sort function
 */
class gui_table_cell_t : public gui_label_t
{
protected:
	bool border_bottom_right;
	bool fixed_height = false;
	PIXVAL bgcolor;
	PIXVAL border_color = SYSCOL_TD_BORDER;
public:
	gui_table_cell_t(const char *text, PIXVAL bgcolor = SYSCOL_TD_BACKGROUND, align_t align = left, bool border_br = false) : gui_label_t(text, SYSCOL_TEXT, align)
	{
		set_padding(scr_size(3,1));
		border_bottom_right = border_br;
		this->bgcolor = bgcolor;
	}

	void set_border_color(PIXVAL color) { border_bottom_right = true; border_color = color; }
	void set_flexible(bool horizontally, bool vertically=true) { fixed_width = !horizontally; fixed_height = !vertically; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};

class gui_table_header_t: public gui_table_cell_t
{
	bool border_top_left;
public:
	gui_table_header_t(const char *text, PIXVAL bgcolor = SYSCOL_TH_BACKGROUND_TOP, align_t align = centered, bool border_tl=false, bool border_br = true) : gui_table_cell_t(text, bgcolor, align, border_br)
	{
		set_padding(scr_size(3,2));
		border_top_left = border_tl;
		set_color(bgcolor==SYSCOL_TH_BACKGROUND_TOP ? SYSCOL_TH_TEXT_TOP : SYSCOL_TH_TEXT_LEFT);
		set_border_color(SYSCOL_TH_BORDER);
	}

	void draw(scr_coord offset) OVERRIDE;
};


class gui_table_cell_buf_t: public gui_label_buf_t
{
protected:
	bool border_bottom_right;
	bool fixed_height = false;
	PIXVAL bgcolor;
	PIXVAL border_color=SYSCOL_TD_BORDER;
public:
	gui_table_cell_buf_t(const char *text=NULL, PIXVAL bgcolor = SYSCOL_TD_BACKGROUND, align_t align = left, bool border_br = false) : gui_label_buf_t(SYSCOL_TEXT, align)
	{
		set_text(text);
		buf().append(text);
		set_padding(scr_size(3,1));
		border_bottom_right = border_br;
		this->bgcolor = bgcolor;
		update();
	}

	void set_border_color(PIXVAL color) { border_bottom_right = true; border_color = color; }
	void set_flexible(bool horizontally, bool vertically = true) { fixed_width = !horizontally; fixed_height = !vertically; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};

class gui_table_header_buf_t: public gui_table_cell_buf_t
{
	bool border_top_left;
public:
	gui_table_header_buf_t(const char *text=NULL, PIXVAL bgcolor = SYSCOL_TH_BACKGROUND_TOP, align_t align = centered, bool border_tl=false, bool border_br = true) : gui_table_cell_buf_t(text, bgcolor, align, border_br)
	{
		set_text(text);
		buf().append(text);
		set_padding(scr_size(3,2));
		border_top_left = border_tl;
		set_color(bgcolor==SYSCOL_TH_BACKGROUND_TOP ? SYSCOL_TH_TEXT_TOP : SYSCOL_TH_TEXT_LEFT);
		set_border_color(SYSCOL_TH_BORDER);
	}

	void draw(scr_coord offset) OVERRIDE;
};


class table_sort_button_t : public button_t {
	bool reversed = false;

public:
	table_sort_button_t() {
		init(button_t::roundbox_middle_state, "");
	}

	void set_reverse(bool yesno = false) { reversed=yesno; }

	// set text and set size (including sort arrow space)
	void set_text(const char * text) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


#endif
