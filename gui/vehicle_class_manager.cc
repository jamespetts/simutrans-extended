/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

 /*
  * Vehicle class manager
  */
#include "../utils/simstring.h"

#include "vehicle_class_manager.h"
#include "schedule_list.h"
#include "components/gui_image.h"
#include "components/gui_divider.h"

#include "../simconvoi.h"
#include "../player/simplay.h"


#include "../dataobj/translator.h"


void accommodation_summary_t::add_vehicle(vehicle_t *veh)
{
	const uint8 catg_index = veh->get_cargo_type()->get_catg_index();
	const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(catg_index);

	if ((veh->get_desc()->get_total_capacity()+veh->get_desc()->get_overcrowded_capacity())>0) {
		for (uint8 ac=0; ac<number_of_classes; ac++) {
			if (!veh->get_accommodation_capacity(ac)) continue;

			// append accommo capacity
			accommodation_t accommo;
			accommo.catg_index = catg_index;
			accommo.accommo_class = ac;
			accommo.name = veh->get_desc()->get_accommodation_name(ac);

			bool found=false;
			FOR(slist_tpl<accommodation_info_t>, &info, accommo_list) {
				if ( accommo.is_match(info.accommodation)  &&  info.assingned_class==veh->get_reassigned_class(ac) ) {
					info.capacity += veh->get_accommodation_capacity(ac);
					info.count++;
					if (catg_index == goods_manager_t::INDEX_PAS) {
						info.min_comfort = min(info.min_comfort, veh->get_desc()->get_comfort(ac));
						info.max_comfort = max(info.max_comfort, veh->get_desc()->get_comfort(ac));
					}
					found = true;
					break;
				}
			}
			if (!found) {
				accommodation_info_t temp;
				temp.accommodation = accommo;
				temp.count = 1;
				temp.assingned_class = veh->get_reassigned_class(ac);
				temp.accommodation = accommo;
				temp.capacity = veh->get_accommodation_capacity(ac);
				if (catg_index == goods_manager_t::INDEX_PAS) {
					temp.min_comfort = veh->get_desc()->get_comfort(ac);
					temp.max_comfort = veh->get_desc()->get_comfort(ac);
				}

				accommo_list.append(temp);
			}
		}
	}
}

void accommodation_summary_t::add_vehicle_desc(const vehicle_desc_t *veh_type)
{
	const uint8 catg_index = veh_type->get_freight_type()->get_catg_index();
	const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(catg_index);

		if ((veh_type->get_total_capacity()+veh_type->get_overcrowded_capacity())>0) {
		for (uint8 ac=0; ac<number_of_classes; ac++) {
			if (!veh_type->get_capacity(ac)) continue;

			// append accommo capacity
			accommodation_t accommo;
			accommo.catg_index = catg_index;
			accommo.accommo_class = ac;
			accommo.name = veh_type->get_accommodation_name(ac);

			bool found=false;
			FOR(slist_tpl<accommodation_info_t>, &info, accommo_list) {
				if ( accommo.is_match(info.accommodation)  &&  info.assingned_class==ac ) {
					info.capacity += veh_type->get_capacity(ac);
					info.count++;
					if (catg_index == goods_manager_t::INDEX_PAS) {
						info.min_comfort = min(info.min_comfort, veh_type->get_comfort(ac));
						info.max_comfort = max(info.max_comfort, veh_type->get_comfort(ac));
					}
					found = true;
					break;
				}
			}
			if (!found) {
				accommodation_info_t temp;
				temp.accommodation = accommo;
				temp.count = 1;
				temp.assingned_class = ac;
				temp.capacity = veh_type->get_capacity(ac);
				if (catg_index == goods_manager_t::INDEX_PAS) {
					temp.min_comfort = veh_type->get_comfort(ac);
					temp.max_comfort = veh_type->get_comfort(ac);
				}

				accommo_list.append(temp);
			}
		}
	}
}

void accommodation_summary_t::add_convoy(convoihandle_t cnv)
{
	for (uint8 v=0; v<cnv->get_vehicle_count(); v++) {
		add_vehicle(cnv->get_vehicle(v));
	}
	sort();
}


void accommodation_summary_t::add_line(linehandle_t line)
{
	for (uint8 icnv=0; icnv < line->count_convoys(); icnv++) {
		convoihandle_t const cnv = line->get_convoy(icnv);
		for (uint8 v = 0; v < cnv->get_vehicle_count(); v++) {
			add_vehicle(cnv->get_vehicle(v));
		}
	}
	sort();
}

void accommodation_summary_t::sort()
{
	accommo_list.sort(accommodation_info_t::compare);
}

gui_accommodation_fare_manager_t::gui_accommodation_fare_manager_t(linehandle_t line)
{
	this->line = line;
	cnv = convoihandle_t();

	update();
}


gui_accommodation_fare_manager_t::gui_accommodation_fare_manager_t(convoihandle_t cnv)
{
	this->cnv = cnv;
	line = linehandle_t();

	update();
}

void gui_accommodation_fare_manager_t::update()
{
	remove_all();
	accommodations.clear();
	if( cnv.is_bound() ) {
		accommodations.add_convoy(cnv);
	}
	else if( line.is_bound() ) {
		accommodations.add_line(line);
	}
	else {
		return;
	}
	set_table_layout(7,0);

	// header
	new_component<gui_empty_t>();
	new_component_span<gui_label_t>("Accommodation", 2);
	new_component<gui_label_t>("Compartments");
	new_component<gui_label_t>("Capacity");
	new_component<gui_label_t>("Comfort");
	new_component<gui_label_t>("Class");

	// border
	new_component<gui_empty_t>();
	new_component_span<gui_border_t>(2);
	new_component<gui_border_t>();
	new_component<gui_border_t>();
	new_component<gui_border_t>();
	new_component<gui_border_t>();

	FOR(slist_tpl<accommodation_info_t>, acm, accommodations.get_accommodations()) {
		new_component<gui_image_t>(goods_manager_t::get_info_catg_index(acm.accommodation.catg_index)->get_catg_symbol(), 0, ALIGN_NONE, true);
		const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(acm.accommodation.catg_index);

		if (number_of_classes>1) {
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
			lb->buf().printf("[%u]", acm.accommodation.accommo_class+1);
			lb->update();
			lb->set_fixed_width(lb->get_min_size().w);
		}
		else {
			new_component<gui_label_t>("-", SYSCOL_TEXT_INACTIVE);
		}

		gui_label_buf_t *lb = new_component<gui_label_buf_t>();
		lb->buf().append(translator::translate(acm.accommodation.name));
		lb->update();

		// total compartments of this accommodation
		lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		lb->buf().append(acm.count, 0);
		lb->update();

		// total capacity of this accommodation
		lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		lb->buf().append(acm.capacity, 0);
		lb->update();

		// min/max comfort of this accommodation
		if( acm.accommodation.catg_index==goods_manager_t::INDEX_PAS ) {
			add_table(2,1);
			{
				new_component<gui_image_t>(skinverwaltung_t::comfort ? skinverwaltung_t::comfort->get_image_id(0) : IMG_EMPTY, 0, ALIGN_NONE, true);
				lb = new_component<gui_label_buf_t>();
				lb->buf().append(acm.min_comfort, 0);
				if (acm.max_comfort>acm.min_comfort) {
					lb->buf().printf(" - %u", acm.max_comfort);
				}
				lb->set_tooltip(translator::translate("Comfort"));
				lb->update();
			}
			end_table();
		}
		else{
			new_component<gui_label_t>("-", SYSCOL_TEXT_INACTIVE, gui_label_t::centered);
		}

		// current assigned fare
		if( cnv.is_bound() )  new_component<gui_accommo_fare_changer_t>(cnv,  acm.accommodation, acm.assingned_class);
		if( line.is_bound() ) new_component<gui_accommo_fare_changer_t>(line, acm.accommodation, acm.assingned_class);
	}

	set_size(get_min_size());
}

void gui_accommodation_fare_manager_t::set_line(linehandle_t line)
{
	this->line = line;
	cnv = convoihandle_t();
	update();
}


gui_cabin_fare_changer_t::gui_cabin_fare_changer_t(vehicle_t *v, uint8 original_class)
{
	vehicle = v;
	cabin_class = original_class;

	set_table_layout(4, 1);
	set_alignment(ALIGN_LEFT | ALIGN_TOP);
	const uint8 g_classes = vehicle->get_cargo_type()->get_number_of_classes();

	if ( g_classes<2 || !vehicle->get_desc()->get_capacity(cabin_class) || cabin_class>(g_classes-1) ){
		// class error => Does not perform all processing
		cabin_class = old_assingned_fare = 255;
	}

	if (cabin_class != 255) {
		add_component(&up_or_down);
		add_component(&lb_assigned_fare);
		bt_down.init(button_t::arrowdown, NULL);
		bt_up.init( button_t::arrowup, NULL);
		bt_down.add_listener(this);
		bt_up.add_listener(this);
		add_component(&bt_down);
		add_component(&bt_up);
	}
	else {
		new_component_span<gui_label_t>("-", SYSCOL_TEXT_INACTIVE, gui_label_t::centered, 4);
	}
}

void gui_cabin_fare_changer_t::draw(scr_coord offset)
{
	if (cabin_class != 255) {
		const uint8 current_fare = vehicle->get_reassigned_class(cabin_class);
		bt_down.enable(current_fare!=0);
		bt_up.enable( current_fare != vehicle->get_cargo_type()->get_number_of_classes()-1);
		if (current_fare != old_assingned_fare) {
			old_assingned_fare = current_fare;

			up_or_down.set_value((sint64)current_fare - (sint64)cabin_class);

			lb_assigned_fare.buf().append(goods_manager_t::get_translated_fare_class_name(vehicle->get_cargo_type()->get_catg_index(), current_fare));
			lb_assigned_fare.update();
		}
	}

	set_size(get_size());
	gui_aligned_container_t::draw(offset);
}


bool gui_cabin_fare_changer_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( vehicle->get_owner()==world()->get_active_player()  &&  !world()->get_active_player()->is_locked()  &&  cabin_class!=255 ) {
		const uint8 current_fare = vehicle->get_reassigned_class(cabin_class);
		const uint8 g_classes = vehicle->get_cargo_type()->get_number_of_classes();
		if( (comp==&bt_up  &&  current_fare < g_classes-1)  ||  (comp==&bt_down  &&  current_fare > 0)) {
			convoihandle_t cnv = vehicle->get_convoi()->self;
			const uint8 vehicle_count = cnv->get_vehicle_count();
			for (uint8 i = 0; i < vehicle_count; i++) {
				if (cnv->get_vehicle(i) == vehicle) {
					const uint8 target_fare = (comp == &bt_up) ? current_fare+1 : current_fare-1;
					cbuffer_t buf;
					buf.printf("%hhi,%hhi,%hhi", i, cabin_class, target_fare);
					cnv->call_convoi_tool('j', buf);
					break;
				}
			}
			return true;
		}
	}
	return false;
}


gui_accommo_fare_changer_t::gui_accommo_fare_changer_t(convoihandle_t cnv_, accommodation_t acm_, uint8 current_fare_class)
{
	cnv = cnv_;
	line = linehandle_t();
	acm = acm_;

	init(current_fare_class);
}

gui_accommo_fare_changer_t::gui_accommo_fare_changer_t(linehandle_t line_, accommodation_t acm_, uint8 current_fare_class)
{
	cnv = convoihandle_t();
	line = line_;
	acm = acm_;

	init(current_fare_class);
}

void gui_accommo_fare_changer_t::init(uint8 current_fare_class)
{
	set_table_layout(4,1);
	set_alignment(ALIGN_LEFT | ALIGN_CENTER_V);

	const uint8 g_classes = goods_manager_t::get_classes_catg_index(acm.catg_index);

	if( (!cnv.is_bound() && !line.is_bound())  ||  g_classes<2  ||  current_fare_class > (g_classes-1)  ||  acm.accommo_class > (g_classes-1)   ||  acm.catg_index==goods_manager_t::INDEX_NONE  ) {
		// class error => Does not perform all processing
		current_fare = 255;
	}
	else {
		current_fare = current_fare_class;
	}

	if( current_fare!=255 ) {
		const sint64 class_diff = (sint64)current_fare -(sint64)acm.accommo_class;
		new_component<gui_fluctuation_triangle_t>(class_diff);
		new_component<gui_label_t>(goods_manager_t::get_translated_fare_class_name(acm.catg_index, current_fare));

		bt_down.init(button_t::arrowdown, NULL);
		bt_up.init(button_t::arrowup, NULL);
		bt_down.enable(current_fare != 0);
		bt_up.enable(current_fare < g_classes-1);
		bt_down.add_listener(this);
		bt_up.add_listener(this);
		add_component(&bt_down);
		add_component(&bt_up);
	}
	else {
		new_component_span<gui_label_t>("-", SYSCOL_TEXT_INACTIVE, 4);
	}
}

bool gui_accommo_fare_changer_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	const player_t *owner = cnv.is_bound() ? cnv->get_owner() : line->get_owner();
	if( owner==world()->get_active_player()  &&  !world()->get_active_player()->is_locked()  &&  current_fare!=255 ) {
		const uint8 g_classes = goods_manager_t::get_classes_catg_index(acm.catg_index);
		if( (comp==&bt_up  &&  current_fare < g_classes-1)  ||  (comp==&bt_down  &&  current_fare > 0)) {
			const uint8 target_fare = (comp == &bt_up) ? current_fare+1 : current_fare-1;
			if( line.is_bound() ) {
				for (uint8 icnv=0; icnv < line->count_convoys(); icnv++) {
					change_convoy_fare_class(line->get_convoy(icnv), target_fare);
				}
			}
			else {
				change_convoy_fare_class(cnv, target_fare);
			}
			return true;
		}
	}
	return false;
}


void gui_accommo_fare_changer_t::change_convoy_fare_class(convoihandle_t cnv_, uint8 target_fare)
{
	cbuffer_t buf;
	for (uint8 i = 0; i<cnv_->get_vehicle_count(); ++i) {
		const vehicle_t* veh = cnv_->get_vehicle(i);
		const uint8 catg_index = veh->get_cargo_type()->get_catg_index();
		if (acm.catg_index != catg_index) continue;
		const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(catg_index);
		for ( uint8 ac=0; ac<number_of_classes; ++ac ) {
			if( !veh->get_desc()->get_capacity(ac) ) continue;
			accommodation_t acm_temp;
			acm_temp.set_accommodation(veh->get_desc(), ac);
			if ( acm.is_match(acm_temp)  &&  current_fare==veh->get_reassigned_class(ac) ) {
				buf.clear();
				buf.printf("%hhi,%hhi,%hhi", i, ac, target_fare);
				cnv_->call_convoi_tool('j', buf);
			}
		}
	}
	vehicle_class_manager_t *win = dynamic_cast<vehicle_class_manager_t*>(win_get_magic((ptrdiff_t)(magic_class_manager + cnv.get_id())));
	if (win) {
		win->recalc_income();
	}
}


vehicle_class_manager_t::vehicle_class_manager_t(convoihandle_t cnv)
	: gui_frame_t("", cnv->get_owner()),
	capacity_info(linehandle_t(), cnv, false),
	cont_by_accommo(cnv),
	scrolly_by_vehicle(&cont_by_vehicle, true, true),
	scrolly_by_accommo(&cont_by_accommo, true, true)
{
	if (cnv.is_bound()) {
		init(cnv);
	}
}


void vehicle_class_manager_t::init(convoihandle_t cnv)
{
	this->cnv = cnv;
	title_buf.clear();
	title_buf.printf("%s - %s", translator::translate("class_manager"), cnv->get_name());
	gui_frame_t::set_name(title_buf);

	set_table_layout(1,0);
	add_table(4,0);
	{
		new_component<gui_label_t>("income_pr_km_(when_full):");
		add_component(&lb_total_max_income);
		new_component<gui_label_t>("/km");
		new_component<gui_fill_t>();

		new_component<gui_label_t>("Convoy running costs per km");
		add_component(&lb_total_running_cost);
		new_component<gui_label_t>("/km");
		new_component<gui_fill_t>();

		// NOTE:
		// Catering income is time-based and difficult to calculate here
		// ... and the passenser never buy anything unless they meet the minimum required time.
		//translator::translate("tpo_income_pr_km_(full_convoy):")
		//translator::translate("catering_income_pr_km_(full_convoy):")

		new_component<gui_label_t>("monthly_maintenance_cost");
		add_component(&lb_total_maint);
		new_component<gui_label_t>("/month");
		new_component<gui_fill_t>();
	}
	end_table();
	recalc_income();

	new_component<gui_divider_t>();

	add_table(2,1)->set_alignment(ALIGN_TOP);
	{
		add_component(&capacity_info);
		bt_init_fare.init(button_t::roundbox, "reset_all_classes");
		bt_init_fare.set_tooltip(translator::translate("resets_all_classes_to_their_defaults"));
		bt_init_fare.add_listener(this);
		add_component(&bt_init_fare);
	}
	end_table();

	tabs.add_tab( &scrolly_by_vehicle, translator::translate("fare_management_by_vehicle"));
	tabs.add_tab( &scrolly_by_accommo, translator::translate("fare_management_by_accommo"));
	add_component(&tabs);

	lb_total_max_income.set_color(MONEY_PLUS);
	lb_total_running_cost.set_color(MONEY_MINUS);
	lb_total_maint.set_color(MONEY_MINUS);


	set_resizemode(diagonal_resize);
}

void vehicle_class_manager_t::update_list()
{
	cont_by_vehicle.remove_all();

	cont_by_vehicle.set_table_layout(8,0);
	cont_by_vehicle.set_alignment(ALIGN_LEFT | ALIGN_TOP);

	if (!cnv.is_bound()) {
		return;
	}
	old_vehicle_count = cnv->get_vehicle_count();
	old_month = world()->get_current_month() % 12;

	for (uint8 v=0; v<cnv->get_vehicle_count(); v++) {
		// | car_no | vehicle name | catgsymbol | component of cabins |

		vehicle_t* veh = cnv->get_vehicle(v);
		gui_label_buf_t *lb = cont_by_vehicle.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
		sint16 car_number = cnv->get_car_numbering(v);
		if (car_number < 0) {
			lb->buf().printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number));
		}
		else {
			lb->buf().append(car_number);
		}
		lb->set_color(veh->get_desc()->has_available_upgrade(world()->get_timeline_year_month()) ? SYSCOL_UPGRADEABLE : SYSCOL_TEXT_WEAK);
		lb->set_fixed_width((D_BUTTON_WIDTH*3)>>3);
		lb->update();
		cont_by_vehicle.new_component_span<gui_label_t>(veh->get_desc()->get_name(),7);

		const uint8 catg_index = veh->get_cargo_type()->get_catg_index();
		const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(catg_index);

		if ((veh->get_desc()->get_total_capacity()+veh->get_desc()->get_overcrowded_capacity())>0) {
			bool already_show_catg_symbol = false;
			uint8 lowest_class = 255; // for standing capacity
			for (uint8 ac=0; ac<number_of_classes; ac++) {
				if (!veh->get_accommodation_capacity(ac)) continue;

				// col1 - margin
				cont_by_vehicle.new_component<gui_empty_t>();
				// col2 - category symbol
				if (!already_show_catg_symbol) {
					cont_by_vehicle.new_component<gui_image_t>((veh->get_desc()->get_total_capacity() || veh->get_desc()->get_overcrowded_capacity()) ? veh->get_desc()->get_freight_type()->get_catg_symbol() : IMG_EMPTY, 0, ALIGN_NONE, true);
					already_show_catg_symbol = true;
				}
				else {
					cont_by_vehicle.new_component<gui_empty_t>();
				}

				// col3 - capacity
				lb = cont_by_vehicle.new_component<gui_label_buf_t>();
				lb->buf().append(veh->get_accommodation_capacity(ac),0);
				lb->update();

				// col4 - accomodation rank
				if (number_of_classes>1) {
					lb = cont_by_vehicle.new_component<gui_label_buf_t>();
					lb->buf().printf("[%u]", ac);
					lb->update();
					lb->set_fixed_width(lb->get_min_size().w);
				}
				else {
					cont_by_vehicle.new_component<gui_empty_t>();
				}

				// col5 - accomodation name
				cont_by_vehicle.new_component<gui_label_t>(translator::translate(veh->get_desc()->get_accommodation_name(ac)));

				// col6 : comfort
				if (veh->get_desc()->get_freight_type() == goods_manager_t::passengers) {
					cont_by_vehicle.add_table(2,1);
					{
						cont_by_vehicle.new_component<gui_image_t>(skinverwaltung_t::comfort ? skinverwaltung_t::comfort->get_image_id(0) : IMG_EMPTY, 0, ALIGN_NONE, true);
						lb = cont_by_vehicle.new_component<gui_label_buf_t>();
						lb->buf().append(veh->get_desc()->get_comfort(ac), 0);
						lb->set_tooltip(translator::translate("Comfort"));
						lb->update();
					}
					cont_by_vehicle.end_table();

					lowest_class = min(lowest_class,veh->get_reassigned_class(ac));
				}
				else {
					cont_by_vehicle.new_component<gui_empty_t>();
				}

				// col7
				cont_by_vehicle.new_component<gui_cabin_fare_changer_t>(veh, ac);

				// col8 income
				lb = cont_by_vehicle.new_component<gui_label_buf_t>();

				const sint64 revenue = (veh->get_cargo_type()->get_total_fare(1000, 0u, veh->get_desc()->get_comfort(ac), cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()), min(veh->get_reassigned_class(ac), number_of_classes - 1), 0)) * veh->get_accommodation_capacity(ac);
				const sint64 price = (revenue + 2048) / 4096;
				lb->buf().append_money(price / 100.0);
				lb->update();
			}

			if (veh->get_desc()->get_overcrowded_capacity()) {
				cont_by_vehicle.new_component<gui_empty_t>();
				cont_by_vehicle.new_component<gui_image_t>(skinverwaltung_t::pax_evaluation_icons ? skinverwaltung_t::pax_evaluation_icons->get_image_id(1) : IMG_EMPTY, 0, ALIGN_NONE, true);
				lb = cont_by_vehicle.new_component<gui_label_buf_t>();
				lb->buf().append(veh->get_desc()->get_overcrowded_capacity(), 0);
				lb->update();
				cont_by_vehicle.new_component<gui_empty_t>();
				cont_by_vehicle.new_component<gui_label_t>("overcrowded_capacity");
				cont_by_vehicle.new_component<gui_empty_t>();
				cont_by_vehicle.new_component<gui_empty_t>();
				// col9 income
				if (lowest_class == 255) lowest_class = 0;
				lb = cont_by_vehicle.new_component<gui_label_buf_t>();
				const uint8 base_comfort = veh->get_comfort(cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()));
				const uint8 standing_comfort = (base_comfort < 20) ? (base_comfort / 2) : 10;
				const sint64 revenue = (veh->get_cargo_type()->get_total_fare(1000, 0u, standing_comfort, cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()), lowest_class, 0)) * veh->get_desc()->get_overcrowded_capacity();
				const sint64 price = (revenue + 2048) / 4096;
				lb->buf().append_money(price / 100.0);
				lb->update();
			}
		}
		else {
			// no capacity
			cont_by_vehicle.new_component<gui_empty_t>();
			cont_by_vehicle.new_component<gui_label_t>("-", SYSCOL_TEXT_WEAK);
			cont_by_vehicle.new_component_span<gui_empty_t>(6);
		}
		if (v < cnv->get_vehicle_count()-1) {
			cont_by_vehicle.new_component_span<gui_border_t>(8);
		}
	}

	lb_total_running_cost.buf().printf("%1.2f$", cnv->get_per_kilometre_running_cost()/100.0);
	lb_total_running_cost.update();
	lb_total_maint.buf().printf("%1.2f$", world()->calc_adjusted_monthly_figure(cnv->get_fixed_cost())/100.0);
	lb_total_maint.update();

	cont_by_vehicle.set_size(cont_by_vehicle.get_min_size());
	cont_by_accommo.update();

	reset_min_windowsize();
}


void vehicle_class_manager_t::recalc_income()
{
	sint64 revenue = 0;

	for (uint8 v = 0; v < cnv->get_vehicle_count(); v++) {
		vehicle_t* veh = cnv->get_vehicle(v);
		if ((veh->get_desc()->get_total_capacity() + veh->get_desc()->get_overcrowded_capacity()) > 0) {
			const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(veh->get_cargo_type()->get_catg_index());
			uint8 lowest_class = 255; // for standing capacity
			for (uint8 ac = 0; ac < number_of_classes; ac++) {
				if (!veh->get_accommodation_capacity(ac)) continue;
				const uint8 current_fare_class = veh->get_reassigned_class(ac);
				lowest_class = min(lowest_class, current_fare_class);
				revenue += (veh->get_cargo_type()->get_total_fare(1000, 0u, veh->get_desc()->get_comfort(ac), cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()), min(current_fare_class, number_of_classes-1), 0)) * veh->get_accommodation_capacity(ac);
			}

			if (veh->get_desc()->get_overcrowded_capacity()) {
				if (lowest_class == 255) lowest_class = 0;
				const uint8 base_comfort = veh->get_comfort(cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()));
				const uint8 standing_comfort = (base_comfort < 20) ? (base_comfort / 2) : 10;
				revenue += (veh->get_cargo_type()->get_total_fare(1000, 0u, standing_comfort, cnv->get_catering_level(veh->get_cargo_type()->get_catg_index()), lowest_class, 0)) * veh->get_desc()->get_overcrowded_capacity();
			}
		}
	}
	// Convert to simucents.  Should be very fast.
	const sint64 price = (revenue + 2048) / 4096;

	lb_total_max_income.buf().append_money(price / 100.0);
	lb_total_max_income.update();

	capacity_info.set_convoy(cnv);
	update_list();
}


void vehicle_class_manager_t::draw(scr_coord pos, scr_size size)
{
	if (!cnv.is_bound()) {
		destroy_win(this);
		return;
	}

	if (cnv->get_vehicle_count() != old_vehicle_count) {
		recalc_income();
	}
	else if (world()->get_current_month()%12 != old_month) {
		update_list();
	}

	const bool has_classes = ((cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) && goods_manager_t::passengers->get_number_of_classes() > 1)
		|| (cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL) && goods_manager_t::mail->get_number_of_classes() > 1));
	const bool can_edit = ( cnv->get_owner()==world()->get_active_player()  &&  !world()->get_active_player()->is_locked() );
	bt_init_fare.enable( (has_classes && can_edit) );

	gui_frame_t::draw(pos, size);
}


/**
 * This method is called if an action is triggered
 */
bool vehicle_class_manager_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( !cnv.is_bound() ) { return false; }
	if( comp==&bt_init_fare  &&  cnv->get_owner()==world()->get_active_player()  &&  !world()->get_active_player()->is_locked() ) {
		cbuffer_t buf;
		if( cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) ) {
			buf.printf("%hhi", goods_manager_t::INDEX_PAS);
			cnv->call_convoi_tool('i', buf);
		}

		if( cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL) ) {
			buf.printf("%hhi", goods_manager_t::INDEX_MAIL);
			cnv->call_convoi_tool('i', buf);
		}

		recalc_income();
		return true;
	}
	return false;
}


// dummy for loading
vehicle_class_manager_t::vehicle_class_manager_t()
	: gui_frame_t("", NULL),
	capacity_info(linehandle_t(), convoihandle_t(), false),
	cont_by_accommo(convoihandle_t()),
	scrolly_by_vehicle(&cont_by_vehicle, true, true),
	scrolly_by_accommo(&cont_by_accommo, true, true)
{
	cnv = convoihandle_t();
}

void vehicle_class_manager_t::rdwr(loadsave_t *file)
{
	// convoy data
	convoi_t::rdwr_convoihandle_t(file, cnv);

	// window size, scroll position
	scr_size size = get_windowsize();
	sint32 xoff = tabs.get_active_tab_index() ? scrolly_by_vehicle.get_scroll_x() : scrolly_by_accommo.get_scroll_x();
	sint32 yoff = tabs.get_active_tab_index() ? scrolly_by_vehicle.get_scroll_y() : scrolly_by_accommo.get_scroll_y();

	size.rdwr( file );
	file->rdwr_long( xoff );
	file->rdwr_long( yoff );

	uint8 selected_tab = tabs.get_active_tab_index();
	file->rdwr_byte(selected_tab);

	if(  file->is_loading()  ) {
		// convoy vanished
		if(  !cnv.is_bound()  ) {
			dbg->error( "vehicle_class_manager_t::rdwr()", "Could not restore vehicle class manager window of (%d)", cnv.get_id() );
			destroy_win( this );
			return;
		}

		// now we can open the window ...
		scr_coord const& pos = win_get_pos(this);
		vehicle_class_manager_t *w = new vehicle_class_manager_t(cnv);
		create_win(pos.x, pos.y, w, w_info, magic_class_manager + cnv.get_id());
		w->set_windowsize( size );
		w->tabs.set_active_tab_index(selected_tab);
		w->scrolly_by_vehicle.set_scroll_position( xoff, yoff );

		// we must invalidate halthandle
		cnv = convoihandle_t();
		destroy_win( this );
	}
}

