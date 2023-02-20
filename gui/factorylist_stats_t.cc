/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "factorylist_stats_t.h"
#include "../simskin.h"
#include "../simcity.h"
#include "../simfab.h"
#include "../simworld.h"
#include "../simskin.h"
#include "simwin.h"

#include "../bauer/goods_manager.h"
#include "../bauer/fabrikbauer.h"
#include "../descriptor/skin_desc.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"
#include "../display/viewport.h"


uint8 factorylist_stats_t::sort_mode = factorylist::by_name;
uint8 factorylist_stats_t::region_filter = 0;
uint8 factorylist_stats_t::display_mode = 0;
bool factorylist_stats_t::reverse = false;
bool factorylist_stats_t::filter_own_network = false;
uint8 factorylist_stats_t::filter_goods_catg = goods_manager_t::INDEX_NONE;
uint16 factorylist_stats_t::name_width = 0;

// same as halt_list_stats.cc
#define L_KOORD_LABEL_WIDTH proportional_string_width("(8888,8888)")

const uint8 severity_color[7] =
{
	COL_RED+1, COL_ORANGE_RED, COL_ORANGE, COL_YELLOW-1, 44, COL_DARK_GREEN, COL_GREEN
};

gui_combined_factory_storage_bar_t::gui_combined_factory_storage_bar_t(fabrik_t *fab, bool is_output)
{
	this->fab = fab;
	this->is_output = is_output;
	set_size(get_min_size());
}

void gui_combined_factory_storage_bar_t::draw(scr_coord offset)
{
	if( (is_output&&fab->get_output().empty())  ||  (!is_output&&fab->get_input().empty() )) {
		return;
	}
	offset += pos;

	const scr_coord_val color_bar_w = size.w-2;

	display_ddd_box_clip_rgb(offset.x, offset.y, size.w, size.h, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4));
	display_fillbox_wh_clip_rgb(offset.x + 1, offset.y+1, color_bar_w, size.h-2, color_idx_to_rgb(MN_GREY2), false);

	uint32 sum=0;
	uint32 sum_intransit = 0; // total yellowed bar
	uint32 max_capacity = is_output ? fab->get_total_output_capacity() : fab->get_total_input_capacity();
	if(max_capacity==0){
		max_capacity=1;
	}
	int i = 0;
	FORX(array_tpl<ware_production_t>, const& goods, is_output ? fab->get_output() : fab->get_input(), i++) {
		if (!is_output && !fab->get_desc()->get_supplier(i)) {
			continue;
		}
		if( world()->get_goods_list().is_contained( goods.get_typ() ) ) {
			const sint64 pfactor = is_output ? (sint64)fab->get_desc()->get_product(i)->get_factor()
				: (fab->get_desc()->get_supplier(i) ? (sint64)fab->get_desc()->get_supplier(i)->get_consumption() : 1ll);
			const uint32 storage_capacity = (uint32)goods.get_capacity(pfactor);
			const uint32 stock_quantity = min(storage_capacity,(uint32)goods.get_storage());
			if (!storage_capacity) { continue; }

			uint32 temp_sum = sum+stock_quantity;
			const PIXVAL goods_color = goods.get_typ()->get_color();
			display_cylinderbar_wh_clip_rgb(offset.x+1 + color_bar_w*sum/max_capacity, offset.y+1, color_bar_w*stock_quantity/max_capacity, size.h-2, goods_color, true);

			sum = temp_sum;
			if (!is_output) {
				sum_intransit += min(goods.get_in_transit(), storage_capacity-stock_quantity);
			}
		}
	}
	if (sum_intransit && !is_output && sum<max_capacity) {
		// in transit for input storage
		display_fillbox_wh_clip_rgb(offset.x+1+color_bar_w*sum/max_capacity, offset.y+1, color_bar_w*sum_intransit/max_capacity, size.h - 2, COL_IN_TRANSIT, false);
	}
}

gui_factory_stats_t::gui_factory_stats_t(fabrik_t *fab) :
	input_bar(fab, false),
	output_bar(fab, true)
{
	this->fab = fab;
	set_table_layout(1,0);
	set_spacing(scr_size(1,1));
	set_margin(scr_size(1,1), scr_size(0,0));

	image.set_rigid(true);
	receipt_status.set_rigid(true);
	producing_status.set_rigid(true);
	forwarding_status.set_rigid(true);

	staffing_bar.add_color_value(&staff_shortage_factor, COL_CAUTION);
	staffing_bar.add_color_value(&staffing_level, goods_manager_t::passengers->get_color());
	staffing_bar.add_color_value(&staffing_level2, SYSCOL_STAFF_SHORTAGE);
	staffing_bar.set_width(100);

	update_table();
}

void gui_factory_stats_t::update_table()
{
	remove_all();
	old_month = world()->get_current_month();
	switch (display_mode) {
		case factorylist_stats_t::fl_operation:
			add_table(6,1);
			{
				// this month and last month operation rate
				add_component(&realtime_buf);
				realtime_buf.init(SYSCOL_TEXT, gui_label_t::right);
				realtime_buf.set_tooltip(translator::translate("Operation rate"));
				realtime_buf.set_fixed_width(proportional_string_width("8888.8"));

				new_component<gui_label_t>(" - ");
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->init(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%3.1f", fab->get_stat_converted(1, FAB_PRODUCTION) / 100.0);
				lb->set_tooltip(translator::translate("Last Month"));
				lb->set_fixed_width(proportional_string_width("8888.8"));
				lb->update();

				lb = new_component<gui_label_buf_t>();
				const uint32 max_productivity = (100*(fab->get_desc()->get_electric_boost() + fab->get_desc()->get_pax_boost() + fab->get_desc()->get_mail_boost())) >> DEFAULT_PRODUCTION_FACTOR_BITS;
				lb->buf().printf("/%d%%", max_productivity + 100);
				lb->set_fixed_width(proportional_string_width("/888%"));
				lb->update();

				// electricity
				if (fab->get_desc()->get_electric_boost()) {
					image.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
					image.set_tooltip(translator::translate("Production boosted by power supply"));
					add_component(&image);
				}
				else {
					new_component<gui_empty_t>();
				}
				// staffing
				if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
					add_component(&staffing_bar);
				}
				else {
					new_component<gui_empty_t>();
				}
			}
			end_table();
			break;
		case factorylist_stats_t::fl_storage:
			add_table(7,1);
			{
				image.set_visible(false);
				receipt_status.set_visible(!fab->get_input().empty());
				forwarding_status.set_visible(!fab->get_output().empty());
				if (skinverwaltung_t::in_transit) {
					image.set_image(skinverwaltung_t::in_transit->get_image_id(0), true);
					image.set_tooltip(translator::translate("symbol_help_txt_in_transit"));
					image.set_transparent(0);
				}
				else {
					image.set_image(IMG_EMPTY);
				}
				add_component(&image);
				add_component(&realtime_buf);

				add_component(&receipt_status);

				if (!fab->get_input().empty()) {
					add_component(&input_bar);
				}
				else {
					new_component<gui_margin_t>(LINESPACE*5);
				}

				add_component(&producing_status);

				if (!fab->get_output().empty()) {
					add_component(&output_bar);
				}
				else {
					new_component<gui_empty_t>();
				}

				add_component(&forwarding_status);
			}
			end_table();
			break;

		case factorylist_stats_t::fl_goods_needed:
		case factorylist_stats_t::fl_product:
		{
			const uint8 goods_count = (display_mode==factorylist_stats_t::fl_goods_needed) ? fab->get_input().get_count() : fab->get_output().get_count();
			const uint8 storage_index = (display_mode == factorylist_stats_t::fl_goods_needed) ? 0:1;
			if (goods_count>0) {
				add_table(2,1);
				{
					new_component<gui_image_t>()->set_image(skinverwaltung_t::input_output ? skinverwaltung_t::input_output->get_image_id(storage_index) : IMG_EMPTY, true);
					add_table(goods_manager_t::get_max_catg_index()-goods_manager_t::INDEX_NONE-1,1)->set_spacing(scr_size(0,0));
					{
						uint8 goods_found = 0;
						for (uint8 catg_index = goods_manager_t::INDEX_NONE+1; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
							if (goods_found == goods_count) {
								break;
							}
							if (fab->has_goods_catg_demand(catg_index, storage_index+1)) {
								add_table(2,1);
								{
									new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), 0, ALIGN_NONE, true)->set_tooltip(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name());
									add_table(goods_count*2, 1);
									{
										FOR(array_tpl<ware_production_t>, const& material, storage_index ? fab->get_output() : fab->get_input()) {
											goods_desc_t const* const ware = material.get_typ();
											if (catg_index == ware->get_catg_index()) {
												new_component<gui_colorbox_t>(ware->get_color())->set_size(GOODS_COLOR_BOX_SIZE);
												gui_label_buf_t *lb = new_component<gui_label_buf_t>();
												lb->buf().append(translator::translate( ware->get_name() ));
												lb->buf().append(" ");
												lb->update();
												goods_found++;
											}
										}
									}
									end_table();
								}
								end_table();
							}
						}
					}
					end_table();
				}
				end_table();
			}
			else {
				new_component<gui_empty_t>();
			}
			break;
		}
		case factorylist_stats_t::fl_activity:
			// This mode is for historical statistics and does not update frequently.
			add_table(9,1);
			{
				const bool has_halt = !fab->get_nearby_freight_halts().empty();
				// Required to align the layout of the list
				const scr_coord_val text_width = proportional_string_width("88888.8t");
				const scr_coord_val hyphen_width = proportional_string_width(" - ");

				image.set_visible(false);
				receipt_status.set_visible(!fab->get_input().empty());
				forwarding_status.set_visible(!fab->get_output().empty());

				add_component(&receipt_status);
				sint32 total_in = 0;
				sint32 total_out = 0;
				if (!fab->get_input().empty()) {
					// arrival = FAB_GOODS_RECEIVED
					FOR(array_tpl<ware_production_t>, const& material, fab->get_input()) {
						goods_desc_t const* const ware = material.get_typ();
						total_in  += material.get_stat(1, FAB_GOODS_RECEIVED)*ware->get_weight_per_unit()/100;
						total_out += convert_goods(material.get_stat(1, FAB_GOODS_CONSUMED)*ware->get_weight_per_unit())/100;
					}
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(has_halt ? SYSCOL_TEXT: COL_INACTIVE, gui_label_t::right);
					lb->buf().printf("%.1ft", total_in/10.0);
					lb->set_tooltip(translator::translate("Arrived"));
					lb->set_fixed_width(text_width);
					lb->update();
					new_component<gui_label_t>(" - ");
				}
				else {
					new_component<gui_margin_t>(text_width);
					new_component<gui_margin_t>(hyphen_width);
				}

				if (!fab->get_input().empty()) {
					// consume = FAB_GOODS_CONSUMED
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%.1ft", total_out/10.0);
					lb->set_tooltip(translator::translate("Consumed"));
					lb->set_fixed_width(text_width);
					lb->update();
				}
				else {
					new_component<gui_margin_t>(text_width);
				}

				add_component(&producing_status);

				if (!fab->get_output().empty()) {
					// produced
					total_in = 0;
					total_out = 0;
					FOR(array_tpl<ware_production_t>, const& material, fab->get_output()) {
						goods_desc_t const* const ware = material.get_typ();
						total_in  += convert_goods(material.get_stat(1, FAB_GOODS_PRODUCED)*ware->get_weight_per_unit())/100;
						total_out += material.get_stat(1, FAB_GOODS_DELIVERED)*ware->get_weight_per_unit()/100;
					}
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%.1ft", total_in/10.0);
					lb->set_tooltip(translator::translate("Produced"));
					lb->set_fixed_width(text_width);
					lb->update();
					new_component<gui_label_t>(" - ");
				}
				else if( fab->get_desc()->is_electricity_producer() ) {
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
					lb->buf().printf("%iMW", convert_power(fab->get_stat(1, FAB_POWER)));
					lb->set_tooltip(translator::translate("Power supply"));
					lb->set_fixed_width(text_width);
					lb->update();
					new_component<gui_margin_t>(hyphen_width);
				}
				else {
					new_component<gui_margin_t>(text_width);
					new_component<gui_margin_t>(hyphen_width);
				}

				if (!fab->get_output().empty()) {
					// delivered
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(has_halt ? SYSCOL_TEXT : COL_INACTIVE, gui_label_t::right);
					lb->buf().printf("%.1ft", total_out/10.0);
					lb->set_tooltip(translator::translate("Delivered"));
					lb->set_fixed_width(text_width);
					lb->update();
				}
				else {
					new_component<gui_margin_t>(text_width);
				}
				add_component(&forwarding_status);
			}
			break;

		case factorylist_stats_t::fl_location:
		default:
			add_table(4,1);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->set_fixed_width(L_KOORD_LABEL_WIDTH);
			lb->buf().append(fab->get_pos().get_2d().get_fullstr());
			lb->set_align(gui_label_t::centered);
			lb->update();

			new_component<gui_label_t>("- ");

			stadt_t* c = world()->get_city(fab->get_pos().get_2d());
			if (c) {
				new_component<gui_image_t>()->set_image(skinverwaltung_t::intown->get_image_id(0), true);
			}
			else {
				new_component<gui_empty_t>();
			}
			lb = new_component<gui_label_buf_t>();
			if (c) {
				lb->buf().append(c->get_name());
			}
			if (!world()->get_settings().regions.empty()) {
				lb->buf().printf(" (%s)", translator::translate(world()->get_region_name(fab->get_pos().get_2d()).c_str()));
			}
			lb->update();
			end_table();
			break;
	}
	old_display_mode = display_mode;

	set_size(get_size());
}


void gui_factory_stats_t::update_operation_status(uint8 target_month)
{
	const bool has_halt = !fab->get_nearby_freight_halts().empty();
	const bool staff_shortage = fab->is_staff_shortage(); // ?
	if (!fab->get_input().empty()) {
		if (has_halt) {
			// receipt_status = FAB_GOODS_RECEIVED
			uint8 reciept_score = 0;
			FOR(array_tpl<ware_production_t>, const& material, fab->get_input()) {
				if (material.get_stat(target_month, FAB_GOODS_RECEIVED)) {
					// This factory is receiving materials this month.
					reciept_score += 100 / fab->get_input().get_count();
				}
				else if (material.get_stat(target_month+1, FAB_GOODS_RECEIVED)) {
					// This factory hasn't recieved this month yet, but it did last month.
					reciept_score += 50 / fab->get_input().get_count();
				}
			}
			receipt_status.set_color(color_idx_to_rgb(severity_color[(reciept_score+19)/20]));
			if (reciept_score) {
				receipt_status.set_status(gui_operation_status_t::operation_normal);
			}
			else {
				receipt_status.set_status(gui_operation_status_t::operation_stop);
			}
		}
		else {
			receipt_status.set_status(gui_operation_status_t::operation_invalid);
		}
	}

	if (!fab->get_output().empty()) {
		// forwarding_status = delivered (= FAB_GOODS_DELIVERED)
		if (has_halt) {
			uint8 forwarding_score = 0;
			// calculate forwarding_score
			FOR(array_tpl<ware_production_t>, const& product, fab->get_output()) {
				if (product.get_stat(target_month, FAB_GOODS_DELIVERED)) {
					// This factory is shipping this month.
					forwarding_score+=100/fab->get_output().get_count();
				}
				else if (product.get_stat(target_month+1, FAB_GOODS_DELIVERED)) {
					// This factory hasn't shipped this month yet, but it did last month.
					forwarding_score+=50/fab->get_output().get_count();
				}
			}
			if (staff_shortage && !forwarding_score) {
				// Is the reason why it's not working is staff shortage?
				forwarding_status.set_color(SYSCOL_STAFF_SHORTAGE);
			}
			else {
				forwarding_status.set_color(color_idx_to_rgb(severity_color[(forwarding_score+19)/20]));
			}
			forwarding_status.set_status(forwarding_score ? gui_operation_status_t::operation_normal : gui_operation_status_t::operation_stop);
		}
		else {
			forwarding_status.set_status(gui_operation_status_t::operation_invalid);
		}
	}

	if (has_halt) {
		const uint8 avtivity_score = min(6, (uint8)((fab->get_stat(target_month, FAB_PRODUCTION)+1999)/2000));
		const PIXVAL producing_status_color = color_idx_to_rgb(severity_color[avtivity_score]);
		if (staff_shortage && !avtivity_score) {
			// Is the reason why it's not working is staff shortage?
			producing_status.set_color(SYSCOL_STAFF_SHORTAGE);
		}
		else {
			producing_status.set_color(producing_status_color);
			if (avtivity_score) {
				producing_status.set_status(gui_operation_status_t::operation_normal);
			}
			else if (!fab->get_output().empty() && fab->get_total_out() == fab->get_total_output_capacity()) {
				// The output inventory is full, so it is suspending production
				producing_status.set_status(gui_operation_status_t::operation_pause);
			}
			else {
				producing_status.set_status(gui_operation_status_t::operation_stop);
			}
		}
	}
	else {
		producing_status.set_color(COL_INACTIVE);
		producing_status.set_status(gui_operation_status_t::operation_stop);
	}

}


void gui_factory_stats_t::draw(scr_coord offset)
{
	bool update_flag = false;
	display_mode = factorylist_stats_t::display_mode;
	if (old_display_mode != display_mode) {
		update_flag = true;
	}
	if( ( display_mode==factorylist_stats_t::fl_activity || display_mode==factorylist_stats_t::fl_operation)
		&& old_month != world()->get_current_month()) {
		update_flag = true;
	}

	if (update_flag) {
		update_table();
		if (display_mode == factorylist_stats_t::fl_activity) {
			update_operation_status(1);
		}
	}

	// realtime update
	switch (display_mode) {
		case factorylist_stats_t::fl_operation:
			realtime_buf.buf().printf("%3.1f", fab->get_stat_converted(0, FAB_PRODUCTION) / 100.0);
			realtime_buf.update();
			if (fab->get_desc()->get_electric_boost()) {
				if (fab->get_prodfactor_electric() > 0) {
					image.set_transparent(0);
				}
				else {
					image.set_transparent((TRANSPARENT25_FLAG | OUTLINE_FLAG | SYSCOL_TEXT));
				}
			}
			if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
				staff_shortage_factor = 0;
				if (fab->get_sector() == fabrik_t::end_consumer) {
					staff_shortage_factor = world()->get_settings().get_minimum_staffing_percentage_consumer_industry();
				}
				else if (!(world()->get_settings().get_rural_industries_no_staff_shortage() && fab->get_sector() == fabrik_t::resource)) {
					staff_shortage_factor = world()->get_settings().get_minimum_staffing_percentage_full_production_producer_industry();
				}
				staffing_level = fab->get_staffing_level_percentage();
				staffing_level2 = staff_shortage_factor > staffing_level ? staffing_level : 0;
			}


			break;
		case factorylist_stats_t::fl_storage:
		{
			if (!fab->get_input().empty()) {
				if (fab->get_total_transit()) {
					realtime_buf.buf().append(fab->get_total_transit(), 0);
					if (skinverwaltung_t::in_transit) {
						image.set_visible(true);
					}
				}
				else {
					if (skinverwaltung_t::in_transit) {
						image.set_visible(false);
					}
				}
			}
			update_operation_status();

			realtime_buf.update();
			break;
		}
		case factorylist_stats_t::fl_activity:
		default:
			break;
	}

	gui_aligned_container_t::draw(offset);
}

factorylist_stats_t::factorylist_stats_t(fabrik_t *fab)
{
	this->fab = fab;
	// pos button
	set_table_layout(4,1);
	button_t *b = new_component<button_t>();
	b->set_typ(button_t::posbutton_automatic);
	b->set_targetpos3d(fab->get_pos());
	// indicator bar
	indicator.init(COL_INACTIVE, scr_size(D_INDICATOR_WIDTH,D_INDICATOR_HEIGHT),true,false);
	add_component(&indicator);
	// factory name
	add_component(&lb_name);
	update_label();
	swichable_info = new_component<gui_factory_stats_t>(fab);
}


void factorylist_stats_t::update_label()
{
	tmp_name_width = proportional_string_width(fab->get_name()) + D_BUTTON_PADDINGS_X + D_H_SPACE;
	if (tmp_name_width > name_width) {
		name_width = tmp_name_width;
	}
	lb_name.set_color((fab->get_desc()->get_building()->get_retire_year_month() < world()->get_timeline_year_month() ) ? SYSCOL_OUT_OF_PRODUCTION : SYSCOL_TEXT);
	lb_name.set_fixed_width(name_width);
	lb_name.buf().printf("%s ", fab->get_name());
	lb_name.update();

	set_size(size);
}


void factorylist_stats_t::set_size(scr_size size)
{
	gui_aligned_container_t::set_size(size);
}


bool factorylist_stats_t::is_valid() const
{
	return world()->get_fab_list().is_contained(fab);
}


bool factorylist_stats_t::infowin_event(const event_t * ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	if(  !swallowed  &&  IS_LEFTRELEASE(ev)  ) {
		fab->show_info();
		swallowed = true;
	}
	else if (IS_RIGHTRELEASE(ev)) {
		world()->get_viewport()->change_world_position(fab->get_pos());
	}
	return swallowed;
}


void factorylist_stats_t::draw(scr_coord pos)
{
	update_label();

	indicator.set_color( color_idx_to_rgb(fabrik_t::status_to_color[fab->get_status()]) );

	if (win_get_magic((ptrdiff_t)fab)) {
		display_blend_wh_rgb(pos.x + get_pos().x, pos.y + get_pos().y, get_size().w, get_size().h, SYSCOL_TEXT_HIGHLIGHT, 30);
	}
	gui_aligned_container_t::draw(pos);
}


bool factorylist_stats_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	const factorylist_stats_t* fa = dynamic_cast<const factorylist_stats_t*>(aa);
	const factorylist_stats_t* fb = dynamic_cast<const factorylist_stats_t*>(bb);
	// good luck with mixed lists
	assert(fa != NULL  &&  fb != NULL);
	fabrik_t *a=fa->fab, *b=fb->fab;

	int cmp;
	switch (sort_mode) {
		default:
		case factorylist::by_name:
			cmp = 0;
			break;

		case factorylist::by_input:
		{
			int a_in = a->get_input().empty() ? -1 : (int)a->get_total_in();
			int b_in = b->get_input().empty() ? -1 : (int)b->get_total_in();
			cmp = a_in - b_in;
			break;
		}

		case factorylist::by_transit:
		{
			int a_transit = a->get_input().empty() ? -1 : (int)a->get_total_transit();
			int b_transit = b->get_input().empty() ? -1 : (int)b->get_total_transit();
			cmp = a_transit - b_transit;
			break;
		}

		case factorylist::by_available:
		{
			int a_in = a->get_input().empty() ? -1 : (int)(a->get_total_in()+a->get_total_transit());
			int b_in = b->get_input().empty() ? -1 : (int)(b->get_total_in()+b->get_total_transit());
			cmp = a_in - b_in;
			break;
		}

		case factorylist::by_output:
		{
			int a_out = a->get_output().empty() ? -1 : (int)a->get_total_out();
			int b_out = b->get_output().empty() ? -1 : (int)b->get_total_out();
			cmp = a_out - b_out;
			break;
		}

		case factorylist::by_maxprod:
			cmp = a->get_base_production()*a->get_prodfactor() - b->get_base_production()*b->get_prodfactor();
			break;

		case factorylist::by_status:
			cmp = a->get_status() - b->get_status();
			break;

		case factorylist::by_power:
			cmp = a->get_prodfactor_electric() - b->get_prodfactor_electric();
			break;
		case factorylist::by_sector:
			cmp = a->get_sector() - b->get_sector();
			break;
		case factorylist::by_staffing:
			cmp = a->get_staffing_level_percentage() - b->get_staffing_level_percentage();
			break;
		case factorylist::by_operation_rate:
			cmp = a->get_stat(1, FAB_PRODUCTION) - b->get_stat(1, FAB_PRODUCTION);
			break;
		case factorylist::by_region:
			cmp = world()->get_region(a->get_pos().get_2d()) - world()->get_region(b->get_pos().get_2d());
			if (cmp == 0) {
				const koord a_city_koord = world()->get_city(a->get_pos().get_2d()) ? world()->get_city(a->get_pos().get_2d())->get_pos() : koord(0, 0);
				const koord b_city_koord = world()->get_city(b->get_pos().get_2d()) ? world()->get_city(b->get_pos().get_2d())->get_pos() : koord(0, 0);
				cmp = a_city_koord.x - b_city_koord.x;
				if (cmp == 0) {
					cmp = a_city_koord.y - b_city_koord.y;
				}
			}
			break;
	}
	if (cmp == 0) {
		cmp = STRICMP(a->get_name(), b->get_name());
	}
	return reverse ? cmp > 0 : cmp < 0;
}
