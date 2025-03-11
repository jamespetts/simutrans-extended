/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <string.h>

#include "../simcolor.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../network/network_cmd_ingame.h"
#include "../dataobj/environment.h"
#include "../dataobj/scenario.h"
#include "../dataobj/translator.h"

#include "simwin.h"
#include "../utils/simstring.h"
//#include "../player/ai_scripted.h"

#include "money_frame.h" // for the finances
#include "password_frame.h" // for the password
#include "player_frame_t.h"

#include "components/gui_divider.h"
#include "player_ranking_frame.h"


password_button_t::password_button_t()
{
	const bool has_locked_icon =
		(skinverwaltung_t::gadget
			&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_NOTLOCKED) != IMG_EMPTY
			&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_LOCKED) != IMG_EMPTY);
	init(has_locked_icon ? button_t::imagebox : button_t::box, "");
	set_state(EMPTY);
	set_rigid(true);
	set_tooltip(translator::translate("Name/password"));
}

void password_button_t::set_state(uint8 state) {
	if (state >= MAX_LOCKED_STATUS) return;

	const bool has_locked_icon =
		(skinverwaltung_t::gadget
			&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_NOTLOCKED) != IMG_EMPTY
			&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_LOCKED) != IMG_EMPTY);

	set_visible(true);
	switch (state)
	{
	case password_button_t::PENDING:
		if (has_locked_icon) {
			set_image(IMG_EMPTY);
		}
		else {
			background_color = color_idx_to_rgb(COL_YELLOW);
		}
		break;
	case password_button_t::NOT_LOCKED:
		if (has_locked_icon) {
			set_image(IMG_EMPTY);
		}
		else {
			background_color = color_idx_to_rgb(COL_GREEN);
		}
		break;
	case password_button_t::UNLOCKED:
		if (has_locked_icon) {
			set_image(skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_NOTLOCKED));
		}
		else {
			background_color = color_idx_to_rgb(COL_GREEN - 1);
		}
		break;
	case password_button_t::LOCKED:
		if (has_locked_icon) {
			set_image(skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_LOCKED));
		}
		else {
			background_color = color_idx_to_rgb(COL_RED);
		}
		break;
	default:
	case password_button_t::EMPTY:
		set_visible(false);
		break;
	}

	return;
}


ki_kontroll_t::ki_kontroll_t() :
	gui_frame_t( translator::translate("Spielerliste") )
{
	old_player_nr = welt->get_active_player_nr();
	set_table_layout(1,0);

	add_table(8, 0);

	const bool has_locked_icon =
		(  skinverwaltung_t::gadget
		&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_NOTLOCKED) != IMG_EMPTY
		&& skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_LOCKED)    != IMG_EMPTY );

	// table of takeover companies
	cont_company_takeovers.set_table_frame(true, true);
	cont_company_takeovers.set_table_layout(1, 0);
	cont_company_takeovers.add_table(2,1)->set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT, 0));
	cont_company_takeovers.new_component<gui_label_with_symbol_t>(translator::translate("available_company_takeovers:"), skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(1):IMG_EMPTY);
	cont_company_takeovers.new_component<gui_fill_t>();
	cont_company_takeovers.end_table();
	cont_company_takeovers.add_table(3,0)->set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE>>1), scr_size(D_MARGIN_RIGHT, D_V_SPACE));

	// header
	new_component_span<gui_label_t>("Spieler", SYSCOL_TEXT, gui_label_t::centered, 2);
	new_component<gui_image_t>(skinverwaltung_t::gadget ? skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_LOCKED) : IMG_EMPTY, 0, ALIGN_CENTER_V, true)->set_tooltip(translator::translate("Name/password"));
	new_component_span<gui_label_t>("Access", 4);
	new_component<gui_label_t>("Cash", SYSCOL_TEXT, gui_label_t::right);

	new_component_span<gui_border_t>(2);
	new_component<gui_border_t>();
	new_component_span<gui_border_t>(4);
	new_component<gui_border_t>();

	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

		const player_t *const player = welt->get_player(i);

		add_table(2,1);
		{
			// activate player buttons
			// .. not available for the two first players (first human and second public)
			if(	i >= 2	) {
				// AI button (small square)
				player_active[i-2].init(button_t::square_state, "");
				player_active[i-2].add_listener(this);
				player_active[i-2].set_rigid(false);
				add_component( player_active+(i-2) );
			}

			// Player select button (arrow)
			player_change_to[i].init(button_t::arrowright_state, "");
			player_change_to[i].add_listener(this);
			player_change_to[i].set_rigid(true);
			add_component(player_change_to+i);
		}
		end_table();

		// Prepare finances button
		player_get_finances[i].init( button_t::box | button_t::flexible, "");
		player_get_finances[i].background_color = PLAYER_FLAG | color_idx_to_rgb((player ? player->get_player_color1():i*8)+env_t::gui_player_color_bright);
		player_get_finances[i].add_listener(this);

		// Player type selector, Combobox
		player_select[i].set_focusable( false );

		// add table that contains these two buttons, only one of them will be visible
		add_table(1,0);
		// When adding new players, activate the interface
		player_select[i].set_selection(welt->get_settings().get_player_type(i));
		player_select[i].add_listener(this);

		add_component( player_get_finances+i );
		add_component( player_select+i );

		if(	player != NULL	) {
			player_get_finances[i].set_text( player->get_name() );
			player_select[i].set_visible(false);
		}
		else {
			player_get_finances[i].set_visible(false);
		}
		end_table();

		// password/locked button
		add_component(&player_lock[i]);
		player_lock[i].add_listener(this);

		// Access button
		new_component<gui_fill_t>();
		access_out[i].init(button_t::square_state, "");
		access_out[i].set_rigid(true);
		add_component(&access_out[i]);
		access_out[i].add_listener(this);
		access_in[i].set_rigid(true);
		add_component(&access_in[i]);
		new_component<gui_fill_t>();

		// Income label
		ai_income[i] = new_component<gui_label_buf_t>(MONEY_PLUS, gui_label_t::money_right);
		ai_income[i]->set_rigid(true);

		// takeover buttons for takeovers table
		take_over_player[i].init(button_t::roundbox, translator::translate("take_over"), scr_coord(0,0), D_BUTTON_SIZE);
		take_over_player[i].add_listener(this);
		cont_company_takeovers.add_component(&take_over_player[i]);

		cont_company_takeovers.add_component(&lb_take_over_player[i]);
		lb_take_over_cost[i].set_align(gui_label_t::money_right);
		cont_company_takeovers.add_component(&lb_take_over_cost[i]);

	}

	// freeplay mode
	freeplay.init( button_t::square_state, "freeplay mode");
	freeplay.add_listener(this);
	freeplay.pressed = welt->get_settings().is_freeplay();
	end_table();

	bt_open_ranking.init(button_t::roundbox_state, "Player ranking");
	if (skinverwaltung_t::open_window) {
		bt_open_ranking.set_image(skinverwaltung_t::open_window->get_image_id(0));
		bt_open_ranking.set_image_position_right(true);
	}	bt_open_ranking.add_listener(this);
	bt_open_ranking.set_tooltip(translator::translate("Open the player ranking dialog"));
	add_component(&bt_open_ranking);

	add_component( &freeplay );

	new_component<gui_divider_t>();

	cont_company_takeovers.end_table();
	add_component(&cont_company_takeovers);

	allow_take_over_of_company.init(button_t::roundbox, "allow_takeover_of_your_company");
	allow_take_over_of_company.add_listener(this);
	allow_take_over_of_company.set_tooltip(translator::translate("allows_other_players_to_take_over_your_company"));
	add_component(&allow_take_over_of_company);

	update_data(); // calls reset_min_windowsize

	set_windowsize(get_min_windowsize());
}


/**
 * This method is called if an action is triggered
 */
bool ki_kontroll_t::action_triggered( gui_action_creator_t *comp,value_t p )
{
	// Free play button?
	if(	comp == &freeplay	) {
		welt->call_change_player_tool(karte_t::toggle_freeplay, 255, 0);
		return true;
	}

	// Check the GUI list of buttons
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {
		if(	i>=2	&&	comp == (player_active+i-2)	) {
			// switch AI on/off
			if(	welt->get_player(i)==NULL	) {
				// create new AI
				welt->call_change_player_tool(karte_t::new_player, i, player_select[i].get_selection());

				//// if scripted ai without script -> open script selector window
				//ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i));
				//if (ai	&&	!ai->has_script()	&&	(!env_t::networkmode	||	env_t::server)) {
				//	create_win( new ai_selector_t(i), w_info, magic_finances_t + i );
				//}
			}
			else {
				// If turning on again, reload script
				//if (!env_t::networkmode	&&	!welt->get_player(i)->is_active()) {
				//	if (ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i))) {
				//		ai->reload_script();
				//	}
				//}
				// Current AI on/off
				welt->call_change_player_tool(karte_t::toggle_player_active, i, !welt->get_player(i)->is_active());
			}
			break;
		}

		// Finance button pressed
		if(	comp == (player_get_finances+i)	) {
			// get finances
			player_get_finances[i].pressed = false;
			//// if scripted ai without script -> open script selector window
			//ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(welt->get_player(i));
			//if (ai	&&	!ai->has_script()	&&	(!env_t::networkmode	||	env_t::server)) {
			//	create_win( new ai_selector_t(i), w_info, magic_finances_t + i );
			//}
			//else {
				create_win( new money_frame_t(welt->get_player(i)), w_info, magic_finances_t + i );
			//}
			break;
		}

		// Changed active player
		if(	comp == (player_change_to+i)	) {
			// make active player
			player_t *const prevplayer = welt->get_active_player();
			welt->switch_active_player(i,false);

			// unlocked public service player can change into any company in multiplayer games
			player_t *const player = welt->get_active_player();
			if(	env_t::networkmode	&&	prevplayer == welt->get_public_player()	&&	!prevplayer->is_locked()	&&	player->is_locked()	) {
				player->unlock(false, true);

				// send unlock command
				nwc_auth_player_t *nwc = new nwc_auth_player_t();
				nwc->player_nr = player->get_player_nr();
				network_send_server(nwc);
			}

			break;
		}

		// Change player name and/or password
		if(	comp == (&player_lock[i])	&&	welt->get_player(i)	) {
			if (!welt->get_player(i)->is_unlock_pending()) {
				// set password
				create_win(new password_frame_t(welt->get_player(i)), w_info, magic_pwd_t + i );
				player_lock[i].pressed = false;
			}
		}

		// New player assigned in an empty slot
		if(	comp == (player_select+i)	) {

			// make active player
			if(	p.i<player_t::MAX_AI	&&	p.i>0	) {
				player_active[i-2].set_visible(true);
				welt->get_settings().set_player_type(i, (uint8)p.i);
			}
			else {
				player_active[i-2].set_visible(false);
				player_select[i].set_selection(0);
				welt->get_settings().set_player_type(i, 0);
			}
			break;
		}

		// Allow access to the selected player
		if(comp == access_out + i)
		{
			access_out[i].pressed = !access_out[i].pressed;
			player_t* player = welt->get_player(i);
			tooltip_out[i].clear();
			if (access_out[i].pressed && player)
			{
				tooltip_out[i].printf(translator::translate("Withdraw %s's access your ways and stops"), player->get_name());
			}
			else if (player)
			{
				tooltip_out[i].printf(translator::translate("Allow %s to access your ways and stops"), player->get_name());
			}

			static char param[16];
			sprintf(param,"g%hu,%i,%i", (uint16)welt->get_active_player_nr(), i, (int)access_out[i].pressed);
			tool_t *tool = create_tool( TOOL_ACCESS_TOOL | SIMPLE_TOOL );
			tool->set_default_param(param);
			welt->set_tool( tool, welt->get_active_player() );
			// since init always returns false, it is save to delete immediately
			delete tool;
		}

		if (comp == take_over_player + i)
		{
			const uint8 current_player_nr = welt->get_active_player_nr();
			if (i == current_player_nr) {
				// cancel take over
				static char param[16];
				sprintf(param, "t, %hi, %hi", welt->get_active_player_nr(), false);
				tool_t* tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
				tool->set_default_param(param);
				welt->set_tool(tool, welt->get_active_player());
				// since init always returns false, it is save to delete immediately
				delete tool;
			}
			else {
				static char param[16];
				sprintf(param, "u, %hu, %hu", (uint16)current_player_nr, (uint16)i);
				tool_t* tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
				tool->set_default_param(param);
				welt->set_tool(tool, welt->get_active_player());
				// since init always returns false, it is save to delete immediately
				delete tool;
				take_over_player[i].disable(); // Fail proof, in case the entry stays in the window
			}
		}
	}

	if (comp == &allow_take_over_of_company)
	{
		static char param[16];
		sprintf(param, "t, %hi, %hi", welt->get_active_player_nr(), true);
		tool_t* tool = create_tool(TOOL_CHANGE_PLAYER | SIMPLE_TOOL);
		tool->set_default_param(param);
		welt->set_tool(tool, welt->get_active_player());
		// since init always returns false, it is save to delete immediately
		delete tool;

		//update_data();
	}
	else if ( comp==&bt_open_ranking ) {
		create_win(new player_ranking_frame_t(), w_info, magic_player_ranking);
	}

	return true;
}


void ki_kontroll_t::update_data()
{
	// switching active player allowed?
	bool player_change_allowed = welt->get_settings().get_allow_player_change() || !welt->get_public_player()->is_locked();

	// activate player etc allowed?
	bool player_tools_allowed = true;

	// check also scenario rules
	if (welt->get_scenario()->is_scripted()) {
		player_tools_allowed = welt->get_scenario()->is_tool_allowed(NULL, TOOL_SWITCH_PLAYER | SIMPLE_TOOL);
		player_change_allowed &= player_tools_allowed;
	}

	freeplay.enable();
	if (welt->get_public_player()->is_locked() || !welt->get_settings().get_allow_player_change()	||	!player_tools_allowed) {
		freeplay.disable();
	}

	uint8 takeovers_count= 0; // Trigger to display the component

	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

		if(	player_t *player = welt->get_player(i)	) {

			const bool available_for_takeover = player->available_for_takeover();
			if (available_for_takeover) takeovers_count++;

			player_select[i].set_visible(false);
			player_get_finances[i].set_visible(true);
			player_change_to[i].set_visible(player_change_allowed);

			// scripted ai without script get different button without color
			//ai_scripted_t *ai = dynamic_cast<ai_scripted_t*>(player);

			//if (ai	&&	!ai->has_script()	&&	(!env_t::networkmode	||	env_t::server)) {
			//	player_get_finances[i].set_typ(button_t::roundbox | button_t::flexible);
			//	player_get_finances[i].set_text("Load Scripted AI");
			//}
			//else {
				player_get_finances[i].set_typ(button_t::box | button_t::flexible);
				player_get_finances[i].set_text(player->get_name());
			//}

			player_get_finances[i].background_color = PLAYER_FLAG | color_idx_to_rgb(player->get_player_color1()+env_t::gui_player_color_bright);
			if (skinverwaltung_t::alerts) {
				if (player->get_allow_voluntary_takeover()) {
					player_get_finances[i].set_image(skinverwaltung_t::alerts->get_image_id(1));
				}
				else {
					const player_t::solvency_status ss = player->check_solvency();
					if (ss == player_t::in_liquidation) {
						player_get_finances[i].set_image(skinverwaltung_t::alerts->get_image_id(4));
					}
					else if (ss == player_t::in_administration) {
						player_get_finances[i].set_image(skinverwaltung_t::alerts->get_image_id(3));
					}
					else {
						player_get_finances[i].set_image(IMG_EMPTY);
					}
				}
			}

			// always update locking status
			uint8 locked_state = password_button_t::NOT_LOCKED;
			if (player && !player->access_password_hash().empty()) {
				locked_state = player->is_locked() ? (player->is_unlock_pending() ? password_button_t::PENDING : password_button_t::LOCKED) : password_button_t::UNLOCKED;
			}
			player_lock[i].set_state(locked_state);

			// human players cannot be deactivated
			if (i>1) {
				player_active[i-2].set_visible( player->get_ai_id()!=player_t::HUMAN	 &&	player_tools_allowed);
			}

			tooltip_out[i].clear();
			access_out[i].pressed = player && welt->get_active_player()->allows_access_to(player->get_player_nr());
			if(access_out[i].pressed && player)
			{
				tooltip_out[i].printf(translator::translate("Withdraw %s's access your ways and stops"), player->get_name());
			}
			else if(player)
			{
				tooltip_out[i].printf(translator::translate("Allow %s to access your ways and stops"), player->get_name());
			}
			access_out[i].set_tooltip(tooltip_out[i]);
			access_out[i].set_visible(i != welt->get_active_player_nr());

			const bool allow_access = player && player->allows_access_to(welt->get_active_player_nr());
			tooltip_in[i].clear();
			if (allow_access) {
				tooltip_in[i].printf(translator::translate("%s allows you to access its ways and stops"), player->get_name());
			}
			else if (player) {
				tooltip_in[i].printf(translator::translate("%s does not allow you to access its ways and stops"), player->get_name());
			}
			access_in[i].set_visible(i != welt->get_active_player_nr());
			access_in[i].init(allow_access ? COL_CLEAR : COL_DANGER, scr_size(LINEASCENT, LINEASCENT), true, true);
			access_in[i].set_tooltip(tooltip_in[i]);

			take_over_player[i].set_visible(available_for_takeover);
			lb_take_over_player[i].set_visible(available_for_takeover);
			lb_take_over_cost[i].set_visible(available_for_takeover);
			if (available_for_takeover) {
				lb_take_over_player[i].buf().clear();
				lb_take_over_player[i].buf().append(player->get_name());
				lb_take_over_player[i].update();

				if (player == welt->get_active_player()) {
					take_over_player[i].set_text("Cancel");
					take_over_player[i].set_tooltip(translator::translate("cancel_the_takeover_of_your_company"));
				}
				else {
					take_over_player[i].set_text("take_over");
					take_over_player[i].set_tooltip(translator::translate("take_over_this_company"));
				}

				// If this company is available to be taken over, disable the take over buttons for the others
				bool can_afford = !welt->get_active_player()->available_for_takeover() &&
					(welt->get_active_player_nr()!= PUBLIC_PLAYER_NR && welt->get_active_player()->can_afford(player->calc_takeover_cost()));
				take_over_player[i].enable( !welt->get_active_player()->is_locked() && (can_afford || player==welt->get_active_player() ) );
			}

			ai_income[i]->set_visible(true);
		}
		else {

			// inactive player => button needs removal?
			player_get_finances[i].set_visible(false);
			player_change_to[i].set_visible(false);
			player_select[i].set_visible(player_tools_allowed);
			player_lock[i].set_state(password_button_t::EMPTY);

			if (i>1) {
				player_active[i-2].set_visible(0 < player_select[i].get_selection()	&&	player_select[i].get_selection() < player_t::MAX_AI);
			}

			// Create combobox list data
			int select = player_select[i].get_selection();
			player_select[i].clear_elements();
			player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("slot empty"), SYSCOL_TEXT ) ;
			player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Manual (Human)"), SYSCOL_TEXT ) ;
			if(	!welt->get_public_player()->is_locked()	||	!env_t::networkmode	) {
				player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Goods AI"), SYSCOL_TEXT ) ;
				player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Passenger AI"), SYSCOL_TEXT ) ;

				//if (!env_t::networkmode	||	env_t::server) {
				//	// only server can start scripted players
				//	player_select[i].new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Scripted AI's"), SYSCOL_TEXT ) ;
				//}
			}
			player_select[i].set_selection(select);
			//assert(	player_t::MAX_AI==5	);

			access_out[i].set_visible(false);
			access_in[i].set_visible(false);

			ai_income[i]->set_visible(false);

			take_over_player[i].set_visible(false);
			lb_take_over_player[i].set_visible(false);
			lb_take_over_cost[i].set_visible(false);
		}
	}

	cont_company_takeovers.set_visible(takeovers_count);

	allow_take_over_of_company.enable( (welt->get_active_player_nr()!=PUBLIC_PLAYER_NR && !welt->get_active_player()->get_allow_voluntary_takeover() && !welt->get_active_player()->is_locked() ) );

	update_income();

	reset_min_windowsize();
}


void ki_kontroll_t::update_income()
{
	// Update finance
	for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {
		ai_income[i]->buf().clear();
		player_t *player = welt->get_player(i);
		if(	player != NULL	) {
			const player_t::solvency_status ss = player->check_solvency();
			if (ss == player_t::in_liquidation)
			{
				ai_income[i]->set_color( color_idx_to_rgb(COL_DARK_RED) );
				ai_income[i]->set_align(gui_label_t::centered);
				ai_income[i]->buf().append(translator::translate("in_liquidation"));
			}
			else if (ss == player_t::in_administration)
			{
				ai_income[i]->set_color( color_idx_to_rgb(COL_DARK_BLUE) );
				ai_income[i]->set_align(gui_label_t::centered);
				ai_income[i]->buf().append(translator::translate("in_administration"));
			}
			else
			{
				double account=player->get_account_balance_as_double();
				char str[128];
				money_to_string(str, account );
				ai_income[i]->buf().append(str);
				ai_income[i]->set_color( account>=0.0 ? MONEY_PLUS : MONEY_MINUS );
				ai_income[i]->set_align(gui_label_t::money_right);
			}

			if ( player->available_for_takeover() ) {
				char str[128];
				money_to_string(str, player->calc_takeover_cost() / 100, true);
				lb_take_over_cost[i].buf().append(str);
			}
		}
		ai_income[i]->update();
		lb_take_over_cost[i].update();
	}
}

/**
 * Draw the component
 */
void ki_kontroll_t::draw(scr_coord pos, scr_size size)
{
	// Update free play
	freeplay.pressed = welt->get_settings().is_freeplay();
	if (welt->get_public_player()->is_locked() || !welt->get_settings().get_allow_player_change()) {
		freeplay.disable();
	}
	else {
		freeplay.enable();
	}

	// Update finance
	update_income();

	if (old_player_nr!=welt->get_active_player_nr()) {
		old_player_nr = welt->get_active_player_nr();
		update_data();
	}
	else {
		// Update buttons
		for(int i=0; i<MAX_PLAYER_COUNT-1; i++) {

			player_t *player = welt->get_player(i);

			player_change_to[i].pressed = false;
			if(i>=2) {
				player_active[i-2].pressed = player !=NULL	&&	player->is_active();
			}

			if (player_t* player = welt->get_player(i)) {
				uint8 locked_state = password_button_t::NOT_LOCKED;
				if (player && !player->access_password_hash().empty()) {
					locked_state = player->is_locked() ? (player->is_unlock_pending() ? password_button_t::PENDING : password_button_t::LOCKED) : password_button_t::UNLOCKED;
				}
				player_lock[i].set_state(locked_state);
			}
			else {
				player_lock[i].set_state(password_button_t::EMPTY);
			}
		}
	}

	player_change_to[welt->get_active_player_nr()].pressed = true;
	bt_open_ranking.pressed = win_get_magic(magic_player_ranking);

	// All controls updated, draw them...
	gui_frame_t::draw(pos, size);
}
