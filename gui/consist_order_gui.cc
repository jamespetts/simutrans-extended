/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "consist_order_gui.h"
#include "convoi_detail_t.h"
#include "convoy_item.h"
#include "simwin.h"
#include "messagebox.h"
#include "../bauer/goods_manager.h"
#include "../display/viewport.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"
#include "components/gui_divider.h"
#include "components/gui_table.h"
#include "../player/finance.h"


#define L_OWN_VEHICLE_COUNT_WIDTH (proportional_string_width("8,888") + D_H_SPACE)
#define L_OWN_VEHICLE_LABEL_OFFSET_LEFT (L_OWN_VEHICLE_COUNT_WIDTH + VEHICLE_BAR_HEIGHT*4+D_H_SPACE)


const char *vehicle_spec_texts[gui_simple_vehicle_spec_t::MAX_VEH_SPECS] =
{
	"Payload",
	"Range",
	"Power:",
	"Tractive force:",
	"Max. brake force:",
	"Max. speed:",
	"Gewicht",
	"Axle load:",
	"Running costs per km", // "Operation" Vehicle running costs per km
	"Fixed cost per month"
};


vehicle_scrollitem_t::vehicle_scrollitem_t(own_vehicle_t own_veh_)
	: gui_label_t(own_veh_.veh_type == nullptr ? "Vehicle not found" : own_veh_.veh_type->get_name(), SYSCOL_TEXT)
{
	own_veh = own_veh_;

	set_focusable(true);
	focused = false;
	selected = false;

	if( own_veh.veh_type!=nullptr ) {
		label.buf().printf("%5u", own_veh.count);
		label.update();
		label.set_fixed_width(proportional_string_width("8,888"));

		// vehicle color bar
		uint16 month_now = world()->get_current_month();
		const PIXVAL veh_bar_color = own_veh.veh_type->is_obsolete(month_now) ? SYSCOL_OBSOLETE : (own_veh.veh_type->is_future(month_now) || own_veh.veh_type->is_retired(month_now)) ? SYSCOL_OUT_OF_PRODUCTION : COL_SAFETY;
		colorbar.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar_edge.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar.init(veh_bar_color);
		colorbar_edge.init(SYSCOL_LIST_TEXT_SELECTED_FOCUS, colorbar.get_size()+scr_size(2,2));
	}
	else {
		set_color(SYSCOL_EDIT_TEXT_DISABLED);
	}
}


scr_size vehicle_scrollitem_t::get_min_size() const
{
	return scr_size(L_OWN_VEHICLE_LABEL_OFFSET_LEFT + gui_label_t::get_min_size().w,LINESPACE);
}


void vehicle_scrollitem_t::draw(scr_coord offset)
{
	scr_coord_val left = D_H_SPACE;
	if( own_veh.veh_type!=nullptr ) {
		if (selected) {
			display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h+1, (focused ? SYSCOL_LIST_BACKGROUND_SELECTED_F : SYSCOL_LIST_BACKGROUND_SELECTED_NF), true);
			color = SYSCOL_LIST_TEXT_SELECTED_FOCUS;
			label.set_color(SYSCOL_LIST_TEXT_SELECTED_FOCUS);
			colorbar_edge.draw(pos + offset + scr_coord(L_OWN_VEHICLE_COUNT_WIDTH-1, D_GET_CENTER_ALIGN_OFFSET(colorbar.get_size().h, get_size().h)-1));
		}
		else {
			color = SYSCOL_TEXT;
			label.set_color(SYSCOL_TEXT);
		}

		label.draw(pos+offset + scr_coord(left,0));
		colorbar.draw(pos+offset + scr_coord(L_OWN_VEHICLE_COUNT_WIDTH, D_GET_CENTER_ALIGN_OFFSET(colorbar.get_size().h, get_size().h)));
		left = L_OWN_VEHICLE_LABEL_OFFSET_LEFT;
	}
	gui_label_t::draw(offset+scr_coord(left,0));
}



void gui_simple_vehicle_spec_t::init_table()
{
	remove_all();
	set_table_layout(1,0);
	set_spacing(scr_size(D_H_SPACE,2));
	set_alignment(ALIGN_TOP);

	add_table(2,1);
	{
		// image
		add_table(1,2);
		{
			new_component<gui_image_t>(veh_type->get_base_image(), player_nr, 0, true);
			new_component<gui_margin_t>(get_base_tile_raster_width()>>1);
		}
		end_table();

		// capacity info
		add_table(1,0);
		{
			new_component<gui_label_t>("(dummy)capacity info here");
		}
		end_table();
	}
	end_table();

	gui_aligned_container_t *tbl = add_table(2,0);
	tbl->set_table_frame(true, true);
	tbl->set_margin(scr_size(3,3), scr_size(3,3));
	tbl->set_spacing(scr_size(D_H_SPACE, 2));
	tbl->set_alignment(ALIGN_CENTER_V);
	{
		for( uint8 i=1; i<MAX_VEH_SPECS; ++i ) {
			if( (i==SPEC_POWER || i== SPEC_TRACTIVE_FORCE) && !veh_type->get_power() ) {
				continue;
			}
			if( i==SPEC_AXLE_LOAD && veh_type->get_waytype()==water_wt ) {
				continue;
			}
			new_component<gui_table_header_t>(vehicle_spec_texts[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);

			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
			switch (i)
			{
				case SPEC_RANGE:
					if (veh_type->get_range() == 0) {
						lb->buf().append(translator::translate("unlimited"));
					}
					else {
						lb->buf().printf("%u km", veh_type->get_range());
					}
					break;
				case SPEC_POWER:
					if (!veh_type->get_power()) {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					else {
						lb->buf().printf("%u kW", veh_type->get_power());
					}
					break;
				case SPEC_TRACTIVE_FORCE:
					if (veh_type->get_power()) {
						lb->buf().printf("%u kN", veh_type->get_tractive_effort());
					}
					break;
				case SPEC_BRAKE_FORCE:
					if (veh_type->get_brake_force() != 0) {
						vehicle_as_potential_convoy_t convoy(*veh_type);
						lb->buf().printf("%.2f kN", convoy.get_braking_force().to_double() / 1000.0);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_INACTIVE);
					}
					break;
				case SPEC_SPEED:
					lb->buf().printf("%i km/h", veh_type->get_topspeed());
					break;
				case SPEC_WEIGHT:
					lb->buf().printf("%.1f", veh_type->get_weight() / 1000.0);
					if (veh_type->get_total_capacity() || veh_type->get_overcrowded_capacity()) {
						lb->buf().printf(" - %.1f", veh_type->get_max_loading_weight() / 1000.0);
					}
					lb->buf().append(" t");
					break;
				case SPEC_AXLE_LOAD:
					lb->buf().printf("%u t", veh_type->get_axle_load());
					break;
				case SPEC_RUNNING_COST:
					if (veh_type->get_running_cost()) {
						lb->buf().printf("%1.2f$", veh_type->get_running_cost(world()) / 100.0);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				case SPEC_FIXED_COST:
					if (veh_type->get_fixed_cost()) {
						lb->buf().printf("%1.2f$", veh_type->get_adjusted_monthly_fixed_cost() / 100.0);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				default:
					new_component<gui_empty_t>();
					break;
			}
			lb->update();
		}
	}
	end_table();

	new_component_span<gui_label_t>("DEBUG:",2);
	tbl = add_table(2,0);
	tbl->set_table_frame(true, true);
	tbl->set_margin(scr_size(3,3), scr_size(3,3));
	tbl->set_spacing(scr_size(D_H_SPACE, 2));
	tbl->set_alignment(ALIGN_CENTER_V);
	{
		new_component<gui_table_header_t>("is_bidirectional", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		lb->buf().printf("%s", veh_type->is_bidirectional() ? "true" : "false");
		lb->update();

		new_component<gui_table_header_t>("constraint_prev", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		const uint8 constraint_prev_bits = veh_type->get_basic_constraint_prev();
		if( (constraint_prev_bits&vehicle_desc_t::only_at_front) == vehicle_desc_t::only_at_front) {
			lb->buf().append("Cannot connect");
			lb->set_color(COL_DANGER);
		}
		else if( (constraint_prev_bits&vehicle_desc_t::intermediate_unique) > 0 ) {
			lb->buf().append(veh_type->get_leader(0)->get_name());
			lb->set_color(COL_WARNING);
		}
		else if (veh_type->get_leader_count()>1) {
			lb->buf().append("Connection is limited");
			lb->set_color(COL_CAUTION);
		}
		else if (!veh_type->get_leader_count()) {
			lb->buf().append("No restrictions");
			lb->set_color(COL_SAFETY);
		}
		else {
			lb->buf().append("unkown");
		}
		lb->update();

		new_component<gui_table_header_t>("constraint_next", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		const uint8 constraint_next_bits = veh_type->get_basic_constraint_next();
		if( (constraint_next_bits&vehicle_desc_t::only_at_end) == vehicle_desc_t::only_at_end) {
			lb->buf().append("Cannot connect");
			lb->set_color(COL_DANGER);
		}
		else if( (constraint_next_bits&vehicle_desc_t::intermediate_unique) > 0 ) {
			lb->buf().append(veh_type->get_trailer(0)->get_name());
			lb->set_color(COL_WARNING);
		}
		else if (veh_type->get_trailer_count() > 1) {
			lb->buf().append("Connection is limited");
			lb->set_color(COL_CAUTION);
		}
		else if (!veh_type->get_trailer_count()) {
			lb->buf().append("No restrictions");
			lb->set_color(COL_SAFETY);
		}
		else {
			lb->buf().append("unkown");
		}
		lb->update();
	}
	end_table();

	set_size(get_min_size());
}


gui_vehicle_description_t::gui_vehicle_description_t(consist_order_t *order, sint8 player_nr, uint32 order_element_index, uint32 description_index)
{
	this->order = order;
	this->order_element_index = order_element_index;
	this->description_index= description_index;

	// no., image, name - UI TODO: this is a temporary design
	set_table_layout(6,0);
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
	lb->buf().printf("%u", description_index+1);
	lb->update();

	bt_down.init(button_t::arrowdown, NULL);
	bt_down.enable(description_index<order->get_order(order_element_index).get_count()-1);
	bt_up.init(button_t::arrowup, NULL);
	bt_up.enable(description_index>0);
	bt_down.add_listener(this);
	bt_up.add_listener(this);
	add_component(&bt_up);
	add_component(&bt_down);

	const consist_order_element_t order_element = order->get_order(order_element_index);
	const vehicle_description_element element = order_element.get_vehicle_description(description_index);
	if( element.specific_vehicle ) {
		new_component<gui_image_t>(element.specific_vehicle->get_image_id(ribi_t::dir_northeast, goods_manager_t::none), player_nr, 0, true);
		new_component<gui_label_t>(element.specific_vehicle->get_name(), SYSCOL_TEXT_STRONG);
	}
	else {
		new_component<gui_label_t>("(???)", SYSCOL_TEXT_WEAK);
		new_component<gui_label_t>("(FIXME: dummy label)");
	}
	bt_remove.init(button_t::roundbox, "Remove");
	bt_remove.add_listener(this);
	add_component(&bt_remove);
}


bool gui_vehicle_description_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(  comp==&bt_up  ) {
		// TODO:
	}
	else if(  comp==&bt_down  ) {
		// TODO:

	}
	else if(  comp==&bt_remove  ) {
		consist_order_element_t *order_element = &order->get_order(order_element_index);
		order_element->remove_vehicle_description_at(description_index);
	}
	return true;
}


cont_order_overview_t::cont_order_overview_t(consist_order_t *order)
{
	this->order = order;
	init_table();
}

void cont_order_overview_t::init_table()
{
	remove_all();
	set_table_layout(1,0);
	if (!order->get_count() || order_element_index >= order->get_count() ) {
		old_count = 0;
		// empty or invalid element index
		new_component<gui_label_t>("Select a consist order", SYSCOL_TEXT_WEAK);
	}
	else{
		consist_order_element_t elem= order->get_order(order_element_index);
		old_count=elem.get_count();
		if( !old_count ) {
			new_component<gui_label_t>("Set vehicle descriptions", SYSCOL_TEXT_WEAK);
		}
		else {
			for (uint8 i = 0; i < old_count; i++) {
				new_component<gui_vehicle_description_t>(order, player_nr, order_element_index, i);
			}
		}
	}
	set_size(get_min_size());
}


void cont_order_overview_t::draw(scr_coord offset)
{
	if (order) {
		const uint32 description_count = order->get_order(order_element_index).get_count();
		if (description_count != old_count) {
			init_table();
		}
		gui_aligned_container_t::draw(offset);
	}
}


consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t(translator::translate("consist_order")),
	halt_number(-1),
	cont_order_overview(&order),
	scl(gui_scrolled_list_t::listskin),
	scl_vehicles(gui_scrolled_list_t::listskin),
	scl_convoys(gui_scrolled_list_t::listskin),
	scrolly_order(&cont_order, false, true),
	img_convoy(convoihandle_t()),
	formation(convoihandle_t(), false),
	scrollx_formation(&formation, true, false)
{
	if (player && schedule) {
		init(player, schedule, entry_id);
		init_table();
	}
}


void consist_order_frame_t::init(player_t* player, schedule_t *schedule, uint16 entry_id)
{
	this->player = player;
	this->schedule = schedule;
	unique_entry_id = entry_id;
	veh_specs.set_player_nr(player->get_player_nr());
	set_owner(player);
	update();
}

void consist_order_frame_t::init_table()
{
	remove_all();
	cont_order.remove_all();
	cont_picker_frame.remove_all();
	cont_convoy_copier.remove_all();
	tabs.clear();

	old_entry_count = schedule->get_count();
	order=schedule->orders.get(unique_entry_id);

	bt_filter_halt_convoy.pressed = true;
	bt_filter_single_vehicle.pressed = true;
	cont_convoy_filter.set_visible(false);
	update();

	set_table_layout(1,0);


	//debug
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(COL_DANGER);
	lb->buf().printf("(debug)entry-id: %u", unique_entry_id);
	lb->update();

	add_table(2,1);
	{
		add_component(&halt_number);
		add_component(&lb_halt);
	}
	end_table();

	// [OVERVIEW] (orders)
	add_table(2,1);
	{
		scl.clear_elements();
		scl.set_size(scr_size(D_LABEL_WIDTH<<1, LINESPACE*4));
		scl.set_maximize(true);
		scl.add_listener(this);
		update_order_list();
		add_component(&scl);

		add_table(2, 1);
		{
			cont_order.set_table_layout(1,0);
			cont_order.add_component(&cont_order_overview);
			scrolly_order.set_maximize(true);
			add_component(&scrolly_order);
			cont_order.set_size(cont_order.get_min_size());

			cont_order.new_component<gui_label_t>("(dummy)order description table here");
		}
		end_table();
	}
	end_table();

	add_table(3,1)->set_force_equal_columns(true);
	{
		bt_new.init(   button_t::roundbox | button_t::flexible, "New order");
		bt_new.add_listener(this);
		add_component(&bt_new);
		bt_delete.init(button_t::roundbox | button_t::flexible, "Delete order");
		bt_delete.enable(order.get_count());
		bt_delete.add_listener(this);
		add_component(&bt_delete);
	}
	end_table();

	new_component<gui_divider_t>();

	// TODO: filter, sort

	freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------"), SYSCOL_TEXT);
	freight_type_c.set_selection(0);
	for (uint8 i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
		if (i==goods_manager_t::INDEX_NONE) {
			continue;
		}

		const goods_desc_t* info = goods_manager_t::get_info_catg_index(i);
		freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(info->get_catg_name()), SYSCOL_TEXT, info->get_catg_symbol());
	}
	freight_type_c.add_listener(this);
	freight_type_c.set_size(scr_size(D_BUTTON_WIDTH*2, D_EDIT_HEIGHT));
	freight_type_c.set_width_fixed(true);

	add_table(3,1);
	{
		new_component<gui_label_t>("clf_chk_waren");
		add_component(&freight_type_c);
		new_component<gui_fill_t>();
	}
	end_table();

	// [VEHICLE PICKER]
	old_vehicle_assets = player->get_finance()->get_history_veh_year(finance_t::translate_waytype_to_tt(schedule->get_waytype()), 0, ATV_NEW_VEHICLE);
	build_vehicle_list();
	cont_picker_frame.set_table_layout(2,2);
	cont_picker_frame.set_alignment(ALIGN_TOP);
	{
		cont_picker_frame.add_table(3,2);
		{
			cont_picker_frame.new_component<gui_label_t>("cl_txt_sort");
			cont_picker_frame.new_component<gui_label_t>("dummy sort combobox");
			bt_sort_order_veh.init(button_t::sortarrow_state, "");
			bt_sort_order_veh.set_tooltip(translator::translate("hl_btn_sort_order"));
			bt_sort_order_veh.add_listener(this);
			bt_sort_order_veh.pressed = false;
			cont_picker_frame.add_component(&bt_sort_order_veh);
		}
		cont_picker_frame.end_table();

		bt_add_vehicle.init(button_t::roundbox, "Add vehicle");
		bt_add_vehicle.add_listener(this);
		cont_picker_frame.add_component(&bt_add_vehicle);

		scl_vehicles.set_maximize(true);
		scl_vehicles.add_listener(this);
		cont_picker_frame.add_component(&scl_vehicles);

		cont_picker_frame.add_table(2,1)->set_alignment(ALIGN_TOP);
		{
			//get_base_tile_raster_width();
			cont_picker_frame.add_component(&veh_specs);
			cont_picker_frame.new_component<gui_fill_t>();
		}
		cont_picker_frame.end_table();
	}

	// [CONVOY COPIER]
	cont_convoy_copier.set_table_layout(2,1);
	cont_convoy_copier.set_alignment(ALIGN_TOP);
	{
		// selector (left)
		cont_convoy_copier.add_table(1,3);
		{
			// TODO: put into filter option container
			cont_convoy_filter.set_table_layout(1,0);
			cont_convoy_filter.set_table_frame(true);
			{
				bt_filter_halt_convoy.init(button_t::square_state, "filter_halt_convoy");
				bt_filter_halt_convoy.set_tooltip(translator::translate("Narrow down to convoys that using this stop"));
				bt_filter_halt_convoy.add_listener(this);
				cont_convoy_filter.add_component(&bt_filter_halt_convoy);

				bt_filter_single_vehicle.init(button_t::square_state, "filter_single_vehicle_convoy");
				bt_filter_single_vehicle.set_tooltip(translator::translate("Exclude convoys made up of one vehicle"));
				bt_filter_single_vehicle.add_listener(this);
				cont_convoy_filter.add_component(&bt_filter_single_vehicle);

				// electric / bidirectional
				// TODO: name filter
				// TODO: home depot filter
			}
			cont_convoy_filter.set_size(cont_convoy_filter.get_min_size());
			cont_convoy_filter.set_rigid(false);

			cont_convoy_copier.add_table(2,1)->set_alignment(ALIGN_TOP);
			{
				bt_show_hide_convoy_filter.init(button_t::roundbox, "+");
				bt_show_hide_convoy_filter.set_width(display_get_char_width('+') + D_BUTTON_PADDINGS_X);
				bt_show_hide_convoy_filter.add_listener(this);
				cont_convoy_copier.add_component(&bt_show_hide_convoy_filter);
				cont_convoy_copier.add_table(1,3)->set_spacing(scr_size(0,0));
				{
					lb_open_convoy_filter.init("open_convoy_filter_option");
					lb_open_convoy_filter.set_rigid(false);
					cont_convoy_copier.add_component(&lb_open_convoy_filter);
					cont_convoy_copier.add_component(&cont_convoy_filter);
					cont_convoy_copier.new_component<gui_margin_t>(cont_convoy_filter.get_min_size().w);
				}
				cont_convoy_copier.end_table();
			}
			cont_convoy_copier.end_table();

			// TODO: add sorter
			cont_convoy_copier.add_table(3,2);
			{
				cont_convoy_copier.new_component<gui_label_t>("cl_txt_sort");
				cont_convoy_copier.new_component<gui_label_t>("dummy sort combobox");
				bt_sort_order_cnv.init(button_t::sortarrow_state, "");
				bt_sort_order_cnv.set_tooltip(translator::translate("hl_btn_sort_order"));
				bt_sort_order_cnv.add_listener(this);
				bt_sort_order_cnv.pressed = false;
				cont_convoy_copier.add_component(&bt_sort_order_cnv);
			}
			cont_convoy_copier.end_table();

			scl_convoys.set_maximize(true);
			scl_convoys.add_listener(this);
			cont_convoy_copier.add_component(&scl_convoys);
		}
		cont_convoy_copier.end_table();

		// details (right)
		cont_convoy_copier.add_table(1,0);
		{
			bt_copy_convoy.init(button_t::roundbox, "copy_convoy_order");
			bt_copy_convoy.set_tooltip(translator::translate("Set the order of this convoy to the selected order"));
			bt_copy_convoy.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy);

			bt_copy_convoy_vehicle.init(button_t::square_state, "bt_same_vehicle");
			bt_copy_convoy_vehicle.pressed = true;
			bt_copy_convoy_vehicle.set_tooltip(translator::translate("Limited to the same vehicle"));
			bt_copy_convoy_vehicle.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy_vehicle);

			cont_convoy_copier.add_component(&line_label);
			cont_convoy_copier.add_component(&img_convoy);
			scrollx_formation.set_maximize(true);
			cont_convoy_copier.add_component(&scrollx_formation);
			cont_convoy_copier.add_component(&lb_vehicle_count);

			bt_convoy_detail.init(button_t::roundbox, "Details");
			if (skinverwaltung_t::open_window) {
				bt_convoy_detail.set_image(skinverwaltung_t::open_window->get_image_id(0));
				bt_convoy_detail.set_image_position_right(true);
			}
			bt_convoy_detail.set_tooltip("Vehicle details");
			bt_convoy_detail.add_listener(this);
			cont_convoy_copier.add_component(&bt_convoy_detail);

			cont_convoy_copier.new_component<gui_fill_t>();
		}
		cont_convoy_copier.end_table();
	}

	tabs.add_tab(&cont_picker_frame, translator::translate("Vehicle picker"));
	tabs.add_tab(&cont_convoy_copier, translator::translate("Convoy copier"));
	add_component(&tabs);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));
	set_resizemode(diagonal_resize);
}


// reflesh labels, call when entry changed
void consist_order_frame_t::update()
{
	halt = halthandle_t();
	// serach entry and halt
	uint8 entry_idx = -1;
	for (uint i = 0; i < schedule->entries.get_count(); i++) {
		if( unique_entry_id==schedule->entries[i].unique_entry_id ) {
			entry_idx = i;
			halt = haltestelle_t::get_halt(schedule->entries[i].pos, player);
			if( halt.is_bound() ) {
				uint8 halt_symbol_style = 0;
				if ((halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1) {
					halt_symbol_style = gui_schedule_entry_number_t::number_style::interchange;
				}
				halt_number.init(i, halt->get_owner()->get_player_color1(), halt_symbol_style, schedule->entries[i].pos);
			}
			break;
		}
	}
	if( !halt.is_bound() ) { destroy_win(this); }

	lb_halt.buf().append(halt->get_name());
	lb_halt.update();

	update_order_list();
}


// copy selected convoy's order
void consist_order_frame_t::set_convoy(convoihandle_t cnv)
{
}

void consist_order_frame_t::update_order_list()
{
	scl.clear_elements();
	if( !order.get_count() ) {
		// Need an empty order to edit
		append_new_order();
	}
	old_order_count = order.get_count();
	for( uint32 i=0; i<old_order_count; ++i ) {
		cbuffer_t buf;
		buf.printf("%s #%u", translator::translate("Consist order"), i+1); // TODO: add vehicle count
		scl.new_component<gui_scrolled_list_t::buf_text_scrollitem_t>(buf, SYSCOL_TEXT);
	}
	bt_delete.enable(old_order_count);
	resize(scr_size(0,0));
}

void consist_order_frame_t::update_vehicle_info()
{
	if( selected_vehicle==NULL ) {
		veh_specs.set_visible(false);
		bt_add_vehicle.disable();
	}
	else {
		veh_specs.set_vehicle(selected_vehicle);
		veh_specs.set_visible(true);
		bt_add_vehicle.enable();
	}
	cont_picker_frame.set_size(cont_picker_frame.get_min_size());
	// adjust the size if the tab is open
	if (tabs.get_active_tab_index() == 0) {
		reset_min_windowsize();
		resize(scr_size(0,0));
	}
}

void consist_order_frame_t::update_convoy_info()
{
	line_label.set_line(linehandle_t());
	if( selected_convoy.is_bound() ) {
		lb_vehicle_count.buf().printf("%s %i", translator::translate("Fahrzeuge:"), selected_convoy->get_vehicle_count());
		if (selected_convoy->front()->get_waytype() != water_wt) {
			lb_vehicle_count.buf().printf(" (%s %i)", translator::translate("Station tiles:"), selected_convoy->get_tile_length());
		}
		if (selected_convoy->get_line().is_bound()) {
			line_label.set_line(selected_convoy->get_line());
		}
	}
	bt_copy_convoy.enable(selected_convoy.is_bound());
	bt_copy_convoy_vehicle.enable(selected_convoy.is_bound());
	bt_convoy_detail.enable(selected_convoy.is_bound());
	lb_vehicle_count.update();

	img_convoy.set_cnv(selected_convoy);
	formation.set_cnv(selected_convoy);
	cont_convoy_copier.set_size(cont_convoy_copier.get_min_size());
	// adjust the size if the tab is open
	if (tabs.get_active_tab_index() == 1) {
		reset_min_windowsize();
		resize(scr_size(0,0));
	}
}

// returns position of depot on the map
koord3d consist_order_frame_t::get_weltpos(bool)
{
	return halt_number.get_entry_pos();
}

bool consist_order_frame_t::is_weltpos()
{
	return (world()->get_viewport()->is_on_center(get_weltpos(false)));
}


void consist_order_frame_t::draw(scr_coord pos, scr_size size)
{
	if (player != welt->get_active_player()) { destroy_win(this); }
	if( schedule->get_count() != old_entry_count ) {
		update();
	}
	else if( order.get_count()!=old_order_count ) {
		update_order_list();
	}

	// update when player purchases or sells this waytype's vehicle
	const sint64 temp_veh_assets = player->get_finance()->get_history_veh_year(finance_t::translate_waytype_to_tt(schedule->get_waytype()), 0, ATV_NEW_VEHICLE);
	if (old_vehicle_assets != temp_veh_assets) {
		old_vehicle_assets = temp_veh_assets;
		build_vehicle_list();
	}

	gui_frame_t::draw(pos, size);
}


void consist_order_frame_t::append_new_order()
{
	order.append(consist_order_element_t());
}


bool consist_order_frame_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	if( comp==&scl_vehicles ) {
		sint32 e = scl_vehicles.get_selection();
		scl_vehicles.get_selection();
		vehicle_scrollitem_t *item = (vehicle_scrollitem_t*)scl_vehicles.get_element(v.i);
		selected_vehicle = item->get_vehicle();
		update_vehicle_info();
	}
	else if( comp==&scl_convoys  &&  own_convoys.get_count() ) {
		sint32 e = scl_convoys.get_selection();
		scl_convoys.get_selection();
		convoy_scrollitem_t *item = (convoy_scrollitem_t*)scl_convoys.get_element(v.i);
		selected_convoy = item->get_convoy();
		update_convoy_info();
	}
	else if( comp==&scl ) {
		cont_order_overview.set_element(scl.get_selection(), player->get_player_nr());
		resize(scr_size(0,0));
	}
	else if( comp==&bt_new ) {
		scl.set_selection(-1);

		// append new order slot
		append_new_order();
		resize(scr_size(0,0));
		update_order_list();
	}
	else if( comp==&bt_delete ) {
		if (scl.get_selection()<0 || (uint32)scl.get_selection()>=order.get_count() ) { return true; }

		// delete selected order
		order.remove_order( (uint32)scl.get_selection() );
		update_order_list();
	}
	else if( comp==&bt_sort_order_veh ) {
		bt_sort_order_veh.pressed = !bt_sort_order_veh.pressed;
		// TODO: execute sorting
	}
	else if( comp==&bt_sort_order_cnv ) {
		bt_sort_order_cnv.pressed = !bt_sort_order_cnv.pressed;
		// TODO: execute sorting
	}
	else if( comp==&bt_show_hide_vehicle_filter ) {
		// TODO:
	}
	else if( comp==&bt_show_hide_convoy_filter ) {
		lb_open_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		cont_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		bt_show_hide_convoy_filter.set_text(lb_open_convoy_filter.is_visible() ? "+" : "-");

		resize(scr_size(0,0));
	}
	else if( comp==&bt_add_vehicle ) {
		// TODO: check total own count

	}
	else if( comp==&bt_filter_halt_convoy ){
		bt_filter_halt_convoy.pressed = !bt_filter_halt_convoy.pressed;
		build_vehicle_list();
	}
	else if( comp==&bt_filter_single_vehicle){
		bt_filter_single_vehicle.pressed = !bt_filter_single_vehicle.pressed;
		build_vehicle_list();
	}
	else if(  comp==&bt_copy_convoy  ) {
		if( scl.get_selection()==-1 ){
			create_win(new news_img("Select copy destination!"), w_no_overlap, magic_none);
		}
		else {
			if( !selected_convoy.is_bound() ) {
				create_win(new news_img("No valid convoy selected!"), w_no_overlap, magic_none);
			}
			else {
				order.set_convoy_order(scl.get_selection(), selected_convoy, bt_copy_convoy_vehicle.pressed);
				resize(scr_size(0,0));
			}
		}
	}
	else if(  comp==&bt_copy_convoy_vehicle  ) {
		bt_copy_convoy_vehicle.pressed = !bt_copy_convoy_vehicle.pressed;
	}
	else if(  comp == &bt_convoy_detail  ) {
		if (selected_convoy.is_bound()) {
			create_win(20, 20, new convoi_detail_t(selected_convoy), w_info, magic_convoi_detail+ selected_convoy.get_id() );
		}
		return true;
	}
	else if( comp==&freight_type_c ) {
		switch( freight_type_c.get_selection() )
		{
			case 0:
				filter_catg = goods_manager_t::INDEX_NONE;
				break;
			case 1:
				filter_catg = goods_manager_t::INDEX_PAS;
				break;
			case 2:
				filter_catg = goods_manager_t::INDEX_MAIL;
				break;
			default:
				filter_catg = freight_type_c.get_selection();
				break;
		}
		build_vehicle_list();
	}

	return false;
}


// TODO: filter, consider connection
// v-shape filter, catg filter
void consist_order_frame_t::build_vehicle_list()
{
	const bool search_only_halt_convoy = bt_filter_halt_convoy.pressed;

	own_vehicles.clear();
	scl_vehicles.clear_elements();
	selected_vehicle = NULL;
	scl_vehicles.set_selection(-1);

	own_convoys.clear();
	scl_convoys.clear_elements();
	scl_convoys.set_selection(-1);

	// list only own vehicles
	for (auto const cnv : world()->convoys()) {
		if(  cnv->get_owner()==player  &&  cnv->front()->get_waytype()==schedule->get_waytype()) {
			// count own vehicle
			for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
				const vehicle_desc_t *veh_type = cnv->get_vehicle(i)->get_desc();
				//TODO: filter speed power, type
				// filter
				if( veh_type->get_freight_type()->get_catg_index() != filter_catg) {
					continue;
				}

				// serach for already own
				bool found = false;
				for (auto &own_veh : own_vehicles) {
					if (own_veh.veh_type == veh_type) {
						own_veh.count++;
						found = true;
						break;
					}
				}
				if (!found){
					own_vehicle_t temp;
					temp.count = 1;
					temp.veh_type = veh_type;
					own_vehicles.append(temp);
				}
			}

			// append convoy
			if( bt_filter_single_vehicle.pressed  &&  cnv->get_vehicle_count()<2 ) {
				continue;
			}
			if (!search_only_halt_convoy) {
				// filter
				if( filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
					continue;
				}
				own_convoys.append(cnv);
			}
		}
	}
	// also count vehicles that stored at depots
	for( auto const depot : depot_t::get_depot_list() ) {
		if( depot->get_owner() == player ) {
			for( auto const veh : depot->get_vehicle_list() ) {
				const vehicle_desc_t *veh_type = veh->get_desc();
				// filter
				if (veh_type->get_freight_type()->get_catg_index() != filter_catg) {
					continue;
				}

				// serach for already own
				bool found = false;
				for (auto &own_veh : own_vehicles) {
					if (own_veh.veh_type == veh_type) {
						own_veh.count++;
						found = true;
						break;
					}
				}
				if (!found){
					own_vehicle_t temp;
					temp.count = 1;
					temp.veh_type = veh_type;
					own_vehicles.append(temp);
				}
			}
		}
	}

	if (search_only_halt_convoy) {
		// line
		for (uint32 i=0; i < halt->registered_lines.get_count(); ++i) {
			const linehandle_t line = halt->registered_lines[i];
			if( line->get_owner()==player  &&  line->get_schedule()->get_waytype()==schedule->get_waytype() ) {
				for( uint32 j=0; j < line->count_convoys(); ++j ) {
					convoihandle_t const cnv = line->get_convoy(j);
					if (cnv->in_depot()) {
						continue;
					}
					// filter
					if (filter_catg != goods_manager_t::INDEX_NONE && !cnv->get_goods_catg_index().is_contained(filter_catg)) {
						continue;
					}
					if( bt_filter_single_vehicle.pressed  &&  cnv->get_vehicle_count()<2 ) {
						continue;
					}
					own_convoys.append(cnv);
				}
			}
		}

		// convoy
		for (uint32 i=0; i < halt->registered_convoys.get_count(); ++i) {
			const convoihandle_t cnv = halt->registered_convoys[i];
			if( cnv->get_owner()==player  &&cnv->front()->get_waytype()==schedule->get_waytype() ) {
				// filter
				if( filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
					continue;
				}
				own_convoys.append(cnv);
			}
		}
	}

	// update selector
	for (auto &own_veh : own_vehicles) {
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_veh);
	}
	if( !own_vehicles.get_count() ) {
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_vehicle_t());
	}

	bool found=false;
	for (auto &own_cnv : own_convoys) {
		scl_convoys.new_component<convoy_scrollitem_t>(own_cnv, false);
		// select the same one again
		if (selected_convoy==own_cnv) {
			scl_convoys.set_selection(scl_convoys.get_count()-1);
			found = true;
		}
	}
	if (!own_convoys.get_count()) {
		scl_convoys.new_component<convoy_scrollitem_t>(convoihandle_t(), false);
	}
	if (!found) {
		// The selected one has been lost from the list. So turn off the display as well. Otherwise the execute button will cause confusion.
		selected_convoy = convoihandle_t();
		update_convoy_info();
	}
	reset_min_windowsize();
	resize(scr_size(0,0));
}


void consist_order_frame_t::rdwr(loadsave_t *file)
{
	scr_size size = get_windowsize();
	size.rdwr(file);

	file->rdwr_short(unique_entry_id);

	// schedules
	if (file->is_loading()) {
		// dummy types
		schedule = new truck_schedule_t();
	}
	schedule->rdwr(file);

	if (file->is_loading()) {
		init(world()->get_active_player(), schedule, unique_entry_id);
		init_table();
	}
}
