/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CITY_INFO_H
#define GUI_CITY_INFO_H


#include "../simcity.h"
#include "simwin.h"

#include "gui_frame.h"
#include "components/gui_chart.h"
#include "components/gui_textinput.h"
#include "components/action_listener.h"
#include "components/gui_label.h"
#include "components/gui_button.h"
#include "components/gui_button_to_chart.h"
#include "components/gui_tab_panel.h"
#include "components/gui_table.h"
#include "components/gui_scrollpane.h"
#include "components/gui_speedbar.h"
#include "../display/viewport.h"

class stadt_t;
template <class T> class sparse_tpl;
class gui_city_minimap_t;

#define PAX_DEST_MIN_SIZE (16)      ///< minimum width/height of the minimap
#define PAX_DEST_VERTICAL (1.0/1.5) ///< aspect factor where minimaps change to over/under instead of left/right
#define PAX_DEST_COLOR_LEGENDS 8

/**
 * Component to show both passenger destination minimaps
 */
class gui_city_minimap_t : public gui_world_component_t
{
	stadt_t* city;
	scr_size minimaps_size;        ///< size of minimaps
	scr_coord minimap2_offset;     ///< position offset of second minimap
	array2d_tpl<PIXVAL> pax_dest_old, pax_dest_new;
	uint32 pax_destinations_last_change;
	bool show_contour=true;

	void init_pax_dest(array2d_tpl<PIXVAL> &pax_dest);
	void add_pax_dest(array2d_tpl<PIXVAL> &pax_dest, const sparse_tpl<PIXVAL>* city_pax_dest);

public:
	gui_city_minimap_t(stadt_t* city) : city(city),
		pax_dest_old(0, 0),
		pax_dest_new(0, 0)
	{
		minimaps_size = scr_size(min(200,world()->get_size().x), min(200,world()->get_size().y)); // default minimaps size
		minimap2_offset = scr_coord(minimaps_size.w + D_H_SPACE, 0);
	}
	scr_size get_min_size() const OVERRIDE { return scr_size(PAX_DEST_MIN_SIZE * 2 + D_H_SPACE, PAX_DEST_MIN_SIZE); }

	// for loading
	void set_city(stadt_t* c) { city = c; }

	void set_show_contour(bool yesno) { show_contour = yesno; init_pax_dest(pax_dest_new); }

	// set size of minimap, decide for horizontal or vertical arrangement
	void set_size(scr_size size) OVERRIDE
	{
		// calculate new minimaps size : expand horizontally or vertically ?
		const float world_aspect = (float)welt->get_size().x / (float)welt->get_size().y;
		const scr_coord space(size.w, size.h);
		const float space_aspect = (float)space.x / (float)space.y;

		if (world_aspect / space_aspect > PAX_DEST_VERTICAL) { // world wider than space, use vertical minimap layout
			minimaps_size.h = (space.y - D_V_SPACE) / 2;
			if (minimaps_size.h * world_aspect <= space.x) {
				// minimap fits
				minimaps_size.w = (sint16)(minimaps_size.h * world_aspect);
			}
			else {
				// too large, truncate
				minimaps_size.w = space.x;
				minimaps_size.h = max((int)(minimaps_size.w / world_aspect), PAX_DEST_MIN_SIZE);
			}
			minimap2_offset = scr_coord(0, minimaps_size.h + D_V_SPACE);
		}
		else { // horizontal minimap layout
			minimaps_size.w = (space.x - D_H_SPACE) / 2;
			if (minimaps_size.w / world_aspect <= space.y) {
				// minimap fits
				minimaps_size.h = (sint16)(minimaps_size.w / world_aspect);
			}
			else {
				// too large, truncate
				minimaps_size.h = space.y;
				minimaps_size.w = max((int)(minimaps_size.h * world_aspect), PAX_DEST_MIN_SIZE);
			}
			minimap2_offset = scr_coord(minimaps_size.w + D_H_SPACE, 0);
		}

		// resize minimaps
		pax_dest_old.resize(minimaps_size.w, minimaps_size.h);
		pax_dest_new.resize(minimaps_size.w, minimaps_size.h);

		// reinit minimaps data
		init_pax_dest(pax_dest_old);
		pax_dest_new = pax_dest_old;
		add_pax_dest(pax_dest_old, city->get_pax_destinations_old());
		add_pax_dest(pax_dest_new, city->get_pax_destinations_new());
		pax_destinations_last_change = city->get_pax_destinations_new_change();

		gui_world_component_t::set_size(scr_size(min(size.w, minimap2_offset.x + minimaps_size.w), min(size.h, minimap2_offset.y + minimaps_size.h)));
	}
	// handle clicks into minimaps
	bool infowin_event(const event_t *ev) OVERRIDE
	{
		int my = ev->my;
		if (my > minimaps_size.h  &&  minimap2_offset.y > 0) {
			// Little trick to handle both maps with the same code: Just remap the y-values of the bottom map.
			my -= minimaps_size.h + D_V_SPACE;
		}

		if (ev->ev_class != EVENT_KEYBOARD && ev->ev_code == MOUSE_LEFTBUTTON && 0 <= my && my < minimaps_size.h) {
			int mx = ev->mx;
			if (mx > minimaps_size.w  &&  minimap2_offset.x > 0) {
				// Little trick to handle both maps with the same code: Just remap the x-values of the right map.
				mx -= minimaps_size.w + D_H_SPACE;
			}

			if (0 <= mx && mx < minimaps_size.w) {
				// Clicked in a minimap.
				const koord p = koord(
					(mx * welt->get_size().x) / (minimaps_size.w),
					(my * welt->get_size().y) / (minimaps_size.h));
				welt->get_viewport()->change_world_position(p);
				return true;
			}
		}
		return false;
	}
	// draw both minimaps
	void draw(scr_coord offset) OVERRIDE
	{
		const uint32 current_pax_destinations = city->get_pax_destinations_new_change();
		if (pax_destinations_last_change > current_pax_destinations) {
			// new month started
			pax_dest_old = pax_dest_new;
			init_pax_dest(pax_dest_new);
			add_pax_dest(pax_dest_new, city->get_pax_destinations_new());
		}
		else if (pax_destinations_last_change != current_pax_destinations) {
			// Since there are only new colors, this is enough:
			add_pax_dest(pax_dest_new, city->get_pax_destinations_new());
		}
		pax_destinations_last_change = current_pax_destinations;

		display_array_wh(pos.x + offset.x, pos.y + offset.y, minimaps_size.w, minimaps_size.h, pax_dest_old.to_array());
		display_array_wh(pos.x + offset.x + minimap2_offset.x, pos.y + offset.y + minimap2_offset.y, minimaps_size.w, minimaps_size.h, pax_dest_new.to_array());
	}
};


class city_location_map_t : public gui_world_component_t
{
	stadt_t* city;
	array2d_tpl<PIXVAL> map_array;

public:
	city_location_map_t(stadt_t* city);

	void draw(scr_coord offset) OVERRIDE;

	// handle clicks into minimaps
	bool infowin_event(const event_t *ev) OVERRIDE;

	void update(); // init location map

	// for loading saved game
	void set_city(stadt_t* c) { city = c; update(); };

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return size; }
};

/**
 * Window containing information about a city.
 */
class city_info_t : public gui_frame_t, private action_listener_t
{
private:
	char name[256], old_name[256]; ///< Name and old name of the city.

	stadt_t *city;                 ///< City for which the information is displayed

	gui_textinput_t name_input;    ///< Input field where the name of the city can be changed
	button_t allow_growth;         ///< Checkbox to enable/disable city growth
	button_t bt_show_contour, bt_show_hide_legend, bt_city_attractions, bt_city_stops, bt_city_factories;
	gui_label_t lb_collapsed;
	gui_label_buf_t lb_border;
	gui_table_cell_buf_t lb_size, lb_buildings, lb_powerdemand;
	gui_label_with_symbol_t lb_allow_growth;

	gui_tab_panel_t year_month_tabs, tabs;
	gui_aligned_container_t container_chart, container_year, container_month, cont_destination_map, cont_minimap_legend;
	gui_chart_t chart, mchart;                ///< Year and month history charts

	gui_city_minimap_t pax_map;
	city_location_map_t location_map;

	gui_button_to_chart_array_t button_to_chart;

	/// Renames the city to the name given in the text input field
	void rename_city();

	/// Resets the value of the text input field to the name of the city,
	/// e.g. when losing focus
	void reset_city_name();

	void update_labels();

	// stats table
	gui_bandgraph_t transportation_last_year, transportation_this_year;
	gui_aligned_container_t cont_city_stats;
	gui_scrollpane_t scrolly_stats;
	int transportation_pas[6];
	// update trigger
	uint32 old_month = 0;
	int update_seed = 0;
	void update_stats();

	void init();
public:
	city_info_t(stadt_t *city = NULL);

	virtual ~city_info_t();

	const char *get_help_filename() const OVERRIDE { return "citywindow.txt"; }

	koord3d get_weltpos(bool) OVERRIDE;

	bool is_weltpos() OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE;

	// since we need to update the city pointer when topped
	bool infowin_event(event_t const*) OVERRIDE;

	void update_data();

	void rdwr(loadsave_t *file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_city_info_t; }
};

#endif
