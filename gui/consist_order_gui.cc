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
#include "../bauer/vehikelbauer.h"
#include "../display/viewport.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"
#include "components/gui_divider.h"
#include "components/gui_table.h"
#include "components/gui_waytype_image_box.h"
#include "../player/finance.h"


#define L_OWN_VEHICLE_COUNT_WIDTH (proportional_string_width("8,888") + D_H_SPACE)
#define L_OWN_VEHICLE_LABEL_OFFSET_LEFT (L_OWN_VEHICLE_COUNT_WIDTH + VEHICLE_BAR_HEIGHT*4+D_H_SPACE)

bool consist_order_frame_t::need_reflesh_descriptions = false;
bool consist_order_frame_t::need_reflesh_order_list = false;

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
	"Fixed cost per month",
	"Fuel per km",
	"Staff factor",
	"Drivers"
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
					if (veh_type->get_fixed_cost(world())) {
						lb->buf().printf("%1.2f$", veh_type->get_adjusted_monthly_fixed_cost() / 100.0);
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				case SPEC_FUEL_PER_KM:
					if( veh_type->get_power() && veh_type->get_fuel_per_km() ) {
						lb->buf().printf("%u %s", veh_type->get_fuel_per_km(), translator::translate("km/L") );
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				case SPEC_STAFF_FACTOR:
					if (veh_type->get_total_staff_hundredths()) {
						lb->buf().printf("%u", veh_type->get_total_staff_hundredths());
					}
					else {
						lb->buf().append("-");
						lb->set_color(SYSCOL_TEXT_WEAK);
					}
					break;
				case SPEC_DRIVERS:
					if (veh_type->get_total_drivers()) {
						lb->buf().printf("%u", veh_type->get_total_drivers());
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
	set_table_layout(1,0);
	set_alignment(ALIGN_CENTER_H);

	bt_remove.init(button_t::box, "X");
	bt_remove.background_color=color_idx_to_rgb(COL_RED);
	bt_remove.set_tooltip(translator::translate("Remove this vehicle from consist."));
	bt_remove.add_listener(this);
	add_component(&bt_remove);

	gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
	lb->buf().printf("%u", order_element_index + 1);
	lb->set_fixed_width(proportional_string_width("888"));
	lb->update();

	consist_order_element_t order_element = order->get_order(order_element_index);
	vehicle_description_element element = order_element.get_vehicle_description(0); // TODO: Add the capability to have stacked vehicle description slots by priority here
	if(order_element.get_vehicle_description(0).specific_vehicle ) {
		// TODO: reverse image or not
		new_component<gui_fill_t>(false,true);
		new_component<gui_image_t>(element.specific_vehicle->get_image_id(ribi_t::dir_south, goods_manager_t::none), player_nr, 0, true)->set_tooltip(translator::translate(element.specific_vehicle->get_name()));
		vehicle_bar.set_flags(element.specific_vehicle->get_basic_constraint_prev(false), element.specific_vehicle->get_basic_constraint_next(false), element.specific_vehicle->get_interactivity());
	}
	else {
		new_component<gui_fill_t>(false,true);
		new_component<gui_label_t>("(???)", SYSCOL_TEXT_WEAK);
		vehicle_bar.set_flags(vehicle_desc_t::unknown_constraint, vehicle_desc_t::unknown_constraint, (element.min_power>0||element.min_tractive_effort>0) ? 2:0);
	}

	check_constraint();
	add_component(&vehicle_bar);

	add_table(2,1)->set_spacing(scr_size(0,0));
	{
		bt_up.init(button_t::roundbox_left, "<");
		bt_up.enable(order_element_index >0);
		bt_down.init(button_t::roundbox_right, ">");
		bt_down.enable(order_element_index < order->get_order(order_element_index).get_count() - 1);
		bt_down.add_listener(this);
		bt_up.add_listener(this);
		add_component(&bt_up);
		add_component(&bt_down);
	}
	end_table();

	//add_component(&bt_inverse);
	bt_can_empty.init(button_t::square_state, "");
	bt_can_empty.set_tooltip(translator::translate("This slot is skippable and no vehicles are allowed."));
	bt_can_empty.pressed = element.empty;
	bt_can_empty.add_listener(this);
	add_component(&bt_can_empty);

	bt_edit.init(button_t::roundbox, "Edit");
	bt_edit.set_tooltip(translator::translate("Manually defines the description of vehicles that can occupy this slot."));
	bt_edit.add_listener(this);
	bt_edit.enable(!element.specific_vehicle);
	add_component(&bt_edit);
}

void gui_vehicle_description_t::check_constraint()
{
	bool dummy;

	bool is_reversed = false; // TODO: When considering vehicle reversal
	PIXVAL col_front_state = 0;
	PIXVAL col_rear_state  = 0;

	consist_order_element_t order_element = order->get_order(description_index);
	vehicle_description_element element = order_element.get_vehicle_description(0);

	// front side
	if( element.specific_vehicle ){
		// vehicle at front
		if (order_element_index == 0) {
			// front vehicle => as it is
			col_front_state = element.specific_vehicle->get_can_be_at_front(is_reversed) ? COL_SAFETY : COL_DANGER;
		}
		else {
			// TODO:

		}

	}
	else {
		// TODO:

	}

	// rear side
	if (element.specific_vehicle) {
		// vehicle at end
		if (order_element_index == order_element.get_count()-1) {
			col_rear_state = element.specific_vehicle->get_can_be_at_rear(is_reversed) ? COL_SAFETY : COL_DANGER;
		}
		else {
			// TODO:

		}
	}
	else {
		// TODO:

	}

	// update color
	vehicle_bar.set_color(col_front_state, col_rear_state);
}


bool gui_vehicle_description_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	consist_order_element_t *order_element = &order->access_order(description_index);
	if(  comp==&bt_up  ) {
		// FIXME
		order_element->increment_index(description_index-1);
		consist_order_frame_t::need_reflesh_descriptions = true;
	}
	else if(  comp==&bt_down  ) {
		// FIXME
		order_element->increment_index(description_index);
		consist_order_frame_t::need_reflesh_descriptions = true;
	}
	else if(  comp==&bt_remove  ) {
		order_element->remove_vehicle_description_at(0); // TODO: Add the capability to have stacked vehicle description slots by priority here
		consist_order_frame_t::need_reflesh_order_list = true;
	}
	else if(  comp==&bt_edit  ) {
		// access editor
		consist_order_frame_t *win = dynamic_cast<consist_order_frame_t*>(win_get_magic(magic_consist_order));
		if (win) {
			win->open_description_editor(0); // TODO: Add the capability to have stacked vehicle description slots by priority here
		}
	}
	else if(  comp==&bt_can_empty  ) {
		bt_can_empty.pressed ^= 1;
		order_element->access_vehicle_description(0).set_empty(bt_can_empty.pressed); // TODO: Add the capability to have stacked vehicle description slots by priority here
		consist_order_frame_t::need_reflesh_descriptions = true; // because changes may occur in the connectabale status
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
	consist_order_frame_t::need_reflesh_descriptions = false;
	remove_all();
	uint8 total_vehicles = order->get_count();
	set_table_layout(2,0);
	if (!order->get_count() || order_element_index >= order->get_count() ) {
		old_count = 0;
		// empty or invalid element index
		new_component<gui_label_t>("Select a consist order", SYSCOL_TEXT_WEAK);
	}
	else{
		consist_order_element_t elem = order->get_order(order_element_index);
		old_count = elem.get_count();
		if( !total_vehicles || !old_count) {
			new_component<gui_label_t>("Set vehicle descriptions", SYSCOL_TEXT_WEAK);
		}
		else {
			uint8 max_rows = 3; // FIXME
			add_table(total_vehicles +1, max_rows)->set_alignment(ALIGN_CENTER_H);
			{
				for (uint8 row = 0; row < max_rows; row++) {
					for (uint8 col = 0; col < total_vehicles +1; col++) {
						if (col==0) {
							// header
							switch (row) {
								case 0:
									new_component<gui_empty_t>();
									break;
								case 1:
									new_component<gui_table_header_t>("Freight", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_flexible(true, false);
									break;
								case 2:
									new_component<gui_table_header_t>(vehicle_spec_texts[0], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_flexible(true, false);
									break;
								default:
									break;
							}
						}
						else {
							// vehicle data
							if (row==0) {
								new_component<gui_vehicle_description_t>(order, player_nr, col - 1, 0); // TODO: Allow for stacked vehicle description elements
								continue;
							}
							const vehicle_description_element vde = order->get_order(col - 1).get_vehicle_description(0); // TODO: Allow for stacked vehicle description elements
							if (row==1) {
								if (vde.specific_vehicle) {
									new_component<gui_image_t>(vde.specific_vehicle->get_freight_type()->get_catg_symbol(), 0, ALIGN_CENTER_V | ALIGN_CENTER_H, true);
								}
								else {
									// TODO? : When category can be specified in description
									new_component<gui_empty_t>();
								}
								continue;
							}
							gui_table_cell_buf_t *td = new_component<gui_table_cell_buf_t>();
							td->set_flexible(true,false);
							switch (row) {
								case 2:
									if (vde.specific_vehicle) {
										td->buf().append(vde.specific_vehicle->get_capacity(), 0);
									}
									else {
										if (vde.min_capacity>0) {
											td->buf().append(vde.min_capacity, 0);
										}
										if (vde.min_capacity>0 || vde.max_capacity<65535) {
											td->buf().append(" - ");
											td->set_color(SYSCOL_TEXT_STRONG);
										}
										if (vde.max_capacity<65535) {
											td->buf().append(vde.max_capacity, 0);
										}
									}
									break;
								case 0:
								case 1:
								default:
									/* skip */
									break;
							}
							td->update();
						}
					}
				}
			}
			end_table();
			new_component<gui_fill_t>();
			new_component<gui_fill_t>(false, true);
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
		else if (consist_order_frame_t::need_reflesh_descriptions == true) {
			init_table();
		}
		gui_aligned_container_t::draw(offset);
	}
}


consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t(translator::translate("consist_order")),
	halt_number(255),
	cont_order_overview(&order),
	scl(gui_scrolled_list_t::listskin),
	scl_vehicles(gui_scrolled_list_t::listskin),
	scl_convoys(gui_scrolled_list_t::listskin),
	scroll_order(&cont_order, true, true),
	img_convoy(convoihandle_t()),
	formation(convoihandle_t(), false),
	scrollx_formation(&formation, true, false),
	scroll_editor(&cont_vdesc_editor, true, true)
{
	if (player && schedule) {
		init(player, schedule, entry_id);
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


void consist_order_frame_t::init(player_t* player, schedule_t *schedule, uint16 entry_id)
{
	this->player = player;
	this->schedule = schedule;
	if( unique_entry_id!=65535  &&  unique_entry_id != entry_id) {
		save_order();
	}
	unique_entry_id = entry_id;
	order=schedule->orders.get(unique_entry_id);
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

	bt_filter_halt_convoy.pressed = false;
	bt_filter_line_convoy.pressed = true;
	bt_filter_single_vehicle.pressed = false;
	cont_convoy_filter.set_visible(false);
	update();

	set_table_layout(1,0);


	//debug
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(COL_DANGER);
	lb->buf().printf("(debug)entry-id: %u", unique_entry_id);
	lb->update();

	new_component<gui_label_t>("Select the vehicles that you want to make up this consist at this stop and onwards on this schedule.");

	add_table(3,1);
	{
		new_component<gui_waytype_image_box_t>(schedule->get_waytype());
		add_component(&halt_number);
		add_component(&lb_halt);
	}
	end_table();

	// [OVERVIEW] (orders)
	add_table(3,1);
	{
		add_table(1,3)->set_alignment(ALIGN_TOP);

		new_component<gui_fill_t>(false, true);

		end_table();
		update_order_list(0);

		cont_order.set_table_layout(1,0);
		cont_order.set_margin(scr_size(0,D_V_SPACE), scr_size(D_SCROLLBAR_WIDTH,0));
		cont_order.add_component(&cont_order_overview);
		cont_order.new_component<gui_fill_t>();
		scroll_order.set_maximize(true);

		add_component(&scroll_order,2);

		cont_order.set_size(cont_order.get_min_size());
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
	build_vehicle_list();
	cont_picker_frame.set_table_layout(2,2);
	cont_picker_frame.set_alignment(ALIGN_TOP);
	{
		// TODO: Place show/hide option when filter option is increased
		bt_connectable_vehicle_filter.init(button_t::square_state, "show_only_appendable");
		bt_connectable_vehicle_filter.set_tooltip(translator::translate("Show only vehicles that can be added to the end of the selected order."));
		bt_connectable_vehicle_filter.pressed = true;
		bt_connectable_vehicle_filter.add_listener(this);
		cont_picker_frame.add_component(&bt_connectable_vehicle_filter);

		cont_picker_frame.add_table(2,1);
		{
			bt_add_vehicle.init(button_t::roundbox, "Add vehicle");
			bt_add_vehicle.enable( selected_vehicle!=NULL );
			bt_add_vehicle.add_listener(this);
			cont_picker_frame.add_component(&bt_add_vehicle);

			bt_add_vehicle_limit_vehicle.init(button_t::square_state, "limit_to_same_vehicle");
			bt_add_vehicle_limit_vehicle.enable( selected_vehicle!=NULL );
			bt_add_vehicle_limit_vehicle.pressed = true;
			bt_add_vehicle_limit_vehicle.set_tooltip(translator::translate("Limited to the same vehicle"));
			bt_add_vehicle_limit_vehicle.add_listener(this);
			cont_picker_frame.add_component(&bt_add_vehicle_limit_vehicle);
		}
		cont_picker_frame.end_table();

		cont_picker_frame.add_table(1,2)->set_alignment(ALIGN_TOP);
		{
			cont_picker_frame.add_table(3, 2);
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

			scl_vehicles.set_maximize(true);
			scl_vehicles.add_listener(this);
			cont_picker_frame.add_component(&scl_vehicles);
		}
		cont_picker_frame.end_table();


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
			bt_copy_convoy.init(button_t::roundbox, "copy_consist_order");
			bt_copy_convoy.set_tooltip(translator::translate("Set the order of this consist to the selected order"));
			bt_copy_convoy.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy);

			bt_copy_convoy_limit_vehicle.init(button_t::square_state, "limit_to_same_vehicle");
			bt_copy_convoy_limit_vehicle.pressed = true;
			bt_copy_convoy_limit_vehicle.set_tooltip(translator::translate("Limited to the same vehicle"));
			bt_copy_convoy_limit_vehicle.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy_limit_vehicle);

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

	// [VEHICLE DESCRIPTION EDITOR]
	init_editor();

	tabs.add_tab(&cont_convoy_copier, translator::translate("Consist copier"));
	tabs.add_tab(&cont_picker_frame, translator::translate("Vehicle picker"));
	tabs.add_tab(&scroll_editor, translator::translate("desc_editor"));
	add_component(&tabs);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));
	set_resizemode(diagonal_resize);
}


void consist_order_frame_t::init_editor()
{
	cont_vdesc_editor.remove_all();
	cont_vdesc_editor.set_table_layout(1,0);
	cont_vdesc_editor.add_table(5,1);
	{
		edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("anhaengen"), SYSCOL_TEXT); // append
		edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("voranstellen"), SYSCOL_TEXT); // insert
		edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Overwrite"), SYSCOL_TEXT);
		edit_action_selector.set_selection(0);
		edit_action_selector.add_listener(this);
		cont_vdesc_editor.add_component(&edit_action_selector);

		numimp_edit_target.disable();
		numimp_edit_target.set_limits(1, 255);
		numimp_edit_target.set_value(edit_target_index);
		cont_vdesc_editor.add_component(&numimp_edit_target);

		bt_commit.init(button_t::roundbox, "Commit");
		bt_commit.add_listener(this);
		cont_vdesc_editor.add_component(&bt_commit);

		bt_reset_editor.init(button_t::roundbox, "reset");
		bt_reset_editor.add_listener(this);
		cont_vdesc_editor.add_component(&bt_reset_editor);

		cont_vdesc_editor.new_component<gui_fill_t>();
	}
	cont_vdesc_editor.end_table();

	// edit table
	gui_aligned_container_t *edit_table = cont_vdesc_editor.add_table(5,0);
	edit_table->set_table_frame(true, true);
	edit_table->set_margin(scr_size(3, 3), scr_size(3, 3));
	edit_table->set_spacing(scr_size(D_H_SPACE, 2));
	edit_table->set_alignment(ALIGN_CENTER_V);
	{
		cont_vdesc_editor.new_component<gui_table_header_t>(vehicle_spec_texts[0], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		bt_enable_rules[0].init(button_t::square_automatic, "");
		bt_enable_rules[0].add_listener(this);
		cont_vdesc_editor.add_component(&bt_enable_rules[0]);
		cont_vdesc_editor.add_component(&rules_imp_min[0]);
		cont_vdesc_editor.new_component<gui_label_t>(" - ");
		cont_vdesc_editor.add_component(&rules_imp_max[0]);
		bt_enable_rules[0].pressed = (new_vdesc_element.min_capacity != 0 || new_vdesc_element.max_capacity != 65535);
		if (bt_enable_rules[0].pressed) {
			rules_imp_min[0].set_value(new_vdesc_element.min_capacity);
			rules_imp_max[0].set_value(new_vdesc_element.max_capacity);
		}
		rules_imp_min[0].enable(bt_enable_rules[0].pressed);
		rules_imp_max[0].enable(bt_enable_rules[0].pressed);

		cont_vdesc_editor.new_component<gui_table_header_t>("engine_type", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
		engine_type_rule.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All traction types"), SYSCOL_TEXT);
		for (int i = 0; i < vehicle_desc_t::MAX_TRACTION_TYPE; i++) {
			engine_type_rule.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)i]), SYSCOL_TEXT);
		}
		engine_type_rule.add_listener(this);
		engine_type_rule.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
		engine_type_rule.set_width_fixed(true);
		cont_vdesc_editor.add_component(&engine_type_rule,4);

		for (uint8 i = 1; i< gui_simple_vehicle_spec_t::MAX_VEH_SPECS;i++) {
			cont_vdesc_editor.new_component<gui_table_header_t>(vehicle_spec_texts[i], SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::left)->set_fixed_width(D_WIDE_BUTTON_WIDTH);
			bt_enable_rules[i].init(button_t::square_automatic, "");
			bt_enable_rules[i].add_listener(this);
			cont_vdesc_editor.add_component(&bt_enable_rules[i]);
			rules_imp_min[i].disable();
			cont_vdesc_editor.add_component(&rules_imp_min[i]);
			cont_vdesc_editor.new_component<gui_label_t>(" - ");
			rules_imp_max[i].disable();
			cont_vdesc_editor.add_component(&rules_imp_max[i]);

			// read new_vdesc_element
			switch (i) {
					case gui_simple_vehicle_spec_t::SPEC_RANGE:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_range != 0 || new_vdesc_element.max_range != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_range);
							rules_imp_max[i].set_value(new_vdesc_element.max_range);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_POWER:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_power != 0 || new_vdesc_element.max_power != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_power);
							rules_imp_max[i].set_value(new_vdesc_element.max_power);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_TRACTIVE_FORCE:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_tractive_effort != 0 || new_vdesc_element.max_tractive_effort != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_tractive_effort);
							rules_imp_max[i].set_value(new_vdesc_element.max_tractive_effort);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_BRAKE_FORCE:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_brake_force != 0 || new_vdesc_element.max_brake_force != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_brake_force);
							rules_imp_max[i].set_value(new_vdesc_element.max_brake_force);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_SPEED:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_topspeed != 0 || new_vdesc_element.max_topspeed != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_topspeed);
							rules_imp_max[i].set_value(new_vdesc_element.max_topspeed);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_WEIGHT:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_weight != 0 || new_vdesc_element.max_weight != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_weight);
							rules_imp_max[i].set_value(new_vdesc_element.max_weight);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_AXLE_LOAD:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_axle_load != 0 || new_vdesc_element.max_axle_load != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_axle_load);
							rules_imp_max[i].set_value(new_vdesc_element.max_axle_load);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_RUNNING_COST:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_running_cost != 0 || new_vdesc_element.max_running_cost != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_running_cost);
							rules_imp_max[i].set_value(new_vdesc_element.max_running_cost);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_FIXED_COST:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_fixed_cost != 0 || new_vdesc_element.max_fixed_cost != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_fixed_cost);
							rules_imp_max[i].set_value(new_vdesc_element.max_fixed_cost);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_FUEL_PER_KM:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_fuel_per_km != 0 || new_vdesc_element.max_fuel_per_km != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_fuel_per_km);
							rules_imp_max[i].set_value(new_vdesc_element.max_fuel_per_km);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_STAFF_FACTOR:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_staff_hundredths != 0 || new_vdesc_element.max_staff_hundredths != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_staff_hundredths);
							rules_imp_max[i].set_value(new_vdesc_element.max_staff_hundredths);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_DRIVERS:
						bt_enable_rules[i].pressed = (new_vdesc_element.min_drivers != 0 || new_vdesc_element.max_drivers != UINT32_MAX_VALUE);
						if (bt_enable_rules[i].pressed) {
							rules_imp_min[i].set_value(new_vdesc_element.min_drivers);
							rules_imp_max[i].set_value(new_vdesc_element.max_drivers);
						}
						break;
					case gui_simple_vehicle_spec_t::SPEC_PAYLOADS:
					default:
							break;
			}
			if (bt_enable_rules[i].pressed) {
				rules_imp_min[i].enable();
				rules_imp_max[i].enable();
			}
		}
	}
	cont_vdesc_editor.end_table();


	cont_vdesc_editor.set_size(cont_vdesc_editor.get_min_size());
}


void consist_order_frame_t::open_description_editor(uint8 vdesc_index)
{
	consist_order_element_t *order_element = &order.access_order(vdesc_index);
	set_vehicle_description(order_element->get_vehicle_description(0)); // TODO: Add the capability to have stacked vehicle description slots by priority here
	numimp_edit_target.set_value(vdesc_index+1);
	edit_action_selector.set_selection(2); // overwrite mode
	tabs.set_active_tab_index(2);
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
		if( !halt.is_null() ) { destroy_win(this); }

		lb_halt.buf().append(halt->get_name());
	}
	lb_halt.update();

	update_order_list();
}


void consist_order_frame_t::update_order_list(sint32 reselect_index)
{
	need_reflesh_order_list = false;
	sint32 current_selection = scl.get_selection();
	scl.clear_elements();
	if( !order.get_count() ) {
		// Need an empty order to edit
		append_new_order();
	}
	old_order_count = order.get_count();
	for( uint32 i=0; i<old_order_count; ++i ) {
		cbuffer_t buf;
		const uint32 v_description_count = order.get_order(i).get_count();
		buf.printf("%s #%u (%u)", translator::translate("Consist order"), i+1, v_description_count);
		scl.new_component<gui_scrolled_list_t::buf_text_scrollitem_t>(buf, v_description_count ? SYSCOL_TEXT : SYSCOL_EMPTY);
	}

	// reselect the selection
	scl.set_selection(reselect_index >=(sint32)old_order_count ? -1 : reselect_index);
	cont_order_overview.set_element((uint32)scl.get_selection(), player->get_player_nr());
	resize(scr_size(0,0));
}

void consist_order_frame_t::update_vehicle_info()
{
	if( selected_vehicle==NULL ) {
		veh_specs.set_visible(false);
		bt_add_vehicle.disable();
		bt_add_vehicle_limit_vehicle.disable();
	}
	else {
		veh_specs.set_vehicle(selected_vehicle);
		veh_specs.set_visible(true);
		bt_add_vehicle.enable();
		bt_add_vehicle_limit_vehicle.enable();
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
	bt_copy_convoy_limit_vehicle.enable(selected_convoy.is_bound());
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
	if (player != welt->get_active_player() || !schedule->get_count()) { destroy_win(this); }
	if( schedule->get_count() != old_entry_count ) {
		update();
	}
	else if( order.get_count()!=old_order_count ) {
		update_order_list();
	}
	else if (need_reflesh_order_list==true) {
		update_order_list(scl.get_selection());
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
	if( comp==&scl_vehicles ) {
		scl_vehicles.get_selection();
		vehicle_scrollitem_t *item = (vehicle_scrollitem_t*)scl_vehicles.get_element(v.i);
		selected_vehicle = item->get_vehicle();
		update_vehicle_info();
	}
	else if( comp==&scl_convoys  &&  own_convoys.get_count() ) {
		scl_convoys.get_selection();
		convoy_scrollitem_t *item = (convoy_scrollitem_t*)scl_convoys.get_element(v.i);
		selected_convoy = item->get_convoy();
		update_convoy_info();
	}
	else if( comp==&scl ) {
		cont_order_overview.set_element(scl.get_selection(), player->get_player_nr());
		resize(scr_size(0,0));
	}
	else if( comp==&bt_sort_order_veh ) {
		bt_sort_order_veh.pressed ^= 1;
		// TODO: execute sorting
	}
	else if( comp==&bt_sort_order_cnv ) {
		bt_sort_order_cnv.pressed ^= 1;
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
		const sint32 sel = scl.get_selection();
		if (scl.get_selection() < 0 || (uint32)sel >= order.get_count()) {
			create_win(new news_img("Select a target order!"), w_time_delete, magic_none);
			return true;
		}
		if (!selected_vehicle) {
			create_win(new news_img("No vehicle selected!"), w_time_delete, magic_none);
			return true;
		}
		// The below is wrong: it adds another *alternative* vehicle description to the existing order element rather than adding a new order element.
		/*
		consist_order_element_t *order_element = &order.access_order((uint32)sel);
		order_element->append_vehicle(selected_vehicle, bt_add_vehicle_limit_vehicle.pressed);
		*/

		consist_order_element_t new_element;
		new_element.append_vehicle(selected_vehicle, bt_add_vehicle_limit_vehicle.pressed);
		order.append(new_element);

		update_order_list(sel);
		if( bt_connectable_vehicle_filter.pressed ) {
			// Vehicle list needs to be updated
			build_vehicle_list();
			update_vehicle_info();
		}
	}
	else if( comp==&bt_connectable_vehicle_filter ) {
		bt_connectable_vehicle_filter.pressed ^= 1;
		build_vehicle_list();
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
	else if(  comp==&bt_copy_convoy  ) {
		if( scl.get_selection()==-1 ){
			create_win(new news_img("Select valid consist order slot!"), w_time_delete, magic_none);
		}
		else {
			if( !selected_convoy.is_bound() ) {
				create_win(new news_img("No valid convoy selected!"), w_time_delete, magic_none);
			}
			else {
				const sint32 sel = scl.get_selection();
				order.set_convoy_order(scl.get_selection(), selected_convoy, bt_copy_convoy_limit_vehicle.pressed);
				update_order_list(sel);
			}
		}
	}
	else if(  comp==&bt_commit  ) {
		const sint32 sel = scl.get_selection();
		if (scl.get_selection() < 0 || (uint32)sel >= order.get_count()) {
			create_win(new news_img("Select valid consist order slot!"), w_time_delete, magic_none);
			return true;
		}
		consist_order_element_t *order_element = &order.access_order((uint32)sel);
		switch (edit_action_selector.get_selection())
		{
			default:
			case 0: // append
				order_element->append_vehicle_description(new_vdesc_element);
				consist_order_frame_t::need_reflesh_order_list = true;
				break;
			case 1: // insert
				if (edit_target_index >= order_element->get_count()) {
					edit_target_index = order_element->get_count()-1;
				}
				order_element->insert_vehicle_description_at(edit_target_index, new_vdesc_element);
				consist_order_frame_t::need_reflesh_order_list = true;
				break;
			case 2: // overwrite
				if( edit_target_index >= order_element->get_count() ){
					create_win(new news_img("Invalid target!"), w_time_delete, magic_none);
					return true;
				}
				order_element->remove_vehicle_description_at(edit_target_index);
				order_element->insert_vehicle_description_at(edit_target_index, new_vdesc_element);
				break;
		}
		consist_order_frame_t::need_reflesh_descriptions = true;
	}
	else if(  comp==&bt_reset_editor  ) {
		set_vehicle_description();
		edit_target_index = 1;
		edit_action_selector.set_selection(0);
		numimp_edit_target.disable();
	}
	else if(  comp==&edit_action_selector  ) {
		// mode==append => disable target input
		numimp_edit_target.enable(edit_action_selector.get_selection()!=0);
	}
	else if(  comp==&numimp_edit_target  ) {
		edit_target_index = v.i;
	}
	else if(  comp==&bt_copy_convoy_limit_vehicle  ) {
		bt_copy_convoy_limit_vehicle.pressed = !bt_copy_convoy_limit_vehicle.pressed;
	}
	else if(  comp==&bt_add_vehicle_limit_vehicle  ) {
		bt_add_vehicle_limit_vehicle.pressed = !bt_add_vehicle_limit_vehicle.pressed;
	}
	else if(  comp == &bt_convoy_detail  ) {
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
	else {
		for( uint8 i=0; i<gui_simple_vehicle_spec_t::MAX_VEH_SPECS; i++ ) {
			if( comp==&bt_enable_rules[i] ) {
				rules_imp_min[i].enable(bt_enable_rules[i].pressed);
				rules_imp_max[i].enable(bt_enable_rules[i].pressed);

				// set or disable description value
				switch (i)
				{
					case gui_simple_vehicle_spec_t::SPEC_PAYLOADS:
						new_vdesc_element.min_capacity = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_capacity = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : 65535;
						break;
					case gui_simple_vehicle_spec_t::SPEC_RANGE:
						new_vdesc_element.min_range = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_range = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_POWER:
						new_vdesc_element.min_power = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_power = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_TRACTIVE_FORCE:
						new_vdesc_element.min_tractive_effort = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_tractive_effort = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_BRAKE_FORCE:
						new_vdesc_element.min_brake_force = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_brake_force = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : 65535;
						break;
					case gui_simple_vehicle_spec_t::SPEC_SPEED:
						new_vdesc_element.min_topspeed = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_topspeed = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_WEIGHT:
						new_vdesc_element.min_weight = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_weight = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_AXLE_LOAD:
						new_vdesc_element.min_axle_load = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_axle_load = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_RUNNING_COST:
						new_vdesc_element.min_running_cost = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_running_cost = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_FIXED_COST:
						new_vdesc_element.min_fixed_cost = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_fixed_cost = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_FUEL_PER_KM:
						new_vdesc_element.min_fuel_per_km = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_fuel_per_km = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_STAFF_FACTOR:
						new_vdesc_element.min_staff_hundredths = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_staff_hundredths = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					case gui_simple_vehicle_spec_t::SPEC_DRIVERS:
						new_vdesc_element.min_drivers = bt_enable_rules[i].pressed ? rules_imp_min[i].get_value() : 0;
						new_vdesc_element.max_drivers = bt_enable_rules[i].pressed ? rules_imp_max[i].get_value() : UINT32_MAX_VALUE;
						break;
					default:
							break;
				}
			}

			if ( comp==&rules_imp_min[i] ) {
				rules_imp_min[i].set_value(  min( v.i,rules_imp_max[i].get_value() )  );
			}
			if ( comp==&rules_imp_max[i] ) {
				rules_imp_max[i].set_value(  max( v.i,rules_imp_min[i].get_value() )  );
			}
		}
	}
	return false;
}


// TODO: filter, consider connection
// v-shape filter, catg filter
void consist_order_frame_t::build_vehicle_list()
{
	const bool search_only_halt_convoy = bt_filter_halt_convoy.pressed;
	const bool search_only_line_convoy = bt_filter_line_convoy.pressed;
	const bool search_only_appendable_vehicle = bt_connectable_vehicle_filter.pressed;

	own_vehicles.clear();
	scl_vehicles.clear_elements();
	selected_vehicle = NULL;
	scl_vehicles.set_selection(-1);

	own_convoys.clear();
	scl_convoys.clear_elements();
	scl_convoys.set_selection(-1);

	const vehicle_desc_t *conect_target = NULL;
	if( search_only_appendable_vehicle ) {
		consist_order_element_t *order_element = &order.access_order((uint32)scl.get_selection());
		if( order_element->get_count() ) {
			vehicle_description_element last_vde = order_element->get_vehicle_description(0); // TODO: Allow for stacked vehicle description elements
			conect_target = last_vde.specific_vehicle;
		}
	}

	// list only own vehicles
	for (auto const cnv : world()->convoys()) {
		if((cnv->get_owner() == player || (cnv->get_owner()->allows_access_to(player->get_player_nr()) && player->allows_access_to(cnv->get_owner()->get_player_nr()))) && cnv->front()->get_waytype()==schedule->get_waytype())
		{
			// count own vehicle
			for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
				const vehicle_desc_t *veh_type = cnv->get_vehicle(i)->get_desc();
				// TODO: filter speed power, type
				// filter
				if( filter_catg!=255  &&  veh_type->get_freight_type()->get_catg_index() != filter_catg) {
					continue;
				}
				// TODO: Consider vehicle reversal
				// NOTE: The execution of consist orders entails, for reversed consists, de-reversing it, adding the new vehicle and then reversing it again.
				if( conect_target!=NULL  &&  !conect_target->can_lead(veh_type) ) {
					continue;  // cannot append
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
		if((depot->get_owner() == player || (depot->get_owner()->allows_access_to(player->get_player_nr()) && player->allows_access_to(depot->get_owner()->get_player_nr()))) && depot->get_waytype() == schedule->get_waytype()) {
			for( auto const veh : depot->get_vehicle_list() ) {
				const vehicle_desc_t *veh_type = veh->get_desc();
				// filter
				if( filter_catg!=255  &&  veh_type->get_freight_type()->get_catg_index() != filter_catg) {
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

	// These are required for restore
	uint8 player_nr;		// player that edits
	uint8 schedule_type;	// enum schedule_type
	uint8 selected_tab = tabs.get_active_tab_index();

	if (file->is_saving()) {
		player_nr = player->get_player_nr();
		schedule_type = schedule->get_type();
		save_order();
	}
	file->rdwr_byte(player_nr);
	file->rdwr_byte(schedule_type);
	file->rdwr_byte(selected_tab);

	file->rdwr_short(unique_entry_id);

	// schedules
	if (file->is_loading()) {
		player = welt->get_player(player_nr);
		switch (schedule_type) {
			case schedule_t::truck_schedule:
				schedule = new truck_schedule_t(); break;
			case schedule_t::train_schedule:
				schedule = new train_schedule_t(); break;
			case schedule_t::ship_schedule:
				schedule = new ship_schedule_t(); break;
			case schedule_t::airplane_schedule:
				schedule = new airplane_schedule_(); break;
			case schedule_t::monorail_schedule:
				schedule = new monorail_schedule_t(); break;
			case schedule_t::tram_schedule:
				schedule = new tram_schedule_t(); break;
			case schedule_t::maglev_schedule:
				schedule = new maglev_schedule_t(); break;
			case schedule_t::narrowgauge_schedule:
				schedule = new narrowgauge_schedule_t(); break;
			default:
				dbg->fatal("consist_order_frame_t::rdwr", "Cannot create default schedule!");
		}
	}
	schedule->rdwr(file);

	if (file->is_loading()) {
		// now we can open the window ...
		scr_coord const& pos = win_get_pos(this);
		schedule_t *save_schedule = schedule->copy();
		consist_order_frame_t *w = new consist_order_frame_t(player, save_schedule, save_schedule->entries[save_schedule->get_current_stop()].unique_entry_id);
		create_win(pos.x, pos.y, w, w_info, magic_consist_order_rdwr_dummy);
		w->set_windowsize(size);
		w->tabs.set_active_tab_index(selected_tab);
		player = NULL;
		delete schedule;
		destroy_win(this);
	}
}
