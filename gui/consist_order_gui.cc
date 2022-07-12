/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "consist_order_gui.h"
#include "../bauer/goods_manager.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"
#include "../simworld.h"


#define L_OWN_VEHICLE_COUNT_WIDTH (proportional_string_width("8,888") + D_H_SPACE)
#define L_OWN_VEHICLE_LABEL_OFFSET_LEFT (L_OWN_VEHICLE_COUNT_WIDTH + VEHICLE_BAR_HEIGHT*4+D_H_SPACE)

vehicle_scrollitem_t::vehicle_scrollitem_t(own_vehicle_t own_veh_)
	: gui_label_t(own_veh_.veh_type->get_name(), SYSCOL_TEXT)
{
	own_veh = own_veh_;

	set_focusable(true);
	focused = false;
	selected = false;

	label.buf().printf("%5u", own_veh.count);
	label.update();
	label.set_fixed_width(proportional_string_width("8,888"));

	// vehicle color bar
	uint16 month_now = world()->get_current_month();
	const PIXVAL veh_bar_color = own_veh.veh_type->is_obsolete(month_now) ? COL_OBSOLETE : (own_veh.veh_type->is_future(month_now) || own_veh.veh_type->is_retired(month_now)) ? COL_OUT_OF_PRODUCTION : COL_SAFETY;
	colorbar.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
	colorbar.init(veh_bar_color);
}


scr_size vehicle_scrollitem_t::get_min_size() const
{
	return scr_size(L_OWN_VEHICLE_LABEL_OFFSET_LEFT + gui_label_t::get_min_size().w,LINESPACE);
}


void vehicle_scrollitem_t::draw(scr_coord offset)
{
	if (selected) {
		display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h+1, (focused ? SYSCOL_LIST_BACKGROUND_SELECTED_F : SYSCOL_LIST_BACKGROUND_SELECTED_NF), true);
	}

	label.draw(pos+offset + scr_coord(D_H_SPACE,0));
	colorbar.draw(pos+offset + scr_coord(L_OWN_VEHICLE_COUNT_WIDTH, D_GET_CENTER_ALIGN_OFFSET(colorbar.get_size().h, get_size().h)));
	gui_label_t::draw(offset+scr_coord(L_OWN_VEHICLE_LABEL_OFFSET_LEFT,0));
}



consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t("", player),
	scl(gui_scrolled_list_t::listskin)
{
	this->player = player;
	this->schedule = schedule;
	if (unique_entry_id != entry_id) {
		unique_entry_id = entry_id;
		init_table();
	}
}


void consist_order_frame_t::init(player_t* player, schedule_t *schedule, uint16 entry_id)
{
	this->player = player;
	this->schedule = schedule;
	if (unique_entry_id != entry_id) {
		unique_entry_id = entry_id;
		init_table();
	}
}

void consist_order_frame_t::init_table()
{
	remove_all();
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
	resize(scr_size(0,0));
	set_resizemode(diagonal_resize);
}

// copy selected convoy's order
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
	if (player != welt->get_active_player()) { destroy_win(this); }
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
