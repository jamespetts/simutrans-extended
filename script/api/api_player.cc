/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "api.h"

/** @file api_player.cc exports player/company related functions. */

#include "../api_class.h"
#include "../api_function.h"
#include "../../player/simplay.h"
#include "../../player/finance.h"

// for creation of lines
#include "../../simline.h"
#include "../../simmenu.h"
#include "../../simworld.h"


using namespace script_api;

/**
 * Whether a value of accounting_type_common is a money amount.
 * ATC_HALTS is a count and ATC_SCENARIO_COMPLETED is a percentage.
 */
static bool is_money_common(sint32 index)
{
	switch (index) {
		case ATC_HALTS:
		case ATC_SCENARIO_COMPLETED:
			return false;
		default:
			return true;
	}
}

/**
 * Whether a value of accounting_type_vehicles is a money amount.
 * The remaining entries are counts, distances, quantities and percentages.
 */
static bool is_money_vehicles(sint32 index)
{
	switch (index) {
		case ATV_PROFIT_MARGIN:
		case ATV_TRANSPORTED_PASSENGER:
		case ATV_TRANSPORTED_MAIL:
		case ATV_TRANSPORTED_GOOD:
		case ATV_CONVOY_DISTANCE:
		case ATV_VEHICLE_DISTANCE:
		case ATV_CONVOIS:
		case ATV_VEHICLES:
		case ATV_WAY_LENGTH:
			return false;
		default:
			return true;
	}
}

/**
 * Reads one finance history series.
 * @param common read the whole-company array (indexed by accounting_type_common)
 *               instead of a per-transport-type array (indexed by accounting_type_vehicles)
 * @param index the accounting type
 * @param tt the transport type; ignored when @p common is true
 * @param monthly true for the monthly series, false for the yearly series
 */
static vector_tpl<sint64> const& get_player_history_series(player_t *player, bool common, sint32 index, sint32 tt, bool monthly)
{
	static vector_tpl<sint64> v;
	v.clear();
	if (player == NULL) {
		return v;
	}
	if (common) {
		if (index < 0  ||  index >= ATC_MAX) {
			return v;
		}
	}
	else if (index < 0  ||  index >= ATV_MAX  ||  tt < 0  ||  tt >= TT_MAX) {
		return v;
	}

	finance_t *finance = player->get_finance();
	// calculate current month
	finance->calc_finance_history();

	const uint16 maxi = monthly ? MAX_PLAYER_HISTORY_MONTHS : MAX_PLAYER_HISTORY_YEARS;
	for (uint16 i = 0; i < maxi; i++) {
		sint64 m;
		if (common) {
			m = monthly ? finance->get_history_com_month(i, index) : finance->get_history_com_year(i, index);
			if (is_money_common(index)) {
				m = convert_money(m);
			}
		}
		else {
			m = monthly ? finance->get_history_veh_month((transport_type)tt, i, index)
			            : finance->get_history_veh_year((transport_type)tt, i, index);
			if (is_money_vehicles(index)) {
				m = convert_money(m);
			}
		}
		v.append(m);
	}
	return v;
}

vector_tpl<sint64> const& get_player_stat(player_t *player, sint32 INDEX, sint32 TTYPE, bool monthly)
{
	const bool common = (TTYPE < 0  ||  TTYPE >= TT_MAX);
	return get_player_history_series(player, common, INDEX, common ? 0 : TTYPE, monthly);
}

vector_tpl<sint64> const& get_player_common_history(player_t *player, sint32 index, bool monthly)
{
	return get_player_history_series(player, true, index, 0, monthly);
}

vector_tpl<sint64> const& get_player_vehicle_history(player_t *player, sint32 index, sint32 tt, bool monthly)
{
	return get_player_history_series(player, false, index, tt, monthly);
}

vector_tpl<sint64> const& get_player_profit_after_depreciation_and_tax(player_t *player, bool monthly)
{
	static vector_tpl<sint64> v;
	v.clear();
	if (player == NULL) {
		return v;
	}

	finance_t *finance = player->get_finance();
	// calculate current month
	finance->calc_finance_history();

	const uint16 maxi = monthly ? MAX_PLAYER_HISTORY_MONTHS : MAX_PLAYER_HISTORY_YEARS;
	for (uint16 i = 0; i < maxi; i++) {
		const sint64 operating_profit = monthly ? finance->get_history_veh_month(TT_ALL, i, ATV_OPERATING_PROFIT)
		                                       : finance->get_history_veh_year(TT_ALL, i, ATV_OPERATING_PROFIT);
		const sint64 depreciation = monthly ? finance->get_history_com_month(i, ATC_DEPRECIATION)
		                                   : finance->get_history_com_year(i, ATC_DEPRECIATION);
		const sint64 interest = monthly ? finance->get_history_com_month(i, ATC_INTEREST)
		                               : finance->get_history_com_year(i, ATC_INTEREST);
		const sint64 tax = monthly ? finance->get_history_com_month(i, ATC_TAX)
		                          : finance->get_history_com_year(i, ATC_TAX);
		v.append(convert_money(operating_profit + depreciation + interest + tax));
	}
	return v;
}


// export of finance_t only here
namespace script_api {
	declare_specialized_param(finance_t*, param<player_t*>::typemask(), param<player_t*>::squirrel_type());

	finance_t* param<finance_t*>::get(HSQUIRRELVM vm, SQInteger index)
	{
		player_t *player = param<player_t*>::get(vm, index);
		return player ? player->get_finance() : NULL;
	}
};
// also export transport_type - to get correct parameters
namespace script_api {
	declare_specialized_param(transport_type, "i", "integer");

	transport_type param<transport_type>::get(HSQUIRRELVM vm, SQInteger index)
	{
		return (transport_type) min(param<uint16>::get(vm, index), TT_MAX-1);
	}
};


SQInteger player_export_line_list(HSQUIRRELVM vm)
{
	if (player_t* player = param<player_t*>::get(vm, 1)) {
		push_instance(vm, "line_list_x");
		set_slot(vm, "player_id", player->get_player_nr());
		return 1;
	}
	return SQ_ERROR;
}

call_tool_init player_create_line(player_t *player, waytype_t wt)
{
	simline_t::linetype lt = simline_t::waytype_to_linetype(wt);
	if (lt == simline_t::MAX_LINE_TYPE) {
		return "Invalid waytype provided";
	}
	// build param string (see schedule_list_gui_t::action_triggered)
	// The schedule string carries an entry count in the type field and a "33"
	// entry terminator; this is an empty schedule.
	cbuffer_t buf;
	buf.printf( "c,0,%i,0,0|%i,0|33", lt, lt);
	return call_tool_init(TOOL_CHANGE_LINE | SIMPLE_TOOL, buf, 0, player);
}

call_tool_init player_book_account(player_t *player, sint32 delta)
{
	// build param string (see tool_change_player_t)
	cbuffer_t buf;
	buf.printf( "$,%i,%i", player->get_player_nr(), delta);
	return call_tool_init(TOOL_CHANGE_PLAYER | SIMPLE_TOOL, buf, 0, welt->get_public_player());
}

SQInteger player_get_my_player(HSQUIRRELVM vm)
{
	return script_api::param<player_t*>::push(vm, get_my_player(vm) );
}

call_tool_init player_allow_access(player_t *player, player_t *recipient, bool allow)
{
	if (!player || !recipient) {
		return "Cannot change access for a missing player";
	}
	cbuffer_t buf;
	buf.printf("g%u,%u,%u", (unsigned int)player->get_player_nr(), (unsigned int)recipient->get_player_nr(), allow ? 1u : 0u);
	return call_tool_init(TOOL_ACCESS_TOOL | SIMPLE_TOOL, buf, 0, player);
}


call_tool_init player_set_name(player_t *player, const char* name)
{
	return script_api::command_rename(player, 'p', player->get_player_nr(), name);
}

void export_player(HSQUIRRELVM vm, bool scenario)
{
	/**
	 * Class to access player statistics.
	 * Here, a player refers to one transport company, not to an individual playing simutrans.
	 */
	begin_class(vm, "player_x", "extend_get,ingame_object");

	/**
	 * Constructor.
	 * @param nr player number, 0 = standard player, 1 = public player
	 * @typemask (integer)
	 */
	// actually defined in simutrans/script/script_base.nut
	// register_function(..., "constructor", ...);

	/**
	 * @returns if object is still valid.
	 */
	export_is_valid<player_t*>(vm); //register_function("is_valid")

	if (!scenario) {
		/**
		 * @returns player associated with the AI.
		 * @ingroup ai_only
		 * @typemask player_x()
		 * @note Only available in AI mode.
		 */
		STATIC register_function(vm, &player_get_my_player, "self", 0, "", true);
	}
	/**
	 * Return headquarters level.
	 * @returns level, level is zero if no headquarters was built
	 */
	register_method(vm, &player_t::get_headquarters_level, "get_headquarter_level");
	/**
	 * Return headquarters position.
	 * @returns coordinate, (-1,-1) if no headquarters was built
	 */
	register_method(vm, &player_t::get_headquarter_pos,   "get_headquarter_pos");
	/**
	 * Return name of company.
	 * @returns name
	 */
	register_method(vm, &player_t::get_name,              "get_name");
	/**
	 * Sets name of company.
	 * @param name the new name
	 * @ingroup rename_func
	 */
	register_method(vm, &player_set_name, "set_name", true);
	/** Allow or withdraw this player's ways for another player. */
	register_method(vm, &player_allow_access, "set_allow_access_to", true);
	/** Whether this player permits the supplied player on its ways. */
	register_method(vm, &player_t::allows_access_to, "allows_access_to");
	/**
	 * Get monthly statistics of construction costs.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_construction",      freevariable<sint32,sint32,bool>(ATV_CONSTRUCTION_COST, TT_ALL, true), true);
	/**
	 * Get monthly statistics of the distance-based vehicle running cost.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_running_costs",     freevariable<sint32,sint32,bool>(ATV_RUNNING_COST, TT_ALL, true), true);
	/**
	 * Get monthly statistics of the distance-based vehicle running cost.
	 * @returns array, index [0] corresponds to current month, in credits
	 * @note The name is historical. This series is ATV_RUNNING_COST, not the
	 *       monthly vehicle maintenance charge; use get_vehicle_maintenance
	 *       for that series.
	 * @deprecated Prefer get_running_costs.
	 */
	register_method_fv(vm, &get_player_stat, "get_vehicle_maint",     freevariable<sint32,sint32,bool>(ATV_RUNNING_COST, TT_ALL, true), true);
	/**
	 * Get monthly statistics of the monthly vehicle maintenance charge,
	 * including routine maintenance and overhauls.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_vehicle_maintenance", freevariable<sint32,sint32,bool>(ATV_VEHICLE_MAINTENANCE, TT_ALL, true), true);
	/**
	 * Get monthly statistics of costs for vehicle purchase.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_new_vehicles",      freevariable<sint32,sint32,bool>(ATV_NEW_VEHICLE, TT_ALL, true), true);
	/**
	 * Get monthly statistics of income.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_income",            freevariable<sint32,sint32,bool>(ATV_REVENUE_TRANSPORT, TT_ALL, true), true);
	/**
	 * Get monthly statistics of infrastructure maintenance.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_maintenance",       freevariable<sint32,sint32,bool>(ATV_INFRASTRUCTURE_MAINTENANCE, TT_ALL, true), true);
	/**
	 * Get monthly statistics of assets.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_assets",            freevariable<sint32,sint32,bool>(ATV_NON_FINANCIAL_ASSETS, TT_ALL, true), true);
	/**
	 * Get monthly statistics of cash.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_cash",              freevariable<sint32,sint32,bool>(ATC_CASH, -1, true), true);
	/**
	 * Get monthly statistics of net wealth.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_net_wealth",        freevariable<sint32,sint32,bool>(ATC_NETWEALTH, -1, true), true);
	/**
	 * Get monthly statistics of profit. This is the cash-flow profit: it
	 * deducts construction and vehicle purchase, and includes interest, but
	 * deducts neither depreciation nor tax.
	 * @returns array, index [0] corresponds to current month, in credits
	 * @see get_profit_after_depreciation_and_tax
	 */
	register_method_fv(vm, &get_player_stat, "get_profit",            freevariable<sint32,sint32,bool>(ATV_PROFIT, TT_ALL, true), true);
	/**
	 * Get monthly statistics of operating profit.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_operating_profit",  freevariable<sint32,sint32,bool>(ATV_OPERATING_PROFIT, TT_ALL, true), true);
	/**
	 * Get monthly statistics of the operating profit margin.
	 * @returns array, index [0] corresponds to current month, in hundredths
	 *          of a percent
	 */
	register_method_fv(vm, &get_player_stat, "get_margin",            freevariable<sint32,sint32,bool>(ATV_PROFIT_MARGIN, TT_ALL, true), true);
	/**
	 * Get monthly statistics of income from powerlines.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_powerline",         freevariable<sint32,sint32,bool>(ATV_REVENUE, TT_POWERLINE, true), true);
	/**
	 * Get monthly statistics of transported passengers.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_player_stat, "get_transported_pax",   freevariable<sint32,sint32,bool>(ATV_TRANSPORTED_PASSENGER, TT_ALL, true), true);
	/**
	 * Get monthly statistics of transported mail.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_player_stat, "get_transported_mail",  freevariable<sint32,sint32,bool>(ATV_TRANSPORTED_MAIL, TT_ALL, true), true);
	/**
	 * Get monthly statistics of transported goods.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_player_stat, "get_transported_goods", freevariable<sint32,sint32,bool>(ATV_TRANSPORTED_GOOD, TT_ALL, true), true);
	/**
	 * Get monthly statistics of number of convoys.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_player_stat, "get_convoys",           freevariable<sint32,sint32,bool>(ATV_CONVOIS, TT_ALL, true), true);
	/**
	 * Get monthly statistics of income/loss due to way tolls.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_way_tolls",         freevariable<sint32,sint32,bool>(ATV_WAY_TOLL, TT_ALL, true), true);
	/**
	 * Get monthly statistics of interest received and paid.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_interest",          freevariable<sint32,sint32,bool>(ATC_INTEREST, -1, true), true);
	/**
	 * Get monthly statistics of corporation tax paid.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_tax",               freevariable<sint32,sint32,bool>(ATC_TAX, -1, true), true);
	/**
	 * Get monthly statistics of the pooled depreciation charge. Depreciation
	 * has no cash effect and is not included in get_profit.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_depreciation",      freevariable<sint32,sint32,bool>(ATC_DEPRECIATION, -1, true), true);
	/**
	 * Get monthly statistics of term-instrument principal repaid.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_loan_repayment",    freevariable<sint32,sint32,bool>(ATC_LOAN_REPAYMENT, -1, true), true);
	/**
	 * Get monthly statistics of the pooled tax allowance for the period.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_tax_allowance",     freevariable<sint32,sint32,bool>(ATC_TAX_ALLOWANCE, -1, true), true);
	/**
	 * Get monthly statistics of the total outstanding secured principal at
	 * each month end. get_debt returns the current total.
	 * @returns array, index [0] corresponds to current month, in credits
	 */
	register_method_fv(vm, &get_player_stat, "get_secured_debt",      freevariable<sint32,sint32,bool>(ATC_SECURED_DEBT, -1, true), true);
	/**
	 * Get monthly statistics of the soft credit limit, beyond which the
	 * player cannot spend money.
	 * @returns array, index [0] corresponds to current month, in credits;
	 *          the limit is negative
	 */
	register_method_fv(vm, &get_player_stat, "get_soft_credit_limit", freevariable<sint32,sint32,bool>(ATC_SOFT_CREDIT_LIMIT, -1, true), true);
	/**
	 * Get monthly statistics of the hard credit limit, beyond which the
	 * player becomes insolvent.
	 * @returns array, index [0] corresponds to current month, in credits;
	 *          the limit is negative
	 */
	register_method_fv(vm, &get_player_stat, "get_hard_credit_limit", freevariable<sint32,sint32,bool>(ATC_HARD_CREDIT_LIMIT, -1, true), true);
	/**
	 * Get the accrual profit: operating profit after depreciation, interest
	 * and tax. This is the bottom row of the accrual profit and loss view in
	 * the finance window, and differs from get_profit, which is the
	 * cash-flow profit.
	 * @param monthly true for the monthly series, false for the yearly series
	 * @returns array, index [0] corresponds to the current month or year, in
	 *          credits
	 */
	register_method(vm, &get_player_profit_after_depreciation_and_tax, "get_profit_after_depreciation_and_tax", true);
	/**
	 * Get a whole-company finance history series.
	 * @param index one of the accounting_types_common constants
	 * @param monthly true for the monthly series, false for the yearly series
	 * @returns array, index [0] corresponds to the current month or year;
	 *          money values are in credits
	 */
	register_method(vm, &get_player_common_history, "get_common_history", true);
	/**
	 * Get a finance history series for one transport type.
	 * @param index one of the accounting_types_vehicles constants
	 * @param tt one of the transport_types constants
	 * @param monthly true for the monthly series, false for the yearly series
	 * @returns array, index [0] corresponds to the current month or year;
	 *          money values are in credits
	 */
	register_method(vm, &get_player_vehicle_history, "get_vehicle_history", true);

	if (scenario) {
		/**
		 * Change bank account of player by given amount @p delta.
		 * @param delta
		 * @ingroup scen_only
		 */
		register_method(vm, player_book_account, "book_cash", true);
	}
	/**
	 * Returns the current account balance.
	 */
	register_method(vm, &player_t::get_account_balance_as_double, "get_current_cash");
	/**
	 * Returns the current net worth [in 1/100 cr].
	 */
	register_method(vm, &finance_t::get_netwealth, "get_current_net_wealth");
	/**
	 * Returns the current maintenance [in 1/100 cr].
	 */
	register_method_fv(vm, &finance_t::get_maintenance_with_bits, "get_current_maintenance", freevariable<uint8>(TT_ALL));
	/**
	 * Returns the total outstanding secured debt [in 1/100 cr].
	 */
	register_method(vm, &finance_t::get_total_secured_debt, "get_debt");
	/**
	 * Returns the credit rating band (0 = AAA, 4 = BB, 8 = C).
	 */
	register_method(vm, &finance_t::get_credit_rating_index, "get_credit_rating");
	/**
	 * Returns the number of term instruments outstanding.
	 */
	register_method(vm, &finance_t::get_loan_count, "get_loan_count");
	/**
	 * Returns the term-debt limit [in 1/100 cr].
	 */
	register_method(vm, &finance_t::get_term_debt_limit, "get_loan_limit");
	/**
	 * Returns the credit asset value, the security for term borrowing [in 1/100 cr].
	 */
	register_method(vm, &finance_t::get_credit_asset_value, "get_asset_value");
	/**
	 * Returns whether the player (still) exists in the game.
	 *
	 * @deprecated Only available for api versions less than 120.1, see @ref get_api_version.
	 * @typemask bool()
	 */
	// register_method(vm,, "is_active", true); implemented in scenario_compat.nut
	/**
	 * Exports list of lines of this player.
	 * @typemask line_list_x()
	 */
	register_function(vm, &player_export_line_list, "get_line_list", 1, param<player_t*>::typemask());

	/**
	 * Creates a new line for the player of the given way type.
	 * @param wt way type
	 * @ingroup game_cmd
	 */
	register_method(vm, &player_create_line, "create_line", true);
	/**
	 * Returns player type: 1 = human, 2,3 = old c++ AI, 4 = scripted AI
	 */
	register_method(vm, &player_t::get_ai_id, "get_type");

	end_class(vm);
}
