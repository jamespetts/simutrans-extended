/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simhalt.h"
#include "../simmenu.h"
#include "../simplan.h"
#include "../simworld.h"
#include "../simsignalbox.h"

#include "../dataobj/environment.h"
#include "../descriptor/building_desc.h"
#include "../display/viewport.h"
#include "../obj/gebaeude.h"
#include "../obj/signal.h"
#include "../player/simplay.h"
# include "../utils/simstring.h"

#include "building_info.h"
#include "simwin.h"
#include "components/gui_image.h"
#include "components/gui_colorbox.h"

sint16 building_info_t::tabstate = -1;

gui_building_stats_t::gui_building_stats_t(const gebaeude_t* gb, PIXVAL color)
{
	building = NULL;
	init(gb, color);
}

void gui_building_stats_t::init(const gebaeude_t* gb, PIXVAL color)
{
	if (gb != NULL) {
		building = gb;
		frame_color = color;

		set_table_layout(1, 0);
		set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(0, 0));

		init_class_table();
		init_stats_table();
	}
}

void gui_building_stats_t::init_class_table()
{
	scr_coord_val value_cell_width = max(proportional_string_width(" 888.8%"), 60);
	const bool show_population = building->get_tile()->get_desc()->get_type() == building_desc_t::city_res;
	const bool show_job_info   = (building->get_adjusted_jobs() && !show_population);
	const bool show_visitor_demands = (building->get_adjusted_visitor_demand() && !show_population);
	if (show_population) {
		new_component<gui_heading_t>("residents_wealth", SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
	}
	else if (show_visitor_demands && show_job_info) {
		new_component<gui_heading_t>("wealth_of_visitors_/_commuters", SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
	}
	else if (show_job_info){
		new_component<gui_heading_t>("wealth_of_commuters", SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
	}
	else if (show_visitor_demands) {
		new_component<gui_heading_t>("wealth_of_visitors",  SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
	}
	else {
		return; // no demand
	}

	// passenger class table
	const uint8 cols = 3 + show_population + show_job_info + show_visitor_demands;
	add_table(cols,0);

	for (uint8 c = 0; c < goods_manager_t::passengers->get_number_of_classes(); c++) {
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		p_class_names[c].buf().append(goods_manager_t::get_translated_wealth_name(goods_manager_t::INDEX_PAS, c));
		p_class_names[c].update();
		new_component<gui_label_t>(p_class_names[c]);

		if (show_population) {
			new_component<gui_data_bar_t>()->init(building->get_adjusted_population_by_class(c), building->get_adjusted_population(), value_cell_width, color_idx_to_rgb(COL_DARK_GREEN+1), false, true);
		}
		if (show_visitor_demands) {
			// NOTE: Jobs and residents are indivisible numbers, but demand is not.
			// So get_adjusted_visitor_demand_by_class should not be used here
			// Calculate how much each class is as a percentage of the total amount
			// Remember, each class proportion is *cumulative* with all previous class proportions.
			const uint16 class_proportion = (c == 0) ? building->get_tile()->get_desc()->get_class_proportion(c) : building->get_tile()->get_desc()->get_class_proportion(c)- building->get_tile()->get_desc()->get_class_proportion(c-1);
			new_component<gui_data_bar_t>()->init(class_proportion, building->get_tile()->get_desc()->get_class_proportions_sum(), value_cell_width, goods_manager_t::passengers->get_color(), false, true);
		}
		if (show_job_info) {
			new_component<gui_data_bar_t>()->init(building->get_adjusted_jobs_by_class(c), building->get_adjusted_jobs(), value_cell_width, color_idx_to_rgb(COL_COMMUTER-1), false, true);
		}
		new_component<gui_fill_t>();
	}
	end_table();
	new_component<gui_margin_t>(0, D_V_SPACE);
}

void gui_building_stats_t::init_stats_table()
{
	scr_coord_val value_cell_width = max(proportional_string_width(translator::translate("This Year")), proportional_string_width(translator::translate("Last Year")));

	if (building->get_tile()->get_desc()->get_type() != building_desc_t::city_res || building->get_adjusted_mail_demand()) {
		new_component<gui_heading_t>("Trip data", SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
		add_table(5,0)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE/2));
		{
			// header
			new_component<gui_margin_t>(8);
			new_component<gui_margin_t>(D_BUTTON_WIDTH);
			new_component<gui_label_t>("This Year");
			new_component<gui_label_t>("Last Year");
			new_component<gui_fill_t>();

			if (building->get_tile()->get_desc()->get_type() != building_desc_t::city_res) {
				// Buildings other than houses -> display arrival data
				if (building->get_adjusted_visitor_demand()) {
					new_component<gui_colorbox_t>(goods_manager_t::passengers->get_color())->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
					new_component<gui_label_t>("Visitor arrivals");
					lb_visitor_arrivals[0].set_fixed_width(value_cell_width);
					lb_visitor_arrivals[1].set_fixed_width(value_cell_width);
					lb_visitor_arrivals[0].set_align(gui_label_t::right);
					lb_visitor_arrivals[1].set_align(gui_label_t::right);
					add_component(&lb_visitor_arrivals[0]);
					add_component(&lb_visitor_arrivals[1]);
					new_component<gui_fill_t>();
				}

				new_component<gui_colorbox_t>(color_idx_to_rgb(COL_COMMUTER))->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
				new_component<gui_label_t>("Commuter arrivals");
				lb_commuter_arrivals[0].set_fixed_width(value_cell_width);
				lb_commuter_arrivals[1].set_fixed_width(value_cell_width);
				lb_commuter_arrivals[0].set_align(gui_label_t::right);
				lb_commuter_arrivals[1].set_align(gui_label_t::right);
				add_component(&lb_commuter_arrivals[0]);
				add_component(&lb_commuter_arrivals[1]);
				new_component<gui_fill_t>();
			}
			if (building->get_adjusted_mail_demand()) {
				new_component<gui_colorbox_t>(goods_manager_t::mail->get_color())->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
				new_component<gui_label_t>("hd_mailing");
				lb_mail_sent[0].set_fixed_width(value_cell_width);
				lb_mail_sent[1].set_fixed_width(value_cell_width);
				lb_mail_sent[0].set_align(gui_label_t::right);
				lb_mail_sent[1].set_align(gui_label_t::right);
				add_component(&lb_mail_sent[0]);
				add_component(&lb_mail_sent[1]);
				new_component<gui_fill_t>();
			}
		}
		end_table();
		new_component<gui_margin_t>(0, D_V_SPACE);
	}

	if (building->get_tile()->get_desc()->get_type() == building_desc_t::city_res || building->get_adjusted_mail_demand()) {
		new_component<gui_heading_t>("Success rate", SYSCOL_TEXT, frame_color, 2)->set_width(D_DEFAULT_WIDTH-D_MARGINS_X-D_H_SPACE);
		add_table(5,0)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE/2));
		{
			// header
			new_component<gui_margin_t>(8);
			new_component<gui_margin_t>(D_BUTTON_WIDTH);
			new_component<gui_label_t>("This Year");
			new_component<gui_label_t>("Last Year");
			new_component<gui_fill_t>();

			if (building->get_tile()->get_desc()->get_type() == building_desc_t::city_res) {
				new_component<gui_colorbox_t>(goods_manager_t::passengers->get_color())->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
				new_component<gui_label_t>("Visiting trip");
				lb_visiting_success_rate[0].set_fixed_width(value_cell_width);
				lb_visiting_success_rate[1].set_fixed_width(value_cell_width);
				lb_visiting_success_rate[0].set_align(gui_label_t::right);
				lb_visiting_success_rate[1].set_align(gui_label_t::right);
				add_component(&lb_visiting_success_rate[0]);
				add_component(&lb_visiting_success_rate[1]);
				new_component<gui_fill_t>();

				new_component<gui_colorbox_t>(color_idx_to_rgb(COL_COMMUTER))->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
				new_component<gui_label_t>("Commuting trip");
				lb_commuting_success_rate[0].set_fixed_width(value_cell_width);
				lb_commuting_success_rate[1].set_fixed_width(value_cell_width);
				lb_commuting_success_rate[0].set_align(gui_label_t::right);
				lb_commuting_success_rate[1].set_align(gui_label_t::right);
				add_component(&lb_commuting_success_rate[0]);
				add_component(&lb_commuting_success_rate[1]);
				new_component<gui_fill_t>();
			}

			// show only if this building has mail demands
			if(building->get_adjusted_mail_demand()) {
				new_component<gui_colorbox_t>(goods_manager_t::mail->get_color())->set_size(scr_size(LINESPACE/2 + 2, LINESPACE/2 + 2));
				new_component<gui_label_t>("hd_mailing");
				lb_mail_success_rate[0].set_fixed_width(value_cell_width);
				lb_mail_success_rate[1].set_fixed_width(value_cell_width);
				lb_mail_success_rate[0].set_align(gui_label_t::right);
				lb_mail_success_rate[1].set_align(gui_label_t::right);
				add_component(&lb_mail_success_rate[0]);
				add_component(&lb_mail_success_rate[1]);
				new_component<gui_fill_t>();
			}
		}
		end_table();
	}

	update_stats();
}

void gui_building_stats_t::update_stats()
{
	if (building->get_tile()->get_desc()->get_type() == building_desc_t::city_res) {
		lb_visiting_success_rate[0].buf().printf( building->get_passenger_success_percent_this_year_visiting()<65535 ? "%u%%" : "-",  building->get_passenger_success_percent_this_year_visiting());
		lb_visiting_success_rate[1].buf().printf( building->get_passenger_success_percent_last_year_visiting()<65535 ? "%u%%" : "-",  building->get_passenger_success_percent_last_year_visiting());
		lb_commuting_success_rate[0].buf().printf(building->get_passenger_success_percent_this_year_commuting()<65535 ? "%u%%" : "-", building->get_passenger_success_percent_this_year_commuting());
		lb_commuting_success_rate[1].buf().printf(building->get_passenger_success_percent_last_year_commuting()<65535 ? "%u%%" : "-", building->get_passenger_success_percent_last_year_commuting());
		lb_visiting_success_rate[0].update();
		lb_visiting_success_rate[1].update();
		lb_commuting_success_rate[0].update();
		lb_commuting_success_rate[1].update();
	}
	else {
		if (building->get_adjusted_visitor_demand()) {
			lb_visitor_arrivals[0].buf().printf(building->get_passengers_succeeded_visiting() < 65535 ? "%u" : "-", building->get_passengers_succeeded_visiting());
			lb_visitor_arrivals[1].buf().printf(building->get_passenger_success_percent_last_year_visiting() < 65535 ? "%u" : "-", building->get_passenger_success_percent_last_year_visiting());
			lb_visitor_arrivals[0].update();
			lb_visitor_arrivals[1].update();
		}
		lb_commuter_arrivals[0].buf().printf(building->get_passengers_succeeded_commuting() < 65535 ? "%u" : "-", building->get_passengers_succeeded_commuting());
		lb_commuter_arrivals[1].buf().printf(building->get_passenger_success_percent_last_year_commuting() < 65535 ? "%u" : "-", building->get_passenger_success_percent_last_year_commuting());
		lb_commuter_arrivals[0].update();
		lb_commuter_arrivals[1].update();
	}

	if (building->get_adjusted_mail_demand()) {
		lb_mail_success_rate[0].buf().printf(building->get_mail_delivery_success_percent_this_year()<65535 ? "%u%%" : "-", building->get_mail_delivery_success_percent_this_year());
		lb_mail_success_rate[1].buf().printf(building->get_mail_delivery_success_percent_last_year()<65535 ? "%u%%" : "-", building->get_mail_delivery_success_percent_last_year());
		lb_mail_success_rate[0].update();
		lb_mail_success_rate[1].update();
		lb_mail_sent[0].buf().printf(building->get_mail_delivery_succeeded() < 65535 ? "%u" : "-", building->get_mail_delivery_succeeded());
		lb_mail_sent[1].buf().printf(building->get_mail_delivery_succeeded_last_year() < 65535 ? "%u" : "-", building->get_mail_delivery_succeeded_last_year());
		lb_mail_sent[0].update();
		lb_mail_sent[1].update();
	}
}

void gui_building_stats_t::draw(scr_coord offset)
{
	if (building) {
		update_stats();
		gui_aligned_container_t::draw(offset);
	}
}

building_info_t::building_info_t(gebaeude_t* gb, player_t* owner) :
	base_infowin_t(translator::translate(gb->get_name()), owner),
	building_view(koord3d::invalid, scr_size(max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	cont_stats(gb, get_titlecolor()),
	scrolly_stats(&cont_stats, true),
	scrolly_near_by_halt(&cont_near_by_halt, true),
	scrolly_signalbox(&cont_signalbox_info, true)
{
	this->owner = owner;
	building = gb->get_first_tile();

	building_view.set_location(building->get_pos());
	set_embedded(&building_view);

	add_table(1,0)->set_margin(scr_size(0,0), scr_size(0, 0));
	tabs.add_tab(&scrolly_stats, translator::translate("Statistics"));
	tabs.add_tab(&scrolly_near_by_halt, translator::translate("Stops potentially within walking distance:"));
	tabs.add_listener(this);
	add_component(&tabs);
	end_table();

	cont_near_by_halt.set_table_layout(7,0);
	cont_near_by_halt.set_margin(scr_size(0, D_V_SPACE), scr_size(0, 0));

	if (building->is_signalbox()) {
		tabs.add_tab(&scrolly_signalbox, translator::translate("Signalbox info."));
		tabs.set_active_tab_index(2);
		cont_signalbox_info.set_table_layout(1,0);
		cont_signalbox_info.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_H_SPACE, 0));
		cont_signalbox_info.add_table(3,0);
		{
			if( owner == welt->get_active_player() ) {
				cont_signalbox_info.new_component<gui_label_t>("Fixed Costs");
				gui_label_buf_t *lb_sb_fixedcost = cont_signalbox_info.new_component<gui_label_buf_t>();
				sint64 maintenance = building->get_tile()->get_desc()->get_maintenance();
				if (maintenance == PRICE_MAGIC)
				{
					maintenance = building->get_tile()->get_desc()->get_level() * welt->get_settings().maint_building;
				}
				char maintenance_number[64];
				money_to_string(maintenance_number, (double)welt->calc_adjusted_monthly_figure(maintenance) / 100.0);
				lb_sb_fixedcost->buf().append(maintenance_number);
				lb_sb_fixedcost->update();
				cont_signalbox_info.new_component<gui_fill_t>();
			}

			cont_signalbox_info.new_component<gui_label_t>("Radius");
			gui_label_buf_t *lb_sb_radius = cont_signalbox_info.new_component<gui_label_buf_t>();
			const uint32 radius = building->get_tile()->get_desc()->get_radius();
			if (!radius) {
				lb_sb_radius->buf().append(translator::translate("infinite_range"));
			}
			else if (radius < 1000) {
				lb_sb_radius->buf().printf("%im", radius);
			}
			else {
				const uint8 digit = radius < 20000 ? 1 : 0;
				lb_sb_radius->buf().append((double)radius / 1000.0, digit);
				lb_sb_radius->buf().append("km");
			}
			lb_sb_radius->update();
			cont_signalbox_info.new_component<gui_fill_t>();

			cont_signalbox_info.new_component<gui_label_t>("Signals");
			cont_signalbox_info.add_component(&lb_signals);
			cont_signalbox_info.new_component<gui_fill_t>();
		}
		cont_signalbox_info.end_table();

		signal_table.set_table_layout(4,0);
		cont_signalbox_info.add_component(&signal_table);

		update_signalbox_info();
		tabs.set_active_tab_index(2);
	}
	update_near_by_halt();
	building->info(buf);
	recalc_size();
	set_resizemode(diagonal_resize);
	set_min_windowsize(scr_size(max(D_DEFAULT_WIDTH, get_min_windowsize().w), D_TITLEBAR_HEIGHT + textarea.get_size().h + D_V_SPACE + D_MARGIN_TOP + D_TAB_HEADER_HEIGHT));
	set_windowsize(scr_size(get_min_windowsize().w, textarea.get_size().h + cont_stats.get_size().h + D_MARGINS_Y*2 + D_V_SPACE*2 + D_TITLEBAR_HEIGHT + D_TAB_HEADER_HEIGHT));
}

bool building_info_t::is_weltpos()
{
	return (welt->get_viewport()->is_on_center(building->get_pos()));
}

void building_info_t::update_near_by_halt()
{
	cont_near_by_halt.remove_all();
	// List of stops potentially within walking distance.
	const planquadrat_t* plan = welt->access(building->get_pos().get_2d());
	const nearby_halt_t *const halt_list = plan->get_haltlist();
	bool any_operating_stops_passengers = false;
	bool any_operating_stops_mail = false;

	// header
	if (plan->get_haltlist_count()>0) {
		cont_near_by_halt.new_component<gui_margin_t>(8);
		cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::passengers->get_image_id(0), true);
		cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::mail->get_image_id(0), true);
		cont_near_by_halt.new_component_span<gui_empty_t>(3);
		cont_near_by_halt.new_component<gui_empty_t>();
	}

	for (int h = 0; h < plan->get_haltlist_count(); h++) {
		const halthandle_t halt = halt_list[h].halt;
		if (!halt->is_enabled(goods_manager_t::passengers) && !halt->is_enabled(goods_manager_t::mail)) {
			continue; // Exclude freight stations as this is not for a factory window
		}
		// distance
		const uint32 distance = halt_list[h].distance * world()->get_settings().get_meters_per_tile();
		gui_label_buf_t *l = cont_near_by_halt.new_component<gui_label_buf_t>();
		if (distance < 1000) {
			l->buf().printf("%um", distance);
		}
		else {
			l->buf().append((float)(distance/1000.0), 2);
			l->buf().append("km");
		}
		l->set_align(gui_label_t::right);
		l->update();

		// Service operation indicator
		if (halt->is_enabled(goods_manager_t::passengers)) {
			cont_near_by_halt.new_component<gui_colorbox_t>()->init(halt->get_status_color(0), scr_size(10, D_INDICATOR_HEIGHT), true, false);
		}
		else {
			cont_near_by_halt.new_component<gui_margin_t>(8);
		}
		if (halt->is_enabled(goods_manager_t::mail)) {
			cont_near_by_halt.new_component<gui_colorbox_t>()->init(halt->get_status_color(1), scr_size(10, D_INDICATOR_HEIGHT), true, false);
		}
		else {
			cont_near_by_halt.new_component<gui_margin_t>(8);
		}

		// station name with owner color
		cont_near_by_halt.new_component<gui_label_t>(halt->get_name(), color_idx_to_rgb(halt->get_owner()->get_player_color1()+env_t::gui_player_color_dark), gui_label_t::left);

		if (skinverwaltung_t::on_foot) {
			cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::on_foot->get_image_id(0), true);
		}
		else {
			cont_near_by_halt.new_component<gui_label_t>("Walking time");
		}

		const uint16 walking_time = welt->walking_time_tenths_from_distance(halt_list[h].distance);
		char walking_time_as_clock[32];
		welt->sprintf_time_tenths(walking_time_as_clock, sizeof(walking_time_as_clock), walking_time);
		gui_label_buf_t *lb_wt = cont_near_by_halt.new_component<gui_label_buf_t>();
		lb_wt->buf().printf("%s %s", skinverwaltung_t::on_foot ? "" : ":", walking_time_as_clock);
		lb_wt->update();
		cont_near_by_halt.new_component<gui_fill_t>();
	}
	// footer
	if (plan->get_haltlist_count() > 0) {
		cont_near_by_halt.new_component<gui_margin_t>(8);
		if (skinverwaltung_t::alerts && !any_operating_stops_passengers) {
			cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::alerts->get_image_id(2), true);
		}
		else {
			cont_near_by_halt.new_component<gui_empty_t>();
		}
		if (skinverwaltung_t::alerts && !any_operating_stops_mail) {
			cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::alerts->get_image_id(2), true);
		}
		else {
			cont_near_by_halt.new_component<gui_empty_t>();
		}
		cont_near_by_halt.new_component_span<gui_empty_t>(3);
		cont_near_by_halt.new_component<gui_empty_t>();
	}

	if (!any_operating_stops_passengers) {
		cont_near_by_halt.new_component_span<gui_margin_t>((D_H_SPACE,D_V_SPACE), 7);
		cont_near_by_halt.new_component<gui_empty_t>();
		if (skinverwaltung_t::alerts) {
			cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::alerts->get_image_id(2), true);
		}
		else {
			cont_near_by_halt.new_component<gui_empty_t>();
		}
		cont_near_by_halt.new_component_span<gui_label_t>("No passenger service", 4);
		cont_near_by_halt.new_component<gui_empty_t>();
	}
	if (!any_operating_stops_mail) {
		cont_near_by_halt.new_component_span<gui_margin_t>((D_H_SPACE, D_V_SPACE), 7);
		cont_near_by_halt.new_component<gui_empty_t>();
		if (skinverwaltung_t::alerts) {
			cont_near_by_halt.new_component<gui_image_t>()->set_image(skinverwaltung_t::alerts->get_image_id(2), true);
		}
		else {
			cont_near_by_halt.new_component<gui_empty_t>();
		}
		cont_near_by_halt.new_component_span<gui_label_t>("No mail service", 4);
		cont_near_by_halt.new_component<gui_empty_t>();
	}
	resize(scr_size(0,0));
}

void building_info_t::update_signalbox_info() {
	const signalbox_t *sb = static_cast<const signalbox_t *>(building->get_first_tile());
	lb_signals.buf().printf("%d/%d", sb->get_number_of_signals_controlled_from_this_box(), building->get_tile()->get_desc()->get_capacity());
	lb_signals.update();

	signal_table.remove_all();
	cont_signalbox_info.set_visible(false);
	if (sb->get_number_of_signals_controlled_from_this_box() > 0) {
		signal_table.set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(0, 0));
		// connected signal list
		const slist_tpl<koord3d> &signals = sb->get_signal_list();
		FOR(slist_tpl<koord3d>, k, signals){
			grund_t* gr = welt->lookup(k);
			if (!gr) {
				continue;
			}
			signal_t* s = gr->find<signal_t>();
			if (!s) {
				continue;
			}

			const uint32 distance = shortest_distance(building->get_pos().get_2d(), k.get_2d()) * welt->get_settings().get_meters_per_tile();
			gui_label_buf_t *lb = signal_table.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			if (distance<1000) {
				lb->buf().printf("%um", distance);
			}
			else if(distance<20000) {
				lb->buf().printf("%.1fkm", (double)distance/1000.0);
			}
			else {
				lb->buf().printf("%ukm", distance/1000);
			}
			lb->update();

			lb = signal_table.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
			lb->buf().printf("(%s)", k.get_str());
			lb->update();

			signal_table.new_component<gui_label_t>(translator::translate(s->get_desc()->get_name()));

			signal_table.new_component<gui_fill_t>();
		}

		cont_signalbox_info.set_visible(true);
	}
	reset_min_windowsize();
}

// Tabs are closed => left click the tab to open it with the appropriate size.
// A tab is open   => Switching to a different tab does not change the size.
//                    left click the same tab again to adjust it to the appropriate size.
void building_info_t::set_tab_opened()
{
	const scr_coord_val margin_above_tab = D_TITLEBAR_HEIGHT + textarea.get_size().h + D_V_SPACE + D_MARGIN_TOP + D_TAB_HEADER_HEIGHT;
	scr_coord_val ideal_size_h = margin_above_tab + D_MARGIN_BOTTOM;
	switch (tabstate)
	{
		case 0:
		default:
			ideal_size_h += cont_stats.get_size().h;
			break;
		case 1: // near by stop
			ideal_size_h += cont_near_by_halt.get_size().h;
			break;
		case 2: // signalbox info
			ideal_size_h += cont_signalbox_info.get_size().h;
			break;
	}
	if (get_windowsize().h != ideal_size_h) {
		set_windowsize(scr_size(get_windowsize().w, min(display_get_height() - margin_above_tab, ideal_size_h)));
	}
}

bool building_info_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(  comp == &tabs  ) {
		const sint16 old_tab = tabstate;
		tabstate = tabs.get_active_tab_index();
		if ( get_windowsize().h == get_min_windowsize().h || tabstate == old_tab  ) {
			set_tab_opened();
		}
		return true;
	}
	return false;
}

void building_info_t::draw(scr_coord pos, scr_size size)
{
	buf.clear();
	building->info(buf);

	switch (tabs.get_active_tab_index())
	{
		case 0:
			cont_stats.update_stats();
			break;
		case 1:
			update_near_by_halt(); break;
		case 2:
			update_signalbox_info(); break;
		default:
			break;
	}
	if (get_embedded() == NULL) {
		// no building anymore, destroy window
		destroy_win(this);
		return;
	}
	base_infowin_t::draw(pos, size);
}

void building_info_t::map_rotate90(sint16 new_ysize)
{
	building_view.map_rotate90(new_ysize);
}
