/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "consist_order_gui.h"
#include "convoi_detail_t.h"
#include "convoy_item.h"
#include "simwin.h"
#include "messagebox.h"
#include "vehicle_detail.h"
#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../display/viewport.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"
#include "components/gui_convoy_assembler.h"
#include "components/gui_divider.h"
#include "components/gui_table.h"
#include "components/gui_textarea.h"
#include "components/gui_waytype_image_box.h"
#include "../player/finance.h"


#define L_OWN_VEHICLE_COUNT_WIDTH (proportional_string_width("8,888") + D_H_SPACE)
#define L_OWN_VEHICLE_LABEL_OFFSET_LEFT (L_OWN_VEHICLE_COUNT_WIDTH + VEHICLE_BAR_HEIGHT*4+D_H_SPACE)

int vehicle_scrollitem_t::sort_mode = 0;
bool vehicle_scrollitem_t::sortreverse = false;

// sort option for Consist Copier
// TODO: add sort option
static const char *cc_sort_text[1] = {
	"Name"
};

// sort option for Vehicle Picker
static const char *vp_sort_text[vehicle_scrollitem_t::SORT_MODES] = {
	"Name",
	"by_own",
	"Leistung",
	"cl_btn_sort_max_speed",
	"Intro. date",
	"Role"
};

// filter option for Vehicle Picker
static const char *vp_powered_filter_text[3] = {
	"All",
	"helptxt_powered_vehicle",
	"helptxt_unpowered_vehicle"
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
		colorbar.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar_edge.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar.init(own_veh.veh_type->get_vehicle_status_color());
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


bool vehicle_scrollitem_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	const vehicle_scrollitem_t *a = dynamic_cast<const vehicle_scrollitem_t*>(aa);
	const vehicle_scrollitem_t *b = dynamic_cast<const vehicle_scrollitem_t*>(bb);
	int cmp = 0;
	switch (sort_mode) {
		case by_own:
			cmp = a->own_veh.count - b->own_veh.count;
			return sortreverse ? cmp > 0 : cmp < 0;
		case by_power:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_power);
			break;
		case by_max_speed:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_speed);
			break;
		case by_intro_date:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_intro_date);
			break;
		case by_role:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_role);
			break;
		default: // by_name
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_name);
			break;
	}

	return sortreverse ? cmp <= 0 : cmp > 0;
}



bool gui_vehicle_element_list_t::infowin_event(const event_t *ev)
{
	int sel_index = index_at(scr_coord(0,0) - pos, ev->mx, ev->my);
	if( sel_index != -1 ) {
		if( (IS_LEFTCLICK(ev) || IS_LEFTDBLCLK(ev)) || (IS_RIGHTCLICK(ev) || IS_RIGHTDBLCLK(ev))) {
			value_t p;
			p.i = (IS_LEFTCLICK(ev) || IS_LEFTDBLCLK(ev)) ? sel_index : -(++sel_index);
			call_listeners(p);
			return true;
		}
	}
	return false;
}


gui_consist_order_shifter_t::gui_consist_order_shifter_t(consist_order_t *order_, uint32 index)
{
	order = order_;
	slot_index = index;

	//set_margin(scr_size(D_H_SPACE,D_V_SPACE), scr_size(D_H_SPACE, 0));
	set_table_layout(3,1);
	set_spacing(scr_size(0, D_V_SPACE));

	bt_forward.init(button_t::roundbox_left, "<");
	bt_forward.enable(slot_index > 0);
	bt_forward.add_listener(this);
	add_component(&bt_forward);

	bt_remove.init(button_t::box, "X");
	bt_remove.background_color=color_idx_to_rgb(COL_RED);
	bt_remove.set_tooltip(translator::translate("Remove this vehicle from consist."));
	bt_remove.add_listener(this);
	add_component(&bt_remove);

	bt_backward.init(button_t::roundbox_right, ">");
	bt_backward.enable(slot_index < order->get_count()-1);
	bt_backward.add_listener(this);
	add_component(&bt_backward);
}

bool gui_consist_order_shifter_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if (slot_index >= order->get_count()) return false;

	if (comp == &bt_remove) {
		order->remove_order(slot_index);
	}
	if (comp == &bt_forward) {
		//order->forward_element(slot_index);
	}
	if (comp == &bt_backward) {
		//order->backward_element(slot_index);
	}
	return true;
}


gui_vehicle_description_element_t::gui_vehicle_description_element_t(consist_order_t *order_, uint32 index, waytype_t wt) :
	vde(&vde_vec),
	scrolly(&vde, false, true)
{
	order = order_;
	slot_index = index;
	way_type = wt;

	if (slot_index < order->get_count()) {
		set_table_layout(1,0);
		set_alignment(ALIGN_TOP);

		//bt_can_empty.init(button_t::square_state, "");
		//bt_can_empty.set_tooltip(translator::translate("This slot is skippable and no vehicles are allowed."));
		//bt_can_empty.add_listener(this);
		//add_component(&bt_can_empty);
		new_component<gui_label_t>("chk_empty");
		new_component<gui_label_t>("btn_goods");

		// constraints check indicator
		gui_aligned_container_t *tbl = add_table(2,1);
		tbl->set_spacing(scr_size(0, 0));
		tbl->set_force_equal_columns(true);
		{
			state_prev.set_show_frame(false);
			state_next.set_show_frame(false);
			add_component(&state_prev);
			add_component(&state_next);
		}
		end_table();
		const scr_coord grid = gui_convoy_assembler_t::get_grid(way_type);
		vde.set_grid(grid);
		vde.set_placement(gui_convoy_assembler_t::get_placement(way_type));
		vde.set_player_nr(world()->get_active_player_nr());
		vde.set_max_width(grid.x+ 2 * gui_image_list_t::BORDER);
		vde.add_listener(this);

		update();
		scrolly.set_maximize(true);
		// image list height
		scrolly.set_min_height(grid.y+D_H_SPACE*2);
		gui_aligned_container_t *tbl_orders = add_table(1,2);
		tbl_orders->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
		tbl_orders->set_table_frame(false,true);
		tbl_orders->set_margin(scr_size(0,0), scr_size(D_SCROLLBAR_WIDTH,0));
		{
			add_component(&scrolly);
			new_component<gui_fill_t>(false, true);
		}
		end_table();
	}
}

void gui_vehicle_description_element_t::update()
{
	clear_ptr_vector(vde_vec);
	if (slot_index < order->get_count()) {
		//uint16 livery_scheme_index = world()->get_player(player_nr)->get_favorite_livery_scheme_index((uint8)simline_t::waytype_to_linetype(way_type));
		consist_order_element_t elem = order->access_order(slot_index);
		old_count = elem.get_count();
		//bt_can_empty.pressed = elem.empty;

		// update images
		for (uint8 i = 0; i < elem.get_count(); i++) {
			if (const vehicle_desc_t* veh_type = elem.get_vehicle_description(i).specific_vehicle) {
				gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t(veh_type->get_name(), veh_type->get_base_image());
				// The vehicle state bar color here is determined only by the timeline
				const PIXVAL state_col = veh_type->get_vehicle_status_color();
				img_data->lcolor = state_col;
				img_data->rcolor = state_col;
				img_data->basic_coupling_constraint_prev = veh_type->get_basic_constraint_prev();
				img_data->basic_coupling_constraint_next = veh_type->get_basic_constraint_next();
				img_data->interactivity = veh_type->get_interactivity();
				vde_vec.append(img_data);
			}
			else {
				break;
			}
		}

		// update connection statuses
		state_prev.set_color(order->get_constraint_state_color(slot_index, false));
		state_next.set_color(order->get_constraint_state_color(slot_index));

	}
	set_size(get_min_size());
}

void gui_vehicle_description_element_t::show_vehicle_detail(uint32 index)
{
	if (slot_index < order->get_count()) {
		consist_order_element_t elem = order->access_order(slot_index);
		if (const vehicle_desc_t* veh_type = elem.get_vehicle_description(index).specific_vehicle) {
			vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail_for_consist_order));
			if (!win) {
				create_win(new vehicle_detail_t(veh_type), w_info, magic_vehicle_detail_for_consist_order);
			}
			else {
				win->set_vehicle(veh_type);
				top_win(win, false);
			}
		}
	}
}

void gui_vehicle_description_element_t::draw(scr_coord offset)
{
	if (slot_index < order->get_count()) {
		consist_order_element_t elem = order->access_order(slot_index);
		if (elem.get_count() != old_count) {
			update();
		}
		gui_aligned_container_t::draw(offset);
	}
}

bool gui_vehicle_description_element_t::action_triggered(gui_action_creator_t *comp, value_t p)
{
	if (order->get_count() <= slot_index) { return false; }

	if (comp == &vde && slot_index < order->get_count()) {
		if (p.i<0) {
			uint32 index = -1 - p.i;
			consist_order_element_t &elem = order->access_order(slot_index);
			if(index<elem.get_count()){
				elem.remove_vehicle_description_at(index);
			}
		}
		else {
			show_vehicle_detail((uint32)p.i);
		}
	}
	return true;
}


cont_order_overview_t::cont_order_overview_t(consist_order_t *order, waytype_t wt)
{
	this->order = order;
	way_type = wt;
	init_table();
}

void cont_order_overview_t::init_table()
{
	remove_all();
	old_count = order->get_count();
	set_table_layout(2,0);
	set_alignment(ALIGN_TOP);
	set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(D_MARGIN_RIGHT, D_SCROLLBAR_HEIGHT));
	if (!old_count) {
		// order is empty
		buf.clear();
		buf.append(translator::translate("Select the vehicles that you want to make up this consist at this stop\nand onwards on this schedule."));
		new_component<gui_textarea_t>(&buf);
	}
	else{
		const uint16 sel= abs(selected_index)-1;
		const bool is_append_mode = (selected_index>0);
		add_table(old_count+is_append_mode, 1);
		{
			for (uint8 col = 0; col < old_count; col++) {
				if (is_append_mode && (col == sel)) {
					gui_colored_label_t *lb = new_component<gui_colored_label_t>(SYSCOL_TD_BACKGROUND_HIGHLIGHT);
					lb->set_padding(scr_size(3,3));
					lb->buf().append("+");
					lb->update();
				}
				gui_aligned_container_t *tbl_orders = add_table(1, 3);
				tbl_orders->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
				tbl_orders->set_spacing(scr_size(0,0));
				new_component<gui_consist_order_shifter_t>(order, col);
				new_component<gui_margin_t>(0,D_V_SPACE);
				new_component<gui_consist_order_element_t>(order, col, way_type, !is_append_mode && (col == sel));
				end_table();
			}
		}
		end_table();
		new_component<gui_fill_t>();
	}
	set_size(get_min_size());
}

gui_consist_order_element_t::gui_consist_order_element_t(consist_order_t *order, uint32 index, waytype_t wt, bool sel)
{
	selected = sel;
	set_table_layout(1,2);
	set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
	set_spacing(scr_size(0,0));
	set_margin(scr_size(0,0), scr_size(0,0));
	set_table_frame(true);

	gui_colored_label_t *th = new_component<gui_colored_label_t>(selected ? SYSCOL_TH_TEXT_SELECTED : SYSCOL_TH_TEXT_TOP, gui_label_t::centered, selected ? SYSCOL_TH_BACKGROUND_SELECTED : SYSCOL_TH_BACKGROUND_TOP);
	th->buf().printf(" %u ", index+1);
	th->set_underline(selected);
	th->set_padding(scr_size(0,3));
	th->update();

	new_component<gui_vehicle_description_element_t>(order, index, wt);
	set_size(get_min_size());
}

void gui_consist_order_element_t::draw(scr_coord offset)
{
	if (selected) {
		// draw background
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, size.w, size.h, SYSCOL_TD_BACKGROUND_HIGHLIGHT, false);
	}
	gui_aligned_container_t::draw(offset);
}

void cont_order_overview_t::draw(scr_coord offset)
{
	if (order) {
		if (order->get_count() != old_count) {
			init_table();
		}
		gui_aligned_container_t::draw(offset);
	}
}


consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t(translator::translate("consist_order")),
	halt_number(255),
	cont_order_overview(&order, schedule->get_waytype()),
	scl_vehicles(gui_scrolled_list_t::listskin, vehicle_scrollitem_t::compare),
	scl_convoys(gui_scrolled_list_t::listskin),
	scroll_order(&cont_order, true, false),
	img_convoy(convoihandle_t()),
	formation(convoihandle_t(), false),
	scrollx_formation(&formation, true, false)
{
	if (player && schedule) {
		this->player = player;
		set_owner(player);
		init(schedule, entry_id);
		init_table();
	}
}


void consist_order_frame_t::save_order()
{
	if (player) {
		schedule->orders.remove(unique_entry_id);
		if (order.get_count()) {
			schedule->orders.put(unique_entry_id, order);
		}
	}
}


void consist_order_frame_t::init(schedule_t *schedule, uint16 entry_id)
{
	this->schedule = schedule;
	if (unique_entry_id != 65535 && unique_entry_id != entry_id) {
		save_order();
	}
	unique_entry_id = entry_id;
	order = schedule->orders.get(unique_entry_id);
}

void consist_order_frame_t::init_table()
{
	bt_filter_halt_convoy.pressed = false;
	bt_filter_line_convoy.pressed = true;
	bt_filter_single_vehicle.pressed = false;
	cont_convoy_filter.set_visible(false);
	update();

	set_table_layout(1, 0);
	set_alignment(ALIGN_TOP);

#ifdef DEBUG
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(COL_DANGER);
	lb->buf().printf("(debug)entry-id: %u", unique_entry_id);
	lb->update();
#endif

	const waytype_t way_type = schedule->get_waytype();
	add_table(3,1);
	{
		new_component<gui_waytype_image_box_t>(way_type);
		add_component(&halt_number);
		add_component(&lb_halt);
	}
	end_table();

	// [OVERVIEW] (orders)
	cont_order.set_table_layout(2,0);
	cont_order.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_SCROLLBAR_WIDTH, 0));
	cont_order.set_alignment(ALIGN_TOP);
	{
		cont_order.add_component(&cont_order_overview);
		cont_order.new_component<gui_fill_t>();
		cont_order.new_component<gui_fill_t>(false, true);
	}
	const scr_coord grid = gui_convoy_assembler_t::get_grid(way_type);
	scroll_order.set_min_height(grid.y + LINESPACE + D_BUTTON_HEIGHT*3 + D_INDICATOR_HEIGHT + D_V_SPACE*5 + D_SCROLLBAR_HEIGHT);
	scroll_order.set_maximize(true);
	add_component(&scroll_order);

	new_component<gui_divider_t>();

	// filter, sort
	freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------"), SYSCOL_TEXT);
	freight_type_c.set_selection(0);
	for (uint8 i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
		if (i==goods_manager_t::INDEX_NONE) {
			continue;
		}

		const goods_desc_t* info = goods_manager_t::get_info_catg_index(i);
		freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(info->get_catg_name()), SYSCOL_TEXT, info->get_catg_symbol());
	}
	freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("none"), SYSCOL_TEXT);
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
	cont_picker_frame.set_table_layout(2,2);
	cont_picker_frame.set_alignment(ALIGN_TOP);
	{
		cont_picker_frame.new_component<gui_label_t>("Filter:");
		cont_picker_frame.add_table(4,1);
		{
			edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("add_elem_at"), SYSCOL_TEXT); // add consist_order_element_t at certain position
			edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("append_vde"), SYSCOL_TEXT); // append the vehicle as a vehicle description element
			edit_action_selector.set_selection(0);
			edit_action_selector.add_listener(this);
			cont_picker_frame.add_component(&edit_action_selector);

			init_input_value_range();
			numimp_append_target.set_value(1);
			numimp_append_target.add_listener(this);
			cont_picker_frame.add_component(&numimp_append_target);
			cont_order_overview.set_selected_index(1);

			bt_add_vehicle.init(button_t::roundbox, "Add vehicle");
			bt_add_vehicle.enable( selected_vehicle!=NULL );
			bt_add_vehicle.add_listener(this);
			cont_picker_frame.add_component(&bt_add_vehicle);
			cont_picker_frame.new_component<gui_fill_t>();
		}
		cont_picker_frame.end_table();


		cont_picker_frame.add_table(2,7);
		{
			bt_connectable_vehicle_filter.init(button_t::square_state, "show_only_appendable");
			bt_connectable_vehicle_filter.set_tooltip(translator::translate("Show only vehicles that can be added to the end of the selected order."));
			bt_connectable_vehicle_filter.pressed = true;
			bt_connectable_vehicle_filter.add_listener(this);
			cont_picker_frame.add_component(&bt_connectable_vehicle_filter, 2);

			cont_picker_frame.new_component<gui_label_t>("powered_filter");
			for (uint8 i = 0; i < 3; i++) {
				vp_powered_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vp_powered_filter_text[i]), SYSCOL_TEXT);
			}
			vp_powered_filter.set_selection(0);
			vp_powered_filter.add_listener(this);
			cont_picker_frame.add_component(&vp_powered_filter);

			cont_picker_frame.new_component<gui_label_t>("engine_type");
			engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All"), SYSCOL_TEXT);
			for (uint8 i = 1; i < 11; i++) {
				engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)i]), SYSCOL_TEXT);
			}
			engine_filter.set_selection(0);
			engine_filter.add_listener(this);
			cont_picker_frame.add_component(&engine_filter);

			if (world()->get_settings().get_allow_buying_obsolete_vehicles()) {
				bt_outdated.init(button_t::square_state, "Show outdated");
				bt_outdated.set_tooltip("Show also vehicles no longer in production.");
				bt_outdated.pressed = true;
				bt_outdated.add_listener(this);
				cont_picker_frame.add_component(&bt_outdated, 2);
			}
			if (world()->get_settings().get_allow_buying_obsolete_vehicles() == 1) {
				bt_obsolete.init(button_t::square_state, "Show obsolete");
				bt_obsolete.set_tooltip("Show also vehicles whose maintenance costs have increased due to obsolescence.");
				bt_obsolete.pressed = true;
				bt_obsolete.add_listener(this);
				cont_picker_frame.add_component(&bt_obsolete, 2);
			}

			bt_show_unidirectional.init(button_t::square_state, "Show unidirectional vehicle");
			bt_show_unidirectional.set_tooltip("Show also unidirectional vehicles.");
			bt_show_unidirectional.pressed = true;
			bt_show_unidirectional.add_listener(this);
			cont_picker_frame.add_component(&bt_show_unidirectional, 2);
		}
		cont_picker_frame.end_table();

		build_vehicle_list();

		cont_picker_frame.add_table(1,2)->set_alignment(ALIGN_TOP);
		{
			cont_picker_frame.add_table(3, 2);
			{
				cont_picker_frame.new_component<gui_label_t>("cl_txt_sort");
				for (uint8 i = 0; i < vehicle_scrollitem_t::SORT_MODES; i++) {
					vp_sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vp_sort_text[i]), SYSCOL_TEXT);
				}
				vp_sortedby.set_selection(0);
				vp_sortedby.add_listener(this);
				cont_picker_frame.add_component(&vp_sortedby);
				bt_sort_order_veh.init(button_t::sortarrow_state, "");
				bt_sort_order_veh.set_tooltip(translator::translate("hl_btn_sort_order"));
				bt_sort_order_veh.add_listener(this);
				bt_sort_order_veh.pressed = vehicle_scrollitem_t::sortreverse;
				cont_picker_frame.add_component(&bt_sort_order_veh);
			}
			cont_picker_frame.end_table();

			scl_vehicles.set_maximize(true);
			scl_vehicles.add_listener(this);
			cont_picker_frame.add_component(&scl_vehicles);
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
			cont_convoy_filter.set_table_layout(3,1);
			cont_convoy_filter.set_table_frame(true, true);
			{
				cont_convoy_filter.new_component<gui_empty_t>(); // left margin
				cont_convoy_filter.add_table(1,0);
				{
					cont_convoy_filter.new_component<gui_empty_t>(); // top margin
					bt_filter_halt_convoy.init(button_t::square_state, "filter_halt_consist");
					bt_filter_halt_convoy.set_tooltip(translator::translate("Narrow down to consists that use this stop"));
					bt_filter_halt_convoy.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_halt_convoy);

					bt_filter_line_convoy.init(button_t::square_state, "filter_line_consist");
					bt_filter_line_convoy.set_tooltip(translator::translate("Narrow down to only consists belonging to this line"));
					bt_filter_line_convoy.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_line_convoy);

					bt_filter_single_vehicle.init(button_t::square_state, "filter_single_vehicle_consist");
					bt_filter_single_vehicle.set_tooltip(translator::translate("Exclude consists made up of one vehicle"));
					bt_filter_single_vehicle.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_single_vehicle);

					// electric / bidirectional
					// TODO: name filter
					// TODO: home depot filter
					cont_convoy_filter.new_component<gui_empty_t>(); // bottom margin
				}
				cont_convoy_filter.end_table();
				cont_convoy_filter.new_component<gui_empty_t>(); // right margin
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
					lb_open_convoy_filter.init("open_consist_filter_option");
					lb_open_convoy_filter.set_rigid(false);
					cont_convoy_copier.add_component(&lb_open_convoy_filter);
					cont_convoy_copier.add_component(&cont_convoy_filter);
					cont_convoy_copier.new_component<gui_margin_t>(cont_convoy_filter.get_min_size().w);
				}
				cont_convoy_copier.end_table();
			}
			cont_convoy_copier.end_table();

			cont_convoy_copier.add_table(3,2);
			{
				cont_convoy_copier.new_component<gui_label_t>("cl_txt_sort");
				// TODO: add sort option
				//cc_sortedby
				for (uint8 i = 0; i < 1; i++) {
					cc_sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(cc_sort_text[i]), SYSCOL_TEXT);
				}
				cc_sortedby.set_selection(0);
				cc_sortedby.add_listener(this);
				cont_convoy_copier.add_component(&cc_sortedby);
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
			bt_copy_convoy.init(button_t::roundbox, "copy_consist_order");
			bt_copy_convoy.set_tooltip(translator::translate("Set the order of this consist to the selected order"));
			bt_copy_convoy.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy);

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

	tabs.add_tab(&cont_convoy_copier, translator::translate("Consist copier"));
	tabs.add_tab(&cont_picker_frame, translator::translate("Vehicle picker"));
	tabs.add_listener(this);
	add_component(&tabs);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));
	set_resizemode(diagonal_resize);
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

void consist_order_frame_t::init_input_value_range()
{
	numimp_append_target.enable(!(edit_action_selector.get_selection() == 1 && !order.get_count()));
	const bool is_append_mode = (edit_action_selector.get_selection() == 0);
	const uint32 max_vehicles = is_append_mode ? order.get_count() + 1 : max(1, order.get_count());
	if (numimp_append_target.get_value()>=max_vehicles) {
		cont_order_overview.set_selected_index(is_append_mode ? max_vehicles : -max_vehicles);
		numimp_append_target.set_value(max_vehicles);
	}
	numimp_append_target.set_limits(1, (sint32)max_vehicles);
}


void consist_order_frame_t::draw(scr_coord pos, scr_size size)
{
	if (player != welt->get_active_player() || !schedule->get_count()) { destroy_win(this); }
	if( schedule->get_count() != old_entry_count ) {
		init_input_value_range();
		update();
	}
	else if (order.get_count() != old_order_count) {
		build_vehicle_list();
		init_input_value_range();
	}
	gui_frame_t::draw(pos, size);
}


bool consist_order_frame_t::infowin_event(const event_t *ev)
{
	if (ev->ev_class == INFOWIN && ev->ev_code == WIN_CLOSE) {
		save_order();
		return false;
	}
	return gui_frame_t::infowin_event(ev);
}


bool consist_order_frame_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	// [CONVOY COPIER]
	if( comp==&scl_convoys  &&  own_convoys.get_count() ) {
		scl_convoys.get_selection();
		convoy_scrollitem_t *item = (convoy_scrollitem_t*)scl_convoys.get_element(v.i);
		selected_convoy = item->get_convoy();
		update_convoy_info();
	}
	else if( comp==&bt_filter_halt_convoy ){
		bt_filter_halt_convoy.pressed ^= 1;
		if( bt_filter_halt_convoy.pressed ) {
			bt_filter_line_convoy.pressed = false;
		}
		build_vehicle_list();
	}
	else if( comp==&bt_filter_line_convoy){
		bt_filter_line_convoy.pressed ^= 1;
		if( bt_filter_line_convoy.pressed ) {
			bt_filter_halt_convoy.pressed = false;
		}
		build_vehicle_list();
	}
	else if( comp==&bt_filter_single_vehicle ){
		bt_filter_single_vehicle.pressed = !bt_filter_single_vehicle.pressed;
		build_vehicle_list();
	}
	else if( comp==&bt_show_hide_convoy_filter ) {
		lb_open_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		cont_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		bt_show_hide_convoy_filter.set_text(lb_open_convoy_filter.is_visible() ? "+" : "-");

	}
	else if( comp==&bt_sort_order_cnv ) {
		bt_sort_order_cnv.pressed ^= 1;
		// TODO: execute sorting
	}
	else if(  comp==&bt_copy_convoy  ) {
		if( !selected_convoy.is_bound() ) {
			create_win(new news_img("No valid convoy selected!"), w_time_delete, magic_none);
		}
		else {
			order.set_convoy_order(selected_convoy);
		}
		init_input_value_range();
		edit_action_selector.set_selection(0);
		numimp_append_target.set_value(order.get_count()+1);
		cont_order_overview.set_selected_index(order.get_count() + 1);
	}
	else if(  comp==&bt_convoy_detail  ) {
		if (selected_convoy.is_bound()) {
			create_win(20, 20, new convoi_detail_t(selected_convoy), w_info, magic_convoi_detail+ selected_convoy.get_id() );
		}
		return true;
	}
	else if( comp==&freight_type_c ) {
		const int selection_temp = freight_type_c.get_selection();
		if (selection_temp == freight_type_c.count_elements()-1) {
			filter_catg = goods_manager_t::INDEX_NONE;
		}
		else {
			switch( selection_temp )
			{
				case 0:
					filter_catg = 255; // all
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
		}
		build_vehicle_list();
	}
	//else if (comp == &cc_sortedby) {
	//	// TODO: add sort option
	//
	//}

	// [VEHICLE PICKER]
	else if( comp==&scl_vehicles ) {
		scl_vehicles.get_selection();
		vehicle_scrollitem_t *item = (vehicle_scrollitem_t*)scl_vehicles.get_element(v.i);
		selected_vehicle = item->get_vehicle();
		if( selected_vehicle==NULL ) {
			bt_add_vehicle.disable();
		}
		else {
			open_vehicle_detail(selected_vehicle);
			bt_add_vehicle.enable();
		}
		cont_picker_frame.set_size(cont_picker_frame.get_min_size());
		// adjust the size if the tab is open
		if (tabs.get_active_tab_index() == 0) {
			reset_min_windowsize();
			resize(scr_size(0,0));
		}
	}
	else if( comp==&vp_sortedby ) {
		vehicle_scrollitem_t::sort_mode = v.i;
		scl_vehicles.sort(0);
	}
	else if( comp==&bt_sort_order_veh ) {
		vehicle_scrollitem_t::sortreverse = !vehicle_scrollitem_t::sortreverse;
		scl_vehicles.sort(0);
		bt_sort_order_veh.pressed = vehicle_scrollitem_t::sortreverse;
	}
	else if( comp==&bt_connectable_vehicle_filter ) {
		bt_connectable_vehicle_filter.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&bt_outdated ) {
		bt_outdated.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&bt_obsolete ) {
		bt_obsolete.pressed ^= 1;
		build_vehicle_list();
	}
	else if(comp==&bt_show_unidirectional ) {
		bt_show_unidirectional.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&vp_powered_filter || comp==&engine_filter ) {
		build_vehicle_list();
	}
	else if( comp==&numimp_append_target ) {
		build_vehicle_list();
		const sint32 value = numimp_append_target.get_value();
		cont_order_overview.set_selected_index((edit_action_selector.get_selection()==0) ? value : -value);
	}
	else if(  comp==&edit_action_selector  ) {
		init_input_value_range();
		const sint32 value = numimp_append_target.get_value();
		cont_order_overview.set_selected_index((edit_action_selector.get_selection()==0) ? value : -value);
	}
	else if( comp==&bt_add_vehicle ) {
		if (!selected_vehicle) {
			create_win(new news_img("No vehicle selected!"), w_time_delete, magic_none);
			return true;
		}
		uint32 append_target_index = (uint32)numimp_append_target.get_value();
		if (append_target_index<1 || append_target_index > order.get_count()+1) {
			append_target_index=order.get_count();
		}

		if (edit_action_selector.get_selection()==0 || !order.get_count()) {
			// add consist_order_element_t at certain position
			consist_order_element_t new_elem;
			new_elem.append_vehicle(selected_vehicle);
			order.insert_at(append_target_index-1, new_elem);
			const sint32 new_index = max(2, append_target_index + 1);
			numimp_append_target.set_value(new_index);
			cont_order_overview.set_selected_index((edit_action_selector.get_selection() == 0) ? new_index : -new_index);
		}
		else {
			// append the vehicle as a vehicle description element
			order.append_vehicle_at(append_target_index-1, selected_vehicle);
		}

		init_input_value_range();
	}
	// others
	else if (comp == &tabs) {
		if (tabs.get_active_tab_index() == 0) {
			cont_order_overview.set_selected_index(0);
		}
		else {
			const sint32 value = numimp_append_target.get_value();
			cont_order_overview.set_selected_index((edit_action_selector.get_selection() == 0) ? value : -value);
		}
	}
	resize(scr_size(0,0));
	return false;
}


// TODO: filter, consider connection
// v-shape filter, catg filter
// TODO: Consider vehicle reversal
// NOTE: The execution of consist orders entails, for reversed consists, de-reversing it, adding the new vehicle and then reversing it again.
void consist_order_frame_t::build_vehicle_list()
{
	const bool search_only_halt_convoy = bt_filter_halt_convoy.pressed;
	const bool search_only_line_convoy = bt_filter_line_convoy.pressed;
	const bool search_only_appendable_vehicle = bt_connectable_vehicle_filter.pressed;

	own_vehicles.clear();
	scl_vehicles.clear_elements();
	const vehicle_desc_t *old_vehicle = selected_vehicle;
	selected_vehicle = NULL;
	scl_vehicles.set_selection(-1);

	own_convoys.clear();
	scl_convoys.clear_elements();
	scl_convoys.set_selection(-1);

	old_order_count = order.get_count();

	// Vehicles that have already been determined to be unconnectable
	slist_tpl<const vehicle_desc_t *>unconnectable_vehicles;
	uint32 append_target_index = min((uint32)numimp_append_target.get_value()-1, order.get_count());

	// list only own vehicles
	for (auto const cnv : world()->convoys()) {
		if((cnv->get_owner() == player || cnv->get_owner()->allows_access_to(player->get_player_nr()) && player->allows_access_to(cnv->get_owner()->get_player_nr())) && cnv->front()->get_waytype()==schedule->get_waytype())
		{
			// count own vehicle
			for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
				const vehicle_desc_t *veh_type = cnv->get_vehicle(i)->get_desc();
				// filter
				if (is_filtered(veh_type)) continue;

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
					// Exclude same vehicle
					if (edit_action_selector.get_selection() == 1 && order.get_count() && append_target_index < order.get_count()) {
						if(order.get_order(append_target_index).has_same_vehicle(veh_type)) {
							unconnectable_vehicles.append(veh_type);
							continue;
						}
					}

					if (search_only_appendable_vehicle) {
						// Check if appendable
						if (unconnectable_vehicles.is_contained(veh_type)) {
							continue;
						}
						// prev
						if (append_target_index==0 && edit_action_selector.get_selection() == 1) {
							if (!(veh_type->get_basic_constraint_prev()&vehicle_desc_t::can_be_head)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (append_target_index && !order.get_order(append_target_index-1).can_connect(veh_type, true)) {
							unconnectable_vehicles.append(veh_type);
							continue;
						}
						// next
						uint32 target_order = edit_action_selector.get_selection()==0 ? append_target_index : append_target_index+1;
						if (target_order<order.get_count()) {
							if (!order.get_order(target_order).can_connect(veh_type, false)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (edit_action_selector.get_selection() == 1) {
							if (!veh_type->can_lead(NULL)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
					}
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
			if (!search_only_halt_convoy && !search_only_line_convoy) {
				// filter
				if( filter_catg!=255  &&  filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
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
				if (is_filtered(veh_type)) continue;

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
					if (search_only_appendable_vehicle) {
						// Check if appendable
						if (unconnectable_vehicles.is_contained(veh_type)) {
							continue;
						}
						// prev
						if (append_target_index==1) {
							if (!veh_type->can_follow(NULL)) continue;
						}
						else if (append_target_index > 1 && append_target_index <= order.get_count()) {
							if (!order.get_order(append_target_index - 1).can_connect(veh_type, false)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						// next
						if (edit_action_selector.get_selection() == 0) {
							if (append_target_index >= order.get_count()) {
								if (!veh_type->can_lead(NULL)) continue;
							}
							else if (!order.get_order(append_target_index-1).can_connect(veh_type)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (edit_action_selector.get_selection() == 1) {
							if (append_target_index >= order.get_count()-1) {
								if (!veh_type->can_lead(NULL)) continue;
							}
							else if (!order.get_order(append_target_index).can_connect(veh_type)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
					}
					own_vehicle_t temp;
					temp.count = 1;
					temp.veh_type = veh_type;
					own_vehicles.append(temp);
				}
			}
		}
	}

	if( search_only_halt_convoy || search_only_line_convoy ) {
		// halt line
		for (uint32 i=0; i < halt->registered_lines.get_count(); ++i) {
			const linehandle_t line = halt->registered_lines[i];
			if( line->get_owner()==player  &&  line->get_schedule()->get_waytype()==schedule->get_waytype() ) {
				if (search_only_line_convoy && !schedule->matches(world(),line->get_schedule())) {
					continue;
				}
				for( uint32 j=0; j < line->count_convoys(); ++j ) {
					convoihandle_t const cnv = line->get_convoy(j);
					if (cnv->in_depot()) {
						continue;
					}
					// filter
					if( filter_catg!=255  &&  filter_catg != goods_manager_t::INDEX_NONE && !cnv->get_goods_catg_index().is_contained(filter_catg)) {
						continue;
					}
					if( bt_filter_single_vehicle.pressed  &&  cnv->get_vehicle_count()<2 ) {
						continue;
					}
					// Exclude convoys consisting of the same vehicle in the same line
					bool alredey_append = false;
					uint8 k = j;
					while (k > 0 && !alredey_append) {
						--k;
						if (cnv->has_same_vehicles(line->get_convoy(k))) {
							alredey_append=true;
						}
					}
					if (alredey_append) {
						continue;
					}
					own_convoys.append(cnv);
				}
			}
		}
	}
	if (search_only_halt_convoy) {
		// halt convoy
		for (uint32 i=0; i < halt->registered_convoys.get_count(); ++i) {
			const convoihandle_t cnv = halt->registered_convoys[i];
			if( cnv->get_owner()==player  &&cnv->front()->get_waytype()==schedule->get_waytype() ) {
				// filter
				if( filter_catg!=255  &&  filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
					continue;
				}
				own_convoys.append(cnv);
			}
		}
	}

	// update selector
	for (auto &own_veh : own_vehicles) {
		if (own_veh.veh_type == old_vehicle) {
			selected_vehicle = old_vehicle;
		}
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_veh);
		if (own_veh.veh_type == selected_vehicle) {
			// reselect
			scl_vehicles.set_selection(scl_vehicles.get_count()-1);
		}
	}
	if( !own_vehicles.get_count() ) {
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_vehicle_t());
	}
	scl_vehicles.sort(0);
	if (!selected_vehicle) {
		// close vehicle details
		destroy_win(magic_vehicle_detail_for_consist_order);
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
	}
	update_convoy_info();
	reset_min_windowsize();
	resize(scr_size(0,0));
}


bool consist_order_frame_t::is_filtered(const vehicle_desc_t *veh_type)
{
	const int filter_powered_vehicle = vp_powered_filter.get_selection();
	const int filter_engine_type = engine_filter.get_selection();
	if (filter_catg != 255 && veh_type->get_freight_type()->get_catg_index() != filter_catg) {
		return true;
	}
	if (filter_powered_vehicle == 1 && !veh_type->get_power()) {
		return true;
	}
	else if (filter_powered_vehicle == 2 && veh_type->get_power()) {
		return true;
	}
	if (filter_engine_type > 0 && (uint8)veh_type->get_engine_type() != filter_engine_type - 1) {
		return true;
	}
	if (!bt_outdated.pressed && veh_type->get_vehicle_status_color() == SYSCOL_OUT_OF_PRODUCTION) {
		return true;
	}
	if (!bt_obsolete.pressed && veh_type->get_vehicle_status_color() == SYSCOL_OBSOLETE) {
		return true;
	}
	if (!bt_show_unidirectional.pressed && !veh_type->is_bidirectional()) {
		return true;
	}

	return false;
}

// reflesh labels, call when entry changed
void consist_order_frame_t::update()
{
	halt = halthandle_t();
	old_entry_count = schedule->get_count();
	// serach entry and halt
	uint8 entry_idx = 255;
	if (old_entry_count) {
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
	}
	init_input_value_range();
	lb_halt.update();
	resize(scr_size(0,0));
}


void consist_order_frame_t::open_vehicle_detail(const vehicle_desc_t* veh_type) const
{
	if (veh_type) {
		vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail_for_consist_order));
		if (!win) {
			// try to open to the right
			scr_coord sc = win_get_pos(this);
			scr_coord lc = sc;
			lc.x += get_windowsize().w;
			if (lc.x > display_get_width()) {
				lc.x = max(0, display_get_width() - 100);
			}
			create_win(lc.x, lc.y, new vehicle_detail_t(selected_vehicle), w_info, magic_vehicle_detail_for_consist_order, true);
			top_win(this, false); // Keyscroll should be enabled continuously. This window must remain on topmost.
		}
		else {
			win->set_vehicle(selected_vehicle);
			////top_win(win, false); // NOTE: Keyscroll should be enabled continuously. So don't bring the new window to topmost.
		}
	}
}
