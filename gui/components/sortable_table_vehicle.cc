/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Vehicle info including availability
 */

#include "sortable_table_vehicle.h"
#include "gui_colorbox.h"

#include "../../simworld.h"
#include "../../simcolor.h"

#include "../../bauer/goods_manager.h"
#include "../../bauer/vehikelbauer.h"
#include "../../descriptor/vehicle_desc.h"

#include "../../dataobj/translator.h"
#include "../../display/simgraph.h"
#include "../../player/simplay.h"

#include "../../utils/simstring.h"

#include "../gui_theme.h"
#include "../vehicle_detail.h"
#include "../../descriptor/intro_dates.h"

int vehicle_desc_row_t::sort_mode = vehicle_desc_row_t::VD_SPEED;
bool vehicle_desc_row_t::sortreverse = false;
bool vehicle_desc_row_t::side_view_mode = true;
uint8 vehicle_desc_row_t::filter_flag = 0;

void display_insignia_dot(scr_coord_val xp, scr_coord_val yp, PIXVAL color= SYSCOL_CLASS_INSIGNIA, bool dirty=false)
{
	if (LINESPACE<18) {
		display_fillbox_wh_clip_rgb(xp, yp, 2, 2, color, dirty);
	}
	else if (LINESPACE<24) {
		display_fillbox_wh_clip_rgb(xp+1, yp, 1, 3, color, dirty);
		display_fillbox_wh_clip_rgb(xp, yp+1, 3, 1, color, dirty);
	}
	else {
		display_fillbox_wh_clip_rgb(xp + 1, yp, 2, 4, color, dirty);
		display_fillbox_wh_clip_rgb(xp, yp + 1, 4, 2, color, dirty);
	}
}


vehicle_side_image_cell_t::vehicle_side_image_cell_t(image_id img, const char* text_)
	: text_cell_t(text_)
{
	side_image = img;
	scr_coord_val x, y, w, h;
	display_get_base_image_offset(side_image, &x, &y, &w, &h);
	min_size = scr_size(w, h);
	set_size(min_size);
	remove_offset = scr_coord(x, y);
}

void vehicle_side_image_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	display_base_img(side_image, offset.x - remove_offset.x+ draw_offset.x, offset.y - remove_offset.y + draw_offset.y, world()->get_active_player_nr(), false, true);
}


vehicle_class_cell_t::vehicle_class_cell_t(const vehicle_desc_t* desc) : values_cell_t((sint64)desc->get_freight_type()->get_catg_index(), 0)
{
	align=centered;
	min_size = scr_size(D_FIXED_SYMBOL_WIDTH+D_H_SPACE, D_FIXED_SYMBOL_WIDTH);
	if (!desc->get_total_capacity() && !desc->get_overcrowded_capacity()) {
		// no capacity => nothing to draw
		veh_type=NULL;
		value=0; sub_value=0;
	}
	else {
		veh_type=desc;
		if (veh_type->get_freight_type()->get_number_of_classes() > 1) {
			// need to draw insignia dots => calculate size
			const scr_coord_val insignia_dot_size = (LINESPACE<18) ? 2: (LINESPACE<24) ? 3 : 4;
			uint accommodations=0;
			for (uint8 ac=0; ac < veh_type->get_freight_type()->get_number_of_classes(); ac++) {
				if (veh_type->get_capacity(ac)) {
					accommodations++;
				}
			}
			sub_value = veh_type->get_max_accommodation_class();

			min_size.h = D_FIXED_SYMBOL_WIDTH + accommodations * insignia_dot_size *1.5;
			min_size.w = max(D_FIXED_SYMBOL_WIDTH, veh_type->get_max_accommodation_class() * (int)(insignia_dot_size * 1.5));
		}
	}
	set_size(min_size);
}

void vehicle_class_cell_t::set_width(scr_coord_val new_width)
{
	size.w = new_width + L_CELL_PADDING * 2;
	draw_offset.x = (size.w - D_FIXED_SYMBOL_WIDTH) / 2;
}

void vehicle_class_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);
	if (veh_type) {
		offset += pos;
		display_color_img(veh_type->get_freight_type()->get_catg_symbol(), offset.x + draw_offset.x, offset.y + draw_offset.y, 0, false, false);
		const uint8 number_of_classes = veh_type->get_freight_type()->get_number_of_classes();
		if (number_of_classes > 1) {
			// draw dots
			const scr_coord_val insignia_dot_size = (LINESPACE<18) ? 2: (LINESPACE<24) ? 3 : 4;
			scr_coord_val yoff = D_FIXED_SYMBOL_WIDTH + insignia_dot_size;
			for (uint8 a_class = 0; a_class < number_of_classes; ++a_class) {
				if (veh_type->get_capacity(a_class)) {
					int xoff = -((insignia_dot_size *(a_class+1) + insignia_dot_size /2*a_class))/2;
					for (uint8 n = 0; n < a_class + 1; ++n) {
						display_insignia_dot(offset.x + size.w / 2 + xoff, offset.y + draw_offset.y + yoff);
						xoff += (int)(insignia_dot_size *1.5);
					}
					yoff += (int)(insignia_dot_size * 1.5);
				}
			}
		}
	}
}


engine_type_cell_t::engine_type_cell_t(vehicle_desc_t::engine_t et) :
	value_cell_t((sint64)et, gui_chart_t::STANDARD)
{
	uint8 engine_type = (uint8)et + 1;
	value= engine_type;
	buf.clear();
	if (engine_type) {
		buf.append(vehicle_builder_t::engine_type_names[engine_type]);
	}
	else {
		buf.append("-");
		align = centered;
		color = COL_INACTIVE;
	}
	min_size = scr_size(proportional_string_width(buf), LINESPACE);
	set_size(min_size);
}


vehicle_status_bar_t::vehicle_status_bar_t(const vehicle_desc_t* desc)
	: values_cell_t((sint64)desc->get_basic_constraint_prev(), 0)
{
	veh_type=desc;
	align = centered;
	min_size = scr_size(VEHICLE_BAR_HEIGHT*4, VEHICLE_BAR_HEIGHT);
	sub_value= (sint64)veh_type->get_basic_constraint_next(); // Substitute here to avoid making the size two lines.
	set_size(min_size);

	l_color = veh_type->get_basic_constraint_prev() & vehicle_desc_t::intermediate_unique ? COL_CAUTION : veh_type->get_vehicle_status_color();
	r_color = veh_type->get_basic_constraint_next() & vehicle_desc_t::intermediate_unique ? COL_CAUTION : veh_type->get_vehicle_status_color();
}

void vehicle_status_bar_t::set_width(scr_coord_val new_width)
{
	size.w = new_width + L_CELL_PADDING * 2;
	draw_offset.x = size.w/2- min_size.w/2;
}

void vehicle_status_bar_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	offset += pos;
	display_veh_form_wh_clip_rgb(offset.x + draw_offset.x, offset.y +draw_offset.y, VEHICLE_BAR_HEIGHT * 2, VEHICLE_BAR_HEIGHT, l_color, false, false, veh_type->get_basic_constraint_prev(), veh_type->get_interactivity());
	display_veh_form_wh_clip_rgb(offset.x + draw_offset.x+min_size.w/2, offset.y +draw_offset.y, VEHICLE_BAR_HEIGHT * 2, VEHICLE_BAR_HEIGHT, r_color, false, true, veh_type->get_basic_constraint_next(), veh_type->get_interactivity());
}


vehicle_desc_row_t::vehicle_desc_row_t(const vehicle_desc_t* desc)
{
	veh_type = desc;
	tooltip_buf.printf(veh_type->get_name());

	// 1. image/name
	if (side_view_mode) {
		new_component<vehicle_side_image_cell_t>(veh_type->get_image_id(ribi_t::dir_southwest, goods_manager_t::none), veh_type->get_name());
	}
	else {
		new_component<text_cell_t>(veh_type->get_name());
	}
	// 2. color bar
	new_component<vehicle_status_bar_t>(veh_type);
	// 3. value
	new_component<value_cell_t>(veh_type->get_value(), gui_chart_t::MONEY, table_cell_item_t::right);
	// 4. enigine type
	new_component<engine_type_cell_t>(veh_type->get_engine_type());
	// 5. power
	new_component<value_cell_t>((sint64)veh_type->get_power(), gui_chart_t::KW, table_cell_item_t::right);
	// 6. force
	new_component<value_cell_t>((sint64)veh_type->get_tractive_effort(), gui_chart_t::FORCE, table_cell_item_t::right);
	// 7. speed
	new_component<value_cell_t>((sint64)veh_type->get_topspeed(), gui_chart_t::KMPH, table_cell_item_t::right);
	// 8. catg
	new_component<vehicle_class_cell_t>(veh_type);
	// 9. capacity
	new_component<values_cell_t>((sint64)veh_type->get_total_capacity(), veh_type->get_overcrowded_capacity());
	// 10. weight
	new_component<value_cell_t>((sint64)veh_type->get_weight(), gui_chart_t::TONNEN, table_cell_item_t::right);
	// 11. axle load
	new_component<value_cell_t>(veh_type->get_waytype() == water_wt ? 0 : (sint64)veh_type->get_axle_load() * 1000, gui_chart_t::TONNEN, table_cell_item_t::right);
	// 12. intro date
	PIXVAL status_color = veh_type->get_vehicle_status_color();
	new_component<value_cell_t>((sint64)veh_type->get_intro_year_month(), gui_chart_t::DATE, table_cell_item_t::right, status_color == COL_SAFETY ? SYSCOL_TEXT : status_color);
	// 13. retire date
	sint64 retire_date= DEFAULT_RETIRE_DATE * 12;
	if (veh_type->get_retire_year_month() != DEFAULT_RETIRE_DATE * 12 &&
		(((!world()->get_settings().get_show_future_vehicle_info() && veh_type->will_end_prodection_soon(world()->get_timeline_year_month()))
			|| world()->get_settings().get_show_future_vehicle_info()
			|| veh_type->is_retired(world()->get_timeline_year_month()))))
	{
		retire_date = (sint64)veh_type->get_retire_year_month();

	}
	new_component<value_cell_t>(retire_date, gui_chart_t::DATE, table_cell_item_t::right);

	//init cell height
	for (auto& cell : owned_cells) {
		cell->set_height(row_height);
	}
}

void vehicle_desc_row_t::update_highlight()
{
	old_filter_flag=filter_flag;
	set_highlight(VD_ENGINE_TYPE, filter_flag&VL_FILTER_FUEL);
	set_highlight(VD_FREIGHT_TYPE, filter_flag&VL_FILTER_FREIGHT);
	set_highlight(VD_STATUSBAR, filter_flag&VL_FILTER_UPGRADABLE);
}

bool vehicle_desc_row_t::infowin_event(const event_t* ev)
{
	bool swallowed = gui_scrolled_list_t::scrollitem_t::infowin_event(ev);
	if (!swallowed && veh_type && IS_RIGHTRELEASE(ev)) {
		vehicle_detail_t* win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail));
		if (!win) {
			create_win(new vehicle_detail_t(veh_type), w_info, magic_vehicle_detail);
		}
		else {
			win->set_vehicle(veh_type);
			top_win(win, false);
		}
		return false;
	}
	return swallowed;
}

void vehicle_desc_row_t::draw(scr_coord offset)
{
	if (filter_flag != old_filter_flag) {
		update_highlight();
	}
	gui_sort_table_row_t::draw(offset);
	// show tooltip
	if (getroffen(get_mouse_pos() - offset)) {
		win_set_tooltip(get_mouse_pos() + TOOLTIP_MOUSE_OFFSET, tooltip_buf, this);
	}
}


bool vehicle_desc_row_t::compare(const gui_component_t* aa, const gui_component_t* bb)
{
	const vehicle_desc_row_t* row_a = dynamic_cast<const vehicle_desc_row_t*>(aa);
	const vehicle_desc_row_t* row_b = dynamic_cast<const vehicle_desc_row_t*>(bb);
	if (row_a == NULL || row_b == NULL) {
		dbg->warning("vehicle_desc_row_t::compare()", "row data error");
		return false;
	}

	const table_cell_item_t* a = row_a->get_element(sort_mode);
	const table_cell_item_t* b = row_b->get_element(sort_mode);
	if (a == NULL || b == NULL) {
		dbg->warning("vehicle_desc_row_t::compare()", "Could not get table_cell_item_t successfully");
		return false;
	}

	int cmp = gui_sort_table_row_t::compare(a, b);
	return sortreverse ? cmp < 0 : cmp > 0; // Do not include 0
}
