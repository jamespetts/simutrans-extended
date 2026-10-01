/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SCHEDULE_GUI_H
#define GUI_SCHEDULE_GUI_H


#include "simwin.h"
#include "gui_frame.h"

#include "components/action_listener.h"
#include "components/gui_numberinput.h"
#include "components/gui_colorbox.h"
#include "components/gui_combobox.h"
#include "components/gui_button.h"
#include "components/gui_image.h"
#include "components/gui_tab_panel.h"

#include "components/gui_scrollpane.h"
#include "components/gui_schedule_item.h"

#include "../convoihandle_t.h"
#include "../linehandle_t.h"
#include "../halthandle_t.h"
#include "../tpl/vector_tpl.h"


class schedule_t;
struct schedule_entry_t;
class player_t;
class cbuffer_t;
class loadsave_t;
class gui_schedule_entry_t;

#define DELETE_FLAG (0x8000)
//#define UP_FLAG (0x4000)
#define DOWN_FLAG (0x2000)

class gui_wait_loading_schedule_t : public gui_component_t
{
	uint16 val = 0;
	uint32 flags;
public:
	gui_wait_loading_schedule_t(uint32 flags, uint16 val=0);

	void draw(scr_coord offset);

	void init_data(uint32 flags_, uint16 v = 0) { flags = flags_, val = v; };

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_schedule_couple_order_t : public gui_container_t
{
	uint16 join = 0;
	uint16 leave = 0;
	gui_label_buf_t lb_join, lb_leave;

public:
	gui_schedule_couple_order_t(uint16 leave=0, uint16 join=0);

	void draw(scr_coord offset);

	void set_value(const uint16 j, const uint16 l) { join = j; leave = l; };

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


/**
 * One entry in the list of schedule entries.
 */
class gui_schedule_entry_t : public gui_aligned_container_t, public gui_action_creator_t, public action_listener_t
{
	schedule_entry_t entry;
	bool is_current;
	bool is_air_wt;
	uint number;
	player_t* player;
	gui_image_t img_hourglass, img_nc_alert, img_layover, img_refuel, img_ignore_choose;
	gui_label_buf_t stop;
	gui_label_buf_t lb_reverse, lb_distance, lb_pos, lb_speed_limit;
	gui_schedule_entry_number_t entry_no;
	gui_waypoint_box_t wpbox;
	gui_colored_route_bar_t *route_bar;
	gui_schedule_couple_order_t *couple_order;
	gui_wait_loading_schedule_t *wait_loading;
	button_t bt_del, bt_swap;
	button_t bt_pos;

public:
	gui_schedule_entry_t(player_t* pl, schedule_entry_t e, uint n, bool air_wt = false, uint8 line_color_index = 254);

	void update_label();
	void set_distance(koord3d next_pos, uint32 distance_to_next_halt = 0, uint16 range_limit = 0);
	void set_speed_limit(uint32 speed);
	void set_line_style(uint8 s);
	void set_active(bool yesno);

	void update_entry(schedule_entry_t e) {
		entry = e;
		update_label();
	}

	// Removal/addition buttons are meaningless in display-only uses
	// (picker list, selected-entry display): hidden buttons collapse
	// (invisible + non-rigid takes no space) and leave focus handling.
	void hide_action_buttons() { bt_del.set_visible(false); bt_swap.set_visible(false); }
	void hide_swap_button() { bt_swap.set_visible(false); }

	void draw(scr_coord offset) OVERRIDE;
	bool infowin_event(const event_t *ev) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


/**
 * One schedule-entry row with press/release commit semantics for the
 * uncouple-target-entry picker: highlight on left press, commit the entry's
 * unique id on release only if the cursor is still inside the row, cancel
 * otherwise. Release events route by press position, so a press here and a
 * release elsewhere can neither commit nor leave a stale highlight: this row
 * always sees its own release.
 * The del/swap child buttons keep working (handled before the row itself);
 * their actions are swallowed by the picker window.
 */
class gui_pickable_schedule_entry_t : public gui_schedule_entry_t
{
	// The schedule entry's stable identity, committed on valid release.
	uint16 target_unique_id;
	bool press_armed = false;

public:
	gui_pickable_schedule_entry_t(player_t* pl, schedule_entry_t e, uint n) :
		gui_schedule_entry_t(pl, e, n, false), target_unique_id(e.unique_entry_id) { hide_action_buttons(); }

	bool infowin_event(const event_t *ev) OVERRIDE;
	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


/**
 * Picker window for the uncouple target entry: shows the target line/convoy
 * schedule's entries at the dividing halt as full schedule-entry rows (the
 * same visual presentation as the schedule window's stop list). Built over a
 * snapshot copy owned by the window. Commits the picked entry's unique id to
 * its listeners, then closes itself (self-destroy is kill-list-deferred while
 * inside event handling, so closing on commit is safe).
 * Transient: default get_rdwr_id (magic_reserved) keeps it out of savegames.
 */
class uncouple_entry_picker_t : public gui_frame_t, public action_listener_t, public gui_action_creator_t
{
	schedule_t *target_schedule; // snapshot copy, owned
	player_t *player;
	cbuffer_t title;

	gui_aligned_container_t rows;
	gui_scrollpane_t scroll;

public:
	uncouple_entry_picker_t(player_t *pl, schedule_t *target_snapshot, halthandle_t halt, const char *target_name);
	~uncouple_entry_picker_t() { delete target_schedule; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};

class schedule_gui_stats_t : public gui_aligned_container_t, action_listener_t, public gui_action_creator_t
{
private:
	static cbuffer_t buf;

	vector_tpl<gui_schedule_entry_t*> entries;
	schedule_t *last_schedule;
	zeiger_t *current_stop_mark;

	uint8 line_color_index = 254;

public:
	schedule_t* schedule;
	player_t* player;
	uint16 range_limit;

	schedule_gui_stats_t();
	~schedule_gui_stats_t();

	void set_schedule( schedule_t* f ) { schedule = f; }

	void set_line_color_index(uint8 idx = 254) { line_color_index = idx; }

	void highlight_schedule( schedule_t *markschedule, bool marking );

	// Draw the component
	void draw(scr_coord offset) OVERRIDE;

	void update_schedule();

	void update_current_entry() { entries[schedule->get_current_stop()]->update_entry(schedule->get_current_entry()); }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	scr_size get_max_size() const OVERRIDE { return scr_size::inf; }
};



/**
 * GUI for Schedule dialog
 */
class schedule_gui_t : public gui_frame_t, public action_listener_t
{
	enum mode_t {
		adding,
		inserting,
		removing,
		undefined_mode
	};

	mode_t mode;

	// only active with lines
	button_t bt_promote_to_line;
	gui_combobox_t line_selector;
	gui_label_buf_t lb_load, lb_wait, lb_waitlevel_as_clock, lb_spacing_as_clock;

	// UI TODO: Make the below features work with the new UI (ignore choose, layover, range stop, consist order)
	// always needed
	button_t bt_add, bt_insert, bt_consist_order; // stop management
	button_t bt_bidirectional, bt_mirror, bt_same_spacing_shift;
	button_t bt_wait_for_time, bt_discharge_payload, bt_setdown_only, bt_pickup_only;
	button_t bt_ignore_choose, bt_lay_over, bt_range_stop, bt_speed_limit;
	button_t filter_btn_all_pas, filter_btn_all_mails, filter_btn_all_freights;

	button_t bt_wait_prev, bt_wait_next;	// waiting in parts of month

	gui_numberinput_t numimp_load;

	gui_label_t lb_spacing, lb_shift, lb_plus;
	gui_numberinput_t numimp_spacing;

	gui_numberinput_t conditional_depart, condition_broadcast, numimp_speed_limit;

	gui_label_t lb_spacing_shift;
	gui_numberinput_t numimp_spacing_shift;
	gui_label_t lb_spacing_shift_as_clock;

	gui_schedule_entry_number_t *entry_no;
	gui_label_buf_t lb_entry_pos;

	gui_label_t lb_speed_limit, lb_speed_limit_kmh;
	gui_label_t lb_consist_order_modified;

	char str_parts_month[32];
	char str_parts_month_as_clock[32];

	char str_spacing_as_clock[32];
	char str_spacing_shift_as_clock[32];

	schedule_gui_stats_t *stats;
	gui_scrollpane_t scroll;

	button_t bt_couple, bt_uncouple;
	button_t bt_couple_is_line, bt_couple_is_cnv;
	button_t bt_uncouple_is_line, bt_uncouple_is_cnv;
	gui_combobox_t condition_line_selector;
	gui_combobox_t couple_target_selector;
	gui_combobox_t uncouple_target_selector;
	// Radio default for one Line/Consist pair: line mode if any eligible
	// line calls at this halt, consist mode otherwise. Only used for the
	// no-target-yet state; stored targets restore their own mode.
	void default_target_mode(bool &line_pressed, bool &cnv_pressed);
	void disable_couple_target_selector(bool is_uncouple=false);
	void update_target_line_selection(bool condition, bool couple, bool uncouple);
	void update_target_convoy_selection(bool couple, bool uncouple);

	// Opens the uncouple-target-entry picker; text follows the Line/Convoy
	// toggle mode ("Use this target line/consist/schedule from...").
	button_t bt_uncouple_entry_picker;
	// Inline display of the selected target entry (one schedule-entry row).
	// The red X clears the selection; plain clicks on the row are ignored.
	// Rebuilds are draw-deferred (see request_uncouple_entry_display):
	// handlers that run inside this container's own dispatch must never
	// remove its rows synchronously (use-after-free of the dispatch's
	// comp pointer, observed as 0xC00000FD in gui_container_t).
	gui_aligned_container_t cont_uncouple_entry;
	gui_schedule_entry_t *uncouple_entry_row = NULL;
	bool uncouple_display_dirty = false;
	void request_uncouple_entry_display() { uncouple_display_dirty = true; }
	// Live target schedule of the current entry, or NULL if none/invalid.
	schedule_t* get_uncouple_target_schedule();
	void update_uncouple_entry_button();
	void update_uncouple_entry_display();
	void close_uncouple_picker() { destroy_win(magic_uncouple_entry_picker); }

	gui_aligned_container_t cont_settings_1, cont_settings_2;
	gui_tab_panel_t tabs;

	// to add new lines automatically
	uint32 old_line_count;
	uint32 last_schedule_count;

	// Tracks for which schedule entry the couple/uncouple toggle modes were
	// last set: update_selection() preserves the user's Line/Convoy mode
	// within an entry (it rebuilds the selectors after every action) and
	// resets it when another entry is shown.
	uint8 last_toggle_stop = 255;

	// set the correct tool now ...
	void update_tool(bool set);

	// changes the waiting/loading levels if allowed
	void update_selection();

	// pas=1, mail=2, freight=3
	uint8 line_type_flags = 0;

	void update_current_entry() { stats->update_current_entry(); }

protected:
	schedule_t *schedule;
	schedule_t* old_schedule;
	player_t *player;
	convoihandle_t cnv;
	cbuffer_t title;

	linehandle_t new_line, old_line;

	gui_image_t img_electric, img_refuel;

	uint16 min_range = UINT16_MAX;
	gui_label_buf_t lb_min_range;

	void init_components();
	void build_table();

	inline void set_min_range(uint16 range) { stats->range_limit = range; };

public:
	schedule_gui_t(schedule_t* schedule = NULL, player_t* player = NULL, convoihandle_t cnv = convoihandle_t());
	// for convoi
	void init(schedule_t* schedule, player_t* player, convoihandle_t cnv = convoihandle_t());
	// for line
	void init(linehandle_t line);

	virtual ~schedule_gui_t();

	virtual uint16 get_min_top_speed_kmh() { return cnv.is_bound() ? speed_to_kmh(cnv->get_min_top_speed()) : 65535; }

	// for updating info ...
	void init_line_selector();

	bool infowin_event(event_t const*) OVERRIDE;

	const char *get_help_filename() const OVERRIDE {return "schedule.txt";}

	/**
	 * Draw the Frame
	 */
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	/**
	 * Map rotated, rotate schedules too
	 */
	void map_rotate90( sint16 ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_schedule_rdwr_dummy; }
};

#endif
