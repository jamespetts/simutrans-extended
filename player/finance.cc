/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <assert.h>
#include <algorithm>

#include "../utils/float32e8_t.h"
#include "../simworld.h"
#include "../descriptor/building_desc.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/scenario.h"
#include "../dataobj/translator.h"
#include "simplay.h"

#include "finance.h"
#include "../dataobj/settings.h"


// The capital accounting settings are indexed by transport_type.
static_assert(TT_MAX == settings_t::MAX_CAPITAL_ACCOUNTING_TYPES, "The capital accounting settings must have one entry per transport type");
// The credit-rating settings are indexed by credit_rating_t.
static_assert(CREDIT_RATING_MAX == settings_t::MAX_CREDIT_RATINGS, "The credit-rating settings must have one entry per rating band");


char const *const credit_rating_names[CREDIT_RATING_MAX] =
{
	"AAA", "AA", "A", "BBB", "BB", "B", "CCC", "CC", "C"
};


/**
 * initialize finance history arrays
 */
finance_t::finance_t(player_t * _player, karte_t * _world) :
	player(_player),
	world(_world)
{
	// Starting money is inflation-adjusted so its purchasing power follows config/prices.tab
	// (the nominal simuconf figures are in year-1900 prices); old saves keep their stored value.
	account_balance = world->get_inflation_adjusted_price(world->get_timeline_year_month(), world->get_settings().get_starting_money(world->get_last_year()), general);
	starting_money = account_balance;
	account_overdrawn = 0;

	for (int year=0; year<MAX_PLAYER_HISTORY_YEARS; year++) {
		for (int cost_type=0; cost_type<ATC_MAX; cost_type++) {
			com_year[year][cost_type] = 0;
			if ((cost_type == ATC_CASH) || (cost_type == ATC_NETWEALTH)) {
				com_year[year][cost_type] = starting_money;
			}
		}
	}

	for (int month=0; month<MAX_PLAYER_HISTORY_MONTHS; month++) {
		for (int cost_type=0; cost_type<ATC_MAX; cost_type++) {
			com_month[month][cost_type] = 0;
			if ((cost_type == ATC_CASH) || (cost_type == ATC_NETWEALTH)) {
				com_month[month][cost_type] = starting_money;
			}
		}
	}

	for (int transport_type=0; transport_type<TT_MAX; ++transport_type){
		for (int year=0; year<MAX_PLAYER_HISTORY_YEARS; year++) {
			for (int cost_type=0; cost_type<ATV_MAX; cost_type++) {
				veh_year[transport_type][year][cost_type] = 0;
			}
		}
	}

	for (int transport_type=0; transport_type<TT_MAX; ++transport_type){
		for (int month=0; month<MAX_PLAYER_HISTORY_MONTHS; month++) {
			for (int cost_type=0; cost_type<ATV_MAX; cost_type++) {
				veh_month[transport_type][month][cost_type] = 0;
			}
		}
	}

	for(int i=0; i<TT_MAX; ++i){
		maintenance[i] = 0;
		asset_value[i] = 0;
		tax_pool[i] = 0;
	}

	asset_ledger_needs_initialisation = false;

	next_loan_id = 1;
	credit_rating = CREDIT_RATING_BB;
	// Negative means "not assessed yet"; month zero is a valid month.
	credit_assessment_month = -1;

	// for(int i=0; i<TT_MAX_VEH; ++i){
	//	vehicle_maintenance[i] = 0;
	// }

	// calc_credit_limits()
	// We should recalculate credit limits here.
	// However, the starting credit limit will be zero
	// (no assets, no profits)
}


void finance_t::calc_finance_history()
{
	// vehicles
	for(int tt=1; tt<TT_MAX; ++tt){
		// ATV_REVENUE_TRANSPORT = ATV_REVENUE_PAS+MAIL+GOOD
		sint64 revenue, mrevenue;
		revenue = mrevenue = 0;
		for(int i=0; i<ATV_REVENUE_TRANSPORT; ++i){
			mrevenue += veh_month[tt][0][i];
			revenue  += veh_year[ tt][0][i];
		}
		veh_month[tt][0][ATV_REVENUE_TRANSPORT] = mrevenue;
		veh_year[ tt][0][ATV_REVENUE_TRANSPORT] = revenue;

		// ATV_REVENUE = ATV_REVENUE_TRANSPORT + ATV_TOLL_RECEIVED
		veh_month[tt][0][ATV_REVENUE] = veh_month[tt][0][ATV_REVENUE_TRANSPORT] + veh_month[tt][0][ATV_TOLL_RECEIVED];
		veh_year[tt][0][ATV_REVENUE] = veh_year[tt][0][ATV_REVENUE_TRANSPORT] + veh_year[tt][0][ATV_TOLL_RECEIVED];

		// ATC_EXPENDITURE = ATC_RUNNING_COST + ATC_VEH_MAINTENENCE + ATC_INF_MAINTENANCE + ATC_TOLL_PAID;
		sint64 expenditure, mexpenditure;
		expenditure = mexpenditure = 0;
		for(int i=ATV_RUNNING_COST; i<ATV_EXPENDITURE; ++i){
			mexpenditure += veh_month[tt][0][i];
			expenditure  += veh_year[ tt][0][i];
		}
		veh_month[tt][0][ATV_EXPENDITURE] = mexpenditure;
		veh_year[ tt][0][ATV_EXPENDITURE] = expenditure;
		veh_month[tt][0][ATV_OPERATING_PROFIT] = veh_month[tt][0][ATV_REVENUE] + mexpenditure;
		veh_year[ tt][0][ATV_OPERATING_PROFIT] = veh_year[ tt][0][ATV_REVENUE] +  expenditure;

		// PROFIT = OPERATING_PROFIT + NEW_VEHICLES + construction costs + interest
		sint64 profit, mprofit;
		profit = mprofit = 0;
		for(int i=ATV_OPERATING_PROFIT; i<ATV_PROFIT; ++i){
			mprofit += veh_month[tt][0][i];
			profit  += veh_year[ tt][0][i];
		}
		veh_month[tt][0][ATV_PROFIT] = mprofit;
		veh_year[ tt][0][ATV_PROFIT] =  profit;

		veh_month[tt][0][ATV_WAY_TOLL] = veh_month[tt][0][ATV_TOLL_RECEIVED] + veh_month[tt][0][ATV_TOLL_PAID];
		veh_year[ tt][0][ATV_WAY_TOLL] = veh_year[tt][0][ATV_TOLL_RECEIVED] + veh_year[tt][0][ATV_TOLL_PAID];

		veh_month[tt][0][ATV_PROFIT_MARGIN] = calc_margin(veh_month[tt][0][ATV_OPERATING_PROFIT], veh_month[tt][0][ATV_REVENUE]);
		veh_year[tt][0][ATV_PROFIT_MARGIN] = calc_margin(veh_year[tt][0][ATV_OPERATING_PROFIT], veh_year[tt][0][ATV_REVENUE]);
	}

	// sum up statistic for all transport types
	for( int j=0; j< ATV_MAX; ++j ) {
		veh_month[TT_ALL][0][j] =0;
		for( int tt=1; tt<TT_MAX; ++tt ) {
			// do not add powerline revenue to vehicles revenue
			if ( ( tt != TT_POWERLINE ) || ( j >= ATV_REVENUE )) {
				veh_month[TT_ALL][0][j] += veh_month[tt][0][j];
			}
		}
	}
	for( int j=0; j< ATV_MAX; ++j ) {
		veh_year[TT_ALL][0][j] =0;
		for( int tt=1; tt<TT_MAX; ++tt ) {
			// do not add powerline revenue to vehicles revenue
			if ( ( tt != TT_POWERLINE ) || ( j >= ATV_REVENUE )) {
				veh_year[TT_ALL][0][j] += veh_year[tt][0][j];
			}
		}
	}
	// recalc margin for TT_ALL
	veh_month[TT_ALL][0][ATV_PROFIT_MARGIN] = calc_margin(veh_month[TT_ALL][0][ATV_OPERATING_PROFIT], veh_month[TT_ALL][0][ATV_REVENUE]);
	veh_year[TT_ALL][0][ATV_PROFIT_MARGIN] = calc_margin(veh_year[TT_ALL][0][ATV_OPERATING_PROFIT], veh_year[TT_ALL][0][ATV_REVENUE]);
	veh_month[TT_ALL][0][ATV_PROFIT] += com_month[0][ATC_INTEREST];
	veh_year[TT_ALL][0][ATV_PROFIT] += com_year[0][ATC_INTEREST];
	veh_month[TT_OTHER][0][ATV_PROFIT] += com_month[0][ATC_INTEREST];
	veh_year[TT_OTHER][0][ATV_PROFIT] += com_year[0][ATC_INTEREST];

	com_month[0][ATC_SECURED_DEBT] = get_total_secured_debt();
	com_year[0][ATC_SECURED_DEBT] = get_total_secured_debt();

	// undistinguishable by type of transport
	com_month[0][ATC_CASH] = account_balance;
	com_year [0][ATC_CASH] = account_balance;
	com_month[0][ATC_NETWEALTH] = veh_month[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] + account_balance;
	com_year [0][ATC_NETWEALTH] = veh_year[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] + account_balance;
}


sint64 finance_t::get_maintenance_with_bits(transport_type tt) const
{
	assert(tt<TT_MAX);
	return world->get_inflation_adjusted_price(world->get_timeline_year_month(), world->calc_adjusted_monthly_figure(maintenance[tt]), infrastructure);
}

void finance_t::new_month()
{
	// First, make sure everything is recorded right, before changing the month.
	calc_finance_history();
	roll_history_month();

	if(world->get_last_month()==0) {
		roll_history_year();
	}

	// Term-instrument interest and principal come before the overdraft
	// interest, so that the overdraft is charged on the post-payment balance.
	book_loans_monthly();

	// subtract interest (before subtracting infrastructure maintenance)
	book_interest_monthly();

	if (!player->is_public_service())
	{
		book_corporation_tax_monthly();
	}

	// subtract infrastructure maintenance
	for(int i=0; i<TT_MAX; ++i){
		veh_month[i][0][ATV_INFRASTRUCTURE_MAINTENANCE] -= get_maintenance_with_bits((transport_type)i);
		veh_year [i][0][ATV_INFRASTRUCTURE_MAINTENANCE] -= get_maintenance_with_bits((transport_type)i);
	}

	// update the pooled asset values and the tax allowance
	depreciate_assets_monthly();
	book_tax_allowance_monthly();
}

void finance_t::depreciate_assets_monthly()
{
	const settings_t& settings = world->get_settings();
	sint64 total_depreciation = 0;

	for (int i = 0; i < TT_MAX; i++)
	{
		const sint16 rate = settings.get_asset_depreciation_percent((uint8)i);
		if (rate <= 0 || asset_value[i] <= 0)
		{
			continue;
		}

		sint64 depreciation = (asset_value[i] * (sint64)rate) / 1200ll;
		if (depreciation > asset_value[i])
		{
			depreciation = asset_value[i];
		}
		asset_value[i] -= depreciation;
		total_depreciation += depreciation;
	}

	if (total_depreciation > 0)
	{
		// A negative amount, like the other expense entries.
		com_month[0][ATC_DEPRECIATION] -= total_depreciation;
		com_year[0][ATC_DEPRECIATION] -= total_depreciation;
	}
}

void finance_t::book_tax_allowance_monthly()
{
	const settings_t& settings = world->get_settings();
	sint64 total_allowance = 0;

	for (int i = 0; i < TT_MAX; i++)
	{
		const sint16 rate = settings.get_tax_allowance_percent((uint8)i);
		if (rate <= 0 || tax_pool[i] <= 0)
		{
			continue;
		}

		sint64 allowance = (tax_pool[i] * (sint64)rate) / 1200ll;
		if (allowance > tax_pool[i])
		{
			allowance = tax_pool[i];
		}
		tax_pool[i] -= allowance;
		total_allowance += allowance;
	}

	com_month[0][ATC_TAX_ALLOWANCE] += total_allowance;
	com_year[0][ATC_TAX_ALLOWANCE] += total_allowance;
}

void finance_t::initialise_legacy_asset_ledger()
{
	const settings_t& settings = world->get_settings();

	for (int i = 0; i < TT_MAX; i++)
	{
		const sint64 way_length = veh_month[i][0][ATV_WAY_LENGTH];
		const sint64 cost_per_tile = settings.get_legacy_way_asset_cost((uint8)i);
		if (way_length <= 0 || cost_per_tile <= 0)
		{
			continue;
		}
		// The way length statistic counts 10 units for a straight tile and 7
		// for a diagonal tile, so dividing by 10 gives the straight-tile count.
		asset_value[i] = (way_length * cost_per_tile) / 10ll;
	}

	asset_ledger_needs_initialisation = false;
}

/**
 * Books all interest.
 * Currently only overdraft interest
 * TODO: Add other kinds of borrowing
 */
void finance_t::book_interest_monthly()
{
	const sint64 overdraft_rate_bp = get_overdraft_rate_bp();
	if (overdraft_rate_bp > 0)
	{
		sint64 interest;
		if(get_account_balance() < 0)
		{
			interest = (overdraft_rate_bp * get_account_balance()) / 120000ll;
		}
		else
		{
			interest = 0;
		}

		book_interest(interest);
	}
}


void finance_t::book_loans_monthly()
{
	const sint32 current_month = world->get_current_month();

	for (uint32 i = 0; i < loans.get_count(); )
	{
		loan_t &loan = loans[i];

		if (current_month - loan.start_month <= 0)
		{
			// The first payment falls due in the month after drawdown.
			i++;
			continue;
		}

		const sint64 interest = loan.get_interest_for_month();
		if (interest > 0)
		{
			book_interest(-interest);
		}

		sint64 principal_due = loan.get_principal_due_this_month();
		if (loan.type == LOAN_TYPE_BOND && loan.is_matured(current_month))
		{
			principal_due = loan.outstanding;
		}

		if (principal_due > 0)
		{
			if (principal_due > loan.outstanding)
			{
				principal_due = loan.outstanding;
			}
			book_loan_repayment(-principal_due);
			loan.outstanding -= principal_due;
		}

		loan.payments_made++;

		if (loan.outstanding <= 0)
		{
			loans.remove_at(i);
		}
		else
		{
			i++;
		}
	}
}

void finance_t::book_corporation_tax_monthly()
{
	const sint64 tax_rate = world->get_inflation_adjusted_price(world->get_timeline_year_month(), 100, price_type::corporation_tax);

	// Corporation tax has generous capital allowances, so players are taxed on their previous year's gross profit. Thus, new players
	// do not have to pay any tax for the first year. In each month, players then pay corporation tax on 1/12th of the gross profit.
	// from the previous year. The tax is given to the public player.

	// The tax base is the cash-flow profit with capital expenditure added back,
	// less the pooled tax allowance of the year. ATV_PROFIT already deducts
	// ATV_CONSTRUCTION_COST and ATV_NEW_VEHICLE, so adding capital expenditure
	// back means subtracting those (negative) entries.
	const sint64 capital_expenditure_last_year = veh_year[TT_ALL][1][ATV_CONSTRUCTION_COST] + veh_year[TT_ALL][1][ATV_NEW_VEHICLE];
	const sint64 tax_allowance_last_year = com_year[1][ATC_TAX_ALLOWANCE];
	const sint64 taxable_profit_last_year = veh_year[TT_ALL][1][ATV_PROFIT] - capital_expenditure_last_year - tax_allowance_last_year;

	// Divide by months and 100s
	sint64 tax_payable = (taxable_profit_last_year * tax_rate) / 1200ll;

	if (tax_payable < 0)
	{
		tax_payable = 0;
	}

	book_tax(-tax_payable);
	world->get_public_player()->book_tax(tax_payable);
}

void finance_t::calc_credit_limits()
{
	sint64 hard_limit_by_profits = credit_limit_by_profits();
	sint64 hard_limit_by_assets = credit_limit_by_assets();

	// The player gets the better of the two credit limits.  Remember that they are negative.
	sint64 hard_credit_limit = std::min(hard_limit_by_profits, hard_limit_by_assets);
	assert(hard_credit_limit <= 0);

	// Soft credit limit is a percentage of the hard credit limit
	// This percentage will be set by settings -- FIXME LATER
	uint8 soft_credit_limit_percent = 50;

	// Don't worry about exact computations here.
	sint64 soft_credit_limit = (hard_credit_limit / 100) * soft_credit_limit_percent;
	assert(soft_credit_limit <= 0);

	com_month[0][ATC_HARD_CREDIT_LIMIT] = hard_credit_limit;
	com_month[0][ATC_SOFT_CREDIT_LIMIT] = soft_credit_limit;

	if(world->get_last_month()==0) {
		// New year, record new starting credit limit
		com_year[0][ATC_HARD_CREDIT_LIMIT] = hard_credit_limit;
		com_year[0][ATC_SOFT_CREDIT_LIMIT] = soft_credit_limit;
	}
}

/**
 * Subroutine for credit limit calculuations.
 * Calculates a credit limit based on past year's profitability
 * (ability to cover interest costs).
 */
sint64 finance_t::credit_limit_by_profits() const
{
	// The idea is that yearly profits should cover yearly interest
	// Look back 12 months (full year's profit)
	sint64 profit_total=0;
	// We need 12 months of history at least, not including this month
	assert(MAX_PLAYER_HISTORY_MONTHS >= 13);
	// Start by looking at *last* month and go back 12 (one year)
	for(int month = 1; month < 13; month++)
	{
		// Use operating profits as the basis for interest coverage.
		// This is before interst and before construction costs.
		profit_total += get_history_veh_month(TT_ALL, month, ATV_OPERATING_PROFIT);
    }
	const sint64 overdraft_interest_rate = (sint64)get_overdraft_rate_percent();

	if(overdraft_interest_rate == 0)
	{
		return 0;
	}

	// *Divide* by the interest rate: if all the profits went to interest,
	// this tells us how much debt (principal) we could pay interest on
	// Does not account for compound interest, so generous to the player
	sint64 hard_limit_by_profits = - (profit_total * 100ll) / overdraft_interest_rate;
	// The following deals with recurring losses;
	// It also deals (badly) with overflow errors.
	if (hard_limit_by_profits > 0) {
		hard_limit_by_profits = 0;
	}
	return hard_limit_by_profits;
}

/**
 * Subroutine for credit limit calculations.
 * Calculates an asset-based credit limit.
 * Secured borrowing against assets.
 */
sint64 finance_t::credit_limit_by_assets() const
{
	// Can borrow against potentially all assets.
	sint64 hard_limit_by_assets = - get_history_veh_month(TT_ALL, 0, ATV_NON_FINANCIAL_ASSETS);
	// The following deals with potential bugs.
    if (hard_limit_by_assets > 0) {
        hard_limit_by_assets = 0;
    }
	return hard_limit_by_assets;
}


sint64 finance_t::get_total_secured_debt() const
{
	sint64 total = 0;
	for (uint32 i = 0; i < loans.get_count(); i++)
	{
		total += loans[i].outstanding;
	}
	return total;
}


sint64 finance_t::get_term_debt_limit() const
{
	const settings_t &settings = world->get_settings();
	const sint64 minimum = world->get_inflation_adjusted_price(world->get_timeline_year_month(), settings.get_minimum_loan_limit(), general);
	const sint64 gearing_limit = (get_credit_asset_value() * (sint64)settings.get_maximum_secured_gearing_percent()) / 100ll;
	return max(minimum, gearing_limit);
}


sint64 finance_t::get_available_term_debt() const
{
	const sint64 available = get_term_debt_limit() - get_total_secured_debt();
	return available > 0 ? available : 0;
}


sint32 finance_t::get_credit_spread_bp(uint8 rating) const
{
	if (rating >= CREDIT_RATING_MAX)
	{
		rating = CREDIT_RATING_C;
	}
	return world->get_settings().get_credit_spread_bp(rating);
}


sint32 finance_t::get_secured_gearing_premium_bp(sint64 total_secured_debt) const
{
	const settings_t &settings = world->get_settings();
	const sint64 asset_value = get_credit_asset_value();

	// Without a credit asset value every secured borrowing is at the highest
	// band; the limit check prevents borrowing where none is available.
	if (asset_value <= 0)
	{
		return settings.secured_gearing_premium[4].premium_bp;
	}

	const sint64 gearing_percent = (total_secured_debt * 100ll) / asset_value;
	for (int i = 0; i < 5; i++)
	{
		if (gearing_percent <= (sint64)settings.secured_gearing_premium[i].max_gearing_percent)
		{
			return settings.secured_gearing_premium[i].premium_bp;
		}
	}
	return settings.secured_gearing_premium[4].premium_bp;
}


sint32 finance_t::get_overdraft_rate_bp() const
{
	// The base rate in prices.tab is expressed in per cent, like the legacy
	// overdraft calculation; convert it to basis points.
	const sint32 base_rate_percent = (sint32)world->get_inflation_adjusted_price(world->get_timeline_year_month(), 100, price_type::base_rate);
	return base_rate_percent * 100
		+ (sint32)world->get_settings().get_overdraft_percent_above_base_rate() * 100
		+ get_credit_spread_bp(credit_rating);
}


sint16 finance_t::get_overdraft_rate_percent() const
{
	return (sint16)((get_overdraft_rate_bp() + 50) / 100);
}


sint32 finance_t::get_term_rate_bp(loan_term_t term, uint8 rating, sint64 additional_principal) const
{
	const settings_t &settings = world->get_settings();
	// The base rate in prices.tab is expressed in per cent, like the legacy
	// overdraft calculation; convert it to basis points.
	const sint32 base_rate_percent = (sint32)world->get_inflation_adjusted_price(world->get_timeline_year_month(), 100, price_type::base_rate);

	sint32 rate = base_rate_percent * 100
		+ (sint32)settings.get_term_lending_margin_bp()
		+ (sint32)settings.get_term_offset_bp((uint8)term)
		+ get_credit_spread_bp(rating)
		+ get_secured_gearing_premium_bp(get_total_secured_debt() + additional_principal);

	return rate > 0 ? rate : 0;
}


bool finance_t::is_term_available(loan_term_t term, uint8 type, uint8 rating) const
{
	if (!world->get_settings().term_loans_allowed() || term >= LOAN_TERM_MAX)
	{
		return false;
	}

	// CCC and below may issue no new term debt.
	if (rating >= CREDIT_RATING_CCC)
	{
		return false;
	}

	if (type == LOAN_TYPE_BOND && term == LOAN_TERM_25_YEARS)
	{
		// A 25-year bond requires an investment-grade rating.
		return rating <= CREDIT_RATING_BBB;
	}

	if (rating <= CREDIT_RATING_BBB)
	{
		return true;
	}
	if (rating == CREDIT_RATING_BB)
	{
		return term <= LOAN_TERM_10_YEARS;
	}
	// B
	return term <= LOAN_TERM_5_YEARS;
}


loan_term_t finance_t::term_from_months(sint32 months)
{
	if (months <= 12) { return LOAN_TERM_1_YEAR; }
	if (months <= 24) { return LOAN_TERM_2_YEARS; }
	if (months <= 60) { return LOAN_TERM_5_YEARS; }
	if (months <= 120) { return LOAN_TERM_10_YEARS; }
	return LOAN_TERM_25_YEARS;
}


uint8 finance_t::assess_credit_rating(sint64 additional_principal, sint64 additional_debt_service) const
{
	const settings_t &settings = world->get_settings();

	if (player != NULL && !player->is_public_service())
	{
		const player_t::solvency_status solvency = player->check_solvency();
		if (solvency == player_t::in_liquidation)
		{
			return CREDIT_RATING_C;
		}
		if (solvency == player_t::in_administration)
		{
			return CREDIT_RATING_CC;
		}
	}

	if (player != NULL && (sint16)player->get_player_age() < settings.get_new_company_credit_rating_months())
	{
		const sint16 rating = settings.get_new_company_credit_rating();
		return (uint8)clamp<sint16>(rating, 0, CREDIT_RATING_MAX - 1);
	}

	// Debt-service cover: trailing twelve months of operating profit against the
	// debt service of the next twelve months.
	sint64 operating_profit = 0;
	for (int month = 1; month <= 12 && month < MAX_PLAYER_HISTORY_MONTHS; month++)
	{
		operating_profit += get_history_veh_month(TT_ALL, month, ATV_OPERATING_PROFIT);
	}

	sint64 debt_service = additional_debt_service;
	for (uint32 i = 0; i < loans.get_count(); i++)
	{
		debt_service += loans[i].get_debt_service_for_next_months(12);
	}

	uint8 cover_band = CREDIT_RATING_AAA;
	if (debt_service > 0)
	{
		const sint64 cover_hundredths = (operating_profit * 100ll) / debt_service;
		cover_band = CREDIT_RATING_CCC;
		for (uint8 band = 0; band < CREDIT_RATING_MAX; band++)
		{
			if (cover_hundredths >= (sint64)settings.get_credit_min_debt_service_cover(band))
			{
				cover_band = band;
				break;
			}
		}
	}

	// Leverage: total debt against total debt plus equity, where equity is cash
	// plus the credit asset value less debt. The proposed instrument adds to
	// both cash and debt, so the denominator gains the principal only.
	const sint64 overdraft = account_balance < 0 ? -account_balance : 0;
	const sint64 total_debt = get_total_secured_debt() + additional_principal + overdraft;
	const sint64 capital = account_balance + additional_principal + get_credit_asset_value();

	uint8 leverage_band = CREDIT_RATING_CCC;
	if (capital > 0)
	{
		const sint64 leverage_percent = (total_debt * 100ll) / capital;
		for (uint8 band = 0; band < CREDIT_RATING_MAX; band++)
		{
			if (leverage_percent <= (sint64)settings.get_credit_max_leverage_percent(band))
			{
				leverage_band = band;
				break;
			}
		}
	}

	return max(cover_band, leverage_band);
}


void finance_t::update_credit_rating()
{
	credit_rating = assess_credit_rating();
	credit_assessment_month = world->get_current_month();
}


loan_t * finance_t::get_loan(uint32 id)
{
	for (uint32 i = 0; i < loans.get_count(); i++)
	{
		if (loans[i].id == id)
		{
			return &loans[i];
		}
	}
	return NULL;
}


char const *finance_t::issue_loan(uint8 type, sint64 amount, loan_term_t term)
{
	const settings_t &settings = world->get_settings();

	if (!settings.term_loans_allowed())
	{
		return translator::translate("Term borrowing is not enabled in this game.");
	}
	if (type != LOAN_TYPE_BANK && type != LOAN_TYPE_BOND)
	{
		return translator::translate("Invalid instrument type.");
	}
	if (amount <= 0 || term >= LOAN_TERM_MAX)
	{
		return translator::translate("Invalid amount or term.");
	}
	if (player != NULL && player->check_solvency() != player_t::solvent)
	{
		return translator::translate("A company in administration or liquidation cannot borrow.");
	}

	const sint32 term_months = loan_term_months(term);
	if (term_months <= 0)
	{
		return translator::translate("Invalid amount or term.");
	}

	// Provisional post-loan assessment, including the new principal and its
	// debt service.
	loan_t proposed(0, type, world->get_current_month(), term_months, amount, 0, credit_rating);
	const uint8 rating = assess_credit_rating(amount, proposed.get_debt_service_for_next_months(12));

	if (!is_term_available(term, type, rating))
	{
		return translator::translate("Your credit rating does not permit this term.");
	}

	if (amount > get_available_term_debt())
	{
		return translator::translate("That would exceed the term borrowing limit.");
	}

	const sint32 rate_bp = get_term_rate_bp(term, rating, amount);

	loan_t loan(next_loan_id++, type, world->get_current_month(), term_months, amount, rate_bp, rating);
	loans.append(loan);

	// Draw down the principal.
	book_account(amount);

	return NULL;
}


char const *finance_t::settle_loan(uint32 id, bool replacing)
{
	loan_t * const loan = get_loan(id);
	if (loan == NULL)
	{
		return translator::translate("No such instrument.");
	}
	if (player != NULL && player->check_solvency() != player_t::solvent)
	{
		return translator::translate("A company in administration or liquidation cannot repay instruments early.");
	}

	const settings_t &settings = world->get_settings();
	const sint32 base_rate_percent = (sint32)world->get_inflation_adjusted_price(world->get_timeline_year_month(), 100, price_type::base_rate);
	const sint32 discount_rate_bp = base_rate_percent * 100
		+ (sint32)settings.get_term_offset_bp((uint8)term_from_months(loan->get_months_remaining(world->get_current_month())))
		+ get_credit_spread_bp(loan->rating);

	const sint64 settlement = loan->get_early_repayment_settlement(discount_rate_bp);

	// A replacement draws the same principal again immediately, so only the
	// break cost has to be affordable; a plain repayment has to fund the whole
	// settlement.
	const sint64 break_cost = settlement - loan->outstanding;
	if (!can_afford(replacing ? break_cost : settlement))
	{
		return NOTICE_INSUFFICIENT_FUNDS;
	}

	// Book the principal as a repayment and any excess as a break cost, which
	// is an interest expense.
	book_loan_repayment(-loan->outstanding);
	if (break_cost > 0)
	{
		book_interest(-break_cost);
	}

	loan->outstanding = 0;

	for (uint32 i = 0; i < loans.get_count(); i++)
	{
		if (loans[i].id == id)
		{
			loans.remove_at(i);
			break;
		}
	}

	return NULL;
}


char const *finance_t::repay_loan(uint32 id)
{
	return settle_loan(id, false);
}


char const *finance_t::refinance_loan(uint32 id, loan_term_t term)
{
	loan_t * const loan = get_loan(id);
	if (loan == NULL)
	{
		return translator::translate("No such instrument.");
	}
	if (player != NULL && player->check_solvency() != player_t::solvent)
	{
		return translator::translate("A company in administration or liquidation cannot refinance.");
	}
	if (term >= LOAN_TERM_MAX)
	{
		return translator::translate("Invalid amount or term.");
	}

	const sint64 outstanding = loan->outstanding;
	const uint8 type = loan->type;

	// The replacement carries the same outstanding principal, so the net
	// secured debt after refinancing is unchanged. The existing instrument is
	// replaced, so its debt service is removed from the assessment.
	const loan_t proposed(0, type, world->get_current_month(), loan_term_months(term), outstanding, 0, credit_rating);
	const sint64 debt_service_change = proposed.get_debt_service_for_next_months(12) - loan->get_debt_service_for_next_months(12);
	const uint8 rating = assess_credit_rating(0, debt_service_change);

	if (!is_term_available(term, type, rating))
	{
		return translator::translate("Your credit rating does not permit this term.");
	}

	const sint32 rate_bp = get_term_rate_bp(term, rating, 0);

	// Early refinancing settles the existing instrument at the make-whole
	// amount, then draws the same principal again.
	char const * const error = settle_loan(id, true);
	if (error != NULL)
	{
		return error;
	}

	loan_t replacement(next_loan_id++, type, world->get_current_month(), loan_term_months(term), outstanding, rate_bp, rating);
	loans.append(replacement);
	book_account(outstanding);

	return NULL;
}


void finance_t::assume_loans(finance_t const *other)
{
	for (uint32 i = 0; i < other->loans.get_count(); i++)
	{
		loan_t loan = other->loans[i];
		// Identifiers are unique only within a player.
		loan.id = next_loan_id++;
		loans.append(loan);
	}
}

/* most recent savegame version: now with detailed finance statistics by type of transport */
void finance_t::rdwr(loadsave_t *file)
{
	// detailed statistic were introduced in this version
	if( file->is_version_less(112, 5) ) {
		rdwr_compatibility(file);
		if ( file->is_loading() ) {
			// Loaded hard credit limit will be wrong, fix it quick to avoid spurious insolvency
			calc_credit_limits();
		}
		return;
	}

	/* following lines enables FORWARD compatibility
	/ you will be still able to load future versions of games with:
	*   longer history
	*   more transport_types
	*   and new items in ATC_ or ATV_
	* Warning: extended adds three lines to ATC_ immediately, with version 112005.
	* If Standard adds lines to ATC_, we must make adjustments by pushing the extended lines "down".
	*/
	sint8 max_years  = MAX_PLAYER_HISTORY_YEARS;
	sint8 max_months = MAX_PLAYER_HISTORY_MONTHS;
	sint8 max_tt     = TT_MAX;
	sint8 max_atc    = ATC_MAX;
	sint8 max_atv    = ATV_MAX;

	// used for reading longer history
	sint64 dummy = 0;

	// calc finance history for TT_ALL to save it correctly
	if( ! file->is_loading() ) {
		calc_finance_history();
	}

	file->rdwr_longlong(account_balance);
	file->rdwr_long(account_overdrawn);
	file->rdwr_longlong(starting_money);

	file->rdwr_byte( max_years );
	file->rdwr_byte( max_months );
	file->rdwr_byte( max_tt ); // tt = transport type
	file->rdwr_byte( max_atc ); // atc = accounting type common
	file->rdwr_byte( max_atv ); // atv = accounting type vehicles

	for(int year = 0;  year < max_years ; ++year ) {
		for( int cost_type = 0; cost_type < max_atc ;  ++cost_type  ) {
			if( ( year < MAX_PLAYER_HISTORY_YEARS ) && ( cost_type < ATC_MAX ) ) {
				file->rdwr_longlong( com_year[year][cost_type] );
				// convert
				if( file->is_loading() && file->is_version_ex_less(14,64) && cost_type==2 /*was ATC_ALL_CONVOIS*/) {
					veh_year[TT_ALL][year][ATV_CONVOIS] = com_year[year][2];
					com_year[year][2] = 0;
				}
			} else {
				file->rdwr_longlong( dummy );
			}
		}
	}
	for(int month = 0; month < max_months; ++month) {
		for( int cost_type = 0; cost_type < max_atc;  ++cost_type ) {
			if( ( month < MAX_PLAYER_HISTORY_MONTHS ) && ( cost_type < ATC_MAX ) ) {
				file->rdwr_longlong( com_month[month][cost_type] );
				// convert
				if (file->is_loading() && file->is_version_ex_less(14, 64) && cost_type == 2 /*was ATC_ALL_CONVOIS*/) {
					veh_month[TT_ALL][month][ATV_CONVOIS] = com_month[month][2];
					com_month[month][2] = 0;
				}
			} else {
				file->rdwr_longlong( dummy );
			}
		}
	}
	for(int tt=0; tt < max_tt; ++tt){
		for( int year = 0;  year < max_years;  ++year ) {
			for( int cost_type = 0; cost_type < max_atv;  ++cost_type  ) {
				if (file->is_loading() && file->is_version_ex_less(14, 64) && cost_type > 20) {
					// esase broken data
					file->rdwr_longlong(dummy);
					if(tt!=TT_ALL && cost_type==ATV_CONVOIS) { // ATV_CONVOIS[TT_ALL] is inherited from ATC_CONVOIS
						veh_year[tt][year][cost_type] = 0;
					}
				}
				else if( ( tt < TT_MAX ) && ( year < MAX_PLAYER_HISTORY_YEARS ) && ( cost_type < ATV_MAX ) ) {
					file->rdwr_longlong( veh_year[tt][year][cost_type] );
				} else {
					file->rdwr_longlong( dummy );
				}
			}
		}
	}
	for(int tt=0; tt < max_tt; ++tt){
		for( int month = 0; month < max_months; ++month ) {
			for( int cost_type = 0; cost_type < max_atv;  ++cost_type  ) {
				if (file->is_loading() && file->is_version_ex_less(14, 64) && cost_type > 20) {
					// esase broken data
					file->rdwr_longlong(dummy);
					if(tt!=TT_ALL && cost_type == ATV_CONVOIS) { // ATV_CONVOIS[TT_ALL] is inherited from ATC_CONVOIS
						veh_month[tt][month][cost_type] = 0;
					}
				}
				else if( ( tt < TT_MAX ) && ( month < MAX_PLAYER_HISTORY_MONTHS ) && ( cost_type < ATV_MAX ) ) {
					file->rdwr_longlong( veh_month[tt][month][cost_type] );
				} else {
					file->rdwr_longlong( dummy );
				}
			}
		}
		// The current value is calculated by finish_rd
		veh_month[tt][0][ATV_WAY_LENGTH] = 0;
		veh_year[tt][0][ATV_WAY_LENGTH] = 0;
	}

	if (file->is_version_ex_atleast(15, 18))
	{
		for (int i = 0; i < TT_MAX; i++)
		{
			file->rdwr_longlong(asset_value[i]);
		}
		for (int i = 0; i < TT_MAX; i++)
		{
			file->rdwr_longlong(tax_pool[i]);
		}

		uint32 loan_count = loans.get_count();
		file->rdwr_long(loan_count);
		if (file->is_loading())
		{
			loans.clear();
			for (uint32 i = 0; i < loan_count; i++)
			{
				loan_t loan;
				loan.rdwr(file);
				loans.append(loan);
			}
		}
		else
		{
			for (uint32 i = 0; i < loan_count; i++)
			{
				loans[i].rdwr(file);
			}
		}

		file->rdwr_long(next_loan_id);
		file->rdwr_byte(credit_rating);
		file->rdwr_long(credit_assessment_month);

		if (file->is_loading() && credit_rating >= CREDIT_RATING_MAX)
		{
			credit_rating = CREDIT_RATING_C;
		}
	}
	else if (file->is_loading())
	{
		for (int i = 0; i < TT_MAX; i++)
		{
			asset_value[i] = 0;
			tax_pool[i] = 0;
		}
		// Initialised by player_t::finish_rd(), once the way length is known.
		asset_ledger_needs_initialisation = true;

		loans.clear();
		next_loan_id = 1;
		// Assessed by player_t::finish_rd().
		credit_rating = CREDIT_RATING_BB;
		credit_assessment_month = -1;
	}
}


void finance_t::roll_history_month()
{
	// undistinguishable
	for (int i=MAX_PLAYER_HISTORY_MONTHS-1; i>0; i--) {
		for(int accounting_type=0; accounting_type<ATC_MAX; ++accounting_type){
			com_month[i][accounting_type] = com_month[i-1][accounting_type];
		}
	}
	for(int i=0; i<ATC_MAX; ++i){
		if(i != ATC_HALTS  &&  i != ATC_SCENARIO_COMPLETED){
			com_month[0][i] = 0;
		}
	}
	// vehicles
	for(int tt=0; tt<TT_MAX; ++tt){
		for (int i=MAX_PLAYER_HISTORY_MONTHS-1; i>0; i--) {
			for(int accounting_type=0; accounting_type<ATV_MAX; ++accounting_type){
				veh_month[tt][i][accounting_type] = veh_month[tt][i-1][accounting_type];
			}
		}
		for(int accounting_type=0; accounting_type<ATV_MAX; ++accounting_type){
			if (accounting_type < ATV_CARRY_OVER_DATA_TO_NEXT_MON ) {
				veh_month[tt][0][accounting_type] = 0;
			}
		}
	}
}


void finance_t::roll_history_year()
{
	// undistinguishable
	for (int i=MAX_PLAYER_HISTORY_YEARS-1; i>0; i--) {
		for(int accounting_type=0; accounting_type<ATC_MAX; ++accounting_type){
			com_year[i][accounting_type] = com_year[i-1][accounting_type];
		}
	}
	for(int i=0; i<ATC_MAX; ++i){
		if(i != ATC_HALTS  &&  i != ATC_SCENARIO_COMPLETED){
			com_year[0][i] = 0;
		}
	}
	// vehicles
	for(int tt=0; tt<TT_MAX; ++tt){
		for (int i=MAX_PLAYER_HISTORY_YEARS-1; i>0; i--) {
			for(int accounting_type=0; accounting_type<ATV_MAX; ++accounting_type){
				veh_year[tt][i][accounting_type] = veh_year[tt][i-1][accounting_type];
			}
		}
		for(int accounting_type=0; accounting_type<ATV_MAX; ++accounting_type){
			if (accounting_type < ATV_CARRY_OVER_DATA_TO_NEXT_MON ) {
				veh_year[tt][0][accounting_type] = 0;
			}
		}
	}
}


void finance_t::set_assets(const sint64 (&assets)[TT_MAX])
{
	for(int i=0; i < TT_MAX; ++i){
		veh_year[i][0][ATV_NON_FINANCIAL_ASSETS] = veh_month[i][0][ATV_NON_FINANCIAL_ASSETS] = assets[i];
	}
	com_year[0][ATC_NETWEALTH] = com_month[0][ATC_NETWEALTH] = veh_month[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] + account_balance;
}


void finance_t::update_assets(sint64 const delta, const waytype_t wt)
{
	transport_type tt = translate_waytype_to_tt(wt);
	veh_year[ tt][0][ATV_NON_FINANCIAL_ASSETS] += delta;
	veh_month[tt][0][ATV_NON_FINANCIAL_ASSETS] += delta;
	veh_year[ TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] += delta;
	veh_month[TT_ALL][0][ATV_NON_FINANCIAL_ASSETS] += delta;

	com_year[ 0][ATC_NETWEALTH] += delta;
	com_month[0][ATC_NETWEALTH] += delta;
}


transport_type finance_t::translate_waytype_to_tt(const waytype_t wt)
{
	switch(wt){
		case road_wt:      return TT_ROAD;
		case track_wt:     return TT_RAILWAY;
		case water_wt:     return TT_SHIP;
		case monorail_wt:  return TT_MONORAIL;
		case maglev_wt:    return TT_MAGLEV;
		case tram_wt:      return TT_TRAM;
		case narrowgauge_wt: return TT_NARROWGAUGE;
		case air_wt:       return TT_AIR;
		case powerline_wt: return TT_POWERLINE;
		case ignore_wt:
		case overheadlines_wt:
		default:           return TT_OTHER;
	}
}

waytype_t finance_t::translate_tt_to_waytype(const transport_type tt)
{
	switch (tt)
	{
		case TT_ROAD:           return road_wt;
		case TT_RAILWAY:        return track_wt;
		case TT_SHIP:           return water_wt;
		case TT_MONORAIL:       return monorail_wt;
		case TT_MAGLEV:         return maglev_wt;
		case TT_TRAM:           return tram_wt;
		case TT_NARROWGAUGE:    return narrowgauge_wt;
		case TT_AIR:            return air_wt;
		case TT_POWERLINE:      return powerline_wt;
		case TT_OTHER:          return overheadlines_wt;
		default:                return ignore_wt;
	}
}


/* order has to be same as in enum transport_type in file finance.h */
/* Also these have to match the strings in simline_t::linetype2name! */
/* (and it is sad that the order between those do not match ...) */
const char* finance_t::transport_type_values[TT_MAX] = {
	"All",
	"Truck",
	"Train",
	"Ship",
	"Monorail",
	"Maglev",
	"Tram",
	"Narrowgauge",
	"Aircraft",
	"tt_Other",
	"Powerlines",
};

/** compatibility code follows **/

#define OLD_MAX_PLAYER_HISTORY_YEARS  (12) // number of years to keep history
#define OLD_MAX_PLAYER_HISTORY_MONTHS  (12) // number of months to keep history

enum player_cost {
	COST_CONSTRUCTION=0,     // Construction
	COST_VEHICLE_RUN,        // Vehicle running costs
	COST_NEW_VEHICLE,        // New vehicles
	COST_INCOME,             // Income
	COST_MAINTENANCE,        // Upkeep
	COST_ASSETS,             // value of all vehicles and buildings
	COST_CASH,               // Cash
	COST_NETWEALTH,          // Total Cash + Assets
	COST_PROFIT,             // COST_POWERLINES+COST_INCOME-(COST_CONSTRUCTION+COST_VEHICLE_RUN+COST_NEW_VEHICLE+COST_MAINTENANCE)
	COST_OPERATING_PROFIT,   // COST_POWERLINES+COST_INCOME-(COST_VEHICLE_RUN+COST_MAINTENANCE)+COST_INTEREST
	COST_MARGIN,             // COST_OPERATING_PROFIT/COST_INCOME
	COST_ALL_TRANSPORTED,    // all transported goods
	COST_POWERLINES,         // revenue from the power grid
	COST_TRANSPORTED_PAS,    // number of passengers that actually reached destination
	COST_TRANSPORTED_MAIL,
	COST_TRANSPORTED_GOOD,
	COST_ALL_CONVOIS,        // number of convois
	COST_SCENARIO_COMPLETED, // scenario success (only useful if there is one ... )
	COST_WAY_TOLLS,
	COST_INTEREST,		// From extended
	COST_CREDIT_LIMIT	// From extended
	// OLD_MAX_PLAYER_COST = 21
};


void finance_t::export_to_cost_month(sint64 finance_history_month[][OLD_MAX_PLAYER_COST])
{
	calc_finance_history();
	for(int i=0; i<OLD_MAX_PLAYER_HISTORY_MONTHS; ++i){
		finance_history_month[i][COST_CONSTRUCTION] = veh_month[TT_ALL][i][ATV_CONSTRUCTION_COST];
		finance_history_month[i][COST_VEHICLE_RUN]  = veh_month[TT_ALL][i][ATV_RUNNING_COST] + veh_month[TT_ALL][i][ATV_VEHICLE_MAINTENANCE];
		finance_history_month[i][COST_NEW_VEHICLE]  = veh_month[TT_ALL][i][ATV_NEW_VEHICLE];
		finance_history_month[i][COST_INCOME]       = veh_month[TT_ALL][i][ATV_REVENUE_TRANSPORT];
		finance_history_month[i][COST_MAINTENANCE]  = veh_month[TT_ALL][i][ATV_INFRASTRUCTURE_MAINTENANCE];
		finance_history_month[i][COST_ASSETS]       = veh_month[TT_ALL][i][ATV_NON_FINANCIAL_ASSETS];
		finance_history_month[i][COST_CASH]         = com_month[i][ATC_CASH];
		finance_history_month[i][COST_NETWEALTH]    = com_month[i][ATC_NETWEALTH];
		finance_history_month[i][COST_PROFIT]       = veh_month[TT_ALL][i][ATV_PROFIT];
		finance_history_month[i][COST_OPERATING_PROFIT] = veh_month[TT_ALL][i][ATV_OPERATING_PROFIT];
		finance_history_month[i][COST_MARGIN]           = veh_month[TT_ALL][i][ATV_PROFIT_MARGIN];
		finance_history_month[i][COST_ALL_TRANSPORTED]  = 0;
		finance_history_month[i][COST_POWERLINES]       = veh_month[TT_POWERLINE][i][ATV_REVENUE];
		finance_history_month[i][COST_TRANSPORTED_PAS]  = 0;
		finance_history_month[i][COST_TRANSPORTED_MAIL] = 0;
		finance_history_month[i][COST_TRANSPORTED_GOOD] = 0;
		finance_history_month[i][COST_ALL_CONVOIS]      = veh_month[TT_ALL][i][ATV_CONVOIS];
		finance_history_month[i][COST_SCENARIO_COMPLETED] = com_month[i][ATC_SCENARIO_COMPLETED];
		finance_history_month[i][COST_WAY_TOLLS]        = veh_month[TT_ALL][i][ATV_WAY_TOLL];
		finance_history_month[i][COST_INTEREST]			= com_month[i][ATC_INTEREST];
		finance_history_month[i][COST_CREDIT_LIMIT]		= - com_month[i][ATC_SOFT_CREDIT_LIMIT]; // reversed sign
	}
}


void finance_t::export_to_cost_year( sint64 finance_history_year[][OLD_MAX_PLAYER_COST])
{
	calc_finance_history();
	for(int i=0; i<OLD_MAX_PLAYER_HISTORY_YEARS; ++i){
		finance_history_year[i][COST_CONSTRUCTION] = veh_year[TT_ALL][i][ATV_CONSTRUCTION_COST];
		finance_history_year[i][COST_VEHICLE_RUN]  = veh_year[TT_ALL][i][ATV_RUNNING_COST] + veh_month[TT_ALL][i][ATV_VEHICLE_MAINTENANCE];
		finance_history_year[i][COST_NEW_VEHICLE]  = veh_year[TT_ALL][i][ATV_NEW_VEHICLE];
		finance_history_year[i][COST_INCOME]       = veh_year[TT_ALL][i][ATV_REVENUE_TRANSPORT];
		finance_history_year[i][COST_MAINTENANCE]  = veh_year[TT_ALL][i][ATV_INFRASTRUCTURE_MAINTENANCE];
		finance_history_year[i][COST_ASSETS]       = veh_year[TT_ALL][i][ATV_NON_FINANCIAL_ASSETS];
		finance_history_year[i][COST_CASH]         = com_year[i][ATC_CASH];
		finance_history_year[i][COST_NETWEALTH]    = com_year[i][ATC_NETWEALTH];
		finance_history_year[i][COST_PROFIT]       = veh_year[TT_ALL][i][ATV_PROFIT];
		finance_history_year[i][COST_OPERATING_PROFIT] = veh_year[TT_ALL][i][ATV_OPERATING_PROFIT];
		finance_history_year[i][COST_MARGIN]           = veh_year[TT_ALL][i][ATV_PROFIT_MARGIN];
		finance_history_year[i][COST_ALL_TRANSPORTED]  = 0;
		finance_history_year[i][COST_POWERLINES]       = veh_year[TT_POWERLINE][i][ATV_REVENUE];
		finance_history_year[i][COST_TRANSPORTED_PAS]  = 0;
		finance_history_year[i][COST_TRANSPORTED_MAIL] = 0;
		finance_history_year[i][COST_TRANSPORTED_GOOD] = 0;
		finance_history_year[i][COST_ALL_CONVOIS]      = veh_year[TT_ALL][i][ATV_CONVOIS];
		finance_history_year[i][COST_SCENARIO_COMPLETED] = com_year[i][ATC_SCENARIO_COMPLETED];
		finance_history_year[i][COST_WAY_TOLLS]        = veh_year[TT_ALL][i][ATV_WAY_TOLL];
		finance_history_year[i][COST_INTEREST]			= com_year[i][ATC_INTEREST];
		finance_history_year[i][COST_CREDIT_LIMIT]		= - com_year[i][ATC_SOFT_CREDIT_LIMIT]; // reversed sign
	}
}


void finance_t::import_from_cost_month(const sint64 finance_history_month[][OLD_MAX_PLAYER_COST])
{
	// does it need initial clean-up ? (= initialization)
	for(int i=0; i<OLD_MAX_PLAYER_HISTORY_MONTHS; ++i){
		veh_month[TT_OTHER][i][ATV_CONSTRUCTION_COST] = finance_history_month[i][COST_CONSTRUCTION];
		veh_month[TT_ALL  ][i][ATV_CONSTRUCTION_COST] = finance_history_month[i][COST_CONSTRUCTION];
		veh_month[TT_OTHER][i][ATV_RUNNING_COST]      = finance_history_month[i][COST_VEHICLE_RUN];
		veh_month[TT_ALL  ][i][ATV_RUNNING_COST]      = finance_history_month[i][COST_VEHICLE_RUN];
		veh_month[TT_OTHER][i][ATV_NEW_VEHICLE]       = finance_history_month[i][COST_NEW_VEHICLE];
		veh_month[TT_ALL  ][i][ATV_NEW_VEHICLE]       = finance_history_month[i][COST_NEW_VEHICLE];
		// he have to store it in _GOOD for not being override in calc_finance history() to 0
		veh_month[TT_OTHER][i][ATV_REVENUE_GOOD]      = finance_history_month[i][COST_INCOME];
		veh_month[TT_ALL  ][i][ATV_REVENUE_GOOD]      = finance_history_month[i][COST_INCOME];
		veh_month[TT_OTHER][i][ATV_REVENUE_TRANSPORT]      = finance_history_month[i][COST_INCOME];
		veh_month[TT_ALL  ][i][ATV_REVENUE_TRANSPORT]      = finance_history_month[i][COST_INCOME];
		veh_month[TT_OTHER][i][ATV_INFRASTRUCTURE_MAINTENANCE] = finance_history_month[i][COST_MAINTENANCE];
		veh_month[TT_ALL  ][i][ATV_INFRASTRUCTURE_MAINTENANCE] = finance_history_month[i][COST_MAINTENANCE];
		veh_month[TT_OTHER][i][ATV_NON_FINANCIAL_ASSETS] = finance_history_month[i][COST_ASSETS];
		veh_month[TT_ALL  ][i][ATV_NON_FINANCIAL_ASSETS] = finance_history_month[i][COST_ASSETS];
		com_month[i][ATC_CASH]                        = finance_history_month[i][COST_CASH];
		com_month[i][ATC_NETWEALTH]                   = finance_history_month[i][COST_NETWEALTH];
		veh_month[TT_OTHER][i][ATV_PROFIT]            = finance_history_month[i][COST_PROFIT];
		veh_month[TT_ALL  ][i][ATV_PROFIT]            = finance_history_month[i][COST_PROFIT];
		veh_month[TT_OTHER][i][ATV_OPERATING_PROFIT]  = finance_history_month[i][COST_OPERATING_PROFIT];
		veh_month[TT_ALL  ][i][ATV_OPERATING_PROFIT]  = finance_history_month[i][COST_OPERATING_PROFIT];
		veh_month[TT_ALL  ][i][ATV_PROFIT_MARGIN]     = finance_history_month[i][COST_MARGIN]; // this needs to be recalculate before usage
		veh_month[TT_OTHER][i][ATV_CONVOIS]           = finance_history_month[i][COST_ALL_CONVOIS];
		veh_month[TT_ALL  ][i][ATV_CONVOIS]           = finance_history_month[i][COST_ALL_CONVOIS];
		veh_month[TT_POWERLINE][i][ATV_REVENUE]       = finance_history_month[i][COST_POWERLINES];
		veh_month[TT_OTHER][i][ATV_VEHICLES]        = 0;
		veh_month[TT_ALL  ][i][ATV_VEHICLES]        = 0;
		veh_month[TT_OTHER][i][ATV_CONVOY_DISTANCE] = 0;
		veh_month[TT_ALL  ][i][ATV_CONVOY_DISTANCE] = 0;
		veh_month[TT_OTHER][i][ATV_VEHICLE_DISTANCE]= 0;
		veh_month[TT_ALL  ][i][ATV_VEHICLE_DISTANCE]= 0;
		com_month[i][ATC_HALTS]                     = 0;
		com_month[i][ATC_SCENARIO_COMPLETED]          = finance_history_month[i][COST_SCENARIO_COMPLETED];
		if(finance_history_month[i][COST_WAY_TOLLS] > 0 ){
			veh_month[TT_OTHER][i][ATV_TOLL_RECEIVED] = finance_history_month[i][COST_WAY_TOLLS];
			veh_month[TT_ALL  ][i][ATV_TOLL_RECEIVED] = finance_history_month[i][COST_WAY_TOLLS];
		}
		else{
			veh_month[TT_OTHER][i][ATV_TOLL_PAID] = finance_history_month[i][COST_WAY_TOLLS];
			veh_month[TT_ALL  ][i][ATV_TOLL_PAID] = finance_history_month[i][COST_WAY_TOLLS];
		}
		veh_month[TT_OTHER][i][ATV_WAY_TOLL] = finance_history_month[i][COST_WAY_TOLLS];
		veh_month[TT_ALL  ][i][ATV_WAY_TOLL] = finance_history_month[i][COST_WAY_TOLLS];
		com_month[i][ATC_INTEREST] = finance_history_month[i][COST_INTEREST];
		com_month[i][ATC_SOFT_CREDIT_LIMIT] = - finance_history_month[i][COST_CREDIT_LIMIT]; // reversed sign
		com_month[i][ATC_HARD_CREDIT_LIMIT] = - 2 * finance_history_month[i][COST_CREDIT_LIMIT]; // reversed sign, doubled
	}
}


void finance_t::import_from_cost_year( const sint64 finance_history_year[][OLD_MAX_PLAYER_COST])
{
	for(int i=0; i<OLD_MAX_PLAYER_HISTORY_YEARS; ++i){
		veh_year[TT_OTHER][i][ATV_CONSTRUCTION_COST] = finance_history_year[i][COST_CONSTRUCTION];
		veh_year[TT_ALL  ][i][ATV_CONSTRUCTION_COST] = finance_history_year[i][COST_CONSTRUCTION];
		veh_year[TT_OTHER][i][ATV_RUNNING_COST]      = finance_history_year[i][COST_VEHICLE_RUN];
		veh_year[TT_ALL  ][i][ATV_RUNNING_COST]      = finance_history_year[i][COST_VEHICLE_RUN];
		veh_year[TT_OTHER][i][ATV_NEW_VEHICLE]       = finance_history_year[i][COST_NEW_VEHICLE];
		veh_year[TT_ALL  ][i][ATV_NEW_VEHICLE]       = finance_history_year[i][COST_NEW_VEHICLE];
		// we have to store it in _GOOD for not being override in calc_finance history() to 0
		veh_year[TT_OTHER][i][ATV_REVENUE_GOOD]      = finance_history_year[i][COST_INCOME];
		veh_year[TT_ALL  ][i][ATV_REVENUE_GOOD]      = finance_history_year[i][COST_INCOME];
		veh_year[TT_OTHER][i][ATV_REVENUE_TRANSPORT]      = finance_history_year[i][COST_INCOME];
		veh_year[TT_ALL  ][i][ATV_REVENUE_TRANSPORT]      = finance_history_year[i][COST_INCOME];
		veh_year[TT_OTHER][i][ATV_INFRASTRUCTURE_MAINTENANCE] = finance_history_year[i][COST_MAINTENANCE];
		veh_year[TT_ALL  ][i][ATV_INFRASTRUCTURE_MAINTENANCE] = finance_history_year[i][COST_MAINTENANCE];
		veh_year[TT_OTHER][i][ATV_NON_FINANCIAL_ASSETS] = finance_history_year[i][COST_ASSETS];
		veh_year[TT_ALL  ][i][ATV_NON_FINANCIAL_ASSETS] = finance_history_year[i][COST_ASSETS];
		com_year[i][ATC_CASH]                        = finance_history_year[i][COST_CASH];
		com_year[i][ATC_NETWEALTH]                   = finance_history_year[i][COST_NETWEALTH];
		veh_year[TT_OTHER][i][ATV_PROFIT]            = finance_history_year[i][COST_PROFIT];
		veh_year[TT_ALL  ][i][ATV_PROFIT]            = finance_history_year[i][COST_PROFIT];
		veh_year[TT_OTHER][i][ATV_OPERATING_PROFIT]  = finance_history_year[i][COST_OPERATING_PROFIT];
		veh_year[TT_ALL  ][i][ATV_OPERATING_PROFIT]  = finance_history_year[i][COST_OPERATING_PROFIT];
		veh_year[TT_ALL  ][i][ATV_PROFIT_MARGIN]     = finance_history_year[i][COST_MARGIN]; // this needs to be recalculate before usage
		// we have to store it in ATV_TRANSPORTED_GOOD, otherwise calc_finance_history will set ATV_TRANSPORTED to 0
		veh_year[TT_OTHER][i][ATV_TRANSPORTED_GOOD]  = finance_history_year[i][COST_ALL_TRANSPORTED];
		veh_year[TT_ALL  ][i][ATV_TRANSPORTED_GOOD]  = finance_history_year[i][COST_ALL_TRANSPORTED];
		veh_year[TT_OTHER][i][ATV_CONVOIS]           = finance_history_year[i][COST_ALL_CONVOIS];
		veh_year[TT_ALL  ][i][ATV_CONVOIS]           = finance_history_year[i][COST_ALL_CONVOIS];
		veh_year[TT_POWERLINE][i][ATV_REVENUE]       = finance_history_year[i][COST_POWERLINES];
		veh_year[TT_OTHER][i][ATV_VEHICLES]          = 0;
		veh_year[TT_ALL  ][i][ATV_VEHICLES]          = 0;
		veh_year[TT_OTHER][i][ATV_CONVOY_DISTANCE]   = 0;
		veh_year[TT_ALL  ][i][ATV_CONVOY_DISTANCE]   = 0;
		veh_year[TT_OTHER][i][ATV_VEHICLE_DISTANCE]  = 0;
		veh_year[TT_ALL  ][i][ATV_VEHICLE_DISTANCE]  = 0;
		com_year[i][ATC_HALTS]                       = 0;
		com_year[i][ATC_SCENARIO_COMPLETED]          = finance_history_year[i][COST_SCENARIO_COMPLETED];
		if(finance_history_year[i][COST_WAY_TOLLS] > 0 ){
			veh_year[TT_OTHER][i][ATV_TOLL_RECEIVED] = finance_history_year[i][COST_WAY_TOLLS];
			veh_year[TT_ALL  ][i][ATV_TOLL_RECEIVED] = finance_history_year[i][COST_WAY_TOLLS];
		}
		else{
			veh_year[TT_OTHER][i][ATV_TOLL_PAID] = finance_history_year[i][COST_WAY_TOLLS];
			veh_year[TT_ALL  ][i][ATV_TOLL_PAID] = finance_history_year[i][COST_WAY_TOLLS];
		}
		veh_year[TT_OTHER][i][ATV_WAY_TOLL] = finance_history_year[i][COST_WAY_TOLLS];
		veh_year[TT_ALL  ][i][ATV_WAY_TOLL] = finance_history_year[i][COST_WAY_TOLLS];
		com_year[i][ATC_INTEREST] = finance_history_year[i][COST_INTEREST];
		com_year[i][ATC_SOFT_CREDIT_LIMIT] = - finance_history_year[i][COST_CREDIT_LIMIT]; // reversed sign
		com_year[i][ATC_HARD_CREDIT_LIMIT] = - 2 * finance_history_year[i][COST_CREDIT_LIMIT]; // reversed sign, doubled
	}
}


void finance_t::rdwr_compatibility(loadsave_t *file)
{
	sint64 finance_history_year[OLD_MAX_PLAYER_HISTORY_YEARS][OLD_MAX_PLAYER_COST];
	sint64 finance_history_month[OLD_MAX_PLAYER_HISTORY_MONTHS][OLD_MAX_PLAYER_COST];

	for (int year=0; year<OLD_MAX_PLAYER_HISTORY_YEARS; year++) {
		for (int cost_type=0; cost_type<OLD_MAX_PLAYER_COST; cost_type++) {
			finance_history_year[year][cost_type] = 0;
			if ((cost_type == COST_CASH) || (cost_type == COST_NETWEALTH)) {
				finance_history_year[year][cost_type] = get_starting_money();
			}
		}
	}

	for (int month=0; month<OLD_MAX_PLAYER_HISTORY_MONTHS; month++) {
		for (int cost_type=0; cost_type<OLD_MAX_PLAYER_COST; cost_type++) {
			finance_history_month[month][cost_type] = 0;
			if ((cost_type == COST_CASH) || (cost_type == COST_NETWEALTH)) {
				finance_history_month[month][cost_type] = get_starting_money();
			}
		}
	}

	if( ( file->is_version_less(112, 5) ) && ( ! file->is_loading() ) ) { // for saving of game in old format
		export_to_cost_month( finance_history_month );
		export_to_cost_year( finance_history_year );
	}
	if (file->is_version_less(84, 8)) {
		// not so old save game
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<OLD_MAX_PLAYER_COST; cost_type++) {
				if (file->get_version_int() < 84007) {
					// a cost_type has has been added. For old savegames we only have 9 cost_types, now we have 10.
					// for old savegames only load 9 types and calculate the 10th; for new savegames load all 10 values
					if (cost_type < 9) {
						file->rdwr_longlong(finance_history_year[year][cost_type]);
					}
				} else {
					if (cost_type < 10) {
						file->rdwr_longlong(finance_history_year[year][cost_type]);
					}
				}
			}
		}
	}
	else if (file->is_version_less(86, 0)) {
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<10; cost_type++) {
				file->rdwr_longlong(finance_history_year[year][cost_type]);
			}
		}
		// in 84008 monthly finance history was introduced
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<10; cost_type++) {
				file->rdwr_longlong(finance_history_month[month][cost_type]);
			}
		}
	}
	else if (file->is_version_less(99, 11)) {
		// powerline category missing
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<12; cost_type++) {
				file->rdwr_longlong(finance_history_year[year][cost_type]);
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<12; cost_type++) {
				file->rdwr_longlong(finance_history_month[month][cost_type]);
			}
		}
	}
	else if (file->is_version_less(99, 17)) {
		// without detailed goo statistics
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<13; cost_type++) {
				file->rdwr_longlong(finance_history_year[year][cost_type]);
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<13; cost_type++) {
				file->rdwr_longlong(finance_history_month[month][cost_type]);
			}
		}
	}
	else if(  file->is_version_less(102, 3) && file->get_extended_version() <= 1  ) {
		// saved everything
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<18; cost_type++) {
				file->rdwr_longlong(finance_history_year[year][cost_type]);
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<18; cost_type++) {
				file->rdwr_longlong(finance_history_month[month][cost_type]);
			}
		}
	}
	else if(  file->get_version_int()<=102002  ) {
		// saved everything
		// Extended had INTEREST, CREDIT_LIMIT
		for (int year = 0;year<OLD_MAX_PLAYER_HISTORY_YEARS;year++) {
			for (int cost_type = 0; cost_type<21; cost_type++) {
				if (cost_type != COST_WAY_TOLLS) {
					file->rdwr_longlong(finance_history_year[year][cost_type]);
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<21; cost_type++) {
				if (cost_type != COST_WAY_TOLLS) {
					file->rdwr_longlong(finance_history_month[month][cost_type]);
				}
			}
		}
	}
	else if(  file->is_version_less(110, 7)  && file->get_extended_version()==0  ) {
		// only save what is needed
		// no way tolls
		for(int year = 0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			for(  int cost_type = 0;   cost_type<18;   cost_type++  ) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_year[year][cost_type]);
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<18; cost_type++) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_month[month][cost_type]);
				}
			}
		}
	}
	/* Note that extended did not adopt way tolls until version 11
	 * As a result the logic for version <=110006 for extended can fall through to the
	 * logic for version <= 112004
	 */
	else if (  file->is_version_less(112, 5)  && file->get_extended_version() == 0  ) {
		// savegame version: now with toll
		for(int year = 0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			for(  int cost_type = 0;   cost_type<19;   cost_type++  ) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_year[year][cost_type]);
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<19; cost_type++) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_month[month][cost_type]);
				}
			}
		}
	}
	else if (  file->get_version_int() <= 112004  && file->get_extended_version() == 1  ) {
		// is this combination even possible?  I doubt it
		// no way tolls in extended despite being in standard
		// no interest or credit limit in extended
		for(int year = 0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			for(  int cost_type = 0;   cost_type<18;   cost_type++  ) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_year[year][cost_type]);
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<18; cost_type++) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_month[month][cost_type]);
				}
			}
		}
	}
	else if (  file->get_version_int() <= 112004 && file->get_extended_version() <= 10  ) {
		// Standard had way tolls, extended still didn't
		// Extended also had INTEREST, CREDIT_LIMIT
		for(int year = 0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			for(  int cost_type = 0;   cost_type<21;   cost_type++  ) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					if (cost_type != COST_WAY_TOLLS) {
						file->rdwr_longlong(finance_history_year[year][cost_type]);
					}
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<21; cost_type++) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					if (cost_type != COST_WAY_TOLLS) {
						file->rdwr_longlong(finance_history_month[month][cost_type]);
					}
				}
			}
		}
	}
	else if (  file->get_version_int() <= 112004  ) {
		// Extended version 11 with old save file format
		// May happen in files saved with some development versions
		// Extended has WAY_TOLLS, INTEREST, CREDIT_LIMIT
		for(int year = 0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			for(  int cost_type = 0;   cost_type<21;   cost_type++  ) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_year[year][cost_type]);
				}
			}
		}
		for (int month = 0;month<OLD_MAX_PLAYER_HISTORY_MONTHS;month++) {
			for (int cost_type = 0; cost_type<21; cost_type++) {
				if(  cost_type<COST_NETWEALTH  ||  cost_type>COST_MARGIN  ) {
					file->rdwr_longlong(finance_history_month[month][cost_type]);
				}
			}
		}
	}
	else if (  file->get_version_int() >= 112005  ) {
		// We should not get here in compatibility loading mode
		assert(false);
	}

	if(  file->is_version_atleast(102, 3) && file->get_extended_version() != 7  ) {
		file->rdwr_longlong(starting_money);
	}

	// we have to pay maintenance at the beginning of a month
	if(file->is_version_less(99, 18)  &&  file->is_loading()) {
		finance_history_month[0][COST_MAINTENANCE] -= finance_history_month[1][COST_MAINTENANCE];
		finance_history_year [0][COST_MAINTENANCE] -= finance_history_month[1][COST_MAINTENANCE];
		set_account_balance(get_account_balance() - finance_history_month[1][COST_MAINTENANCE]);
	}

	if(file->is_loading()) {

		/* prior versions calculated margin incorrectly.
		 * we also save only some values and recalculate all dependent ones
		 * (remember: negative costs are just saved as negative numbers!)
		 */
		for(  int year=0;  year<OLD_MAX_PLAYER_HISTORY_YEARS;  year++  ) {
			finance_history_year[year][COST_NETWEALTH] = finance_history_year[year][COST_CASH]+finance_history_year[year][COST_ASSETS];
			// only revenue minus running costs
			finance_history_year[year][COST_OPERATING_PROFIT] = finance_history_year[year][COST_INCOME] + finance_history_year[year][COST_POWERLINES] + finance_history_year[year][COST_VEHICLE_RUN] + finance_history_year[year][COST_MAINTENANCE] + finance_history_year[year][COST_WAY_TOLLS];

			// including also investments into vehicles/infrastructure
			finance_history_year[year][COST_PROFIT] = finance_history_year[year][COST_OPERATING_PROFIT]+finance_history_year[year][COST_CONSTRUCTION]+finance_history_year[year][COST_NEW_VEHICLE]+finance_history_year[year][COST_INTEREST];
			finance_history_year[year][COST_MARGIN] = calc_margin(finance_history_year[year][COST_OPERATING_PROFIT], finance_history_year[year][COST_INCOME]);
		}
		for(  int month=0;  month<OLD_MAX_PLAYER_HISTORY_MONTHS;  month++  ) {
			finance_history_month[month][COST_NETWEALTH] = finance_history_month[month][COST_CASH]+finance_history_month[month][COST_ASSETS];
			finance_history_month[month][COST_OPERATING_PROFIT] = finance_history_month[month][COST_INCOME] + finance_history_month[month][COST_POWERLINES] + finance_history_month[month][COST_VEHICLE_RUN] + finance_history_month[month][COST_MAINTENANCE] + finance_history_month[month][COST_WAY_TOLLS];
			finance_history_month[month][COST_PROFIT] = finance_history_month[month][COST_OPERATING_PROFIT]+finance_history_month[month][COST_CONSTRUCTION]+finance_history_month[month][COST_NEW_VEHICLE]+finance_history_month[month][COST_INTEREST];
			finance_history_month[month][COST_MARGIN] = calc_margin(finance_history_month[month][COST_OPERATING_PROFIT], finance_history_month[month][COST_INCOME]);
		}

		// now import the statistics in old format to the new one
		import_from_cost_month(finance_history_month);
		import_from_cost_year(finance_history_year);

	}
}
