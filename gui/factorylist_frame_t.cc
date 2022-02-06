/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "factorylist_frame_t.h"
#include "gui_theme.h"
#include "../dataobj/translator.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../dataobj/environment.h"


char factorylist_frame_t::name_filter[256];
uint8 factorylist_frame_t::factory_type_filter_bits = 255;

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

const char *factorylist_frame_t::display_mode_text[factorylist_stats_t::FACTORYLIST_MODES] =
{
	"fl_btn_operation",
	"fl_btn_storage",
	"fl_stats_last_month",
	"Needed",
	"Products",
	"fl_btn_region"
};


const char *factorylist_frame_t::factory_type_text[MAX_FACTORY_TYPE_FILTER] =
{
	"All",
	"gl_prod",
	"Manufacturers",
	"gl_con"
};


const enum button_t::type factorylist_frame_t::factory_type_button_style[MAX_FACTORY_TYPE_FILTER] =
{
	button_t::roundbox_state, button_t::roundbox_left_state, button_t::roundbox_middle_state, button_t::roundbox_right_state
};


class playername_const_scroll_item_t : public gui_scrolled_list_t::const_text_scrollitem_t {
public:
	const uint8 player_nr;
	playername_const_scroll_item_t( player_t *pl ) : gui_scrolled_list_t::const_text_scrollitem_t( pl->get_name(), color_idx_to_rgb(pl->get_player_color1()+env_t::gui_player_color_dark) ), player_nr(pl->get_player_nr()) { }
};

factorylist_frame_t::factorylist_frame_t(stadt_t* city) :
	gui_frame_t( translator::translate("fl_title") ),
	scrolly(gui_scrolled_list_t::windowskin, factorylist_stats_t::compare),
	filter_city(city)
{
	old_factories_count = 0;

	set_table_layout(1,0);
	add_table(2,3);
	{
		new_component<gui_label_t>("hl_txt_sort");
		add_table(3,1);
		{
			new_component<gui_label_t>("Filter:");
			name_filter_input.set_text(name_filter, lengthof(name_filter));
			name_filter_input.set_width(D_BUTTON_WIDTH);
			add_component(&name_filter_input);


			filter_within_network.init(button_t::square_state, "Within own network");
			filter_within_network.set_tooltip("Show only connected to own transport network");
			filter_within_network.add_listener(this);
			filter_within_network.pressed = factorylist_stats_t::filter_own_network;
			add_component(&filter_within_network);
		}
		end_table();

		// 2nd row
		for (size_t i = 0; i < lengthof(sort_text); i++) {
			sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
		}
		sortedby.set_selection(factorylist_stats_t::sort_mode);
		sortedby.add_listener(this);
		add_component(&sortedby);

		add_table(6,1);
		{
			sorteddir.init(button_t::sortarrow_state, NULL);
			sorteddir.set_tooltip(translator::translate("hl_btn_sort_order"));
			sorteddir.add_listener(this);
			sorteddir.pressed = factorylist_stats_t::reverse;
			add_component(&sorteddir);

			new_component<gui_margin_t>(LINESPACE);
			viewable_freight_types.append(NULL);
			freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All freight types"), SYSCOL_TEXT);
			for (int i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
				const goods_desc_t *freight_type = goods_manager_t::get_info_catg(i);
				const int index = freight_type->get_catg_index();
				if (index == goods_manager_t::INDEX_NONE || freight_type->get_catg() == 0) {
					continue;
				}
				freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(freight_type->get_catg_name()), SYSCOL_TEXT, freight_type->get_catg_symbol());
				viewable_freight_types.append(freight_type);
			}
			for (int i = 0; i < goods_manager_t::get_count(); i++) {
				const goods_desc_t *ware = goods_manager_t::get_info(i);
				if (ware->get_catg() == 0 && ware->get_index() > 2) {
					// Special freight: Each good is special
					viewable_freight_types.append(ware);
					freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(ware->get_name()), SYSCOL_TEXT, ware->get_catg_symbol());
				}
			}
			freight_type_c.set_selection((factorylist_stats_t::filter_goods_catg == goods_manager_t::INDEX_NONE) ? 0 : factorylist_stats_t::filter_goods_catg);

			freight_type_c.add_listener(this);
			add_component(&freight_type_c);

			if (!welt->get_settings().regions.empty()) {
				//region_selector
				region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All regions"), SYSCOL_TEXT);

				for (uint8 r = 0; r < welt->get_settings().regions.get_count(); r++) {
					region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(welt->get_settings().regions[r].name.c_str()), SYSCOL_TEXT);
				}
				region_selector.set_selection(factorylist_stats_t::region_filter);
				region_selector.set_width_fixed(true);
				region_selector.set_rigid(false);
				region_selector.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
				region_selector.add_listener(this);
				add_component(&region_selector);
			}
			else {
				new_component<gui_empty_t>();
			}

			lb_target_city.set_visible(false);
			lb_target_city.set_rigid(false);
			lb_target_city.set_color(SYSCOL_TEXT_HIGHLIGHT);
			add_component(&lb_target_city);

			bt_cansel_cityfilter.init(button_t::roundbox, "reset");
			bt_cansel_cityfilter.add_listener(this);
			bt_cansel_cityfilter.set_visible(false);
			bt_cansel_cityfilter.set_rigid(false);
			add_component(&bt_cansel_cityfilter);
		}
		end_table();

		add_component(&lb_factory_counter);
		add_table(6,1)->set_spacing(scr_size(0,0));
		{
			for (uint8 i = 0; i < factorylist_stats_t::FACTORYLIST_MODES; i++) {
				cb_display_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(display_mode_text[i]), SYSCOL_TEXT);
			}
			cb_display_mode.set_selection(factorylist_stats_t::display_mode);
			cb_display_mode.set_width_fixed(true);
			cb_display_mode.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
			cb_display_mode.add_listener(this);
			add_component(&cb_display_mode);

			new_component<gui_margin_t>(D_V_SPACE);

			filter_buttons[0].init(button_t::roundbox_state, factory_type_text[0], scr_coord(0, 0), scr_size(proportional_string_width(translator::translate("All")) + D_BUTTON_PADDINGS_X, D_BUTTON_HEIGHT));
			filter_buttons[0].pressed = (factory_type_filter_bits == 255);
			filter_buttons[0].add_listener(this);
			add_component(&filter_buttons[0]);
			for (uint8 i = 1; i < MAX_FACTORY_TYPE_FILTER; i++) {
				filter_buttons[i].init(factory_type_button_style[i], factory_type_text[i]);
				filter_buttons[i].add_listener(this);
				filter_buttons[i].pressed = factory_type_filter_bits & (1 << (i - 1));
				add_component(filter_buttons + i);
			}
		}
		end_table();
	}
	end_table();

	set_cityfilter(city);

	add_component(&scrolly);
	fill_list();

	set_resizemode(diagonal_resize);
	scrolly.set_maximize(true);
	reset_min_windowsize();
}


/**
 * This method is called if an action is triggered
 */
bool factorylist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if (comp == &sortedby) {
		factorylist_stats_t::sort_mode = v.i;
		scrolly.sort(0);
	}
	else if (comp == &region_selector) {
		factorylist_stats_t::region_filter = max(0, v.i);
		fill_list();
	}
	else if (comp == &sorteddir) {
		factorylist_stats_t::reverse = !factorylist_stats_t::reverse;
		sorteddir.pressed = factorylist_stats_t::reverse;
		scrolly.sort(0);
	}
	else if (comp == &cb_display_mode) {
		int tmp = cb_display_mode.get_selection();
		if (tmp < 0 || tmp >= cb_display_mode.count_elements())
		{
			tmp = 0;
		}
		cb_display_mode.set_selection(tmp);
		factorylist_stats_t::display_mode = tmp;
		scrolly.sort(0);
		resize(scr_coord(0,0));
	}
	else if (comp == &filter_within_network) {
		factorylist_stats_t::filter_own_network = !factorylist_stats_t::filter_own_network;
		filter_within_network.pressed = factorylist_stats_t::filter_own_network;
		fill_list();
	}
	else if (comp == &freight_type_c) {
		if (freight_type_c.get_selection() > 0) {
			factorylist_stats_t::filter_goods_catg = viewable_freight_types[freight_type_c.get_selection()]->get_catg_index();
		}
		else if (freight_type_c.get_selection() == 0) {
			factorylist_stats_t::filter_goods_catg = goods_manager_t::INDEX_NONE;
		}
		fill_list();
	}
	else if (comp == &bt_cansel_cityfilter) {
		set_cityfilter(NULL);
	}
	else if (comp == &filter_buttons[0]) {
		filter_buttons[0].pressed ^= 1;
		if (filter_buttons[0].pressed) {
			for (int i = 1; i < MAX_FACTORY_TYPE_FILTER; i++) {
				filter_buttons[i].pressed = true;
			}
			factory_type_filter_bits = 255;
		}
		else {
			for (int i = 1; i < MAX_FACTORY_TYPE_FILTER; i++) {
				factory_type_filter_bits &= ~(1 << (i - 1));
				filter_buttons[i].pressed = false;
			}
		}
		fill_list();
	}
	else {
		for (int i = 1; i < MAX_FACTORY_TYPE_FILTER; i++) {
			if (comp == filter_buttons + i) {
				filter_buttons[0].pressed = false;
				filter_buttons[i].pressed ^= 1;
				if (filter_buttons[i].pressed) {
					factory_type_filter_bits |= (1 << (i - 1));
				}
				else {
					factory_type_filter_bits &= ~(1 << (i - 1));
				}

				if (factory_type_filter_bits == 255) {
					filter_buttons[0].pressed = true;
				}
				fill_list();
				return true;
			}
		}

	}

	return true;
}


void factorylist_frame_t::fill_list()
{
	old_factories_count = world()->get_fab_list().get_count(); // to avoid too many redraws ...
	scrolly.clear_elements();
	uint32 count = 0;
	FOR(const vector_tpl<fabrik_t *>,fab,world()->get_fab_list()) {
		if (factorylist_stats_t::region_filter && (factorylist_stats_t::region_filter-1) != world()->get_region(fab->get_pos().get_2d())) {
			continue;
		}
		if (filter_city != NULL && filter_city != fab->get_city()) {
			continue;
		}
		if (last_name_filter[0] != 0 && !utf8caseutf8(fab->get_name(), last_name_filter)) {
			continue;
		}
		// filter by producer/manufacturer/endconsumer
		if (!filter_buttons[0].pressed) {
			bool match = false;
			if (filter_buttons[1].pressed && (!fab->get_output().empty() && fab->get_input().empty())) {
				match = true;
			}
			if (!match && filter_buttons[2].pressed && (!fab->get_output().empty() && !fab->get_input().empty())) {
				match = true;
			}
			if (!match && filter_buttons[3].pressed && (!fab->get_input().empty() && fab->get_output().empty())) {
				match = true;
			}
			if (!match) {
				continue;
			}
		}
		// filter by goods category
		if (factorylist_stats_t::filter_goods_catg != goods_manager_t::INDEX_NONE && !fab->has_goods_catg_demand(factorylist_stats_t::filter_goods_catg)) {
			continue;
		}

		if (!factorylist_stats_t::filter_own_network ||
			(factorylist_stats_t::filter_own_network && fab->is_connected_to_network(world()->get_active_player()))) {
			scrolly.new_component<factorylist_stats_t>(fab);
			count++;
		}
	}
	lb_factory_counter.buf().printf("%u/%u", count, world()->get_fab_list().get_count());
	lb_factory_counter.update();
	scrolly.sort(0);
	scrolly.set_size(scrolly.get_size());
}

void factorylist_frame_t::set_cityfilter(stadt_t *city)
{
	filter_city = city;
	region_selector.set_visible(filter_city == NULL && !welt->get_settings().regions.empty());
	bt_cansel_cityfilter.set_visible(filter_city != NULL);
	lb_target_city.set_visible(filter_city != NULL);
	if (city) {
		filter_within_network.pressed = false;
		factorylist_stats_t::filter_own_network = false;
		factorylist_stats_t::region_filter = 0;
		region_selector.set_selection(0);
		lb_target_city.buf().printf("%s>%s", translator::translate("City"), city->get_name());
		lb_target_city.update();
	}
	resize(scr_size(0, 0));
	fill_list();
}


void factorylist_frame_t::draw(scr_coord pos, scr_size size)
{
	if(  world()->get_fab_list().get_count() != old_factories_count  ||  strcmp(last_name_filter, name_filter)  ) {
		strcpy(last_name_filter, name_filter);
		fill_list();
	}

	gui_frame_t::draw(pos,size);
}


void factorylist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();

	size.rdwr(file);
	scrolly.rdwr(file);
	file->rdwr_byte(factorylist_stats_t::sort_mode);
	file->rdwr_bool(factorylist_stats_t::reverse);
	file->rdwr_bool(factorylist_stats_t::filter_own_network);
	if (file->is_loading() && file->is_version_ex_less(14,50)) {
		uint8 dummy=0;
		file->rdwr_byte(dummy);
	}
	file->rdwr_byte(factorylist_stats_t::display_mode);

	if (file->is_version_ex_atleast(14,50)) {
		file->rdwr_byte(factorylist_stats_t::region_filter);
		freight_type_c.rdwr(file);
		file->rdwr_str(name_filter, lengthof(name_filter));
	}
	if (file->is_loading()) {
		sorteddir.pressed = factorylist_stats_t::reverse;
		sortedby.set_selection(factorylist_stats_t::sort_mode);
		filter_within_network.pressed = factorylist_stats_t::filter_own_network;
		name_filter_input.set_text(name_filter, lengthof(name_filter));
		if (freight_type_c.get_selection() > 0) {
			factorylist_stats_t::filter_goods_catg = viewable_freight_types[freight_type_c.get_selection()]->get_catg_index();
		}
		else if (freight_type_c.get_selection() == 0) {
			factorylist_stats_t::filter_goods_catg = goods_manager_t::INDEX_NONE;
		}
		cb_display_mode.set_selection(factorylist_stats_t::display_mode);
		region_selector.set_selection(factorylist_stats_t::region_filter);
		fill_list();
		set_windowsize(size);
	}
}
