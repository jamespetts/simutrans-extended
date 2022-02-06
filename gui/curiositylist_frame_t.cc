/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "curiositylist_frame_t.h"
#include "curiositylist_stats_t.h"

#include "components/gui_label.h"
#include "../dataobj/translator.h"
#include "../simcolor.h"
#include "../simworld.h"
#include "../obj/gebaeude.h"
#include "../descriptor/building_desc.h"


char curiositylist_frame_t::name_filter[256];

const char* sort_text[curiositylist::SORT_MODES] = {
	"hl_btn_sort_name",
	"Passagierrate",
	"sort_pas_arrived",
	/*"by_city",*/
	"by_region"
};

class attraction_item_t : public gui_scrolled_list_t::const_text_scrollitem_t {
public:
	attraction_item_t(uint8 i) : gui_scrolled_list_t::const_text_scrollitem_t(translator::translate(sort_text[i]), SYSCOL_TEXT) { }
};

curiositylist_frame_t::curiositylist_frame_t(stadt_t* city) :
	gui_frame_t(translator::translate("curlist_title")),
	scrolly(gui_scrolled_list_t::windowskin, curiositylist_stats_t::compare),
	filter_city(city)
{
	attraction_count = 0;

	set_table_layout(1, 0);
	add_table(2,2);
	{
		// 1st row
		new_component<gui_label_t>("hl_txt_sort");

		add_table(5,1);
		{
			new_component<gui_label_t>("Filter:");
			name_filter_input.set_text(name_filter, lengthof(name_filter));
			add_component(&name_filter_input);

			if (!welt->get_settings().regions.empty()) {
				//region_selector
				region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All regions"), SYSCOL_TEXT);

				for (uint8 r = 0; r < welt->get_settings().regions.get_count(); r++) {
					region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(welt->get_settings().regions[r].name.c_str()), SYSCOL_TEXT);
				}
				region_selector.set_selection(curiositylist_stats_t::region_filter);
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

		// 2nd row
		for (int i = 0; i < curiositylist::SORT_MODES; i++) {
			sortedby.new_component<attraction_item_t>(i);
		}
		sortedby.set_selection(curiositylist_stats_t::sort_mode);
		sortedby.set_width_fixed(true);
		sortedby.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
		sortedby.add_listener(this);
		add_component(&sortedby);

		add_table(3,1);
		{
			// sort asc/desc switching button
			sort_order.init(button_t::sortarrow_state, "");
			sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
			sort_order.add_listener(this);
			sort_order.pressed = curiositylist_stats_t::sortreverse;
			add_component(&sort_order);

			new_component<gui_margin_t>(LINESPACE);

			filter_within_network.init(button_t::square_state, "Within own network");
			filter_within_network.set_tooltip("Show only connected to own passenger transportation network");
			filter_within_network.add_listener(this);
			filter_within_network.pressed = curiositylist_stats_t::filter_own_network;
			add_component(&filter_within_network);
		}
		end_table();
	}
	end_table();

	set_cityfilter(city);

	set_alignment(ALIGN_STRETCH_V | ALIGN_STRETCH_H);
	add_component(&scrolly);
	fill_list();

	set_resizemode(diagonal_resize);
	scrolly.set_maximize(true);
	reset_min_windowsize();
}


void curiositylist_frame_t::fill_list()
{
	scrolly.clear_elements();
	const weighted_vector_tpl<gebaeude_t*>& world_attractions = welt->get_attractions();
	attraction_count = world_attractions.get_count();

	FOR(const weighted_vector_tpl<gebaeude_t*>, const geb, world_attractions) {
		if (curiositylist_stats_t::region_filter && (curiositylist_stats_t::region_filter - 1) != welt->get_region(geb->get_pos().get_2d())) {
			continue;
		}
		if (filter_city!=NULL && filter_city!=geb->get_stadt()) {
			continue;
		}
		if (last_name_filter[0] != 0 && !utf8caseutf8(geb->get_tile()->get_desc()->get_name(), name_filter)) {
			continue;
		}
		if (geb != NULL &&
			geb->get_first_tile() == geb &&
			geb->get_adjusted_visitor_demand() != 0) {

			if (!curiositylist_stats_t::filter_own_network ||
				(curiositylist_stats_t::filter_own_network && geb->is_within_players_network(welt->get_active_player(), goods_manager_t::INDEX_PAS))) {
				scrolly.new_component<curiositylist_stats_t>(geb);
			}
		}
	}
	scrolly.sort(0);
	scrolly.set_size(scrolly.get_size());
}

void curiositylist_frame_t::set_cityfilter(stadt_t *city)
{
	filter_city = city;
	region_selector.set_visible(filter_city==NULL && !welt->get_settings().regions.empty());
	bt_cansel_cityfilter.set_visible(filter_city!=NULL);
	lb_target_city.set_visible(filter_city!=NULL);
	if (city) {
		filter_within_network.pressed = false;
		curiositylist_stats_t::filter_own_network = false;
		curiositylist_stats_t::region_filter=0;
		region_selector.set_selection(0);
		lb_target_city.buf().printf("%s>%s", translator::translate("City"), city->get_name());
		lb_target_city.update();
	}
	resize(scr_size(0,0));
	fill_list();
}

/**
 * This method is called if an action is triggered
 */
bool curiositylist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if(comp == &sortedby) {
		curiositylist_stats_t::sort_mode = max(0, v.i);
		scrolly.sort(0);
	}
	else if (comp == &region_selector) {
		curiositylist_stats_t::region_filter = max(0, v.i);
		fill_list();
	}
	else if (comp == &sort_order) {
		curiositylist_stats_t::sortreverse = !curiositylist_stats_t::sortreverse;
		scrolly.sort(0);
		sort_order.pressed = curiositylist_stats_t::sortreverse;
	}
	else if (comp == &filter_within_network) {
		curiositylist_stats_t::filter_own_network = !curiositylist_stats_t::filter_own_network;
		filter_within_network.pressed = curiositylist_stats_t::filter_own_network;
		fill_list();
	}
	else if (comp == &bt_cansel_cityfilter) {
		set_cityfilter(NULL);
	}
	return true;
}


void curiositylist_frame_t::draw(scr_coord pos, scr_size size)
{
	if(  world()->get_attractions().get_count() != attraction_count  ||  strcmp(last_name_filter, name_filter)  ) {
		strcpy(last_name_filter, name_filter);
		fill_list();
	}

	gui_frame_t::draw(pos, size);
}


void curiositylist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();
	uint32 townindex = UINT32_MAX;

	size.rdwr(file);
	scrolly.rdwr(file);
	file->rdwr_str(name_filter, lengthof(name_filter));
	file->rdwr_byte(curiositylist_stats_t::sort_mode);
	file->rdwr_bool(curiositylist_stats_t::sortreverse);
	file->rdwr_byte(curiositylist_stats_t::region_filter);
	file->rdwr_bool(curiositylist_stats_t::filter_own_network);
	if (file->is_saving()) {
		if (filter_city != NULL) {
			townindex = welt->get_cities().index_of(filter_city);
		}
		file->rdwr_long(townindex);
	}
	if (file->is_loading()) {
		if (file->is_version_ex_atleast(14, 50)) {
			file->rdwr_long(townindex);
			if (townindex != UINT32_MAX) {
				filter_city = welt->get_cities()[townindex];
			}
		}
		sortedby.set_selection(curiositylist_stats_t::sort_mode);
		region_selector.set_selection(curiositylist_stats_t::region_filter);
		sort_order.pressed = curiositylist_stats_t::sortreverse;
		filter_within_network.pressed = curiositylist_stats_t::filter_own_network;
		if (filter_city!=NULL) {
			set_cityfilter(filter_city);
		}
		fill_list();
		set_windowsize(size);
	}
}
