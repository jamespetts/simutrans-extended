/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string.h>
#include <algorithm>

#include "components/gui_convoiinfo.h"

#include "convoi_frame.h"
#include "convoi_filter_frame.h"

#include "simwin.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "../simworld.h"
#include "../unicode.h"
#include "../descriptor/goods_desc.h"
#include "../bauer/goods_manager.h"
#include "../dataobj/translator.h"
#include "../player/simplay.h"
#include "../utils/simstring.h"
#include "../vehicle/vehicle.h"
#include "../simline.h"

 /**
 * All filter and sort settings are static, so the old settings are
 * used when the window is reopened.
 */
convoi_frame_t::sort_mode_t convoi_frame_t::sortby = convoi_frame_t::by_name;
static uint8 default_sortmode = 0;
bool convoi_frame_t::sortreverse = false;
static uint8 cl_display_mode = gui_convoy_formation_t::appearance;

const char *convoi_frame_t::sort_text[SORT_MODES] = {
	"cl_btn_sort_name",
	"Line",
	"Home depot",
	"cl_btn_sort_income",
	"cl_btn_sort_type",
	"cl_btn_sort_id",
	"cl_btn_sort_max_speed",
	"cl_btn_sort_power",
	"cl_btn_sort_value",
	"cl_btn_sort_age",
	"cl_btn_sort_range",
	"L/F(passenger)"
};

const slist_tpl<const goods_desc_t*>* convoi_frame_t::waren_filter = NULL;
const uint8 convoi_frame_t::sortmode_to_label[SORT_MODES] = { 0,1,9,2,0,0,4,5,6,7,8,10 };
/**
 * Scrolled list of gui_convoiinfo_ts.
 * Filters (by setting visibility) and sorts.
 */
class gui_scrolled_convoy_list_t : public gui_scrolled_list_t
{
	convoi_frame_t *main;

	static convoi_frame_t *main_static;
public:
	gui_scrolled_convoy_list_t(convoi_frame_t *m) :  gui_scrolled_list_t(gui_scrolled_list_t::windowskin)
	{
		main = m;
		set_cmp(compare);
	}

	void sort()
	{
		// set visibility according to filter
		for(  vector_tpl<gui_component_t*>::iterator iter = item_list.begin();  iter != item_list.end();  ++iter) {
			gui_convoiinfo_t *a = dynamic_cast<gui_convoiinfo_t*>(*iter);

			a->set_visible( main->passes_filter(a->get_cnv()) );
			a->set_mode(cl_display_mode);
			a->set_switchable_label(convoi_frame_t::sortmode_to_label[default_sortmode]);
		}
		main_static = main;
		gui_scrolled_list_t::sort(0);
	}

	static bool compare(const gui_component_t *aa, const gui_component_t *bb)
	{
		const gui_convoiinfo_t *a = dynamic_cast<const gui_convoiinfo_t*>(aa);
		const gui_convoiinfo_t *b = dynamic_cast<const gui_convoiinfo_t*>(bb);

		return main_static->compare_convois(a->get_cnv(), b->get_cnv());
	}
};
convoi_frame_t* gui_scrolled_convoy_list_t::main_static;


bool convoi_frame_t::passes_filter(convoihandle_t cnv)
{
	waytype_t wt = tabs.get_active_tab_waytype();
	if(  wt  &&  cnv->front()->get_desc()->get_waytype() != wt  ) {
		// not the right kind of vehivle
		return false;
	}

	if(  name_filter[0]!=0  &&  !utf8caseutf8(cnv->get_name(), name_filter)  ) {
		// not the right name
		return false;
	}

	if(  get_filter(convoi_filter_frame_t::special_filter)  ) {
		if ((!get_filter(convoi_filter_frame_t::noroute_filter)  || cnv->get_state() != convoi_t::NO_ROUTE) &&
				(!get_filter(convoi_filter_frame_t::stucked_filter)    || (cnv->get_state() != convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS && cnv->get_state() != convoi_t::CAN_START_TWO_MONTHS)) &&
				(!get_filter(convoi_filter_frame_t::indepot_filter)    || !cnv->in_depot()) &&
				(!get_filter(convoi_filter_frame_t::noline_filter)     ||  cnv->get_line().is_bound()) &&
				(!get_filter(convoi_filter_frame_t::noschedule_filter) ||  cnv->get_schedule()) &&
				(!get_filter(convoi_filter_frame_t::noincome_filter)   ||  cnv->get_jahresgewinn() >= 100) &&
				(!get_filter(convoi_filter_frame_t::obsolete_filter)   || !cnv->has_obsolete_vehicles()))
		{
			return false;
		}
	}

	if(  get_filter(convoi_filter_frame_t::ware_filter)  ) {
		unsigned i;
		for(  i = 0; i < cnv->get_vehicle_count(); i++) {
			const goods_desc_t *wb = cnv->get_vehicle(i)->get_cargo_type();
			if(  wb->get_catg()!=0  ) {
				wb = goods_manager_t::get_info_catg(wb->get_catg());
			}
			if(  waren_filter->is_contained(wb)  ) {
				return true;
			}
		}
		if(  i == cnv->get_vehicle_count()  ) {
			return false;
		}
	}
	return true;
}


bool convoi_frame_t::compare_convois(convoihandle_t const cnv1, convoihandle_t const cnv2)
{
	sint32 result = 0;

	switch (sortby) {
		default:
		case by_name:
			result = strcmp(cnv1->get_internal_name(), cnv2->get_internal_name());
			break;
		case by_line:
			result = cnv1->get_line().get_id() - cnv2->get_line().get_id();
			break;
		case by_home_depot:
		{
			char a_name[256] = "\0";
			char b_name[256] = "\0";
			const koord3d a_coord = cnv1->get_home_depot();
			const koord3d b_coord = cnv2->get_home_depot();
			if( a_coord !=koord3d::invalid ) {
				if( grund_t* gr = welt->lookup(a_coord) ) {
					if( depot_t* dep = gr->get_depot() ) {
						tstrncpy(a_name, dep->get_name(), lengthof(a_name) );
					}
				}
			}
			if( b_coord !=koord3d::invalid ) {
				if( grund_t* gr = welt->lookup(b_coord) ) {
					if( depot_t* dep = gr->get_depot() ) {
						tstrncpy(b_name, dep->get_name(), lengthof(b_name) );
					}
				}
			}
			result = strcmp(a_name, b_name);
			if( result==0  &&  a_coord!=koord3d::invalid  &&  b_coord!=koord3d::invalid ) {
				result = a_coord.x - b_coord.x;
				if (result == 0) {
					result = a_coord.y - b_coord.y;
				}
			}
			break;
		}
		case by_profit:
			result = sgn(cnv1->get_jahresgewinn() - cnv2->get_jahresgewinn());
			break;
		case by_type:
			if(cnv1->get_vehicle_count()*cnv2->get_vehicle_count()>0) {
				vehicle_t const* const tdriver1 = cnv1->front();
				vehicle_t const* const tdriver2 = cnv2->front();

				result = tdriver1->get_typ() - tdriver2->get_typ();
				if(result == 0) {
					result = tdriver1->get_cargo_type()->get_catg_index() - tdriver2->get_cargo_type()->get_catg_index();
					if(result == 0) {
						result = tdriver1->get_base_image() - tdriver2->get_base_image();
					}
				}
			}
			break;
		case by_id:
			result = cnv1.get_id()-cnv2.get_id();
			break;
		case by_max_speed:
			result = cnv1->get_min_top_speed() - cnv2->get_min_top_speed();
			break;
		case by_power:
			result = cnv1->get_sum_power() - cnv2->get_sum_power();
			break;
		case by_value:
			result = cnv1->get_purchase_cost() - cnv2->get_purchase_cost();
			break;
		case by_age:
			result = cnv1->get_average_age() - cnv2->get_average_age();
			break;
		case by_range:
			result = cnv1->get_min_range() - cnv2->get_min_range();
			break;
		case by_loadfactor_pax:
			const sint64 factor_1 = cnv1->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) ? (sint64)cnv1->get_load_factor_pax() : -1;
			const sint64 factor_2 = cnv2->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) ? (sint64)cnv2->get_load_factor_pax() : -1;
			result = factor_1 - factor_2;
			break;
	}
	return sortreverse ? result > 0 : result < 0;
}


void convoi_frame_t::fill_list()
{
	last_world_convois = welt->convoys().get_count();

	const bool all = owner->get_player_nr() == 1;
	scrolly->clear_elements();
	for(convoihandle_t const cnv : welt->convoys()) {
		if(  all  ||  cnv->get_owner()==owner  ) {
			if(  passes_filter( cnv )  ) {
				scrolly->new_component<gui_convoiinfo_t>( cnv );
			}
		}
	}
	sort_list();
}


void convoi_frame_t::sort_list()
{
	scrolly->sort();
}


void convoi_frame_t::sort_list( uint32 filter, const slist_tpl<const goods_desc_t *> *wares )
{
	waren_filter = wares;
	filter_flags = filter;
	fill_list();
}


convoi_frame_t::convoi_frame_t() :
	gui_frame_t( translator::translate("cl_title"), welt->get_active_player())
{
	owner = welt->get_active_player();
	last_name_filter[0] = name_filter[0] = 0;
	filter_flags = 0;

	set_table_layout(1,0);

	add_table(4,2);
	{
		if( skinverwaltung_t::search ) {
			new_component<gui_image_t>(skinverwaltung_t::search->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Filter:"));
		}
		else {
			new_component<gui_label_t>("Filter:");
		}		name_filter_input.set_text(name_filter, lengthof(name_filter));
		name_filter_input.set_search_box(true);
		add_component(&name_filter_input);

		filter_details.init(button_t::roundbox, "cl_btn_filter_settings");
		if( skinverwaltung_t::open_window ) {
			filter_details.set_image(skinverwaltung_t::open_window->get_image_id(0));
			filter_details.set_image_position_right(true);
		}
		filter_details.set_size(D_BUTTON_SIZE);
		filter_details.add_listener(this);
		new_component<gui_empty_t>();
		add_component(&filter_details);

		new_component<gui_label_t>("hl_txt_sort");

		// sort asc/desc switching button
		add_table(3,1);
		{
			for (int i = 0; i < SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(default_sortmode);
			sortedby.set_width_fixed(true);
			sortedby.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_EDIT_HEIGHT));
			sortedby.add_listener(this);
			add_component(&sortedby);

			sort_order.init(button_t::sortarrow_state, "");
			sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
			sort_order.add_listener(this);
			sort_order.pressed = sortreverse;
			add_component(&sort_order);

			new_component<gui_margin_t>(10);
		}
		end_table();

		new_component<gui_label_t>("cl_txt_mode");

		for (uint8 i = 0; i < gui_convoy_formation_t::CONVOY_OVERVIEW_MODES; i++) {
			overview_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(gui_convoy_formation_t::cnvlist_mode_button_texts[i]), SYSCOL_TEXT);
		}
		overview_selector.set_selection(cl_display_mode);
		overview_selector.add_listener(this);
		add_component(&overview_selector);
	}
	end_table();

	scrolly = new gui_scrolled_convoy_list_t(this);
	scrolly->set_maximize( true );
	scrolly->set_checkered( true );

	tabs.init_tabs(scrolly);
	tabs.add_listener(this);
	add_component(&tabs);

	fill_list();

	set_resizemode(diagonal_resize);
	reset_min_windowsize();
}


convoi_frame_t::~convoi_frame_t()
{
	destroy_win( magic_convoi_list_filter+owner->get_player_nr() );
}


bool convoi_frame_t::infowin_event(const event_t *ev)
{
	if(ev->ev_class == INFOWIN  &&  ev->ev_code == WIN_CLOSE) {
		destroy_win( magic_convoi_list_filter+owner->get_player_nr() );
	}
	return gui_frame_t::infowin_event(ev);
}


/**
 * This method is called if an action is triggered
 */
bool convoi_frame_t::action_triggered( gui_action_creator_t *comp, value_t /* */ )
{
	if(  comp == &tabs  ) {
		fill_list();
	}
	else if(  comp == &sortedby  ) {
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			sortby = (convoi_frame_t::sort_mode_t)tmp;
		}
		else {
			sortedby.set_selection(0);
			sortby = convoi_frame_t::by_name;
		}
		default_sortmode = (uint8)tmp;
		sort_list();
	}
	else if(  comp==&sort_order  ) {
		set_reverse( !get_reverse() );
		sort_list();
		sort_order.pressed = sortreverse;
	}
	else if(  comp==&overview_selector  ) {
		cl_display_mode = overview_selector.get_selection();
		sort_list();
		resize(scr_size(0, 0));
	}
	else if(  comp == &filter_details  ) {
		if(  !destroy_win( magic_convoi_list_filter+owner->get_player_nr() )  ) {
			convoi_filter_frame_t *gui_cff = new convoi_filter_frame_t(owner, this);
			gui_cff->init(filter_flags, waren_filter);
			scr_coord const& dialog_pos = win_get_pos(this);
			scr_coord_val new_dialog_pos_x = -1;
			if ((dialog_pos.x + get_windowsize().w + D_DEFAULT_WIDTH) < display_get_width()) {
				new_dialog_pos_x = dialog_pos.x + get_windowsize().w;
			}
			else if ((dialog_pos.x + get_windowsize().w + D_DEFAULT_WIDTH / 2) < display_get_width()) {
				new_dialog_pos_x = dialog_pos.x + get_windowsize().w / 2;
			}
			create_win({ new_dialog_pos_x, dialog_pos.y }, gui_cff, w_info, magic_convoi_list_filter + owner->get_player_nr());
		}
	}
	return true;
}


void convoi_frame_t::draw(scr_coord pos, scr_size size)
{
	filter_details.pressed = win_get_magic( magic_convoi_list_filter+owner->get_player_nr() );

	if (last_world_convois != welt->convoys().get_count()  ||  strcmp(last_name_filter,name_filter)) {
		strcpy( last_name_filter, name_filter );
		// some deleted/ added => resort
		fill_list();
	}

	gui_frame_t::draw(pos, size);
}


void convoi_frame_t::rdwr(loadsave_t *file)
{
	scr_size size = get_windowsize();
	uint8 player_nr = owner->get_player_nr();

	file->rdwr_byte( player_nr );
	size.rdwr( file );
	file->rdwr_bool( sortreverse );
	bool dummy;
	file->rdwr_bool( dummy );
	file->rdwr_long( filter_flags );
	file->rdwr_byte( default_sortmode );
	file->rdwr_byte( cl_display_mode );
	if (file->is_saving()) {
		uint8 good_nr = get_filter(convoi_filter_frame_t::ware_filter) ? waren_filter->get_count() : 0;
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			for(const goods_desc_t * const i : *waren_filter ) {
				char *name = const_cast<char *>(i->get_name());
				file->rdwr_str(name,256);
			}
		}
	}
	else {
		uint8 good_nr;
		file->rdwr_byte(good_nr);
		if (good_nr > 0) {
			static slist_tpl<const goods_desc_t *>waren_filter_rd;
			for (sint16 i = 0; i < good_nr; i++) {
				char name[256];
				file->rdwr_str(name, lengthof(name));
				if (const goods_desc_t *gd = goods_manager_t::get_info(name)) {
					waren_filter_rd.append(gd);
				}
			}
			waren_filter = &waren_filter_rd;
		}

		sortby = (sort_mode_t)default_sortmode;
		sort_order.pressed = sortreverse;
		sortedby.set_selection(default_sortmode);
		overview_selector.set_selection(cl_display_mode);
		owner = welt->get_player(player_nr);
		win_set_magic(this, magic_convoi_list + player_nr);

		fill_list();
		set_windowsize(size);

	}
}
