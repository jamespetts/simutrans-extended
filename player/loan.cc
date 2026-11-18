/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../dataobj/loadsave.h"

#include "loan.h"


sint32 loan_term_months(loan_term_t term)
{
	switch (term)
	{
		case LOAN_TERM_1_YEAR:   return 12;
		case LOAN_TERM_2_YEARS:  return 24;
		case LOAN_TERM_5_YEARS:  return 60;
		case LOAN_TERM_10_YEARS: return 120;
		case LOAN_TERM_25_YEARS: return 300;
		default:                 return 0;
	}
}


loan_t::loan_t() :
	id(0),
	type(LOAN_TYPE_BANK),
	start_month(0),
	term_months(0),
	principal(0),
	outstanding(0),
	rate_bp(0),
	rating(0),
	payments_made(0)
{}


loan_t::loan_t(uint32 id, uint8 type, sint32 start_month, sint32 term_months, sint64 principal, sint32 rate_bp, uint8 rating) :
	id(id),
	type(type),
	start_month(start_month),
	term_months(term_months),
	principal(principal),
	outstanding(principal),
	rate_bp(rate_bp),
	rating(rating),
	payments_made(0)
{}


sint64 loan_t::get_interest_for_month() const
{
	if (outstanding <= 0 || rate_bp <= 0)
	{
		return 0;
	}
	return (outstanding * (sint64)rate_bp) / 120000ll;
}


sint64 loan_t::get_principal_due_this_month() const
{
	if (outstanding <= 0 || term_months <= 0)
	{
		return 0;
	}

	if (type == LOAN_TYPE_BOND)
	{
		// Repaid in one sum at maturity, which the caller checks separately.
		return 0;
	}

	if (payments_made + 1 >= (uint16)term_months)
	{
		// The final instalment takes the rounding remainder.
		return outstanding;
	}

	const sint64 instalment = principal / (sint64)term_months;
	return min(instalment, outstanding);
}


sint32 loan_t::get_months_remaining(sint32 current_month) const
{
	const sint32 remaining = start_month + term_months - current_month;
	return remaining > 0 ? remaining : 0;
}


bool loan_t::is_matured(sint32 current_month) const
{
	return current_month - start_month >= term_months;
}


sint64 loan_t::get_total_interest_remaining() const
{
	if (outstanding <= 0 || rate_bp <= 0)
	{
		return 0;
	}

	sint64 total = 0;
	sint64 projected_outstanding = outstanding;
	uint16 projected_payments = payments_made;

	while (projected_outstanding > 0 && projected_payments < (uint16)term_months)
	{
		sint64 interest;
		sint64 principal_due;
		project_next_payment(projected_outstanding, projected_payments, interest, principal_due);
		total += interest;
	}

	return total;
}


sint64 loan_t::get_debt_service_for_next_months(sint32 months) const
{
	if (outstanding <= 0)
	{
		return 0;
	}

	sint64 total = 0;
	sint64 projected_outstanding = outstanding;
	uint16 projected_payments = payments_made;

	for (sint32 i = 0; i < months && projected_outstanding > 0 && projected_payments < (uint16)term_months; i++)
	{
		sint64 interest;
		sint64 principal_due;
		project_next_payment(projected_outstanding, projected_payments, interest, principal_due);
		total += interest + principal_due;
	}

	return total;
}


sint64 loan_t::get_early_repayment_settlement(sint32 discount_rate_bp) const
{
	if (outstanding <= 0)
	{
		return 0;
	}

	// Fixed-scale integer discounting. The scale is one millionth of a unit;
	// the monthly factor and every discounted payment are truncated. Truncation
	// can only reduce the present value, so the par floor below protects the
	// lender from a settlement below the outstanding principal.
	static const sint64 SCALE = 1000000ll;
	sint64 factor = SCALE;
	if (discount_rate_bp > 0)
	{
		factor -= (SCALE * (sint64)discount_rate_bp) / 120000ll;
	}
	if (factor < 0)
	{
		factor = 0;
	}

	sint64 discount = SCALE;
	sint64 present_value = 0;
	sint64 projected_outstanding = outstanding;
	uint16 projected_payments = payments_made;

	while (projected_outstanding > 0 && projected_payments < (uint16)term_months)
	{
		sint64 interest;
		sint64 principal_due;
		project_next_payment(projected_outstanding, projected_payments, interest, principal_due);
		discount = (discount * factor) / SCALE;
		present_value += ((interest + principal_due) * discount) / SCALE;
	}

	return max(outstanding, present_value);
}


void loan_t::project_next_payment(sint64 &projected_outstanding, uint16 &projected_payments, sint64 &interest, sint64 &principal_due) const
{
	interest = (projected_outstanding * (sint64)rate_bp) / 120000ll;

	if (type == LOAN_TYPE_BOND)
	{
		principal_due = (projected_payments + 1 >= (uint16)term_months) ? projected_outstanding : 0;
	}
	else
	{
		const sint64 instalment = principal / (sint64)term_months;
		principal_due = (projected_payments + 1 >= (uint16)term_months) ? projected_outstanding : min(instalment, projected_outstanding);
	}

	if (principal_due > projected_outstanding)
	{
		principal_due = projected_outstanding;
	}
	if (principal_due < 0)
	{
		principal_due = 0;
	}

	projected_outstanding -= principal_due;
	projected_payments++;
}


void loan_t::rdwr(loadsave_t *file)
{
	file->rdwr_long(id);
	file->rdwr_byte(type);
	file->rdwr_long(start_month);
	file->rdwr_long(term_months);
	file->rdwr_longlong(principal);
	file->rdwr_longlong(outstanding);
	file->rdwr_long(rate_bp);
	file->rdwr_byte(rating);
	file->rdwr_short(payments_made);
}
