/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "depotlist_frame.h"
#include "gui_theme.h"

#include "../player/simplay.h"

#include "../simcity.h"
#include "../simdepot.h"
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

enum sort_mode_t { by_name, by_waytype, by_convoys, by_vehicle, by_coord, by_region, SORT_MODES };

uint8 depotlist_stats_t::sort_mode = by_name;
bool depotlist_stats_t::reverse = false;
uint16 depotlist_stats_t::name_width = D_LABEL_WIDTH;
uint8 depotlist_frame_t::selected_tab = 0;

static karte_ptr_t welt;

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


depotlist_stats_t::depotlist_stats_t(depot_t *d)
{
	this->depot = d;
	// pos button
	set_table_layout(8,1);
	gotopos.set_typ(button_t::posbutton_automatic);
	gotopos.set_targetpos3d(depot->get_pos());
	add_component(&gotopos);
	const waytype_t wt = d->get_wegtyp();
	waytype_symbol.set_image(skinverwaltung_t::get_waytype_skin(wt)->get_image_id(0), true);
	add_component(&waytype_symbol);

	const weg_t *w = welt->lookup(depot->get_pos())->get_weg(wt != tram_wt ? wt : track_wt);
	const bool way_electrified = w ? w->is_electrified() : false;
	if (way_electrified) {
		new_component<gui_image_t>()->set_image(skinverwaltung_t::electricity->get_image_id(0), true);
	}
	else {
		new_component<gui_margin_t>(skinverwaltung_t::electricity->get_image(0)->w);
	}

	lb_name.buf().append( translator::translate(depot->get_name()) );
	const scr_coord_val temp_w = proportional_string_width( translator::translate(depot->get_name()) );
	if (temp_w > name_width) {
		name_width = temp_w;
	}
	lb_name.set_fixed_width(name_width);
	add_component(&lb_name);

	lb_cnv_count.init(SYSCOL_TEXT, gui_label_t::right);
	lb_cnv_count.set_fixed_width(proportional_string_width(translator::translate("%d convois")));
	add_component(&lb_cnv_count);
	lb_vh_count.init(SYSCOL_TEXT, gui_label_t::right);
	lb_vh_count.set_fixed_width(proportional_string_width(translator::translate("%d vehicles")));
	add_component(&lb_vh_count);

	lb_region.buf().printf( " %s ", depot->get_pos().get_2d().get_fullstr() );
	stadt_t* c = welt->get_city(depot->get_pos().get_2d());
	if (c) {
		lb_region.buf().append("- ");
		lb_region.buf().append( c->get_name() );
	}
	if (!welt->get_settings().regions.empty()) {
		if (!c) {
			lb_region.buf().append("-");
		}
		lb_region.buf().printf(" (%s)", translator::translate(welt->get_region_name(depot->get_pos().get_2d()).c_str()));
	}
	lb_region.update();
	add_component(&lb_region);
	update_label();

	new_component<gui_fill_t>();
}


void depotlist_stats_t::update_label()
{
	lb_cnv_count.buf().clear();
	int cnvs = depot->convoi_count();
	if( cnvs == 0 ) {
//		buf.append( translator::translate( "no convois" ) );
	}
	else if( cnvs == 1 ) {
		lb_cnv_count.buf().append( translator::translate( "1 convoi" ) );
	}
	else {
		lb_cnv_count.buf().printf( translator::translate( "%d convois" ), cnvs );
	}
	lb_cnv_count.update();

	int vhls = depot->get_vehicle_list().get_count();
	lb_vh_count.buf().clear();
	if( vhls == 0 ) {
		//buf.append( translator::translate( "Keine Einzelfahrzeuge im Depot" ) );
	}
	else if( vhls == 1 ) {
		lb_vh_count.buf().append( translator::translate( "1 vehicle" ) );
	}
	else {
		lb_vh_count.buf().printf( translator::translate( "%d vehicles" ), vhls );
	}
	lb_vh_count.update();
}


void depotlist_stats_t::set_size(scr_size size)
{
	gui_aligned_container_t::set_size(size);
}


bool depotlist_stats_t::is_valid() const
{
	return depot_t::get_depot_list().is_contained(depot);
}


bool depotlist_stats_t::infowin_event(const event_t * ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	if(  !swallowed  &&  IS_LEFTRELEASE(ev)  ) {
		depot->show_info();
		swallowed = true;
	}
	return swallowed;
}


void depotlist_stats_t::draw(scr_coord pos)
{
	update_label();

	gui_aligned_container_t::draw(pos);
}


bool depotlist_stats_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	const depotlist_stats_t* fa = dynamic_cast<const depotlist_stats_t*>(aa);
	const depotlist_stats_t* fb = dynamic_cast<const depotlist_stats_t*>(bb);
	// good luck with mixed lists
	assert(fa != NULL  &&  fb != NULL);
	depot_t *a=fa->depot, *b=fb->depot;

	int cmp;
	switch( sort_mode ) {
	default:
	case by_coord:
		cmp = 0;
		break;

	case by_region:
		cmp = welt->get_region(a->get_pos().get_2d()) - welt->get_region(b->get_pos().get_2d());
		if (cmp == 0) {
			const koord a_city_koord = welt->get_city(a->get_pos().get_2d()) ? welt->get_city(a->get_pos().get_2d())->get_pos() : koord(0, 0);
			const koord b_city_koord = welt->get_city(b->get_pos().get_2d()) ? welt->get_city(b->get_pos().get_2d())->get_pos() : koord(0, 0);
			cmp = a_city_koord.x - b_city_koord.x;
			if (cmp == 0) {
				cmp = a_city_koord.y - b_city_koord.y;
			}
		}
		break;

	case by_name:
		cmp = strcmp(a->get_name(), b->get_name());
		break;

	case by_waytype:
		cmp = a->get_waytype() - b->get_waytype();
		if (cmp == 0) {
			cmp = strcmp(a->get_name(), b->get_name());
		}
		break;

	case by_convoys:
		cmp = a->convoi_count() - b->convoi_count();
		if( cmp == 0 ) {
			cmp = a->get_vehicle_list().get_count() - b->get_vehicle_list().get_count();
		}
		break;

	case by_vehicle:
		cmp = a->get_vehicle_list().get_count() - b->get_vehicle_list().get_count();
		if( cmp == 0 ) {
			cmp = a->convoi_count() - b->convoi_count();
		}
		break;

	}
	if (cmp == 0) {
		cmp = koord_distance( a->get_pos(), koord( 0, 0 ) ) - koord_distance( b->get_pos(), koord( 0, 0 ) );
		if( cmp == 0 ) {
			cmp = a->get_pos().x - b->get_pos().x;
		}
	}
	return reverse ? cmp > 0 : cmp < 0;
}




static const char *sort_text[SORT_MODES] = {
	"hl_btn_sort_name",
	"waytype",
	"convoys stored",
	"vehicles stored",
	"koord",
	"by_region"
};

depotlist_frame_t::depotlist_frame_t(player_t *player) :
	gui_frame_t( translator::translate("dp_title"), player ),
	scrolly(gui_scrolled_list_t::windowskin, depotlist_stats_t::compare)
{
	this->player = player;
	last_depot_count = 0;

	set_table_layout(1,0);

	add_table(2,1);
	{
		new_component<gui_label_t>("hl_txt_sort");
		add_table(3,1);
		{
			sortedby.clear_elements();
			for (int i = 0; i < SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(depotlist_stats_t::sort_mode);
			sortedby.add_listener(this);
			sortedby.set_width_fixed(true);
			sortedby.set_width(LINESPACE*12);
			add_component(&sortedby);

			// sort asc/desc switching button
			sort_order.init(button_t::sortarrow_state, "");
			sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
			sort_order.add_listener(this);
			sort_order.pressed = depotlist_stats_t::reverse;
			add_component(&sort_order);

			new_component<gui_margin_t>(D_H_SPACE*2);
		}
		end_table();
	}
	end_table();

	tabs.init_tabs(&scrolly);
	tabs.add_listener(this);
	add_component(&tabs);
	fill_list();

	set_resizemode(diagonal_resize);
	scrolly.set_maximize(true);
	reset_min_windowsize();
}


depotlist_frame_t::depotlist_frame_t() :
	gui_frame_t(translator::translate("dp_title"), NULL),
	scrolly(gui_scrolled_list_t::windowskin, depotlist_stats_t::compare)
{
	player = NULL;
	last_depot_count = 0;

	set_table_layout(1, 0);

	scrolly.set_maximize(true);
	tabs.init_tabs(&scrolly);
	tabs.add_listener(this);
	add_component(&tabs);
	fill_list();

	set_resizemode(diagonal_resize);
	reset_min_windowsize();
}

/**
 * This method is called if an action is triggered
 */
bool depotlist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if (comp == &tabs) {
		fill_list();
	}
	else if(comp == &sortedby) {
		depotlist_stats_t::sort_mode = max(0, v.i);
		scrolly.sort(0);
	}
	else if (comp == &sort_order) {
		depotlist_stats_t::reverse = !depotlist_stats_t::reverse;
		scrolly.sort(0);
		sort_order.pressed = depotlist_stats_t::reverse;
	}
	return true;
}


void depotlist_frame_t::fill_list()
{
	scrolly.clear_elements();
	FOR(slist_tpl<depot_t*>, const depot, depot_t::get_depot_list()) {
		if( depot->get_owner() == player ) {
			if(  tabs.get_active_tab_index() == 0  ||  depot->get_waytype() == tabs.get_active_tab_waytype()  ) {
				scrolly.new_component<depotlist_stats_t>(depot);
			}
		}
	}
	scrolly.sort(0);
	scrolly.set_size( scrolly.get_size());

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
	file->rdwr_bool(sort_order.pressed);
	uint8 s = depotlist_stats_t::sort_mode;
	file->rdwr_byte(s);
	file->rdwr_byte(player_nr);
	size.rdwr(file);

	if (file->is_loading()) {
		player = welt->get_player(player_nr);
		gui_frame_t::set_owner(player);
		win_set_magic(this, magic_depotlist + player_nr);
		sortedby.set_selection(s);
		depotlist_stats_t::sort_mode = s;
		depotlist_stats_t::reverse = sort_order.pressed;
		tabs.set_active_tab_index(selected_tab<tabs.get_count() ? selected_tab:0);
		fill_list();
		set_windowsize(size);
	}
}
