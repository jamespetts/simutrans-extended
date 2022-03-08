/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_LINELIST_STATS_T_H
#define GUI_LINELIST_STATS_T_H


#include "components/gui_aligned_container.h"
#include "components/gui_label.h"
#include "../simline.h"


// Similar function: gui_convoy_handle_catg_img_t
class gui_line_handle_catg_img_t : public gui_container_t
{
	linehandle_t line;

public:
	gui_line_handle_catg_img_t(linehandle_t line);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE;
};


// line name with line letter code
class gui_line_label_t : public gui_aligned_container_t
{
	linehandle_t line;
	cbuffer_t name_buf;
	uint16 old_seed = -1;

public:
	gui_line_label_t(linehandle_t line);

	void update_check();
	void update();

	void draw(scr_coord offset) OVERRIDE;
};


/**
 * Helper class to show a panel whose contents are switched according to the mode
 */
class gui_line_stats_t : public gui_aligned_container_t
{
	linehandle_t line;
	cbuffer_t buf;

	sint64 old_seed = 0;
	uint8 mode = ll_frequency;

public:
	enum stats_mode_t {
		ll_frequency,
		ll_handle_goods,
		ll_route,
		LINELIST_MODES
	};

	gui_line_stats_t(linehandle_t line, uint8 mode=ll_frequency);

	void set_mode(uint8 m) { mode=m; init_table(); }

	void update_check();
	void init_table();

	void draw(scr_coord offset) OVERRIDE;
};


#endif
