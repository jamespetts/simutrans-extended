/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef PLAYER_LOAN_H
#define PLAYER_LOAN_H


#include "../simtypes.h"


class loadsave_t;


/**
 * The two term instruments. A bank loan repays its principal in equal monthly
 * instalments; a bond pays interest only and repays the principal in one sum at
 * maturity.
 */
enum loan_type_t
{
	LOAN_TYPE_BANK = 0,
	LOAN_TYPE_BOND = 1
};


/**
 * The available terms. The numeric value indexes the term-offset table in the
 * settings, so the order must not change.
 */
enum loan_term_t
{
	LOAN_TERM_1_YEAR = 0,
	LOAN_TERM_2_YEARS,
	LOAN_TERM_5_YEARS,
	LOAN_TERM_10_YEARS,
	LOAN_TERM_25_YEARS,
	LOAN_TERM_MAX
};


/**
 * @return the term in months for a term index.
 */
sint32 loan_term_months(loan_term_t term);


/**
 * One term instrument held by a player. Interest is fixed at drawdown and is
 * charged monthly in arrears on the opening outstanding principal.
 */
class loan_t
{
public:
	/// Unique within the owning player.
	uint32 id;

	/// One of loan_type_t.
	uint8 type;

	/// karte_t::get_current_month() at drawdown.
	sint32 start_month;

	/// Term in months.
	sint32 term_months;

	/// Original amount in Simucents.
	sint64 principal;

	/// Remaining principal in Simucents.
	sint64 outstanding;

	/// Fixed annual rate in basis points.
	sint32 rate_bp;

	/// The credit-rating band at drawdown, used by the make-whole discount.
	uint8 rating;

	/// Number of principal instalments already paid; unused for a bond.
	uint16 payments_made;

	loan_t();
	loan_t(uint32 id, uint8 type, sint32 start_month, sint32 term_months, sint64 principal, sint32 rate_bp, uint8 rating);

	/// Interest falling due in the current month, on the opening balance.
	sint64 get_interest_for_month() const;

	/// Principal falling due in the current month.
	sint64 get_principal_due_this_month() const;

	/// Interest plus principal falling due over the next months, projected on
	/// the contractual schedule from the next unprocessed payment.
	sint64 get_debt_service_for_next_months(sint32 months) const;

	/// Months from the current month to maturity, never negative.
	sint32 get_months_remaining(sint32 current_month) const;

	/// Total interest remaining over the rest of the contract.
	sint64 get_total_interest_remaining() const;

	/**
	 * Settlement amount for early repayment: the greater of the outstanding
	 * principal and the present value of the remaining contractual payments,
	 * discounted at the given annual rate in basis points.
	 */
	sint64 get_early_repayment_settlement(sint32 discount_rate_bp) const;

	bool is_matured(sint32 current_month) const;

	void rdwr(loadsave_t *file);

private:
	/**
	 * Advances a projected copy of the instrument by one monthly payment.
	 * @param projected_outstanding running outstanding principal, updated
	 * @param projected_payments running payment count, updated
	 * @param interest receives the interest of that month
	 * @param principal_due receives the principal of that month
	 */
	void project_next_payment(sint64 &projected_outstanding, uint16 &projected_payments, sint64 &interest, sint64 &principal_due) const;
};

#endif
