/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CONSIST_ORDER_GUI_H
#define GUI_CONSIST_ORDER_GUI_H


#include "gui_frame.h"
#include "simwin.h"
#include "linelist_stats_t.h"
#include "components/gui_aligned_container.h"
#include "components/gui_button.h"
#include "components/gui_colorbox.h"
#include "components/gui_combobox.h"
#include "components/gui_convoy_formation.h"
#include "components/gui_image.h"
#include "components/gui_image_list.h"
#include "components/gui_label.h"
#include "components/gui_numberinput.h"
#include "components/gui_schedule_item.h"
#include "components/gui_scrollpane.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_tab_panel.h"

#include "../tpl/slist_tpl.h"

#include "../convoihandle_t.h"
#include "../halthandle_t.h"
#include "../player/simplay.h"

#include "../dataobj/schedule.h"
#include "../dataobj/consist_order_t.h"

class consist_order_t;


// Scrolled list with a content-driven default size. A plain scrolled list
// reports almost no minimum size, so the table column holding it collapses to
// the width of the sibling rows: items get clipped and a horizontal scrollbar
// appears even at the default window size. This floors the width at the
// widest item and the height at min_rows of the tallest item.
class consist_list_t : public gui_scrolled_list_t
{
	uint8 min_rows;
public:
	consist_list_t(enum type t = windowskin, item_compare_func cmp = 0, uint8 rows = 6) : gui_scrolled_list_t(t, cmp), min_rows(rows) { }
	scr_size get_min_size() const OVERRIDE;
	// Unbounded maximum: the shared table max-height math caps a list row at
	// one row, which strands all vertical surplus as void below the list and
	// pins the pane small. The default size is still governed by the minimum.
	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, scr_size::inf.h); }
	// Refresh after the content was rebuilt: reset the container to the new
	// content, restore row breathing room (sort() zeroes the container
	// margins), and restart at the top so no stale offset survives.
	void refresh();
};

struct own_vehicle_t
{
	uint32 count = 0;
	const vehicle_desc_t* veh_type=nullptr;
};


class vehicle_scrollitem_t : public gui_label_t, public gui_scrolled_list_t::scrollitem_t
{
	own_vehicle_t own_veh;

	gui_label_buf_t label;
	gui_vehicle_bar_t colorbar, colorbar_edge;

public:
	static int sort_mode;
	static bool sortreverse;
	enum sort_mode_t {
		by_name = 0,
		by_own,
		by_power,
		by_max_speed,
		by_intro_date,
		by_role,
		SORT_MODES
	};

	vehicle_scrollitem_t(own_vehicle_t own_veh);

	void draw(scr_coord offset) OVERRIDE;

	char const* get_text() const OVERRIDE { return own_veh.veh_type==nullptr ? "Vehicle not found" : own_veh.veh_type->get_name(); }

	const vehicle_desc_t* get_vehicle() const { return own_veh.veh_type; }

	using gui_component_t::infowin_event;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, LINESPACE); }

	static bool compare(const gui_component_t *a, const gui_component_t *b);
};



class gui_vehicle_element_list_t : public gui_image_list_t
{
public:
	gui_vehicle_element_list_t(vector_tpl<image_data_t*> *images) :	gui_image_list_t(images) { }
	bool infowin_event(event_t const*) OVERRIDE;
};

class gui_consist_order_shifter_t : public gui_aligned_container_t, private action_listener_t
{
	consist_order_t *order;
	uint32 slot_index;

	button_t bt_remove, bt_forward, bt_backward;

public:
	gui_consist_order_shifter_t(consist_order_t *order, uint32 index);

	bool action_triggered(gui_action_creator_t *comp, value_t) OVERRIDE;
};

// gui_vehicle_description_element_t
class gui_vehicle_description_element_t : public gui_aligned_container_t, private action_listener_t
{
	consist_order_t *order;
	uint32 slot_index = 0;
	waytype_t way_type; // use for grid size and (player favorite) livery scheme

	gui_colorbox_t state_prev, state_next;
	button_t bt_can_empty;
	button_t bt_add_rule;
	gui_label_buf_t lb_catg;
	gui_label_t *rule_header = nullptr;

	// Rule-based alternatives (specific_vehicle == nullptr) in this slot are listed
	// here with an edit/remove row each; rebuilt by update(). rule_alt_indices maps
	// each row to the alternative index within the slot.
	gui_aligned_container_t *rule_table = nullptr;
	vector_tpl<uint32> rule_alt_indices;
	vector_tpl<button_t*> rule_edit_buttons;
	vector_tpl<button_t*> rule_remove_buttons;

	uint32 old_count = 0;
	vector_tpl<gui_image_list_t::image_data_t*> vde_vec;
	gui_vehicle_element_list_t vde;
	void show_vehicle_detail(uint32 index);
	void open_rule_editor(uint32 alt_index);

	gui_scrollpane_t scrolly;

public:
	gui_vehicle_description_element_t(consist_order_t *order, uint32 index, waytype_t wt);

	void update();

	void draw(scr_coord offset) OVERRIDE;

	bool action_triggered(gui_action_creator_t *comp, value_t p) OVERRIDE;
};


class gui_consist_order_element_t : public gui_aligned_container_t
{
	bool selected;
public:
	gui_consist_order_element_t(consist_order_t *order, uint32 index, waytype_t wt, bool selected);
	void draw(scr_coord offset) OVERRIDE;
};


class cont_order_overview_t : public gui_aligned_container_t
{
	consist_order_t *order;
	waytype_t way_type;

	uint32 old_count=0; // reflesh flag
	uint32 old_mod_count=0; // reflesh flag for changes that keep the element count (slot reorder)

	sint16 selected_index = 0; // target index from consist_order_frame_t

	cbuffer_t buf;

public:
	cont_order_overview_t(consist_order_t *order, waytype_t wt);

	void init_table();

	void set_waytype(waytype_t wt) { way_type = wt; }

	void draw(scr_coord offset) OVERRIDE;

	void set_selected_index(sint16 new_index = 0) {
		selected_index = new_index;
		init_table();
	}
};


class consist_order_frame_t : public gui_frame_t , private action_listener_t
{
	uint16 unique_entry_id=65535;
	player_t* player;
	schedule_t *schedule;

	uint8 old_entry_count = 0;
	sint64 old_vehicle_assets; // init in init_table()

	halthandle_t halt;

	consist_order_t order;

	cbuffer_t title_buf;
	gui_schedule_entry_number_t halt_number;
	gui_label_buf_t lb_halt;

	// [ORDER]
	gui_aligned_container_t cont_order;
	cont_order_overview_t cont_order_overview;
	gui_scrollpane_t scroll_order;
	uint32 old_order_count = 0;
	uint32 old_order_mod_count = 0;

	// filter (common)
	uint8 filter_catg=255; // all
	gui_combobox_t freight_type_c;

	// [VEHICLE PICKER]
	const vehicle_desc_t* selected_vehicle = nullptr;
	slist_tpl<own_vehicle_t> own_vehicles;

	gui_numberinput_t numimp_append_target;
	gui_combobox_t edit_action_selector;
	void init_input_value_range();
	button_t bt_add_vehicle;
	button_t bt_add_rule_slot;
	void open_rule_editor(consist_order_t *order, uint32 slot_index, uint32 alt_index);

	button_t bt_sort_order_veh, bt_show_hide_vehicle_filter, bt_connectable_vehicle_filter;
	button_t bt_outdated, bt_obsolete, bt_show_unidirectional;
	button_t bt_show_powered, bt_show_unpowered;
	gui_label_t lb_open_vehicle_filter;
	gui_aligned_container_t cont_picker_frame, cont_vehicle_filter;
	gui_combobox_t engine_filter, vp_sortedby;
	consist_list_t scl_vehicles;
	void build_vehicle_list(); // also update convoy list
	// Check if the vehicle is filtered by UI options
	bool is_filtered(const vehicle_desc_t *veh_type);

	// [CONVOY COPIER]
	convoihandle_t selected_convoy = convoihandle_t();
	slist_tpl<convoihandle_t> own_convoys;
	// filter
	gui_label_t lb_open_convoy_filter;
	button_t bt_filter_halt_convoy, bt_filter_line_convoy, bt_filter_single_vehicle, bt_show_hide_convoy_filter;
	gui_combobox_t cc_sortedby;
	gui_aligned_container_t cont_convoy_filter;
	//
	button_t bt_sort_order_cnv, bt_copy_convoy, bt_convoy_detail;
	gui_label_buf_t lb_vehicle_count;
	gui_line_label_t line_label;
	gui_convoi_images_t img_convoy;
	gui_convoy_formation_t formation;
	gui_scrollpane_t scrollx_formation;
	gui_aligned_container_t cont_convoy_copier;
	consist_list_t scl_convoys;
	void update_convoy_info();

	gui_tab_panel_t tabs;

	// place the components
	void init_table();

	// update components
	void update();

	void save_order();

public:
	consist_order_frame_t(player_t* player=NULL, schedule_t *schedule=NULL, uint16 unique_entry_id=65535);

	void init(schedule_t *schedule, uint16 unique_entry_id);

	// Rebind to another schedule/entry (e.g. the button was pressed in a
	// second schedule window): saves unsaved edits to the old target first,
	// then rebuilds the entry-dependent display. Leaves the window untouched
	// when the new entry has no halt (waypoint/depot tile).
	void retarget(schedule_t *schedule, uint16 unique_entry_id);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool infowin_event(event_t const*) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;

	void open_vehicle_detail(const vehicle_desc_t* veh_type) const;

	//uint32 get_rdwr_id() OVERRIDE { return magic_consist_order; }
};


// Rule editor window (singleton): edits the hard-constraint ranges of one
// rule-based alternative (specific_vehicle == nullptr). All edits go to a
// working copy, committed to the consist window's order only by OK.
class consist_rule_editor_t : public gui_frame_t, private action_listener_t
{
public:
	enum { LINE_COUNT = 15 };

private:
	player_t *player = nullptr;
	consist_order_t *order = nullptr;
	uint32 slot_index = 0;
	uint32 alt_index = 0;
	waytype_t waytype = invalid_wt;

	// Working copy: committed to the order only by OK.
	vehicle_description_element edit_elem;
	uint8 edit_catg = 0;

	gui_combobox_t engine_selector;
	gui_combobox_t catg_selector;
	vector_tpl<uint8> catg_values; // combo selection -> slot catg_index
	gui_combobox_t class_selector;
	vector_tpl<cbuffer_t*> class_numeric_labels; // stable text for unnamed class entries
	button_t bt_empty;
	button_t bt_ok, bt_cancel;
	cbuffer_t reason_buf; // stable text for the OK-button tooltip
	button_t bt_use[LINE_COUNT];
	gui_numberinput_t num_min[LINE_COUNT], num_max[LINE_COUNT];
	// Shown (greyed) in place of the inputs when a line is not constraining, so
	// the "no limit" sentinel never has to be displayed as a number.
	gui_label_t lb_any[LINE_COUNT];
	// Explicit per-line "this line constrains" state. Deliberately NOT derived
	// from the stored bounds: a line can be meaningfully constrained while its
	// bounds still read 0/max (catering 0-5 being the obvious case), and
	// deriving state from values made such ticks revert immediately.
	bool line_used[LINE_COUNT];

	bool structured() const;
	bool valid() const;
	void get_line(uint8 line, uint32 &mn, uint32 &mx) const;
	void set_line(uint8 line, uint32 mn, uint32 mx);
	void init_line_used();
	void refresh_line(uint8 line);
	uint8 class_option_count() const;
	void rebuild_class_list();
	bool pool_desc_ok(const vehicle_desc_t *desc) const;
	bool rule_line_matchable(uint8 line) const;
	void validate();
	void refresh();
	// Per-line span (min and max) of the named attribute over all pool vehicles
	// of this slot's way type and category; used for default ticking values and
	// to cap the input range, so all bounds shown are attainable.
	bool pool_attr_bounds(uint8 line, uint32 &mn, uint32 &mx) const;
	// Editable maximum per line (= pool maximum for that attribute, falling back
	// to the static default), so inputs are never wider than the real range.
	uint32 line_edit_max[LINE_COUNT];

public:
	consist_rule_editor_t(player_t *player_, consist_order_t *order_, uint32 slot, uint32 alt, waytype_t wt);

	void retarget(player_t *player_, consist_order_t *order_, uint32 slot, uint32 alt, waytype_t wt);

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t v) OVERRIDE;
};

#endif
