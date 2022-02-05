/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CITYLIST_FRAME_T_H
#define GUI_CITYLIST_FRAME_T_H


#include "simwin.h"
#include "gui_frame.h"
#include "citylist_stats_t.h"
#include "components/action_listener.h"
#include "components/gui_button.h"
#include "components/gui_button_to_chart.h"
#include "components/gui_label.h"
#include "components/gui_chart.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_tab_panel.h"
#include "components/gui_combobox.h"

// for the number of cost entries
#include "../simworld.h"

/**
 * City list window
 */
class citylist_frame_t : public gui_frame_t, private action_listener_t
{

 private:
	static const char *sort_text[citylist_stats_t::SORT_MODES];
	static const char *display_mode_text[citylist_stats_t::CITYLIST_MODES];

	static const char hist_type[karte_t::MAX_WORLD_COST][21];
	static const char hist_type_tooltip[karte_t::MAX_WORLD_COST][256];
	static const uint8 hist_type_color[karte_t::MAX_WORLD_COST];
	static const uint8 hist_type_type[karte_t::MAX_WORLD_COST];

	gui_combobox_t sortedby, region_selector, cb_display_mode;
	button_t sorteddir;
	button_t filter_within_network;

	static char name_filter[256];
	char last_name_filter[256];
	gui_textinput_t name_filter_input;

	gui_scrolled_list_t scrolly;

	gui_aligned_container_t container_year, container_month;
	gui_chart_t chart, mchart;
	gui_button_to_chart_array_t button_to_chart;
	gui_tab_panel_t year_month_tabs;
	gui_tab_panel_t main;

	gui_aligned_container_t list, statistics;
	gui_label_buf_t citizens;
	gui_label_updown_t fluctuation_world;
#ifdef DEBUG
	gui_label_buf_t lb_worker_shortage, lb_job_shortage;
#endif

	void fill_list();
	void update_label();
	/*
     * All filter settings are static, so they are not reset each
     * time the window closes.
     */
    static citylist_stats_t::sort_mode_t sortby;

 public:
    citylist_frame_t();

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	const char *get_help_filename() const OVERRIDE {return "citylist_filter.txt"; }

	static void set_sortierung(const citylist_stats_t::sort_mode_t& sm) { sortby = sm; }

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE { fill_list(); }

	void rdwr(loadsave_t* file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_citylist_frame_t; }
};

#endif
