/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "convoi_detail_t.h"
#include "components/gui_chart.h"
#include "components/gui_colorbox.h"
#include "components/gui_image.h"
#include "components/gui_divider.h"
#include "components/gui_schedule_item.h"
#include "components/gui_table.h"
#include "components/gui_vehicle_cargoinfo.h"

#include "../simconvoi.h"
#include "../simcolor.h"
#include "../simunits.h"
#include "../simworld.h"
#include "../simline.h"
#include "../simhalt.h"

#include "../dataobj/environment.h"
#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/loadsave.h"

#include "../player/simplay.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

#include "simwin.h"

#include "vehicle_class_manager.h"

#include "../display/simgraph.h"
#include "../display/viewport.h"

#include "../bauer/vehikelbauer.h"


#define CHART_HEIGHT (100)
#define L_COL_ACCEL_FULL COL_ORANGE_RED
#define L_COL_ACCEL_EMPTY COL_DODGER_BLUE

sint16 convoi_detail_t::tabstate = -1;

class convoy_t;

static const uint8 physics_curves_color[MAX_PHYSICS_CURVES] =
{
	COL_WHITE,
	COL_GREEN-1,
	L_COL_ACCEL_FULL,
	L_COL_ACCEL_EMPTY,
	COL_PURPLE+1,
	COL_DARK_SLATEBLUE
};

static const uint8 curves_type[MAX_PHYSICS_CURVES] =
{
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::FORCE,
	gui_chart_t::FORCE
};

static const gui_chart_t::chart_marker_t marker_type[MAX_PHYSICS_CURVES] = {
	gui_chart_t::none, gui_chart_t::cross, gui_chart_t::square, gui_chart_t::diamond,
	gui_chart_t::diamond, gui_chart_t::cross
};

static const char curve_name[MAX_PHYSICS_CURVES][64] =
{
	"",
	"Acceleration(actual)",
	"Acceleration(full load)",
	"Acceleration(empty)",
	"Tractive effort",
	"Running resistance"
};

static char const* const chart_help_text[] =
{
	"",
	"helptxt_actual_acceleration",
	"Acceleration graph when nothing is loaded on the convoy",
	"helptxt_vt_graph_full_load",
	"helptxt_fv_graph_tractive_effort",
	"Total force acting in the opposite direction of the convoy"
};

static char const* const txt_loaded_display[4] =
{
	"Hide loaded detail",
	"Unload halt",
	"Destination halt",
	"Final destination"
};

static char const* const txt_spec_modes[gui_convoy_spec_table_t::MAX_SPEC_TABLE_MODE] =
{
	"principal_spec",
	"payload_spec",
	"maintenance_spec",
	"equipments"
};

static char const* const spec_table_first_col_text[gui_convoy_spec_table_t::MAX_SPECS] =
{
	"Car no.",
	"",
	"Freight",
	"engine_type",
	"Range",
	//"replenishment_seconds",
	"Power:",
	"Gear:",
	"Tractive force:",
	//"Fuel per km",
	"Rated speed",
	"Max. speed:",
	"curb_weight",
	"Max. gross weight",
	"Axle load:",
	"Length(debug):",
	"Max. brake force:",
	//"Payload",
	//"Comfort",
	"car_age_in_month",
	"can_upgrade",
	"Running costs per km", // "Operation" Vehicle running costs per km
	"Fixed cost per month", // Vehicle maintenance costs per month
	"Restwert:",
	"Too tall for low bridges",
	"tilting_vehicle_equipment"/*,
	"maintenance_interval",
	"max_overhauls_interval",
	"initial_overhaul_cost",
	"Staffing Needs"*/
};

static char const* const payload_table_first_col_text[gui_convoy_spec_table_t::MAX_PAYLOAD_ROW] =
{
	"mixed_load_prohibition",
	"Catering level",
	"travelling post office",
	"min_loading_time",
	"max_loading_time"
};

static scr_coord_val spec_table_first_col_width = LINEASCENT*7;





// helper class
gui_acceleration_label_t::gui_acceleration_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x=D_H_SPACE;
	cbuffer_t buf;
	lazy_convoy_t &convoy = *cnv.get_rep();
	const sint32 friction = convoy.get_current_friction();
	const double starting_acceleration = convoy.calc_acceleration(weight_summary_t(cnv->get_weight_summary().weight, friction), 0).to_double() / 1000.0;
	const double starting_acceleration_max = convoy.calc_acceleration(weight_summary_t(cnv->get_vehicle_summary().weight, friction), 0).to_double() / 1000.0;
	const double starting_acceleration_min = convoy.calc_acceleration(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), 0).to_double() / 1000.0;

	buf.clear();
	if (starting_acceleration_min == starting_acceleration_max || starting_acceleration_max == 0.0) {
		buf.printf("%.2f km/h/s", starting_acceleration_min);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, starting_acceleration_min == 0.0 ? COL_DANGER : SYSCOL_TEXT, true);
	}
	else {
		if (!cnv->in_depot()) {
			// show actual value
			buf.printf("%.2f km/h/s", starting_acceleration);
			total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_x += D_H_SPACE;
			total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, "(", ALIGN_LEFT, SYSCOL_TEXT, true);
		}
		buf.clear();
		buf.printf("%.2f", starting_acceleration_min);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL-1), true);

		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		buf.printf("%.2f km/h/s", starting_acceleration_max);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY-1), true);
		if (!cnv->in_depot()) {
			total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, ")", ALIGN_LEFT, SYSCOL_TEXT, true);
		}
	}

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


gui_acceleration_time_label_t::gui_acceleration_time_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_time_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x = D_H_SPACE;
	cbuffer_t buf;
	vector_tpl<const vehicle_desc_t*> vehicles;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicles.append(cnv->get_vehicle(i)->get_desc());
	}
	potential_convoy_t convoy(vehicles);
	const sint32 friction = convoy.get_current_friction();
	const sint32 max_weight = convoy.get_vehicle_summary().weight + convoy.get_freight_summary().max_freight_weight;
	const sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
	const double min_time = convoy.calc_acceleration_time(weight_summary_t(cnv->get_vehicle_summary().weight, friction), min_speed);
	const double max_time = convoy.calc_acceleration_time(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), min_speed);

	buf.clear();
	if (min_time == 0.0) {
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, translator::translate("Unreachable"), ALIGN_LEFT, COL_DANGER, true);
	}
	else if (min_time == max_time) {
		buf.printf(translator::translate("%.2f sec"), min_time);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
	}
	else {
		buf.printf(translator::translate("%.2f sec"), min_time);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY-1), true);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		if (max_time == 0.0) {
			buf.append(translator::translate("Unreachable"));
		}
		else {
			buf.printf(translator::translate("%.2f sec"), max_time);
		}
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL-1), true);
	}
	total_x += D_H_SPACE;

	buf.clear();
	buf.printf(translator::translate("@ %d km/h"), min_speed);
	total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), true);

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


gui_acceleration_dist_label_t::gui_acceleration_dist_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_dist_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x = D_H_SPACE;
	cbuffer_t buf;
	vector_tpl<const vehicle_desc_t*> vehicles;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicles.append(cnv->get_vehicle(i)->get_desc());
	}
	potential_convoy_t convoy(vehicles);
	const sint32 friction = convoy.get_current_friction();
	const sint32 max_weight = convoy.get_vehicle_summary().weight + convoy.get_freight_summary().max_freight_weight;
	const sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
	const uint32 min_distance = convoy.calc_acceleration_distance(weight_summary_t(cnv->get_vehicle_summary().weight, friction), min_speed);
	const uint32 max_distance = convoy.calc_acceleration_distance(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), min_speed);

	buf.clear();
	if (min_distance == 0.0) {
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, translator::translate("Unreachable"), ALIGN_LEFT, COL_DANGER, true);
	}
	else if (min_distance == max_distance) {
		buf.printf(translator::translate("%u m"), min_distance);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
	}
	else {
		buf.printf(translator::translate("%u m"), min_distance);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY - 1), true);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		if (max_distance == 0.0) {
			buf.append(translator::translate("Unreachable"));
		}
		else {
			buf.printf(translator::translate("%u m"), max_distance);
		}
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL - 1), true);
	}

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


gui_convoy_spec_table_t::gui_convoy_spec_table_t(convoihandle_t c)
{
	cnv = c;
	set_table_layout(1,0);
	set_margin(scr_size(D_H_SPACE,0), scr_size(D_H_SPACE,0));
	set_spacing(scr_size(D_H_SPACE, D_V_SPACE/2));

	// First, calculate the width of the first column
	for (uint8 i=SPEC_FREIGHT_TYPE; i<MAX_SPECS; i++) {
		spec_table_first_col_width = max(spec_table_first_col_width, proportional_string_width(spec_table_first_col_text[i]));
	}

	if( cnv.is_bound() ) {
		update_seed = cnv->get_vehicle_count() + world()->get_current_month() + cnv->get_current_schedule_order() + spec_table_mode + show_sideview;
		update();
	}
}

void gui_convoy_spec_table_t::update()
{
	remove_all();

	gui_aligned_container_t *tbl = add_table(cnv->get_vehicle_count()+1+(cnv->get_vehicle_count()>1), 0);
	tbl->set_alignment(ALIGN_CENTER_H|ALIGN_CENTER_V);
	tbl->set_margin(scr_size(3,3),scr_size(3,3));
	tbl->set_table_frame(true, true);

	for (uint8 i=0; i < SPECS_DETAIL_START; i++) {
		for (uint8 j=0; j < cnv->get_vehicle_count()+1+(cnv->get_vehicle_count()>1); j++) {
			// Row label
			if (j == 0) {
				new_component<gui_table_header_t>(spec_table_first_col_text[i], i==0 ? SYSCOL_TH_BACKGROUND_TOP:SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(spec_table_first_col_width);
				continue;
			}

			buf.clear();
			// Convoy total value. Displayed only when there are two or more vehicles
			if( j == cnv->get_vehicle_count()+1 ) {
				switch (i) {
					case SPEC_CAR_NUMBER:
					{
						gui_table_header_buf_t *th = new_component<gui_table_header_buf_t>();
						th->buf().append(translator::translate("convoy_value"));
						th->update();
						break;
					}
					default:
					{
						gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM, gui_label_t::centered);
						td->update();
						break;
					}
				}
				continue;
			}

			const vehicle_desc_t *veh_type = cnv->get_vehicle(j-1)->get_desc();
			const bool reversed = cnv->get_vehicle(j-1)->is_reversed();

			// Whether the cell component is in buf text format or not?
			if (i == SPEC_ROLE) {
				const uint16 month_now = world()->get_timeline_year_month();
				add_table(1,3)->set_alignment(ALIGN_CENTER_H);
				{
					if( show_sideview ) {
						image_id side_view = veh_type->get_image_id(reversed ? ribi_t::dir_northeast : ribi_t::dir_southwest, goods_manager_t::none, cnv->get_vehicle(j - 1)->get_current_livery());
						new_component<gui_fill_t>(false, true);
						new_component<gui_image_t>(side_view, cnv->get_owner()->get_player_nr(), 0, true);
					}

					const PIXVAL veh_bar_color = veh_type->is_obsolete(month_now) ? SYSCOL_OBSOLETE : (veh_type->is_future(month_now) || veh_type->is_retired(month_now)) ? SYSCOL_OUT_OF_PRODUCTION : COL_SAFETY;
					new_component<gui_vehicle_bar_t>(veh_bar_color, scr_size(D_LABEL_HEIGHT*4, max(GOODS_COLOR_BOX_HEIGHT,LINEASCENT-2)))->set_flags(veh_type->get_basic_constraint_prev(reversed), veh_type->get_basic_constraint_next(reversed), veh_type->get_interactivity());
				}
				end_table();

			}
			else {

				switch(i) {
					case SPEC_CAR_NUMBER:
					{
						gui_table_header_buf_t *th = new_component<gui_table_header_buf_t>();
						const sint16 car_number = cnv->get_car_numbering(j-1);
						if (car_number < 0) {
							th->buf().printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number));
						}
						else {
							th->buf().append(car_number, 0);
						}
						th->set_color(veh_type->has_available_upgrade(world()->get_current_month()) == 2 ? SYSCOL_UPGRADEABLE : SYSCOL_TH_TEXT_TOP);
						th->update();
						break;
					}
					default:
					{
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
						lb->buf().append("-");
						lb->update();
						break;
					}
				}
			}
		}
	}
	switch (spec_table_mode) {
		case SPEC_TABLE_PAYLOAD:
			insert_payload_rows();
			break;
		case SPEC_TABLE_MAINTENANCE:
			insert_maintenance_rows();
			break;
		case SPEC_TABLE_CONSTRAINTS:
			insert_constraints_rows();
			break;
		default:
		case SPEC_TABLE_PHYSICS:
			insert_spec_rows();
			break;
	}
	end_table();
	new_component<gui_fill_t>(false, true);

	set_size(get_min_size());
}

void gui_convoy_spec_table_t::insert_spec_rows()
{
	for (uint8 i=SPECS_DETAIL_START; i < SPECS_DETAIL_END; i++) {
#ifndef DEBUG
		if (i==SPEC_LENGTH) {
			continue;
		}
#endif

		new_component<gui_table_header_t>(spec_table_first_col_text[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(spec_table_first_col_width);
		for (uint8 j=0; j < cnv->get_vehicle_count(); j++) {

			const vehicle_desc_t *veh_type = cnv->get_vehicle(j)->get_desc();
			//const bool reversed = cnv->get_vehicle(j)->is_reversed();

			// Whether the cell component is in buf text format or not?
			if (i == SPEC_FREIGHT_TYPE) {
				new_component<gui_image_t>()->set_image((veh_type->get_total_capacity() || veh_type->get_overcrowded_capacity()) ? veh_type->get_freight_type()->get_catg_symbol() : IMG_EMPTY, true);
			}
			else {
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);

				switch (i) {
				case SPEC_ENGINE_TYPE:
					if (veh_type->get_engine_type() == vehicle_desc_t::unknown) {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					else {
						const uint8 et = (uint8)veh_type->get_engine_type()+1;
						lb->buf().printf("%s", translator::translate( vehicle_builder_t::engine_type_names[et] ));
					}
					break;
				case SPEC_RANGE:
					if( veh_type->get_range()==0 ) {
						lb->buf().append(translator::translate("unlimited"));
					}
					else {
						lb->buf().printf("%u km", veh_type->get_range());
					}
					break;
				//case SPEC_REPLENSHMENT_SEC:
					//if( veh_type->get_range()!=0 ) {
					//	char time_as_clock[32];
					//	world()->sprintf_ticks(time_as_clock, sizeof(time_as_clock), veh_type->get_replenishment_seconds());
					//  //gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
					//	lb->buf().append(time_as_clock);
					//	lb->update();
					//}
					//else {
					//	lb->buf().append("-");
					//	lb->set_color(SYSCOL_TEXT_INACTIVE);
					//}
					//break;
				case SPEC_POWER:
					if( !veh_type->get_power() ) {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					else {
						lb->buf().printf("%u kW", veh_type->get_power());
					}
					break;
				case SPEC_GEAR:
					if( veh_type->get_power() ) {
						lb->buf().printf("%0.2f : 1", veh_type->get_gear() / 64.0);
					}
					break;
				case SPEC_TRACTIVE_FORCE:
					if( veh_type->get_power() ) {
						lb->buf().printf("%u kN", veh_type->get_tractive_effort());
					}
					break;
				//case SPEC_FUEL_PER_KM:
				//	if( veh_type->get_power() ) {
				//		//lb->buf().printf("%u", veh_type->get_fuel_per_km()); // TODO: add fuel unit such as km/L
				//	}
				//	break;
				case SPEC_RATED_SPEED:
					if (veh_type->get_power()) {
						vehicle_as_potential_convoy_t convoy(*veh_type);
						sint32 friction = convoy.get_current_friction();
						sint32 min_speed = convoy.calc_max_speed(weight_summary_t(convoy.calc_max_starting_weight(friction), friction));
						lb->buf().printf("%i km/h", min_speed);
					}
					break;
				case SPEC_SPEED:
					lb->buf().printf("%i km/h", veh_type->get_topspeed());
					lb->set_color(veh_type->get_topspeed() > cnv->get_min_top_speed() ? SYSCOL_TEXT_INACTIVE : SYSCOL_TEXT);
					break;
				case SPEC_TARE_WEIGHT:
					lb->buf().printf("%.1f t", veh_type->get_weight() / 1000.0);
					break;
				case SPEC_MAX_GROSS_WIGHT:
					if (veh_type->get_total_capacity() || veh_type->get_overcrowded_capacity()) {
						lb->buf().printf("(%.1f t)", veh_type->get_max_loading_weight() / 1000.0);
					}
					break;
				case SPEC_AXLE_LOAD:
					lb->buf().printf("%u t", veh_type->get_axle_load());
					lb->set_color(veh_type->get_axle_load() < cnv->get_highest_axle_load() ? SYSCOL_TEXT_INACTIVE : SYSCOL_TEXT);
					break;
				case SPEC_LENGTH:
					lb->buf().append(veh_type->get_length(), 0);
					break;
				case SPEC_BRAKE_FORCE:
					if (veh_type->get_brake_force() != 0) {
						vehicle_as_potential_convoy_t convoy(*veh_type);
						lb->buf().printf("%.2f kN", convoy.get_braking_force().to_double() / 1000.0);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					break;
				default:
					lb->buf().append("-");
					break;
				}
				lb->update();
			}
		}

		// Convoy total value
		buf.clear();
		if (cnv->get_vehicle_count()==1) {
			; // nothing to do
		}
		else {
			switch (i) {
			case SPEC_POWER:
				// FIXME: this is multiplied by "gear"
				buf.printf("%u kW", cnv->get_sum_power() / 1000); break;
			case SPEC_TRACTIVE_FORCE:
				// FIXME: this is multiplied by "gear"
				buf.printf("%u kN", cnv->get_starting_force().to_sint32() / 1000); break;
			case SPEC_SPEED:
				buf.printf("%i km/h", speed_to_kmh(cnv->get_min_top_speed())); break;
			case SPEC_TARE_WEIGHT:
				buf.printf("%.1f t", cnv->get_sum_weight() / 1000.0); break;
			case SPEC_MAX_GROSS_WIGHT:
				buf.printf("(%.1f t)", (cnv->get_sum_weight() + cnv->get_freight_summary().max_freight_weight) / 1000.0); break;
			case SPEC_AXLE_LOAD:
				buf.printf("%u t", cnv->get_highest_axle_load()); break;
			case SPEC_LENGTH:
				buf.printf("%u (%u%s)", cnv->get_length(), cnv->get_vehicle_summary().tiles,
					cnv->get_vehicle_summary().tiles > 1 ? translator::translate("tiles") : translator::translate("tile"));
				break;
			case SPEC_BRAKE_FORCE:
				buf.printf("%.2f kN", cnv->get_braking_force().to_double() / 1000.0); break;
			case SPEC_RANGE:
				if (!cnv->get_min_range()) {
					buf.append(translator::translate("unlimited"));
				}
				else {
					buf.printf("%u km", cnv->get_min_range());
				}
				break;
			//case SPEC_FUEL_PER_KM: // Unified notation is difficult in the case of compound
			case SPEC_FREIGHT_TYPE:
			//case SPEC_REPLENSHMENT_SEC:
			case SPEC_GEAR:
			case SPEC_RATED_SPEED:
			default:
				break;
			}
			new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM)->buf().append(buf);
		}
	}
}

void gui_convoy_spec_table_t::insert_payload_rows()
{
	const uint8 pas_and_mail_rows = goods_manager_t::passengers->get_number_of_classes() + goods_manager_t::passengers->get_number_of_classes() + 1/* overcrowd capacity*/;
	const uint8 payload_rows = goods_manager_t::get_max_catg_index() + pas_and_mail_rows;

	const uint8 convoy_catering_level = cnv->get_catering_level(goods_manager_t::INDEX_PAS);

	for (uint8 i = 0; i < payload_rows; i++) {
		const uint8 freight_type_idx = i <= goods_manager_t::passengers->get_number_of_classes() ? goods_manager_t::INDEX_PAS :
			i < pas_and_mail_rows ? goods_manager_t::INDEX_MAIL : i - pas_and_mail_rows;
		if (!cnv->get_goods_catg_index().is_contained(freight_type_idx)) {
			continue;
		}
		const uint8 g_class = freight_type_idx == goods_manager_t::INDEX_PAS ? i :
			freight_type_idx == goods_manager_t::INDEX_MAIL ? i - goods_manager_t::passengers->get_number_of_classes() - 1 :
			0;
		const bool is_overcrowded_capacity = i == goods_manager_t::passengers->get_number_of_classes();
		// convoy does not have the capacity of this freight type (class) => skip a row
		uint16 total_ac_class_capacity=0;
		if (is_overcrowded_capacity) {
			if (!cnv->get_overcrowded_capacity()) {
				continue;
			}
		}
		else {
			for (uint8 j = 0; j < cnv->get_vehicle_count(); j++) {
				const vehicle_desc_t* veh_type = cnv->get_vehicle(j)->get_desc();
				if (veh_type->get_freight_type()->get_catg_index() != freight_type_idx) continue;
				total_ac_class_capacity += veh_type->get_capacity(g_class);
			}
			if (!total_ac_class_capacity) continue;
		}
		if (g_class && g_class > goods_manager_t::get_info_catg_index(freight_type_idx)->get_number_of_classes()-1) {
			continue;
		}

		for (uint8 j = 0; j < cnv->get_vehicle_count()+2; j++) {
			if (j == 0) {
				// symbol with freight type(class) nmame
				add_table(2,1);
				{
					if (is_overcrowded_capacity) {
						new_component<gui_image_t>()->set_image(skinverwaltung_t::pax_evaluation_icons ? skinverwaltung_t::pax_evaluation_icons->get_image_id(1) : IMG_EMPTY, true);
						new_component<gui_label_t>("overcrowded_capacity");
					}
					else {

						new_component<gui_image_t>()->set_image(goods_manager_t::get_info_catg_index(freight_type_idx)->get_catg_symbol(), true);
						if (freight_type_idx == goods_manager_t::INDEX_PAS || freight_type_idx == goods_manager_t::INDEX_MAIL) {
							gui_label_buf_t *lb = new_component<gui_label_buf_t>();
							lb->buf().append(goods_manager_t::get_default_accommodation_class_name(freight_type_idx, g_class));
							lb->update();
						}
						else {
							new_component<gui_label_t>(goods_manager_t::get_info_catg_index(freight_type_idx)->get_catg_name());
						}
					}
				}
				end_table();
				continue;
			}
			else if (j == cnv->get_vehicle_count()+1) {
				// Convoy total value
				if (cnv->get_vehicle_count() == 1) {
					; // nothing to do
				}
				else {
					gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM, gui_label_t::centered);
					if (is_overcrowded_capacity) {
						td->buf().append(cnv->get_overcrowded_capacity(), 0);
					}
					else {
						td->buf().printf("%u %s", total_ac_class_capacity, translator::translate(goods_manager_t::get_info_catg_index(freight_type_idx)->get_mass()));
					}
					td->update();
				}
				continue;
			}

			vehicle_t* veh = cnv->get_vehicle(j-1);
			add_table(3,1);
			{
				// catg check
				if (veh->get_desc()->get_freight_type()->get_catg_index() == freight_type_idx) {
					const sint64 class_diff = (sint64)veh->get_reassigned_class(g_class)-(sint64)g_class;
					if (class_diff) {
						new_component<gui_fluctuation_triangle_t>(class_diff);
					}
					else if( freight_type_idx==goods_manager_t::INDEX_PAS  &&  skinverwaltung_t::comfort ) {
						new_component<gui_margin_t>((LINESPACE*3)>>2);
					}
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
					if (is_overcrowded_capacity) {
						lb->buf().append(veh->get_desc()->get_overcrowded_capacity(), 0);
					}
					else {
						if (veh->get_accommodation_capacity(g_class)) {
							lb->buf().append(veh->get_accommodation_capacity(g_class), 0);
							lb->update();
							if( freight_type_idx==goods_manager_t::INDEX_PAS  &&  skinverwaltung_t::comfort ) {
								// comfort display
								add_table(2,1);
								{
									new_component<gui_image_t>(skinverwaltung_t::comfort->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Comfort"));
									lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
									lb->buf().append(veh->get_desc()->get_comfort(g_class), 0);
									lb->update();
									lb->set_fixed_width(lb->get_min_size().w);
								}
								end_table();
							}
						}
						else {
							lb->buf().append("-");
							lb->set_color(SYSCOL_TEXT_INACTIVE);
							lb->update();
						}
					}
				}
				else {
					new_component<gui_empty_t>();
				}
			}
			end_table();
		}
	}

	bool has_tpo = cnv->get_catering_level(goods_manager_t::INDEX_MAIL);
	for( uint8 i=0; i < MAX_PAYLOAD_ROW; i++ ) {
		if( i==SPEC_CATERING  &&  (!cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) && !convoy_catering_level) ) {
			continue;  // skip catering row
		}
		if( i==SPEC_TPO  &&  (!cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL) && !has_tpo) ) {
			continue;  // skip travering offce row
		}

		new_component<gui_table_header_t>(payload_table_first_col_text[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(spec_table_first_col_width);
		for (uint8 j=0; j < cnv->get_vehicle_count(); j++) {
			const vehicle_desc_t *veh_type = cnv->get_vehicle(j)->get_desc();

			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
			switch (i) {
				case SPEC_PROHIBIT_MIXLOAD:
					if (veh_type->get_mixed_load_prohibition()) {
						lb->buf().append(translator::translate("YES")); // "prohibited" is already used for overtaking
					}
					break;

				case SPEC_CATERING:
				{
					const uint8 catering_level = veh_type->get_catering_level();
					if( catering_level  &&  veh_type->get_freight_type()->get_catg_index() == goods_manager_t::INDEX_PAS ) {
						lb->set_color(catering_level == convoy_catering_level ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
						lb->buf().append(catering_level, 0);
						//lb->set_underline(!veh_type->get_self_contained_catering());
					}
					break;
				}

				case SPEC_TPO:
				{
					const uint8 catering_level = veh_type->get_catering_level();
					if( catering_level  &&  veh_type->get_freight_type()->get_catg_index() == goods_manager_t::INDEX_MAIL ) {
						lb->buf().append(translator::translate("YES")); // TODO: add translation, or draw a circle
					}
					break;
				}
				case SPEC_MIN_LOADING_TIME:
				case SPEC_MAX_LOADING_TIME:
				{
					if( veh_type->get_capacity() || veh_type->get_overcrowded_capacity() ) {
						char time_as_clock[32];
						world()->sprintf_ticks(time_as_clock, sizeof(time_as_clock), i==SPEC_MIN_LOADING_TIME ? veh_type->get_min_loading_time() : veh_type->get_max_loading_time());
						lb->buf().append(time_as_clock);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					break;
				}

				default:
					break;
			}
			lb->update();
		}

		// Convoy total value
		if (cnv->get_vehicle_count() == 1) {
			; // nothing to do
		}
		else {
			gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM, gui_label_t::centered);
			switch (i) {
				case SPEC_CATERING:
					if( convoy_catering_level ) {
						td->buf().append(convoy_catering_level, 0);
					}
					else {
						td->buf().append("-");
					}
					break;
				case SPEC_TPO:
					if( has_tpo ) {
						td->buf().append(translator::translate("YES"));
					}
					break;

				case SPEC_MIN_LOADING_TIME:
				case SPEC_MAX_LOADING_TIME:
				{
					char time_as_clock[32];
					world()->sprintf_ticks(time_as_clock, sizeof(time_as_clock), i==SPEC_MIN_LOADING_TIME ? cnv->calc_longest_min_loading_time() : cnv->calc_longest_max_loading_time());
					td->buf().append(time_as_clock);
					break;
				}

				case SPEC_PROHIBIT_MIXLOAD:
				default:
					break;
			}
			td->update();
		}
	}
}

void gui_convoy_spec_table_t::insert_maintenance_rows()
{
	const uint16 month_now = world()->get_timeline_year_month();
	const bool is_aircraft = cnv->front()->get_waytype() == air_wt;
	uint32 total; // for calculate convoy total

	for (uint8 i = SPECS_MAINTENANCE_START; i < SPECS_MAINTENANCE_END; i++) {
		total = 0;

		new_component<gui_table_header_t>(spec_table_first_col_text[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(spec_table_first_col_width);
		for (uint8 j=0; j < cnv->get_vehicle_count(); j++) {
			const vehicle_desc_t *veh_type = cnv->get_vehicle(j)->get_desc();

			if( i==SPEC_CAN_UPGRADE  && skinverwaltung_t::upgradable ) {
				// use symbol cell
				if (veh_type->get_upgrades_count()) {
					new_component<gui_image_t>(skinverwaltung_t::upgradable->get_image_id(veh_type->has_available_upgrade(month_now) ? 1 : 0), 0, ALIGN_CENTER_V, true);
				}
				else {
					new_component<gui_label_t>("-");
				}
			}
			//else if( i==SPEC_NEED_STAFF  /*&& skinverwaltung_t::staff_cost*/ ) {
				// use symbol cell
				//if (veh_type->get_total_staff_hundredths()) {
				//	new_component<gui_image_t>(skinverwaltung_t::staff_cost->get_image_id(0), 0, ALIGN_CENTER_V, true);
				//}
				//else {
				//	new_component<gui_label_t>("-");
				//}
			//}
			else {
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);

				switch (i) {
					case SPEC_AGE:
					{
						const uint32 age_in_month = world()->get_current_month() - (uint32)cnv->get_vehicle(j)->get_purchase_time();
						lb->buf().append(age_in_month, 0);
						break;
					}
					case SPEC_CAN_UPGRADE:
						if (veh_type->get_upgrades_count()) {
							lb->buf().printf("%s", translator::translate(veh_type->has_available_upgrade(month_now) ? "Available" : "Yet"));
						}
						else {
							lb->buf().append("-");
							lb->set_color(SYSCOL_TEXT_WEAK);
						}
						break;
					case SPEC_RUNNING_COST:
						if( veh_type->get_running_cost() ) {
							lb->buf().printf("%1.2f$", veh_type->get_running_cost(world()) / 100.0);
						}
						else {
							lb->buf().append("-");
							lb->set_color(SYSCOL_TEXT_WEAK);
						}
						break;
					case SPEC_FIXED_COST:
						if( veh_type->get_fixed_cost() ) {
							lb->buf().printf("%1.2f$", veh_type->get_adjusted_monthly_fixed_cost() / 100.0);
						}
						else {
							lb->buf().append("-");
							lb->set_color(SYSCOL_TEXT_WEAK);
						}
						break;
					case SPEC_VALUE:
						lb->buf().printf("%1.2f$", cnv->get_vehicle(j)->calc_sale_value() / 100.0);
						break;
					/*case SPEC_MAINTENANCE_INTERVAL:
					{
						//const uint32 interval_km = veh_type->get_maintenance_interval_km();
						//lb->buf().printf("%u km", interval_km);
						//if (interval_km > 0) { // ignore "0"
						//	total = min(total, interval_km);
						//}
						//break;
					}
					case SPEC_OVERHAUL_INTERVAL:
					{
						//const uint32 interval = veh_type->get_waytype() == air_wt ? veh_type->get_max_takeoffs() : veh_type->get_max_distance_between_overhauls();
						//if (is_aircraft) {
						//	lb->buf().printf(translator::translate("%u takeoffs"), interval);
						//}
						//else {
						//	lb->buf().printf("%u km", interval);
						//}
						//if (interval > 0) { // ignore "0"
						//	total = min(total, interval);
						//}
						break;
					}
					case SPEC_OVERHAUL_COST:
						//lb->buf().printf("%1.2f$", veh_type->get_initial_overhaul_cost() / 100.0);
						break;
					case SPEC_NEED_STAFF:
						//if (veh_type->get_total_staff_hundredths()) {
						//	lb->buf().append(translator::translate("YES"));
						//}
						//else {
							lb->buf().append("-");
							lb->set_color(SYSCOL_TEXT_WEAK);
						//}
						break;*/
					default:
						lb->buf().append("-");
						break;
				}
				lb->update();
			}
		}

		// Convoy total value
		if (cnv->get_vehicle_count() == 1) {
			; // nothing to do
		}
		else {
			gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM, gui_label_t::centered);
			switch (i) {
				case SPEC_AGE:
					td->buf().printf("%u %s", cnv->get_average_age(),
						cnv->get_average_age() > 1 ? translator::translate("months") : translator::translate("month"));
					break;
				case SPEC_RUNNING_COST:
					td->buf().printf("%.2f$/km", cnv->get_running_cost() / 100.0); break;
				case SPEC_FIXED_COST:
					td->buf().printf("%.2f$/%s", world()->calc_adjusted_monthly_figure(cnv->get_fixed_cost()) / 100.0, translator::translate("month")); break;
				case SPEC_VALUE:
					td->buf().printf("%.2f$", cnv->get_purchase_cost() / 100.0); break;
				/*case SPEC_MAINTENANCE_INTERVAL:
				case SPEC_OVERHAUL_INTERVAL:
					// Convoy min value
					if (total > 0) {
						if (i==SPEC_OVERHAUL_INTERVAL  &&  is_aircraft) {
							td->buf().printf(translator::translate("%u takeoffs"), total);
						}
						else {
							td->buf().printf("%u km", total);
						}
					}
					else {
						td->buf().append("-");
					}
					break;

				case SPEC_OVERHAUL_COST:
					td->buf().printf("%1.2f$", total / 100.0);
					break;
				case SPEC_NEED_STAFF:
					//td->buf().printf("%1.2f$", cnv->get_salaries(100) / 100.0);
					break;*/
				case SPEC_CAN_UPGRADE:
				default:
					break;
			}
			td->update();
		}
	}

#ifdef DEBUG
	// driver
	new_component<gui_label_t>("(DBG driver)")->set_fixed_width(spec_table_first_col_width);
	for (uint8 j = 0; j < cnv->get_vehicle_count(); j++) {
		const vehicle_desc_t *veh_type = cnv->get_vehicle(j)->get_desc();
		//if (veh_type->get_total_drivers()) {
		//	if (skinverwaltung_t::staff_cost) {
		//		new_component<gui_image_t>(skinverwaltung_t::staff_cost->get_image_id(0), 0, ALIGN_CENTER_V, true);
		//	}
		//	else {
		//		new_component<gui_label_t>("*");
		//	}
		//}
		//else {
			new_component<gui_label_t>("-");
		//}
	}
	// Convoy total
	new_component<gui_empty_t>();

#endif

}

void gui_convoy_spec_table_t::insert_constraints_rows()
{

		for (uint8 i = SPECS_CONSTRAINTS_START; i < MAX_SPECS; i++) {
		uint32 total = 0;

		new_component<gui_table_header_t>(spec_table_first_col_text[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(spec_table_first_col_width);
		for (uint8 j=0; j < cnv->get_vehicle_count(); j++) {
			const vehicle_desc_t *veh_type = cnv->get_vehicle(j)->get_desc();
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);

			switch (i) {
				case SPEC_IS_TALL:
					if( veh_type->get_fixed_cost() ) {
						lb->buf().append("*");
						lb->set_color(COL_WARNING);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				case SPEC_TILTING:
					if( veh_type->get_tilting() ) {
						lb->buf().append(translator::translate("*"));
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				default:
					lb->buf().append("-");
					break;
			}
			lb->update();
		}

		// Convoy total value
		if (cnv->get_vehicle_count() == 1) {
			; // nothing to do
		}
		else {
			gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND_SUM, gui_label_t::centered);
			switch (i) {
				case SPEC_IS_TALL:
					if( cnv->has_tall_vehicles() ) {
						td->buf().append(translator::translate("*"));
					}
					else {
						td->buf().append("-");
					}
					break;
				case SPEC_TILTING:
				default:
					break;
			}
			td->update();
		}
	}
}

void gui_convoy_spec_table_t::draw(scr_coord offset)
{
	if( cnv.is_bound() ) {
		const uint32 temp_seed = cnv->get_vehicle_count() + world()->get_current_month() + cnv->get_current_schedule_order() + spec_table_mode + show_sideview;

		if( temp_seed!=update_seed ) {
			update_seed = temp_seed;
			update();
		}

		gui_aligned_container_t::draw(offset);
	}
}


// main class
convoi_detail_t::convoi_detail_t(convoihandle_t cnv) :
	gui_frame_t(""),
	formation(cnv),
	cont_payload_info(cnv),
	cont_maintenance(cnv),
	spec_table(cnv),
	scrollx_formation(&formation, true, false),
	scrolly_payload_info(&cont_payload_info, true, true),
	scrolly_maintenance(&cont_maintenance),
	scroll_spec(&spec_table, true, true)
{
	if (cnv.is_bound()) {
		init(cnv);
	}
}


void convoi_detail_t::init(convoihandle_t cnv)
{
	this->cnv = cnv;
	gui_frame_t::set_name(cnv->get_name());
	gui_frame_t::set_owner(cnv->get_owner());

	set_table_layout(1,0);
	add_table(3,2)->set_spacing(scr_size(0,0));
	{
		// 1st row
		add_component(&lb_vehicle_count);
		new_component<gui_fill_t>();

		class_management_button.init(button_t::roundbox, "class_manager", scr_coord(0, 0), scr_size(D_BUTTON_WIDTH*1.5, D_BUTTON_HEIGHT));
		class_management_button.set_tooltip("see_and_change_the_class_assignments");
		add_component(&class_management_button);
		class_management_button.add_listener(this);

		// 2nd row
		new_component_span<gui_empty_t>(2);

		for (uint8 i = 0; i < gui_convoy_formation_t::CONVOY_OVERVIEW_MODES; i++) {
			overview_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(gui_convoy_formation_t::cnvlist_mode_button_texts[i]), SYSCOL_TEXT);
		}
		overview_selector.set_selection(gui_convoy_formation_t::convoy_overview_t::formation);
		overview_selector.set_width_fixed(true);
		overview_selector.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_EDIT_HEIGHT));
		overview_selector.add_listener(this);
		add_component(&overview_selector);
	}
	end_table();

	// 3rd row: convoy overview
	scrollx_formation.set_scroll_discrete_x(false);
	scrollx_formation.set_size_corner(false);
	scrollx_formation.set_maximize(true);
	add_component(&scrollx_formation);

	new_component<gui_margin_t>(0, D_V_SPACE);

	// content of spec table
	cont_spec_tab.set_margin(scr_size(1, D_V_SPACE), scr_size(1, 0));
	cont_spec_tab.set_table_layout(1,0);
	cont_spec_tab.add_table(5,0)->set_spacing(scr_size(0,0));
	{
		cont_spec_tab.new_component<gui_margin_t>(D_MARGIN_LEFT);
		for (uint8 i = 0; i < 4; i++) {
			cb_spec_table_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_spec_modes[i]), SYSCOL_TEXT);
		}
		cb_spec_table_mode.set_selection(spec_table_mode);
		cb_spec_table_mode.add_listener(this);
		cont_spec_tab.add_component(&cb_spec_table_mode);
		cont_spec_tab.new_component<gui_margin_t>(LINESPACE);
		bt_show_sideview.init(button_t::square_state, "show_sideview");
		bt_show_sideview.pressed = spec_table.show_sideview;
		bt_show_sideview.add_listener(this);
		cont_spec_tab.add_component(&bt_show_sideview);
		cont_spec_tab.new_component<gui_fill_t>();
	}
	cont_spec_tab.end_table();
	cont_spec_tab.add_component(&scroll_spec);
	scroll_spec.set_maximize(true);

	add_component(&tabs);
	tabs.add_tab(&cont_maintenance_tab, translator::translate("cd_maintenance_tab"));
	tabs.add_tab(&cont_payload_tab, translator::translate("cd_payload_tab"));
	tabs.add_tab(&container_chart,  translator::translate("cd_physics_tab"));
	tabs.add_tab(&cont_spec_tab,    translator::translate("cd_spec_tab"));
	tabs.add_listener(this);

	// content of payload info tab
	cont_payload_tab.set_table_layout(1,0);

	cont_payload_tab.add_table(4,3)->set_spacing(scr_size(0,0));
	{
		cont_payload_tab.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT, 0));

		cont_payload_tab.new_component<gui_label_t>("Loading time:");
		cont_payload_tab.add_component(&lb_loading_time);
		cont_payload_tab.new_component<gui_margin_t>(D_H_SPACE*2);

		cont_payload_tab.add_table(3,1)->set_spacing(scr_size(0, 0));
		{
			cont_payload_tab.new_component<gui_label_t>("Display loaded detail")->set_tooltip("Displays detailed information of the vehicle's load.");
			for(uint8 i=0; i<4; i++) {
				cb_loaded_detail.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_loaded_display[i]), SYSCOL_TEXT);
			}
			cont_payload_tab.add_component(&cb_loaded_detail);
			cb_loaded_detail.add_listener(this);
			cb_loaded_detail.set_selection(1);
			cont_payload_tab.new_component<gui_fill_t>();
		}
		cont_payload_tab.end_table();

		if (cnv->get_catering_level(goods_manager_t::INDEX_PAS)) {
			cont_payload_tab.new_component<gui_label_t>("Catering level");
			cont_payload_tab.add_component(&lb_catering_level);
			cont_payload_tab.new_component<gui_margin_t>(D_H_SPACE*2);
			cont_payload_tab.new_component<gui_empty_t>();
		}
		if (cnv->get_catering_level(goods_manager_t::INDEX_MAIL)) {
			cont_payload_tab.new_component_span<gui_label_t>("traveling_post_office",4);
		}
	}
	cont_payload_tab.end_table();

	cont_payload_tab.add_component(&scrolly_payload_info);
	scrolly_payload_info.set_maximize(true);

	// content of maintenance tab
	cont_maintenance_tab.set_table_layout(1,0);
	cont_maintenance_tab.add_table(5,3)->set_spacing(scr_size(0,0));
	{
		cont_maintenance_tab.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT,0));
		// 1st row
		cont_maintenance_tab.new_component<gui_label_t>("Restwert:");
		cont_maintenance_tab.add_component(&lb_value);
		cont_maintenance_tab.new_component<gui_fill_t>();

		retire_button.init(button_t::roundbox, "Retire", scr_coord(BUTTON3_X, D_BUTTON_HEIGHT), D_BUTTON_SIZE);
		retire_button.set_tooltip("Convoi is sent to depot when all wagons are empty.");
		retire_button.add_listener(this);
		cont_maintenance_tab.add_component(&retire_button);

		sale_button.init(button_t::roundbox, "Verkauf", scr_coord(0,0), D_BUTTON_SIZE);
		if (skinverwaltung_t::alerts) {
			sale_button.set_image(skinverwaltung_t::alerts->get_image_id(3));
		}
		sale_button.set_tooltip("Remove vehicle from map. Use with care!");
		sale_button.add_listener(this);
		cont_maintenance_tab.add_component(&sale_button);

		// 2nd row
		cont_maintenance_tab.new_component<gui_label_t>("Odometer:");
		cont_maintenance_tab.add_component(&lb_odometer);
		cont_maintenance_tab.new_component<gui_fill_t>();

		cont_maintenance_tab.new_component<gui_empty_t>();
		withdraw_button.init(button_t::roundbox, "withdraw", scr_coord(0,0), D_BUTTON_SIZE);
		withdraw_button.set_tooltip("Convoi is sold when all wagons are empty.");
		withdraw_button.add_listener(this);
		cont_maintenance_tab.add_component(&withdraw_button);
	}
	cont_maintenance_tab.end_table();

	cont_maintenance_tab.add_component(&scrolly_maintenance);
	scrolly_maintenance.set_maximize(true);

	container_chart.set_table_layout(1,0);
	container_chart.add_table(3,0)->set_spacing(scr_size(0,1));
	{
		container_chart.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT,0));
		container_chart.new_component<gui_label_t>("Starting acceleration:")->set_tooltip(translator::translate("helptxt_starting_acceleration"));
		lb_acceleration = container_chart.new_component<gui_acceleration_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();

		container_chart.new_component<gui_label_t>("time_to_top_speed:")->set_tooltip(translator::translate("helptxt_acceleration_time"));
		lb_acc_time = container_chart.new_component<gui_acceleration_time_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();

		container_chart.new_component<gui_label_t>("distance_required_to_top_speed:")->set_tooltip(translator::translate("helptxt_acceleration_distance"));
		lb_acc_distance = container_chart.new_component<gui_acceleration_dist_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();
	}
	container_chart.end_table();
	container_chart.add_component(&switch_chart);

	switch_chart.add_tab(&cont_accel, translator::translate("v-t graph"), NULL, translator::translate("helptxt_v-t_graph"));
	switch_chart.add_tab(&cont_force, translator::translate("f-v graph"), NULL, translator::translate("helptxt_f-v_graph"));

	cont_accel.set_table_layout(1,0);
	cont_accel.add_component(&accel_chart);
	accel_chart.set_dimension(SPEED_RECORDS, 10000);
	accel_chart.set_background(SYSCOL_CHART_BACKGROUND);
	accel_chart.set_min_size(scr_size(0, CHART_HEIGHT));

	cont_accel.add_table(4,0);
	{
		for (uint8 btn = 0; btn < MAX_ACCEL_CURVES; btn++) {
			for (uint8 i = 0; i < SPEED_RECORDS; i++) {
				accel_curves[i][btn] = 0;
			}
			sint16 curve = accel_chart.add_curve(color_idx_to_rgb(physics_curves_color[btn]), (sint64*)accel_curves, MAX_ACCEL_CURVES, btn, SPEED_RECORDS, curves_type[btn], false, true, btn==0 ? 0:1, NULL, marker_type[btn]);

			button_t *b = cont_accel.new_component<button_t>();
			b->init(button_t::box_state_automatic | button_t::flexible, curve_name[btn]);
			b->background_color = color_idx_to_rgb(physics_curves_color[btn]);
			b->set_tooltip(translator::translate(chart_help_text[btn]));
			// always show the max speed reference line and the full load acceleration graph is displayed by default
			b->pressed = (btn == 0) || (cnv->in_depot() && btn == 2);
			b->set_visible(btn!=0);

			btn_to_accel_chart.append(b, &accel_chart, curve);
			if (b->pressed) {
				accel_chart.show_curve(btn);
			}
		}
	}
	cont_accel.end_table();

	cont_force.set_table_layout(1,0);
	cont_force.add_component(&force_chart);
	force_chart.set_dimension(SPEED_RECORDS, 10000);
	force_chart.set_background(SYSCOL_CHART_BACKGROUND);
	force_chart.set_min_size(scr_size(0, CHART_HEIGHT));

	cont_force.add_table(4,0);
	{
		for (uint8 btn = 0; btn < MAX_FORCE_CURVES; btn++) {
			for (uint8 i = 0; i < SPEED_RECORDS; i++) {
				force_curves[i][btn] = 0;
			}
			sint16 force_curve = force_chart.add_curve(color_idx_to_rgb(physics_curves_color[MAX_ACCEL_CURVES + btn]), (sint64*)force_curves, MAX_FORCE_CURVES, btn, SPEED_RECORDS, curves_type[MAX_ACCEL_CURVES + btn], false, true, 3, NULL, marker_type[MAX_ACCEL_CURVES + btn]);

			button_t *bf = cont_force.new_component<button_t>();
			bf->init(button_t::box_state_automatic | button_t::flexible, curve_name[MAX_ACCEL_CURVES + btn]);
			bf->background_color = color_idx_to_rgb(physics_curves_color[MAX_ACCEL_CURVES + btn]);
			bf->set_tooltip(translator::translate(chart_help_text[MAX_ACCEL_CURVES + btn]));
			bf->pressed = (btn == 0);
			if (bf->pressed) {
				force_chart.show_curve(btn);
			}

			btn_to_force_chart.append(bf, &force_chart, force_curve);
		}
	}
	cont_force.end_table();

	if (cnv->in_depot()) {
		tabs.set_active_tab_index(3);
	}

	update_labels();

	reset_min_windowsize();
	set_windowsize(scr_size(D_DEFAULT_WIDTH, tabs.get_pos().y + container_chart.get_size().h));
	set_resizemode(diagonal_resize);
}

void convoi_detail_t::set_tab_opened()
{
	const scr_coord_val margin_above_tab = D_TITLEBAR_HEIGHT + tabs.get_pos().y;
	scr_coord_val ideal_size_h = margin_above_tab + D_MARGIN_BOTTOM;
	switch (tabstate)
	{
		case CD_TAB_MAINTENANCE:
		default:
			ideal_size_h += cont_maintenance_tab.get_size().h + D_V_SPACE*2;
			break;
		case CD_TAB_LOADED_DETAIL:
			ideal_size_h += cont_payload_tab.get_size().h;
			break;
		case CD_TAB_PHYSICS_CHARTS:
			ideal_size_h += container_chart.get_size().h + D_V_SPACE*2;
			break;
	}
	if (get_windowsize().h != ideal_size_h) {
		set_windowsize(scr_size(get_windowsize().w, min(display_get_height() - margin_above_tab, ideal_size_h)));
	}
}

void convoi_detail_t::update_labels()
{
	lb_vehicle_count.buf().printf("%s %i", translator::translate("Fahrzeuge:"), cnv->get_vehicle_count());
	if( cnv->front()->get_waytype()!=water_wt ) {
		lb_vehicle_count.buf().printf(" (%s %i)", translator::translate("Station tiles:"), cnv->get_tile_length());
	}
	lb_vehicle_count.update();

	// update the convoy overview panel
	formation.set_mode(overview_selector.get_selection());

	// update contents of tabs
	switch (tabstate)
	{
		case CD_TAB_MAINTENANCE:
		{
			char number[64];
			number_to_string(number, (double)cnv->get_total_distance_traveled(), 0);
			lb_odometer.buf().append(" ");
			lb_odometer.buf().printf(translator::translate("%s km"), number);
			lb_odometer.update();

			// current resale value
			money_to_string(number, cnv->calc_sale_value() / 100.0);
			lb_value.buf().printf(" %s", number);
			lb_value.update();

			const sint64 seed_temp = cnv->is_reversed() + cnv->get_vehicle_count() + world()->get_timeline_year_month();

			if (old_seed != seed_temp) {
				// something has changed => update
				old_seed = seed_temp;
				cont_maintenance.update_list();
			}
			break;
		}

		case CD_TAB_LOADED_DETAIL:
		{
			// convoy min - max loading time
			char min_loading_time_as_clock[32];
			char max_loading_time_as_clock[32];
			welt->sprintf_ticks(min_loading_time_as_clock, sizeof(min_loading_time_as_clock), cnv->calc_longest_min_loading_time());
			welt->sprintf_ticks(max_loading_time_as_clock, sizeof(max_loading_time_as_clock), cnv->calc_longest_max_loading_time());
			lb_loading_time.buf().printf(" %s - %s", min_loading_time_as_clock, max_loading_time_as_clock);
			lb_loading_time.update();

			// convoy (max) catering level
			if (cnv->get_catering_level(goods_manager_t::INDEX_PAS)) {
				lb_catering_level.buf().printf(": %i", cnv->get_catering_level(goods_manager_t::INDEX_PAS));
				lb_catering_level.update();
			}

			const sint64 seed_temp = cnv->is_reversed() + cnv->get_vehicle_count() + cnv->get_sum_weight() + cb_loaded_detail.get_selection();
			if (old_seed != seed_temp) {
				// something has changed => update
				old_seed = seed_temp;
				cont_payload_info.update_list();
			}
			break;
		}
		default:
			break;
	}


	set_min_windowsize(scr_size(max(D_DEFAULT_WIDTH, get_min_windowsize().w), D_TITLEBAR_HEIGHT + tabs.get_pos().y + tabs.get_required_size().h));
	resize(scr_coord(0, 0));
}


void convoi_detail_t::draw(scr_coord pos, scr_size size)
{
	if(!cnv.is_bound()) {
		destroy_win(this);
		return;
	}

	if(cnv->get_owner()==welt->get_active_player()  &&  !welt->get_active_player()->is_locked()) {
		withdraw_button.enable();
		sale_button.enable();
		retire_button.enable();
		class_management_button.enable();
		if (cnv->in_depot()) {
			retire_button.disable();
			withdraw_button.disable();
		}
	}
	else {
		withdraw_button.disable();
		sale_button.disable();
		retire_button.disable();
		class_management_button.disable();
	}
	withdraw_button.pressed = cnv->get_withdraw();
	retire_button.pressed = cnv->get_depot_when_empty();
	class_management_button.pressed = win_get_magic(magic_class_manager+cnv.get_id());

	if (tabs.get_active_tab_index()==CD_TAB_PHYSICS_CHARTS) {
		// common existing_convoy_t for acceleration curve and weight/speed info.
		convoi_t &convoy = *cnv.get_rep();

		// create dummy convoy and calcurate theoretical acceleration curve
		vector_tpl<const vehicle_desc_t*> vehicles;
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
		{
			vehicles.append(cnv->get_vehicle(i)->get_desc());
		}
		potential_convoy_t empty_convoy(vehicles);
		potential_convoy_t dummy_convoy(vehicles);
		const sint32 min_weight = dummy_convoy.get_vehicle_summary().weight;
		const sint32 max_freight_weight = dummy_convoy.get_freight_summary().max_freight_weight;

		const sint32 akt_speed_soll = kmh_to_speed(convoy.calc_max_speed(convoy.get_weight_summary()));
		const sint32 akt_speed_soll_ = dummy_convoy.get_vehicle_summary().max_sim_speed;
		float32e8_t akt_v = 0;
		float32e8_t akt_v_min = 0;
		float32e8_t akt_v_max = 0;
		sint32 akt_speed = 0;
		sint32 akt_speed_min = 0;
		sint32 akt_speed_max = 0;
		sint32 sp_soll = 0;
		sint32 sp_soll_min = 0;
		sint32 sp_soll_max = 0;
		int i = SPEED_RECORDS - 1;
		long delta_t = 1000;
		sint32 delta_s = (welt->get_settings().ticks_to_seconds(delta_t)).to_sint32();
		accel_curves[i][1] = akt_speed;
		accel_curves[i][2] = akt_speed_min;
		accel_curves[i][3] = akt_speed_max;

		if (env_t::left_to_right_graphs) {
			accel_chart.set_seed(delta_s * (SPEED_RECORDS - 1));
			accel_chart.set_x_axis_span(delta_s);
		}
		else {
			accel_chart.set_seed(0);
			accel_chart.set_x_axis_span(0 - delta_s);
		}
		accel_chart.set_abort_display_x(0);

		uint32 empty_weight = convoy.get_vehicle_summary().weight;
		const sint32 max_speed = convoy.calc_max_speed(weight_summary_t(empty_weight, convoy.get_current_friction()));
		while (i > 0 && max_speed>0)
		{
			empty_convoy.calc_move(welt->get_settings(), delta_t, weight_summary_t(min_weight, empty_convoy.get_current_friction()), akt_speed_soll_, akt_speed_soll_, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed_min, sp_soll_min, akt_v_min);
			dummy_convoy.calc_move(welt->get_settings(), delta_t, weight_summary_t(min_weight+max_freight_weight, dummy_convoy.get_current_friction()), akt_speed_soll_, akt_speed_soll_, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed_max, sp_soll_max, akt_v_max);
			convoy.calc_move(welt->get_settings(), delta_t, akt_speed_soll, akt_speed_soll, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed, sp_soll, akt_v);
			if (env_t::left_to_right_graphs) {
				accel_curves[--i][1] = cnv->in_depot() ? 0 : akt_speed;
				accel_curves[i][2] = akt_speed_max;
				accel_curves[i][3] = akt_speed_min;
			}
			else {
				accel_curves[SPEED_RECORDS-i][1] = cnv->in_depot() ? 0 : akt_speed;
				accel_curves[SPEED_RECORDS-i][2] = akt_speed_max;
				accel_curves[SPEED_RECORDS-i][3] = akt_speed_min;
				i--;
			}
		}
		// for max speed reference line
		for (i = 0; i < SPEED_RECORDS; i++) {
			accel_curves[i][0] = empty_convoy.get_vehicle_summary().max_sim_speed;
		}

		// force chart
		if (max_speed > 0) {
			const uint16 display_interval = (max_speed + SPEED_RECORDS-1) / SPEED_RECORDS;
			float32e8_t rolling_resistance = cnv->get_adverse_summary().fr;
			te_curve_abort_x = max(2,(uint8)((max_speed + (display_interval-1)) / display_interval));
			force_chart.set_abort_display_x(te_curve_abort_x);
			force_chart.set_dimension(te_curve_abort_x, 10000);

			if (env_t::left_to_right_graphs) {
				force_chart.set_seed(display_interval * (SPEED_RECORDS-1));
				force_chart.set_x_axis_span(display_interval);
				for (i = 0; i < max_speed; i++) {
					if (i % display_interval == 0) {
						force_curves[SPEED_RECORDS-i / display_interval-1][0] = cnv->get_force_summary(i*kmh2ms).to_sint32();
						force_curves[SPEED_RECORDS-i / display_interval-1][1] = cnv->calc_speed_holding_force(i*kmh2ms, rolling_resistance).to_sint32();
					}
				}
			}
			else {
				force_chart.set_seed(0);
				force_chart.set_x_axis_span(0 - display_interval);
				for (int i = 0; i < max_speed; i++) {
					if (i % display_interval == 0) {
						force_curves[i/display_interval][0] = cnv->get_force_summary(i*kmh2ms).to_sint32();
						force_curves[i/display_interval][1] = cnv->calc_speed_holding_force(i*kmh2ms, rolling_resistance).to_sint32();
					}
				}
			}
		}
	}

	update_labels();

	// all gui stuff set => display it
	gui_frame_t::draw(pos, size);
}



/**
 * This method is called if an action is triggered
 */
bool convoi_detail_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(cnv.is_bound()) {
		if(comp==&sale_button) {
			cnv->call_convoi_tool( 'x', NULL );
			return true;
		}
		else if(comp==&withdraw_button) {
			cnv->call_convoi_tool( 'w', NULL );
			return true;
		}
		else if(comp==&retire_button) {
			cnv->call_convoi_tool( 'T', NULL );
			return true;
		}
		else if (comp == &class_management_button) {
			create_win(20, 40, new vehicle_class_manager_t(cnv), w_info, magic_class_manager + cnv.get_id());
			return true;
		}
		else if (comp == &overview_selector) {
			update_labels();
			return true;
		}
		else if( comp==&cb_loaded_detail ) {
			cont_payload_info.set_display_mode(cb_loaded_detail.get_selection());
			return true;
		}
		else if (comp == &cb_spec_table_mode) {
			spec_table_mode = cb_spec_table_mode.get_selection();
			spec_table.spec_table_mode = spec_table_mode;
			return true;
		}
		else if (comp == &bt_show_sideview) {
			spec_table.show_sideview = !spec_table.show_sideview;
			bt_show_sideview.pressed = spec_table.show_sideview;
			return true;
		}
		else if (comp == &tabs) {
			const sint16 old_tab = tabstate;
			tabstate = tabs.get_active_tab_index();
			if (get_windowsize().h == get_min_windowsize().h || tabstate == old_tab) {
				set_tab_opened();
			}
			return true;
		}
	}
	return false;
}

bool convoi_detail_t::infowin_event(const event_t *ev)
{
	if (cnv.is_bound() && formation.getroffen(ev->click_pos.x - formation.get_pos().x, ev->click_pos.y - D_TITLEBAR_HEIGHT  - scrollx_formation.get_pos().y)) {
		if (IS_LEFTRELEASE(ev)) {
			cnv->show_info();
			return true;
		}
		else if (IS_RIGHTRELEASE(ev)) {
			world()->get_viewport()->change_world_position(cnv->get_pos());
			return true;
		}
	}
	return gui_frame_t::infowin_event(ev);
}

void convoi_detail_t::rdwr(loadsave_t *file)
{
	// convoy data
	convoi_t::rdwr_convoihandle_t(file, cnv);

	// window size, scroll position
	scr_size size = get_windowsize();
	sint32 xoff = scrolly_maintenance.get_scroll_x();
	sint32 yoff = scrolly_maintenance.get_scroll_y();
	sint32 formation_xoff = scrollx_formation.get_scroll_x();
	sint32 formation_yoff = scrollx_formation.get_scroll_y();

	size.rdwr( file );
	file->rdwr_long( xoff );
	file->rdwr_long( yoff );
	uint8 selected_tab = tabs.get_active_tab_index();
	if( file->is_version_ex_atleast(14,41) ) {
		file->rdwr_byte(selected_tab);
	}

	if(  file->is_loading()  ) {
		// convoy vanished
		if(  !cnv.is_bound()  ) {
			dbg->error( "convoi_detail_t::rdwr()", "Could not restore convoi detail window of (%d)", cnv.get_id() );
			destroy_win( this );
			return;
		}

		// now we can open the window ...
		scr_coord const& pos = win_get_pos(this);
		convoi_detail_t *w = new convoi_detail_t(cnv);
		create_win(pos.x, pos.y, w, w_info, magic_convoi_detail + cnv.get_id());
		w->set_windowsize( size );
		w->scrolly_maintenance.set_scroll_position( xoff, yoff );
		w->scrollx_formation.set_scroll_position(formation_xoff, formation_yoff);
		w->tabs.set_active_tab_index(selected_tab);
		w->cont_payload_info.set_cnv(cnv);
		// we must invalidate halthandle
		cnv = convoihandle_t();
		destroy_win( this );
	}
}


// component for payload display
gui_convoy_payload_info_t::gui_convoy_payload_info_t(convoihandle_t cnv)
{
	this->cnv = cnv;

	set_table_layout(1,0);
	set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(0, D_V_SPACE));
	update_list();
}

void gui_convoy_payload_info_t::update_list()
{
	remove_all();
	if (cnv.is_bound()) {
		add_table(2,0)->set_alignment(ALIGN_TOP);
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
			new_component_span<gui_border_t>(2);

			const vehicle_t* veh = cnv->get_vehicle(i);
			// left part
			add_table(1,3)->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
			{
				const uint16 month_now = world()->get_timeline_year_month();
				// cars number in this convoy
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
				sint16 car_number = cnv->get_car_numbering(i);
				if (car_number < 0) {
					lb->buf().printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number)); // This also applies to horses and tractors and push locomotives.
				}
				else {
					lb->buf().append(car_number);
				}
				lb->set_color(veh->get_desc()->has_available_upgrade(month_now) ? SYSCOL_UPGRADEABLE : SYSCOL_TEXT_WEAK);
				lb->set_fixed_width((D_BUTTON_WIDTH*3)>>3);
				lb->update();

				// vehicle color bar
				const PIXVAL veh_bar_color = veh->get_desc()->is_obsolete(month_now) ? SYSCOL_OBSOLETE : (veh->get_desc()->is_future(month_now) || veh->get_desc()->is_retired(month_now)) ? SYSCOL_OUT_OF_PRODUCTION : COL_SAFETY;
				new_component<gui_vehicle_bar_t>(veh_bar_color, scr_size((D_BUTTON_WIDTH*3>>3)-6, VEHICLE_BAR_HEIGHT))
					->set_flags(
						veh->is_reversed() ? veh->get_desc()->get_basic_constraint_next() : veh->get_desc()->get_basic_constraint_prev(),
						veh->is_reversed() ? veh->get_desc()->get_basic_constraint_prev() : veh->get_desc()->get_basic_constraint_next(),
						veh->get_desc()->get_interactivity()
					);

				// goods category symbol
				if (veh->get_desc()->get_total_capacity() || veh->get_desc()->get_overcrowded_capacity()) {
					new_component<gui_image_t>(veh->get_desc()->get_freight_type()->get_catg_symbol(), 0, ALIGN_CENTER_H, true);
				}
				else {
					new_component<gui_empty_t>();
				}
			}
			end_table();

			// right part
			add_table(2,2);
			{
				gui_label_buf_t *lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left, 2);
				lb->buf().append(translator::translate(veh->get_desc()->get_name()));
				lb->update();
				new_component<gui_margin_t>(LINESPACE>>1);
				new_component<gui_vehicle_cargo_info_t>(cnv->get_vehicle(i), display_mode);
			}
			end_table();
		}
		end_table();
	}
	set_size(get_size());
}


gui_convoy_maintenance_info_t::gui_convoy_maintenance_info_t(convoihandle_t cnv)
{
	this->cnv = cnv;

	set_table_layout(1,0);
	set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(0, D_V_SPACE));
	update_list();
}

void gui_convoy_maintenance_info_t::update_list()
{
	remove_all();
	if (cnv.is_bound() && cnv->get_vehicle_count()) {
		any_obsoletes = false;
		uint32 run_actual = 0, run_nominal = 0, run_percent = 0;
		uint32 mon_actual = 0, mon_nominal = 0, mon_percent = 0;
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
			const vehicle_desc_t *desc = cnv->get_vehicle(i)->get_desc();
			run_nominal += desc->get_running_cost();
			run_actual += desc->get_running_cost( world() );
			mon_nominal += world()->calc_adjusted_monthly_figure(desc->get_fixed_cost());
			mon_actual += world()->calc_adjusted_monthly_figure(desc->get_fixed_cost( world() ));
		}
		if (run_nominal) run_percent = ((run_actual - run_nominal) * 100) / run_nominal;
		if (mon_nominal) mon_percent = ((mon_actual - mon_nominal) * 100) / mon_nominal;

		if (run_percent || mon_percent) {
			any_obsoletes = true;
		}

		vector_tpl<livery_scheme_t*>* schemes = world()->get_settings().get_livery_schemes();
		livery_scheme_t* convoy_scheme = schemes->get_element(cnv->get_livery_scheme_index());
		const uint16 month_now = world()->get_timeline_year_month();

		add_table(2,0)->set_alignment(ALIGN_TOP);
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
			new_component_span<gui_border_t>(2);

			const vehicle_t* veh = cnv->get_vehicle(i);
			// left part
			add_table(1,2)->set_alignment(ALIGN_TOP);
			{
				// cars number in this convoy
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
				sint8 car_number = cnv->get_car_numbering(i);
				if (car_number < 0) {
					lb->buf().printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number)); // This also applies to horses and tractors and push locomotives.
				}
				else {
					lb->buf().append(car_number);
				}
				lb->set_color(veh->get_desc()->has_available_upgrade(month_now) ? SYSCOL_UPGRADEABLE : SYSCOL_TEXT_WEAK);
				lb->set_fixed_width((D_BUTTON_WIDTH*3)>>3);
				lb->update();

				// image
				new_component<gui_image_t>(veh->get_loaded_image(), veh->get_owner()->get_player_nr(), 0, true);
			}
			end_table();

			// right part
			add_table(3,0);
			{
				// name
				gui_label_buf_t *lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left, 3);
				lb->buf().append(translator::translate(veh->get_desc()->get_name()));
				lb->update();

				// livery scheme info
				if( veh->get_desc()->get_livery_count()>1 ) {
					if (!strcmp(veh->get_current_livery(), "default")) {
						if (convoy_scheme->is_contained(veh->get_current_livery(), month_now)) {
							// current livery belongs to convoy applied livery scheme and active
							new_component<gui_margin_t>(LINESPACE>>1);
							// is current livery latest one? no => change text color
							new_component<gui_label_t>(convoy_scheme->get_name(), strcmp(convoy_scheme->get_latest_available_livery(month_now, veh->get_desc()), veh->get_current_livery()) ? SYSCOL_UPGRADEABLE : SYSCOL_TEXT);
							new_component<gui_fill_t>();
						}
						else if (convoy_scheme->is_contained(veh->get_current_livery())) {
							// this is old livery
							new_component<gui_margin_t>(LINESPACE>>1);
							// TODO: add livery scheme symbol
							new_component<gui_label_t>(convoy_scheme->get_name(), SYSCOL_OBSOLETE);
							new_component<gui_fill_t>();
						}
						//else { // no livery=>no display }
					}
					else {
						// current livery does not belong to convoy applied livery scheme
						// note: livery may belong to more than one livery scheme
						bool found_active_scheme = false;

						new_component<gui_margin_t>(LINESPACE>>1);
						gui_label_buf_t *lb_livery = new_component<gui_label_buf_t>(color_idx_to_rgb(COL_BROWN), gui_label_t::left);
						int cnt = 0;
						for(uint32 i = 0; i < schemes->get_count(); i++)
						{
							livery_scheme_t* scheme = schemes->get_element(i);
							if (scheme->is_contained(veh->get_current_livery())) {
								if (scheme->is_available(month_now)) {
									found_active_scheme = true;
									if (cnt) { lb_livery->buf().append(", "); }
									lb_livery->buf().append(translator::translate(scheme->get_name()));
									cnt++;
								}
								else if(!found_active_scheme){
									if (cnt) { lb_livery->buf().append(", "); }
									lb_livery->buf().append(translator::translate(scheme->get_name()));
									cnt++;
								}
							}
						}
						if (!found_active_scheme) {
							lb_livery->set_color(color_idx_to_rgb(COL_DARK_BROWN));
						}
						lb_livery->update();

						new_component<gui_fill_t>();
					}
				}

				// age
				const sint32 month = veh->get_purchase_time();
				uint32 age_in_months = world()->get_current_month() - month;

				new_component<gui_margin_t>(LINESPACE >> 1);
				lb = new_component<gui_label_buf_t>();

				lb->buf().printf("%s %s  (", translator::translate("Manufactured:"), translator::get_year_month(month));
				lb->buf().printf(age_in_months < 2 ? translator::translate("%i month") : translator::translate("%i months"), age_in_months);
				lb->buf().append(")");
				lb->update();
				new_component<gui_fill_t>();

				// value
				// TODO: Indication of depreciation
				char number[64];
				money_to_string(number, veh->calc_sale_value() / 100.0);
				new_component<gui_margin_t>(LINESPACE >> 1);
				lb = new_component<gui_label_buf_t>();
				lb->buf().printf("%s %s", translator::translate("Restwert:"), number);
				lb->update();
				new_component<gui_fill_t>();

				// maintenance
				new_component<gui_margin_t>(LINESPACE>>1);
				lb = new_component<gui_label_buf_t>();
				lb->buf().printf(translator::translate("Maintenance: %1.2f$/km, %1.2f$/month\n"), veh->get_desc()->get_running_cost()/100.0, veh->get_desc()->get_adjusted_monthly_fixed_cost()/100.0);
				lb->update();
				new_component<gui_fill_t>();

				// TODO: upgrade info

			}
			end_table();
		}
		end_table();
	}
	set_size(get_size());
}
