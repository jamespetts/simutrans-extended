/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "times_history_container.h"

#include "../simhalt.h"
#include "../simline.h"
#include "../dataobj/schedule.h"
#include "../display/viewport.h"

#include "components/gui_schedule_item.h"
#include "../player/simplay.h"
#include "components/gui_image.h"
#include "components/gui_divider.h"
#include "components/gui_table.h"

#include "../vehicle/vehicle.h"

#include "../dataobj/environment.h"

#define L_TIME_6_DIGITS_WIDTH (proportional_string_width("88:88:88")+6) // May be shared with another gui

gui_times_history_t::gui_times_history_t(linehandle_t line_, convoihandle_t convoi_, bool line_reversed_display)
{
	line = line_;
	convoy = convoi_;
	reversed = line_reversed_display;

	set_table_layout(1,0);
	set_spacing(scr_size(1,0));
	init();
}


void gui_times_history_t::init()
{
	if (line.is_bound()) {
		schedule = line->get_schedule();
		map = &line->get_journey_times_history();
		player = line->get_owner();
		mirrored = schedule->is_mirrored();
		build_table();
	}
	else if (convoy.is_bound()) {
		schedule = convoy->get_schedule();
		map = &convoy->get_journey_times_history();
		player = convoy->get_owner();
		mirrored = schedule->is_mirrored();
		build_table();
	}
	else {
		remove_all();
	}
}

void gui_times_history_t::construct_data(slist_tpl<uint8> *schedule_indices, slist_tpl<departure_point_t *> *time_keys) {
	const minivec_tpl<schedule_entry_t>& entries = schedule->entries;
	const uint8 size = entries.get_count();

	if (size <= 1) return;

	if (mirrored) {
		sint16 first_halt_index = -1;
		sint16 last_halt_index = -1;

		for (sint16 i = 0; i < size - 1; i++) {
			const halthandle_t halt = haltestelle_t::get_halt(entries[i].pos, player);
			if (halt.is_bound()) {
				if (first_halt_index == -1 || i < first_halt_index) first_halt_index = i;
				if (last_halt_index == -1 || i > last_halt_index) last_halt_index = i;
				schedule_indices->append(i);
				time_keys->append(new departure_point_t(i, false));
			}
		}

		const halthandle_t halt_last = haltestelle_t::get_halt(entries[size - 1].pos, player);
		if (halt_last.is_bound()) {
			last_halt_index = size - 1;
			schedule_indices->append(size - 1);
			time_keys->append(new departure_point_t(size - 1, true));
		}

		for (sint16 i = size - 2; i >= 1; i--) {
			const halthandle_t halt = haltestelle_t::get_halt(entries[i].pos, player);
			if (halt.is_bound()) {
				schedule_indices->append(i);
				time_keys->append(new departure_point_t(i, true));
			}
		}

		const halthandle_t halt_first = haltestelle_t::get_halt(entries[0].pos, player);
		if (halt_first.is_bound()) {
			schedule_indices->append(0);
		}
	}
	else if (reversed) {
		sint16 first_halt_index = -1;
		for (sint16 i = size - 1; i >= 0; i--) {
			const halthandle_t halt = haltestelle_t::get_halt(entries[i].pos, player);
			if (halt.is_bound()) {
				if (first_halt_index == -1 || i > first_halt_index) first_halt_index = i;
				schedule_indices->append(i);
				time_keys->append(new departure_point_t(i, true));
			}
		}
		schedule_indices->append(first_halt_index);
	}
	else {
		sint16 first_halt_index = -1;
		for (sint16 i = 0; i < size; i++) {
			const halthandle_t halt = haltestelle_t::get_halt(entries[i].pos, player);
			if (halt.is_bound()) {
				if (first_halt_index == -1 || i < first_halt_index) first_halt_index = i;
				schedule_indices->append(i);
				time_keys->append(new departure_point_t(i, false));
			}
		}
		schedule_indices->append(first_halt_index);
	}
}

void gui_times_history_t::build_table()
{
	if (convoy.is_bound()) {
		reversed = convoy->get_reverse_schedule();
	}
	remove_all();

	slist_tpl<uint8> *schedule_indices = new slist_tpl<uint8>();
	slist_tpl<departure_point_t *> *time_keys = new slist_tpl<departure_point_t *>();

	construct_data(schedule_indices, time_keys);

	if (!schedule_indices->get_count()) {
		return;
	}

	PIXVAL base_color;
	const uint8 temp_col_index = line.is_bound() ? line->get_line_color_index() : !convoy.is_bound() ? 255 : convoy->get_line().is_bound() ? convoy->get_line()->get_line_color_index() : 255;
	switch (temp_col_index) {
		case 254:
			base_color = color_idx_to_rgb(player->get_player_color1()+4);
			break;
		case 255:
			base_color = color_idx_to_rgb(player->get_player_color1()+2);
			break;
		default:
			base_color = line_color_idx_to_rgb(temp_col_index);
			break;
	}

	if (!is_dark_color(base_color)) {
		base_color = display_blend_colors(base_color, color_idx_to_rgb(COL_BLACK), 15);
	}

	const scr_coord_val heading_pading_x= D_HEADING_HEIGHT*2;
	if (mirrored) {
		new_component<gui_heading_t>("bidirectional_order",
			base_color, base_color, 0)->set_width(proportional_string_width(translator::translate("bidirectional_order"))+heading_pading_x+4);
	}
	else if (reversed) {
		add_table(2,1);
		new_component<gui_heading_t>("reversed_unidirectional_order",
			color_idx_to_rgb(COL_WHITE), base_color, 1)->set_width(proportional_string_width(translator::translate("reversed_unidirectional_order"))+heading_pading_x);
		new_component<gui_image_t>(skinverwaltung_t::reverse_arrows ? skinverwaltung_t::reverse_arrows->get_image_id(1) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
		end_table();
	}
	else {
		new_component<gui_heading_t>("normal_unidirectional_order",
			color_idx_to_rgb(COL_WHITE), base_color, 1)->set_width(proportional_string_width(translator::translate("normal_unidirectional_order"))+heading_pading_x);
	}

	new_component<gui_margin_t>(0, D_V_SPACE);

	const minivec_tpl<schedule_entry_t>& entries = schedule->entries;

	// for convoy location
	sint32 cnv_route_index = 0;
	sint32 cnv_route_index_left = 0;
	PIXVAL convoy_state_col = COL_SAFETY;
	uint16 min_range; // in km
	bool found_location = false;
	if (convoy.is_bound()) {
		cnv_route_index = convoy->front()->get_route_index();
		cnv_route_index_left = convoy->get_route()->get_count() - cnv_route_index;
		old_route_index = cnv_route_index;
		if (convoy->has_obsolete_vehicles()) {
			convoy_state_col = SYSCOL_OBSOLETE;
		}
		min_range = convoy->get_min_range();
	}
	else {
		min_range = line->get_min_range();
	}

	gui_aligned_container_t *tbl = add_table(8 + convoy.is_bound(), 0);
	tbl->set_table_frame(true, true);
	tbl->set_spacing(scr_size(0, 0));
	{
		// header
		new_component<gui_table_header_t>("", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left)->set_flexible(true, false);
		gui_table_header_t *th = new_component<gui_table_header_t>("", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left);
		th->set_flexible(true, false);
		th->set_padding(scr_size(0,2));
		th->set_fixed_width(D_ENTRY_NO_WIDTH);
		if (convoy.is_bound()) {
			th = new_component<gui_table_header_t>("", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left);
			th->set_flexible(true, false);
			th->set_padding(scr_size(2,2));
			th->set_fixed_width(LINESPACE*0.7);
		}
		new_component<gui_table_header_t>("stations", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left)->set_flexible(true, false);
		new_component_span<gui_table_header_t>("latest_3", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left, 3)->set_flexible(true, false);
		new_component_span<gui_table_header_t>("average", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left, 2)->set_flexible(true, false);

		new_component_span<gui_margin_t>(1, D_V_SPACE<<1, 2);

		if (convoy.is_bound()) {
			new_component<gui_empty_t>();
		}
		new_component_span<gui_empty_t>(6);

		for (uint32 i = 0; i < schedule_indices->get_count(); i++) {
			const uint8 entry_index = min(schedule_indices->at(i), entries.get_count()-1);
			const uint8 next_entry_index = i == schedule_indices->get_count()-1 ? 0 : min(schedule_indices->at(i+1), entries.get_count()-1);
			const schedule_entry_t entry = entries[entry_index];
			const halthandle_t halt = haltestelle_t::get_halt(entry.pos, player);

			const bool is_interchange = halt.is_bound() ? (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1 : false;

			// convoy location check
			bool stopped_here = false;
			bool in_this_section = false;
			const uint8 next_stop_index = min(schedule_indices->at(i == schedule_indices->get_count() - 1 ? 0 : i + 1), entries.get_count()-1);
			if (convoy.is_bound() && !found_location) {
				if (cnv_route_index_left > 0) {
					if (mirrored) {
						if ((!reversed && i < (schedule_indices->get_count()/2)) || (reversed && i >= (schedule_indices->get_count()/2))) {
							in_this_section = next_stop_index == schedule->get_current_stop();
						}
					}
					else {
						in_this_section = next_stop_index == schedule->get_current_stop();
					}

					if (cnv_route_index==1 && in_this_section) {
						// still at this halt
						in_this_section = false;
						stopped_here = true;
					}
				}
				else if(!stopped_here) {
					if (mirrored) {
						if ((!reversed && i <= (schedule_indices->get_count()/2)) || (reversed && i > (schedule_indices->get_count()/2))) {
							stopped_here = entry_index == schedule->get_current_stop();
						}
					}
					else {
						stopped_here = (i>0 && entry_index == schedule->get_current_stop());
					}
				}
				found_location = (in_this_section || stopped_here);
			}

			add_table(2,1)->set_spacing(scr_size(0,0));
			{
				add_table(2,1)->set_alignment(ALIGN_LEFT);
				{
					button_t *b = new_component<button_t>();
					b->set_typ(button_t::posbutton_automatic);
					b->set_targetpos(entry.pos.get_2d());
					new_component<gui_margin_t>(max(0,D_TIME_6_DIGITS_WIDTH-D_POS_BUTTON_WIDTH-D_FIXED_SYMBOL_WIDTH*2));
				}
				end_table();
				add_table(2,1)->set_alignment(ALIGN_RIGHT); {
					new_component<gui_image_t>()->set_image(entry.wait_for_time && skinverwaltung_t::waiting_time ? skinverwaltung_t::waiting_time->get_image_id(0) : IMG_EMPTY, true);
					new_component<gui_image_t>()->set_image(entry.minimum_loading>0 ? skinverwaltung_t::goods->get_image_id(0) : IMG_EMPTY, true);
				}
				end_table();
			}
			end_table();

			uint8 halt_col_idx = (!mirrored && reversed) ? player->get_player_color2() : player->get_player_color1();
			new_component<gui_schedule_entry_number_t>(entry_index, halt.is_bound() ? halt->get_owner()->get_player_color1() : halt_col_idx,
				halt.is_bound() ? (is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt)
					: gui_schedule_entry_number_t::number_style::waypoint,
				scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT,D_ENTRY_NO_HEIGHT)), entry.pos);
			// convoy location
			if (convoy.is_bound()) {
				if (stopped_here) {
					new_component<gui_convoy_arrow_t>(convoy_state_col);
				}
				else {
					new_component<gui_empty_t>();
				}
			}

			add_table(2,1); {
				const bool can_serve = halt.is_bound() ? line.is_bound() ? halt->can_serve(line) : halt->can_serve(convoy) : false;
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(can_serve ? SYSCOL_TEXT : COL_INACTIVE);
				if (!can_serve) {
					lb->buf().printf("(%s)", halt.is_bound() ? halt->get_name() : translator::translate("Wegpunkt"));
				}
				else {
					lb->buf().append(halt->get_name());
				}
				lb->set_padding(scr_size(D_BUTTON_PADDINGS_X,0));
				lb->update();
				if (entry.reverse == 1) {
					new_component<gui_label_t>("[<<]", SYSCOL_TEXT_STRONG);
				}
			}
			end_table();

			new_component_span<gui_empty_t>(5);

			// row2
			if (i < schedule_indices->get_count() - 1) {
				const uint32 distance_to_next_stop = shortest_distance(entry.pos.get_2d(), entries[next_entry_index].pos.get_2d()) * world()->get_settings().get_meters_per_tile();
				const bool exceed_range = min_range && (distance_to_next_stop > min_range*1000);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(exceed_range ? COL_DANGER : SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%4.1f%s", (double)(distance_to_next_stop / 1000.0), "km");
				if (exceed_range) {
					lb->set_tooltip(translator::translate("out of range"));
				}
				lb->set_fixed_width(D_TIME_6_DIGITS_WIDTH);
				lb->update();

				uint8 base_line_style = gui_colored_route_bar_t::solid;
				if (mirrored && (i==(schedule_indices->get_count()/2)-1 || i==schedule_indices->get_count()-2)) {
					base_line_style = gui_colored_route_bar_t::downward;
				}
				new_component<gui_colored_route_bar_t>(base_color, base_line_style)->set_alert_level(exceed_range*3);

				if (convoy.is_bound()) {
					if (in_this_section) {
						new_component<gui_convoy_arrow_t>(convoy_state_col);
					}
					else {
						new_component<gui_empty_t>();
					}
				}

				new_component<gui_empty_t>();

				// times history
				times_history_data_t history;
				const times_history_data_t *retrieved_value = map->access(*time_keys->at(i));
				if (retrieved_value) { history = *retrieved_value; }

				for (uint8 j = 0; j < TIMES_HISTORY_SIZE; j++) {
					const uint32 time = history.get_entry(j);
					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					if (time != 0) {
						char time_as_clock[32];
						world()->sprintf_time_tenths(time_as_clock, 32, time);
						lb->buf().append(time_as_clock);
					}
					else {
						lb->buf().append("--:--");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					lb->set_fixed_width(D_TIME_6_DIGITS_WIDTH);
					lb->update();
				}
				uint32 time = history.get_average_seconds();
				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				if (time != 0) {
					char average_time_str[32];
					world()->sprintf_time_secs(average_time_str, 32, time, env_t::show_month == env_t::DATE_FMT_64_SECOND_MINUTE || env_t::show_month == env_t::DATE_FMT_64_SECOND_MINUTE_PRETTY);
					lb->buf().printf("%s", average_time_str);
				}
				else {
					lb->buf().append(translator::translate("no_time_entry"));
					lb->set_color(SYSCOL_TEXT_INACTIVE);
				}
				lb->set_fixed_width(D_TIME_6_DIGITS_WIDTH);
				lb->update();

				// average speed
				if (time != 0) {
				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT_WEAK, gui_label_t::centered);
					sint64 average_speed = 3600 * distance_to_next_stop / time / 100; // x10
					lb->buf().printf("(%.1fkm/h)", (float)average_speed / 10.0);
					lb->set_color(SYSCOL_TEXT_INACTIVE);
					lb->update();
				}
				else {
					new_component<gui_empty_t>();
				}
			}
			else {
				new_component<gui_empty_t>();
				new_component<gui_colored_route_bar_t>(base_color, gui_colored_route_bar_t::line_style::dashed);
				if (convoy.is_bound()) {
					new_component<gui_empty_t>();
				}
				new_component_span<gui_empty_t>(6);
			}
		}

		if (convoy.is_bound()) {
			new_component_span<gui_empty_t>(9);

			new_component_span<gui_empty_t>(4);
			new_component_span<gui_label_t>(translator::translate("Avg trip time"), SYSCOL_TEXT, gui_label_t::right, 3);

			sint64 average_round_trip_time = convoy->get_average_round_trip_time();
			gui_label_buf_t *avg_triptime_label = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
			if (average_round_trip_time) {
				char as_clock[32];
				world()->sprintf_ticks(as_clock, sizeof(as_clock), average_round_trip_time);
				avg_triptime_label->buf().printf(": %s", as_clock);
			}
			avg_triptime_label->update();
			new_component<gui_empty_t>();

			new_component<gui_margin_t>(1, D_V_SPACE<<1);
			new_component_span<gui_empty_t>(8);
		}
	}
	end_table();
	new_component<gui_margin_t>(0, D_V_SPACE);


	if (line.is_bound()) {
		update_time = world()->get_ticks();
	}
	delete time_keys;
}

void gui_times_history_t::draw(scr_coord offset)
{
	if (schedule==NULL) {
		init();
	}
	else if (convoy.is_bound()){
		if (!convoy->get_schedule()->empty() && !convoy->get_schedule()->matches(world(), schedule)) {
			init();
		}
		else {
			sint32 cnv_route_index = convoy->front()->get_route_index();
			const sint32 cnv_route_index_left = convoy->get_route()->get_count() - convoy->front()->get_route_index();
			if (cnv_route_index < old_route_index || (cnv_route_index_left==0 && old_route_index != cnv_route_index) || cnv_route_index == 2) {
				old_route_index = cnv_route_index;
				old_current_stop = schedule->get_current_stop();
				build_table();
			}
		}
	}
	else if(line.is_bound()) {
		if (!line->get_schedule()->empty() && !line->get_schedule()->matches(world(), schedule)) {
			init();
		}
		else if(world()->get_ticks() - update_time > 10000) {
			build_table();
		}
	}

	set_size(get_size());
	gui_aligned_container_t::draw(offset);
}
