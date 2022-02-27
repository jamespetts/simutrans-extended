/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_factory_storage_info.h"
#include "gui_image.h"
#include "gui_halt_indicator.h"

#include "../../simcolor.h"
#include "../../simworld.h"

#include "../../dataobj/environment.h"
#include "../../dataobj/translator.h"
#include "../../utils/cbuffer_t.h"

#include "../../display/viewport.h"
#include "../../utils/simstring.h"

#include "../../player/simplay.h"

#include "../map_frame.h"
#include "../simwin.h"

#define STORAGE_INDICATOR_WIDTH (50)

// Half a display unit (0.5).
static const sint64 FAB_DISPLAY_UNIT_HALF = ((sint64)1 << (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS - 1));


gui_factory_storage_bar_t::gui_factory_storage_bar_t(const ware_production_t *ware, uint32 factor, bool is_input)
{
	this->ware = ware;
	this->is_input_item = is_input;
	this->factor = factor;
	set_size(get_min_size());
}

void gui_factory_storage_bar_t::draw(scr_coord offset)
{
	offset += pos;

	const scr_coord_val color_bar_w = size.w-2;

	display_ddd_box_clip_rgb(offset.x, offset.y, size.w, size.h, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4));
	display_fillbox_wh_clip_rgb(offset.x + 1, offset.y+1, color_bar_w, size.h-2, color_idx_to_rgb(MN_GREY2), false);

	uint32 substantial_intransit = 0; // yellowed bar

	if ( ware->get_typ()->is_available() ) {
		const uint32 storage_capacity = (uint32)ware->get_capacity(factor);
		if (storage_capacity == 0) { return; }
		const uint32 stock_quantity = min((uint32)ware->get_storage(), storage_capacity);

		const PIXVAL goods_color = ware->get_typ()->get_color();
		display_cylinderbar_wh_clip_rgb(offset.x+1, offset.y+1, color_bar_w*stock_quantity/storage_capacity, size.h-2, goods_color, true);

		// input has in_transit
		if (is_input_item) {
			substantial_intransit = min((uint32)ware->get_in_transit(), storage_capacity-stock_quantity);
			if (substantial_intransit) {
				// in transit for input storage
				display_fillbox_wh_clip_rgb(offset.x+1+color_bar_w* stock_quantity/storage_capacity, offset.y+1, color_bar_w*substantial_intransit/storage_capacity, size.h-2, COL_IN_TRANSIT, false);
			}
		}
	}
}


gui_factory_product_item_t::gui_factory_product_item_t(fabrik_t *factory, const ware_production_t *ware, bool is_input_item) :
	lb_leadtime(""), lb_alert("")
{
	this->fab = factory;
	this->ware = ware;
	this->is_input_item = is_input_item;
	lb_leadtime.set_image(skinverwaltung_t::travel_time ? skinverwaltung_t::travel_time->get_image_id(0) : IMG_EMPTY);
	lb_leadtime.set_tooltip(translator::translate("symbol_help_txt_lead_time"));
	lb_alert.set_visible(false);

	if (fab) {
		set_table_layout(7,0);
		init_table();
	}
}

void gui_factory_product_item_t::init_table()
{
	goods_desc_t const* const desc = ware->get_typ();

	// material arrival status for input (vs supplier)
	if (is_input_item) {
		add_component(&operation_status);
	}
	new_component<gui_image_t>(desc->get_catg_symbol(), 0, ALIGN_CENTER_V, true);
	new_component<gui_colorbox_t>(desc->get_color())->set_size(scr_size(LINESPACE/2+2, LINESPACE/2+2));
	new_component<gui_label_t>(desc->get_name(), ware->get_typ()->is_available() ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
	// material delivered status for output (vs consumer)
	if (!is_input_item) {
		add_component(&operation_status);
	}
	new_component<gui_margin_t>(LINESPACE);
	add_component(&lb_leadtime);
	lb_leadtime.set_visible(is_input_item);
	add_component(&lb_alert);
}

void gui_factory_product_item_t::draw(scr_coord offset)
{
	// update operation status
	if (is_input_item) {
		// lead time
		uint32 lead_time = fab->get_lead_time(ware->get_typ());
		if (lead_time == UINT32_MAX_VALUE) {
			lb_leadtime.buf().append("--:--:--");
		}
		else {
			char lead_time_as_clock[32];
			world()->sprintf_time_tenths(lead_time_as_clock, 32, lead_time);
			lb_leadtime.buf().append(lead_time_as_clock);
			//col_val = is_connected_to_own_network ? SYSCOL_TEXT : SYSCOL_TEXT_INACTIVE;
		}
		lb_leadtime.update();

		sint32 goods_needed = fab->goods_needed(ware->get_typ());

		// operation status, reciept/intansit or not
		uint8 reciept_score = 0;
		if (ware->get_stat(0, FAB_GOODS_RECEIVED)) {
			// This factory is receiving materials this month.
			reciept_score += 80;
		}
		if (ware->get_stat(1, FAB_GOODS_RECEIVED)) {
			// This factory hasn't recieved this month yet, but it did last month.
			reciept_score = min(100, reciept_score + 50);
		}
		if (ware->get_stat(0, FAB_GOODS_TRANSIT)) {
			reciept_score = min(100, reciept_score + 20);
		}
		operation_status.set_color(color_idx_to_rgb(severity_color[(reciept_score+19)/20]));
		if (!reciept_score) {
			if (goods_needed <= 0) {
				// No shipments demanded: sufficient product is in stock
				operation_status.set_status(gui_operation_status_t::operation_pause);
				lb_alert.set_image(skinverwaltung_t::pax_evaluation_icons ? skinverwaltung_t::pax_evaluation_icons->get_image_id(4) : IMG_EMPTY);
				lb_alert.buf().append(translator::translate("Shipment is suspended"));
				lb_alert.set_tooltip(translator::translate("Shipment has been suspended due to consumption demand"));
				lb_alert.update();
				lb_alert.set_visible(true);
			}
			else {
				operation_status.set_color(COL_INACTIVE);
				operation_status.set_status(gui_operation_status_t::operation_stop);
				lb_alert.set_visible(false);
			}
		}
		else {
			lb_alert.set_visible(false);
			if (goods_needed <= 0) {
				lb_alert.set_image(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(3) : IMG_EMPTY);
				lb_alert.buf().append(translator::translate("Shipment is suspended"));
				lb_alert.set_tooltip(translator::translate("Suspension of new orders due to sufficient supply"));
				lb_alert.update();
				lb_alert.set_visible(true);
			}
			operation_status.set_status(gui_operation_status_t::operation_normal);
		}
	}
	else {
		uint8 shipping_score = 0;
		if (ware->get_stat(0, FAB_GOODS_DELIVERED)) {
			// This factory is shipping this month.
			shipping_score += 80;
		}
		if (ware->get_stat(1, FAB_GOODS_DELIVERED)) {
			// This factory hasn't shipped this month yet, but it did last month.
			shipping_score = min(100, shipping_score + 50);
		}
		if (!shipping_score) {
			if (!ware->get_stat(0, FAB_GOODS_STORAGE)) {
				operation_status.set_status(gui_operation_status_t::operation_stop);
				operation_status.set_color(fab->is_staff_shortage() ? COL_STAFF_SHORTAGE : SYSCOL_TEXT_WEAK);
			}
			else {
				// Stopped due to demand/connection issue
				operation_status.set_status(gui_operation_status_t::operation_pause);
				operation_status.set_color(color_idx_to_rgb(COL_RED+1));
			}
		}
		else {
			operation_status.set_color(color_idx_to_rgb(severity_color[(shipping_score+19)/20]));
			operation_status.set_status(gui_operation_status_t::operation_normal);
		}
	}

	gui_aligned_container_t::draw(offset);
}

bool gui_factory_product_item_t::infowin_event(const event_t *ev)
{
	if (IS_LEFTRELEASE(ev)) {
		ware->get_typ()->show_info();
	}
	return gui_aligned_container_t::infowin_event(ev);
}


// component for factory storage display
gui_factory_storage_info_t::gui_factory_storage_info_t(fabrik_t* factory)
{
	this->fab = factory;
}

void gui_factory_storage_info_t::draw(scr_coord offset)
{
	int left = 0;
	int yoff = 2;
	if (fab) {
		offset.x += D_MARGIN_LEFT;

		cbuffer_t buf;

		const uint32 input_count = fab->get_input().get_count();
		const uint32 output_count = fab->get_output().get_count();

		// input storage info (Consumption)
		if (input_count) {
			// if pakset has symbol, display it
			if (skinverwaltung_t::input_output)
			{
				display_color_img(skinverwaltung_t::input_output->get_image_id(0), pos.x + offset.x, pos.y + offset.y + yoff + FIXED_SYMBOL_YOFF, 0, false, false);
				left += 12;
			}
			display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, translator::translate("Verbrauch"), ALIGN_LEFT, SYSCOL_TEXT, true);
			yoff += LINESPACE;

			int i = 0;
			FORX(array_tpl<ware_production_t>, const& goods, fab->get_input(), i++) {
				if (!fab->get_desc()->get_supplier(i))
				{
					continue;
				}
				const bool is_available = goods.get_typ()->is_available();

				const sint64 pfactor = fab->get_desc()->get_supplier(i) ? (sint64)fab->get_desc()->get_supplier(i)->get_consumption() : 1ll;
				const sint64 max_transit = (uint32)((FAB_DISPLAY_UNIT_HALF + (sint64)goods.max_transit * pfactor) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS));
				const uint32 stock_quantity = (uint32)goods.get_storage();
				const uint32 storage_capacity = (uint32)goods.get_capacity(pfactor);
				const PIXVAL goods_color = goods.get_typ()->get_color();

				left = 2;
				yoff += 2; // box position adjistment
				// [storage indicator]
				display_ddd_box_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF, STORAGE_INDICATOR_WIDTH + 2, GOODS_COLOR_BOX_HEIGHT, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4));
				display_fillbox_wh_clip_rgb(pos.x + offset.x + left + 1, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF + 1, STORAGE_INDICATOR_WIDTH, GOODS_COLOR_BOX_HEIGHT-2, color_idx_to_rgb(MN_GREY2), true);
				if (storage_capacity) {
					const uint16 colored_width = min(STORAGE_INDICATOR_WIDTH, (uint16)(STORAGE_INDICATOR_WIDTH * stock_quantity / storage_capacity));
					display_cylinderbar_wh_clip_rgb(pos.x + offset.x + left + 1, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF + 1, colored_width, 6, goods_color, true);
					if (goods.get_in_transit()) {
						const uint16 intransint_width = min(STORAGE_INDICATOR_WIDTH - colored_width, STORAGE_INDICATOR_WIDTH * (uint16)goods.get_in_transit() / storage_capacity);
						display_fillbox_wh_clip_rgb(pos.x + offset.x + left + 1 + colored_width, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF + 1, intransint_width, 6, COL_IN_TRANSIT, true);
					}
				}
				left += STORAGE_INDICATOR_WIDTH + 2 + D_H_SPACE;

				// [goods color box] This design is the same as the goods list
				display_colorbox_with_tooltip(pos.x + offset.x + left, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF, GOODS_COLOR_BOX_HEIGHT, GOODS_COLOR_BOX_HEIGHT, goods_color, false);
				left += 12;
				yoff -= 2; // box position adjistment

				// [goods name]
				buf.clear();
				buf.printf("%s", translator::translate(goods.get_typ()->get_name()));
				left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, is_available ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK, true);

				left += 10;

				// [goods category]
				display_color_img_with_tooltip(goods.get_typ()->get_catg_symbol(), pos.x + offset.x + left, pos.y + offset.y + yoff + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(goods.get_typ()->get_catg_name()));
				goods.get_typ()->get_catg_name();
				left += 14;

				// [storage capacity]
				buf.clear();
				buf.printf("%u/%u", stock_quantity, storage_capacity);
				if (is_available) {
					buf.append(", ");
				}
				left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, is_available ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK, true);
				left += D_H_SPACE;

				buf.clear();
				if (is_available) {
					// [in transit]
					if (fab->get_status() != fabrik_t::inactive) {
						//const bool in_transit_over_storage = (stock_quantity + (uint32)goods.get_in_transit() > storage_capacity);
						const sint32 actual_max_transit = max(goods.get_in_transit(), max_transit);
						if (skinverwaltung_t::in_transit) {
							display_color_img_with_tooltip(skinverwaltung_t::in_transit->get_image_id(0), pos.x + offset.x + left, pos.y + offset.y + yoff + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate("symbol_help_txt_in_transit"));
							left += 14;
						}
						buf.printf("%i/%i", goods.get_in_transit(), actual_max_transit);
						PIXVAL col_val = actual_max_transit == 0 ? COL_DANGER : max_transit == 0 ? color_idx_to_rgb(COL_DARK_ORANGE) : SYSCOL_TEXT;
						left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, col_val, true);
						buf.clear();
						buf.append(", ");
					}


					// [monthly production]
					const uint32 monthly_prod = (uint32)(fab->get_current_production()*pfactor * 10 >> DEFAULT_PRODUCTION_FACTOR_BITS);
					if (monthly_prod < 100) {
						buf.printf(translator::translate("consumption %.1f%s/month"), (float)monthly_prod / 10.0, translator::translate(goods.get_typ()->get_mass()));
					}
					else {
						buf.printf(translator::translate("consumption %u%s/month"), monthly_prod / 10, translator::translate(goods.get_typ()->get_mass()));
					}
					left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				}
				yoff += LINESPACE;
			}
			yoff += LINESPACE;
		}


		// output storage info (Production)
		if (output_count) {
			left = 0;
			// if pakset has symbol, display it
			if(skinverwaltung_t::input_output)
			{
				display_color_img(skinverwaltung_t::input_output->get_image_id(1), pos.x + offset.x, pos.y + offset.y + yoff + FIXED_SYMBOL_YOFF, 0, false, false);
				left += 12;
			}
			display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, translator::translate("Produktion"), ALIGN_LEFT, SYSCOL_TEXT, true);
			yoff += LINESPACE;

			int i = 0;
			FORX(array_tpl<ware_production_t>, const& goods, fab->get_output(), i++) {
				const sint64 pfactor = (sint64)fab->get_desc()->get_product(i)->get_factor();
				const uint32 stock_quantity   = (uint32)goods.get_storage();
				const uint32 storage_capacity = (uint32)goods.get_capacity(pfactor);
				const PIXVAL goods_color  = goods.get_typ()->get_color();
				left = 2;
				yoff+=2; // box position adjistment
				// [storage indicator]
				display_ddd_box_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF, STORAGE_INDICATOR_WIDTH+2, GOODS_COLOR_BOX_HEIGHT, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4));
				display_fillbox_wh_clip_rgb(pos.x + offset.x + left+1, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF + 1, STORAGE_INDICATOR_WIDTH, GOODS_COLOR_BOX_HEIGHT-2, color_idx_to_rgb(MN_GREY2), true);
				if (storage_capacity) {
					const uint16 colored_width = min(STORAGE_INDICATOR_WIDTH, (uint16)(STORAGE_INDICATOR_WIDTH * stock_quantity / storage_capacity));
					display_cylinderbar_wh_clip_rgb(pos.x + offset.x + left + 1, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF + 1, colored_width, 6, goods_color, true);
				}
				left += STORAGE_INDICATOR_WIDTH + 2 + D_H_SPACE;

				// [goods color box] This design is the same as the goods list
				display_colorbox_with_tooltip(pos.x + offset.x + left, pos.y + offset.y + yoff + GOODS_COLOR_BOX_YOFF, GOODS_COLOR_BOX_HEIGHT, GOODS_COLOR_BOX_HEIGHT, goods_color, false);
				left += 12;
				yoff-=2; // box position adjistment

				// [goods name]
				buf.clear();
				buf.printf("%s", translator::translate(goods.get_typ()->get_name()));
				left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, SYSCOL_TEXT, true);

				left += 10;

				// [goods category]
				display_color_img_with_tooltip(goods.get_typ()->get_catg_symbol(), pos.x + offset.x + left, pos.y + offset.y + yoff + FIXED_SYMBOL_YOFF, 0, false, false, translator::translate(goods.get_typ()->get_catg_name()));
				left += 14;

				// [storage capacity]
				buf.clear();
				buf.printf("%i/%i,", stock_quantity, storage_capacity);
				left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				left += D_H_SPACE;

				// [monthly production]
				buf.clear();
				const uint32 monthly_prod = (uint32)(fab->get_current_production()*pfactor * 10 >> DEFAULT_PRODUCTION_FACTOR_BITS);
				if (monthly_prod < 100) {
					buf.printf(translator::translate("production %.1f%s/month"), (float)monthly_prod / 10.0, translator::translate(goods.get_typ()->get_mass()));
				}
				else {
					buf.printf(translator::translate("production %u%s/month"), monthly_prod / 10, translator::translate(goods.get_typ()->get_mass()));
				}
				left += display_proportional_clip_rgb(pos.x + offset.x + left, pos.y + offset.y + yoff, buf, ALIGN_LEFT, SYSCOL_TEXT, true);

				yoff += LINESPACE;
			}
			yoff += LINESPACE;
		}

	}
	scr_size size(400, yoff);
	if (size != get_size()) {
		set_size(size);
	}
}

void gui_factory_storage_info_t::recalc_size()
{
	if (fab) {
		uint lines = fab->get_input().get_count() ? fab->get_input().get_count() + 1 : 0;
		if (fab->get_output().get_count()) {
			if (lines) { lines++; }
			lines += fab->get_output().get_count()+1;
		}
		set_size(scr_size(400, lines * (LINESPACE + 1)));
	}
	else {
		set_size(scr_size(400, LINESPACE + 1));
	}
}


gui_freight_halt_stat_t::gui_freight_halt_stat_t(halthandle_t halt)
{
	this->halt = halt;
	if (halt.is_bound()) {
		set_table_layout(5,1);
		new_component<gui_halt_capacity_bar_t>(halt,2)->set_width(LINESPACE*4); // todo: set width
		add_component(&label_name);
		old_month = -1; // force update
		lb_handling_amount.init(SYSCOL_TEXT, gui_label_t::right);
		lb_handling_amount.set_tooltip(translator::translate("The number of goods that handling at the stop last month"));
		new_component<gui_halt_handled_goods_images_t>(halt, true);
		add_component(&lb_handling_amount);
		bt_show_halt_network.init( button_t::roundbox, "Networks" );
		bt_show_halt_network.set_tooltip( translator::translate("Open the minimap window to show the freight network of this stop") );
		bt_show_halt_network.add_listener(this);
		add_component(&bt_show_halt_network);
	}
}

bool gui_freight_halt_stat_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if ( comp==&bt_show_halt_network ) {
		map_frame_t *win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		if (!win) {
			create_win(-1, -1, new map_frame_t(), w_info, magic_reliefmap);
			win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		}
		win->set_halt(halt, true);
		top_win(win);
		return true;
	}
	return false;
}

bool gui_freight_halt_stat_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);
	if (ev->mx < 0 || ev->mx > bt_show_halt_network.get_pos().x ) {
		return swallowed;
	}
	if(  !swallowed  &&  ev->my > 0 && ev->my < size.h &&  halt.is_bound()  ) {

		if(IS_LEFTRELEASE(ev)) {
			if(  event_get_last_control_shift() != 2  ) {
				halt->show_info();
			}
			return true;
		}
		if(  IS_RIGHTRELEASE(ev)  ) {
			world()->get_viewport()->change_world_position(halt->get_basis_pos3d());
			return true;
		}
	}
	return swallowed;
}

void gui_freight_halt_stat_t::draw(scr_coord offset)
{
	label_name.buf().append(halt->get_name());
	label_name.set_color(color_idx_to_rgb(halt->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
	label_name.update();

	if (old_month != world()->get_current_month() % 12) {
		old_month = world()->get_current_month()%12;
		lb_handling_amount.buf().append(halt->get_finance_history(1, HALT_GOODS_HANDLING_VOLUME)/100.0, 1);
		lb_handling_amount.buf().append(translator::translate("tonnen"));

		lb_handling_amount.update();
	}

	set_size(get_size());
	if (win_get_magic(magic_halt_info + halt.get_id())) {
		display_blend_wh_rgb(offset.x + get_pos().x, offset.y + get_pos().y, get_size().w, get_size().h, SYSCOL_TEXT_HIGHLIGHT, 30);
	}
	gui_aligned_container_t::draw(offset);
}

gui_factory_nearby_halt_info_t::gui_factory_nearby_halt_info_t(fabrik_t *factory)
{
	this->fab = factory;
	set_table_layout(1,0);
	if (fab) {
		old_halt_count = fab->get_nearby_freight_halts().get_count();
		update_table();
	}
}

void gui_factory_nearby_halt_info_t::update_table()
{
	old_halt_count = fab->get_nearby_freight_halts().get_count();
	remove_all();
	FOR(const vector_tpl<nearby_halt_t>, freight_halt, fab->get_nearby_freight_halts()) {
		new_component<gui_freight_halt_stat_t>(freight_halt.halt);
	}
	if (!old_halt_count) {
		add_table(2,1);
		new_component<gui_image_t>(skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image_id(2) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
		new_component<gui_label_t>("No connection to the freight stop.");
		end_table();
	}
	set_size(get_size());
}

void gui_factory_nearby_halt_info_t::draw(scr_coord offset)
{
	if (old_halt_count != fab->get_nearby_freight_halts().get_count()) {
		update_table();
	}
	gui_aligned_container_t::draw(offset);
}


void gui_goods_handled_factory_t::build_factory_list(const goods_desc_t *ware)
{
	factory_list.clear();
	FOR(vector_tpl<fabrik_t*>, const f, world()->get_fab_list()) {
		if (show_consumer) {
			// consume(accept) this?
			if(f->get_desc()->get_accepts_these_goods(ware)) {
				factory_list.append_unique(f->get_desc());
			}
		}
		else {
			// produce this?
			FOR(array_tpl<ware_production_t>, const& product, f->get_output()) {
				if (product.get_typ() == ware) {
					factory_list.append_unique(f->get_desc());
					break; // found
				}
			}
		}
	}
}

void gui_goods_handled_factory_t::draw(scr_coord offset)
{
	offset += pos;
	scr_coord_val xoff = 0;
	if (factory_list.get_count() > 0) {
		// if pakset has symbol, show symbol
		if (skinverwaltung_t::input_output){
			display_color_img(skinverwaltung_t::input_output->get_image_id(show_consumer ? 0:1), offset.x, offset.y + FIXED_SYMBOL_YOFF, 0, false, false);
			xoff += 14;
		}

		uint n = 0;
		FORX(vector_tpl<const factory_desc_t*>, f, factory_list, n++) {
			bool is_retired=false;
			if (world()->use_timeline()){
				if (f->get_building()->get_intro_year_month() > world()->get_timeline_year_month()) {
					// is future
					continue;
				}
				if (f->get_building()->get_retire_year_month() < world()->get_timeline_year_month()) {
					is_retired = true;
				}
			}
			if (n) {
				xoff += display_proportional_clip_rgb(offset.x + xoff, offset.y, ", ", ALIGN_LEFT, SYSCOL_TEXT, true);
			}
			buf.clear();
			buf.printf("%s", translator::translate(f->get_name()));
			xoff += display_proportional_clip_rgb(offset.x + xoff, offset.y, buf, ALIGN_LEFT, is_retired ? SYSCOL_OUT_OF_PRODUCTION : SYSCOL_TEXT, true);
		}

	}
	else {
		xoff += display_proportional_clip_rgb(offset.x + xoff, offset.y, "-", ALIGN_LEFT, COL_INACTIVE, true);
	}

	set_size(scr_size(xoff + D_H_SPACE * 2, D_LABEL_HEIGHT));
}
