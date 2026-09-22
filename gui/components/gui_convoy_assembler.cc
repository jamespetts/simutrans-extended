/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <string>

#include "gui_convoy_assembler.h"
#include "gui_colorbox.h"

#include "../depot_frame.h"
#include "../replace_frame.h"
#include "../vehicle_detail.h"

#include "../../simskin.h"
#include "../../simworld.h"
#include "../../simconvoi.h"
#include "../../convoy.h"
#include "../simwin.h"
#include "../messagebox.h"


#include "../../bauer/goods_manager.h"
#include "../../bauer/vehikelbauer.h"
#include "../../descriptor/intro_dates.h"
#include "../../dataobj/replace_data.h"
#include "../../dataobj/translator.h"
#include "../../dataobj/environment.h"
#include "../../dataobj/livery_scheme.h"
#include "../../utils/simstring.h"
#include "../../vehicle/vehicle.h"
#include "../../descriptor/building_desc.h"
#include "../../descriptor/vehicle_desc.h"
#include "../../player/simplay.h"

#include "../../utils/cbuffer_t.h"
#include "../../unicode.h"
#include "../../utils/for.h"

#include "../../dataobj/settings.h"


#define COL_LACK_OF_MONEY color_idx_to_rgb(COL_DARK_ORANGE)
#define COL_EXCEED_AXLE_LOAD_LIMIT color_idx_to_rgb(COL_GREY3)
#define COL_UPGRADE_RESTRICTION color_idx_to_rgb(COL_DARK_PURPLE)

bool gui_convoy_assembler_t::show_all = false;
bool gui_convoy_assembler_t::show_outdated_vehicles = false;
bool gui_convoy_assembler_t::show_obsolete_vehicles = false;

int gui_convoy_assembler_t::selected_filter = VEHICLE_FILTER_RELEVANT;
char gui_convoy_assembler_t::name_filter_value[64] = "";

sint16 gui_convoy_assembler_t::sort_by_action=0;
bool gui_convoy_assembler_t::sort_reverse = false;


gui_vehicle_spec_t::gui_vehicle_spec_t(const vehicle_desc_t* desc)
{
	veh_type = desc;
	bt_main_view.init(button_t::roundbox_left_state, "basic_spec");
	bt_main_view.add_listener(this);
	bt_secondary_view.init(button_t::roundbox_right_state, "secondary_spec");
	bt_secondary_view.add_listener(this);

	update(0,0);
}

void gui_vehicle_spec_t::update(uint8 action, uint32 resale_value, uint16 current_livery)
{
	remove_all();
	set_table_layout(1,0);
	set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(0, D_MARGIN_BOTTOM));
	set_spacing(scr_size(D_H_SPACE, 1));
	if (veh_type != NULL) {
		vehicle_as_potential_convoy_t convoy(*veh_type);

		// Name and traction type
		add_table(2,1);
		{
			new_component<gui_label_t>(translator::translate(veh_type->get_name(), world()->get_settings().get_name_language_id()));
			if (veh_type->get_power() > 0) {
				const uint8 et = (uint8)veh_type->get_engine_type() + 1;
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TRACTION_TYPE);
				lb->buf().printf("(%s)", vehicle_builder_t::engine_type_names[et]);
				lb->update();
			}
		}
		end_table();

		new_component<gui_margin_t>(0, D_V_SPACE);

		if( is_secondary_view ) {
			// secondary display, livery, upgrade, author
			build_secondary_view( current_livery );
		}
		else {
			// main spec view
			add_table(4,1)->set_alignment(ALIGN_TOP);
			{
				// left
				gui_aligned_container_t *tbl = add_table(2,0);
				tbl->set_spacing(scr_size(1,1));
				tbl->set_table_frame(true, true);
				tbl->set_margin(scr_size(2,1), scr_size(2,1));
				{
					// Cost information:
					switch (action)
					{
						char tmp[128];
						case gui_convoy_assembler_t::va_sell:
						{
							new_component<gui_table_header_t>("Resale value:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
							gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>();
							money_to_string(tmp, resale_value / 100.0, false);
							td->buf().append(tmp);
							td->set_color(SYSCOL_OUT_OF_PRODUCTION);
							td->set_border_color(COL_DANGER);
							td->update();
							break;
						}
						case gui_convoy_assembler_t::va_upgrade:
						{
							new_component<gui_table_header_t>("Upgrade price:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
							gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>();
							td->set_color(SYSCOL_UPGRADEABLE);
							money_to_string(tmp, ((veh_type->is_available_only_as_upgrade() && !veh_type->get_upgrade_price()) ? veh_type->get_value() : veh_type->get_upgrade_price()) / 100.0, false);
							td->buf().append(tmp);
							td->update();
							break;
						}
						case gui_convoy_assembler_t::va_append:
						case gui_convoy_assembler_t::va_insert:
						default:
						{
							// new vehicle
							gui_table_header_buf_t *th = new_component<gui_table_header_buf_t>("", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
							th->buf().printf("%s:", translator::translate("Price"));
							th->update();
							gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>();
							money_to_string(tmp, veh_type->get_value() / 100.0, false);
							td->buf().append(tmp);
							td->update();
							break;
						}
					}

					new_component<gui_table_header_t>("Maintenance:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					add_table(1,2)->set_spacing(scr_size(0,0));
					{
						gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>();
						td->set_color(veh_type->get_running_cost() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						td->buf().printf("%1.2f$/km", veh_type->get_running_cost() / 100.0);
						td->update();
						td = new_component<gui_table_cell_buf_t>();
						td->set_color(veh_type->get_adjusted_monthly_fixed_cost() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						td->buf().printf("%1.2f$/month", veh_type->get_adjusted_monthly_fixed_cost() / 100.0);
						td->update();
					}
					end_table();

					new_component<gui_table_header_t>("fuel_cost", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					add_table(1, 2)->set_spacing(scr_size(0, 0));
					{
						gui_table_cell_buf_t* td = new_component<gui_table_cell_buf_t>();
						td->set_color(veh_type->get_fuel_per_km() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						const sint64 fuel_cost_per_unit = world()->get_fuel_cost(world()->get_timeline_year_month(), veh_type->get_engine_type());
						const sint64 fuel_cost_per_km = (fuel_cost_per_unit * veh_type->get_fuel_per_km()) / world()->get_settings().get_fuel_unit_cost_divider();
						td->buf().printf("%1.2f$/km", (double)fuel_cost_per_km / 100.0);
						td->update();
						td = new_component<gui_table_cell_buf_t>();
						td->set_color(veh_type->get_calibration_speed() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						td->buf().printf("%s %3d %s", "@", veh_type->get_calibration_speed(), "km/h");
						td->update();
					}
					end_table();

					// TODO: Find a more sophisticated way of representing fractional staff. Currently, they are not accounted for at all.
					new_component<gui_table_header_t>("staff_cost", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					gui_table_cell_buf_t* td = new_component<gui_table_cell_buf_t>();
					sint64 total_staff_cost = 0;
					for (uint8 i = 0; i < 255; i++)
					{
						const uint32 staff_hundredths_this_type = veh_type->get_staff_hundredths(i);
						const uint32 drivers_this_type = veh_type->get_drivers(i);
						const sint64 staff_this_type = (((sint64)staff_hundredths_this_type + 99ll) / 100ll) + (sint64)drivers_this_type;
						total_staff_cost += ((sint64)world()->get_staff_salary(world()->get_timeline_year_month(), i) * staff_this_type);
					}

					total_staff_cost = world()->calc_adjusted_monthly_figure(total_staff_cost);
					td->buf().printf("%1.2f$/month", (double)total_staff_cost / 100.0);
					td->update();

					new_component<gui_table_header_t>("availability", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					td = new_component<gui_table_cell_buf_t>();
					td->buf().printf("%3d %s", veh_type->get_starting_availability(), "%");
					td->update();

					// Physics information:
					new_component<gui_table_header_t>("Max. speed:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					td = new_component<gui_table_cell_buf_t>();
					td->buf().printf("%3d %s", veh_type->get_topspeed(), "km/h");
					td->update();

					new_component<gui_table_header_t>("Weight:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					td = new_component<gui_table_cell_buf_t>();
					td->buf().printf("%4.1ft", veh_type->get_weight() / 1000.0);
					td->update();

					if (veh_type->get_waytype() != water_wt) {
						new_component<gui_table_header_t>("Axle load:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						td = new_component<gui_table_cell_buf_t>();
						td->buf().printf("%it", veh_type->get_axle_load());
						td->update();

						new_component<gui_table_header_t>("Way wear factor", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						td = new_component<gui_table_cell_buf_t>();
						td->buf().append(veh_type->get_way_wear_factor() / 10000.0, 4);
						td->update();

						new_component<gui_table_header_t>("Max. brake force:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						td = new_component<gui_table_cell_buf_t>();
						td->set_color(veh_type->get_brake_force() > 0 ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						td->buf().printf("%4.1fkN", convoy.get_braking_force().to_double() / 1000.0);
						td->update();

						new_component<gui_table_header_t>("Rolling resistance:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						td = new_component<gui_table_cell_buf_t>();
						td->buf().printf("%4.3fkN", veh_type->get_rolling_resistance().to_double() * (double)veh_type->get_weight() / 1000.0);
						td->update();
					}

					new_component<gui_table_header_t>("Range", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					if (veh_type->get_range() == 0) {
						new_component<gui_table_cell_t>("unlimited");
					}
					else {
						td = new_component<gui_table_cell_buf_t>();
						td->buf().printf("%i km", veh_type->get_range());
						td->update();
					}

					if (veh_type->get_waytype() == air_wt) {
						new_component<gui_table_header_t>("Minimum runway length", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						td = new_component<gui_table_cell_buf_t>();
						td->buf().printf("%i m", veh_type->get_minimum_runway_length());
						td->update();
					}

					// Engine information:
					if (veh_type->get_power() > 0) {
						new_component<gui_table_header_t>("Power:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
						const uint8 et = (uint8)veh_type->get_engine_type() + 1;
						td = new_component<gui_table_cell_buf_t>();
						td->buf().printf("%4d kW / %d kN", veh_type->get_power(), veh_type->get_tractive_effort());
						td->update();

						if (veh_type->get_gear() != 64) {
							new_component<gui_table_header_t>("Gear:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
							td = new_component<gui_table_cell_buf_t>();
							td->buf().printf("%0.2f : 1", veh_type->get_gear() / 64.0);
							td->update();
						}

						sint32 friction = convoy.get_current_friction();
						sint32 max_weight = convoy.calc_max_starting_weight(friction);
						sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
						sint32 min_weight = convoy.calc_max_weight(friction);
						sint32 max_speed = convoy.get_vehicle_summary().max_speed;
						if (min_weight < convoy.get_vehicle_summary().weight) {
							min_weight = convoy.get_vehicle_summary().weight;
							max_speed = convoy.calc_max_speed(weight_summary_t(min_weight, friction));
						}
						td = new_component_span<gui_table_cell_buf_t>(2);
						td->buf().printf("%s:", translator::translate("Pulls"));
						td->buf().printf(
							min_speed == max_speed ? translator::translate(" %gt @ %d km/h ") : translator::translate(" %gt @ %dkm/h%s%gt @ %dkm/h"),
							min_weight * 0.001f, max_speed, translator::translate("..."), max_weight * 0.001f, min_speed);
						td->update();
					}
					else {
						new_component_span<gui_label_t>("unpowered", 2);
					}

					// To fix the minimum width of the left table
					new_component_span<gui_margin_t>((D_BUTTON_WIDTH<<1)+D_H_SPACE,1, 2);
				}
				end_table();
				new_component<gui_fill_t>();


				// right
				add_table(1,0)->set_spacing(scr_size(D_H_SPACE,1));
				{
					add_table(2,0)->set_spacing(scr_size(D_H_SPACE,1));
					{
						// Vehicle intro and retire information:
						new_component<gui_label_t>("Intro. date:");
						gui_label_buf_t *lb = new_component<gui_label_buf_t>();
						lb->buf().printf("%s", translator::get_short_date(veh_type->get_intro_year_month()/12, veh_type->get_intro_year_month()%12));
						lb->update();

						if (veh_type->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
							(((!world()->get_settings().get_show_future_vehicle_info() && veh_type->will_end_prodection_soon(world()->get_timeline_year_month()))
								|| world()->get_settings().get_show_future_vehicle_info()
								|| veh_type->is_retired(world()->get_timeline_year_month()))))
						{
							new_component<gui_label_t>("Retire. date:");
							new_component<gui_label_t>(translator::get_short_date(veh_type->get_retire_year_month()/12, veh_type->get_retire_year_month()%12));
						}
						else {
							new_component<gui_margin_t>(proportional_string_width(translator::translate("Retire. date:")),D_LABEL_HEIGHT);
						}
					}
					end_table();

					new_component<gui_border_t>();

					// Capacity information:
					new_component<gui_vehicle_capacity_t>(veh_type);

					add_table(2,0)->set_spacing(scr_size(D_H_SPACE, 1));
					{
						// Permissive way constraints
						// (If vehicle has, way must have)
						const way_constraints_t &way_constraints = veh_type->get_way_constraints();
						bool any_permissive = false;
						for (uint8 i = 0; i < way_constraints.get_count(); i++)
						{
							if (way_constraints.get_permissive(i))
							{
								if (!any_permissive) {
									new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(4):IMG_EMPTY, 0, ALIGN_CENTER_V, true);
									new_component<gui_label_t>("\nMUST USE: ", COL_DANGER); // TODO: Remove "\n" from the translation
								}
								new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
								gui_label_buf_t *lb = new_component<gui_label_buf_t>();
								char tmpbuf[30];
								sprintf(tmpbuf, "Permissive %i-%i", veh_type->get_waytype(), i);
								lb->buf().append(translator::translate(tmpbuf));
								lb->update();
								any_permissive = true;
							}
						}
						if (veh_type->get_is_tall())
						{
							if (!any_permissive) {
								new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
								new_component<gui_label_t>("\nMUST USE: ", 64262); // TODO: Remove "\n" from the translation
							}
							new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
							new_component<gui_label_t>("high_clearance_under_bridges_(no_low_bridges)");
							any_permissive = true;
						}
						if (any_permissive) {
							new_component<gui_empty_t>();
							new_component<gui_margin_t>(1, D_V_SPACE<<1);
						}

						// Prohibitibve way constraints
						// (If way has, vehicle must have)
						bool any_prohibitive = false;
						for (uint8 i = 0; i < way_constraints.get_count(); i++)
						{
							if (way_constraints.get_prohibitive(i))
							{
								if (!any_prohibitive) {
									new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(0) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
									new_component<gui_label_t>("\nMAY USE: ", COL_SAFETY); // TODO: Remove "\n" from the translation
								}
								new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
								gui_label_buf_t *lb = new_component<gui_label_buf_t>();
								char tmpbuf[30];
								sprintf(tmpbuf, "Prohibitive %i-%i", veh_type->get_waytype(), i);
								lb->buf().append(translator::translate(tmpbuf));
								lb->update();
								any_prohibitive = true;
							}
						}
						if (any_prohibitive) {
							new_component<gui_empty_t>();
							new_component<gui_margin_t>(1, D_V_SPACE<<1);
						}
					}
					end_table();

					if (veh_type->get_tilting())
					{
						gui_label_buf_t *lb = new_component<gui_label_buf_t>();
						lb->buf().printf("%s: %s", translator::translate("equipped_with"), translator::translate("tilting_vehicle_equipment"));
						lb->update();
						new_component<gui_margin_t>(1, D_V_SPACE << 1);
					}

					// To fix the minimum width of the right table
					new_component<gui_margin_t>((D_BUTTON_WIDTH<<1) + D_H_SPACE,1);
				}
				end_table();
				new_component<gui_fill_t>();
			}
			end_table();
		}
	}
	else {
		add_table(4,1)->set_spacing(scr_size(0,0));
		{
			bt_main_view.pressed      = !is_secondary_view;
			bt_secondary_view.pressed =  is_secondary_view;
			new_component<gui_margin_t>(D_MARGIN_RIGHT);
			new_component<gui_label_t>("switch_spec_display_mode");
			add_component(&bt_main_view);
			add_component(&bt_secondary_view);
		}
		end_table();
		new_component<gui_vehicle_bar_legends_t>();
	}
	set_size(get_min_size());
}

void gui_vehicle_spec_t::build_secondary_view(uint16 current_livery)
{
	const uint16 month_now = world()->get_timeline_year_month();

	add_table(5,1)->set_alignment(ALIGN_TOP);
	{
		new_component<gui_margin_t>(1, 12*(D_LABEL_HEIGHT+4));
		if( veh_type->get_livery_count()>1 ) {
			add_table(1,0);
			{
				new_component<gui_heading_t>("Selectable livery schemes", SYSCOL_LIVERY_SCHEME, SYSCOL_LIVERY_SCHEME, 0)->set_width(proportional_string_width(translator::translate("Selectable livery schemes")) + LINESPACE + D_MARGINS_X + D_BUTTON_PADDINGS_X);

				const uint8 max_rows = 13*D_LABEL_HEIGHT/((gui_convoy_assembler_t::get_grid(veh_type->get_waytype()).y-VEHICLE_BAR_HEIGHT)/2);
				vector_tpl<livery_scheme_t*>* schemes = world()->get_settings().get_livery_schemes();
				uint8 available_liveries=0;
				bool too_many_liveries=false;
				add_table(3,1)->set_alignment(ALIGN_TOP); // livery preview table
				{
					for (uint32 i = 0; i < schemes->get_count(); i++)
					{
						livery_scheme_t* scheme = schemes->get_element(i);
						if( !scheme->is_available(month_now) ) {
							continue;
						}
						if( const char* livery = scheme->get_latest_available_livery(month_now, veh_type) ) {
							available_liveries++;
							if( available_liveries==max_rows+1 ) {
								end_table();
							}
							if( available_liveries==1 || available_liveries==max_rows+1 ) {
								add_table(2,max_rows);
							}

							if (available_liveries > max_rows*2) {
								if (!too_many_liveries) {
									too_many_liveries=true;
									end_table();
									end_table();
									add_table(2,1)->set_alignment(ALIGN_TOP); // extra table
									new_component<gui_label_t>("...");
									add_table(5,0); // name table
								}
							}
							if (!too_many_liveries) {
								new_component<gui_image_t>(veh_type->get_image_id(ribi_t::dir_southwest, goods_manager_t::none, livery), 1, 0, true);
							}
							// UI TODO: draw underline for selected
							new_component<gui_label_t>(scheme->get_name(), (i == current_livery) ? SYSCOL_LIVERY_SCHEME : SYSCOL_TEXT);
						}
					}
					if( available_liveries ) {
						end_table();
					}
				}
				end_table();

				if (!available_liveries) {
					// has livery, but cannot selected
					new_component<gui_label_t>("not available", SYSCOL_TEXT_WEAK);
				}
			}
			end_table();
		}

		new_component<gui_margin_t>(LINEASCENT);

		// UI TODO:
		//if( veh_type->has_available_upgrade(month_now) ) {
		//
		//}

		new_component<gui_fill_t>();
	}
	end_table();

	new_component<gui_margin_t>(1, LINESPACE);

	// Copyright information:
	if (char const* const maker = veh_type->get_copyright()) {
		gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->buf().printf(translator::translate("Constructed by %s"), maker);
		lb->update();
	}

}

bool gui_vehicle_spec_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	bool switch_button = false;
	if( comp==&bt_main_view  &&  is_secondary_view ) {
		is_secondary_view = false;
		switch_button = true;
	}
	else if( comp==&bt_secondary_view  &&  !is_secondary_view ) {
		is_secondary_view = true;
		switch_button = true;
	}
	if( switch_button ) {
		bt_main_view.pressed      = !is_secondary_view;
		bt_secondary_view.pressed =  is_secondary_view;
	}

	return true;
}

scr_size gui_vehicle_spec_t::get_max_size() const
{
	return scr_size(scr_size::inf.w, size.h);
}


gui_vehicles_capacity_info_t::gui_vehicles_capacity_info_t(vector_tpl<const vehicle_desc_t *> *vehs)
{
	vehicles = vehs;
	if (vehicles->get_count()) {
		update_accommodations();
	}
}

void gui_vehicles_capacity_info_t::update_accommodations()
{
	accommodations.clear();
	old_vehicle_count = vehicles->get_count();
	for( uint8 i=0; i < vehicles->get_count(); ++i ) {
		accommodations.add_vehicle_desc(vehicles->get_element(i));
	}
	init_table();
}

void gui_vehicles_capacity_info_t::init_table()
{
	remove_all();
	set_table_layout(3,0);
	if (vehicles->get_count()) {
		accommodations.sort();
		for (auto &acm : accommodations.get_accommodations()) {
			const uint8 g_classes = goods_manager_t::get_classes_catg_index(acm.accommodation.catg_index);
			const goods_desc_t* ware_type = goods_manager_t::get_info_catg_index(acm.accommodation.catg_index);

			// 1: goods category symbol
			new_component<gui_image_t>(ware_type->get_catg_symbol(), 0, ALIGN_CENTER_V, true);

			// 2: fare class name / category name
			if (g_classes>1) {
				// "accommodation" class name
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().printf("[%u] %s", acm.assingned_class+1, translator::translate(acm.accommodation.name) );
				lb->update();
			}
			else {
				// no classes => show the category name
				new_component<gui_label_t>(ware_type->get_catg_name());
			}

			// 3: capacity text
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			lb->buf().append(acm.capacity, 0);
			lb->set_fixed_width(proportional_string_width("88,888"));
			lb->update();
		}
	}
}

void gui_vehicles_capacity_info_t::draw(scr_coord offset)
{
	if (old_vehicle_count!=vehicles->get_count()) {
		update_accommodations();
		set_size(get_min_size());
	}

	gui_aligned_container_t::draw(offset);
}


gui_convoy_assembler_t::gui_convoy_assembler_t(depot_frame_t *frame) :
	convoi(&convoi_pics),
	scrollx_convoi(&cont_convoi, true, false),
	scrolly_convoi_capacity(&cont_convoi_capacity, false, true),
	capacity_info(linehandle_t(), convoihandle_t(), false),
	pas(&pas_vec),
	pas2(&pas2_vec),
	electrics(&electrics_vec),
	locos(&locos_vec),
	waggons(&waggons_vec),
	scrolly_pas(&pas),
	scrolly_pas2(&pas2),
	scrolly_electrics(&electrics),
	scrolly_locos(&locos),
	scrolly_waggons(&waggons)
{
	depot_frame = frame;
	replace_frame = NULL;
}

gui_convoy_assembler_t::gui_convoy_assembler_t(replace_frame_t *frame) :
	convoi(&convoi_pics),
	scrollx_convoi(&cont_convoi, true, false),
	scrolly_convoi_capacity(&cont_convoi_capacity, false, true),
	capacity_info(linehandle_t(), convoihandle_t(), false),
	pas(&pas_vec),
	pas2(&pas2_vec),
	electrics(&electrics_vec),
	locos(&locos_vec),
	waggons(&waggons_vec),
	scrolly_pas(&pas),
	scrolly_pas2(&pas2),
	scrolly_electrics(&electrics),
	scrolly_locos(&locos),
	scrolly_waggons(&waggons)
{
	replace_frame = frame;
	depot_frame = NULL;
}


void gui_convoy_assembler_t::init(waytype_t wt, signed char player_nr, bool electrified)
{
	way_type = wt;
	way_electrified = electrified;
	new_vehicle_length = 0;
	old_veh_type = NULL;
	tile_occupancy.set_new_veh_length(new_vehicle_length);
	livery_scheme_index = world()->get_player(player_nr)->get_favorite_livery_scheme_index((uint8)simline_t::waytype_to_linetype(way_type));
	if (depot_frame || replace_frame) {
		// assemble the UI table basic structure here
		set_table_layout(1,0);
		add_table(2,1)->set_alignment(ALIGN_TOP);
		{
			// top left
			add_table(1,3)->set_margin(scr_size(D_MARGIN_LEFT,0), scr_size(0,0));
			{
				add_table(2,1);
				{
					cont_convoi.set_table_layout(2,1);
					cont_convoi.set_margin(scr_size(0,0), scr_size(get_grid(wt).x/2,0));
					cont_convoi.set_spacing(scr_size(0,0));
					cont_convoi.add_component(&convoi);
					lb_makeup.init("Select vehicles to make up a convoy", scr_coord(0,0), SYSCOL_TEXT_INACTIVE, gui_label_t::centered);
					cont_convoi.add_component(&lb_makeup);
					convoi.set_max_rows(1);
					scrollx_convoi.set_maximize(true);
					add_component(&scrollx_convoi);

					new_component<gui_margin_t>(0, get_grid(wt).y+8);
				}
				end_table();

				// convoy length
				add_table(5,1);
				{
					new_component<gui_label_t>("Fahrzeuge:");
					lb_convoi_count.set_fixed_width(proportional_string_width("888"));
					add_component(&lb_convoi_count);
					lb_convoi_count_fluctuation.set_fixed_width(proportional_string_width("888"));
					lb_convoi_count_fluctuation.set_rigid(true);
					add_component(&lb_convoi_count_fluctuation);
					if (wt != water_wt && wt != air_wt) {
						add_component(&lb_convoi_tiles);
						add_component(&tile_occupancy);
					}
				}
				end_table();

				// convoy specs
				cont_convoi_spec.set_table_layout(1,2);
				gui_aligned_container_t *tbl = cont_convoi_spec.add_table(4,0);
				tbl->set_table_frame(true, true);
				tbl->set_spacing(scr_size(1,1));
				tbl->set_margin(scr_size(2,2), scr_size(2,2));
				{
					cont_convoi_spec.new_component<gui_table_header_t>("Cost:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_cost);
					cont_convoi_spec.new_component<gui_table_header_t>("maintenance", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_maintenance);

					cont_convoi_spec.new_component<gui_table_header_t>("Max. speed:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_table(2,1);
					{
						cont_convoi_spec.add_component(&img_alert_speed);
						//lb_convoi_speed.set_tooltip(tooltip_convoi_speed);
						cont_convoi_spec.add_component(&lb_convoi_speed);
					}
					cont_convoi_spec.end_table();
					cont_convoi_spec.new_component<gui_table_header_t>("Weight:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_weight);

					cont_convoi_spec.new_component<gui_table_header_t>("Power:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_table(2,1);
					{
						img_alert_power.set_image(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(4) : IMG_EMPTY, true);
						cont_convoi_spec.add_component(&img_alert_power);
						cont_convoi_spec.add_component(&lb_convoi_power);
					}
					cont_convoi_spec.end_table();
					cont_convoi_spec.new_component<gui_table_header_t>("Max. axle load:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_axle_load);

					cont_convoi_spec.new_component<gui_table_header_t>("Max. brake force:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_brake_force);
					cont_convoi_spec.new_component<gui_table_header_t>("Way wear factor", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_way_wear);

					cont_convoi_spec.add_component(&lb_convoi_brake_distance,2);
					cont_convoi_spec.new_component<gui_table_header_t>("Rolling resistance:", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
					cont_convoi_spec.add_component(&lb_convoi_rolling_resistance);
				}
				cont_convoi_spec.end_table();
				cont_convoi_spec.set_rigid(true);
				add_component(&cont_convoi_spec);
			}
			end_table();

			// top right
			add_table(1,3)->set_margin(scr_size(0,0), scr_size(D_V_SPACE, 0));
			{
				if( depot_frame ) {
					bt_class_management.init(button_t::roundbox | button_t::flexible, "class_manager");
					bt_class_management.set_tooltip("see_and_change_the_class_assignments");
					if (skinverwaltung_t::open_window) {
						bt_class_management.set_image(skinverwaltung_t::open_window->get_image_id(0));
						bt_class_management.set_image_position_right(true);
					}
					bt_class_management.add_listener(this);
					add_component(&bt_class_management);
				}
				cont_convoi_capacity.set_table_layout(1,0);
				cont_convoi_capacity.set_margin(scr_size(0, D_V_SPACE), scr_size(0, D_V_SPACE));
				{

					if (depot_frame) {
						// "Fare" capacity
						cont_convoi_capacity.add_component(&capacity_info);
						cont_convoi_capacity.new_component<gui_fill_t>(false,true);
					}
					else {
						// "Accommodation" capacity
						cont_convoi_capacity.new_component<gui_vehicles_capacity_info_t>(&vehicles);
					}
				}
				scrolly_convoi_capacity.set_maximize(true);
				add_component(&scrolly_convoi_capacity);
				new_component<gui_margin_t>(D_WIDE_BUTTON_WIDTH,1);
			}
			end_table();
		}
		end_table();
		new_component<gui_border_t>();

		add_table(3,1)->set_margin(scr_size(D_MARGIN_LEFT,0), scr_size(D_MARGIN_RIGHT,0));
		{
			add_table(3,2);
			{
				// mode
				new_component<gui_label_t>("Fahrzeuge:");
				static const char *txt_veh_action[4] = { "anhaengen", "voranstellen", "verkaufen", "Upgrade" };
				action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_veh_action[0]), SYSCOL_TEXT);
				action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_veh_action[1]), SYSCOL_TEXT);
				if (depot_frame) {
					action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_veh_action[2]), SYSCOL_TEXT);
				}
				action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_veh_action[3]), SYSCOL_TEXT);
				action_selector.set_selection(veh_action);
				action_selector.add_listener(this);
				add_component(&action_selector);
				new_component<gui_empty_t>();

				// sort
				new_component<gui_label_t>("hl_txt_sort");
				for (int i = 0; i < vehicle_builder_t::sb_length; i++) {
					sort_by.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::vehicle_sort_by[i]), SYSCOL_TEXT);
				}
				sort_by.add_listener(this);
				sort_by.set_selection(sort_by_action);
				add_component(&sort_by);

				sort_order.init(button_t::sortarrow_state, "");
				sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
				sort_order.pressed = sort_reverse;
				sort_order.add_listener(this);
				add_component(&sort_order);
			}
			end_table();
			new_component<gui_margin_t>(D_H_SPACE);

			// filters
			add_table(1,2);
			{
				add_table(5,1);
				{
					if( skinverwaltung_t::search ) {
						new_component<gui_image_t>(skinverwaltung_t::search->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Filter:"));
					}
					name_filter_input.set_text(name_filter_value, 32);
					name_filter_input.set_search_box(true);
					add_component(&name_filter_input);
					name_filter_input.add_listener(this);
					new_component<gui_margin_t>(D_H_SPACE);

					// goods filter => category?
					new_component<gui_image_t>(skinverwaltung_t::goods->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("clf_chk_waren"));
					vehicle_filter.add_listener(this);
					add_component(&vehicle_filter);
				}
				end_table();

				// checkboxes
				add_table(3, 1);
				{
					bt_show_all.init(button_t::square_state, "Show all");
					bt_show_all.set_tooltip("Show also vehicles that do not match for current action.");
					bt_show_all.pressed = show_all;
					bt_show_all.add_listener(this);
					add_component(&bt_show_all);

					if( world()->use_timeline() ) {
						if( world()->get_settings().get_allow_buying_obsolete_vehicles() ) {
							bt_outdated.init(button_t::square_state, "Show outdated");
							bt_outdated.set_tooltip("Show also vehicles no longer in production.");
							bt_outdated.pressed = show_outdated_vehicles;
							bt_outdated.add_listener(this);
							add_component(&bt_outdated);
						}
						if( world()->get_settings().get_allow_buying_obsolete_vehicles() == 1 ) {
							bt_obsolete.init(button_t::square_state, "Show obsolete");
							bt_obsolete.set_tooltip("Show also vehicles whose maintenance costs have increased due to obsolescence.");
							bt_obsolete.pressed = show_obsolete_vehicles;
							bt_obsolete.add_listener(this);
							add_component(&bt_obsolete);
						}
					}
				}
				end_table();
			}
			end_table();
		}
		end_table();

		// tab
		add_component(&tabs);
		new_component<gui_divider_t>();

		add_table(5,1)->set_margin(scr_size(D_MARGIN_LEFT,0), scr_size(D_MARGIN_RIGHT,0));
		if (depot_frame) {
			lb_too_heavy_notice.set_visible(false);
			lb_too_heavy_notice.init("too heavy", scr_coord(0, 0), SYSCOL_TEXT_STRONG);
			lb_too_heavy_notice.set_rigid(true);
			add_component(&lb_too_heavy_notice);
			new_component<gui_fill_t>();
			// livery selector
			{
				new_component<gui_label_t>("Livery scheme:");
				livery_selector.add_listener(this);
				add_component(&livery_selector);
				lb_livery_counter.set_rigid(true);
				lb_livery_counter.set_color(SYSCOL_LIVERY_SCHEME);
				lb_livery_counter.set_fixed_width(proportional_string_width("(888)"));
				add_component(&lb_livery_counter);
			}
		}
		end_table();

		add_component(&cont_vspec);

		const scr_coord     grid    = get_grid(way_type);
		const scr_coord_val grid_dx = grid.x>>1;

		scr_coord     placement    = get_placement(way_type);
		scr_coord_val placement_dx = grid.x>>2;
		placement.x = placement.x* get_base_tile_raster_width() / 64 + 2;
		placement.y = placement.y* get_base_tile_raster_width() / 64 + 2;

		const scr_coord_val scr_min_h = grid.y + gui_image_list_t::BORDER*2;
		scrolly_pas.set_min_height(scr_min_h);
		scrolly_pas2.set_min_height(scr_min_h);
		scrolly_electrics.set_min_height(scr_min_h);
		scrolly_locos.set_min_height(scr_min_h);
		scrolly_waggons.set_min_height(scr_min_h);
		set_size(get_min_size());
		gui_image_list_t* ilists[] = { &convoi, &pas, &pas2, &electrics, &locos, &waggons };
		for (uint32 i = 0; i < lengthof(ilists); i++) {
			gui_image_list_t* il = ilists[i];
			il->set_grid(scr_coord(i==0 ? grid.x - grid_dx:grid.x, grid.y));
			il->set_placement(scr_coord(placement.x - placement_dx, placement.y));
			il->set_player_nr(player_nr);
			il->add_listener(this);
			// only convoi list gets overlapping images
			placement_dx = 0;
			if (i>0) {
				il->set_max_width(size.w);
			}
		}
	}
}

void gui_convoy_assembler_t::update_tabs()
{
	gui_component_t *old_tab = tabs.get_aktives_tab();
	tabs.clear();

	bool one = false;

	// add only if there are any
	if(  !pas_vec.empty()  ) {
		tabs.add_tab(&scrolly_pas, translator::translate( get_passenger_name(way_type) ) );
		one = true;
	}
	if(  !pas2_vec.empty()  ) {
		tabs.add_tab(&scrolly_pas2, translator::translate( get_passenger2_name(way_type) ) );
		one = true;
	}

	// add only if there are any trolleybuses
	const uint16 shifter = 1 << vehicle_desc_t::electric;
	const bool correct_traction_type = (veh_action==va_sell) || !depot_frame || (shifter & depot_frame->get_depot()->get_traction_types());
	if(  !electrics_vec.empty() && correct_traction_type  ) {
		tabs.add_tab(&scrolly_electrics, translator::translate( get_electrics_name(way_type) ) );
		one = true;
	}

	// add, if waggons are there ...
	if(  !locos_vec.empty() || !waggons_vec.empty()  ) {
		tabs.add_tab(&scrolly_locos, translator::translate( get_zieher_name(way_type) ) );
		one = true;
	}

	// only add, if there are waggons
	if(  !waggons_vec.empty()  ) {
		tabs.add_tab(&scrolly_waggons, translator::translate( get_haenger_name(way_type) ) );
		one = true;
	}

	if(  !one  ) {
		// add passenger as default
		tabs.add_tab(&scrolly_pas, translator::translate( get_passenger_name(way_type) ) );
 	}

	// Look, if there is our old tab present again (otherwise it will be 0 by tabs.clear()).
	for(  uint8 i = 0;  i < tabs.get_count();  i++  ) {
		if(  old_tab == tabs.get_tab(i)  ) {
			// Found it!
			tabs.set_active_tab_index(i);
			break;
		}
	}

	// Update vehicle filter
	vehicle_filter.clear_elements();
	vehicle_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All"), SYSCOL_TEXT);
	vehicle_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Relevant"), SYSCOL_TEXT);

	for(goods_desc_t const* const i : world()->get_goods_list()) {
		vehicle_filter.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(i->get_name()), SYSCOL_TEXT, i->get_catg_symbol());
	}
	if (selected_filter > vehicle_filter.count_elements()) {
		selected_filter = VEHICLE_FILTER_RELEVANT;
	}
	vehicle_filter.set_selection(selected_filter);
}

// free memory: all the image_data_t

gui_convoy_assembler_t::~gui_convoy_assembler_t()
{
	clear_vectors();
}

void gui_convoy_assembler_t::clear_vectors()
{
	vehicle_map.clear();
	clear_ptr_vector(pas_vec);
	clear_ptr_vector(pas2_vec);
	clear_ptr_vector(electrics_vec);
	clear_ptr_vector(locos_vec);
	clear_ptr_vector(waggons_vec);
}


scr_coord gui_convoy_assembler_t::get_placement(waytype_t wt)
{
	if (wt==road_wt) {
		return scr_coord(-20,-25);
	}
	if (wt==water_wt) {
		return scr_coord(-1,-11);
	}
	if (wt==air_wt) {
		return scr_coord(-10,-23);
	}
	return scr_coord(-25,-28);
}

scr_coord gui_convoy_assembler_t::get_grid(waytype_t wt)
{
	scr_coord_val base = (wt == water_wt || wt == air_wt) ? 32 : 24;
	base = base * get_base_tile_raster_width() / 64;
	return scr_coord(base+4, base+4+VEHICLE_BAR_HEIGHT);
}

// Names for the tabs for different depot types, defaults to railway depots
const char * gui_convoy_assembler_t::get_passenger_name(waytype_t wt)
{
	if (wt==road_wt) {
		return "Bus_tab";
	}
	if (wt==water_wt) {
		return "Ferry_tab";
	}
	if (wt==air_wt) {
		return "Flug_tab";
	}
	return "Pas_tab";
}

const char * gui_convoy_assembler_t::get_passenger2_name(waytype_t wt)
{
	if (wt==track_wt || wt==tram_wt || wt==narrowgauge_wt) {
		return "Railcar_tab";
	}
	//if (wt==road_wt) {
	//	return "Bus_tab";
	//}
	return "Pas_tab"; // dummy
}

const char * gui_convoy_assembler_t::get_electrics_name(waytype_t wt)
{
	if (wt==road_wt) {
		return "TrolleyBus_tab";
	}
	return "Electrics_tab";
}

const char * gui_convoy_assembler_t::get_zieher_name(waytype_t wt)
{
	if (wt==road_wt) {
		return "LKW_tab";
	}
	if (wt==water_wt) {
		return "Schiff_tab";
	}
	if (wt==air_wt) {
		return "aircraft_tab";
	}
	return "Lokomotive_tab";
}

const char * gui_convoy_assembler_t::get_haenger_name(waytype_t wt)
{
	if (wt==road_wt) {
		return "Anhaenger_tab";
	}
	if (wt==water_wt) {
		return "Schleppkahn_tab";
	}
	return "Waggon_tab";
}


bool gui_convoy_assembler_t::action_triggered( gui_action_creator_t *comp,value_t p)
{
	if(comp != NULL) {	// message from outside!
		// image list selection here ...
		if (  comp == &convoi  ) {
			image_from_convoi_list( p.i );
		}
		else if(  comp == &pas  ) {
			image_from_storage_list(pas_vec[p.i]);
		}
		else if(  comp == &pas2  ) {
			image_from_storage_list(pas2_vec[p.i]);
		}
		else if(  comp == &electrics  ) {
			image_from_storage_list(electrics_vec[p.i]);
		}
		else if(  comp == &locos  ) {
			image_from_storage_list(locos_vec[p.i]);
		}
		else if(  comp == &waggons  ) {
			image_from_storage_list(waggons_vec[p.i]);
		}
		// convoi filters
		else if(  comp == &bt_outdated  ) {
			show_outdated_vehicles = (show_outdated_vehicles == 0);
			bt_outdated.pressed = show_outdated_vehicles;
			build_vehicle_lists();
		}
		else if(  comp == &bt_obsolete  ) {
			show_obsolete_vehicles = (show_obsolete_vehicles == 0);
			bt_obsolete.pressed = show_obsolete_vehicles;
			build_vehicle_lists();
		}
		else if(  comp == &bt_show_all  ) {
			show_all = (show_all == 0);
			bt_show_all.pressed = show_all;
			build_vehicle_lists();
		}
		else if(  comp == &name_filter_input  ) {
			build_vehicle_lists();
		}
		else if(comp == &action_selector) {
			sint32 selection = replace_frame&&(p.i == va_sell) ? va_upgrade : p.i;
			if ( selection < 0 ) {
				action_selector.set_selection(0);
				selection=0;
			}
			if(  (unsigned)(selection)<= va_upgrade) {
				veh_action=(unsigned)(selection);
				build_vehicle_lists();
			}
		}
		else if( comp==&vehicle_filter ) {
			selected_filter = vehicle_filter.get_selection();
			build_vehicle_lists();
		}
		else if( comp==&sort_by ) {
			build_vehicle_lists();
		}
		else if(  comp==&sort_order  ) {
			sort_reverse = !sort_reverse;
			sort_order.pressed = sort_reverse;
			build_vehicle_lists();
		}
		else if (comp == &bt_class_management)
		{
			convoihandle_t cnv;
			if (depot_frame)
			{
				cnv = depot_frame->get_convoy();
			}
			else if (replace_frame)
			{
				cnv = replace_frame->get_convoy();
			}
			if( cnv.is_bound() ){
				create_win({ 20, 20 }, new vehicle_class_manager_t(cnv), w_info, magic_class_manager + cnv.get_id());
				return true;
			}
			return false;

		}
		else if (comp == &livery_selector)
		{
			sint32 livery_selection = p.i;
			if (livery_selection < 0)
			{
				livery_selector.set_selection(0);
				livery_selection = 0;
			}
			livery_scheme_index = livery_scheme_indices.empty() ? 0 : livery_scheme_indices[livery_selection];
			update_livery();
		}
		else
		{
			return false;
		}
	}
	return true;
}


image_id gui_convoy_assembler_t::get_latest_available_livery_image_id(const vehicle_desc_t *info)
{
	const uint16 month_now = world()->get_timeline_year_month();
	const livery_scheme_t* const scheme = world()->get_settings().get_livery_scheme(livery_scheme_index);
	if(scheme)
	{
		const char* livery = scheme->get_latest_available_livery(month_now, info);
		if(livery)
		{
			return info->get_base_image(livery);
		}
		else
		{
			for(uint32 j = 0; j < world()->get_settings().get_livery_schemes()->get_count(); j ++)
			{
				const livery_scheme_t* const new_scheme = world()->get_settings().get_livery_scheme(j);
				const char* new_livery = new_scheme->get_latest_available_livery(month_now, info);
				if(new_livery)
				{
					return info->get_base_image(new_livery);
				}
			}
			return info->get_base_image();
		}
	}
	return info->get_base_image();
}


int gui_convoy_assembler_t::remove_vehicle_at(uint8 nr, bool execute)
{
	if (!vehicles.get_count() || nr >= vehicles.get_count()) {
		dbg->fatal("gui_convoy_assembler_t::remove_vehicle_at()", "Invalid vehicle index");
	}

	int removed_count = 0;
	// Check backward connections first.
	const vehicle_desc_t *removed_veh = vehicles[nr];
	const vehicle_desc_t *last_veh = nr == 0 ? NULL : vehicles[nr-1];
	uint8 chk_nr = nr;
	uint8 removed_len= 0;

	do {
		removed_veh = vehicles[chk_nr];
		if (execute) {
			vehicles.remove_at(chk_nr);
		}
		else {
			chk_nr++;
			removed_count++;
			removed_len += removed_veh->get_length();
		}
	}
	while (chk_nr < vehicles.get_count()
		&& removed_veh->get_basic_constraint_next()&vehicle_desc_t::intermediate_unique
		&& !vehicles[chk_nr]->can_follow(last_veh)
	);

	// Check frontward connections
	if (nr > 0) {
		chk_nr = nr;
		while (chk_nr > 0 && vehicles[chk_nr - 1]->get_basic_constraint_next()&vehicle_desc_t::intermediate_unique) {
			chk_nr--;
			if (execute) {
				vehicles.remove_at(chk_nr);
			}
			removed_count++;
			removed_len += removed_veh->get_length();
		}
	}

	if (!execute) {
		// update tile occupancy bar
		const uint32 total_vehicles = vehicles.get_count();
		const uint8 new_last_veh_length = (nr == total_vehicles - 1 && total_vehicles > 1) ?
			vehicles[total_vehicles-2]->get_length() :
			vehicles[total_vehicles-1]->get_length();
		tile_occupancy.set_new_veh_length(0 - removed_len, false, new_last_veh_length);
	}

	return removed_count;
}


// add a single vehicle (helper function)
void gui_convoy_assembler_t::add_to_vehicle_list(const vehicle_desc_t *info)
{
	// Check if vehicle should be filtered
	const goods_desc_t *freight = info->get_freight_type();
	// Only filter when required and never filter engines
	if (selected_filter > 0 && info->get_total_capacity() > 0) {
		if (selected_filter == VEHICLE_FILTER_RELEVANT) {
			if(freight->get_catg_index() >= 3) {
				bool found = false;
				for(goods_desc_t const* const i : world()->get_goods_list()) {
					if (freight->get_catg_index() == i->get_catg_index()) {
						found = true;
						break;
					}
				}
				// If no current goods can be transported by this vehicle, don't display it
				if (!found) return;
			}

		}
		else if (selected_filter > VEHICLE_FILTER_RELEVANT) {
			// Filter on specific selected good
			uint32 goods_index = selected_filter - VEHICLE_FILTER_GOODS_OFFSET;
			if (goods_index < world()->get_goods_list().get_count()) {
				const goods_desc_t *selected_good = world()->get_goods_list()[goods_index];
				if (freight->get_catg_index() != selected_good->get_catg_index()) {
					return; // This vehicle can't transport the selected good
				}
			}
		}
	}

	if (livery_scheme_index >= world()->get_settings().get_livery_schemes()->get_count())
	{
		// To prevent errors when loading a game with fewer livery schemes than that just played.
		livery_scheme_index = 0;
	}

	gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t(info->get_name(), get_latest_available_livery_image_id(info));

	// since they come "pre-sorted" for the vehiclebauer, we have to do nothing to keep them sorted
	if (info->get_freight_type() == goods_manager_t::passengers || info->get_freight_type() == goods_manager_t::mail) {
		// Distributing passenger and mail cars to three types of tabs
		if(info->get_engine_type() == vehicle_desc_t::electric) {
			electrics_vec.append(img_data);
		}
		else if ((info->get_waytype() == track_wt || info->get_waytype() == tram_wt || info->get_waytype() == narrowgauge_wt)
				&& (info->get_engine_type() != vehicle_desc_t::unknown && info->get_engine_type() != 255))
		{
			pas2_vec.append(img_data);
		}
		else {
			pas_vec.append(img_data);
		}
	}
	else if(info->get_power() > 0  || (info->get_total_capacity()==0  && (info->get_leader_count() > 0 || info->get_trailer_count() > 0)))
	{
		locos_vec.append(img_data);
	}
	else {
		waggons_vec.append(img_data);
	}
	// add reference to map
	vehicle_map.set(info, img_data);
}



// add all current vehicles
void gui_convoy_assembler_t::build_vehicle_lists()
{
	if (vehicle_builder_t::get_info(way_type).empty()) {
		// there are tracks etc. but now vehicles => do nothing
		// at least initialize some data
		if(replace_frame)
		{
			replace_frame->update_data(); // TODO: recheck
		}
		update_tabs();
		return;
	}
	clear_vectors();

	// we do not allow to built electric vehicle in a depot without electrification (way_electrified)

	const uint16 month_now = world()->get_timeline_year_month();
	vector_tpl<livery_scheme_t*>* schemes = world()->get_settings().get_livery_schemes();

	sort_by_action = sort_by.get_selection();
	vector_tpl<const vehicle_desc_t*> typ_list;

	if(!show_all  &&  veh_action==va_sell && depot_frame) {
		// show only sellable vehicles
		for(vehicle_t* const v : depot_frame->get_depot()->get_vehicle_list()) {
			vehicle_desc_t const* const d = v->get_desc();
			typ_list.append(d);
		}
	}
	else {
		slist_tpl<vehicle_desc_t*> const& tmp_list = vehicle_builder_t::get_info(way_type, sort_by_action);
		for(slist_tpl<vehicle_desc_t*>::const_iterator itr = tmp_list.begin(); itr != tmp_list.end(); ++itr) {
			if( sort_reverse ) {
				typ_list.insert_at(0, *itr);
			}
			else {
				typ_list.append(*itr);
			}
		}
	}

	// use this to show only sellable vehicles
	if(!show_all  &&  veh_action==va_sell && depot_frame) {
		// just list the one to sell
		for(vehicle_desc_t const* const info : typ_list) {
			if (vehicle_map.get(info)) continue;
			add_to_vehicle_list(info);
		}
	}
	else {
		// list only matching ones
		for(vehicle_desc_t const* const info : typ_list) {
			const vehicle_desc_t *veh = NULL;
			if(vehicles.get_count()>0) {
				veh = (veh_action == va_insert) ? vehicles[0] : vehicles[vehicles.get_count()-1];
			}

			// current vehicle
			if ((depot_frame && depot_frame->get_depot()->is_contained(info)) ||
				((way_electrified || info->get_engine_type() != vehicle_desc_t::electric) &&
				(((!info->is_future(month_now)) && (!info->is_retired(month_now))) ||
					(info->is_retired(month_now) && (((show_obsolete_vehicles && info->is_obsolete(month_now)) ||
					(show_outdated_vehicles && (!info->is_obsolete(month_now)))))))))
			{
				// check if allowed
				bool append = true;
				bool upgradeable = true;
				if(!show_all)
				{
					if(veh_action == va_insert)
					{
						append = info->can_lead(veh)  &&  (veh==NULL  ||  veh->can_follow(info));
					}
					else if(veh_action == va_append)
					{
						append = info->can_follow(veh)  &&  (veh==NULL  ||  veh->can_lead(info));
					}
					if(veh_action == va_upgrade)
					{
						vector_tpl<const vehicle_desc_t*> vehicle_list;
						upgradeable = false;

						FOR(vector_tpl<const vehicle_desc_t*>, vehicle, vehicles)
						{
							vehicle_list.append(vehicle);
						}

						if(vehicle_list.get_count() < 1)
						{
							break;
						}

						FOR(vector_tpl<const vehicle_desc_t*>, vehicle, vehicle_list)
						{
							for(uint16 c = 0; c < vehicle->get_upgrades_count(); c++)
							{
								if(vehicle->get_upgrades(c) && (info == vehicle->get_upgrades(c)))
								{
									upgradeable = true;
								}
							}
						}
					}
					else
					{
						if(info->is_available_only_as_upgrade())
						{
							if(depot_frame && !depot_frame->get_depot()->find_oldest_newest(info, false))
							{
								append = false;
							}
							else if(replace_frame)
							{
								append = false;
								convoihandle_t cnv = replace_frame->get_convoy();
								if( cnv.is_bound() ) {
									const uint8 count = cnv->get_vehicle_count();
									for(uint8 i = 0; i < count; i++)
									{
										if(cnv->get_vehicle(i)->get_desc() == info)
										{
											append = true;
											break;
										}
									}
								}
							}
						}
					}
				}
				const bool correct_traction_type = veh_action == va_sell || (replace_frame && info->get_traction_type() & replace_frame->get_traction_types() ) || (depot_frame && (info->get_traction_type() & depot_frame->get_depot()->get_traction_types()));
				const weg_t* way = depot_frame ? world()->lookup(depot_frame->get_depot()->get_pos())->get_weg(depot_frame->get_depot()->get_waytype()) : NULL;
				const bool correct_way_constraint = !way || missing_way_constraints_t(info->get_way_constraints(), way->get_way_constraints()).check_next_tile();
				if(!correct_way_constraint || (!correct_traction_type && (info->get_power() > 0 || (veh_action == va_insert && info->get_leader_count() == 1 && info->get_leader(0) && info->get_leader(0)->get_power() > 0))))
				{
					append = false;
				}
				if((append && (veh_action == va_append || veh_action == va_insert)) || (upgradeable &&  veh_action == va_upgrade) || (show_all && veh_action == va_sell))
				{
					if(  name_filter_value[0]==0  ||  (utf8caseutf8(info->get_name(), name_filter_value)  ||  utf8caseutf8(translator::translate(info->get_name()), name_filter_value))  ) {
						add_to_vehicle_list( info );
					}
				}
			}

			// check livery scheme and build the available livery scheme list
			if (info->get_livery_count()>0)
			{
				for (uint32 i = 0; i < schemes->get_count(); i++)
				{
					livery_scheme_t* scheme = schemes->get_element(i);
					if (scheme->is_available(month_now))
					{
						if(livery_scheme_indices.is_contained(i)){
							continue;
						}
						if (scheme->get_latest_available_livery(month_now, info)) {
							livery_scheme_indices.append(i);
							continue;
						}
					}
				}
			}
		}
		if (depot_frame) {
			livery_selector.clear_elements();
			std::sort(livery_scheme_indices.begin(), livery_scheme_indices.end()); // This function is acting very sluggish
			uint8 j=0;
			FORX(vector_tpl<uint16>, const& i, livery_scheme_indices, ++j) {
				livery_scheme_t* scheme = schemes->get_element(i);
				livery_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(scheme->get_name()), SYSCOL_TEXT);
				if (i==livery_scheme_index) {
					livery_selector.set_selection(j);
				}
			}
		}
	}
DBG_DEBUG("gui_convoy_assembler_t::build_vehicle_lists()", "finally %i passenger vehicle, %i  engines, %i good wagons", pas_vec.get_count(), locos_vec.get_count(), waggons_vec.get_count());

	update_data();
	update_tabs();
}


void gui_convoy_assembler_t::update_convoi()
{
	uint16 highest_axle_load=0;
	uint32 total_power  = 0;
	uint32 total_force  = 0;
	uint32 total_cost   = 0;
	uint32 maint_per_km = 0;
	uint32 maint_per_month = 0;
	double way_wear_factor = 0.0;

	clear_ptr_vector( convoi_pics );
	if (depot_frame) {
		// convoy vehicles should reflect the currently applied livery scheme
		convoihandle_t cnv = depot_frame->get_convoy();
		if(  cnv.is_bound()  &&  cnv->get_vehicle_count() > 0  ) {
			for(  unsigned i=0;  i < cnv->get_vehicle_count();  i++  ) {
				vehicle_t& v = *cnv->get_vehicle(i);
				// UI TODO for v15: Since we can collect data on the degree of deterioration of the vehicle here, we may be able to utilize it.

				gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t(v.get_desc()->get_name(), v.get_base_image() );
				convoi_pics.append(img_data);
			}
		}
	}

	const uint32 number_of_vehicles = vehicles.get_count();
	if (number_of_vehicles) {
		for(  unsigned i=0;  i < number_of_vehicles;  i++  ) {
			// just make sure, there is this vehicle also here!
			const vehicle_desc_t *info=vehicles[i];
			if(  vehicle_map.get( info ) == NULL  ) {
				add_to_vehicle_list( info );
			}

			if( replace_frame ) {
				bool vehicle_available = false;
				image_id image;
				convoihandle_t cnv = replace_frame->get_convoy();
				if( cnv.is_bound() ) {
					for(uint8 n = 0; n < cnv->get_vehicle_count(); n++)
					{
						if(cnv->get_vehicle(n)->get_desc() == info)
						{
							vehicle_available = true;
							image = info->get_base_image(cnv->get_vehicle(n)->get_current_livery());
							break;
						}
					}
				}
				if( !vehicle_available ) {
					const uint16 date = world()->get_timeline_year_month();
					image = vehicles[i]->get_base_image();
					if( const livery_scheme_t* const scheme = world()->get_settings().get_livery_scheme(livery_scheme_index) )
					{
						if( const char* livery = scheme->get_latest_available_livery(date, vehicles[i]) )
						{
							image = vehicles[i]->get_base_image(livery);
						}
						else
						{
							for(uint32 j = 0; j < world()->get_settings().get_livery_schemes()->get_count(); j++)
							{
								const livery_scheme_t* const new_scheme = world()->get_settings().get_livery_scheme(j);
								if( const char* new_livery = new_scheme->get_latest_available_livery(date, vehicles[i]) )
								{
									image = vehicles[i]->get_base_image(new_livery);
									break;
								}
							}
						}
					}
				}

				gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t(info->get_name(), image);
				convoi_pics.append(img_data);
			}

			highest_axle_load = max(highest_axle_load, info->get_axle_load());
			total_cost      += info->get_value();
			total_power     += info->get_power();
			total_force     += info->get_tractive_effort();
			maint_per_km    += info->get_running_cost();
			maint_per_month += info->get_adjusted_monthly_fixed_cost();
			way_wear_factor += (double)info->get_way_wear_factor();
		}

		/* color bars for current convoi: */
		init_convoy_color_bars(&vehicles);

		// update cont_convoi_spec table
		potential_convoy_t convoy(vehicles);
		const vehicle_summary_t &vsum = convoy.get_vehicle_summary();
		const sint32 friction = convoy.get_current_friction();
		const double rolling_resistance = convoy.get_resistance_summary().to_double();
		sint32 allowed_speed = vsum.max_speed;
		sint32 min_weight = vsum.weight;
		sint32 max_weight = vsum.weight + convoy.get_freight_summary().max_freight_weight;
		sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
		sint32 max_speed = min_speed;

		if (way_type != water_wt && way_type != air_wt) {
			tile_occupancy.set_assembling_incomplete((convoi_pics[0]->lcolor == COL_YELLOW || convoi_pics[number_of_vehicles-1]->rcolor == COL_YELLOW) ? true : false);
			tile_occupancy.set_base_convoy_length( vsum.length, vehicles.get_element(number_of_vehicles-1)->get_length() );
		}

		lb_convoi_count.buf().append(number_of_vehicles,0);
		lb_convoi_count.set_color(number_of_vehicles==depot_t::get_max_convoy_length(way_type) ? COL_DANGER : SYSCOL_TEXT);
		lb_convoi_count.update();
		lb_convoi_tiles.buf().printf("%s %i",
			translator::translate("Station tiles:"), vsum.tiles);
		lb_convoi_tiles.update();

		sint64 resale_value = total_cost;
		if (depot_frame) {
			convoihandle_t cnv = depot_frame->get_convoy();
			if (cnv.is_bound())
			{
				resale_value = cnv->calc_sale_value();
				depot_frame->set_resale_value(total_cost, resale_value);
			}
			else {
				depot_frame->set_resale_value();
			}
		}
		lb_convoi_cost.set_color(resale_value == total_cost ? SYSCOL_TEXT : SYSCOL_OUT_OF_PRODUCTION);
		lb_convoi_cost.buf().append_money(total_cost / 100.0);
		lb_convoi_cost.update();

		lb_convoi_maintenance.buf().printf("%1.2f$/km, %1.2f$%s", (double)maint_per_km / 100.0, (double)maint_per_month / 100.0, translator::translate("/mon"));
		lb_convoi_maintenance.update();

		// speed
		//tooltip_convoi_speed.clear();
		PIXVAL col_convoi_speed = min_speed == 0 ? SYSCOL_TEXT_STRONG : SYSCOL_TEXT;
		img_alert_speed.set_visible(false);
		if (min_speed < allowed_speed && min_weight < max_weight && min_speed != 0) {
			uint8 steam_convoy = vehicles.get_element(0)->get_engine_type() == vehicle_desc_t::steam ? 1 : 0;
			if (min_speed < allowed_speed / 2) {
				if (skinverwaltung_t::alerts) {
					img_alert_speed.set_image(skinverwaltung_t::alerts->get_image_id(2 + steam_convoy));
					img_alert_speed.set_visible(true);
				}
				else {
					lb_convoi_speed.set_color(COL_LACK_OF_MONEY);
				}
			}
			else if (min_speed < allowed_speed && !steam_convoy) {
				if (skinverwaltung_t::alerts) {
					img_alert_speed.set_image(skinverwaltung_t::alerts->get_image_id(2));
					img_alert_speed.set_visible(true);
				}
				else {
					lb_convoi_speed.set_color(COL_INTEREST);
				}
			}
		}
		if (min_speed == 0)
		{
			if (convoy.get_starting_force() > 0)
			{
				lb_convoi_speed.buf().printf(" %d -", min_speed);
				//tooltip_convoi_speed.printf(" %d km/h @ %g t: ", min_speed, max_weight * 0.001f);
				//
				max_weight = convoy.calc_max_starting_weight(friction);
				min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
				//
				max_speed = allowed_speed;
				min_weight = convoy.calc_max_weight(friction);
			}
		}
		else if (min_speed < allowed_speed && min_weight < max_weight)
		{
			max_speed = convoy.calc_max_speed(weight_summary_t(min_weight, friction));
			if (min_speed < max_speed)
			{
				if (max_speed == allowed_speed)
				{
					// Show max. weight that can be pulled with max. allowed speed
					min_weight = convoy.calc_max_weight(friction);
				}
			}
		}
		lb_convoi_speed.buf().printf(" %d km/h", min_speed);
		lb_convoi_speed.set_color(col_convoi_speed);
		lb_convoi_speed.update();
		//	tooltip_convoi_speed.printf(
		//	min_speed == max_speed ? " %d km/h @ %g t" : " %d km/h @ %g t %s %d km/h @ %g t",
		//	min_speed, max_weight * 0.001f, translator::translate("..."), max_speed, min_weight * 0.001f);

		// power
		img_alert_power.set_visible(false);
		if (!total_power) {
			if (skinverwaltung_t::alerts) {
				img_alert_power.set_visible(true);
			}
			else {
				lb_convoi_power.set_color(col_convoi_speed);
			}
		}
		else {
			lb_convoi_power.set_color(SYSCOL_TEXT);
		}
		lb_convoi_power.buf().printf("%4d kW, %d kN", total_power, total_force);
		lb_convoi_power.update();

		lb_convoi_brake_force.buf().printf("%4.2f kN", convoy.get_braking_force().to_double() / 1000.0);
		lb_convoi_brake_force.update();

		const sint32 brake_distance_min = convoy.calc_min_braking_distance(weight_summary_t(min_weight, friction), kmh2ms * max_speed).to_sint32();
		const sint32 brake_distance_max = convoy.calc_min_braking_distance(weight_summary_t(max_weight, friction), kmh2ms * max_speed).to_sint32();
		lb_convoi_brake_distance.buf().printf(
			brake_distance_min == brake_distance_max ? translator::translate("brakes from max. speed in %i m") : translator::translate("brakes from max. speed in %i - %i m"),
			brake_distance_min, brake_distance_max);
		lb_convoi_brake_distance.update();

		// weight
		sint32 total_min_weight = vsum.weight + convoy.get_freight_summary().min_freight_weight;
		sint32 total_max_weight = vsum.weight + convoy.get_freight_summary().max_freight_weight;

		// Adjust the number of digits for weight. High precision is required for early small convoys
		int digit = std::to_string(vsum.weight / 1000).length();
		int decimal_digit = 4 - digit > 0 ? 4 - digit : 0;
		lb_convoi_weight.buf().printf("%.*ft", decimal_digit, vsum.weight / 1000.0);
		lb_convoi_weight.buf().append(" ");
		if(  convoy.get_freight_summary().max_freight_weight>0  ) {
			if(convoy.get_freight_summary().min_freight_weight != convoy.get_freight_summary().max_freight_weight) {
				lb_convoi_weight.buf().printf(translator::translate("(%.*f-%.*ft laden)"), decimal_digit, total_min_weight / 1000.0, decimal_digit, total_max_weight / 1000.0 );
				lb_convoi_rolling_resistance.buf().printf("%.3fkN, %.3fkN, %.3fkN", (rolling_resistance * (double)vsum.weight / 1000.0) / number_of_vehicles, (rolling_resistance * (double)total_min_weight / 1000.0) / number_of_vehicles, (rolling_resistance * (double)total_max_weight / 1000.0) / number_of_vehicles);
			}
			else {
				lb_convoi_weight.buf().printf(translator::translate("(%.*ft laden)"), decimal_digit, total_max_weight / 1000.0 );
				lb_convoi_rolling_resistance.buf().printf("%.3fkN, %.3fkN", (rolling_resistance * (double)vsum.weight / 1000.0) / number_of_vehicles, (rolling_resistance * (double)total_max_weight / 1000.0) / number_of_vehicles);
			}
		}
		else {
				lb_convoi_rolling_resistance.buf().printf("%.3fkN", (rolling_resistance * (double)vsum.weight / 1000.0) / number_of_vehicles);
		}
		lb_convoi_weight.update();
		lb_convoi_rolling_resistance.update();
		lb_convoi_axle_load.buf().printf("%d t", highest_axle_load);
		lb_convoi_axle_load.update();

		lb_convoi_way_wear.buf().printf("%.4f", way_wear_factor/10000.0);
		lb_convoi_way_wear.update();
		cont_convoi.set_size(cont_convoi.get_min_size()); // Update the size when the number of vehicles changes
	}

	cont_convoi_capacity.set_size(cont_convoi_capacity.get_min_size());

	if (replace_frame) {
		replace_frame->update_data(); // update money
	}

	build_vehicle_lists();
}


void gui_convoy_assembler_t::update_data()
{
	const vehicle_desc_t *veh = NULL;

	if (vehicles.get_count()) {
		if(veh_action == va_insert) {
			veh = vehicles[0];
		} else if(veh_action == va_append) {
			veh = vehicles[vehicles.get_count()-1];
		}
	}

	const player_t *player = world()->get_active_player();
	const uint16 month_now = world()->get_timeline_year_month();

	for(auto const& i : vehicle_map) {
		vehicle_desc_t const* const    info = i.key;
		gui_image_list_t::image_data_t& img  = *i.value;

		PIXVAL ok_color = info->get_vehicle_status_color();

		img.count = 0;
		img.lcolor = ok_color;
		img.rcolor = ok_color;
		set_vehicle_bar_shape(&img, info);
		if (info->get_upgrades_count()) {
			img.has_upgrade = info->has_available_upgrade(month_now);
		}

		/*
		* color bars for current convoi:
		*  green/green	okay to append/insert
		*  red/red		cannot be appended/inserted
		*  green/yellow	append okay, cannot be end of train
		*  yellow/green	insert okay, cannot be start of train
		*  orange/orange - too expensive
		*  purple/purple available only as upgrade
		*  dark purple/dark purple cannot upgrade to this vehicle
		*/
		if(veh_action == va_insert) {
			if(!info->can_lead(veh)  ||  (veh  &&  !veh->can_follow(info))) {
				img.lcolor = COL_DANGER;
				img.rcolor = COL_DANGER;
			}
			else if(!info->can_follow(NULL)) {
				img.lcolor = COL_CAUTION;
			}
		}
		else if(veh_action == va_append) {
			if(!info->can_follow(veh)  ||  (veh  &&  !veh->can_lead(info))) {
				img.lcolor = COL_DANGER;
				img.rcolor = COL_DANGER;
			}
			else if(!info->can_lead(NULL)) {
				img.rcolor = COL_CAUTION;
			}
		}
		if (veh_action != va_sell)
		{
			// Check whether too expensive
			// @author: jamespetts
			if(img.lcolor == ok_color || img.lcolor == COL_CAUTION)
			{
				// Only flag as too expensive that which could be purchased but for its price.
				if(veh_action != va_upgrade)
				{
					if(!player->can_afford(info->get_value()))
					{
						img.lcolor = COL_LACK_OF_MONEY;
						img.rcolor = COL_LACK_OF_MONEY;
					}
				}
				else
				{
					if(!player->can_afford(info->get_upgrade_price()))
					{
						img.lcolor = COL_LACK_OF_MONEY;
						img.rcolor = COL_LACK_OF_MONEY;
					}
				}
				if(depot_frame && (i.key->get_power() > 0 || (veh_action == va_insert && i.key->get_leader_count() == 1 && i.key->get_leader(0) && i.key->get_leader(0)->get_power() > 0)))
				{
					if(!(i.key->get_traction_type() & depot_frame->get_depot()->get_traction_types()))
					{
						// Do not allow purchasing of vehicle if depot is of the wrong type.
						img.lcolor = COL_DANGER;
						img.rcolor = COL_DANGER;
					}
				}
				const weg_t* way = depot_frame ? world()->lookup(depot_frame->get_depot()->get_pos())->get_weg(depot_frame->get_depot()->get_waytype()) : NULL;
				if (!way && depot_frame)
				{
					// Might be tram depot, using way no. 2.
					const grund_t* gr_depot = world()->lookup(depot_frame->get_depot()->get_pos());
					if (gr_depot)
					{
						way = gr_depot->get_weg_nr(0);
					}
				}
				if(way && !missing_way_constraints_t(i.key->get_way_constraints(), way->get_way_constraints()).check_next_tile())
				{
					// Do not allow purchasing of vehicle if depot is on an incompatible way.
					img.lcolor = COL_DANGER;
					img.rcolor = COL_DANGER;
				} //(highest_axle_load * 100) / weight_limit > 110)

				if(depot_frame &&
					(way &&
					((world()->get_settings().get_enforce_weight_limits() == 2
						&& world()->lookup(depot_frame->get_depot()->get_pos())->get_weg(depot_frame->get_depot()->get_waytype())
						&& i.key->get_axle_load() > world()->lookup(depot_frame->get_depot()->get_pos())->get_weg(depot_frame->get_depot()->get_waytype())->get_max_axle_load())
					|| (world()->get_settings().get_enforce_weight_limits() == 3
						&& (way->get_max_axle_load() > 0 && (i.key->get_axle_load() * 100) / way->get_max_axle_load() > 110)))))

				{
					// Indicate if vehicles are too heavy
					img.lcolor = COL_EXCEED_AXLE_LOAD_LIMIT;
					img.rcolor = COL_EXCEED_AXLE_LOAD_LIMIT;
				}
			}
		}
		else
		{
			// If selling, one cannot buy - mark all purchasable vehicles red.
			img.lcolor = COL_DANGER;
			img.rcolor = COL_DANGER;
		}

		if(veh_action == va_upgrade)
		{
			//Check whether there are any vehicles to upgrade
			img.lcolor = color_idx_to_rgb(COL_DARK_PURPLE);
			img.rcolor = color_idx_to_rgb(COL_DARK_PURPLE);
			vector_tpl<const vehicle_desc_t*> vehicle_list;

			for (auto vehicle : vehicles)
			{
				vehicle_list.append(vehicle);
			}

			for(auto vehicle : vehicles)
			{
				for(uint16 c = 0; c < vehicle->get_upgrades_count(); c++)
				{
					if(vehicle->get_upgrades(c) && (info == vehicle->get_upgrades(c)))
					{
						if(!player->can_afford(info->get_upgrade_price()))
						{
							img.lcolor = COL_LACK_OF_MONEY;
							img.rcolor = COL_LACK_OF_MONEY;
						}
						else
						{
							img.lcolor = COL_SAFETY;
							img.rcolor = COL_SAFETY;
						}
						if(replace_frame != NULL)
						{
							// If we are using the replacing window,
							// vehicle_list is the list of all vehicles in the current convoy.
							// vehicles is the list of the vehicles to replace them with.
							int upgradable_count = 0;

							for(auto vehicle_2 : vehicles)
							{
								for(uint16 k = 0; k < vehicle_2->get_upgrades_count(); k++)
								{
									if(vehicle_2->get_upgrades(k) && (vehicle_2->get_upgrades(k) == info))
									{
										// Counts the number of vehicles in the current convoy that can
										// upgrade to the currently selected vehicle.
										upgradable_count ++;
									}
								}
							}

							if(upgradable_count == 0)
							{
								//There are not enough vehicles left to upgrade.
								img.lcolor = color_idx_to_rgb(COL_DARK_PURPLE);
								img.rcolor = color_idx_to_rgb(COL_DARK_PURPLE);
							}
						}
					}
				}
			}
		}
		else
		{
			if(info->is_available_only_as_upgrade())
			{
				bool purple = false;
				if(depot_frame && !depot_frame->get_depot()->find_oldest_newest(info, false))
				{
						purple = true;
				}
				else if(replace_frame)
				{
					purple = true;
					convoihandle_t cnv = replace_frame->get_convoy();
					if( cnv.is_bound() ) {
						const uint8 count = cnv->get_vehicle_count();
						for(uint8 i = 0; i < count; i++)
						{
							if(cnv->get_vehicle(i)->get_desc() == info)
							{
								purple = false;
								break;
							}
						}
					}
				}
				if(purple)
				{
					img.lcolor = SYSCOL_UPGRADEABLE;
					img.rcolor = SYSCOL_UPGRADEABLE;
				}
			}
		}

DBG_DEBUG("gui_convoy_assembler_t::update_data()","current %s with colors %i,%i",info->get_name(),img.lcolor,img.rcolor);
	}

	if (depot_frame)
	{
		FOR(slist_tpl<vehicle_t *>, const& iter2, depot_frame->get_depot()->get_vehicle_list())
		{
			gui_image_list_t::image_data_t *imgdat=vehicle_map.get(iter2->get_desc());
			// can fail, if currently not visible
			if(imgdat)
			{
				imgdat->count++;
				if(veh_action == va_sell)
				{
					imgdat->lcolor = COL_SAFETY;
					imgdat->rcolor = COL_SAFETY;
				}
			}
		}
		capacity_info.set_convoy(depot_frame->get_depot()->get_convoi(depot_frame->get_icnv()));
	}
}


void gui_convoy_assembler_t::update_livery()
{
	for (auto const& i : vehicle_map) {
		vehicle_desc_t const* const info = i.key;
		if (info->get_livery_count()>1) {
			gui_image_list_t::image_data_t& img = *i.value;
			img.image = get_latest_available_livery_image_id(info);
		}
	}
}


void gui_convoy_assembler_t::image_from_storage_list(gui_image_list_t::image_data_t* image_data)
{
	if(  image_data->lcolor != COL_DANGER  &&  image_data->rcolor != COL_DANGER  ) {
		const convoihandle_t cnv = depot_frame ? depot_frame->get_depot()->get_convoi(depot_frame->get_icnv()) : replace_frame->get_convoy();

		if(  (veh_action==va_insert||veh_action==va_append) &&  cnv.is_bound()  ) {
			const uint32 length = vehicles.get_count() + 1;
			if( length > depot_t::get_max_convoy_length(way_type) ) {
				// Do not add vehicles over maximum length.
				create_win(new news_img("Do not add vehicles over maximum length!"), w_time_delete, magic_none);
				return;
			}
		}

		// Can the action be executed?
		//   Dark orange = too expensive
		//   Purple = available only as upgrade
		//   Grey = too heavy
		if( image_data->rcolor == COL_EXCEED_AXLE_LOAD_LIMIT ||
			image_data->lcolor == COL_EXCEED_AXLE_LOAD_LIMIT ||
			image_data->rcolor == color_idx_to_rgb(COL_DARK_PURPLE) ||
			image_data->lcolor == color_idx_to_rgb(COL_DARK_PURPLE) ||
			image_data->rcolor == SYSCOL_UPGRADEABLE ||
			image_data->lcolor == SYSCOL_UPGRADEABLE )
		{
			return;
		}

		const vehicle_desc_t *info = vehicle_builder_t::get_info(image_data->text);

		if (depot_frame)
		{
			depot_t *depot = depot_frame->get_depot();
			if( !((image_data->lcolor == COL_LACK_OF_MONEY || image_data->rcolor == COL_LACK_OF_MONEY)
				&& veh_action != va_sell
				&& !depot_frame->get_depot()->find_oldest_newest(info, true)) )
			{
				if(  veh_action == va_sell  ) {
					depot->call_depot_tool( 's', convoihandle_t(), image_data->text, livery_scheme_index );
				}
				else {
					if(  !cnv.is_bound()  &&   !depot->get_owner()->is_locked()  ) {
						// adding new convoi, block depot actions until command executed
						// otherwise in multiplayer it's likely multiple convois get created
						// rather than one new convoi with multiple vehicles
						depot->set_command_pending();
					}
					if(veh_action != va_upgrade)
					{
						depot->call_depot_tool( veh_action == va_insert ? 'i' : 'a', cnv, image_data->text, livery_scheme_index );
					}
					else
					{
						depot->call_depot_tool( 'u', cnv, image_data->text, livery_scheme_index );
					}
				}


			}
		}
		else {
			if (!((image_data->lcolor == COL_LACK_OF_MONEY || image_data->rcolor == COL_LACK_OF_MONEY)
				&& veh_action != va_sell))
			{
				if(veh_action == va_upgrade)
				{
					uint8 count;
					uint32 n = 0;
					for (const vehicle_desc_t *vehicle : vehicles)
					{
						(void)vehicle;
						count = vehicles[n]->get_upgrades_count();
						for(int i = 0; i < count; i++)
						{
							if(vehicles[n]->get_upgrades(i) == info)
							{
								vehicles.insert_at(n, info);
								vehicles.remove_at(n+1);
								return;
							}
						}
						n++;
					}
				}
				else
				{
					if(veh_action == va_insert)
					{
						vehicles.insert_at(0, info);
						slist_tpl<const vehicle_desc_t *>new_vehicle_info;
						const vehicle_desc_t *prev_veh = info;
						while (((prev_veh->get_leader_count() == 1 && prev_veh->get_leader(0) != NULL))
							&& !new_vehicle_info.is_contained(prev_veh->get_leader(0))) {
							prev_veh = prev_veh->get_leader(0);
							new_vehicle_info.insert(prev_veh);
							vehicles.insert_at(0, prev_veh);
						}
					}
					else if(veh_action == va_append)
					{
						vehicles.append(info);
						slist_tpl<const vehicle_desc_t *>new_vehicle_info;
						const vehicle_desc_t *next_veh = info;
						while (((next_veh->get_basic_constraint_next()&vehicle_desc_t::intermediate_unique))
							&& !new_vehicle_info.is_contained(next_veh->get_trailer(0))) {
							next_veh = next_veh->get_trailer(0);
							new_vehicle_info.insert(next_veh);
							vehicles.append(next_veh);
						}
					}
				}
				// No action for sell - not available in the replacer window.
			}
			update_convoi();
		}
	}
}


void gui_convoy_assembler_t::image_from_convoi_list(uint nr)
{
	if(depot_frame)
	{
		// We're in an actual depot.
		depot_t* depot;
		depot = depot_frame->get_depot();

		const convoihandle_t cnv = depot->get_convoi(depot_frame->get_icnv());
		if(  cnv.is_bound()  &&   nr < cnv->get_vehicle_count()  ) {
			// we remove all connected vehicles together!
			// find start
			unsigned start_nr = nr;
			while(  start_nr > 0  ) {
				start_nr--;
				const vehicle_desc_t *info = cnv->get_vehicle(start_nr)->get_desc();
				if(  info->get_trailer_count() != 1  ) {
					start_nr++;
					break;
				}
			}

			cbuffer_t start;
			start.append( start_nr );

			depot->call_depot_tool( 'r', cnv, start, livery_scheme_index );
		}
	}
	else
	{
		// We're in a replacer.
		remove_vehicle_at(nr);
		update_convoi();
	}
}



void gui_convoy_assembler_t::set_vehicle_bar_shape(gui_image_list_t::image_data_t *pic, const vehicle_desc_t *v)
{
	if (!v || !pic) {
		dbg->error("void gui_convoy_assembler_t::set_vehicle_bar_shape()", "pass the invalid parameter)");
		return;
	}
	pic->basic_coupling_constraint_prev = v->get_basic_constraint_prev();
	pic->basic_coupling_constraint_next = v->get_basic_constraint_next();
	pic->interactivity = v->get_interactivity();
}

/* color bars for current or upgraded convoi: */
void gui_convoy_assembler_t::init_convoy_color_bars(vector_tpl<const vehicle_desc_t *> *vehs)
{
	if (!vehs->get_count()) {
		dbg->error("void gui_convoy_assembler_t::init_convoy_color_bars()", "Convoy trying to initialize colorbar was null)");
		return;
	}

	const uint16 month_now = world()->get_timeline_year_month();
	uint32 i=0;
	// change green into blue for retired vehicles
	PIXVAL base_col = vehs->get_element(i)->get_vehicle_status_color();
	uint32 end = vehs->get_count();
	set_vehicle_bar_shape(convoi_pics[0], vehs->get_element(0));
	convoi_pics[0]->lcolor = vehs->get_element(0)->can_follow(NULL) ? base_col : COL_CAUTION;
	for (i = 1; i < end; i++) {
		base_col = vehs->get_element(i)->get_vehicle_status_color();
		convoi_pics[i-1]->rcolor = vehs->get_element(i-1)->can_lead(vehs->get_element(i)) ?   base_col : COL_DANGER;
		convoi_pics[i]->lcolor   = vehs->get_element(i)->can_follow(vehs->get_element(i-1)) ? base_col : COL_DANGER;
		set_vehicle_bar_shape(convoi_pics[i], vehs->get_element(i));
	}
	convoi_pics[i-1]->rcolor = vehs->get_element(i-1)->can_lead(NULL) ? base_col : COL_CAUTION;
}


void gui_convoy_assembler_t::set_vehicles(convoihandle_t cnv)
{
	vehicles.clear();
	if (cnv.is_bound()) {
		for (uint8 i=0; i<cnv->get_vehicle_count(); i++) {
			vehicles.append(cnv->get_vehicle(i)->get_desc());
		}
		capacity_info.set_convoy(cnv);

		// check convoy livery
		if (depot_frame) {
			check_livery_scheme_index(cnv->get_livery_scheme_index());
		}
	}
	else {
		capacity_info.set_convoy(convoihandle_t());
	}
	update_convoi();
}


void gui_convoy_assembler_t::set_vehicles(const vector_tpl<const vehicle_desc_t *>* vv)
{
	vehicles.clear();
	vehicles.resize(vv->get_count());  // To save some memory
	for (uint16 i=0; i<vv->get_count(); ++i) {
		vehicles.append((*vv)[i]);
	}
	update_convoi();
}



bool gui_convoy_assembler_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	//if(IS_LEFTCLICK(ev) &&  !action_selector.getroffen( ev->click_pos-16)) {
	//	// close combo box; we must do it ourselves, since the box does not recieve outside events ...
	//	action_selector.close_box();
	//	return true;
	//}

	if(  get_focus()==&name_filter_input  &&  (ev->ev_class == EVENT_KEYBOARD  ||  ev->ev_class == EVENT_STRING)  ) {
		build_vehicle_lists();
		return true;
	}

	return swallowed;
}

void gui_convoy_assembler_t::set_panel_width()
{
	// set width of image-lists
	gui_image_list_t* ilists[] = { &pas, &electrics, &locos, &waggons };
	for (uint32 i = 0; i < lengthof(ilists); i++) {
		gui_image_list_t* il = ilists[i];
		il->set_max_width(size.w);
	}
}


void gui_convoy_assembler_t::draw(scr_coord offset)
{
	if (depot_frame==NULL && replace_frame==NULL) return;

	update_vehicle_info_text(pos+offset);

	const uint8 player_nr = depot_frame ? depot_frame->get_depot()->get_owner_nr() : replace_frame->get_player_nr();
	// show/hide [CONVOI]
	lb_makeup.set_visible(!vehicles.get_count() && (world()->get_active_player_nr()==player_nr));
	cont_convoi_spec.set_visible(vehicles.get_count());
	scrolly_convoi_capacity.set_visible(vehicles.get_count());

	gui_aligned_container_t::draw(offset);
}


void gui_convoy_assembler_t::update_vehicle_info_text(scr_coord pos)
{
	// unmark vehicle
	for(gui_image_list_t::image_data_t* const& iptr : convoi_pics) {
		iptr->count = 0;
	}

	// Find vehicle under mouse cursor
	gui_component_t const* const tab = tabs.get_aktives_tab();
	gui_image_list_t const* const lst =
		tab == &scrolly_pas       ? &pas       :
		tab == &scrolly_pas2      ? &pas2      :
		tab == &scrolly_electrics ? &electrics :
		tab == &scrolly_locos     ? &locos     :
		&waggons;
	const scr_coord mouse_pos = get_mouse_pos();

	//double resale_value = -1.0;
	const vehicle_desc_t *veh_type = NULL;
	bool new_vehicle_length_sb_force_zero = false;
	sint16 convoi_number = -1;
	uint16 selected_livery_index= livery_scheme_index;
	uint32 resale_value = UINT32_MAX_VALUE;
	scr_coord relpos = scr_coord( 0, ((gui_scrollpane_t *)tabs.get_aktives_tab())->get_scroll_y() );
	int sel_index = lst->index_at(pos + tabs.get_pos() - relpos, mouse_pos.x, mouse_pos.y - tabs.get_required_size().h);

	int vehicle_fluctuation = 0;
	// init convoy/station tile count
	if (!vehicles.get_count()) {
		lb_convoi_count.update();
		lb_convoi_tiles.buf().printf("%s", translator::translate("Station tiles:"));
		lb_convoi_tiles.update();
		tile_occupancy.set_base_convoy_length(0,0);
	}

	if(  (sel_index != -1)  &&  (tabs.getroffen(get_mouse_pos() - pos)) ) {
		// cursor over a vehicle in the selection list
		const vector_tpl<gui_image_list_t::image_data_t*>& vec = (lst == &electrics ? electrics_vec : (lst == &pas ? pas_vec : (lst == &pas2 ? pas2_vec : (lst == &locos ? locos_vec : waggons_vec))));
		veh_type = vehicle_builder_t::get_info(vec[sel_index]->text);

		// Updated only if the selected vehicle changes
		if (veh_type != old_veh_type) {
			if(  vec[sel_index]->lcolor == COL_DANGER ||  veh_action == va_sell || veh_action == va_upgrade) {
				// don't show new_vehicle_length_sb when can't actually add the highlighted vehicle, or selling from inventory
				new_vehicle_length_sb_force_zero = true;
			}
			if (depot_frame && vec[sel_index]->count > 0) {
				resale_value = depot_frame->calc_sale_value(veh_type);
			}
			if(vec[sel_index]->lcolor == COL_EXCEED_AXLE_LOAD_LIMIT)
			{
				lb_too_heavy_notice.set_visible(true);
			}
			else{
				lb_too_heavy_notice.set_visible(false);
			}

			// update tile occupancy bar
			if (way_type != water_wt && way_type != air_wt) {
				new_vehicle_length = new_vehicle_length_sb_force_zero ? 0 : veh_type->get_length();
				uint8 auto_addition_length = 0;
				if (!new_vehicle_length_sb_force_zero) {
					auto_addition_length = veh_type->calc_auto_connection_length(!(veh_action == va_insert));
					vehicle_fluctuation += veh_type->get_auto_connection_vehicle_count(!(veh_action == va_insert));
					vehicle_fluctuation++;
					lb_convoi_count.set_visible(true);
				}

				if (vehicle_fluctuation + vehicles.get_count() <= depot_t::get_max_convoy_length(way_type)) {
					tile_occupancy.set_new_veh_length(new_vehicle_length + auto_addition_length, (veh_action == va_insert), new_vehicle_length_sb_force_zero ? 0xFFu : new_vehicle_length);
					if (!new_vehicle_length_sb_force_zero) {
						if (veh_action == va_append && auto_addition_length == 0) {
							tile_occupancy.set_assembling_incomplete(vec[sel_index]->rcolor == COL_YELLOW);
						}
						else if (veh_action == va_insert && auto_addition_length == 0) {
							tile_occupancy.set_assembling_incomplete(vec[sel_index]->lcolor == COL_YELLOW);
						}
						else {
							tile_occupancy.set_assembling_incomplete(false);
						}
					}
					else if(convoi_pics.get_count()){
						tile_occupancy.set_assembling_incomplete((convoi_pics[0]->lcolor == COL_YELLOW || convoi_pics[convoi_pics.get_count() - 1]->rcolor == COL_YELLOW));
					}
				}
			}

			// Search and focus on upgrade targets in convoy
			if (veh_action == va_upgrade && vec[sel_index]->lcolor == COL_SAFETY) {
				convoihandle_t cnv;
				if (depot_frame)
				{
					cnv = depot_frame->get_convoy();
				}
				else if (replace_frame)
				{
					cnv = replace_frame->get_convoy();
				}
				if (cnv.is_bound()) {
					vector_tpl<const vehicle_desc_t*> dummy_vehicles;
					bool found = false;
					for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
						for (uint16 c = 0; c < cnv->get_vehicle(i)->get_desc()->get_upgrades_count(); c++)
						{
							if (veh_type == cnv->get_vehicle(i)->get_desc()->get_upgrades(c)) {
								convoi.set_focus(i);
								found = true;
								break;
							}
						}
						if (found) {
							// Forecast after upgrade.
							dummy_vehicles.clear();
							dummy_vehicles.append(veh_type);
							// forward
							bool continuous_upgrade=true;
							for (int j=i-1; j >= 0; j--) {
								bool found_auto_upgrade = false;
								if (dummy_vehicles[i-j-1]->get_leader_count() != 1 || dummy_vehicles[i-j-1]->get_leader(0) == NULL) {
									continuous_upgrade = false;
								}

								if(dummy_vehicles[i-j-1]->get_leader_count() == 1 && dummy_vehicles[i-j-1]->get_leader(0) != NULL && continuous_upgrade == true)
								{
									for (uint16 c = 0; c < cnv->get_vehicle(j)->get_desc()->get_upgrades_count(); c++)
									{
										if (dummy_vehicles[i-j-1]->get_leader(0) == cnv->get_vehicle(j)->get_desc()->get_upgrades(c)) {
											dummy_vehicles.insert_at(0, (dummy_vehicles[i-j-1]->get_leader(0)));
											found_auto_upgrade = true;
											break;
										}
									}
								}
								if( !found_auto_upgrade ) {
									dummy_vehicles.insert_at(0, (cnv->get_vehicle(j)->get_desc()));
								}
							}
							// backward
							continuous_upgrade = true;
							for (uint8 j = i+1; j < cnv->get_vehicle_count(); j++)
							{
								bool found_auto_upgrade = false;
								if (dummy_vehicles[j-1]->get_trailer_count() != 1 || dummy_vehicles[j-1]->get_trailer(0) == NULL) {
									continuous_upgrade = false;
								}
								if (dummy_vehicles[j-1]->get_trailer_count() == 1 && dummy_vehicles[j-1]->get_trailer(0) != NULL && continuous_upgrade == true)
								{
									for (uint16 c = 0; c < cnv->get_vehicle(j)->get_desc()->get_upgrades_count(); c++)
									{
										if (dummy_vehicles[j-1]->get_trailer(0) == cnv->get_vehicle(j)->get_desc()->get_upgrades(c)) {
											dummy_vehicles.append((dummy_vehicles[j-1]->get_trailer(0)));
											found_auto_upgrade = true;
											break;
										}
									}
								}
								if( !found_auto_upgrade ) {
									dummy_vehicles.append((cnv->get_vehicle(j)->get_desc()));
								}
							}
							break;
						}
					}
					if (found) {
						// Update color bars
						init_convoy_color_bars(&dummy_vehicles);

						uint8 upgrade_count = 0;
						for (uint8 i = 0; i < dummy_vehicles.get_count(); i++) {
							if(cnv->get_vehicle(i)->get_desc() != dummy_vehicles[i])
							{
								//convoi.set_focus(i);
								if (convoi_pics[i]->lcolor == COL_SAFETY) { convoi_pics[i]->lcolor = SYSCOL_UPGRADEABLE; }
								if (convoi_pics[i]->rcolor == COL_SAFETY) { convoi_pics[i]->rcolor = SYSCOL_UPGRADEABLE; }
								upgrade_count++;
							}
						}
						// display upgrade counter
						lb_convoi_count_fluctuation.buf().printf("(%hu)", (uint16)upgrade_count);
						lb_convoi_count_fluctuation.set_visible(true);
						lb_convoi_count_fluctuation.set_color(SYSCOL_UPGRADEABLE);
						lb_convoi_count_fluctuation.update();
					}
				}
			}
			else {
				convoi.set_focus(-1);
				if (vehicles.get_count()) {
					init_convoy_color_bars(&vehicles);
				}
			}
		}
	}
	else {
		lb_too_heavy_notice.set_visible(false);
		// cursor over a vehicle in the convoi
		relpos = scr_coord(scrollx_convoi.get_scroll_x(), 0);

		//sel_index = convoi.index_at(pos, x, y);
		convoi_number = sel_index = convoi.index_at(pos - relpos + scrollx_convoi.get_pos(), mouse_pos.x, mouse_pos.y);
		if(  sel_index != -1  ) {
			if (depot_frame)
			{
				convoihandle_t cnv = depot_frame->get_convoy();
				if(cnv.is_bound())
				{
					veh_type = cnv->get_vehicle( sel_index )->get_desc();
					selected_livery_index = cnv->get_livery_scheme_index();

					// mark selected vehicle in convoi
					convoi_pics[convoi_number]->count = cnv->get_car_numbering(sel_index);

					// Updated only if the selected vehicle changes
					if (veh_type != old_veh_type) {
						resale_value = cnv->get_vehicle( sel_index )->calc_sale_value();

						if (way_type != water_wt && way_type != air_wt) {
							if (cnv->get_vehicle_count() == 1) {
								// "Extra margin" needs to be removed
								tile_occupancy.set_new_veh_length(0 - cnv->get_length(), false, 0);
								vehicle_fluctuation = -1;
							}
							else {
								// update tile occupancy bar
								const uint8 new_last_veh_length = (sel_index == cnv->get_vehicle_count()-1 && cnv->get_vehicle_count() > 1) ?
									cnv->get_vehicle(cnv->get_vehicle_count() - 2)->get_desc()->get_length() :
									cnv->get_vehicle(cnv->get_vehicle_count() - 1)->get_desc()->get_length();
								// check auto removal
								const uint8 removal_len = cnv->calc_auto_removal_length(sel_index) ? cnv->calc_auto_removal_length(sel_index) : veh_type->get_length();
								vehicle_fluctuation = 0 - cnv->get_auto_removal_vehicle_count(sel_index);
								// TODO: Auto continuous removal may have incorrect last vehicle length

								tile_occupancy.set_new_veh_length(0 - removal_len, false, new_last_veh_length);
							}
							tile_occupancy.set_assembling_incomplete(false);
						}
					}
				}
				else {
					//resale_value = 0;
					vehicle_fluctuation = 0;
					veh_type = NULL;
					tile_occupancy.set_new_veh_length(0);
				}
			}
			else {
				veh_type =vehicles[sel_index];
				// update station tiles info
				if (way_type != water_wt && way_type != air_wt) {
					vehicle_fluctuation = -remove_vehicle_at(sel_index, false);
				}
			}

		}
	}

	if (veh_type != old_veh_type) {
		old_veh_type = veh_type;

		if( veh_type==NULL ) {
			// nothing select, initialize
			old_veh_type = NULL;
			new_vehicle_length = 0;
			lb_too_heavy_notice.set_visible(false);
			lb_livery_counter.set_visible(false);
			if (way_type != water_wt && way_type != air_wt) {
				tile_occupancy.set_new_veh_length(0);
				vehicle_fluctuation = 0;
				if (depot_frame || replace_frame)
				{
					if (convoi_pics.get_count())
					{
						tile_occupancy.set_assembling_incomplete((convoi_pics[0]->lcolor == COL_YELLOW || convoi_pics[convoi_pics.get_count()-1]->rcolor == COL_YELLOW) ? true : false);
					}
				}
			}
			convoi.set_focus(-1);
			if(  veh_action==va_upgrade  && vehicles.get_count() ) {
				init_convoy_color_bars(&vehicles);
			}
			cont_vspec.set_vehicle(NULL);
		}
		else {
			// update vehicle text
			if (convoi_number!=-1 && (veh_action!=va_upgrade)) {
				const uint16 month_now = world()->get_timeline_year_month();
				if (veh_action == va_upgrade && veh_type->has_available_upgrade(month_now)) {
					cont_vspec.set_vehicle(veh_type, veh_action, selected_livery_index); // upgrade mode
				}
				else {
					// show Resale value:
					cont_vspec.set_vehicle(veh_type, veh_action, selected_livery_index, resale_value);
				}
			}
			else {
				cont_vspec.set_vehicle(veh_type, (veh_type->is_available_only_as_upgrade()&& veh_action!=va_sell) ? va_upgrade : veh_action, selected_livery_index, resale_value);
			}

			if (depot_frame) {
				if (veh_type->get_livery_count() > 0) {
					lb_livery_counter.buf().printf("(%i)", veh_type->get_available_livery_count(world()));
					lb_livery_counter.set_visible(true);
				}
				else {
					lb_livery_counter.set_visible(false);
				}
				lb_livery_counter.update();
			}
		}
		if (way_type != water_wt && way_type != air_wt) {
			lb_convoi_count_fluctuation.set_visible(false);
			if(  vehicle_fluctuation!=0  &&  (vehicle_fluctuation+vehicles.get_count() <= depot_t::get_max_convoy_length(way_type))  ) {
				// vehicle number fluctuation counter
				lb_convoi_count_fluctuation.buf().printf("%s%i", vehicle_fluctuation > 0 ? "+" : "", vehicle_fluctuation);
				lb_convoi_count_fluctuation.set_color(vehicle_fluctuation > 0 ? SYSCOL_UP_TRIANGLE : SYSCOL_DOWN_TRIANGLE);
				lb_convoi_count_fluctuation.update();
				lb_convoi_count_fluctuation.set_visible(true);
			}
		}
	}
}


void gui_convoy_assembler_t::set_electrified( bool ele )
{
	if( way_electrified != ele )
	{
		way_electrified = ele;
		build_vehicle_lists();
	}
}


void gui_convoy_assembler_t::check_livery_scheme_index(uint16 test_index)
{
	bool found = false;
	if (test_index != UINT16_MAX && test_index != livery_scheme_index && livery_scheme_indices.is_contained(test_index)) {
		const uint32 sel = livery_scheme_indices.index_of(test_index);
		if( sel!=0xFFFFFFFFu ) {
			livery_scheme_index = test_index;
			livery_selector.set_selection(sel);
			update_livery();
			found = true;
		}
	}
	if (!found && depot_frame) {
		const uint32 fav_index = depot_frame->get_depot()->get_owner()->get_favorite_livery_scheme_index((uint8)simline_t::waytype_to_linetype(way_type));
		if (livery_scheme_index!=fav_index) {
			const uint32 sel = livery_scheme_indices.index_of(fav_index);
			if( sel!=0xFFFFFFFFu ) {
				livery_scheme_index = fav_index;
				livery_selector.set_selection(sel);
				update_livery();
			}
		}
	}
}


//#define VEHICLE_BAR_COLORS 9
/*
static const PIXVAL bar_colors[] =
{
	COL_SAFETY,
	COL_CAUTION,
	SYSCOL_OUT_OF_PRODUCTION,
	SYSCOL_OBSOLETE,
	COL_DANGER,
	COL_LACK_OF_MONEY,
	COL_EXCEED_AXLE_LOAD_LIMIT,
	SYSCOL_UPGRADEABLE,
	COL_UPGRADE_RESTRICTION
};*/

static const char bar_color_helptexts[][64] =
{
	"hlptxt_vh_normal_state",
	"need to connect vehicle",
	"out of production",
	"become obsolete",
	"cannot select",
	"lack of money",
	"axle weight is excessive",
	"appears only in upgrades",
	"cannot upgrade"
};

gui_vehicle_bar_legends_t::gui_vehicle_bar_legends_t()
{
	set_table_layout(1,0);
	set_spacing( scr_size(D_H_SPACE,2) );
	new_component<gui_margin_t>(0,LINESPACE);

	new_component<gui_label_t>("Vehicle bar shape legend:");
	add_table(6,0)->set_spacing( scr_size(D_H_SPACE,1) );
	{
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY)->set_flags(vehicle_desc_t::can_be_head, vehicle_desc_t::can_be_tail, 0);
		new_component<gui_label_t>("helptxt_one_direction", SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>("helptxt_powered_vehicle", SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY)->set_flags(vehicle_desc_t::can_be_head, vehicle_desc_t::can_be_head|vehicle_desc_t::can_be_tail, 1);
		new_component<gui_label_t>("helptxt_cab_head", SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 0);
		new_component<gui_label_t>("helptxt_unpowered_vehicle", SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY)->set_flags(vehicle_desc_t::can_be_tail, vehicle_desc_t::can_be_tail, 1);
		new_component<gui_label_t>("helptxt_can_be_at_rear", SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_image_t>(skinverwaltung_t::upgradable ? skinverwaltung_t::upgradable->get_image_id(1):IMG_EMPTY, 0, ALIGN_NONE, true);
		new_component<gui_label_t>("Upgrade available", skinverwaltung_t::upgradable ? SYSCOL_TEXT_WEAK : SYSCOL_UPGRADEABLE);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY)->set_flags(0, 0, 0);
		new_component<gui_label_t>("helptxt_intermediate", SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_image_t>(skinverwaltung_t::upgradable ? skinverwaltung_t::upgradable->get_image_id(0):IMG_EMPTY, 0, ALIGN_NONE, true);
		new_component<gui_label_t>(world()->get_settings().get_show_future_vehicle_info() ? "Upgrade is not available yet" : "Upgrade will be available in the near future", SYSCOL_TEXT_WEAK)->set_visible(skinverwaltung_t::upgradable);
	}
	end_table();

	new_component<gui_divider_t>();

	add_table(6,0)->set_spacing( scr_size(D_H_SPACE,1) );
	{
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_SAFETY, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[0], SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_LACK_OF_MONEY, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[5], SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_CAUTION, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[1], SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_EXCEED_AXLE_LOAD_LIMIT, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[6], SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(SYSCOL_OUT_OF_PRODUCTION, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[2], SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(SYSCOL_UPGRADEABLE, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[7], SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(SYSCOL_OBSOLETE, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[3], SYSCOL_TEXT_WEAK);
		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_UPGRADE_RESTRICTION, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[8], SYSCOL_TEXT_WEAK);

		new_component<gui_margin_t>(D_MARGIN_LEFT);
		new_component<gui_vehicle_bar_t>(COL_DANGER, scr_size(VEHICLE_BAR_HEIGHT*2-2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, 2/*has_power*/);
		new_component<gui_label_t>(bar_color_helptexts[4], SYSCOL_TEXT_WEAK);
	}
	end_table();

	if (skinverwaltung_t::comfort) {
		new_component<gui_divider_t>();
		add_table(4,1)->set_spacing(scr_size(D_H_SPACE, 1));
		{
			new_component<gui_margin_t>(D_MARGIN_LEFT);
			new_component<gui_image_t>(skinverwaltung_t::comfort ? skinverwaltung_t::comfort->get_image_id(0) : IMG_EMPTY, 0, ALIGN_NONE, true);
			new_component<gui_label_t>("(Max. comfortable journey time: ", SYSCOL_TEXT_WEAK);
			new_component<gui_fill_t>();
		}
		end_table();
	}
	else {
		new_component<gui_margin_t>(1, D_LABEL_HEIGHT);
	}

	// Margin for adjusting to the height of the spac table
	new_component<gui_margin_t>(1, D_LABEL_HEIGHT*2+4*12);

	set_size(get_min_size());
}
