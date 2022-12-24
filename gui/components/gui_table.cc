/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_table.h"
#include "../../display/simgraph.h"
#include "../simwin.h"
#include "../../dataobj/translator.h"


void gui_table_cell_t::draw(scr_coord offset)
{
	// background
	display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h, bgcolor, true);

	// border
	if (border_bottom_right) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y+get_size().h-1, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x+ get_size().w-1, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_label_t::draw(offset);
}

scr_size gui_table_cell_t::get_max_size() const
{
	return scr_size(fixed_width ? get_min_size().w : scr_size::inf.w, fixed_height ? get_min_size().h : scr_size::inf.h);
}

void gui_table_header_t::draw(scr_coord offset)
{
	// border
	if (border_top_left) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_table_cell_t::draw(offset);
}


void gui_table_cell_buf_t::draw(scr_coord offset)
{
	// background
	display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h, bgcolor, true);

	// border
	if (border_bottom_right) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y+get_size().h-1, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x+ get_size().w-1, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_label_buf_t::draw(offset);
}

scr_size gui_table_cell_buf_t::get_max_size() const
{
	return scr_size(fixed_width ? get_min_size().w : scr_size::inf.w, fixed_height ? get_min_size().h : scr_size::inf.h);
}

void gui_table_header_buf_t::draw(scr_coord offset)
{
	// border
	if (border_top_left) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, 1, border_color, true);
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, 1, get_size().h, border_color, true);
	}

	gui_table_cell_buf_t::draw(offset);
}



void table_sort_button_t::set_text(const char * text)
{
	this->text = text;
	translated_text = translator::translate(text);
	scr_coord_val w = translated_text ? proportional_string_width(translated_text) : 0;
	w += 2 * D_H_SPACE + D_BUTTON_PADDINGS_X + 5; // padding + arrow size
	set_size(scr_size(w, size.h));
}

void table_sort_button_t::draw(scr_coord offset)
{
	// draw table header cell
	PIXVAL bgcolor = pressed ? SYSCOL_TH_BACKGROUND_SELECTED : SYSCOL_TH_BACKGROUND_TOP;
	text_color = pressed ? SYSCOL_TH_TEXT_SELECTED : SYSCOL_TH_TEXT_TOP;
	display_ddd_box_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, get_size().h, SYSCOL_TH_BORDER, SYSCOL_TH_BORDER);
	display_fillbox_wh_clip_rgb(pos.x + offset.x+1, pos.y + offset.y+1, get_size().w-2, get_size().h-2, bgcolor, true);

	// draw text
	const scr_rect area(offset + pos, size);
	scr_rect area_text = area - gui_theme_t::gui_button_text_offset_right;
	area_text.set_pos(gui_theme_t::gui_button_text_offset + area.get_pos()-scr_coord(2,0));
	if(  text  ) {
		display_proportional_ellipsis_rgb( area_text, translated_text, ALIGN_CENTER_H | ALIGN_CENTER_V | DT_CLIP, text_color, true );
	}
	if (pressed) {
		// draw an arrow

		scr_coord_val xoff_arrow_center = pos.x + offset.x + size.w - D_H_SPACE - 3;
		display_fillbox_wh_clip_rgb(xoff_arrow_center, pos.y+offset.y + D_V_SPACE-1, 1, size.h - D_V_SPACE*2+2, SYSCOL_TH_TEXT_SELECTED, false);
		if (reversed) {
			// asc
			display_fillbox_wh_clip_rgb(xoff_arrow_center-1, pos.y+offset.y + D_V_SPACE,   3, 1, SYSCOL_TH_TEXT_SELECTED, false);
			display_fillbox_wh_clip_rgb(xoff_arrow_center-2, pos.y+offset.y + D_V_SPACE+1, 5, 1, SYSCOL_TH_TEXT_SELECTED, false);
		}
		else {
			// desc
			display_fillbox_wh_clip_rgb(xoff_arrow_center-1, pos.y+offset.y + size.h - D_V_SPACE - 1, 3, 1, SYSCOL_TH_TEXT_SELECTED, false);
			display_fillbox_wh_clip_rgb(xoff_arrow_center-2, pos.y+offset.y + size.h - D_V_SPACE - 2, 5, 1, SYSCOL_TH_TEXT_SELECTED, false);
		}

	}
}
