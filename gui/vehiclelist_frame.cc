/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_theme.h"
#include "vehiclelist_frame.h"
#include "vehicle_detail.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"

#include "../simskin.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"

#include "../display/simgraph.h"

#include "../dataobj/translator.h"

#include "../descriptor/goods_desc.h"
#include "../descriptor/intro_dates.h"
#include "../descriptor/skin_desc.h"
#include "../descriptor/vehicle_desc.h"

#include "../utils/simstring.h"
#include "../unicode.h"


char vehiclelist_frame_t::name_filter[256] = "";
char vehiclelist_frame_t::status_counts_text[10][VL_MAX_STATUS_FILTER] = {};


static const char *const vl_header_text[vehicle_desc_row_t::MAX_COLS] =
{
	"Name", "", "Wert", "engine_type",
	"Leistung", "TF_", "Max. speed", "",
	"Capacity", "curb_weight", "Axle load:", "Intro. date","Retire date"
};

static const char *const timeline_filter_button_text[vehiclelist_frame_t::VL_MAX_STATUS_FILTER] =
{
	"Show future", "Show available", "Show outdated", "Show obsolete", "upgrade_only"
};


vehiclelist_frame_t::vehiclelist_frame_t() :
	gui_frame_t( translator::translate("vh_title") ),
	scrolly(gui_scrolled_list_t::windowskin, vehicle_desc_row_t::compare),
	scrollx_tab(&cont_sortable,true,true)
{
	last_name_filter[0] = 0;
	// Scrolling in x direction should not be possible due to fixed header
	scrolly.set_show_scroll_x(false);
	scrolly.set_checkered(true);
	scrolly.set_scroll_amount_y(LINESPACE + D_H_SPACE + 2); // default cell height

	// init table sort buttons
	table_header.add_listener(this);
	for (uint8 col = 0; col < vehicle_desc_row_t::MAX_COLS; col++) {
		table_header.new_component<sortable_header_cell_t>(vl_header_text[col]);
	}

	cont_sortable.set_margin(NO_SPACING, NO_SPACING);
	cont_sortable.set_spacing(NO_SPACING);
	cont_sortable.set_table_layout(1,2);
	cont_sortable.set_alignment(ALIGN_TOP); // Without this, the layout will be broken if the height is small.
	cont_sortable.add_component(&table_header);
	cont_sortable.add_component(&scrolly);

	set_table_layout(1,0);

	add_table(6,1);
	{
		if( skinverwaltung_t::search ) {
			new_component<gui_image_t>(skinverwaltung_t::search->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Filter:"));
		}
		else {
			new_component<gui_label_t>("Filter:");
		}

		name_filter_input.set_text(name_filter, lengthof(name_filter));
		name_filter_input.set_search_box(true);
		add_component(&name_filter_input);
		name_filter_input.add_listener(this);
		new_component<gui_margin_t>(D_H_SPACE);

		engine_filter.clear_elements();
		engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All traction types"), SYSCOL_TEXT);
		for (int i = 0; i < vehicle_desc_t::MAX_TRACTION_TYPE; i++) {
			engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)i]), SYSCOL_TEXT);
		}
		engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Trailers"), SYSCOL_TEXT);
		vehicle_desc_row_t::filter_flag = 0;
		engine_filter.set_selection( 0 );
		engine_filter.add_listener( this );
		add_component( &engine_filter );

		ware_filter.clear_elements();
		ware_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All freight types"), SYSCOL_TEXT);
		idx_to_ware.append(NULL);
		for (int i = 0; i < goods_manager_t::get_count(); i++) {
			const goods_desc_t *ware = goods_manager_t::get_info(i);
			if (ware == goods_manager_t::none) {
				continue;
			}
			if (ware->get_catg() == 0) {
				ware_filter.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(ware->get_name()), SYSCOL_TEXT, ware->get_catg_symbol());
				idx_to_ware.append(ware);
			}
		}
		// now add other good categories
		for (int i = 1; i < goods_manager_t::get_max_catg_index(); i++) {
			const goods_desc_t *ware = goods_manager_t::get_info_catg(i);
			if (ware->get_catg() != 0) {
				ware_filter.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(ware->get_catg_name()), SYSCOL_TEXT, ware->get_catg_symbol());
				idx_to_ware.append(ware);
			}
		}
		ware_filter.set_selection(0);
		ware_filter.add_listener(this);
		add_component(&ware_filter);

		bt_upgradable.init(button_t::square_state, "Upgradable");
		bt_upgradable.add_listener(this);
		add_component(&bt_upgradable);
	}
	end_table();

	// timeline filter
	const PIXVAL timeline_filter_button_colors[vehiclelist_frame_t::VL_MAX_STATUS_FILTER] =
	{
		color_idx_to_rgb(MN_GREY0), COL_SAFETY, SYSCOL_OUT_OF_PRODUCTION, SYSCOL_OBSOLETE, SYSCOL_UPGRADEABLE
	};
	add_table(3, 1);
	{
		new_component<gui_label_t>("Status filter(count):");
		const scr_coord_val button_width = proportional_string_width("888888") + D_BUTTON_PADDINGS_X;
		gui_aligned_container_t *tbl = add_table(VL_MAX_STATUS_FILTER+1,1);
		//tbl->set_force_equal_columns(true);
		{
			bt_timeline_filters[VL_SHOW_AVAILABLE].pressed = true;
			for( uint8 i=0; i<VL_MAX_STATUS_FILTER; ++i ) {
				if (i==VL_FILTER_UPGRADE_ONLY) new_component<gui_margin_t>(LINESPACE);
				bt_timeline_filters[i].init( button_t::box_state|button_t::flexible, "");
				bt_timeline_filters[i].set_tooltip( translator::translate(timeline_filter_button_text[i]) );
				bt_timeline_filters[i].set_width(button_width);
				bt_timeline_filters[i].background_color = timeline_filter_button_colors[i];
				bt_timeline_filters[i].add_listener(this);
				add_component(&bt_timeline_filters[i]);
			}
		}
		end_table();
		new_component<gui_fill_t>();
	}
	end_table();

	add_table(4,1)->set_spacing(scr_size(0,0));
	{
		add_component( &lb_count );
		new_component<gui_fill_t>();
		bt_show_name.init( button_t::roundbox_left_state, "show_name");
		bt_show_name.add_listener(this);
		bt_show_side_view.init(button_t::roundbox_right_state, "show_side_view");
		bt_show_side_view.add_listener(this);
		bt_show_name.pressed = !vehicle_desc_row_t::side_view_mode;
		bt_show_side_view.pressed = vehicle_desc_row_t::side_view_mode;
		add_component( &bt_show_name );
		add_component( &bt_show_side_view);
	}
	end_table();

	tabs.init_tabs(&scrollx_tab);
	tabs.add_listener(this);
	add_component(&tabs);
	reset_min_windowsize();

	fill_list();

	set_resizemode(diagonal_resize);
	scrollx_tab.set_maximize(false);
}


void vehiclelist_frame_t::draw(scr_coord pos, scr_size size)
{
	if( strcmp(last_name_filter, name_filter) ) {
		fill_list();
	}
	gui_frame_t::draw(pos, size);
}


/**
 * This method is called if an action is triggered
 */
bool vehiclelist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if( comp==&engine_filter ) {
		fill_list();
	}
	else if(comp == &ware_filter) {
		fill_list();
	}
	else if (comp == &bt_upgradable) {
		bt_upgradable.pressed ^= 1;
		fill_list();
	}
	else if(comp == &tabs) {
		fill_list();
	}
	else if( comp == &bt_show_name ) {
		bt_show_name.pressed      = true;
		bt_show_side_view.pressed = false;
		vehicle_desc_row_t::side_view_mode = false;
		fill_list();
	}
	else if( comp == &bt_show_side_view) {
		bt_show_name.pressed      = false;
		bt_show_side_view.pressed = true;
		vehicle_desc_row_t::side_view_mode = true;
		fill_list();
	}
	else if (comp == &table_header) {
		vehicle_desc_row_t::sort_mode = v.i;
		table_header.set_selection(vehicle_desc_row_t::sort_mode);
		vehicle_desc_row_t::sortreverse = table_header.is_reversed();
		scrolly.sort(0);
	}
	else {
		for( uint8 i=0; i<VL_MAX_STATUS_FILTER; ++i ) {
			if(  comp==&bt_timeline_filters[i]  ) {
				bt_timeline_filters[i].pressed ^= 1;
				fill_list();
				return true;
			}
		}
	}
	return true;
}


void vehiclelist_frame_t::fill_list()
{
	uint16 status_counts[VL_MAX_STATUS_FILTER] = {};
	if( engine_filter.get_selection()==0 ) {
		vehicle_desc_row_t::filter_flag &= ~vehicle_desc_row_t::VL_FILTER_FUEL;
	}
	else {
		vehicle_desc_row_t::filter_flag |= vehicle_desc_row_t::VL_FILTER_FUEL;
	}
	if( ware_filter.get_selection()==0 ) {
		vehicle_desc_row_t::filter_flag &= ~vehicle_desc_row_t::VL_FILTER_FREIGHT;
	}
	else {
		vehicle_desc_row_t::filter_flag |= vehicle_desc_row_t::VL_FILTER_FREIGHT;
	}
	if( bt_upgradable.pressed ) {
		vehicle_desc_row_t::filter_flag |= vehicle_desc_row_t::VL_FILTER_UPGRADABLE;
	}
	else {
		vehicle_desc_row_t::filter_flag &= ~vehicle_desc_row_t::VL_FILTER_UPGRADABLE;
	}

	strcpy(last_name_filter, name_filter);
	if (last_name_filter[0] == 0) {
		vehicle_desc_row_t::filter_flag &= ~vehicle_desc_row_t::VL_FILTER_NAME;
	}
	else {
		vehicle_desc_row_t::filter_flag |= vehicle_desc_row_t::VL_FILTER_NAME;
	}

	scrolly.clear_elements();

	count = 0;
	uint32 month = world()->get_current_month();
	const goods_desc_t *ware = idx_to_ware[ max( 0, ware_filter.get_selection() ) ];
	const bool show_all_upgrade_only_vehicle = bt_timeline_filters[VL_FILTER_UPGRADE_ONLY].pressed
		&& !bt_timeline_filters[VL_SHOW_FUTURE].pressed
		&& !bt_timeline_filters[VL_SHOW_AVAILABLE].pressed
		&& !bt_timeline_filters[VL_SHOW_OUT_OF_PROD].pressed
		&& !bt_timeline_filters[VL_SHOW_OUT_OBSOLETE].pressed;
	if(  tabs.get_active_tab_waytype() == ignore_wt) {
		// adding all vehiles, i.e. iterate over all available waytypes
		for(  uint32 i=1;  i<tabs.get_count();  i++  ) {
			for( auto const veh : vehicle_builder_t::get_info(tabs.get_tab_waytype(i)) ) {
				// engine type filter
				switch (engine_filter.get_selection()) {
					case 0:
						/* do nothing */
						break;
					case (uint8)vehicle_desc_t::engine_t::MAX_TRACTION_TYPE+1:
						if (veh->get_engine_type() != vehicle_desc_t::engine_t::unknown) {
							continue;
						}
						break;
					default:
						if (((uint8)veh->get_engine_type()+1) != engine_filter.get_selection()-1) {
							continue;
						}
						break;
				}

				// name filter
				if( last_name_filter[0]!=0 && !utf8caseutf8( veh->get_name(),name_filter )  &&  !utf8caseutf8(translator::translate(veh->get_name()), name_filter) ) {
					continue;
				}

				if( bt_timeline_filters[VL_FILTER_UPGRADE_ONLY].pressed && !veh->is_available_only_as_upgrade() && !show_all_upgrade_only_vehicle ) {
					continue;
				}
				if( bt_upgradable.pressed  &&  !veh->has_available_upgrade(month) ) {
					continue;
				}

				// timeline status filter
				bool timeline_matches = false;
				if( bt_timeline_filters[VL_SHOW_AVAILABLE].pressed  &&  veh->is_available(month) ) {
					timeline_matches = true; // show green ones
				}
				if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_OUT_OF_PROD].pressed  &&  !veh->is_obsolete(month)  &&  veh->is_retired(month) ) {
					timeline_matches = true; // show royalblue ones
				}
				if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_OUT_OBSOLETE].pressed  &&  veh->is_obsolete(month) ) {
					timeline_matches = true; // show blue ones
				}
				if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_FUTURE].pressed  &&  veh->is_future(month) ) {
					if( !welt->get_settings().get_show_future_vehicle_info()  &&  veh->is_future(month)==1 ) {
						// Do not show vehicles in the distant future with this setting
						continue;
					}
					timeline_matches = true; // show gray ones
				}
				if( show_all_upgrade_only_vehicle && veh->is_available_only_as_upgrade() ) {
					timeline_matches = true;
				}
				if( !timeline_matches ) {
					continue;
				}

				// goods category filter
				if( ware ) {
					const goods_desc_t *vware = veh->get_freight_type();
					if( ( ware->get_catg_index()!=goods_manager_t::INDEX_NONE  &&  vware->get_catg_index()!=ware->get_catg_index())  &&   vware->get_index()!=ware->get_index() ) {
						continue;
					}
				}
				scrolly.new_component<vehicle_desc_row_t>(veh);
				//scrolly.new_component<vehiclelist_stats_t>( veh, side_view_mode );
				if( veh->is_available_only_as_upgrade() ){
					status_counts[VL_FILTER_UPGRADE_ONLY]++;
				}
				const PIXVAL status_color = veh->get_vehicle_status_color();
				// count vehicle status
				if (status_color==COL_SAFETY) {
					status_counts[VL_SHOW_AVAILABLE]++;
				}
				else if (status_color == SYSCOL_OBSOLETE) {
					status_counts[VL_SHOW_OUT_OBSOLETE]++;
				}
				else if (status_color == SYSCOL_OUT_OF_PRODUCTION) {
					status_counts[VL_SHOW_OUT_OF_PROD]++;
				}
				else if (status_color == color_idx_to_rgb(MN_GREY0)) {
					status_counts[VL_SHOW_FUTURE]++;
				}
				count++;
			}
		}
	}
	else {
		for( auto const veh : vehicle_builder_t::get_info(tabs.get_active_tab_waytype()) ) {
			// engine type filter
			switch (engine_filter.get_selection()) {
				case 0:
					/* do nothing */
					break;
				case (uint8)vehicle_desc_t::engine_t::MAX_TRACTION_TYPE+1:
					if (veh->get_engine_type() != vehicle_desc_t::engine_t::unknown) {
						continue;
					}
					break;
				default:
					if (((uint8)veh->get_engine_type()+1) != engine_filter.get_selection()-1) {
						continue;
					}
					break;
			}

			// name filter
			if( last_name_filter[0]!=0 && !utf8caseutf8( veh->get_name(),name_filter )  &&  !utf8caseutf8(translator::translate(veh->get_name()), name_filter) ) {
				continue;
			}

			if( bt_timeline_filters[VL_FILTER_UPGRADE_ONLY].pressed && !veh->is_available_only_as_upgrade() && !show_all_upgrade_only_vehicle ) {
				continue;
			}
			if( bt_upgradable.pressed  &&  !veh->has_available_upgrade(month) ) {
				continue;
			}

			// timeline status filter
			bool timeline_matches = false;
			if( bt_timeline_filters[VL_SHOW_AVAILABLE].pressed  &&  veh->is_available(month) ) {
				timeline_matches = true; // show green ones
			}
			if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_OUT_OF_PROD].pressed  &&  !veh->is_obsolete(month)  &&  veh->is_retired(month) ) {
				timeline_matches = true; // show royalblue ones
			}
			if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_OUT_OBSOLETE].pressed  &&  veh->is_obsolete(month) ) {
				timeline_matches = true; // show blue ones
			}
			if( !timeline_matches  &&  bt_timeline_filters[VL_SHOW_FUTURE].pressed  &&  veh->is_future(month) ) {
				if( welt->get_settings().get_show_future_vehicle_info()  &&  veh->is_future(month)==1 ) {
					// Do not show vehicles in the distant future with this setting
					continue;
				}
				timeline_matches = true; // show blue ones
			}
			if( show_all_upgrade_only_vehicle && veh->is_available_only_as_upgrade() ) {
				timeline_matches = true;
			}
			if( !timeline_matches ) {
				continue;
			}

			// goods category filter
			if( ware ) {
				const goods_desc_t *vware = veh->get_freight_type();
				if( ( ware->get_catg_index()!=goods_manager_t::INDEX_NONE  &&  vware->get_catg_index()!=ware->get_catg_index())  &&   vware->get_index()!=ware->get_index() ) {
					continue;
				}
			}
			//scrolly.new_component<vehiclelist_stats_t>( veh, side_view_mode );
			scrolly.new_component<vehicle_desc_row_t>(veh);
			if( veh->is_available_only_as_upgrade() ){
				status_counts[VL_FILTER_UPGRADE_ONLY]++;
			}
			const PIXVAL status_color = veh->get_vehicle_status_color();
			// count vehicle status
			if (status_color == COL_SAFETY) {
				status_counts[VL_SHOW_AVAILABLE]++;
			}
			else if (status_color == SYSCOL_OBSOLETE) {
				status_counts[VL_SHOW_OUT_OBSOLETE]++;
			}
			else if (status_color == SYSCOL_OUT_OF_PRODUCTION) {
				status_counts[VL_SHOW_OUT_OF_PROD]++;
			}
			else if (status_color == color_idx_to_rgb(MN_GREY0)) {
				status_counts[VL_SHOW_FUTURE]++;
			}
			count++;
		}
	}
	scrolly.sort(0);
	scrolly.set_size( scrolly.get_size() );
	switch (count) {
		case 0:
			lb_count.buf().printf(translator::translate("Vehicle not found"), count);
			lb_count.set_color(SYSCOL_EMPTY);
			break;
		case 1:
			lb_count.buf().printf(translator::translate("One vehicle found"), count);
			lb_count.set_color(SYSCOL_TEXT);
			break;
		default:
			lb_count.buf().printf(translator::translate("%u vehicles found"), count);
			lb_count.set_color(SYSCOL_TEXT);
			break;
	}
	lb_count.update();

	// recalc stats table width
	scr_coord_val max_widths[vehicle_desc_row_t::MAX_COLS] = {};

	// check column widths
	table_header.set_selection(vehicle_desc_row_t::sort_mode);
	table_header.set_width(D_H_SPACE); // init width
	for (uint c = 0; c < vehicle_desc_row_t::MAX_COLS; c++) {
		// get header widths
		max_widths[c] = table_header.get_min_width(c);
	}
	for (int r = 0; r < scrolly.get_count(); r++) {
		vehicle_desc_row_t* row = (vehicle_desc_row_t*)scrolly.get_element(r);
		for (uint c = 0; c < vehicle_desc_row_t::MAX_COLS; c++) {
			max_widths[c] = max(max_widths[c], row->get_min_width(c));
		}
	}

	// set column widths
	for (uint c = 0; c < vehicle_desc_row_t::MAX_COLS; c++) {
		table_header.set_col(c, max_widths[c]);
	}
	table_header.set_width(table_header.get_size().w + D_SCROLLBAR_WIDTH);

	for (int r = 0; r < scrolly.get_count(); r++) {
		vehicle_desc_row_t* row = (vehicle_desc_row_t*)scrolly.get_element(r);
		row->set_size(scr_size(0, row->get_size().h));
		for (uint c = 0; c < vehicle_desc_row_t::MAX_COLS; c++) {
			row->set_col(c, max_widths[c]);
		}
	}

	for( uint8 i=0; i<VL_MAX_STATUS_FILTER; ++i ) {
		if( bt_timeline_filters[i].pressed ){
			sprintf(status_counts_text[i], "%u", status_counts[i]);
			bt_timeline_filters[i].set_text(status_counts_text[i]);
		}
		else {
			bt_timeline_filters[i].set_text("off");
		}
	}
	cont_sortable.set_size(cont_sortable.get_min_size());
	resize(scr_size(0,0));
}

void vehiclelist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();

	size.rdwr(file);
	tabs.rdwr(file);
	scrolly.rdwr(file);
	ware_filter.rdwr(file);
	if( file->is_loading()  &&  file->is_version_ex_less(14,62) ) {
		gui_combobox_t dummy_sort_by;
		dummy_sort_by.rdwr(file);
	}
	engine_filter.rdwr(file);
	file->rdwr_bool(vehicle_desc_row_t::sortreverse);
	file->rdwr_bool(bt_timeline_filters[VL_SHOW_OUT_OBSOLETE].pressed);
	file->rdwr_bool(bt_timeline_filters[VL_SHOW_FUTURE].pressed);
	file->rdwr_bool(bt_timeline_filters[VL_SHOW_OUT_OF_PROD].pressed);
	file->rdwr_bool(bt_timeline_filters[VL_FILTER_UPGRADE_ONLY].pressed);
	if( file->is_version_ex_atleast(14,59) ) { // TODO: recheck version
		file->rdwr_bool(bt_upgradable.pressed);
	}

	if (file->is_loading()) {
		fill_list();
		set_windowsize(size);
	}
}
