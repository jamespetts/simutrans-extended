/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_VEHICLELIST_FRAME_H
#define GUI_VEHICLELIST_FRAME_H


#include "simwin.h"
#include "gui_frame.h"
#include "components/gui_combobox.h"
#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_waytype_tab_panel.h"
#include "components/gui_table.h"

class vehicle_desc_t;
class goods_desc_t;


class vehiclelist_frame_t : public gui_frame_t, private action_listener_t
{
public:
	enum veh_spec_col_t {
		VL_IMAGE,
		VL_STATUSBAR,
		VL_COST,
		VL_ENGINE_TYPE,
		VL_POWER,
		VL_TRACTIVE_FORCE,
		VL_FREIGHT_TYPE,
		VL_CAPACITY,
		VL_SPEED,
		VL_WEIGHT,
		VL_AXLE_LOAD,
		VL_INTRO_DATE,
		VL_RETIRE_DATE,
		VL_MAX_SPECS
	};
	static int cell_width[vehiclelist_frame_t::VL_MAX_SPECS];
	static int stats_width;

	// vehicle status(mainly timeline) filter index
	enum status_filter_t {
		VL_SHOW_FUTURE =0,    // gray
		VL_SHOW_AVAILABLE,    // green
		VL_SHOW_OUT_OF_PROD,  // royalblue
		VL_SHOW_OUT_OBSOLETE, // blue
		VL_FILTER_UPGRADE_ONLY, // purple
		VL_MAX_STATUS_FILTER,
	};

	// 1=fuel filer on, 2=freight type fiter on
	enum {
		VL_NO_FILTER      = 0,
		VL_FILTER_FUEL    = 1<<0,
		VL_FILTER_FREIGHT = 1<<1,
		VL_FILTER_UPGRADABLE = 1<<2
	};
	static uint8 filter_flag;

	// false=show name, true=show side view
	static bool side_view_mode;

private:
	button_t bt_timeline_filters[VL_MAX_STATUS_FILTER];
	button_t bt_upgradable;
	gui_aligned_container_t cont_list_table;
	gui_scrolled_list_t scrolly;
	gui_scrollpane_t scrollx_tab;
	gui_waytype_tab_panel_t tabs;
	gui_combobox_t ware_filter, engine_filter;
	vector_tpl<const goods_desc_t *>idx_to_ware;
	gui_label_buf_t lb_count;

	void fill_list();

	// may waytypes available
	uint32 count;

	static char status_counts_text[10][VL_MAX_STATUS_FILTER];

	button_t bt_show_name, bt_show_side_view;
	table_sort_button_t bt_table_sort[VL_MAX_SPECS];

	static char name_filter[256];
	char last_name_filter[256];
	gui_textinput_t name_filter_input;

public:
	vehiclelist_frame_t();

	const char *get_help_filename() const OVERRIDE {return "vehiclelist.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void rdwr(loadsave_t* file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_vehiclelist; }
};


class vehiclelist_stats_t : public gui_scrolled_list_t::scrollitem_t
{
private:
	const vehicle_desc_t *veh;
	cbuffer_t tooltip_buf;
	cbuffer_t buf;
	int height;

public:
	static int sort_mode;
	static bool reverse;

	vehiclelist_stats_t(const vehicle_desc_t *);

	char const* get_text() const OVERRIDE;
	scr_size get_size() const OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return get_size(); };
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }

	static bool compare(const gui_component_t *a, const gui_component_t *b );

	bool infowin_event(event_t const *ev) OVERRIDE;

	void draw( scr_coord offset ) OVERRIDE;
};

#endif
