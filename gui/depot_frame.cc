/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * The depot window, where to buy convois
 */

#include <stdio.h>
#include <algorithm>

#include "../simunits.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "simwin.h"
#include "../simcolor.h"
#include "../simdebug.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../simline.h"
#include "../simlinemgmt.h"
#include "../simmenu.h"
#include "../simskin.h"

#include "../descriptor/building_desc.h"


#include "schedule_gui.h"
#include "line_management_gui.h"
#include "line_item.h"
#include "convoy_item.h"
#include "messagebox.h"
#include "schedule_list.h"

#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../player/simplay.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simrandom.h"

#include "../bauer/vehikelbauer.h"

#include "../boden/wege/weg.h"

#include "depot_frame.h"
#include "convoi_detail_t.h"


bool depot_frame_t::compare_line(linehandle_t const& a, linehandle_t const& b)
{
	// first: try to sort by line letter code
	const char *alcl = a->get_linecode_l();
	const char *blcl = b->get_linecode_l();
	if (strcmp(alcl, blcl)) {
		return strcmp(alcl, blcl) < 0;
	}
	const char *alcr = a->get_linecode_r();
	const char *blcr = b->get_linecode_r();
	if (strcmp(alcr, blcr)) {
		return strcmp(alcr, blcr) < 0;
	}

	// second: try to sort by number
	const char *atxt = a->get_name();
	int aint = 0;
	// isdigit produces with UTF8 assertions ...
	if (atxt[0] >= '0'  &&  atxt[0] <= '9') {
		aint = atoi(atxt);
	}
	else if (atxt[0] == '('  &&  atxt[1] >= '0'  &&  atxt[1] <= '9') {
		aint = atoi(atxt + 1);
	}
	const char *btxt = b->get_name();
	int bint = 0;
	if (btxt[0] >= '0'  &&  btxt[0] <= '9') {
		bint = atoi(btxt);
	}
	else if (btxt[0] == '('  &&  btxt[1] >= '0'  &&  btxt[1] <= '9') {
		bint = atoi(btxt + 1);
	}
	if (aint != bint) {
		return (aint - bint) < 0;
	}
	// otherwise: sort by name
	return strcmp(atxt, btxt) < 0;

	return false;
}

depot_frame_t::depot_frame_t(depot_t* depot) :
	gui_frame_t("", NULL),
	depot(depot),
	icnv(-1),
	convoy_assembler(this)
{
	old_vehicle_count = 0;

	if (depot) {
		old_vehicle_count = depot->get_vehicle_list().get_count()+1;
		init(depot);
	}
}

void depot_frame_t::init(depot_t *dep)
{
	depot = dep;
	reset_depot_name();
	set_owner(depot->get_owner());
	icnv = depot->convoi_count()-1;

DBG_DEBUG("depot_frame_t::depot_frame_t()","get_max_convoi_length()=%i",depot->get_max_convoi_length());
	last_selected_line = depot->get_last_selected_line();
	no_schedule_text     = translator::translate("<no schedule set>");
	clear_schedule_text  = translator::translate("<clear schedule>");
	unique_schedule_text = translator::translate("<individual schedule>");
	new_line_text        = translator::translate("<create new line>");
	line_separator       = translator::translate("--------------------------------");
	new_convoy_text      = translator::translate("new convoi");
	promote_to_line_text = translator::translate("<promote to line>");

	scr_size size(0,0);
	line_type_flags = 0;

	convoy_assembler.init(depot->get_wegtyp(), depot->get_owner_nr(), check_way_electrified(true));
	init_table();
	set_convoy();

	if(depot->get_traction_types() == 0)
	{
		lb_traction_types.buf().printf("%s", translator::translate("Unpowered vehicles only"));
	}
	else if(depot->get_traction_types() == 65535)
	{
		lb_traction_types.buf().printf("%s", translator::translate("All traction types"));
	}
	else
	{
		uint16 shifter;
		bool first = true;
		for(uint16 i = 0; i < (vehicle_desc_t::MAX_TRACTION_TYPE); i ++)
		{
			shifter = 1 << i;
			if((shifter & depot->get_traction_types()))
			{
				if(first)
				{
					first = false;
					lb_traction_types.buf().clear();
				}
				else
				{
					lb_traction_types.buf().printf(", ");
				}
				lb_traction_types.buf().printf("%s", translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)(i+1)]));
			}
		}
	}
	lb_traction_types.update();

	convoy_assembler.update_convoi();

	line_selector.set_width(convoy_selector.get_size().w);
	line_selector.set_width_fixed(true);
	set_resizemode( diagonal_resize );
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));

	depot->clear_command_pending();
}

void depot_frame_t::init_table()
{
	/*
	 * [SELECT]:
	 */
	set_table_layout(1,0);
	set_margin(scr_size(0,0), scr_size(0,0));

	add_table(1,0)->set_margin(scr_size(D_MARGIN_LEFT, D_MARGIN_TOP), scr_size(D_MARGIN_RIGHT, 0));
		add_table(3,1);
		{
			name_input.set_text(name, sizeof(name));
			name_input.add_listener(this);
			add_component(&name_input);

			// Bolt image for electrified depots:
			img_bolt.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
			img_bolt.set_rigid(true);
			add_component(&img_bolt);
			lb_traction_types.set_color(SYSCOL_TRACTION_TYPE);
			add_component(&lb_traction_types);
		}
		end_table();
		add_table(3,2);
		{
			// text will be translated by ourselves (after update data)!
			add_component(&lb_convois);

			convoy_selector.set_highlight_color(color_idx_to_rgb(depot->get_owner()->get_player_color1() + 1));
			convoy_selector.add_listener(this);
			add_component(&convoy_selector);
			lb_vehicle_count.init(SYSCOL_TEXT,gui_label_t::right);
			add_component(&lb_vehicle_count);

			/*
			 * [SELECT ROUTE]:
			 */
			add_table(2,1);
			{
				// goto line button
				if (skinverwaltung_t::open_window) {
					line_button.init(button_t::imagebox, NULL);
					line_button.set_image(skinverwaltung_t::open_window->get_image_id(0));
				}
				else {
					line_button.set_typ(button_t::arrowright);
				}
				line_button.add_listener(this);
				add_component(&line_button);
				new_component<gui_label_t>("Serves Line:");
			}
			end_table();

			line_selector.add_listener(this);
			line_selector.set_highlight_color( color_idx_to_rgb(depot->get_owner()->get_player_color1() + 1));
			line_selector.set_wrapping(false);
			line_selector.set_focusable(true);
			add_component(&line_selector);

			add_table(3,1)->set_spacing(scr_size(0,0));
			{
				// [freight type filter buttons]
				filter_btn_all_pas.init(button_t::roundbox_state, NULL, scr_coord(0,0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
				filter_btn_all_pas.set_image(skinverwaltung_t::passengers->get_image_id(0));
				filter_btn_all_pas.set_tooltip("filter_pas_line");
				filter_btn_all_pas.add_listener(this);
				add_component(&filter_btn_all_pas);

				filter_btn_all_mails.init(button_t::roundbox_state, NULL, scr_coord(0,0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
				filter_btn_all_mails.set_image(skinverwaltung_t::mail->get_image_id(0));
				filter_btn_all_mails.set_tooltip("filter_mail_line");
				filter_btn_all_mails.add_listener(this);
				add_component(&filter_btn_all_mails);

				filter_btn_all_freights.init(button_t::roundbox_state, NULL, scr_coord(0,0), scr_size(D_BUTTON_HEIGHT, D_BUTTON_HEIGHT));
				filter_btn_all_freights.set_image(skinverwaltung_t::goods->get_image_id(0));
				filter_btn_all_freights.set_tooltip("filter_freight_line");
				filter_btn_all_freights.add_listener(this);
				add_component(&filter_btn_all_freights);
			}
			end_table();
		}
		end_table();

		/*
		* [ACTIONS]
		*/
		add_table(5,1)->set_force_equal_columns(true);
			bt_start.init(button_t::roundbox | button_t::flexible, "Start");
			bt_start.add_listener(this);
			bt_start.set_tooltip("Start the selected vehicle(s)");
			add_component(&bt_start);

			bt_schedule.init(button_t::roundbox | button_t::flexible, "Fahrplan");
			bt_schedule.add_listener(this);
			bt_schedule.set_tooltip("Give the selected vehicle(s) an individual schedule"); // translated to "Edit the selected vehicle(s) individual schedule or assigned line"
			add_component(&bt_schedule);

			bt_copy_convoi.init(button_t::roundbox | button_t::flexible, "Copy Convoi");
			bt_copy_convoi.add_listener(this);
			bt_copy_convoi.set_tooltip("Copy the selected convoi and its schedule or line");
			add_component(&bt_copy_convoi);

			bt_sell.init(button_t::roundbox | button_t::flexible, "verkaufen");
			bt_sell.add_listener(this);
			bt_sell.set_tooltip("Sell the selected vehicle(s)");
			add_component(&bt_sell);

			bt_details.init(button_t::roundbox_state | button_t::flexible, "Details");
			if (skinverwaltung_t::open_window) {
				bt_details.set_image(skinverwaltung_t::open_window->get_image_id(0));
				bt_details.set_image_position_right(true);
			}
			bt_details.add_listener(this);
			bt_details.set_tooltip("Open the convoy detail window");
			bt_details.disable();
			add_component(&bt_details);
		end_table();
	end_table();

	// assembler
	add_component(&convoy_assembler);
}


// returns position of depot on the map
koord3d depot_frame_t::get_weltpos(bool)
{
	return depot->get_pos();
}


bool depot_frame_t::is_weltpos()
{
	return ( welt->get_viewport()->is_on_center( get_weltpos(false) ) );
}


void depot_frame_t::set_windowsize( scr_size size )
{
	convoy_assembler.set_panel_width();
	gui_frame_t::set_windowsize(size);
}


void depot_frame_t::activate_convoi( convoihandle_t c )
{
	icnv = -1;	// deselect
	for(  uint i = 0;  i < depot->convoi_count();  i++  ) {
		if(  c == depot->get_convoi(i)  ) {
			icnv = i;
			break;
		}
	}
	set_convoy();
}

/*
static void get_line_list(const depot_t* depot, vector_tpl<linehandle_t>* lines)
{
	depot->get_owner()->simlinemgmt.get_lines(depot->get_line_type(), lines, line_type_flags, true);
}
*/

/*
* Reset convoy count
*/
void depot_frame_t::update_data()
{
	cbuffer_t &txt_convois = lb_convois.buf();
	switch(depot->convoi_count()) {
		case 0: {
			txt_convois.printf( translator::translate("no convois") );
			break;
		}

		case 1: {
			if(  icnv == -1  ) {
				txt_convois.append( translator::translate("1 convoi") );
			}
			else {
				txt_convois.printf( translator::translate("convoi %d of %d"), icnv + 1, depot->convoi_count() );
			}
			break;
		}

		default: {
			if(  icnv == -1  ) {
				txt_convois.printf( translator::translate("%d convois"), depot->convoi_count() );
			}
			else {
				txt_convois.printf( translator::translate("convoi %d of %d"), icnv + 1, depot->convoi_count() );
			}
			break;
		}
	}
	lb_convois.update();

	/*
	* Reset counts and check for valid vehicles
	*/
	convoihandle_t cnv = depot->get_convoi( icnv );

	// update convoy selector
	convoy_selector.clear_elements();
	convoy_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( new_convoy_text, SYSCOL_TEXT ) ;
	convoy_selector.set_selection(0);

	// check all matching convoys
	FOR(slist_tpl<convoihandle_t>, const c, depot->get_convoy_list()) {
		convoy_selector.new_component<convoy_scrollitem_t>(c) ;
		if(  cnv.is_bound()  &&  c == cnv  ) {
			convoy_selector.set_selection( convoy_selector.count_elements() - 1 );
		}
	}

	// update the line selector
	build_line_list();
	set_resale_value();

	reset_min_windowsize();
	set_windowsize(scr_size(max(get_min_windowsize().w, get_windowsize().w), max(get_min_windowsize().h, get_windowsize().h)));
	resize(scr_size(0,0));
}

void depot_frame_t::build_line_list()
{
	convoihandle_t cnv = depot->get_convoi(icnv);
	line_selector.clear_elements();
	int extra_option = 0;
	// check all matching lines
	if (cnv.is_bound()) {
		selected_line = cnv->get_line();
	}
	if(  !last_selected_line.is_bound()  ) {
		// new line may have a valid line now
		last_selected_line = selected_line;
		// if still nothing, resort to line management dialoge
		if(  !last_selected_line.is_bound()  ) {
			// try last specific line
			last_selected_line = schedule_list_gui_t::selected_line[ depot->get_owner()->get_player_nr() ][ depot->get_line_type() ];
		}
		if(  !last_selected_line.is_bound()  ) {
			// try last general line
			last_selected_line = schedule_list_gui_t::selected_line[ depot->get_owner()->get_player_nr() ][ 0 ];
			if(  last_selected_line.is_bound()  &&  last_selected_line->get_linetype() != depot->get_line_type()  ) {
				last_selected_line = linehandle_t();
			}
		}
	}
	if(  cnv.is_bound()  &&  cnv->get_schedule()  &&  !cnv->get_schedule()->empty()  ) {
		if (cnv->get_line().is_bound()) {
			line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( clear_schedule_text, SYSCOL_TEXT ) ;
			line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( new_line_text, SYSCOL_TEXT ) ;
			extra_option += 2;
		}
		else {
			line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( unique_schedule_text, SYSCOL_TEXT ) ;
			line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( promote_to_line_text, SYSCOL_TEXT ) ;
			extra_option += 2;
		}
	}
	else {
		line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( no_schedule_text, SYSCOL_TEXT ) ;
		line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( new_line_text, SYSCOL_TEXT ) ;
		extra_option += 2;
	}
	// select "create new schedule" for default
	line_selector.set_selection( 0 );
	if( last_selected_line.is_bound() ) {
		line_selector.new_component<line_scrollitem_t>( last_selected_line ) ;
		extra_option++;
		if (selected_line == last_selected_line) {
			line_selector.set_selection(line_selector.count_elements() - 1);
		}
	}
	if (selected_line.is_bound() && (selected_line != last_selected_line)) {
		// Places the currently selected line for safety.
		line_selector.new_component<line_scrollitem_t>(selected_line);
		line_selector.set_selection(line_selector.count_elements() - 1);
		extra_option++;
	}
	line_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( line_separator, SYSCOL_TEXT ) ;
	extra_option++;

	vector_tpl<linehandle_t> lines;
	depot->get_owner()->simlinemgmt.get_lines(depot->get_line_type(), &lines, line_type_flags, true);
	std::sort(lines.begin(), lines.end(), compare_line);
	FOR(  vector_tpl<linehandle_t>,  const line,  lines  ) {
		line_selector.new_component<line_scrollitem_t>(line) ;
		if(  selected_line.is_bound()  &&  selected_line == line  ) {
			line_selector.set_selection( line_selector.count_elements() - 1 );
		}
	}
	if(  line_selector.get_selection() == 0  ) {
		// no line selected
		selected_line = linehandle_t();
	}
}


void depot_frame_t::rename_depot()
{
	const char *t = name_input.get_text();
	// only change if old name and current name are the same
	// otherwise some unintended undo if renaming would occur
	if(  t  &&  t[0]  &&  strcmp(t, depot->get_name())  &&  strcmp(old_name, depot->get_name())==0) {
		// text changed => call tool
		cbuffer_t buf;
		buf.printf("d%s,%s", depot->get_pos().get_str(), t);
		tool_t *tool = create_tool(TOOL_RENAME | SIMPLE_TOOL);
		tool->set_default_param(buf);
		welt->set_tool(tool, depot->get_owner());
		// since init always returns false, it is safe to delete immediately
		delete tool;
		// do not trigger this command again
		tstrncpy(old_name, depot->get_name(), sizeof(old_name));
	}
}

void depot_frame_t::reset_depot_name()
{
	tstrncpy(old_name, depot->get_name(), sizeof(old_name));
	tstrncpy(name, depot->get_name(), sizeof(name));
	set_name(translator::translate(depot->get_name()));
}



sint64 depot_frame_t::calc_sale_value(const vehicle_desc_t *veh_type)
{
	sint64 wert = 0;
	FOR(slist_tpl<vehicle_t*>, const v, depot->get_vehicle_list()) {
		if(  v->get_desc() == veh_type  ) {
			wert += v->calc_sale_value();
		}
	}
	return wert;
}


void depot_frame_t::set_resale_value(uint32 nominal_cost, sint64 resale_value)
{
	if (nominal_cost == resale_value) {
		bt_sell.set_text(translator::translate("Refund"));
		bt_sell.set_tooltip("Return the vehicle(s) for a full refund.");
	}
	else if (resale_value == 0) {
		bt_sell.set_text(translator::translate("Scrap"));
		bt_sell.set_tooltip("Scrap all vehicles in the convoy.");
	}
	else {
		bt_sell.set_text(translator::translate("verkaufen"));
		txt_convoi_cost.clear();
		char buf[128];
		money_to_string(buf, resale_value/100.0);
		txt_convoi_cost.printf(translator::translate("Sell the convoy for %s"), buf);
		bt_sell.set_tooltip(txt_convoi_cost);
	}
	return;
}


bool depot_frame_t::action_triggered( gui_action_creator_t *comp, value_t p)
{
	convoihandle_t cnv = depot->get_convoi( icnv );

	if(  depot->is_command_pending()  ) {
		// block new commands until last command is executed
		return true;
	}

	if(  comp != NULL  ) {	// message from outside!
		if(  comp == &bt_start  ) {
			if(  cnv.is_bound()  ) {
				//first: close schedule (will update schedule on clients)
				destroy_win( (ptrdiff_t)cnv->get_schedule() );
				// only then call the tool to start
				char tool = event_get_last_control_shift() == 2 ? 'B' : 'b'; // start all with CTRL-click
				depot->call_depot_tool( tool, cnv, NULL);
				set_convoy();
			}
			return true;
		} else if(comp == &bt_schedule) {
			if(  line_selector.get_selection() == 1  &&  !line_selector.is_dropped()  ) { // create new line
				// promote existing individual schedule to line
				cbuffer_t buf;
				if(  cnv.is_bound()  &&  !selected_line.is_bound()  ) {
					schedule_t* schedule = cnv->get_schedule();
					if(  schedule  ) {
						schedule->sprintf_schedule( buf );
					}
				}
				depot->call_depot_tool('l', convoihandle_t(), buf);
				return true;
			}
			else {
				open_schedule_editor();
				return true;
			}
		} else if(comp == &bt_sell) {
			if(  cnv.is_bound()  ) {
				depot->call_depot_tool( 'v', cnv, NULL );
				icnv = min(icnv, depot->convoi_count()-2);
			}
			return true;
		}
		else if(  comp == &line_button  ) {
			if(  cnv.is_bound()  ) {
				cnv->get_owner()->simlinemgmt.show_lineinfo( cnv->get_owner(), cnv->get_line() );
				welt->set_dirty();
			}
			return true;
		}
		else if (comp == &bt_details) {
			create_win(20, 20, new convoi_detail_t(cnv), w_info, magic_convoi_detail + cnv.get_id());
			return true;
		}
		else if(  comp == &bt_copy_convoi  )
		{
			if(  cnv.is_bound() && cnv->all_vehicles_are_buildable())
			{
				if (cnv->get_schedule() && (!cnv->get_schedule()->is_editing_finished()))
				{
					create_win(new news_img("Schedule is incomplete/not finished"), w_time_delete, magic_none);
				}
				else if(  !welt->use_timeline()  ||  welt->get_settings().get_allow_buying_obsolete_vehicles()  ||  depot->check_obsolete_inventory( cnv )  )
				{
					depot->call_depot_tool('c', cnv, NULL, cnv->get_livery_scheme_index());
					update_data();
				}
				else
				{
					create_win( new news_img("Can't buy obsolete vehicles!"), w_time_delete, magic_none );
				}
			}
			return true;
		}
		else if(  comp == &convoy_selector  ) {
			icnv = p.i - 1;
			convoy_assembler.set_vehicles(depot->get_convoi(icnv));
			convoy_assembler.update_convoi();
			update_data();
			return true;
		}
		else if(  comp == &line_selector  ) {
			int selection = p.i;
			if(  selection == 0  ) { // unique
				if(  selected_line.is_bound()  ) {
					selected_line = linehandle_t();
					apply_line();
				}
				// HACK mark line_selector temporarily un-focusable.
				// We call set_focus(NULL) later if we can.
				// Calling set_focus(NULL) now would have no effect due to logic in gui_container_t::infowin_event.
				line_selector.set_focusable( false );
				return true;
			}
			else if(  selection == 1  ) { // create new line
				if(  line_selector.is_dropped()  ) { // but not from next/prev buttons
					// promote existing individual schedule to line
					cbuffer_t buf;
					if(  cnv.is_bound()  &&  !selected_line.is_bound()  ) {
						schedule_t* schedule = cnv->get_schedule();
						if(  schedule  ) {
							schedule->sprintf_schedule( buf );
						}
					}
					line_selector.set_focusable( false );
					last_selected_line = linehandle_t();	// clear last selected line so we can get a new one ...
					depot->call_depot_tool('l', cnv, buf);

				}
				return true;
			}
			if(  last_selected_line.is_bound()  ) {
				if(  selection == 2  ) { // last selected line
					selected_line = last_selected_line;
					apply_line();
					convoy_assembler.check_livery_scheme_index(last_selected_line->get_livery_scheme_index());
					return true;
				}
			}

			// access the selected element to get selected line
			line_scrollitem_t *item = dynamic_cast<line_scrollitem_t*>(line_selector.get_element(selection));
			if(  item  ) {
				selected_line = item->get_line();
				depot->set_last_selected_line( selected_line );
				last_selected_line = selected_line;
				apply_line();
				convoy_assembler.check_livery_scheme_index(last_selected_line->get_livery_scheme_index());
				return true;
			}
			line_selector.set_focusable( false );
		}
		else if (comp == &filter_btn_all_pas) {
			line_type_flags ^= (1 << simline_t::all_pas);
			filter_btn_all_pas.pressed = line_type_flags & (1 << simline_t::all_pas);
			build_line_list();
			return true;
		}
		else if (comp == &filter_btn_all_mails) {
			line_type_flags ^= (1 << simline_t::all_mail);
			filter_btn_all_mails.pressed = line_type_flags & (1 << simline_t::all_mail);
			build_line_list();
			return true;
		}
		else if (comp == &filter_btn_all_freights) {
			line_type_flags ^= (1 << simline_t::all_freight);
			filter_btn_all_freights.pressed = line_type_flags & (1 << simline_t::all_freight);
			build_line_list();
			return true;
		}
		else if (comp == &name_input) {
			// send rename command if necessary
			rename_depot();
		}
		else {
			return false;
		}
	}
	return true;
}


bool depot_frame_t::infowin_event(const event_t *ev)
{
	// enable disable button actions
	if(  depot == NULL  ||  (ev->ev_class < INFOWIN  &&  welt->get_active_player() != depot->get_owner()) ) {
		return false;
	}

	const bool swallowed = gui_frame_t::infowin_event(ev);

	// HACK make line_selector focusable again
	// now we can release focus
	if(  !line_selector.is_focusable( ) ) {
		line_selector.set_focusable( true );
		set_focus(NULL);
	}

	if(IS_WINDOW_CHOOSE_NEXT(ev)) {

		bool dir = (ev->ev_code==NEXT_WINDOW);
		depot_t *next_dep = depot_t::find_depot( depot->get_pos(), depot->get_typ(), depot->get_owner(), dir == NEXT_WINDOW );
		if(next_dep == NULL) {
			if(dir == NEXT_WINDOW) {
				// check the next from start of map
				next_dep = depot_t::find_depot( koord3d(-1,-1,0), depot->get_typ(), depot->get_owner(), true );
			}
			else {
				// respective end of map
				next_dep = depot_t::find_depot( koord3d(8192,8192,127), depot->get_typ(), depot->get_owner(), false );
			}
		}

		if(next_dep  &&  next_dep!=this->depot) {
			//  Replace our depot_frame_t with a new at the same position.
			scr_coord const pos = win_get_pos(this);
			destroy_win( this );

			next_dep->show_info();
			win_set_pos(win_get_magic((ptrdiff_t)next_dep), pos.x, pos.y);
			welt->get_viewport()->change_world_position(next_dep->get_pos());
		}
		else {
			// recenter on current depot
			welt->get_viewport()->change_world_position(depot->get_pos());
		}

		return true;
	} else if(ev->ev_class == INFOWIN && ev->ev_code == WIN_OPEN) {
		convoy_assembler.update_convoi();
	}
	if(0) {
		if(IS_LEFTCLICK(ev)  ) {
			if(  !convoy_selector.getroffen(ev->cx, ev->cy-D_TITLEBAR_HEIGHT)  &&  convoy_selector.is_dropped()  ) {
				// close combo box; we must do it ourselves, since the box does not receive outside events ...
				convoy_selector.close_box();
			}
			if(  line_selector.is_dropped()  &&  !line_selector.getroffen(ev->cx, ev->cy-D_TITLEBAR_HEIGHT)  ) {
				// close combo box; we must do it ourselves, since the box does not receive outside events ...
				line_selector.close_box();
				if(  line_selector.get_selection() < last_selected_line.is_bound()+3  &&  get_focus() == &line_selector  ) {
					set_focus( NULL );
				}
			}
			//if(  !vehicle_filter.is_hit(ev->cx, ev->cy-D_TITLEBAR_HEIGHT)  &&  vehicle_filter.is_dropped()  ) {
			//	// close combo box; we must do it ourselves, since the box does not receive outside events ...
			//	vehicle_filter.close_box();
			//}
		}
	}

	//if(  get_focus() == &vehicle_filter  &&  !vehicle_filter.is_dropped()  ) {
	//	set_focus( NULL );
	//}

	return swallowed;
}


void depot_frame_t::draw(scr_coord pos, scr_size size)
{
	const bool action_allowed = welt->get_active_player() == depot->get_owner();

	// enable disable button actions
	bt_copy_convoi.enable( action_allowed );
	bt_start.enable( action_allowed );
	bt_schedule.enable( action_allowed );
	bt_sell.enable( action_allowed );

	convoihandle_t cnv = depot->get_convoi(icnv);
	line_button.enable( action_allowed && cnv.is_bound() && cnv->get_line().is_bound() );
	bt_details.enable( action_allowed && cnv.is_bound() );
	if( cnv.is_bound() ) {
		bt_details.pressed = win_get_magic(magic_convoi_detail + cnv.get_id());
	}

	// number of convoys
	if (old_vehicle_count != depot->get_vehicle_list().get_count()) {
		const uint32 count = depot->get_vehicle_list().get_count();
		switch (count) {
			case 0: {
				lb_vehicle_count.buf().append(translator::translate("Keine Einzelfahrzeuge im Depot"));
				break;
			}
			case 1: {
				lb_vehicle_count.buf().append(translator::translate("1 Einzelfahrzeug im Depot"));
				break;
			}
			default: {
				lb_vehicle_count.buf().printf(translator::translate("%d Einzelfahrzeuge im Depot"), count);
				break;
			}
		}
		old_vehicle_count = count;
		lb_vehicle_count.update();
	}

	// check for data inconsistencies (can happen with withdraw-all and vehicle in depot)
	const vector_tpl<gui_image_list_t::image_data_t*>* convoi_pics = convoy_assembler.get_convoi_pics();
	if(  !cnv.is_bound()  &&  !convoi_pics->empty()  ) {
		icnv=0;
		convoy_assembler.update_convoi();
		cnv = depot->get_convoi(icnv);
	}

	gui_frame_t::draw(pos, size);
}

void depot_frame_t::apply_line()
{
	if(  icnv > -1  ) {
		convoihandle_t cnv = depot->get_convoi( icnv );
		// if no convoi is selected, do nothing
		if(  !cnv.is_bound()  ) {
			return;
		}

		if(  selected_line.is_bound()  ) {
			// set new route only, a valid route is selected:
			char id[16];
			sprintf( id, "%i", selected_line.get_id() );
			cnv->call_convoi_tool('l', id );
		}
		else {
			// sometimes the user might wish to remove convoy from line
			// => we clear the schedule completely
			schedule_t *dummy = cnv->create_schedule()->copy();
			dummy->entries.clear();

			cbuffer_t buf;
			dummy->sprintf_schedule(buf);
			cnv->call_convoi_tool('g', (const char*)buf );

			delete dummy;
		}
	}
}


void depot_frame_t::set_convoy()
{
	convoy_assembler.set_vehicles(depot->get_convoi(icnv));
	update_data();
}


void depot_frame_t::open_schedule_editor()
{
	convoihandle_t cnv = depot->get_convoi( icnv );

	if(  cnv.is_bound()  &&  cnv->get_vehicle_count() > 0  ) {
		if(  selected_line.is_bound()  &&  event_get_last_control_shift() == 2  ) { // update line with CTRL-click
			create_win( new line_management_gui_t( selected_line, depot->get_owner() ), w_info, (ptrdiff_t)selected_line.get_rep() );
		}
		else { // edit individual schedule
			cnv->call_convoi_tool( 'f', NULL );
		}
	}
	else {
		create_win( new news_img("Please choose vehicles first\n"), w_time_delete, magic_none );
	}
}

bool depot_frame_t::check_way_electrified(bool init)
{
	const waytype_t wt = depot->get_wegtyp();
	const weg_t *w = welt->lookup(depot->get_pos())->get_weg(wt!=tram_wt ? wt : track_wt);
	const bool way_electrified = w ? w->is_electrified() : false;
	if(!init)
	{
		convoy_assembler.set_electrified( way_electrified );
	}
	img_bolt.set_visible(way_electrified);

	return way_electrified;
}


uint32 depot_frame_t::get_rdwr_id()
{
	return magic_depot;
}

void depot_frame_t::rdwr(loadsave_t *file)
{
	// depot position
	koord3d pos;
	if(  file->is_saving()  ) {
		pos = depot->get_pos();
	}
	pos.rdwr( file );
	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	file->rdwr_byte(convoy_assembler.veh_action);
	file->rdwr_short(convoy_assembler.sort_by_action);
	//file->rdwr_long(convoy_assembler.selected_filter); // not working
	file->rdwr_bool(convoy_assembler.show_all);
	file->rdwr_bool(convoy_assembler.show_outdated_vehicles);
	file->rdwr_bool(convoy_assembler.show_obsolete_vehicles);
	file->rdwr_bool(convoy_assembler.sort_reverse);

	int current_tab_index = convoy_assembler.get_current_tab_index();
	file->rdwr_long(current_tab_index);

	if(  file->is_loading()  ) {
		depot_t *dep = welt->lookup(pos)->get_depot();
		if (dep) {
			init(dep);
		}
	}
	file->rdwr_long(icnv);
	simline_t::rdwr_linehandle_t(file, selected_line);

	if(  depot  &&  file->is_loading()  ) {
		reset_min_windowsize();
		set_windowsize(size);

		win_set_magic(this, (ptrdiff_t)depot);
		convoy_assembler.set_current_tab_index(current_tab_index);
	}

	if (depot == NULL) {
		destroy_win(this);
	}
}
