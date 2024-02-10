/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_FACTORY_LEGEND_H
#define GUI_FACTORY_LEGEND_H


#include "gui_frame.h"
#include "map_frame.h"
#include "components/gui_scrollpane.h"
#include "components/gui_label.h"
#include  "../display/simgraph.h"

#include "../descriptor/factory_desc.h"


class factory_legend_t : public gui_frame_t, action_listener_t
{
	/*
	 * We are bound to this window. All filter states are stored in main_frame.
	 */
	map_frame_t *main_frame;

	const factory_desc_t *focus_fac=NULL;
	uint8 focused_factory_side=1; // 0=left, 1=nothig, 2=right

	bool double_column=false;
	button_t bt_open_2nd_column;
	button_t filter_buttons[3];
	button_t hide_mismatch;

	uint8 old_catg_index = goods_manager_t::INDEX_NONE; // update flag

	gui_label_with_symbol_t lb_input, lb_output;

	gui_aligned_container_t cont_left, cont_right, cont_filter_buttons;
	gui_scrollpane_t scrolly_left, scrolly_right;

public:
	factory_legend_t(map_frame_t *parent);

	enum {
		PRODUCER     = 0,
		MANUFACTURER = 1,
		CONSUMER     = 2,
		MAX_IND_TYPE = 3
	};

	// Not enough space for gadgets
	//const char * get_help_filename() const OVERRIDE { return "factory_legend.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	void update_factory_legend();

	void set_focus_factory(const factory_desc_t *fac_desc, uint8 side = 1);

	// Is the item currently in focus?
	bool is_focused(const factory_desc_t *fac_desc, uint8 side = 1);

	// Does the factory have a goods-based connection with the focused factory?
	// side specifies the direction of the flow of goods. That is, 0 and 2 have opposite directions.
	const goods_desc_t *is_linked(const factory_desc_t *fac_desc, uint8 side = 1);
};

#endif
