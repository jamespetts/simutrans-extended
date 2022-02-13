/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_HALT_INDICATOR_H
#define GUI_COMPONENTS_GUI_HALT_INDICATOR_H


#include "gui_container.h"

#include "../../simhalt.h"

#define HALT_CAPACITY_BAR_WIDTH 100

/**
 * Helper class to draw freight type capacity bar
 */
class gui_halt_capacity_bar_t : public gui_container_t
{
	halthandle_t halt;
	uint8 freight_type;
public:
	gui_halt_capacity_bar_t(halthandle_t h, uint8 ft);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return scr_size(HALT_CAPACITY_BAR_WIDTH + 2, GOODS_COLOR_BOX_HEIGHT); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


/**
 * Helper class to draw halt handled goods categories
 */
class gui_halt_handled_goods_images_t : public gui_container_t
{
	halthandle_t halt;
	bool show_only_freight;
public:
	gui_halt_handled_goods_images_t(halthandle_t h, bool only_freight=false);

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
