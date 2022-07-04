/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_CONVOY_ASSEMBLER_H
#define GUI_COMPONENTS_GUI_CONVOY_ASSEMBLER_H


#include "action_listener.h"
#include "gui_aligned_container.h"
#include "gui_button.h"
#include "gui_combobox.h"
#include "gui_divider.h"
#include "gui_image.h"
#include "gui_image_list.h"
#include "gui_label.h"
#include "gui_scrollpane.h"
#include "gui_tab_panel.h"
#include "gui_speedbar.h"
#include "../gui_theme.h"

#include "gui_container.h"
//#include "gui_vehicle_capacitybar.h"

#include "../../convoihandle_t.h"

#include "../../descriptor/vehicle_desc.h"

#include "gui_action_creator.h"

#include "../../tpl/ptrhashtable_tpl.h"
#include "../../tpl/vector_tpl.h"
#include "../../utils/cbuffer_t.h"

#define VEHICLE_FILTER_RELEVANT 1
#define VEHICLE_FILTER_GOODS_OFFSET 2

class gui_vehicle_spec_t : public gui_aligned_container_t
{
	const vehicle_desc_t *veh_type;
	cbuffer_t comfort_tooltip_buf;

	scr_coord_val min_h;

	// update spac
	void update(uint8 mode, uint32 value);
public:
	gui_vehicle_spec_t(const vehicle_desc_t* v=NULL);

	// NULL=clear, resale value is used only in va_sell mode
	void set_vehicle(const vehicle_desc_t* v = NULL, uint8 mode = 0, uint32 resale_value=0) { veh_type = v; update(mode, resale_value); }

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};


class depot_convoi_capacity_t : public gui_container_t
{
private:
	uint32 total_pax;
	uint32 total_standing_pax;
	uint32 total_mail;
	uint32 total_goods;

	uint8 total_pax_classes;
	uint8 total_mail_classes;

	int good_type_0 = -1;
	int good_type_1 = -1;
	int good_type_2 = -1;
	int good_type_3 = -1;
	int good_type_4 = -1;

	uint32 good_type_0_amount;
	uint32 good_type_1_amount;
	uint32 good_type_2_amount;
	uint32 good_type_3_amount;
	uint32 good_type_4_amount;
	uint32 rest_good_amount;

	uint8 highest_catering;
	bool is_tpo;

public:
	depot_convoi_capacity_t();
	void set_totals(uint32 pax, uint32 standing_pax, uint32 mail, uint32 goods, uint8 pax_classes, uint8 mail_classes, int good_0, int good_1, int good_2, int good_3, int good_4, uint32 good_0_amount, uint32 good_1_amount, uint32 good_2_amount, uint32 good_3_amount, uint32 good_4_amount, uint32 rest_good, uint8 catering, bool tpo);
	void draw(scr_coord offset);
};

class gui_vehicle_bar_legends_t : public gui_aligned_container_t
{
public:
	gui_vehicle_bar_legends_t();
};

/**
 * This class allows the player to assemble a convoy from vehicles.
 * The code was extracted from depot_frame_t and adapted by isidoro
 *   in order to be used elsewhere if needed (Jan-09).
 * The author markers of the original code have been preserved when
 *   possible.
 */
class gui_convoy_assembler_t : public gui_aligned_container_t, public gui_action_creator_t, private action_listener_t
{
	/**
	 * Parameters to determine layout and behaviour of convoy images.
	 * Originally in simdepot.h.  Based in the code of:
	 */
	static scr_coord get_placement(waytype_t wt);
public:
	static scr_coord get_grid(waytype_t wt);

private:
	waytype_t way_type;
	bool way_electrified;

	// The selected convoy so far...
	vector_tpl<const vehicle_desc_t *> vehicles;

	// If this is used for a depot, which depot_frame manages, else NULL
	class depot_frame_t *depot_frame;
	class replace_frame_t *replace_frame;

	// [CONVOI]
	vector_tpl<gui_image_list_t::image_data_t*> convoi_pics;
	gui_image_list_t convoi;
	gui_label_t lb_makeup;

	gui_aligned_container_t cont_convoi, cont_convoi_capacity;
	gui_scrollpane_t scrollx_convoi, scrolly_convoi_capacity;

	button_t bt_class_management;
	//gui_convoy_loading_info_t capacity_info;

	gui_label_buf_t lb_convoi_count;
	gui_label_buf_t lb_convoi_count_fluctuation;
	gui_label_buf_t lb_convoi_tiles;
	gui_tile_occupancybar_t tile_occupancy;
	sint8 new_vehicle_length;

	// convoy spec
	gui_image_t img_alert_speed, img_alert_power;
	gui_aligned_container_t cont_convoi_spec;
	gui_label_buf_t lb_convoi_speed;
	gui_label_buf_t lb_convoi_cost;
	gui_label_buf_t lb_convoi_maintenance;
	gui_label_buf_t lb_convoi_power;
	gui_label_buf_t lb_convoi_weight;
	gui_label_buf_t lb_convoi_brake_force;
	gui_label_buf_t lb_convoi_brake_distance;
	gui_label_buf_t lb_convoi_axle_load;
	gui_label_buf_t lb_convoi_rolling_resistance;
	gui_label_buf_t lb_convoi_way_wear;


	// [VEHICLE LIST]
	gui_tab_panel_t tabs;
	vector_tpl<gui_image_list_t::image_data_t*> pas_vec;
	vector_tpl<gui_image_list_t::image_data_t*> pas2_vec;
	vector_tpl<gui_image_list_t::image_data_t*> electrics_vec;
	vector_tpl<gui_image_list_t::image_data_t*> locos_vec;
	vector_tpl<gui_image_list_t::image_data_t*> waggons_vec;

	gui_image_list_t pas;
	gui_image_list_t pas2;
	gui_image_list_t electrics;
	gui_image_list_t locos;
	gui_image_list_t waggons;
	gui_scrollpane_t scrolly_pas;
	gui_scrollpane_t scrolly_pas2;
	gui_scrollpane_t scrolly_electrics;
	gui_scrollpane_t scrolly_locos;
	gui_scrollpane_t scrolly_waggons;

	// text for the tabs is defaulted to the train names
	static const char * get_electrics_name(waytype_t wt);
	static const char * get_passenger_name(waytype_t wt);
	static const char * get_zieher_name(waytype_t wt);
	static const char * get_haenger_name(waytype_t wt);
	static const char * get_passenger2_name(waytype_t wt);

	// [FILTER]
	static char name_filter_value[64];
	gui_textinput_t name_filter_input;

	gui_combobox_t vehicle_filter;
	static bool show_all;               // show available vehicles (same for all depot)
	static bool show_outdated_vehicles; // shoe vehicled that production is stopped but not increase maintenance yet.
	static bool show_obsolete_vehicles; // (same for all depot)
	button_t bt_show_all;
	button_t bt_outdated;
	button_t bt_obsolete;


	// [MODE SELECTOR]
	uint8 veh_action;
	gui_combobox_t action_selector;

	/**
	 * A helper map to update loks_vec and waggons_Vec. All entries from
	 * loks_vec and waggons_vec are referenced here.
	 */
	typedef ptrhashtable_tpl<vehicle_desc_t const*, gui_image_list_t::image_data_t*, N_BAGS_LARGE> vehicle_image_map;
	vehicle_image_map vehicle_map;

	/**
	 * Update the info text for the vehicle the mouse is over - if any.
	 */
	void update_vehicle_info_text(scr_coord pos);
	//void update_vehicle_info_text() { update_vehicle_info_text(pos); }

	// cache old values
	const vehicle_desc_t *old_veh_type;

	// add a single vehicle (helper function)
	void add_to_vehicle_list(const vehicle_desc_t *info);

	// for convoi image
	void image_from_convoi_list(uint nr);

	void image_from_storage_list(gui_image_list_t::image_data_t *image_data);

	// [UNDER TABS]
	gui_label_t lb_too_heavy_notice;

	gui_vehicle_spec_t cont_vspec;

	static uint16 livery_scheme_index;
	vector_tpl<uint16> livery_scheme_indices;
	gui_combobox_t livery_selector;
	gui_label_buf_t lb_livery_counter;

	/* color bars for current convoi: */
	void set_vehicle_bar_shape(gui_image_list_t::image_data_t *pic, const vehicle_desc_t *v);

	// Reflects changes when upgrading to the target vehicle
	void init_convoy_color_bars(vector_tpl<const vehicle_desc_t *>*vehs);

public:
	enum {
		va_append,
		va_insert,
		va_sell,
		va_upgrade
	};

	// Last selected vehicle filter
	static int selected_filter;

	gui_convoy_assembler_t(depot_frame_t *frame);
	gui_convoy_assembler_t(replace_frame_t *frame);

	void init(waytype_t wt, signed char player_nr, bool electrified = true);

	/**
	 * Update texts, image lists and buttons according to the current state.
	 */
	// update [CONVOI] data
	void update_convoi();

private:
	// update vehicles colorbar
	// ** do not call this from depot frame! ** //
	void update_data();
	void update_tabs();

	/**
	 * Create and fill vehicle vectors (for all tabs)
	 */
	void build_vehicle_lists();

public:
	/* Getter/setter methods */

	inline vector_tpl<const vehicle_desc_t *>* get_vehicles() {return &vehicles;}
	inline const vector_tpl<gui_image_list_t::image_data_t* >* get_convoi_pics() const { return &convoi_pics; }
	void set_vehicles(convoihandle_t cnv);
	void set_vehicles(const vector_tpl<const vehicle_desc_t *>* vv);

	void set_panel_width();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t *comp, value_t extra) OVERRIDE;
	bool infowin_event(const event_t *ev) OVERRIDE;

	void set_electrified(bool ele);

	static uint16 get_livery_scheme_index() { return livery_scheme_index; }
};

#endif
