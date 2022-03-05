/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "linelist_stats_t.h"

#include "../simhalt.h"

#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../player/simplay.h"
#include "../utils/cbuffer_t.h"

#include "components/gui_colorbox.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_line_lettercode.h"

gui_line_handle_catg_img_t::gui_line_handle_catg_img_t(linehandle_t line)
{
	this->line = line;
}

void gui_line_handle_catg_img_t::draw(scr_coord offset)
{
	if( line.is_null() ) {
		return;
	}
	scr_coord_val offset_x = 2;
	size.w = line->get_goods_catg_index().get_count() * D_FIXED_SYMBOL_WIDTH+4;
	offset += pos;
	FOR(minivec_tpl<uint8>, const catg_index, line->get_goods_catg_index()) {
		uint8 temp = catg_index;
		display_color_img(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), offset.x + offset_x + 2, offset.y + 3, 0, false, true);
		offset_x += D_FIXED_SYMBOL_WIDTH+2;
	}
}

scr_size gui_line_handle_catg_img_t::get_min_size() const
{
	return scr_size(size.w, D_FIXED_SYMBOL_WIDTH + 2);
}

scr_size gui_line_handle_catg_img_t::get_max_size() const
{
	return scr_size(scr_size::inf.w, D_FIXED_SYMBOL_WIDTH + 2);
}


gui_line_label_t::gui_line_label_t(linehandle_t l)
{
	line = l;
	set_table_layout(2,1);
	set_spacing(scr_size(D_H_SPACE,0));
	set_margin( scr_size(0,0), scr_size(0,0) );

	update_check();
}

void gui_line_label_t::update_check()
{
	bool need_update = false;

	// check 1 - numeric thing
	const uint16 seed = line->get_line_lettercode_style() + line->get_line_color_index() + line->get_owner()->get_player_color1();
	if (old_seed != seed) {
		old_seed = seed;
		need_update = true;
	}

	// check 2 - texts
	cbuffer_t buf;
	buf.printf("%s%s%s", line->get_linecode_l(), line->get_linecode_r(), line->get_name());
	if ( strcmp(buf.get_str(), name_buf.get_str())!=0 ) {
		name_buf.clear();
		name_buf.append(buf.get_str());
		need_update = true;
	}

	if (need_update) {
		update();
	}
}

void gui_line_label_t::update()
{
	remove_all();
	name_buf.clear();
	if( line.is_bound() ) {
		if (line->get_line_color_index() == 255) {
			new_component<gui_margin_t>(0);
		}
		else {
			new_component<gui_line_lettercode_t>(line->get_line_color())->set_line(line);
		}
		gui_label_buf_t *lb = new_component<gui_label_buf_t>(PLAYER_FLAG | color_idx_to_rgb(line->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
		lb->buf().append(line->get_name());
		lb->update();
	}
	set_size(get_min_size());
}

void gui_line_label_t::draw(scr_coord offset)
{
	if( line.is_null() ) {
		return;
	}
	update_check();
	gui_aligned_container_t::draw(offset);
}


gui_line_stats_t::gui_line_stats_t(linehandle_t l, uint8 m)
{
	line = l;
	mode = m;
	set_table_layout(1,1);
	set_alignment(ALIGN_LEFT | ALIGN_CENTER_V);
	set_margin( scr_size(1,1), scr_size(0,0) );
	set_spacing(scr_size(1,1));

	init_table();
}

void gui_line_stats_t::init_table()
{
	remove_all();
	switch (mode)
	{
		case ll_handle_goods:
			new_component<gui_line_handle_catg_img_t>(line);
			break;
		case ll_route:
			add_table(4,1);
			{
				const halthandle_t origin_halt = line->get_schedule()->get_origin_halt(line->get_owner());
				const halthandle_t destination_halt = line->get_schedule()->get_destination_halt(line->get_owner());
				/*
				if ( origin_halt.is_bound() ) {
					const bool is_interchange = (origin_halt.get_rep()->registered_lines.get_count() + origin_halt.get_rep()->registered_convoys.get_count()) > 1;
					new_component<gui_schedule_entry_number_t>(-1, origin_halt.get_rep()->get_owner()->get_player_color1(),
						is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
						scr_size(LINESPACE+4, LINESPACE),
						origin_halt.get_rep()->get_basis_pos3d()
						);
				}
				*/
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
				if (origin_halt.is_bound()) {
					lb->buf().printf("%s", origin_halt->get_name());
				}
				lb->update();

				const PIXVAL line_color = line->get_line_color_index() > 253 ? color_idx_to_rgb(line->get_owner()->get_player_color1()+3) : line->get_line_color();
				new_component<gui_vehicle_bar_t>(line_color, scr_size(LINESPACE*2, (LINEASCENT>>1)+2))->set_flags(
					line->get_schedule()->is_mirrored() ? vehicle_desc_t::can_be_head | vehicle_desc_t::can_be_tail : 0, vehicle_desc_t::can_be_head | vehicle_desc_t::can_be_tail, 3);

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
				if (destination_halt.is_bound() && origin_halt!=destination_halt) {
					lb->buf().printf("%s", destination_halt->get_name());
				}
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
				line->get_travel_distance();
				lb->buf().printf(" %.1fkm ", (float)(line->get_travel_distance()*world()->get_settings().get_meters_per_tile() / 1000.0));
				lb->update();
			}
			end_table();
			break;
		case ll_frequency:
		default:
			add_table(4,1);
			{
				image_id schedule_symbol = skinverwaltung_t::service_frequency ? skinverwaltung_t::service_frequency->get_image_id(0):IMG_EMPTY;
				PIXVAL time_txtcol=SYSCOL_TEXT;
				if (line->get_state() & simline_t::line_has_stuck_convoy) {
					// has stucked convoy
					time_txtcol = color_idx_to_rgb(COL_DANGER);
					if (skinverwaltung_t::pax_evaluation_icons) schedule_symbol = skinverwaltung_t::pax_evaluation_icons->get_image_id(4);
				}
				else if (line->get_state() & simline_t::line_missing_scheduled_slots) {
					time_txtcol = color_idx_to_rgb(COL_DARK_TURQUOISE); // FIXME for dark theme
					if(skinverwaltung_t::missing_scheduled_slot) schedule_symbol = skinverwaltung_t::missing_scheduled_slot->get_image_id(0);
				}
				new_component<gui_image_t>(schedule_symbol, 0, ALIGN_CENTER_V, true);

				const sint64 service_frequency = line->get_service_frequency();
				gui_label_buf_t *lb_frequency = new_component<gui_label_buf_t>(time_txtcol, gui_label_t::right);
				if (service_frequency) {
					char as_clock[32];
					world()->sprintf_ticks(as_clock, sizeof(as_clock), service_frequency);
					lb_frequency->buf().printf(" %s", as_clock);
				}
				else {
					lb_frequency->buf().append("--:--:--");
					lb_frequency->set_color(COL_INACTIVE);
				}
				lb_frequency->update();
				lb_frequency->set_fixed_width( D_TIME_6_DIGITS_WIDTH );

				// convoy count
				gui_label_buf_t *lb_convoy_count = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb_convoy_count->buf().append("");
				if (line->get_convoys().get_count() == 1) {
					lb_convoy_count->buf().append(translator::translate("1 convoi"));
				}
				else if(line->get_convoys().get_count()) {
					lb_convoy_count->buf().printf(translator::translate("%d convois"), line->get_convoys().get_count());
				}
				else {
					lb_convoy_count->buf().append(translator::translate("no convois")); // error
				}
				lb_convoy_count->set_color(line->has_overcrowded() ? color_idx_to_rgb(COL_DARK_PURPLE) : SYSCOL_TEXT);
				lb_convoy_count->update();
				new_component<gui_fill_t>();
			}
			end_table();
			break;
	}
	set_size(get_size());
}

void gui_line_stats_t::update_check()
{
	sint64 seed;
	switch (mode)
	{
		case ll_handle_goods:
			// seed is capacity
			seed = line->get_finance_history(0, LINE_CAPACITY);
			break;
		case ll_route:
			// schedule count or distance or line color
			seed = line->get_schedule()->get_count() + line->get_line_color_index() + line->get_travel_distance();
			break;
		case ll_frequency:
		default:
			// seed is frequency or convoys
			seed = line->get_convoys().get_count()+line->get_service_frequency();
			break;
	}
	if (seed!=old_seed) {
		old_seed = seed;
		init_table();
	}
}

void gui_line_stats_t::draw(scr_coord offset)
{
	if( line.is_null() ) {
		return;
	}
	update_check();
	gui_aligned_container_t::draw(offset);
}

