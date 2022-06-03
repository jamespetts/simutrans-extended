/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_HALT_LIST_STATS_H
#define GUI_HALT_LIST_STATS_H


#include "components/gui_component.h"

#include "components/gui_aligned_container.h"
#include "components/gui_colorbox.h"
#include "components/gui_button.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_speedbar.h"
#include "../halthandle_t.h"

class gui_halt_type_images_t;


class gui_capped_arrow_t : public gui_component_t
{
public:
	gui_capped_arrow_t() {};

	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return scr_size(5,5); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

/**
 * Helper class to show a panel whose contents are switched according to the mode
 */
class gui_halt_stats_t : public gui_aligned_container_t
{
	halthandle_t halt;
	cbuffer_t buf;

	sint32 update_seed = 0;
	uint8 old_display_mode = 255;
	gui_bandgraph_t bandgraph;

	int num[5];


public:
	uint8 display_mode = 0;
	gui_halt_stats_t(halthandle_t h);

	void update_table();

	void draw(scr_coord offset) OVERRIDE;
};


class halt_list_stats_t : public gui_aligned_container_t, public gui_scrolled_list_t::scrollitem_t
{
private:
	halthandle_t halt;
	uint8 player_nr; // for color display of other player halts

public:
	enum stats_mode_t {
		hl_waiting_detail,
		hl_facility,
		hl_serve_lines,
		hl_location,
		hl_waiting_pax,
		hl_waiting_mail,
		hl_waiting_goods,
		hl_pax_evaluation,
		hl_mail_evaluation,
		hl_pax_volume,
		hl_mail_volume,
		hl_goods_needed,
		hl_products,
		coverage_output_pax,
		coverage_output_mail,
		coverage_visitor_demands,
		coverage_job_demands,
		HALTLIST_MODES
	};

	gui_label_buf_t label_name;
	gui_image_t img_enabled[3];
	gui_halt_type_images_t *img_types;
	gui_halt_stats_t *swichable_info;
	gui_colorbox_t indicator[3];

	static uint16 name_width;

	halt_list_stats_t(halthandle_t halt_, uint8 player_nr = (uint8)-1);

	bool infowin_event(event_t const*) OVERRIDE;

	/**
	 * Draw the component
	 */
	void draw(scr_coord offset) OVERRIDE;

	void set_mode(uint8 mode) { swichable_info->display_mode = mode; };

	void update_label();

	const char* get_text() const OVERRIDE;

	bool is_valid() const OVERRIDE { return halt.is_bound(); }

	halthandle_t get_halt() const { return halt; }

	using gui_aligned_container_t::get_min_size;
	using gui_aligned_container_t::get_max_size;
};

#endif
