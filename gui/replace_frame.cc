/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "replace_frame.h"

#include "../simcolor.h"
#include "simwin.h"
#include "../simworld.h"
#include "../player/simplay.h"
#include "../player/finance.h" // NOTICE_INSUFFICIENT_FUNDS
#include "../simline.h"
#include "../simdepot.h"

#include "../dataobj/translator.h"
#include "../dataobj/replace_data.h"
#include "../utils/simstring.h"
#include "../vehicle/vehicle.h"

#include "../bauer/vehikelbauer.h"
#include "linelist_stats_t.h"
#include "messagebox.h"
#include "components/gui_convoi_button.h"
#include "convoi_detail_t.h"


static const char rpl_label_texts[3][64] =
{
	"rpl_cnv_replace",
	"rpl_cnv_sell",
	"rpl_cnv_skip"
};


static bool _is_electrified(const karte_t* welt, const convoihandle_t& cnv)
{
	vehicle_t *veh = cnv->get_vehicle(0);
	grund_t* gr = welt->lookup(veh->get_pos());
	weg_t* way = gr->get_weg(veh->get_waytype());
	return way ? way->is_electrified() : false;
}

replace_frame_t::replace_frame_t(convoihandle_t cnv) :
	gui_frame_t("", NULL),
	replace_mode(only_this_convoy), depot(false),
	state(state_replace), replaced_so_far(0),
	current_convoi(&current_convoi_pics),
	scrollx_convoi(&current_convoi, true, false),
	convoy_assembler(this),
	txt_line_replacing(&buf_line_help)
{
	this->cnv = cnv;
	target_line=linehandle_t();
	if( cnv.is_bound() ) {
		init();
	}
}

replace_frame_t::replace_frame_t(linehandle_t line) :
	gui_frame_t("", NULL),
	replace_mode(all_convoys_of_this_line), depot(false),
	state(state_replace), replaced_so_far(0),
	current_convoi(&current_convoi_pics),
	scrollx_convoi(&current_convoi, true, false),
	convoy_assembler(this),
	txt_line_replacing(&buf_line_help)
{
	target_line = line;
	cnv=convoihandle_t();
	if( line.is_bound() ) {
		init();
	}
}

void replace_frame_t::init()
{
	if( cnv.is_null()  &&  !target_line.is_bound()) return; // Reload measures

	set_title();
	if( target_line.is_bound() ){
		set_owner(target_line->get_owner());
		convoy_assembler.init(target_line->get_schedule()->get_waytype(), target_line->get_owner()->get_player_nr(), target_line->needs_electrification());
		convoy_assembler.set_vehicles(cnv);
		rpl = new replace_data_t();
	}
	else {
		set_owner(cnv->get_owner());
		convoy_assembler.init(cnv->front()->get_desc()->get_waytype(), cnv->get_owner()->get_player_nr(), _is_electrified(welt, cnv));
		rpl = cnv->get_replace() ? new replace_data_t(cnv->get_replace()) : new replace_data_t();
	}

	copy = false;
	init_traction_types();
	init_table();

	update_data();
	set_resizemode(diagonal_resize);
	reset_min_windowsize();
	set_windowsize(get_min_size());
	resize(scr_size(0, 0));
}

void replace_frame_t::set_title()
{
	if( target_line.is_bound() ){
		title_buf.printf("%s > %s", translator::translate("replace_line_convoys"), target_line->get_name());
	}
	else if( cnv.is_bound() ){
		title_buf.printf("%s > %s", translator::translate("Replace"), cnv->get_name());
	}
	else {
		return; // Reload measures
	}
	set_name(title_buf);
}

void replace_frame_t::init_traction_types()
{
	traction_types = 0;
	last_depot_count = depot_t::get_depot_list().get_count();
	const waytype_t wt = target_line.is_bound() ? target_line->get_schedule()->get_waytype() : cnv->front()->get_desc()->get_waytype();
	for (auto const depot : depot_t::get_depot_list()) {
		if (depot->get_owner()->get_player_nr() != get_player_nr()) continue;
		if (depot->get_waytype() != wt) continue;
		traction_types |= depot->get_traction_types();
	}
}

// Construct the framework of the entire of this UI.
// This is called only once from init() at initialization (if convoy exists).
// Subsequent updates should be performed for each component.
void replace_frame_t::init_table()
{
	set_table_layout(1,0);
	set_margin(scr_size(0,D_MARGIN_TOP), scr_size(0,0));

	linehandle_t line = cnv.is_bound() ? cnv->get_line() : target_line;

	// [LINK BUTTON]
	add_table(7,1);
	{
		new_component<gui_margin_t>(D_H_SPACE);
		if( cnv.is_bound() ) {
			new_component<gui_convoi_button_t>(cnv);
			new_component<gui_label_t>("Current convoy:");
			bt_details.init(button_t::roundbox_state, "Details");
			if (skinverwaltung_t::open_window) {
				bt_details.set_image(skinverwaltung_t::open_window->get_image_id(0));
				bt_details.set_image_position_right(true);
			}
			bt_details.add_listener(this);
			bt_details.set_tooltip("Open the convoy detail window");
			add_component(&bt_details);
		}
		if( line.is_bound() ) {
			new_component<gui_line_button_t>(line);
			new_component<gui_line_label_t>(line);
			if( !cnv.is_bound() ) {
				add_table(3,1);
				{
					add_component(&lb_vehicle_count);
					if (uint16 traction_type_count = line->get_traction_types()) {
						traction_type_count = (traction_type_count & 0x55555555) + (traction_type_count >> 1 & 0x55555555);
						traction_type_count = (traction_type_count & 0x33333333) + (traction_type_count >> 2 & 0x33333333);
						traction_type_count = (traction_type_count & 0x0f0f0f0f) + (traction_type_count >> 4 & 0x0f0f0f0f);
						traction_type_count = (traction_type_count & 0x00ff00ff) + (traction_type_count >> 8 & 0x00ff00ff);
						traction_type_count = (traction_type_count & 0x0000ffff) + (traction_type_count >> 16 & 0x0000ffff);
						if (traction_type_count>1) {
							new_component<gui_margin_t>(LINEASCENT);
							new_component<gui_label_with_symbol_t>(translator::translate("line_has_multiple_traction_types"), skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2):IMG_EMPTY)->set_tooltip(translator::translate("helptxt_warning_replacing_line_has_multiple_traction_types"));
						}
					}
				}
				end_table();
			}
		}
		new_component<gui_fill_t>();
	}
	end_table();

	// [CURRENT CONVOY]
	if( cnv.is_bound() ) {
		current_convoi.set_max_rows(1);
		scr_coord grid = convoy_assembler.get_grid(cnv->front()->get_waytype());
		grid.x = grid.x>>1;
		grid.y = (grid.y*3)>>2;
		current_convoi.set_grid(grid);
		//set_placement(scr_coord(placement.x - placement_dx, placement.y));
		current_convoi.set_player_nr( cnv->get_owner()->get_player_nr() );

		scrollx_convoi.set_maximize(true);
		scrollx_convoi.set_min_height(scrollx_convoi.get_max_size().h);
		add_component(&scrollx_convoi);
	}

	add_table(2,1)->set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(D_MARGIN_RIGHT, 0));
	{
		// left
		add_table(1,0);
		{
			set_vehicles(true);

			// [ACTION BUTTONS]
			add_table(5,1)->set_spacing(scr_size(0,0));
			{
				bt_autostart.init(button_t::roundbox | button_t::flexible, "Full replace");
				bt_autostart.set_tooltip("Send convoy to depot, replace and restart it automatically");
				bt_autostart.add_listener(this);
				add_component(&bt_autostart);

				bt_depot.init(button_t::roundbox | button_t::flexible, "Replace but stay");
				bt_depot.set_tooltip("Send convoy to depot, replace it and stay there");
				bt_depot.add_listener(this);
				add_component(&bt_depot);

				bt_mark.init(button_t::roundbox | button_t::flexible, "Mark for replacing");
				bt_mark.set_tooltip("Mark for replacing. The convoy will replace when manually sent to depot");
				bt_mark.add_listener(this);
				add_component(&bt_mark);

				new_component<gui_margin_t>(D_H_SPACE);

				if( cnv.is_bound() ) {
					// "replace all" and "replace all in line" are confusing, but are left for backward compatibility of the translation files.
					cb_replace_target.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Only this convoy"), SYSCOL_TEXT);
					cb_replace_target.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("replace all"), SYSCOL_TEXT);
					if( line.is_bound() ) {
						cb_replace_target.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("replace all in line"), SYSCOL_TEXT);
					}
					cb_replace_target.add_listener(this);

					cb_replace_target.set_selection(replace_mode);
					add_component(&cb_replace_target);
				}
			}
			end_table();

			// [OPTION BUTTONS]
			add_table(3,1);
			{
				bt_retain_in_depot.init(button_t::square_state, "Retain in depot");
				bt_retain_in_depot.set_tooltip("Keep replaced vehicles in the depot for future use rather than selling or upgrading them.");
				bt_retain_in_depot.pressed = rpl->get_retain_in_depot();
				bt_retain_in_depot.add_listener(this);
				add_component(&bt_retain_in_depot);

				bt_use_home_depot.init(button_t::square_state, "Use home depot");
				bt_use_home_depot.set_tooltip("Send the convoy to its home depot for replacing rather than the nearest depot.");
				bt_use_home_depot.pressed = rpl->get_use_home_depot();
				bt_use_home_depot.add_listener(this);
				add_component(&bt_use_home_depot);

				bt_allow_using_existing_vehicles.init(button_t::square_state, "Use existing vehicles");
				bt_allow_using_existing_vehicles.set_tooltip("Use any vehicles already present in the depot, if available, instead of buying new ones or upgrading.");
				bt_allow_using_existing_vehicles.pressed = rpl->get_allow_using_existing_vehicles();
				bt_allow_using_existing_vehicles.add_listener(this);
				add_component(&bt_allow_using_existing_vehicles);
			}
			end_table();
		}
		end_table();

		// right
		add_table(3,0)->set_spacing(scr_size(0,0));
		{
			new_component_span<gui_label_t>("Replace cycle:",3);
			// | label | input | number(text) |
			for( uint8 i=0; i<n_states; ++i ) {
				numinp[i].set_value(i==0 ? 1:0); // init value first
				if( target_line.is_bound() && i>0 ) continue;
				lb_inp[i].init(rpl_label_texts[i]);
				add_component(&lb_inp[i]);
				numinp[i].set_increment_mode(1);
				numinp[i].set_limits(0, 999);
				numinp[i].add_listener(this);
				add_component(&numinp[i]);
				lb_text[i].set_fixed_width(proportional_string_width("888"));
				add_component(&lb_text[i]);
			}
		}
		end_table();
	}
	end_table();


	new_component<gui_border_t>();
	add_table(5,1)->set_margin(scr_size(D_MARGIN_LEFT,0), scr_size(D_MARGIN_RIGHT, 0));
	{
		new_component<gui_heading_t>("To be replaced by:",
			SYSCOL_TEXT, get_titlecolor(), 2)->set_width(proportional_string_width(translator::translate("To be replaced by:")) + D_HEADING_HEIGHT*2);
		lb_money.init(SYSCOL_TEXT, gui_label_t::money_right);
		lb_money.set_fixed_width(D_LABEL_WIDTH);
		add_component(&lb_money);

		if( cnv.is_bound() ) {
			bt_reset.init(button_t::roundbox, "reset", scr_coord(0,0), D_BUTTON_SIZE);
			bt_reset.set_tooltip("Reset this replacing operation");
			bt_reset.add_listener(this);
			add_component(&bt_reset);
		}

		bt_clear.init(button_t::roundbox, "Clear", scr_coord(0,0), D_BUTTON_SIZE);
		bt_clear.set_tooltip("Clear this replacing operation");
		bt_clear.add_listener(this);
		add_component(&bt_clear);

		new_component<gui_fill_t>();
	}
	end_table();

	// [ASSEMBLER]
	add_component(&convoy_assembler);
}

void replace_frame_t::set_vehicles(bool init)
{
	uint8 vehicle_count=0;
	array_tpl<vehicle_t*> veh_tmp_list; // To restore the order of vehicles that are reversing
	if (cnv.is_bound()) {
		vehicle_count = cnv->get_vehicle_count();
		veh_tmp_list.resize(vehicle_count, NULL);
		for (uint8 i = 0; i < vehicle_count; i++) {
			vehicle_t* dummy_veh = vehicle_builder_t::build(koord3d(), world()->get_public_player(), NULL, cnv->get_vehicle(i)->get_desc());
			dummy_veh->set_flag(obj_t::not_on_map);
			dummy_veh->set_current_livery(cnv->get_vehicle(i)->get_current_livery());
			dummy_veh->set_reversed(cnv->get_vehicle(i)->is_reversed());
			veh_tmp_list[i] = dummy_veh;
		}
		// If convoy is reversed, reorder it
		if( cnv->is_reversed() ) {
			// reverse_order
			bool reversable = convoi_t::get_terminal_shunt_mode(veh_tmp_list, vehicle_count) == convoi_t::change_direction ? true : false;
			convoi_t::execute_reverse_order(veh_tmp_list, vehicle_count, reversable);
		}
	}

	if (init  &&  cnv.is_bound() ) {
		const uint16 month_now = world()->get_timeline_year_month();
		// TODO: reversing?
		for(  uint8 i=0;  i < vehicle_count;  i++  ) {
			const vehicle_desc_t *veh_type = veh_tmp_list[i]->get_desc();
			gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t( veh_type->get_name(), veh_tmp_list[i]->get_base_image() );
			current_convoi_pics.append(img_data);
			// set color bar
			PIXVAL base_col = (!veh_type->is_future(month_now) && !veh_type->is_retired(month_now)) ? COL_SAFETY :
				(veh_type->is_obsolete(month_now)) ? SYSCOL_OBSOLETE : SYSCOL_OUT_OF_PRODUCTION;

			// change green into blue for retired vehicles
			if (i!=0) {
				const vehicle_desc_t *prev_veh_type = veh_tmp_list[i-1]->get_desc();
				current_convoi_pics[i-1]->rcolor = prev_veh_type->can_lead(veh_type)   ? base_col : COL_DANGER;
				current_convoi_pics[i]->lcolor   = veh_type->can_follow(prev_veh_type) ? base_col : COL_DANGER;
			}
			else {
				current_convoi_pics[0]->lcolor = veh_type->can_follow(NULL) ? base_col : COL_CAUTION;
			}
			if( i == vehicle_count-1 ) {
				current_convoi_pics[i]->rcolor = veh_type->can_lead(NULL) ? base_col : COL_CAUTION;
			}

			current_convoi_pics[i]->basic_coupling_constraint_prev = veh_type->get_basic_constraint_prev();
			current_convoi_pics[i]->basic_coupling_constraint_next = veh_type->get_basic_constraint_next();
			current_convoi_pics[i]->interactivity = veh_type->get_interactivity();
			// has upgrade
			if (veh_type->has_available_upgrade(month_now)) {
				current_convoi_pics[i]->has_upgrade = 2;
			}
		}

		add_table(1,0)->set_margin(scr_size(0,0), scr_size(0,0));
		{
			add_table(3,1);
			{
				add_component(&lb_vehicle_count);
				add_component(&lb_station_tiles);

				gui_tile_occupancybar_t *tile_bar = new_component<gui_tile_occupancybar_t>();
				tile_bar->set_base_convoy_length(cnv->get_length(), veh_tmp_list[vehicle_count-1]->get_desc()->get_length());
			}
			end_table();
		}
		end_table();
	}

	if( init  &&  target_line.is_bound() ) {
		// line replacing help text
		buf_line_help.printf(translator::translate("multiline_helptext_replace_all_line_convoys"));
		txt_line_replacing.recalc_size();
		add_component(&txt_line_replacing);
	}

	// update assembler
	if(cnv.is_bound() && cnv->get_replace())
	{
		cnv->get_replace()->check_contained(cnv);
		convoy_assembler.set_vehicles(cnv->get_replace()->get_replacing_vehicles());
	}
	else
	{
		vector_tpl<const vehicle_desc_t*> *existing_vehicles = new vector_tpl<const vehicle_desc_t*>();
		for(uint8 i = 0; i < vehicle_count; i ++)
		{
			existing_vehicles->append(veh_tmp_list[i]->get_desc());
		}
		convoy_assembler.set_vehicles(existing_vehicles);
	}
	for (uint8 i = vehicle_count; i-- != 0; ) {
		world()->get_public_player()->book_vehicle_number(-1, cnv->front()->get_desc()->get_waytype());
		world()->get_public_player()->book_new_vehicle(veh_tmp_list[i]->get_desc()->get_value(), koord::invalid, cnv->front()->get_desc()->get_waytype());
		delete veh_tmp_list[i];
	}
	veh_tmp_list.clear();
}

void replace_frame_t::update_data()
{
	uint32 n[3];
	n[0] = 0;
	n[1] = 0;
	n[2] = 0;
	money = 0;
	const sint64 base_total_cost = cnv.is_bound() ? calc_total_cost(cnv):0;
	if (replace_mode != only_this_convoy) {
		start_replacing();
	} else {
		money -= base_total_cost;
	}

	switch (replace_mode)
	{
		case only_this_convoy:
		{
			// none
			break;
		}
		case same_consist_for_player:
		{
			for (uint32 i=0; i<welt->convoys().get_count(); i++)
			{
				convoihandle_t cnv_aux=welt->convoys()[i];
				if (!cnv.is_bound() || cnv_aux->get_owner() != cnv->get_owner() || !cnv->has_same_vehicles(cnv_aux)) continue;

				uint8 present_state=get_present_state();
				if (present_state==(uint8)(-1)) continue;

				money -= base_total_cost;
				n[present_state]++;
			}
			break;
		}
		case same_consist_for_this_line:
		{
			linehandle_t line=cnv.is_bound()?cnv->get_line():linehandle_t();
			if (!line.is_bound()) break;

			for (uint32 i=0; i<line->count_convoys(); i++)
			{
				convoihandle_t cnv_aux=line->get_convoy(i);
				if (!cnv->has_same_vehicles(cnv_aux)) continue;

				uint8 present_state=get_present_state();
				if (present_state==(uint8)(-1)) continue;

				money -= base_total_cost;
				n[present_state]++;
			}
			break;
		}
		case all_convoys_of_this_line:
		{
			if (!target_line.is_bound()) break;
			uint16 total_vehicles=0;

			for (uint32 i=0; i<target_line->count_convoys(); i++)
			{
				uint8 present_state=get_present_state();
				if (present_state==(uint8)(-1)) continue;

				money -= calc_total_cost(target_line->get_convoy(i));
				n[present_state]++;
				total_vehicles += target_line->get_convoy(i)->get_vehicle_count();
			}

			// line convoy count
			lb_vehicle_count.buf().append(", ");
			if( target_line->count_convoys()==1 ) {
				lb_vehicle_count.buf().append( translator::translate( "1 convoi" ) );
			}
			else {
				lb_vehicle_count.buf().printf( translator::translate("%d convois") , target_line->count_convoys());
			}

			// line vehicle count
			lb_vehicle_count.buf().append(", ");
			if (total_vehicles == 1) {
				lb_vehicle_count.buf().append(translator::translate("1 vehicle"));
			}
			else {
				lb_vehicle_count.buf().printf(translator::translate("%d vehicles"), total_vehicles);
			}

			lb_vehicle_count.update();
			break;
		}
	}

	for (uint8 i = 0; i < n_states; ++i) {
		if (replace_mode != only_this_convoy) {
			numinp[i].enable();
			lb_inp[i].set_color(SYSCOL_TEXT);
			lb_text[i].buf().append(n[i],0);
		}
		else {
			numinp[i].disable();
			// Make replace cycle grey if not in use
			lb_inp[i].set_color(SYSCOL_BUTTON_TEXT_DISABLED);
		}
		lb_text[i].update();
	}

	if (cnv.is_bound()) {
		lb_vehicle_count.buf().printf("%s %u", translator::translate("Fahrzeuge:"), cnv->get_vehicle_count());
		lb_vehicle_count.update();

		lb_station_tiles.buf().printf("%s %u", translator::translate("Station tiles:"), cnv->get_tile_length());
		lb_station_tiles.update();
	}

	if (convoy_assembler.get_vehicles()->get_count()>0) {
		lb_money.append_money(money/100.0);
		lb_money.set_color(money>=0?MONEY_PLUS:MONEY_MINUS);
	}
	lb_money.update();
	reset_min_windowsize();
	if( get_size().w < get_min_size().w ) {
		set_windowsize(scr_size(get_min_size().w, get_size().h));
	}
	resize(scr_size(0,0));
}

void replace_frame_t::set_windowsize( scr_size size )
{
	convoy_assembler.set_panel_width();
	gui_frame_t::set_windowsize(size);
}

uint8 replace_frame_t::get_present_state()
{
	if (numinp[state_replace].get_value()==0 && numinp[state_sell].get_value()==0 && numinp[state_skip].get_value()==0) {
		return (uint8)(-1);
	}
	for (uint8 i=0; i<n_states; ++i) {
		if (replaced_so_far>=numinp[state].get_value()) {
			replaced_so_far=0;
			state=(state+1)%n_states;
		} else {
			break;
		}
	}
	replaced_so_far++;
	return state;
}


bool replace_frame_t::replace_convoy(convoihandle_t cnv_rpl, bool mark)
{
	uint8 state=get_present_state();
	if (!cnv_rpl.is_bound() || cnv_rpl->in_depot() || state==(uint8)(-1))
	{
		return false;
	}

	switch (state)
	{
	case state_replace:
	{
		if(convoy_assembler.get_vehicles()->get_count()==0)
		{
			break;
		}

		if(!welt->get_active_player()->can_afford(0 - money))
		{
			const char *err = NOTICE_INSUFFICIENT_FUNDS;
			news_img *box = new news_img(err);
			create_win(box, w_time_delete, magic_none);
			break;
		}

		if(!copy)
		{
			rpl->set_replacing_vehicles(convoy_assembler.get_vehicles());

			cbuffer_t buf;
			rpl->sprintf_replace(buf);

			cnv_rpl->call_convoi_tool('R', buf);
		}

		else
		{
			cbuffer_t buf;
			buf.append((long)master_convoy.get_id());
			cnv_rpl->call_convoi_tool('C', buf);
		}

		if(!mark && depot && !rpl->get_autostart())
		{
			cnv_rpl->call_convoi_tool('D', NULL);
		}

	}
		break;

	case state_sell:
		cnv_rpl->call_convoi_tool('T');
		break;
	case state_skip:
	break;
	}

	replaced_so_far++;
	return true;
}


bool replace_frame_t::action_triggered( gui_action_creator_t *comp,value_t /*p*/)
{
	if (welt->get_active_player()->get_player_nr() != get_player_nr()) {
		return false;
	}

	if( comp==&cb_replace_target ) {
		replace_mode = (replace_mode_t)cb_replace_target.get_selection();
		update_data();
	}
	else if (comp == &bt_retain_in_depot)
	{
		rpl->set_retain_in_depot(!rpl->get_retain_in_depot());
		bt_retain_in_depot.pressed = rpl->get_retain_in_depot();
	}
	else if (comp == &bt_use_home_depot)
	{
		rpl->set_use_home_depot(!rpl->get_use_home_depot());
		bt_use_home_depot.pressed = rpl->get_use_home_depot();
	}
	else if (comp == &bt_allow_using_existing_vehicles)
	{
		rpl->set_allow_using_existing_vehicles(!rpl->get_allow_using_existing_vehicles());
		bt_allow_using_existing_vehicles.pressed = rpl->get_allow_using_existing_vehicles();
	}
	else if (comp == &bt_clear)
	{
		if (cnv.is_bound()) {
			cnv->call_convoi_tool('X', NULL);
		}
		rpl = new replace_data_t();
		convoy_assembler.set_vehicles(convoihandle_t());
		bt_retain_in_depot.pressed = rpl->get_retain_in_depot();
		bt_use_home_depot.pressed = rpl->get_use_home_depot();
		bt_allow_using_existing_vehicles.pressed = rpl->get_allow_using_existing_vehicles();
		update_data();
	}
	else if (comp == &bt_reset)
	{
		set_vehicles();
	}

	if (comp == &bt_mark)
	{
		// HACK: This is intended to be temporary until the "mark for replacing" button can be replaced with
		// a more fitting UI element for these purposes that allows the setting of all the possible values of "replace at".
		rpl->set_replace_at(replace_data_t::manual);
	}
	else if(comp == &bt_autostart || comp == &bt_depot || comp == &bt_mark)
	{
		if( (replace_mode==same_consist_for_player || replace_mode==same_consist_for_this_line) ) {
			// check replace cycle input value
			if( !numinp[state_replace].get_value()  &&  !numinp[state_sell].get_value() ){
				create_win(new news_img("error_text_invalid_replace_cycle"), w_time_delete, magic_none);
				return false;
			}
		}
		depot=(comp==&bt_depot);
		rpl->set_autostart((comp==&bt_autostart));

		start_replacing();

		switch (replace_mode)
		{
			case only_this_convoy:
			{
				replace_convoy(cnv, comp == &bt_mark);
				break;
			}
			case same_consist_for_player:
			{
				bool first_success = false;
				for (uint32 i=0; i<welt->convoys().get_count(); i++)
				{
					convoihandle_t cnv_aux = welt->convoys()[i];
					if (!cnv_aux.is_bound() || cnv_aux->get_owner() != cnv->get_owner() || !cnv->has_same_vehicles(cnv_aux)) continue;

					first_success = replace_convoy(cnv_aux, comp == &bt_mark);
					if (!copy) master_convoy = cnv_aux;
					if (first_success) copy = true;
				}
				break;
			}
			case same_consist_for_this_line:
			{
				linehandle_t line = cnv.is_bound() ? cnv->get_line() : linehandle_t();
				if (!line.is_bound()) break;

				bool first_success = false;
				for (uint32 i = 0; i<line->count_convoys(); i++)
				{
					convoihandle_t cnv_aux = line->get_convoy(i);
					if (!cnv->has_same_vehicles(cnv_aux)) continue;

					first_success = replace_convoy(cnv_aux, comp == &bt_mark);
					if (!copy) master_convoy = cnv_aux;
					if (first_success) copy = true;
				}
				break;
			}
			case all_convoys_of_this_line:
			{
				if (!target_line.is_bound()) break;

				bool first_success = false;
				for (uint32 i = 0; i< target_line->count_convoys(); i++)
				{
					convoihandle_t cnv_aux = target_line->get_convoy(i);
					if (!cnv_aux.is_bound() || cnv_aux->get_owner() != target_line->get_owner()) continue;

					first_success = replace_convoy(cnv_aux, comp == &bt_mark);
					if (!copy) master_convoy = cnv_aux;
					if (first_success) copy = true;
				}
				break;
			}
		}

//#ifndef DEBUG
		//// FIXME: Oddly, this line causes crashes in 10.13 and over when
		//// the replace window is closed automatically with "full replace".
		//// The difficulty appears to relate to the objects comprising the
		//// window being destroyed before they have finished being used by
		//// the GUI system leading to access violations.
		destroy_win(this);
//#endif
		copy = false;
		update_data();
		return true;
	}
	else if (comp == &bt_details) {
		create_win({ 20, 20 }, new convoi_detail_t(cnv), w_info, magic_convoi_detail + cnv.get_id());
		return true;
	}

	if (replace_mode != only_this_convoy) {
		for (uint8 i = 0; i < n_states; ++i) {
			if( comp==&numinp[i] ) {
				update_data();
			}
		}
	}

	copy = false;
	return true;
}


uint8 replace_frame_t::get_player_nr() const
{
	return target_line.is_bound() ?	target_line->get_owner()->get_player_nr() : cnv->get_owner()->get_player_nr();
}


void replace_frame_t::draw(scr_coord pos, scr_size size)
{
	player_t *owner = target_line.is_bound() ? target_line->get_owner() : cnv->get_owner();
	if (welt->get_active_player() != owner) {
		destroy_win(this);
		return;
	}

	if (last_depot_count != depot_t::get_depot_list().get_count()) {
		// update the assembler
		init_traction_types();
		convoy_assembler.build_vehicle_lists();
	}

	if (cnv.is_bound()) bt_details.pressed = win_get_magic(magic_convoi_detail + cnv.get_id());

	if (convoy_assembler.get_min_size().w>get_min_size().w) {
		reset_min_windowsize();
		resize(scr_size(0,0));
	}
	gui_frame_t::draw(pos, size);
}

sint64 replace_frame_t::calc_total_cost(convoihandle_t current_cnv)
{
	sint64 total_cost = 0;
	vector_tpl<const vehicle_t*> current_vehicles;
	vector_tpl<uint16> keep_vehicles;
	for(uint8 i = 0; i < current_cnv->get_vehicle_count(); i ++)
	{
		current_vehicles.append(current_cnv->get_vehicle(i));
	}

	for(auto vehicle : *convoy_assembler.get_vehicles())
	{
		const vehicle_desc_t* veh = NULL;
		// First - check whether there are any of the required vehicles already
		// in the convoy (free)
		uint32 k = 0u;
		for(auto current_vehicle : current_vehicles)
		{
			if(!keep_vehicles.is_contained(k) && current_vehicle->get_desc() == vehicle)
			{
				veh = current_vehicle->get_desc();
				keep_vehicles.append_unique(k);
				// No change to price here.
				break;
			}
			k++;
		}

		// We cannot look up the home depot here, so we cannot check whether there are any
		// suitable vehicles stored there as is done when the actual replacing takes place.

		if (veh == NULL)
		{
			// Second - check whether the vehicle can be upgraded (cheap).
			// But only if the user does not want to keep the vehicles for
			// something else.
			if(!rpl->get_retain_in_depot())
			{
				uint32 l = 0u;
				for(auto current_vehicle : current_vehicles)
				{
					for(uint8 c = 0; c < current_vehicle->get_desc()->get_upgrades_count(); c ++)
					{
						if(!keep_vehicles.is_contained(l) && (vehicle == current_vehicle->get_desc()->get_upgrades(c)))
						{
							veh = current_vehicle->get_desc();
							keep_vehicles.append_unique(l);
							total_cost += veh ? veh->get_upgrades(c)->get_upgrade_price() : 0;
							goto end_loop;
						}
					}
					l ++;
				}
			}
end_loop:
			if(veh == NULL)
			{
				// Third - if all else fails, buy from new (expensive).
				total_cost += vehicle->get_value();
			}
		}
	}
	uint32 m = 0;
	for (auto current_vehicle : current_vehicles)
	{
		if(!keep_vehicles.is_contained(m))
		{
			// This vehicle will not be kept after replacing -
			// deduct its resale value from the total cost.
			total_cost -= current_vehicle->calc_sale_value();
		}
		m++;
	}

	return total_cost;
}

replace_frame_t::~replace_frame_t()
{
	clear_ptr_vector(current_convoi_pics);

	// TODO: Find why this causes crashes. Without it, there is a small memory leak.
	//delete rpl;
}

uint32 replace_frame_t::get_rdwr_id()
{
	return target_line.is_bound() ? magic_replace_line + target_line.get_id() : magic_replace + cnv.get_id();
}

void replace_frame_t::rdwr(loadsave_t *file)
{
	// convoy data
	convoi_t::rdwr_convoihandle_t(file, cnv);

	// window size
	scr_size size = get_windowsize();
	size.rdwr(file);

	// Prefer edited content over rpl, so temporarily memorize it here and restore it later.
	bool retain_in_depot = bt_retain_in_depot.pressed;
	file->rdwr_bool(retain_in_depot);
	bool use_home_depot = bt_use_home_depot.pressed;
	file->rdwr_bool(use_home_depot);
	bool use_existing_vehicles = bt_allow_using_existing_vehicles.pressed;
	file->rdwr_bool(use_existing_vehicles);
	sint32 selectet_target = (sint32)replace_mode; // sint32 for save backward compatibility
	file->rdwr_long(selectet_target);
	sint32 num_temp[n_states];
	for (uint8 i = 0; i < n_states; ++i) {
		num_temp[i] = numinp[i].get_value();
		file->rdwr_long(num_temp[i]);
	}

	simline_t::rdwr_linehandle_t(file, target_line);

	// init window
	if(  file->is_loading()  ) {
		if (replace_mode == all_convoys_of_this_line) {
			set_line(target_line);
			replace_mode = all_convoys_of_this_line;
		}
		else if (cnv.is_bound()) {
			set_convoy(cnv);
			cb_replace_target.set_selection(selectet_target);
			replace_mode = (replace_mode_t)selectet_target;
		}
		set_windowsize(size);

		// Overwrite with changes
		rpl->set_retain_in_depot(retain_in_depot);
		rpl->set_use_home_depot(use_home_depot);
		rpl->set_allow_using_existing_vehicles(use_existing_vehicles);
		bt_retain_in_depot.pressed = retain_in_depot;
		bt_use_home_depot.pressed  = use_home_depot;
		bt_allow_using_existing_vehicles.pressed = use_existing_vehicles;

		for (uint8 i = 0; i < n_states; ++i) {
			numinp[i].set_value(num_temp[i]);
		}
		update_data();
	}

	// convoy vanished
	if (!cnv.is_bound() && !target_line.is_bound()) {
		if(!cnv.is_bound())         dbg->error("replace_frame_t::rdwr()", "Could not restore replace window of convoy(%d)", cnv.get_id());
		if(!target_line.is_bound()) dbg->error("replace_frame_t::rdwr()", "Could not restore replace window of line(%d)",   target_line.get_id());
		destroy_win(this);
		return;
	}
}
