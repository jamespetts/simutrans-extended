/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "consist_order_gui.h"
#include "../bauer/goods_manager.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"

consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t("", player),
	scl(gui_scrolled_list_t::listskin)
{
	this->player = player;
	this->schedule = schedule;
	unique_entry_id = entry_id;
	init_table();
}


void consist_order_frame_t::init(player_t* player, schedule_t *schedule, uint16 entry_id)
{
	this->player = player;
	this->schedule = schedule;
	unique_entry_id = entry_id;

	init_table();
}

void consist_order_frame_t::init_table()
{
	old_entry_count = schedule->get_count();
	order=schedule->orders.get(unique_entry_id);
	halt = schedule->get_halt(player, unique_entry_id);
	if( !halt.is_bound() ) {
		destroy_win(this);
		return;
	}
	remove_all();
	set_table_layout(1,0);
	gui_frame_t::set_name("consist_order"); // TODO:

	set_table_layout(1,0);


	new_component<gui_label_t>(halt->get_name());

	scl.set_size(scr_size(D_LABEL_WIDTH<<1, LINESPACE*4));
	scl.set_maximize(true);
	scl.add_listener(this);
	add_component(&scl);

	add_table(3,1)->set_force_equal_columns(true);
	{
		bt_new.init(   button_t::roundbox | button_t::flexible, "New order");
		bt_new.add_listener(this);
		add_component(&bt_new);
		bt_edit.init(  button_t::roundbox | button_t::flexible, "Edit order");
		bt_edit.add_listener(this);
		add_component(&bt_edit);
		bt_delete.init(button_t::roundbox | button_t::flexible, "Delete order");
		bt_delete.add_listener(this);
		add_component(&bt_delete);
	}
	end_table();


	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(diagonal_resize);
}

void consist_order_frame_t::set_convoy(convoihandle_t cnv)
{
}

void consist_order_frame_t::update()
{
	// update order list
	scl.clear_elements();
	scl.set_selection(-1);
	resize(scr_size(0,0));

}

void consist_order_frame_t::draw(scr_coord pos, scr_size size)
{
	if( schedule->get_count() != old_entry_count ) {
		// Check if the entry has been deleted
		halt = schedule->get_halt(player, unique_entry_id);
		if( !halt.is_bound() ) {
			destroy_win(this);
		}
	}

	gui_frame_t::draw(pos, size);
}

bool consist_order_frame_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( comp == &scl ) {
		scl.get_selection();
	}
	if( comp==&bt_new ) {
		scl.set_selection(-1);
	}
	if( comp==&bt_edit ) {
	}
	if( comp==&bt_delete ) {
		update();
	}


	return false;
}
