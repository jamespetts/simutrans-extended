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
#include "player_ranking_gui.h"


uint8 player_ranking_gui_t::transport_type_option = TT_ALL;
uint8 player_ranking_gui_t::selected_year = 1;

// Text should match that of the finance dialog
static const char* cost_type_name[player_ranking_gui_t::MAX_PLAYER_RANKING_CHARTS] =
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
static const uint8 cost_type[player_ranking_gui_t::MAX_PLAYER_RANKING_CHARTS*2] =
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

static const uint8 cost_type_color[player_ranking_gui_t::MAX_PLAYER_RANKING_CHARTS] =
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
static const uint8 history_type_idx[player_ranking_gui_t::MAX_PLAYER_RANKING_CHARTS*2] =
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

static int compare_atv(uint8 player_nr_a, uint8 player_nr_b, uint8 atv_index) {
	int comp = 0;
	player_t* a_player = world()->get_player(player_nr_a);
	player_t* b_player = world()->get_player(player_nr_b);

	if (a_player && b_player) {
		comp = b_player->get_finance()->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, player_ranking_gui_t::selected_year, atv_index) - a_player->get_finance()->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, player_ranking_gui_t::selected_year, atv_index);
		if (comp==0 && player_ranking_gui_t::selected_year) {
			comp = b_player->get_finance()->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, 0, atv_index) - a_player->get_finance()->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, 0, atv_index);
		}
	}
	if (comp==0) {
		comp = player_nr_b - player_nr_a;
	}
	return comp;
}

static int compare_atc(uint8 player_nr_a, uint8 player_nr_b, uint8 atc_index) {
	int comp = 0;
	player_t* a_player = world()->get_player(player_nr_a);
	player_t* b_player = world()->get_player(player_nr_b);

	if (a_player && b_player) {
		comp = b_player->get_finance()->get_history_com_year(player_ranking_gui_t::selected_year, atc_index) - a_player->get_finance()->get_history_com_year(player_ranking_gui_t::selected_year, atc_index);
		if (comp == 0) {
			comp = b_player->get_finance()->get_history_com_year(0, atc_index) - a_player->get_finance()->get_history_com_year(0, atc_index);
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


player_ranking_gui_t::player_ranking_gui_t(uint8 selected_player_nr) :
	gui_frame_t(translator::translate("Player ranking")),
	scrolly(&cont_players, true, true)
{
	selected_player = selected_player_nr;
	last_year = welt->get_last_year();
	const uint16 world_age = ((12 + welt->get_last_month()-welt->get_public_player()->get_player_age()%12)%12+ welt->get_public_player()->get_player_age())/12;
	player_ranking_gui_t::selected_year = min(1, world_age);

	set_table_layout(1,0);

	add_table(2,1)->set_alignment(ALIGN_TOP);
	{
		add_component(&chart);
		chart.set_dimension(MAX_PLAYER_HISTORY_YEARS, 10000);
		chart.set_seed(welt->get_last_year());
		chart.set_background(SYSCOL_CHART_BACKGROUND);
		chart.set_min_size(scr_size(24*LINESPACE, 11*LINESPACE));

		cont_players.set_table_layout(3,0);
		cont_players.set_alignment(ALIGN_CENTER_H);
		cont_players.set_margin(NO_SPACING, scr_size(D_SCROLLBAR_WIDTH+D_H_SPACE,D_SCROLLBAR_HEIGHT));

		for (int np = 0; np < MAX_PLAYER_COUNT; np++) {
			//if (np == PUBLIC_PLAYER_NR) continue; // Public player only appears in the ranking of number of stations owned.
			if (welt->get_player(np) ) {
				player_button_t* b = new player_button_t(np);
				b->add_listener(this);
				if (np==selected_player) {
					b->pressed = true;
				}
				buttons.append(b);
			}
		}

		sort_player();

		add_table(1,2);
		{
			add_table(3, 1);
			{
				cb_year_selector.add_listener(this);
				cb_year_selector.set_inverse_side_scroll(true);
				add_component(&cb_year_selector);

				for (int i = 0, count = 0; i < TT_OTHER; ++i) {
					if (i>0 && !way_builder_t::is_active_waytype(finance_t::translate_tt_to_waytype((transport_type)(i)))) continue;
					transport_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(finance_t::get_transport_type_name((transport_type)i)), SYSCOL_TEXT);
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
	transport_type_c.set_width_fixed(true);
	cb_year_selector.set_width_fixed(true);
	set_resizemode(diagonal_resize);
}

player_ranking_gui_t::~player_ranking_gui_t()
{
	while (!buttons.empty()) {
		player_button_t* b = buttons.remove_first();
		cont_players.remove_component(b);
		delete b;
	}
}


void player_ranking_gui_t::sort_player()
{
	cont_players.remove_all();
	switch (selected_item)
	{
		case PR_REVENUE:
			buttons.sort(compare_revenue);
			break;
		case PR_PROFIT:
			buttons.sort(compare_profit);
			break;
		case PR_TRANSPORT_PAX:
			buttons.sort(compare_transport_pax);
			break;
		case PR_TRANSPORT_MAIL:
			buttons.sort(compare_transport_mail);
			break;
		case PR_TRANSPORT_GOODS:
			buttons.sort(compare_transport_goods);
			break;
		case PR_CASH:
			buttons.sort(compare_cash);
			break;
		case PR_NETWEALTH:
			buttons.sort(compare_netwealth);
			break;
		case PR_MARGIN:
			buttons.sort(compare_margin);
			break;
		case PR_VEHICLES:
			buttons.sort(compare_vehicles);
			break;
		case PR_HALTS:
			buttons.sort(compare_halts);
			break;
		case PR_CONVOY_DISTANCE:
			buttons.sort(compare_convoy_km);
			break;
		case PR_VEHICLE_DISTANCE:
			buttons.sort(compare_vehicle_km);
			break;
		case PR_WAY_KILOMETREAGE:
			buttons.sort(compare_way_length);
			break;
		default:
		case PR_CONVOIS:
			buttons.sort(compare_convois);
			break;
	}
	uint8 count = 0;
	for (uint i = 0; i < buttons.get_count(); i++) {
		const uint8 player_nr = buttons.at(i)->get_player_nr();
		// Exclude players who are not in the competition
		if( is_chart_table_zero(player_nr) ) {
			continue;
		}
		// Public player only appers in stop number ranking
		if( player_nr==PUBLIC_PLAYER_NR && selected_item!=PR_HALTS ) {
			continue;
		}
		count++;

		switch (count)
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
				lb->buf().printf("%u", count);
				lb->update();
				lb->set_fixed_width(lb->get_min_size().w);
				break;
		}

		if( player_nr != selected_player ) {
			buttons.at(i)->pressed = false;
		}
		cont_players.add_component(buttons.at(i));

		cont_players.add_component(&lb_player_val[player_nr]);
	}


	if (count) {
		// update labels
		const bool is_atv = history_type_idx[selected_item * 2];

		// check totals.
		sint64 total=0;
		if( history_type_idx[selected_item*2+1]== ATV_TRANSPORTED_PASSENGER
			|| history_type_idx[selected_item*2+1] == ATV_TRANSPORTED_MAIL
			|| history_type_idx[selected_item*2+1] == ATV_TRANSPORTED_GOOD
			|| history_type_idx[selected_item*2+1] == ATV_WAY_LENGTH)
		{
			assert(is_atv);
			// Calculate the total value first
			for (uint i = 0; i < buttons.get_count(); i++) {
				const uint8 player_nr = buttons.at(i)->get_player_nr();
				const finance_t* finance = world()->get_player(player_nr)->get_finance();
				total += finance->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, player_ranking_gui_t::selected_year, history_type_idx[selected_item*2+1]);
			}

		}
		for (int np = 0; np < MAX_PLAYER_COUNT - 1; np++) {
			if( player_t* player = welt->get_player(np) ) {
				const finance_t* finance = player->get_finance();
				const bool is_atv = history_type_idx[selected_item * 2];
				PIXVAL color = SYSCOL_TEXT;
				sint64 value = is_atv ? finance->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, player_ranking_gui_t::selected_year, history_type_idx[selected_item * 2 + 1])
					: finance->get_history_com_year(player_ranking_gui_t::selected_year, history_type_idx[selected_item * 2 + 1]);
				switch (cost_type[selected_item*2+1]) {
					case gui_chart_t::MONEY:
						lb_player_val[np].buf().append_money(value / 100.0);
						color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;
						break;
					case gui_chart_t::PERCENT:
						lb_player_val[np].buf().append(value / 100.0, 2);
						lb_player_val[np].buf().append("%");
						color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;
						break;
					case gui_chart_t::PAX_KM:
						lb_player_val[np].buf().append(value / 10.0, 0);
						lb_player_val[np].buf().append(translator::translate("pkm"));
						break;
					case gui_chart_t::TON_KM_MAIL:
						lb_player_val[np].buf().append(value / 1000.0, 2);
						lb_player_val[np].buf().append(translator::translate("tkm"));
						break;
					case gui_chart_t::TON_KM:
						lb_player_val[np].buf().append(value / 10.0, 0);
						lb_player_val[np].buf().append(translator::translate("tkm"));
						break;
					case gui_chart_t::DISTANCE:
						if (selected_item==PR_WAY_KILOMETREAGE) {
							lb_player_val[np].buf().append(value * welt->get_settings().get_meters_per_tile() / 10000.0, 1);
						}
						else {
							lb_player_val[np].buf().append(value);
						}
						lb_player_val[np].buf().append(translator::translate("km"));
						break;
					case gui_chart_t::STANDARD:
					default:
						lb_player_val[np].buf().append(value);
						break;
				}
				if (total) {
					lb_player_val[np].buf().printf(" (%.1f%%)", (double)value*100/total);
				}
				lb_player_val[np].set_color(color);
				lb_player_val[np].set_align(gui_label_t::right);
				lb_player_val[np].update();
			}
		}
		cont_players.new_component<gui_fill_t>(false, true);
		scrolly.set_visible(true);
		cont_players.set_size(cont_players.get_min_size());
		scrolly.set_size(cont_players.get_min_size());
		scrolly.set_min_width(cont_players.get_min_size().w);
	}
	else{
		cont_players.new_component<gui_fill_t>(); // keep the size so that the combo box position does not move
		scrolly.set_visible(false);
	}
}

bool player_ranking_gui_t::is_chart_table_zero(uint8 player_nr) const
{
	// search for any non-zero values
	if( player_t* player = welt->get_player(player_nr) ) {
		const finance_t* finance = player->get_finance();
		const bool is_atv = history_type_idx[selected_item*2];
		for (int y = 0; y < MAX_PLAYER_HISTORY_MONTHS; y++) {
			sint64 val = is_atv ? finance->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, y, history_type_idx[selected_item * 2 + 1])
				: finance->get_history_com_year(y, history_type_idx[selected_item * 2 + 1]);
			if (val) return false;
		}
	}

	return true;
}

/**
 * This method is called if an action is triggered
 */
bool player_ranking_gui_t::action_triggered( gui_action_creator_t *comp,value_t p )
{
	// Check the GUI list of buttons
	// player filter
	for(int np=0; np<MAX_PLAYER_COUNT-1; np++) {
		if( welt->get_player(np) ){
			for (auto bt : buttons) {
				if (comp==bt) {
					if (bt->pressed) {
						selected_player = 255;
					}
					else {
						selected_player = bt->get_player_nr();
					}
					bt->pressed ^= 1;

					update_chart();
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

	if( comp == &cb_year_selector ) {
		player_ranking_gui_t::selected_year = cb_year_selector.get_selection();
		update_chart();
	}

	return true;
}


void player_ranking_gui_t::update_chart()
{
	// update year selector
	cb_year_selector.clear_elements();
	const uint16 world_age = ((12 + welt->get_last_month()-welt->get_public_player()->get_player_age()%12)%12+ welt->get_public_player()->get_player_age())/12;
	for (uint8 y = 0; y < MAX_PLAYER_HISTORY_YEARS; y++) {
		sprintf(years_str[y], "%i", world()->get_last_year()-y);
		cb_year_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(years_str[y], SYSCOL_TEXT);
		if (y>=world_age) {
			break;
		}
	}
	cb_year_selector.set_selection(player_ranking_gui_t::selected_year);
	chart.set_highlight_x(player_ranking_gui_t::selected_year);

	// deselect chart buttons
	for (uint8 i=0; i < MAX_PLAYER_RANKING_CHARTS; i++) {
		if (i != selected_item) {
			bt_charts[i].pressed = false;
		}
	}

	// need to clear the chart once to update the suffix and digit
	chart.remove_curves();
	for (int np = 0; np < MAX_PLAYER_COUNT - 1; np++) {
		if (np == PUBLIC_PLAYER_NR && selected_item != PR_HALTS) continue;
		if ( player_t* player = welt->get_player(np) ) {
			if (is_chart_table_zero(np)) continue;
			// create chart
			const int curve_type = (int)cost_type[selected_item*2+1];
			const int curve_precision=cost_type[selected_item*2];
			gui_chart_t::chart_marker_t marker = (np==selected_player) ? gui_chart_t::square : gui_chart_t::none;
			chart.add_curve(color_idx_to_rgb( player->get_player_color1()+3), *p_chart_table, MAX_PLAYER_COUNT-1, np, MAX_PLAYER_HISTORY_YEARS, curve_type, true, false, curve_precision, selected_item==PR_WAY_KILOMETREAGE ? convert_waylength:NULL, marker);
		}
	}

	const bool is_atv = history_type_idx[selected_item*2];
	for (int np = 0; np < MAX_PLAYER_COUNT - 1; np++) {
		if ( player_t* player = welt->get_player(np) ) {
			// update chart records
			for (int y = 0; y < MAX_PLAYER_HISTORY_YEARS; y++) {
				const finance_t* finance = player->get_finance();
				p_chart_table[y][np] = is_atv ? finance->get_history_veh_year((transport_type)player_ranking_gui_t::transport_type_option, y, history_type_idx[selected_item*2+1])
					: finance->get_history_com_year(y, history_type_idx[selected_item*2+1]);
			}
			chart.show_curve(np);
		}
	}
	transport_type_c.set_visible(is_atv);

	sort_player();

	reset_min_windowsize();
}


void player_ranking_gui_t::update_buttons()
{
	for (auto bt : buttons) {
		bt->update();
	}
	scrolly.set_min_width(cont_players.get_min_size().w);
}

/**
 * Draw the component
 */
void player_ranking_gui_t::draw(scr_coord pos, scr_size size)
{
	if (last_year != world()->get_last_year()) {
		last_year = world()->get_last_year();
		chart.set_seed(last_year);
		update_chart();
	}

	gui_frame_t::draw(pos, size);
}
