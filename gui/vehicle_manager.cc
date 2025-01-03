/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "vehicle_manager.h"
#include "vehicle_detail.h"

#include "../simworld.h"
#include "../simmenu.h"
#include "../convoy.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../display/viewport.h"
#include "../descriptor/vehicle_desc.h"
#include "../descriptor/intro_dates.h"
#include "../utils/simstring.h"
#include "components/gui_colorbox.h"
#include "components/gui_divider.h"
#include "components/gui_waytype_image_box.h"

#include "vehiclelist_frame.h"


static const char* const vl_header_text[vehicle_row_t::MAX_COLS] =
{
	"Name", "", "Wert", "engine_type",
	"Leistung", "TF_", "Max. speed", "", "Capacity",
	"curb_weight", "Axle load:", "Built date", "Intro. date"
};


vehicle_manager_t::vehicle_manager_t(depot_t* dep) :
	gui_frame_t(""),
	scrolly(gui_scrolled_list_t::windowskin, vehicle_row_t::compare),
	scroll_sortable(&cont_sortable, true, true)
{
	old_selections = 0;
	old_vhicles    = 0;

	if (dep) {
		depot=dep;
		gui_frame_t::set_name(translator::translate("vehicle_management"));
		gui_frame_t::set_owner(depot->get_owner());
		set_table_layout(1, 0);
		init_table();
	}
}

void vehicle_manager_t::reset_depot_name()
{
	tstrncpy(old_name, depot->get_name(), sizeof(old_name));
	tstrncpy(name, depot->get_name(), sizeof(name));
	set_name(translator::translate(depot->get_name()));
}

void vehicle_manager_t::update_list()
{
	old_month = world()->get_current_month();
	scrolly.clear_elements();

	old_listed_vehicles=world()->get_listed_vehicle_number(depot->get_waytype());
	if (dep_action==da_buy) {
		if (old_listed_vehicles-depot->get_vehicles_for_sale().get_count()) {
			player_t *player=depot->get_owner();
			for (depot_t* const other_depot : depot_t::get_depot_list()) {
				// filter
				if (player == other_depot->get_owner()) continue;
				if (depot->get_waytype() != other_depot->get_waytype()) continue;

				for (auto vehicle : other_depot->get_vehicles_for_sale()) {
					// Check if this depot can accept the vehicle
					if( !depot->is_suitable_for( vehicle, vehicle->get_desc()->get_traction_type(), true ) ) continue;
					scrolly.new_component<vehicle_row_t>(vehicle);
				}
			}
		}
	}
	else {
		for (auto vehicle : dep_action==da_cancel_listing ? depot->get_vehicles_for_sale() : depot->get_vehicle_list()){
			scrolly.new_component<vehicle_row_t>(vehicle);
		}
	}
	scrolly.set_selection(-1);
	scrolly.sort(0);
	// recalc stats table width
	scr_coord_val max_widths[vehicle_row_t::MAX_COLS]={};

	// check column widths
	table_header.set_selection(vehicle_row_t::sort_mode);
	table_header.set_width(D_H_SPACE); // init width
	for (uint c = 0; c < vehicle_row_t::MAX_COLS; c++) {
		// get header widths
		max_widths[c] = table_header.get_min_width(c);
	}
	for (int r = 0; r < scrolly.get_count(); r++) {
		vehicle_row_t* row = (vehicle_row_t*)scrolly.get_element(r);
		for (uint c=0; c<vehicle_row_t::MAX_COLS; c++) {
			max_widths[c]= max(max_widths[c],row->get_min_width(c));
		}
	}

	// set column widths
	for (uint c = 0; c < vehicle_row_t::MAX_COLS; c++) {
		table_header.set_col(c, max_widths[c]);
	}
	table_header.set_width(table_header.get_size().w+D_SCROLLBAR_WIDTH);

	for (int r = 0; r < scrolly.get_count(); r++) {
		vehicle_row_t* row = (vehicle_row_t*)scrolly.get_element(r);
		row->set_size(scr_size(0, row->get_size().h));
		for (uint c = 0; c < vehicle_row_t::MAX_COLS; c++) {
			row->set_col(c, max_widths[c]);
		}
	}

	old_vhicles = depot->get_vehicle_list().get_count();
	lb_vehicle_count.buf().clear();
	if (old_vhicles == 0) {
		lb_vehicle_count.buf().append( translator::translate( "Keine Einzelfahrzeuge im Depot" ) );
	}
	else {
		lb_vehicle_count.buf().append(translator::translate("Stored"));
		lb_vehicle_count.buf().append(": ");
		if (old_vhicles == 1) {
			lb_vehicle_count.buf().append(translator::translate("1 vehicle"));
		}
		else {
			lb_vehicle_count.buf().printf(translator::translate("%d vehicles"), old_vhicles);
		}
	}
	uint32 for_sale_cnt = depot->get_vehicles_for_sale().get_count();
	if (for_sale_cnt) {
		if (old_vhicles) {
			lb_vehicle_count.buf().append(", ");
		}
		lb_vehicle_count.buf().append(translator::translate("Listed"));
		lb_vehicle_count.buf().append(": ");
		if (for_sale_cnt == 1) {
			lb_vehicle_count.buf().append(translator::translate("1 vehicle"));
		}
		else {
			lb_vehicle_count.buf().printf(translator::translate("%d vehicles"), for_sale_cnt);
		}
	}
	lb_vehicle_count.update();
	resize(scr_size(0, 0));

	bt_mode_scrap.enable(old_vhicles);
	bt_mode_sell.enable(old_vhicles);
	bt_mode_cancel_listing.enable(for_sale_cnt);
	bt_mode_buy.enable( old_listed_vehicles - for_sale_cnt );

	scroll_sortable.set_visible(scrolly.get_count());

	calc_total_cost();
}

void vehicle_manager_t::init_table()
{
	if (depot == NULL) {
		destroy_win(this);
	}

	reset_depot_name();

	scrolly.add_listener(this);
	scrolly.set_show_scroll_x(false);
	scrolly.set_multiple_selection(2);
	scrolly.set_checkered(true);
	scrolly.set_scroll_amount_y(LINESPACE + D_H_SPACE +2); // default cell height

	scroll_sortable.set_rigid(true);
	scroll_sortable.set_maximize(true);

	table_header.add_listener(this);
	for (uint8 col=0; col<vehicle_row_t::MAX_COLS; col++) {
		table_header.new_component<sortable_header_cell_t>(vl_header_text[col]);
	}

	cont_sortable.set_margin(NO_SPACING, NO_SPACING);
	cont_sortable.set_spacing(NO_SPACING);
	cont_sortable.set_table_layout(1,2);
	cont_sortable.set_alignment(ALIGN_TOP); // Without this, the layout will be broken if the height is small.
	cont_sortable.add_component(&table_header);
	cont_sortable.add_component(&scrolly);

	add_table(3,1);
	{
		new_component<gui_waytype_image_box_t>(depot->get_waytype());
		name_input.set_text(name, sizeof(name));
		name_input.add_listener(this);
		add_component(&name_input);

		gui_label_buf_t *lb = new_component<gui_label_buf_t>();
		if (depot->get_traction_types() == 0)
		{
			lb->buf().printf("%s", translator::translate("Unpowered vehicles only"));
		}
		else if (depot->get_traction_types() == 65535)
		{
			lb->buf().printf("%s", translator::translate("All traction types"));
		}
		else
		{
			uint16 shifter;
			bool first = true;
			for (uint16 i = 0; i < (vehicle_desc_t::MAX_TRACTION_TYPE); i++)
			{
				shifter = 1 << i;
				if ((shifter & depot->get_traction_types()))
				{
					if (first)
					{
						first = false;
						lb->buf().clear();
					}
					else
					{
						lb->buf().printf(", ");
					}
					lb->buf().printf("%s", translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)(i + 1)]));
				}
			}
		}
	}
	end_table();

	add_table(2, 1);
	{
		add_component(&lb_vehicle_count);
		new_component<gui_fill_t>();
	}
	end_table();

	add_table(8,1)->set_spacing(NO_SPACING);
	{
		bt_mode_scrap.init(button_t::roundbox_left_state, translator::translate("Scrap"));
		bt_mode_scrap.pressed=true;
		bt_mode_scrap.add_listener(this);
		add_component(&bt_mode_scrap);
		bt_mode_sell.init(button_t::roundbox_middle_state, translator::translate("Sell"));
		bt_mode_sell.add_listener(this);
		add_component(&bt_mode_sell);
		bt_mode_cancel_listing.init(button_t::roundbox_middle_state, translator::translate("Listed"));
		bt_mode_cancel_listing.add_listener(this);
		add_component(&bt_mode_cancel_listing);
		bt_mode_buy.init(button_t::roundbox_right_state, translator::translate("Buy"));
		bt_mode_buy.add_listener(this);
		add_component(&bt_mode_buy);

		new_component<gui_margin_t>(LINESPACE);
		bt_sel_all.init(button_t::roundbox, translator::translate("clf_btn_alle"));
		bt_sel_all.add_listener(this);
		add_component(&bt_sel_all);
		bt_reset.init(button_t::roundbox, translator::translate("clf_btn_keine"));
		bt_reset.add_listener(this);
		add_component(&bt_reset);
		new_component<gui_fill_t>();
	}
	end_table();

	add_component(&scroll_sortable);
	update_list();
	add_component(&lb_total_cost);
	bt_execute.init(button_t::roundbox, "execute_scrap");
	bt_execute.add_listener(this);
	add_component(&bt_execute);

	set_resizemode(diagonal_resize);
	reset_min_windowsize();
	resize(scr_size(0,0));
}

void vehicle_manager_t::calc_total_cost()
{
	last_selected = scrolly.get_selection(); // current focused one
	old_selections = scrolly.get_selections().get_count(); // selected vehicle count
	if ( bt_mode_cancel_listing.pressed ){
		lb_total_cost.buf().append(translator::translate("Cancel the listing request for the selected vehicles"));
	}
	else {
		sint64 total=0;
		for (auto i : scrolly.get_selections()) {
			vehicle_row_t* vr = (vehicle_row_t*)scrolly.get_element(i);
			vehicle_t* veh = vr->get_vehicle();
			total += (sint64)veh->calc_sale_value();
		}

		char number[64];
		money_to_string(number, total / 100.0);
		if (bt_mode_buy.pressed) {
			lb_total_cost.buf().printf("%s: ", translator::translate("Price"));
		}
		else {
			lb_total_cost.buf().printf("%s ", translator::translate("Restwert:"));
		}
		lb_total_cost.buf().append(number);
	}
	lb_total_cost.update();
	lb_total_cost.set_size(lb_total_cost.get_min_size()); // When the width is increased, the text may be cut off.

	bt_execute.enable(old_selections);
}

void vehicle_manager_t::scrap_vehicles()
{
	// standard's code only allows players to sell the newest vehicle,
	// but this UI requires players to sell the specific vehicle selected precisely.
	uint16 stored_idx= (uint16)depot->get_vehicle_list().get_count();
	// The process is done backwards so that the vehicle index does not change.
	for (uint16 stored_idx = depot->get_vehicle_list().get_count(); stored_idx>0; stored_idx--) {
		vehicle_t *veh = depot->get_vehicle_list().at(stored_idx-1);
		for (auto i : scrolly.get_selections()) {
			vehicle_row_t* vi = (vehicle_row_t*)scrolly.get_element(i);
			if (vi->get_vehicle() && vi->get_vehicle() == veh) {
				depot->call_depot_tool('S', convoihandle_t(), vi->get_text(), stored_idx-1);
				break; // check next
			}
		}
	}
}

void vehicle_manager_t::offer_vehicles_for_sale()
{
	uint16 stored_idx = (uint16)depot->get_vehicle_list().get_count();
	// The process is done backwards so that the vehicle index does not change.
	for (uint16 stored_idx = depot->get_vehicle_list().get_count(); stored_idx>0; stored_idx--) {
		vehicle_t *veh = depot->get_vehicle_list().at(stored_idx-1);
		for (auto i : scrolly.get_selections()) {
			vehicle_row_t* vi = (vehicle_row_t*)scrolly.get_element(i);
			if (vi->get_vehicle() && vi->get_vehicle() == veh) {
				depot->call_depot_tool('f', convoihandle_t(), vi->get_text(), stored_idx-1);
				break; // check next
			}
		}
	}
}

void vehicle_manager_t::cancel_vehicles_for_sale()
{
	// The process is done backwards so that the vehicle index does not change.
	for (uint16 listed_idx = depot->get_vehicles_for_sale().get_count(); listed_idx >0; listed_idx--) {
		vehicle_t *veh = depot->get_vehicles_for_sale().at(listed_idx-1);
		for (auto i : scrolly.get_selections()) {
			vehicle_row_t* vi = (vehicle_row_t*)scrolly.get_element(i);
			if (vi->get_vehicle() && vi->get_vehicle() == veh) {
				depot->call_depot_tool('F', convoihandle_t(), vi->get_text(), listed_idx -1);
				break; // check next
			}
		}
	}
}

void vehicle_manager_t::takeover_vehicles()
{
	for (uint32 i=scrolly.get_selections().get_count(); i>0; i--) {
		vehicle_row_t* vinfo = (vehicle_row_t*)scrolly.get_element(scrolly.get_selections().get_element(i-1));
		vehicle_t *veh = vinfo->get_vehicle();

		if (grund_t* gr = welt->lookup(veh->get_pos())) {
			if (depot_t* seller_dep = gr->get_depot()) {
				uint32 j=0;
				for (auto vj : seller_dep->get_vehicles_for_sale()) {
					if (veh==vj) {
						tool_t* tmp_tool = create_tool(TOOL_TRANSFER_VEHICLE | SIMPLE_TOOL);
						cbuffer_t buf;
						buf.append(seller_dep->get_pos().get_str());
						buf.append(",");
						buf.append(depot->get_pos().get_str());
						buf.printf(",%u",j);
						tmp_tool->set_default_param(buf);
						welt->set_tool(tmp_tool, depot->get_owner());
						// since init always returns false, it is safe to delete immediately
						delete tmp_tool;
						break;
					}
					j++;
				}
			}
			else {
				dbg->warning("vehicle_manager_t::takeover_vehicles", "Depot not found at %s", veh->get_pos().get_fullstr());
			}
		}
		else {
			dbg->warning("vehicle_manager_t::takeover_vehicles", "Invalid coord - %s", veh->get_pos().get_fullstr());
		}
	}
}


void vehicle_manager_t::rename_depot()
{
	const char* t = name_input.get_text();
	// only change if old name and current name are the same
	// otherwise some unintended undo if renaming would occur
	if (t && t[0] && strcmp(t, depot->get_name()) && strcmp(old_name, depot->get_name()) == 0) {
		// text changed => call tool
		cbuffer_t buf;
		buf.printf("d%s,%s", depot->get_pos().get_str(), t);
		tool_t* tool = create_tool(TOOL_RENAME | SIMPLE_TOOL);
		tool->set_default_param(buf);
		welt->set_tool(tool, depot->get_owner());
		// since init always returns false, it is safe to delete immediately
		delete tool;
		// do not trigger this command again
		tstrncpy(old_name, depot->get_name(), sizeof(old_name));

		reset_depot_name();
	}
}


void vehicle_manager_t::draw(scr_coord pos, scr_size size)
{
	if (depot == NULL || depot->get_owner_nr() != world()->get_active_player_nr()) {
		destroy_win(this);
	}

	// update
	if( depot->get_vehicle_list().get_count()!=old_vhicles
		|| old_month != world()->get_current_month()
		|| old_listed_vehicles != world()->get_listed_vehicle_number(depot->get_waytype()) ) {
		update_list();
	}

	if( last_selected != scrolly.get_selection() || old_selections != scrolly.get_selections().get_count() ) {
		calc_total_cost();
	}

	gui_frame_t::draw(pos, size);
}

void vehicle_manager_t::init_mode_buttons()
{
	bt_mode_scrap.pressed = dep_action == da_scrap;
	bt_mode_sell.pressed = dep_action == da_listing;
	bt_mode_buy.pressed = dep_action == da_buy;
	bt_mode_cancel_listing.pressed = dep_action == da_cancel_listing;
	switch (dep_action)
	{
		default:
			bt_execute.set_text("execute");
			break;
		case da_scrap:
			bt_execute.set_text("execute_scrap");
			break;
		case da_listing:
			bt_execute.set_text("execute_listing");
			break;
		case da_cancel_listing:
			bt_execute.set_text("cancel_the_listing");
			cancel_vehicles_for_sale();
			break;
		case da_buy:
			bt_execute.set_text("execute_purchase");
			takeover_vehicles();
			break;
		}
}


bool vehicle_manager_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	if (depot->get_owner_nr() != world()->get_active_player_nr()) return false;

	if( comp==&bt_sel_all ||  comp == &bt_reset) {
		const bool select = comp==&bt_sel_all;
		for (int i=0; i<scrolly.get_count(); i++) {
			vehicle_row_t* vi = (vehicle_row_t*)scrolly.get_element(i);
			vi->selected = select;
		}
		scrolly.set_selection(-1);
	}
	bool need_update=false;
	if( comp == &bt_mode_scrap  ) {
		need_update = (dep_action!=da_scrap && dep_action!=da_listing);
		dep_action=da_scrap;
		init_mode_buttons();
	}
	if( comp == &bt_mode_sell  ) {
		need_update = (dep_action!=da_scrap && dep_action!=da_listing);
		dep_action=da_listing;
		init_mode_buttons();
	}
	if( comp == &bt_mode_cancel_listing ) {
		need_update= dep_action != da_cancel_listing;
		dep_action=da_cancel_listing;
		init_mode_buttons();
	}
	if( comp==&bt_mode_buy) {
		need_update = dep_action != da_buy;
		dep_action= da_buy;
		init_mode_buttons();
	}
	if (need_update) {
		update_list();
		return true;
	}
	if (comp == &table_header) {
		vehicle_row_t::sort_mode = v.i;
		table_header.set_selection(vehicle_row_t::sort_mode);
		vehicle_row_t::sortreverse = table_header.is_reversed();
		scrolly.sort(0);
	}
	if (comp == &bt_execute) {
		switch (dep_action)
		{
			default:
				break;
			case da_scrap:
				scrap_vehicles();
				break;
			case da_listing:
				offer_vehicles_for_sale();
				break;
			case da_cancel_listing:
				cancel_vehicles_for_sale();
				break;
			case da_buy:
				takeover_vehicles();
				break;
		}
	}
	else if (comp == &name_input) {
		// send rename command if necessary
		rename_depot();
	}

	return false;
}


// returns position of depot on the map
koord3d vehicle_manager_t::get_weltpos(bool)
{
	return depot->get_pos();
}


bool vehicle_manager_t::is_weltpos()
{
	return (world()->get_viewport()->is_on_center(get_weltpos(false)));
}
