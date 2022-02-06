/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_CITYLIST_STATS_T_H
#define GUI_CITYLIST_STATS_T_H


#include "components/gui_aligned_container.h"
#include "components/gui_label.h"
#include "components/gui_scrolled_list.h"
#include "../simcity.h"

#include "components/gui_image.h"

class stadt_t;


#include "components/gui_speedbar.h"
/**
 * Helper class to show a panel whose contents are switched according to the mode
 */
class gui_city_stats_t : public gui_aligned_container_t
{
	stadt_t* city;
	cbuffer_t buf;

	sint32 update_seed = 0;
	uint8 mode = 255;
	gui_bandgraph_t bandgraph;

	int num[4];

public:
	gui_city_stats_t(stadt_t *c);

	void update_table();

	void draw(scr_coord offset) OVERRIDE;
};


// City list stats display
class citylist_stats_t : public gui_aligned_container_t, public gui_scrolled_list_t::scrollitem_t
{
private:
	stadt_t* city;

	gui_label_buf_t lb_name;
	gui_image_t electricity;
	void update_label();

public:
	enum sort_mode_t {
		SORT_BY_NAME = 0,
		SORT_BY_SIZE,
		SORT_BY_GROWTH,
		SORT_BY_JOBS,
		SORT_BY_VISITOR_DEMANDS,
		SORT_BY_TRANSPORTED,
		SORT_BY_RATIO_PAX,
		SORT_BY_SENT,
		SORT_BY_RATIO_MAIL,
		SORT_BY_GOODS_DEMAND,
		SORT_BY_GOODS_RECEIVED,
		SORT_BY_RATIO_GOODS,
		SORT_BY_LAND_AREA,
		SORT_BY_POPULATION_DENSITY,
		SORT_BY_REGION,
#ifdef DEBUG
		SORT_BY_JOB_DEMAND,
		SORT_BY_RES_DEMAND,
#endif // DEBUG
		SORT_MODES
	};

	enum stats_mode_t {
		cl_general,
		cl_population,
		cl_jobs,
		cl_visitor_demand,
		pax_traffic,
		mail_traffic,
		goods_traffic,
		goods_demand,
		goods_product,
#ifdef DEBUG
		dbg_demands,
#endif // DEBUG
		CITYLIST_MODES
	};

	gui_city_stats_t *swichable_info;

	static uint8 sort_mode, region_filter, display_mode;
	static bool sortreverse, filter_own_network;
	static uint16 name_width;

	// Used to adjust the graph scale
	static uint32 world_max_value;
	static void recalc_wold_max();

public:
	citylist_stats_t(stadt_t *);

	void draw( scr_coord pos) OVERRIDE;

	char const* get_text() const OVERRIDE { return city->get_name(); }
	bool is_valid() const OVERRIDE;
	bool infowin_event(const event_t *) OVERRIDE;
	void set_size(scr_size size) OVERRIDE;

	static bool compare(const gui_component_t *a, const gui_component_t *b);
};

#endif
