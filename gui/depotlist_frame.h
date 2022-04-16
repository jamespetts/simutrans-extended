/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_DEPOTLIST_FRAME_H
#define GUI_DEPOTLIST_FRAME_H


#include "simwin.h"
#include "gui_frame.h"
#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_waytype_tab_panel.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_combobox.h"
#include "../player/finance.h"

class depot_t;


class depotlist_frame_t : public gui_frame_t, private action_listener_t
{
private:
	gui_combobox_t sortedby;
	button_t sort_order;
	gui_scrolled_list_t scrolly;

	gui_waytype_tab_panel_t tabs;

	uint32 last_depot_count;
	static uint8 selected_tab;

	void fill_list();

	player_t *player;

public:
	depotlist_frame_t();

	depotlist_frame_t(player_t *player);

	const char *get_help_filename() const OVERRIDE {return "depotlist.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	// yes we can reload
	uint32 get_rdwr_id() OVERRIDE { return magic_depotlist; }
	void rdwr(loadsave_t *file) OVERRIDE;

	// Whether the waytype is available in pakset
	// This is determined by whether the pakset has a vehicle.
	static bool is_available_wt(waytype_t wt);

	void map_rotate90( sint16 ) OVERRIDE { fill_list(); }
};


class depotlist_stats_t : public gui_aligned_container_t, public gui_scrolled_list_t::scrollitem_t
{
private:
	depot_t *depot;
	gui_label_buf_t lb_name, lb_cnv_count, lb_vh_count, lb_region;
	gui_image_t waytype_symbol;
	button_t gotopos;

	void update_label();

public:
	static uint8 sort_mode;
	static bool reverse;
	static uint16 name_width;

	depotlist_stats_t(depot_t *);

	void draw( scr_coord pos) OVERRIDE;

	char const* get_text() const OVERRIDE { return ""; /* label.buf().get_str(); */ }
	bool infowin_event(const event_t *) OVERRIDE;
	bool is_valid() const OVERRIDE;
	void set_size(scr_size size) OVERRIDE;

	static bool compare(const gui_component_t *a, const gui_component_t *b );
};

#endif
