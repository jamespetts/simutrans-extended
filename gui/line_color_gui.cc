/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "line_color_gui.h"
#include "simwin.h"
#include "components/gui_divider.h"
#include "../utils/simstring.h"
#include "messagebox.h"
#include "schedule_list.h"
#include "../simmenu.h"

class choose_color_button_t : public button_t
{
	scr_coord_val w;
public:
	choose_color_button_t() : button_t()
	{
		w = max(D_BUTTON_HEIGHT, display_get_char_width('X') + gui_theme_t::gui_button_text_offset.w + gui_theme_t::gui_button_text_offset_right.x);
	}
	scr_size get_min_size() const OVERRIDE
	{
		return scr_size(w, D_BUTTON_HEIGHT);
	}
};

line_color_gui_t::line_color_gui_t(linehandle_t line_) :
	gui_frame_t(translator::translate("edit_line_color"), line_->get_owner()),
	lc_preview(0)
{
	line = line_;
	set_table_layout(1,0);

	bt_enable_line_color.init(button_t::square_state, "Use unique line color");
	bt_enable_line_color.add_listener(this);

	bt_enable_line_lc.init(button_t::square_state, "Use line letter code");
	bt_enable_line_lc.add_listener(this);

	bt_colored_bg.init(button_t::box_state, "Colored", scr_coord(0,0), scr_size(LINEASCENT*8, D_BUTTON_HEIGHT));
	bt_colored_bg.add_listener(this);
	bt_uncolored_bg.init(button_t::box_state, "White", scr_coord(0,0), scr_size(LINEASCENT*8, D_BUTTON_HEIGHT));
	bt_uncolored_bg.background_color=color_idx_to_rgb(COL_WHITE);
	bt_uncolored_bg.add_listener(this);

	bt_left_box_flag.init(button_t::square_state, "");
	bt_left_box_flag.add_listener(this);
	bt_right_box_flag.init(button_t::square_state, "");
	bt_right_box_flag.add_listener(this);

	inp_letter_code.set_width(LINESPACE*3);
	inp_letter_code.add_listener(this);
	inp_number.set_size( scr_size(LINESPACE*3, D_EDIT_HEIGHT) );
	inp_number.add_listener(this);

	bt_frame_flag.init(button_t::square_state, "Frame with corners");
	bt_frame_flag.add_listener(this);

	bt_edit.init(button_t::roundbox, "save", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_edit.set_tooltip("Save your changes and close the dialog.");
	bt_edit.add_listener(this);
	bt_reset.init(button_t::roundbox, "reset", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_reset.add_listener(this);

	cont_palette.set_table_layout(1,0);
	cont_palette.new_component<gui_label_t>("COLOR_CHOOSE\n");
	cont_palette.add_table(MAX_LINE_COLOR_PALETTE / 4, 4);
	{
		for (uint8 i = 0; i < MAX_LINE_COLOR_PALETTE; i++) {
			line_colors[i] = cont_palette.new_component<choose_color_button_t>();
			const PIXVAL box_color = line_color_idx_to_rgb(i);
			line_colors[i]->pressed = (line->get_line_color() == box_color);
			line_colors[i]->init(button_t::box_state, line_colors[i]->pressed ? "X" : "");
			line_colors[i]->background_color = box_color;
			line_colors[i]->add_listener(this);
		}
	}
	cont_palette.end_table();

	cont_edit_lc.set_table_layout(2,1);
	cont_edit_lc.new_component<gui_margin_t>(D_MARGIN_LEFT);
	cont_edit_lc.add_table(1, 0);
	{
		cont_edit_lc.add_table(3,1)->set_spacing(scr_size(0,1));
		{
			cont_edit_lc.new_component<gui_label_t>("Base color:");
			cont_edit_lc.add_component(&bt_colored_bg);
			cont_edit_lc.add_component(&bt_uncolored_bg);
		}
		cont_edit_lc.end_table();
		cont_edit_lc.add_table(3,3)->set_alignment(ALIGN_CENTER_H);
		{
			cont_edit_lc.new_component<gui_margin_t>(5);
			cont_edit_lc.new_component<gui_label_t>("Letter code");
			cont_edit_lc.new_component<gui_label_t>("Number");

			cont_edit_lc.new_component<gui_margin_t>(5);
			cont_edit_lc.add_component(&inp_letter_code);
			cont_edit_lc.add_component(&inp_number);

			cont_edit_lc.new_component<gui_label_t>("sub_bg:")->set_tooltip("helptxt_linecode_use_sub_background");
			cont_edit_lc.add_component(&bt_left_box_flag);
			cont_edit_lc.add_component(&bt_right_box_flag);
		}
		cont_edit_lc.end_table();
	}
	cont_edit_lc.end_table();

	init();
}


void line_color_gui_t::init()
{
	new_color_idx = line->get_line_color_index();
	new_style = line->get_line_lettercode_style();
	reset_line_color_buttons();
	tstrncpy(tmp_linecode_l, line->get_linecode_l(), sizeof(tmp_linecode_l));
	inp_letter_code.set_text(tmp_linecode_l, sizeof(tmp_linecode_l));
	tstrncpy(tmp_linecode_r, line->get_linecode_r(), sizeof(tmp_linecode_r));
	inp_number.set_text(tmp_linecode_r, sizeof(tmp_linecode_r));
	lc_preview.set_linecode_l(tmp_linecode_l);
	lc_preview.set_linecode_r(tmp_linecode_r);

	bt_enable_line_color.pressed = new_color_idx!=255;
	bt_enable_line_lc.pressed = (new_style || tmp_linecode_l[0]!='\0' || tmp_linecode_r[0]!='\0');
	if (new_color_idx == 255 && (bt_enable_line_lc.pressed || bt_enable_line_color.pressed)) {
		new_color_idx--;
	}
	else if (new_color_idx<MAX_LINE_COLOR_PALETTE) {
		line_colors[new_color_idx]->pressed = true;
		line_colors[new_color_idx]->set_text("X");
	}
	lc_preview.set_base_color( get_line_colorval(new_color_idx) );
	bt_colored_bg.background_color = get_line_colorval(new_color_idx);

	bt_frame_flag.pressed     = new_style & simline_t::frame_flag;
	bt_uncolored_bg.pressed   = new_style & simline_t::white_bg_flag;
	bt_colored_bg.pressed     = !bt_uncolored_bg.pressed;
	if (tmp_linecode_l[0] == '\0' && tmp_linecode_r[0] == '\0') {
		clear_text();
	}
	else {
		bt_left_box_flag.enable();
		bt_right_box_flag.enable();
		bt_left_box_flag.pressed  = new_style & simline_t::left_roundbox_flag;
		bt_right_box_flag.pressed = new_style & simline_t::right_roundbox_flag;
	}
	bt_frame_flag.enable(bt_enable_line_lc.pressed || bt_enable_line_color.pressed);
	if( !bt_frame_flag.enabled() ) {
		bt_frame_flag.pressed = false;
		new_style &= ~simline_t::frame_flag;
	}
	cont_palette.set_visible(bt_enable_line_color.pressed);
	cont_edit_lc.set_visible(bt_enable_line_lc.pressed);

	lc_preview.set_style(new_style);

	bt_edit.disable();
	bt_reset.disable();

	remove_all();
	// line name
	add_table(2,1);
	{
		add_component(&lc_preview);
		lb_line_name.buf().append(line->get_name());
		lb_line_name.update();
		add_component(&lb_line_name);
	}
	end_table();

	// color palette
	add_component(&bt_enable_line_color);
	add_table(1,1)->set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(0,0));
	{
		add_component(&cont_palette);
	}
	end_table();

	new_component<gui_divider_t>();

	// design settings
	add_component(&bt_enable_line_lc);
	add_component(&cont_edit_lc);

	add_component(&bt_frame_flag);
	new_component<gui_empty_t>();

	// finish/reset button
	add_table(4,1);
	{
		new_component<gui_fill_t>();
		add_component(&bt_edit);
		add_component(&bt_reset);
		new_component<gui_fill_t>();
	}
	end_table();

	reset_min_windowsize();
}

bool line_color_gui_t::has_changed()
{
	if (!line.is_bound()) {
		return false;
	}
	if (line->get_line_color_index() != new_color_idx) return true;
	if (line->get_line_lettercode_style() != new_style) return true;
	if ( strcmp(line->get_linecode_l(),tmp_linecode_l) != 0 ) return true;
	if ( strcmp(line->get_linecode_r(),tmp_linecode_r) != 0 ) return true;

	return false;
}

void line_color_gui_t::reset_line_color_buttons()
{
	for (uint8 j = 0; j < MAX_LINE_COLOR_PALETTE; j++) {
		line_colors[j]->pressed = false;
		line_colors[j]->set_text("");
	}
}

bool line_color_gui_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( comp==&bt_reset ) {
		init();
		return true;
	}
	if( comp==&bt_enable_line_color || comp == &bt_enable_line_lc ) {
		if (comp == &bt_enable_line_color) {
			bt_enable_line_color.pressed = !bt_enable_line_color.pressed;
			new_color_idx = 255;
			if(!bt_enable_line_color.pressed) {
				reset_line_color_buttons();
			}
			cont_palette.set_visible(bt_enable_line_color.pressed);
		}
		if( comp==&bt_enable_line_lc ) {
			bt_enable_line_lc.pressed = !bt_enable_line_lc.pressed;
			clear_text();
			if (!bt_enable_line_lc.pressed && !bt_enable_line_color.pressed) {
				new_color_idx=255;
			}
			cont_edit_lc.set_visible(bt_enable_line_lc.pressed);
		}
		if (new_color_idx==255 && (bt_enable_line_lc.pressed || bt_enable_line_color.pressed)) {
			new_color_idx--;
		}
		bt_frame_flag.enable(bt_enable_line_lc.pressed || bt_enable_line_color.pressed);
		if( !bt_frame_flag.enabled() ) {
			bt_frame_flag.pressed=false;
			new_style &=~simline_t::frame_flag;
		}
		lc_preview.set_style(new_style);
		lc_preview.set_base_color(get_line_colorval(new_color_idx));
		bt_colored_bg.background_color = get_line_colorval(new_color_idx);
		set_execute_button_state();
		reset_min_windowsize();
		return true;
	}
	if( comp==&bt_colored_bg ) {
		bt_colored_bg.pressed   = true;
		bt_uncolored_bg.pressed = false;
		new_style &= ~simline_t::white_bg_flag;
		lc_preview.set_style(new_style);
		set_execute_button_state();
		return true;
	}
	if( comp==&bt_uncolored_bg ) {
		bt_uncolored_bg.pressed = true;
		bt_colored_bg.pressed   = false;
		new_style |= simline_t::white_bg_flag;
		lc_preview.set_style(new_style);
		set_execute_button_state();
		return true;
	}
	if( comp==&bt_left_box_flag ) {
		bt_left_box_flag.pressed = !bt_left_box_flag.pressed;
		new_style &= ~simline_t::left_roundbox_flag;
		if (bt_left_box_flag.pressed) new_style |= simline_t::left_roundbox_flag;
		lc_preview.set_style(new_style);
		set_execute_button_state();
		return true;
	}
	if( comp==&bt_right_box_flag ) {
		bt_right_box_flag.pressed = !bt_right_box_flag.pressed;
		new_style &= ~simline_t::right_roundbox_flag;
		if (bt_right_box_flag.pressed) new_style |= simline_t::right_roundbox_flag;
		lc_preview.set_style(new_style);
		set_execute_button_state();
		return true;
	}
	// color button
	for (uint8 i = 0; i < MAX_LINE_COLOR_PALETTE; i++) {
		if (comp == line_colors[i]) {
			reset_line_color_buttons();
			line_colors[i]->pressed=true;
			line_colors[i]->set_text("X");
			new_color_idx = i;
			lc_preview.set_base_color(get_line_colorval(new_color_idx));
			bt_colored_bg.background_color = get_line_colorval(new_color_idx);
			set_execute_button_state();
			return true;
		}
	}
	if(  comp==&inp_letter_code || comp == &inp_number  ) {
		if( comp==&inp_letter_code ) {
			tstrncpy( tmp_linecode_l, inp_letter_code.get_text(), sizeof(line->get_linecode_l()) );
			lc_preview.set_linecode_l(tmp_linecode_l);
		}
		else if( comp==&inp_number ) {
			tstrncpy( tmp_linecode_r, inp_number.get_text(), sizeof(line->get_linecode_r()) );
			lc_preview.set_linecode_r(tmp_linecode_r);
		}
		if( tmp_linecode_l[0]=='\0' && tmp_linecode_r[0]=='\0' ) {
			clear_text();
		}
		else {
			bt_left_box_flag.enable();
			bt_right_box_flag.enable();
		}
		set_execute_button_state();
		reset_min_windowsize();
		return true;
	}
	if( comp==&bt_frame_flag ) {
		bt_frame_flag.pressed = !bt_frame_flag.pressed;
		new_style &= ~simline_t::frame_flag;
		if (bt_frame_flag.pressed) new_style |= simline_t::frame_flag;
		lc_preview.set_style(new_style);
		set_execute_button_state();
		return true;
	}

	// save
	if( comp==&bt_edit  &&  line.is_bound() ) {
		if ( line->get_owner()==world()->get_active_player()  &&  !world()->get_active_player()->is_locked() ) {
			if (has_changed()) {
				// text changed => call tool
				cbuffer_t buf;
				buf.printf("A%u,%s", line.get_id(), tmp_linecode_l);
				tool_t *tool = create_tool(TOOL_RENAME | SIMPLE_TOOL);
				tool->set_default_param(buf);
				world()->set_tool(tool, line->get_owner());

				buf.clear();
				buf.printf("B%u,%s", line.get_id(), tmp_linecode_r);
				tool = create_tool(TOOL_RENAME | SIMPLE_TOOL);
				tool->set_default_param(buf);
				world()->set_tool(tool, line->get_owner());

				buf.clear();
				buf.printf("A,%u,%u,%u", line.get_id(), new_color_idx, new_style);
				tool = create_tool(TOOL_CHANGE_LINE | SIMPLE_TOOL);
				tool->set_default_param(buf);
				world()->set_tool(tool, line->get_owner());
				// since init always returns false, it is safe to delete immediately
				delete tool;
				destroy_win(this);
				schedule_list_gui_t *sl = dynamic_cast<schedule_list_gui_t *>(win_get_magic(magic_line_management_t + line->get_owner()->get_player_nr()));
				if (sl) {
					sl->update_data(line);
				}
			}
		}
		else {
			// Permission error
			create_win(new news_img("You do not have permission to edit!"), w_time_delete, magic_none);
		}
		return true;
	}
	return false;
}

void line_color_gui_t::draw(scr_coord pos, scr_size size)
{
	if (!line.is_bound()) {
		destroy_win(this);
	}
	if (old_preview_width != lc_preview.get_size().w) {
		old_preview_width = lc_preview.get_size().w;
		reset_min_windowsize();
	}

	gui_frame_t::draw(pos, size);
}
