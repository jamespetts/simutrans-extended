/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_LINE_WAITING_STATUS_H
#define GUI_LINE_WAITING_STATUS_H


#include "components/gui_component.h"
#include "components/gui_aligned_container.h"
#include "components/gui_label.h"
#include "components/gui_schedule_item.h"
#include "../simline.h"
#include "../simhalt.h"


class gui_convoy_access_arrow_t : public gui_aligned_container_t
{
	convoihandle_t cnv;
	cbuffer_t tooltip_buf;

public:
	gui_convoy_access_arrow_t(convoihandle_t cnv);
	bool infowin_event(event_t const*) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};


class gui_line_convoy_location_t : public gui_aligned_container_t
{
	linehandle_t line;
	uint8 section; // schedule entry
	uint16 convoy_count = 0;

public:
	gui_line_convoy_location_t(linehandle_t line_, uint8 prev_entry)
	{
		line = line_;
		section = prev_entry;
		set_table_layout(0, 1);
		check_convoy();
	}

	void check_convoy();

	void draw(scr_coord offset) OVERRIDE;

};


class gui_halt_waiting_catg_t : public gui_aligned_container_t
{
	halthandle_t halt;
	linehandle_t line;
	uint8 catg_index;
	cbuffer_t buf;

	// for direction
	uint8 start_entry; // End of upward entry search
	uint8 this_entry;  // schedule entry number of this stop
	uint8 end_entry;   // End of downward entry search

	// for update flag.
	uint32 update_seed = 0;
	bool divide_by_class;

public:
	gui_halt_waiting_catg_t(halthandle_t h, uint8 catg, linehandle_t line = linehandle_t(), bool divide_by_class = true, uint8 start_entry=0, uint8 this_entry=0, uint8 end_entry = 0);

	void update();

	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_line_waiting_status_t : public gui_aligned_container_t
{
	linehandle_t line;

	schedule_t *schedule;

	bool show_name=true;
	bool divide_by_class = false;
	bool filter_by_line = true;

	uint32 update_time;

public:
	gui_line_waiting_status_t(linehandle_t line);

	void init();

	// for reload from the save
	void set_line(linehandle_t line_) { line = line_; init(); }
	void set_show_name(bool yesno) { show_name = yesno; init(); }
	void set_divided_by_class(bool yesno) { divide_by_class = yesno; init(); }
	void set_filter_by_line(bool yesno) { filter_by_line = yesno; init(); }

	void draw(scr_coord offset) OVERRIDE;
};

#endif
