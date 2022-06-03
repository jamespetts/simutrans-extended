/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_HALT_DETAIL_H
#define GUI_HALT_DETAIL_H


#include "../simfab.h"
#include "../simline.h"
#include "../halthandle_t.h"
#include "../utils/cbuffer_t.h"

#include "../player/simplay.h"

#include "gui_frame.h"
#include "simwin.h"
#include "halt_info.h"
#include "components/gui_scrollpane.h"
#include "components/gui_label.h"
#include "components/gui_tab_panel.h"
#include "components/gui_container.h"
#include "components/action_listener.h"
#include "components/gui_button.h"

class player_t;
class gui_halt_waiting_indicator_t;


// tab1 - pax and mail
class halt_detail_pas_t : public gui_container_t
{
private:
	halthandle_t halt;

	cbuffer_t pas_info;

public:
	halt_detail_pas_t(halthandle_t halt);

	void set_halt(halthandle_t h) { halt = h; }

	void draw(scr_coord offset);

	// draw pax or mail classes waiting amount information table
	void draw_class_table(scr_coord offset, const uint8 class_name_cell_width, const goods_desc_t *warentyp);
};

// tab2 - freight
class halt_detail_goods_t : public gui_container_t
{
private:
	halthandle_t halt;

	cbuffer_t goods_info;

public:
	halt_detail_goods_t(halthandle_t halt);
	void set_halt(halthandle_t h) { halt = h; }

	uint32 active_freight_catg = 0;

	void recalc_size();

	void draw(scr_coord offset);
};

// A display of nearby industries info for station detail
class gui_halt_nearby_factory_info_t : public gui_world_component_t
{
private:
	halthandle_t halt;

	slist_tpl<const goods_desc_t *> required_material;
	slist_tpl<const goods_desc_t *> active_product;
	slist_tpl<const goods_desc_t *> inactive_product;

	uint32 line_selected;

public:
	gui_halt_nearby_factory_info_t(const halthandle_t& halt);

	bool infowin_event(event_t const *ev) OVERRIDE;

	void recalc_size();

	void draw(scr_coord offset) OVERRIDE;
};

class gui_halt_service_info_t : public gui_aligned_container_t, public action_listener_t
{
	/**
     * Button to open line window
     */
	class gui_line_button_t : public button_t, public action_listener_t
	{
		linehandle_t line;
	public:
		gui_line_button_t(linehandle_t line) : button_t()
		{
			this->line = line;
			init(button_t::posbutton, NULL);
			add_listener(this);
		}

		bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE
		{
			player_t *player = world()->get_active_player();
			if (player == line->get_owner()) {
				player->simlinemgmt.show_lineinfo(player, line);
			}
			return true;
		}

		void draw(scr_coord offset) OVERRIDE
		{
			if (line->get_owner() == world()->get_active_player()) {
				button_t::draw(offset);
			}
		}
	};
	/**
	 * Button to open convoi window
	 */
	class gui_convoi_button_t : public button_t, public action_listener_t
	{
		convoihandle_t convoi;
	public:
		gui_convoi_button_t(convoihandle_t convoi) : button_t() {
			this->convoi = convoi;
			init(button_t::posbutton, NULL);
			add_listener(this);
		}

		bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE {
			convoi->show_info();
			return true;
		}
	};

private:
	gui_aligned_container_t container;
	gui_scrollpane_t scrolly;

	halthandle_t halt;

	button_t bt_access_minimap;

	uint32 cached_line_count;
	uint32 cached_convoy_count;

	void insert_show_nothing() {
		new_component<gui_empty_t>();
		new_component<gui_label_t>("keine", SYSCOL_TEXT_INACTIVE);
	}

public:
	gui_halt_service_info_t( halthandle_t halt = halthandle_t() );

	void init(halthandle_t halt);

	void update_connections();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	// FIXME
	//scr_size get_min_size() const OVERRIDE { return get_size(); }
	//scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

class gui_halt_route_info_t : public gui_world_component_t
{
private:
	halthandle_t halt;

	vector_tpl<halthandle_t> halt_list;
	uint32 line_selected;

	uint8 selected_route_catg_index = goods_manager_t::INDEX_PAS;
	uint8 selected_class = 255;
	bool station_display_mode;

	void draw_list_by_catg(scr_coord offset);
	void draw_list_by_dest(scr_coord offset);

public:
	gui_halt_route_info_t(const halthandle_t& halt, uint8 catg_index, bool station_mode = false);

	void build_halt_list(uint8 catg_index, uint8 g_class = 255, bool station_mode = false);
	bool infowin_event(event_t const *ev) OVERRIDE;

	void recalc_size();

	void draw(scr_coord offset) OVERRIDE;
};

class halt_detail_t : public gui_frame_t, action_listener_t
{
private:
	halthandle_t halt;
	player_t *cached_active_player; // So that, if different from current, change line links
	uint32 cached_line_count;
	uint32 cached_convoy_count;
	uint32 old_factory_count, old_catg_count;
	uint32 update_time;
	scr_coord_val cached_size_y;
	static sint16 tabstate;
	bool show_pas_info, show_freight_info;

	gui_halt_waiting_indicator_t *waiting_bar;
	halt_detail_pas_t pas;
	halt_detail_goods_t goods;
	gui_container_t cont_goods, cont_desinations;
	gui_aligned_container_t cont_route;
	gui_halt_service_info_t cont_service;
	gui_scrollpane_t scrolly_pas, scrolly_goods, scrolly_service, scrolly_route;
	gui_label_buf_t lb_selected_route_catg;
	gui_heading_t lb_nearby_factory, lb_routes, lb_serve_catg;

	gui_halt_nearby_factory_info_t nearby_factory;
	gui_tab_panel_t tabs;

	// route tab stuffs
	uint8 selected_route_catg_index = goods_manager_t::INDEX_NONE;
	uint8 selected_class = 0;
	gui_halt_route_info_t destinations;
	bool list_by_station = false;
	button_t bt_by_category, bt_by_station;
	slist_tpl<button_t *>catg_buttons, pas_class_buttons, mail_class_buttons;

	// Opening and closing the button panel on the route tab
	void open_close_catg_buttons();

	void update_components();

	void set_tab_opened();

public:
	halt_detail_t(halthandle_t halt = halthandle_t());

	~halt_detail_t();

	void init();

	const char * get_help_filename() const OVERRIDE { return "station_details.txt"; }

	// Set window size and adjust component sizes and/or positions accordingly
	virtual void set_windowsize(scr_size size) OVERRIDE;

	virtual koord3d get_weltpos(bool set) OVERRIDE;

	virtual bool is_weltpos() OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	bool infowin_event(const event_t *ev) OVERRIDE;

	// only defined to update schedule, if changed
	void draw( scr_coord pos, scr_size size ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_halt_detail; }
};

#endif
