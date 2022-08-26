/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_theme.h"
#include "vehiclelist_frame.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"

#include "../simskin.h"
#include "../simworld.h"

#include "../display/simgraph.h"

#include "../dataobj/translator.h"

#include "../descriptor/goods_desc.h"
#include "../descriptor/intro_dates.h"
#include "../descriptor/skin_desc.h"
#include "../descriptor/vehicle_desc.h"

#include "../utils/simstring.h"


int vehiclelist_stats_t::sort_mode = vehicle_builder_t::sb_intro_date;
bool vehiclelist_stats_t::reverse = false;

// for having uniform spaced columns
int vehiclelist_stats_t::img_width = 100;


vehiclelist_stats_t::vehiclelist_stats_t(const vehicle_desc_t *v)
{
	veh = v;

	// width of image
	scr_coord_val x, y, w, h;
	const image_id image = veh->get_image_id( ribi_t::dir_south, veh->get_freight_type() );
	display_get_base_image_offset(image, &x, &y, &w, &h );
	if( w > img_width ) {
		img_width = w + D_H_SPACE;
	}
	height = h;

	// name is the widest entry in column 1
	name_width = proportional_string_width( translator::translate( veh->get_name(), world()->get_settings().get_name_language_id() ) );
	if( veh->get_power() > 0 ) {
		char str[ 256 ];
		const uint8 et = (uint8)veh->get_engine_type() + 1;
		sprintf( str, " (%s)", translator::translate( vehicle_builder_t::engine_type_names[et] ) );
		name_width += proportional_string_width( str );
	}

	// column 1
	part1.clear();
	part1.append(" ");
	if( sint64 fix_cost = world()->scale_with_month_length( veh->get_maintenance() ) ) {
		char tmp[ 128 ];
		money_to_string( tmp, veh->get_value() / 100.0, false );
		part1.printf( translator::translate( "Cost: %8s (%.2f$/km %.2f$/m)\n" ), tmp, veh->get_running_cost() / 100.0, fix_cost / 100.0 );
	}
	else {
		char tmp[ 128 ];
		money_to_string( tmp, veh->get_value() / 100.0, false );
		part1.printf( translator::translate( "Cost: %8s (%.2f$/km)\n" ), tmp, veh->get_running_cost() / 100.0 );
	}
	if( veh->get_total_capacity() > 0 || veh->get_overcrowded_capacity() ) { // must translate as "Capacity: %3d%s %s\n"
		part1.append(" ");
		char capacity[32];
		sprintf(capacity, veh->get_overcrowded_capacity() > 0 ? "%i (%i)" : "%i", v->get_total_capacity(), v->get_overcrowded_capacity());
		part1.printf( translator::translate( "capacity: %s %s" ),
			capacity,
			translator::translate( veh->get_freight_type()->get_mass() ),
			veh->get_freight_type()->get_catg() == 0 ? translator::translate( veh->get_freight_type()->get_name() ) : translator::translate( veh->get_freight_type()->get_catg_name() )
		);
		part1.append("\n");
	}
	part1.printf( " %s %3d km/h\n", translator::translate( "Max. speed:" ), veh->get_topspeed() );
	if( veh->get_power() > 0 ) {
		part1.append(" ");
		part1.printf( translator::translate( "Power: %4d kW, %d kN\n" ), veh->get_power(), veh->get_tractive_effort());
	}
	int text1w, text1h;
	display_calc_proportional_multiline_string_len_width( text1w, text1h, part1, part1.len() );
	col1_width = text1w + D_H_SPACE;

	// column 2
	part2.clear();
	part2.printf( "%s %4.1ft", translator::translate( "Weight:" ), veh->get_weight() / 1000.0 );
	if (veh->get_waytype() != water_wt) {
		part2.printf( ", %s %it", translator::translate( "Axle load:" ), veh->get_axle_load() );
	}
	part2.append("\n");
	part2.printf( "%s %s - ", translator::translate( "Available:" ), translator::get_short_date( veh->get_intro_year_month() / 12, veh->get_intro_year_month() % 12 ) );
	if (veh->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
		(((!world()->get_settings().get_show_future_vehicle_info() && veh->will_end_prodection_soon(world()->get_timeline_year_month()))
			|| world()->get_settings().get_show_future_vehicle_info()
			|| veh->is_retired(world()->get_timeline_year_month()))))
	{
		part2.printf( "%s", translator::get_short_date( veh->get_retire_year_month() / 12, veh->get_retire_year_month() % 12 ) );
	}
	if( char const* const copyright = veh->get_copyright() ) {
		part2.append("\n");
		part2.printf( translator::translate( "Constructed by %s" ), copyright );
	}
	int text2w, text2h;
	display_calc_proportional_multiline_string_len_width( text2w, text2h, part2, part2.len() );
	col2_width = text2w;

	height = max( height, max( text1h, text2h+LINESPACE ) + D_V_SPACE*2 );
}


void vehiclelist_stats_t::draw( scr_coord offset )
{
	uint32 month = world()->get_current_month();
	offset += pos;

	offset.x += D_MARGIN_LEFT;
	scr_coord_val x, y, w, h;
	const image_id image = veh->get_image_id( ribi_t::dir_south, veh->get_freight_type() );
	display_get_base_image_offset(image, &x, &y, &w, &h );
	display_base_img(image, offset.x - x, offset.y - y, world()->get_active_player_nr(), false, true);

	const uint8 upgradable_state = veh->has_available_upgrade(month);
	// upgradable symbol
	if (upgradable_state && skinverwaltung_t::upgradable) {
		if (world()->get_settings().get_show_future_vehicle_info() || (!world()->get_settings().get_show_future_vehicle_info() && veh->is_future(month) != 2)) {
			display_color_img(skinverwaltung_t::upgradable->get_image_id(upgradable_state-1), offset.x+w-D_FIXED_SYMBOL_WIDTH, offset.y+h, 0, false, false);
		}
	}

	// first name
	offset.x += img_width;
	PIXVAL name_colval = SYSCOL_TEXT;
	if (veh->is_future(month)) {
		name_colval = SYSCOL_EMPTY;
	}
	else if (veh->is_available_only_as_upgrade()) {
		name_colval = SYSCOL_UPGRADEABLE;
	}
	else if (veh->is_obsolete(month)) {
		name_colval = SYSCOL_OBSOLETE;
	}
	else if (veh->is_retired(month)) {
		name_colval = SYSCOL_OUT_OF_PRODUCTION;
	}
	int dx = display_proportional_rgb(
		offset.x, offset.y,
		translator::translate( veh->get_name(), world()->get_settings().get_name_language_id() ),
		ALIGN_LEFT|DT_CLIP,
		name_colval,
		false
	);
	if( veh->get_power() > 0 ) {
		char str[ 256 ];
		const uint8 et = (uint8)veh->get_engine_type() + 1;
		sprintf( str, " (%s)", translator::translate( vehicle_builder_t::engine_type_names[et] ) );
		display_proportional_rgb( offset.x+dx, offset.y, str, ALIGN_LEFT|DT_CLIP, SYSCOL_TEXT, false );
	}

	int yyy = offset.y + LINESPACE;
	display_multiline_text_rgb( offset.x, yyy, part1, SYSCOL_TEXT );

	display_multiline_text_rgb( offset.x + col1_width, yyy, part2, SYSCOL_TEXT );
}

const char *vehiclelist_stats_t::get_text() const
{
	return translator::translate( veh->get_name() );
}

bool vehiclelist_stats_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	bool result = vehicle_builder_t::compare_vehicles( dynamic_cast<const vehiclelist_stats_t*>(aa)->veh, dynamic_cast<const vehiclelist_stats_t*>(bb)->veh, (vehicle_builder_t::sort_mode_t)vehiclelist_stats_t::sort_mode );
	return vehiclelist_stats_t::reverse ? !result : result;
}


vehiclelist_frame_t::vehiclelist_frame_t() :
	gui_frame_t( translator::translate("vh_title") ),
	scrolly(gui_scrolled_list_t::windowskin, vehiclelist_stats_t::compare)
{
	scrolly.set_cmp( vehiclelist_stats_t::compare );

	set_table_layout(1,0);

	add_table(2,0);
	{
		// left column
		add_table(1,3);
		{
			add_table(1,2);
			{
				new_component<gui_label_t>( "hl_txt_sort" );

				add_table(3,1);
				{
					sort_by.clear_elements();
					for(int i = 0; i < vehicle_builder_t::sb_length; i++) {
						sort_by.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::vehicle_sort_by[i]), SYSCOL_TEXT);
					}
					sort_by.set_selection( vehiclelist_stats_t::sort_mode );
					sort_by.set_width_fixed( true );
					sort_by.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
					sort_by.add_listener( this );
					add_component( &sort_by );

					// sort asc/desc switching button
					sort_order.init(button_t::sortarrow_state, "");
					sort_order.set_tooltip( translator::translate("hl_btn_sort_order") );
					sort_order.add_listener( this );
					sort_order.pressed = vehiclelist_stats_t::reverse;
					add_component( &sort_order );

					new_component<gui_margin_t>(LINESPACE);
				}
				end_table();

			}
			end_table();

			new_component<gui_margin_t>(0,LINESPACE);

			lb_count.set_size(scr_size(D_LABEL_WIDTH*1.5, D_LABEL_HEIGHT));
			add_component( &lb_count );
		}
		end_table();

		// right column
		add_table(1, 3);
		{
			add_table(2,1);
			{
				new_component<gui_label_t>("Filter:");
				add_table(1, 2);
				{
					engine_filter.clear_elements();
					engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All traction types"), SYSCOL_TEXT);
					for (int i = 0; i < vehicle_desc_t::MAX_TRACTION_TYPE; i++) {
						engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)i]), SYSCOL_TEXT);
					}
					engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Trailers"), SYSCOL_TEXT);
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
				}
				end_table();
			}
			end_table();

			add_table(2, 2);
			{
				bt_obsolete.init(button_t::square_state, "Show obsolete");
				bt_obsolete.add_listener(this);
				add_component(&bt_obsolete);

				bt_outdated.init(button_t::square_state, "Show outdated");
				bt_outdated.add_listener(this);
				add_component(&bt_outdated);

				bt_future.init(button_t::square_state, "Show future");
				bt_future.add_listener(this);
				if (!welt->get_settings().get_show_future_vehicle_info()) {
					bt_future.set_visible(false);
				}
				bt_future.pressed = false;
				add_component(&bt_future);

				bt_only_upgrade.init(button_t::square_state, "Show upgraded");
				bt_only_upgrade.add_listener(this);
				add_component(&bt_only_upgrade);
			}
			end_table();
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


/**
 * This method is called if an action is triggered
 */
bool vehiclelist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if(comp == &sort_by) {
		vehiclelist_stats_t::sort_mode = max(0,v.i);
		fill_list();
	}
	else if(comp == &engine_filter) {
		fill_list();
	}
	else if(comp == &ware_filter) {
		fill_list();
	}
	else if (comp == &sort_order) {
		vehiclelist_stats_t::reverse = !vehiclelist_stats_t::reverse;
		scrolly.sort(0);
		sort_order.pressed = vehiclelist_stats_t::reverse;
	}
	else if(comp == &bt_obsolete) {
		bt_obsolete.pressed ^= 1;
		fill_list();
	}
	else if (comp == &bt_outdated) {
		bt_outdated.pressed ^= 1;
		fill_list();
	}
	else if (comp == &bt_future) {
		bt_future.pressed ^= 1;
		fill_list();
	}
	else if (comp == &bt_only_upgrade) {
		bt_only_upgrade.pressed ^= 1;
		fill_list();
	}
	else if(comp == &tabs) {
		fill_list();
	}
	return true;
}


void vehiclelist_frame_t::fill_list()
{
	scrolly.clear_elements();
	count = 0;
	vehiclelist_stats_t::img_width = 32; // reset col1 width
	uint32 month = world()->get_current_month();
	const goods_desc_t *ware = idx_to_ware[ max( 0, ware_filter.get_selection() ) ];
	if(  tabs.get_active_tab_waytype() == ignore_wt) {
		// adding all vehiles, i.e. iterate over all available waytypes
		for(  int i=1;  i<tabs.get_count();  i++  ) {
			FOR( slist_tpl<vehicle_desc_t *>, const veh, vehicle_builder_t::get_info(tabs.get_tab_waytype(i)) ) {
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

				// timeline status filter
				if (!bt_only_upgrade.pressed && veh->is_available_only_as_upgrade()) {
					continue;
				}
				if (!bt_outdated.pressed && (veh->is_retired(month) && !veh->is_obsolete(month))) {
					continue;
				}
				if (!bt_obsolete.pressed && veh->is_obsolete(month)) {
					continue;
				}
				if (!bt_future.pressed && veh->is_future(month)) {
					continue;
				}
				// goods category filter
				if( ware ) {
					const goods_desc_t *vware = veh->get_freight_type();
					if(  (ware->get_catg_index() > 0  &&  vware->get_catg_index() == ware->get_catg_index())  ||  vware->get_index() == ware->get_index()  ) {
						scrolly.new_component<vehiclelist_stats_t>( veh );
						count++;
					}
				}
				else {
					scrolly.new_component<vehiclelist_stats_t>( veh );
					count++;
				}
			}
		}
	}
	else {
		FOR(slist_tpl<vehicle_desc_t *>, const veh, vehicle_builder_t::get_info(tabs.get_active_tab_waytype())) {
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

			// timeline status filter
			if (!bt_only_upgrade.pressed && veh->is_available_only_as_upgrade()) {
				continue;
			}
			if (!bt_outdated.pressed && (veh->is_retired(month) && !veh->is_obsolete(month))) {
				continue;
			}
			if (!bt_obsolete.pressed && veh->is_obsolete(month)) {
				continue;
			}
			if (!bt_future.pressed && veh->is_future(month)) {
				continue;
			}
			// goods category filter
			if( ware ) {
				const goods_desc_t *vware = veh->get_freight_type();
				if(  (ware->get_catg_index() > 0  &&  vware->get_catg_index() == ware->get_catg_index())  ||  vware->get_index() == ware->get_index()  ) {
					scrolly.new_component<vehiclelist_stats_t>( veh );
					count++;
				}
			}
			else {
				scrolly.new_component<vehiclelist_stats_t>( veh );
				count++;
			}
		}
	}
	if( vehiclelist_stats_t::sort_mode != 0 ) {
		scrolly.sort(0);
	}
	else {
		scrolly.set_size( scrolly.get_size() );
	}
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
}

void vehiclelist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();

	size.rdwr(file);
	tabs.rdwr(file);
	scrolly.rdwr(file);
	ware_filter.rdwr(file);
	sort_by.rdwr(file);
	engine_filter.rdwr(file);
	file->rdwr_bool(sort_order.pressed);
	file->rdwr_bool(bt_obsolete.pressed);
	file->rdwr_bool(bt_future.pressed);
	file->rdwr_bool(bt_outdated.pressed);
	file->rdwr_bool(bt_only_upgrade.pressed);

	if (file->is_loading()) {
		fill_list();
		set_windowsize(size);
	}
}
