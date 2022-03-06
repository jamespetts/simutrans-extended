/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_FACTORYLIST_STATS_T_H
#define GUI_FACTORYLIST_STATS_T_H


#include "components/gui_colorbox.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_scrolled_list.h"
#include "components/gui_speedbar.h"
#include "../simfab.h"

class fabrik_t;


namespace factorylist {
	enum sort_mode_t {
		by_name = 0,
		by_available,
		by_output,
		by_maxprod,
		by_status,
		by_power,
		by_sector,
		by_staffing,
		by_operation_rate,
		by_region,
		SORT_MODES,

		// the last two are unused
		by_input,
		by_transit
	};
};


/**
 * Helper class to draw a combined factory storage bar
 */
class gui_combined_factory_storage_bar_t : public gui_component_t
{
	fabrik_t *fab;
	bool is_output;

public:
	gui_combined_factory_storage_bar_t(fabrik_t *fab, bool is_output=false);

	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return scr_size(LINESPACE*5, GOODS_COLOR_BOX_HEIGHT); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


/**
 * Helper class to show a panel whose contents are switched according to the mode
 */
class gui_factory_stats_t : public gui_aligned_container_t
{
	fabrik_t *fab;
	cbuffer_t buf;

	uint32 old_month = 0;
	uint8 old_display_mode = 255;

	// It is used to indicate status and changes
	gui_image_t image; // for electricity or in_transit
	gui_operation_status_t receipt_status, producing_status, forwarding_status;

	gui_label_buf_t realtime_buf;

	gui_speedbar_fixed_length_t staffing_bar;
	gui_combined_factory_storage_bar_t input_bar, output_bar;
	sint32 staffing_level;
	sint32 staffing_level2;
	sint32 staff_shortage_factor;

	void update_operation_status(uint8 target_month=0);

public:
	uint8 display_mode = 0;
	gui_factory_stats_t(fabrik_t *fab);

	void update_table();

	void draw(scr_coord offset) OVERRIDE;
};


/**
 * Factory list stats display
 * Where factory stats are calculated for list dialog
 */
class factorylist_stats_t : public gui_aligned_container_t, public gui_scrolled_list_t::scrollitem_t
{
private:
	fabrik_t *fab;
	gui_colorbox_t indicator;
	//gui_image_t halt_connected; // TODO:
	gui_label_buf_t lb_name;

	gui_factory_stats_t *swichable_info;

	uint16 tmp_name_width;

	void update_label();
public:
	enum stats_mode_t {
		fl_operation,
		fl_storage,
		fl_activity,
		fl_goods_needed,
		fl_product,
		fl_location,
		FACTORYLIST_MODES
	};

	static uint8 sort_mode, region_filter, display_mode;
	static bool reverse, filter_own_network;
	static uint8 filter_goods_catg;
	static uint16 name_width;

	factorylist_stats_t(fabrik_t *);

	void draw( scr_coord pos) OVERRIDE;

	void set_mode(uint8 mode) { swichable_info->display_mode = mode; };

	char const* get_text() const OVERRIDE { return fab->get_name(); }
	bool infowin_event(const event_t *) OVERRIDE;
	bool is_valid() const OVERRIDE;
	void set_size(scr_size size) OVERRIDE;

	static bool compare(const gui_component_t *a, const gui_component_t *b );
};


#endif
