/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_HALT_INFO_H
#define GUI_HALT_INFO_H


#include "gui_frame.h"
#include "components/gui_label.h"
#include "components/gui_scrollpane.h"
#include "components/gui_textarea.h"
#include "components/gui_textinput.h"
#include "components/gui_button.h"
#include "components/gui_button_to_chart.h"
#include "components/gui_location_view_t.h"
#include "components/gui_tab_panel.h"
#include "components/action_listener.h"
#include "components/gui_chart.h"
#include "components/gui_image.h"
#include "components/gui_colorbox.h"
#include "components/gui_combobox.h"
#include "components/gui_speedbar.h"

#include "../utils/cbuffer_t.h"
#include "../simhalt.h"
#include "simwin.h"

#define HALT_CAPACITY_BAR_WIDTH 100

/**
 * Helper class to show type symbols (train, bus, etc)
 */
class gui_halt_type_images_t : public gui_aligned_container_t
{
	halthandle_t halt;
	gui_image_t img_transport[9];
public:
	gui_halt_type_images_t(halthandle_t h);

	void draw(scr_coord offset) OVERRIDE;
};

/**
 * Helper class to draw halt handled goods categories
 */
class gui_halt_handled_goods_images_t : public gui_container_t
{
	halthandle_t halt;
public:
	gui_halt_handled_goods_images_t(halthandle_t h);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

class gui_halt_goods_demand_t : public gui_component_t
{
	halthandle_t halt;
	slist_tpl<const goods_desc_t *> goods_list;
	bool show_products=true;
	uint32 old_fab_count=0;

	void build_goods_list();

public:
	gui_halt_goods_demand_t(halthandle_t h, bool show_products);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

class gui_halt_waiting_summary_t : public gui_container_t
{
	halthandle_t halt;
	cbuffer_t buf;
public:
	gui_halt_waiting_summary_t(halthandle_t h);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

/**
 * Helper class to draw freight type capacity bar
 */
class gui_halt_capacity_bar_t : public gui_container_t
{
	halthandle_t halt;
	uint8 freight_type;
public:
	gui_halt_capacity_bar_t(halthandle_t h, uint8 ft);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return scr_size(HALT_CAPACITY_BAR_WIDTH + 2, GOODS_COLOR_BOX_HEIGHT); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

/**
 * Helper class to show three freight category waiting indicator
 */
class gui_halt_waiting_indicator_t : public gui_aligned_container_t
{
	halthandle_t halt;
	gui_image_t img_alert;
	gui_halt_capacity_bar_t *capacity_bar[3];
	gui_label_buf_t lb_waiting[3];
	gui_label_buf_t lb_capacity[3];
	gui_label_buf_t lb_transfer_time[3];

	bool show_transfer_time;

	void init();
public:
	gui_halt_waiting_indicator_t(halthandle_t h, bool show_transfer_time = true);

	void update();

	void set_halt(halthandle_t h) { halt = h; init(); }

	void draw(scr_coord offset) OVERRIDE;
};

/**
 * Main class: the station info window.
 */
class halt_info_t : public gui_frame_t, private action_listener_t
{
private:

	gui_aligned_container_t *container_top;
	gui_label_buf_t lb_pax_storage, lb_mail_storage;
	gui_label_t lb_evaluation;
	gui_colorbox_t indicator_color;
	gui_image_t img_enable[2];
	gui_halt_type_images_t *img_types;
	gui_combobox_t db_mode_selector;
	/**
	* Buffer for freight info text string.
	*/
	cbuffer_t freight_info;
	cbuffer_t tooltip_buf;
	gui_label_buf_t joined_buf;

	// departure stuff (departure and arrival times display)
	class dest_info_t {
	public:
		halthandle_t halt;
		sint32 delta_ticks;
		convoihandle_t cnv;
		dest_info_t() : delta_ticks(0) {}
		dest_info_t( halthandle_t h, sint32 d_t, convoihandle_t c ) : halt(h), delta_ticks(d_t), cnv(c) {}
		bool operator == (const dest_info_t &other) const { return ( this->cnv==other.cnv ); }
	};

	static bool compare_hi(const dest_info_t &a, const dest_info_t &b) { return a.delta_ticks <= b.delta_ticks; }

	vector_tpl<dest_info_t> db_halts;

	button_t bt_arrivals, bt_departures;
	gui_aligned_container_t cont_tab_departure, cont_departure;
	gui_scrollpane_t scrolly_departure_board;

	enum {
		SHOW_DEPARTURES = 1 << 1,
		SHOW_LINE_NAME  = 1 << 2
	};
	uint8 display_mode_bits = 0;

	// departure board filter
	button_t bt_db_filter[3];
	enum {
		DB_SHOW_PAX   = 1 << 0,
		DB_SHOW_MAIL  = 1 << 1,
		DB_SHOW_GOODS = 1 << 2,
		DB_SHOW_ALL = DB_SHOW_PAX | DB_SHOW_MAIL | DB_SHOW_GOODS
	};
	uint8 db_filter_bits = DB_SHOW_ALL;

	void update_cont_departure();

	// other UI definitions
	gui_aligned_container_t container_freight, container_chart;
	gui_textarea_t text_freight;
	gui_scrollpane_t scrolly_freight;

	int pax_ev_num[5], mail_ev_num[2];
	int old_pax_ev_sum, old_mail_ev_sum;
	gui_aligned_container_t cont_pax_ev_detail, cont_mail_ev_detail; // values with symbol
	gui_bandgraph_t evaluation_pax, evaluation_mail;
	gui_halt_waiting_indicator_t waiting_bar;

	gui_textinput_t input;
	gui_chart_t chart;
	location_view_t view;
	button_t detail_button;
	// button_t sort_button;
	gui_combobox_t freight_sort_selector;

	gui_button_to_chart_array_t button_to_chart;

	gui_tab_panel_t switch_mode;

	halthandle_t halt;
	char edit_name[320];

	void update_components();

	void set_tab_opened();

	void init(halthandle_t halt);

	void activate_chart_buttons();

	// for departure board
	// return a its destination/origin stop
	halthandle_t get_convoy_target_halt(convoihandle_t cnv);

public:
	enum sort_mode_t { by_destination = 0, by_via = 1, by_amount_via = 2, by_amount = 3, by_origin = 4, by_origin_sum = 5, by_destination_detil = 6, by_class_detail = 7, by_class_via = 8, by_line = 9, by_line_via = 10, SORT_MODES = 11 };
	enum halt_freight_type_t { // freight capacity type (for chart/list)
		ft_pax    = 0,
		ft_mail   = 1,
		ft_goods  = 2,
		ft_others = 3
	};


	halt_info_t(halthandle_t halt = halthandle_t());

	virtual ~halt_info_t();

	const char * get_help_filename() const OVERRIDE {return "station.txt";}

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	koord3d get_weltpos(bool) OVERRIDE;

	bool is_weltpos() OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_halt_info+halt.get_id(); }
};

#endif
