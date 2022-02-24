/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_LINE_LETTERCODE_H
#define GUI_COMPONENTS_GUI_LINE_LETTERCODE_H


#include "../../simline.h"
#include "gui_container.h"

class gui_line_lettercode_t: public gui_container_t
{
private:
	uint8 style;
	PIXVAL line_color;
	char linecode_l[4] = {};
	char linecode_r[4] = {};

public:
	gui_line_lettercode_t(PIXVAL color_val, uint8 line_lettercode_style = simline_t::no_letter_code);

	// if line_color_index=254, set player_color1. col_val=0(black) means disable
	void set_base_color(PIXVAL color_val) { line_color = color_val; }

	void set_style(uint8 s = simline_t::no_letter_code) { style = s; }

	void set_linecode_l(const char *str);
	void set_linecode_r(const char *str);

	void set_line(linehandle_t line);

	void init_size(scr_coord_val width);

	inline void clear_text()
	{
		linecode_l[0] = '\0';
		linecode_r[0] = '\0';
	}

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
