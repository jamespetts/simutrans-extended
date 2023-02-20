/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_WAY_INFO_H
#define GUI_WAY_INFO_H


#include "gui_frame.h"
#include "../boden/grund.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_location_view_t.h"
#include "components/gui_scrollpane.h"
#include "components/gui_speedbar.h"
#include "components/gui_tab_panel.h"
#include "../simconvoi.h"

#include "../utils/cbuffer_t.h"


class grund_t;

class gui_way_detail_info_t : public gui_aligned_container_t
{
	weg_t *way;
	gui_image_t speed_restricted;

public:
	gui_way_detail_info_t(weg_t *way=NULL);

	void set_way(weg_t *w) { way = w; }

	void draw(scr_coord offset) OVERRIDE;
};

class way_info_t : public gui_frame_t, public action_listener_t
{
	const grund_t* gr;
	weg_t *way1, *way2;
	uint8 has_way = 0;
	bool has_road=false;

	//cbuffer_t way_tooltip;
	location_view_t way_view;
	gui_aligned_container_t cont;
	gui_aligned_container_t cont_road_routes;
	gui_way_detail_info_t cont_way1, cont_way2;
	gui_scrollpane_t scrolly, scrolly_way1, scrolly_way2, scrolly_road_routes;
	gui_tab_panel_t tabs;
	gui_speedbar_t condition_bar1, condition_bar2;
	sint32 condition1=0, degraded_cond1=0, condition2=0, degraded_cond2=0; // for speedbar value

	// for reserved convoy display
	gui_aligned_container_t cont_reserved_convoy;
	button_t reserving_vehicle_button;
	convoihandle_t reserved_convoi;
	convoihandle_t get_reserved_convoy(const weg_t *way) const;
	gui_label_buf_t lb_is_reserved, lb_reserved_convoy, lb_signal_wm, lb_reserved_cnv_speed, lb_reserved_cnv_distance;
	gui_image_t speed_restricted;

	// for road route info
	gui_label_buf_t lb_city_count;
	vector_tpl<koord> building_list;

	void init_tabs();
	void update();
	void update_way_info();
public:
	way_info_t(const grund_t* gr);

	const char *get_help_filename() const OVERRIDE { return "way_info.txt"; }

	koord3d get_weltpos(bool) OVERRIDE { return gr->get_pos(); }

	bool is_weltpos() OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void map_rotate90(sint16) OVERRIDE;

	bool action_triggered(gui_action_creator_t *comp, value_t) OVERRIDE;
};

#endif
