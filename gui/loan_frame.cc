/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simworld.h"
#include "../simtool.h"
#include "../dataobj/translator.h"
#include "../dataobj/koord3d.h"
#include "../player/simplay.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"

#include "simwin.h"
#include "loan_frame.h"


// Colours for the graph series: debt, interest and principal repaid.
static const uint8 chart_series_color[3] = { 132, 36, 44 };

static const char *const loan_type_text[2] = { "Bank loan", "Bond" };
static const char *const loan_type_tooltip[2] =
{
	"Bank loan: principal repaid in monthly instalments.",
	"Bond: interest only until maturity, when the principal is repaid; a 25-year term is available only for bonds."
};
static const char *const loan_term_text[LOAN_TERM_MAX] = { "1 year", "2 years", "5 years", "10 years", "25 years" };


static char const *rating_name(uint8 rating)
{
	if (rating >= CREDIT_RATING_MAX)
	{
		rating = CREDIT_RATING_C;
	}
	return translator::translate(credit_rating_names[rating]);
}


/// Reformats a sum in Simucents as a money label.
static void set_money(gui_label_buf_t &label, sint64 simucents)
{
	char buffer[64];
	money_to_string(buffer, (double)simucents / 100.0);
	label.buf().printf("%s", buffer);
	label.update();
}


loan_frame_t::loan_frame_t(player_t *player) :
	gui_frame_t(translator::translate("Borrowing"), player),
	instrument_scroll(&instrument_cont, true, true),
	selected_term(LOAN_TERM_5_YEARS),
	selected_type(LOAN_TYPE_BANK),
	quote_valid(false),
	credit_blocked(false),
	last_month(0xFFFFFFFF),
	last_loan_count(0xFFFFFFFF)
{
	this->player = player;

	set_table_layout(1, 0);

	// The lender view.
	add_table(4, 3);
	{
		new_component<gui_label_t>("Credit rating");
		add_component(&lb_rating);
		new_component<gui_label_t>("Available credit");
		add_component(&lb_available);

		new_component<gui_label_t>("Term borrowing limit");
		add_component(&lb_credit_limit);
		new_component<gui_label_t>("Secured debt");
		add_component(&lb_secured_debt);

		new_component<gui_label_t>("Credit asset value");
		add_component(&lb_asset_value);
		new_component<gui_label_t>("Secured gearing");
		add_component(&lb_gearing);
	}
	end_table();

	// Breathing room between the lender summary and the graph, including the
	// space the chart uses for its top axis label.
	new_component<gui_margin_t>(0, 2 * LINESPACE);

	// The debt history graph.
	chart.set_min_size(scr_size(0, 100));
	chart.set_dimension(MAX_PLAYER_HISTORY_MONTHS, 10000);
	chart.set_seed(0);
	chart.set_background(SYSCOL_CHART_BACKGROUND);
	for (uint8 i = 0; i < CHART_SERIES; i++)
	{
		chart.add_curve(color_idx_to_rgb(chart_series_color[i]), *chart_table, CHART_SERIES, i, MAX_PLAYER_HISTORY_MONTHS, gui_chart_t::MONEY, true, true, 2);
	}
	add_table(2, 1);
	add_component(&chart);
	new_component<gui_margin_t>(45);
	end_table();

	// The instrument table.
	instrument_cont.set_table_layout(INSTRUMENT_COLUMNS, MAX_INSTRUMENT_ROWS + 1);
	instrument_cont.set_spacing(scr_size(0, 0));
	instrument_cont.new_component<gui_table_header_t>("Type", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	instrument_cont.new_component<gui_table_header_t>("Start", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	instrument_cont.new_component<gui_table_header_t>("Matures", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	instrument_cont.new_component<gui_table_header_t>("Rate", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	instrument_cont.new_component<gui_table_header_t>("Principal", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true)->set_tooltip(translator::translate("Original principal"));
	instrument_cont.new_component<gui_table_header_t>("Balance", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true)->set_tooltip(translator::translate("Outstanding principal"));
	instrument_cont.new_component<gui_table_header_t>("Payment", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true)->set_tooltip(translator::translate("Next payment, interest plus principal"));
	instrument_cont.new_component<gui_table_header_t>("Interest", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true)->set_tooltip(translator::translate("Interest remaining"));
	for (uint8 row = 0; row < MAX_INSTRUMENT_ROWS; row++)
	{
		const PIXVAL band = (row & 1) ? SYSCOL_TD_BACKGROUND_SUM : SYSCOL_TD_BACKGROUND;
		for (uint8 column = 0; column < INSTRUMENT_COLUMNS; column++)
		{
			instrument_rows[row][column].set_bgcolor(band);
			instrument_rows[row][column].set_border_bottom_right(true);
			instrument_rows[row][column].set_align(column >= 3 ? gui_label_t::right : gui_label_t::left);
			instrument_cont.add_component(&instrument_rows[row][column]);
		}
	}
	new_component<gui_label_t>("Existing instruments", SYSCOL_TEXT_HIGHLIGHT);
	add_component(&instrument_scroll);

	// The borrowing form: a new instrument, then the existing instruments.
	add_table(4, 1);
	{
		new_component<gui_label_t>("New instrument");
		add_component(&type_c);
		for (uint8 i = 0; i < 2; i++)
		{
			type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(loan_type_text[i]), SYSCOL_TEXT);
		}
		type_c.set_selection(0);
		type_c.add_listener(this);

		new_component<gui_label_t>("Amount");
		amount.init(100000, 0, 2000000000, 100000, false, 12, true);
		amount.set_formatted(true);
		amount.add_listener(this);
		add_component(&amount);
	}
	end_table();

	add_table(LOAN_TERM_MAX + 3, 1);
	{
		new_component<gui_label_t>("Term");
		for (uint8 i = 0; i < LOAN_TERM_MAX; i++)
		{
			term_buttons[i].init(button_t::box_state, loan_term_text[i]);
			term_buttons[i].add_listener(this);
			add_component(&term_buttons[i]);
		}

		new_component<gui_margin_t>(D_H_SPACE * 2);

		bt_borrow.init(button_t::roundbox, "Borrow");
		bt_borrow.add_listener(this);
		bt_borrow.set_tooltip("Commit the selected term and amount");
		add_component(&bt_borrow);
	}
	end_table();

	add_component(&lb_quote);
	add_component(&lb_status);

	add_table(4, 1);
	{
		new_component<gui_label_t>("Existing instrument");
		add_component(&instrument_c);
		instrument_c.add_listener(this);

		bt_repay.init(button_t::roundbox, "Repay early");
		bt_repay.add_listener(this);
		bt_repay.set_no_translate(true);
		add_component(&bt_repay);

		bt_refinance.init(button_t::roundbox, "Refinance");
		bt_refinance.add_listener(this);
		bt_refinance.set_tooltip("Replace the selected instrument with a new one on the selected term.");
		add_component(&bt_refinance);
	}
	end_table();

	update_instrument_choices();
	update_data();
	instrument_scroll.set_min_width(instrument_cont.get_min_size().w);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(diagonal_resize);
	resize(scr_size(0,0));
}


finance_t *loan_frame_t::get_finance() const
{
	return player->get_finance();
}


void loan_frame_t::update_instrument_choices()
{
	instrument_c.clear_elements();
	const vector_tpl<loan_t> &loans = get_finance()->get_loans();
	const uint32 count = min((uint32)MAX_INSTRUMENT_ROWS, loans.get_count());
	for (uint32 i = 0; i < count; i++)
	{
		sprintf(instrument_names[i], "%s %u", translator::translate(loans[i].type == LOAN_TYPE_BOND ? "Bond" : "Bank loan"), (unsigned)loans[i].id);
		instrument_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(instrument_names[i], SYSCOL_TEXT);
	}
	if (count == 0)
	{
		instrument_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("None"), SYSCOL_TEXT);
	}
	instrument_c.set_selection(0);
}


void loan_frame_t::update_quote()
{
	finance_t *finance = get_finance();
	const sint64 amount_simucents = (sint64)amount.get_value() * 100ll;
	const sint32 term_months = loan_term_months(selected_term);
	const uint8 rating = finance->assess_credit_rating_for_instrument((uint8)selected_type, selected_term, amount_simucents, NULL);

	lb_quote.buf().clear();
	lb_status.buf().clear();
	quote_valid = false;

	if (amount_simucents <= 0 || !finance->is_term_available(selected_term, (uint8)selected_type, rating))
	{
		lb_status.buf().printf("%s", translator::translate("This term is not available at your credit rating."));
		lb_status.update();
		lb_quote.update();
		return;
	}

	if (finance->get_available_term_debt() <= 0)
	{
		lb_status.buf().printf("%s", translator::translate("No further borrowing is available within the term borrowing limit."));
		lb_status.update();
		lb_quote.update();
		return;
	}

	if (amount_simucents > finance->get_available_term_debt())
	{
		lb_status.buf().printf("%s", translator::translate("That would exceed the term borrowing limit."));
		lb_status.update();
		lb_quote.update();
		return;
	}

	quote_valid = true;
	const sint32 rate_bp = finance->get_term_rate_bp(selected_term, rating, amount_simucents);
	loan_t proposed(0, (uint8)selected_type, welt->get_current_month(), term_months, amount_simucents, rate_bp, rating);
	const sint64 monthly_interest = (amount_simucents * (sint64)rate_bp) / 120000ll;
	const sint64 monthly_principal = proposed.get_principal_due_this_month();

	char payment[64];
	money_to_string(payment, (double)(monthly_interest + monthly_principal) / 100.0);
	char total_interest[64];
	money_to_string(total_interest, (double)proposed.get_total_interest_remaining() / 100.0);

	const uint32 maturity = welt->get_current_month() + term_months;
	lb_quote.buf().printf(translator::translate("Rate %.2f%%, payment %s per month, total interest %s, maturity %s %u"),
		(double)rate_bp / 100.0, payment, total_interest,
		translator::get_month_name(maturity % 12), maturity / 12);
	lb_quote.update();
	lb_status.update();
}


void loan_frame_t::update_data()
{
	finance_t *finance = get_finance();

	lb_rating.buf().printf("%s", rating_name(finance->get_credit_rating()));
	lb_rating.update();

	set_money(lb_credit_limit, finance->get_term_debt_limit());
	set_money(lb_available, finance->get_available_term_debt());
	set_money(lb_secured_debt, finance->get_total_secured_debt());
	set_money(lb_asset_value, finance->get_credit_asset_value());

	const sint64 asset_value = finance->get_credit_asset_value();
	lb_gearing.buf().printf("%.1f%%", asset_value > 0 ? (double)(finance->get_total_secured_debt() * 100) / (double)asset_value : 0.0);
	lb_gearing.update();

	// The instrument table.
	const vector_tpl<loan_t> &loans = finance->get_loans();
	const bool none = loans.get_count() == 0;
	for (uint8 row = 0; row < MAX_INSTRUMENT_ROWS; row++)
	{
		const bool used = none ? (row == 0) : (row < loans.get_count());
		for (uint8 column = 0; column < INSTRUMENT_COLUMNS; column++)
		{
			instrument_rows[row][column].buf().clear();
			instrument_rows[row][column].set_visible(used);
		}
		if (!used)
		{
			continue;
		}

		gui_table_cell_buf_t *const cells = instrument_rows[row];
		if (none)
		{
			cells[0].buf().append(translator::translate("None"));
			continue;
		}

		const loan_t &loan = loans[row];
		cells[0].buf().printf("%s", translator::translate(loan.type == LOAN_TYPE_BOND ? "Bond" : "Bank loan"));
		cells[1].buf().printf("%u/%u", loan.start_month % 12, loan.start_month / 12);
		const sint32 maturity = loan.start_month + loan.term_months;
		cells[2].buf().printf("%u/%u", maturity % 12, maturity / 12);
		cells[3].buf().printf("%.2f%%", (double)loan.rate_bp / 100.0);

		char buffer[64];
		money_to_string(buffer, (double)loan.principal / 100.0);
		cells[4].buf().printf("%s", buffer);
		money_to_string(buffer, (double)loan.outstanding / 100.0);
		cells[5].buf().printf("%s", buffer);
		money_to_string(buffer, (double)(loan.get_interest_for_month() + loan.get_principal_due_this_month()) / 100.0);
		cells[6].buf().printf("%s", buffer);
		money_to_string(buffer, (double)loan.get_total_interest_remaining() / 100.0);
		cells[7].buf().printf("%s", buffer);
	}

	// The graph.
	for (int month = 0; month < MAX_PLAYER_HISTORY_MONTHS; month++)
	{
		chart_table[month][0] = finance->get_history_com_month(month, ATC_SECURED_DEBT);
		chart_table[month][1] = -finance->get_history_com_month(month, ATC_INTEREST);
		chart_table[month][2] = -finance->get_history_com_month(month, ATC_LOAN_REPAYMENT);
	}

	for (uint8 i = 0; i < MAX_INSTRUMENT_ROWS; i++)
	{
		for (uint8 j = 0; j < INSTRUMENT_COLUMNS; j++)
		{
			instrument_rows[i][j].update();
		}
	}

	update_quote();

	instrument_scroll.set_min_width(instrument_cont.get_min_size().w);
	update_repay_tooltip();
}


void loan_frame_t::update_repay_tooltip()
{
	const vector_tpl<loan_t> &loans = get_finance()->get_loans();
	const int selection = instrument_c.get_selection();
	if (selection < 0 || (uint32)selection >= loans.get_count())
	{
		bt_repay.set_tooltip(translator::translate("Select an existing instrument to repay early."));
		return;
	}

	const loan_t &loan = loans[selection];
	const sint64 settlement = get_finance()->get_early_repayment_settlement(loan);
	const sint64 charge = settlement - loan.outstanding;
	char charge_text[64];
	money_to_string(charge_text, (double)charge / 100.0);

	repay_tooltip.clear();
	repay_tooltip.printf(translator::translate("Repay the selected instrument early, including the early repayment charge of %s."), charge_text);
	bt_repay.set_tooltip(repay_tooltip);
}


void loan_frame_t::draw(scr_coord pos, scr_size size)
{
	if (welt->get_current_month() != last_month)
	{
		last_month = welt->get_current_month();
		last_loan_count = get_finance()->get_loans().get_count();
		update_instrument_choices();
		update_data();
		reset_min_windowsize();
	}
	else if (get_finance()->get_loans().get_count() != last_loan_count)
	{
		last_loan_count = get_finance()->get_loans().get_count();
		update_instrument_choices();
		update_data();
		reset_min_windowsize();
	}

	// The instrument-type tooltip describes the currently selected type only.
	const int type_selection = type_c.get_selection();
	type_c.set_tooltip(loan_type_tooltip[type_selection == 1 ? 1 : 0]);

	// Term availability and the selected-instrument controls depend on the
	// company state, which changes outside the month turn.
	const uint8 rating = get_finance()->assess_credit_rating();
	bool any_term_available = false;
	for (uint8 i = 0; i < LOAN_TERM_MAX; i++)
	{
		const loan_term_t term = (loan_term_t)i;
		const bool available = get_finance()->is_term_available(term, (uint8)selected_type, rating);
		if (available)
		{
			any_term_available = true;
			term_buttons[i].enable();
			term_buttons[i].set_tooltip(NULL);
		}
		else
		{
			term_buttons[i].disable();
			if (!welt->get_settings().term_loans_allowed())
			{
				term_buttons[i].set_tooltip("Term borrowing is disabled.");
			}
			else if (selected_type == LOAN_TYPE_BANK && term == LOAN_TERM_25_YEARS)
			{
				term_buttons[i].set_tooltip("A 25-year term is available only for bonds.");
			}
			else
			{
				term_buttons[i].set_tooltip("This term is not available at your credit rating.");
			}
		}
		// Never show an unavailable term as the selected term: the pressed
		// state would otherwise look like an active choice.
		term_buttons[i].pressed = available && selected_term == term;
	}

	if (get_finance()->get_loans().get_count() == 0)
	{
		bt_repay.disable();
		bt_refinance.disable();
	}
	else
	{
		bt_repay.enable();
		bt_refinance.enable();
	}

	// A fully used term-debt limit blocks new borrowing entirely, so disable the
	// new-instrument controls and say why on the commit button.
	const sint64 available_credit = get_finance()->get_available_term_debt();
	const sint64 requested = (sint64)amount.get_value() * 100ll;
	const bool no_credit = available_credit <= 0;
	if (no_credit != credit_blocked)
	{
		credit_blocked = no_credit;
		if (no_credit)
		{
			type_c.disable();
			amount.disable();
		}
		else
		{
			type_c.enable();
			amount.enable();
		}
		update_quote();
	}
	if (no_credit)
	{
		quote_valid = false;
		bt_borrow.set_tooltip("No further borrowing is available within the term borrowing limit.");
	}
	else if (!welt->get_settings().term_loans_allowed())
	{
		quote_valid = false;
		bt_borrow.set_tooltip("Term borrowing is disabled.");
	}
	else if (!any_term_available)
	{
		quote_valid = false;
		bt_borrow.set_tooltip("Your credit rating does not permit new term borrowing.");
	}
	else if (!get_finance()->is_term_available(selected_term, (uint8)selected_type, rating))
	{
		quote_valid = false;
		if (selected_type == LOAN_TYPE_BANK && selected_term == LOAN_TERM_25_YEARS)
		{
			bt_borrow.set_tooltip("A 25-year term is available only for bonds.");
		}
		else
		{
			bt_borrow.set_tooltip("The selected term is not available at your credit rating.");
		}
	}
	else if (requested <= 0)
	{
		quote_valid = false;
		bt_borrow.set_tooltip("Enter an amount greater than zero.");
	}
	else if (requested > available_credit)
	{
		quote_valid = false;
		bt_borrow.set_tooltip("The amount exceeds the remaining term borrowing limit.");
	}
	else if (!quote_valid)
	{
		bt_borrow.set_tooltip("The proposed borrowing is not available on the selected term.");
	}
	else
	{
		bt_borrow.set_tooltip("Commit the selected term and amount");
	}

	if (quote_valid)
	{
		bt_borrow.enable();
	}
	else
	{
		bt_borrow.disable();
	}

	gui_frame_t::draw(pos, size);
}


bool loan_frame_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	finance_t *finance = get_finance();
	static char param[128];

	if (comp == &type_c)
	{
		selected_type = (uint16)type_c.get_selection();
		update_quote();
		return true;
	}

	for (uint8 i = 0; i < LOAN_TERM_MAX; i++)
	{
		if (comp == &term_buttons[i])
		{
			selected_term = (loan_term_t)i;
			update_quote();
			return true;
		}
	}

	if (comp == &amount)
	{
		update_quote();
		return true;
	}

	if (comp == &bt_borrow)
	{
		sprintf(param, "b,%i,%i,%i,%lld", player->get_player_nr(), (int)selected_type, (int)selected_term, (long long)((sint64)amount.get_value() * 100ll));
		tool_t *tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
		tool->set_default_param(param);
		welt->set_tool(tool, player);
		// The tool always returns false from init, so it is safe to delete now.
		delete tool;
		return true;
	}

	if (comp == &instrument_c)
	{
		update_repay_tooltip();
		return true;
	}

	const vector_tpl<loan_t> &loans = finance->get_loans();
	const int selection = instrument_c.get_selection();
	if (selection < 0 || (uint32)selection >= loans.get_count())
	{
		return true;
	}
	const uint32 instrument_id = loans[selection].id;

	if (comp == &bt_repay)
	{
		sprintf(param, "p,%i,%u", player->get_player_nr(), (unsigned)instrument_id);
		tool_t *tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
		tool->set_default_param(param);
		welt->set_tool(tool, player);
		delete tool;
		return true;
	}

	if (comp == &bt_refinance)
	{
		sprintf(param, "e,%i,%u,%i", player->get_player_nr(), (unsigned)instrument_id, (int)selected_term);
		tool_t *tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
		tool->set_default_param(param);
		welt->set_tool(tool, player);
		delete tool;
		return true;
	}

	return false;
}
