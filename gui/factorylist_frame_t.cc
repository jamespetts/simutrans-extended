/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "factorylist_frame_t.h"
#include "gui_theme.h"
#include "../dataobj/translator.h"

bool factorylist_frame_t::sortreverse = false;

factorylist::sort_mode_t factorylist_frame_t::sortby = factorylist::by_name;
static uint8 default_sortmode = 0;

// filter by within current player's network
bool factorylist_frame_t::filter_own_network = false;
uint8 factorylist_frame_t::filter_goods_catg = goods_manager_t::INDEX_NONE;
uint8 factorylist_frame_t::display_mode = 0;

const char *factorylist_frame_t::sort_text[factorylist::SORT_MODES] = {
	"Fabrikname",
	"Input",
	"Output",
	"Produktion",
	"Rating",
	"Power",
	"Sector",
	"Staffing",
	"Operation rate",
	"by_region"
};

const char *factorylist_frame_t::display_mode_text[FACTORYLIST_MODES] = {
	"fl_btn_operation",
	"fl_btn_storage",
	"fl_btn_demand",
	"fl_btn_region"
};


factorylist_frame_t::factorylist_frame_t() :
	gui_frame_t( translator::translate("fl_title") ),
	stats(sortby,sortreverse, filter_own_network, filter_goods_catg),
	scrolly(&stats)
{
	set_table_layout(1, 0);
	add_table(2, 2);
	{
		new_component<gui_label_t>("hl_txt_sort"); // (1,1)

		filter_within_network.init(button_t::square_state, "Within own network");
		filter_within_network.set_tooltip("Show only connected to own transport network");
		filter_within_network.add_listener(this);
		filter_within_network.pressed = filter_own_network;
		add_component(&filter_within_network); // (1,2)

		add_table(3,1);
		{
			for (int i = 0; i < factorylist::SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(default_sortmode);
			sortedby.set_width_fixed(true);
			sortedby.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_EDIT_HEIGHT));
			sortedby.add_listener(this);
			add_component(&sortedby); // (2,1,1)

			// sort asc/desc switching button
			sorteddir.init(button_t::sortarrow_state, "");
			sorteddir.set_tooltip(translator::translate("hl_btn_sorteddir"));
			sorteddir.add_listener(this);
			sorteddir.pressed = sortreverse;
			add_component(&sorteddir); // (2,1,2)

			new_component<gui_margin_t>(LINESPACE); // (2,1,3)
		}
		end_table(); // (2,1)

		add_table(2, 1);
		{
			viewable_freight_types.append(NULL);
			freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All freight types"), SYSCOL_TEXT);
			for (int i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
				const goods_desc_t *freight_type = goods_manager_t::get_info_catg(i);
				const int index = freight_type->get_catg_index();
				if (index == goods_manager_t::INDEX_NONE || freight_type->get_catg() == 0) {
					continue;
				}
				freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(freight_type->get_catg_name()), SYSCOL_TEXT);
				viewable_freight_types.append(freight_type);
			}
			for (int i = 0; i < goods_manager_t::get_count(); i++) {
				const goods_desc_t *ware = goods_manager_t::get_info(i);
				if (ware->get_catg() == 0 && ware->get_index() > 2) {
					// Special freight: Each good is special
					viewable_freight_types.append(ware);
					freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(ware->get_name()), SYSCOL_TEXT);
				}
			}
			freight_type_c.set_selection((filter_goods_catg == goods_manager_t::INDEX_NONE) ? 0 : filter_goods_catg);
			set_filter_goods_catg(filter_goods_catg);

			freight_type_c.add_listener(this);
			add_component(&freight_type_c); // (2,2,1)

			btn_display_mode.init(button_t::roundbox, translator::translate(display_mode_text[display_mode]), scr_coord(BUTTON4_X, 14), D_BUTTON_SIZE);
			btn_display_mode.add_listener(this);
			add_component(&btn_display_mode); // (2,2,2)
		}
		end_table(); // (2,2)

	}
	end_table(); // (2,0)

	scrolly.set_scroll_amount_y(LINESPACE+1);
	add_component(&scrolly); // (3,0)
	scrolly.set_maximize(true);

	display_list();

	reset_min_windowsize();
	set_resizemode(diagonal_resize);
}



/**
 * This method is called if an action is triggered
 */
bool factorylist_frame_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	if(comp == &sortedby) {
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			set_sortierung((factorylist::sort_mode_t)tmp);
		}
		else {
			sortedby.set_selection(0);
			set_sortierung(factorylist::by_name);
		}
		default_sortmode = (uint8)tmp;
		display_list();
	}
	else if (comp == &sorteddir) {
		set_reverse(!get_reverse());
		display_list();
		sorteddir.pressed = sortreverse;
	}
	else if (comp == &filter_within_network) {
		filter_own_network = !filter_own_network;
		filter_within_network.pressed = filter_own_network;
		display_list();
	}
	else if (comp == &btn_display_mode) {
		display_mode = (++display_mode)%FACTORYLIST_MODES;
		btn_display_mode.set_text(translator::translate(display_mode_text[display_mode]));
		stats.display_mode = display_mode;
	}
	else if (comp == &freight_type_c) {
		if (freight_type_c.get_selection() > 0) {
			filter_goods_catg = viewable_freight_types[freight_type_c.get_selection()]->get_catg_index();
		}
		else if (freight_type_c.get_selection() == 0) {
			filter_goods_catg = goods_manager_t::INDEX_NONE;
		}
		display_list();
	}
	return true;
}


void factorylist_frame_t::display_list()
{
	stats.sort(sortby, get_reverse(), get_filter_own_network(), filter_goods_catg);
	stats.recalc_size();
}

void factorylist_frame_t::draw(scr_coord pos, scr_size size)
{
	display_list();

	gui_frame_t::draw(pos, size);
}


void factorylist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();
	uint8 sm = (uint8)sortby;

	size.rdwr(file);
	scrolly.rdwr(file);
	//file->rdwr_str(name_filter, lengthof(name_filter));
	file->rdwr_byte(sm);
	file->rdwr_bool(sortreverse);
	file->rdwr_bool(filter_own_network);
	file->rdwr_byte(filter_goods_catg);
	file->rdwr_byte(display_mode);
	file->rdwr_byte(default_sortmode);

	if (file->is_loading()) {
		sortby = (factorylist::sort_mode_t)sm;
		sortedby.set_selection(default_sortmode);
		freight_type_c.set_selection((filter_goods_catg == goods_manager_t::INDEX_NONE) ? 0 : filter_goods_catg);
		set_filter_goods_catg(filter_goods_catg);
		sorteddir.pressed = sortreverse;
		filter_within_network.pressed = filter_own_network;
		btn_display_mode.set_text(translator::translate(display_mode_text[display_mode]));
		stats.display_mode = display_mode;
		display_list();
		set_windowsize(size);
	}
}
