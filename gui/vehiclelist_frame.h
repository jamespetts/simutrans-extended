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
#include "components/sortable_table_vehicle.h"
#include "components/sortable_table_header.h"

class vehicle_desc_t;
class goods_desc_t;


class vehiclelist_frame_t : public gui_frame_t, private action_listener_t
{
public:
	// vehicle status(mainly timeline) filter index
	enum status_filter_t {
		VL_SHOW_FUTURE =0,    // gray
		VL_SHOW_AVAILABLE,    // green
		VL_SHOW_OUT_OF_PROD,  // royalblue
		VL_SHOW_OUT_OBSOLETE, // blue
		VL_FILTER_UPGRADE_ONLY, // purple
		VL_MAX_STATUS_FILTER,
	};

private:
	button_t bt_timeline_filters[VL_MAX_STATUS_FILTER];
	button_t bt_upgradable;
	gui_sort_table_header_t table_header;
	gui_aligned_container_t cont_sortable;
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

#endif
