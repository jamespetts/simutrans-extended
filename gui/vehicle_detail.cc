/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "vehicle_detail.h"

#include "../simworld.h"
#include "../convoy.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../descriptor/vehicle_desc.h"
#include "../descriptor/intro_dates.h"
#include "../utils/simstring.h"
#include "components/gui_colorbox.h"
#include "components/gui_divider.h"


bool vehicle_detail_t::need_update = false;

gui_vehicle_capacity_t::gui_vehicle_capacity_t(const vehicle_desc_t *veh_type)
{
	set_table_layout(1,0);
	if (veh_type->get_total_capacity() > 0 || veh_type->get_overcrowded_capacity() > 0) {
		// total capacity
		add_table(3,1);
		{
			new_component<gui_label_t>("Capacity:");
			new_component<gui_image_t>(veh_type->get_freight_type()->get_catg_symbol(), 0, ALIGN_CENTER_V, true);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf(" %3d", veh_type->get_total_capacity());
			if (veh_type->get_overcrowded_capacity()) {
				lb->buf().printf(" (%d)", veh_type->get_overcrowded_capacity());
			}
			lb->buf().printf("%s %s", translator::translate(veh_type->get_freight_type()->get_mass()), translator::translate(veh_type->get_freight_type()->get_catg_name()));
			lb->update();
		}
		end_table();

		// compartment info
		const uint8 catg_index = veh_type->get_freight_type()->get_catg_index();
		if( catg_index==goods_manager_t::INDEX_PAS  ||  goods_manager_t::get_classes_catg_index(catg_index)>1 ) {
			// | margin | capacity | accommo rank | accommo name | comfort(+tooltip) |
			add_table(5,0)->set_spacing(scr_size(D_H_SPACE,1));
			{
				for( uint8 ac=0; ac<goods_manager_t::get_classes_catg_index(catg_index); ac++ ) {
					if( veh_type->get_capacity(ac)>0 ) {
						new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
						lb->buf().printf("%3u", veh_type->get_capacity(ac));
						lb->set_fixed_width(proportional_string_width("8888"));
						lb->update();
						lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
						lb->buf().printf("[%u]", ac + 1);
						lb->set_fixed_width(proportional_string_width("[8]"));
						lb->update();
						lb = new_component<gui_label_buf_t>();
						lb->buf().append(translator::translate(veh_type->get_accommodation_name(ac)));
						lb->update();
						// comfort
						if( catg_index==goods_manager_t::INDEX_PAS ) {
							add_table(2,1);
							{
								char time_str[32];
								world()->sprintf_time_secs(time_str, 32, world()->get_settings().max_tolerable_journey(veh_type->get_comfort(ac)), (env_t::show_month==env_t::DATE_FMT_64_SECOND_MINUTE || env_t::show_month==env_t::DATE_FMT_64_SECOND_MINUTE_PRETTY));
								if( skinverwaltung_t::comfort ) {
									new_component<gui_image_t>(skinverwaltung_t::comfort->get_image_id(0), 0, ALIGN_NONE, true);
									lb = new_component<gui_label_buf_t>();
									lb->buf().append(time_str);
									lb->update();
								}
								else {
									lb = new_component<gui_label_buf_t>();
									lb->buf().printf("%s %s", translator::translate("(Max. comfortable journey time: "), time_str);
									lb->update();
								}
							}
							end_table();
						}
						else {
							new_component<gui_empty_t>();
						}
					}
				}

				if (veh_type->get_overcrowded_capacity()) {
					new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%3u", veh_type->get_overcrowded_capacity());
					lb->set_fixed_width(proportional_string_width("8888"));
					lb->update();
					new_component<gui_image_t>(SYMBOL_OVERCROWDING, 0, ALIGN_CENTER_V, true);
					new_component<gui_label_t>("overcrowded_capacity");
					new_component<gui_empty_t>();
				}
			}
			end_table();
		}
		else if (veh_type->get_mixed_load_prohibition()) {
			add_table(3,1)->set_spacing(scr_size(D_H_SPACE,1));
			{
				new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH - D_H_SPACE);
				new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(1) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
				new_component<gui_label_t>("(mixed_load_prohibition)", SYSCOL_MIXLOAD_PROHIBITION);
			}
			end_table();
		}
	}
	else {
		new_component<gui_label_t>("this_vehicle_carries_no_good", SYSCOL_TEXT_WEAK);
	}
	new_component<gui_margin_t>(1,D_V_SPACE);

	// Loading time is only relevant if there is something to load.
	if( veh_type->get_max_loading_time() ) {
		add_table(2,0)->set_spacing(scr_size(D_H_SPACE,1));
		{
			char min_loading_time_as_clock[32];
			char max_loading_time_as_clock[32];
			world()->sprintf_ticks(min_loading_time_as_clock, sizeof(min_loading_time_as_clock), veh_type->get_min_loading_time());
			world()->sprintf_ticks(max_loading_time_as_clock, sizeof(max_loading_time_as_clock), veh_type->get_max_loading_time());
			new_component<gui_label_t>("Loading time:");
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf("%s - %s", min_loading_time_as_clock, max_loading_time_as_clock);
			lb->update();
			new_component<gui_margin_t>(1, D_V_SPACE);
		}
		end_table();
	}

	// catering
	if (veh_type->get_catering_level() > 0) {
		if (veh_type->get_freight_type() == goods_manager_t::mail) {
			//Catering vehicles that carry mail are treated as TPOs.
			new_component<gui_label_t>("This is a travelling post office");
		}
		else {
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf(translator::translate("Catering level: %i"), veh_type->get_catering_level());
			lb->update();
		}
		new_component<gui_margin_t>(1, D_V_SPACE);
	}
}


gui_vehicle_detail_access_t::gui_vehicle_detail_access_t(const vehicle_desc_t *veh)
{
	veh_type = veh;
	if( veh_type==NULL ) {
		dbg->warning("gui_vehicle_detail_access_t::gui_vehicle_detail_access_t()", "Component created with vehicle_desc is null");
	}
	else {
		set_table_layout(2,2);
		new_component<gui_image_t>(veh_type->get_image_id(ribi_t::dir_southwest, goods_manager_t::none), 1, 0, true);
		add_table(1,2);
		{
			new_component<gui_table_cell_t>( veh_type->get_name(), SYSCOL_TH_BACKGROUND_LEFT )->set_flexible(true, false);

			add_table(2,1);
			{
				const uint16 month_now = world()->get_timeline_year_month();
				PIXVAL col_val = COL_SAFETY;
				if (veh_type->is_available_only_as_upgrade()) {
					if (veh_type->is_retired(month_now)) { col_val = color_idx_to_rgb(COL_DARK_PURPLE); }
					else if (veh_type->is_obsolete(month_now)) { col_val = SYSCOL_OBSOLETE; }
					else { col_val = SYSCOL_UPGRADEABLE; }
				}
				else if (veh_type->is_future(month_now)) { col_val = color_idx_to_rgb(MN_GREY0); }
				else if (veh_type->is_obsolete(month_now)) { col_val = SYSCOL_OBSOLETE; }
				else if (veh_type->is_retired(month_now)) { col_val = SYSCOL_OUT_OF_PRODUCTION; }
				new_component<gui_vehicle_bar_t>(col_val)->set_flags(veh_type->get_basic_constraint_prev(), veh_type->get_basic_constraint_next(), veh_type->get_interactivity());

				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().printf("%s: %s - ", translator::translate("Available"), translator::get_short_date(veh->get_intro_year_month() / 12, veh->get_intro_year_month() % 12));
				if (veh_type->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
					(((!world()->get_settings().get_show_future_vehicle_info() && veh_type->will_end_prodection_soon(world()->get_timeline_year_month()))
						|| world()->get_settings().get_show_future_vehicle_info()
						|| veh_type->is_retired(world()->get_timeline_year_month()))))
				{
					lb->buf().printf("%s", translator::get_short_date(veh->get_retire_year_month() / 12, veh->get_retire_year_month() % 12));
				}
				lb->update();
			}
			end_table();
		}
		end_table();
		new_component_span<gui_border_t>(2);
	}
}


bool gui_vehicle_detail_access_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	if( veh_type==NULL ) {
		dbg->warning("gui_vehicle_detail_access_t::infowin_event()", "vehicle_desc is null");
	}
	else {
		if( !swallowed  &&  IS_LEFTRELEASE(ev) ) {
			vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail));
			if( win ) {
				win->set_vehicle(veh_type);
				vehicle_detail_t::need_update = true;
				swallowed = true;
			}
			else {
				dbg->warning("gui_vehicle_detail_access_t::infowin_event()", "vehicle_detail dialog is not found");
			}
		}
	}
	return swallowed;
}


vehicle_detail_t::vehicle_detail_t(const vehicle_desc_t *v) :
	gui_frame_t(""),
	scroll_livery(&cont_livery,true,true),
	scroll_upgrade(&cont_upgrade, true, true)

{
	veh_type = v;

	gui_frame_t::set_name(translator::translate("vehicle_details"));
	init_table();
}


void vehicle_detail_t::init_table()
{
	if (veh_type == NULL) {
		destroy_win(this);
	}
	remove_all();
	tabs.clear();
	set_table_layout(1,0);
	new_component<gui_label_t>(veh_type->get_name());
	add_table(2,1)->set_alignment(ALIGN_TOP);
	{

		add_table(1,3)->set_alignment(ALIGN_TOP|ALIGN_CENTER_H);
		{
			new_component<gui_image_t>(veh_type->get_image_id(ribi_t::dir_south, veh_type->get_freight_type()), 0, ALIGN_NONE, true);
			month_now = world()->get_timeline_year_month();
			PIXVAL col_val = COL_SAFETY;
			if (veh_type->is_available_only_as_upgrade()) {
				if (veh_type->is_retired(month_now)) { col_val = color_idx_to_rgb(COL_DARK_PURPLE); }
				else if (veh_type->is_obsolete(month_now)) { col_val = SYSCOL_OBSOLETE; }
				else { col_val = SYSCOL_UPGRADEABLE; }
			}
			else if (veh_type->is_future(month_now)) { col_val = color_idx_to_rgb(MN_GREY0); }
			else if (veh_type->is_obsolete(month_now)) { col_val = SYSCOL_OBSOLETE; }
			else if (veh_type->is_retired(month_now)) { col_val = SYSCOL_OUT_OF_PRODUCTION; }
			new_component<gui_vehicle_bar_t>(col_val)->set_flags(veh_type->get_basic_constraint_prev(), veh_type->get_basic_constraint_next(), veh_type->get_interactivity());

			gui_aligned_container_t *tbl = add_table(2,1);
			tbl->set_spacing(scr_size(0,0));
			tbl->set_force_equal_columns(true);
			tbl->set_alignment(ALIGN_CENTER_H|ALIGN_CENTER_V);
			{
				bt_prev.init(button_t::roundbox_left, "<");
				bt_next.init(button_t::roundbox_right, ">");
				// front side constraint
				if (veh_type->get_basic_constraint_prev()&vehicle_desc_t::unconnectable) {
					tbl->new_component<gui_label_t>(" - ", COL_DANGER)->set_fixed_width(bt_prev.get_min_size().w);
				}
				else if (veh_type->get_basic_constraint_prev()&vehicle_desc_t::intermediate_unique) {
					bt_prev.add_listener(this);
					tbl->add_component(&bt_prev);
				}
				else {
					tbl->new_component<gui_empty_t>();
				}

				// rear side constraint
				if (veh_type->get_basic_constraint_next()&vehicle_desc_t::unconnectable) {
					tbl->new_component<gui_label_t>(" - ", COL_DANGER)->set_fixed_width(bt_prev.get_min_size().w);
				}
				else if (veh_type->get_basic_constraint_next()&vehicle_desc_t::intermediate_unique) {
					bt_next.add_listener(this);
					tbl->add_component(&bt_next);
				}
				else {
					tbl->new_component<gui_empty_t>();
				}
			}
			end_table();
		}
		end_table();
		// capacity, loading time, catering
		new_component<gui_vehicle_capacity_t>(veh_type);
	}
	end_table();

	// TODO: MUST_USE/MAY_USE here


	// init main specs table
	{
		cont_spec.remove_all();
		cont_spec.set_table_layout(2,0);
		cont_spec.set_table_frame(true, true);
		cont_spec.set_margin(scr_size(3, 3), scr_size(3, 3));
		cont_spec.set_spacing(scr_size(D_H_SPACE, 2));
		cont_spec.set_alignment(ALIGN_CENTER_V);

		vehicle_as_potential_convoy_t convoy(*veh_type);
		// Engine information:
		const uint8 et = (uint8)veh_type->get_engine_type()+1;
		if (et) {
			cont_spec.new_component<gui_table_header_t>("engine_type", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			gui_table_cell_buf_t *td = cont_spec.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%s", translator::translate(vehicle_builder_t::engine_type_names[et]));
			td->update();
		}

		if (veh_type->get_power()) {
			cont_spec.new_component<gui_table_header_t>("Power:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			gui_table_cell_buf_t *td = cont_spec.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%u kW", veh_type->get_power());

			cont_spec.new_component<gui_table_header_t>("Tractive force:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			cont_spec.add_table(1,3)->set_spacing(scr_size(0,0));
			{
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().printf("%u kN", veh_type->get_tractive_effort());
				td->update();
				sint32 friction = convoy.get_current_friction();
				sint32 max_weight = convoy.calc_max_starting_weight(friction);
				sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
				sint32 min_weight = convoy.calc_max_weight(friction);
				sint32 max_speed = convoy.get_vehicle_summary().max_speed;
				if (min_weight < convoy.get_vehicle_summary().weight) {
					min_weight = convoy.get_vehicle_summary().weight;
					max_speed = convoy.calc_max_speed(weight_summary_t(min_weight, friction));
				}
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().printf(translator::translate(" %gt @ %d km/h "), min_weight * 0.001f, max_speed);
				if (min_speed != max_speed) {
					td = cont_spec.new_component<gui_table_cell_buf_t>();
					td->buf().printf(translator::translate(" %gt @ %d km/h "), max_weight * 0.001f, min_speed);
					td->update();
				}
			}
			cont_spec.end_table();
		}

		cont_spec.new_component<gui_table_header_t>("Range", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		if (veh_type->get_range() == 0) {
			cont_spec.new_component<gui_table_cell_t>("unlimited");
		}
		else {
			gui_table_cell_buf_t *td = cont_spec.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%i km", veh_type->get_range());
			td->update();
		}
		if (veh_type->get_waytype() == air_wt) {
			cont_spec.new_component<gui_table_header_t>("Minimum runway length", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			gui_table_cell_buf_t *td = cont_spec.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%i m", veh_type->get_minimum_runway_length());
			td->update();
		}

		cont_spec.new_component<gui_table_header_t>("Max. speed:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		gui_table_cell_buf_t *td = cont_spec.new_component<gui_table_cell_buf_t>();
		td->buf().printf("%3d %s", veh_type->get_topspeed(), "km/h");
		td->update();

		cont_spec.new_component<gui_table_header_t>("Gewicht", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		td = cont_spec.new_component<gui_table_cell_buf_t>();
		td->buf().printf("%4.1ft", veh_type->get_weight() / 1000.0);
		td->update();

		if (veh_type->get_waytype() != water_wt) {
				cont_spec.new_component<gui_table_header_t>("Axle load:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().printf("%it", veh_type->get_axle_load());
				td->update();

				cont_spec.new_component<gui_table_header_t>("Way wear factor", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().append(veh_type->get_way_wear_factor() / 10000.0, 4);
				td->update();

				cont_spec.new_component<gui_table_header_t>("Max. brake force:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().printf("%4.1fkN", convoy.get_braking_force().to_double() / 1000.0);
				td->set_color(veh_type->get_brake_force() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
				td->update();

				cont_spec.new_component<gui_table_header_t>("Rolling resistance:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
				td = cont_spec.new_component<gui_table_cell_buf_t>();
				td->buf().printf("%4.3fkN", veh_type->get_rolling_resistance().to_double() * (double)veh_type->get_weight() / 1000.0);
				td->update();
		}
		cont_spec.set_size(cont_spec.get_min_size());
	}

	// init maintenance data table
	{
		cont_maintenance.remove_all();
		cont_maintenance.set_table_layout(2, 0);
		cont_maintenance.set_table_frame(true, true);
		cont_maintenance.set_margin(scr_size(3, 3), scr_size(3, 3));
		cont_maintenance.set_spacing(scr_size(D_H_SPACE, 2));
		cont_maintenance.set_alignment(ALIGN_CENTER_V);

		char tmp[128];

		if (veh_type->is_available_only_as_upgrade()) {
			cont_maintenance.new_component<gui_table_header_t>("Upgrade price:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		}
		else {
			cont_maintenance.new_component<gui_table_header_t>("Price", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		}
		gui_table_cell_buf_t *td = cont_maintenance.new_component<gui_table_cell_buf_t>();
		if (veh_type->is_available_only_as_upgrade() && !veh_type->get_upgrade_price()) {
			money_to_string(tmp, veh_type->get_upgrade_price() / 100.0, false);
			td->set_color(SYSCOL_UPGRADEABLE);
		}
		else {
			money_to_string(tmp, veh_type->get_value() / 100.0, false);
		}
		td->buf().append(tmp);
		td->update();

		cont_maintenance.new_component<gui_table_header_t>("Maintenance", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		cont_maintenance.add_table(1, 2)->set_spacing(scr_size(0, 0));
		{
			td = cont_maintenance.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%1.2f$/km", veh_type->get_running_cost() / 100.0);
			td->update();
			td = cont_maintenance.new_component<gui_table_cell_buf_t>();
			td->set_color(veh_type->get_adjusted_monthly_fixed_cost() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
			td->buf().printf("%1.2f$/month", veh_type->get_adjusted_monthly_fixed_cost() / 100.0);
			td->update();
		}
		cont_maintenance.end_table();

		// UI TODO: add ex-15 parameters here
		// fuel per km
		// staff factor

		// Vehicle intro and retire information:
		cont_maintenance.new_component<gui_table_header_t>("Intro. date:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		td = cont_maintenance.new_component<gui_table_cell_buf_t>();
		td->buf().printf("%s", translator::get_short_date(veh_type->get_intro_year_month() / 12, veh_type->get_intro_year_month() % 12));
		td->update();

		if (veh_type->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
			(((!world()->get_settings().get_show_future_vehicle_info() && veh_type->will_end_prodection_soon(world()->get_timeline_year_month()))
				|| world()->get_settings().get_show_future_vehicle_info()
				|| veh_type->is_retired(world()->get_timeline_year_month()))))
		{
			cont_maintenance.new_component<gui_table_header_t>("Retire. date:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			//cont_maintenance.new_component<gui_table_cell_t>(translator::get_short_date(veh_type->get_retire_year_month()/12, veh_type->get_retire_year_month()%12));
			td = cont_maintenance.new_component<gui_table_cell_buf_t>();
			td->buf().printf("%s", translator::get_short_date(veh_type->get_retire_year_month() / 12, veh_type->get_retire_year_month() % 12));
			td->update();
		}


		cont_maintenance.new_component<gui_fill_t>(false,true);

		cont_maintenance.set_size(cont_spec.get_min_size());
	}

	bool has_available_liveries = false;
	// init livery info
	cont_livery.remove_all();
	if( veh_type->get_livery_count() ) {
		cont_livery.set_table_layout(2,0);
		vector_tpl<livery_scheme_t*>* schemes = welt->get_settings().get_livery_schemes();
		uint8 available_liveries = 0;
		for (uint32 i = 0; i < schemes->get_count(); i++)
		{
			livery_scheme_t* scheme = schemes->get_element(i);
			if (!scheme->is_available(month_now)) {
				continue;
			}
			if (const char* livery = scheme->get_latest_available_livery(month_now, veh_type)) {
				cont_livery.new_component<gui_image_t>(veh_type->get_image_id(ribi_t::dir_southwest, goods_manager_t::none, livery), 1, 0, true);
				cont_livery.new_component<gui_label_t>(scheme->get_name());
				has_available_liveries = true;
			}
		}
	}

	// init upgrade info
	cont_upgrade.remove_all();
	cont_upgrade.set_table_layout(1,0);
	bool show_upgrade_info=false;

	for (auto const veh_from : vehicle_builder_t::get_info(veh_type->get_waytype())) {
		if( veh_from->has_upgrade_to(veh_type) ) {
			if (!show_upgrade_info) {
				show_upgrade_info = true;
				cont_upgrade.new_component<gui_table_header_t>("this_vehicle_can_upgrade_from", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left)->set_flexible(true,false);
			}
			cont_upgrade.new_component<gui_vehicle_detail_access_t>(veh_from);
		}
	}
	if (veh_type->get_upgrades_count()) {
		if (show_upgrade_info) {
			cont_upgrade.new_component<gui_margin_t>(1,D_V_SPACE);
		}
		cont_upgrade.new_component<gui_table_header_t>("this_vehicle_can_upgrade_to", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::left)->set_flexible(true, false);
		for( uint8 i=0; i<veh_type->get_upgrades_count(); ++i ) {
			cont_upgrade.new_component<gui_vehicle_detail_access_t>(veh_type->get_upgrades(i));
		}

		show_upgrade_info = true;
	}

	tabs.add_tab(&cont_spec, translator::translate("basic_spec"));
	tabs.add_tab(&cont_maintenance, translator::translate("cd_maintenance_tab"));
	if (has_available_liveries) {
		tabs.add_tab(&scroll_livery, translator::translate("Selectable livery schemes"));
	}
	if (show_upgrade_info) {
		// in the simutrans(standard) specification, the image with number 0 is used for the icon
		tabs.add_tab(&scroll_upgrade, translator::translate("ug"), skinverwaltung_t::upgradable);
	}
	tabs.add_listener(this);
	add_component(&tabs);
	reset_min_windowsize();
	need_update = false;
}


void vehicle_detail_t::draw(scr_coord pos, scr_size size)
{
	if( need_update==true  ||  (welt->use_timeline()  &&  month_now != world()->get_timeline_year_month()) ) {
		init_table();
	}
	gui_frame_t::draw(pos, size);
}

bool vehicle_detail_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( comp==&bt_prev  &&  veh_type->get_leader_count() ) {
		set_vehicle(veh_type->get_leader(0));
	}
	if( comp==&bt_next  &&  veh_type->get_trailer_count()  ) {
		set_vehicle(veh_type->get_trailer(0));
	}
	return true;
}
