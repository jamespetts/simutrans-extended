/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <algorithm>

#include "messagebox.h"
#include "schedule_list.h"
#include "line_management_gui.h"
#include "components/gui_convoiinfo.h"
#include "components/gui_divider.h"
#include "line_item.h"
#include "simwin.h"
#include "replace_frame.h"

#include "../simcolor.h"
#include "../simdepot.h"
#include "../simhalt.h"
#include "../simworld.h"
#include "../simevent.h"
#include "../display/simgraph.h"
#include "../simskin.h"
#include "../simconvoi.h"
#include "../vehicle/vehicle.h"
#include "../simlinemgmt.h"
#include "../simmenu.h"
#include "../utils/simstring.h"
#include "../player/simplay.h"
#include "../player/finance.h"

#include "../bauer/vehikelbauer.h"

#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/livery_scheme.h"
#include "../dataobj/environment.h"

#include "../boden/wege/kanal.h"
#include "../boden/wege/maglev.h"
#include "../boden/wege/monorail.h"
#include "../boden/wege/narrowgauge.h"
#include "../boden/wege/runway.h"
#include "../boden/wege/schiene.h"
#include "../boden/wege/strasse.h"

#include "../unicode.h"

#include "minimap.h"
#include "map_frame.h"
#include "halt_info.h"
#include "line_color_gui.h"

uint16 schedule_list_gui_t::livery_scheme_index = 0;

static const char *cost_type[MAX_LINE_COST] =
{
	"Seat-km",
	"Pax-km",
	"Mail-km",
	"Freight-km", // ton-km
	"Freight capacity",
	"Distance",
	"Average speed",
	"Comfort time",
	"Revenue",
	"Operation",
	"Refunds",
	"Road toll",
	"Profit",
	"Convoys",
	"Departures",
	"Scheduled"
};

const uint8 cost_type_color[MAX_LINE_COST] =
{
	COL_FREE_CAPACITY,
	COL_LIGHT_PURPLE,
	COL_TRANSPORTED,
	COL_BROWN,
	COL_LIGHT_BROWN,
	COL_DISTANCE,
	COL_AVERAGE_SPEED,
	COL_COMFORT,
	COL_REVENUE,
	COL_OPERATION,
	COL_CAR_OWNERSHIP,
	COL_TOLL,
	COL_PROFIT,
	COL_VEHICLE_ASSETS,
	COL_MAXSPEED,
	COL_LILAC
};
static uint32 bFilterStates = 0;

static uint8 tabs_to_lineindex[8];
static uint8 max_idx=0;

static uint8 statistic[MAX_LINE_COST]={
	LINE_CAPACITY, LINE_PAX_DISTANCE, LINE_MAIL_DISTANCE, LINE_PAYLOAD_DISTANCE,
	LINE_GOODS_CAPACITY, LINE_DISTANCE, LINE_AVERAGE_SPEED, LINE_COMFORT, LINE_REVENUE,
	LINE_OPERATIONS, LINE_REFUNDS, LINE_WAYTOLL, LINE_PROFIT,
	LINE_CONVOIS, LINE_DEPARTURES, LINE_DEPARTURES_SCHEDULED
};

static uint8 statistic_type[MAX_LINE_COST]={
	gui_chart_t::SEAT_KM, gui_chart_t::PAX_KM, gui_chart_t::KG_KM, gui_chart_t::TON_KM,
	gui_chart_t::TON_KM, gui_chart_t::DISTANCE, gui_chart_t::STANDARD, gui_chart_t::TIME, gui_chart_t::MONEY,
	gui_chart_t::MONEY, gui_chart_t::MONEY, gui_chart_t::MONEY, gui_chart_t::MONEY, gui_chart_t::STANDARD, gui_chart_t::STANDARD, gui_chart_t::STANDARD
};

static const char * line_alert_helptexts[5] =
{
  "line_nothing_moved",
  "line_missing_scheduled_slots",
  "line_has_obsolete_vehicles",
  "line_overcrowded",
  "line_has_upgradeable_vehicles"
};


static uint8 current_sort_mode = 0;

#define LINE_NAME_COLUMN_WIDTH ((D_BUTTON_WIDTH*3)+11+4)
#define SCL_HEIGHT (min(L_DEFAULT_WINDOW_HEIGHT/2+D_TAB_HEADER_HEIGHT,(15*LINESPACE)))
#define L_DEFAULT_WINDOW_HEIGHT max(305, 30*LINESPACE)

/// selected convoy tab
static uint8 selected_convoy_tab = 0;

/// selected line tab per player
static uint8 selected_tab[MAX_PLAYER_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/// selected line per tab (static)
linehandle_t schedule_list_gui_t::selected_line[MAX_PLAYER_COUNT][simline_t::MAX_LINE_TYPE];

// selected convoy list display mode
static uint8 selected_cnvlist_mode[MAX_PLAYER_COUNT] = {0};

// sort stuff
schedule_list_gui_t::sort_mode_t schedule_list_gui_t::sortby = by_name;
static uint8 default_sortmode = 0;
bool schedule_list_gui_t::sortreverse = false;

const char *schedule_list_gui_t::sort_text[SORT_MODES] = {
	"cl_btn_sort_name",
	"cl_btn_sort_schedule",
	"cl_btn_sort_income",
	"cl_btn_sort_loading_lvl",
	"cl_btn_sort_max_speed",
	"cl_btn_sort_power",
	"cl_btn_sort_value",
	"cl_btn_sort_age",
	"cl_btn_sort_range",
	"L/F(passenger)"
};


schedule_list_gui_t::schedule_list_gui_t(player_t *player_) :
	gui_frame_t(translator::translate("Line Management"), player_),
	player(player_),
	lc_preview(0),
	cont_by_accommo(linehandle_t()),
	cont_haltlist(linehandle_t()),
	cont_line_network(linehandle_t()),
	cont_line_capacity_by_catg(linehandle_t(), convoihandle_t()),
	scrolly_convois(&cont),
	scroll_halt_waiting(&cont_tab_haltlist, true, true),
	scroll_times_history(&cont_times_history, true, true),
	scroll_line_info(&cont_line_info, true, true),
	scroll_fare_manager(&cont_tab_fare_manager, true, true),
	scroll_fleet(&cont_fleet, true, true),
	scl(gui_scrolled_list_t::listskin, line_scrollitem_t::compare),
	lbl_filter("Line Filter"),
	convoy_infos(),
	halt_entry_origin(-1), halt_entry_dest(-1),
	routebar_middle(player_->get_player_color1(), gui_colored_route_bar_t::downward)
{
	selection = -1;
	schedule_filter[0] = 0;
	old_schedule_filter[0] = 0;
	last_schedule = NULL;
	old_player = NULL;
	line_goods_catg_count = 0;
	origin_halt = halthandle_t();
	destination_halt = halthandle_t();

	// init scrolled list
	scl.set_pos(scr_coord(0,1));
	scl.set_size(scr_size(LINE_NAME_COLUMN_WIDTH-11-4, SCL_HEIGHT-18));
	scl.set_highlight_color(color_idx_to_rgb(player->get_player_color1()+1));
	scl.add_listener(this);

	// tab panel
	tabs.set_pos(scr_coord(11,5));
	tabs.set_size(scr_size(LINE_NAME_COLUMN_WIDTH-11-4, SCL_HEIGHT));
	tabs.add_tab(&scl, translator::translate("All"), 0, translator::translate("All"), world()->get_settings().get_waytype_color(ignore_wt));
	max_idx = 0;
	tabs_to_lineindex[max_idx++] = simline_t::line;

	// now add all specific tabs
	if(maglev_t::default_maglev) {
		tabs.add_tab(&scl, translator::translate("Maglev"), skinverwaltung_t::maglevhaltsymbol, translator::translate("Maglev"), world()->get_settings().get_waytype_color(maglev_wt));
		tabs_to_lineindex[max_idx++] = simline_t::maglevline;
	}
	if(monorail_t::default_monorail) {
		tabs.add_tab(&scl, translator::translate("Monorail"), skinverwaltung_t::monorailhaltsymbol, translator::translate("Monorail"), world()->get_settings().get_waytype_color(monorail_wt));
		tabs_to_lineindex[max_idx++] = simline_t::monorailline;
	}
	if(schiene_t::default_schiene) {
		tabs.add_tab(&scl, translator::translate("Train"), skinverwaltung_t::zughaltsymbol, translator::translate("Train"), world()->get_settings().get_waytype_color(track_wt));
		tabs_to_lineindex[max_idx++] = simline_t::trainline;
	}
	if(narrowgauge_t::default_narrowgauge) {
		tabs.add_tab(&scl, translator::translate("Narrowgauge"), skinverwaltung_t::narrowgaugehaltsymbol, translator::translate("Narrowgauge"), world()->get_settings().get_waytype_color(narrowgauge_wt));
		tabs_to_lineindex[max_idx++] = simline_t::narrowgaugeline;
	}
	if (!vehicle_builder_t::get_info(tram_wt).empty()) {
		tabs.add_tab(&scl, translator::translate("Tram"), skinverwaltung_t::tramhaltsymbol, translator::translate("Tram"), world()->get_settings().get_waytype_color(tram_wt));
		tabs_to_lineindex[max_idx++] = simline_t::tramline;
	}
	if(strasse_t::default_strasse) {
		tabs.add_tab(&scl, translator::translate("Truck"), skinverwaltung_t::autohaltsymbol, translator::translate("Truck"), world()->get_settings().get_waytype_color(road_wt));
		tabs_to_lineindex[max_idx++] = simline_t::truckline;
	}
	if (!vehicle_builder_t::get_info(water_wt).empty()) {
		tabs.add_tab(&scl, translator::translate("Ship"), skinverwaltung_t::schiffshaltsymbol, translator::translate("Ship"), world()->get_settings().get_waytype_color(water_wt));
		tabs_to_lineindex[max_idx++] = simline_t::shipline;
	}
	if(runway_t::default_runway) {
		tabs.add_tab(&scl, translator::translate("Air"), skinverwaltung_t::airhaltsymbol, translator::translate("Air"), world()->get_settings().get_waytype_color(air_wt));
		tabs_to_lineindex[max_idx++] = simline_t::airline;
	}
	tabs.add_listener(this);
	add_component(&tabs);

	cont_line_name.set_table_layout(3,1);
	cont_line_name.set_spacing(scr_size(0,0));
	cont_line_name.set_pos(scr_coord(LINE_NAME_COLUMN_WIDTH, D_MARGIN_TOP));

	cont_line_name.add_component(&wt_symbol);
	cont_line_name.add_component(&lc_preview);

	// editable line name
	inp_name.add_listener(this);
	cont_line_name.add_component(&inp_name);
	add_component(&cont_line_name);

	// sort button on convoy list, define this first to prevent overlapping
	sortedby.set_pos(scr_coord(BUTTON1_X, 2));
	sortedby.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_BUTTON_HEIGHT));
	sortedby.set_max_size(scr_size(D_BUTTON_WIDTH * 1.5, LINESPACE * 8));
	for (int i = 0; i < SORT_MODES; i++) {
		sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
	}
	sortedby.set_selection(default_sortmode);
	sortedby.add_listener(this);
	cont_convoys.add_component(&sortedby);

	// convoi list
	cont.set_size(scr_size(200, 80));
	cont.set_pos(scr_coord(D_H_SPACE, D_V_SPACE));
	scrolly_convois.set_pos(scr_coord(0, D_BUTTON_HEIGHT+D_H_SPACE));
	scrolly_convois.set_show_scroll_y(true);
	scrolly_convois.set_scroll_amount_y(40);
	scrolly_convois.set_visible(false);
	cont_convoys.add_component(&scrolly_convois);

	scroll_line_info.set_visible(false);
	cont_line_info.set_table_layout(1,0);
	cont_line_info.set_margin(scr_size(D_MARGIN_LEFT,D_V_SPACE),scr_size(D_H_SPACE,D_MARGIN_BOTTOM));

	cont_line_info.add_table(2,1)->set_spacing(scr_size(0,0));
	{
		cont_line_info.add_component(&bt_withdraw_line);
		bt_replace.init(button_t::roundbox, "replace_line_convoys", scr_coord(0,0), D_WIDE_BUTTON_SIZE);
		if (skinverwaltung_t::open_window) {
			bt_replace.set_image(skinverwaltung_t::open_window->get_image_id(0));
			bt_replace.set_image_position_right(true);
		}
		bt_replace.set_tooltip("helptxt_replace_all_convoys_of_this_line");
		bt_replace.set_visible(false);
		bt_replace.add_listener(this);
		cont_line_info.add_component(&bt_replace);

		bt_withdraw_line.init(button_t::box_state, "Withdraw All", scr_coord(0, 0), scr_size(D_BUTTON_WIDTH+18,D_BUTTON_HEIGHT));
		bt_withdraw_line.set_tooltip("Convoys run empty and go to the next depot on their schedule, where they are held.");
		if (skinverwaltung_t::alerts) {
			bt_withdraw_line.set_image(skinverwaltung_t::alerts->get_image_id(2));
		}
		bt_withdraw_line.add_listener(this);
	}
	cont_line_info.end_table();

	gui_aligned_container_t *tbl = cont_line_info.add_table(2,0);
	tbl->set_spacing(scr_size(D_H_SPACE, 0));
	tbl->set_table_frame(true,true);
	tbl->set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE), scr_size(D_MARGIN_LEFT, D_V_SPACE));
	{
		cont_line_info.add_component(&halt_entry_origin);
		cont_line_info.add_component(&lb_line_origin);

		cont_line_info.add_component(&routebar_middle);
		cont_line_info.new_component<gui_fill_t>();

		cont_line_info.add_component(&halt_entry_dest);
		cont_line_info.add_component(&lb_line_destination);
	}
	cont_line_info.end_table();

	cont_line_info.add_component(&lb_travel_distance);
	cont_line_info.add_table(2,1);
	{
		if (skinverwaltung_t::service_frequency) {
			cont_line_info.new_component<gui_image_t>(skinverwaltung_t::service_frequency->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Service frequency"));
		}
		else {
			cont_line_info.new_component<gui_label_t>("Service frequency");
		}
		cont_line_info.add_component(&lb_service_frequency);
	}
	cont_line_info.end_table();

	cont_line_info.add_component(&lb_convoy_count);
	cont_line_info.new_component<gui_divider_t>();
	cont_line_info.add_component(&cont_line_capacity_by_catg);

	// Passenger Load factor
	cont_load_factor.set_rigid(false);
	cont_load_factor.set_table_layout(2, 0);
	cont_load_factor.new_component<gui_divider_t>();
	cont_load_factor.new_component<gui_empty_t>();
	gui_aligned_container_t* tbl2 = cont_load_factor.add_table(3,2);
	tbl2->set_table_frame(true, true);
	tbl2->set_spacing(scr_size(1, 1));
	{
		cont_load_factor.new_component<gui_table_header_t>("")->set_flexible(true, false);
		cont_load_factor.new_component<gui_table_header_t>("Last Month")->set_flexible(true, false);
		cont_load_factor.new_component<gui_table_header_t>("This Year")->set_flexible(true, false);

		cont_load_factor.new_component<gui_table_header_t>("L/F(passenger)", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left);
		lb_load_factor_pax_last_month.set_align(gui_label_t::right);
		lb_load_factor_pax_year.set_align(gui_label_t::right);
		cont_load_factor.add_component(&lb_load_factor_pax_last_month);
		cont_load_factor.add_component(&lb_load_factor_pax_year);
	}
	cont_load_factor.end_table();
	cont_load_factor.new_component<gui_fill_t>();

	cont_line_info.add_component(&cont_load_factor);

	cont_line_info.new_component<gui_divider_t>();
	// Transport density
	cont_line_info.new_component<gui_label_t>("Monthly transportation density");
	cont_transport_density.set_table_layout(3,0);
	cont_transport_density.set_spacing(scr_size(1,1));
	cont_transport_density.set_table_frame(true,true);
	cont_line_info.add_table(2, 1);
	{
		cont_line_info.add_component(&cont_transport_density);
		cont_line_info.new_component<gui_fill_t>();
	}
	cont_line_info.end_table();

	scroll_line_info.set_pos(scr_coord(0, 8 + SCL_HEIGHT + D_BUTTON_HEIGHT + D_BUTTON_HEIGHT + 2));
	add_component(&scroll_line_info);

	// filter lines by
	lbl_filter.set_pos( scr_coord( 11, 7+SCL_HEIGHT+2 ) );
	add_component(&lbl_filter);

	inp_filter.set_pos( scr_coord( 11+D_BUTTON_WIDTH, 7+SCL_HEIGHT ) );
	inp_filter.set_size( scr_size( D_BUTTON_WIDTH*2- D_BUTTON_HEIGHT *3, D_EDIT_HEIGHT ) );
	inp_filter.set_text( schedule_filter, lengthof(schedule_filter) );
//	inp_filter.set_tooltip("Only show lines containing");
	inp_filter.set_search_box(true);
	inp_filter.add_listener(this);
	add_component(&inp_filter);

	filter_btn_all_pas.init(button_t::roundbox_state, NULL, scr_coord(inp_filter.get_pos() + scr_coord(inp_filter.get_size().w, 0)), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_pas.set_image(skinverwaltung_t::passengers->get_image_id(0));
	filter_btn_all_pas.set_tooltip("filter_pas_line");
	filter_btn_all_pas.pressed = line_type_flags & (1 << simline_t::all_pas);
	add_component(&filter_btn_all_pas);
	filter_btn_all_pas.add_listener(this);

	filter_btn_all_mails.init(button_t::roundbox_state, NULL, scr_coord(filter_btn_all_pas.get_pos() + scr_coord(D_BUTTON_HEIGHT, 0)), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_mails.set_image(skinverwaltung_t::mail->get_image_id(0));
	filter_btn_all_mails.set_tooltip("filter_mail_line");
	filter_btn_all_mails.pressed = line_type_flags & (1 << simline_t::all_mail);
	filter_btn_all_mails.add_listener(this);
	add_component(&filter_btn_all_mails);

	filter_btn_all_freights.init(button_t::roundbox_state, NULL, scr_coord(filter_btn_all_mails.get_pos() + scr_coord(D_BUTTON_HEIGHT, 0)), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_freights.set_image(skinverwaltung_t::goods->get_image_id(0));
	filter_btn_all_freights.set_tooltip("filter_freight_line");
	filter_btn_all_freights.pressed = line_type_flags & (1 << simline_t::all_freight);
	filter_btn_all_freights.add_listener(this);
	add_component(&filter_btn_all_freights);


	// normal buttons edit new remove
	bt_new_line.init(button_t::roundbox, "New Line", scr_coord(11, 8+SCL_HEIGHT+D_BUTTON_HEIGHT ), D_BUTTON_SIZE);
	bt_new_line.add_listener(this);
	add_component(&bt_new_line);

	bt_edit_line.init(button_t::roundbox, "Update Line", scr_coord(11+D_BUTTON_WIDTH, 8+SCL_HEIGHT+D_BUTTON_HEIGHT ), D_BUTTON_SIZE);
	bt_edit_line.set_tooltip("Modify the selected line");
	bt_edit_line.add_listener(this);
	bt_edit_line.disable();
	add_component(&bt_edit_line);

	bt_delete_line.init(button_t::roundbox, "Delete Line", scr_coord(11+2*D_BUTTON_WIDTH, 8+SCL_HEIGHT+D_BUTTON_HEIGHT ), D_BUTTON_SIZE);
	bt_delete_line.set_tooltip("Delete the selected line (if without associated convois).");
	bt_delete_line.add_listener(this);
	bt_delete_line.disable();
	add_component(&bt_delete_line);


	int offset_y = D_MARGIN_TOP + D_BUTTON_HEIGHT*2;

	bt_line_color_editor.init(button_t::roundbox_state, "edit_line_color", scr_coord(LINE_NAME_COLUMN_WIDTH, offset_y), D_WIDE_BUTTON_SIZE);
	if (skinverwaltung_t::open_window) {
		bt_line_color_editor.set_image(skinverwaltung_t::open_window->get_image_id(0));
		bt_line_color_editor.set_image_position_right(true);
	}
	bt_line_color_editor.set_visible(false);
	bt_line_color_editor.add_listener(this);
	add_component(&bt_line_color_editor);

	bt_access_minimap.init(button_t::roundbox, "access_minimap", scr_coord(LINE_NAME_COLUMN_WIDTH+D_WIDE_BUTTON_WIDTH, offset_y), D_WIDE_BUTTON_SIZE);
	if (skinverwaltung_t::open_window) {
		bt_access_minimap.set_image(skinverwaltung_t::open_window->get_image_id(0));
		bt_access_minimap.set_image_position_right(true);
	}
	bt_access_minimap.set_tooltip("helptxt_access_minimap");
	bt_access_minimap.set_visible(false);
	bt_access_minimap.add_listener(this);
	add_component(&bt_access_minimap);

	offset_y += D_BUTTON_HEIGHT;
	// Select livery
	livery_selector.set_pos(scr_coord(LINE_NAME_COLUMN_WIDTH, offset_y));
	livery_selector.set_focusable(false);
	livery_selector.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_BUTTON_HEIGHT));
	livery_selector.set_max_size(scr_size(D_BUTTON_WIDTH - 8, LINESPACE * 8 + 2 + 16));
	livery_selector.set_highlight_color(1);
	livery_selector.clear_elements();
	livery_scheme_indices.clear();
	uint32 i = 0;
	for(auto scheme : *welt->get_settings().get_livery_schemes())
	{
		if(scheme->is_available(welt->get_timeline_year_month()))
		{
			livery_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(scheme->get_name()), SYSCOL_TEXT);
			livery_scheme_indices.append(i);
		}
		i++;
	}
	livery_selector.add_listener(this);
	add_component(&livery_selector);

	// sort asc/desc switching button
	sort_order.set_typ(button_t::sortarrow_state); // NOTE: This dialog is not yet auto-aligned, so we need to pre-set the typ to calculate the size
	sort_order.init(button_t::sortarrow_state, "", scr_coord(BUTTON1_X + D_BUTTON_WIDTH * 1.5 + D_H_SPACE, 2), sort_order.get_min_size());
	sort_order.set_tooltip(translator::translate("cl_btn_sort_order"));
	sort_order.add_listener(this);
	sort_order.pressed = sortreverse;
	cont_convoys.add_component(&sort_order);

	bt_mode_convois.init(button_t::roundbox, gui_convoy_formation_t::cnvlist_mode_button_texts[selected_cnvlist_mode[player->get_player_nr()]], scr_coord(BUTTON3_X, 2), scr_size(D_BUTTON_WIDTH+15, D_BUTTON_HEIGHT));
	bt_mode_convois.add_listener(this);
	cont_convoys.add_component(&bt_mode_convois);
	info_tabs.add_tab(&cont_convoys, translator::translate("Convoys"));

	// Fleet tab: target, automatic policy, fleet summary and depot detail.
	cont_fleet.set_table_layout(1, 0);
	cont_fleet.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_H_SPACE, D_V_SPACE));
	cont_fleet.add_table(3, 1)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE));
	{
		cont_fleet.new_component<gui_label_t>("Target consists");
		numimp_target_consists.init(0, 0, 65535, 1);
		numimp_target_consists.add_listener(this);
		cont_fleet.add_component(&numimp_target_consists);
		bt_set_target.init(button_t::roundbox, "Set", scr_coord(0, 0), D_BUTTON_SIZE);
		bt_set_target.add_listener(this);
		cont_fleet.add_component(&bt_set_target);
	}
	cont_fleet.end_table();
	cont_fleet.add_component(&lb_fleet_summary);
	cont_fleet.add_component(&lb_fleet_load);
	cont_fleet.add_component(&lb_fleet_max);
	cont_fleet.new_component<gui_divider_t>();

	bt_fleet_policy.init(button_t::square_state, "Automatic fleet policy");
	bt_fleet_policy.set_tooltip("The Target Fleet Size follows the line's passenger and goods load factors.");
	bt_fleet_policy.add_listener(this);
	cont_fleet.add_component(&bt_fleet_policy);

	cont_fleet.add_table(4, 2)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE));
	{
		cont_fleet.new_component<gui_label_t>("Target min");
		numimp_fleet_target_min.init(1, 1, 65535, 1);
		numimp_fleet_target_min.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_target_min);
		cont_fleet.new_component<gui_label_t>("Target max");
		numimp_fleet_target_max.init(0, 0, 65535, 1);
		numimp_fleet_target_max.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_target_max);

		cont_fleet.new_component<gui_label_t>("Add threshold (0.1%)");
		numimp_fleet_add_threshold.init(0, 0, 1000, 10);
		numimp_fleet_add_threshold.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_add_threshold);
		cont_fleet.new_component<gui_label_t>("Remove threshold (0.1%)");
		numimp_fleet_remove_threshold.init(0, 0, 1000, 10);
		numimp_fleet_remove_threshold.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_remove_threshold);

		cont_fleet.new_component<gui_label_t>("Add months");
		numimp_fleet_add_months.init(0, 0, 255, 1);
		numimp_fleet_add_months.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_add_months);
		cont_fleet.new_component<gui_label_t>("Remove months");
		numimp_fleet_remove_months.init(0, 0, 255, 1);
		numimp_fleet_remove_months.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_remove_months);
	}
	cont_fleet.end_table();
	bt_set_fleet_policy.init(button_t::roundbox, "Set policy", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_set_fleet_policy.add_listener(this);
	cont_fleet.add_component(&bt_set_fleet_policy);
	cont_fleet.new_component<gui_divider_t>();

	// Schedule spacing: the line frequency and the per-stop time shifts.
	cont_fleet.add_table(4, 1)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE));
	{
		cont_fleet.new_component<gui_label_t>("Spacing (departures/year)")->set_tooltip("The number of departures per year. A step of 12 is one departure per month.");
		numimp_fleet_spacing.init(0, 0, 999, 12);
		numimp_fleet_spacing.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_spacing);
		cont_fleet.add_component(&lb_fleet_spacing);
		cont_fleet.new_component<gui_empty_t>();
	}
	cont_fleet.end_table();

	bt_fleet_spacing_sync.init(button_t::square_state, "Frequency sets the number of consists");
	bt_fleet_spacing_sync.set_tooltip("When set, the schedule frequency and the measured round-trip time determine the Target Fleet Size. The automatic fleet policy is turned off.");
	bt_fleet_spacing_sync.add_listener(this);
	cont_fleet.add_component(&bt_fleet_spacing_sync);

	cont_fleet.add_table(3, 1)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE));
	{
		bt_same_spacing_shift_fleet.init(button_t::square_state, "Use the same shift at every stop");
		bt_same_spacing_shift_fleet.set_tooltip("When set, one spacing shift applies to every stop. Turn it off to give individual stops their own shift.");
		bt_same_spacing_shift_fleet.add_listener(this);
		cont_fleet.add_component(&bt_same_spacing_shift_fleet);

		cont_fleet.new_component<gui_label_t>("Shift")->set_tooltip("The time shift within the departure interval. The default unit is one minute of the 24-hour month.");
		numimp_fleet_shift.init(0, 0, welt->get_settings().get_spacing_shift_divisor(), 1);
		numimp_fleet_shift.add_listener(this);
		cont_fleet.add_component(&numimp_fleet_shift);
	}
	cont_fleet.end_table();

	cont_fleet.add_component(&cont_fleet_spacing_stops);
	bt_set_fleet_spacing.init(button_t::roundbox, "Set spacing", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_set_fleet_spacing.add_listener(this);
	cont_fleet.add_component(&bt_set_fleet_spacing);
	cont_fleet.add_component(&lb_fleet_required);

	cont_fleet.new_component<gui_divider_t>();
	cont_fleet.add_component(&cont_fleet_depots);
	info_tabs.add_tab(&scroll_fleet, translator::translate("Fleet"));


	offset_y += D_BUTTON_HEIGHT;
	// right tabs
	info_tabs.set_pos(scr_coord(LINE_NAME_COLUMN_WIDTH-4, offset_y));
	info_tabs.add_listener(this);
	info_tabs.set_size(scr_size(get_windowsize().w- LINE_NAME_COLUMN_WIDTH+4+D_MARGIN_RIGHT, get_windowsize().h-offset_y));
	add_component(&info_tabs);

	//CHART
	chart.set_dimension(12, 1000);
	chart.set_pos( scr_coord(68, LINESPACE) );
	chart.set_seed(0);
	chart.set_background(SYSCOL_CHART_BACKGROUND);
	chart.set_ltr(env_t::left_to_right_graphs);
	cont_charts.add_component(&chart);

	// add filter buttons
	for (int i=0; i<MAX_LINE_COST; i++) {
		filterButtons[i].init(button_t::box_state,cost_type[i],scr_coord(0,0), D_BUTTON_SIZE);
		filterButtons[i].add_listener(this);
		filterButtons[i].background_color = color_idx_to_rgb(cost_type_color[i]);
		cont_charts.add_component(filterButtons + i);
	}
	info_tabs.add_tab(&cont_charts, translator::translate("Chart"));
	info_tabs.set_active_tab_index(selected_convoy_tab);

	cont_times_history.set_table_layout(1,0);
	info_tabs.add_tab(&scroll_times_history, translator::translate("times_history"));

	cont_tab_haltlist.set_table_layout(1,0);
	cont_tab_haltlist.add_table(2,1);
	{
		bt_hw_filter_by_line.init(button_t::square_state, "filter_by_line");
		bt_hw_filter_by_line.set_tooltip("Show only cargoes waiting for this line");
		bt_hw_filter_by_line.pressed = true;
		bt_hw_filter_by_line.add_listener(this);
		cont_tab_haltlist.add_component(&bt_hw_filter_by_line);

		cont_tab_haltlist.new_component<gui_fill_t>();
	}
	cont_tab_haltlist.end_table();

	cont_tab_haltlist.add_table(4,1);
	{
		bt_hw_show_halt_name.init(button_t::square_state, "show station names");
		bt_hw_show_halt_name.set_tooltip("helptxt_show_station_name");
		bt_hw_show_halt_name.pressed=true;
		bt_hw_show_halt_name.add_listener(this);
		cont_tab_haltlist.add_component(&bt_hw_show_halt_name);

		cont_tab_haltlist.new_component<gui_margin_t>(LINEASCENT);

		bt_hw_divided_class.init(button_t::square_state, "Divided by class");
		bt_hw_divided_class.set_tooltip("Waiting cargoes are displayed separately for each class.");
		bt_hw_divided_class.pressed = false;
		bt_hw_divided_class.add_listener(this);
		cont_tab_haltlist.add_component(&bt_hw_divided_class);

		cont_tab_haltlist.new_component<gui_fill_t>();
	}
	cont_tab_haltlist.end_table();

	cont_tab_haltlist.add_component(&cont_haltlist);
	info_tabs.add_tab(&scroll_halt_waiting, translator::translate("waiting_status"));

	info_tabs.add_tab(&cont_line_network, translator::translate("line_network"));

	cont_tab_fare_manager.set_table_layout(1,0);
	cont_tab_fare_manager.add_table(2,1);
	{
		cont_tab_fare_manager.new_component<gui_fill_t>();
		cont_tab_fare_manager.add_table(2,1);
		{
			reset_all_pass_button.set_visible(false);
			reset_all_pass_button.set_rigid(false);
			reset_all_pass_button.set_typ(button_t::roundbox);
			reset_all_pass_button.set_text("reset_all_pass_classes");
			reset_all_pass_button.add_listener(this);
			reset_all_pass_button.set_tooltip("resets_all_passenger_classes_to_their_defaults");
			cont_tab_fare_manager.add_component(&reset_all_pass_button);

			reset_all_mail_button.set_visible(false);
			reset_all_mail_button.set_rigid(false);
			reset_all_mail_button.set_typ(button_t::roundbox);
			reset_all_mail_button.set_text("reset_all_mail_classes");
			reset_all_mail_button.add_listener(this);
			reset_all_mail_button.set_tooltip("resets_all_mail_classes_to_their_defaults");
			cont_tab_fare_manager.add_component(&reset_all_mail_button);
		}
		cont_tab_fare_manager.end_table();
	}
	cont_tab_fare_manager.end_table();
	cont_tab_fare_manager.new_component<gui_empty_t>();
	cont_tab_fare_manager.add_component(&cont_by_accommo);

	info_tabs.add_tab(&scroll_fare_manager, translator::translate("line_class_manager"));

	// recover last selected line
	int index = 0;
	for(  uint i=0;  i<max_idx;  i++  ) {
		if(  tabs_to_lineindex[i] == selected_tab[player->get_player_nr()]  ) {
			line = selected_line[player->get_player_nr()][selected_tab[player->get_player_nr()]];
			// check owner here: selected_line[][] is not reset between games, so line could have another player as owner
			if (line.is_bound()  &&  line->get_owner() != player) {
				line = linehandle_t();
			}
			index = i;
			break;
		}
	}
	selected_tab[player->get_player_nr()] = tabs_to_lineindex[index]; // reset if previous selected tab is not there anymore
	tabs.set_active_tab_index(index);
	if(index>0) {
		bt_new_line.enable();
	}
	else {
		bt_new_line.disable();
	}
	update_lineinfo( line );

	// resize button
	// Leave enough room to the right of the line list for all info tabs: their
	// labels scale with the font, so a fixed number of button widths no longer fits.
	const scr_coord_val tabs_min_w = info_tabs.get_min_size().w;
	const scr_coord_val min_width = max( LINE_NAME_COLUMN_WIDTH - 4 + D_MARGIN_RIGHT + tabs_min_w, LINE_NAME_COLUMN_WIDTH + D_BUTTON_WIDTH*3 + D_MARGIN_LEFT*2 );
	set_min_windowsize(scr_size(min_width, L_DEFAULT_WINDOW_HEIGHT));
	set_resizemode(diagonal_resize);
	resize(scr_coord(0,0));
	resize(scr_coord(D_BUTTON_WIDTH, LINESPACE*3+D_V_SPACE)); // suitable for 4 buttons horizontally and 5 convoys vertically

	build_line_list(index);
}


schedule_list_gui_t::~schedule_list_gui_t()
{
	delete last_schedule;
	// change line name if necessary
	rename_line();
}


/**
 * Mouse clicks are hereby reported to the GUI-Components
 */
bool schedule_list_gui_t::infowin_event(const event_t *ev)
{
	if(ev->ev_class == INFOWIN) {
		if(ev->ev_code == WIN_CLOSE) {
			// hide schedule on minimap (may not current, but for safe)
			minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );
		}
		else if(  (ev->ev_code==WIN_OPEN  ||  ev->ev_code==WIN_TOP)  &&  line.is_bound() ) {
			if(  line->count_convoys()>0  ) {
				// set this schedule as current to show on minimap if possible
				minimap_t::get_instance()->set_selected_cnv( line->get_convoy(0) );
			}
			else {
				// set this schedule as current to show on minimap if possible
				minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );
			}
		}
	}
	return gui_frame_t::infowin_event(ev);
}


bool schedule_list_gui_t::action_triggered( gui_action_creator_t *comp, value_t v )
{
	if(comp == &bt_edit_line) {
		if(line.is_bound()) {
			create_win( new line_management_gui_t(line, player), w_info, (ptrdiff_t)line.get_rep() );
		}
	}
	else if(comp == &bt_new_line) {
		// create typed line
		assert(  tabs.get_active_tab_index() > 0  &&  tabs.get_active_tab_index()<max_idx  );
		// update line schedule via tool!
		tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
		cbuffer_t buf;
		int type = tabs_to_lineindex[tabs.get_active_tab_index()];
		buf.printf( "c,0,%i,0,0|%i,0|33", type, type ); // an empty schedule: entry count 0 and the "33" terminator
		tmp_tool->set_default_param(buf);
		welt->set_tool( tmp_tool, player );
		// since init always returns false, it is safe to delete immediately
		delete tmp_tool;
		depot_t::update_all_win();
	}
	else if(comp == &bt_delete_line) {
		if(line.is_bound()) {
			tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
			cbuffer_t buf;
			buf.printf( "d,%i", line.get_id() );
			tmp_tool->set_default_param(buf);
			welt->set_tool( tmp_tool, player );
			// since init always returns false, it is safe to delete immediately
			delete tmp_tool;
			depot_t::update_all_win();
		}
	}
	else if (comp == &bt_set_target) {
		if (line.is_bound()) {
			tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
			cbuffer_t buf;
			buf.printf( "n,%i,%i", line.get_id(), (int)numimp_target_consists.get_value() );
			tmp_tool->set_default_param(buf);
			welt->set_tool( tmp_tool, player );
			delete tmp_tool;
		}
	}
	else if (comp == &bt_fleet_policy) {
		bt_fleet_policy.pressed ^= 1;
		if (bt_fleet_policy.pressed) {
			bt_fleet_spacing_sync.pressed = false;
		}
		update_fleet_tab(false);
	}
	else if (comp == &bt_set_fleet_policy) {
		if (line.is_bound()) {
			tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
			cbuffer_t buf;
			buf.printf( "p,%i,%i,%i,%i,%i,%i,%i,%i", line.get_id(),
				bt_fleet_policy.pressed ? 1 : 0,
				(int)numimp_fleet_target_min.get_value(),
				(int)numimp_fleet_target_max.get_value(),
				(int)numimp_fleet_add_threshold.get_value(),
				(int)numimp_fleet_remove_threshold.get_value(),
				(int)numimp_fleet_add_months.get_value(),
				(int)numimp_fleet_remove_months.get_value() );
			tmp_tool->set_default_param(buf);
			welt->set_tool( tmp_tool, player );
			delete tmp_tool;
		}
	}
	else if (comp == &bt_fleet_spacing_sync) {
		bt_fleet_spacing_sync.pressed ^= 1;
		if (bt_fleet_spacing_sync.pressed) {
			bt_fleet_policy.pressed = false;
		}
		fleet_required_deployed = 0xFFFF;
		update_fleet_tab(false);
	}
	else if (comp == &bt_same_spacing_shift_fleet) {
		bt_same_spacing_shift_fleet.pressed ^= 1;
		update_fleet_tab(false);
	}
	else if (comp == &bt_set_fleet_spacing) {
		if (line.is_bound()) {
			const uint8 spacing_shift_mode = welt->get_settings().get_spacing_shift_mode();
			const bool same_shift = bt_same_spacing_shift_fleet.pressed || spacing_shift_mode <= settings_t::SPACING_SHIFT_PER_LINE;
			const uint32 shift_count = same_shift ? 0 : fleet_shift_entries.get_count();
			cbuffer_t buf;
			buf.printf("f,%i,%i,%i,%i,%i,%i", line.get_id(),
				bt_fleet_spacing_sync.pressed ? 1 : 0,
				same_shift ? 1 : 0,
				(int)numimp_fleet_spacing.get_value(),
				(int)numimp_fleet_shift.get_value(),
				(int)shift_count);
			for (uint32 i = 0; i < shift_count; i++) {
				buf.printf(",%i,%i", (int)fleet_shift_entries[i], (int)fleet_shift_inputs[i]->get_value());
			}
			tool_t *tmp_tool = create_tool(TOOL_CHANGE_LINE | SIMPLE_TOOL);
			tmp_tool->set_default_param(buf);
			welt->set_tool(tmp_tool, player);
			delete tmp_tool;
			update_fleet_tab(false);
		}
	}
	else if(comp == &bt_withdraw_line) {
		bt_withdraw_line.pressed ^= 1;
		if (line.is_bound()) {
			tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
			cbuffer_t buf;
			buf.printf( "w,%i,%i", line.get_id(), bt_withdraw_line.pressed );
			tmp_tool->set_default_param(buf);
			welt->set_tool( tmp_tool, player );
			// since init always returns false, it is safe to delete immediately
			delete tmp_tool;
		}
	}
	else if (  comp==&bt_access_minimap  ) {
		map_frame_t *win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		if (!win) {
			create_win({ -1, -1 }, new map_frame_t(), w_info, magic_reliefmap);
			win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		}
		win->activate_individual_network_mode(origin_halt.is_bound() ? origin_halt->get_basis_pos() : koord::invalid);
		if (line->count_convoys() > 0) {
			minimap_t::get_instance()->set_selected_cnv(line->get_convoy(0));
		}
		else {
			minimap_t::get_instance()->set_selected_cnv(convoihandle_t());
		}
		top_win(win);
		return true;
	}
	else if (comp == &bt_replace && line.is_bound() && line->count_convoys()) {
		create_win({ 20, 20 }, new replace_frame_t(line), w_info, magic_replace_line + line.get_id());
		return true;
	}
	else if (comp == &bt_line_color_editor && line.is_bound()) {
		destroy_win( magic_line_color_gui_t );
		create_win({ 20, 20 }, new line_color_gui_t(line), w_info, magic_line_color_gui_t);
		return true;
	}
	else if (comp == &sortedby) {
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			set_sortierung((sort_mode_t)tmp);
		}
		else {
			sortedby.set_selection(0);
			set_sortierung(by_name);
		}
		default_sortmode = (uint8)tmp;
		update_lineinfo(line);
	}
	else if (comp == &sort_order) {
		set_reverse(!get_reverse());
		update_lineinfo(line);
		sort_order.pressed = sortreverse;
	}
	else if (comp == &bt_mode_convois) {
		selected_cnvlist_mode[player->get_player_nr()] = (selected_cnvlist_mode[player->get_player_nr()] + 1) % gui_convoy_formation_t::CONVOY_OVERVIEW_MODES;
		bt_mode_convois.set_text(gui_convoy_formation_t::cnvlist_mode_button_texts[selected_cnvlist_mode[player->get_player_nr()]]);
		update_lineinfo(line);
	}
	else if(comp == &livery_selector)
	{
			sint32 livery_selection = livery_selector.get_selection();
			if(livery_selection < 0)
			{
				livery_selector.set_selection(0);
				livery_selection = 0;
			}
			livery_scheme_index = livery_scheme_indices.empty()? 0 : livery_scheme_indices[livery_selection];
			if (line.is_bound())
			{
				tool_t *tmp_tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
				cbuffer_t buf;
				buf.printf( "V,%i,%i", line.get_id(), livery_scheme_index );
				tmp_tool->set_default_param(buf);
				welt->set_tool( tmp_tool, player );
				// since init always returns false, it is safe to delete immediately
				delete tmp_tool;
			}
	}
	else if (comp == &tabs) {
		int const tab = tabs.get_active_tab_index();
		uint8 old_selected_tab = selected_tab[player->get_player_nr()];
		selected_tab[player->get_player_nr()] = tabs_to_lineindex[tab];
		if(  old_selected_tab == simline_t::line  &&  selected_line[player->get_player_nr()][0].is_bound()  &&  selected_line[player->get_player_nr()][0]->get_linetype() == selected_tab[player->get_player_nr()]  ) {
			// switching from general to same waytype tab while line is seletced => use current line instead
			selected_line[player->get_player_nr()][selected_tab[player->get_player_nr()]] = selected_line[player->get_player_nr()][0];
		}
		update_lineinfo( selected_line[player->get_player_nr()][selected_tab[player->get_player_nr()]] );
		build_line_list(tab);
		if (tab>0) {
			bt_new_line.enable( (welt->get_active_player() == player || player == welt->get_player(1))  &&  !welt->get_active_player()->is_locked() );
		}
		else {
			bt_new_line.disable();
		}
	}
	else if(comp == &info_tabs){
		selected_convoy_tab = (uint8)info_tabs.get_active_tab_index();
		if (selected_convoy_tab == 3) {
			cont_haltlist.init();
		}
	}
	else if (comp == &scl) {
		if(  line_scrollitem_t *li=(line_scrollitem_t *)scl.get_element(v.i)  ) {
			update_lineinfo( li->get_line() );
		}
		else {
			// no valid line
			update_lineinfo(linehandle_t());
		}
		selected_line[player->get_player_nr()][selected_tab[player->get_player_nr()]] = line;
		selected_line[player->get_player_nr()][0] = line; // keep these the same in overview
	}
	else if (comp == &inp_filter) {
		if(  strcmp(old_schedule_filter,schedule_filter)  ) {
			build_line_list(tabs.get_active_tab_index());
			strcpy(old_schedule_filter,schedule_filter);
		}
	}
	else if (comp == &inp_name) {
		rename_line();
	}
	else if (comp == &filter_btn_all_pas) {
		line_type_flags ^= (1 << simline_t::all_pas);
		filter_btn_all_pas.pressed = line_type_flags & (1 << simline_t::all_pas);
		build_line_list(tabs.get_active_tab_index());
	}
	else if (comp == &filter_btn_all_mails) {
		line_type_flags ^= (1 << simline_t::all_mail);
		filter_btn_all_mails.pressed = line_type_flags & (1 << simline_t::all_mail);
		build_line_list(tabs.get_active_tab_index());
	}
	else if (comp == &filter_btn_all_freights) {
		line_type_flags ^= (1 << simline_t::all_freight);
		filter_btn_all_freights.pressed = line_type_flags & (1 << simline_t::all_freight);
		build_line_list(tabs.get_active_tab_index());
	}
	else if (comp == &bt_hw_filter_by_line) {
		bt_hw_filter_by_line.pressed = !bt_hw_filter_by_line.pressed;
		cont_haltlist.set_filter_by_line( bt_hw_filter_by_line.pressed );
	}
	else if (comp == &bt_hw_show_halt_name) {
		bt_hw_show_halt_name.pressed = !bt_hw_show_halt_name.pressed;
		cont_haltlist.set_show_name( bt_hw_show_halt_name.pressed );
	}
	else if (comp == &bt_hw_divided_class) {
		bt_hw_divided_class.pressed = !bt_hw_divided_class.pressed;
		cont_haltlist.set_divided_by_class(bt_hw_divided_class.pressed);
	}
	else if (comp == &reset_all_pass_button) {
		cbuffer_t buf;
		buf.printf("%hhi", goods_manager_t::INDEX_PAS);
		for (uint32 icnv = 0; icnv < line->count_convoys(); icnv++)
		{
			convoihandle_t cnv = line->get_convoy(icnv);
			cnv->call_convoi_tool('i', buf);
		}
		update_lineinfo( line );
		return true;
	}
	else if (comp == &reset_all_mail_button) {
		cbuffer_t buf;
		buf.printf("%hhi", goods_manager_t::INDEX_MAIL);
		for (uint32 icnv = 0; icnv < line->count_convoys(); icnv++)
		{
			convoihandle_t cnv = line->get_convoy(icnv);
			cnv->call_convoi_tool('i', buf);
		}
	}
	else {
		if (line.is_bound()) {
			for ( int i = 0; i<MAX_LINE_COST; i++) {
				if (comp == &filterButtons[i]) {
					bFilterStates ^= (1 << i);
					if(bFilterStates & (1 << i)) {
						chart.show_curve(i);
					}
					else {
						chart.hide_curve(i);
					}
					break;
				}
			}
		}
	}

	return true;
}


void schedule_list_gui_t::reset_line_name()
{
	// change text input of selected line
	if (line.is_bound()) {
		tstrncpy(old_line_name, line->get_name(), sizeof(old_line_name));
		tstrncpy(line_name, line->get_name(), sizeof(line_name));
		inp_name.set_text(line_name, sizeof(line_name));
	}
}


void schedule_list_gui_t::rename_line()
{
	if (line.is_bound()
		&& ((player == welt->get_active_player() && !welt->get_active_player()->is_locked()) || welt->get_active_player() == welt->get_public_player())) {
		const char *t = inp_name.get_text();
		// only change if old name and current name are the same
		// otherwise some unintended undo if renaming would occur
		if(  t  &&  t[0]  &&  strcmp(t, line->get_name())  &&  strcmp(old_line_name, line->get_name())==0) {
			// text changed => call tool
			cbuffer_t buf;
			buf.printf( "l%u,%s", line.get_id(), t );
			tool_t *tmp_tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
			tmp_tool->set_default_param( buf );
			welt->set_tool( tmp_tool, line->get_owner() );
			// since init always returns false, it is safe to delete immediately
			delete tmp_tool;
			// do not trigger this command again
			tstrncpy(old_line_name, t, sizeof(old_line_name));
		}
	}
}


void schedule_list_gui_t::draw(scr_coord pos, scr_size size)
{
	if(  old_line_count != player->simlinemgmt.get_line_count()  ) {
		show_lineinfo( line );
	}

	if(  old_player != welt->get_active_player()  ) {
		// deativate buttons, if not curretn player
		old_player = welt->get_active_player();
		const bool activate = (old_player == player || old_player == welt->get_player( 1 )) && !welt->get_active_player()->is_locked();
		bt_delete_line.enable( activate );
		bt_edit_line.enable( activate );
		bt_new_line.enable( activate   &&  tabs.get_active_tab_index() > 0);
		bt_withdraw_line.set_visible( activate );
		livery_selector.enable( activate );
		bt_line_color_editor.enable( activate );
		reset_all_pass_button.enable( activate );
		reset_all_mail_button.enable( activate );
	}

	// if search string changed, update line selection
	if(  strcmp( old_schedule_filter, schedule_filter )  ) {
		build_line_list(tabs.get_active_tab_index());
		strcpy( old_schedule_filter, schedule_filter );
	}

	gui_frame_t::draw(pos, size);

	if(  line.is_bound()  ) {
		if(  (!line->get_schedule()->empty()  &&  !line->get_schedule()->matches( welt, last_schedule ))  ||  last_vehicle_count != line->count_convoys()  ||  line->get_goods_catg_index().get_count() != line_goods_catg_count  ) {
			update_lineinfo( line );
		}
		else if (info_tabs.get_active_tab_index() == 1)
		{
			// Keep the Fleet tab live without overwriting a control that the
			// player is editing.
			update_fleet_tab(false);
		}

		PUSH_CLIP( pos.x + 1, pos.y + D_TITLEBAR_HEIGHT, size.w - 2, size.h - D_TITLEBAR_HEIGHT);
		display(pos);
		POP_CLIP();
	}

	for (int i = 0; i < MAX_LINE_COST; i++) {
		filterButtons[i].pressed = ((bFilterStates & (1 << i)) != 0);
	}
}


void schedule_list_gui_t::display(scr_coord pos)
{
	uint32 icnv = line->count_convoys();

	cbuffer_t buf;
	char ctmp[128];
	int top = D_TITLEBAR_HEIGHT + D_MARGIN_TOP + D_EDIT_HEIGHT + D_V_SPACE;
	int left = LINE_NAME_COLUMN_WIDTH;

	sint64 profit = line->get_finance_history(0,LINE_PROFIT);

	uint32 line_total_vehicle_count=0;
	for (uint32 i = 0; i<icnv; i++) {
		convoihandle_t const cnv = line->get_convoy(i);
		// we do not want to count the capacity of depot convois
		if (!cnv->in_depot()) {
			line_total_vehicle_count += cnv->get_vehicle_count();
		}
	}

	if (last_schedule != line->get_schedule() && origin_halt.is_bound()) {
		uint8 halt_symbol_style = gui_schedule_entry_number_t::halt;
		lb_line_origin.buf().append(origin_halt->get_name());
		if ((origin_halt->registered_lines.get_count() + origin_halt->registered_convoys.get_count()) > 1) {
			halt_symbol_style = gui_schedule_entry_number_t::interchange;
		}
		halt_entry_origin.init(halt_entry_idx[0], origin_halt->get_owner()->get_player_color1(), halt_symbol_style, origin_halt->get_basis_pos3d());
		if (line->get_schedule()->is_mirrored()) {
			routebar_middle.set_line_style(gui_colored_route_bar_t::doubled);
		}
		else {
			routebar_middle.set_line_style(gui_colored_route_bar_t::downward);
		}
		if ( line->get_line_color()==0 ) {
			routebar_middle.set_color( player->get_player_color1() ); // uint8
		}
		else {
			PIXVAL line_color = line->get_line_color();
			if (!is_dark_color(line_color)) line_color = display_blend_colors(line_color, color_idx_to_rgb(COL_BLACK), 15);
			routebar_middle.set_color( line_color );
		}
		if (destination_halt.is_bound()) {
			halt_entry_dest.set_visible(true);
			halt_symbol_style = gui_schedule_entry_number_t::halt;
			if ((destination_halt->registered_lines.get_count() + destination_halt->registered_convoys.get_count()) > 1) {
				halt_symbol_style = gui_schedule_entry_number_t::interchange;
			}
			halt_entry_dest.init(halt_entry_idx[1], destination_halt->get_owner()->get_player_color1(), halt_symbol_style, destination_halt->get_basis_pos3d());
			lb_line_destination.buf().append(destination_halt->get_name());
			lb_travel_distance.buf().printf("%s: %.1fkm ", translator::translate("travel_distance"), (float)(line->get_travel_distance()*world()->get_settings().get_meters_per_tile()/1000.0));
			lb_travel_distance.update();
		}
		else {
			halt_entry_dest.set_visible(false);
		}
	}
	lb_line_origin.update();
	lb_line_destination.update();

	// convoy count
	if (line->get_convoys().get_count()>1) {
		lb_convoy_count.buf().printf(translator::translate("%d convois"), icnv);
	}
	else {
		lb_convoy_count.buf().append(line->get_convoys().get_count() == 1 ? translator::translate("1 convoi") : translator::translate("no convois"));
	}

	if (icnv && line_total_vehicle_count>1) {
		lb_convoy_count.buf().printf(translator::translate(", %d vehicles"), line_total_vehicle_count);
	}
	lb_convoy_count.update();

	update_fleet_tab(true);

	// Display service frequency
	const sint64 service_frequency = line->get_service_frequency();
	if(service_frequency)
	{
		char as_clock[32];
		welt->sprintf_ticks(as_clock, sizeof(as_clock), service_frequency);
		lb_service_frequency.buf().printf(" %s",  as_clock);
		lb_service_frequency.set_color(line->get_state() & simline_t::line_missing_scheduled_slots ? color_idx_to_rgb(COL_DARK_TURQUOISE) : SYSCOL_TEXT);
		lb_service_frequency.set_tooltip(line->get_state() & simline_t::line_missing_scheduled_slots ? translator::translate(line_alert_helptexts[1]) : "");
	}
	else {
		lb_service_frequency.buf().append("--:--");
	}
	lb_service_frequency.update();

	int len2 = display_proportional_clip_rgb(pos.x+LINE_NAME_COLUMN_WIDTH, pos.y+top, translator::translate("Gewinn"), ALIGN_LEFT, SYSCOL_TEXT, true );
	money_to_string(ctmp, profit/100.0);
	len2 += display_proportional_clip_rgb(pos.x+LINE_NAME_COLUMN_WIDTH+len2+5, pos.y+top, ctmp, ALIGN_LEFT, profit>=0?MONEY_PLUS:MONEY_MINUS, true );

	top += D_BUTTON_HEIGHT + LINESPACE + 1;
	left = LINE_NAME_COLUMN_WIDTH + D_BUTTON_WIDTH*1.5 + D_V_SPACE;
	buf.clear();
	// show the state of the line, if interresting
	if (line->get_state() & simline_t::line_nothing_moved) {
		if (skinverwaltung_t::alerts) {
			display_color_img_with_tooltip(skinverwaltung_t::alerts->get_image_id(2), pos.x + left, pos.y + top + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(line_alert_helptexts[0]));
			left += GOODS_SYMBOL_CELL_WIDTH;
		}
		else {
			buf.append(translator::translate(line_alert_helptexts[0]));
		}
	}
	if (line->count_convoys() && line->get_state() & simline_t::line_has_upgradeable_vehicles) {
		if (skinverwaltung_t::upgradable) {
			display_color_img_with_tooltip(skinverwaltung_t::upgradable->get_image_id(1), pos.x + left, pos.y + top + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(line_alert_helptexts[4]));
			left += GOODS_SYMBOL_CELL_WIDTH;
		}
		else if (!buf.len() && line->get_state_color() == COL_PURPLE) {
			buf.append(translator::translate(line_alert_helptexts[4]));
		}
	}
	if (line->count_convoys() && line->get_state() & simline_t::line_has_obsolete_vehicles) {
		if (skinverwaltung_t::alerts) {
			display_color_img_with_tooltip(skinverwaltung_t::alerts->get_image_id(1), pos.x + left, pos.y + top + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(line_alert_helptexts[2]));
			left += GOODS_SYMBOL_CELL_WIDTH;
		}
		else if (!buf.len()) {
			buf.append(translator::translate(line_alert_helptexts[2]));
		}
	}
	if (line->count_convoys() && line->get_state() & simline_t::line_overcrowded) {
		if (skinverwaltung_t::pax_evaluation_icons) {
			display_color_img_with_tooltip(skinverwaltung_t::pax_evaluation_icons->get_image_id(1), pos.x + left, pos.y + top + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(line_alert_helptexts[3]));
			left += GOODS_SYMBOL_CELL_WIDTH;
		}
		else if (!buf.len() && line->get_state_color() == COL_DARK_PURPLE) {
			buf.append(translator::translate(line_alert_helptexts[3]));
		}
	}
	if (buf.len() > 0) {
		display_proportional_clip_rgb(pos.x + left, pos.y + top, buf, ALIGN_LEFT, line->get_state_color(), true);
	}
	cont_line_info.set_size(cont_line_info.get_min_size());
}


void schedule_list_gui_t::set_windowsize(scr_size size)
{
	gui_frame_t::set_windowsize(size);

	int rest_width = get_windowsize().w-LINE_NAME_COLUMN_WIDTH;
	int button_per_row=max(1,rest_width/(D_BUTTON_WIDTH+D_H_SPACE));
	int button_rows= MAX_LINE_COST/button_per_row + ((MAX_LINE_COST%button_per_row)!=0);

	scroll_line_info.set_size( scr_size(LINE_NAME_COLUMN_WIDTH-4, get_client_windowsize().h - scroll_line_info.get_pos().y-1) );

	info_tabs.set_size( scr_size(rest_width+2, get_windowsize().h-info_tabs.get_pos().y-D_TITLEBAR_HEIGHT-1) );
	scrolly_convois.set_size( scr_size(info_tabs.get_size().w+1, info_tabs.get_size().h - scrolly_convois.get_pos().y - D_H_SPACE-1) );
	chart.set_size(scr_size(rest_width-68-D_MARGIN_RIGHT, SCL_HEIGHT-14-(button_rows*(D_BUTTON_HEIGHT+D_H_SPACE))));
	cont_line_name.set_size(scr_size(rest_width-D_MARGIN_RIGHT, D_EDIT_HEIGHT));

	int y=SCL_HEIGHT-(button_rows*(D_BUTTON_HEIGHT+D_H_SPACE))+18;
	for (int i=0; i<MAX_LINE_COST; i++) {
		filterButtons[i].set_pos( scr_coord((i%button_per_row)*(D_BUTTON_WIDTH+D_H_SPACE)+D_H_SPACE, y+(i/button_per_row)*(D_BUTTON_HEIGHT+D_H_SPACE)) );
	}
}

void schedule_list_gui_t::build_line_list(int selected_tab)
{
	sint32 sel = -1;
	scl.clear_elements();
	player->simlinemgmt.get_lines(tabs_to_lineindex[selected_tab], &lines, get_filter_type_bits(), true);

	FOR(vector_tpl<linehandle_t>, const l, lines) {
		// search name
		if(  utf8caseutf8(l->get_name(), schedule_filter)  ) {
			scl.new_component<line_scrollitem_t>(l);

			if (  line == l  ) {
				sel = scl.get_count() - 1;
			}
		}
	}

	scl.set_selection( sel );
	line_scrollitem_t::sort_mode = (line_scrollitem_t::sort_modes_t)current_sort_mode;
	scl.sort( 0 );
	scl.set_size(scl.get_size());

	old_line_count = player->simlinemgmt.get_line_count();
}


/* hides show components */
void schedule_list_gui_t::update_lineinfo(linehandle_t new_line)
{
	// change line name if necessary
	if (line.is_bound()) {
		rename_line();
	}
	if(new_line.is_bound()) {
		const bool activate = (old_player == player || old_player == welt->get_player(1)) && !welt->get_active_player()->is_locked();
		// ok, this line is visible
		cont_line_name.set_visible(true);
		scrolly_convois.set_visible(true);
		scroll_halt_waiting.set_visible(true);
		scroll_line_info.set_visible(new_line->get_schedule()->get_count()>0);
		//inp_name.set_visible(true);
		wt_symbol.set_waytype(new_line->get_schedule()->get_waytype());
		lc_preview.set_line(new_line);
		lc_preview.set_base_color( new_line->get_line_color() );
		lc_preview.set_visible(new_line->get_line_color_index()!=255);
		livery_selector.set_visible(true);
		bt_line_color_editor.set_visible(true);
		bt_replace.set_visible(true);

		if( goods_manager_t::passengers->get_number_of_classes()>1 ) {
			reset_all_pass_button.set_visible(new_line->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS));
		}
		if( goods_manager_t::mail->get_number_of_classes()>1 ) {
			reset_all_mail_button.set_visible(new_line->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL));
		}
		cont_line_capacity_by_catg.set_line(new_line);
		cont_by_accommo.set_line(new_line);
		cont_line_network.set_line(new_line);

		cont_times_history.set_visible(true);
		cont_times_history.remove_all();
		cont_times_history.new_component<gui_times_history_t>(new_line, convoihandle_t(), false);
		if (!new_line->get_schedule()->is_mirrored() && new_line->has_reverse_scheduled_convoy()) {
			cont_times_history.new_component<gui_times_history_t>(new_line, convoihandle_t(), true);
		}

		cont_haltlist.set_visible(true);
		cont_haltlist.set_line(new_line);

		livery_selector.set_visible(true);

		// fill container with info of line's convoys
		// we do it here, since this list needs only to be
		// refreshed when the user selects a new line
		line_convoys.clear();
		line_convoys = new_line->get_convoys();
		sort_list();

		uint32 i, icnv = 0;
		line_goods_catg_count = new_line->get_goods_catg_index().get_count();
		icnv = new_line->count_convoys();
		// display convoys of line
		cont.remove_all();
		while (!convoy_infos.empty()) {
			delete convoy_infos.pop_back();
		}
		convoy_infos.resize(icnv);
		scr_coord_val ypos = 0;
		for(i = 0;  i<icnv;  i++  ) {
			gui_convoiinfo_t* const cinfo = new gui_convoiinfo_t(line_convoys.get_element(i), false);
			cinfo->set_pos(scr_coord(0, ypos-D_MARGIN_TOP));
			scr_size csize = cinfo->get_min_size();
			cinfo->set_size(scr_size(400, csize.h-D_MARGINS_Y));
			cinfo->set_mode(selected_cnvlist_mode[player->get_player_nr()]);
			cinfo->set_switchable_label(sortby==by_loadfactor_pax? 10:sortby);
			convoy_infos.append(cinfo);
			cont.add_component(cinfo);
			ypos += csize.h - D_MARGIN_TOP-D_V_SPACE*2;
		}
		cont.set_size(scr_size(600, ypos));

		bt_delete_line.disable();
		bt_withdraw_line.set_visible(false);
		if( icnv>0 ) {
			bt_withdraw_line.set_visible(true);
		}
		else {
			bt_delete_line.enable( activate );
		}
		bt_edit_line.enable( activate );

		bt_withdraw_line.pressed = new_line->get_withdraw();
		bt_withdraw_line.background_color = color_idx_to_rgb( bt_withdraw_line.pressed ? COL_DARK_YELLOW-1 : COL_YELLOW );
		bt_withdraw_line.text_color = color_idx_to_rgb(bt_withdraw_line.pressed ? COL_WHITE : COL_BLACK);

		livery_selector.set_focusable(true);
		livery_selector.clear_elements();
		livery_scheme_indices.clear();
		if( icnv>0 ) {
			// build available livery schemes list for this line
			if (new_line->count_convoys()) {
				const uint16 month_now = welt->get_timeline_year_month();
				vector_tpl<livery_scheme_t*>* schemes = welt->get_settings().get_livery_schemes();

				for(uint32 i = 0; i < schemes->get_count(); i ++)
				{
					bool found = false;
					livery_scheme_t* scheme = schemes->get_element(i);
					if (scheme->is_available(month_now))
					{
						for (uint32 j = 0; j < new_line->count_convoys(); j++)
						{
							convoihandle_t const cnv_in_line = new_line->get_convoy(j);
							for (int k = 0; k < cnv_in_line->get_vehicle_count(); k++) {
								const vehicle_desc_t *desc = cnv_in_line->get_vehicle(k)->get_desc();
								if (desc->get_livery_count()) {
									const char* test_livery = scheme->get_latest_available_livery(month_now, desc);
									if (test_livery) {
										if (scheme->is_contained(test_livery, month_now)) {
											livery_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(scheme->get_name()), SYSCOL_TEXT);
											livery_scheme_indices.append(i);
											found = true;
											break;
										}
									}
								}
							}
							if (found) {
								livery_selector.set_selection(livery_scheme_indices.index_of(i));
								break;
							}
						}
					}
				}
			}
		}
		if(livery_scheme_indices.empty()) {
			livery_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("No livery"), SYSCOL_TEXT);
			livery_selector.set_selection(0);
		}


		uint8 entry_idx = 0;
		halt_entry_idx[0] = 255;
		halt_entry_idx[1] = 255;
		origin_halt = halthandle_t();
		destination_halt = halthandle_t();
		FORX(minivec_tpl<schedule_entry_t>, const& i, new_line->get_schedule()->entries, ++entry_idx) {
			halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
			if (halt.is_bound()) {
				if (halt_entry_idx[0] == 255) {
					halt_entry_idx[0] = entry_idx;
					origin_halt = halt;
				}
				else {
					halt_entry_idx[1] = entry_idx;
					destination_halt = halt;
				}
			}
		}

		// chart
		chart.remove_curves();
		for(i=0; i<MAX_LINE_COST; i++)  {
			const uint8 precision = statistic_type[i] == gui_chart_t::MONEY ? 2 : (statistic_type[i]==gui_chart_t::PAX_KM || statistic_type[i]==gui_chart_t::KG_KM || statistic_type[i]==gui_chart_t::TON_KM) ? 1 : 0;
			chart.add_curve(color_idx_to_rgb(cost_type_color[i]), new_line->get_finance_history(), MAX_LINE_COST, statistic[i], MAX_MONTHS, statistic_type[i], filterButtons[i].pressed, true, precision);
			if (bFilterStates & (1 << i)) {
				chart.show_curve(i);
			}
		}
		chart.set_visible(true);

		// update passenger load factor
		cont_load_factor.set_visible(false);
		if (new_line->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS)) {
			cont_load_factor.set_visible(true);
			lb_load_factor_pax_year.buf().clear();
			lb_load_factor_pax_last_month.buf().clear();

			lb_load_factor_pax_year.buf().printf("%.1f%%", new_line->get_load_factor_pax_year()/10.0);
			lb_load_factor_pax_last_month.buf().printf("%.1f%%", new_line->get_load_factor_pax_last_month() / 10.0);

			lb_load_factor_pax_year.update();
			lb_load_factor_pax_last_month.update();
		}

		// update transportation density
		cont_transport_density.set_visible(true);
		cont_transport_density.remove_all();
		if (float line_distance = new_line->get_travel_distance()*world()->get_settings().get_meters_per_tile()/100.0) {
			gui_table_header_t *th = cont_transport_density.new_component<gui_table_header_t>("");
			th->set_flexible(true, false);
			th->set_fixed_width(D_FIXED_SYMBOL_WIDTH);
			cont_transport_density.new_component<gui_table_header_t>("Last Month")->set_flexible(true, false);
			cont_transport_density.new_component<gui_table_header_t>("Yearly average")->set_flexible(true, false);

			for (uint8 j=0; j<3; j++) {
				// 0=pax, 1=mail, 2=freight
				if (j==0 && !new_line->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS)) {
					continue;
				}
				else if (j==1 && !new_line->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL)) {
					continue;
				}
				if (j==2) {
					bool any_freight = false;
					for (uint8 catg_index = goods_manager_t::INDEX_NONE + 1; catg_index < goods_manager_t::get_max_catg_index(); catg_index++)
					{
						if (new_line->get_goods_catg_index().is_contained(catg_index)) {
							any_freight = true;
							break;
						}
					}
					if (!any_freight) break;
				}

				// display data
				cont_transport_density.new_component<gui_image_t>((j==0 ? skinverwaltung_t::passengers : j==1 ? skinverwaltung_t::mail : skinverwaltung_t::goods)->get_image_id(0), 0, 0, true)->set_padding(scr_size(4, 0));

				sint64 hist_sum = new_line->get_finance_history(1, j==0? LINE_PAX_DISTANCE : j==1? LINE_MAIL_DISTANCE: LINE_PAYLOAD_DISTANCE);
				gui_table_cell_buf_t *td = cont_transport_density.new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND, gui_label_t::right, true);
				td->buf().printf("%.1f ", hist_sum/line_distance);
				switch (j)
				{
					case 0:
						td->buf().append(translator::translate(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)->get_mass()));
						break;
					case 1:
						td->buf().append(translator::translate("kg"));
						break;
					default:
						td->buf().append(translator::translate("tonnen"));
						break;
				}
				td->buf().append(" ");
				td->set_padding(scr_size(2,2));
				td->set_flexible(true, false);
				td->update();

				for (uint8 i=2; i < MAX_MONTHS; i++) {
					hist_sum += new_line->get_finance_history(2, j==0 ? LINE_PAX_DISTANCE : j==1 ? LINE_MAIL_DISTANCE : LINE_PAYLOAD_DISTANCE);
				}
				// yearly average
				td = cont_transport_density.new_component<gui_table_cell_buf_t>("", SYSCOL_TD_BACKGROUND, gui_label_t::right, true);
				td->buf().printf("%.1f ", hist_sum / line_distance / (MAX_MONTHS-1));
				switch (j)
				{
					case 0:
						td->buf().append(translator::translate(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)->get_mass()));
						break;
					case 1:
						td->buf().append(translator::translate("kg"));
						break;
					default:
						td->buf().append(translator::translate("tonnen"));
						break;
				}
				td->buf().append(" ");
				td->set_padding(scr_size(2,2));
				td->set_flexible(true, false);
				td->update();
			}
		}

		// has this line a single running convoi?
		if(  new_line.is_bound()  &&  new_line->count_convoys() > 0  ) {
			// set this schedule as current to show on minimap if possible
			minimap_t::get_instance()->set_selected_cnv( new_line->get_convoy(0) );
		}
		else {
			minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );
		}

		delete last_schedule;
		last_schedule = new_line->get_schedule()->copy();
		last_vehicle_count = new_line->count_convoys();
		resize(scr_size(0,0));
	}
	else if(  cont_line_name.is_visible()  ) {
		// previously a line was visible
		// thus the need to hide everything
		line_convoys.clear();
		cont.remove_all();
		cont_times_history.remove_all();
		cont_transport_density.remove_all();
		cont_line_capacity_by_catg.set_line(linehandle_t());
		cont_by_accommo.set_line();
		cont_line_network.set_line(linehandle_t());
		cont_line_name.set_visible(false);
		scrolly_convois.set_visible(false);
		scroll_halt_waiting.set_visible(false);
		livery_selector.set_visible(false);
		bt_line_color_editor.set_visible(false);
		bt_replace.set_visible(false);
		scroll_line_info.set_visible(false);
		lc_preview.set_visible(false);
		cont_times_history.set_visible(false);
		cont_transport_density.set_visible(false);
		cont_haltlist.set_visible(false);
		reset_all_pass_button.set_visible(false);
		reset_all_mail_button.set_visible(false);
		scl.set_selection(-1);
		bt_delete_line.disable();
		bt_edit_line.disable();
		for(int i=0; i<MAX_LINE_COST; i++)  {
			chart.hide_curve(i);
		}
		chart.set_visible(true);

		// hide schedule on minimap (may not current, but for safe)
		minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );

		delete last_schedule;
		last_schedule = NULL;
		last_vehicle_count = 0;
	}
	line = new_line;
	bt_access_minimap.set_visible(line.is_bound());
	bt_line_color_editor.set_visible(line.is_bound());

	reset_line_name();
}


void schedule_list_gui_t::update_fleet_tab(bool update_controls)
{
	if (!line.is_bound())
	{
		return;
	}

	uint16 deployed = line->count_deployed_convoys();
	uint16 held = 0;
	uint16 under = 0;
	sint64 monthly_fixed = 0;
	for (auto const c : line->get_convoys())
	{
		if (!c.is_bound())
		{
			continue;
		}
		const int state = c->get_state();
		if (state == convoi_t::INITIAL)
		{
			held++;
			monthly_fixed += c->get_fixed_cost();
		}
		else if (convoi_t::is_inside_depot_state(state))
		{
			under++;
		}
	}

	uint16 pool_consists = 0;
	cont_fleet_depots.remove_all();
	cont_fleet_depots.set_table_layout(1, 0);
	schedule_t *sch = line->get_schedule();
	for (uint8 i = 0; sch && i < sch->get_count(); i++)
	{
		const grund_t *gr = welt->lookup(sch->entries[i].pos);
		depot_t *dep = gr ? gr->get_depot() : NULL;
		if (!dep)
		{
			continue;
		}
		const uint16 entry_id = sch->entries[i].unique_entry_id;
		const bool has_order = sch->entry_has_consist_order(entry_id) && sch->get_consist_order(entry_id).get_count() > 0;
		uint16 line_under = 0;
		for (auto const c : dep->get_convoy_list())
		{
			if (c.is_bound() && c->get_line().is_bound() && c->get_line().get_id() == line.get_id()
				&& (c->get_state() == convoi_t::MAINTENANCE || c->get_state() == convoi_t::OVERHAUL))
			{
				line_under++;
			}
		}
		uint16 available = 0;
		for (auto const v : dep->get_vehicle_list())
		{
			monthly_fixed += v->get_fixed_cost(welt);
			if (!v->has_depot_job() || v->is_depot_job_complete())
			{
				available++;
			}
		}
		uint16 consists = 0;
		if (has_order)
		{
			const consist_order_t &order = sch->get_consist_order(entry_id);
			const uint16 slots = (uint16)order.get_count();
			uint16 min_match = 65535;
			for (uint16 s = 0; s < slots; s++)
			{
				uint16 match = 0;
				for (auto const v : dep->get_vehicle_list())
				{
					if (v->has_depot_job() && !v->is_depot_job_complete())
					{
						continue;
					}
					for (uint8 a = 0; a < order.get_order(s).get_count(); a++)
					{
						if (v->matches_consist_order_element(order.get_order(s), a))
						{
							match++;
							break;
						}
					}
				}
				min_match = min(min_match, match);
			}
			if (slots > 0 && min_match != 65535)
			{
				consists = min(min_match, available / slots);
			}
			pool_consists += consists;
		}
		gui_label_buf_t *row = cont_fleet_depots.new_component<gui_label_buf_t>();
		row->init(has_order ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED, gui_label_t::left);
		row->buf().printf("%s: %s, %d under maintenance, %d pool (%d available), up to %d consist(s)",
			dep->get_name(), translator::translate(has_order ? "order set" : "no order"),
			line_under, dep->get_vehicle_list().get_count(), available, consists);
		row->update();
		if (!has_order)
		{
			row->set_tooltip("This depot has no departure consist order. Add one on the depot's schedule entry to re-form trains here.");
		}
	}

	const uint32 pax_month = line->get_load_factor_pax_last_month();
	const uint32 pax_two = line->get_load_factor_pax_last_two_months();
	const uint32 goods_month = line->get_load_factor_goods_last_month();
	const uint32 goods_two = line->get_load_factor_goods_last_two_months();
	const uint32 policy_load = max(pax_two, goods_two);

	lb_fleet_summary.buf().printf(translator::translate("Deployed %d, held %d, under maintenance %d, total %d; held and pool fixed cost %lld per month"),
		(int)deployed, (int)held, (int)under, (int)line->count_convoys(), (long long)convert_money(monthly_fixed));
	lb_fleet_summary.update();
	lb_fleet_load.buf().printf(translator::translate("Passengers %.1f%% last month, %.1f%% two months; goods %.1f%% and %.1f%%; policy uses %.1f%%"),
		pax_month / 10.0, pax_two / 10.0, goods_month / 10.0, goods_two / 10.0, policy_load / 10.0);
	lb_fleet_load.update();
	lb_fleet_max.buf().printf(translator::translate("Maximum consists that can run now: %d (deployed %d + held %d + pool %d)"),
		(int)(deployed + held + pool_consists), (int)deployed, (int)held, (int)pool_consists);
	lb_fleet_max.update();

	const uint8 spacing_shift_mode = welt->get_settings().get_spacing_shift_mode();
	bool has_timed_stops = false;
	if (sch != NULL)
	{
		for (uint8 i = 0; i < sch->get_count(); i++)
		{
			if (sch->entries[i].minimum_loading > 0 || sch->entries[i].is_flag_set(schedule_entry_t::wait_for_time))
			{
				has_timed_stops = true;
				break;
			}
		}
	}

	const bool schedule_changed = (sch != fleet_spacing_schedule)
		|| (line.get_id() != fleet_spacing_line.get_id())
		|| (line->get_schedule_revision() != fleet_spacing_revision);
	if (schedule_changed)
	{
		fleet_spacing_line = line;
		fleet_spacing_schedule = sch;
		fleet_spacing_revision = line->get_schedule_revision();
		rebuild_fleet_spacing_rows(sch);
	}

	if (update_controls || schedule_changed)
	{
		bt_fleet_spacing_sync.pressed = line->is_fleet_spacing_sync() && has_timed_stops
			&& sch != NULL && sch->get_spacing() > 0;
		bt_same_spacing_shift_fleet.pressed = sch != NULL
			&& (sch->is_same_spacing_shift() || spacing_shift_mode <= settings_t::SPACING_SHIFT_PER_LINE);
	}

	const bool has_frequency = has_timed_stops && sch != NULL && sch->get_spacing() > 0;
	const bool sync = bt_fleet_spacing_sync.pressed && has_frequency;
	const bool shift_available = has_frequency && spacing_shift_mode != settings_t::SPACING_SHIFT_DISABLED;
	const bool same_shift = bt_same_spacing_shift_fleet.pressed;

	bt_fleet_spacing_sync.enable(has_frequency);
	numimp_fleet_spacing.enable(has_timed_stops);
	bt_set_fleet_spacing.enable(has_timed_stops);

	numimp_target_consists.enable(!sync);
	bt_set_target.enable(!sync);
	bt_fleet_policy.enable(!sync);
	bt_set_fleet_policy.enable(!sync);
	numimp_fleet_target_min.enable(!sync);
	numimp_fleet_target_max.enable(!sync);
	numimp_fleet_add_threshold.enable(!sync);
	numimp_fleet_remove_threshold.enable(!sync);
	numimp_fleet_add_months.enable(!sync);
	numimp_fleet_remove_months.enable(!sync);

	bt_same_spacing_shift_fleet.enable(shift_available && spacing_shift_mode > settings_t::SPACING_SHIFT_PER_LINE);
	numimp_fleet_shift.enable(shift_available && same_shift);
	cont_fleet_spacing_stops.set_visible(shift_available && !same_shift);

	uint16 first_shift = 0;
	if (sch != NULL)
	{
		for (uint8 i = 0; i < sch->get_count(); i++)
		{
			if (sch->entries[i].minimum_loading > 0 || sch->entries[i].is_flag_set(schedule_entry_t::wait_for_time))
			{
				first_shift = (uint16)sch->entries[i].spacing_shift;
				break;
			}
		}
	}

	if (sch != NULL)
	{
		if (sch->get_spacing() > 0)
		{
			welt->sprintf_ticks(str_fleet_spacing_clock, sizeof(str_fleet_spacing_clock), (welt->ticks_per_world_month * 12u) / sch->get_spacing());
			lb_fleet_spacing.buf().printf("%s", str_fleet_spacing_clock);
		}
		else
		{
			lb_fleet_spacing.buf().printf("%s", translator::translate("off"));
		}
		lb_fleet_spacing.update();
	}

	if (update_controls || schedule_changed || deployed != fleet_required_deployed)
	{
		fleet_required_cache = line->calc_required_convoys_for_spacing();
		fleet_required_deployed = deployed;
	}

	if (has_timed_stops && sch != NULL && sch->get_spacing() > 0)
	{
		if (fleet_required_cache > 0)
		{
			lb_fleet_required.buf().printf(translator::translate(sync
				? "This frequency sets the target to %d consists"
				: "This frequency implies %d consists"), (int)fleet_required_cache);
		}
		else
		{
			lb_fleet_required.buf().printf("%s", translator::translate("Waiting for a measured round-trip time"));
		}
	}
	else
	{
		lb_fleet_required.buf().printf("%s", translator::translate("No timed stops with spacing in this schedule"));
	}
	lb_fleet_required.update();

	if (update_controls || schedule_changed)
	{
		numimp_target_consists.set_value(line->get_target_convoy_count());
		bt_fleet_policy.pressed = line->get_fleet_policy() != 0;
		numimp_fleet_target_min.set_value(line->get_fleet_target_min());
		numimp_fleet_target_max.set_value(line->get_fleet_target_max());
		numimp_fleet_add_threshold.set_value(line->get_fleet_add_threshold());
		numimp_fleet_remove_threshold.set_value(line->get_fleet_remove_threshold());
		numimp_fleet_add_months.set_value(line->get_fleet_add_months());
		numimp_fleet_remove_months.set_value(line->get_fleet_remove_months());
		if (sch != NULL)
		{
			numimp_fleet_spacing.set_value(sch->get_spacing());
			numimp_fleet_shift.set_value(first_shift);
		}
		if (schedule_changed)
		{
			for (uint32 i = 0; i < fleet_shift_inputs.get_count(); i++)
			{
				const uint8 entry = fleet_shift_entries[i];
				if (sch != NULL && entry < sch->get_count())
				{
					fleet_shift_inputs[i]->set_value(sch->entries[entry].spacing_shift);
				}
			}
		}
	}
}


void schedule_list_gui_t::rebuild_fleet_spacing_rows(schedule_t *sch)
{
	cont_fleet_spacing_stops.remove_all();
	fleet_shift_inputs.clear();
	fleet_shift_entries.clear();

	cont_fleet_spacing_stops.set_table_layout(2, 0);
	cont_fleet_spacing_stops.set_spacing(scr_size(D_H_SPACE, D_V_SPACE));
	if (sch == NULL)
	{
		return;
	}

	const uint16 divisor = welt->get_settings().get_spacing_shift_divisor();
	for (uint8 i = 0; i < sch->get_count(); i++)
	{
		const schedule_entry_t &entry = sch->entries[i];
		if (entry.minimum_loading == 0 && !entry.is_flag_set(schedule_entry_t::wait_for_time))
		{
			continue;
		}
		gui_label_buf_t *name = cont_fleet_spacing_stops.new_component<gui_label_buf_t>();
		schedule_t::gimme_short_stop_name(name->buf(), welt, player, sch, i, 32);
		name->set_tooltip("The time shift within this stop's departure interval. The default unit is one minute of the 24-hour month.");
		name->update();

		gui_numberinput_t *input = cont_fleet_spacing_stops.new_component<gui_numberinput_t>();
		input->init(entry.spacing_shift, 0, divisor, 1);
		input->add_listener(this);
		fleet_shift_inputs.append(input);
		fleet_shift_entries.append(i);
	}
}


void schedule_list_gui_t::show_lineinfo(linehandle_t line)
{
	update_lineinfo(line);

	if(  line.is_bound()  ) {
		// rebuilding line list will also show selection
		for(  uint8 i=0;  i<max_idx;  i++  ) {
			if(  tabs_to_lineindex[i]==line->get_linetype()  ) {
				tabs.set_active_tab_index( i );
				build_line_list( i );
				break;
			}
		}
	}
}


bool schedule_list_gui_t::compare_convois(convoihandle_t const cnv1, convoihandle_t const cnv2)
{
	sint32 cmp = 0;

	switch (sortby) {
		default:
		case by_name:
			cmp = strcmp(cnv1->get_internal_name(), cnv2->get_internal_name());
			break;
		case by_schedule:
			cmp = cnv1->get_current_schedule_order() - cnv2->get_current_schedule_order();
			if( cmp==0 ) {
				cmp = cnv1->front()->get_route_index() - cnv2->front()->get_route_index();
			}
			break;
		case by_profit:
			cmp = sgn(cnv1->get_jahresgewinn() - cnv2->get_jahresgewinn());
			break;
		case by_loading_lvl:
			cmp = cnv1->get_loading_level() - cnv2->get_loading_level();
			break;
		case by_max_speed:
			cmp = cnv1->get_min_top_speed() - cnv2->get_min_top_speed();
			break;
		case by_power:
			cmp = cnv1->get_sum_power() - cnv2->get_sum_power();
			break;
		case by_age:
			cmp = cnv1->get_average_age() - cnv2->get_average_age();
			break;
		case by_value:
			cmp = sgn(cnv1->get_purchase_cost() - cnv2->get_purchase_cost());
			break;
		case by_loadfactor_pax:
			const sint64 factor_1 = cnv1->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) ? (sint64)cnv1->get_load_factor_pax() : -1;
			const sint64 factor_2 = cnv2->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) ? (sint64)cnv2->get_load_factor_pax() : -1;
			cmp = factor_1 - factor_2;
			break;
	}

	return sortreverse ? cmp > 0 : cmp < 0;
}


// sort the line convoy list
void schedule_list_gui_t::sort_list()
{
	if (!line_convoys.get_count()) {
		return;
	}
	std::sort(line_convoys.begin(), line_convoys.end(), compare_convois);
}


void schedule_list_gui_t::update_data(linehandle_t changed_line)
{
	if (changed_line.is_bound()) {
		const uint16 i = tabs.get_active_tab_index();
		if (tabs_to_lineindex[i] == simline_t::line  ||  tabs_to_lineindex[i] == changed_line->get_linetype()) {
			// rebuilds the line list, but does not change selection
			build_line_list(i);
		}

		// change text input of selected line and line letter code
		if (changed_line.get_id() == line.get_id()) {
			update_lineinfo(line);
			//reset_line_name();
		}
	}
}


uint32 schedule_list_gui_t::get_rdwr_id()
{
	return magic_line_management_t+player->get_player_nr();
}


void schedule_list_gui_t::rdwr( loadsave_t *file )
{
	scr_size size;
	sint32 cont_xoff, cont_yoff, halt_xoff, halt_yoff;
	if(  file->is_saving()  ) {
		size = get_windowsize();
		cont_xoff = scrolly_convois.get_scroll_x();
		cont_yoff = scrolly_convois.get_scroll_y();
		halt_xoff = scroll_halt_waiting.get_scroll_x();
		halt_yoff = scroll_halt_waiting.get_scroll_y();
	}
	size.rdwr( file );
	simline_t::rdwr_linehandle_t(file, line);
	// The Freight capacity curve was inserted at index 4 in 15.24. Older saves
	// store 15 button states in the old order, without that curve.
	const bool has_goods_capacity = file->is_version_ex_atleast(15, 24);
	const int chart_records = has_goods_capacity ? line_cost_t::MAX_LINE_COST : line_cost_t::MAX_LINE_COST - 1;
	for (int i = 0; i < chart_records; i++)
	{
		const int button_index = has_goods_capacity || i < 4 ? i : i + 1;
		bool b = filterButtons[button_index].pressed;
		file->rdwr_bool(b);
		filterButtons[button_index].pressed = b;
	}
	if (!has_goods_capacity)
	{
		filterButtons[4].pressed = false;
	}
	file->rdwr_long( cont_xoff );
	file->rdwr_long( cont_yoff );
	file->rdwr_long( halt_xoff );
	file->rdwr_long( halt_yoff );
	// open dialogue
	if(  file->is_loading()  ) {
		show_lineinfo( line );
		set_windowsize( size );
		resize( scr_coord(0,0) );
		scrolly_convois.set_scroll_position( cont_xoff, cont_yoff );
		scroll_halt_waiting.set_scroll_position( halt_xoff, halt_yoff );
	}
}
