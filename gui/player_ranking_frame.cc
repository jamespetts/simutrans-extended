/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simcolor.h"
#include "../simworld.h"
#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../bauer/wegbauer.h"

#include "simwin.h"
#include "money_frame.h" // for the finances

#include "components/gui_halthandled_lines.h" // color label
#include "player_ranking_frame.h"


uint8 player_ranking_frame_t::transport_type_option = TT_ALL;
uint8 player_ranking_frame_t::selected_x_axis = 1;
uint8 player_ranking_frame_t::selected_hist_mode = AC_HISTORY_YEARS;

// Text should match that of the finance dialog
static const char* cost_type_name[player_ranking_frame_t::MAX_PLAYER_RANKING_CHARTS] =
{
	"Cash",
	"Net Wealth",
	"Revenue",
	"Ops Profit",
	"Margin (%%)",
	"Pax-km",
	"Mail-km",
	"Freight-km",
	"Distance",
	"Vehicle-km",
	"Convoys",
	"Vehicles",
	"Stops",
	"way_distances" // way length
};

// digit and suffix
static const uint8 cost_type[player_ranking_frame_t::MAX_PLAYER_RANKING_CHARTS*2] =
{
	2,gui_chart_t::MONEY,
	2,gui_chart_t::MONEY,
	2,gui_chart_t::MONEY,
	2,gui_chart_t::MONEY,
	2,gui_chart_t::PERCENT,
	1,gui_chart_t::PAX_KM,
	3,gui_chart_t::TON_KM_MAIL,
	1,gui_chart_t::TON_KM,
	0,gui_chart_t::DISTANCE,
	0,gui_chart_t::DISTANCE,
	0,gui_chart_t::STANDARD,
	0,gui_chart_t::STANDARD,
	0,gui_chart_t::STANDARD,
	2,gui_chart_t::DISTANCE
};

static const uint8 cost_type_color[player_ranking_frame_t::MAX_PLAYER_RANKING_CHARTS] =
{
	COL_CASH,
	COL_WEALTH,
	COL_REVENUE,
	COL_PROFIT,
	COL_MARGIN,
	COL_LIGHT_PURPLE,
	COL_TRANSPORTED,
	COL_BROWN,
	COL_DISTANCE,
	COL_RED+2,
	COL_COUNVOI_COUNT,
	COL_NEW_VEHICLES,
	COL_DODGER_BLUE,
	COL_GREY3
};

// is_atv=1, ATV:vehicle finance record, ATC:common finance record
static const uint8 history_type_idx[player_ranking_frame_t::MAX_PLAYER_RANKING_CHARTS * 2] =
{
	0,ATC_CASH,
	0,ATC_NETWEALTH,
	1,ATV_REVENUE,
	1,ATV_OPERATING_PROFIT,
	1,ATV_PROFIT_MARGIN,
	1,ATV_TRANSPORTED_PASSENGER,
	1,ATV_TRANSPORTED_MAIL,
	1,ATV_TRANSPORTED_GOOD,
	1,ATV_CONVOY_DISTANCE,
	1,ATV_VEHICLE_DISTANCE,
	1,ATV_CONVOIS,
	1,ATV_VEHICLES,
	0,ATC_HALTS,
	1,ATV_WAY_LENGTH
};


sint64 convert_waylength(sint64 value) { return (sint64)(value * world()->get_settings().get_meters_per_tile() / 100.0); }

static const gui_chart_t::convert_proc proc = convert_waylength;

static int compare_atv(uint8 player_nr_a, uint8 player_nr_b, uint8 atv_index)
{
	sint64 comp = 0; // otherwise values may overflow
	player_t* a_player = world()->get_player(player_nr_a);
	player_t* b_player = world()->get_player(player_nr_b);

	if (a_player && b_player) {
		switch (player_ranking_frame_t::selected_hist_mode) {
			case AC_HISTORY_MONTHS:
				comp = b_player->get_finance()->get_history_veh_month((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis, atv_index) - a_player->get_finance()->get_history_veh_month((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis, atv_index);
				break;
			case AC_HISTORY_YEARS:
			default:
				comp = b_player->get_finance()->get_history_veh_year((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis, atv_index) - a_player->get_finance()->get_history_veh_year((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis, atv_index);
				break;
		}
		if (comp == 0) {
			comp = b_player->get_age() - a_player->get_age();
		}
		if (comp == 0 && player_ranking_frame_t::selected_x_axis) {
			switch (player_ranking_frame_t::selected_hist_mode) {
				case AC_HISTORY_MONTHS:
				comp = b_player->get_finance()->get_history_veh_month((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis-1, atv_index)
					 - a_player->get_finance()->get_history_veh_month((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis-1, atv_index);
					break;
				case AC_HISTORY_YEARS:
				default:
				comp = b_player->get_finance()->get_history_veh_year((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis-1, atv_index)
					 - a_player->get_finance()->get_history_veh_year((transport_type)player_ranking_frame_t::transport_type_option, player_ranking_frame_t::selected_x_axis-1, atv_index);
					break;
			}
		}
	}
	if (comp == 0) {
		comp = player_nr_b - player_nr_a;
	}
	return comp;
}

static int compare_atc(uint8 player_nr_a, uint8 player_nr_b, uint8 atc_index)
{
	sint64 comp = 0;
	player_t* a_player = world()->get_player(player_nr_a);
	player_t* b_player = world()->get_player(player_nr_b);

	if (a_player && b_player) {
		switch (player_ranking_frame_t::selected_hist_mode) {
			case AC_HISTORY_MONTHS:
				comp = b_player->get_finance()->get_history_com_month(player_ranking_frame_t::selected_x_axis, atc_index) - a_player->get_finance()->get_history_com_month(player_ranking_frame_t::selected_x_axis, atc_index);
				break;
			case AC_HISTORY_YEARS:
			default:
				comp = b_player->get_finance()->get_history_com_year(player_ranking_frame_t::selected_x_axis, atc_index) - a_player->get_finance()->get_history_com_year(player_ranking_frame_t::selected_x_axis, atc_index);
				break;
		}
		if (comp == 0) {
			comp = b_player->get_age() - a_player->get_age();
		}
		if (comp == 0 && player_ranking_frame_t::selected_x_axis) {
			switch (player_ranking_frame_t::selected_hist_mode) {
				case AC_HISTORY_MONTHS:
					comp = b_player->get_finance()->get_history_com_month(player_ranking_frame_t::selected_x_axis-1, atc_index)
						 - a_player->get_finance()->get_history_com_month(player_ranking_frame_t::selected_x_axis-1, atc_index);
					break;
				case AC_HISTORY_YEARS:
				default:
					comp = b_player->get_finance()->get_history_com_year(player_ranking_frame_t::selected_x_axis-1, atc_index)
						 - a_player->get_finance()->get_history_com_year(player_ranking_frame_t::selected_x_axis-1, atc_index);
					break;
		}
		}
	}
	if (comp == 0) {
		comp = player_nr_b - player_nr_a;
	}
	return comp;
}

static int compare_revenue(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_REVENUE);
}
static int compare_profit(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_OPERATING_PROFIT);
}
static int compare_transport_pax(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_TRANSPORTED_PASSENGER);
}
static int compare_transport_mail(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_TRANSPORTED_MAIL);
}
static int compare_transport_goods(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_TRANSPORTED_GOOD);
}
static int compare_margin(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_PROFIT_MARGIN);
}
static int compare_convois(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_CONVOIS);
}
static int compare_convoy_km(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_CONVOY_DISTANCE);
}
static int compare_vehicles(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_VEHICLES);
}
static int compare_vehicle_km(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_VEHICLE_DISTANCE);
}
static int compare_way_length(player_button_t* const& a, player_button_t* const& b) {
	return compare_atv(a->get_player_nr(), b->get_player_nr(), ATV_WAY_LENGTH);
}


static int compare_cash(player_button_t* const &a, player_button_t* const &b) {
	return compare_atc(a->get_player_nr(), b->get_player_nr(), ATC_CASH);
}
static int compare_netwealth(player_button_t* const& a, player_button_t* const& b) {
	return compare_atc(a->get_player_nr(), b->get_player_nr(), ATC_NETWEALTH);
}
static int compare_halts(player_button_t* const& a, player_button_t* const& b) {
	return compare_atc(a->get_player_nr(), b->get_player_nr(), ATC_HALTS);
}


player_button_t::player_button_t(uint8 player_nr_)
{
	player_nr = player_nr_;
	init(button_t::box_state | button_t::flexible, NULL, scr_coord(0,0), D_BUTTON_SIZE);
	set_tooltip("Right-click to open the finance dialog.");
	update();
}

void player_button_t::update()
{
	player_t* player = world()->get_player(player_nr);
	if (player) {
		set_text(player->get_name());
		background_color = color_idx_to_rgb(player->get_player_color1() + env_t::gui_player_color_bright);
		enable();
		set_visible(true);
	}
	else {
		set_visible(false);
		disable();
	}
}

bool player_button_t::infowin_event(const event_t* ev)
{
	if (IS_RIGHTRELEASE(ev)) {
		player_t* player = world()->get_player(player_nr);
		if (player) {
			create_win(new money_frame_t(player), w_info, magic_finances_t + player_nr);
			return true;
		}
	}
	return button_t::infowin_event(ev);
}


player_ranking_frame_t::player_ranking_frame_t(uint8 selected_player_nr) :
	gui_frame_t(translator::translate("Player ranking")),
	scrolly(&cont_players, true, true)
{
	selected_player = selected_player_nr;
	last_month = world()->get_last_month();
	world_age = welt->get_public_player()->get_age();
	player_ranking_frame_t::selected_x_axis = min(1, world_age);

	set_table_layout(1,0);

	cont_chart.set_table_layout(2, 2);
	{
		chart.set_background(SYSCOL_CHART_BACKGROUND);
		chart.set_min_size(scr_size(24*LINESPACE, 11*LINESPACE));
		cont_chart.add_component(&chart,2);

		cb_chart_span_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(chart_span_text[AC_HISTORY_MONTHS]), SYSCOL_TEXT);
		cb_chart_span_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(chart_span_text[AC_HISTORY_YEARS]), SYSCOL_TEXT);
		cb_chart_span_selector.set_selection(player_ranking_frame_t::selected_hist_mode);
		cb_chart_span_selector.add_listener(this);
		cont_chart.add_component(&cb_chart_span_selector);

		cont_chart.new_component<gui_fill_t>();
	}

	add_table(2, 1)->set_alignment(ALIGN_TOP);
	{
		if( env_t::left_to_right_graphs ) {
			add_component(&cont_chart); // Position the ranking so that it flows from the chart.
		}

		cont_players.set_table_layout(3, 0);
		cont_players.set_alignment(ALIGN_CENTER_H);
		cont_players.set_margin(NO_SPACING, scr_size(D_SCROLLBAR_WIDTH+D_H_SPACE,D_SCROLLBAR_HEIGHT));

		add_table(1,2);
		{
			add_table(3, 1);
			{

				update_x_axis_selector();
				cb_x_axis_selector.add_listener(this);
				cb_x_axis_selector.set_inverse_side_scroll(env_t::left_to_right_graphs);
				add_component(&cb_x_axis_selector);

				for (int i = 0, count = 0; i < TT_OTHER; ++i) {
					if (i>0 && !way_builder_t::is_active_waytype(finance_t::translate_tt_to_waytype((transport_type)(i)))) continue;
					transport_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(finance_t::transport_type_values[i]), SYSCOL_TEXT);
					transport_types[count++] = i;
				}
				transport_type_c.set_focusable(false);
				transport_type_c.add_listener(this);
				transport_type_c.set_selection(0);
				add_component(&transport_type_c);

				new_component<gui_empty_t>();
			}
			end_table();

			scrolly.set_rigid(true); // keep the size so that the combo box position does not move
			scrolly.set_show_scroll_x(false); // UI TODO: chat window
			// There will be changes to the scroll panel function along with the chat window function,
			// so we will need to adjust it after that. Adjust the width of the scroll panel so that it does not expand.
			add_component(&scrolly);
		}
		end_table();

		if( !env_t::left_to_right_graphs ) {
			add_component(&cont_chart); // Position the ranking so that it flows from the chart.
		}
	}
	end_table();

	// init chart buttons
	for (uint8 i = 0; i < MAX_PLAYER_RANKING_CHARTS; i++) {
		bt_charts[i].init(button_t::box_state | button_t::flexible, cost_type_name[i]);
		bt_charts[i].background_color = color_idx_to_rgb(cost_type_color[i]);
		if (i == selected_item) bt_charts[i].pressed = true;
		bt_charts[i].add_listener(this);
	}

	add_table(2,0);
	{
		new_component<gui_label_t>("Finanzen");
		add_table(3,1)->set_force_equal_columns(true); {
			add_component(&bt_charts[0]);
			add_component(&bt_charts[1]);
			new_component<gui_empty_t>();
		} end_table();
		new_component<gui_empty_t>();
		add_table(3, 1)->set_force_equal_columns(true); {
			add_component(&bt_charts[2]);
			add_component(&bt_charts[3]);
			add_component(&bt_charts[4]);
		} end_table();

		new_component<gui_label_t>("Transportation results");
		add_table(3, 1)->set_force_equal_columns(true); {
			add_component(&bt_charts[5]);
			add_component(&bt_charts[6]);
			add_component(&bt_charts[7]);
		} end_table();
		new_component<gui_empty_t>();
		add_table(3, 1)->set_force_equal_columns(true); {
			add_component(&bt_charts[8]);
			add_component(&bt_charts[9]);
			new_component<gui_empty_t>();
		} end_table();

		new_component<gui_label_t>("Infrastructures");
		add_table(4, 1)->set_force_equal_columns(true); {
			add_component(&bt_charts[10]);
			add_component(&bt_charts[11]);
			add_component(&bt_charts[12]);
			add_component(&bt_charts[13]);
		} end_table();
	}
	end_table();

	update_chart();

	set_windowsize(get_min_windowsize());
	cb_chart_span_selector.set_width_fixed(true);
	transport_type_c.set_width_fixed(true);
	cb_x_axis_selector.set_width_fixed(true);
	set_resizemode(diagonal_resize);
}

void player_ranking_frame_t::remove_player_buttons()
{
	while (!player_buttons.empty()) {
		player_button_t* b = player_buttons.remove_first();
		cont_players.remove_component(b);
		delete b;
	}
}

player_ranking_frame_t::~player_ranking_frame_t()
{
	remove_player_buttons();
}


void player_ranking_frame_t::sort_player()
{
	cont_players.remove_all();

	bool show_share=false;
	switch (selected_item)
	{
		case PR_REVENUE:
			player_buttons.sort(compare_revenue);
			break;
		case PR_PROFIT:
			player_buttons.sort(compare_profit);
			break;
		case PR_TRANSPORT_PAX:
			show_share = true;
			player_buttons.sort(compare_transport_pax);
			break;
		case PR_TRANSPORT_MAIL:
			show_share = true;
			player_buttons.sort(compare_transport_mail);
			break;
		case PR_TRANSPORT_GOODS:
			show_share = true;
			player_buttons.sort(compare_transport_goods);
			break;
		case PR_CASH:
			player_buttons.sort(compare_cash);
			break;
		case PR_NETWEALTH:
			player_buttons.sort(compare_netwealth);
			break;
		case PR_MARGIN:
			player_buttons.sort(compare_margin);
			break;
		case PR_VEHICLES:
			player_buttons.sort(compare_vehicles);
			break;
		case PR_HALTS:
			player_buttons.sort(compare_halts);
			break;
		case PR_CONVOY_DISTANCE:
			player_buttons.sort(compare_convoy_km);
			break;
		case PR_VEHICLE_DISTANCE:
			player_buttons.sort(compare_vehicle_km);
			break;
		case PR_WAY_KILOMETREAGE:
			show_share = true;
			player_buttons.sort(compare_way_length);
			break;
		default:
		case PR_CONVOIS:
			player_buttons.sort(compare_convois);
			break;
	}
	uint8 count = 0;
	uint8 rank  = 1; // There may be multiple same ranks
	sint64 prev_value=0; // To check if they are in the same rank
	const bool is_atv = history_type_idx[selected_item*2];

	// In order to calculate the share, the total must be calculated first for certain genres
	sint64 total=0;
	if (show_share) {
		// check totals.
		for (uint i = 0; i < player_buttons.get_count(); i++) {
			const uint8 player_nr = player_buttons.at(i)->get_player_nr();
			assert(is_atv);
			if(player_t* player = welt->get_player(player_nr)){
				total += get_value_from_history(player,player_ranking_frame_t::selected_x_axis);
			}
		}
	}

	for (uint i = 0; i < player_buttons.get_count(); i++) {
		const uint8 player_nr = player_buttons.at(i)->get_player_nr();
		player_t* player = welt->get_player(player_nr);
		if (!player) continue;

		// Exclude players who are not in the competition
		if( is_chart_table_zero(player_nr) ) {
			continue;
		}
		// Public player only appers in stop number ranking
		if( player_nr==PUBLIC_PLAYER_NR && selected_item!=PR_HALTS ) {
			continue;
		}

		int age = (int)(player_ranking_frame_t::selected_hist_mode==AC_HISTORY_MONTHS ? player->get_player_age() : player->get_age());
		const bool born_yet = (age - (int)selected_x_axis < 0);
		if (!born_yet) {
			count++;
		}

		// pick up value first for the same rank
		PIXVAL color = player->get_player_nr() == selected_player ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_TEXT;
		sint64 value = get_value_from_history(player, player_ranking_frame_t::selected_x_axis);
		if (prev_value!=value) {
			rank = count;
		}

		prev_value = value;

		if (born_yet) {
			cont_players.new_component<gui_label_t>("-");
		}
		else {
			switch (rank)
			{
				case 1:
					cont_players.new_component<gui_color_label_t>("1", color_idx_to_rgb(COL_WHITE), 56512, 1);
					break;
				case 2:
					cont_players.new_component<gui_color_label_t>("2", 0, 36053, 1);
					break;
				case 3:
					cont_players.new_component<gui_color_label_t>("3", 0, 37702, 1);
					break;

				default:
					gui_label_buf_t* lb = cont_players.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
					lb->buf().printf("%u", rank);
					lb->update();
					lb->set_fixed_width(lb->get_min_size().w);
					break;
			}
		}

		if( player_nr != selected_player ) {
			player_buttons.at(i)->pressed = false;
		}
		cont_players.add_component(player_buttons.at(i));

		// update label
		if (born_yet) {
			cont_players.new_component<gui_label_t>("-");
		}
		else {
			switch (cost_type[selected_item*2+1]) {
				case gui_chart_t::MONEY:
					lb_player_val[player_nr].buf().append_money(value / 100.0);
					color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;
					break;
				case gui_chart_t::PERCENT:
					lb_player_val[player_nr].buf().append(value / 100.0, 2);
					lb_player_val[player_nr].buf().append("%");
					color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;
					break;
				case gui_chart_t::PAX_KM:
					lb_player_val[player_nr].buf().append(value / 10.0, 0);
					lb_player_val[player_nr].buf().append(translator::translate("pkm"));
					break;
				case gui_chart_t::TON_KM_MAIL:
					lb_player_val[player_nr].buf().append(value / 1000.0, 2);
					lb_player_val[player_nr].buf().append(translator::translate("tkm"));
					break;
				case gui_chart_t::TON_KM:
					lb_player_val[player_nr].buf().append(value / 10.0, 0);
					lb_player_val[player_nr].buf().append(translator::translate("tkm"));
					break;
				case gui_chart_t::DISTANCE:
					if (selected_item==PR_WAY_KILOMETREAGE) {
						lb_player_val[player_nr].buf().append(value * welt->get_settings().get_meters_per_tile() / 10000.0, 1);
					}
					else {
						lb_player_val[player_nr].buf().append(value);
					}
					lb_player_val[player_nr].buf().append(translator::translate("km"));
					break;
				case gui_chart_t::STANDARD:
				default:
					lb_player_val[player_nr].buf().append(value);
					break;
			}
			cont_players.add_component(&lb_player_val[player_nr]);

			if (show_share && total) {
				lb_player_val[player_nr].buf().printf(" (%.1f%%)", (double)value*100/total);
			}
			lb_player_val[player_nr].set_color(color);
			lb_player_val[player_nr].set_align(gui_label_t::right);
			lb_player_val[player_nr].update();
		}
	}

	if( count ) {
		scrolly.set_visible(true);
		cont_players.set_size(cont_players.get_min_size());
		scrolly.set_size(cont_players.get_min_size());
		scrolly.set_min_width(cont_players.get_min_size().w);
	}
	else {
		cont_players.new_component<gui_fill_t>(); // keep the size so that the combo box position does not move
		scrolly.set_visible(false);
	}
}

sint64 player_ranking_frame_t::get_value_from_history(player_t* player, uint8 offset) const
{
	sint64 value = 0;
	if( player ) {
		const bool is_atv = history_type_idx[selected_item * 2];
		const finance_t* finance = player->get_finance();
		switch (selected_hist_mode) {
			case AC_HISTORY_MONTHS:
				value = is_atv ? finance->get_history_veh_month((transport_type)player_ranking_frame_t::transport_type_option, offset, history_type_idx[selected_item * 2 + 1])
					: finance->get_history_com_month(offset, history_type_idx[selected_item * 2 + 1]);
				break;

			default:
			case AC_HISTORY_YEARS:
				value = is_atv ? finance->get_history_veh_year((transport_type)player_ranking_frame_t::transport_type_option, offset, history_type_idx[selected_item * 2 + 1])
					: finance->get_history_com_year(offset, history_type_idx[selected_item * 2 + 1]);
				break;
		}
	}
	return value;
}

bool player_ranking_frame_t::is_chart_table_zero(uint8 player_nr) const
{
	// search for any non-zero values
	if (player_t* player = welt->get_player(player_nr)) {
		int age = (int)player->get_age();
		const bool is_atv = history_type_idx[selected_item * 2];
		for (int y = 0; y < MAX_PLAYER_HISTORY_MONTHS; y++) {
			if (get_value_from_history(player, y)) return false;
			if (age-y<0) break;
		}
	}

	return true;
}

/**
 * This method is called if an action is triggered
 */
bool player_ranking_frame_t::action_triggered(gui_action_creator_t* comp, value_t v)
{
	// Check the GUI list of buttons
	// player filter
	for(int np=0; np<MAX_PLAYER_COUNT-1; np++) {
		if( welt->get_player(np) ){
			for (auto bt : player_buttons) {
				if (comp==bt) {
					if (bt->pressed) {
						selected_player = 255;
					}
					else {
						selected_player = bt->get_player_nr();
					}
					bt->pressed ^= 1;

					update_chart(false);
					return true;
				}
			}
		}
	}

	// chart selector
	for (uint8 i = 0; i < MAX_PLAYER_RANKING_CHARTS; i++) {
		if (comp == &bt_charts[i]) {
			if (bt_charts[i].pressed) {
				// nothing to do
				return true;
			}
			bt_charts[i].pressed ^= 1;
			selected_item = i;
			update_chart();
			return true;
		}
	}

	if(  comp == &transport_type_c) {
		int tmp = transport_type_c.get_selection();
		if((0 <= tmp) && (tmp < transport_type_c.count_elements())) {
			transport_type_option = (uint8)tmp;
			transport_type_option = transport_types[tmp];
			update_chart();
		}
	}

	if( comp == &cb_x_axis_selector ) {
		player_ranking_frame_t::selected_x_axis = cb_x_axis_selector.get_selection();
		update_chart();
	}
	if( comp == &cb_chart_span_selector ) {
		player_ranking_frame_t::selected_hist_mode = cb_chart_span_selector.get_selection();
		update_x_axis_selector();
		update_chart();
	}

	return true;
}


void player_ranking_frame_t::update_chart(bool init_player_button)
{
	last_month = welt->get_last_month();

	int total_x = cb_x_axis_selector.count_elements();
	if (player_ranking_frame_t::selected_x_axis> total_x) {
		player_ranking_frame_t::selected_x_axis = min(1, total_x);
	}
	cb_x_axis_selector.set_selection(player_ranking_frame_t::selected_x_axis);
	chart.set_highlight_x(player_ranking_frame_t::selected_x_axis);

	// deselect chart buttons
	for (uint8 i=0; i < MAX_PLAYER_RANKING_CHARTS; i++) {
		if (i != selected_item) {
			bt_charts[i].pressed = false;
		}
	}

	// need to clear the chart once to update the suffix and digit
	chart.remove_curves();
	if (init_player_button) {
		remove_player_buttons();
		player_buttons.clear();
	}
	const bool is_atv = history_type_idx[selected_item*2];
	for (int np = 0; np < MAX_PLAYER_COUNT - 1; np++) {
		if (np == PUBLIC_PLAYER_NR && selected_item != PR_HALTS) continue;
		if ( player_t* player = welt->get_player(np) ) {
			if (is_chart_table_zero(np)) continue;
			// create chart
			const int curve_type = (int)cost_type[selected_item*2+1];
			const int curve_precision=cost_type[selected_item*2];
			gui_chart_t::chart_marker_t marker = (np==selected_player) ? gui_chart_t::square : gui_chart_t::none;
			const int elements = selected_hist_mode== AC_HISTORY_YEARS ?
				min(total_x, player->get_age()+1) :
				min(total_x, player->get_player_age()+1);
			chart.add_curve(color_idx_to_rgb( player->get_player_color1()+env_t::gui_player_color_dark), *p_chart_table, MAX_PLAYER_COUNT-1, np, elements, curve_type, true, false, curve_precision, selected_item==PR_WAY_KILOMETREAGE ? convert_waylength:NULL, marker);

			if(init_player_button) {
				player_button_t* b = new player_button_t(np);
				b->add_listener(this);
				if (np == selected_player) {
					b->pressed = true;
				}
				player_buttons.append(b);
			}

			// update chart records
			for (int x = 0; x < total_x; x++) {
				p_chart_table[x][np] = get_value_from_history(player, x);
			}
			chart.show_curve(np);
		}
	}
	chart.set_dimension(total_x, 10000);
	transport_type_c.set_visible(is_atv);

	sort_player();

	reset_min_windowsize();
}

void player_ranking_frame_t::update_x_axis_selector()
{
	cb_x_axis_selector.clear_elements();
	switch (selected_hist_mode) {
	case AC_HISTORY_MONTHS:
		chart.set_seed(0);
		// update x-axis selector
		for (uint8 m = 0; m < MAX_PLAYER_HISTORY_MONTHS; m++) {
			sprintf(years_str[m], "%i", m);
			cb_x_axis_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(years_str[m], SYSCOL_TEXT);
			if (m >= welt->get_public_player()->get_player_age()) {
				break;
			}
		}
		break;
	default:
	case AC_HISTORY_YEARS:
		chart.set_seed(welt->get_last_year());
		// update x-axis selector
		for (uint8 y = 0; y < MAX_PLAYER_HISTORY_YEARS; y++) {
			sprintf(years_str[y], "%i", world()->get_last_year() - y);
			cb_x_axis_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(years_str[y], SYSCOL_TEXT);
			if (y >= world_age) {
				break;
			}
		}
		break;
	}
}

void player_ranking_frame_t::update_buttons()
{
	for (auto bt : player_buttons) {
		bt->update();
	}
	scrolly.set_min_width(cont_players.get_min_size().w);
}

/**
 * Draw the component
 */
void player_ranking_frame_t::draw(scr_coord pos, scr_size size)
{
	if (last_month != world()->get_last_month()) {
		world_age = welt->get_public_player()->get_age();
		update_chart();
	}

	gui_frame_t::draw(pos, size);
}
