//
// This file is part of the Simutrans-Extended project under the Artistic License.
// (see LICENSE.txt)
//


//
// Tests for player_x stuff
//


function test_player_cash()
{
	local pl = player_x(0)

	ASSERT_EQUAL(pl.get_current_cash(),        200000)     // get_current_cash is in credits (returns float)
	ASSERT_EQUAL(pl.get_current_net_wealth(),  200000*100) // get_current_net_wealth is in 1/100 credits
	ASSERT_EQUAL(pl.get_current_maintenance(), 0)

	local ok = pl.book_cash(0)
	ASSERT_EQUAL(ok, true)
	ASSERT_EQUAL(pl.get_current_cash(),       200000)
	ASSERT_EQUAL(pl.get_current_net_wealth(), 200000 * 100)
	ASSERT_EQUAL(pl.get_current_maintenance(), 0)

	ok = pl.book_cash(-200000 * 100)               // book_cash is in 1/100 credits
	ASSERT_EQUAL(ok, true)
	ASSERT_EQUAL(pl.get_current_cash(), 0)
	ASSERT_EQUAL(pl.get_current_net_wealth(), 0)
	ASSERT_EQUAL(pl.get_current_maintenance(), 0)

	ok = pl.book_cash(-1)
	ASSERT_EQUAL(ok, true)
	ASSERT_EQUAL(pl.get_current_cash(), -0.01)
	ASSERT_EQUAL(pl.get_current_net_wealth(), -1)
	ASSERT_EQUAL(pl.get_current_maintenance(), 0)

	// clean up
	ok = pl.book_cash(200000 * 100 + 1)
	ASSERT_EQUAL(ok, true)
	ASSERT_EQUAL(pl.get_current_cash(),       200000)
	ASSERT_EQUAL(pl.get_current_net_wealth(), 200000 * 100)
	ASSERT_EQUAL(pl.get_current_maintenance(), 0)

	RESET_ALL_PLAYER_FUNDS()
}


function test_player_isactive()
{
	local pl = player_x(0)
	ASSERT_EQUAL(pl.is_valid(), true)
	ASSERT_EQUAL(pl.is_active(), true)

	local public_pl = player_x(1)
	ASSERT_EQUAL(public_pl.is_valid(), true)
	ASSERT_EQUAL(public_pl.is_active(), true)

	local nonexist = player_x(2)
	ASSERT_EQUAL(nonexist.is_valid(), false)
	ASSERT_EQUAL(nonexist.is_active(), false)

	RESET_ALL_PLAYER_FUNDS()
}


function test_player_headquarters()
{
	local pl = player_x(0)
	ASSERT_EQUAL(pl.get_headquarter_level(), 0)
	ASSERT_EQUAL(pl.get_headquarter_pos().tostring(), coord(-1, -1).tostring())

	RESET_ALL_PLAYER_FUNDS()
}


function test_player_name()
{
	local pl = player_x(0)

	ASSERT_EQUAL(pl.get_name(), "Default player")
	ASSERT_TRUE(pl.set_name("Foo"))
	ASSERT_EQUAL(pl.get_name(), "Foo")
	ASSERT_TRUE(pl.set_name("Foo"))
	ASSERT_EQUAL(pl.get_name(), "Foo")

	// clean up
	ASSERT_TRUE(pl.set_name("Default player"))

	RESET_ALL_PLAYER_FUNDS()
}


function test_player_finance_history()
{
	local pl = player_x(0)

	local check_equal = function(a, b) {
		ASSERT_EQUAL(a.len(), b.len())
		for (local i = 0; i < a.len(); i++) {
			ASSERT_EQUAL(a[i], b[i])
		}
	}

	// The generic accessors return one entry per month and one per year.
	// Index 0 is the current month or year.
	ASSERT_EQUAL(pl.get_common_history(atc_cash, true).len(), 26)
	ASSERT_EQUAL(pl.get_common_history(atc_cash, false).len(), 26)
	ASSERT_EQUAL(pl.get_vehicle_history(atv_non_financial_assets, tt_all, true).len(), 26)
	ASSERT_EQUAL(pl.get_vehicle_history(atv_non_financial_assets, tt_all, false).len(), 26)

	// The generic accessors agree with the named getters.
	check_equal(pl.get_common_history(atc_cash, true), pl.get_cash())
	check_equal(pl.get_common_history(atc_net_wealth, true), pl.get_net_wealth())
	check_equal(pl.get_vehicle_history(atv_non_financial_assets, tt_all, true), pl.get_assets())
	check_equal(pl.get_vehicle_history(atv_revenue_transport, tt_all, true), pl.get_income())

	// The named accrual getters read the whole-company arrays.
	check_equal(pl.get_interest(), pl.get_common_history(atc_interest, true))
	check_equal(pl.get_tax(), pl.get_common_history(atc_tax, true))
	check_equal(pl.get_depreciation(), pl.get_common_history(atc_depreciation, true))
	check_equal(pl.get_loan_repayment(), pl.get_common_history(atc_loan_repayment, true))
	check_equal(pl.get_tax_allowance(), pl.get_common_history(atc_tax_allowance, true))
	check_equal(pl.get_secured_debt(), pl.get_common_history(atc_secured_debt, true))
	check_equal(pl.get_soft_credit_limit(), pl.get_common_history(atc_soft_credit_limit, true))
	check_equal(pl.get_hard_credit_limit(), pl.get_common_history(atc_hard_credit_limit, true))

	// get_vehicle_maint is the historical name for the running-cost series.
	// The monthly maintenance charge has its own series.
	check_equal(pl.get_vehicle_maint(), pl.get_running_costs())
	check_equal(pl.get_vehicle_maintenance(),
		pl.get_vehicle_history(atv_vehicle_maintenance, tt_all, true))

	// The margin series is read from the per-transport-type arrays. A
	// non-empty result confirms that the ATV index is looked up in the ATV
	// range rather than in the ATC range.
	ASSERT_EQUAL(pl.get_margin().len(), 26)

	// The accrual profit is available for both periods.
	ASSERT_EQUAL(pl.get_profit_after_depreciation_and_tax(true).len(), 26)
	ASSERT_EQUAL(pl.get_profit_after_depreciation_and_tax(false).len(), 26)

	// Invalid indices and transport types return an empty array.
	ASSERT_EQUAL(pl.get_common_history(-1, true).len(), 0)
	ASSERT_EQUAL(pl.get_common_history(1000, true).len(), 0)
	ASSERT_EQUAL(pl.get_vehicle_history(-1, tt_all, true).len(), 0)
	ASSERT_EQUAL(pl.get_vehicle_history(atv_revenue_transport, 1000, true).len(), 0)

	RESET_ALL_PLAYER_FUNDS()
}


function test_player_lines()
{
	local pl = player_x(0)

	// empty line list
	{
		local line_list = pl.get_line_list()
		ASSERT_TRUE(line_list != null)
		ASSERT_EQUAL(line_list.get_count(), 0)
	}

	// non-empty line list
	{
		ASSERT_TRUE(pl.create_line(wt_road))

		local line_list = pl.get_line_list()
		ASSERT_TRUE(line_list != null)
		ASSERT_EQUAL(line_list.get_count(), 1)

		foreach (line in line_list) {
			ASSERT_TRUE(line != null)
			ASSERT_EQUAL(line.is_valid(), true)
			ASSERT_EQUAL(line.get_waytype(), wt_road)
		}

		foreach (line in line_list) {
			ASSERT_TRUE(line.destroy(pl))
		}

		ASSERT_EQUAL(pl.get_line_list().get_count(), 0)
	}

	// TODO Destroy lines owned by other players

	// clean up
	RESET_ALL_PLAYER_FUNDS()
}

