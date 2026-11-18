/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_LOAN_FRAME_H
#define GUI_LOAN_FRAME_H


#include "gui_frame.h"
#include "components/action_listener.h"
#include "components/gui_button.h"
#include "components/gui_chart.h"
#include "components/gui_combobox.h"
#include "components/gui_label.h"
#include "components/gui_numberinput.h"
#include "components/gui_scrollpane.h"

#include "../player/finance.h"


class player_t;


/**
 * Borrowing window: the credit assessment, the term instruments, the borrowing
 * form and a graph of the debt history. Shown for one player's company.
 */
class loan_frame_t : public gui_frame_t, private action_listener_t
{
	static const uint8 MAX_INSTRUMENT_ROWS = 16;
	static const uint8 INSTRUMENT_COLUMNS = 8;
	static const uint8 CHART_SERIES = 3;

	player_t *player;

	gui_aligned_container_t instrument_cont;
	gui_scrollpane_t instrument_scroll;

	/// Rows of the instrument table: type, start, maturity, rate, original
	/// principal, outstanding principal, next payment, interest remaining.
	gui_label_buf_t instrument_rows[MAX_INSTRUMENT_ROWS][INSTRUMENT_COLUMNS];

	/// Storage for the instrument selection list; the combobox stores pointers.
	char instrument_names[MAX_INSTRUMENT_ROWS][64];

	// The borrower view.
	gui_label_buf_t lb_rating;
	gui_label_buf_t lb_credit_limit;
	gui_label_buf_t lb_available;
	gui_label_buf_t lb_secured_debt;
	gui_label_buf_t lb_asset_value;
	gui_label_buf_t lb_gearing;

	// The borrowing form.
	gui_combobox_t type_c;
	gui_combobox_t instrument_c;
	button_t term_buttons[LOAN_TERM_MAX];
	gui_numberinput_t amount;
	button_t bt_borrow;
	button_t bt_repay;
	button_t bt_refinance;
	gui_label_buf_t lb_quote;
	gui_label_buf_t lb_status;

	loan_term_t selected_term;
	uint16 selected_type;

	gui_chart_t chart;
	sint64 chart_table[MAX_PLAYER_HISTORY_MONTHS][CHART_SERIES];

	uint32 last_month;
	uint32 last_loan_count;

	finance_t *get_finance() const;

	void update_data();
	void update_quote();
	void update_instrument_choices();

public:
	loan_frame_t(player_t *player);

	const char *get_help_filename() const OVERRIDE { return "loans.txt"; }

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t *comp, value_t v) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_loan_frame; }
};

#endif
