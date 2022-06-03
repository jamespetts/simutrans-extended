/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "halt_list_stats.h"
#include "../simhalt.h"
#include "../simskin.h"
#include "../simcolor.h"
#include "../simcity.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../display/simimg.h"

#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../descriptor/skin_desc.h"

#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"

#include "gui_frame.h"
#include "halt_info.h" // gui_halt_type_images_t
#include "components/gui_halthandled_lines.h"

#define HISTBAR_SCALE_FACTOR 10
#define WAITINGBAR_SCALE_FACTOR 4
#define L_KOORD_LABEL_WIDTH proportional_string_width("(8888,8888)")
#define L_HALT_CAPACITY_LABEL_WIDTH proportional_string_width("8,888")

uint16 halt_list_stats_t::name_width = 0;

// helper class
void gui_capped_arrow_t::draw(scr_coord offset)
{
	offset += pos;
	display_fillbox_wh_clip_rgb(offset.x,   offset.y+1, 4, 3, color_idx_to_rgb(COL_WHITE), false);
	display_fillbox_wh_clip_rgb(offset.x+2, offset.y,   1, 5, color_idx_to_rgb(COL_WHITE), false);
	display_fillbox_wh_clip_rgb(offset.x+4, offset.y+2, 1, 1, color_idx_to_rgb(COL_WHITE), false);
}

gui_halt_stats_t::gui_halt_stats_t(halthandle_t h)
{
	halt = h;
	set_table_layout(1,0);
	set_spacing(scr_size(1,1));
	set_margin(scr_size(1,1), scr_size(0,0));

	update_table();
}

void gui_halt_stats_t::update_table()
{
	remove_all();
	switch (display_mode) {
		case halt_list_stats_t::hl_waiting_detail:
			new_component<gui_halt_waiting_summary_t>(halt);
			break;
		case halt_list_stats_t::hl_facility:
			add_table(6,1);
			{
				if (halt->get_pax_enabled()) {
					buf.clear();
					buf.printf("%4u", halt->get_capacity(0));
					new_component<gui_label_with_symbol_t>(buf,skinverwaltung_t::passengers->get_image_id(0), halt->get_status_color(0)==color_idx_to_rgb(COL_OVERCROWD) ? color_idx_to_rgb(COL_OVERCROWD):SYSCOL_TEXT )->set_fixed_width(L_HALT_CAPACITY_LABEL_WIDTH);
				}
				else {
					new_component<gui_margin_t>(L_HALT_CAPACITY_LABEL_WIDTH+14);
				}
				if (halt->get_mail_enabled()) {
					buf.clear();
					buf.printf("%4u", halt->get_capacity(1));
					new_component<gui_label_with_symbol_t>(buf,skinverwaltung_t::mail->get_image_id(0), halt->get_status_color(1)==color_idx_to_rgb(COL_OVERCROWD) ? color_idx_to_rgb(COL_OVERCROWD):SYSCOL_TEXT)->set_fixed_width(L_HALT_CAPACITY_LABEL_WIDTH);
				}
				else {
					new_component<gui_margin_t>(L_HALT_CAPACITY_LABEL_WIDTH+14);
				}
				if (halt->get_ware_enabled()) {
					buf.clear();
					buf.printf("%4u", halt->get_capacity(2));
					new_component<gui_label_with_symbol_t>(buf,skinverwaltung_t::goods->get_image_id(0), halt->get_status_color(2)==color_idx_to_rgb(COL_OVERCROWD) ? color_idx_to_rgb(COL_OVERCROWD):SYSCOL_TEXT)->set_fixed_width(L_HALT_CAPACITY_LABEL_WIDTH);
				}
				else {
					new_component<gui_margin_t>(L_HALT_CAPACITY_LABEL_WIDTH+14);
				}

				new_component<gui_halt_type_images_t>(halt);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%2u%s, ", halt->get_tiles().get_count(),
					halt->get_tiles().get_count() > 1 ? translator::translate("tiles") : translator::translate("tile"));
				lb->set_fixed_width(proportional_string_width("188") + proportional_string_width(translator::translate("tiles,")));
				lb->update();
				new_component<gui_label_buf_t>()->buf().printf("%.2f$/%s", (double)world()->calc_adjusted_monthly_figure(halt->calc_maintenance()) / 100.0, translator::translate("month"));
			}
			end_table();
			break;
		case halt_list_stats_t::hl_serve_lines:
		{
			add_table(2, 0);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->init(SYSCOL_TEXT, gui_label_t::right);
			lb->buf().append(halt->get_finance_history(1, HALT_CONVOIS_ARRIVED), 0);
			lb->buf().append(" - ");
			lb->set_tooltip(translator::translate("The number of times convoy arrived at this stop last month"));
			lb->set_fixed_width(D_BUTTON_WIDTH/2);
			lb->update();
			new_component<gui_halthandled_lines_t>(halt);
			end_table();
			break;
		}
		case halt_list_stats_t::hl_waiting_pax:   // freight_type==0
		case halt_list_stats_t::hl_waiting_mail:  // freight_type==1
		case halt_list_stats_t::hl_waiting_goods: // freight_type==2
		{
			const uint8 freight_type = display_mode == halt_list_stats_t::hl_waiting_pax ? 0 : display_mode == halt_list_stats_t::hl_waiting_mail ? 1 : 2;
			if( (freight_type==0 && halt->get_pax_enabled()) || (freight_type==1 && halt->get_mail_enabled()) || (freight_type==2 && halt->get_ware_enabled())) {
				add_table(4,1);
				const uint32 capacity    = halt->get_capacity(freight_type);
				const PIXVAL colval      = freight_type==0 ? goods_manager_t::passengers->get_color() : freight_type==1 ? goods_manager_t::mail->get_color() : color_idx_to_rgb(116);
				const uint32 waiting_sum = freight_type==0 ? halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) :
				                           freight_type==1 ? halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL)) :
				                        /* freight_type==2*/ halt->get_finance_history(0, HALT_WAITING) - halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) - halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL));

				num[0] = (sint32)min(waiting_sum, capacity);
				num[1] = (sint64)waiting_sum - num[0];

				bandgraph.clear();
				bandgraph.add_color_value(&num[0], colval, true);
				bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_OVERCROWD), true);
				add_component(&bandgraph);
				bandgraph.set_size(scr_size(min(256, (waiting_sum + (WAITINGBAR_SCALE_FACTOR-1)) / WAITINGBAR_SCALE_FACTOR), D_LABEL_HEIGHT*2/3));

				// show arrow for capped values
				new_component<gui_capped_arrow_t>()->set_visible(waiting_sum > 1024);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().append(waiting_sum, 0);
				lb->init(waiting_sum > capacity ? color_idx_to_rgb(COL_OVERCROWD) : SYSCOL_TEXT, gui_label_t::left);
				lb->update();

				end_table();
			}
			else {
				new_component<gui_label_t>("-", COL_INACTIVE);
			}
			break;
		}
		case halt_list_stats_t::hl_pax_volume:
		case halt_list_stats_t::hl_mail_volume:
		{
			// last month record
			const uint8 freight_type = display_mode == halt_list_stats_t::hl_pax_volume ? 0 : 1;
			if ((freight_type == 0 && halt->get_pax_enabled()) || (freight_type == 1 && halt->get_mail_enabled()) ) {
				add_table(2,1);
				const PIXVAL colval = freight_type ? goods_manager_t::mail->get_color() : goods_manager_t::passengers->get_color();

				num[0] = freight_type ? halt->get_finance_history(1, HALT_MAIL_HANDLING_VOLUME) : halt->get_finance_history(1, HALT_VISITORS);
				num[1] = freight_type ? 0 : halt->get_finance_history(1, HALT_COMMUTERS);
				const sint64 sum = num[0]+num[1];

				bandgraph.clear();
				bandgraph.add_color_value(&num[0], colval);
				bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_COMMUTER));
				add_component(&bandgraph);
				bandgraph.set_size(scr_size(min(256, (sum + (HISTBAR_SCALE_FACTOR-1))/HISTBAR_SCALE_FACTOR), D_LABEL_HEIGHT*2/3));

				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().append(sum, 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
				lb->update();

				end_table();
			}
			else {
				new_component<gui_label_t>("-", COL_INACTIVE);
			}
			break;
		}
		case halt_list_stats_t::hl_location:
		{
			add_table(4,1);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->set_fixed_width(L_KOORD_LABEL_WIDTH);
			lb->buf().append(halt->get_basis_pos().get_fullstr());
			lb->set_align(gui_label_t::centered);
			lb->update();

			new_component<gui_label_t>("- ");

			stadt_t* c = world()->get_city(halt->get_basis_pos());
			if (c) {
				new_component<gui_image_t>()->set_image(skinverwaltung_t::intown->get_image_id(0), true);
			}
			else {
				new_component<gui_empty_t>();
			}
			lb = new_component<gui_label_buf_t>();
			if (c) {
				lb->buf().append(c->get_name());
			}
			if (!world()->get_settings().regions.empty()) {
				lb->buf().printf(" (%s)", translator::translate(world()->get_region_name(halt->get_basis_pos()).c_str()));
			}
			lb->update();
			end_table();
			break;
		}
		case halt_list_stats_t::hl_pax_evaluation:
		{
			add_table(3,1);
			num[0] = halt->get_finance_history(1, HALT_HAPPY);
			num[1] = halt->get_finance_history(1, HALT_UNHAPPY);
			num[2] = halt->get_finance_history(1, HALT_TOO_WAITING);
			num[3] = halt->get_finance_history(1, HALT_TOO_SLOW);
			num[4] = halt->get_finance_history(1, HALT_NOROUTE);

			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			if (halt->get_potential_passenger_number(1)) {
				lb->buf().append(num[0], 0);
				lb->init(color_idx_to_rgb(COL_HAPPY), gui_label_t::right);
			}
			else {
				lb->buf().append("-");
				lb->init(COL_INACTIVE, gui_label_t::right);
			}
			lb->set_fixed_width(D_BUTTON_WIDTH/2);
			lb->init(color_idx_to_rgb(COL_HAPPY), gui_label_t::right);
			lb->update();

			bandgraph.clear();
			bandgraph.add_color_value(&num[0], color_idx_to_rgb(COL_HAPPY));
			bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_OVERCROWD));
			bandgraph.add_color_value(&num[2], color_idx_to_rgb(COL_TOO_WAITNG));
			bandgraph.add_color_value(&num[3], color_idx_to_rgb(COL_TOO_SLOW));
			bandgraph.add_color_value(&num[4], color_idx_to_rgb(COL_NO_ROUTE));
			add_component(&bandgraph);
			bandgraph.set_size(scr_size(min(256, (halt->get_potential_passenger_number(1) + (HISTBAR_SCALE_FACTOR-1)) / HISTBAR_SCALE_FACTOR), D_LABEL_HEIGHT*2/3));

			lb = new_component<gui_label_buf_t>();
			if (halt->get_pax_enabled()) {
				lb->buf().append(halt->get_potential_passenger_number(1), 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
			}
			lb->update();

			end_table();
			break;
		}
		case halt_list_stats_t::hl_mail_evaluation:
		{
			add_table(3,1);
			num[0] = halt->get_finance_history(1, HALT_MAIL_DELIVERED);
			num[1] = halt->get_finance_history(1, HALT_MAIL_NOROUTE);
			const sint64 potential_mail_users = num[0] + num[1];

			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			if (potential_mail_users) {
				lb->buf().append(num[0], 0);
				lb->init(color_idx_to_rgb(COL_MAIL_DELIVERED), gui_label_t::right);
			}
			else {
				lb->buf().append("-");
				lb->init(COL_INACTIVE, gui_label_t::right);
			}
			lb->set_fixed_width(D_BUTTON_WIDTH/2);
			lb->init(color_idx_to_rgb(COL_HAPPY), gui_label_t::right);
			lb->update();

			bandgraph.clear();
			bandgraph.add_color_value(&num[0], color_idx_to_rgb(COL_MAIL_DELIVERED));
			bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_MAIL_NOROUTE));
			add_component(&bandgraph);
			bandgraph.set_size(scr_size(min(256, (potential_mail_users + (HISTBAR_SCALE_FACTOR-1)) / HISTBAR_SCALE_FACTOR), D_LABEL_HEIGHT*2/3));

			lb = new_component<gui_label_buf_t>();
			if (halt->get_mail_enabled()) {
				lb->buf().append(potential_mail_users, 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
			}
			lb->update();

			end_table();
			break;
		}
		case halt_list_stats_t::hl_goods_needed:
		case halt_list_stats_t::hl_products:
			if (halt->get_ware_enabled()) {
				add_table(2,1);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().append(halt->get_finance_history(1, HALT_GOODS_HANDLING_VOLUME)/100.0, 1);
				lb->buf().append(translator::translate("tonnen"));
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->set_fixed_width((D_BUTTON_WIDTH * 3) >> 2);
				lb->update();

				new_component<gui_halt_goods_demand_t>(halt, display_mode==halt_list_stats_t::hl_goods_needed ? false:true);
				end_table();
			}
			break;
		case halt_list_stats_t::coverage_visitor_demands:
			if (halt->get_pax_enabled()) {
				add_table(3,1);
				const uint8 bar_scale = HISTBAR_SCALE_FACTOR * world()->get_settings().get_population_per_level()*3;
				new_component<gui_colorbox_t>()->init(goods_manager_t::passengers->get_color(), scr_size(min(256, (halt->get_around_visitor_demand() + (bar_scale-1)) / bar_scale), D_LABEL_HEIGHT * 2 / 3), true, false);
				new_component<gui_capped_arrow_t>()->set_visible(halt->get_around_visitor_demand() / bar_scale > 256);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().append(halt->get_around_visitor_demand(), 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
				lb->update();

				end_table();
			}
			break;
		case halt_list_stats_t::coverage_job_demands:
		{
			if (halt->get_pax_enabled()) {
				add_table(4,1);
				const uint32 job_demand = halt->get_around_job_demand();
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().printf("%.1f%%", job_demand ? 100.0 * halt->get_around_employee_factor() / job_demand : 0.0);
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->set_tooltip(translator::translate("Staffing"));
				lb->update();
				const uint8 bar_scale = HISTBAR_SCALE_FACTOR * world()->get_settings().get_jobs_per_level()*3;
				new_component<gui_colorbox_t>()->init(color_idx_to_rgb(COL_COMMUTER), scr_size(min(256, (job_demand + (bar_scale-1)) / bar_scale), D_LABEL_HEIGHT * 2 / 3), true, false);
				new_component<gui_capped_arrow_t>()->set_visible(job_demand/bar_scale > 256);
				lb = new_component<gui_label_buf_t>();
				lb->buf().append(job_demand, 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
				lb->update();

				end_table();
			}
			break;
		}
		case halt_list_stats_t::coverage_output_pax:
		{
			if (halt->get_pax_enabled()) {
				add_table(7,1);
				new_component<gui_colorbox_t>(goods_manager_t::passengers->get_color())->set_size(scr_size(LINESPACE/2+2, LINESPACE/2+2));
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				const uint32 visitor_generated = halt->get_around_visitor_generated();
				lb->buf().printf("%.1f%%", visitor_generated ? 100.0*halt->get_around_succeeded_visiting() / visitor_generated : 0.0);
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->set_tooltip(translator::translate("visiting_trip_success_rete"));
				lb->update();

				new_component<gui_colorbox_t>(color_idx_to_rgb(COL_COMMUTER))->set_size(scr_size(LINESPACE/2+2, LINESPACE/2+2));
				lb = new_component<gui_label_buf_t>();
				const uint32 commuter_generated = halt->get_around_commuter_generated();
				lb->buf().printf("%.1f%%", commuter_generated ? 100.0*halt->get_around_succeeded_commuting() / commuter_generated : 0.0);
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->set_tooltip(translator::translate("commuting_trip_success_rete"));
				lb->update();

				const uint8 bar_scale = HISTBAR_SCALE_FACTOR * world()->get_settings().get_population_per_level()*3;
				new_component<gui_colorbox_t>()->init(color_idx_to_rgb(COL_DARK_GREEN+1), scr_size(min(256, (halt->get_around_population() + (bar_scale-1)) / bar_scale), D_LABEL_HEIGHT * 2 / 3), true, false);
				new_component<gui_capped_arrow_t>()->set_visible(halt->get_around_population()/bar_scale>256);
				lb = new_component<gui_label_buf_t>();
				lb->buf().append(halt->get_around_population(), 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
				lb->update();
				end_table();
			}
			break;
		}
		case halt_list_stats_t::coverage_output_mail:
		{
			if (halt->get_mail_enabled()) {
				add_table(4,1);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				const uint32 mail_generated = halt->get_around_mail_generated();
				lb->buf().printf("%.1f%%", mail_generated ? 100.0*halt->get_around_mail_delivery_succeeded()/mail_generated : 0.0);
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->set_tooltip(translator::translate("ratio_mail"));
				lb->update();

				const uint8 bar_scale = HISTBAR_SCALE_FACTOR * world()->get_settings().get_mail_per_level()*6;
				new_component<gui_colorbox_t>()->init(goods_manager_t::mail->get_color(), scr_size(min(256, (halt->get_around_mail_demand() + (bar_scale-1)) / bar_scale), D_LABEL_HEIGHT*2/3), true, false);
				new_component<gui_capped_arrow_t>()->set_visible(halt->get_around_mail_demand()/bar_scale>256);
				lb = new_component<gui_label_buf_t>();
				lb->buf().append(halt->get_around_mail_demand(), 0);
				lb->init(SYSCOL_TEXT, gui_label_t::left);
				lb->update();
				end_table();
			}
			break;
		}
		default:
#ifdef DEBUG
			new_component<gui_label_t>("(debug)default", COL_DANGER);
#endif
			break;
	}

	old_display_mode = display_mode;

	set_size(get_size());
}

void gui_halt_stats_t::draw(scr_coord offset)
{
	bool update_flag = false;

	if (old_display_mode != display_mode) {
		update_flag = true;
	}
	else {
		switch (display_mode) {
			case halt_list_stats_t::hl_waiting_pax:
			case halt_list_stats_t::hl_waiting_mail:
			case halt_list_stats_t::hl_waiting_goods:
				if (update_seed != halt->get_finance_history(0, HALT_WAITING)) {
					update_flag = true;
					update_seed = halt->get_finance_history(0, HALT_WAITING);
				}
				break;
			case halt_list_stats_t::hl_facility:
			{
				const sint32 check = (sint32)(halt->get_capacity(0) + halt->get_capacity(1) + halt->get_capacity(2) + halt->get_tiles().get_count());
				if (update_seed != check) {
					update_flag = true;
					update_seed = check;
				}
				break;
			}
			case halt_list_stats_t::hl_serve_lines:
			case halt_list_stats_t::hl_pax_evaluation:
			case halt_list_stats_t::hl_mail_evaluation:
			case halt_list_stats_t::hl_pax_volume:
			case halt_list_stats_t::hl_mail_volume:
			case halt_list_stats_t::hl_goods_needed:
			case halt_list_stats_t::hl_products:
				// update once at the beginning of the month
				if (update_seed != (sint32)world()->get_current_month()) {
					update_flag = true;
					update_seed = (sint32)world()->get_current_month();
				}
				break;
			// These have a high processing load, so please do not update them frequently.
			case halt_list_stats_t::coverage_output_pax:
			case halt_list_stats_t::coverage_output_mail:
			case halt_list_stats_t::coverage_visitor_demands:
			case halt_list_stats_t::coverage_job_demands:
				update_flag = false; // Only when reselect or reopen.
				break;
			case halt_list_stats_t::hl_waiting_detail:
			case halt_list_stats_t::hl_location:
			default:
				// No automatic updates or components do their own updates.
				update_flag = false;
				break;
		}
	}

	if (update_flag) {
		update_seed = 0;
		update_table();
	}

	gui_aligned_container_t::draw(offset);
}


// main class
static karte_ptr_t welt;
/**
 * Events werden hiermit an die GUI-components
 * gemeldet
 */
bool halt_list_stats_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);
	if(  !swallowed  &&  halt.is_bound()  ) {

		if(IS_LEFTRELEASE(ev)) {
			if( IS_SHIFT_PRESSED(ev) ) {
				halt->show_detail();
				return true;
			}
			if(  event_get_last_control_shift() != 2  ) {
				halt->show_info();
				return true;
			}
		}
		if(  IS_RIGHTRELEASE(ev)  ) {
			welt->get_viewport()->change_world_position(halt->get_basis_pos3d());
			return true;
		}
	}
	return swallowed;
}


halt_list_stats_t::halt_list_stats_t(halthandle_t h, uint8 player_nr_)
{
	halt = h;
	player_nr = player_nr_;
	set_table_layout(2,3);
	set_spacing(scr_size(D_H_SPACE, 0));

	gui_aligned_container_t *table = add_table(3,2);
	{
		table->set_spacing(scr_size(1,1));
		add_component(&img_enabled[0]);
		img_enabled[0].set_image(skinverwaltung_t::passengers->get_image_id(0), true);
		add_component(&img_enabled[1]);
		img_enabled[1].set_image(skinverwaltung_t::mail->get_image_id(0), true);
		add_component(&img_enabled[2]);
		img_enabled[2].set_image(skinverwaltung_t::goods->get_image_id(0), true);

		indicator[0].init(COL_INACTIVE, scr_size(10,D_INDICATOR_HEIGHT-1),true,false);
		indicator[1].init(COL_INACTIVE, scr_size(10,D_INDICATOR_HEIGHT-1),true,false);
		indicator[2].init(COL_INACTIVE, scr_size(10,D_INDICATOR_HEIGHT-1),true,false);
		add_component(&indicator[0]);
		add_component(&indicator[1]);
		add_component(&indicator[2]);

		indicator[0].set_rigid(true);
		indicator[1].set_rigid(true);
		indicator[2].set_rigid(true);
		img_enabled[0].set_rigid(true);
		img_enabled[1].set_rigid(true);
		img_enabled[2].set_rigid(true);
	}
	end_table();

	add_table(2,1);
	{
		const scr_coord_val temp_w = proportional_string_width(halt->get_name());
		if (temp_w > name_width) {
			name_width = temp_w;
		}
		label_name.set_fixed_width(name_width);
		add_component(&label_name);
		label_name.buf().append(halt->get_name());
		if(  player_nr==(uint8)-1  ||  ( player_nr!=(uint8)-1 && player_nr!=halt->get_owner()->get_player_nr() ) ) {
			label_name.set_color(color_idx_to_rgb(halt->get_owner()->get_player_color1()+env_t::gui_player_color_dark));
		}
		label_name.update();

		swichable_info = new_component<gui_halt_stats_t>(halt);
	}
	end_table();

	new_component<gui_margin_t>(0,D_V_SPACE);
}


const char* halt_list_stats_t::get_text() const
{
	return halt->get_name();
}

/**
 * Draw the component
 */
void halt_list_stats_t::draw(scr_coord offset)
{
	if (halt.is_bound()) {
		for (uint8 i=0; i<3; i++) {
			indicator[i].set_color(halt->get_status_color(i));
			indicator[i].set_visible(halt->get_status_color(i)!=COL_INACTIVE ? i==0 ? halt->get_pax_enabled() : i==1 ? halt->get_mail_enabled() : halt->get_ware_enabled() : false);
			img_enabled[i].set_visible(i==0 ? halt->get_pax_enabled() : i==1 ? halt->get_mail_enabled() : halt->get_ware_enabled());
			if (img_enabled[i].is_visible()) {
				if (halt->get_status_color(i) == COL_INACTIVE) {
					img_enabled[i].set_transparent((TRANSPARENT25_FLAG | OUTLINE_FLAG | SYSCOL_TEXT));
				}
				else if(halt->get_status_color(i) == color_idx_to_rgb(COL_OVERCROWD)) {
					img_enabled[i].set_transparent( (TRANSPARENT75_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_OVERCROWD)) );
				}
				else {
					img_enabled[i].set_transparent(0);
				}
			}
		}

		label_name.buf().append(halt->get_name());
		if (halt->get_owner() != welt->get_active_player()) {
			label_name.set_color(color_idx_to_rgb(halt->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
		}
		label_name.update();

		set_size(get_size());
		gui_aligned_container_t::draw(offset);
	}
}

