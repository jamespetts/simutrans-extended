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

#include "components/gui_scrollpane.h"

#include "../convoihandle_t.h"
#include "../linehandle_t.h"
#include "../tpl/vector_tpl.h"


class schedule_t;
struct schedule_entry_t;
class player_t;
class cbuffer_t;
class loadsave_t;
class gui_schedule_entry_t;


class gui_wait_loading_schedule_t : public gui_container_t
{
	uint8 val = 0;
	uint32 flags;
public:
	gui_wait_loading_schedule_t(uint32 flags, uint8 val=0);

	void draw(scr_coord offset);

	void init(uint32 flags_, uint8 v = 0) { flags = flags_, val = v; };

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


class schedule_gui_stats_t : public gui_aligned_container_t, action_listener_t, public gui_action_creator_t
{
private:
	static cbuffer_t buf;

	vector_tpl<gui_schedule_entry_t*> entries;
	schedule_t *last_schedule;
	zeiger_t *current_stop_mark;
public:
	schedule_t* schedule;
	player_t* player;
	uint16 range_limit;

	schedule_gui_stats_t();
	~schedule_gui_stats_t();

	void set_schedule( schedule_t* f ) { schedule = f; }

	void highlight_schedule( bool marking );

	// Draw the component
	void draw(scr_coord offset) OVERRIDE;

	void update_schedule();

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
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
	button_t bt_add, bt_insert, bt_remove, bt_consist_order; // stop management
	button_t bt_bidirectional, bt_mirror, bt_same_spacing_shift;
	button_t bt_wait_for_time, bt_discharge_payload, bt_setdown_only, bt_pickup_only, bt_ignore_choose, bt_lay_over, bt_range_stop;
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

	char str_parts_month[32];
	char str_parts_month_as_clock[32];

	char str_spacing_as_clock[32];
	char str_spacing_shift_as_clock[32];

	schedule_gui_stats_t *stats;
	gui_scrollpane_t scroll;

	// to add new lines automatically
	uint32 old_line_count;
	uint32 last_schedule_count;

	// set the correct tool now ...
	void update_tool(bool set);

	// changes the waiting/loading levels if allowed
	void update_selection();

	// pas=1, mail=2, freight=3
	uint8 line_type_flags = 0;

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

	void init(schedule_t* schedule, player_t* player, convoihandle_t cnv);

	inline void set_min_range(uint16 range) { stats->range_limit = range; };

public:
	schedule_gui_t(schedule_t* schedule = NULL, player_t* player = NULL, convoihandle_t cnv = convoihandle_t());

	virtual ~schedule_gui_t();

	// for updating info ...
	void init_line_selector();

	bool infowin_event(event_t const*) OVERRIDE;

	const char *get_help_filename() const OVERRIDE {return "schedule.txt";}

	/**
	 * Draw the Frame
	 */
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	/**
	 * Set window size and adjust component sizes and/or positions accordingly
	 */
	void set_windowsize(scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	/**
	 * Map rotated, rotate schedules too
	 */
	void map_rotate90( sint16 ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_schedule_rdwr_dummy; }
};

#endif
