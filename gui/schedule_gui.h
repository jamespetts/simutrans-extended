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
public:
	gui_wait_loading_schedule_t(uint16 val=0);

	void draw(scr_coord offset);

	void init_data(uint16 v = 0) { val = v; };

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
	gui_image_t img_hourglass, img_nc_alert;
	gui_label_buf_t stop;
	gui_label_buf_t lb_reverse, lb_distance, lb_pos;
	gui_schedule_entry_number_t entry_no;
	gui_waypoint_box_t wpbox;
	gui_colored_route_bar_t *route_bar;
	gui_wait_loading_schedule_t *wait_loading;
	button_t bt_del, bt_swap;
	button_t bt_pos;

public:
	gui_schedule_entry_t(player_t* pl, schedule_entry_t e, uint n, bool air_wt = false, uint8 line_color_index = 254);

	void update_label();
	void set_distance(koord3d next_pos, uint32 distance_to_next_halt = 0, uint16 range_limit = 0);
	void set_line_style(uint8 s);
	void set_active(bool yesno);

	void update_entry(schedule_entry_t e) {
		entry = e;
		update_label();
	}

	void draw(scr_coord offset) OVERRIDE;
	bool infowin_event(const event_t *ev) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


class entry_index_scrollitem_t : public gui_scrolled_list_t::const_text_scrollitem_t
{
	uint8 index;

public:
	//uint16 unique_entry_id;
	entry_index_scrollitem_t(uint8 entry_index, schedule_entry_t entry) : gui_scrolled_list_t::const_text_scrollitem_t(NULL, SYSCOL_TEXT) {
		index=entry_index;
		//unique_entry_id = entry.unique_entry_id;
	}

	char const* get_text() const OVERRIDE
	{
		static char str[3];
		sprintf(str, "%u", index+1);
		return str;
	}
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
	button_t bt_add, bt_insert/*, bt_revert*/; // stop management
	button_t bt_bidirectional, bt_mirror, bt_same_spacing_shift;
	button_t bt_wait_for_time;
	button_t filter_btn_all_pas, filter_btn_all_mails, filter_btn_all_freights;

	button_t bt_wait_prev, bt_wait_next;	// waiting in parts of month

	gui_numberinput_t numimp_load;

	gui_label_t lb_spacing, lb_shift, lb_plus;
	gui_numberinput_t numimp_spacing;

	gui_label_t lb_spacing_shift;
	gui_numberinput_t numimp_spacing_shift;
	gui_label_t lb_spacing_shift_as_clock;

	gui_schedule_entry_number_t *entry_no;
	gui_label_buf_t lb_entry_pos;

	char str_parts_month[32];
	char str_parts_month_as_clock[32];

	char str_spacing_as_clock[32];
	char str_spacing_shift_as_clock[32];

	schedule_gui_stats_t *stats;
	gui_scrollpane_t scroll;

	gui_aligned_container_t cont_settings_1;
	gui_tab_panel_t tabs;

	// to add new lines automatically
	uint32 old_line_count;
	uint32 last_schedule_count;

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

	gui_image_t img_electric;

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
