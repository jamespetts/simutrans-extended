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

#include "../display/simgraph.h"

#include "../dataobj/translator.h"

#include "../descriptor/goods_desc.h"
#include "../descriptor/intro_dates.h"
#include "../descriptor/skin_desc.h"
#include "../descriptor/vehicle_desc.h"

#include "../utils/simstring.h"
#include "../unicode.h"


int vehiclelist_stats_t::sort_mode = vehicle_builder_t::sb_intro_date;
bool vehiclelist_stats_t::reverse = false;

uint8 vehiclelist_frame_t::filter_flag=0;
bool vehiclelist_frame_t::side_view_mode = true;
char vehiclelist_frame_t::name_filter[256] = "";
char vehiclelist_frame_t::status_counts_text[10][VL_MAX_STATUS_FILTER] = {};

int vehiclelist_frame_t::cell_width[vehiclelist_frame_t::VL_MAX_SPECS] = {};
#define MAX_IMG_WIDTH vehiclelist_frame_t::cell_width[vehiclelist_frame_t::VL_IMAGE]
int vehiclelist_frame_t::stats_width = 0;

static int col_to_sort_mode[vehiclelist_frame_t::VL_MAX_SPECS] = {
	vehicle_builder_t::sb_name,
	vehicle_builder_t::sb_role,
	vehicle_builder_t::sb_value,
	vehicle_builder_t::sb_enigine_type,
	vehicle_builder_t::sb_power,
	vehicle_builder_t::sb_tractive_force,
	vehicle_builder_t::sb_freight,
	vehicle_builder_t::sb_capacity,
	vehicle_builder_t::sb_speed,
	vehicle_builder_t::sb_weight,
	vehicle_builder_t::sb_axle_load,
	vehicle_builder_t::sb_intro_date,
	vehicle_builder_t::sb_retire_date
};

static const char *const vl_header_text[vehiclelist_frame_t::VL_MAX_SPECS] =
{
	"Name", "", "Wert", "engine_type",
	"Leistung", "TF_", "", "Capacity",
	"Max. speed", "curb_weight", "Axle load:", "Intro. date","Retire date"
};

static const char *const timeline_filter_button_text[vehiclelist_frame_t::VL_MAX_STATUS_FILTER] =
{
	"Show future", "Show available", "Show outdated", "Show obsolete", "upgrade_only"
};

vehiclelist_stats_t::vehiclelist_stats_t(const vehicle_desc_t *v)
{
	veh = v;

	// name for tooltip
	tooltip_buf.clear();
	tooltip_buf.append( translator::translate( veh->get_name(), world()->get_settings().get_name_language_id() ) );

	// calculate first column width
	if( vehiclelist_frame_t::side_view_mode ) {
		// width of image
		scr_coord_val x, y, w, h;
		const image_id image = veh->get_image_id( ribi_t::dir_southwest, veh->get_freight_type() );
		display_get_base_image_offset(image, &x, &y, &w, &h );
		if( w > MAX_IMG_WIDTH ) {
			MAX_IMG_WIDTH = min(w + D_H_SPACE, int(get_base_tile_raster_width()*0.67)); // 1 tile = length 16
		}
		height = h;
	}
	else {
		MAX_IMG_WIDTH = max(proportional_string_width(tooltip_buf), MAX_IMG_WIDTH);
	}

	height = max( height, LINEASCENT*2 + D_V_SPACE*3 );
}


void vehiclelist_stats_t::draw( scr_coord offset )
{
	// show tooltip
	if (getroffen(get_mouse_pos() - offset)) {
		win_set_tooltip(get_mouse_pos() + TOOLTIP_MOUSE_OFFSET, tooltip_buf, this);
	}

	const uint32 month = world()->get_current_month();
	offset += pos;
	offset.x += D_H_SPACE;
	const int text_offset_y = (height-1-LINEASCENT)>>1;

	vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail));
	const bool selected = win ? (win->get_vehicle()==veh) : false;

	if (vehiclelist_frame_t::side_view_mode) {
		if( selected ) {
			display_fillbox_wh_clip_rgb(offset.x, offset.y, MAX_IMG_WIDTH - 1, height - 1, SYSCOL_TR_BACKGROUND_SELECTED, false);
		}
		// show side view image
		scr_coord_val x, y, w, h;
		const image_id image = veh->get_image_id( ribi_t::dir_southwest, veh->get_freight_type() );
		display_get_base_image_offset(image, &x, &y, &w, &h );
		display_base_img(image, offset.x - x, offset.y - y + D_GET_CENTER_ALIGN_OFFSET(h, height-1), world()->get_active_player_nr(), false, true);
	}
	else {
		// show name
		PIXVAL name_colval = veh->get_vehicle_status_color();
		if (veh->is_available_only_as_upgrade()) {
			if (name_colval == SYSCOL_OBSOLETE) {
				name_colval = color_idx_to_rgb(COL_DARK_PURPLE);
			}
			else {
				name_colval = SYSCOL_UPGRADEABLE;
			}
		}
		if (name_colval==COL_SAFETY) {
			name_colval = selected ? SYSCOL_TD_TEXT_SELECTED : SYSCOL_TEXT;
		}
		display_fillbox_wh_clip_rgb(offset.x, offset.y, MAX_IMG_WIDTH - 1, height - 1, selected ? SYSCOL_TR_BACKGROUND_SELECTED:SYSCOL_TH_BACKGROUND_LEFT, false);
		display_proportional_rgb(
			offset.x, offset.y + text_offset_y,
			translator::translate( veh->get_name(), world()->get_settings().get_name_language_id() ),
			ALIGN_LEFT|DT_CLIP,
			name_colval,
			false
		);
	}

	offset.x += MAX_IMG_WIDTH;
	for (uint8 col = 1; col<vehiclelist_frame_t::VL_MAX_SPECS; col++) {

		// first, draw the table cell background
		const PIXVAL bg_color = selected ? SYSCOL_TR_BACKGROUND_SELECTED
			: (( col==vehiclelist_frame_t::VL_STATUSBAR && vehiclelist_frame_t::filter_flag&vehiclelist_frame_t::VL_FILTER_UPGRADABLE)) ? SYSCOL_TD_BACKGROUND_HIGHLIGHT
			: (( col==vehiclelist_frame_t::VL_ENGINE_TYPE && vehiclelist_frame_t::filter_flag&vehiclelist_frame_t::VL_FILTER_FUEL) || (col==vehiclelist_frame_t::VL_FREIGHT_TYPE && vehiclelist_frame_t::filter_flag&vehiclelist_frame_t::VL_FILTER_FREIGHT)) ? SYSCOL_TD_BACKGROUND_HIGHLIGHT
			: SYSCOL_TD_BACKGROUND;
		display_fillbox_wh_clip_rgb( offset.x, offset.y, vehiclelist_frame_t::cell_width[col]-1, height-1, bg_color, false );

		buf.clear();
		PIXVAL text_color = selected ? SYSCOL_TD_TEXT_SELECTED : SYSCOL_TEXT;
		switch (col) {
			case vehiclelist_frame_t::VL_STATUSBAR:
			{
				PIXVAL status_color = veh->get_vehicle_status_color();
				if (veh->is_available_only_as_upgrade()) {
					if (status_color == SYSCOL_OBSOLETE) {
						status_color = color_idx_to_rgb(COL_DARK_PURPLE);
					}
					else {
						status_color = SYSCOL_UPGRADEABLE;
					}
				}

				scr_coord_val xoff= offset.x + D_H_SPACE;
				display_veh_form_wh_clip_rgb(xoff, offset.y + D_GET_CENTER_ALIGN_OFFSET(VEHICLE_BAR_HEIGHT, height-1), VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT, status_color, true, false, veh->get_basic_constraint_prev(), veh->get_interactivity());
				xoff += VEHICLE_BAR_HEIGHT * 2;
				display_veh_form_wh_clip_rgb(xoff, offset.y + D_GET_CENTER_ALIGN_OFFSET(VEHICLE_BAR_HEIGHT, height-1), VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT, status_color, true, true, veh->get_basic_constraint_next(), veh->get_interactivity());

				const uint8 upgradable_state = veh->has_available_upgrade(month);
				if( vehiclelist_frame_t::filter_flag&vehiclelist_frame_t::VL_FILTER_UPGRADABLE  &&  upgradable_state && skinverwaltung_t::upgradable ) {
					if (world()->get_settings().get_show_future_vehicle_info() || (!world()->get_settings().get_show_future_vehicle_info() && veh->is_future(month) != 2)) {
						xoff = xoff + VEHICLE_BAR_HEIGHT * 2 - D_FIXED_SYMBOL_WIDTH;
						display_color_img(skinverwaltung_t::upgradable->get_image_id(upgradable_state - 1), xoff, offset.y + height - D_FIXED_SYMBOL_WIDTH, 0, false, false);
					}
				}
				break;
			}
			case vehiclelist_frame_t::VL_COST:
			{
				char tmp[ 128 ];
				money_to_string( tmp, veh->get_value() / 100.0, false );
				buf.printf("%8s", tmp);
				display_proportional_rgb( offset.x+ vehiclelist_frame_t::cell_width[col]-D_H_SPACE, offset.y+text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				break;
			}
			case vehiclelist_frame_t::VL_ENGINE_TYPE:
			{
				char str[ 256 ];
				const uint8 et = (uint8)veh->get_engine_type() + 1;
				if( et ) {
					sprintf( str, "%s", translator::translate( vehicle_builder_t::engine_type_names[et] ) );
					display_proportional_rgb( offset.x+D_H_SPACE, offset.y+text_offset_y, str, ALIGN_LEFT|DT_CLIP, text_color, false );
				}
				break;
			}
			case vehiclelist_frame_t::VL_POWER:
				if (veh->get_power() > 0) {
					buf.printf("%4d kW", veh->get_power());
					display_proportional_rgb(offset.x + vehiclelist_frame_t::cell_width[col] - D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				}
				break;

			case vehiclelist_frame_t::VL_TRACTIVE_FORCE:
				if (veh->get_power() > 0) {
					buf.printf("%3d kN", veh->get_tractive_effort());
					display_proportional_rgb(offset.x + vehiclelist_frame_t::cell_width[col] - D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				}
				break;

			case vehiclelist_frame_t::VL_FREIGHT_TYPE:
				if( veh->get_total_capacity() || veh->get_overcrowded_capacity() ){
					const uint8 number_of_classes = goods_manager_t::get_classes_catg_index( veh->get_freight_type()->get_catg_index() );
					// owned class indicator
					if( number_of_classes>1 ) {
						int yoff = D_GET_CENTER_ALIGN_OFFSET((D_FIXED_SYMBOL_WIDTH+veh->get_accommodations()*3), height-1);
						display_color_img(veh->get_freight_type()->get_catg_symbol(), offset.x + D_H_SPACE, offset.y + yoff, 0, false, false);
						yoff += D_FIXED_SYMBOL_WIDTH+1;
						for (uint8 a_class=0; a_class<number_of_classes; ++a_class ) {
							if( veh->get_capacity(a_class) ) {
								int xoff = (vehiclelist_frame_t::cell_width[col]>>1)-(int)(a_class*3/2)-1;
								for( uint8 n=0; n<a_class+1; ++n ){
									display_fillbox_wh_clip_rgb(offset.x+xoff, offset.y+yoff, 2, 2, SYSCOL_CLASS_INSIGNIA, false);
									xoff += 3;
								}
								yoff+=3;
								if (yoff>height) {
									break;
								}
							}
						}
					}
					else {
						display_color_img(veh->get_freight_type()->get_catg_symbol(),offset.x + D_H_SPACE, offset.y+ D_GET_CENTER_ALIGN_OFFSET(D_FIXED_SYMBOL_WIDTH, height-1),0, false, false);
					}
				}
				break;

			case vehiclelist_frame_t::VL_CAPACITY:
			{
				int yoff = text_offset_y;
				if (!veh->get_total_capacity() && !veh->get_overcrowded_capacity() ) {
					buf.append("-");
					text_color = SYSCOL_TEXT_WEAK;
				}
				else if(veh->get_total_capacity() && veh->get_overcrowded_capacity()) {
					// display two lines
					buf.printf("%4d", veh->get_total_capacity());
					display_proportional_rgb(offset.x + (vehiclelist_frame_t::cell_width[col]>>1), offset.y + D_V_SPACE, buf, ALIGN_CENTER_H | DT_CLIP, text_color, false);
					buf.clear();
					yoff = LINEASCENT+D_V_SPACE*2;
					buf.printf("(%d)", veh->get_overcrowded_capacity());
				}
				else {
					if (veh->get_total_capacity()) {
						buf.printf("%4d", veh->get_total_capacity());
					}
					if (veh->get_overcrowded_capacity()) {
						buf.printf("(%d)", veh->get_overcrowded_capacity());
					}
				}
				display_proportional_rgb(offset.x + (vehiclelist_frame_t::cell_width[col]>>1), offset.y + yoff, buf, ALIGN_CENTER_H | DT_CLIP, text_color, false);
				break;
			}
			case vehiclelist_frame_t::VL_SPEED:
				buf.printf("%3d km/h", veh->get_topspeed());
				display_proportional_rgb(offset.x + vehiclelist_frame_t::cell_width[col] - D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				break;
			case vehiclelist_frame_t::VL_WEIGHT:
				if (veh->get_weight()) {
					buf.printf("%4.1ft", veh->get_weight() / 1000.0);
				}
				else {
					buf.append("-");
					text_color = SYSCOL_TEXT_WEAK;
				}
				display_proportional_rgb(offset.x + vehiclelist_frame_t::cell_width[col] - D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				break;
			case vehiclelist_frame_t::VL_AXLE_LOAD:
				if (veh->get_waytype() != water_wt) {
					buf.printf("%2it", veh->get_axle_load());
					display_proportional_rgb(offset.x + vehiclelist_frame_t::cell_width[col] - D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_RIGHT | DT_CLIP, text_color, false);
				}
				break;
			case vehiclelist_frame_t::VL_INTRO_DATE:
				buf.append( translator::get_short_date(veh->get_intro_year_month()/12, veh->get_intro_year_month()%12) );
				display_proportional_rgb(offset.x + D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_LEFT | DT_CLIP, text_color, false);
				break;
			case vehiclelist_frame_t::VL_RETIRE_DATE:
				if (veh->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
					(((!world()->get_settings().get_show_future_vehicle_info() && veh->will_end_prodection_soon(world()->get_timeline_year_month()))
						|| world()->get_settings().get_show_future_vehicle_info()
						|| veh->is_retired(world()->get_timeline_year_month()))))
				{
					buf.append( translator::get_short_date(veh->get_retire_year_month()/12, veh->get_retire_year_month()%12) );
				}
				display_proportional_rgb(offset.x + D_H_SPACE, offset.y + text_offset_y, buf, ALIGN_LEFT | DT_CLIP, text_color, false);
				break;

			default:
				break;
		}
		offset.x += vehiclelist_frame_t::cell_width[col];
	}
}


bool vehiclelist_stats_t::infowin_event(const event_t *ev)
{
	if(  IS_LEFTRELEASE(ev)  &&  getroffen( ev->mouse_pos+pos )  ) {
		vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail));
		if (!win) {
			create_win(new vehicle_detail_t(veh), w_info, magic_vehicle_detail);
		}
		else {
			win->set_vehicle(veh);
			top_win(win, false);
		}
	}
	return false;
}


const char *vehiclelist_stats_t::get_text() const
{
	return translator::translate( veh->get_name() );
}

scr_size vehiclelist_stats_t::get_size() const
{
	return scr_size(D_MARGINS_X + vehiclelist_frame_t::stats_width, height);
}

bool vehiclelist_stats_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	if (reverse) {
		return vehicle_builder_t::compare_vehicles(dynamic_cast<const vehiclelist_stats_t*>(bb)->veh, dynamic_cast<const vehiclelist_stats_t*>(aa)->veh, (vehicle_builder_t::sort_mode_t)vehiclelist_stats_t::sort_mode);
	}
	return vehicle_builder_t::compare_vehicles( dynamic_cast<const vehiclelist_stats_t*>(aa)->veh, dynamic_cast<const vehiclelist_stats_t*>(bb)->veh, (vehicle_builder_t::sort_mode_t)vehiclelist_stats_t::sort_mode );
}


vehiclelist_frame_t::vehiclelist_frame_t() :
	gui_frame_t( translator::translate("vh_title") ),
	scrolly(gui_scrolled_list_t::windowskin, vehiclelist_stats_t::compare),
	scrollx_tab(&cont_list_table,true,false)
{
	last_name_filter[0] = 0;
	scrolly.set_cmp( vehiclelist_stats_t::compare );
	// Scrolling in x direction should not be possible due to fixed header
	scrolly.set_show_scroll_x(false);

	// init table sort buttons
	for (uint8 i=0; i<VL_MAX_SPECS; ++i) {
		bt_table_sort[i].set_text(translator::translate(vl_header_text[i]));
		bt_table_sort[i].add_listener(this);
	}

	// calculate table cell width
	cell_width[VL_IMAGE] = max(cell_width[VL_IMAGE], bt_table_sort[VL_IMAGE].get_min_size().w);
	cell_width[VL_STATUSBAR] = VEHICLE_BAR_HEIGHT*4+D_H_SPACE*2+1;
	cell_width[VL_COST] = max(proportional_string_width("88,888,888$"), bt_table_sort[VL_COST].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_ENGINE_TYPE] = 0; // need initialization for refresh
	for (uint8 i = 0; i < 11; i++) {
		cell_width[VL_ENGINE_TYPE]=max(cell_width[VL_ENGINE_TYPE], proportional_string_width( translator::translate( vehicle_builder_t::engine_type_names[i] )));
	}
	cell_width[VL_ENGINE_TYPE] = max(cell_width[VL_ENGINE_TYPE], bt_table_sort[VL_ENGINE_TYPE].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_ENGINE_TYPE] += D_H_SPACE*2+1;

	cell_width[VL_POWER] = max(proportional_string_width("8,888 kW"), bt_table_sort[VL_POWER].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_TRACTIVE_FORCE] = max(proportional_string_width("8,888 kN"), bt_table_sort[VL_TRACTIVE_FORCE].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_FREIGHT_TYPE] = D_FIXED_SYMBOL_WIDTH + D_H_SPACE*2+1;
	cell_width[VL_CAPACITY] = max(proportional_string_width("(8888)"), bt_table_sort[VL_CAPACITY].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_SPEED] = max(proportional_string_width("8,888 km/h"), bt_table_sort[VL_SPEED].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_WEIGHT] = max(proportional_string_width("888.8t"), bt_table_sort[VL_WEIGHT].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_AXLE_LOAD] = max(proportional_string_width("888t"), bt_table_sort[VL_AXLE_LOAD].get_min_size().w) + D_H_SPACE*2+1;
	cell_width[VL_INTRO_DATE] = 0; // need initialization for refresh
	for (uint8 i = 0; i < 11; i++) {
		cell_width[VL_INTRO_DATE] = max(cell_width[VL_INTRO_DATE], proportional_string_width(translator::get_short_month_name(i)));
	}
	cell_width[VL_INTRO_DATE] += proportional_string_width("8888 ");
	if (translator::translate("YEAR_SYMBOL")!="" &&  translator::translate("YEAR_SYMBOL") != "YEAR_SYMBOL") {
		cell_width[VL_INTRO_DATE] += proportional_string_width(translator::translate("YEAR_SYMBOL"));
	}
	cell_width[VL_RETIRE_DATE] = cell_width[VL_INTRO_DATE];
	cell_width[VL_INTRO_DATE] = max(cell_width[VL_INTRO_DATE], bt_table_sort[VL_INTRO_DATE].get_min_size().w) + D_H_SPACE * 2 + 1;
	cell_width[VL_RETIRE_DATE] = max(cell_width[VL_RETIRE_DATE], bt_table_sort[VL_RETIRE_DATE].get_min_size().w) + D_H_SPACE * 2 + D_SCROLLBAR_WIDTH + 1; // need scrollbar space

	cont_list_table.set_margin(scr_size(0,0), scr_size(0,0));
	cont_list_table.set_spacing(scr_size(0,0));
	cont_list_table.set_table_layout(1,0);
	cont_list_table.add_table(VL_MAX_SPECS+1,1)->set_spacing(scr_size(0,0));
	{
		cont_list_table.new_component<gui_margin_t>(D_H_SPACE<<1);
		for( uint8 i=0; i<VL_MAX_SPECS; ++i ) {
			bt_table_sort[i].set_width(cell_width[i]);
			cont_list_table.add_component(&bt_table_sort[i]);
		}
	}
	cont_list_table.end_table();
	cont_list_table.add_component(&scrolly);

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
		filter_flag = 0;
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
		bt_show_name.pressed = !side_view_mode;
		bt_show_side_view.pressed = side_view_mode;
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
	scrolly.set_maximize(true);
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
		side_view_mode = false;
		fill_list();
	}
	else if( comp == &bt_show_side_view) {
		bt_show_name.pressed      = false;
		bt_show_side_view.pressed = true;
		side_view_mode = true;
		fill_list();
	}
	else {
		for( uint8 i=0; i<VL_MAX_STATUS_FILTER; ++i ) {
			if(  comp==&bt_timeline_filters[i]  ) {
				bt_timeline_filters[i].pressed ^= 1;
				fill_list();
				return true;
			}
		}

		for( uint8 i=0; i<VL_MAX_SPECS; ++i ) {
			if (comp == &bt_table_sort[i]) {
				bt_table_sort[i].pressed = true;
				if (vehiclelist_stats_t::sort_mode == col_to_sort_mode[i]) {
					vehiclelist_stats_t::reverse = !vehiclelist_stats_t::reverse;
					bt_table_sort[i].set_reverse(vehiclelist_stats_t::reverse);
				}
				else {
					vehiclelist_stats_t::reverse = false;
					bt_table_sort[i].set_reverse();
				}
				vehiclelist_stats_t::sort_mode = col_to_sort_mode[i];
				fill_list();
			}
			else {
				bt_table_sort[i].set_reverse();
				bt_table_sort[i].pressed = false;
			}
		}
	}
	return true;
}


void vehiclelist_frame_t::fill_list()
{
	uint16 status_counts[VL_MAX_STATUS_FILTER] = {};
	if( engine_filter.get_selection()==0 ) {
		vehiclelist_frame_t::filter_flag &= ~vehiclelist_frame_t::VL_FILTER_FUEL;
	}
	else {
		vehiclelist_frame_t::filter_flag |= vehiclelist_frame_t::VL_FILTER_FUEL;
	}
	if( ware_filter.get_selection()==0 ) {
		vehiclelist_frame_t::filter_flag &= ~vehiclelist_frame_t::VL_FILTER_FREIGHT;
	}
	else {
		vehiclelist_frame_t::filter_flag |= vehiclelist_frame_t::VL_FILTER_FREIGHT;
	}
	if( bt_upgradable.pressed ) {
		vehiclelist_frame_t::filter_flag |= vehiclelist_frame_t::VL_FILTER_UPGRADABLE;
	}
	else {
		vehiclelist_frame_t::filter_flag &= ~vehiclelist_frame_t::VL_FILTER_UPGRADABLE;
	}

	scrolly.clear_elements();
	strcpy(last_name_filter, name_filter);
	count = 0;
	MAX_IMG_WIDTH = 32; // reset col1 width
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
				scrolly.new_component<vehiclelist_stats_t>( veh );
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
			scrolly.new_component<vehiclelist_stats_t>( veh );
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

	for( uint8 i=0; i<VL_MAX_STATUS_FILTER; ++i ) {
		if( bt_timeline_filters[i].pressed ){
			sprintf(status_counts_text[i], "%u", status_counts[i]);
			bt_timeline_filters[i].set_text(status_counts_text[i]);
		}
		else {
			bt_timeline_filters[i].set_text("off");
		}
	}

	for( uint8 i=0; i<VL_MAX_SPECS; ++i ) {
		bt_table_sort[i].set_width(cell_width[i]);
	}

	// recalc stats table width
	stats_width = 0;
	for (uint i = 0; i < VL_MAX_SPECS; ++i) {
		stats_width+=cell_width[i];
	}
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
	file->rdwr_bool(vehiclelist_stats_t::reverse);
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
