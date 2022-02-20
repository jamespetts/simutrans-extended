/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <algorithm>
#include <string.h>

#include "halt_list_frame.h"
#include "halt_list_filter_frame.h"

#include "../player/simplay.h"
#include "../simhalt.h"
#include "../simware.h"
#include "../simfab.h"
#include "../unicode.h"
#include "simwin.h"
#include "../descriptor/skin_desc.h"

#include "../bauer/goods_manager.h"

#include "../dataobj/translator.h"

#include "../utils/cbuffer_t.h"


static bool passes_filter(haltestelle_t & s); // see below
static uint8 default_sortmode = 0;
uint8 halt_list_frame_t::display_mode = 0;

/**
 * Scrolled list of halt_list_stats_ts.
 * Filters (by setting visibility) and sorts.
 */
class gui_scrolled_halt_list_t : public gui_scrolled_list_t
{
	uint8 mode = halt_list_frame_t::display_mode;
public:
	gui_scrolled_halt_list_t() :  gui_scrolled_list_t(gui_scrolled_list_t::windowskin, compare) {}

	void sort()
	{
		// set visibility according to filter
		for(  vector_tpl<gui_component_t*>::iterator iter = item_list.begin();  iter != item_list.end();  ++iter) {
			halt_list_stats_t *a = dynamic_cast<halt_list_stats_t*>(*iter);

			a->set_visible( passes_filter(*a->get_halt()) );
			a->set_mode(mode);
		}

		gui_scrolled_list_t::sort(0);
	}

	void set_mode(uint8 m)
	{
		mode = m;
	}

	static bool compare(const gui_component_t *aa, const gui_component_t *bb)
	{
		const halt_list_stats_t *a = dynamic_cast<const halt_list_stats_t*>(aa);
		const halt_list_stats_t *b = dynamic_cast<const halt_list_stats_t*>(bb);

		return halt_list_frame_t::compare_halts(a->get_halt(), b->get_halt());
	}
};


/**
 * All filter and sort settings are static, so the old settings are
 * used when the window is reopened.
 */

/**
 * This variable defines by which column the table is sorted
 */
halt_list_frame_t::sort_mode_t halt_list_frame_t::sortby = nach_name;

/**
 * This variable defines the sort order (ascending or descending)
 * Values: 1 = ascending, 2 = descending)
 */
bool halt_list_frame_t::sortreverse = false;

/**
 * Default filter: no Oil rigs!
 */
uint32 halt_list_frame_t::filter_flags = 0;

char halt_list_frame_t::name_filter_value[64] = "";

slist_tpl<const goods_desc_t *> halt_list_frame_t::waren_filter_ab;
slist_tpl<const goods_desc_t *> halt_list_frame_t::waren_filter_an;

const char *halt_list_frame_t::sort_text[SORT_MODES] = {
	"hl_btn_sort_name",
	"by_waiting_passengers",
	"by_waiting_mails",
	"by_waiting_goods",
	"hl_btn_sort_type",
	"hl_sort_tiles",
	"hl_sort_capacity",
	"hl_sort_overcrowding_rate",
	"by_potential_pax_number",
	"by_potential_mail_users",
	"by_pax_happy_last_month",
	"pax_handled_last_month",
	"by_mail_delivered_last_month",
	"mail_handled_last_month",
	"goods_handled_last_month",
	"by_convoy_arrivals_last_month",
	"by_region",
	"by_surrounding_population",
	"by_surrounding_mail_demand",
	"by_surrounding_visitor_demand",
	"by_surrounding_jobs"
};


const char *halt_list_frame_t::display_mode_text[halt_list_stats_t::HALTLIST_MODES] = {
	"hl_waiting_detail",
	"hl_facility",
	"Serve lines",
	"hl_location",
	"hl_waiting_pax",
	"hl_waiting_mail",
	"hl_waiting_goods",
	"hl_pax_evaluation",
	"hl_mail_evaluation",
	"pax_handled_last_month",
	"mail_handled_last_month",
	"hl_goods_needed",
	"hl_products",
	"coverage_population",
	"coverage_mail_demands",
	"coverage_visitor_demands",
	"coverage_jobs"
};


/**
* This function compares two stations
*
* @return halt1 < halt2
*/
bool halt_list_frame_t::compare_halts(halthandle_t const halt1, halthandle_t const halt2)
{
	int order;

	/***********************************
	* Compare station 1 and station 2
	***********************************/
	switch (sortby) {
		default:
		case nach_name: // sort by station name
			order = 0;
			break;
		case by_waiting_pax:
		{
			// Distinguish between 0 and "disable"
			const int a = halt1->get_pax_enabled() ? halt1->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) : -1;
			const int b = halt2->get_pax_enabled() ? halt2->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) : -1;
			order = (int)(a - b);
			break;
		}
		case by_waiting_mail:
		{
			// Distinguish between 0 and "disable"
			const int a = halt1->get_mail_enabled() ? halt1->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL)) : -1;
			const int b = halt2->get_mail_enabled() ? halt2->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL)) : -1;
			order = (int)(a - b);
			break;
		}
		case by_waiting_goods:
		{
			const int waiting_goods_a = halt1->get_ware_enabled() ? (int)(halt1->get_finance_history(0, HALT_WAITING) - halt1->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) - halt1->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL))): -1;
			const int waiting_goods_b = halt2->get_ware_enabled() ? (int)(halt2->get_finance_history(0, HALT_WAITING) - halt2->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) - halt2->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL))): -1;
			order = waiting_goods_a - waiting_goods_b;
			break;
		}
		case nach_typ: // sort by station type
			order = halt1->get_station_type() - halt2->get_station_type();
			break;
		case by_tiles:
			order = halt1->get_tiles().get_count() - halt2->get_tiles().get_count();
			if (order == 0) {
				order = halt1->calc_maintenance()-halt2->calc_maintenance();
			}
			break;
		case by_capacity:
		{
			const uint64 a = halt1->get_capacity(0) + halt1->get_capacity(1) + halt1->get_capacity(2);
			const uint64 b = halt2->get_capacity(0) + halt2->get_capacity(1) + halt2->get_capacity(2);
			order = (int)(a - b);
			break;
		}
		case by_overcrowding_rate:
		{
			uint8 weighting_a = halt1->get_pax_enabled() + halt1->get_mail_enabled() + halt1->get_ware_enabled();
			uint8 weighting_b = halt2->get_pax_enabled() + halt2->get_mail_enabled() + halt2->get_ware_enabled();
			if (!weighting_a) { weighting_a=1; }
			if (!weighting_b) { weighting_b=1; }
			const sint64 crowding_factor_a = halt1->get_overcrowded_proporion(0) + halt1->get_overcrowded_proporion(1) + halt1->get_overcrowded_proporion(2);
			const sint64 crowding_factor_b = halt2->get_overcrowded_proporion(0) + halt2->get_overcrowded_proporion(1) + halt2->get_overcrowded_proporion(2);
			order = crowding_factor_a/weighting_a - crowding_factor_b/weighting_b;
			break;
		}
		case by_potential_pax:
			order = halt1->get_potential_passenger_number(1) - halt2->get_potential_passenger_number(1);
			break;
		case by_potential_mail:
			order = (int)(halt1->get_finance_history(1, HALT_MAIL_DELIVERED) + halt1->get_finance_history(1, HALT_MAIL_NOROUTE) - halt2->get_finance_history(1, HALT_MAIL_DELIVERED) - halt2->get_finance_history(1, HALT_MAIL_NOROUTE));
			break;
		case by_pax_happy_last_month:
		{
			const int happy_a = halt1->get_pax_enabled() ? halt1->get_finance_history(1, HALT_HAPPY) : -1;
			const int happy_b = halt2->get_pax_enabled() ? halt2->get_finance_history(1, HALT_HAPPY) : -1;
			order = (int)(happy_a - happy_b);
			break;
		}
		case by_mail_delivered_last_month:
		{
			const int delivered_a = halt1->get_mail_enabled() ? halt1->get_finance_history(1, HALT_MAIL_DELIVERED) : -1;
			const int delivered_b = halt2->get_mail_enabled() ? halt2->get_finance_history(1, HALT_MAIL_DELIVERED) : -1;
			order = (int)(delivered_a - delivered_b);
			break;
		}
		case by_pax_handled_last_month:
		{
			const int hist_a = halt1->get_pax_enabled() ? halt1->get_finance_history(1, HALT_VISITORS)+halt2->get_finance_history(1, HALT_COMMUTERS) : -1;
			const int hist_b = halt2->get_pax_enabled() ? halt2->get_finance_history(1, HALT_VISITORS)+halt2->get_finance_history(1, HALT_COMMUTERS) : -1;
			order = (int)(hist_a - hist_b);
			break;
		}
		case by_mail_handled_last_month:
		{
			const int hist_a = halt1->get_mail_enabled() ? halt1->get_finance_history(1, HALT_MAIL_HANDLING_VOLUME) : -1;
			const int hist_b = halt2->get_mail_enabled() ? halt2->get_finance_history(1, HALT_MAIL_HANDLING_VOLUME) : -1;
			order = (int)(hist_a - hist_b);
			break;
		}
		case by_goods_handled_last_month:
		{
			const int hist_a = halt1->get_ware_enabled() ? halt1->get_finance_history(1, HALT_GOODS_HANDLING_VOLUME) : -1;
			const int hist_b = halt2->get_ware_enabled() ? halt2->get_finance_history(1, HALT_GOODS_HANDLING_VOLUME) : -1;
			order = (int)(hist_a - hist_b);
			break;
		}
		case by_convoy_arrivals_last_month:
			order = (int)(halt1->get_finance_history(1, HALT_CONVOIS_ARRIVED) - halt2->get_finance_history(1, HALT_CONVOIS_ARRIVED));
			break;
		case by_region:
			order = welt->get_region(halt1->get_basis_pos()) - welt->get_region(halt2->get_basis_pos());
			if (order == 0) {
				const koord a = world()->get_city(halt1->get_basis_pos()) ? world()->get_city(halt1->get_basis_pos())->get_pos() : koord(0,0);
				const koord b = world()->get_city(halt2->get_basis_pos()) ? world()->get_city(halt2->get_basis_pos())->get_pos() : koord(0,0);
				order = a.x - b.x;
				if (order == 0) {
					order = a.y - b.y;
				}
			}
			break;

		case by_surrounding_population:
		{
			const int a = halt1->get_pax_enabled() ? halt1->get_around_population() : -1;
			const int b = halt2->get_pax_enabled() ? halt2->get_around_population() : -1;
			order = (int)(a - b);
			break;
		}
		case by_surrounding_mail_demand:
		{
			const int a = halt1->get_mail_enabled() ? halt1->get_around_mail_demand() : -1;
			const int b = halt2->get_mail_enabled() ? halt2->get_around_mail_demand() : -1;
			order = (int)(a - b);
			break;
		}
		case by_surrounding_visitor_demand:
		{
			const int a = halt1->get_pax_enabled() ? halt1->get_around_visitor_demand() : -1;
			const int b = halt2->get_pax_enabled() ? halt2->get_around_visitor_demand() : -1;
			order = (int)(a - b);
			break;
		}
		case by_surrounding_jobs:
		{
			const int a = halt1->get_pax_enabled() ? halt1->get_around_job_demand() : -1;
			const int b = halt2->get_pax_enabled() ? halt2->get_around_job_demand() : -1;
			order = (int)(a - b);
			break;
		}

	}
	/**
	 * use name as an additional sort, to make sort more stable.
	 */
	if(order == 0) {
		order = strcmp(halt1->get_name(), halt2->get_name());
	}
	/***********************************
	 * Consider sorting order
	 ***********************************/
	return sortreverse ? order > 0 : order < 0;
}


static bool passes_filter_name(haltestelle_t const& s)
{
	if (!halt_list_frame_t::get_filter(halt_list_frame_t::name_filter)) return true;

	return utf8caseutf8(s.get_name(), halt_list_frame_t::access_name_filter());
}


static bool passes_filter_type(haltestelle_t const& s)
{
	if (!halt_list_frame_t::get_filter(halt_list_frame_t::typ_filter)) return true;

	haltestelle_t::stationtyp const t = s.get_station_type();
	if (halt_list_frame_t::get_filter(halt_list_frame_t::frachthof_filter)       && t & haltestelle_t::loadingbay)      return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::bahnhof_filter)         && t & haltestelle_t::railstation)     return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::bushalt_filter)         && t & haltestelle_t::busstop)         return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::airport_filter)         && t & haltestelle_t::airstop)         return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::dock_filter)            && t & haltestelle_t::dock)            return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::monorailstop_filter)    && t & haltestelle_t::monorailstop)    return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::maglevstop_filter)      && t & haltestelle_t::maglevstop)      return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::narrowgaugestop_filter) && t & haltestelle_t::narrowgaugestop) return true;
	if (halt_list_frame_t::get_filter(halt_list_frame_t::tramstop_filter)        && t & haltestelle_t::tramstop)        return true;
	return false;
}

static bool passes_filter_special(haltestelle_t & s)
{
	if (!halt_list_frame_t::get_filter(halt_list_frame_t::spezial_filter)) return true;

	if (halt_list_frame_t::get_filter(halt_list_frame_t::ueberfuellt_filter)) {
		PIXVAL const farbe = s.get_status_farbe();
		if (farbe == color_idx_to_rgb(COL_RED) || farbe == color_idx_to_rgb(COL_ORANGE)) {
			return true; // overcrowded
		}
	}

	if(halt_list_frame_t::get_filter(halt_list_frame_t::ohneverb_filter))
	{
		bool walking_connexion_only = true; // Walking connexion or no connexion at all.
		for(uint8 i = 0; i < goods_manager_t::get_max_catg_index(); ++i)
		{
			// TODO: Add UI to show different connexions for multiple classes
			uint8 g_class = 0;
			if(i == goods_manager_t::INDEX_PAS)
			{
				g_class = goods_manager_t::passengers->get_number_of_classes() - 1;
			}
			else if(i == goods_manager_t::INDEX_MAIL)
			{
				g_class = goods_manager_t::mail->get_number_of_classes() - 1;
			}

			if(!s.get_connexions(i, g_class)->empty())
			{
				// There might be a walking connexion here - do not count a walking connexion.
				for(auto &c : *s.get_connexions(i, g_class) )
				{
					if(c.value->best_line.is_bound() || c.value->best_convoy.is_bound())
					{
						walking_connexion_only = false;
					}
				}
			}
		}
		return walking_connexion_only; //only display stations with NO connexion (other than a walking connexion)
	}

	return false;
}


static bool passes_filter_out(haltestelle_t const& s)
{
	if (!halt_list_frame_t::get_filter(halt_list_frame_t::ware_ab_filter)) return true;

	/*
	 * Die Unterkriterien werden gebildet aus:
	 * - die Ware wird produziert (pax/post_enabled bzw. factory vorhanden)
	 * - es existiert eine Zugverbindung mit dieser Ware (!ziele[...].empty())
	 */

	for (uint32 i = 0; i != goods_manager_t::get_count(); ++i) {
		goods_desc_t const* const ware = goods_manager_t::get_info(i);
		if (!halt_list_frame_t::get_ware_filter_ab(ware)) continue;

		if (ware == goods_manager_t::passengers) {
			if (s.get_pax_enabled()) return true;
		} else if (ware == goods_manager_t::mail) {
			if (s.get_mail_enabled()) return true;
		} else if (ware != goods_manager_t::none) {
			// Sigh - a doubly nested loop per halt
			// Fortunately the number of factories and their number of outputs
			// is limited (usually 1-2 factories and 0-1 outputs per factory)
			FOR(slist_tpl<fabrik_t*>, const f, s.get_fab_list()) {
				FOR(array_tpl<ware_production_t>, const& j, f->get_output()) {
					if (j.get_typ() == ware) return true;
				}
			}
		}
	}

	return false;
}


static bool passes_filter_in(haltestelle_t const& s)
{
	if (!halt_list_frame_t::get_filter(halt_list_frame_t::ware_an_filter)) return true;
	/*
	 * Die Unterkriterien werden gebildet aus:
	 * - die Ware wird verbraucht (pax/post_enabled bzw. factory vorhanden)
	 * - es existiert eine Zugverbindung mit dieser Ware (!ziele[...].empty())
	 */

	for (uint32 i = 0; i != goods_manager_t::get_count(); ++i) {
		goods_desc_t const* const ware = goods_manager_t::get_info(i);
		if (!halt_list_frame_t::get_ware_filter_an(ware)) continue;

		if (ware == goods_manager_t::passengers) {
			if (s.get_pax_enabled()) return true;
		} else if (ware == goods_manager_t::mail) {
			if (s.get_mail_enabled()) return true;
		}
		else if (ware != goods_manager_t::none) {
			// Sigh - a doubly nested loop per halt
			// Fortunately the number of factories and their number of outputs
			// is limited (usually 1-2 factories and 0-1 outputs per factory)
			FOR(slist_tpl<fabrik_t*>, const f, s.get_fab_list()) {
				FOR(array_tpl<ware_production_t>, const& j, f->get_input()) {
					if (j.get_typ() == ware) return true;
				}
			}
		}
	}

	return false;
}


/**
 * Check all filters for one halt.
 * returns true, if it is not filtered away.
 */
static bool passes_filter(haltestelle_t & s)
{
	if (halt_list_frame_t::get_filter(halt_list_frame_t::any_filter)) {
		if (!passes_filter_name(s))    return false;
		if (!passes_filter_type(s))    return false;
		if (!passes_filter_special(s)) return false;
		if (!passes_filter_out(s))     return false;
		if (!passes_filter_in(s))      return false;
	}
	return true;
}


halt_list_frame_t::halt_list_frame_t(stadt_t *city) :
	gui_frame_t( translator::translate("hl_title"), city ? welt->get_public_player() : welt->get_active_player()),
	filter_city(city)
{
	m_player = filter_city ? welt->get_public_player() : welt->get_active_player();
	filter_frame = NULL;

	set_table_layout(1,0);

	add_table(3,2);
	{
		new_component<gui_label_t>("hl_txt_sort");

		filter_on.init(button_t::square_state, "cl_txt_filter");
		filter_on.set_tooltip(translator::translate("cl_btn_filter_tooltip"));
		filter_on.pressed = get_filter(any_filter);
		filter_on.add_listener(this);
		add_component(&filter_on);

		add_table(3, 1);
		{
			btn_show_mutual_use.init(button_t::square_state, "show_mutual_use");
			btn_show_mutual_use.set_tooltip(translator::translate("Also shows the stops of other players you are using"));
			btn_show_mutual_use.pressed = show_mutual_stops;
			btn_show_mutual_use.set_rigid(false);
			btn_show_mutual_use.add_listener(this);
			add_component(&btn_show_mutual_use);

			lb_target_city.set_visible(false);
			lb_target_city.set_rigid(false);
			lb_target_city.set_color(SYSCOL_TEXT_HIGHLIGHT);
			add_component(&lb_target_city);

			bt_cancel_cityfilter.init(button_t::roundbox, "reset");
			bt_cancel_cityfilter.add_listener(this);
			bt_cancel_cityfilter.set_visible(false);
			bt_cancel_cityfilter.set_rigid(false);
			add_component(&bt_cancel_cityfilter);
		}
		end_table();

		add_table(3,1);
		{
			for (uint8 i = 0; i < SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(default_sortmode);
			sortedby.set_width_fixed(true);
			sortedby.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
			sortedby.add_listener(this);
			add_component(&sortedby);

			// sort asc/desc switching button
			sort_order.init(button_t::sortarrow_state, "");
			sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
			sort_order.add_listener(this);
			sort_order.pressed = sortreverse;
			add_component(&sort_order);

			new_component<gui_margin_t>(10);
		}
		end_table();

		filter_details.init(button_t::roundbox, "hl_btn_filter_settings");
		if (skinverwaltung_t::open_window) {
			filter_details.set_image(skinverwaltung_t::open_window->get_image_id(0));
			filter_details.set_image_position_right(true);
		}
		filter_details.set_size(D_BUTTON_SIZE);
		filter_details.add_listener(this);
		add_component(&filter_details);

		for (uint8 i = 0; i < halt_list_stats_t::HALTLIST_MODES; i++  ){
			cb_display_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(display_mode_text[i]), SYSCOL_TEXT);
		}
		cb_display_mode.set_selection(display_mode);
		cb_display_mode.set_width_fixed(true);
		cb_display_mode.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
		cb_display_mode.add_listener(this);
		add_component(&cb_display_mode);
	}
	end_table();

	scrolly = new_component<gui_scrolled_halt_list_t>();
	scrolly->set_maximize( true );

	if( filter_city ) {
		set_cityfilter( filter_city );
	}
	else {
		fill_list();
	}

	set_resizemode(diagonal_resize);
	reset_min_windowsize();
}


halt_list_frame_t::~halt_list_frame_t()
{
	if(filter_frame) {
		destroy_win(filter_frame);
	}
}


/**
* This function refreshes the station-list
*/
void halt_list_frame_t::fill_list()
{
	last_world_stops = haltestelle_t::get_alle_haltestellen().get_count(); // count of stations
	if (halt_list_stats_t::name_width==0) {
		// set name width
		FOR(vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen()) {
			const scr_coord_val temp_w = proportional_string_width(halt->get_name());
			if (temp_w > halt_list_stats_t::name_width) {
				halt_list_stats_t::name_width = temp_w;
			}
		}
	}

	scrolly->clear_elements();
	FOR(vector_tpl<halthandle_t>, const halt, haltestelle_t::get_alle_haltestellen()) {
		if (filter_city != NULL){
			if (filter_city != world()->get_city(halt->get_basis_pos())) {
				continue;
			}
			scrolly->new_component<halt_list_stats_t>(halt, (uint8)-1);
		}
		else if(  halt->get_owner() == m_player || (show_mutual_stops && halt->has_available_network(m_player))  ) {
			scrolly->new_component<halt_list_stats_t>(halt, show_mutual_stops ? m_player->get_player_nr() : (uint8)-1);
		}
	}
	sort_list();
}


void halt_list_frame_t::sort_list()
{
	scrolly->sort();
}


void halt_list_frame_t::set_cityfilter(stadt_t *city)
{
	filter_city = city;
	btn_show_mutual_use.set_visible(filter_city==NULL);
	bt_cancel_cityfilter.set_visible(filter_city!=NULL);
	lb_target_city.set_visible(filter_city!=NULL);
	if (city) {
		btn_show_mutual_use.pressed = false;
		show_mutual_stops = false;
		lb_target_city.buf().printf("%s>%s", translator::translate("City"), city->get_name());
		lb_target_city.update();
	}
	resize(scr_size(0, 0));
	fill_list();
}


bool halt_list_frame_t::infowin_event(const event_t *ev)
{
	if(ev->ev_class == INFOWIN  &&  ev->ev_code == WIN_CLOSE) {
		if(filter_frame) {
			filter_frame->infowin_event(ev);
		}
	}
	return gui_frame_t::infowin_event(ev);
}


/**
 * This method is called if an action is triggered
 */
bool halt_list_frame_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	if (comp == &filter_on) {
		set_filter(any_filter, !get_filter(any_filter));
		filter_on.pressed = get_filter(any_filter);
		sort_list();
	}
	else if (comp == &sortedby) {
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			sortby = (halt_list_frame_t::sort_mode_t)tmp;
		}
		else {
			sortedby.set_selection(0);
			sortby = halt_list_frame_t::nach_name;
		}
		default_sortmode = (uint8)tmp;
		sort_list();
	}
	else if (comp == &sort_order) {
		set_reverse(!get_reverse());
		sort_list();
		sort_order.pressed = sortreverse;
	}
	else if (comp == &cb_display_mode) {
		int tmp = cb_display_mode.get_selection();
		if (tmp < 0 || tmp >= cb_display_mode.count_elements())
		{
			tmp = 0;
		}
		cb_display_mode.set_selection(tmp);
		scrolly->set_mode(tmp);
		display_mode = tmp;
		sort_list();
	}
	else if (comp == &filter_details) {
		if (filter_frame) {
			destroy_win(filter_frame);
		}
		else {
			filter_frame = new halt_list_filter_frame_t(m_player, this);
			create_win(filter_frame, w_info, (ptrdiff_t)this);
		}
	}
	else if (comp == &btn_show_mutual_use) {
		show_mutual_stops = !show_mutual_stops;
		fill_list();
		btn_show_mutual_use.pressed = show_mutual_stops;
	}
	else if (comp == &bt_cancel_cityfilter) {
		set_cityfilter(NULL);
	}
	return true;
}


void halt_list_frame_t::draw(scr_coord pos, scr_size size)
{
	filter_details.pressed = filter_frame != NULL;

	if(  last_world_stops != haltestelle_t::get_alle_haltestellen().get_count()  ) {
		// some deleted/ added => resort
		fill_list();
	}

	gui_frame_t::draw(pos, size);
}


void halt_list_frame_t::set_ware_filter_ab(const goods_desc_t *ware, int mode)
{
	if(ware != goods_manager_t::none) {
		if(get_ware_filter_ab(ware)) {
			if(mode != 1) {
				waren_filter_ab.remove(ware);
			}
		}
		else {
			if(mode != 0) {
				waren_filter_ab.append(ware);
			}
		}
	}
}


void halt_list_frame_t::set_ware_filter_an(const goods_desc_t *ware, int mode)
{
	if(ware != goods_manager_t::none) {
		if(get_ware_filter_an(ware)) {
			if(mode != 1) {
				waren_filter_an.remove(ware);
			}
		}
		else {
			if(mode != 0) {
				waren_filter_an.append(ware);
			}
		}
	}
}


void halt_list_frame_t::set_alle_ware_filter_ab(int mode)
{
	if(mode == 0) {
		waren_filter_ab.clear();
	}
	else {
		for(unsigned int i = 0; i<goods_manager_t::get_count(); i++) {
			set_ware_filter_ab(goods_manager_t::get_info(i), mode);
		}
	}
}


void halt_list_frame_t::set_alle_ware_filter_an(int mode)
{
	if(mode == 0) {
		waren_filter_an.clear();
	}
	else {
		for(unsigned int i = 0; i<goods_manager_t::get_count(); i++) {
			set_ware_filter_an(goods_manager_t::get_info(i), mode);
		}
	}
}


void halt_list_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();
	uint8 player_nr = m_player->get_player_nr();
	uint8 sort_mode = default_sortmode;
	uint32 townindex=UINT32_MAX;

	file->rdwr_byte(player_nr);
	size.rdwr(file);
	//tabs.rdwr(file); // no tabs in extended
	scrolly->rdwr(file);
	file->rdwr_str(name_filter_value, lengthof(name_filter_value));
	file->rdwr_byte(sort_mode);
	file->rdwr_bool(sortreverse);
	file->rdwr_long(filter_flags);
	if (file->is_saving()) {
		uint8 good_nr = waren_filter_ab.get_count();
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			FOR(slist_tpl<const goods_desc_t*>, const i, waren_filter_ab) {
				char* name = const_cast<char*>(i->get_name());
				file->rdwr_str(name, 256);
			}
		}
		good_nr = waren_filter_an.get_count();
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			FOR(slist_tpl<const goods_desc_t*>, const i, waren_filter_an) {
				char* name = const_cast<char*>(i->get_name());
				file->rdwr_str(name, 256);
			}
		}
		file->rdwr_byte(display_mode);
		file->rdwr_bool(show_mutual_stops);
		if (filter_city != NULL) {
			townindex = welt->get_cities().index_of(filter_city);
		}
		file->rdwr_long(townindex);
	}
	else {
		// restore warenfilter
		uint8 good_nr;
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			waren_filter_ab.clear();
			for (sint16 i = 0; i < good_nr; i++) {
				char name[256];
				file->rdwr_str(name, lengthof(name));
				if (const goods_desc_t* gd = goods_manager_t::get_info(name)) {
					waren_filter_ab.append(gd);
				}
			}
		}
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			waren_filter_an.clear();
			for (sint16 i = 0; i < good_nr; i++) {
				char name[256];
				file->rdwr_str(name, lengthof(name));
				if (const goods_desc_t* gd = goods_manager_t::get_info(name)) {
					waren_filter_an.append(gd);
				}
			}
		}
		if (file->is_version_ex_atleast(14,50)) {
			file->rdwr_byte(display_mode);
			file->rdwr_bool(show_mutual_stops);
			file->rdwr_long(townindex);
			if (townindex != UINT32_MAX) {
				filter_city = welt->get_cities()[townindex];
			}
		}

		default_sortmode = (sort_mode_t)sort_mode;
		sortedby.set_selection(default_sortmode);
		m_player = filter_city ? welt->get_public_player() : welt->get_player(player_nr);
		set_owner(m_player);
		win_set_magic(this, magic_halt_list + player_nr);
		filter_on.pressed = get_filter(any_filter);
		sort_order.pressed = sortreverse;
		btn_show_mutual_use.pressed = show_mutual_stops;
		cb_display_mode.set_selection(display_mode);
		scrolly->set_mode(display_mode);
		if (filter_city!=NULL) {
			set_cityfilter(filter_city);
		}
		fill_list();
		set_windowsize(size);
	}
}
