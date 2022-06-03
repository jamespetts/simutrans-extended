/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_LINE_COLOR_GUI_H
#define GUI_LINE_COLOR_GUI_H


#include "gui_frame.h"
#include "../player/simplay.h"
#include "components/gui_label.h"
#include "components/gui_line_lettercode.h"
#include "components/gui_textinput.h"


class choose_color_button_t;

class line_color_gui_t : public gui_frame_t, action_listener_t
{
	linehandle_t line;

	char tmp_linecode_l[4];
	char tmp_linecode_r[4];

	uint8 new_color_idx = 255;
	uint8 new_style = 0;

	gui_line_lettercode_t lc_preview;
	uint8 old_preview_width = 0;

	gui_aligned_container_t cont_palette, cont_edit_lc;
	choose_color_button_t* line_colors[MAX_LINE_COLOR_PALETTE];

	gui_label_buf_t lb_line_name;
	button_t bt_edit, bt_reset;
	// design flag buttons
	button_t bt_enable_line_color, bt_enable_line_lc, bt_frame_flag, bt_colored_bg, bt_uncolored_bg, bt_left_box_flag, bt_right_box_flag;
	gui_textinput_t inp_letter_code, inp_number;

	void init();
	void reset_line_color_buttons();
	inline void clear_text()
	{
		tmp_linecode_l[0] = '\0';
		tmp_linecode_r[0] = '\0';
		bt_left_box_flag.pressed = false;
		bt_right_box_flag.pressed = false;
		bt_left_box_flag.disable();
		bt_right_box_flag.disable();
		new_style &= ~simline_t::left_roundbox_flag;
		new_style &= ~simline_t::right_roundbox_flag;
		lc_preview.clear_text();
	}

	inline PIXVAL get_line_colorval(uint8 idx)
	{
		switch (idx) {
		case 254: // use player color1
			return (line.is_bound() ? color_idx_to_rgb(line->get_owner()->get_player_color1() + 4) : color_idx_to_rgb(COL_WHITE));
		case 255: // diable
			return 0;
		default:
			return line_color_idx_to_rgb(idx);
		}
	}

	// something has edited
	bool has_changed();
	// edited something => reset execute button enable state
	inline void set_execute_button_state()
	{
		bt_edit.enable(has_changed());
		bt_reset.enable(has_changed());
	}

public:
	line_color_gui_t(linehandle_t line_);

	const char * get_help_filename() const OVERRIDE { return "line_color_edit.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;
};

#endif
