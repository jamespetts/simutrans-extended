/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "depotlist_frame.h"
#include "gui_theme.h"
#include "minimap.h"

#include "../player/simplay.h"

#include "../simcity.h"
#include "../simskin.h"
#include "../simworld.h"

#include "../dataobj/translator.h"

#include "../descriptor/skin_desc.h"
#include "../boden/wege/maglev.h"
#include "../boden/wege/monorail.h"
#include "../boden/wege/narrowgauge.h"
#include "../boden/wege/runway.h"
#include "../boden/wege/schiene.h"
#include "../boden/wege/strasse.h"
#include "../bauer/vehikelbauer.h"

#include "../descriptor/building_desc.h"
#include "../display/viewport.h"

static const char* const dl_header_text[depotlist_row_t::MAX_COLS] =
{
	"", "Name", "convoys stored", "vehicles stored", "koord", "by_region", "City", "Fixed Costs", "Built in"
};

int depotlist_row_t::sort_mode = 0;
bool depotlist_row_t::sortreverse = false;


uint8 depotlist_frame_t::selected_tab = 0;

static karte_ptr_t welt;


depot_type_cell_t::depot_type_cell_t(obj_t::typ typ, bool is_electrified) : values_cell_t((sint64)typ, (sint64)is_electrified, SYSCOL_TEXT, true)
{
	align = centered;
	min_size = scr_size(D_FIXED_SYMBOL_WIDTH + LINESPACE, ((LINESPACE-2)*2/3-1));
	color = minimap_t::get_depot_color(typ);

	set_size(min_size);
}


void depot_type_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	offset += pos;

	display_depot_symbol_rgb(offset.x + draw_offset.x, offset.y + draw_offset.y-1, LINESPACE-2, color, false);

	if (sub_value) {
		display_color_img(skinverwaltung_t::electricity->get_image_id(0), offset.x + draw_offset.x + LINESPACE+2, offset.y + draw_offset.y + (LINESPACE * 2 / 3 - 1)/2 - D_FIXED_SYMBOL_WIDTH/2, 0, false, false);
	}
}

depotlist_row_t::depotlist_row_t(depot_t *dep)
{
	assert(depot!=NULL);
	depot = dep;

	// 1. type
	const waytype_t wt = depot->get_wegtyp();
	const weg_t* w = welt->lookup(depot->get_pos())->get_weg(wt != tram_wt ? wt : track_wt);
	const bool way_electrified = w ? w->is_electrified() : false;
	new_component<depot_type_cell_t>(depot->get_typ(), way_electrified);
	// 2. name
	new_component<text_cell_t>(depot->get_name());
	// 3. convoys
	old_convoys = depot->get_convoy_list().get_count();
	new_component<value_cell_t>((sint64)old_convoys, gui_chart_t::STANDARD, table_cell_item_t::right, old_convoys ? SYSCOL_TEXT : SYSCOL_TEXT_INACTIVE);
	// 4. vehicle count and listed count
	const sint64 listed_vehicles = (sint64)depot->get_vehicles_for_sale().get_count();
	old_vehicles = depot->get_vehicle_list().get_count();
	new_component<values_cell_t>((sint64)old_vehicles+listed_vehicles, listed_vehicles, SYSCOL_TEXT, true);
	// 5. pos
	const koord depot_pos = depot->get_pos().get_2d();
	new_component<coord_cell_t>(depot_pos, table_cell_item_t::centered);
	// 6. region
	new_component<text_cell_t>(welt->get_settings().regions.empty() ? "" : translator::translate(welt->get_region_name(depot_pos).c_str()));
	// 7. city
	stadt_t* c = welt->get_city(depot_pos);
	new_component<coord_cell_t>(c ? c->get_name() : "-", c ? c->get_center(): koord::invalid);
	// 8. Fixed Costs
	sint64 maintenance = depot->get_tile()->get_desc()->get_maintenance();
	if (maintenance == PRICE_MAGIC)
	{
		maintenance = depot->get_tile()->get_desc()->get_level() * welt->get_settings().maint_building;
	}
	new_component<value_cell_t>((double)welt->calc_adjusted_monthly_figure(maintenance), gui_chart_t::MONEY, table_cell_item_t::right);
	// 9. Built date
	new_component<value_cell_t>((sint64)depot->get_purchase_time(), gui_chart_t::DATE, table_cell_item_t::right);

	// Possibility of adding items:
	// supported traction types

	// init cells height
	for (auto& cell : owned_cells) {
		cell->set_height(row_height);
	}

}

void depotlist_row_t::draw(scr_coord offset)
{
	if (old_convoys != depot->get_convoy_list().get_count()) {
		value_cell_t *cell = dynamic_cast<value_cell_t*>(owned_cells[DEPOT_CONVOYS]);
		old_convoys = depot->get_convoy_list().get_count();
		cell->set_value((sint64)old_convoys);
		cell->set_color(old_convoys ? SYSCOL_TEXT : SYSCOL_TEXT_INACTIVE);
	}
	if (old_vehicles != depot->get_vehicle_list().get_count()) {
		values_cell_t* cell = dynamic_cast<values_cell_t*>(owned_cells[DEPOT_VEHICLES]);
		old_vehicles = depot->get_vehicle_list().get_count();
		cell->set_values((sint64)old_vehicles, (sint64)depot->get_vehicles_for_sale().get_count());
	}
	gui_sort_table_row_t::draw(offset);
}

void depotlist_row_t::show_depot()
{
	if (depot->get_owner() == world()->get_active_player() ) {
		depot->show_info();
	}
}


bool depotlist_row_t::infowin_event(const event_t* ev)
{
	bool swallowed = gui_scrolled_list_t::scrollitem_t::infowin_event(ev);

	if (!swallowed && depot) {
		if (IS_RIGHTRELEASE(ev)) {
			for (auto& cell : owned_cells) {
				if (cell->get_type() == table_cell_item_t::cell_coord && cell->getroffen(ev->mouse_pos)) {
					const coord_cell_t* coord_cell = dynamic_cast<const coord_cell_t*>(cell);
					const koord k = coord_cell->get_coord();
					if (k != koord::invalid) {
						world()->get_viewport()->change_world_position(k);
						return true;
					}
					return false;
				}
			}
		}
	}
	return swallowed;
}



bool depotlist_row_t::compare(const gui_component_t* aa, const gui_component_t* bb)
{
	const depotlist_row_t* row_a = dynamic_cast<const depotlist_row_t*>(aa);
	const depotlist_row_t* row_b = dynamic_cast<const depotlist_row_t*>(bb);
	if (row_a == NULL || row_b == NULL) {
		dbg->warning("depotlist_row_t::compare()", "row data error");
		return false;
	}

	const table_cell_item_t* a = row_a->get_element(sort_mode);
	const table_cell_item_t* b = row_b->get_element(sort_mode);
	if (a == NULL || b == NULL) {
		dbg->warning("depotlist_row_t::compare()", "Could not get table_cell_item_t successfully");
		return false;
	}

	int cmp = gui_sort_table_row_t::compare(a, b);
	return sortreverse ? cmp < 0 : cmp > 0; // Do not include 0
}


bool depotlist_frame_t::is_available_wt(waytype_t wt)
{
	switch (wt) {
		case maglev_wt:
			return maglev_t::default_maglev ? true : false;
		case monorail_wt:
			return monorail_t::default_monorail ? true : false;
		case track_wt:
			return schiene_t::default_schiene ? true : false;
		case tram_wt:
			return vehicle_builder_t::get_info(tram_wt).empty() ? false : true;
		case narrowgauge_wt:
			return narrowgauge_t::default_narrowgauge ? true : false;
		case road_wt:
			return strasse_t::default_strasse ? true : false;
		case water_wt:
			return vehicle_builder_t::get_info(water_wt).empty() ? false : true;
		case air_wt:
			return runway_t::default_runway ? true : false;
		default:
			return false;
	}
	return false;
}



depotlist_frame_t::depotlist_frame_t(player_t *player) :
	gui_frame_t( translator::translate("dp_title"), player ),
	scrolly(gui_scrolled_list_t::windowskin, depotlist_row_t::compare),
	scroll_tab(&cont_sortable, true, false)
{
	this->player = player;

	init_table();
}


depotlist_frame_t::depotlist_frame_t() :
	gui_frame_t(translator::translate("dp_title"), NULL),
	scrolly(gui_scrolled_list_t::windowskin, depotlist_row_t::compare),
	scroll_tab(&cont_sortable, true, false)
{
	player = NULL;

	init_table();
}


void depotlist_frame_t::init_table()
{
	last_depot_count = 0;

	scrolly.add_listener(this);
	scrolly.set_show_scroll_x(false);
	scrolly.set_checkered(true);
	scrolly.set_scroll_amount_y(LINESPACE + D_H_SPACE + 2); // default cell height

	scroll_tab.set_maximize(true);

	// init table sort buttons
	table_header.add_listener(this);
	for (uint8 col = 0; col < depotlist_row_t::MAX_COLS; col++) {
		table_header.new_component<sortable_header_cell_t>(dl_header_text[col]);
	}

	cont_sortable.set_margin(NO_SPACING, NO_SPACING);
	cont_sortable.set_spacing(NO_SPACING);
	cont_sortable.set_table_layout(1, 2);
	cont_sortable.set_alignment(ALIGN_TOP); // Without this, the layout will be broken if the height is small.
	cont_sortable.add_component(&table_header);
	cont_sortable.add_component(&scrolly);

	set_table_layout(1,0);

	lb_depot_counter.set_fixed_width(proportional_string_width("8888/8888"));
	add_component(&lb_depot_counter);

	tabs.init_tabs(&scroll_tab);
	tabs.add_listener(this);
	add_component(&tabs);
	set_min_windowsize(scr_size(LINESPACE*24, LINESPACE*12));
	fill_list();

	set_resizemode(diagonal_resize);
}

/**
 * This method is called if an action is triggered
 */
bool depotlist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if (comp == &tabs) {
		fill_list();
	}
	else if (comp == &table_header) {
		depotlist_row_t::sort_mode = v.i;
		table_header.set_selection(depotlist_row_t::sort_mode);
		depotlist_row_t::sortreverse = table_header.is_reversed();
		scrolly.sort(0);
	}
	else if ( comp == &scrolly ) {
		scrolly.get_selection();
		depotlist_row_t* row = (depotlist_row_t*)scrolly.get_element(v.i);
		row->show_depot();
	}

	return true;
}


void depotlist_frame_t::fill_list()
{
	scrolly.clear_elements();
	uint32 p_total = 0;
	for(depot_t* const depot : depot_t::get_depot_list()) {
		if( depot->get_owner() == player ) {
			if(  tabs.get_active_tab_index() == 0  ||  depot->get_waytype() == tabs.get_active_tab_waytype()  ) {
				scrolly.new_component<depotlist_row_t>(depot);
			}
			p_total++;
		}
	}
	lb_depot_counter.buf().printf("%u/%u", scrolly.get_count(), p_total);
	lb_depot_counter.update();
	scrolly.sort(0);
	scrolly.set_size(scr_size(get_windowsize().w, scrolly.get_size().h));

	// recalc stats table width
	scr_coord_val max_widths[depotlist_row_t::MAX_COLS] = {};

	// check column widths
	table_header.set_selection(depotlist_row_t::sort_mode);
	table_header.set_width(D_H_SPACE); // init width
	for (uint c = 0; c < depotlist_row_t::MAX_COLS; c++) {
		// get header widths
		max_widths[c] = table_header.get_min_width(c);
	}
	for (int r = 0; r < scrolly.get_count(); r++) {
		depotlist_row_t* row = (depotlist_row_t*)scrolly.get_element(r);
		for (uint c = 0; c < depotlist_row_t::MAX_COLS; c++) {
			max_widths[c] = max(max_widths[c], row->get_min_width(c));
		}
	}

	// set column widths
	for (uint c = 0; c < depotlist_row_t::MAX_COLS; c++) {
		table_header.set_col(c, max_widths[c]);
	}
	table_header.set_width(table_header.get_size().w + D_SCROLLBAR_WIDTH);

	for (int r = 0; r < scrolly.get_count(); r++) {
		depotlist_row_t* row = (depotlist_row_t*)scrolly.get_element(r);
		row->set_size(scr_size(0, row->get_size().h));
		for (uint c = 0; c < depotlist_row_t::MAX_COLS; c++) {
			row->set_col(c, max_widths[c]);
		}
	}

	cont_sortable.set_size(cont_sortable.get_min_size());

	last_depot_count = depot_t::get_depot_list().get_count();
	resize(scr_coord(0, 0));
}


void depotlist_frame_t::draw(scr_coord pos, scr_size size)
{
	if(  depot_t::get_depot_list().get_count() != last_depot_count  ) {
		fill_list();
	}

	gui_frame_t::draw(pos,size);
}


void depotlist_frame_t::rdwr(loadsave_t *file)
{
	scr_size size = get_windowsize();
	uint8 player_nr = player ? player->get_player_nr() : 0;

	selected_tab = tabs.get_active_tab_index();
	file->rdwr_byte(selected_tab);
	file->rdwr_bool(depotlist_row_t::sortreverse);
	uint8 s = (uint8)depotlist_row_t::sort_mode;
	file->rdwr_byte(s);
	file->rdwr_byte(player_nr);
	size.rdwr(file);

	if (file->is_loading()) {
		player = welt->get_player(player_nr);
		gui_frame_t::set_owner(player);
		win_set_magic(this, magic_depotlist + player_nr);
		depotlist_row_t::sort_mode = (int)s;
		tabs.set_active_tab_index(selected_tab<tabs.get_count() ? selected_tab:0);
		fill_list();
		set_windowsize(size);
	}
}
