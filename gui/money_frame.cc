/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string.h>

#include "money_frame.h"
#include "ai_option_t.h"
#include "headquarter_info.h"
#include "simwin.h"

#include "../simworld.h"
#include "../simdebug.h"
#include "../display/simgraph.h"
#include "../simcolor.h"
#include "../utils/simstring.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../dataobj/scenario.h"
#include "../dataobj/loadsave.h"

#include "components/gui_button_to_chart.h"

// for headquarter construction only ...
#include "../simmenu.h"

// for stats only ...
#include "../convoy.h"
#include "../simhalt.h"
#include "../simline.h"
#include "../linehandle_t.h"
#include "../simdepot.h"
#include "../obj/bruecke.h"
#include "../obj/gebaeude.h"
#include "../obj/wayobj.h"
#include "../obj/signal.h"
#include "../obj/roadsign.h"
#include "../obj/tunnel.h"
#include "../descriptor/way_desc.h"
#include "../descriptor/building_desc.h"
#include "../descriptor/tunnel_desc.h"
#include "../vehicle/simvehicle.h"
#include "halt_list_frame.h"
#include "schedule_list.h"
#include "convoi_frame.h"
#include "components/gui_divider.h"
#include "signalboxlist_frame.h"
#include "../simsignalbox.h"

// remembers last settings
static vector_tpl<sint32> bFilterStates;

#define BUTTONSPACE max(D_BUTTON_HEIGHT, LINESPACE)

#define FINANCE_TABLE_ROWS 11

static const char *cost_type_name[MAX_PLAYER_COST_BUTTON] =
{
	"Revenue",
	"Operation",
	"Vehicle maintenance",
	"Maintenance",
	"Road toll",
	"Ops Profit",
	"New Vehicles",
	"Construction_Btn",
	"Interest",
	"Gross Profit",
	"Transported",
	"Cash",
	"Assets",
	"Net Wealth",
	"Credit Limit",
	"Solvency Limit",
	"Margin (%)"
};

static const char *cost_tooltip[MAX_PLAYER_COST_BUTTON] =
{
  "Gross revenue",
  "Vehicle running costs per km",
  "Vehicle maintenance costs per month",
  "Recurring expenses of infrastructure maintenance",
  "The charges incurred or revenues earned by running on other players' ways",
  "Operating revenue less operating expenditure",
  "Capital expenditure on vehicle purchases and upgrades",
  "Capital expenditure on infrastructure",
  "Cost of overdraft interest payments",
  "Total income less total expenditure",
  "Number of units of passengers and goods transported",
  "Total liquid assets",
  "Total capital assets, excluding liabilities",
  "Total assets less total liabilities",
  "The maximum amount that can be borrowed without prohibiting further capital outlays",
  "The maximum amount that can be borrowed without going bankrupt",
  "Percentage of revenue retained as profit"
};


static const uint8 cost_type_color[MAX_PLAYER_COST_BUTTON] =
{
	COL_REVENUE,
	COL_OPERATION,
	COL_VEH_MAINTENANCE,
	COL_MAINTENANCE,
	COL_TOLL,
	COL_PROFIT,
	COL_NEW_VEHICLES,
	COL_CONSTRUCTION,
	COL_INTEREST,
	COL_CASH_FLOW,
	COL_TRANSPORTED,
	COL_CASH,
	COL_VEHICLE_ASSETS,
	COL_WEALTH,
	COL_SOFT_CREDIT_LIMIT,
	COL_HARD_CREDIT_LIMIT,
	COL_MARGIN
};


static const uint8 cost_type[3*MAX_PLAYER_COST_BUTTON] =
{
	ATV_REVENUE_TRANSPORT,          TT_ALL, MONEY,    // Income
	ATV_RUNNING_COST,               TT_ALL, MONEY,    // Vehicle running costs
	ATV_VEHICLE_MAINTENANCE,        TT_ALL, MONEY,    // Vehicle monthly maintenance
	ATV_INFRASTRUCTURE_MAINTENANCE, TT_ALL, MONEY,    // Upkeep
	ATV_WAY_TOLL,                   TT_ALL, MONEY,
	ATV_OPERATING_PROFIT,           TT_ALL, MONEY,
	ATV_NEW_VEHICLE,                TT_ALL, MONEY,    // New vehicles
	ATV_CONSTRUCTION_COST,          TT_ALL, MONEY,    // Construction
	ATC_INTEREST,                   TT_MAX, MONEY,    // Interest paid servicing debt
	ATV_PROFIT,                     TT_ALL, MONEY,
	ATV_TRANSPORTED,                TT_ALL, STANDARD, // all transported goods
	ATC_CASH,                       TT_MAX, MONEY,    // Cash
	ATV_NON_FINANCIAL_ASSETS,       TT_ALL, MONEY,    // value of all vehicles and buildings
	ATC_NETWEALTH,                  TT_MAX, MONEY,    // Total Cash + Assets
	ATC_SOFT_CREDIT_LIMIT,          TT_MAX, MONEY,    // Maximum amount that can be borrowed
	ATC_HARD_CREDIT_LIMIT,          TT_MAX, MONEY,    // Borrowing which will lead to insolvency
	ATV_PROFIT_MARGIN,              TT_ALL, PERCENT
};

static const sint8 cell_to_buttons[] =
{
	0,  -1,  -1,  -1,  -1,
	1,  -1,  -1,  -1,  -1,
	2,  -1,  -1,  -1,  -1,
	3,  -1,  -1,  -1,  -1,
	4,  -1,  -1,  -1,  -1,
	5,  -1,  -1,  11,  -1,
	6,  -1,  -1,  12,  -1,
	7,  -1,  -1,  13,  -1,
	8,  -1,  -1,  14,  -1,
	9,  -1,  -1,  15,  -1,
	10, -1,  -1,  16,  -1
};


// money label types: tt, atv, current/previous, type
static const uint16 label_type[] =
{
	TT_ALL, ATV_REVENUE_TRANSPORT,          0, MONEY,
	TT_ALL, ATV_REVENUE_TRANSPORT,          1, MONEY,
	TT_ALL, ATV_RUNNING_COST,               0, MONEY,
	TT_ALL, ATV_RUNNING_COST,               1, MONEY,
	TT_ALL, ATV_VEHICLE_MAINTENANCE,        0, MONEY,
	TT_ALL, ATV_VEHICLE_MAINTENANCE,        1, MONEY,
	TT_ALL, ATV_INFRASTRUCTURE_MAINTENANCE, 0, MONEY,
	TT_ALL, ATV_INFRASTRUCTURE_MAINTENANCE, 1, MONEY,
	TT_ALL, ATV_WAY_TOLL,                   0, MONEY,
	TT_ALL, ATV_WAY_TOLL,                   1, MONEY,
	TT_ALL, ATV_OPERATING_PROFIT,           0, MONEY,
	TT_ALL, ATV_OPERATING_PROFIT,           1, MONEY,
	TT_ALL, ATV_NEW_VEHICLE,                0, MONEY,
	TT_ALL, ATV_NEW_VEHICLE,                1, MONEY,
	TT_ALL, ATV_CONSTRUCTION_COST,          0, MONEY,
	TT_ALL, ATV_CONSTRUCTION_COST,          1, MONEY,
	TT_MAX, ATC_INTEREST,                   0, MONEY,
	TT_MAX, ATC_INTEREST,                   1, MONEY,
	TT_ALL, ATV_PROFIT,                     0, MONEY,
	TT_ALL, ATV_PROFIT,                     1, MONEY,
	TT_ALL, ATV_TRANSPORTED,                0, STANDARD,
	TT_ALL, ATV_TRANSPORTED,                1, STANDARD,
	TT_MAX, ATC_CASH,                       0, MONEY,
	TT_ALL, ATV_NON_FINANCIAL_ASSETS,       0, MONEY,
	TT_MAX, ATC_NETWEALTH,                  0, MONEY,
	TT_MAX, ATC_SOFT_CREDIT_LIMIT,          0, MONEY,
	TT_MAX, ATC_HARD_CREDIT_LIMIT,          0, MONEY,
	TT_ALL, ATV_PROFIT_MARGIN,              0, PERCENT
};

static const sint8 cell_to_moneylabel[] =
{
	-1,   0,   1,  -1,  -1,
	-1,   2,   3,  -1,  -1,
	-1,   4,   5,  -1,  -1,
	-1,   6,   7,  -1,  -1,
	-1,   8,   9,  -1,  -1,
	-1,  10,  11,  -1,  22,
	-1,  12,  13,  -1,  23,
	-1,  14,  15,  -1,  24,
	-1,  16,  17,  -1,  25,
	-1,  18,  19,  -1,  26,
	-1,  20,  21,  -1,  27,
};


/// Helper method to query data from players statistics
sint64 money_frame_t::get_statistics_value(int tt, uint8 type, int yearmonth, bool monthly)
{
	const finance_t* finance = player->get_finance();
	if (tt == TT_MAX) {
		return monthly ? finance->get_history_com_month(yearmonth, type)
		               : finance->get_history_com_year( yearmonth, type);
	}
	else {
		assert(0 <= tt  &&  tt < TT_MAX);
		return monthly ? finance->get_history_veh_month((transport_type)tt, yearmonth, type)
		               : finance->get_history_veh_year( (transport_type)tt, yearmonth, type);
	}
}

class money_frame_label_t : public gui_label_buf_t
{
	uint8 transport_type;
	uint8 type;
	uint8 label_type;
	uint8 index;
	bool monthly;

public:
	money_frame_label_t(uint8 tt, uint8 t, uint8 lt, uint8 i, bool mon)
	: gui_label_buf_t(lt == STANDARD ? SYSCOL_TEXT : MONEY_PLUS, lt != MONEY ? gui_label_t::right : gui_label_t::money_right)
	, transport_type(tt), type(t), label_type(lt), index(i), monthly(mon)
	{
	}

	void update(money_frame_t *mf)
	{
		uint8 tt = transport_type == TT_ALL ? mf->transport_type_option : transport_type;
		sint64 value = mf->get_statistics_value(tt, type, index, monthly ? 1 : 0);
		PIXVAL color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;

		switch (label_type) {
			case MONEY:
				buf().append_money(value / 100.0);
				break;
			case PERCENT:
				buf().append(value / 100.0, 2);
				buf().append("%");
				break;
			default:
				buf().append(value * 1.0, 0);
		}
		gui_label_buf_t::update();
		set_color(color);
	}
};


void money_frame_t::fill_chart_tables()
{
	// fill tables for chart curves
	for (int i = 0; i<MAX_PLAYER_COST_BUTTON; i++) {
		const uint8 tt = cost_type[3*i+1] == TT_ALL ? transport_type_option : cost_type[3*i+1];

		for(int j=0; j<MAX_PLAYER_HISTORY_MONTHS; j++) {
			chart_table_month[j][i] = get_statistics_value(tt, cost_type[3*i], j, true);
		}

		for(int j=0; j<MAX_PLAYER_HISTORY_YEARS; j++) {
			chart_table_year[j][i] =  get_statistics_value(tt, cost_type[3*i], j, false);
		}
	}
}


bool money_frame_t::is_chart_table_zero(int ttoption)
{
	if (player->get_finance()->get_maintenance_with_bits((transport_type)ttoption) != 0) {
		return false;
	}
	// search for any non-zero values
	for (int i = 0; i<MAX_PLAYER_COST_BUTTON; i++) {
		const uint8 tt = cost_type[3*i+1] == TT_ALL ? ttoption : cost_type[3*i+1];

		if (tt == TT_MAX  &&  ttoption != TT_ALL) {
			continue;
		}

		for(int j=0; j<MAX_PLAYER_HISTORY_MONTHS; j++) {
			if (get_statistics_value(tt, cost_type[3*i], j, true) != 0) {
				return false;
			}
		}

		for(int j=0; j<MAX_PLAYER_HISTORY_YEARS; j++) {
			if (get_statistics_value(tt, cost_type[3*i], j, false) != 0) {
				return false;
			}
		}
	}
	return true;
}

#define L_VALUE_CELL_WIDTH proportional_string_width("8,888.8km")
void money_frame_t::init_stats()
{
	uint8 active_wt_count = 0;
	for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
		if( depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) ) ) {
			active_wt_count++;
		}
	}
	cont_stats.set_table_layout(1,0);
	cont_stats.new_component<gui_heading_t>("Monthly maintenance cost details", SYSCOL_TEXT, get_titlecolor(), 1);
	cont_stats.add_table(active_wt_count+3,0)->set_spacing( scr_size(D_H_SPACE,1) );
	{
		// 0. header (symbol)
		cont_stats.new_component<gui_margin_t>(10);
		cont_stats.new_component<gui_margin_t>(10);
		// symbol
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.new_component<gui_image_t>()->set_image(skinverwaltung_t::get_waytype_skin( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )->get_image_id(0), true);
			}
		}
		cont_stats.new_component<gui_label_t>("Total");

		//-- buildings
		// 1-2. depots
		bt_access_depotlist.init(button_t::arrowright_state, "");
		bt_access_depotlist.add_listener(this);
		cont_stats.add_component(&bt_access_depotlist);
		cont_stats.new_component<gui_label_t>("Depots")->set_tooltip(translator::translate("Number of depots per way type, and those monthly maintenance costs."));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_depots[i]);
				lb_depots[i].set_align(gui_label_t::right);
			}
		}
		lb_total_depots.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_depots);

		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_empty_t>();
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_depots_maint[i]);
				lb_depots_maint[i].set_align(gui_label_t::right);
			}
		}
		lb_total_depot_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_depot_maint);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE / 2, active_wt_count+3);

		// 3. station counts
		bt_access_haltlist.init(button_t::arrowright_state, "");
		bt_access_haltlist.add_listener(this);
		cont_stats.add_component(&bt_access_haltlist);
		cont_stats.new_component<gui_label_t>("Stops")->set_tooltip(translator::translate("hlptxt_mf_stations"));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_station_counts[i]);
				lb_station_counts[i].set_align(gui_label_t::right);
				lb_station_counts[i].set_min_size(scr_size(L_VALUE_CELL_WIDTH,D_LABEL_HEIGHT));
			}
		}
		lb_total_halts.set_align(gui_label_t::right);
		lb_total_halts.set_min_size(scr_size(L_VALUE_CELL_WIDTH, D_LABEL_HEIGHT));
		cont_stats.add_component(&lb_total_halts);

		cont_stats.new_component_span<gui_empty_t>(active_wt_count+2);
		lb_total_halt_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_halt_maint);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE / 2, active_wt_count+3);

		// 4. signalbox
		bt_access_signalboxlist.init(button_t::arrowright_state, "");
		bt_access_signalboxlist.add_listener(this);
		cont_stats.add_component(&bt_access_signalboxlist);
		cont_stats.new_component<gui_label_t>("Signalboxes")->set_tooltip(translator::translate("Number of signalboxes and total monthly maintenance costs."));
		cont_stats.new_component_span<gui_empty_t>(active_wt_count);
		lb_signalbox_count.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_signalbox_count);

		cont_stats.new_component_span<gui_empty_t>(active_wt_count+2);
		lb_signalbox_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_signalbox_maint);

		// divider
		cont_stats.new_component_span<gui_divider_t>(active_wt_count+3);

		// way and way objects
		// 5-6. way total distance (tiles)
		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_label_t>("way_distances")->set_tooltip(translator::translate("hlptxt_mf_way_distances"));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_way_distances[i]);
				lb_way_distances[i].set_align(gui_label_t::right);
			}
		}
		cont_stats.new_component<gui_empty_t>();

		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_empty_t>();
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_way_maintenances[i]);
				lb_way_maintenances[i].set_align(gui_label_t::right);
			}
		}
		lb_total_way_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_way_maint);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE/2, active_wt_count+3);

		// 4-5. electrification  distance (tiles)
		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_label_t>("Electrified Distances")->set_tooltip(translator::translate("hlptxt_mf_electrified_distances"));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_electrified_distances[i]);
				lb_electrified_distances[i].set_align(gui_label_t::right);
			}
		}
		cont_stats.new_component<gui_empty_t>();

		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_empty_t>();
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_electrification_maint[i]);
				lb_electrification_maint[i].set_align(gui_label_t::right);
			}
		}
		lb_total_electrification_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_electrification_maint);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE/2, active_wt_count+3);

		// 12. signals/signs
		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_label_t>("Signals/signs")->set_tooltip(translator::translate("Number of signals and signs per way type, and those monthly maintenance costs."));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_sign_counts[i]);
				lb_sign_counts[i].set_align(gui_label_t::right);
			}
		}
		lb_own_sign_count.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_own_sign_count);

		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_empty_t>();
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_sign_maint[i]);
				lb_sign_maint[i].set_align(gui_label_t::right);
			}
		}
		lb_total_sign_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_sign_maint);

		// divider
		cont_stats.new_component_span<gui_divider_t>(active_wt_count+3);

		// 8. lines
		bt_access_schedulelist.init(button_t::arrowright_state, "");
		bt_access_schedulelist.add_listener(this);
		cont_stats.add_component(&bt_access_schedulelist);
		cont_stats.new_component<gui_label_t>("Active lines")->set_tooltip(translator::translate("Number of active lines per way type."));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_line_counts[i]);
				lb_line_counts[i].set_align(gui_label_t::right);
			}
		}
		lb_total_active_lines.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_active_lines);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE/2, active_wt_count+3);

		// 9. convoys
		bt_access_convoylist.init(button_t::arrowright_state, "");
		bt_access_convoylist.add_listener(this);
		cont_stats.add_component(&bt_access_convoylist);
		cont_stats.new_component<gui_label_t>("Convois")->set_tooltip(translator::translate("Number of convoys per way type, and inactive convoys number in parentheses."));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				lb_convoy_counts[i].set_align(gui_label_t::right);
				cont_stats.add_component(&lb_convoy_counts[i]);
			}
		}
		lb_own_convoy_count.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_own_convoy_count);
		cont_stats.new_component_span<gui_margin_t>(0, LINESPACE/2, active_wt_count+3);

		// 10-11. vehicles
		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_label_t>("Vehicles")->set_tooltip(translator::translate("Number of vehicles per way type, and those monthly maintenance costs."));
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_vehicle_counts[i]);
				lb_vehicle_counts[i].set_align(gui_label_t::right);
			}
		}
		lb_own_vehicle_count.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_own_vehicle_count);

		cont_stats.new_component<gui_empty_t>();
		cont_stats.new_component<gui_empty_t>();
		for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
			if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
				cont_stats.add_component(&lb_vehicle_maint[i]);
				lb_vehicle_maint[i].set_align(gui_label_t::right);
			}
		}
		lb_total_veh_maint.set_align(gui_label_t::right);
		cont_stats.add_component(&lb_total_veh_maint);
	}
	cont_stats.end_table();
}

money_frame_t::money_frame_t(player_t *player) :
	gui_frame_t( translator::translate("Finanzen"), player),
	maintenance_money(MONEY_PLUS, gui_label_t::money_right),
	scenario_desc(SYSCOL_TEXT_HIGHLIGHT, gui_label_t::left),
	scenario_completion(SYSCOL_TEXT, gui_label_t::left),
	warn(SYSCOL_TEXT_STRONG, gui_label_t::centered),
	scrolly_stats(&cont_stats, true),
	transport_type_option(0)
{
	if(welt->get_player(0)!=player) {
		money_frame_title.printf(translator::translate("Finances of %s"), translator::translate(player->get_name()) );
		set_name(money_frame_title);
	}

	this->player = player;

	set_table_layout(1,0);

	// scenario name
	if(player->get_player_nr()!=1  &&  welt->get_scenario()->active()) {

		add_table(3,1);
		new_component<gui_label_t>("Scenario_", SYSCOL_TEXT_HIGHLIGHT);
		add_component(&scenario_desc);
		add_component(&scenario_completion);
		end_table();
	}

	// select transport type
	gui_aligned_container_t *top = add_table(4,1);
	{
		new_component<gui_label_t>("Show finances for transport type");

		transport_type_c.set_selection(0);
		transport_type_c.set_focusable( false );

		for(int i=0, count=0; i<TT_MAX; ++i) {
			if (!is_chart_table_zero(i)) {
				transport_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(finance_t::get_transport_type_name((transport_type)i)), SYSCOL_TEXT);
				transport_types[ count++ ] = i;
			}
		}

		add_component(&transport_type_c);
		transport_type_c.add_listener( this );

		transport_type_c.set_selection(0);
		top->set_focus( &transport_type_c );
		set_focus(top);

		new_component<gui_fill_t>();

		add_component(&headquarter);
		headquarter.init(button_t::roundbox, "", scr_coord(0,0), D_BUTTON_SIZE);
		headquarter.add_listener(this);
	}
	end_table();

	// tab panels
	// tab (month/year)
	year_month_tabs.add_tab( &container_year, translator::translate("Years"));
	year_month_tabs.add_tab( &container_month, translator::translate("Months"));
	// tab (stats)
	init_stats();
	update_stats(); // Needed to initialize size
	cont_stats.set_size(cont_stats.get_size());
	year_month_tabs.add_tab( &scrolly_stats, translator::translate("player_stats"));

	year_month_tabs.add_listener(this);
	add_component(&year_month_tabs);

	// fill both containers
	gui_aligned_container_t *current = &container_year;
	gui_chart_t *current_chart = &chart;
	// .. put the same buttons in both containers
	button_t* buttons[MAX_PLAYER_COST_BUTTON];

	for(uint8 i = 0; i < 2 ; i++) {
		uint8 k = 0;
		current->set_table_layout(1,0);

		current->add_table(5, FINANCE_TABLE_ROWS+1);

		// first row: some labels
		current->new_component<gui_empty_t>();
		current->new_component<gui_label_t>(i==0 ? "This Year" : "This Month", SYSCOL_TEXT_HIGHLIGHT);
		current->new_component<gui_label_t>(i==0 ? "Last Year" : "Last Month", SYSCOL_TEXT_HIGHLIGHT);
		current->new_component<gui_empty_t>();
		current->new_component<gui_empty_t>();

		// all other rows: mix of buttons and money-labels
		for(uint8 r = 0; r < FINANCE_TABLE_ROWS; r++) {
			for(uint8 c = 0; c < 5; c++, k++) {
				sint8 cost = cell_to_buttons[k];
				sint8 l = cell_to_moneylabel[k];
				// button + chart line
				if (cost >=0 ) {
					// add chart line
					const int curve_type = cost_type[3*cost+2];
					const int curve_precision = curve_type == STANDARD ? 0 : 2;
					sint16 curve = i == 0
					? chart.add_curve(  color_idx_to_rgb(cost_type_color[cost]), *chart_table_year,  MAX_PLAYER_COST_BUTTON, cost, MAX_PLAYER_HISTORY_YEARS,  curve_type, false, true, curve_precision)
					: mchart.add_curve( color_idx_to_rgb(cost_type_color[cost]), *chart_table_month, MAX_PLAYER_COST_BUTTON, cost, MAX_PLAYER_HISTORY_MONTHS, curve_type, false, true, curve_precision);
					// add button
					button_t *b;
					if (i == 0) {
						b = current->new_component<button_t>();
						b->init(button_t::box_state_automatic | button_t::flexible, cost_type_name[cost]);
						b->set_tooltip(cost_tooltip[cost]);
						b->background_color = color_idx_to_rgb(cost_type_color[cost]);
						b->pressed = false;
						buttons[cost] = b;
					}
					else {
						b = buttons[cost];
						current->add_component(b);
					}
					button_to_chart.append(b, current_chart, curve);
				}
				else if (l >= 0) {
					// money_frame_label_t(uint8 tt, uint8 t, uint8 lt, uint8 i, bool mon)
					money_labels.append( current->new_component<money_frame_label_t>(label_type[4*l], label_type[4*l+1], label_type[4*l+3], label_type[4*l+2], i==1) );
				}
				else {
					if (r >= 2  &&  r<=4  &&  c == 4) {
						switch(r) {
							case 2: current->new_component<gui_label_t>("This Month", SYSCOL_TEXT_HIGHLIGHT); break;
							case 3: current->add_component(&maintenance_money); break;
							case 4: current->new_component<gui_label_t>("This Year", SYSCOL_TEXT_HIGHLIGHT);
						}
					}
					else {
						current->new_component<gui_empty_t>();
					}
				}
			}
		}
		current->end_table();

		// bankruptcy notice
		current->add_component(&warn);
		warn.set_visible(false);

		current->new_component<gui_margin_t>(LINESPACE/2);

		current->add_component(current_chart);
		current = &container_month;
		current_chart = &mchart;
	}

	// recover button states
	if (bFilterStates.get_count() > 0) {
		for(uint8 i = 0; i<bFilterStates.get_count(); i++) {
			button_to_chart[i]->get_button()->pressed = bFilterStates[i] > 0;
			button_to_chart[i]->update();
		}
	}

	chart.set_min_size(scr_size(0 ,8*BUTTONSPACE));
	chart.set_dimension(MAX_PLAYER_HISTORY_YEARS, 10000);
	chart.set_seed(welt->get_last_year());
	chart.set_background(SYSCOL_CHART_BACKGROUND);

	mchart.set_min_size(scr_size(0,8*BUTTONSPACE));
	mchart.set_dimension(MAX_PLAYER_HISTORY_MONTHS, 10000);
	mchart.set_seed(0);
	mchart.set_background(SYSCOL_CHART_BACKGROUND);

	update_labels();

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(diagonal_resize);
	resize(scr_size(0,0));
}


money_frame_t::~money_frame_t()
{
	bFilterStates.clear();
	// save button states
	FOR(vector_tpl<gui_button_to_chart_t*>, b2c, button_to_chart.list()) {
		bFilterStates.append( b2c->get_button()->pressed ? 1 : 0);
	}
}

void money_frame_t::update_labels()
{
	FOR(vector_tpl<money_frame_label_t*>, lb, money_labels) {
		lb->update(this);
	}

	// scenario
	if(player->get_player_nr()!=1  &&  welt->get_scenario()->active()) {
		welt->get_scenario()->update_scenario_texts();
		scenario_desc.buf().append( welt->get_scenario()->description_text );
		scenario_desc.buf().append( ":" );
		scenario_desc.update();

		sint32 percent = welt->get_scenario()->get_completion(player->get_player_nr());
		if (percent >= 0) {
			scenario_completion.buf().printf(translator::translate("Scenario complete: %i%%"), percent );
		}
		else {
			scenario_completion.buf().printf(translator::translate("Scenario lost!"));
		}
		scenario_completion.update();
	}

	// update headquarter button
	headquarter.disable();
	/*if(  player->get_ai_id()!=player_t::HUMAN  ) {
		headquarter.set_tooltip( "Configure AI setttings" );
		headquarter.set_text( "Configure AI" );
		headquarter.enable();
	}
	else {*/
	if (player->get_ai_id() == player_t::HUMAN) {
		koord pos = player->get_headquarter_pos();
		headquarter.set_text( pos != koord::invalid ? "show HQ" : "build HQ" );
		headquarter.set_tooltip( NULL );

		if (pos == koord::invalid) {
			if(player == welt->get_active_player()) {
				// reuse tooltip from tool_headquarter_t
				const char * c = tool_t::general_tool[TOOL_HEADQUARTER]->get_tooltip(player);
				if(c) {
					// only true, if the headquarter can be built/updated
					headquarter_tooltip.clear();
					headquarter_tooltip.append(c);
					headquarter.set_tooltip( headquarter_tooltip );
					headquarter.enable();
				}

			}
		}
		else {
			headquarter.enable();
		}
	}

	// current maintenance
	double maintenance = player->get_finance()->get_maintenance_with_bits((transport_type)transport_type_option) / 100.0;
	maintenance_money.append_money(-maintenance);
	maintenance_money.update();

	// warning/success messages
	bool visible = warn.is_visible();

	if (player->check_solvency() == player_t::in_administration)
	{
		warn.set_color(COL_DARK_BLUE);
		warn.buf().append(translator::translate("in_administration"));
		warn.set_visible(true);
	}
	else if (player->check_solvency() == player_t::in_liquidation)
	{
		warn.set_color(COL_DARK_RED);
		warn.buf().append(translator::translate("in_liquidation"));
		warn.set_visible(true);
	}
	else if (player->get_finance()->get_history_com_month(0, ATC_CASH) < player->get_finance()->get_history_com_month(0, ATC_SOFT_CREDIT_LIMIT))
	{
		warn.set_color(MONEY_MINUS);
		warn.buf().append(translator::translate("Credit limit exceeded"));
		warn.set_visible(true);
	}
	else if (player->get_finance()->get_history_com_year(0, ATC_NETWEALTH) * 10 < welt->get_settings().get_starting_money(welt->get_current_month() / 12))
	{
		warn.set_color(MONEY_MINUS);
		warn.buf().append( translator::translate("Net wealth near zero") );
		warn.set_visible(true);
	}
	else if (player->get_account_overdrawn())
	{
		warn.set_color(COL_YELLOW);
		warn.buf().printf(translator::translate("On loan since %i month(s)"), player->get_account_overdrawn() );
		warn.set_visible(true);
	}
	else {
		warn.set_visible(false);
	}
	warn.update();
	if (visible != warn.is_visible()) {
		resize(scr_size(0,0));
	}
}

void money_frame_t::update_stats()
{
	// reset data
	for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
		tt_halt_counts[i] = 0;
		tt_way_length[i] = 0;
		tt_way_maint[i] = 0;
		tt_electrified_len[i] = 0;
		tt_electrification_maint[i] = 0;
		tt_convoy_counts[i] = 0;
		tt_inactive_convoy_counts[i] = 0;
		tt_vehicle_counts[i] = 0;
		tt_vehicle_maint[i] = 0;
		tt_depot_counts[i] = 0;
		tt_depot_maint[i] = 0;
		tt_sign_counts[i] = 0;
		tt_sign_maint[i] = 0;
	}
	/*
	memset(tt_halt_counts,            0, sizeof(tt_halt_counts));
	memset(tt_way_length,             0, sizeof(tt_way_length));
	memset(tt_way_maint,              0, sizeof(tt_way_maint));
	memset(tt_electrified_len,        0, sizeof(tt_electrified_len));
	memset(tt_electrification_maint,  0, sizeof(tt_electrification_maint));
	memset(tt_convoy_counts,          0, sizeof(tt_convoy_counts));
	memset(tt_inactive_convoy_counts, 0, sizeof(tt_inactive_convoy_counts));
	memset(tt_vehicle_counts,         0, sizeof(tt_vehicle_counts));
	memset(tt_vehicle_maint,          0, sizeof(tt_vehicle_maint));
	memset(tt_depot_counts,           0, sizeof(tt_depot_counts));
	memset(tt_depot_maint,            0, sizeof(tt_depot_maint));
	memset(tt_sign_counts,            0, sizeof(tt_sign_counts));
	memset(tt_sign_maint,             0, sizeof(tt_sign_maint));
	*/
	// Collect data
	uint16 total_halts=0;
	sint64 total_halt_maint=0;
	sint64 total_way_maintenance = 0;
	sint64 total_electrification_maintenance = 0;
	uint16 total_own_convoys = 0;
	uint32 total_own_vehicles = 0;
	uint32 total_depots = 0;
	sint64 total_depot_maintenance=0;
	uint32 total_vehicle_maintenance = 0;
	uint32 total_sign_maintenance = 0;
	uint16 total_own_signalboxes = 0;
	uint64 total_signalbox_maintenance = 0;

	// - stations
	FOR(vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen()) {
		if (halt->get_owner() == player) {
			for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
				if (finance_t::translate_tt_to_waytype((transport_type)(i+1)) ==road_wt) {
					if (halt->get_station_type() & haltestelle_t::busstop || halt->get_station_type() & haltestelle_t::loadingbay) {
						tt_halt_counts[i]++;
					}
				}
				else if (halt->get_station_type() & simline_t::linetype_to_stationtype[simline_t::waytype_to_linetype( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )]) {
					tt_halt_counts[i]++;
				}
			}
			total_halts++;
			total_halt_maint += halt->calc_maintenance();
		}
	}
	// - depot & vehicle
	FOR(slist_tpl<depot_t*>, const depot, depot_t::get_depot_list()) {
		if (depot->get_player_nr() == player->get_player_nr()) {
			const uint8 tt_idx = finance_t::translate_waytype_to_tt(depot->get_waytype())-1;
			tt_depot_counts[tt_idx]++;
			total_depot_maintenance += welt->get_settings().maint_building * depot->get_tile()->get_desc()->get_level();
			tt_depot_maint[tt_idx] += welt->get_settings().maint_building * depot->get_tile()->get_desc()->get_level();
			total_depots++;
			// all vehicles stored in depot not part of a convoi
			tt_vehicle_counts[tt_idx] += depot->get_vehicle_list().get_count();
			total_own_vehicles += depot->get_vehicle_list().get_count();
			FOR(slist_tpl<vehicle_t*>, vehicle, depot->get_vehicle_list()) {
				total_vehicle_maintenance += vehicle->get_fixed_cost(welt);
				tt_vehicle_maint[tt_idx] += vehicle->get_fixed_cost(welt);
			}
		}
	}
	// - convoys
	FOR(vector_tpl<convoihandle_t>, const cnv, welt->convoys()) {
		if (cnv->get_owner() == player) {
			const uint8 tt_idx = finance_t::translate_waytype_to_tt(cnv->front()->get_desc()->get_waytype())-1;
			tt_convoy_counts[tt_idx]++;
			if (cnv->in_depot()) {
				tt_inactive_convoy_counts[tt_idx]++;
			}
			tt_vehicle_counts[tt_idx] += cnv->get_vehicle_count();
			total_own_vehicles += cnv->get_vehicle_count();
			total_own_convoys++;
			tt_vehicle_maint[tt_idx] += cnv->get_fixed_cost();
			total_vehicle_maintenance+=cnv->get_fixed_cost();
		}
	}
	// - signalboxes
	FOR(slist_tpl<signalbox_t*>, const sigb, signalbox_t::all_signalboxes) {
		if (sigb->get_owner() == player && sigb->get_first_tile() == sigb) {
			total_signalbox_maintenance += (uint32)welt->lookup(sigb->get_pos())->get_building()->get_tile()->get_desc()->get_maintenance();
			total_own_signalboxes++;
		}
	}
	// - way & electrification
	FOR(vector_tpl<weg_t*>, const way, weg_t::get_alle_wege()) {
		const uint8 tt_idx = finance_t::translate_waytype_to_tt(way->get_desc()->get_finance_waytype())-1;
		if (tt_idx >= TT_MAX_VEH-1) {
			continue;
		}
		const grund_t* gr = welt->lookup(way->get_pos());
		if (way->get_owner()==player) {
			sint32 maint = way->get_desc()->get_maintenance();

			if( gr->ist_bruecke() ) {
				if( bruecke_t* bridge = gr->find<bruecke_t>(1) ) {
					maint += bridge->get_desc()->get_maintenance();
				}
			}
			if( gr->ist_tunnel() ) {
				if( tunnel_t* tunnel = gr->find<tunnel_t>(1) ) {
					maint += tunnel->get_desc()->get_maintenance();
				}
			}

			if(way->is_diagonal()){
				maint *= 10;
				maint /= 14;
				tt_way_length[tt_idx]+=7;
			}
			else {
				tt_way_length[tt_idx]+=10;
			}
			total_way_maintenance += maint;
			tt_way_maint[tt_idx]  += maint;
		}
		// way and electrification object owners can be different
		if (way->is_electrified()) {
			const wayobj_t *wo = gr->get_wayobj(way->get_waytype());
			if (wo->get_owner() == player) {
				sint32 wo_maint = wo->get_desc()->get_maintenance();
				if (way->is_diagonal()) {
					tt_electrified_len[tt_idx] += 7;
					wo_maint *= 10;
					wo_maint /= 14;
				}
				else {
					tt_electrified_len[tt_idx] += 10;
				}
				tt_electrification_maint[tt_idx] += wo_maint;
				total_electrification_maintenance += wo_maint;
			}
		}
		if (way->has_sign()) {
			const roadsign_t* rs = gr->find<roadsign_t>();
			if (rs->get_owner()==player) {
				tt_sign_counts[tt_idx]++;
				tt_sign_maint[tt_idx]+= rs->get_desc()->get_maintenance();
			}
		}
		if (way->has_signal()) {
			const signal_t* sig = gr->find<signal_t>();
			if (sig->get_owner() == player) {
				tt_sign_counts[tt_idx]++;
				tt_sign_maint[tt_idx] += sig->get_desc()->get_maintenance();
			}
		}
	}


	// Update table(labels)
	uint32 active_lines = 0;
	for (uint8 i = 0; i < TT_MAX_VEH-1; i++) {
		if (depotlist_frame_t::is_available_wt( finance_t::translate_tt_to_waytype((transport_type)(i+1)) )) {
			// depots
			if (tt_depot_counts[i]) {
				lb_depots[i].buf().printf("%u", tt_depot_counts[i]);
				lb_depots_maint[i].buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(tt_depot_maint[i]) / 100.0);
			}
			else {
				lb_depots[i].buf().append("-");
			}
			lb_depots_maint[i].update();
			lb_depots[i].update();

			// halt
			if (tt_halt_counts[i]) {
				lb_station_counts[i].buf().printf("%u", tt_halt_counts[i]);
			}
			else {
				lb_station_counts[i].buf().append("-");
			}
			lb_station_counts[i].update();

			// way
			if (tt_way_length[i]) {
				lb_way_distances[i].buf().printf("%.1fkm", (double)(tt_way_length[i] * welt->get_settings().get_meters_per_tile()/10000.0));
				lb_way_maintenances[i].buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(tt_way_maint[i]) / 100.0);
			}
			else {
				lb_way_distances[i].buf().append("-");
			}
			lb_way_distances[i].update();
			lb_way_maintenances[i].update();
			// electrification
			if (tt_electrified_len[i]) {
				lb_electrified_distances[i].buf().printf("%.1fkm", (double)(tt_electrified_len[i] * welt->get_settings().get_meters_per_tile() / 10000.0));
				lb_electrification_maint[i].buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(tt_electrification_maint[i]) / 100.0);
			}
			else {
				lb_electrified_distances[i].buf().append("-");
			}
			lb_electrified_distances[i].update();
			lb_electrification_maint[i].update();

			// lines
			vector_tpl<linehandle_t> lines;
			player->simlinemgmt.get_lines(simline_t::waytype_to_linetype( finance_t::translate_tt_to_waytype((transport_type)(i+1)) ), &lines, 0, false);
			if (lines.get_count()) {
				lb_line_counts[i].buf().append(lines.get_count());
				active_lines+=lines.get_count();
			}
			else {
				lb_line_counts[i].buf().append("-");
			}
			lb_line_counts[i].update();

			// convoys
			if (tt_convoy_counts[i]) {
				lb_convoy_counts[i].buf().printf("%u", tt_convoy_counts[i]);
				if (tt_inactive_convoy_counts[i]) {
					lb_convoy_counts[i].buf().printf("(%u)", tt_inactive_convoy_counts[i]);
				}
			}
			else {
				lb_convoy_counts[i].buf().append("-");
			}
			lb_convoy_counts[i].update();

			// vehicles
			if (tt_vehicle_counts[i]) {
				lb_vehicle_counts[i].buf().printf("%u", tt_vehicle_counts[i]);
				lb_vehicle_maint[i].buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(tt_vehicle_maint[i]) / 100.0);
			}
			else {
				lb_vehicle_counts[i].buf().append("-");
			}
			lb_vehicle_maint[i].update();
			lb_vehicle_counts[i].update();

			// signs
			if (tt_sign_counts[i]) {
				lb_sign_counts[i].buf().printf("%u", tt_sign_counts[i]);
				lb_sign_maint[i].buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(tt_sign_maint[i]) / 100.0);
				total_sign_maintenance += tt_sign_maint[i];
			}
			else {
				lb_sign_counts[i].buf().append("-");
			}
			lb_sign_maint[i].update();
			lb_sign_counts[i].update();
		}
	}
	// update total values
	lb_total_halts.buf().append(total_halts,0);
	lb_total_halts.update();
	lb_total_halt_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_halt_maint) / 100.0);
	lb_total_halt_maint.update();

	lb_total_way_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_way_maintenance) / 100.0);
	lb_total_way_maint.update();

	lb_total_electrification_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_electrification_maintenance) / 100.0);
	lb_total_electrification_maint.update();

	lb_total_depots.buf().append(total_depots, 0);
	lb_total_depots.update();
	lb_total_depot_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_depot_maintenance) / 100.0);
	lb_total_depot_maint.update();

	lb_total_active_lines.buf().append(active_lines, 0);
	lb_total_active_lines.update();

	lb_own_convoy_count.buf().append(total_own_convoys, 0);
	lb_own_convoy_count.update();

	lb_own_vehicle_count.buf().append(total_own_vehicles, 0);
	lb_own_vehicle_count.update();

	lb_total_veh_maint.buf().printf("%.2f$", welt->calc_adjusted_monthly_figure(total_vehicle_maintenance) / 100.0);
	lb_total_veh_maint.update();

	lb_total_sign_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_sign_maintenance) / 100.0);
	lb_total_sign_maint.update();

	lb_signalbox_count.buf().append(total_own_signalboxes, 0);
	lb_signalbox_count.update();
	lb_signalbox_maint.buf().printf("%.2f$", (double)welt->calc_adjusted_monthly_figure(total_signalbox_maintenance) / 100.0);
	lb_signalbox_maint.update();
}

void money_frame_t::set_windowsize(scr_size size)
{
	gui_frame_t::set_windowsize(size);
	// recompute the active container
	if (year_month_tabs.get_active_tab_index() == 0) {
		container_year.set_size( container_year.get_size() );
	}
	else {
		container_month.set_size( container_month.get_size() );
	}
}


void money_frame_t::draw(scr_coord pos, scr_size size)
{

	player->get_finance()->calc_finance_history();
	fill_chart_tables();
	update_labels();

	// update chart seed
	chart.set_seed(welt->get_last_year());

	if (year_month_tabs.get_active_tab_index() == 2) {
		update_stats();
	}

	gui_frame_t::draw(pos, size);
}


bool money_frame_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	if(  comp == &headquarter  ) {
		if(  player->get_ai_id()!=player_t::HUMAN  ) {
			create_win( new ai_option_t(player), w_info, magic_ai_options_t+player->get_player_nr() );
		}
		else {
			if (player->get_headquarter_pos() == koord::invalid) {
				welt->set_tool( tool_t::general_tool[TOOL_HEADQUARTER], player );
			}
			else {
				// open dedicated HQ window
				create_win( new headquarter_info_t(player), w_info, magic_headquarter+player->get_player_nr() );
			}
		}
		return true;
	}
	if (comp == &year_month_tabs) {
		if (year_month_tabs.get_active_tab_index() == 0) {
			container_year.set_size( container_year.get_size() );
		}
		else {
			container_month.set_size( container_month.get_size() );
		}
	}
	if(  comp == &transport_type_c) {
		int tmp = transport_type_c.get_selection();
		if((0 <= tmp) && (tmp < transport_type_c.count_elements())) {
			transport_type_option = transport_types[tmp];
		}
		return true;
	}
	if (  comp == &bt_access_haltlist  ) {
		create_win( new halt_list_frame_t(), w_info, magic_halt_list );
		return true;
	}
	if (  comp == &bt_access_depotlist  ) {
		create_win( new depotlist_frame_t(player), w_info, magic_depotlist + player->get_player_nr() );
		return true;
	}
	if (  comp == &bt_access_schedulelist  ) {
		create_win( new schedule_list_gui_t(player), w_info, magic_line_management_t + player->get_player_nr() );
		return true;
	}
	if (  comp == &bt_access_convoylist  ) {
		create_win( new convoi_frame_t(), w_info, magic_convoi_list + player->get_player_nr() );
		return true;
	}
	if (  comp == &bt_access_signalboxlist) {
		create_win( new signalboxlist_frame_t(player), w_info, magic_signalboxlist + player->get_player_nr() );
		return true;
	}
	return false;
}


bool money_frame_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_frame_t::infowin_event(ev);
	set_focus( &transport_type_c );
	return swallowed;
}


uint32 money_frame_t::get_rdwr_id()
{
	return magic_finances_t+player->get_player_nr();
}


void money_frame_t::rdwr( loadsave_t *file )
{
	// button-to-chart array
	button_to_chart.rdwr(file);

	year_month_tabs.rdwr(file);

	file->rdwr_short(transport_type_option);
	if (file->is_loading()) {
		for(int i=0; i<transport_type_c.count_elements(); i++) {
			if (transport_types[i] == transport_type_option) {
				transport_type_c.set_selection(i);
				break;
			}
		}
	}
}
