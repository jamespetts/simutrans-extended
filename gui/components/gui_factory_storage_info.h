/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_FACTORY_STORAGE_INFO_H
#define GUI_COMPONENTS_GUI_FACTORY_STORAGE_INFO_H


#include "gui_container.h"
#include "gui_scrollpane.h"
#include "gui_speedbar.h"
#include "gui_label.h"

#include "../../simfab.h"
#include "../simwin.h"

#include "gui_component.h"
#include "gui_aligned_container.h"
#include "gui_colorbox.h"

#include "../../simhalt.h"

#include "../../utils/cbuffer_t.h"  // for industry chain reference(gui_goods_handled_factory_t)

class fabrik_t;


class gui_operation_status_t : public gui_colorbox_t
{
	uint8 status = operation_stop;
public:
	gui_operation_status_t(PIXVAL c = SYSCOL_TEXT);

	enum {
		operation_stop   = 0,
		operation_pause  = 1,
		operation_normal = 2,   // right pointer
		operation_ok     = 3,   // circle
		operation_caution = 4,  // alert symbol
		operation_warning = 5,  // alert symbol
		operation_invalid = 255 // nothing to show
	};

	void set_status(uint8 shape_value) { status = shape_value; }

	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_factory_operation_status_t : public gui_operation_status_t
{
	fabrik_t *fab;
public:
	gui_factory_operation_status_t(fabrik_t *factory);

	void draw(scr_coord offset) OVERRIDE;
};


/**
 * Helper class to draw a factory storage bar
 */
class gui_factory_storage_bar_t : public gui_component_t
{
	const ware_production_t* ware;
	uint32 factor;
	bool is_input_item; // which display is needed? - input or output

public:
	gui_factory_storage_bar_t(const ware_production_t* ware, uint32 factor, bool is_input_item = false);

	void draw(scr_coord offset) OVERRIDE;
	scr_size get_min_size() const OVERRIDE { return scr_size(min(100,LINESPACE*6), GOODS_COLOR_BOX_HEIGHT); }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


class gui_factory_product_item_t : public gui_aligned_container_t
{
	fabrik_t *fab;
	const ware_production_t* ware;
	bool is_input_item; // which display is needed? - input or output

	gui_operation_status_t operation_status;
	gui_label_with_symbol_t lb_leadtime; // only for suppliers
	gui_label_with_symbol_t lb_alert;


	void init_table();

public:
	gui_factory_product_item_t(fabrik_t *factory, const ware_production_t *ware, bool is_input_item = false);

	bool infowin_event(event_t const*) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};


// A GUI component of the factory storage info
class gui_factory_storage_info_t : public gui_container_t
{
private:
	fabrik_t *fab;

public:
	gui_factory_storage_info_t(fabrik_t *factory);

	void set_fab(fabrik_t *f) { this->fab = f; }

	void draw(scr_coord offset);
	void recalc_size();
};


class gui_freight_halt_stat_t : public gui_aligned_container_t, private action_listener_t
{
private:
	halthandle_t halt;
	gui_label_buf_t label_name, lb_handling_amount;
	button_t bt_show_halt_network;

	// update trigger
	uint8 old_month = 255;


public:
	gui_freight_halt_stat_t(halthandle_t halt);

	bool infowin_event(event_t const* ev) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	void draw(scr_coord offset) OVERRIDE;
};

// A display of nearby freight stations for factory GUI
class gui_factory_nearby_halt_info_t : public gui_aligned_container_t
{
private:
	fabrik_t *fab;
	uint32 old_halt_count;

public:
	gui_factory_nearby_halt_info_t(fabrik_t *factory);

	void set_fab(fabrik_t *f) { this->fab = f; update_table(); }

	void update_table();

	void draw(scr_coord offset) OVERRIDE;

};


// for industry chain reference
class gui_goods_handled_factory_t : public gui_component_t
{
private:
	bool show_consumer=false; // false=show producer
	vector_tpl<const factory_desc_t*> factory_list;
	void build_factory_list(const goods_desc_t *ware);
	goods_desc_t item;
	cbuffer_t buf;

public:
	gui_goods_handled_factory_t(const goods_desc_t *ware, bool yesno) { show_consumer = yesno; build_factory_list(ware); set_size(D_LABEL_SIZE); };
	void draw(scr_coord offset) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};

#endif
