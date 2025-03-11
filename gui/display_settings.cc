/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "display_settings.h"
#include "../simdebug.h"
#include "../simworld.h"
#include "../display/simimg.h"
#include "../simintr.h"
#include "../simcolor.h"
#include "../dataobj/settings.h"
#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../obj/baum.h"
#include "../obj/zeiger.h"
#include "../display/simgraph.h"
#include "../simmenu.h"
#include "../player/simplay.h"
#include "../utils/simstring.h"
#include "../sys/simsys.h"

#include "gui_theme.h"
#include "themeselector.h"
#include "loadfont_frame.h"
#include "simwin.h"

#include "../path_explorer.h"
#include "components/gui_image.h"

// display text label in player colors
void display_text_label(sint16 xpos, sint16 ypos, const char* text, const player_t *player, bool dirty); // grund.cc

enum {
	IDBTN_SCROLL_INVERSE,
	IDBTN_IGNORE_NUMLOCK,
	IDBTN_PEDESTRIANS_AT_STOPS,
	IDBTN_PEDESTRIANS_IN_TOWNS,
	IDBTN_DAY_NIGHT_CHANGE,
	IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN,
	IDBTN_HIDE_TREES,
	IDBTN_TRANSPARENT_STATION_COVERAGE,
	IDBTN_SHOW_STATION_COVERAGE,
	IDBTN_UNDERGROUND_VIEW,
	IDBTN_SHOW_GRID,
	IDBTN_HIDE_LABELS,
	IDBTN_LABEL_STYLE_ARROW,
	IDBTN_SHOW_WAITING_BARS,
	IDBTN_SHOW_SLICE_MAP_VIEW,
	IDBTN_HIDE_BUILDINGS,
	IDBTN_SHOW_SCHEDULES_STOP,
	IDBTN_SHOW_THEMEMANAGER,
	IDBTN_SIMPLE_DRAWING,
	IDBTN_CHANGE_FONT,
	IDBTN_LEFT_TO_RIGHT_GRAPHS,
	IDBTN_SHOW_SIGNALBOX_COVERAGE,
	IDBTN_CLASSES_WAITING_BAR,
	IDBTN_SHOW_DEPOT_NAME,
	IDBTN_SHOW_FACTORY_STORAGE,
	IDBTN_INFINITE_SCROLL,
	COLORS_MAX_BUTTONS,
};

static button_t buttons[COLORS_MAX_BUTTONS];

/**
* class to visualize station names
*/
class gui_label_stationname_t : public gui_label_t
{
	karte_ptr_t welt;
public:
	gui_label_stationname_t(const char* text) : gui_label_t(text) {}

	void draw(scr_coord offset) OVERRIDE
	{
		scr_coord p = pos + offset;

		const player_t* player = welt->get_active_player();
		const char *text = get_text_pointer();

		display_text_label(p.x, p.y + get_size().h/2, text, player, true);
	}

	scr_size get_min_size() const OVERRIDE
	{
		return gui_label_t::get_min_size() + scr_size(LINESPACE + D_H_SPACE, 4);
	}
};


gui_settings_t::gui_settings_t()
{
	set_table_layout( 1, 0 );
	buttons[ IDBTN_LEFT_TO_RIGHT_GRAPHS ].init(button_t::square_state, "Inverse graphs");
	buttons[ IDBTN_LEFT_TO_RIGHT_GRAPHS ].pressed = !env_t::left_to_right_graphs;
	buttons[ IDBTN_LEFT_TO_RIGHT_GRAPHS ].set_tooltip("Graphs right to left instead of left to right");
	add_component( buttons + IDBTN_LEFT_TO_RIGHT_GRAPHS );

	// Show thememanager
	buttons[ IDBTN_SHOW_THEMEMANAGER ].init( button_t::roundbox_state | button_t::flexible, "Select a theme for display" );
	if (skinverwaltung_t::open_window) {
		buttons[ IDBTN_SHOW_THEMEMANAGER ].set_image(skinverwaltung_t::open_window->get_image_id(0));
		buttons[ IDBTN_SHOW_THEMEMANAGER ].set_image_position_right(true);
	}
	add_component( buttons + IDBTN_SHOW_THEMEMANAGER );

	// Change font
	buttons[ IDBTN_CHANGE_FONT ].init( button_t::roundbox_state | button_t::flexible, "Select display font" );
	if (skinverwaltung_t::open_window) {
		buttons[ IDBTN_CHANGE_FONT ].set_image(skinverwaltung_t::open_window->get_image_id(0));
		buttons[ IDBTN_CHANGE_FONT ].set_image_position_right(true);
	}
	add_component( buttons + IDBTN_CHANGE_FONT );

	// screen scale number input
	add_table(3,1)->set_spacing(scr_size(0,0));
	{
		new_component<gui_label_t>("Screen scale:");

		add_table(2,0);
		{
			screen_scale_numinp.init(dr_get_screen_scale(), 25, 400, 25, false);
			screen_scale_numinp.add_listener(this);
			add_component(&screen_scale_numinp);

			screen_scale_auto.init(button_t::roundbox_state, "Auto");
			screen_scale_auto.add_listener(this);
			add_component(&screen_scale_auto);
		}
		end_table();

		new_component<gui_fill_t>();
	}
	end_table();

	// position of menu
	add_table(5,1)->set_spacing(scr_size(0,0));
	{
		new_component<gui_label_t>("Toolbar position:");
		new_component<gui_margin_t>(D_H_SPACE);
		static char const* const positions[] = { "Left", "Top", "Right", "Bottom" };
		for (uint8 i=0; i<4; i++) {
			toolbar_pos[i].pressed = (env_t::menupos == i);
			toolbar_pos[i].init( button_t::roundbox_state, positions[i], scr_coord(0,0), scr_size(D_BUTTON_WIDTH*0.8, D_BUTTON_HEIGHT) );
		}

		add_component( &toolbar_pos[MENU_LEFT] );
		add_table(1,2);
		{
			add_component( &toolbar_pos[MENU_TOP] );
			add_component( &toolbar_pos[MENU_BOTTOM] );
		}
		end_table();
		add_component( &toolbar_pos[MENU_RIGHT] );
	}
	end_table();

	reselect_closes_tool.init( button_t::square_state, "Reselect closes tools" );
	reselect_closes_tool.pressed = env_t::reselect_closes_tool;
	add_component( &reselect_closes_tool, 2 );

	fullscreen.init( button_t::square_state, "Fullscreen (changed after restart)" );
	fullscreen.pressed = ( dr_get_fullscreen() == FULLSCREEN );
	add_component( &fullscreen, 2 );

	borderless.init( button_t::square_state, "Borderless (disabled on fullscreen)" );
	borderless.enable ( dr_get_fullscreen() != FULLSCREEN );
	borderless.pressed = ( dr_get_fullscreen() == BORDERLESS );
	add_component( &borderless, 2 );

	new_component<gui_divider_t>();

	// window pop-up controll
	new_component<gui_label_t>("Information windows pop up control:");
	add_table(5,0)->set_spacing(scr_size(0,D_V_SPACE));
	{
		for (uint8 i=0; i<4; i++) {
			info_window_toggler[i*2].init(  button_t::roundbox_left_state,  "hl_btn_filter_enable",  scr_coord(0,0), D_BUTTON_SIZE );
			info_window_toggler[i*2+1].init(button_t::roundbox_right_state, "hl_btn_filter_disable", scr_coord(0,0), D_BUTTON_SIZE );
		}

		new_component<gui_margin_t>(LINESPACE);
		new_component<gui_label_t>("ground_info")->set_tooltip(translator::translate("helptxt_ground_info_toggler"));
		new_component<gui_margin_t>(D_H_SPACE);
		info_window_toggler[0].pressed =  env_t::ground_info;
		info_window_toggler[1].pressed = !env_t::ground_info;
		add_component(&info_window_toggler[0]);
		add_component(&info_window_toggler[1]);

		// tree, rock, birds
		new_component<gui_margin_t>(LINESPACE);
		new_component<gui_label_t>("tree_info")->set_tooltip(translator::translate("helptxt_tree_info_toggler"));
		new_component<gui_margin_t>(D_H_SPACE);
		info_window_toggler[2].pressed =  env_t::tree_info;
		info_window_toggler[3].pressed = !env_t::tree_info;
		add_component(&info_window_toggler[2]);
		add_component(&info_window_toggler[3]);

		new_component<gui_margin_t>(LINESPACE);
		new_component<gui_label_t>("Private car")->set_tooltip(translator::translate("can open a dedicated window by clicking on a private car."));
		new_component<gui_margin_t>(D_H_SPACE);
		info_window_toggler[4].pressed = env_t::road_user_info & 1;
		info_window_toggler[5].pressed = !info_window_toggler[4].pressed;
		add_component(&info_window_toggler[4]);
		add_component(&info_window_toggler[5]);

		new_component<gui_margin_t>(LINESPACE);
		new_component<gui_label_t>("Fussgaenger")->set_tooltip(translator::translate("can open a dedicated window by clicking on a pedestrian."));
		new_component<gui_margin_t>(D_H_SPACE);
		info_window_toggler[6].pressed = env_t::road_user_info & 2;
		info_window_toggler[7].pressed = !info_window_toggler[6].pressed;
		add_component(&info_window_toggler[6]);
		add_component(&info_window_toggler[7]);
	}
	end_table();

	new_component<gui_divider_t>();

	// add controls to info container
	add_table(2,0);
	{
		set_alignment(ALIGN_LEFT);

		// Frame time label
		new_component<gui_label_t>("Frame time:");
		frame_time_value_label.buf().printf(" 9999 ms");
		frame_time_value_label.update();
		add_component(&frame_time_value_label);
		// Idle time label
		new_component<gui_label_t>("Idle:");
		idle_time_value_label.buf().printf(" 9999 ms");
		idle_time_value_label.update();
		add_component(&idle_time_value_label);
		// FPS label
		new_component<gui_label_t>("FPS:");
		fps_value_label.buf().printf(" 99.9 fps");
		fps_value_label.update();
		add_component(&fps_value_label);
		// Simloops label
		new_component<gui_label_t>("Sim:");
		simloops_value_label.buf().printf(" 999.9");
		simloops_value_label.update();
		add_component(&simloops_value_label);
	}
	end_table();

	new_component<gui_divider_t>();

	add_table(2, 0);
	{
		set_alignment(ALIGN_LEFT);
		new_component<gui_label_t>("Rebuild connexions:");
		rebuild_connexion_label.buf().printf("-");
		rebuild_connexion_label.set_color(SYSCOL_TEXT_TITLE);
		rebuild_connexion_label.update();
		add_component(&rebuild_connexion_label);

		new_component<gui_label_t>("Filter eligible halts:");
		eligible_halts_label.buf().printf("-");
		eligible_halts_label.set_color(SYSCOL_TEXT_TITLE);
		eligible_halts_label.update();
		add_component(&eligible_halts_label);

		new_component<gui_label_t>("Fill path matrix:");
		fill_path_matrix_label.buf().printf("-");
		fill_path_matrix_label.set_color(SYSCOL_TEXT_TITLE);
		fill_path_matrix_label.update();
		add_component(&fill_path_matrix_label);

		new_component<gui_label_t>("Explore paths:");
		explore_path_label.buf().printf("-");
		explore_path_label.set_color(SYSCOL_TEXT_TITLE);
		explore_path_label.update();
		add_component(&explore_path_label);

		new_component<gui_label_t>("Re-route goods:");
		reroute_goods_label.buf().printf("-");
		reroute_goods_label.set_color(SYSCOL_TEXT_TITLE);
		reroute_goods_label.update();
		add_component(&reroute_goods_label);

		new_component<gui_label_t>("Status:");
		status_label.buf().printf("stand-by");
		status_label.set_color(SYSCOL_TEXT_TITLE);
		status_label.update();
		add_component(&status_label);

		// Private cars hereafter

		new_component<gui_label_t>("Private car routes reading index:");
		reading_index_label.buf().printf("-");
		reading_index_label.set_color(SYSCOL_TEXT_TITLE);
		reading_index_label.update();
		add_component(&reading_index_label);

		new_component<gui_label_t>("Cities awaiting private car route check:");
		cities_awaiting_private_car_route_check_label.buf().printf("-");
		cities_awaiting_private_car_route_check_label.set_color(SYSCOL_TEXT_TITLE);
		cities_awaiting_private_car_route_check_label.update();
		add_component(&cities_awaiting_private_car_route_check_label);

		new_component<gui_label_t>("Cities to process this step for private car routes:");
		cities_to_process_label.buf().printf("-");
		cities_to_process_label.set_color(SYSCOL_TEXT_TITLE);
		cities_to_process_label.update();
		add_component(&cities_to_process_label);
	}
	end_table();
}

void gui_settings_t::draw(scr_coord offset)
{
	// Update label buffers
	frame_time_value_label.buf().printf(" %d ms", get_frame_time() );
	frame_time_value_label.update();
	idle_time_value_label.buf().printf(" %d ms", world()->get_idle_time() );
	idle_time_value_label.update();

	// fps_label
	uint32 target_fps = world()->is_fast_forward() ? env_t::ff_fps : env_t::fps;
	uint32 loops = world()->get_realFPS();
	PIXVAL color = SYSCOL_TEXT_HIGHLIGHT;
	if(  loops < (target_fps*16*3)/4  ) {
		color = color_idx_to_rgb(( loops <= target_fps*16/2 ) ? COL_RED : COL_YELLOW);
	}
	fps_value_label.set_color(color);
	fps_value_label.buf().printf(" %d fps", loops/16 );
#if MSG_LEVEL >= 3
	if(  env_t::simple_drawing  ) {
		fps_value_label.buf().append( "*" );
	}
#endif
	fps_value_label.update();

	//simloops_label
	loops = world()->get_simloops();
	color = SYSCOL_TEXT_HIGHLIGHT;
	if(  loops <= 30  ) {
		color = color_idx_to_rgb((loops<=20) ? COL_RED : COL_YELLOW);
	}
	simloops_value_label.set_color(color);
	simloops_value_label.buf().printf(" %d%c%d", loops/10, get_fraction_sep(), loops%10 );
	simloops_value_label.update();

	if ( path_explorer_t::is_processing() )
	{
		status_label.buf().printf("%s (%s) / %s", translator::translate(path_explorer_t::get_current_category_name()), path_explorer_t::get_current_class_name(), path_explorer_t::get_current_phase_name());
	}
	else {
		status_label.buf().printf("%s", "stand-by");
	}
	status_label.update();

	rebuild_connexion_label.buf().printf("%lu", path_explorer_t::get_limit_rebuild_connexions());
	rebuild_connexion_label.update();

	eligible_halts_label.buf().printf("%lu", path_explorer_t::get_limit_filter_eligible());
	eligible_halts_label.update();

	fill_path_matrix_label.buf().printf("%lu", path_explorer_t::get_limit_fill_matrix());
	fill_path_matrix_label.update();

	explore_path_label.buf().printf("%lu", (long)path_explorer_t::get_limit_explore_paths());
	explore_path_label.update();

	reroute_goods_label.buf().printf("%lu", path_explorer_t::get_limit_reroute_goods());
	reroute_goods_label.update();

	reading_index_label.buf().printf("%lu", weg_t::private_car_routes_currently_reading_element);
	reading_index_label.update();

	cities_awaiting_private_car_route_check_label.buf().printf("%lu", world()->get_cities_awaiting_private_car_route_check_count());
	cities_awaiting_private_car_route_check_label.update();

	cities_to_process_label.buf().printf("%i", world()->get_cities_to_process());
	cities_to_process_label.update();

	// All components are updated, now draw them...
	gui_aligned_container_t::draw(offset);
}


bool gui_settings_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if (comp == &screen_scale_numinp) {
		const sint16 new_value = screen_scale_numinp.get_value();
		dr_set_screen_scale(new_value);
	}
	else if (comp == &screen_scale_auto) {
		dr_set_screen_scale(-1);
		screen_scale_numinp.set_value(dr_get_screen_scale());
	}

	return true;
}


map_settings_t::map_settings_t()
{
	set_table_layout( 1, 0 );
	new_component<gui_label_t>("Grid");
	add_table(3,0);
	{
		// Show grid checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SHOW_GRID].init(button_t::square_state, "show grid");
		buttons[IDBTN_SHOW_GRID].set_tooltip("Shows the borderlines of each tile in the main game window. Can be useful for construction. Toggle with the # key.");
		add_component(buttons + IDBTN_SHOW_GRID, 2);

		// Underground view checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_UNDERGROUND_VIEW].init(button_t::square_state, "underground mode");
		buttons[IDBTN_UNDERGROUND_VIEW].set_tooltip("See under the ground, to build tunnels and underground railways/metros. Toggle with SHIFT + U");
		add_component(buttons + IDBTN_UNDERGROUND_VIEW, 2);

		// Show slice map view checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SHOW_SLICE_MAP_VIEW].init(button_t::square_state, "sliced underground mode");
		buttons[IDBTN_SHOW_SLICE_MAP_VIEW].set_tooltip("See under the ground, one layer at a time. Toggle with CTRL + U. Move up/down in layers with HOME and END.");
		add_component(buttons + IDBTN_SHOW_SLICE_MAP_VIEW);
		// underground slice edit
		inp_underground_level.set_value(grund_t::underground_mode == grund_t::ugm_level ? grund_t::underground_level : world()->get_zeiger()->get_pos().z);
		inp_underground_level.set_limits(world()->get_groundwater() - 10, 32);
		inp_underground_level.add_listener(this);
		add_component(&inp_underground_level);

#ifdef DEBUG
		// Toggle simple drawing for debugging
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SIMPLE_DRAWING].init(button_t::square_state, "Simple drawing");
		add_component(buttons + IDBTN_SIMPLE_DRAWING, 2);
#endif
	}
	end_table();

	new_component<gui_label_t>("Brightness");
	add_table(3, 0);
	{
		// Day/night change checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_DAY_NIGHT_CHANGE].init(button_t::square_state, "8WORLD_CHOOSE");
		buttons[IDBTN_DAY_NIGHT_CHANGE].set_tooltip("Whether the lighting in the main game window simulates a periodic transition between day and night.");
		add_component(buttons + IDBTN_DAY_NIGHT_CHANGE, 2);

		// Brightness label
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("1LIGHT_CHOOSE");
		// brightness edit
		brightness.set_value(env_t::daynight_level);
		brightness.set_limits(0, 9);
		brightness.add_listener(this);
		add_component(&brightness);
	}
	end_table();

	new_component<gui_label_t>("Map scroll");
	add_table(3, 0);
	{
		// Numpad key
		new_component<gui_margin_t>(LINESPACE / 2);
		buttons[IDBTN_IGNORE_NUMLOCK].init(button_t::square_state, "Num pad keys always move map");
		buttons[IDBTN_IGNORE_NUMLOCK].pressed = env_t::numpad_always_moves_map;
		add_component(buttons + IDBTN_IGNORE_NUMLOCK, 2);

		// Scroll inverse checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SCROLL_INVERSE].init(button_t::square_state, "4LIGHT_CHOOSE");
		buttons[IDBTN_SCROLL_INVERSE].set_tooltip("The main game window can be scrolled by right-clicking and dragging the ground.");
		add_component(buttons + IDBTN_SCROLL_INVERSE, 2);

		// Scroll infinite checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_INFINITE_SCROLL].init(button_t::square_state, "Infinite mouse scrolling");
		buttons[IDBTN_INFINITE_SCROLL].set_tooltip("Infinite scrolling using mouse");
		add_component(buttons + IDBTN_INFINITE_SCROLL, 2);

		// scroll with genral tool selected if moved above a threshold
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("Scroll threshold");
		scroll_threshold.init(env_t::scroll_threshold, 1, 64, 1, false);
		scroll_threshold.add_listener(this);
		add_component(&scroll_threshold);

		// Scroll speed label
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("3LIGHT_CHOOSE");
		// Scroll speed edit
		scrollspeed.set_value(abs(env_t::scroll_multi));
		scrollspeed.set_limits(1, 9);
		scrollspeed.add_listener(this);
		add_component(&scrollspeed);
	}
	end_table();

	// Set date format
	new_component<gui_label_t>("Date format");
	add_table(2,0);
	{
		new_component<gui_margin_t>(LINESPACE/2);
		time_setting.set_focusable( false );
		uint8 old_show_month = env_t::show_month;
		sint32 current_tick = world()->get_ticks();
		for( env_t::show_month = 0; env_t::show_month<env_t::DATE_FMT_LIMIT_FOR_LOOPS; env_t::show_month++ ) {
			DBG_DEBUG("map_settings_t()", "The date format loop has reached %u", env_t::show_month);
			tstrncpy( time_str[env_t::show_month], tick_to_string( current_tick, true ), 64 );
			DBG_DEBUG("map_settings_t()", "The last entry in the time_str array was %s", tstrncpy( time_str[env_t::show_month], tick_to_string( current_tick, true ), 64 ));
			time_setting.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( time_str[env_t::show_month], SYSCOL_TEXT );
		}
		env_t::show_month = old_show_month;
		time_setting.set_selection( old_show_month );
		add_component( &time_setting );
		time_setting.add_listener( this );
	}
	end_table();
	new_component<gui_divider_t>();

	new_component<gui_label_t>("transparencies");
	add_table(3, 0);
	{
		// Transparent instead of hidden checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN].init(button_t::square_state, "hide transparent");
		buttons[IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN].set_tooltip("All hidden items (such as trees and buildings) will appear as transparent.");
		add_component(buttons + IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN, 2);

		// Hide trees checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_HIDE_TREES].init(button_t::square_state, "hide trees");
		buttons[IDBTN_HIDE_TREES].set_tooltip("Trees will be miniaturised or made transparent in the main game window.");
		add_component(buttons + IDBTN_HIDE_TREES, 2);

		// Hide buildings
		new_component<gui_margin_t>(LINESPACE/2);
		hide_buildings.set_focusable(false);
		hide_buildings.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("no buildings hidden"), SYSCOL_TEXT);
		hide_buildings.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("hide city building"), SYSCOL_TEXT);
		hide_buildings.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("hide all building"), SYSCOL_TEXT);
		hide_buildings.set_selection(env_t::hide_buildings);
		add_component(&hide_buildings, 2);
		hide_buildings.add_listener(this);

		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_HIDE_BUILDINGS].init(button_t::square_state, "Smart hide objects");
		buttons[IDBTN_HIDE_BUILDINGS].set_tooltip("hide objects under cursor");
		add_component(buttons + IDBTN_HIDE_BUILDINGS);
		// Smart hide objects edit
		cursor_hide_range.set_value(env_t::cursor_hide_range);
		cursor_hide_range.set_limits(0, 10);
		cursor_hide_range.add_listener(this);
		add_component(&cursor_hide_range);
	}
	end_table();
}

bool map_settings_t::action_triggered( gui_action_creator_t *comp, value_t v )
{
	// Brightness edit
	if( &brightness == comp ) {
		env_t::daynight_level = (sint8)v.i;
	}
	// Scroll speed edit
	else if (&scroll_threshold == comp) {
		env_t::scroll_threshold = v.i;
	}
	// Scroll speed edit
	else if (&scrollspeed == comp) {
		env_t::scroll_multi = (sint16)(buttons[IDBTN_SCROLL_INVERSE].pressed ? -v.i : v.i);
	}
	// underground slice edit
	else if( comp == &inp_underground_level ) {
		if( grund_t::underground_mode == grund_t::ugm_level ) {
			grund_t::underground_level = inp_underground_level.get_value();

			// calc new images
			world()->update_underground();
		}
	}
	else if( comp == &time_setting ) {
		env_t::show_month = v.i;
		return true;
	}

	// Smart hide objects edit
	if( &cursor_hide_range == comp ) {
		env_t::cursor_hide_range = cursor_hide_range.get_value();
	}
	// Hide building
	if( &hide_buildings == comp ) {
		env_t::hide_buildings = (uint8)v.i;
		world()->set_dirty();
	}
	return true;
}


void map_settings_t::draw( scr_coord offset )
{
	hide_buildings.set_selection( env_t::hide_buildings );

	gui_aligned_container_t::draw(offset);
}


label_settings_t::label_settings_t()
{
	set_table_layout(1,0);

	// Show signalbox coverage
	buttons[IDBTN_SHOW_SIGNALBOX_COVERAGE].init(button_t::square_state, "show signalbox coverage");
	buttons[IDBTN_SHOW_SIGNALBOX_COVERAGE].pressed = env_t::signalbox_coverage_show;
	buttons[IDBTN_SHOW_SIGNALBOX_COVERAGE].set_tooltip("Show coverage radius of the signalbox.");
	add_component(buttons + IDBTN_SHOW_SIGNALBOX_COVERAGE);
	new_component<gui_divider_t>();

	new_component<gui_label_t>("Station display");
	add_table(5, 0);
	{
		// Transparent station coverage
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_TRANSPARENT_STATION_COVERAGE].init(button_t::square_state, "transparent station coverage");
		buttons[IDBTN_TRANSPARENT_STATION_COVERAGE].pressed = env_t::use_transparency_station_coverage;
		buttons[IDBTN_TRANSPARENT_STATION_COVERAGE].set_tooltip("The display of the station coverage can either be a transparent rectangle or a series of boxes.");
		add_component(buttons + IDBTN_TRANSPARENT_STATION_COVERAGE, 4);

		// Show station coverage
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SHOW_STATION_COVERAGE].init(button_t::square_state, "show station coverage");
		buttons[IDBTN_SHOW_STATION_COVERAGE].set_tooltip("Show from how far that passengers or goods will come to use your stops. Toggle with the v key.");
		add_component(buttons + IDBTN_SHOW_STATION_COVERAGE, 4);

		// Show waiting bars checkbox
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[IDBTN_SHOW_WAITING_BARS].init(button_t::square_state, "show waiting bars");
		buttons[IDBTN_SHOW_WAITING_BARS].pressed = env_t::show_names & 2;
		buttons[IDBTN_SHOW_WAITING_BARS].set_tooltip("Shows a bar graph representing the number of passengers/mail/goods waiting at stops.");
		add_component(buttons + IDBTN_SHOW_WAITING_BARS, 4);

		// waiting bar option for passenger and mail classes
		bool pakset_has_pass_classes = (goods_manager_t::passengers->get_number_of_classes() > 1);
		bool pakset_has_mail_classes = (goods_manager_t::mail->get_number_of_classes() > 1);
		if (pakset_has_pass_classes || pakset_has_mail_classes) {
			new_component<gui_margin_t>(LINESPACE/2);
			new_component<gui_margin_t>(LINESPACE/2);
			new_component<gui_image_t>()->set_image(pakset_has_pass_classes ? skinverwaltung_t::passengers->get_image_id(0) : IMG_EMPTY, true);
			new_component<gui_image_t>()->set_image(pakset_has_mail_classes ? skinverwaltung_t::mail->get_image_id(0) : IMG_EMPTY, true);
			buttons[IDBTN_CLASSES_WAITING_BAR].init(button_t::square_state, "Divided by class");
			buttons[IDBTN_CLASSES_WAITING_BAR].pressed = env_t::classes_waiting_bar;
			buttons[IDBTN_CLASSES_WAITING_BAR].enable(env_t::show_names & 2);
			buttons[IDBTN_CLASSES_WAITING_BAR].set_tooltip("Waiting bars are displayed separately for each class.");
			add_component(buttons + IDBTN_CLASSES_WAITING_BAR);
		}

		// waiting bar option for freight
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_image_t>()->set_image(skinverwaltung_t::goods->get_image_id(0), true);
		new_component<gui_empty_t>();
		freight_waiting_bar.set_focusable(false);
		freight_waiting_bar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Unified"), SYSCOL_TEXT);
		freight_waiting_bar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Divided by category"), SYSCOL_TEXT);
		freight_waiting_bar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Divided by goods"), SYSCOL_TEXT);
		freight_waiting_bar.set_selection(env_t::freight_waiting_bar_level);
		freight_waiting_bar.enable(env_t::show_names & 2);
		add_component(&freight_waiting_bar);
		freight_waiting_bar.add_listener(this);
	}
	end_table();

	// Show station coverage
	buttons[IDBTN_HIDE_LABELS].init(button_t::square_state, "Hide labels");
	add_component(buttons + IDBTN_HIDE_LABELS, 2);

	// Show station names arrow
	add_table(3,1);
	{
		new_component<gui_margin_t>(LINESPACE/2);
		buttons[ IDBTN_LABEL_STYLE_ARROW ].set_typ( button_t::arrowright );
		buttons [IDBTN_LABEL_STYLE_ARROW ].set_tooltip("Shows the names of the individual stations in the main game window.");
		//buttons[ IDBTN_LABEL_STYLE_ARROW ].set_tooltip( "Change label style" );
		add_component( buttons + IDBTN_LABEL_STYLE_ARROW );
		new_component<gui_label_stationname_t>( "Change label style" );
	}
	end_table();

	// Show own depot name
	buttons[IDBTN_SHOW_DEPOT_NAME].init(button_t::square_state, "Show own depot names");
	buttons[IDBTN_SHOW_DEPOT_NAME].pressed = env_t::show_depot_names;
	buttons[IDBTN_SHOW_DEPOT_NAME].set_tooltip("Show the name of own depots in the main game window.");
	add_component(buttons + IDBTN_SHOW_DEPOT_NAME);

	new_component<gui_divider_t>();
	add_table(2,1);
	{
		new_component<gui_label_t>("Industry overlay")->set_tooltip(translator::translate("Display bars above the factory to show the status"));
		factory_tooltip.set_focusable(false);
		factory_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("never show"), SYSCOL_TEXT);
		factory_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("mouseover"), SYSCOL_TEXT);
		factory_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Within own network"), SYSCOL_TEXT);
		factory_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("always show all"), SYSCOL_TEXT);
		factory_tooltip.set_selection(env_t::show_factory_storage_bar);
		add_component(&factory_tooltip);
		factory_tooltip.add_listener(this);
	}
	end_table();

	new_component<gui_divider_t>();

	new_component<gui_label_t>("Convoy tooltips");
	add_table(3,0);
	{
		// Convoy nameplate
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("Nameplates")->set_tooltip(translator::translate("The line name or convoy name is displayed above the convoy."));
		convoy_nameplate.set_focusable(false);
		convoy_nameplate.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("never show"), SYSCOL_TEXT);
		convoy_nameplate.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("mouseover"), SYSCOL_TEXT);
		convoy_nameplate.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("only active player's"), SYSCOL_TEXT);
		convoy_nameplate.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("always show all"), SYSCOL_TEXT);
		convoy_nameplate.set_selection(env_t::show_cnv_nameplates);
		add_component(&convoy_nameplate);
		convoy_nameplate.add_listener(this);

		new_component<gui_empty_t>();
		new_component<gui_empty_t>();
		add_table(4,1)->set_spacing(scr_size(0,0));
		{
			new_component<gui_margin_t>( D_ARROW_LEFT_WIDTH+D_H_SPACE );
			// UI TODO: radio button is better
			bt_convoy_id_plate[0].init(button_t::roundbox_left_state  | button_t::flexible, "name_plate");
			bt_convoy_id_plate[1].init(button_t::roundbox_right_state | button_t::flexible, "Convoy ID");
			bt_convoy_id_plate[0].set_tooltip("help_text_btn_line_name_plate");
			bt_convoy_id_plate[1].set_tooltip("Shows the convoy unique ID");
			bt_convoy_id_plate[0].add_listener(this);
			bt_convoy_id_plate[1].add_listener(this);
			add_component(&bt_convoy_id_plate[0]);
			add_component(&bt_convoy_id_plate[1]);
			new_component<gui_margin_t>( D_ARROW_LEFT_WIDTH+D_H_SPACE );
		}
		end_table();

		// Convoy loading bar
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("Loading bar")->set_tooltip(translator::translate("A loading rate bar is displayed above the convoy."));
		convoy_loadingbar.set_focusable(false);
		convoy_loadingbar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("never show"), SYSCOL_TEXT);
		convoy_loadingbar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("mouseover"), SYSCOL_TEXT);
		convoy_loadingbar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("only active player's"), SYSCOL_TEXT);
		convoy_loadingbar.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("always show all"), SYSCOL_TEXT);
		convoy_loadingbar.set_selection(env_t::show_cnv_loadingbar);
		add_component(&convoy_loadingbar);
		convoy_loadingbar.add_listener(this);

		// Convoy tooltip
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("Tooltip")->set_tooltip(translator::translate("Toggle vehicle tooltips"));
		convoy_tooltip.set_focusable(false);
		convoy_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("mouseover"), SYSCOL_TEXT);
		convoy_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("only error convoys"), SYSCOL_TEXT);
		convoy_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("only active player's"), SYSCOL_TEXT);
		convoy_tooltip.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("always show all"), SYSCOL_TEXT);
		convoy_tooltip.set_selection(env_t::show_vehicle_states);
		add_component(&convoy_tooltip);
		convoy_tooltip.add_listener(this);

		// convoi booking message options
		new_component<gui_margin_t>(LINESPACE/2);
		new_component<gui_label_t>("Money message")->set_tooltip(translator::translate("Income display displayed when convoy arrives at the stop."));
		money_booking.set_focusable(false);
		money_booking.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("always show all"), SYSCOL_TEXT);
		money_booking.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("only active player's"), SYSCOL_TEXT);
		money_booking.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("never show"), SYSCOL_TEXT);
		money_booking.set_selection(env_t::show_money_message);
		add_component(&money_booking, 2);
		money_booking.add_listener(this);
	}
	end_table();

}

bool label_settings_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	// Factory tooltip
	if(&factory_tooltip == comp){
		env_t::show_factory_storage_bar = v.i;
	}
	// Convoy tooltip
	if (&convoy_tooltip == comp) {
		env_t::show_vehicle_states = (uint8)v.i;
	}
	// Convoy nameplate
	if (&convoy_nameplate == comp) {
		env_t::show_cnv_nameplates = v.i;
		if (bt_convoy_id_plate[1].pressed) {
			env_t::show_cnv_nameplates |= 4;
		}
	}
	else if (&bt_convoy_id_plate[0] == comp) {
		env_t::show_cnv_nameplates &= ~4;
	}
	else if (&bt_convoy_id_plate[1] == comp) {
		env_t::show_cnv_nameplates |= 4;
	}
	// Convoy loading bar
	if (&convoy_loadingbar == comp) {
		env_t::show_cnv_loadingbar = v.i;
	}
	if (&money_booking == comp) {
		env_t::show_money_message = (sint8)v.i;
	}
	// freight waiting bar detail level
	if (&freight_waiting_bar == comp) {
		env_t::freight_waiting_bar_level = v.i;
	}
	return true;
}

void label_settings_t::draw(scr_coord offset)
{
	convoy_nameplate.set_selection(env_t::show_cnv_nameplates % 4);
	bt_convoy_id_plate[1].pressed = env_t::show_cnv_nameplates & 4;
	bt_convoy_id_plate[0].pressed = !(env_t::show_cnv_nameplates & 4);
	convoy_loadingbar.set_selection(env_t::show_cnv_loadingbar);
	convoy_tooltip.set_selection(env_t::show_vehicle_states);
	freight_waiting_bar.set_selection(env_t::freight_waiting_bar_level);
	freight_waiting_bar.enable(env_t::show_names & 2);
	factory_tooltip.set_selection(env_t::show_factory_storage_bar % 4);

	gui_aligned_container_t::draw(offset);
}

traffic_settings_t::traffic_settings_t()
{
	set_table_layout( 2, 0 );

	// Pedestrians in towns checkbox
	buttons[IDBTN_PEDESTRIANS_IN_TOWNS].init(button_t::square_state, "6LIGHT_CHOOSE");
	buttons[IDBTN_PEDESTRIANS_IN_TOWNS].set_tooltip("Pedestrians will appear randomly in towns.");
	buttons[IDBTN_PEDESTRIANS_IN_TOWNS].pressed = world()->get_settings().get_random_pedestrians();
	add_component(buttons+IDBTN_PEDESTRIANS_IN_TOWNS, 2);

	// Pedestrians at stops checkbox
	buttons[IDBTN_PEDESTRIANS_AT_STOPS].init(button_t::square_state, "5LIGHT_CHOOSE");
	buttons[IDBTN_PEDESTRIANS_AT_STOPS].set_tooltip("Pedestrians will appear near stops whenver a passenger vehicle unloads there.");
	buttons[IDBTN_PEDESTRIANS_AT_STOPS].pressed = world()->get_settings().get_show_pax();
	add_component(buttons+IDBTN_PEDESTRIANS_AT_STOPS, 2);

	// Traffic density label
	new_component<gui_label_t>("6WORLD_CHOOSE");

	// Traffic density edit
	traffic_density.set_value(world()->get_settings().get_traffic_level());
	traffic_density.set_limits( 0, 16 );
	traffic_density.add_listener(this);
	add_component(&traffic_density);

	// Convoy follow mode
	new_component<gui_label_t>("Convoi following mode")->set_tooltip(translator::translate("Select the behavior of the camera when the following convoy enters the tunnel."));

	follow_mode.set_focusable(false);
	follow_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("do nothing"), SYSCOL_TEXT);
	follow_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("underground mode"), SYSCOL_TEXT);
	follow_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("sliced underground mode"), SYSCOL_TEXT);
	follow_mode.set_selection(env_t::follow_convoi_underground);
	add_component(&follow_mode);
	follow_mode.add_listener(this);

	// Show schedule's stop checkbox
	buttons[IDBTN_SHOW_SCHEDULES_STOP].init( button_t::square_state ,  "Highlite schedule" );
	buttons[IDBTN_SHOW_SCHEDULES_STOP].set_tooltip("Highlight the locations of stops on the current schedule");
	add_component(buttons+IDBTN_SHOW_SCHEDULES_STOP, 2);
}

bool traffic_settings_t::action_triggered( gui_action_creator_t *comp, value_t v )
{
	// Traffic density edit
	if( &traffic_density == comp ) {
		if( !env_t::networkmode || world()->get_active_player_nr() == PUBLIC_PLAYER_NR ) {
			static char level[ 16 ];
			sprintf( level, "%li", v.i );
			tool_t::simple_tool[ TOOL_TRAFFIC_LEVEL & 0xFFF ]->set_default_param( level );
			world()->set_tool( tool_t::simple_tool[ TOOL_TRAFFIC_LEVEL & 0xFFF ], world()->get_active_player() );
		}
		else {
			traffic_density.set_value( world()->get_settings().get_traffic_level() );
		}
	}

	if( &follow_mode == comp ) {
		env_t::follow_convoi_underground = (uint8)v.i;
	}
	return true;
}



color_gui_t::color_gui_t() :
	gui_frame_t( translator::translate( "Helligk. u. Farben" ) ),
	scrolly_gui(&gui_settings),
	scrolly_map(&map_settings),
	scrolly_station(&station_settings),
	scrolly_traffic(&traffic_settings)
{
	set_table_layout( 1, 0 );

	scrolly_gui.set_scroll_amount_y(D_BUTTON_HEIGHT/2);
	scrolly_map.set_scroll_amount_y(D_BUTTON_HEIGHT/2);
	scrolly_station.set_scroll_amount_y(D_BUTTON_HEIGHT/2);
	scrolly_traffic.set_scroll_amount_y(D_BUTTON_HEIGHT/2);

	tabs.add_tab(&scrolly_gui, translator::translate("GUI settings"));
	tabs.add_tab(&scrolly_map, translator::translate("map view"));
	tabs.add_tab(&scrolly_station, translator::translate("tooltip/coverage"));
	tabs.add_tab(&scrolly_traffic, translator::translate("traffic settings"));
	add_component(&tabs);

	for( int i = 0; i < COLORS_MAX_BUTTONS; i++ ) {
		buttons[ i ].add_listener( this );
	}
	for( uint8 i = 0; i < 4; i++ ) {
		gui_settings.toolbar_pos[i].add_listener(this);
	}
	for (uint8 i = 0; i < 8; i++) {
		gui_settings.info_window_toggler[i].add_listener(this);
	}
	gui_settings.fullscreen.add_listener( this );
	gui_settings.borderless.add_listener( this );
	gui_settings.reselect_closes_tool.add_listener(this);

	set_resizemode(diagonal_resize);
	set_min_windowsize( scr_size(gui_settings.get_min_size().w, map_settings.get_min_size().h+D_TAB_HEADER_HEIGHT+D_TITLEBAR_HEIGHT) );
	// GUI settings tab is the tab with the most lines in Extended.
	set_windowsize( gui_settings.get_min_size()+scr_size(0,D_TAB_HEADER_HEIGHT+D_TITLEBAR_HEIGHT) );
	resize( scr_coord( 0, 0 ) );
}

bool color_gui_t::action_triggered( gui_action_creator_t *comp, value_t)
{
	bool reflesh_toolbar_pos_btn = false;
	for(  uint8 i=0; i<4; i++  ) {
		if(  comp == &gui_settings.toolbar_pos[i]  ) {
			env_t::menupos = i;
			reflesh_toolbar_pos_btn = true;

			welt->set_dirty();
			// move all windows
			event_t* ev = new event_t();
			ev->ev_class = EVENT_SYSTEM;
			ev->ev_code = SYSTEM_RELOAD_WINDOWS;
			queue_event(ev);
			break;
		}
	}
	if( reflesh_toolbar_pos_btn ) {
		for(  uint8 i=0; i<4; i++  ) {
			gui_settings.toolbar_pos[i].pressed = (env_t::menupos == i);
		}
		return true;
	}

	if(  comp == &gui_settings.fullscreen  ) {
		gui_settings.fullscreen.pressed = !gui_settings.fullscreen.pressed;
		env_t::fullscreen = gui_settings.fullscreen.pressed;
		gui_settings.borderless.pressed = false;
		return true;
	}

	if(  comp == &gui_settings.borderless  ) {
		env_t::fullscreen = dr_toggle_borderless();
		gui_settings.borderless.pressed = dr_get_fullscreen();
		gui_settings.fullscreen.pressed = false;
		return true;
	}

	if(  comp == &gui_settings.reselect_closes_tool  ) {
		env_t::reselect_closes_tool = !env_t::reselect_closes_tool;
		gui_settings.reselect_closes_tool.pressed = env_t::reselect_closes_tool;
		return true;
	}

	if (comp == &gui_settings.info_window_toggler[0] || comp == &gui_settings.info_window_toggler[1]) {
		gui_settings.info_window_toggler[0].pressed = (comp == &gui_settings.info_window_toggler[0]);
		gui_settings.info_window_toggler[1].pressed = !gui_settings.info_window_toggler[0].pressed;
		env_t::ground_info = gui_settings.info_window_toggler[0].pressed;
		return true;
	}
	if (comp == &gui_settings.info_window_toggler[2] || comp == &gui_settings.info_window_toggler[3]) {
		gui_settings.info_window_toggler[2].pressed = (comp == &gui_settings.info_window_toggler[2]);
		gui_settings.info_window_toggler[3].pressed = !gui_settings.info_window_toggler[2].pressed;
		env_t::tree_info = gui_settings.info_window_toggler[2].pressed;
		return true;
	}
	if (comp == &gui_settings.info_window_toggler[4] || comp == &gui_settings.info_window_toggler[5]) {
		env_t::road_user_info &= ~1;
		if (comp == &gui_settings.info_window_toggler[4]) { env_t::road_user_info |= 1; }
		gui_settings.info_window_toggler[4].pressed = (comp == &gui_settings.info_window_toggler[4]);
		gui_settings.info_window_toggler[5].pressed = !gui_settings.info_window_toggler[4].pressed;
		return true;
	}
	if (comp == &gui_settings.info_window_toggler[6] || comp == &gui_settings.info_window_toggler[7]) {
		env_t::road_user_info &= ~2;
		if (comp == &gui_settings.info_window_toggler[6]) { env_t::road_user_info |= 2; }
		gui_settings.info_window_toggler[6].pressed = (comp == &gui_settings.info_window_toggler[6]);
		gui_settings.info_window_toggler[7].pressed = !gui_settings.info_window_toggler[6].pressed;
		return true;
	}

	int i;
	for(  i=0;  i<COLORS_MAX_BUTTONS  &&  comp!=buttons+i;  i++  ) { }

	switch( i )
	{
	case IDBTN_IGNORE_NUMLOCK:
		env_t::numpad_always_moves_map = !env_t::numpad_always_moves_map;
		buttons[IDBTN_IGNORE_NUMLOCK].pressed = env_t::numpad_always_moves_map;
		break;
	case IDBTN_SCROLL_INVERSE:
		env_t::scroll_multi = -env_t::scroll_multi;
		break;
	case IDBTN_INFINITE_SCROLL:
		env_t::scroll_infinite ^= 1;
		break;
	case IDBTN_PEDESTRIANS_AT_STOPS:
		if( !env_t::networkmode || welt->get_active_player_nr() == PUBLIC_PLAYER_NR ) {
			welt->set_tool( tool_t::simple_tool[ TOOL_TOOGLE_PAX & 0xFFF ], welt->get_active_player() );
		}
		break;
	case IDBTN_PEDESTRIANS_IN_TOWNS:
		if( !env_t::networkmode || welt->get_active_player_nr() == PUBLIC_PLAYER_NR ) {
			welt->set_tool( tool_t::simple_tool[ TOOL_TOOGLE_PEDESTRIANS & 0xFFF ], welt->get_active_player() );
		}
		break;
	case IDBTN_HIDE_TREES:
		env_t::hide_trees = !env_t::hide_trees;
		baum_t::recalc_outline_color();
		break;
	case IDBTN_DAY_NIGHT_CHANGE:
		env_t::night_shift = !env_t::night_shift;
		break;
	case IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN:
		env_t::hide_with_transparency = !env_t::hide_with_transparency;
		baum_t::recalc_outline_color();
		break;
	case IDBTN_TRANSPARENT_STATION_COVERAGE:
		env_t::use_transparency_station_coverage = !env_t::use_transparency_station_coverage;
		break;
	case IDBTN_SHOW_STATION_COVERAGE:
		env_t::station_coverage_show = env_t::station_coverage_show == 0 ? 0xFF : 0;
		break;
	case IDBTN_SHOW_DEPOT_NAME:
		env_t::show_depot_names = !env_t::show_depot_names;
		break;
	case IDBTN_UNDERGROUND_VIEW:
		// see simtool.cc::tool_show_underground_t::init
		grund_t::set_underground_mode( buttons[ IDBTN_UNDERGROUND_VIEW ].pressed ? grund_t::ugm_none : grund_t::ugm_all, map_settings.inp_underground_level.get_value() );

		// calc new images
		welt->update_underground();

		// renew toolbar
		tool_t::update_toolbars();
		break;
	case IDBTN_SHOW_GRID:
		grund_t::toggle_grid();
		break;
		case IDBTN_LABEL_STYLE_ARROW:
		{
			int label_style = ((env_t::show_names >> 2)+1)%3;
			env_t::show_names = (env_t::show_names & 3) + (label_style << 2);
		}
		break;
	case IDBTN_SHOW_WAITING_BARS:
		env_t::show_names ^= 2;
		buttons[IDBTN_CLASSES_WAITING_BAR].enable(env_t::show_names & 2);
		break;
	case IDBTN_HIDE_LABELS:
		env_t::show_names ^= 1;
		break;
	case IDBTN_CLASSES_WAITING_BAR:
		env_t::classes_waiting_bar = !env_t::classes_waiting_bar;
		buttons[IDBTN_CLASSES_WAITING_BAR].pressed ^= 1;
		break;
	case IDBTN_SHOW_SLICE_MAP_VIEW:
		// see simtool.cc::tool_show_underground_t::init
		grund_t::set_underground_mode( buttons[ IDBTN_SHOW_SLICE_MAP_VIEW ].pressed ? grund_t::ugm_none : grund_t::ugm_level, map_settings.inp_underground_level.get_value() );

		// calc new images
		welt->update_underground();

		// renew toolbar
		tool_t::update_toolbars();
		break;
	case IDBTN_HIDE_BUILDINGS:
		// see simtool.cc::tool_hide_under_cursor_t::init
		env_t::hide_under_cursor = !env_t::hide_under_cursor  &&  env_t::cursor_hide_range > 0;

		// renew toolbar
		tool_t::update_toolbars();
		break;
	case IDBTN_SHOW_SCHEDULES_STOP:
		env_t::visualize_schedule = !env_t::visualize_schedule;
		break;
	case IDBTN_SHOW_THEMEMANAGER:
		create_win( new themeselector_t(), w_info, magic_themes );
		break;
	case IDBTN_SIMPLE_DRAWING:
		env_t::simple_drawing = !env_t::simple_drawing;
		break;
	case IDBTN_CHANGE_FONT:
		create_win( new loadfont_frame_t(), w_info, magic_font );
		break;
	case IDBTN_LEFT_TO_RIGHT_GRAPHS:
		env_t::left_to_right_graphs = !env_t::left_to_right_graphs;
		buttons[IDBTN_LEFT_TO_RIGHT_GRAPHS].pressed ^= 1;
		break;
	case IDBTN_SHOW_SIGNALBOX_COVERAGE:
		env_t::signalbox_coverage_show = env_t::signalbox_coverage_show == 0 ? 0xFF : 0;
		break;
	default:
		assert( 0 );
	}

	welt->set_dirty();

	return true;
}


void color_gui_t::draw(scr_coord pos, scr_size size)
{
	// Update button states that was changed with keyboard ...
	buttons[IDBTN_PEDESTRIANS_AT_STOPS].pressed = welt->get_settings().get_show_pax();
	buttons[IDBTN_PEDESTRIANS_IN_TOWNS].pressed = welt->get_settings().get_random_pedestrians();
	buttons[IDBTN_HIDE_TREES].pressed = env_t::hide_trees;
	buttons[IDBTN_HIDE_BUILDINGS].pressed = env_t::hide_under_cursor;
	buttons[IDBTN_SHOW_STATION_COVERAGE].pressed = env_t::station_coverage_show;
	buttons[IDBTN_UNDERGROUND_VIEW].pressed = grund_t::underground_mode == grund_t::ugm_all;
	buttons[IDBTN_SHOW_GRID].pressed = grund_t::show_grid;
	buttons[IDBTN_SHOW_WAITING_BARS].pressed = (env_t::show_names&2)!=0;
	buttons[IDBTN_HIDE_LABELS].pressed = (env_t::show_names & 1) == 0;
	buttons[IDBTN_SHOW_DEPOT_NAME].pressed = env_t::show_depot_names;
	buttons[IDBTN_SHOW_SLICE_MAP_VIEW].pressed = grund_t::underground_mode == grund_t::ugm_level;
	buttons[IDBTN_SHOW_SCHEDULES_STOP].pressed = env_t::visualize_schedule;
	buttons[IDBTN_SIMPLE_DRAWING].pressed = env_t::simple_drawing;
	buttons[IDBTN_SIMPLE_DRAWING].enable(welt->is_paused());
	buttons[IDBTN_SCROLL_INVERSE].pressed = env_t::scroll_multi > 0;
	buttons[IDBTN_INFINITE_SCROLL].pressed = env_t::scroll_infinite;
	buttons[IDBTN_DAY_NIGHT_CHANGE].pressed = env_t::night_shift;
	buttons[IDBTN_SHOW_SLICE_MAP_VIEW].pressed = grund_t::underground_mode == grund_t::ugm_level;
	buttons[IDBTN_UNDERGROUND_VIEW].pressed = grund_t::underground_mode == grund_t::ugm_all;
	buttons[IDBTN_TRANSPARENT_STATION_COVERAGE].pressed = env_t::use_transparency_station_coverage;
	buttons[IDBTN_TRANSPARENT_INSTEAD_OF_HIDDEN].pressed = env_t::hide_with_transparency;


	buttons[IDBTN_SHOW_SIGNALBOX_COVERAGE].pressed = env_t::signalbox_coverage_show;

	// All components are updated, now draw them...
	gui_frame_t::draw(pos, size);
}


void color_gui_t::rdwr(loadsave_t *f)
{
	tabs.rdwr(f);
	scrolly_gui.rdwr(f);
	scrolly_map.rdwr(f);
	scrolly_station.rdwr(f);
	scrolly_traffic.rdwr(f);
}
