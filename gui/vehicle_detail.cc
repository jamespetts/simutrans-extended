/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "vehicle_detail.h"
#include "../simworld.h"
#include "../bauer/goods_manager.h"
#include "../descriptor/vehicle_desc.h"


gui_vehicle_capacity_t::gui_vehicle_capacity_t(const vehicle_desc_t *veh_type)
{
	set_table_layout(1,0);
	if (veh_type->get_total_capacity() > 0 || veh_type->get_overcrowded_capacity() > 0) {
		// total capacity
		add_table(3,1);
		{
			new_component<gui_label_t>("Capacity:");
			new_component<gui_image_t>(veh_type->get_freight_type()->get_catg_symbol(), 0, ALIGN_CENTER_V, true);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf(" %3d", veh_type->get_total_capacity());
			if (veh_type->get_overcrowded_capacity()) {
				lb->buf().printf(" (%d)", veh_type->get_overcrowded_capacity());
			}
			lb->buf().printf("%s %s", translator::translate(veh_type->get_freight_type()->get_mass()), translator::translate(veh_type->get_freight_type()->get_catg_name()));
			lb->update();
		}
		end_table();

		// compartment info
		const uint8 catg_index = veh_type->get_freight_type()->get_catg_index();
		if( catg_index==goods_manager_t::INDEX_PAS  ||  goods_manager_t::get_classes_catg_index(catg_index)>1 ) {
			// | margin | capacity | accommo rank | accommo name | comfort(+tooltip) |
			add_table(5,0)->set_spacing(scr_size(D_H_SPACE,1));
			{
				for( uint8 ac=0; ac<goods_manager_t::get_classes_catg_index(catg_index); ac++ ) {
					if( veh_type->get_capacity(ac)>0 ) {
						new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
						lb->buf().printf("%3u", veh_type->get_capacity(ac));
						lb->set_fixed_width(proportional_string_width("8888"));
						lb->update();
						lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
						lb->buf().printf("[%u]", ac + 1);
						lb->set_fixed_width(proportional_string_width("[8]"));
						lb->update();
						lb = new_component<gui_label_buf_t>();
						lb->buf().append(translator::translate(veh_type->get_accommodation_name(ac)));
						lb->update();
						// comfort
						if( catg_index==goods_manager_t::INDEX_PAS ) {
							add_table(2,1);
							{
								char time_str[32];
								world()->sprintf_time_secs(time_str, 32, world()->get_settings().max_tolerable_journey(veh_type->get_comfort(ac)));
								if( skinverwaltung_t::comfort ) {
									new_component<gui_image_t>(skinverwaltung_t::comfort->get_image_id(0), 0, ALIGN_NONE, true);
									lb = new_component<gui_label_buf_t>();
									lb->buf().append(time_str);
									lb->update();
								}
								else {
									lb = new_component<gui_label_buf_t>();
									lb->buf().printf("%s %s", translator::translate("(Max. comfortable journey time: "), time_str);
									lb->update();
								}
							}
							end_table();
						}
						else {
							new_component<gui_empty_t>();
						}
					}
				}

				if (veh_type->get_overcrowded_capacity()) {
					new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH-D_H_SPACE);
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%3u", veh_type->get_overcrowded_capacity());
					lb->set_fixed_width(proportional_string_width("8888"));
					lb->update();
					new_component<gui_image_t>(SYMBOL_OVERCROWDING, 0, ALIGN_CENTER_V, true);
					new_component<gui_label_t>("overcrowded_capacity");
					new_component<gui_empty_t>();
				}
			}
			end_table();
		}
		else if (veh_type->get_mixed_load_prohibition()) {
			add_table(3,1)->set_spacing(scr_size(D_H_SPACE,1));
			{
				new_component<gui_margin_t>(D_FIXED_SYMBOL_WIDTH - D_H_SPACE);
				new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(1) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
				new_component<gui_label_t>("(mixed_load_prohibition)", SYSCOL_MIXLOAD_PROHIBITION);
			}
			end_table();
		}
	}
	else {
		new_component_span<gui_label_t>("this_vehicle_carries_no_good", SYSCOL_TEXT_WEAK, 2);
	}
}


vehicle_detail_t::vehicle_detail_t(const vehicle_desc_t *v) :
	gui_frame_t("")
{
	veh = v;

	gui_frame_t::set_name("vehicle_details");
	init_table();
}


void vehicle_detail_t::init_table()
{
	if (veh == NULL) {
		destroy_win(this);
	}
	remove_all();
	set_table_layout(1,0);
	new_component<gui_label_t>(veh->get_name());
	new_component<gui_image_t>(veh->get_image_id(ribi_t::dir_south, veh->get_freight_type()), 0, ALIGN_NONE, true);
	reset_min_windowsize();
}

bool vehicle_detail_t::action_triggered(gui_action_creator_t*, value_t)
{
	return true;
}
