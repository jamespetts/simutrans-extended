/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_schedule_item.h"

#include "../../simworld.h"
#include "../../simhalt.h"

#include "../../dataobj/environment.h"
#include "../../descriptor/skin_desc.h"
#include "../../display/simgraph.h"
#include "../../display/viewport.h"

gui_colored_route_bar_t::gui_colored_route_bar_t(uint8 p_col, uint8 style_)
{
	style = style_;
	p_color_idx = p_col;
	size = scr_size(D_ENTRY_NO_WIDTH, LINESPACE);
}

void gui_colored_route_bar_t::draw(scr_coord offset)
{
	offset += pos + scr_coord(2,0);
	const uint8 width = (D_ENTRY_NO_WIDTH-4)/2;
	scr_coord_val offset_x = D_ENTRY_NO_WIDTH/4-1;
	PIXVAL base_colval = color_idx_to_rgb(p_color_idx-p_color_idx%8 + 3);
	size = scr_size(D_ENTRY_NO_WIDTH, LINESPACE);

	const PIXVAL alert_colval = (alert_level==1) ? COL_CAUTION : (alert_level==2) ? COL_WARNING : color_idx_to_rgb(COL_RED+1);
	// edge lines
	if (alert_level) {
		switch (style) {
		default:
			display_blend_wh_rgb(offset.x + offset_x-2, offset.y, width+4, LINESPACE, alert_colval, 60);
			break;
		case line_style::dashed:
		case line_style::thin:
			display_blend_wh_rgb(offset.x + offset_x,   offset.y, width,   LINESPACE, alert_colval, 60);
			break;
		case line_style::reversed:
		case line_style::none:
			break;
		}
	}

	// base line/image
	switch (style) {
		case line_style::solid:
		default:
			display_fillbox_wh_clip_rgb(offset.x + offset_x, offset.y, width, LINESPACE, base_colval, true);
			break;
		case line_style::thin:
		{
			const uint8 border_width = 2 + D_ENTRY_NO_WIDTH % 2;
			display_fillbox_wh_clip_rgb(offset.x + D_ENTRY_NO_WIDTH/2-3, offset.y, border_width, LINESPACE, base_colval, true);
			break;
		}
		case line_style::doubled:
		{
			const uint8 border_width = width > 6 ? 3 : 2;
			display_fillbox_wh_clip_rgb(offset.x + offset_x, offset.y, border_width, LINESPACE, base_colval, true);
			display_fillbox_wh_clip_rgb(offset.x + D_ENTRY_NO_WIDTH-4-offset_x-border_width, offset.y, border_width, LINESPACE, base_colval, true);
			break;
		}
		case line_style::downward:
			for (uint8 i = 0; i < width; i++) {
				display_vline_wh_clip_rgb(offset.x+offset_x+i, offset.y, LINESPACE-abs(width/2-i), base_colval, true);
			}
			break;
		case line_style::dashed:
			for (uint8 h=1; h+2 < LINESPACE; h+=4) {
				display_fillbox_wh_clip_rgb(offset.x + offset_x+1, offset.y + h, width-2, 2, base_colval, true);
			}
			break;
		case line_style::reversed:
			if (skinverwaltung_t::reverse_arrows) {
				display_color_img_with_tooltip(skinverwaltung_t::reverse_arrows->get_image_id(0), offset.x + (D_ENTRY_NO_WIDTH-10)/2, offset.y+2, 0, false, false, "Vehicles make a round trip between the schedule endpoints, visiting all stops in reverse after reaching the end.");
			}
			else {
				display_proportional_clip_rgb(offset.x, offset.y, translator::translate("[R]"), ALIGN_LEFT, SYSCOL_TEXT_STRONG, true);
			}
			break;
		case line_style::none:
			size = scr_size(0,0);
			break;
	}
	if (size != get_size()) {
		set_size(size);
	}
}


gui_schedule_entry_number_t::gui_schedule_entry_number_t(uint8 number_, uint8 p_col, uint8 style_, scr_size size_, koord3d pos_)
{
	number = number_ + 1;
	style = style_;
	p_color_idx = p_col;
	size=size_;
	set_size(size);
	entry_pos = pos_;

	lb_number.set_align(gui_label_t::centered);
	lb_number.set_size(size);
	lb_number.set_pos(scr_coord(0,(size.h-D_LABEL_HEIGHT)/2));
	add_component(&lb_number);
}

void gui_schedule_entry_number_t::draw(scr_coord offset)
{
	const PIXVAL base_colval = color_idx_to_rgb(p_color_idx-p_color_idx%8 + 3);
	      PIXVAL text_colval = color_idx_to_rgb(p_color_idx-p_color_idx%8 + env_t::gui_player_color_dark);
	if (number > 99) {
		size.w = proportional_string_width("000") + 6;
	}

	// draw the back image
	switch (style) {
		case number_style::halt:
			display_filled_roundbox_clip(pos.x+offset.x+2, pos.y+offset.y,   size.w-4, size.h,   base_colval, false);
			text_colval = color_idx_to_rgb(COL_WHITE);
			break;
		case number_style::interchange:
			display_filled_roundbox_clip(pos.x+offset.x,   pos.y+offset.y,   size.w,   size.h,   base_colval, false);
			display_filled_roundbox_clip(pos.x+offset.x+2, pos.y+offset.y+2, size.w-4, size.h-4, color_idx_to_rgb(COL_WHITE), false);
			break;
		case number_style::depot:
			for (uint8 i = 0; i < 3; i++) {
				const scr_coord_val w = (size.w/2) * (i+1)/4;
				display_fillbox_wh_clip_rgb(pos.x+offset.x + (size.w - w*2)/2, pos.y+offset.y + i, w*2, 1, color_idx_to_rgb(91), false);
			}
			display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y + 3, size.w, size.h - 3, color_idx_to_rgb(91), false);
			text_colval = color_idx_to_rgb(COL_WHITE);
			break;
		case number_style::none:
			display_fillbox_wh_clip_rgb(pos.x+offset.x + size.w/2 - D_ENTRY_NO_WIDTH/4+1, pos.y+offset.y, (D_ENTRY_NO_WIDTH-4)/2, size.h, base_colval, true);
			break;
		case number_style::waypoint:
			display_fillbox_wh_clip_rgb(pos.x+offset.x + size.w/2 - D_ENTRY_NO_WIDTH/4+1, pos.y+offset.y, (D_ENTRY_NO_WIDTH-4)/2, size.h, base_colval, true);
			display_filled_circle_rgb(  pos.x+offset.x + size.w/2, pos.y+offset.y + size.h/2, size.h/2, base_colval);
			break;
		default:
			display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, size.w, size.h, base_colval, false);
			text_colval = color_idx_to_rgb(COL_WHITE);
			break;
	}
	if (style != number_style::waypoint && number>0) {
		lb_number.buf().printf("%u", number);
		lb_number.set_color(text_colval);
	}
	lb_number.update();
	lb_number.set_fixed_width(size.w);

	gui_container_t::draw(offset);
}

bool gui_schedule_entry_number_t::infowin_event(const event_t *ev)
{
	if(entry_pos != koord3d::invalid) {
		if (IS_LEFTRELEASE(ev)) {
			halthandle_t halt = haltestelle_t::get_halt(entry_pos, world()->get_public_player());
			if (halt.is_bound()) {
				halt->show_info();
				return true;
			}
			return false;
		}
		else if (IS_RIGHTRELEASE(ev)) {
			world()->get_viewport()->change_world_position(entry_pos);
			return true;
		}
	}
	return false;
}

gui_convoy_arrow_t::gui_convoy_arrow_t(PIXVAL col, scr_size size_)
{
	color = col;
	size = size_;
	set_size(size);
}

void gui_convoy_arrow_t::draw(scr_coord offset)
{
	offset += pos;
	display_convoy_arrow_wh_clip_rgb(offset.x, offset.y, size.w, size.h, color, true);
}
