---
status: draft
verified: cost-of-capital @ d80af444a
---
# Cost of capital

**Covers:** the capital-accounting and borrowing model: `player/finance.{h,cc}`,
`player/loan.{h,cc}`, the finance parts of `player/simplay.cc`, `dataobj/settings.{h,cc}`,
`simutrans/config/simuconf.tab`, `gui/loan_frame.{h,cc}`, `gui/money_frame.{h,cc}`,
`simtool.cc` (`tool_change_player_t`) and `script/api/api_player.cc`. The way-maintenance
booking paths are in [way-maintenance-accounting](way-maintenance-accounting.md); the way
renewal classifier is in
[way-renewal-classification](cost-of-capital/way-renewal-classification.md).

## Terms

Each term below has one meaning throughout this document.

- **Overdraft**: the negative balance of `finance_t::account_balance`. It is unsecured and has
  no fixed term.
- **Term Instrument**: one bank loan or one bond, represented by a `loan_t`. Its principal is
  borrowed at drawdown, the month in which the money is paid to the company, and repaid under
  a fixed schedule.
- **Secured Debt**: the sum of the outstanding principal of every Term Instrument
  (`finance_t::get_total_secured_debt`). The Overdraft is not Secured Debt.
- **Asset Ledger**: the book value of a company's infrastructure, held per transport type in
  `finance_t::asset_value`. It excludes vehicles.
- **Credit Asset Value**: the Asset Ledger plus the sale value of the company's vehicles
  (`finance_t::get_credit_asset_value`). It is the security offered for Term Instruments.
- **Secured Gearing**: Secured Debt divided by Credit Asset Value.
- **Term-Debt Limit**: the greatest principal that the company may owe in Term Instruments
  (`finance_t::get_term_debt_limit`).
- **Base Rate**: the `base_rate` series in `config/prices.tab`, expressed in per cent.
- **Rating Spread**: the addition to the Base Rate for the company's Credit Rating
  (`settings_t::credit_spread_bp`).
- **Term Lending Margin**: the fixed addition to the Base Rate for every Term Instrument
  (`settings_t::term_lending_margin_bp`).
- **Term Offset**: the addition to the Base Rate for the Term Instrument's term
  (`settings_t::term_offset_bp`, indexed by `loan_term_t`).
- **Secured-Gearing Premium**: the addition to the Base Rate for post-loan Secured Gearing
  (`settings_t::secured_gearing_premium`).
- **Credit Rating**: one of nine bands from `AAA` (best) to `C` (worst) that sets the Rating
  Spread and the terms that may be issued (`finance_t::assess_credit_rating`).
- **Debt Service**: the interest plus principal falling due over a stated period
  (`loan_t::get_debt_service_for_next_months`).
- **Debt-Service Cover**: the operating profit of the previous twelve months divided by the
  Debt Service of the next twelve months.
- **Tax Written-Down Pool**: the capital expenditure not yet allowed against corporation tax,
  held per transport type in `finance_t::tax_pool`.
- **Revenue Renewal**: the part of a way replacement cost that restores the replaced way's
  existing capability.
- **Capital Improvement**: the part of a way replacement cost that exceeds Revenue Renewal.
- **Way Descriptor**: the pakset definition of a way type, represented by `way_desc_t`.
- **Upgrade Group**: the pakset grouping of interchangeable Way Descriptors, read by
  `obj_base_desc_t::get_upgrade_group`. A replacement within one group is charged the way-only
  cost, which excludes land.
- **Mothballed**: marked as out of use, read by `way_desc_t::is_mothballed`. A Mothballed Way
  Descriptor has no cost, speed limit or maintenance.
- **Make-Whole Settlement**: the amount paid to end a Term Instrument before its maturity.
- **Money Message**: the floating amount that `player_t::add_money_message` displays over the
  place where money was spent.

Administration and Liquidation are the two insolvency states of `player_t::solvency_status`;
[economy-and-passengers](economy-and-passengers.md) describes when each applies.

## Overview

The model gives each company a Credit Rating, a Term-Debt Limit and Term Instruments, and
values the company's infrastructure so that borrowing can be secured against it. Money is in
Simucents (1/100 Simucredit); rates are in basis points (100 basis points = one percentage
point); all arithmetic is integer.

## The asset ledger

`finance_t::asset_value` has one entry per `transport_type` (`TT_MAX` entries). It holds the
book value of the company's infrastructure of that transport type. It excludes vehicles, which
are valued separately by their sale value in `ATV_NON_FINANCIAL_ASSETS`
(`player_t::calc_assets` computes that value at every month change).

**Booking.** Capital expenditure adds to the Asset Ledger and money received on removal
reduces it:

- `finance_t::book_construction_costs` calls `book_infrastructure_capital(-amount, wt)`, so a
  construction cost, which is a negative amount, adds its absolute value; a positive amount
  (money received on removal) reduces the Asset Ledger.
- `finance_t::book_new_vehicle` calls `book_vehicle_capital(-amount, wt)`, which affects only
  the Tax Written-Down Pool: a vehicle purchase adds its price, and a sale reduces the Tax
  Written-Down Pool by the proceeds.

Both `book_infrastructure_capital` and `book_vehicle_capital` floor their pool at zero for
each transport type.

**Depreciation.** `finance_t::depreciate_assets_monthly` runs at the end of
`finance_t::new_month`. For each transport type it reduces `asset_value[i]` by
`asset_value[i] * asset_depreciation_percent[i] / 1200`, that is the configured annual
percentage divided by twelve. The charge is recorded as a negative amount in
`ATC_DEPRECIATION` in both the monthly and yearly history. It has no cash effect.

**Credit Asset Value.** `finance_t::get_credit_asset_value` adds the Asset Ledger of every
transport type to `ATV_NON_FINANCIAL_ASSETS`. It is the denominator of Secured Gearing, it
forms the asset part of the leverage ratio, and it is the base of the Term-Debt Limit.

**Old savegames.** A savegame written before Extended save revision 18 (see
[savegame-versioning](savegame-versioning.md)) has no Asset Ledger. `finance_t::rdwr` then
sets `asset_ledger_needs_initialisation` and `player_t::finish_rd` calls
`finance_t::initialise_legacy_asset_ledger`, which sets each transport type's Asset Ledger to
`ATV_WAY_LENGTH[tt] * legacy_way_asset_cost[tt] / 10`. The way length statistic counts 10
units for a straight tile and 7 for a diagonal tile, so the setting is the as-new value of one
straight tile. A zero setting disables the approximation and leaves that transport type at
zero. Buildings and other fixed assets are not reconstructed.

## The tax written-down pool

`finance_t::tax_pool` mirrors the Asset Ledger per transport type. It receives the same
additions and removals for infrastructure, and it also receives vehicle purchases at their
price and vehicle sales at their proceeds.

`finance_t::book_tax_allowance_monthly` reduces each pool by
`tax_pool[i] * tax_allowance_percent[i] / 1200` and accumulates the total as a positive amount
in `ATC_TAX_ALLOWANCE` in the monthly and yearly history.

`finance_t::book_corporation_tax_monthly` charges tax monthly on the previous year's figures,
with a tax base of `veh_year[TT_ALL][1][ATV_PROFIT] - capital_expenditure - tax_allowance`,
where

- `capital_expenditure` is the sum of `ATV_CONSTRUCTION_COST` and `ATV_NEW_VEHICLE` for the
  previous year, both negative, and
- `tax_allowance` is `com_year[1][ATC_TAX_ALLOWANCE]`.

Adding capital expenditure back to `ATV_PROFIT` and then deducting the pooled allowance is the
whole of the tax model: there is no deferred tax and no balancing charge.

## Accounting entries

Four entries are appended to `accounting_type_common`:

| Entry | Meaning |
|---|---|
| `ATC_LOAN_REPAYMENT` | principal repaid on Term Instruments, a negative cash-flow line |
| `ATC_DEPRECIATION` | pooled depreciation charge, with no cash effect |
| `ATC_SECURED_DEBT` | Secured Debt, written by `calc_finance_history` |
| `ATC_TAX_ALLOWANCE` | pooled tax allowance for the period |

The history arrays are written with their own dimension counts (`max_atc`, `max_atv`), and a
reader that knows fewer entries stores the surplus in a dummy variable, so appending entries
does not break forward or backward compatibility.

## Interest rates

All rates are integer basis points. `finance_t::get_overdraft_rate_bp` and
`finance_t::get_term_rate_bp` both start from the Base Rate;
`karte_t::get_inflation_adjusted_price` returns the value at the current timeline month,
falling back to the `general` series when `config/prices.tab` defines no `base_rate` series
(the conversion mechanism is described in
[economy-and-passengers](economy-and-passengers.md)).

```
Overdraft rate = Base Rate + `overdraft_percent_above_base_rate` + Rating Spread
Term rate      = Base Rate + Term Lending Margin + Term Offset + Rating Spread
                 + Secured-Gearing Premium
```

- `finance_t::book_interest_monthly` charges the Overdraft at
  `overdraft_rate_bp * balance / 120000`. This is the same calculation as dividing a rate in
  per cent by 1200, because 100 basis points make one per cent.
- `finance_t::get_secured_gearing_premium_bp` computes post-loan Secured Gearing as
  `(Secured Debt + new principal) * 100 / Credit Asset Value` and returns the premium of the
  first band in `settings_t::secured_gearing_premium` whose `max_gearing_percent` is at least
  that value. When the Credit Asset Value is zero every Term Instrument pays the last band's
  premium; the Term-Debt Limit is what prevents borrowing in that case.
- `finance_t::is_term_available` applies the rule that depends on the Credit Rating: `CCC` and
  below may issue nothing, `B` may use 1, 2 and 5 years, `BB` may use up to 10 years, and
  `BBB` or better may use every term, with the 25-year term restricted to bonds.

## The credit rating

The bands, best first, are `AAA`, `AA`, `A`, `BBB`, `BB`, `B`, `CCC`, `CC`, `C`
(`credit_rating_t`). `finance_t::assess_credit_rating` returns a band and
`finance_t::update_credit_rating` stores it together with the assessment month.

Inputs:

- **Debt-Service Cover** is the sum of `ATV_OPERATING_PROFIT` over the last twelve months
  divided by the Debt Service of the next twelve months, expressed in hundredths. The Debt
  Service of each Term Instrument comes from `loan_t::get_debt_service_for_next_months`, which
  projects the contract forward month by month.
- **Leverage** is total debt divided by total debt plus equity, where total debt is Secured
  Debt plus any Overdraft and equity is cash plus the Credit Asset Value less debt. The
  denominator therefore reduces to cash plus the Credit Asset Value.

The company takes the worse of the two bands. A company with no Debt Service is placed in the
best band on the cover test. Two states override the calculation:

- a company in Administration is rated `CC` and may not issue new Term Instruments;
- a company in Liquidation is rated `C` and may not borrow at all.

A company younger than `new_company_credit_rating_months` is rated
`new_company_credit_rating`, which defaults to `BB` for 24 months, unless it is insolvent.
The public service player (`player_t::is_public_service`) is exempt from the insolvency
override.

`finance_t::issue_loan` and `finance_t::refinance_loan` price a Term Instrument from a
provisional assessment that includes the proposed principal and its projected Debt Service, so
a new Term Instrument is priced at the Credit Rating the company would have after drawing it
down. `finance_t::refinance_loan` removes the replaced Term Instrument's Debt Service from the
assessment instead of adding a new one.

## Term instruments

`loan_t` records one Term Instrument: an identifier unique within the company, the type
(`LOAN_TYPE_BANK` or `LOAN_TYPE_BOND`), the start month (`karte_t::get_current_month` at
drawdown), the term in months (12, 24, 60, 120 or 300), the original and outstanding principal,
the fixed annual rate, the Credit Rating at drawdown (which the Make-Whole Settlement uses),
and the instalment counter `payments_made`.

Schedules:

- A **bank loan** repays equal principal instalments. The first falls due in the month after
  drawdown; `loan_t::get_principal_due_this_month` returns `principal / term_months` and the
  final instalment takes the rounding remainder.
- A **bond** pays interest only and repays the whole `outstanding` at
  `start_month + term_months`.
- Both pay interest monthly in arrears on the opening outstanding principal:
  `outstanding * rate_bp / 120000`.

`finance_t::book_loans_monthly` runs from `finance_t::new_month` before the Overdraft
interest, so the Overdraft is charged on the post-payment balance. It books interest to
`ATC_INTEREST`, principal to `ATC_LOAN_REPAYMENT`, removes Term Instruments whose principal
has reached zero, and does nothing in the month of drawdown.

**Early repayment.** `loan_t::get_early_repayment_settlement` returns the greater of the
outstanding principal and the present value of the remaining contractual payments: the amount
that, invested now at the discount rate, would fund those payments. The discount rate is the
Base Rate plus the Term Offset nearest to the remaining term plus the Rating Spread at
drawdown. Discounting uses a fixed scale of one million; the monthly factor and each discounted
payment are truncated, which can only lower the present value, so the outstanding principal is
the floor. `finance_t::settle_loan` books the principal to `ATC_LOAN_REPAYMENT` and any excess
as a break cost (the charge for ending the contract early) in `ATC_INTEREST`, then removes the
Term Instrument. The company must be able to afford the whole Make-Whole Settlement for an
early repayment; a refinancing, which draws the same principal again immediately, need afford
only the break cost. `finance_t::repay_loan` is the early-repayment entry point.

**Refinancing.** `finance_t::refinance_loan` settles the existing Term Instrument through
`settle_loan` and issues a replacement for the same outstanding principal at the current rate.
Secured Debt is unchanged, so no limit check is needed.

**Takeover.** `player_t::calc_takeover_cost` subtracts `finance_t::get_total_secured_debt`
from the price and `player_t::take_over` calls `finance_t::assume_loans`, which copies the
Term Instruments and renumbers them within the acquirer. Term Instruments keep their fixed
rates. Term Instruments of a company in Liquidation are not transferred.

**Liquidation.** `player_t::complete_liquidation` leaves the Term Instrument list in place; the
player is then removed by `karte_t::new_month` and the list is destroyed with it. The company's
remaining assets are not distributed among its creditors.

## The term-debt limit

`finance_t::get_term_debt_limit` returns
`max(minimum_loan_limit, maximum_secured_gearing * Credit Asset Value / 100)`, the minimum
being converted to current money through the `general` price series in `config/prices.tab`.
`finance_t::get_available_term_debt` returns the Term-Debt Limit less Secured Debt, floored at
zero. The minimum is a floor on the total facility, not an allowance for each instrument, so it
is what lets a new company with no assets borrow.

## Way renewal classification

A way replacement cost is split between Revenue Renewal and Capital Improvement. The
classifier, its callers and its booking paths are in
[way-renewal-classification](cost-of-capital/way-renewal-classification.md).

## Savegame, network and determinism

Extended save revision 18 (`EX_SAVE_MINOR` in `simversion.h`) adds, in `finance_t::rdwr`:

- `asset_value[TT_MAX]` and `tax_pool[TT_MAX]`;
- the Term Instrument list with all `loan_t` fields, and `next_loan_id`;
- `credit_rating` and `credit_assessment_month`.

`settings_t::rdwr` adds the capital-accounting, rating and lending settings under the same
version gate. Defaults are assigned before the conditional block, so an older savegame loads
with the values currently configured by the pakset and the user's settings file, and with
zeroed pools.

An old savegame gets an empty Term Instrument list, an Asset Ledger from
`initialise_legacy_asset_ledger`, a zero Tax Written-Down Pool, and a Credit Rating from the
first month change: a `credit_assessment_month` of -1 marks the Credit Rating as unassessed and
`player_t::finish_rd` calls `update_credit_rating`.

Borrowing, early repayment and refinancing are state changes and travel as
`TOOL_CHANGE_PLAYER` subcommands in the tool parameter string:

| Subcommand | Parameters |
|---|---|
| `b` | player, instrument type, term index, amount in Simucents |
| `p` | player, instrument id |
| `e` | player, instrument id, term index |

The term index counts from zero in the order 1, 2, 5, 10 and 25 years; the instrument type is
0 for a bank loan and 1 for a bond.

`karte_t::call_work` routes these through the normal command queue, so every peer executes the
same operation. `simworld.cc` exempts `TOOL_CHANGE_PLAYER` from its password check, so
`tool_change_player_t::init` checks `player_t::is_locked` itself, and the finance methods check
the solvency state, the term availability and the Term-Debt Limit on every peer. Instrument
identifiers are assigned from the player's own counter, so the same sequence of commands
produces the same identifiers on every peer.

## Interface

`gui/loan_frame.cc` is the borrowing window, opened from the Borrowing button in the finance
window (`gui/money_frame.cc`) and identified by `magic_loan_frame`. It shows the Credit
Rating, the Credit Asset Value, Secured Debt, Secured Gearing and the Term-Debt Limit; a graph
of Secured Debt, interest and principal repaid; a table of Term Instruments; and a form to
issue a Term Instrument, with buttons to repay the selected one early or refinance it. Terms
that the current Credit Rating does not permit are disabled.

The finance window has a Debt line in the cash-flow views and an Accrual tab showing
revenue, operating costs, operating profit, the pooled depreciation charge, operating profit
after depreciation, interest, tax paid, profit after depreciation and tax, and a current
balance sheet of cash, vehicles at sale value, infrastructure at book value, Secured Debt,
Overdraft and equity. The Net Wealth line is cash plus vehicle value and does not deduct debt.

## Open questions

- The intended finance-window palette is a restricted set of related colours in which every
  series also differs in alpha. The borrowing window uses three palette colours and
  `gui_chart_t` has no per-series alpha.
- The 25-year bond restriction is applied in `finance_t::is_term_available`; the term buttons
  are disabled for a company whose Credit Rating does not reach `BBB`, but the dialog does not
  explain which rating is needed.
- The effect of the renewal classifier on the Credit Rating has not been measured: moving part
  of a replacement from operating cost to capital cost raises operating profit and therefore
  Debt-Service Cover.
