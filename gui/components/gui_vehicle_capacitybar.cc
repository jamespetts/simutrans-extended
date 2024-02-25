/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_vehicle_capacitybar.h"

#include "gui_label.h"
#include "gui_colorbox.h"
#include "gui_image.h"
#include "../../bauer/goods_manager.h"
#include "../../dataobj/translator.h"


void gui_convoy_loading_info_t::update_list()
{
	remove_all();
	if( cnv.is_bound() || line.is_bound() ) {
		// update factors
		if (cnv.is_bound()) {
			old_vehicle_count = cnv->get_vehicle_count();
			old_reversed = cnv->is_reversed();
			old_weight = cnv->get_weight_summary().weight;
		}

		const minivec_tpl<uint8> &goods_catg_index = cnv.is_bound() ? cnv->get_goods_catg_index() : line->get_goods_catg_index();

		add_table(4+show_loading,0);
		{
			for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++)
			{
				if (!goods_catg_index.is_contained(catg_index)) {
						continue;
				}
				const uint8 g_classes = goods_manager_t::get_classes_catg_index(catg_index);

				bool is_lowest_class = true; // for display catgroy symbol
				for (uint8 i = 0; i < g_classes; i++) {
					const goods_desc_t* ware = goods_manager_t::get_info_catg_index(catg_index);
					const uint16 capacity = get_unique_fare_capacity(catg_index,i);
					if (!capacity) {
						continue;
					}
					// 1: goods category symbol
					if (is_lowest_class) {
						new_component<gui_image_t>(ware->get_catg_symbol(), 0, ALIGN_CENTER_V, true);
						is_lowest_class = false;
					}
					else {
						new_component<gui_empty_t>();
					}

					// 2: fare class name / category name
					if (g_classes>1) {
						// "fare" class name
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
						lb->buf().printf("%s", ( goods_manager_t::get_translated_fare_class_name(catg_index,i) ));
						lb->update();
					}
					else {
						// no classes => show the category name
						new_component<gui_label_t>(ware->get_catg_name());
					}

					// 3: capacity text
					const uint16 cargo_sum = get_total_cargo_by_fare_class(catg_index,i);
					uint16 overcrowded_capacity = 0;
					if (catg_index==goods_manager_t::INDEX_PAS) {
						overcrowded_capacity = get_overcrowded_capacity(i);
					}
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					if (show_loading) {
						lb->buf().printf("%4d/%3d", cargo_sum, capacity);
					}
					else {
						lb->buf().append(capacity,0);
					}
					if (overcrowded_capacity) {
						lb->buf().printf("(%u)", overcrowded_capacity);
					}
					lb->update();
					lb->set_fixed_width(lb->get_min_size().w);

					// 4: capacity bar
					if (show_loading) {
						add_table(1,2)->set_spacing(scr_size(0,0));
						{
							PIXVAL catg_bar_col = catg_index < goods_manager_t::INDEX_NONE ? ware->get_color() : color_idx_to_rgb(115);
							new_component<gui_capacity_bar_t>(scr_size(102, scr_coord_val(LINESPACE*0.6+2)), catg_bar_col)->set_value(capacity, min(cargo_sum, capacity));
							if (overcrowded_capacity) {
								new_component<gui_capacity_bar_t>(scr_size(102, scr_coord_val(LINESPACE>>1)), SYSCOL_OVERCROWDED)->set_value(overcrowded_capacity, get_overcrowded(i));
							}
						}
						end_table();
					}

					new_component<gui_fill_t>();
				}
			}
		}
		end_table();
	}
	set_size(get_min_size());
}

uint16 gui_convoy_loading_info_t::get_unique_fare_capacity(uint8 catg_index, uint8 g_class)
{
	if (cnv.is_bound()) {
		return cnv->get_unique_fare_capacity(catg_index, g_class);
	}
	else if (line.is_bound()) {
		return line->get_unique_fare_capacity(catg_index, g_class);
	}
	return 0;
}

uint16 gui_convoy_loading_info_t::get_overcrowded_capacity(uint8 g_class)
{
	uint16 capacity = 0;
	if (cnv.is_bound()) {
		return cnv->get_overcrowded_capacity(g_class);
	}
	else if (line.is_bound()) {
		for (uint32 i = 0; i < line->count_convoys(); i++) {
			convoihandle_t const convoy = line->get_convoy(i);
			// we do not want to count the capacity of depot convois
			if (!convoy->in_depot()) {
				capacity += convoy->get_overcrowded_capacity(g_class);
			}
		}
	}
	return capacity;
}


uint16 gui_convoy_loading_info_t::get_total_cargo_by_fare_class(uint8 catg_index, uint8 g_class)
{
	uint16 sum=0;
	if (cnv.is_bound()) {
		return cnv->get_total_cargo_by_fare_class(catg_index, g_class);
	}
	else if (line.is_bound()) {
		for (uint32 i = 0; i < line->count_convoys(); i++) {
			convoihandle_t const convoy = line->get_convoy(i);
			sum += convoy->get_total_cargo_by_fare_class(catg_index, g_class);
		}
	}
	return sum;
}

uint16 gui_convoy_loading_info_t::get_overcrowded(uint8 g_class)
{
	uint16 sum = 0;
	if (cnv.is_bound()) {
		return cnv->get_overcrowded(g_class);
	}
	else if (line.is_bound()) {
		for (uint32 i = 0; i < line->count_convoys(); i++) {
			convoihandle_t const convoy = line->get_convoy(i);
			sum += convoy->get_overcrowded(g_class);
		}
	}
	return sum;
}


gui_convoy_loading_info_t::gui_convoy_loading_info_t(linehandle_t line, convoihandle_t cnv, bool show_loading_info)
{
	this->cnv = cnv;
	this->line = line;
	show_loading = show_loading_info;
	set_table_layout(1, 0);

	update_list();
}

void gui_convoy_loading_info_t::draw(scr_coord offset)
{
	if (cnv.is_bound()) {
		if(old_weight != cnv->get_weight_summary().weight || old_vehicle_count != cnv->get_vehicle_count() || old_reversed != cnv->is_reversed()) {
			update_list();
		}
	}
	else if (line.is_bound()) {
		// UI TODO: Set sophisticated update conditions
		update_list();
	}
	gui_aligned_container_t::draw(offset);
}
