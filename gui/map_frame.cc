/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string>
#include <stdio.h>
#include <cmath>

#include "minimap.h"
#include "map_frame.h"
#include "factory_legend.h"

#include "../sys/simsys.h"
#include "../simworld.h"
#include "../simhalt.h"
#include "../display/viewport.h"
#include "../simcolor.h"
#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../dataobj/koord.h"
#include "../dataobj/loadsave.h"
#include "../simfab.h"

#include "components/gui_colorbox.h"
#include "depotlist_frame.h"

static koord old_ij=koord::invalid;

karte_ptr_t map_frame_t::welt;

scr_size map_frame_t::window_size;
bool  map_frame_t::legend_visible=false;
bool  map_frame_t::network_option_visible = false;
bool  map_frame_t::scale_visible=false;
bool  map_frame_t::zoomed = true;

// we track our position onscreen
scr_coord map_frame_t::screenpos;

#define L_BUTTON_WIDTH (button_size.w)
#define L_BUTTON_WIDTH_2 100

gui_scrollpane_map_t::gui_scrollpane_map_t(gui_component_t* comp) : gui_scrollpane_t(comp)
{
	set_allow_dragging(false);
	is_dragging = false;
	is_cursor_hidden = false;
}


void gui_scrollpane_map_t::zoom(bool magnify)
{
	if (minimap_t::get_instance()->change_zoom_factor(magnify)) {
		map_frame_t::zoomed = true;

		// recalculate scroll bar width
		set_size(get_size());
		// invalidate old offsets
		old_ij = koord::invalid;
	}
}


bool gui_scrollpane_map_t::infowin_event(event_t const* ev)
{
	if (IS_WHEELUP(ev) || IS_WHEELDOWN(ev)) {
		// otherwise these would go to the vertical scroll bars
		zoom(IS_WHEELUP(ev));
		return true;
	}

	// hack: minimap can resize upon right click
	// we track this here, and adjust size.
	if (IS_RIGHTCLICK(ev)) {
		is_dragging = false;
		display_show_pointer(false);
		is_cursor_hidden = true;
		return true;
	}
	else if (IS_RIGHTRELEASE(ev)) {
		is_dragging = false;
		display_show_pointer(true);
		is_cursor_hidden = false;
		return true;
	}
	else if (IS_RIGHTDRAG(ev)) {
		int x = get_scroll_x();
		int y = get_scroll_y();
		const int scroll_direction = (env_t::scroll_multi > 0 ? 1 : -1);

		x += (ev->mouse_pos.x - ev->click_pos.x) * scroll_direction * 2;
		y += (ev->mouse_pos.y - ev->click_pos.y) * scroll_direction * 2;

		is_dragging = true;

		set_scroll_position(max(0, x), max(0, y));
#if 0
		// Move the mouse pointer back to starting location
		// To prevent a infinite mouse event loop, we just do it when needed.
		if ((ev->mouse_pos.x - ev->click_pos.x) != 0 || (ev->mouse_pos.y - ev->click_pos.y) != 0) {
			move_pointer(map_frame_t::screenpos.x + ev->click_pos.x, map_frame_t::screenpos.y + ev->click_pos.y);
		}
#endif
		return true;
	}
	else if (IS_RIGHTDBLCLK(ev)) {
		// zoom to fit window
		do { // first, zoom all the way in
			map_frame_t::zoomed = false;
			zoom(true);
		} while (map_frame_t::zoomed);

		// then zoom back out to fit
		const scr_size s_size = get_size() - D_SCROLLBAR_SIZE;
		scr_size size = minimap_t::get_instance()->get_size();
		map_frame_t::zoomed = true;
		while (map_frame_t::zoomed && max(size.w / s_size.w, size.h / s_size.h)) {
			zoom(false);
			size = minimap_t::get_instance()->get_size();
		}
		return true;
	}
	else if (is_cursor_hidden) {
		display_show_pointer(true);
		is_cursor_hidden = false;
	}

	return gui_scrollpane_t::infowin_event(ev);
}

/**
 * Show scale of severity-MAX_SEVERITY_COLORS
 */
class gui_scale_t : public gui_component_t
{
public:
	void draw(scr_coord offset) OVERRIDE
	{
		scr_coord pos = get_pos() + offset;
		double bar_width = (double)get_size().w/(double)MAX_SEVERITY_COLORS;
		// color bar
		for(  int i=0;  i<MAX_SEVERITY_COLORS;  i++  ) {
			display_fillbox_wh_clip_rgb(pos.x + (scr_coord_val)(i*bar_width), pos.y+2, (scr_coord_val)bar_width+1, 7, minimap_t::calc_severity_color(i, MAX_SEVERITY_COLORS-1), false);
		}
	}
	scr_size get_min_size() const OVERRIDE
	{
		return scr_size(0, 9);
	}
	scr_size get_max_size() const OVERRIDE
	{
		return scr_size( scr_size::inf.w, 9);
	}
};

typedef struct {
	PIXVAL color;
	PIXVAL select_color;
	const char *button_text;
	const char *tooltip_text;
	minimap_t::MAP_DISPLAY_MODE mode;
} map_button_t;


map_button_t button_init[MAP_MAX_BUTTONS] = {
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "Towns", "Overlay town names", minimap_t::MAP_TOWN },
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "CityLimit", "Overlay city limits", minimap_t::MAP_CITYLIMIT },
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "Tourists", "Highlite tourist attraction", minimap_t::MAP_TOURIST },
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "Factories", "Highlite factories", minimap_t::MAP_FACTORIES },
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "Depots", "Highlite depots", minimap_t::MAP_DEPOT },
	{ COL_LIGHT_GREEN,  COL_DARK_GREEN,  "Convoys", "Show convoys", minimap_t::MAP_CONVOYS },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "Passengers", "The number of passengers at the stop last month", minimap_t::MAP_TRANSFER },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "Handling mails", "The number of mail that handling at the stop last month", minimap_t::MAP_MAIL_HANDLING_VOLUME },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "Handling goods", "The number of goods that handling at the stop last month", minimap_t::MAP_GOODS_HANDLING_VOLUME },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "by_waiting_passengers", "Show how many people/much is waiting at halts", minimap_t::MAP_PAX_WAITING },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "by_waiting_mails", "Show how many mails are waiting at halts", minimap_t::MAP_MAIL_WAITING },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "by_waiting_goods", "Show how many goods are waiting at halts", minimap_t::MAP_GOODS_WAITING },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "Origin", "Show initial passenger departure", minimap_t::MAP_ORIGIN },
	{ COL_LIGHT_PURPLE, COL_DARK_PURPLE, "Service", "Show how many convoi reach a station", minimap_t::MAP_SERVICE },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "map_btn_freight", "Show transported freight/freight network", minimap_t::MAP_FREIGHT },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Traffic", "Show usage of network", minimap_t::MAP_TRAFFIC },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Wear", "Show the condition of ways", minimap_t::MAP_CONDITION },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Congestion", "Show how congested that roads are", minimap_t::MAP_CONGESTION },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Speedlimit", "Show speedlimit of ways", minimap_t::MAX_SPEEDLIMIT },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Weight limit", "Show the weight limit of ways", minimap_t::MAP_WEIGHTLIMIT },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Tracks", "Highlight railroad tracks", minimap_t::MAP_TRACKS },
	{ COL_LIGHT_ORANGE, COL_DARK_ORANGE, "Powerlines", "Highlite electrical transmission lines", minimap_t::MAP_POWERLINES },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "PaxDest", "Overlay passenger destinations when a town window is open", minimap_t::MAP_PAX_DEST },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "map_btn_building_level", "Show level of city buildings", minimap_t::MAP_LEVEL },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "Stop coverage", "Show the distance to the nearest station", minimap_t::MAP_STATION_COVERAGE },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "Commuting", "Show the success rate for commuting passengers", minimap_t::MAP_ACCESSIBILITY_COMMUTING },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "Visiting", "Show the success rate for visiting passengers", minimap_t::MAP_ACCESSIBILITY_TRIP },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "Staffing", "Show the staff shortage rate", minimap_t::MAP_STAFF_FULFILLMENT },
	{ COL_HORIZON_BLUE, COL_SOFT_BLUE,  "Mail delivery", "Show the success rate for mail delivery", minimap_t::MAP_MAIL_DELIVERY },
	{ COL_WHITE,        COL_GREY5,       "Ownership", "Show the owenership of infrastructure", minimap_t::MAP_OWNER },
	{ COL_WHITE,        COL_GREY5,       "Forest", "Highlite forests", minimap_t::MAP_FOREST }
};

#define scrolly (*p_scrolly)

map_frame_t::map_frame_t() :
	gui_frame_t( NULL )
{
	// init statics
	old_ij = koord::invalid;
	zoomed = false;

	// init map
	minimap_t *karte = minimap_t::get_instance();
	karte->init();
	karte->set_display_mode( ( minimap_t::MAP_DISPLAY_MODE)env_t::default_mapmode );
	// show all players by default
	karte->player_showed_on_map = -1;

	p_scrolly = new gui_scrollpane_map_t( minimap_t::get_instance());
	p_scrolly->set_maximize( true );
	p_scrolly->set_min_width( D_DEFAULT_WIDTH-D_MARGIN_LEFT-D_MARGIN_RIGHT );
	// initialize scrollbar positions -- LATER
	const scr_size size = karte->get_size();
	const scr_size s_size=scrolly.get_size();
	const koord ij = welt->get_viewport()->get_world_position();
	const scr_size win_size = size-s_size; // this is the visible area

	scrolly.set_scroll_position( clamp(ij.x-win_size.w/2, 0, size.w), clamp(ij.y-win_size.h/2, 0, size.h) );
	scrolly.set_focusable( true );
	scrolly.set_scrollbar_mode(scrollbar_t::show_always);


	set_table_layout(1,0);

	// first row of controls
	zoom_row = add_table(8,1);
	{
		// zoom levels label
		new_component<gui_label_t>("map zoom");

		// zoom levels arrow left
		zoom_buttons[0].init(button_t::repeatarrowleft, NULL);
		zoom_buttons[0].add_listener( this );
		add_component( zoom_buttons+0 );

		// zoom level value label
		sint16 zoom_in, zoom_out;
		minimap_t::get_instance()->get_zoom_factors(zoom_out, zoom_in);
		zoom_value_label.buf().printf("%i:%i", zoom_in, zoom_out );
		zoom_value_label.set_width(proportional_string_width("10:1")); // This is intended that the selector size does not fluctuate in proportional fonts.
		zoom_value_label.update();
		add_component( &zoom_value_label );

		// zoom levels arrow right
		zoom_buttons[1].init(button_t::repeatarrowright, NULL);
		zoom_buttons[1].add_listener( this );
		add_component( zoom_buttons+1 );

		// rotate map 45 degrees (isometric view)
		b_rotate45.init( button_t::square_state, "isometric map");
		b_rotate45.set_tooltip("Similar view as the main window");
		b_rotate45.add_listener(this);
		b_rotate45.pressed = karte->is_isometric();
		add_component(&b_rotate45);

		b_overlay_networks.init(button_t::square_state, "Networks");
		b_overlay_networks.set_tooltip("Overlay schedules/network");
		b_overlay_networks.add_listener(this);
		b_overlay_networks.pressed = (env_t::default_mapmode & minimap_t::MAP_LINES)!=0;
		add_component( &b_overlay_networks );

		// show contour
		b_show_contour.init(button_t::square_state, "Show contour");
		b_show_contour.set_tooltip("Color-coded terrain according to altitude.");
		b_show_contour.add_listener(this);
		b_show_contour.pressed = karte->show_contour;
		add_component(&b_show_contour);

		// show the building layer
		b_show_buildings.init(button_t::square_state, "Show buildings");
		b_show_buildings.set_tooltip("Displays buildings in gray and stations in red and have priority over bridges and tunnels.");
		b_show_buildings.add_listener(this);
		b_show_buildings.pressed = karte->show_buildings;
		add_component(&b_show_buildings);
	}
	end_table();

	// second row of controls
	add_table(4,1);
	{
		// first row of controls
		b_show_network_option.init(button_t::roundbox_state, "Show networks");
		b_show_network_option.set_tooltip("Shows option buttons about networks.");
		b_show_network_option.set_size(D_BUTTON_SIZE);
		b_show_network_option.add_listener(this);
		add_component(&b_show_network_option);

		// selections button
		b_show_legend.init(button_t::roundbox_state, "Show legend");
		b_show_legend.set_tooltip("Shows buttons on special topics.");
		b_show_legend.set_size(D_BUTTON_SIZE);
		b_show_legend.add_listener(this);
		add_component(&b_show_legend);

		// scale button
		b_show_scale.init(button_t::roundbox_state, "Show map scale");
		b_show_scale.set_tooltip("Shows the color code for several selections.");
		b_show_scale.set_size(D_BUTTON_SIZE);
		b_show_scale.add_listener(this);
		add_component(&b_show_scale);

		// industry list button
		b_show_directory.init(button_t::roundbox_state, "Show industry");
		if (skinverwaltung_t::open_window) {
			b_show_directory.set_image(skinverwaltung_t::open_window->get_image_id(0));
			b_show_directory.set_image_position_right(true);
		}
		b_show_directory.set_tooltip("Shows a listing with all industries on the map.");
		b_show_directory.set_size(D_BUTTON_SIZE);
		b_show_directory.add_listener(this);
		add_component(&b_show_directory);
	}
	end_table();

	// networks filter container
	network_filter_container.set_visible(false);
	add_component(&network_filter_container);

	network_filter_container.set_table_layout(4,1);
	// insert selections: in filter container

	// player combo for network overlay
	viewed_player_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All"), SYSCOL_TEXT);
	viewable_players[ 0 ] = -1;
	for(  int np = 0, count = 1;  np < MAX_PLAYER_COUNT;  np++  ) {
		if(  welt->get_player( np )  /*&&  welt->get_player( np )->get_finance()->has_convoi()*/) { //player does not need any convoy, might be a pure infrastructure company
			viewed_player_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(welt->get_player( np )->get_name(), color_idx_to_rgb(welt->get_player( np )->get_player_color1()+env_t::gui_player_color_dark));
			viewable_players[ count++ ] = np;
		}
	}
	viewed_player_c.set_selection(0);
	viewed_player_c.set_focusable( true );
	viewed_player_c.add_listener( this );
	network_filter_container.add_component(&viewed_player_c);

	// freight combo for network overlay
	{
		viewable_freight_types.append(NULL);
		freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("All freight types"), SYSCOL_TEXT) ;
		viewable_freight_types.append(goods_manager_t::passengers);
		freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>( translator::translate("Passagiere"), SYSCOL_TEXT, skinverwaltung_t::passengers->get_image_id(0)) ;
		viewable_freight_types.append(goods_manager_t::mail);
		freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>( translator::translate("Post"), SYSCOL_TEXT, skinverwaltung_t::mail->get_image_id(0)) ;
		viewable_freight_types.append(goods_manager_t::none); // for all freight ...
		freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>( translator::translate("Fracht"), SYSCOL_TEXT) ;
		for(  int i = 0;  i < goods_manager_t::get_max_catg_index();  i++  ) {
			const goods_desc_t *freight_type = goods_manager_t::get_info_catg(i);
			const int index = freight_type->get_catg_index();
			if(  index == goods_manager_t::INDEX_NONE  ||  freight_type->get_catg()==0  ) {
				continue;
			}
			freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(freight_type->get_catg_name()), SYSCOL_TEXT, freight_type->get_catg_symbol());
			viewable_freight_types.append(freight_type);
		}
		for(  int i=0;  i < goods_manager_t::get_count();  i++  ) {
			const goods_desc_t *ware = goods_manager_t::get_info(i);
			if(  ware->get_catg() == 0  &&  ware->get_index() > 2  ) {
				// Special freight: Each good is special
				viewable_freight_types.append(ware);
				freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>( translator::translate(ware->get_name()), SYSCOL_TEXT, ware->get_catg_symbol()) ;
			}
		}
	}
	freight_type_c.set_selection(0);
	minimap_t::get_instance()->freight_type_group_index_showed_on_map = NULL;
	freight_type_c.set_focusable( true );
	freight_type_c.add_listener( this );
	network_filter_container.add_component(&freight_type_c);

	// mode of transport combo for network overlay
	for (int i = 0; i < simline_t::MAX_LINE_TYPE; i++) {
		transport_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(simline_t::get_linetype_name((simline_t::linetype)i), SYSCOL_TEXT);
	}

	transport_type_c.set_selection(0);
	minimap_t::get_instance()->transport_type_showed_on_map = simline_t::line;
	transport_type_c.set_focusable( true );
	transport_type_c.add_listener( this );
	network_filter_container.add_component(&transport_type_c);

	b_overlay_networks_load_factor.init(button_t::square_state, "Free Capacity");
	b_overlay_networks_load_factor.set_tooltip("Color according to transport capacity left");
	b_overlay_networks_load_factor.add_listener(this);
	b_overlay_networks_load_factor.pressed = 0;
	minimap_t::get_instance()->show_network_load_factor = 0;
	network_filter_container.add_component( &b_overlay_networks_load_factor );

	// filter container
	filter_container.set_visible(false);
	add_component(&filter_container);
	filter_container.set_table_layout(1, 0);

	filter_container.add_table(5,0)->set_force_equal_columns(true);
	// insert filter buttons in legend container
	for (int index=0; index<MAP_MAX_BUTTONS; index++) {
		filter_buttons[index].init( button_t::box_state | button_t::flexible, button_init[index].button_text);
		filter_buttons[index].text_color = SYSCOL_TEXT;
		filter_buttons[index].set_tooltip( button_init[index].tooltip_text );
		filter_buttons[index].add_listener(this);
		filter_container.add_component(filter_buttons + index);
	}
	filter_buttons[6].set_image(skinverwaltung_t::passengers->get_image_id(0));
	filter_buttons[7].set_image(skinverwaltung_t::mail->get_image_id(0));
	filter_buttons[8].set_image(skinverwaltung_t::goods->get_image_id(0));
	filter_buttons[9].set_image(skinverwaltung_t::passengers->get_image_id(0));
	filter_buttons[10].set_image(skinverwaltung_t::mail->get_image_id(0));
	filter_buttons[11].set_image(skinverwaltung_t::goods->get_image_id(0));
	filter_buttons[12].set_image(skinverwaltung_t::passengers->get_image_id(0));
	filter_container.end_table();
	update_buttons();

	// scale container
	scale_container.set_table_layout(1,0);
	scale_container.set_visible(false);
	scale_container.add_table(3,1);
	{
		scale_container.new_component<gui_label_t>("min");
		scale_container.new_component<gui_scale_t>();
		scale_container.new_component<gui_label_t>("max");
	}
	scale_container.end_table();
	// depot color container
	cont_depot_color_legend.set_table_layout(12,2);
	{
		cont_depot_color_legend.set_alignment(ALIGN_CENTER_V);
		cont_depot_color_legend.set_visible((env_t::default_mapmode & minimap_t::MAP_DEPOT) != 0);
		cont_depot_color_legend.set_rigid(false);
		cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::bahndepot));
		cont_depot_color_legend.new_component<gui_label_t>("Bahndepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::strassendepot));
		cont_depot_color_legend.new_component<gui_label_t>("Strassendepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		if (depotlist_frame_t::is_available_wt(water_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::schiffdepot));
			cont_depot_color_legend.new_component<gui_label_t>("Schiffdepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		}
		if (depotlist_frame_t::is_available_wt(air_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::airdepot));
			cont_depot_color_legend.new_component<gui_label_t>("Hangar"); cont_depot_color_legend.new_component<gui_empty_t>();
		}
		if (depotlist_frame_t::is_available_wt(monorail_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::monoraildepot));
			cont_depot_color_legend.new_component<gui_label_t>("Monoraildepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		}
		if (depotlist_frame_t::is_available_wt(tram_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::tramdepot));
			cont_depot_color_legend.new_component<gui_label_t>("Tramdepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		}
		if (depotlist_frame_t::is_available_wt(maglev_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::maglevdepot));
			cont_depot_color_legend.new_component<gui_label_t>("Maglevdepot"); cont_depot_color_legend.new_component<gui_empty_t>();
		}
		if (depotlist_frame_t::is_available_wt(narrowgauge_wt)) {
			cont_depot_color_legend.new_component<gui_depotbox_t>(minimap_t::get_depot_color(obj_t::narrowgaugedepot));
			cont_depot_color_legend.new_component<gui_label_t>("Narrowgaugedepot");
		}
	}
	scale_container.add_component(&cont_depot_color_legend);
	add_component(&scale_container);

	// map scrolly
	scrolly.set_show_scroll_x(true);
	scrolly.set_scroll_discrete_y(false);
	take_component(p_scrolly);

	// restore window size and options
	show_hide_legend( legend_visible );
	show_hide_network_option( network_option_visible );
	show_hide_scale( scale_visible );

	set_title();
	reset_min_windowsize();
	set_windowsize( window_size );
	set_resizemode(diagonal_resize);
}


void map_frame_t::update_buttons()
{
	for(  int i=0;  i<MAP_MAX_BUTTONS;  i++  ) {
		filter_buttons[i].pressed = (button_init[i].mode&env_t::default_mapmode)!=0;
		filter_buttons[i].background_color = color_idx_to_rgb(filter_buttons[i].pressed ? button_init[i].select_color : button_init[i].color);
	}
}


void map_frame_t::show_hide_legend(const bool show)
{
	filter_container.set_visible(show);
	b_show_legend.pressed = show;
	legend_visible = show;
	reset_min_windowsize();
}


void map_frame_t::show_hide_network_option(const bool show)
{
	network_filter_container.set_visible(show);
	b_show_network_option.pressed = show;
	network_option_visible = show;
	reset_min_windowsize();
}


void map_frame_t::show_hide_scale(const bool show)
{
	scale_container.set_visible(show);
	b_show_scale.pressed = show;
	scale_visible = show;
	reset_min_windowsize();
}


void map_frame_t::activate_individual_network_mode(koord network_origin)
{
	b_overlay_networks.pressed = true;
	show_hide_network_option(true);
	show_hide_legend(false);
	show_hide_scale(false);

	env_t::default_mapmode |= minimap_t::MAP_LINES;
	minimap_t::get_instance()->set_display_mode((minimap_t::MAP_DISPLAY_MODE)env_t::default_mapmode);
	scr_coord center = minimap_t::get_instance()->map_to_screen_coord(network_origin);
	const scr_size s_size = scrolly.get_size();
	scrolly.set_scroll_position(max(0,center.x-(s_size.w/2)), max(0,center.y-(s_size.h/2)));
}

void map_frame_t::set_halt(halthandle_t halt)
{
	minimap_t::get_instance()->set_selected_halt(halt.is_bound() ? halt : halthandle_t());
	if( halt.is_null() ) { return;  };

	activate_individual_network_mode(halt->get_basis_pos());
	title_buf.clear();
	title_buf.printf("%s > %s > %s", translator::translate("Reliefkarte"), translator::translate("Networks"), halt->get_name());
	set_name(title_buf);
}

void map_frame_t::set_title()
{
	title_buf.clear();
	title_buf.append(translator::translate("Reliefkarte"));
	if (b_overlay_networks.pressed) {
		title_buf.printf(" > %s", translator::translate("Networks"));
		if (selected_cnv.is_bound()) {
			title_buf.append(" > ");
			if( selected_cnv->get_line().is_bound() ){
				title_buf.append(selected_cnv->get_line()->get_name());
			}
			else {
				title_buf.append(selected_cnv->get_name());
			}
		}
	}
	set_name( title_buf );
}


bool map_frame_t::action_triggered( gui_action_creator_t *comp, value_t)
{
	if(comp==&b_show_legend) {
		show_hide_legend( !b_show_legend.pressed );
	}
	else if(comp==&b_show_network_option) {
		show_hide_network_option( !b_show_network_option.pressed );
	}
	else if(comp==&b_show_scale) {
		show_hide_scale( !b_show_scale.pressed );
	}
	else if(comp==&b_show_directory) {
		scr_coord_val new_dialog_pos_x = get_pos().x+get_windowsize().w;
		if (new_dialog_pos_x >= display_get_width()) {
			new_dialog_pos_x = (display_get_width() + new_dialog_pos_x)>>1;
		}
		create_win({ new_dialog_pos_x, get_pos().y }, new factory_legend_t(this), w_info, magic_factory_legend);
	}
	else if(comp==zoom_buttons+1) {
		// zoom out
		p_scrolly->zoom(true);
	}
	else if(comp==zoom_buttons+0) {
		// zoom in
		p_scrolly->zoom(false);
	}
	else if(comp==&b_rotate45) {
		// rotated/straight map
		minimap_t::get_instance()->toggle_isometric();
		minimap_t::get_instance()->calc_map_size();
		b_rotate45.pressed = minimap_t::get_instance()->is_isometric();
		scrolly.set_size( scrolly.get_size() );
		zoomed = true;
		old_ij = koord::invalid;
	}
	else if (comp == &b_show_contour) {
		// terrain heights color scale
		minimap_t::get_instance()->show_contour ^= 1;
		b_show_contour.pressed = minimap_t::get_instance()->show_contour;
		minimap_t::get_instance()->invalidate_map_lines_cache();
	}
	else if (comp == &b_show_buildings) {
		// terrain heights color scale
		minimap_t::get_instance()->show_buildings ^= 1;
		b_show_buildings.pressed = minimap_t::get_instance()->show_buildings;
		minimap_t::get_instance()->invalidate_map_lines_cache();
	}
	else if(comp==&b_overlay_networks) {
		// This button deactivates station mode.
		minimap_t::get_instance()->set_selected_halt(halthandle_t());
		b_overlay_networks.pressed ^= 1;
		if(  b_overlay_networks.pressed  ) {
			env_t::default_mapmode |= minimap_t::MAP_LINES;
		}
		else {
			env_t::default_mapmode &= ~minimap_t::MAP_LINES;
		}
		minimap_t::get_instance()->set_display_mode(  ( minimap_t::MAP_DISPLAY_MODE)env_t::default_mapmode  );
		minimap_t::get_instance()->invalidate_map_lines_cache();
		set_title();
	}
	else if (  comp == &viewed_player_c  ) {
		minimap_t::get_instance()->player_showed_on_map = viewable_players[viewed_player_c.get_selection()];
		minimap_t::get_instance()->invalidate_map_lines_cache();
	}
	else if (  comp == &transport_type_c  ) {
		minimap_t::get_instance()->transport_type_showed_on_map = transport_type_c.get_selection();
		minimap_t::get_instance()->invalidate_map_lines_cache();
	}
	else if (  comp == &freight_type_c  ) {
		minimap_t::get_instance()->freight_type_group_index_showed_on_map = viewable_freight_types[freight_type_c.get_selection()];
		minimap_t::get_instance()->invalidate_map_lines_cache();
		reset_min_windowsize();
	}
	else if (  comp == &b_overlay_networks_load_factor  ) {
		minimap_t::get_instance()->show_network_load_factor = !minimap_t::get_instance()->show_network_load_factor;
		b_overlay_networks_load_factor.pressed = !b_overlay_networks_load_factor.pressed;
		minimap_t::get_instance()->invalidate_map_lines_cache();
	}
	else {
		for(  int i=0;  i<MAP_MAX_BUTTONS;  i++  ) {
			if(  comp == filter_buttons+i  ) {
				if(  filter_buttons[i].pressed  ) {
					env_t::default_mapmode &= ~button_init[i].mode;
				}
				else {
					if(  (button_init[i].mode & minimap_t::MAP_MODE_FLAGS) == 0  ) {
						// clear all persistent states
						env_t::default_mapmode &= minimap_t::MAP_MODE_FLAGS;
					}
					else if(  button_init[i].mode & minimap_t::MAP_MODE_HALT_FLAGS  ) {
						// clear all other halt states
						env_t::default_mapmode &= ~minimap_t::MAP_MODE_HALT_FLAGS;
					}
					env_t::default_mapmode |= button_init[i].mode;
				}
				filter_buttons[i].pressed ^= 1;
				if (button_init[i].mode == minimap_t::MAP_DEPOT) {
					cont_depot_color_legend.set_visible((env_t::default_mapmode & minimap_t::MAP_DEPOT) != 0);
					reset_min_windowsize();
				}
				break;
			}
		}
		minimap_t::get_instance()->set_display_mode(  ( minimap_t::MAP_DISPLAY_MODE)env_t::default_mapmode  );
		update_buttons();
	}
	return true;
}


/**
 * Report events to the GUI-components
 */
bool map_frame_t::infowin_event(const event_t *ev)
{
	event_t ev2 = *ev;
	ev2.move_origin(scrolly.get_pos() + scr_coord(0, D_TITLEBAR_HEIGHT));

	if(ev->ev_class == INFOWIN) {
		if(ev->ev_code == WIN_OPEN) {
			minimap_t::get_instance()->set_xy_offset_size( scr_coord(0,0), scr_size(0,0) );
		}
		else if(ev->ev_code == WIN_CLOSE) {
			minimap_t::get_instance()->is_visible = false;
		}
	}

	return gui_frame_t::infowin_event(ev);
}


/**
 * size window in response and save it in static size
 */
void map_frame_t::set_windowsize(scr_size size)
{
	gui_frame_t::set_windowsize( size );
	window_size = get_windowsize();
}


/**
 * Draw new component. The values to be passed refer to the window
 * i.e. It's the screen coordinates of the window where the
 * component is displayed.

 */
void map_frame_t::draw(scr_coord pos, scr_size size)
{
	// update our stored screen position
	screenpos = pos;
	minimap_t::get_instance()->set_xy_offset_size( scr_coord(scrolly.get_scroll_x(), scrolly.get_scroll_y()), scrolly.get_client().get_size() );

	// first: check if cursor within map screen size
	koord ij = welt->get_viewport()->get_world_position();
	if(welt->is_within_limits(ij)) {
		scr_coord center = minimap_t::get_instance()->map_to_screen_coord(ij);
		// only re-center if zoomed or world position has changed and its outside visible area
		const scr_size size = scrolly.get_size();
		if(zoomed  ||  ( old_ij != ij  &&
				( scrolly.get_scroll_x()>center.x  ||  scrolly.get_scroll_x()+size.w<=center.x  ||
				  scrolly.get_scroll_y()>center.y  ||  scrolly.get_scroll_y()+size.h<=center.y ) ) ) {
			// re-center cursor by scrolling
			scrolly.set_scroll_position( max(0,center.x-(size.w/2)), max(0,center.y-(size.h/2)) );
			zoomed = false;
			// update zoom factors and zoom label
			sint16 zoom_in, zoom_out;
			minimap_t::get_instance()->get_zoom_factors(zoom_out, zoom_in);
			zoom_value_label.buf().printf("%i:%i", zoom_in, zoom_out);
			zoom_value_label.update();
			zoom_row->set_size(zoom_row->get_size());
		}
		// remember world position, we do not want to have surprises when scrolling later on
		old_ij = ij;
	}

	// update titlebar
	if (b_overlay_networks.pressed) {
		if (minimap_t::get_instance()->get_selected_cnv() != selected_cnv) {
			selected_cnv = minimap_t::get_instance()->get_selected_cnv();
			set_title();
		}
	}
	b_show_directory.pressed = win_get_magic( magic_factory_legend );

	// draw all child controls
	gui_frame_t::draw(pos, size);

	// may add compass
	if(  skinverwaltung_t::compass_map  &&  env_t::compass_map_position!=0  ) {
		display_img_aligned( skinverwaltung_t::compass_map->get_image_id( welt->get_settings().get_rotation()+minimap_t::get_instance()->is_isometric()*4 ),
							scrolly.get_client()+pos+scr_coord(4,4+D_TITLEBAR_HEIGHT)-scr_size(8,8), env_t::compass_map_position, false );
	}
}


void map_frame_t::rdwr( loadsave_t *file )
{
	file->rdwr_bool( legend_visible );
	file->rdwr_bool( scale_visible );
	bool open_factory_legend = win_get_magic(magic_factory_legend);
	file->rdwr_bool( open_factory_legend );
	file->rdwr_long( env_t::default_mapmode );

	file->rdwr_bool( b_overlay_networks_load_factor.pressed );

	minimap_t::get_instance()->rdwr(file);

	window_size.rdwr(file);
	scrolly.rdwr(file);

	viewed_player_c.rdwr(file);
	transport_type_c.rdwr(file);
	freight_type_c.rdwr(file);

	if( file->is_version_ex_atleast(14,50) ){
		file->rdwr_bool( network_option_visible );
	}

	if(  file->is_loading()  ) {
		set_windowsize( window_size );
		// notify minimap of new settings
		minimap_t::get_instance()->calc_map_size();
		scrolly.set_size( scrolly.get_size() );

		minimap_t::get_instance()->set_display_mode(( minimap_t::MAP_DISPLAY_MODE)env_t::default_mapmode);
		update_buttons();

		show_hide_legend(legend_visible);
		show_hide_network_option(network_option_visible);
		show_hide_scale(scale_visible);

		b_overlay_networks.pressed = (env_t::default_mapmode & minimap_t::MAP_LINES)!=0;

		minimap_t::get_instance()->player_showed_on_map = viewable_players[viewed_player_c.get_selection()];
		minimap_t::get_instance()->transport_type_showed_on_map = transport_type_c.get_selection();
		minimap_t::get_instance()->freight_type_group_index_showed_on_map = viewable_freight_types[freight_type_c.get_selection()];
		minimap_t::get_instance()->show_network_load_factor = b_overlay_networks_load_factor.pressed;
		minimap_t::get_instance()->invalidate_map_lines_cache();
		set_title();

		if (open_factory_legend) {
			scr_coord_val new_dialog_pos_x = get_pos().x+get_windowsize().w;
			if (new_dialog_pos_x >= display_get_width()) {
				new_dialog_pos_x = (display_get_width() + new_dialog_pos_x)>>1;
			}
			create_win({ new_dialog_pos_x, get_pos().y }, new factory_legend_t(this), w_info, magic_factory_legend);
		}
	}
}
