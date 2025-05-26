/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "way_info.h"
#include "simwin.h"
#include "components/gui_colorbox.h"
#include "components/gui_divider.h"
#include "../simmenu.h"
#include "../simworld.h"
#include "../simcity.h"
#include "../display/viewport.h"
#include "../player/simplay.h"
#include "../utils/simstring.h"
#include "../boden/grund.h"
#include "../boden/wege/weg.h"
#include "../boden/wege/schiene.h"
#include "../boden/wege/strasse.h"

#include "../obj/tunnel.h"
#include "../obj/bruecke.h"
#include "../obj/wayobj.h"
#include "../obj/roadsign.h" // for working method name
#include "../dataobj/environment.h"
#include "../dataobj/ribi.h"
#include "../bauer/wegbauer.h"
#include "../descriptor/roadsign_desc.h"
#include "../descriptor/tunnel_desc.h"
#include "../descriptor/way_desc.h"
#include "../vehicle/rail_vehicle.h"


static char const* const speed_resticted_text = "speed_restricted";
const uint8 severity_color[6] =
{
	COL_GREEN, COL_LIGHT_YELLOW, 30, COL_ORANGE, COL_RED, COL_PURPLE-1
};

gui_way_detail_info_t::gui_way_detail_info_t(weg_t *way)
{
	this->way = way;

	speed_restricted.set_image(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2) : IMG_EMPTY, true);
	speed_restricted.set_tooltip( translator::translate(speed_resticted_text) );

	set_table_layout(1,0);
	set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE), scr_size(D_H_SPACE, 0));
}

void gui_way_detail_info_t::draw(scr_coord offset)
{
	grund_t *gr = world()->lookup(way->get_pos());
	if (!gr) {
		return;
	}
	if( way ) {
		const waytype_t wt = way->get_waytype();
		const wayobj_t  *wayobj = gr->get_wayobj(wt);
		const bruecke_t *bridge = gr->find<bruecke_t>();
		const tunnel_t  *tunnel = gr->find<tunnel_t>();
		const double tiles_pr_km = (1000 / world()->get_settings().get_meters_per_tile());
		const bool impassible = way->get_remaining_wear_capacity() == 0;

		remove_all();
		if (wt == invalid_wt) {
			return;
		}
		// owner
		if (way->is_public_right_of_way()) {
			if( way->get_desc()->get_waytype()==water_wt  &&  way->get_max_speed()==0 ) {
				// Natural streams => nothing to show
			}
			else if( way->get_desc()->get_waytype() == road_wt  &&  world()->get_city(way->get_pos().get_2d()) ) {
				new_component<gui_label_t>("Municipal way");
			}
			else {
				new_component<gui_label_t>("Public right of way");
			}
		}
		else if(way->get_owner() != NULL) {
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(color_idx_to_rgb(way->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
			lb->buf().printf("%s (%s)", translator::translate("Private way"), way->get_owner()->get_name());
			lb->update();
		}

		add_table(5,0);
		{
			const slope_t::type hang = gr ? gr->get_weg_hang() : slope_t::flat;
			const sint32 potential_way_speed = !hang ? way->get_desc()->get_topspeed() : hang == 1 ? way->get_desc()->get_topspeed_gradient_1() : way->get_desc()->get_topspeed_gradient_2();

			if( bridge ) {
				new_component<gui_margin_t>(10);
				new_component<gui_label_t>(bridge->get_desc()->get_name(), bridge->get_owner() ? color_idx_to_rgb(bridge->get_owner()->get_player_color1() + env_t::gui_player_color_dark) : color_idx_to_rgb(COL_ORANGE));
				gui_label_buf_t *lb_bridge = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(bridge->get_desc()->get_maintenance()), infrastructure) / 100.0;
				lb_bridge->buf().printf(translator::translate(" %1.2f$/mon"), way->is_diagonal() ? maint_per_tile*10/14.0 : maint_per_tile);
				lb_bridge->update();

				const sint32 top_speed = !hang ? bridge->get_desc()->get_topspeed() : hang == 1 ? bridge->get_desc()->get_topspeed_gradient_1() : bridge->get_desc()->get_topspeed_gradient_2();
				lb_bridge = new_component<gui_label_buf_t>(potential_way_speed > top_speed ? COL_WARNING : COL_INACTIVE, gui_label_t::right);
				lb_bridge->buf().printf(translator::translate("%ikm/h"), top_speed);
				lb_bridge->update();
				new_component<gui_fill_t>();
			}
			else if (tunnel) {
				new_component<gui_margin_t>(10);
				new_component<gui_label_t>(tunnel->get_desc()->get_name(), tunnel->get_owner() ? color_idx_to_rgb(tunnel->get_owner()->get_player_color1() + env_t::gui_player_color_dark) : color_idx_to_rgb(COL_ORANGE));
				gui_label_buf_t *lb_tunnel = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(tunnel->get_desc()->get_maintenance()), infrastructure) / 100.0;
				lb_tunnel->buf().printf(translator::translate(" %1.2f$/mon"), way->is_diagonal() ? maint_per_tile * 10 / 14.0 : maint_per_tile);
				lb_tunnel->update();

				const sint32 top_speed = !hang ? tunnel->get_desc()->get_topspeed() : hang == 1 ? tunnel->get_desc()->get_topspeed_gradient_1() : tunnel->get_desc()->get_topspeed_gradient_2();
				lb_tunnel = new_component<gui_label_buf_t>(potential_way_speed > top_speed ? COL_WARNING : COL_INACTIVE, gui_label_t::right);
				lb_tunnel->buf().printf(translator::translate("%ikm/h"), top_speed);
				lb_tunnel->update();
				new_component<gui_fill_t>();
			}

			// way
			new_component<gui_margin_t>(10);
			new_component<gui_label_t>(way->get_desc()->get_name(), way->get_owner() != NULL ? color_idx_to_rgb(way->get_owner()->get_player_color1() + env_t::gui_player_color_dark) : SYSCOL_TEXT);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(way->get_desc()->get_maintenance()==0 ? COL_INACTIVE : SYSCOL_TEXT, gui_label_t::right);
			const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(way->get_desc()->get_maintenance()), infrastructure) / 100.0;
			lb->buf().printf(translator::translate(" %1.2f$/mon"), way->is_diagonal() ? maint_per_tile*10/14.0 : maint_per_tile);
			lb->update();

			lb = new_component<gui_label_buf_t>(potential_way_speed > way->get_max_speed(false) ? COL_WARNING : SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf(translator::translate("%ikm/h"), way->get_max_speed(false));
			lb->update();
			new_component<gui_fill_t>();

			if( wayobj ) {
				if (way->is_electrified()) {
					new_component<gui_image_t>(skinverwaltung_t::electricity->get_image_id(0), 0, ALIGN_NONE, true);
				}
				else {
					new_component<gui_margin_t>(10);
				}
				new_component<gui_label_t>(wayobj->get_desc()->get_name(), color_idx_to_rgb(wayobj->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
				lb = new_component<gui_label_buf_t>(wayobj->get_desc()->get_maintenance()==0 ? COL_INACTIVE : SYSCOL_TEXT, gui_label_t::right);
				const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(wayobj->get_desc()->get_maintenance()), infrastructure) / 100.0;

				lb->buf().printf(translator::translate(" %1.2f$/mon"), way->is_diagonal() ? maint_per_tile*10/14.0 : maint_per_tile);
				lb->update();
				lb = new_component<gui_label_buf_t>(way->get_max_speed(true) != way->get_max_speed(false) ? COL_WARNING : SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf(translator::translate("%ikm/h"), wayobj->get_desc()->get_topspeed());
				lb->update();
				new_component<gui_fill_t>();
			}
		}
		end_table();

		if (impassible) {
			new_component<gui_empty_t>();
			add_table(2, 1);
			{
				new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(4):IMG_EMPTY, 0, ALIGN_NONE, true);
				new_component<gui_label_t>("way_cannot_be_used_by_any_vehicle", COL_DANGER);
			}
			end_table();
		}

		if (wt == road_wt) {
			// Display overtaking_info
			const strasse_t *str = (strasse_t *)way;
			assert(str);
			gui_label_buf_t* lb = new_component<gui_label_buf_t>();
			lb->buf().printf("%s: ", translator::translate("overtaking"));

			switch (str->get_overtaking_mode()) {
				case halt_mode:
					lb->buf().append(translator::translate("halt mode"));
					break;
				case oneway_mode:
					lb->buf().append(translator::translate("oneway"));
					break;
				case twoway_mode:
					lb->buf().append(translator::translate("twoway"));
					break;
				case prohibited_mode:
					lb->buf().append(translator::translate("prohibited"));
					break;
				default:
					lb->buf().append(translator::translate("ERROR"));
					dbg->warning("gui_way_detail_info_t::draw", "Undefined overtaking mode '%i'", str->get_overtaking_mode());
					break;
			}
			lb->update();

			add_table(2,1);
			{
				const uint8 congestion_stage = min(5,(uint8)(way->get_congestion_percentage()/20));
				new_component<gui_colorbox_t>()->init(color_idx_to_rgb(severity_color[congestion_stage]), scr_size(D_INDICATOR_BOX_WIDTH, LINEASCENT/2+2), true);
				lb = new_component<gui_label_buf_t>();
				lb->buf().printf(translator::translate("Congestion: %i%%"), way->get_congestion_percentage());
				lb->update();
			}
			end_table();
		}

		// way permissive (assets)
		const way_constraints_of_way_t way_constraints = way->get_desc()->get_way_constraints();
		bool any_permissive = false;
		if (way_constraints.get_permissive()) {
			new_component<gui_empty_t>();
			for (sint8 i = 0; i < way_constraints.get_count(); i++)
			{
				if (way_constraints.get_permissive(i)) {
					if (!any_permissive) {
						add_table(2, 0)->set_spacing(scr_size(D_H_SPACE, 1)); // <-- start table
						if (skinverwaltung_t::alerts) {
							new_component<gui_image_t>(skinverwaltung_t::alerts->get_image_id(0), 0, ALIGN_NONE, true);
						}
						else {
							new_component<gui_margin_t>(D_H_SPACE);
						}
						new_component<gui_label_t>("assets");
						any_permissive = true;
					}
					new_component<gui_margin_t>(D_H_SPACE);
					gui_label_buf_t *lb_permissive = new_component<gui_label_buf_t>();
					char tmpbuf[30];
					sprintf(tmpbuf, "Permissive %i-%i", wt, i);
					lb_permissive->buf().append(translator::translate(tmpbuf));
					lb_permissive->update();
				}
			}
		}
		if (any_permissive) {
			end_table(); // <-- end table
		}

		// way prohibitive
		bool any_prohibitive = false;
		if (way_constraints.get_prohibitive()) {
			new_component<gui_empty_t>();
			for (sint8 i = 0; i < way_constraints.get_count(); i++)
			{
				if (way_constraints.get_prohibitive(i)) {
					if (!any_prohibitive) {
						add_table(2,0)->set_spacing(scr_size(D_H_SPACE,1)); // <-- start table
						if (skinverwaltung_t::alerts) {
							new_component<gui_image_t>(skinverwaltung_t::alerts->get_image_id(1), 0, ALIGN_NONE, true);
						}
						else {
							new_component<gui_margin_t>(D_H_SPACE);
						}
						new_component<gui_label_t>("Restrictions:");
						any_prohibitive = true;
					}
					new_component<gui_margin_t>(D_H_SPACE);
					gui_label_buf_t *lb_prohibitive = new_component<gui_label_buf_t>();

					char tmpbuf[30];
					sprintf(tmpbuf, "Prohibitive %i-%i", wt, i);
					lb_prohibitive->buf().append(translator::translate(tmpbuf));
					lb_prohibitive->update();
				}
			}
		}
		// height restricted
		if( gr->is_height_restricted() ) {
			if (!any_prohibitive) {
				add_table(3, 0)->set_spacing(scr_size(D_H_SPACE, 1)); // <-- start table
				if (skinverwaltung_t::alerts) {
					new_component<gui_image_t>(skinverwaltung_t::alerts->get_image_id(1), 0, ALIGN_NONE, true);
				}
				else {
					new_component<gui_margin_t>(D_H_SPACE);
				}
				new_component_span<gui_label_t>("Restrictions:", 2);
				any_prohibitive = true;
			}

			new_component_span<gui_empty_t>(2);
			new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2) : 0, 0, ALIGN_NONE, true);
			new_component<gui_label_t>("Low bridge", SYSCOL_TEXT_STRONG);
		}
		if (any_prohibitive) {
			end_table(); // <-- end table
		}


		// current way VS replacement way
		new_component<gui_empty_t>();
		sint32 restricted_speed = SINT32_MAX_VALUE;
		add_table(6,0)->set_alignment(ALIGN_CENTER_V);
		{
			new_component_span<gui_empty_t>(2);
			new_component_span<gui_label_t>("To be renewed with", SYSCOL_TEXT_HIGHLIGHT, gui_label_t::left, 4);

			// way name
			new_component_span<gui_label_t>(2)->init(way->get_desc()->get_name());
			const way_desc_t* replacement_way = way->get_replacement_way();
			if (replacement_way) {
				const uint16 time = world()->get_timeline_year_month();
				const bool is_current = replacement_way->is_available(time);

				// Publicly owned roads in towns are replaced with the latest city road type.
				const bool public_city_road = wt == road_wt && (way->get_owner() == NULL || way->get_owner()->is_public_service()) && world()->get_city(way->get_pos().get_2d());
				const way_desc_t* latest_city_road = world()->get_settings().get_city_road_type(time);
				if (public_city_road) {
					if (replacement_way == latest_city_road || latest_city_road == NULL)
					{
						new_component<gui_right_pointer_t>(SYSCOL_TEXT);
						new_component_span<gui_label_t>("same_as_current", 3);
					}
					else {
						replacement_way = latest_city_road;
						new_component<gui_right_pointer_t>(SYSCOL_UPGRADEABLE);
						new_component_span<gui_label_t>(replacement_way->get_name(), 3);
					}
				}
				else if (way->get_desc() != replacement_way){
					new_component<gui_right_pointer_t>(SYSCOL_UPGRADEABLE);
					if( !is_current ) {
						replacement_way = way_builder_t::weg_search(replacement_way->get_waytype(), replacement_way->get_topspeed(), (sint32)replacement_way->get_axle_load(), time, (systemtype_t)replacement_way->get_styp(), replacement_way->get_wear_capacity());
					}
					new_component_span<gui_label_t>(replacement_way->get_name(), 3);
				}
				else if (!way->is_degraded()) {
					new_component<gui_right_pointer_t>(SYSCOL_TEXT);
					new_component_span<gui_label_t>("same_as_current", 3);
				}
				else {
					// auto-renewal seems to be stopped
					replacement_way = NULL;
					new_component<gui_right_pointer_t>(SYSCOL_OBSOLETE);
					new_component_span<gui_label_t>("keine", SYSCOL_EMPTY, 2);
					new_component<gui_fill_t>();
				}
			}
			else {
				new_component<gui_right_pointer_t>(SYSCOL_OBSOLETE);
				new_component_span<gui_label_t>("keine", SYSCOL_EMPTY, 2);
				new_component<gui_fill_t>();
			}

			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf("%s:", translator::translate("Built"));
			lb->update();
			lb = new_component<gui_label_buf_t>();
			lb->buf().append(translator::get_short_date(way->get_creation_month_year()/12, way->get_creation_month_year()%12));
			lb->update();
			new_component<gui_empty_t>();
			if (!impassible) {
				gui_label_buf_t *lb_last_renewed = new_component_span<gui_label_buf_t>(3);
				if (way->get_creation_month_year() == way->get_last_renewal_monty_year()) {
					lb_last_renewed->buf().append("");
				}
				else {
					lb_last_renewed->buf().printf("(%s: %s)", translator::translate("Last renewed"), translator::get_short_date(way->get_last_renewal_monty_year()/12, way->get_creation_month_year()%12));
				}
				lb_last_renewed->update();
			}
			else {
				gui_label_buf_t* lb_degraded = new_component_span<gui_label_buf_t>(2);
				lb_degraded->buf().printf("(%s: %s)", translator::translate("Degraded"), translator::get_short_date(way->get_last_renewal_monty_year() / 12, way->get_creation_month_year() % 12));
				lb_degraded->set_color(SYSCOL_OBSOLETE);
				lb_degraded->set_fixed_width(148);
				new_component<gui_empty_t>();
			}

			new_component_span<gui_border_t>(2);
			new_component_span<gui_border_t>(2);
			new_component<gui_empty_t>();
			new_component<gui_empty_t>();

			const bool replace_flag = replacement_way != NULL && way->get_desc() != replacement_way;

			lb = new_component<gui_label_buf_t>();
			lb->buf().printf("%s:", translator::translate("monthly_maintenance_cost"));
			lb->update();
			if (way->get_desc()->get_maintenance() > 0) {
				add_table(1,2)->set_spacing(scr_size(0,0));
				{
					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::money_right);
					const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(way->get_desc()->get_maintenance()), infrastructure) / 100.0;
					char maintenance_number[64];
					money_to_string(maintenance_number, way->is_diagonal() ? maint_per_tile*10/14.0 : maint_per_tile);
					lb->buf().printf("%s", maintenance_number);
					lb->update();

					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					char maintenance_km_number[64];
					money_to_string(maintenance_km_number, maint_per_tile * tiles_pr_km);
					lb->buf().printf("(%s/km)", maintenance_km_number);
					lb->update();
				}
				end_table();
			}
			else {
				new_component<gui_label_t>("-");
			}
			if (replace_flag) {
				sint32 change = replacement_way->get_maintenance()-way->get_desc()->get_maintenance();
				const PIXVAL change_col = change<0 ? SYSCOL_UP_TRIANGLE : change>0 ? SYSCOL_DOWN_TRIANGLE : COL_INACTIVE;
				new_component<gui_right_pointer_t>(change_col);
				if (replacement_way->get_maintenance() > 0) {
					add_table(1,2)->set_spacing(scr_size(0,1));
					{
						lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::money_right);
						const double maint_per_tile = (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(replacement_way->get_maintenance()), infrastructure) / 100.0;
						char maintenance_number[64];
						money_to_string(maintenance_number, way->is_diagonal() ? maint_per_tile*10/14.0 : maint_per_tile);
						lb->buf().printf("%s", maintenance_number);
						lb->update();

						lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
						char maintenance_km_number[64];
						money_to_string(maintenance_km_number, maint_per_tile * tiles_pr_km);
						lb->buf().printf("(%s/km)", maintenance_km_number);
						lb->update();
					}
					end_table();
					add_table(1, 2)->set_spacing(scr_size(0, 1));
					{
						const sint64 change_percentage = way->get_desc()->get_maintenance() > 0 ? change*100/(sint64)way->get_desc()->get_maintenance() : 0;
						lb = new_component<gui_label_buf_t>(change_col, gui_label_t::left);
						if (change > 0) {
							lb->buf().append("+");
						}
						char maintenance_number[64];
						if (way->is_diagonal()) {
							change *= 10;
							change /= 14;
						}
						money_to_string(maintenance_number, (double)world()->get_inflation_adjusted_price(world()->get_timeline_year_month(), world()->calc_adjusted_monthly_figure(change), infrastructure) / 100.0);
						lb->buf().printf("%s", maintenance_number);
						lb->update();
						if (change_percentage!=0) {
							lb = new_component<gui_label_buf_t>(change_col, gui_label_t::left);
							lb->buf().append("(");
							if (change > 0) {
								lb->buf().append("+");
							}
							lb->buf().printf("%i%%)", change_percentage);
							lb->update();
						}
						else {
							new_component<gui_margin_t>(0,D_LABEL_HEIGHT);
						}
					}
					end_table();
				}
				else {
					new_component<gui_label_t>("-");
					new_component<gui_empty_t>();
				}
			}
			else {
				new_component_span<gui_empty_t>(3);
			}
			new_component<gui_fill_t>();

			new_component<gui_label_t>("Max. speed:");
			add_table(2,1);
			PIXVAL speed_text_color = SYSCOL_TEXT;
			if (way->get_desc()->get_topspeed() > way->get_max_speed()) {
				add_component(&speed_restricted);
				restricted_speed = way->get_max_speed();
				if (!skinverwaltung_t::alerts) {
					// to support paksets that do not have alert symbols
					speed_text_color = COL_CAUTION;
				}
			}
			else {
				new_component<gui_empty_t>();
			}
			lb = new_component<gui_label_buf_t>(speed_text_color, gui_label_t::right);
			lb->buf().printf("%ikm/h", way->get_max_speed(false));
			lb->update();
			end_table();
			if (replace_flag) {
				sint32 change;
				if (restricted_speed != SINT32_MAX_VALUE) {
					change = min(restricted_speed, replacement_way->get_topspeed()) - restricted_speed;
				}
				else {
					change = replacement_way->get_topspeed() - way->get_max_speed(false);
				}
				new_component<gui_right_pointer_t>(change>0? SYSCOL_UP_TRIANGLE : change<0 ? SYSCOL_DOWN_TRIANGLE : COL_INACTIVE);
				add_table(2,1);
				speed_text_color = SYSCOL_TEXT;
				if (replacement_way->get_topspeed() > restricted_speed) {
					add_component(&speed_restricted);
					if (!skinverwaltung_t::alerts) {
						speed_text_color = COL_CAUTION;
					}
				}
				else {
					new_component<gui_empty_t>();
				}
				lb = new_component<gui_label_buf_t>(speed_text_color, gui_label_t::right);
				lb->buf().printf("%ikm/h", restricted_speed != SINT32_MAX_VALUE ? restricted_speed : replacement_way->get_topspeed());
				lb->update();
				end_table();

				gui_label_updown_t *lb_updown = new_component<gui_label_updown_t>(change > 0 ? SYSCOL_UP_TRIANGLE : change < 0 ? SYSCOL_DOWN_TRIANGLE : SYSCOL_TEXT);
				lb_updown->set_border(0);
				lb_updown->set_value(change);
			}
			else {
				new_component_span<gui_empty_t>(3);
			}
			new_component<gui_fill_t>();


			if (way->get_desc()->get_styp() == type_elevated || wt == air_wt || wt == water_wt) {
				new_component<gui_label_t>("Max. weight:");
			}
			else {
				new_component<gui_label_t>("Max. axle load:");
			}
			lb = new_component<gui_label_buf_t>(way->get_max_axle_load() == 0 ? COL_DANGER : SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf("%u%s", way->get_max_axle_load(), translator::translate("tonnen"));
			lb->update();
			if (replace_flag) {
				const sint64 change = (sint64)replacement_way->get_max_axle_load() - (sint64)way->get_max_axle_load();
				new_component<gui_right_pointer_t>(change>0? SYSCOL_UP_TRIANGLE : change<0 ? SYSCOL_DOWN_TRIANGLE : COL_INACTIVE);
				lb = new_component<gui_label_buf_t>(replacement_way->get_max_axle_load() == 0 ? COL_DANGER : SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%u%s", replacement_way->get_max_axle_load(), translator::translate("tonnen"));
				lb->update();
				gui_label_updown_t *lb_updown = new_component<gui_label_updown_t>(change > 0 ? SYSCOL_UP_TRIANGLE : change < 0 ? SYSCOL_DOWN_TRIANGLE : SYSCOL_TEXT);
				lb_updown->set_border(0);
				lb_updown->set_value(change);
			}
			else {
				new_component_span<gui_empty_t>(3);
			}
			new_component<gui_fill_t>();

			new_component<gui_label_t>("Durability");
			char durability_string[16]; // Need to represent billions plus commas.
			number_to_string(durability_string, (long double)way->get_desc()->get_wear_capacity() / 10000.0, 4);
			lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf("%s", durability_string);
			lb->update();
			if (replace_flag) {
				const sint64 change = (sint64)replacement_way->get_wear_capacity() - (sint64)way->get_desc()->get_wear_capacity();
				const PIXVAL change_col = change>0 ? SYSCOL_UP_TRIANGLE : change<0 ? SYSCOL_DOWN_TRIANGLE : COL_INACTIVE;
				new_component<gui_right_pointer_t>(change>0? SYSCOL_UP_TRIANGLE : change<0 ? SYSCOL_DOWN_TRIANGLE : COL_INACTIVE);
				number_to_string(durability_string, (long double)replacement_way->get_wear_capacity() / 10000.0, 4);
				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%s", durability_string);
				lb->update();

				sint64 change_percentage=0;
				if (way->get_desc()->get_wear_capacity()>0) {
					change_percentage = change*100/(sint64)way->get_desc()->get_wear_capacity();
				}
				lb = new_component<gui_label_buf_t>(change_col, gui_label_t::left);
				if (change_percentage>0) {
					lb->buf().append("+");
				}
				lb->buf().printf("%i%%", change_percentage);
				lb->update();
			}
			else {
				new_component_span<gui_empty_t>(3);
			}
			new_component<gui_fill_t>();

			new_component_span<gui_border_t>(2);
			new_component_span<gui_border_t>(2);
			new_component<gui_empty_t>();
			new_component<gui_empty_t>();

			if (replacement_way != NULL && replacement_way->is_mothballed() == false) {
				new_component<gui_label_t>("renewal_costs");
				new_component<gui_empty_t>();
				new_component<gui_empty_t>();

				add_table(1,2)->set_spacing(scr_size(D_H_SPACE, 1));
				{
					char upgrade_cost_number[64];
					money_to_string(upgrade_cost_number, (double)world()->calc_adjusted_monthly_figure(way->get_desc()->get_upgrade_group() == replacement_way->get_upgrade_group() ? replacement_way->get_way_only_cost() : replacement_way->get_value()) / 100 / 2);
					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%s", upgrade_cost_number);
					lb->update();

					char upgrade_cost_pr_km_number[64];
					money_to_string(upgrade_cost_pr_km_number, (double)world()->calc_adjusted_monthly_figure(way->get_desc()->get_upgrade_group() == replacement_way->get_upgrade_group() ? replacement_way->get_way_only_cost() : replacement_way->get_value()) / 100 / 2 * tiles_pr_km);
					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("(%s/%s)", upgrade_cost_pr_km_number, translator::translate("km"));
					lb->update();
				}
				end_table();

				new_component<gui_empty_t>();
				new_component<gui_empty_t>();
			}
		}
		end_table();


		if (restricted_speed != SINT32_MAX_VALUE) {
			new_component<gui_empty_t>();
			add_table(1,0);
			{
				bool known_reason_for_speed_restriction { false };
				if( tunnel ) {
					if (way->get_max_speed() == tunnel->get_desc()->get_topspeed() || tunnel->get_desc()->get_topspeed_gradient_1() || tunnel->get_desc()->get_topspeed_gradient_2())
					{
						known_reason_for_speed_restriction = true;
						add_table(2,1);
							add_component(&speed_restricted);
							new_component<gui_label_t>("(speed_restricted_by_tunnel)", SYSCOL_TEXT_STRONG);
						end_table();
					}
				}
				else if (bridge) {
					if (way->get_max_speed() == bridge->get_desc()->get_topspeed() || bridge->get_desc()->get_topspeed_gradient_1() || bridge->get_desc()->get_topspeed_gradient_2())
					{
						known_reason_for_speed_restriction = true;
						add_table(2,1);
							add_component(&speed_restricted);
							new_component<gui_label_t>("(speed_restricted_by_bridge)", SYSCOL_TEXT_STRONG);
						end_table();
					}
				}
				else if (wayobj) {
					if (way->get_max_speed() == wayobj->get_desc()->get_topspeed() || wayobj->get_desc()->get_topspeed_gradient_1() || wayobj->get_desc()->get_topspeed_gradient_2())
					{
						known_reason_for_speed_restriction = true;
						add_table(2,1);
							add_component(&speed_restricted);
							new_component<gui_label_t>("(speed_restricted_by_wayobj)", SYSCOL_TEXT_STRONG);
						end_table();
					}
				}
				else if (way->is_degraded()) {
					known_reason_for_speed_restriction = true;
					add_table(2,1);
						add_component(&speed_restricted);
						new_component<gui_label_t>("(speed_restricted_by_degradation)", SYSCOL_TEXT_STRONG);
					end_table();
				}
				else if (way->is_crossing()) {
					known_reason_for_speed_restriction = true;
					add_table(2,1);
						add_component(&speed_restricted);
						new_component<gui_label_t>("(speed_restricted_by_crossing)", SYSCOL_TEXT);
					end_table();
				}

				if ( ( wt == road_wt ) && way->hat_gehweg() ) {
						known_reason_for_speed_restriction = true;
						add_table(2,1);
						{
							add_component(&speed_restricted);
							new_component<gui_label_t>("(speed_restricted_by_city)", SYSCOL_TEXT_STRONG);
						}
						end_table();
				}

				if ( gr->get_weg_hang() ) {
					if ( ( slope_t::is_single(gr->get_weg_hang()) && ( way->get_desc()->get_topspeed_gradient_1() == 0 ) ) || ( slope_t::is_doubles(gr->get_weg_hang()) && ( way->get_desc()->get_topspeed_gradient_2() == 0 ) ) ) {
						known_reason_for_speed_restriction = true;
						add_table(2,1);
						{
							add_component(&speed_restricted);
							new_component<gui_label_t>("(way_blocked_by_slope)", SYSCOL_TEXT_STRONG);
						}
						end_table();
					} else if ( ( slope_t::is_single(gr->get_weg_hang()) && ( way->get_desc()->get_topspeed_gradient_1() < way->get_desc()->get_topspeed() ) ) || ( slope_t::is_doubles(gr->get_weg_hang()) && ( way->get_desc()->get_topspeed_gradient_2() < way->get_desc()->get_topspeed() ) ) ) {
						known_reason_for_speed_restriction = true;
						add_table(2,1);
						{
							add_component(&speed_restricted);
							new_component<gui_label_t>("(speed_restricted_by_slope)", SYSCOL_TEXT);
						}
						end_table();
  					}
				}

				if ( known_reason_for_speed_restriction == false ) {
					add_table(2,1);
					{
						add_component(&speed_restricted);
						new_component<gui_label_t>("(speed_restricted_fallback)", SYSCOL_TEXT_STRONG);
					}
					end_table();
				}
			}
			end_table();
		}

		if (char const* const maker = way->get_desc()->get_copyright()) {
			new_component<gui_margin_t>(1, LINESPACE);
			gui_label_buf_t *lb_maker = new_component<gui_label_buf_t>();
			lb_maker->buf().printf(translator::translate("Constructed by %s"), maker);
			lb_maker->update();
		}

		new_component<gui_empty_t>();

		//update_stats();
		set_size(get_min_size());

		gui_aligned_container_t::draw(offset);
	}
}



way_info_t::way_info_t(const grund_t* gr_) :
	gui_frame_t("", NULL),
	gr(gr_),
	way_view(koord3d::invalid, scr_size( max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width()*7)/8) ) ),
	cont_way1(NULL),
	cont_way2(NULL),
	scrolly(&cont, true),
	scrolly_way1(&cont_way1, true),
	scrolly_way2(&cont_way2, true),
	scrolly_road_routes(&cont_road_routes, true)
{
	way1 = gr->get_weg_nr(0);
	way2 = NULL;

	condition_bar1.set_base(100);
	condition_bar2.set_base(100);
	condition_bar1.set_size(scr_size(condition_bar1.get_size().w, 3));
	condition_bar2.set_size(scr_size(condition_bar2.get_size().w, 3));
	condition_bar2.set_visible(false);
	condition_bar2.set_rigid(true);

	condition_bar1.add_color_value(&condition1, color_idx_to_rgb(COL_GREEN));
	condition_bar1.add_color_value(&degraded_cond1, color_idx_to_rgb(COL_ORANGE_RED));
	condition_bar2.add_color_value(&condition2, color_idx_to_rgb(COL_GREEN));
	condition_bar2.add_color_value(&degraded_cond2, color_idx_to_rgb(COL_ORANGE_RED));

	speed_restricted.set_image(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2):IMG_EMPTY, true);
	speed_restricted.set_tooltip( translator::translate(speed_resticted_text) );

	const obj_t *const d = gr->obj_bei(0);
	if (d != NULL) {
		set_owner(d->get_owner());
	}
	update_way_info();

	set_table_layout(1,0);

	add_table(2,0)->set_alignment(ALIGN_TOP);
	{
		// left
		add_component(&cont);
		cont.set_table_layout(1,0);

		// right
		way_view.set_location(gr->get_pos());
		add_component(&way_view);
	}
	end_table();

	// reserved convoy
	add_component(&lb_is_reserved);
	add_component(&cont_reserved_convoy);

	cont_reserved_convoy.set_table_layout(1,0);

	cont_reserved_convoy.add_table(3,0)->set_spacing(scr_size(D_H_SPACE,1));
	{
		reserving_vehicle_button.init(button_t::posbutton, NULL);
		reserving_vehicle_button.set_tooltip("goto_vehicle");
		reserving_vehicle_button.add_listener(this);
		cont_reserved_convoy.add_component(&reserving_vehicle_button);
		cont_reserved_convoy.add_component(&lb_reserved_convoy);
		cont_reserved_convoy.add_component(&lb_reserved_cnv_speed);

		cont_reserved_convoy.new_component<gui_empty_t>();
		cont_reserved_convoy.add_component(&lb_reserved_cnv_distance);
		cont_reserved_convoy.new_component<gui_fill_t>();

		cont_reserved_convoy.new_component<gui_empty_t>();
		cont_reserved_convoy.add_component(&lb_signal_wm);
		cont_reserved_convoy.new_component<gui_empty_t>(); // UI TODO: replace with signal direction arrow component
	}
	cont_reserved_convoy.end_table();

	init_tabs();
	add_component(&tabs);

	update();
	set_resizemode(diagonal_resize);
	set_windowsize(scr_size(get_min_windowsize().w, D_TITLEBAR_HEIGHT + D_TAB_HEADER_HEIGHT + tabs.get_pos().y + LINESPACE*18));
}

bool way_info_t::is_weltpos()
{
	return ( welt->get_viewport()->is_on_center( gr->get_pos() ) );
}

void  way_info_t::init_tabs()
{
	tabs.clear();
	cont_way2.set_way(NULL);
	cont_way1.set_way(way1);

	waytype_t wt = way1->get_desc()->get_styp() == type_tram ? tram_wt : way1->get_waytype();
	tabs.add_tab(&scrolly_way1, way1->get_name(), skinverwaltung_t::get_waytype_skin(wt), 0, world()->get_settings().get_waytype_color(wt));
	if (has_way==2) {
		cont_way2.set_way(way2);
		wt = way2->get_desc()->get_styp() == type_tram ? tram_wt : way1->get_waytype();
		tabs.add_tab(&scrolly_way2, way2->get_name(), skinverwaltung_t::get_waytype_skin(wt), 0, world()->get_settings().get_waytype_color(wt));
	}
}

void way_info_t::update()
{
	bool update_way_flag = false;
	has_way = (gr->get_weg_nr(0) != NULL) + (gr->get_weg_nr(1) != NULL);
	bool old_has_load = has_road;

	if (way1 != gr->get_weg_nr(0)) {
		way1 = gr->get_weg_nr(0);
		update_way_flag = true;
	}
	if (way1 == NULL) {
		destroy_win(this);
		return;
	}
	if (way2 != gr->get_weg_nr(1)) {
		way2 = gr->get_weg_nr(1);
		update_way_flag = true;
	}
	condition_bar2.set_visible(way2!=NULL);
	if (way2 == NULL) {
		condition2 = 0, degraded_cond2 = 0;
	}

	has_road = (way1->get_waytype() == road_wt || (way2!=NULL && way2->get_waytype() == road_wt));
	if (old_has_load != has_road) {
		// update tab
		cont_road_routes.set_table_layout(1,0);
		update_way_flag = true;
	}
	else if (!has_road && old_has_load) {
		cont_road_routes.remove_all();
	}

	if (update_way_flag) {
		init_tabs();
		update_way_info();
		if (has_road) {
			tabs.add_tab(&scrolly_road_routes, translator::translate("Road routes from here:"));
		}
	}

	cont.remove_all();

	// name(s)
	cont.add_table(3,0)->set_spacing(scr_size(D_H_SPACE, 0));
	{
		cont.new_component<gui_image_t>(skinverwaltung_t::get_waytype_skin(way1->get_desc()->get_styp() == type_tram ? tram_wt : way1->get_waytype())->get_image_id(0),
			way1->get_owner() == NULL ? 1 : way1->get_owner()->get_player_nr(), ALIGN_NONE, true);
		cont.new_component<gui_label_t>(way1->get_name(), way1->get_owner() == NULL ? SYSCOL_TEXT : color_idx_to_rgb(way1->get_owner()->get_player_color1()+env_t::gui_player_color_dark));
		if (way1->is_electrified()) {
			cont.new_component<gui_image_t>(skinverwaltung_t::electricity->get_image_id(0), 0, ALIGN_NONE, true);
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		if (way2) {
			cont.new_component<gui_image_t>(skinverwaltung_t::get_waytype_skin(way2->get_desc()->get_styp() == type_tram ? tram_wt : way2->get_waytype())->get_image_id(0),
				way2->get_owner() == NULL ? 1 : way2->get_owner()->get_player_nr(), ALIGN_NONE, true);
			cont.new_component<gui_label_t>(way2->get_name(), way2->get_owner() == NULL ? SYSCOL_TEXT : color_idx_to_rgb(way2->get_owner()->get_player_color1()+env_t::gui_player_color_dark));
			if (way2->is_electrified()) {
				cont.new_component<gui_image_t>(skinverwaltung_t::electricity->get_image_id(0), 0, ALIGN_NONE, true);
			}
			else {
				cont.new_component<gui_empty_t>();
			}
		}
	}
	cont.end_table();

	if (way1->get_waytype() == air_wt && way1->get_desc()->get_styp() == type_runway) {
		weg_t::runway_directions run_dirs = way1->get_runway_directions();
		cont.add_table(2,0)->set_spacing(scr_size(D_H_SPACE, 0));
		{
			if (run_dirs.runway_36_18)
			{
				const double runway_meters_36_18 = welt->tiles_to_km(way1->get_runway_length(true))*1000.0;
				cont.new_component<gui_label_t>("runway_36/18");

				gui_label_buf_t *lb_runway_36_18 = cont.new_component<gui_label_buf_t>();
				lb_runway_36_18->buf().printf(": %.0f%s", runway_meters_36_18, translator::translate("meter"));
				lb_runway_36_18->update();
			}
			if (run_dirs.runway_9_27)
			{
				const double runway_meters_9_27 = welt->tiles_to_km(way1->get_runway_length(false))*1000.0;
				cont.new_component<gui_label_t>("runway_09/27");

				gui_label_buf_t *lb_runway_9_27 = cont.new_component<gui_label_buf_t>();
				lb_runway_9_27->buf().printf(": %.0f%s", runway_meters_9_27, translator::translate("meter"));
				lb_runway_9_27->update();
			}
		}
		cont.end_table();
	}

	// location
	const stadt_t* city = welt->get_city(gr->get_pos().get_2d());
	std::string region = welt->get_region_name(gr->get_pos().get_2d());
	cont.add_table(city ? 4:3, 1);
	{
		if (city) {
			cont.new_component<gui_image_t>(skinverwaltung_t::intown->get_image_id(0), 0, ALIGN_NONE, true);
			cont.new_component<gui_label_t>(city->get_name());
		}
		else {
			cont.new_component<gui_label_t>("Open countryside");
		}
		if (!region.empty()) {
			cont.new_component<gui_label_buf_t>()->buf().printf( "(%s)", translator::translate( region.c_str() ) );
		}
		cont.new_component<gui_fill_t>();
	}
	cont.end_table();

	char price[64];
	money_to_string(price, abs( welt->get_land_value(gr->get_pos()) )/100.0);
	gui_label_buf_t *lb = cont.new_component<gui_label_buf_t>();
	lb->buf().printf("%s: %s", translator::translate("Land value"), price);
	lb->update();

	cont.add_table(5,0)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE / 2));
	{
		// symbol
		cont.new_component<gui_empty_t>();
		cont.add_table(3,1); {
			cont.new_component<gui_fill_t>();
			cont.new_component<gui_image_t>(skinverwaltung_t::get_waytype_skin(way1->get_desc()->get_styp() == type_tram ? tram_wt : way1->get_waytype())->get_image_id(0),
				way1->get_owner() == NULL ? 1 : way1->get_owner()->get_player_nr(), ALIGN_NONE, true);
			cont.new_component<gui_fill_t>();
		} cont.end_table();
		cont.add_table(3,1); {
			cont.new_component<gui_fill_t>();
			cont.new_component<gui_image_t>(way2 == NULL ? IMG_EMPTY : skinverwaltung_t::get_waytype_skin(way2->get_desc()->get_styp() == type_tram ? tram_wt : way2->get_waytype())->get_image_id(0), 0, ALIGN_NONE, true);
			cont.new_component<gui_fill_t>();
		} cont.end_table();
		cont.new_component<gui_empty_t>();
		cont.new_component<gui_empty_t>();

		cont.new_component<gui_label_t>("Max. speed:");

		bool has_electrification_speed_limit = false;

		cont.add_table(2,1);
		PIXVAL speed_text_color = SYSCOL_TEXT;
		if(way1->get_desc()->get_topspeed() > way1->get_max_speed(false)){
			cont.add_component(&speed_restricted);
			if (!skinverwaltung_t::alerts) {
				// to support paksets that do not have alert symbols
				speed_text_color = COL_CAUTION;
			}
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		lb = cont.new_component<gui_label_buf_t>(speed_text_color, gui_label_t::right);
		lb->buf().printf("%ukm/h", way1->get_max_speed(false));
		lb->update();
		cont.end_table();

		if (way1->is_electrified() && way1->get_max_speed(false) != way1->get_max_speed(true)) {
			has_electrification_speed_limit = true;
		}

		if (way2) {
			cont.add_table(2,1);
			 speed_text_color = SYSCOL_TEXT;
			if (way2->get_desc()->get_topspeed() > way2->get_max_speed(false)) {
				cont.add_component(&speed_restricted);
				if (!skinverwaltung_t::alerts) {
					speed_text_color = COL_CAUTION;
				}
			}
			else {
				cont.new_component<gui_empty_t>();
			}
			lb = cont.new_component<gui_label_buf_t>(speed_text_color, gui_label_t::right);
			lb->buf().printf("%ukm/h", way2->get_max_speed(false));
			lb->update();
			cont.end_table();
			if (!has_electrification_speed_limit && way2->is_electrified() && way2->get_max_speed(false) != way2->get_max_speed(true)) {
				has_electrification_speed_limit = true;
			}
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		cont.new_component<gui_empty_t>(); // for alert
		cont.new_component<gui_fill_t>();

		if (has_electrification_speed_limit) {
			// show electrification speed limit
			cont.new_component<gui_empty_t>();
			if (way1->is_electrified() && way1->get_max_speed(false) != way1->get_max_speed(true)) {
				cont.add_table(2,1);
				{
					cont.new_component<gui_image_t>(skinverwaltung_t::electricity->get_image_id(0), 0, ALIGN_NONE, true);
					lb = cont.new_component<gui_label_buf_t>(COL_WARNING, gui_label_t::right);
					lb->buf().printf("%ukm/h", way1->get_max_speed(true));
					lb->update();
				}
				cont.end_table();
			}
			else {
				cont.new_component<gui_empty_t>();
			}
			if (way2) {
				if (way2->is_electrified() && way2->get_max_speed(false) != way2->get_max_speed(true)) {
					cont.add_table(2, 1);
					{
						cont.new_component<gui_image_t>(skinverwaltung_t::electricity->get_image_id(0), 0, ALIGN_NONE, true);
						lb = cont.new_component<gui_label_buf_t>(COL_WARNING, gui_label_t::right);
						lb->buf().printf("%ukm/h", way2->get_max_speed(true));
						lb->update();
					}
					cont.end_table();
				}
				else {
					cont.new_component<gui_empty_t>();
				}
			}
			else {
				cont.new_component<gui_empty_t>();
			}
			cont.new_component<gui_empty_t>(); // for alert
			cont.new_component<gui_fill_t>();
		}

		// weight limit
		if (way1->get_desc()->get_styp() == type_elevated || way1->get_waytype() == air_wt || way1->get_waytype() == water_wt
			|| (way2 && (way2->get_desc()->get_styp() == type_elevated || way2->get_waytype() == air_wt || way2->get_waytype() == water_wt))) {
			cont.new_component<gui_label_t>("Max. weight:");
		}
		else {
			cont.new_component<gui_label_t>("Max. axle load:");
		}
		lb = cont.new_component<gui_label_buf_t>(way1->get_max_axle_load() == 0 ? COL_DANGER : SYSCOL_TEXT, gui_label_t::right);
		lb->buf().printf("%u%s", way1->get_max_axle_load(), translator::translate("tonnen"));
		lb->update();
		if (way2) {
			lb = cont.new_component<gui_label_buf_t>(way2->get_max_axle_load() == 0 ? COL_DANGER : SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf("%u%s", way2->get_max_axle_load(), translator::translate("tonnen"));
			lb->update();
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		cont.new_component<gui_empty_t>(); // for alert
		cont.new_component<gui_fill_t>();

		// Bridge's weight limit
		if (way1->get_bridge_weight_limit() < UINT32_MAX_VALUE)
		{
			cont.new_component<gui_label_t>("Max. weight:");
			lb = cont.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf("%u%s", way1->get_bridge_weight_limit(), translator::translate("tonnen"));
			lb->update();
			cont.new_component_span<gui_empty_t>(2);
			cont.new_component<gui_fill_t>();
		}

		lb = cont.new_component<gui_label_buf_t>();
		lb->buf().printf("%s:", translator::translate("Condition"));
		lb->update();
		cont.add_table(1,2)->set_spacing(scr_size(0,0));
		{
			lb = cont.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			condition1 = (sint32)way1->get_condition_percent();
			if (condition1 == 0 && way1->get_remaining_wear_capacity() > 0) {
				lb->buf().append("< 1%%");
				condition1 = 1;
			}
			else {
				lb->buf().printf("%i%%", condition1);
			}
			degraded_cond1 = (way1->is_degraded()) ? condition1 : 0;
			lb->update();
			cont.add_component(&condition_bar1);
		}
		cont.end_table();
		if (way2) {
			cont.add_table(1, 2)->set_spacing(scr_size(0, 0));
			{
				lb = cont.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				condition2 = (sint32)way2->get_condition_percent();
				if (condition2 == 0 && way2->get_remaining_wear_capacity() > 0) {
					lb->buf().append("< 1%%");
					condition2 = 1;
				}
				else {
					lb->buf().printf("%i%%", condition2);
				}
				degraded_cond2 = (way2->is_degraded()) ? condition2 : 0;
				lb->update();
				cont.add_component(&condition_bar2);
			}
			cont.end_table();
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		cont.new_component<gui_empty_t>(); // for alert
		cont.new_component<gui_fill_t>();

		cont.new_component<gui_label_t>("convoi passed last month:");
		lb = cont.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->buf().printf("%u", way1->get_statistics(WAY_STAT_LAST_MONTH, WAY_STAT_CONVOIS));
		lb->update();
		if (way2) {
			lb = cont.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
			lb->buf().printf("%u", way2->get_statistics(WAY_STAT_LAST_MONTH, WAY_STAT_CONVOIS));
			lb->update();
		}
		else {
			cont.new_component<gui_empty_t>();
		}
		cont.new_component<gui_empty_t>(); // for alert
		cont.new_component<gui_fill_t>();

		// show reserved convoy
		bool directional_reserve = false;
		// which "way" object is the rail? - 0=nothing, 1=way1, 2=way2
		const uint8 rail_index = way1->is_rail_type() ? 1 : (way2 && way2->is_rail_type()) ? 2 : 0;

		reserved_convoi = get_reserved_convoy(way1);
		if( !reserved_convoi.is_bound() && way2 != NULL ) {
			reserved_convoi = get_reserved_convoy(way2);
		}
		lb_is_reserved.set_visible( way1->get_waytype()==air_wt || rail_index );
		lb_is_reserved.set_rigid(   way1->get_waytype()==air_wt || rail_index );
		cont_reserved_convoy.set_rigid(way1->get_waytype() == air_wt || rail_index);
		cont_reserved_convoy.set_visible( reserved_convoi.is_bound() );
		lb_signal_wm.set_visible( rail_index>0 );
		lb_signal_wm.set_rigid(   rail_index>0 );

		if( reserved_convoi.is_bound() ) {
			//lb_is_reserved.buf().append(translator::translate("\nis reserved by:")); // Note: remove line breaks from this translation
			lb_is_reserved.buf().append(translator::translate("is reserved by:"));
			lb_is_reserved.set_color(SYSCOL_TEXT);
			rail_vehicle_t* rail_vehicle = NULL;
			if( rail_index ) {
				rail_vehicle = (rail_vehicle_t*)reserved_convoi->front();
				const schiene_t *rail = rail_index==1 ? (schiene_t *)way1 : (schiene_t *)way2;
				if (rail->get_reservation_type() == schiene_t::directional) {
					directional_reserve=true;
				}
			}

			lb_reserved_convoy.buf().printf("%s", translator::translate(reserved_convoi->get_name()));
			lb_reserved_convoy.set_color( color_idx_to_rgb(reserved_convoi->get_owner()->get_player_color1()+env_t::gui_player_color_dark) );

			// working method
			if( rail_vehicle ) {
				lb_signal_wm.buf().printf("%s", translator::translate( roadsign_t::get_working_method_name(rail_vehicle->get_working_method()) ));

				if( directional_reserve ) {
					// UI TODO: add blue signal arrow image
					//translator::translate("reservation_heading")
					//get_reserved_direction() ??? - unconfirmed if it matches the new arrow display

				}


				lb_reserved_cnv_speed.buf().printf("(%s, %ukm/h)", translator::translate(roadsign_t::get_directions_name(rail_vehicle->get_direction())), speed_to_kmh(reserved_convoi->get_akt_speed()));
				lb_reserved_cnv_speed.update();

				lb_reserved_cnv_distance.buf().printf("%s:", translator::translate("distance_to_vehicle"));

				koord3d vehpos = reserved_convoi->get_pos();
				const double km_to_vehicle = welt->tiles_to_km(shortest_distance(gr->get_pos().get_2d(), vehpos.get_2d()));
				if (km_to_vehicle < 1)
				{
					float m_to_vehicle = km_to_vehicle * 1000;
					lb_reserved_cnv_distance.buf().append(m_to_vehicle);
					lb_reserved_cnv_distance.buf().append("m");
				}
				else
				{
					uint n_actual;
					if (km_to_vehicle < 20)
					{
						n_actual = 1;
					}
					else
					{
						n_actual = 0;
					}
					char number_actual[10];
					number_to_string(number_actual, km_to_vehicle, n_actual);
					lb_reserved_cnv_distance.buf().append(number_actual);
					lb_reserved_cnv_distance.buf().append("km");
				}
			}
			lb_reserved_cnv_distance.update();
		}
		else {
			if(  way1->get_desc()->get_waytype() == air_wt  ||  (way2 && way2->get_desc()->get_waytype() == air_wt)  ) {
				lb_is_reserved.buf().append(translator::translate("runway_not_reserved"));
			}
			else {
				lb_is_reserved.buf().append(translator::translate("track_not_reserved"));
			}
			lb_is_reserved.set_color(SYSCOL_TEXT_WEAK);
		}
		lb_is_reserved.update();
		lb_reserved_convoy.update();
		lb_signal_wm.update();

	}
	cont.end_table();

	reset_min_windowsize();
}

convoihandle_t way_info_t::get_reserved_convoy(const weg_t *way) const
{
	if (way == NULL) {
		return convoihandle_t();
	}
	waytype_t wt = way->get_waytype();
	if (wt == track_wt || wt == monorail_wt || wt == maglev_wt || wt == tram_wt || wt == narrowgauge_wt || wt == air_wt) {
		const schiene_t *rail = static_cast<const schiene_t *>(way);
		return rail->get_reserved_convoi();
	}
	return convoihandle_t();
}

void way_info_t::update_way_info()
{
	gui_frame_t::set_name( translator::translate(gr->get_name()) );

	if(has_road){
		cont_road_routes.remove_all();

		cont_road_routes.add_component(&lb_city_count);
		cont_road_routes.add_table(3+(!welt->get_settings().regions.empty()),0)->set_spacing(scr_size(D_H_SPACE, 0));
		weg_t *road = way1->get_waytype() == road_wt ? way1 : way2;
		uint32 cities_count = 0;
		building_list.clear();
		for(uint8 i=0;i<5;i++) {
			for(uint32 j=0;j<road->private_car_routes[road->private_car_routes_currently_reading_element][i].get_count();j++){
				const koord dest = road->private_car_routes[road->private_car_routes_currently_reading_element][i][j];
				const grund_t* gr_temp = welt->lookup_kartenboden(dest);

				if( gr_temp && gr_temp->get_building() ){
					building_list.append(dest);
					continue;
				}
				else {
					dbg->message("way_info_t::update_way_info()", "Building that is a destination of a road route not found");
				}

				const stadt_t* dest_city = welt->get_city(dest);
				if (dest_city && dest == dest_city->get_townhall_road())
				{
					cities_count++;
					button_t *b = cont_road_routes.new_component<button_t>();
					b->set_typ(button_t::posbutton_automatic);
					b->set_targetpos(dest_city->get_pos());

					cont_road_routes.new_component<gui_label_t>(dest_city->get_name());

					// region
					if (!welt->get_settings().regions.empty()) {
						gui_label_buf_t *lb_region = cont_road_routes.new_component<gui_label_buf_t>();
						lb_region->buf().printf(" (%s)", translator::translate(welt->get_region_name(dest_city->get_pos()).c_str()));
						lb_region->update();
					}

					// distance
					const uint32 distance = shortest_distance(gr->get_pos().get_2d(), dest_city->get_pos()) * welt->get_settings().get_meters_per_tile();
					gui_label_buf_t *lb_city = cont_road_routes.new_component<gui_label_buf_t>();
					if (distance < 1000) {
						lb_city->buf().printf("%um", distance);
					}
					else if (distance < 20000) {
						lb_city->buf().printf("%.1fkm", (double)distance / 1000.0);
					}
					else {
						lb_city->buf().printf("%ukm", distance / 1000);
					}
					lb_city->update();
				}

			}
		}
		lb_city_count.buf().printf(translator::translate("%u cities"), cities_count);
		lb_city_count.update();

		cont_road_routes.end_table();

		cont_road_routes.new_component<gui_empty_t>();

		gui_label_buf_t *lb = cont_road_routes.new_component<gui_label_buf_t>();
		lb->buf().printf("%u buildings", building_list.get_count());
		lb->update();

		if (building_list.get_count()) {
			cont_road_routes.add_table(4+(!welt->get_settings().regions.empty()),0)->set_spacing(scr_size(D_H_SPACE,0));
			{
				FOR(vector_tpl<koord>, k, building_list) {
					const gebaeude_t* building = welt->lookup_kartenboden(k)->get_building();
					button_t *b = cont_road_routes.new_component<button_t>();
					b->set_typ(button_t::posbutton_automatic);
					b->set_targetpos(k);
					cont_road_routes.new_component<gui_label_t>(translator::translate(building->get_individual_name()));

					if (building->get_stadt() != NULL) {
						cont_road_routes.new_component<gui_label_t>(building->get_stadt()->get_name());
					}
					else {
						cont_road_routes.new_component<gui_empty_t>();
					}

					// region
					if (!welt->get_settings().regions.empty()) {
						lb = cont_road_routes.new_component<gui_label_buf_t>();
						lb->buf().printf(" (%s)", translator::translate(welt->get_region_name(k).c_str()));
						lb->update();
					}

					// distance
					const uint32 distance = shortest_distance(gr->get_pos().get_2d(), k) * welt->get_settings().get_meters_per_tile();
					lb = cont_road_routes.new_component<gui_label_buf_t>();
					if (distance < 1000) {
						lb->buf().printf("%um", distance);
					}
					else if (distance < 20000) {
						lb->buf().printf("%.1fkm", (double)distance / 1000.0);
					}
					else {
						lb->buf().printf("%ukm", distance / 1000);
					}
					lb->update();

				}
			}
			cont_road_routes.end_table();
		}


	}
}


void way_info_t::draw(scr_coord pos, scr_size size)
{
	if(way1 == NULL){
		destroy_win(this);
		return;
	}
	update();

	gui_frame_t::draw(pos, size);
}

void way_info_t::map_rotate90(sint16 new_ysize)
{
	way_view.map_rotate90(new_ysize);
}

bool way_info_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if (comp == &reserving_vehicle_button) {
		if( reserved_convoi.is_bound() ) {
			reserved_convoi->show_info();
			return true;
		}
	}
	return false;
}
