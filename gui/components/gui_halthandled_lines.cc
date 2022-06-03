/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_halthandled_lines.h"
#include "gui_image.h"

#include "../../simcolor.h"
#include "../../simworld.h"

#include "../../utils/cbuffer_t.h"

#include "../gui_theme.h"

#include "../../player/simplay.h"
#include "../../linehandle_t.h"
#include "../../simline.h"


void gui_color_label_t::draw(scr_coord offset)
{
	switch (style)
	{
	case 1:
		display_filled_roundbox_clip(pos.x+offset.x, pos.y+offset.y, size.w, size.h, bg_color, false);
		break;
	default:
		display_fillbox_wh_clip_rgb(pos.x+offset.x+1, pos.y+offset.y+1, size.w-2, size.h-2, bg_color, false);
		break;
	}

	gui_label_buf_t::draw(scr_coord(offset.x+L_COLOR_LABEL_PADDING_X, offset.y));
}

gui_halthandled_lines_t::gui_halthandled_lines_t(halthandle_t halt)
{
	this->halt = halt;
	update_table();
}

void gui_halthandled_lines_t::update_table()
{
	remove_all();
	old_connex = halt->registered_convoys.get_count() + halt->registered_lines.get_count();
	set_table_layout(simline_t::MAX_LINE_TYPE-1,1);
	set_spacing(scr_size(2,0));
	for( uint8 lt=1; lt < simline_t::MAX_LINE_TYPE; lt++ ) {
		if( ( halt->get_station_type() & simline_t::linetype_to_stationtype[lt] )==0 ) {
			continue;
		}

		bool found = false;
		for( sint8 i=0; i<MAX_PLAYER_COUNT; i++ ) {
			//cbuffer_t count_buf;
			uint line_count = 0;
			if (!halt->registered_lines.empty()) {
				for (uint32 line_idx = 0; line_idx < halt->registered_lines.get_count(); line_idx++) {
					if (i == halt->registered_lines[line_idx]->get_owner()->get_player_nr()
						&& lt == halt->registered_lines[line_idx]->get_linetype())
					{
						line_count++;
						if (!found) {
							found = true;
							add_table(MAX_PLAYER_COUNT*2+2,1)->set_spacing(scr_size(2,0)); // waytypespce(1) + line"s" + convoy"s" + end_margin
							new_component<gui_image_t>(skinverwaltung_t::get_waytype_skin(halt->registered_lines[line_idx]->get_schedule()->get_waytype())->get_image_id(0), 0, ALIGN_CENTER_V, true);
						}
					}
				}
				if (line_count) {
					cbuffer_t buf;
					buf.append(line_count, 0);
					new_component<gui_color_label_t>(buf, color_idx_to_rgb(COL_WHITE),  color_idx_to_rgb(world()->get_player(i)->get_player_color1()+3),1);
				}
			}
			uint lineless_convoy_cnt = 0;
			if (!halt->registered_convoys.empty()) {
				for (uint32 c = 0; c < halt->registered_convoys.get_count(); c++) {
					if (i == halt->registered_convoys[c]->get_owner()->get_player_nr()
						&& lt == halt->registered_convoys[c]->get_schedule()->get_type())
					{
						lineless_convoy_cnt++;
						if (!found) {
							found = true;
							add_table(MAX_PLAYER_COUNT*2+1,1); // waytypespce(1) + line"s"-1 + convoy"s" + end_margin
							new_component<gui_image_t>(skinverwaltung_t::get_waytype_skin(halt->registered_convoys[c]->get_schedule()->get_waytype())->get_image_id(0), 0, ALIGN_CENTER_V, true);
						}
					}
				}
				if (lineless_convoy_cnt) {
					cbuffer_t buf;
					buf.append(lineless_convoy_cnt, 0);
					new_component<gui_color_label_t>(buf, color_idx_to_rgb(world()->get_player(i)->get_player_color1()+1));
				}
			}
		}

		if (found) {
			new_component<gui_margin_t>(D_H_SPACE);
			end_table();
		}
	}
	set_size(get_min_size());
}

void gui_halthandled_lines_t::draw(scr_coord offset)
{
	if( !halt.is_bound() ) return;

	if (halt->registered_convoys.get_count()+halt->registered_lines.get_count() != old_connex) {
		update_table();
	}

	gui_aligned_container_t::draw(offset);
}
