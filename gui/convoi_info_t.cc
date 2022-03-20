/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "convoi_info_t.h"
#include "minimap.h"
#include "replace_frame.h"
#include "vehicle_class_manager.h"

#include "../vehicle/air_vehicle.h"
#include "../vehicle/rail_vehicle.h"
#include "../simcolor.h"
#include "../display/viewport.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simhalt.h"
#include "simwin.h"
#include "../convoy.h"

#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../dataobj/loadsave.h"
#include "../simconvoi.h"
#include "../simline.h"

#include "../player/simplay.h"

#include "../utils/simstring.h"
#include "convoi_detail_t.h"

#include "../obj/roadsign.h"
#include "components/gui_vehicle_capacitybar.h"


#define CHART_HEIGHT (100)

sint16 convoi_info_t::tabstate = -1;

static const char cost_type[BUTTON_COUNT][64] =
{
	"Free Capacity",
	"Pax-km",
	"Mail-km",
	"Freight-km", // ton-km
	"Distance",
	"Average speed",
	"Comfort",
	"Revenue",
	"Operation",
	"Refunds",
	"Road toll",
	"Profit"
};

static const uint8 cost_type_color[BUTTON_COUNT] =
{
	COL_FREE_CAPACITY,
	COL_LIGHT_PURPLE,
	COL_TRANSPORTED,
	COL_BROWN,
	COL_DISTANCE,
	COL_AVERAGE_SPEED,
	COL_COMFORT,
	COL_REVENUE,
	COL_OPERATION,
	COL_CAR_OWNERSHIP,
	COL_TOLL,
	COL_PROFIT
};

static const uint8 cost_type_money[BUTTON_COUNT] =
{
	gui_chart_t::STANDARD,
	gui_chart_t::PAX_KM,
	gui_chart_t::KG_KM,
	gui_chart_t::TON_KM,
	gui_chart_t::DISTANCE,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::MONEY,
	gui_chart_t::MONEY,
	gui_chart_t::MONEY,
	gui_chart_t::MONEY,
	gui_chart_t::MONEY
};

static uint8 statistic[convoi_t::MAX_CONVOI_COST] = {
	convoi_t::CONVOI_CAPACITY, convoi_t::CONVOI_PAX_DISTANCE, convoi_t::CONVOI_MAIL_DISTANCE, convoi_t::CONVOI_PAYLOAD_DISTANCE,
	convoi_t::CONVOI_DISTANCE, convoi_t::CONVOI_AVERAGE_SPEED, convoi_t::CONVOI_COMFORT, convoi_t::CONVOI_REVENUE,
	convoi_t::CONVOI_OPERATIONS, convoi_t::CONVOI_REFUNDS, convoi_t::CONVOI_WAYTOLL, convoi_t::CONVOI_PROFIT
};



//bool convoi_info_t::route_search_in_progress=false;

/**
 * This variable defines by which column the table is sorted
 * Values: 0 = destination
 *                 1 = via
 *                 2 = via_amount
 *                 3 = amount
 */
const char *convoi_info_t::sort_text[SORT_MODES] =
{
	"Zielort",
	"via",
	"via Menge",
	"Menge",
	"origin (detail)",
	"origin (amount)",
	"destination (detail)",
	"wealth (detail)",
	"wealth (via)",
	"accommodation (detail)",
	"accommodation (via)"
};



convoi_info_t::convoi_info_t(convoihandle_t cnv) :
	gui_frame_t(""),
	text(&freight_info),
	view(scr_size(max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	loading_bar(cnv),
	next_halt_number(-1),
	cont_times_history(linehandle_t(), cnv),
	loading_info(linehandle_t(), cnv),
	scroll_freight(&container_freight, true, true),
	scroll_times_history(&cont_times_history, true),
	lc_preview(0)
{
	if (cnv.is_bound()) {
		init(cnv);
	}
}

void convoi_info_t::init(convoihandle_t cnv)
{
	this->cnv = cnv;
	this->mean_convoi_speed = speed_to_kmh(cnv->get_akt_speed()*4);
	this->max_convoi_speed = speed_to_kmh(cnv->get_min_top_speed()*4);
	gui_frame_t::set_name(cnv->get_name());
	gui_frame_t::set_owner(cnv->get_owner());
	cont_times_history.set_convoy(cnv);
	loading_info.set_convoy(cnv);

	minimap_t::get_instance()->set_selected_cnv(cnv);
	set_table_layout(1,0);
	set_margin( scr_size(D_MARGIN_LEFT,D_V_SPACE), scr_size(0,D_MARGIN_BOTTOM) );

	// top part: speedbars, view, buttons
	container_top = add_table(2,1);
	{
		container_top->set_alignment(ALIGN_TOP);
		add_table(1,0);
		{
			new_component<gui_empty_t>();
			input.add_listener(this);
			input.set_size(input.get_min_size());
			reset_cnv_name();
			add_component(&input);

			cont_speed.set_table_layout(3,1);
			{
				speed_label.init();
				cont_speed.add_component(&speed_label);
				cont_speed.new_component<gui_fill_t>();
				speed_bar.set_rigid(true);
				speed_bar.set_width(100);
				cont_speed.add_component(&speed_bar);
			}
			cont_speed.set_rigid(false);
			add_component(&cont_speed);

			cont_alert.set_table_layout(2,1);
			{
				img_alert.set_image(IMG_EMPTY);
				cont_alert.add_component(&img_alert);
				cont_alert.add_component(&alert_label);
			}
			add_component(&cont_alert);
			cont_alert.set_rigid(false);

			add_component(&weight_label);

			add_table(3,1);
			{
				new_component<gui_label_t>("Gewinn");
				profit_label.set_align(gui_label_t::left);
				add_component(&profit_label);
				add_component(&running_cost_label);
			}
			end_table();

			add_component(&container_line);

			container_line.set_table_layout(5,1);
			{
				container_line.add_component(&line_button);
				container_line.new_component<gui_label_t>("Serves Line:");
				lc_preview.set_rigid(false);
				container_line.add_component(&lc_preview);
				container_line.add_component(&line_label);
				if (skinverwaltung_t::reverse_arrows) {
					img_reverse_route.set_image(cnv->get_schedule()->is_mirrored() ? skinverwaltung_t::reverse_arrows->get_image_id(0) : skinverwaltung_t::reverse_arrows->get_image_id(1), true);
					img_reverse_route.set_visible(cnv->get_reverse_schedule());
					img_reverse_route.set_rigid(true);
					container_line.add_component(&img_reverse_route);
				}
				else {
					container_line.new_component<gui_empty_t>();
				}
				// goto line button
				line_button.init( button_t::arrowright, NULL );
				line_button.set_targetpos3d( koord3d::invalid );
				line_button.add_listener( this );
				line_bound = false;
			}

			cont_next_stop.set_table_layout(2,3);
			cont_next_stop.set_alignment(ALIGN_CENTER_V);
			{
				cont_next_stop.add_table(2,1);
				{
					cont_next_stop.new_component<gui_label_t>("Fahrtziel"); // "Destination"
					cont_next_stop.add_component(&next_halt_number);
				}
				cont_next_stop.end_table();
				// Note: Fix the width of the station name as the automatic resizing destroys the layout when the station name changes
				// (or it will automatically increase the size independently of the player's intention.)
				target_label.set_fixed_width(LINEASCENT*20);
				cont_next_stop.add_component(&target_label);

				distance_label.set_fixed_width(D_BUTTON_WIDTH);
				distance_label.set_align(gui_label_t::right);
				cont_next_stop.add_component(&distance_label);
				route_bar.set_width(LINEASCENT*20);
				cont_next_stop.add_component(&route_bar);

				const waytype_t wt = cnv->front()->get_waytype();
				if (wt == track_wt || wt == maglev_wt || wt == tram_wt || wt == narrowgauge_wt || wt == monorail_wt) {
					lb_working_method.set_align(gui_label_t::right);
					lb_working_method.set_fixed_width(LINEASCENT*20 + D_BUTTON_WIDTH);
					cont_next_stop.add_component(&lb_working_method,2);
				}
			}
			add_component(&cont_next_stop);
		}
		end_table();

		container_view = add_table(2,0);
		container_view->set_alignment(ALIGN_TOP);
		container_view->set_spacing(scr_size(0,0));
		{
			add_component(&view);
			view.set_obj(cnv->front());

			// access buttons
			cont_access_buttons.set_table_layout(1,0);
			cont_access_buttons.set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
			cont_access_buttons.set_spacing( scr_size(0,0) );
			follow_button.init(button_t::imagebox_state, NULL);
			follow_button.set_image( skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_GOTOPOS) );
			follow_button.set_tooltip("Follow the convoi on the map.");
			follow_button.add_listener(this);
			add_component(&cont_access_buttons);

			loading_bar.set_fixed_size(view.get_size().w);
			add_component(&loading_bar);
			new_component<gui_empty_t>();

			if( skinverwaltung_t::details ) {
				bt_open_detail.init(button_t::imagebox_state, NULL);
				bt_open_detail.set_image(skinverwaltung_t::details->get_image_id(0));
				cont_access_buttons.add_component(&bt_open_detail);
			}
			else {
				bt_open_detail.init(button_t::roundbox | button_t::flexible, "Details");
				if (skinverwaltung_t::open_window) {
					bt_open_detail.set_image(skinverwaltung_t::open_window->get_image_id(0));
					bt_open_detail.set_image_position_right(true);
				}
				add_component(&bt_open_detail);
				new_component<gui_empty_t>();
			}
			bt_open_detail.set_tooltip("Vehicle details");
			bt_open_detail.add_listener(this);

			cont_access_buttons.add_component(&follow_button);

			go_home_button.init(button_t::depot_state, NULL);
			go_home_button.set_tooltip("Sends the convoi to the last depot it departed from!");
			go_home_button.set_targetpos3d(cnv->get_home_depot());
			go_home_button.add_listener(this);
			cont_access_buttons.add_component(&go_home_button);

			if (skinverwaltung_t::reverse_arrows) {
				reverse_button.init(button_t::imagebox_state, NULL, scr_coord(0,0), scr_size(16,14));
				cont_access_buttons.add_component(&reverse_button);
			}
		}
		end_table();
	}
	end_table();

	add_table(4,1)->set_force_equal_columns(true);
	{
		// this convoi doesn't belong to an AI
		schedule_button.init(button_t::roundbox | button_t::flexible, "Fahrplan");
		schedule_button.set_tooltip("Alters a schedule.");
		schedule_button.add_listener(this);
		add_component(&schedule_button);

		replace_button.init(button_t::roundbox | button_t::flexible, "Replace");
		replace_button.set_tooltip("Automatically replace this convoy.");
		add_component(&replace_button);
		replace_button.add_listener(this);

		if (!skinverwaltung_t::reverse_arrows) {
			reverse_button.init(button_t::square_state, "reverse route");
			add_component(&reverse_button);
		}
		else {
			class_management_button.init(button_t::roundbox | button_t::flexible, "class_manager");
			class_management_button.set_tooltip("see_and_change_the_class_assignments");
			class_management_button.add_listener(this);
			add_component(&class_management_button);
		}
		reverse_button.add_listener(this);
		reverse_button.pressed = cnv->get_reverse_schedule();

		no_load_button.init(button_t::square_state, "no load");
		no_load_button.set_tooltip("No goods are loaded onto this convoi.");
		no_load_button.add_listener(this);
		add_component(&no_load_button);
	}
	end_table();

	// tab panel: connections, chart panels
	switch_mode.add_listener(this);
	add_component(&switch_mode);
	switch_mode.add_tab(&scroll_freight, translator::translate("cd_payload_tab"));

	container_freight.set_table_layout(1,0);
	container_freight.add_table(2,1);
	{
		container_freight.new_component<gui_label_t>("loaded passenger/freight");
		freight_sort_selector.clear_elements();
		for (int i = 0; i < SORT_MODES; i++)
		{
			freight_sort_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
		}
		freight_sort_selector.set_focusable(true);
		freight_sort_selector.set_selection(env_t::default_sortmode);
		freight_sort_selector.set_width_fixed(true);
		freight_sort_selector.set_size(scr_size(D_BUTTON_WIDTH*2, D_EDIT_HEIGHT));
		freight_sort_selector.add_listener(this);
		container_freight.add_component(&freight_sort_selector);
	}
	container_freight.end_table();
	container_freight.add_component(&loading_info);
	container_freight.add_component(&text);

	switch_mode.add_tab(&container_stats, translator::translate("Chart"));

	container_stats.set_table_layout(1,0);

	chart.set_dimension(12, 10000);
	chart.set_background(SYSCOL_CHART_BACKGROUND);
	chart.set_min_size(scr_size(0, CHART_HEIGHT));
	container_stats.add_component(&chart);

	container_stats.add_table(4, int((convoi_t::MAX_CONVOI_COST+3) / 4))->set_force_equal_columns(true);

	for (int cost = 0; cost<convoi_t::MAX_CONVOI_COST; cost++) {
		const uint8 precision = cost_type_money[cost] == gui_chart_t::MONEY ? 2 : (cost_type_money[cost]==gui_chart_t::PAX_KM || cost_type_money[cost]==gui_chart_t::KG_KM || cost_type_money[cost]==gui_chart_t::TON_KM) ? 1 : 0;
		uint16 curve = chart.add_curve( color_idx_to_rgb(cost_type_color[cost]), cnv->get_finance_history(), convoi_t::MAX_CONVOI_COST,
			statistic[cost], MAX_MONTHS, cost_type_money[cost], false, true, precision);

		button_t *b = container_stats.new_component<button_t>();
		b->init(button_t::box_state_automatic  | button_t::flexible, cost_type[cost]);
		b->background_color = color_idx_to_rgb(cost_type_color[cost]);
		b->pressed = false;

		button_to_chart.append(b, &chart, curve);
	}
	container_stats.end_table();

	switch_mode.add_tab(&scroll_times_history, translator::translate("times_history"));

	cnv->set_sortby( env_t::default_sortmode );

	speed_bar.set_base(max_convoi_speed);
	speed_bar.set_vertical(false);
	speed_bar.add_color_value(&mean_convoi_speed, color_idx_to_rgb(COL_GREEN));

	// we update this ourself!
	route_bar.init(&cnv_route_index, 0);
	if( cnv->get_vehicle_count()>0  &&  dynamic_cast<rail_vehicle_t *>(cnv->front()) ) {
		// only for trains etc.
		route_bar.set_reservation( &next_reservation_index );
	}
	route_bar.set_height(9);

	update_labels();

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(diagonal_resize);
}


// only handle a pending renaming ...
convoi_info_t::~convoi_info_t()
{
	button_to_chart.clear();
	// rename if necessary
	rename_cnv();
}


void convoi_info_t::update_labels()
{
	air_vehicle_t* air_vehicle = NULL;
	if (cnv->front()->get_waytype() == air_wt)
	{
		air_vehicle = (air_vehicle_t*)cnv->front();
	}
	bool runway_too_short = air_vehicle == NULL ? false : air_vehicle->is_runway_too_short();

	if (runway_too_short) {
		alert_label.buf().printf("%s (%s) %i%s", translator::translate("Runway too short"), translator::translate("requires"), cnv->front()->get_desc()->get_minimum_runway_length(), translator::translate("m"));
		alert_label.set_color(COL_WARNING);
		if (skinverwaltung_t::pax_evaluation_icons) img_alert.set_image(skinverwaltung_t::pax_evaluation_icons->get_image_id(4), true);
		else if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(4), true);
		route_bar.set_state(3);
	}
	else {
		img_alert.set_image(IMG_EMPTY);
		route_bar.set_state(1);
	}
	cont_speed.set_visible(false);
	cont_alert.set_visible(true);

	switch (cnv->get_state())
	{
		case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:
			if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(2), true);
			/* FALLTHROUGH */
		case convoi_t::WAITING_FOR_CLEARANCE:

			if (!runway_too_short)
			{
				alert_label.buf().append(translator::translate("Waiting for clearance!"));
				alert_label.set_color(COL_CAUTION);
				route_bar.set_state(1);
			}
			break;

		case convoi_t::CAN_START_ONE_MONTH:
			if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(2), true);
			/* FALLTHROUGH */
		case convoi_t::CAN_START:

			alert_label.buf().append(translator::translate("Waiting for clearance!"));
			alert_label.set_color(SYSCOL_TEXT);
			route_bar.set_state(1);
			break;

		case convoi_t::EMERGENCY_STOP:

			if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(3), true);
			char emergency_stop_time[64];
			cnv->snprintf_remaining_emergency_stop_time(emergency_stop_time, sizeof(emergency_stop_time));

			alert_label.buf().printf(translator::translate("emergency_stop %s left"), emergency_stop_time);
			alert_label.set_color(COL_DANGER);
			route_bar.set_state(3);
			break;

		case convoi_t::LOADING:

			char waiting_time[64];
			cnv->snprintf_remaining_loading_time(waiting_time, sizeof(waiting_time));
			if (cnv->get_schedule()->get_current_entry().wait_for_time)
			{
				if(skinverwaltung_t::service_frequency) img_alert.set_image(skinverwaltung_t::service_frequency->get_image_id(0), true);
				alert_label.buf().printf(translator::translate("Waiting for schedule. %s left"), waiting_time);
				alert_label.set_color(COL_CAUTION);
			}
			else if (cnv->get_loading_limit())
			{
				if (!cnv->is_wait_infinite() && strcmp(waiting_time, "0:00"))
				{
					if (skinverwaltung_t::waiting_time) img_alert.set_image(skinverwaltung_t::waiting_time->get_image_id(0), true);
					else img_alert.set_image(skinverwaltung_t::goods->get_image_id(0), true);
					alert_label.buf().printf(translator::translate("Loading (%i->%i%%), %s left!"), cnv->get_loading_level(), cnv->get_loading_limit(), waiting_time);
					alert_label.set_color(COL_CAUTION);
				}
				else
				{
					img_alert.set_image(skinverwaltung_t::goods->get_image_id(0), true);
					alert_label.buf().printf(translator::translate("Loading (%i->%i%%)!"), cnv->get_loading_level(), cnv->get_loading_limit());
					alert_label.set_color(COL_CAUTION);
				}
			}
			else
			{
				if (skinverwaltung_t::waiting_time) img_alert.set_image(skinverwaltung_t::waiting_time->get_image_id(0), true);
				alert_label.buf().printf(translator::translate("Loading. %s left!"), waiting_time);
				alert_label.set_color(SYSCOL_TEXT);
			}
			route_bar.set_state(1);

			break;

		case convoi_t::WAITING_FOR_LOADING_THREE_MONTHS:
		case convoi_t::WAITING_FOR_LOADING_FOUR_MONTHS:

			if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(3), true);
			alert_label.buf().printf(translator::translate("Loading (%i->%i%%) Long Time"), cnv->get_loading_level(), cnv->get_loading_limit());
			route_bar.set_state(2);
			break;

		case convoi_t::REVERSING:

			char reversing_time[64];
			cnv->snprintf_remaining_reversing_time(reversing_time, sizeof(reversing_time));
			switch (cnv->get_terminal_shunt_mode()) {
			case convoi_t::rearrange:
			case convoi_t::shunting_loco:
				alert_label.buf().printf(translator::translate("Shunting. %s left"), reversing_time);
				break;
			case convoi_t::change_direction:
				alert_label.buf().printf(translator::translate("Changing direction. %s left"), reversing_time);
				break;
			default:
				alert_label.buf().printf(translator::translate("Reversing. %s left"), reversing_time);
				break;
			}
			alert_label.set_color(SYSCOL_TEXT);
			route_bar.set_state(1);
			break;

		case convoi_t::CAN_START_TWO_MONTHS:
		case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS:

			if (!runway_too_short)
			{
				if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(3), true);
				alert_label.buf().append(translator::translate("clf_chk_stucked"));
				alert_label.set_color(COL_WARNING);
				route_bar.set_state(2);
			}
			break;

		case convoi_t::NO_ROUTE:

			if (skinverwaltung_t::pax_evaluation_icons) img_alert.set_image(skinverwaltung_t::pax_evaluation_icons->get_image_id(4), true);
			else if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(4), true);
			if (!runway_too_short)
			{
				alert_label.buf().append(translator::translate("clf_chk_noroute"));
			}
			alert_label.set_color(COL_DANGER);
			route_bar.set_state(3);
			break;

		case convoi_t::NO_ROUTE_TOO_COMPLEX:
			if (skinverwaltung_t::pax_evaluation_icons) img_alert.set_image(skinverwaltung_t::pax_evaluation_icons->get_image_id(4), true);
			else if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(4), true);
			//speed_label.buf().append(translator::translate("no_route_too_complex_message"));
			alert_label.buf().append(translator::translate("clf_chk_noroute"));
			alert_label.set_color(COL_DANGER);
			route_bar.set_state(3);
			break;

		case convoi_t::OUT_OF_RANGE:

			if (skinverwaltung_t::pax_evaluation_icons) img_alert.set_image(skinverwaltung_t::pax_evaluation_icons->get_image_id(4), true);
			else if (skinverwaltung_t::alerts) img_alert.set_image(skinverwaltung_t::alerts->get_image_id(4), true);
			//speed_label.buf().printf(translator::translate("out_of_range (max %i km)"), cnv->front()->get_desc()->get_range());
			alert_label.buf().printf("%s (%s %i%s)", translator::translate("out of range"), translator::translate("max"), cnv->front()->get_desc()->get_range(), translator::translate("km"));
			alert_label.set_color(COL_DANGER);
			route_bar.set_state(3);
			break;

		case convoi_t::DRIVING:
			route_bar.set_state(0);

		default:
			if (!runway_too_short)
			{
				uint32 empty_weight = cnv->get_vehicle_summary().weight;

				cont_alert.set_visible(false);
				cont_speed.set_visible(true);
				//use median speed to avoid flickering
				mean_convoi_speed += speed_to_kmh(cnv->get_akt_speed() * 4);
				mean_convoi_speed /= 2;
				const sint32 min_speed = cnv->calc_max_speed(cnv->get_weight_summary());
				const sint32 max_speed = cnv->calc_max_speed(weight_summary_t(empty_weight, cnv->get_current_friction()));
				speed_label.buf().printf(translator::translate(min_speed == max_speed ? "%i km/h (max. %ikm/h)" : "%i km/h (max. %i %s %ikm/h)"),
					(mean_convoi_speed + 3) / 4, min_speed, translator::translate("..."), max_speed);
				speed_label.set_color(SYSCOL_TEXT);
			}
			break;
	}
	alert_label.update();
	speed_label.update();
	profit_label.append_money(cnv->get_jahresgewinn()/100.0);
	profit_label.update();

	running_cost_label.buf().printf("(%1.2f$/km, %1.2f$/month", cnv->get_per_kilometre_running_cost() / 100.0, welt->calc_adjusted_monthly_figure(cnv->get_fixed_cost()) / 100.0);
	running_cost_label.update();

	weight_label.buf().printf("%s %.1ft (%.1ft)",
		translator::translate("Weight:"),
		cnv->get_weight_summary().weight / 1000.0,
		(cnv->get_weight_summary().weight-cnv->get_sum_weight()) / 1000.0);
	if ( cnv->front()->get_waytype() != water_wt ) {
		weight_label.buf().printf(", %s %ut", translator::translate("Max. axle load:"), cnv->get_highest_axle_load());
	}
	weight_label.update();

	// next stop
	const schedule_t *schedule = cnv->get_schedule();
	if (go_home_button.pressed) {
		target_label.buf().append(translator::translate("go home"));
	}
	else {
		schedule_t::gimme_short_stop_name(target_label.buf(), welt, cnv->get_owner(), schedule, schedule->get_current_stop(), 50);
	}
	target_label.update();
	uint8 halt_col_idx = COL_GREY3;
	uint8 halt_symbol_style=0;
	const koord3d next_pos = schedule->get_current_entry().pos;
	const halthandle_t next_halt = haltestelle_t::get_halt(next_pos, cnv->get_owner());
	if (next_halt.is_bound()) {
		halt_col_idx= next_halt->get_owner()->get_player_color1();
		if ((next_halt->registered_lines.get_count() + next_halt->registered_convoys.get_count()) > 1) {
			halt_symbol_style = gui_schedule_entry_number_t::number_style::interchange;
		}
	}
	else if (welt->lookup(next_pos) && welt->lookup(next_pos)->get_depot() != NULL) {
		halt_symbol_style=gui_schedule_entry_number_t::number_style::depot;
	}
	next_halt_number.init(schedule->get_current_stop(), halt_col_idx, halt_symbol_style, next_pos);

	// distance
	sint32 cnv_route_index_left = cnv->get_route()->get_count() - 1 - cnv_route_index;
	double distance;
	char distance_display[13];
	distance = (double)(cnv_route_index_left * welt->get_settings().get_meters_per_tile()) / 1000.0;

	if (distance >= 1)
	{
		uint n_actual = distance < 5 ? 1 : 0;
		char tmp[10];
		number_to_string(tmp, distance, n_actual);
		sprintf(distance_display, "%skm", tmp);
	}
	else if (distance > 0)
	{
		sprintf(distance_display, "%.0fm", distance * 1000);
	}
	if (distance > 0) {
		distance_label.buf().printf( translator::translate("%s left"), distance_display);
	}
	distance_label.update();

	// only show assigned line, if there is one!
	if(  cnv->get_line().is_bound()  ) {
		line_label.buf().append(cnv->get_line()->get_name());
		line_label.set_color(cnv->get_line()->get_state_color());
		if( cnv->get_line()->get_line_color_index()==255 ) {
			lc_preview.set_visible(false);
		}
		else {
			lc_preview.set_line( cnv->get_line() );
			lc_preview.set_base_color( cnv->get_line()->get_line_color() );
			lc_preview.set_visible(true);
		}
	}
	line_label.update();


	const waytype_t wt = cnv->front()->get_waytype();
	if (wt == track_wt || wt == maglev_wt || wt == tram_wt || wt == narrowgauge_wt || wt == monorail_wt) {
		if (cnv->in_depot()) {
			lb_working_method.buf().append("");
		}
		else {
			// Current working method
			rail_vehicle_t* rv1 = (rail_vehicle_t*)cnv->front();
			rail_vehicle_t* rv2 = (rail_vehicle_t*)cnv->back();
			working_method_t wm = rv1->is_leading() ? rv1->get_working_method() : rv2->get_working_method();
			route_bar.set_reserved_color(wm==drive_by_sight? COL_WARNING : color_idx_to_rgb(COL_BLUE));
			lb_working_method.buf().printf("%s: %s", translator::translate("Current working method"), translator::translate(roadsign_t::get_working_method_name(wm)));
		}
	}
	else if (uint16 minimum_runway_length = cnv->front()->get_desc()->get_minimum_runway_length()) {
		// for air vehicle
		lb_working_method.buf().printf("%s: %i m \n", translator::translate("Minimum runway length"), minimum_runway_length);
	}
	lb_working_method.update();

	// buffer update now only when needed by convoi itself => dedicated buffer for this
	const int old_len=freight_info.len();
	cnv->get_freight_info(freight_info);
	if(  old_len!=freight_info.len()  ) {
		text.recalc_size();
		scroll_freight.set_size( scroll_freight.get_size() );
	}

	if (skinverwaltung_t::reverse_arrows) {
		img_reverse_route.set_image(cnv->get_schedule()->is_mirrored() ? skinverwaltung_t::reverse_arrows->get_image_id(0) : skinverwaltung_t::reverse_arrows->get_image_id(1), true);
		img_reverse_route.set_visible(cnv->get_reverse_schedule());
		reverse_button.set_image(cnv->get_schedule()->is_mirrored() ? skinverwaltung_t::reverse_arrows->get_image_id(0) : skinverwaltung_t::reverse_arrows->get_image_id(1));
	}

	// realign container - necessary if strings changed length
	container_top->set_size( container_top->get_size() );
	set_min_windowsize(scr_size(max(container_top->get_min_size().w+D_H_SPACE, get_min_windowsize().w), D_TITLEBAR_HEIGHT + switch_mode.get_pos().y + D_TAB_HEADER_HEIGHT));
	resize(scr_size(0,0));
}


/**
 * Draw new component. The values to be passed refer to the window
 * i.e. It's the screen coordinates of the window where the
 * component is displayed.
 */
void convoi_info_t::draw(scr_coord pos, scr_size size)
{
	if (!cnv.is_bound() || cnv->in_depot() || cnv->get_vehicle_count() == 0)
	{
		destroy_win(this);
	}
	next_reservation_index = cnv->get_next_reservation_index();

	// make titlebar dirty to display the correct coordinates
	if(cnv->get_owner()==welt->get_active_player()  &&  !welt->get_active_player()->is_locked()) {

		if (line_bound != cnv->get_line().is_bound()  ) {
			line_bound = cnv->get_line().is_bound();
			container_line.set_visible(line_bound);
		}
		schedule_button.enable();
		line_button.enable();
		go_home_button.enable();

		if(  grund_t* gr=welt->lookup(cnv->get_schedule()->get_current_entry().pos)  ) {
			go_home_button.pressed = gr->get_depot() != NULL;
		}
		bt_open_detail.pressed = win_get_magic(magic_convoi_detail + cnv.get_id());
		no_load_button.pressed = cnv->get_no_load();
		no_load_button.enable();
		replace_button.pressed = cnv->get_replace();
		replace_button.set_text(cnv->get_replace() ? "Replacing" : "Replace");
		replace_button.enable();
		reverse_button.pressed = cnv->get_reverse_schedule();
		if (!skinverwaltung_t::reverse_arrows) {
			reverse_button.set_text(cnv->get_schedule()->is_mirrored() ? "Return trip" : "reverse route");
		}
		else {
			if ((cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) && goods_manager_t::passengers->get_number_of_classes()>1)
				|| (cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL) && goods_manager_t::mail->get_number_of_classes()>1)) {
				class_management_button.enable();
			}
			else {
				class_management_button.disable();
			}
		}
		reverse_button.set_tooltip(cnv->get_schedule()->is_mirrored() ? "during the return trip of the mirror schedule" : "When this is set, the vehicle will visit stops in reverse order.");
		reverse_button.enable();
	}
	else {
		if (line_bound) {
			// do not jump to other player line window
			remove_component(&line_button);
			line_bound = false;
		}
		schedule_button.disable();
		go_home_button.disable();
		no_load_button.disable();
		replace_button.disable();
		reverse_button.disable();
		if (skinverwaltung_t::reverse_arrows) class_management_button.disable();
	}

/*
#ifdef DEBUG_CONVOY_STATES
		{
			// Debug: show covnoy states
			int debug_row = 9;
			{
				const int pos_y = pos_y0 + debug_row * LINESPACE;

				char state_text[32];
				switch (cnv->get_state())
				{
				case convoi_t::INITIAL:			sprintf(state_text, "INITIAL");	break;
				case convoi_t::EDIT_SCHEDULE:	sprintf(state_text, "EDIT_SCHEDULE");	break;
				case convoi_t::ROUTING_1:		sprintf(state_text, "ROUTING_1");	break;
				case convoi_t::ROUTING_2:		sprintf(state_text, "ROUTING_2");	break;
				case convoi_t::DUMMY5:			sprintf(state_text, "DUMMY5");	break;
				case convoi_t::NO_ROUTE:		sprintf(state_text, "NO_ROUTE");	break;
				case convoi_t::NO_ROUTE_TOO_COMPLEX:		sprintf(state_text, "NO_ROUTE_TOO_COMPLEX");	break;
				case convoi_t::DRIVING:			sprintf(state_text, "DRIVING");	break;
				case convoi_t::LOADING:			sprintf(state_text, "LOADING");	break;
				case convoi_t::WAITING_FOR_CLEARANCE:			sprintf(state_text, "WAITING_FOR_CLEARANCE");	break;
				case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:	sprintf(state_text, "WAITING_FOR_CLEARANCE_ONE_MONTH");	break;
				case convoi_t::CAN_START:			sprintf(state_text, "CAN_START");	break;
				case convoi_t::CAN_START_ONE_MONTH:	sprintf(state_text, "CAN_START_ONE_MONTH");	break;
				case convoi_t::SELF_DESTRUCT:		sprintf(state_text, "SELF_DESTRUCT");	break;
				case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS:	sprintf(state_text, "WAITING_FOR_CLEARANCE_TWO_MONTHS");	break;
				case convoi_t::CAN_START_TWO_MONTHS:				sprintf(state_text, "CAN_START_TWO_MONTHS");	break;
				case convoi_t::LEAVING_DEPOT:	sprintf(state_text, "LEAVING_DEPOT");	break;
				case convoi_t::ENTERING_DEPOT:	sprintf(state_text, "ENTERING_DEPOT");	break;
				case convoi_t::REVERSING:		sprintf(state_text, "REVERSING");	break;
				case convoi_t::OUT_OF_RANGE:	sprintf(state_text, "OUT_OF_RANGE");	break;
				case convoi_t::EMERGENCY_STOP:	sprintf(state_text, "EMERGENCY_STOP");	break;
				case convoi_t::ROUTE_JUST_FOUND:	sprintf(state_text, "ROUTE_JUST_FOUND");	break;
				case convoi_t::WAITING_FOR_LOADING_THREE_MONTHS:	sprintf(state_text, "WAITING_FOR_LOADING_THREE_MONTHS");	break;
				case convoi_t::WAITING_FOR_LOADING_FOUR_MONTHS:	sprintf(state_text, "WAITING_FOR_LOADING_FOUR_MONTHS");	break;
				default:	sprintf(state_text, "default");	break;

				}

				display_proportional_rgb(pos_x, pos_y, state_text, ALIGN_LEFT, SYSCOL_TEXT, true);
				debug_row++;
			}
			if (runway_too_short)
			{
				const int pos_y = pos_y0 + debug_row * LINESPACE;
				char runway_too_short[32];
				sprintf(runway_too_short, "air->runway_too_short");
				display_proportional_rgb(pos_x, pos_y, runway_too_short, ALIGN_LEFT, SYSCOL_TEXT, true);
				debug_row++;
			}
			if (cnv->front()->get_is_overweight() == true) // This doesnt flag!
			{
				const int pos_y = pos_y0 + debug_row * LINESPACE;
				char too_heavy[32];
				sprintf(too_heavy, "is_overweight");
				display_proportional_rgb(pos_x, pos_y, too_heavy, ALIGN_LEFT, SYSCOL_TEXT, true);
				debug_row++;
			}
		}
#endif
*/
/*
#ifdef DEBUG_PHYSICS
		// Show braking distance
		{
			const int pos_y = pos_y0 + 6 * LINESPACE; // line 7
			const sint32 brk_meters = convoy.calc_min_braking_distance(convoy.get_weight_summary(), speed_to_v(cnv->get_akt_speed()));
			char tmp[256];
			sprintf(tmp, translator::translate("minimum brake distance"));
			const int len = display_proportional_rgb(pos_x, pos_y, tmp, ALIGN_LEFT, SYSCOL_TEXT, true);
			sprintf(tmp, translator::translate(": %im"), brk_meters);
			display_proportional_rgb(pos_x + len, pos_y, tmp, ALIGN_LEFT, cnv->get_akt_speed() <= cnv->get_akt_speed_soll() ? SYSCOL_TEXT : SYSCOL_TEXT_STRONG, true);
		}
		{
			const int pos_y = pos_y0 + 7 * LINESPACE; // line 8
			char tmp[256];
			const settings_t &settings = welt->get_settings();
			const sint32 kmh = speed_to_kmh(cnv->next_speed_limit);
			const sint32 m_til_limit = settings.steps_to_meters(cnv->steps_til_limit).to_sint32();
			const sint32 m_til_brake = settings.steps_to_meters(cnv->steps_til_brake).to_sint32();
			if (kmh)
				sprintf(tmp, translator::translate("max %ikm/h in %im, brake in %im "), kmh, m_til_limit, m_til_brake);
			else
				sprintf(tmp, translator::translate("stop in %im, brake in %im "), m_til_limit, m_til_brake);
			const int len = display_proportional_rgb(pos_x, pos_y, tmp, ALIGN_LEFT, SYSCOL_TEXT, true);
		}
		{
			const int pos_y = pos_y0 + 8 * LINESPACE; // line 9
			const sint32 current_friction = cnv->front()->get_frictionfactor();
			char tmp[256];
			sprintf(tmp, translator::translate("current friction factor"));
			const int len = display_proportional_rgb(pos_x, pos_y, tmp, ALIGN_LEFT, SYSCOL_TEXT, true);
			sprintf(tmp, translator::translate(": %i"), current_friction);
			display_proportional_rgb(pos_x + len, pos_y, tmp, ALIGN_LEFT, current_friction <= 20 ? SYSCOL_TEXT : SYSCOL_TEXT_STRONG, true);
		}
#endif
*/

	// update button & labels
	follow_button.pressed = (welt->get_viewport()->get_follow_convoi()==cnv);
	update_labels();

	route_bar.set_base(cnv->get_route()->get_count()-1);
	cnv_route_index = cnv->front()->get_route_index() - 1;

	// Hide the x-scrollbar to not hide the tab header.
	switch (switch_mode.get_active_tab_index()) {
		case 0: // loaded detail
			scroll_freight.set_show_scroll_x( scroll_freight.get_size().h > D_SCROLLBAR_HEIGHT );
			break;
		default:
		case 1: // chart
			break;
		case 2: // times history
			scroll_times_history.set_show_scroll_x( scroll_times_history.get_size().h > D_SCROLLBAR_HEIGHT );
			break;
	}

	// all gui stuff set => display it
	gui_frame_t::draw(pos, size);
}


void convoi_info_t::map_rotate90(sint16 new_ysize)
{
	go_home_button.set_targetpos3d(cnv->get_home_depot());
}

bool convoi_info_t::is_weltpos()
{
	return (welt->get_viewport()->get_follow_convoi()==cnv);
}


koord3d convoi_info_t::get_weltpos( bool set )
{
	if(  set  ) {
		if(  !is_weltpos()  )  {
			welt->get_viewport()->set_follow_convoi( cnv );
		}
		else {
			welt->get_viewport()->set_follow_convoi( convoihandle_t() );
		}
		return koord3d::invalid;
	}
	else {
		return cnv->get_pos();
	}
}

void convoi_info_t::set_tab_opened()
{
	tabstate = switch_mode.get_active_tab_index();

	const scr_coord_val margin_above_tab = switch_mode.get_pos().y + D_TAB_HEADER_HEIGHT + D_TITLEBAR_HEIGHT-1;

	scr_coord_val height = 0;
	switch (tabstate)
	{
		case 0: // loaded detail
		default:
			height = container_freight.get_size().h;
			break;
		case 1: // chart
			height = chart.get_size().h+D_BUTTON_HEIGHT*3+D_V_SPACE*2+D_MARGINS_Y;
			break;
		case 2: // times history
			height = cont_times_history.get_size().h + D_MARGINS_Y;
			break;
	}
	if( (get_windowsize().h-margin_above_tab) < height ) {
		set_windowsize( scr_size(get_windowsize().w, min(display_get_height()-margin_above_tab, margin_above_tab+height)+1) );
	}
}


/**
 * This method is called if an action is triggered
 */
bool convoi_info_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	minimap_t::get_instance()->set_selected_cnv(cnv);
	if( comp == &switch_mode  &&  (tabstate!=switch_mode.get_active_tab_index() ||  (get_windowsize().h-get_min_windowsize().h<LINESPACE*5)) ) {
		set_tab_opened();
		return true;
	}

	// follow convoi on map?
	if(  comp == &follow_button  ) {
		if(welt->get_viewport()->get_follow_convoi()==cnv) {
			// stop following
			welt->get_viewport()->set_follow_convoi( convoihandle_t() );
		}
		else {
			welt->get_viewport()->set_follow_convoi(cnv);
		}
		return true;
	}

	// details?
	if(  comp == &bt_open_detail  ) {
		scr_coord const& dialog_pos = win_get_pos(this);
		scr_coord_val new_dialog_pos_x = -1;
		if ((dialog_pos.x + get_windowsize().w + D_DEFAULT_WIDTH) < display_get_width()) {
			new_dialog_pos_x = dialog_pos.x + get_windowsize().w;
		}
		else if ((dialog_pos.x + get_windowsize().w + D_DEFAULT_WIDTH / 2) < display_get_width()) {
			new_dialog_pos_x = dialog_pos.x + get_windowsize().w / 2;
		}
		create_win(new_dialog_pos_x, dialog_pos.y, new convoi_detail_t(cnv), w_info, magic_convoi_detail+cnv.get_id() );
		return true;
	}

	if(  comp == &line_button  ) {
		cnv->get_owner()->simlinemgmt.show_lineinfo( cnv->get_owner(), cnv->get_line() );
		welt->set_dirty();
	}
	else if(  comp == &input  ) {
		// rename if necessary
		rename_cnv();
	}
	// sort by what
	else if(  comp == &freight_sort_selector  ) {
		sint32 sort_mode = freight_sort_selector.get_selection();
		if (sort_mode < 0)
		{
			freight_sort_selector.set_selection(0);
			sort_mode = 0;
		}
		env_t::default_sortmode = (sort_mode_t)((int)(sort_mode)%(int)SORT_MODES);
		cnv->set_sortby( env_t::default_sortmode );
	}

	// some actions only allowed, when I am the player
	if(cnv->get_owner()==welt->get_active_player()  &&  !welt->get_active_player()->is_locked()) {

		if(  comp == &schedule_button  ) {
			cnv->call_convoi_tool( 'f', NULL );
			return true;
		}

		//if(comp == &no_load_button    &&    !route_search_in_progress) {
		if(  comp == &no_load_button  ) {
			cnv->call_convoi_tool( 'n', NULL );
			return true;
		}

		if(  comp == &replace_button  )	{
			create_win(20, 20, new replace_frame_t(cnv, get_name()), w_info, magic_replace + cnv.get_id() );
			return true;
		}

		if(  comp == &class_management_button  ) {
			scr_coord const& dialog_pos = win_get_pos(this);
			scr_coord_val new_dialog_pos_x=-1;
			if ((dialog_pos.x+get_windowsize().w+D_DEFAULT_WIDTH) < display_get_width()) {
				new_dialog_pos_x = dialog_pos.x+get_windowsize().w;
			}
			else if ((dialog_pos.x + get_windowsize().w + D_DEFAULT_WIDTH/2) < display_get_width()) {
				new_dialog_pos_x = dialog_pos.x + get_windowsize().w/2;
			}
			create_win(new_dialog_pos_x, dialog_pos.y, new vehicle_class_manager_t(cnv), w_info, magic_class_manager + cnv.get_id());
			return true;
		}

		if(  comp == &go_home_button  ) {
			if (go_home_button.pressed) return false; // already pressed => ignore (because message is annoying)

			// limit update to certain states that are considered to be safe for schedule updates
			if(cnv->is_locked())
			{
				DBG_MESSAGE("convoi_info_t::action_triggered()","convoi state %i => cannot change schedule ... ", cnv->get_state() );
				return false;
			}
			go_home_button.pressed = true;
			cnv->call_convoi_tool('P', NULL);
			go_home_button.pressed = false;
			return true;
		} // end go home button

		if(comp == &reverse_button)
		{
			cnv->call_convoi_tool('V', NULL);
			reverse_button.pressed = !reverse_button.pressed;
		}
	}

	return false;
}


bool convoi_info_t::infowin_event(const event_t *ev)
{
	if(  ev->ev_class == INFOWIN  &&  ev->ev_code == WIN_CLOSE  ) {
		minimap_t::get_instance()->set_selected_cnv(convoihandle_t());
	}
	return gui_frame_t::infowin_event(ev);
}


void convoi_info_t::reset_cnv_name()
{
	// change text input of selected line
	if (cnv.is_bound()) {
		tstrncpy(old_cnv_name, cnv->get_name(), sizeof(old_cnv_name));
		tstrncpy(cnv_name, cnv->get_name(), sizeof(cnv_name));
		input.set_text(cnv_name, sizeof(cnv_name));
	}
}


void convoi_info_t::rename_cnv()
{
	if (cnv.is_bound()) {
		const char *t = input.get_text();
		// only change if old name and current name are the same
		// otherwise some unintended undo if renaming would occur
		if(  t  &&  t[0]  &&  strcmp(t, cnv->get_name())  &&  strcmp(old_cnv_name, cnv->get_name())==0) {
			// text changed => call tool
			cbuffer_t buf;
			buf.printf( "c%u,%s", cnv.get_id(), t );
			tool_t *tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
			tool->set_default_param( buf );
			welt->set_tool( tool, cnv->get_owner());
			// since init always returns false, it is safe to delete immediately
			delete tool;
			// do not trigger this command again
			tstrncpy(old_cnv_name, t, sizeof(old_cnv_name));
		}
	}
}


void convoi_info_t::rdwr(loadsave_t *file)
{
	// handle
	convoi_t::rdwr_convoihandle_t(file, cnv);

	file->rdwr_byte( env_t::default_sortmode );

	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	// init window
	if(  file->is_loading()  &&  cnv.is_bound())
	{
		loading_bar.set_cnv(cnv);
		init(cnv);

		reset_min_windowsize();
		set_windowsize(size);
	}

	// after initialization
	// components
	scroll_freight.rdwr(file);
	switch_mode.rdwr(file);
	// button-to-chart array
	button_to_chart.rdwr(file);

	// convoy vanished
	if(  !cnv.is_bound()  ) {
		dbg->error( "convoi_info_t::rdwr()", "Could not restore convoi info window of (%d)", cnv.get_id() );
		destroy_win( this );
		return;
	}
}
