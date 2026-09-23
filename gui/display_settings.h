/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_DISPLAY_SETTINGS_H
#define GUI_DISPLAY_SETTINGS_H


#include "simwin.h"
#include "gui_frame.h"
#include "components/gui_divider.h"
#include "components/gui_label.h"
#include "components/gui_button.h"
#include "components/gui_numberinput.h"
#include "components/gui_combobox.h"
#include "components/gui_tab_panel.h"
#include "../dataobj/environment.h"


/**
 * Menu with display settings
 */
class gui_settings_t : public gui_aligned_container_t, public action_listener_t
{
private:
	gui_numberinput_t screen_scale_numinp;
	button_t screen_scale_auto;

	gui_label_buf_t
		frame_time_value_label,
		idle_time_value_label,
		fps_value_label,
		simloops_value_label,

		rebuild_connexion_label,
		eligible_halts_label,
		fill_path_matrix_label,
		explore_path_label,
		reroute_goods_label,
		status_label,

		reading_index_label,
		cities_awaiting_private_car_route_check_label,
		cities_to_process_label;

public:
	button_t toolbar_pos[4];
	button_t reselect_closes_tool;
	button_t info_window_toggler[8];
	button_t fullscreen, borderless;

	gui_settings_t();

	void draw( scr_coord offset ) OVERRIDE;
	bool action_triggered( gui_action_creator_t *comp, value_t v) OVERRIDE;
};


class map_settings_t : public gui_aligned_container_t, public action_listener_t
{
private:
	char time_str[env_t::DATE_FMT_LIMIT_FOR_LOOPS][64];
	gui_numberinput_t cursor_hide_range;
	gui_combobox_t time_setting, hide_buildings;
public:
	gui_numberinput_t
		inp_underground_level,
		brightness,
		scrollspeed,
		scroll_threshold;
	map_settings_t();
	bool action_triggered( gui_action_creator_t *comp, value_t v ) OVERRIDE;
	void draw(scr_coord offset) OVERRIDE;
};

class label_settings_t : public gui_aligned_container_t, public action_listener_t
{
private:
	gui_combobox_t convoy_tooltip, money_booking, convoy_nameplate, convoy_loadingbar, freight_waiting_bar, factory_tooltip;
	button_t bt_convoy_id_plate[2];
public:
	label_settings_t();
	bool action_triggered(gui_action_creator_t *comp, value_t v) OVERRIDE;
	void draw(scr_coord offset) OVERRIDE;
};


class traffic_settings_t : public gui_aligned_container_t, public action_listener_t
{
private:
	gui_numberinput_t traffic_density;
	gui_combobox_t follow_mode;
public:
	traffic_settings_t();
	bool action_triggered( gui_action_creator_t *comp, value_t v ) OVERRIDE;
};

/**
 * Display settings dialog
 */
class color_gui_t : public gui_frame_t, private action_listener_t
{
private:
	gui_settings_t gui_settings;
	map_settings_t map_settings;
	label_settings_t station_settings;
	traffic_settings_t traffic_settings;
	gui_scrollpane_t scrolly_gui, scrolly_map, scrolly_station, scrolly_traffic;
	gui_tab_panel_t tabs;

public:
	color_gui_t();

	/**
	 * Some windows have associated help text.
	 * @return The help file name or NULL
	 */
	const char * get_help_filename() const OVERRIDE { return "display.txt"; }

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_color_gui_t; }

	void rdwr(loadsave_t*) OVERRIDE;
};

#endif
