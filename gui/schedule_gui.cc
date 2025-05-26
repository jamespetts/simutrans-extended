/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simline.h"
#include "../simcolor.h"
#include "../simdepot.h"
#include "../simhalt.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simconvoi.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

#include "../boden/grund.h"

#include "../obj/zeiger.h"

#include "../dataobj/schedule.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../player/simplay.h"
#include "../vehicle/vehicle.h"

#include "../tpl/vector_tpl.h"

#include "depot_frame.h"
#include "gui_theme.h"
#include "schedule_gui.h"
#include "line_item.h"

#include "components/gui_button.h"
#include "components/gui_textarea.h"
#include "components/gui_waytype_image_box.h"
#include "minimap.h"
#include "consist_order_gui.h"
#include "convoy_item.h""

static karte_ptr_t welt;


#define L_ENTRY_NO_HEIGHT (LINESPACE+4)
#define L_ENTRY_NO_WIDTH (proportional_string_width("88")+6)

// helper class
gui_wait_loading_schedule_t::gui_wait_loading_schedule_t(uint32 flags_, uint16 val_)
{
	val = val_;
	flags = flags_;
	size.h = L_ENTRY_NO_HEIGHT;
}

void gui_wait_loading_schedule_t::draw(scr_coord offset)
{
	const scr_coord_val img_width = skinverwaltung_t::goods->get_image(0)->get_pic()->w;
	scr_coord_val left = img_width+2;
	size.w = img_width + 8; // symbol_width + margin + border*2 + bar_width
	if (val || flags & schedule_entry_t::discharge_payload || flags & schedule_entry_t::pick_up_only || flags & schedule_entry_t::set_down_only) {
		display_color_img(skinverwaltung_t::goods->get_image_id(0), pos.x+offset.x, pos.y+offset.y + D_GET_CENTER_ALIGN_OFFSET(skinverwaltung_t::goods->get_image(0)->get_pic()->h, size.h), 0, false, false);
		if (val || flags & schedule_entry_t::discharge_payload) {
			const PIXVAL bgcolor = val ? color_idx_to_rgb(MN_GREY2) : color_idx_to_rgb(COL_RED+1);
			display_ddd_box_clip_rgb(   pos.x+offset.x+left,   pos.y+offset.y,   6, size.h,   color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4)); // frame
			display_fillbox_wh_clip_rgb(pos.x+offset.x+left+1, pos.y+offset.y+1, 4, size.h-2, bgcolor, true);                                          // background
			if (val) {
				const scr_coord_val bar_height = (size.h-2)*val/100;
				display_fillbox_wh_clip_rgb(pos.x+offset.x+left+1, pos.y+offset.y+(size.h-2)+1-bar_height, 4, bar_height, COL_IN_TRANSIT, true);       // load limit
			}
			left += 6;
		}
		if (flags & schedule_entry_t::pick_up_only) {
			left += display_fluctuation_triangle_rgb(pos.x+offset.x+left+1, pos.y+offset.y+D_GET_CENTER_ALIGN_OFFSET(7, size.h), 7, false, 1);
		}
		else if(flags & schedule_entry_t::set_down_only){
			left += display_fluctuation_triangle_rgb(pos.x+offset.x+left+1, pos.y+offset.y+D_GET_CENTER_ALIGN_OFFSET(7, size.h), 7, false, -1);
		}
		size.w = left+1;
		set_size(size);
	}
}


#define L_COUPLE_ORDER_LABEL_WIDTH (proportional_string_width("88")+D_H_SPACE)
gui_schedule_couple_order_t::gui_schedule_couple_order_t(uint16 leave_, uint16 join_)
{
	leave = leave_;
	join = join_;
	size.h = L_ENTRY_NO_HEIGHT;
	lb_leave.init(color_idx_to_rgb(COL_WHITE), gui_label_t::centered);
	lb_leave.set_pos(scr_coord(0, 2));
	lb_leave.set_size(scr_size(L_COUPLE_ORDER_LABEL_WIDTH, D_LABEL_HEIGHT));
	lb_join.init(color_idx_to_rgb(COL_WHITE), gui_label_t::centered);
	lb_join.set_size(scr_size(L_COUPLE_ORDER_LABEL_WIDTH, D_LABEL_HEIGHT));
	lb_leave.set_fixed_width(L_COUPLE_ORDER_LABEL_WIDTH);
	lb_join.set_fixed_width(L_COUPLE_ORDER_LABEL_WIDTH);
	add_component(&lb_leave);
	add_component(&lb_join);
}

void gui_schedule_couple_order_t::draw(scr_coord offset)
{
	scr_coord_val total_x = 0;
	if (leave || join) {
		lb_leave.set_visible(leave);
		lb_join.set_visible(join);
		if (leave) {
			display_veh_form_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y+2, L_COUPLE_ORDER_LABEL_WIDTH + LINESPACE/2, LINEASCENT+2, SYSCOL_DOWN_TRIANGLE, true, true, 3, HAS_POWER|BIDIRECTIONAL);
			lb_leave.buf().printf("%u", leave);
			total_x += L_COUPLE_ORDER_LABEL_WIDTH + LINESPACE/2;
		}
		if (join) {
			display_veh_form_wh_clip_rgb(pos.x+offset.x + total_x, pos.y+offset.y+2, L_COUPLE_ORDER_LABEL_WIDTH + LINESPACE/2, LINEASCENT+2, SYSCOL_UP_TRIANGLE, true, false, 3, HAS_POWER|BIDIRECTIONAL);
			lb_join.buf().printf("%u", join);
			lb_join.set_pos(scr_coord(total_x + LINESPACE/2, 2));
			total_x += L_COUPLE_ORDER_LABEL_WIDTH + LINESPACE/2;
		}
		lb_leave.update();
		lb_join.update();
		gui_container_t::draw(offset);
	}
	size.w = total_x;
}



gui_schedule_entry_t::gui_schedule_entry_t(player_t* pl, schedule_entry_t e, uint n, bool air_wt, uint8 line_color_index) :
	entry_no(n, pl->get_player_nr(), 0), wpbox(0,0,e.pos) // <-- mod here for EX15
{
	player = pl;
	entry  = e;
	number = n;
	is_current = false;
	is_air_wt = air_wt;
	set_table_layout(9,0);
	set_spacing(scr_size(1,0));

	bt_del.init(button_t::imagebox, NULL);
	bt_del.set_image(skinverwaltung_t::gadget->get_image_id(SKIN_GADGET_CLOSE));
	bt_del.set_size(gui_theme_t::gui_arrow_left_size);
	bt_del.background_color = color_idx_to_rgb(COL_RED);
	bt_del.add_listener(this);
	add_component(&bt_del);
	//new_component<gui_margin_t>(D_H_SPACE); // UI TODO: Use variables to make the right margins

	img_layover.set_image(skinverwaltung_t::layover ? skinverwaltung_t::layover->get_image_id(0) : IMG_EMPTY, true);
	img_layover.set_tooltip(translator::translate("if_this_is_set,_convoy_will_go_into_lay_over_state_at_this_stop"));
	img_layover.set_rigid(true); // false breaks the layout
	img_layover.set_visible(false);
	add_component(&img_layover); //1

	img_hourglass.set_image(skinverwaltung_t::waiting_time ? skinverwaltung_t::waiting_time->get_image_id(0) : IMG_EMPTY, true);
	img_hourglass.set_rigid(true);
	img_hourglass.set_visible(entry.is_flag_set(schedule_entry_t::wait_for_time) || (entry.minimum_loading > 0 && entry.waiting_time_shift > 0));
	add_component(&img_hourglass); //2

	wait_loading = new_component<gui_wait_loading_schedule_t>(entry.flags, entry.minimum_loading); // 3

	entry_no.set_visible(true);
	entry_no.set_rigid(false);
	wpbox.set_visible(false);
	wpbox.set_rigid(false);
	wpbox.set_flexible_height(true);
	add_table(2,1)->set_spacing(scr_size(0,0));
	{
		add_component(&entry_no);
		add_component(&wpbox);
	}
	end_table();

	add_table(7,1); //5
	{
		img_nc_alert.set_image(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(4) : IMG_EMPTY, true);
		img_nc_alert.set_tooltip(translator::translate("NO CONTROL TOWER"));
		img_nc_alert.set_visible(false);
		add_component(&img_nc_alert); // 5-1

		couple_order = new_component<gui_schedule_couple_order_t>(entry.condition_bitfield_receiver, entry.condition_bitfield_broadcaster); // 5-2
		//new_component<gui_margin_t>(1); //5-2 dummy for prefix // UI TODO

		img_refuel.set_image(skinverwaltung_t::refuel ? skinverwaltung_t::refuel->get_image_id(1) : IMG_EMPTY, true);
		img_refuel.set_tooltip(translator::translate("if_this_is_set,_this_stop_will_at_all_times_be_considered_a_range_stop"));
		img_refuel.set_rigid(false);
		img_refuel.set_visible(false);
		add_component(&img_refuel); //5-3
		add_component(&stop); // 5-4

		img_ignore_choose.set_image(skinverwaltung_t::ignore_choose ? skinverwaltung_t::ignore_choose->get_image_id(0) : skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(1) : IMG_EMPTY, true);
		img_ignore_choose.set_tooltip(translator::translate("If this is set, choose signals will be ignored while this convoy is heading to this destination."));
		img_ignore_choose.set_rigid(false);
		img_ignore_choose.set_visible(false);
		add_component(&img_ignore_choose); //5-5

		lb_reverse.set_visible(true);
		lb_reverse.buf().append("[<<]");
		lb_reverse.set_color(SYSCOL_TEXT_STRONG);
		lb_reverse.update();
		add_component(&lb_reverse); // 5-6

		new_component<gui_empty_t>(); // 5-7
	}
	end_table();

	new_component<gui_fill_t>(); // 7
	add_table(2,1)->set_spacing(NO_SPACING); //8
	{
		// pos button and label
		bt_pos.set_typ(button_t::posbutton_automatic);
		bt_pos.set_targetpos3d(entry.pos);
		add_component(&bt_pos); // 8-1
		lb_pos.buf().printf("(%s) ", entry.pos.get_str());
		lb_pos.set_tooltip(translator::translate("can_also_jump_to_the_coordinate_by_right-clicking_on_the_entry"));
		lb_pos.update();
		lb_pos.set_fixed_width(lb_pos.get_min_size().w);
		add_component(&lb_pos); // 8-2
	}
	end_table();
	new_component<gui_empty_t>(); // 9

	// 2nd row
	lb_distance.set_fixed_width(proportional_string_width("(0000km) "));
	lb_distance.set_align(gui_label_t::right);
	lb_distance.set_rigid(true);
	lb_distance.buf().append("");
	lb_distance.update();
	add_component(&lb_distance, 4); // 5

	PIXVAL base_color;
	switch (line_color_index) {
		case 254:
			base_color = color_idx_to_rgb(player->get_player_color1()+4);
			break;
		case 255:
			base_color = color_idx_to_rgb(player->get_player_color1()+2);
			break;
		default:
			base_color = line_color_idx_to_rgb(line_color_index);
			break;
	}
	route_bar = new_component<gui_colored_route_bar_t>(base_color, 0); // 6
	wpbox.set_color(base_color);
	route_bar->set_visible(true);

	lb_speed_limit.set_color(SYSCOL_TEXT_STRONG);
	add_component(&lb_speed_limit); // 7
	new_component<gui_fill_t>();  //8

	bt_swap.init(button_t::swap_vertical| button_t::automatic, NULL);
	bt_swap.set_tooltip("helptxt_swap_schedule_entries");
	bt_swap.add_listener(this);
	add_component(&bt_swap); // 9

	update_label();
}

void gui_schedule_entry_t::update_label()
{
	halthandle_t halt = haltestelle_t::get_halt(entry.pos, player);
	wait_loading->init_data(entry.flags, entry.minimum_loading);
	couple_order->set_value(entry.condition_bitfield_receiver, entry.condition_bitfield_broadcaster);

	bool no_control_tower = false; // This flag is left in case the pakset doesn't have alert symbols. UI TODO: Make this unnecessary
	wpbox.set_visible(false);
	if(welt->lookup(entry.pos) && welt->lookup(entry.pos)->get_depot() != NULL){
		// Depot check must come first, as depot and dock tiles can overlap at sea
		entry_no.set_number_style(gui_schedule_entry_number_t::number_style::depot);
		entry_no.set_color(player->get_player_color1());
	}
	else if (halt.is_bound()) {
		const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count())>1;
		entry_no.set_number_style(is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt);
		entry_no.set_color(halt->get_owner()->get_player_color1());

		if (is_air_wt) {
			img_nc_alert.set_visible(halt->has_no_control_tower());
			if (!halt->has_no_control_tower() && !skinverwaltung_t::alerts) {
				no_control_tower = true;
			}
		}
	}
	else {
		entry_no.set_number_style(gui_schedule_entry_number_t::number_style::waypoint);
		entry_no.set_color(player->get_player_color1()); // can't get the owner of the way without passing the value of waytype
		wpbox.set_visible(true);
	}
	entry_no.set_visible(!wpbox.is_visible());

	img_layover.set_visible(entry.is_flag_set(schedule_entry_t::lay_over));
	img_refuel.set_visible(entry.is_flag_set(schedule_entry_t::force_range_stop));

	if (entry.is_flag_set(schedule_entry_t::ignore_choose)) {
		lb_pos.set_color(color_idx_to_rgb(COL_BLUE));
		img_ignore_choose.set_visible(true);
	}
	else {
		lb_pos.set_color(SYSCOL_TEXT);
		img_ignore_choose.set_visible(false);
	}

	schedule_t::gimme_stop_name(stop.buf(), world(), player, entry, no_control_tower); // UI TODO: After porting the function, remove this function
	stop.update();

	img_hourglass.set_visible(entry.is_flag_set(schedule_entry_t::wait_for_time) || (entry.minimum_loading > 0 && entry.waiting_time_shift > 0));
	lb_reverse.set_visible(entry.reverse == 1);

	if (entry.max_speed_kmh != 65535) {
		lb_speed_limit.buf().printf("%u %s", entry.max_speed_kmh, "km/h");
	}
	lb_speed_limit.update();
}

void gui_schedule_entry_t::set_distance(koord3d next_pos, uint32 distance_to_next_halt, uint16 range_limit)
{
	// uint32 distance_to_next_pos;
	const uint32 distance_to_next_pos = shortest_distance(next_pos.get_2d(), entry.pos.get_2d()) * world()->get_settings().get_meters_per_tile();
	if (distance_to_next_pos != distance_to_next_halt || !welt->lookup(entry.pos)->get_halt().is_bound()) {
		// Either is not a station
		lb_distance.buf().printf("(%.1f%s)", (double)(distance_to_next_pos/1000.0), "km");
	}
	else {
		lb_distance.buf().printf("%4.1f%s", (double)(distance_to_next_pos / 1000.0), "km");
	}
	lb_distance.set_color(range_limit && range_limit < (uint16)distance_to_next_halt/1000 ? COL_DANGER : SYSCOL_TEXT);
	route_bar->set_alert_level(range_limit && range_limit < (uint16)distance_to_next_halt/1000 ? 3 : 0);
	lb_distance.update();
}

void gui_schedule_entry_t::set_speed_limit(uint32 speed)
{
	if (speed!=65535) {
		lb_speed_limit.buf().printf("%u %s", speed, "km/h");
	}
	lb_speed_limit.update();
}

void gui_schedule_entry_t::set_line_style(uint8 s)
{
	route_bar->set_line_style(s);
	wpbox.set_line_style(s);
}

void gui_schedule_entry_t::set_active(bool yesno)
{
	is_current = yesno;
	stop.set_color(yesno ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_TEXT);
	lb_pos.set_color(yesno ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_TEXT);
	entry_no.set_highlight(yesno);
	wpbox.set_highlight(yesno);
}

void gui_schedule_entry_t::draw(scr_coord offset)
{
	update_label();
	if (is_current) {
		display_blend_wh_rgb(pos.x+offset.x, pos.y+offset.y-1, size.w, L_ENTRY_NO_HEIGHT+2, SYSCOL_LIST_BACKGROUND_SELECTED_F, 75);
	}
	gui_aligned_container_t::draw(offset);
}

bool gui_schedule_entry_t::infowin_event(const event_t *ev)
{
	if( ev->ev_class == EVENT_CLICK ) {
		if(  IS_RIGHTCLICK(ev)  ) {
			// just center on it
			welt->get_viewport()->change_world_position( entry.pos );
		}
		else {
			call_listeners(number);
		}
		return true;
	}
	return gui_aligned_container_t::infowin_event(ev);
}

bool gui_schedule_entry_t::action_triggered(gui_action_creator_t *c, value_t )
{
	if ( c == &bt_del ) {
		call_listeners( DELETE_FLAG | number);
		return true;
	}
	else if ( c == &bt_swap ) {
		call_listeners( DOWN_FLAG | number);
		return true;
	}

	return false;
}


// shows/deletes highlighting of tiles
void schedule_gui_stats_t::highlight_schedule( schedule_t *markschedule, bool marking )
{
	marking &= env_t::visualize_schedule;
	uint8 n = 0;
	FORX(minivec_tpl<schedule_entry_t>, const& i, schedule->entries, n++) {
		if (grund_t* const gr = welt->lookup(i.pos)) {
			for(  uint idx=0;  idx<gr->get_top();  idx++  ) {
				obj_t *obj = gr->obj_bei(idx);
				if(  marking  ) {
					if(  !obj->is_moving()  ) {
						obj->set_flag( obj_t::highlight );
					}
				}
				else {
					obj->clear_flag( obj_t::highlight );
				}
			}
			gr->set_flag( grund_t::dirty );
			// here on water
			if(  gr->is_water()  ||  gr->ist_natur()  ) {
				if(  marking  ) {
					gr->set_flag( grund_t::marked );
				}
				else {
					gr->clear_flag( grund_t::marked );
				}
			}

			schedule_marker_t *marker = gr->find<schedule_marker_t>();
			if (marking) {
				if (!marker) {
					marker = new schedule_marker_t(i.pos, player, markschedule->get_waytype());
					marker->set_color( line_color_index>=254 ? color_idx_to_rgb(player->get_player_color1()+4) : line_color_idx_to_rgb(line_color_index) );
					gr->obj_add(marker);
				}
				uint8 number_style = gui_schedule_entry_number_t::halt;
				if (haltestelle_t::get_halt(i.pos, player).is_bound()) {
					;
				}
				else if (gr->get_depot() != NULL) {
					number_style = gui_schedule_entry_number_t::depot;
				}
				else {
					number_style = gui_schedule_entry_number_t::waypoint;
				}
				marker->set_entry_data(n, number_style, markschedule->is_mirrored(), (i.reverse == 1));
				marker->set_selected((i == markschedule->get_current_entry()));
			}
			else if (marker) {
				// remove marker
				gr->obj_remove(marker);
			}
		}
	}
}


cbuffer_t schedule_gui_stats_t::buf;



schedule_gui_stats_t::schedule_gui_stats_t()
{
	set_table_layout(1,0);
	set_margin(scr_size(0,0), scr_size(0,0));
	set_spacing(scr_size(D_H_SPACE, 0));
	last_schedule = NULL;

	current_stop_mark = new zeiger_t(koord3d::invalid, NULL);
	current_stop_mark->set_image(tool_t::general_tool[TOOL_SCHEDULE_ADD]->cursor);
}



schedule_gui_stats_t::~schedule_gui_stats_t()
{
	delete current_stop_mark;
	delete last_schedule;
}

void schedule_gui_stats_t::update_schedule()
{
	// compare schedules
	bool ok = (last_schedule != NULL) && last_schedule->matches(world(), schedule);
	for (uint i = 0; ok && i < last_schedule->entries.get_count(); i++) {
		ok = last_schedule->entries[i] == schedule->entries[i];
	}
	if (ok) {
		if (!last_schedule->empty()) {
			entries[last_schedule->get_current_stop()]->set_active(false);
			entries[schedule->get_current_stop()]->set_active(true);
			last_schedule->set_current_stop(schedule->get_current_stop());
		}
	}
	else {
		remove_all();
		entries.clear();
		buf.clear();
		buf.append(translator::translate("Please click on the map to add\nwaypoints or stops to this\nschedule."));
		if (schedule->empty()) {
			add_table(1,1)->set_margin(scr_size(D_MARGIN_LEFT, D_MARGIN_TOP), scr_size(D_MARGIN_RIGHT, D_MARGIN_BOTTOM));
			new_component<gui_textarea_t>(&buf);
			end_table();
		}
		else {
			const grund_t* gr_0 = welt->lookup(schedule->entries[0].pos);
			const uint8 base_line_style = schedule->is_mirrored() ? gui_colored_route_bar_t::line_style::doubled : gui_colored_route_bar_t::line_style::solid;
			const bool is_air_wt = schedule->get_waytype() == air_wt;

			for (uint i = 0; i < schedule->entries.get_count(); i++) {
				const grund_t* gr_i = welt->lookup(schedule->entries[i].pos);
				entries.append(new_component<gui_schedule_entry_t>(player, schedule->entries[i], i, is_air_wt, line_color_index));
				if (i< schedule->entries.get_count()-1) {
					const grund_t* gr_i1 = welt->lookup(schedule->entries[i+1].pos);
					entries[i]->set_distance(schedule->entries[i + 1].pos, 0, range_limit);
					//entries[i]->set_distance(schedule->entries[i+1].pos, schedule->calc_distance_to_next_halt(player, i), range_limit);
					if (schedule->entries[i].pos == schedule->entries[i+1].pos) {
						entries[i]->set_line_style(gui_colored_route_bar_t::line_style::thin);
					}
					else if((gr_i && gr_i->get_depot()) || (gr_i1 && gr_i1->get_depot())) {
						entries[i]->set_line_style(gui_colored_route_bar_t::line_style::dashed);
					}
					else {
						entries[i]->set_line_style(base_line_style);
					}
				}
				entries[i]->set_speed_limit(schedule->entries[i].max_speed_kmh);
				entries.back()->add_listener(this);
			}
			if (schedule->entries.get_count()>1) {
				if (schedule->is_mirrored()) {
					entries[schedule->entries.get_count()-1]->set_line_style(gui_colored_route_bar_t::line_style::reversed);
				}
				else {
					entries[schedule->entries.get_count()-1]->set_distance((gr_0 && gr_0->get_depot()) ? schedule->entries[1].pos : schedule->entries[0].pos, 0, range_limit);
					entries[schedule->entries.get_count()-1]->set_line_style(gui_colored_route_bar_t::line_style::dashed);
				}
			}
			entries[schedule->get_current_stop()]->set_active(true);
		}
		if (last_schedule) {
			last_schedule->copy_from(schedule);
		}
		else {
			last_schedule = schedule->copy();
		}
	}
	highlight_schedule(schedule, true);
}

void schedule_gui_stats_t::draw(scr_coord offset)
{
	update_schedule();
	if (size!=get_min_size()) {
		set_size(get_min_size()); // This is necessary to display the component in the correct position immediately after opening the dialog
	}
	gui_aligned_container_t::draw(offset);
}

bool schedule_gui_stats_t::action_triggered(gui_action_creator_t *, value_t v)
{
	// has to be one of the entries
	if( v.i & DELETE_FLAG ) {
		uint8 delete_stop = v.i & 0x00FF;
		highlight_schedule( schedule, false );
		schedule->remove_entry( delete_stop );
		highlight_schedule(  schedule, true );
		call_listeners( schedule->get_current_stop() );
	}
	else if( v.i & DOWN_FLAG ) {
		uint8 down_stop = v.i & 0x00FF;
		schedule->move_entry_backward( down_stop );
		call_listeners( schedule->get_current_stop() );
	}
	else {
		call_listeners(v);
	}
	return true;
}


schedule_gui_t::schedule_gui_t(schedule_t* sch_, player_t* player_, convoihandle_t cnv_) :
	gui_frame_t("", NULL),
	stats(new schedule_gui_stats_t()),
	scroll(stats, true, true)
{
	schedule = NULL;
	player = NULL;
	tabs.add_listener(this);
	tabs.add_tab(&cont_settings_1, translator::translate("entry_setting_1"));
	if (sch_) {
		init(sch_, player_, cnv_);
	}
}

schedule_gui_t::~schedule_gui_t()
{
	if(  player  ) {
		update_tool( false );
		// hide schedule on minimap (may not current, but for safe)
		minimap_t::get_instance()->set_selected_cnv( convoihandle_t() );
	}
	delete schedule;
	delete stats;
}



void schedule_gui_t::init(schedule_t* schedule_, player_t* player, convoihandle_t cnv)
{
	// initialization
	this->old_schedule = schedule_;
	this->cnv = cnv;
	this->player = player;

	if (!cnv.is_bound()) {
		old_line = new_line = linehandle_t();
	}
	else {
		// set this schedule as current to show on minimap if possible
		minimap_t::get_instance()->set_selected_cnv( cnv );
		old_line = new_line = cnv->get_line();
		title.printf("%s - %s", translator::translate("Fahrplan"), cnv->get_name());
		gui_frame_t::set_name(title);
		if (old_line.is_bound()) {
			min_range = old_line->get_min_range() ? old_line->get_min_range() : UINT16_MAX;
			// set line color for marker
			stats->set_line_color_index(old_line->get_line_color_index());
		}
		if (cnv->get_min_range()) {
			min_range = min(min_range, cnv->get_min_range());
		}
		img_electric.set_visible(cnv->needs_electrification());
	}
	old_line_count = 0;

	init_components();
}


void schedule_gui_t::init(linehandle_t line)
{
	if (!line.is_bound()) return;

	old_line = line;
	this->old_schedule = line->get_schedule()->copy();
	cnv = convoihandle_t();
	player = line->get_owner();
	stats->set_line_color_index(line->get_line_color_index());
	// has this line a single running convoi?
	if(  line->count_convoys() > 0  ) {
		minimap_t::get_instance()->set_selected_cnv(line->get_convoy(0));
		img_electric.set_visible(line->needs_electrification());
		min_range = min(line->get_min_range() ? line->get_min_range() : UINT16_MAX, min_range);
		if (min_range>0 && min_range<UINT16_MAX) {
			lb_min_range.buf().printf("%u km", min_range);
			lb_min_range.update();
		}
		img_refuel.set_visible(min_range>0 && min_range<UINT16_MAX);
		lb_min_range.set_visible(min_range>0 && min_range<UINT16_MAX);
	}
	init_components();
}

// Initializations that are not required when executing the revert schedule are performed here.
void schedule_gui_t::init_components()
{
	// init frame
	set_owner(player);

	line_selector.set_highlight_color(color_idx_to_rgb(player->get_player_color1() + 1));

	filter_btn_all_pas.init(button_t::imagebox_state, NULL, scr_coord(0, 0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_pas.set_image(skinverwaltung_t::passengers->get_image_id(0));
	filter_btn_all_pas.set_tooltip("filter_pas_line");
	filter_btn_all_pas.add_listener(this);

	filter_btn_all_mails.init(button_t::imagebox_state, NULL, scr_coord(0,0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_mails.set_image(skinverwaltung_t::mail->get_image_id(0));
	filter_btn_all_mails.set_tooltip("filter_mail_line");
	filter_btn_all_mails.add_listener(this);

	filter_btn_all_freights.init(button_t::imagebox_state, NULL, scr_coord(0, 0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
	filter_btn_all_freights.set_image(skinverwaltung_t::goods->get_image_id(0));
	filter_btn_all_freights.set_tooltip("filter_freight_line");
	filter_btn_all_freights.add_listener(this);

	bt_promote_to_line.init(button_t::roundbox, "promote to line", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_promote_to_line.set_tooltip("Create a new line based on this schedule");
	bt_promote_to_line.add_listener(this);

	img_electric.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
	img_electric.set_tooltip(translator::translate("This line/convoy needs electrification"));
	img_electric.set_rigid(false);

	bt_add.init(button_t::roundbox_state, "Add Stop", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_add.set_tooltip("Appends stops at the end of the schedule");
	bt_add.add_listener(this);

	bt_insert.init(button_t::roundbox_state, "Ins Stop", scr_coord(0, 0), D_BUTTON_SIZE);
	bt_insert.set_tooltip("Insert stop before the current stop");
	bt_insert.add_listener(this);

	//bt_revert.init(button_t::roundbox, "Revert schedule");
	//bt_revert.set_tooltip("Revert to original schedule");
	//bt_revert.add_listener(this);

	lb_min_range.set_fixed_width(proportional_string_width("8888km "));
	lb_min_range.set_rigid(false);

	bt_mirror.init(button_t::square_automatic, "return ticket");
	bt_mirror.set_tooltip("Vehicles make a round trip between the schedule endpoints, visiting all stops in reverse after reaching the end.");
	bt_mirror.add_listener(this);

	bt_bidirectional.init(button_t::square_automatic, "Alternate directions");
	bt_bidirectional.set_tooltip("When adding convoys to the line, every second convoy will follow it in the reverse direction.");
	bt_bidirectional.add_listener(this);

	bt_same_spacing_shift.init(button_t::square_automatic, "Use same shift for all stops.");
	bt_same_spacing_shift.set_tooltip("Use one spacing shift value for all stops in schedule.");
	bt_same_spacing_shift.add_listener(this);

	build_table();
}

void schedule_gui_t::build_table()
{
	// prepare editing
	old_schedule->start_editing();
	schedule = old_schedule->copy();

	// init stats
	stats->player = player;
	stats->schedule = schedule;
	stats->range_limit = min_range;
	stats->update_schedule();
	stats->add_listener(this);

	set_table_layout(1,0);
	set_margin(scr_size(0,D_V_SPACE), scr_size(D_H_SPACE,0));

	if (cnv.is_bound()) {
		add_table(4,1)->set_margin(scr_size(D_H_SPACE, 0), scr_size(D_H_SPACE, D_V_SPACE));
		{
			new_component<gui_label_t>("Serves Line:");
			line_selector.clear_elements();
			init_line_selector();
			line_selector.add_listener(this);
			add_component(&line_selector);

			add_table(3,1)->set_spacing(scr_size(0,0));
			{
				filter_btn_all_pas.disable();
				add_component(&filter_btn_all_pas);

				filter_btn_all_mails.disable();
				add_component(&filter_btn_all_mails);

				filter_btn_all_freights.disable();
				add_component(&filter_btn_all_freights);
			}
			end_table();

			add_component(&bt_promote_to_line);
		}
		end_table();
	}

	// bottom table
	add_table(2,0)->set_alignment(ALIGN_TOP);
	{
		// left: route setting
		add_table(1,0);
		{
			// buttons
			add_table(3,1)->set_margin(scr_size(D_H_SPACE, 0), scr_size(D_H_SPACE, D_V_SPACE));
			{
				new_component<gui_waytype_image_box_t>(schedule->get_waytype());
				add_component(&img_electric);

				add_table(3,1)->set_spacing(scr_size(0,0));
				{
					bt_add.pressed = true;
					add_component(&bt_add);

					bt_insert.pressed = false;
					add_component(&bt_insert);

					//bt_revert.pressed = false;
					//bt_revert.enable(false); // schedule was not changed yet
					//add_component(&bt_revert);
					new_component<gui_margin_t>(D_BUTTON_WIDTH);
				}
				end_table();
			}
			end_table();

			add_table(6,1);
			{
				new_component<gui_margin_t>(D_H_SPACE);
				img_refuel.set_image(skinverwaltung_t::refuel ? skinverwaltung_t::refuel->get_image_id(0) : IMG_EMPTY, true);
				img_refuel.set_tooltip(translator::translate("This line/convoy needs refueling at the stop."));
				img_refuel.set_rigid(false);
				img_refuel.set_visible(min_range && min_range != UINT16_MAX);
				add_component(&img_refuel);

				if (min_range && min_range != UINT16_MAX) {
					lb_min_range.buf().printf("%u km", min_range);
					lb_min_range.update();
				}
				lb_min_range.set_visible(min_range && min_range != UINT16_MAX);
				add_component(&lb_min_range);

				new_component<gui_fill_t>();
				add_table(2,1);
				{
					new_component<gui_image_t>()->set_image(skinverwaltung_t::reverse_arrows ? skinverwaltung_t::reverse_arrows->get_image_id(0) : IMG_EMPTY, true);
					// Mirror schedule/alternate directions
					bt_mirror.pressed = schedule->is_mirrored();
					add_component(&bt_mirror);
				}
				end_table();

				bt_bidirectional.pressed = schedule->is_bidirectional();
				add_component(&bt_bidirectional);
			}
			end_table();

			scroll.set_maximize(true);
			add_table(1,1)->set_table_frame(true,true);
			{
				add_component(&scroll);
			}
			end_table();
		}
		end_table();

		// right: selected entry setting
		add_table(1,0);
		{
			const uint8 spacing_shift_mode = welt->get_settings().get_spacing_shift_mode();
			if (!cnv.is_bound() && spacing_shift_mode > settings_t::SPACING_SHIFT_PER_LINE) {
				//Same spacing button
				bt_same_spacing_shift.pressed = schedule->is_same_spacing_shift();
				add_component(&bt_same_spacing_shift);
			}

			add_table(2,1);
			{
				entry_no = new_component<gui_schedule_entry_number_t>(-1, player->get_player_nr(), 0);
				entry_no->set_rigid(true);
				add_component(&lb_entry_pos);
			}
			end_table();

			add_component(&tabs);

			cont_settings_1.set_table_frame(true);
			cont_settings_1.set_table_layout(1,0);
			cont_settings_1.set_margin(scr_size(D_H_SPACE, D_MARGIN_TOP), scr_size(0, D_V_SPACE));
			cont_settings_1.add_table(2,0)->set_spacing(scr_size(0,0));
			{
				// Minimum loading
				cont_settings_1.new_component<gui_image_t>()->set_image(skinverwaltung_t::goods->get_image_id(0), true);
				cont_settings_1.add_table(4,1);
				{
					lb_load.buf().append(translator::translate("Full load"));
					lb_load.update();
					cont_settings_1.add_component(&lb_load);
					numimp_load.init(schedule->get_current_entry().minimum_loading, 0, 100, 10, false);
					numimp_load.set_width(70);
					numimp_load.add_listener(this);
					cont_settings_1.add_component(&numimp_load);
					cont_settings_1.new_component<gui_label_t>("%");
					cont_settings_1.new_component<gui_empty_t>();
				}
				cont_settings_1.end_table();

				cont_settings_1.new_component<gui_margin_t>(D_H_SPACE);
				bt_pickup_only.init(button_t::square_automatic, "bt_pickup_only");
				bt_pickup_only.set_tooltip("help_txt_pickup_only");
				bt_pickup_only.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::pick_up_only);
				bt_pickup_only.add_listener(this);
				cont_settings_1.add_component(&bt_pickup_only);

				cont_settings_1.new_component<gui_margin_t>(D_H_SPACE);
				bt_setdown_only.init(button_t::square_automatic, "bt_setdown_only");
				bt_setdown_only.set_tooltip("help_txt_setdown_only");
				bt_setdown_only.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::set_down_only);
				bt_setdown_only.add_listener(this);
				cont_settings_1.add_component(&bt_setdown_only);

				cont_settings_1.new_component<gui_margin_t>(D_H_SPACE);
				bt_discharge_payload.init(button_t::square_automatic, "bt_discharge_payload");
				bt_discharge_payload.set_tooltip("help_txt_discharge_payload");
				bt_discharge_payload.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::discharge_payload);
				bt_discharge_payload.add_listener(this);
				cont_settings_1.add_component(&bt_discharge_payload);
			}
			cont_settings_1.end_table();

			cont_settings_1.add_table(5,0)->set_spacing(scr_size(D_H_SPACE,0));
			{
				if (!cnv.is_bound()) {
					// Wait for time
					cont_settings_1.new_component<gui_image_t>()->set_image(skinverwaltung_t::waiting_time ? skinverwaltung_t::waiting_time->get_image_id(0) : IMG_EMPTY, true);
					bt_wait_for_time.init(button_t::square_automatic, "Wait for time");
					bt_wait_for_time.set_tooltip("If this is set, convoys will wait until one of the specified times before departing, the specified times being fractions of a month.");
					bt_wait_for_time.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::wait_for_time);
					bt_wait_for_time.add_listener(this);
					cont_settings_1.add_component(&bt_wait_for_time,4);
					//cont_settings_1.new_component<gui_empty_t>();
				}

				if (cnv.is_bound()) {
					cont_settings_1.new_component<gui_image_t>()->set_image(skinverwaltung_t::waiting_time ? skinverwaltung_t::waiting_time->get_image_id(0) : IMG_EMPTY, true);
				}
				else {
					cont_settings_1.new_component<gui_margin_t>(D_CHECKBOX_WIDTH);
				}
				// Maximum waiting time
				lb_wait.buf().append(translator::translate("month wait time"));
				lb_wait.update();
				cont_settings_1.add_component(&lb_wait);

				if (schedule->get_current_entry().waiting_time_shift == 0) {
					strcpy(str_parts_month, translator::translate("off"));
					strcpy(str_parts_month_as_clock, translator::translate("off"));
				}
				else {
					sprintf(str_parts_month, "1/%d", 1 << (16 - schedule->get_current_entry().waiting_time_shift));
					sint64 ticks_waiting = welt->ticks_per_world_month >> (16 - schedule->get_current_entry().waiting_time_shift);
					welt->sprintf_ticks(str_parts_month_as_clock, sizeof(str_parts_month_as_clock), ticks_waiting + 1);
				}

				cont_settings_1.add_table(4,1);
				{
					bt_wait_prev.init(button_t::arrowleft, NULL, scr_coord(0, 0));
					bt_wait_prev.add_listener(this);
					cont_settings_1.add_component(&bt_wait_prev);

					lb_waitlevel_as_clock.init(SYSCOL_TEXT_HIGHLIGHT, gui_label_t::right);
					lb_waitlevel_as_clock.buf().append("off");
					lb_waitlevel_as_clock.set_fixed_width(proportional_string_width("--:--:--"));
					lb_waitlevel_as_clock.update();
					cont_settings_1.add_component(&lb_waitlevel_as_clock);

					bt_wait_next.init(button_t::arrowright, NULL, scr_coord(0, 0));
					bt_wait_next.add_listener(this);
					cont_settings_1.add_component(&bt_wait_next);

					cont_settings_1.new_component<gui_empty_t>();
				}
				cont_settings_1.end_table();
				cont_settings_1.new_component<gui_empty_t>();
			}
			cont_settings_1.end_table();


			if (!cnv.is_bound()) {
				cont_settings_1.add_table(5,2)->set_spacing(scr_size(D_H_SPACE,0));
				{
					// Spacing
					cont_settings_1.new_component<gui_margin_t>(D_CHECKBOX_WIDTH<<1);
					lb_spacing.set_text("Spacing cnv/month");
					lb_spacing.set_tooltip(translator::translate("help_txt_departure_per_month"));
					cont_settings_1.add_component(&lb_spacing);

					// UI TODO: Make it clearer to the player that this is set in increments of 12ths of a fraction of a month.
					numimp_spacing.init(schedule->get_spacing(), 0, 999, 12);
					numimp_spacing.add_listener(this);
					cont_settings_1.add_component(&numimp_spacing);

					lb_spacing_as_clock.init(SYSCOL_TEXT);
					lb_spacing_as_clock.buf().append(str_spacing_as_clock);
					lb_spacing_as_clock.set_fixed_width(proportional_string_width("--:--:--"));
					lb_spacing_as_clock.update();
					cont_settings_1.add_component(&lb_spacing_as_clock);
					cont_settings_1.new_component<gui_empty_t>();

					// Spacing shift
					cont_settings_1.new_component<gui_margin_t>(D_CHECKBOX_WIDTH<<1);
					lb_shift.init(translator::translate("time shift"), scr_coord(0, 0));
					lb_shift.set_tooltip(translator::translate("help_txt_departure_time_shift"));
					cont_settings_1.add_component(&lb_shift);

					if (spacing_shift_mode > settings_t::SPACING_SHIFT_DISABLED) {
						numimp_spacing_shift.init(schedule->get_current_entry().spacing_shift, 0, welt->get_settings().get_spacing_shift_divisor(), 1);
						numimp_spacing_shift.add_listener(this);
						cont_settings_1.add_component(&numimp_spacing_shift);
					}
					else {
						cont_settings_1.new_component<gui_empty_t>();
					}

					if (spacing_shift_mode > settings_t::SPACING_SHIFT_PER_LINE) {
						cont_settings_1.add_table(2,1);
						{
							lb_plus.init("+", scr_coord(0, 0));
							cont_settings_1.add_component(&lb_plus);

							lb_spacing_shift_as_clock.set_align(gui_label_t::right);
							lb_spacing_shift_as_clock.set_fixed_width(proportional_string_width("--:--:--"));
							lb_spacing_shift_as_clock.set_text_pointer(str_spacing_shift_as_clock, false);
							cont_settings_1.add_component(&lb_spacing_shift_as_clock);
						}
						cont_settings_1.end_table();
					}
					else {
						cont_settings_1.new_component<gui_empty_t>();
					}
					cont_settings_1.new_component<gui_empty_t>();

				}
				cont_settings_1.end_table();
			}

			// Force range stop
			cont_settings_1.new_component<gui_image_t>()->set_image(skinverwaltung_t::refuel ? skinverwaltung_t::refuel->get_image_id(1) : IMG_EMPTY, true);
			bt_range_stop.init(button_t::square_automatic, "force_range_stop");
			bt_range_stop.set_tooltip("if_this_is_set,_this_stop_will_at_all_times_be_considered_a_range_stop");
			bt_range_stop.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::force_range_stop);
			bt_range_stop.add_listener(this);
			cont_settings_1.add_component(&bt_range_stop);
			cont_settings_1.new_component<gui_empty_t>();

			// Speed limit
			cont_settings_1.add_table(4,2)->set_spacing(scr_size(D_H_SPACE,0));
			{
				cont_settings_1.new_component<gui_margin_t>(D_CHECKBOX_WIDTH);
				bt_speed_limit.init(button_t::square_state, "enable_speed_limit");
				bt_speed_limit.pressed = (schedule->get_current_entry().max_speed_kmh!=65535);
				bt_speed_limit.set_tooltip(translator::translate("help_txt_speed_limit_between_two_points"));
				bt_speed_limit.add_listener(this);
				cont_settings_1.add_component(&bt_speed_limit,3);

				cont_settings_1.new_component<gui_margin_t>(D_CHECKBOX_WIDTH);
				lb_speed_limit.init("Speed limit:", scr_coord(0,0), bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED );
				cont_settings_1.add_component(&lb_speed_limit);
				numimp_speed_limit.init(schedule->get_current_entry().max_speed_kmh, 0, 65535, 1);
				numimp_speed_limit.set_value(schedule->get_current_entry().max_speed_kmh);
				numimp_speed_limit.add_listener(this);
				cont_settings_1.add_component(&numimp_speed_limit);

				lb_speed_limit_kmh.init(" km/h", scr_coord(0,0), bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
				cont_settings_1.add_component(&lb_speed_limit_kmh);
			}
			cont_settings_1.end_table();

			cont_settings_2.set_table_frame(true);
			cont_settings_2.set_table_layout(1, 0);
			cont_settings_2.set_margin(scr_size(0, D_MARGIN_TOP), scr_size(0, D_V_SPACE));
			// Modify convoy button
			if( !cnv.is_bound() ) {
				cont_settings_2.add_table(2,1)->set_spacing(scr_size(D_H_SPACE,1));
				{
					bt_consist_order.init(button_t::roundbox_state, "modify_consist", scr_coord(0,0), D_WIDE_BUTTON_SIZE);
					bt_consist_order.set_tooltip("modify_the_consist_at_this_schedule_entry");
					if (skinverwaltung_t::open_window) {
						bt_consist_order.set_image(skinverwaltung_t::open_window->get_image_id(0));
						bt_consist_order.set_image_position_right(true);
					}
					bt_consist_order.add_listener(this);
					bt_consist_order.pressed = false;
					cont_settings_2.add_component(&bt_consist_order);
					lb_consist_order_modified.init("(*)", scr_coord(0,0), SYSCOL_TEXT_HIGHLIGHT);
					cont_settings_2.add_component(&lb_consist_order_modified);
				}
				cont_settings_2.end_table();
			}

			cont_settings_2.add_table(3,0)->set_spacing(scr_size(0,0));
			{
				// Issuing Lay over
				cont_settings_2.new_component<gui_image_t>()->set_image(skinverwaltung_t::layover ? skinverwaltung_t::layover->get_image_id(0): IMG_EMPTY, true);
				bt_lay_over.init(button_t::square_automatic, "lay_over");

				// Check whether the station has layover facilities and disable the button if so.
				halthandle_t halt = haltestelle_t::get_halt(schedule->get_current_entry().pos, player);
				bt_lay_over.add_listener(this);
				bt_lay_over.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::lay_over);
				if (!halt.is_bound() || !halt->can_lay_over())
				{
					bt_lay_over.disable();
					bt_lay_over.set_tooltip("helptxt_no_layover_enabled_at_stop");
				}
				else
				{
					bt_lay_over.set_tooltip("if_this_is_set,_convoy_will_go_into_lay_over_state_at_this_stop");
				}

				cont_settings_2.add_component(&bt_lay_over);
				cont_settings_2.new_component<gui_empty_t>();

				// Ignore choose sign/signal
				cont_settings_2.new_component<gui_image_t>()->set_image(skinverwaltung_t::ignore_choose ? skinverwaltung_t::ignore_choose->get_image_id(0) : skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(1) : IMG_EMPTY, true);
				bt_ignore_choose.init(button_t::square_automatic, "ignore_choose_on_arrival");
				bt_ignore_choose.set_tooltip("If this is set, choose signals will be ignored while this convoy is heading to this destination.");
				bt_ignore_choose.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::ignore_choose);
				bt_ignore_choose.add_listener(this);
				cont_settings_2.add_component(&bt_ignore_choose);
				cont_settings_2.new_component<gui_empty_t>();
			}
			cont_settings_2.end_table();
			// Conditional depart
			cont_settings_2.add_table(3,2);
			{
				cont_settings_2.new_component<gui_vehicle_bar_t>(SYSCOL_UP_TRIANGLE, scr_size(VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT))->set_flags(3, vehicle_desc_t::unknown_constraint, HAS_POWER|BIDIRECTIONAL);
				cont_settings_2.new_component<gui_label_t>("wait_for_trigger")->set_tooltip(translator::translate("if_this_is_set,_convoys_will_wait_until_this_condition_is_broadcasted_by_another_convoy"));
				conditional_depart.set_width(numimp_load.get_size().w);
				conditional_depart.init(schedule->get_current_entry().condition_bitfield_receiver, 0, 15, 1);
				conditional_depart.add_listener(this);
				cont_settings_2.add_component(&conditional_depart);

				// Condition broadcast
				cont_settings_2.new_component<gui_vehicle_bar_t>(SYSCOL_DOWN_TRIANGLE, scr_size(VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT))->set_flags(vehicle_desc_t::unknown_constraint, 3, HAS_POWER|BIDIRECTIONAL);
				cont_settings_2.new_component<gui_label_t>("broadcast_trigger_on_arrival")->set_tooltip(translator::translate("if_this_is_set,_convoy_will_broadcast_this_condition_to_other_convoys_at_this_station_when_arriving"));
				condition_broadcast.set_width(60);
				condition_broadcast.set_value(schedule->get_current_entry().condition_bitfield_broadcaster);
				condition_broadcast.set_limits(0, 15);
				condition_broadcast.set_increment_mode(1);
				condition_broadcast.add_listener(this);
				cont_settings_2.add_component(&condition_broadcast);
			}
			cont_settings_2.end_table();

			// trigger
			cont_settings_2.new_component<gui_divider_t>();
			cont_settings_2.new_component<gui_label_t>("condition_trigger")->set_tooltip("help_txt_condition_trigger");
			cont_settings_2.add_table(2,1)->set_spacing(scr_size(0, 0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH << 1);
				condition_line_selector.add_listener(this);
				cont_settings_2.add_component(&condition_line_selector);
			}
			cont_settings_2.end_table();

			cont_settings_2.new_component<gui_divider_t>();
			cont_settings_2.new_component<gui_label_t>("couple target")->set_tooltip("help_txt_uncouple_target");
			cont_settings_2.add_table(4,1)->set_spacing(scr_size(0,0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH << 1);
				bt_couple_is_line.add_listener(this);
				bt_couple_is_cnv.add_listener(this);
				bt_couple_is_line.init(button_t::roundbox_left_state, "Line");
				bt_couple_is_cnv.init(button_t::roundbox_right_state, "Convoy");
				cont_settings_2.add_component(&bt_couple_is_line);
				cont_settings_2.add_component(&bt_couple_is_cnv);
				cont_settings_2.new_component<gui_fill_t>();
			}
			cont_settings_2.end_table();
			cont_settings_2.add_table(2,1)->set_spacing(scr_size(0,0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH<<1);
				couple_target_selector.add_listener(this);
				cont_settings_2.add_component(&couple_target_selector);
			}
			cont_settings_2.end_table();

			cont_settings_2.new_component<gui_divider_t>();

			cont_settings_2.new_component<gui_label_t>("uncouple target")->set_tooltip("help_txt_uncouple_target");

			cont_settings_2.add_table(4,1)->set_spacing(scr_size(0, 0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH << 1);
				bt_uncouple_is_line.add_listener(this);
				bt_uncouple_is_cnv.add_listener(this);
				bt_uncouple_is_line.init(button_t::roundbox_left_state, "Line");
				bt_uncouple_is_cnv.init(button_t::roundbox_right_state, "Convoy");
				cont_settings_2.add_component(&bt_uncouple_is_line);
				cont_settings_2.add_component(&bt_uncouple_is_cnv);
				cont_settings_2.new_component<gui_fill_t>();
			}
			cont_settings_2.end_table();
			cont_settings_2.add_table(2, 1)->set_spacing(scr_size(0, 0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH<<1);
				uncouple_target_selector.add_listener(this);
				cont_settings_2.add_component(&uncouple_target_selector);
			}
			cont_settings_2.end_table();

			cb_uncouple_target_entry.add_listener(this);
			cont_settings_2.add_table(4, 1)->set_spacing(scr_size(0, 0));
			{
				cont_settings_2.new_component<gui_margin_t>(D_CHECKBOX_WIDTH<<1);
				cont_settings_2.new_component<gui_label_t>("target_entry_uncouple")->set_tooltip("help_txt_target_entry_uncouple");
				cont_settings_2.add_component(&cb_uncouple_target_entry);
				cont_settings_2.new_component<gui_fill_t>();
			}
			cont_settings_2.end_table();

		}
		end_table();
	}
	end_table();



	mode = adding;
	update_selection();

	set_resizemode(diagonal_resize);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());

	if( cnv.is_bound() ) {
		line_selector.set_width_fixed(true);
	}
}


void schedule_gui_t::update_tool(bool set)
{
	if(!set  ||  mode==removing  ||  mode==undefined_mode) {
		// reset tools, if still selected ...
		if(welt->get_tool(player->get_player_nr())==tool_t::general_tool[TOOL_SCHEDULE_ADD]) {
			if(tool_t::general_tool[TOOL_SCHEDULE_ADD]->get_default_param()==(const char *)schedule) {
				welt->set_tool( tool_t::general_tool[TOOL_QUERY], player );
			}
		}
		else if(welt->get_tool(player->get_player_nr())==tool_t::general_tool[TOOL_SCHEDULE_INS]) {
			if(tool_t::general_tool[TOOL_SCHEDULE_INS]->get_default_param()==(const char *)schedule) {
				welt->set_tool( tool_t::general_tool[TOOL_QUERY], player );
			}
		}
	}
	else {
		//  .. or set them again
		if(mode==adding) {
			tool_t::general_tool[TOOL_SCHEDULE_ADD]->set_default_param((const char *)schedule);
			welt->set_tool( tool_t::general_tool[TOOL_SCHEDULE_ADD], player );
		}
		else if(mode==inserting) {
			tool_t::general_tool[TOOL_SCHEDULE_INS]->set_default_param((const char *)schedule);
			welt->set_tool( tool_t::general_tool[TOOL_SCHEDULE_INS], player );
		}
	}
}


void schedule_gui_t::update_target_line_selection(bool condition, bool couple, bool uncouple)
{
	if (!schedule->empty()) {
		if(condition) condition_line_selector.clear_elements();
		if (couple)   couple_target_selector.clear_elements();
		if (uncouple) uncouple_target_selector.clear_elements();
		const uint8 current_stop = schedule->get_current_stop();
		halthandle_t halt = haltestelle_t::get_halt(schedule->entries[current_stop].pos, player);
		if (halt.is_bound()) {
			if (condition) {
				condition_line_selector.enable();
				condition_line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------------------------"), SYSCOL_TEXT_INACTIVE);
				condition_line_selector.set_selection(0);
			}
			if (couple) {
				couple_target_selector.enable();
				couple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------------------------"), SYSCOL_TEXT_INACTIVE);
				couple_target_selector.set_selection(0);
			}
			if (uncouple) {
				uncouple_target_selector.enable();
				uncouple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------------------------"), SYSCOL_TEXT_INACTIVE);
				uncouple_target_selector.set_selection(0);
			}

			waytype_t wt = schedule->get_waytype();
			for (uint32 i = 0; i < halt->registered_lines.get_count(); i++) {
				const linehandle_t line = halt->registered_lines[i];
				if (!line.is_bound() || wt != line->get_schedule()->get_waytype()) {
					continue;
				}
				if (line == old_line) { continue; }
				if (condition) {
					condition_line_selector.new_component<line_scrollitem_t>(line);
					if (line.get_id() == schedule->entries[current_stop].target_id_condition_trigger) {
						condition_line_selector.set_selection(condition_line_selector.count_elements()-1);
					}
				}
				if (couple && bt_couple_is_line.pressed) {
					couple_target_selector.new_component<line_scrollitem_t>(line);
					if (line.get_id() == schedule->entries[current_stop].target_id_couple) {
						couple_target_selector.set_selection(couple_target_selector.count_elements()-1);
					}
				}
				if (uncouple && bt_uncouple_is_line.pressed) {
					uncouple_target_selector.new_component<line_scrollitem_t>(line);
					if (line.get_id() == schedule->entries[current_stop].target_id_uncouple) {
						uncouple_target_selector.set_selection(uncouple_target_selector.count_elements()-1);
						update_uncouple_target_entries(line->get_schedule());
					}
				}
			}
			if ( condition  &&  condition_line_selector.count_elements()==1 ) {
				condition_line_selector.clear_elements();
				condition_line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("line not found"), SYSCOL_TEXT_INACTIVE);
				condition_line_selector.set_selection(0);
				condition_line_selector.disable();
			}
			if ( couple  &&  couple_target_selector.count_elements()==1 ) {
				disable_couple_target_selector();
			}
			if ( uncouple  &&  uncouple_target_selector.count_elements()==1 ) {
				disable_couple_target_selector(true);
			}
		}
		else {
			condition_line_selector.disable();
			couple_target_selector.disable();
			uncouple_target_selector.disable();
		}
	}
}

void schedule_gui_t::update_target_convoy_selection(bool couple, bool uncouple)
{
	if (!schedule->empty()) {
		if (couple)   couple_target_selector.clear_elements();
		if (uncouple) uncouple_target_selector.clear_elements();
		const uint8 current_stop = schedule->get_current_stop();
		halthandle_t halt = haltestelle_t::get_halt(schedule->entries[current_stop].pos, player);
		if (halt.is_bound()) {
			if (couple) {
				couple_target_selector.enable();
				couple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------------------------"), SYSCOL_TEXT_INACTIVE);
				couple_target_selector.set_selection(0);
			}
			if (uncouple) {
				uncouple_target_selector.enable();
				uncouple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------------------------"), SYSCOL_TEXT_INACTIVE);
				uncouple_target_selector.set_selection(0);
			}

			waytype_t wt = schedule->get_waytype();
			for (uint32 i = 0; i < halt->registered_convoys.get_count(); i++) {
				const convoihandle_t connected_cnv = halt->registered_convoys[i];
				if (wt != connected_cnv->get_schedule()->get_waytype()) {
					continue;
				}
				if ( couple  &&  bt_couple_is_line.pressed ) {
					couple_target_selector.new_component<convoy_scrollitem_t>(connected_cnv, false);
					if (connected_cnv.get_id() == schedule->entries[current_stop].target_id_couple) {
						couple_target_selector.set_selection(couple_target_selector.count_elements() - 1);
					}
				}
				if ( uncouple  &&  bt_uncouple_is_line.pressed ) {
					uncouple_target_selector.new_component<convoy_scrollitem_t>(connected_cnv, false);
					if (connected_cnv.get_id() == schedule->entries[current_stop].target_id_uncouple) {
						uncouple_target_selector.set_selection(uncouple_target_selector.count_elements() - 1);
						update_uncouple_target_entries(connected_cnv->get_schedule());
					}
				}
			}
			if ( couple  &&  couple_target_selector.count_elements()==1 ) {
				disable_couple_target_selector();
			}
			if ( uncouple  &&  uncouple_target_selector.count_elements()==1 ) {
				disable_couple_target_selector(true);
			}
		}
		else {
			couple_target_selector.disable();
			uncouple_target_selector.disable();
		}
	}
}

void schedule_gui_t::disable_couple_target_selector(bool is_uncouple)
{
	if (is_uncouple) {
		uncouple_target_selector.clear_elements();
		if (!bt_uncouple_is_line.pressed && !bt_uncouple_is_cnv.pressed) {
			uncouple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("select_line_or_cnv_first"), SYSCOL_TEXT_INACTIVE);
		}
		else if (bt_uncouple_is_line.pressed) {
			uncouple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("line not found"), SYSCOL_TEXT_INACTIVE);
		}
		else if (bt_uncouple_is_cnv.pressed) {
			uncouple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("convoy not found"), SYSCOL_TEXT_INACTIVE);
		}
		uncouple_target_selector.set_selection(0);
		uncouple_target_selector.disable();
		schedule->entries[schedule->get_current_stop()].target_unique_entry_uncouple = 65535;
		cb_uncouple_target_entry.clear_elements();
		cb_uncouple_target_entry.set_visible(false);
	}
	else {
		couple_target_selector.clear_elements();
		if ( !bt_couple_is_line.pressed  &&  !bt_couple_is_cnv.pressed ) {
			couple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("select_line_or_cnv_first"), SYSCOL_TEXT_INACTIVE);
		}
		else if (bt_couple_is_line.pressed) {
			couple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("line not found"), SYSCOL_TEXT_INACTIVE);
		}
		else if (bt_couple_is_cnv.pressed) {
			couple_target_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("convoy not found"), SYSCOL_TEXT_INACTIVE);
		}
		couple_target_selector.set_selection(0);
		couple_target_selector.disable();
	}
}

void schedule_gui_t::update_uncouple_target_entries(schedule_t* sch)
{
	cb_uncouple_target_entry.clear_elements();
	cb_uncouple_target_entry.set_selection(-1);
	halthandle_t current_halt = haltestelle_t::get_halt(schedule->get_current_entry().pos, player);
	for (uint8 i = 0; i < sch->get_count(); i++) {
		halthandle_t halt = haltestelle_t::get_halt(sch->entries[i].pos, player);

		if (halt.is_bound() && halt == current_halt) {
			cb_uncouple_target_entry.new_component<entry_index_scrollitem_t>(i, sch->entries[i]);
			if (schedule->entries[schedule->get_current_stop()].target_unique_entry_uncouple==65535) {
				schedule->entries[schedule->get_current_stop()].target_unique_entry_uncouple = sch->entries[i].unique_entry_id;
				cb_uncouple_target_entry.set_selection( cb_uncouple_target_entry.count_elements()-1 );
			}
			else if (schedule->entries[schedule->get_current_stop()].target_unique_entry_uncouple == sch->entries[i].unique_entry_id) {
				cb_uncouple_target_entry.set_selection( cb_uncouple_target_entry.count_elements()-1 );
			}
		}
	}
	cb_uncouple_target_entry.set_visible(true);
}

void schedule_gui_t::update_selection()
{
	// UI TODO: update components only when needed

	old_line_count = player->simlinemgmt.get_line_count();
	last_schedule_count = schedule->get_count();

	numimp_load.disable();
	numimp_load.set_value( 0 );
	conditional_depart.set_value(0);
	condition_broadcast.set_value(0);
	bt_wait_prev.disable();
	numimp_spacing.disable();
	numimp_spacing_shift.disable();
	lb_spacing_as_clock.buf().append(translator::translate("off"));
	lb_spacing_as_clock.update();
	lb_plus.set_visible(false);
	lb_entry_pos.set_visible(false);
	sprintf(str_spacing_shift_as_clock, "%s", translator::translate("off") );

	strcpy( str_parts_month, translator::translate("off") );
	bt_wait_next.disable();

	// entry number and halt name
	entry_no->set_visible(false);
	lb_entry_pos.set_visible(false);

	tabs.set_visible(!schedule->empty());
	if (!schedule->empty()) {
		schedule->set_current_stop(min(schedule->get_count() - 1, schedule->get_current_stop()));
		const uint8 current_stop = schedule->get_current_stop();
		const grund_t *gr = welt->lookup(schedule->get_current_entry().pos);
		const bool is_depot = gr ? (welt->lookup(schedule->get_current_entry().pos)->get_depot()!=NULL) : false;
		bt_wait_for_time.enable(is_depot ? false : true); // ??? waypoint ?
		bt_wait_for_time.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::wait_for_time);
		bt_ignore_choose.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::ignore_choose);
		bt_lay_over.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::lay_over);
		bt_range_stop.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::force_range_stop);
		bt_pickup_only.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::pick_up_only);
		bt_setdown_only.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::set_down_only);
		bt_discharge_payload.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::discharge_payload);
		bt_speed_limit.pressed = (schedule->get_current_entry().max_speed_kmh != 65535);
		numimp_speed_limit.set_value(schedule->get_current_entry().max_speed_kmh);
		numimp_speed_limit.enable(bt_speed_limit.pressed);
		lb_speed_limit.set_color( bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
		lb_speed_limit_kmh.set_color(bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
		entry_no->set_visible(true);
		lb_entry_pos.set_visible(true);
		lb_entry_pos.buf().printf("(%s)", schedule->entries[current_stop].pos.get_str());
		lb_entry_pos.update();
		halthandle_t halt = haltestelle_t::get_halt(schedule->entries[current_stop].pos, player);
		if(  halt.is_bound()  ) {
			const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;
			entry_no->init(current_stop, halt->get_owner()->get_player_color1(),
				is_depot ? gui_schedule_entry_number_t::number_style::depot :
				is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt);

			// tab1 componets
			if(!schedule->get_current_entry().is_flag_set(schedule_entry_t::wait_for_time))
			{
				numimp_load.enable();
				numimp_load.set_value( schedule->entries[current_stop].minimum_loading );
				bt_pickup_only.enable();
			}
			else if(!schedule->get_spacing())
			{
				// Cannot have wait for time without some spacing.
				// 12 because the spacing is in 12ths of a fraction of a month.
				schedule->set_spacing(12);
				numimp_spacing.set_value(12);
			}

			if (schedule->entries[current_stop].minimum_loading > 0 || schedule->entries[current_stop].is_flag_set(schedule_entry_t::wait_for_time)) {
				bt_wait_prev.enable();
				bt_setdown_only.disable();
				numimp_spacing.enable();
				numimp_spacing_shift.enable();
				numimp_spacing_shift.set_value(schedule->get_current_entry().spacing_shift);
				if (schedule->get_spacing() ) {
					lb_plus.set_visible(true);
					welt->sprintf_ticks(str_spacing_as_clock, sizeof(str_spacing_as_clock), (welt->ticks_per_world_month * 12u) / schedule->get_spacing());
					welt->sprintf_ticks(str_spacing_shift_as_clock, sizeof(str_spacing_as_clock),
							schedule->entries[current_stop].spacing_shift * welt->ticks_per_world_month / welt->get_settings().get_spacing_shift_divisor() + 1
							);
					lb_spacing_as_clock.buf().clear();
					lb_spacing_as_clock.buf().append(str_spacing_as_clock);
					lb_spacing_as_clock.update();
				}
				bt_wait_next.enable();
			}
			if(  (schedule->entries[current_stop].minimum_loading>0 || schedule->entries[current_stop].is_flag_set(schedule_entry_t::wait_for_time)) &&  schedule->entries[current_stop].waiting_time_shift>0  ) {
				sprintf( str_parts_month, "1/%d",  1<<(16-schedule->entries[current_stop].waiting_time_shift) );
				sint64 ticks_waiting = welt->ticks_per_world_month >> (16-schedule->get_current_entry().waiting_time_shift);
				welt->sprintf_ticks(str_parts_month_as_clock, sizeof(str_parts_month_as_clock), ticks_waiting + 1);
			}
			else {
				strcpy( str_parts_month, translator::translate("off") );
				strcpy( str_parts_month_as_clock, translator::translate("off") );
			}

			lb_waitlevel_as_clock.buf().append(str_parts_month_as_clock);
			lb_waitlevel_as_clock.update();
			bt_setdown_only.enable();
			bt_discharge_payload.enable();

			// tab2 componets
			// If a station is found, tab2 will be hidden, so there is no need to disable these.
			if (tabs.get_count() == 1) tabs.add_tab(&cont_settings_2, translator::translate("entry_setting_2"));
			lb_consist_order_modified.set_visible(schedule->orders.get(schedule->entries[schedule->get_current_stop()].unique_entry_id).get_count());
			bt_lay_over.enable(halt->can_lay_over());
			bt_range_stop.enable();
			if (schedule->get_current_entry().condition_bitfield_receiver > 0)
			{
				conditional_depart.set_value(schedule->get_current_entry().condition_bitfield_receiver);
			}
			if (schedule->get_current_entry().condition_bitfield_broadcaster > 0)
			{
				condition_broadcast.set_value(schedule->get_current_entry().condition_bitfield_broadcaster);
			}

			bt_couple_is_cnv.pressed = false;
			bt_couple_is_line.pressed = false;
			if (schedule->get_current_entry().target_id_couple) {
				bt_couple_is_line.pressed = !schedule->get_current_entry().is_flag_set(schedule_entry_t::couple_target_is_line_or_cnv);
				bt_couple_is_cnv.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::couple_target_is_line_or_cnv);
			}
			bt_uncouple_is_line.pressed = false;
			bt_uncouple_is_cnv.pressed = false;
			if (schedule->get_current_entry().target_id_uncouple) {
				bt_uncouple_is_line.pressed = !schedule->get_current_entry().is_flag_set(schedule_entry_t::uncouple_target_is_line_or_cnv);
				bt_uncouple_is_cnv.pressed = schedule->get_current_entry().is_flag_set(schedule_entry_t::uncouple_target_is_line_or_cnv);
			}

			update_target_line_selection(true, bt_couple_is_line.pressed, bt_uncouple_is_line.pressed);
			update_target_convoy_selection(bt_couple_is_cnv.pressed, bt_uncouple_is_cnv.pressed);
			if (!bt_couple_is_cnv.pressed && !bt_couple_is_line.pressed) {
				disable_couple_target_selector();
			}
			if (!bt_uncouple_is_cnv.pressed && !bt_uncouple_is_line.pressed) {
				disable_couple_target_selector(true);
			}


		}
		else {
			// not a station
			if (is_depot) {
				entry_no->init(current_stop + 1, 0, gui_schedule_entry_number_t::number_style::depot);
			}
			else {
				entry_no->init(old_line.is_bound() ? old_line->get_line_color_index() : 255, player->get_player_nr(), gui_schedule_entry_number_t::number_style::waypoint);
			}

			// tab1 componets
			if (tabs.get_count() > 1) {
				tabs.clear();
				tabs.add_tab(&cont_settings_1, translator::translate("entry_setting_1"));
			}
			bt_discharge_payload.disable();
			bt_setdown_only.disable();
			bt_pickup_only.disable();
			bt_wait_for_time.disable();

			// tab2 componets
			cont_settings_2.set_visible(false);
			bt_lay_over.disable();
			bt_range_stop.disable();
		}
	}

	lb_load.set_color(numimp_load.enabled() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_wait.set_color(bt_wait_prev.enabled() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_shift.set_color(bt_wait_prev.enabled() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_waitlevel_as_clock.set_color(bt_wait_prev.enabled() ? SYSCOL_TEXT_HIGHLIGHT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_spacing.set_color(numimp_spacing.enabled() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_spacing_as_clock.set_color(schedule->get_spacing() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_spacing_shift.set_color(schedule->get_spacing() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
	lb_spacing_shift_as_clock.set_color(schedule->get_spacing() ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
}

/**
 * Mouse clicks are hereby reported to its GUI-Components
 */
bool schedule_gui_t::infowin_event(const event_t *ev)
{
	if(  ev->ev_class == INFOWIN  &&  ev->ev_code == WIN_CLOSE  &&  schedule!=NULL  ) {

		stats->highlight_schedule(schedule, false);

		update_tool( false );
		destroy_win(magic_consist_order); // it may creates data conflicts
		schedule->cleanup();
		schedule->finish_editing();
		// now apply the changes
		if(  cnv.is_bound()  ) {
			// do not send changes if the convoi is about to be deleted
			if(  cnv->get_state() != convoi_t::SELF_DESTRUCT  ) {
				// if a line is selected
				if(  new_line.is_bound()  ) {
					// if the selected line is different to the convoi's line, apply it
					if(  new_line!=cnv->get_line()  ) {
						char id[16];
						sprintf( id, "%i,%i", new_line.get_id(), schedule->get_current_stop() );
						cnv->call_convoi_tool( 'l', id );
					}
					else {
						cbuffer_t buf;
						schedule->sprintf_schedule( buf );
						cnv->call_convoi_tool( 'g', buf );
					}
				}
				else {
					cbuffer_t buf;
					schedule->sprintf_schedule( buf );
					cnv->call_convoi_tool( 'g', buf );
				}

				if(  cnv->in_depot()  ) {
					const grund_t *const ground = welt->lookup( cnv->get_home_depot() );
					if(  ground  ) {
						const depot_t *const depot = ground->get_depot();
						if(  depot  ) {
							depot_frame_t *const frame = dynamic_cast<depot_frame_t *>( win_get_magic( (ptrdiff_t)depot ) );
							if(  frame  ) {
								frame->update_data();
							}
						}
					}
				}
			}
		}
	}
	else if(  ev->ev_class == INFOWIN  &&  (ev->ev_code == WIN_TOP  ||  ev->ev_code == WIN_OPEN)  &&  schedule!=NULL  ) {
		// just to be sure, renew the tools ...
		update_tool( true );
		if(  cnv.is_bound()  ) {
			minimap_t::get_instance()->set_selected_cnv(cnv);
		}
	}
	else if (!line_selector.is_dropped() && ((ev)->ev_code == MOUSE_WHEELUP || (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_UP)) && schedule->entries.get_count()>1) {
		if (schedule->get_current_stop()){
			schedule->set_current_stop(schedule->get_current_stop()-1);
			update_selection();
		}
		return true;
	}
	else if (!line_selector.is_dropped() && ((ev)->ev_code == MOUSE_WHEELDOWN || (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_DOWN)) && schedule->entries.get_count()>1 && schedule->get_current_stop() < schedule->entries.get_count()) {
		schedule->set_current_stop(schedule->get_current_stop()+1);
		update_selection();
		return true;
	}

	return gui_frame_t::infowin_event(ev);
}


bool schedule_gui_t::action_triggered( gui_action_creator_t *comp, value_t p)
{
	if( player!=welt->get_active_player() || welt->get_active_player()->is_locked()) { return true; }
DBG_MESSAGE("schedule_gui_t::action_triggered()","comp=%p combo=%p",comp,&line_selector);
	if(comp == &bt_add) {
		mode = adding;
		bt_add.pressed = true;
		bt_insert.pressed = false;
		update_tool( true );
	}
	else if(comp == &bt_insert) {
		mode = inserting;
		bt_add.pressed = false;
		bt_insert.pressed = true;
		update_tool( true );
	}
	/*else if(comp == &bt_revert) {
		// revert changes and tell listener
	}*/
	else if (comp == &bt_mirror) {
		schedule->set_mirrored(bt_mirror.pressed);
	}
	else if (comp == &bt_bidirectional) {
		schedule->set_bidirectional(bt_bidirectional.pressed);
	}
	else if (comp == &bt_same_spacing_shift) {
		schedule->set_same_spacing_shift(bt_same_spacing_shift.pressed);
		if ( schedule->is_same_spacing_shift() ) {
		    for(  uint8 i=0;  i<schedule->entries.get_count();  i++  ) {
				schedule->entries[i].spacing_shift = schedule->entries[schedule->get_current_stop()].spacing_shift;
			}
		}
	}
	else if (comp == &line_selector) {
// 		uint32 selection = p.i;
//DBG_MESSAGE("schedule_gui_t::action_triggered()","line selection=%i",selection);
		if(  line_scrollitem_t *li = dynamic_cast<line_scrollitem_t*>(line_selector.get_selected_item())  ) {
			new_line = li->get_line();
			stats->set_line_color_index(new_line->get_line_color_index());
			stats->highlight_schedule( schedule, false );
			schedule->copy_from( new_line->get_schedule() );
			schedule->start_editing();
		}
		else {
			// remove line
			new_line = linehandle_t();
			stats->set_line_color_index(254);
			line_selector.set_selection( 0 );
		}
	}
	else if (comp == &filter_btn_all_pas) {
		line_type_flags ^= (1 << simline_t::all_pas);
		filter_btn_all_pas.pressed = line_type_flags & (1 << simline_t::all_pas);
		init_line_selector();
	}
	else if (comp == &filter_btn_all_mails) {
		line_type_flags ^= (1 << simline_t::all_mail);
		filter_btn_all_mails.pressed = line_type_flags & (1 << simline_t::all_mail);
		init_line_selector();
	}
	else if (comp == &filter_btn_all_freights) {
		line_type_flags ^= (1 << simline_t::all_freight);
		filter_btn_all_freights.pressed = line_type_flags & (1 << simline_t::all_freight);
		init_line_selector();
	}
	else if(comp == &bt_promote_to_line) {
		// update line schedule via tool!
		tool_t *tool = create_tool( TOOL_CHANGE_LINE | SIMPLE_TOOL );
		cbuffer_t buf;
		buf.printf( "c,0,%i,%ld,", (int)schedule->get_type(), (long)(uint64)old_schedule );
		schedule->sprintf_schedule( buf );
		tool->set_default_param(buf);
		welt->set_tool( tool, player );
		// since init always returns false, it is safe to delete immediately
		delete tool;
	}
	else if (comp == stats) {
		// click on one of the schedule entries
		const int line = p.i;

		if(  line >= 0 && line < schedule->get_count()  ) {
			schedule->set_current_stop( line );
			if(  mode == removing  ) {
				stats->highlight_schedule( schedule, false );
				schedule->remove();
				action_triggered( &bt_add, value_t() );
			}
			update_selection();
		}
	}
	else if (!schedule->empty()) {
		if (comp == &bt_wait_for_time)
		{
			if (bt_wait_for_time.pressed)
			{
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::wait_for_time);
			}
			else
			{
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::wait_for_time);
			}
			update_selection();
		}
		else if (comp == &numimp_load) {
			schedule->entries[schedule->get_current_stop()].minimum_loading = (uint16)p.i;
			if (schedule->entries[schedule->get_current_stop()].minimum_loading > 0) {
				bt_setdown_only.pressed = false;
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::set_down_only);
			}
			update_selection();
		}
		else if (comp == &bt_pickup_only) {
			if (bt_pickup_only.pressed) {
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::pick_up_only);
				bt_setdown_only.pressed = false;
				bt_discharge_payload.pressed = false;
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::set_down_only);
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::discharge_payload);
			}
			else {
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::pick_up_only);
			}
		}
		else if (comp == &bt_setdown_only) {
			if (bt_setdown_only.pressed) {
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::set_down_only);
				bt_pickup_only.pressed = false;
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::pick_up_only);
			}
			else {
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::set_down_only);
			}
		}
		else if (comp == &bt_discharge_payload) {
			if (bt_discharge_payload.pressed) {
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::discharge_payload);
				bt_pickup_only.pressed = false;
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::pick_up_only);
			}
			else {
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::discharge_payload);
			}
		}
		else if (comp == &bt_wait_prev) {
			sint8& wait = schedule->entries[schedule->get_current_stop()].waiting_time_shift;
			if (wait > 7) {
				wait--;
			}
			else if (wait > 0) {
				wait = 0;
			}
			else {
				wait = 16;
			}
			update_selection();
		}
		else if (comp == &bt_wait_next) {
			sint8& wait = schedule->entries[schedule->get_current_stop()].waiting_time_shift;
			if (wait == 0) {
				wait = 7;
			}
			else if (wait < 16) {
				wait++;
			}
			else {
				wait = 0;
			}
			update_selection();
		}
		else if (comp == &numimp_spacing) {
			schedule->set_spacing(p.i);
			update_selection();
		}
		else if (comp == &numimp_spacing_shift) {
			if (schedule->is_same_spacing_shift()) {
				for (uint8 i = 0; i < schedule->entries.get_count(); i++) {
					schedule->entries[i].spacing_shift = p.i;
				}
			}
			else {
				schedule->entries[schedule->get_current_stop()].spacing_shift = p.i;
			}
			update_selection();
		}
		else if (comp == &bt_speed_limit) {
			bt_speed_limit.pressed = !bt_speed_limit.pressed;
			if (bt_speed_limit.pressed==false) {
				schedule->entries[schedule->get_current_stop()].max_speed_kmh = 65535;
			}
			else {
				// set convoy max speed
				numimp_speed_limit.set_value(get_min_top_speed_kmh());
				schedule->entries[schedule->get_current_stop()].max_speed_kmh = numimp_speed_limit.get_value();
			}
			numimp_speed_limit.enable(bt_speed_limit.pressed);
			lb_speed_limit.set_color(bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
			lb_speed_limit_kmh.set_color(bt_speed_limit.pressed ? SYSCOL_TEXT : SYSCOL_BUTTON_TEXT_DISABLED);
		}
		else if (comp == &numimp_speed_limit) {
			schedule->entries[schedule->get_current_stop()].max_speed_kmh = numimp_speed_limit.get_value();
			if (numimp_speed_limit.get_value()==65535) {
				bt_speed_limit.pressed = false;
			}
			update_current_entry();
		}
		else if (comp == &bt_ignore_choose)
		{
			if (bt_ignore_choose.pressed)
			{
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::ignore_choose);
			}
			else
			{
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::ignore_choose);
			}
		}
		else if (comp == &conditional_depart){
			schedule->entries[schedule->get_current_stop()].condition_bitfield_receiver = conditional_depart.get_value();
		}
		else if (comp == &condition_broadcast) {
			schedule->entries[schedule->get_current_stop()].condition_bitfield_broadcaster = condition_broadcast.get_value();
		}
		else if (comp == &bt_lay_over)
		{
			if (bt_lay_over.pressed)
			{
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::lay_over);
			}
			else
			{
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::lay_over);
			}
		}
		else if (comp == &bt_range_stop) {
			if (bt_range_stop.pressed)
			{
				schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::force_range_stop);
			}
			else
			{
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::force_range_stop);
			}
		}
		else if (comp == &bt_couple_is_line) {
			bt_couple_is_line.pressed = !bt_couple_is_line.pressed;
			schedule->entries[schedule->get_current_stop()].target_id_couple = 0;
			schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::couple_target_is_line_or_cnv);
			if (bt_couple_is_line.pressed) {
				bt_couple_is_cnv.pressed = false;
				update_target_line_selection(false, true, false);
			}
			else {
				disable_couple_target_selector();
			}
		}
		else if (comp == &bt_couple_is_cnv) {
			bt_couple_is_cnv.pressed = !bt_couple_is_cnv.pressed;
			schedule->entries[schedule->get_current_stop()].target_id_couple = 0;
			schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::couple_target_is_line_or_cnv);
			if (bt_couple_is_cnv.pressed) {
				bt_couple_is_line.pressed = false;
				update_target_convoy_selection(true, false);
			}
			else {
				disable_couple_target_selector();
			}
		}
		else if (comp == &bt_uncouple_is_line) {
			bt_uncouple_is_line.pressed = !bt_uncouple_is_line.pressed;
			schedule->entries[schedule->get_current_stop()].target_id_uncouple = 0;
			schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::uncouple_target_is_line_or_cnv);
			if (bt_uncouple_is_line.pressed) {
				bt_uncouple_is_cnv.pressed = false;
				update_target_line_selection(false, false, true);
			}
			else {
				disable_couple_target_selector(true);
			}
		}
		else if (comp == &bt_uncouple_is_cnv) {
			bt_uncouple_is_cnv.pressed = !bt_uncouple_is_cnv.pressed;
			bt_uncouple_is_line.pressed = !bt_uncouple_is_line.pressed;
			schedule->entries[schedule->get_current_stop()].target_id_uncouple = 0;
			if (bt_uncouple_is_cnv.pressed) {
				schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::uncouple_target_is_line_or_cnv);
				bt_uncouple_is_line.pressed = false;
				update_target_convoy_selection(false, true);
			}
			else {
				disable_couple_target_selector(true);
			}
		}
		else if (comp == &condition_line_selector) {
			int selection = p.i;
			if (selection == 0) {
				schedule->entries[schedule->get_current_stop()].target_id_condition_trigger = 0;
				return true;
			}
			else {
				line_scrollitem_t *item = dynamic_cast<line_scrollitem_t*>(condition_line_selector.get_element(selection));
				if (item) {
					schedule->entries[schedule->get_current_stop()].target_id_condition_trigger = item->get_line().get_id();
					return true;
				}
			}
		}
		else if (comp == &couple_target_selector) {
			int selection = p.i;
			if (selection == 0) {
				schedule->entries[schedule->get_current_stop()].target_id_couple = 0;
				return true;
			}
			else {
				if (bt_couple_is_line.pressed) {
					line_scrollitem_t *item = dynamic_cast<line_scrollitem_t*>(couple_target_selector.get_element(selection));
					if (item) {
						schedule->entries[schedule->get_current_stop()].target_id_couple = item->get_line().get_id();
						schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::couple_target_is_line_or_cnv);
						return true;
					}
				}
				else if (bt_couple_is_cnv.pressed) {
					convoy_scrollitem_t *item = (convoy_scrollitem_t*)couple_target_selector.get_element(selection);
					if (item) {
						schedule->entries[schedule->get_current_stop()].target_id_couple = item->get_convoy().get_id();
						schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::couple_target_is_line_or_cnv);
						return true;
					}
				}
			}
		}
		else if (comp == &uncouple_target_selector) {
			int selection = p.i;
			if (selection == 0) {
				schedule->entries[schedule->get_current_stop()].target_id_uncouple = 0;
				cb_uncouple_target_entry.set_visible(false);
				return true;
			}
			else {
				if (bt_uncouple_is_line.pressed) {
					line_scrollitem_t *item = dynamic_cast<line_scrollitem_t*>(uncouple_target_selector.get_element(selection));
					if (item) {
						schedule->entries[schedule->get_current_stop()].target_id_uncouple = item->get_line().get_id();
						schedule->entries[schedule->get_current_stop()].clear_flag(schedule_entry_t::uncouple_target_is_line_or_cnv);
						update_uncouple_target_entries(item->get_line()->get_schedule());
						return true;
					}
				}
				else if (bt_uncouple_is_cnv.pressed) {
					convoy_scrollitem_t *item = (convoy_scrollitem_t*)uncouple_target_selector.get_element(selection);
					if (item) {
						schedule->entries[schedule->get_current_stop()].target_id_uncouple = item->get_convoy().get_id();
						schedule->entries[schedule->get_current_stop()].set_flag(schedule_entry_t::uncouple_target_is_line_or_cnv);
						update_uncouple_target_entries(item->get_convoy()->get_schedule());
						return true;
					}
				}
			}
		}
		else if (comp == &cb_uncouple_target_entry) {
			int selection = p.i;
			entry_index_scrollitem_t *item = dynamic_cast<entry_index_scrollitem_t*>(cb_uncouple_target_entry.get_element(selection));
			if (item) {
				schedule->entries[schedule->get_current_stop()].target_unique_entry_uncouple = item->unique_entry_id;
				return true;
			}
		}
		else if (comp == &bt_consist_order) {
			// Opens new window to alter the consist order
			consist_order_frame_t *win = dynamic_cast<consist_order_frame_t*>(win_get_magic(magic_consist_order));
			if (!win) {
				create_win({ -1, -1 }, new consist_order_frame_t(player, schedule, schedule->entries[schedule->get_current_stop()].unique_entry_id), w_info, magic_consist_order);
			}
			else {
				//win->init(player, schedule, schedule->entries[schedule->get_current_stop()].unique_entry_id);
				//win->init(schedule->entries[schedule->get_current_stop()].unique_entry_id);
			}
			top_win(win);
		}
	}

	// recheck lines
	if(  cnv.is_bound()  ) {
		// unequal to line => remove from line ...
		if(  new_line.is_bound()  &&  !schedule->matches(welt,new_line->get_schedule())  ) {
			new_line = linehandle_t();
			line_selector.set_selection(0);
			stats->set_line_color_index(254);
		}
		// only assign old line, when new_line is not equal
		if(  !new_line.is_bound()  &&  old_line.is_bound()  &&   schedule->matches(welt,old_line->get_schedule())  ) {
			stats->set_line_color_index(old_line->get_line_color_index());
			new_line = old_line;
			init_line_selector();
			min_range = min(new_line->get_min_range() ? new_line->get_min_range() : UINT16_MAX, min_range);
			stats->range_limit = min_range;
			img_electric.set_visible(new_line->needs_electrification());
			img_refuel.set_visible(min_range && min_range != UINT16_MAX);
			if (min_range && min_range!= UINT16_MAX) {
				lb_min_range.buf().printf("%u km", min_range);
				lb_min_range.update();
			}
			lb_min_range.set_visible(min_range && min_range != UINT16_MAX);
		}
		update_selection();
	}
	return true;
}


void schedule_gui_t::init_line_selector()
{
	if( cnv.is_bound() ) {
		line_selector.clear_elements();
		int selection = 0;
		vector_tpl<linehandle_t> lines;

		player->simlinemgmt.get_lines(cnv->get_schedule()->get_type(), &lines, line_type_flags, true);

		// keep assignment with identical schedules
		if (new_line.is_bound() && !cnv->get_schedule()->matches(welt, new_line->get_schedule())) {
			if (old_line.is_bound() && cnv->get_schedule()->matches(welt, old_line->get_schedule())) {
				new_line = old_line;
			}
			else {
				new_line = linehandle_t();
			}
		}
		int offset = 0;
		if (!new_line.is_bound()) {
			selection = 0;
			offset = 1;
			line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("<no line>"), SYSCOL_TEXT);
		}

		FOR(vector_tpl<linehandle_t>, line, lines) {
			line_selector.new_component<line_scrollitem_t>(line);
			if (!new_line.is_bound()) {
				if (cnv->get_schedule()->matches(welt, line->get_schedule())) {
					selection = line_selector.count_elements() - 1;
					new_line = line;
					stats->set_line_color_index(new_line->get_line_color_index());
				}
			}
			else if (new_line == line) {
				selection = line_selector.count_elements() - 1;
			}
		}

		line_selector.set_selection(selection);
		line_scrollitem_t::sort_mode = line_scrollitem_t::SORT_BY_NAME;
		line_selector.sort(offset);
		old_line_count = player->simlinemgmt.get_line_count();

		last_schedule_count = cnv->get_schedule()->get_count();
	}
}



void schedule_gui_t::draw(scr_coord pos, scr_size size)
{
	if (cnv.is_bound()) {
		if (cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS)) {
			filter_btn_all_pas.enable();
		}
		if (cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL)) {
			filter_btn_all_mails.enable();
		}
		for (uint8 catg_index = goods_manager_t::INDEX_NONE + 1; catg_index < goods_manager_t::get_max_catg_index(); catg_index++)
		{
			if (cnv->get_goods_catg_index().is_contained(catg_index)) {
				filter_btn_all_freights.enable();
				break;
			}
		}
	}
	if(  player->simlinemgmt.get_line_count()!=old_line_count  ||  last_schedule_count!=schedule->get_count()  ) {
		// lines added or deleted
		init_line_selector();
		last_schedule_count = schedule->get_count();
		update_selection();
	}

	// after loading in network games, the schedule might still being updated
	if(  cnv.is_bound()  &&  cnv->get_state()==convoi_t::EDIT_SCHEDULE  &&  schedule->is_editing_finished()  ) {
		assert( convoi_t::EDIT_SCHEDULE==1 ); // convoi_t::EDIT_SCHEDULE is 1
		schedule->start_editing();
		cnv->call_convoi_tool( 's', "1" );
	}

	// always dirty, to cater for shortening of halt names and change of selections
	set_dirty();
	resize(scr_coord(0,0));
	gui_frame_t::draw(pos,size);
}


void schedule_gui_t::map_rotate90( sint16 y_size)
{
	schedule->rotate90(y_size);
}


void schedule_gui_t::rdwr(loadsave_t *file)
{
	// this handles only schedules of bound convois
	// lines are handled by line_management_gui_t

	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	// convoy data
	convoi_t::rdwr_convoihandle_t(file, cnv);

	// schedules
	if(  file->is_loading()  ) {
		// dummy types
		old_schedule = new truck_schedule_t();
		schedule = new truck_schedule_t();
	}
	old_schedule->rdwr(file);
	schedule->rdwr(file);

	if(  file->is_loading()  ) {
		if(  cnv.is_bound() ) {
			// now we can open the window ...
			scr_coord const& pos = win_get_pos(this);
			schedule_gui_t *w = new schedule_gui_t( cnv->get_schedule(), cnv->get_owner(), cnv );
			create_win(pos, w, w_info, (ptrdiff_t)cnv->get_schedule());
			w->set_windowsize( size );
			w->schedule->copy_from( schedule );
			cnv->get_schedule()->finish_editing();
			w->schedule->finish_editing();
		}
		else {
			dbg->error( "schedule_gui_t::rdwr", "Could not restore schedule window for (%d)", cnv.get_id() );
		}
		player = NULL;
		delete old_schedule;
		delete schedule;
		schedule = old_schedule = NULL;
		cnv = convoihandle_t();
		destroy_win( this );
	}
}
