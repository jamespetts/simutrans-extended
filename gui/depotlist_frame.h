/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_DEPOTLIST_FRAME_H
#define GUI_DEPOTLIST_FRAME_H


#include "simwin.h"
#include "gui_frame.h"
#include "../simdepot.h"

#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_waytype_tab_panel.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_combobox.h"
#include "components/sortable_table.h"
#include "components/sortable_table_header.h"
#include "../player/finance.h"

class depot_t;

class depot_type_cell_t : public values_cell_t
{
public:
	depot_type_cell_t(obj_t::typ typ, bool is_electrified);

	const uint8 get_type() const OVERRIDE { return cell_values; }

	void draw(scr_coord offset) OVERRIDE;
};


class depotlist_row_t : public gui_sort_table_row_t
{
public:
	enum sort_mode_t {
		DEPOT_LEVEL,
		DEPOT_NAME,
		DEPOT_CONVOYS,
		DEPOT_VEHICLES,
		DEPOT_COORD,
		DEPOT_REGION,
		DEPOT_CITY,
		DEPOT_MAINTENANCE,
		DEPOT_BUILT_DATE,
		MAX_COLS
	};

	static int sort_mode;
	static bool sortreverse;

private:
	depot_t *depot;

	// update flags
	uint32 old_convoys;
	uint32 old_vehicles;

public:
	depotlist_row_t(depot_t *dep);

	char const* get_text() const OVERRIDE { return depot->get_name(); }

	void show_depot();

	bool infowin_event(event_t const* ev) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;

	static bool compare(const gui_component_t* aa, const gui_component_t* bb);
};


class depotlist_frame_t : public gui_frame_t, private action_listener_t
{
private:
	gui_sort_table_header_t table_header;
	gui_aligned_container_t cont_sortable;
	gui_scrolled_list_t scrolly;
	gui_scrollpane_t scroll_tab;

	gui_label_buf_t lb_depot_counter;

	gui_waytype_tab_panel_t tabs;

	uint32 last_depot_count;
	static uint8 selected_tab;

	void init_table();

	player_t *player;

public:
	depotlist_frame_t();

	depotlist_frame_t(player_t *player);

	const char *get_help_filename() const OVERRIDE {return "depotlist.txt"; }

	void fill_list();

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


#endif
