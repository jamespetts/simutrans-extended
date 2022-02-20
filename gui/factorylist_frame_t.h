/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_FACTORYLIST_FRAME_T_H
#define GUI_FACTORYLIST_FRAME_T_H


#include "simwin.h"
#include "../simcity.h"
#include "gui_frame.h"
#include "components/gui_scrollpane.h"
#include "components/gui_label.h"
#include "components/gui_button.h"
#include "components/gui_combobox.h"
#include "factorylist_stats_t.h"

#define MAX_FACTORY_TYPE_FILTER 4


/*
 * Factory list window
 */
class factorylist_frame_t : public gui_frame_t, private action_listener_t
{
private:
	static const char *sort_text[factorylist::SORT_MODES];
	static const char *display_mode_text[factorylist_stats_t::FACTORYLIST_MODES];
	static const char *factory_type_text[MAX_FACTORY_TYPE_FILTER];
	static const enum button_t::type factory_type_button_style[MAX_FACTORY_TYPE_FILTER];
	static uint8 factory_type_filter_bits;

	gui_combobox_t sortedby, freight_type_c, cb_display_mode, region_selector;

	button_t sorteddir;
	button_t filter_within_network, bt_cancel_cityfilter;
	button_t filter_buttons[MAX_FACTORY_TYPE_FILTER];
	gui_label_buf_t lb_target_city, lb_factory_counter;

	gui_scrolled_list_t scrolly;

	static char name_filter[256];
	char last_name_filter[256];
	gui_textinput_t name_filter_input;

	void fill_list();

	stadt_t *filter_city;
	uint32 old_factories_count;

	vector_tpl<const goods_desc_t *> viewable_freight_types;

public:
	factorylist_frame_t(stadt_t *filter_city = NULL);

	const char *get_help_filename() const OVERRIDE {return "factorylist_filter.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void map_rotate90( sint16 ) OVERRIDE { fill_list(); }

	void rdwr(loadsave_t* file) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_factorylist; }

	void set_cityfilter(stadt_t *city);
};

#endif
