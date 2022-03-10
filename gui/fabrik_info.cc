/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "fabrik_info.h"

#include "components/gui_label.h"

#include "help_frame.h"

#include "../simfab.h"
#include "../simcolor.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../simcity.h"
#include "simwin.h"
#include "../simmenu.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../simskin.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../utils/simstring.h"
#include "map_frame.h"

#include "../obj/leitung2.h" // POWER_TO_MW


sint16 fabrik_info_t::tabstate = 0;


/**
 * Helper class: one row of entries in consumer / supplier table
 */
class gui_factory_connection_t : public gui_aligned_container_t
{
	fabrik_t *fab;
	koord target;
	bool supplier;
	const ware_production_t *ware;
	uint8 prod_index;
	gui_label_buf_t lb_name;
	gui_label_with_symbol_t lb_leadtime;
	gui_operation_status_t connection_status;
	//gui_factory_storage_bar_t storage;

public:
	gui_factory_connection_t(fabrik_t* f, koord t, uint8 prod_index, bool s) : fab(f), ware(s ? &f->get_input()[prod_index] : &f->get_output()[prod_index]), target(t), supplier(s), lb_leadtime("")
	{
		this->prod_index = prod_index;
		fabrik_t *target_fab = fabrik_t::get_fab(target);
		uint8 target_ware_index = 0;
		FORX(array_tpl<ware_production_t>, const& product, supplier ? target_fab->get_output(): target_fab->get_input(), target_ware_index++) {
			if (ware->get_typ() == product.get_typ()) {
				break;
			}
		}
		const ware_production_t &target_ware = supplier ? target_fab->get_output()[target_ware_index] : target_fab->get_input()[target_ware_index];
		const uint32 pfactor = supplier ? (uint32)target_fab->get_desc()->get_product(target_ware_index)->get_factor()
			: (target_fab->get_desc()->get_supplier(target_ware_index) ? (uint32)target_fab->get_desc()->get_supplier(target_ware_index)->get_consumption() : 1ll);

		connection_status.set_rigid(true);
		if (!supplier) {
			add_component(&connection_status);
			new_component<gui_factory_storage_bar_t>(&target_ware, pfactor, true);
		}

		set_table_layout(6,1);
		button_t* b = new_component<button_t>();
		b->set_typ(button_t::posbutton_automatic);
		b->set_targetpos(target);

		add_component(&lb_name);
		lb_name.buf().printf("%s (%d,%d)", target_fab->get_name(), target.x, target.y);
		lb_name.update();

		// distance
		double distance;
		char distance_display[10];
		distance = (double)(shortest_distance(target, fab->get_pos().get_2d()) * world()->get_settings().get_meters_per_tile()) / 1000.0;
		if (distance < 1) {
			sprintf(distance_display, "%.0fm", distance * 1000);
		}
		else {
			uint n_actual = distance < 5 ? 1 : 0;
			char tmp[10];
			number_to_string(tmp, distance, n_actual);
			sprintf(distance_display, "%skm", tmp);
		}
		gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->buf().append(distance_display);
		lb->update();

		if (!supplier) {
			lb_leadtime.set_image(skinverwaltung_t::travel_time ? skinverwaltung_t::travel_time->get_image_id(0) : IMG_EMPTY);
			lb_leadtime.set_tooltip(translator::translate("symbol_help_txt_lead_time"));
			add_component(&lb_leadtime);
		}
		else {
			new_component<gui_factory_storage_bar_t>(&target_ware, pfactor, false);
			add_component(&connection_status);
			new_component<gui_margin_t>(D_MARGIN_RIGHT);
		}
	}

	void draw(scr_coord offset) OVERRIDE
	{
		fabrik_t *target_fab = fabrik_t::get_fab(target);
		if (win_get_magic((ptrdiff_t)target_fab)) {
			display_blend_wh_rgb(offset.x + get_pos().x, offset.y + get_pos().y, get_size().w, get_size().h, SYSCOL_TEXT_HIGHLIGHT, 30);
		}
		const bool has_halt = !target_fab->get_nearby_freight_halts().empty();
		const goods_desc_t *goods = ware->get_typ();

		lb_name.set_color(has_halt ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
		// connection check
		connection_status.set_visible(false);
		if (!target_fab->get_nearby_freight_halts().empty() && !fab->get_nearby_freight_halts().empty()) {

			// both factories have freight halt => check for the correct connection

			// connected with a suitable freight network?
			bool has_connection = fab->has_connection_with(target, goods->get_catg_index());
			connection_status.set_visible(has_connection);
			if (has_connection) {
				// check activity
				if (supplier) {
					// supplier's stock and shipment?
					uint8 forwarding_score = 0;
					FOR(array_tpl<ware_production_t>, const& product, target_fab->get_output()) {
						if (goods == product.get_typ()) {
							if (product.get_stat(0, FAB_GOODS_DELIVERED)) {
								// This factory is shipping this month.
								forwarding_score += 80;
							}
							if (product.get_stat(1, FAB_GOODS_DELIVERED)) {
								// This factory hasn't shipped this month yet, but it did last month.
								forwarding_score=min(100, forwarding_score+50);
							}
							break;
						}
					}
					if (!forwarding_score) {
						if (target_fab->is_staff_shortage()) {
							connection_status.set_color(COL_STAFF_SHORTAGE);
							connection_status.set_status(gui_operation_status_t::operation_stop);
						}
						else {
							connection_status.set_color(COL_INACTIVE);
							connection_status.set_status(gui_operation_status_t::operation_pause);
						}
					}
					else {
						connection_status.set_color(color_idx_to_rgb(severity_color[(forwarding_score+19)/20]));
						connection_status.set_status(gui_operation_status_t::operation_normal);
					}
				}
				else {
					uint8 shipping_score = 0;
					FOR(array_tpl<ware_production_t>, const& product, fab->get_output()) {
						if (goods == product.get_typ()) {
							sint32 shipment_demand_to_target=min(target_fab->goods_needed(goods), product.max_transit);
							if (shipment_demand_to_target <= 0){
								connection_status.set_status(gui_operation_status_t::operation_pause);
								connection_status.set_color(color_idx_to_rgb(COL_RED + 1));
								break;
							}

							if (product.get_stat(0, FAB_GOODS_DELIVERED)) {
								// This factory is shipping this month.
								shipping_score += 80;
							}
							if (product.get_stat(1, FAB_GOODS_DELIVERED)) {
								// This factory hasn't shipped this month yet, but it did last month.
								shipping_score = min(100, shipping_score + 50);
							}

							if (!shipping_score) {
								if (!product.get_stat(0, FAB_GOODS_STORAGE)) {
									connection_status.set_status(gui_operation_status_t::operation_stop);
									connection_status.set_color(fab->is_staff_shortage() ? COL_STAFF_SHORTAGE : SYSCOL_TEXT_WEAK);
								}
								else {
									// Stopped due to demand issue
									connection_status.set_status(gui_operation_status_t::operation_pause);
									connection_status.set_color(color_idx_to_rgb(COL_RED+1));
								}
							}
							else {
								connection_status.set_color(color_idx_to_rgb(severity_color[(shipping_score+19)/20]));
								connection_status.set_status(gui_operation_status_t::operation_normal);
							}
							break;
						}
					}
				}
			}
		}

		if (!supplier) {
			const uint32 lead_time = target_fab->get_lead_time(goods);
			if (lead_time == UINT32_MAX_VALUE) {
				lb_leadtime.buf().append("--:--:--");
			}
			else {
				char lead_time_as_clock[32];
				world()->sprintf_time_tenths(lead_time_as_clock, 32, lead_time);
				lb_leadtime.buf().append(lead_time_as_clock);
			}
			lb_leadtime.update();
		}
		gui_aligned_container_t::draw(offset);
	}

	bool infowin_event(const event_t *ev) OVERRIDE
	{
		if (IS_LEFTRELEASE(ev)) {
			if (ev->cx > 0 && ev->cx < 15) {
				world()->get_viewport()->change_world_position(target);
			}
			else {
				fabrik_t::get_fab(target)->show_info();
			}
		}
		else if (IS_RIGHTRELEASE(ev)) {
			world()->get_viewport()->change_world_position(target);
		}
		return false;
	}
};


gui_factory_connection_container_t::gui_factory_connection_container_t(fabrik_t *factory, bool yesno)
{
	this->fab = factory;
	this->is_supplier_display = yesno;
	old_connection_count = 0;

	set_table_layout(1,0);
	update_table();
	set_spacing(scr_size(D_H_SPACE, 0));
}

void gui_factory_connection_container_t::update_table()
{
	remove_all();
	if (!fab) return;
	if( is_supplier_display ) {
		old_connection_count = fab->get_suppliers().get_count();
		if (fab->get_suppliers().empty()) {
			new_component<gui_label_t>("material_not_available", COL_DANGER);
			return;
		}
	}
	else {
		old_connection_count = fab->get_consumers().get_count();
		if (fab->get_consumers().empty()) {
			new_component<gui_label_t>("missing_connection", COL_DANGER);
			return;
		}
	}

	uint8 index = 0;
	FORX(array_tpl<ware_production_t>, const& material, is_supplier_display ? fab->get_input() : fab->get_output(), index++) {
		uint32 found_in_catg=0;
		goods_desc_t const* const ware = material.get_typ();

		//gui_factory_product_item_t *goods_label =
		new_component<gui_factory_product_item_t>(fab, &material, is_supplier_display);
		if ( ware->is_available() ) {
			FOR(vector_tpl<koord>, target_pos, is_supplier_display ? fab->get_suppliers() : fab->get_consumers()) {
				const fabrik_t * fab2 = fabrik_t::get_fab(target_pos);
				if (is_supplier_display) {
					if(!fab2->get_produced_goods()->is_contained(ware)) continue;
				}
				else {
					if (!fab2->get_desc()->get_accepts_these_goods(ware)) continue;
				}

				new_component<gui_factory_connection_t>(fab, target_pos, index, is_supplier_display);

				found_in_catg++;
			}
		}

		if (!found_in_catg) {
			// has no link
			if (is_supplier_display && !fab->get_output().empty()) {
				// missing_connection
				new_component<gui_label_t>("material_not_available", COL_DANGER);
			}
			else {
				new_component<gui_label_t>("-", SYSCOL_TEXT_WEAK);
			}
		}
		new_component<gui_margin_t>(1, LINESPACE);
	}
}

void gui_factory_connection_container_t::draw(scr_coord offset)
{
	if ((is_supplier_display && fab->get_suppliers().get_count() != old_connection_count) || (!is_supplier_display && fab->get_consumers().get_count() != old_connection_count)) {
		update_table();
	}
	gui_aligned_container_t::draw(offset);
}


fabrik_info_t::fabrik_info_t(fabrik_t* fab_, const gebaeude_t* gb) :
	gui_frame_t(""),
	fab(fab_),
	chart(NULL),
	view(scr_size( max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width()*7)/8) )),
	txt(&info_buf),
	lb_staff_shortage(translator::translate("staff_shortage")),
	container_details(gb, get_titlecolor()),
	scroll_info(&container_info),
	scrolly_details(&container_details),
	cont_suppliers(fab_, true),
	cont_consumers(fab_, false),
	nearby_halts(fab_)
{
	if (fab) {
		init(fab, gb);
	}
}


void fabrik_info_t::init(fabrik_t* fab_, const gebaeude_t* gb)
{
	fab = fab_;
	// window name
	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name(fab->get_name());
	set_owner(fab->get_owner());
	staffing_bar.set_width(128);

	set_table_layout(1,0);
	add_table(2,1)->set_alignment(ALIGN_LEFT | ALIGN_TOP);
	{
		// left
		add_table(1,0);
		{
			add_table(2,1);
			{
				new_component<gui_factory_operation_status_t>(fab);

				// input name
				input.set_text( fabname, lengthof(fabname) );
				input.add_listener(this);
				add_component(&input);
			}
			end_table();

			// top part: production number & details, boost indicators, factory view

			// production details per input/output

			add_table(2,3);
			{
				new_component<gui_label_t>("Operation rate");
				add_component(&lb_operation_rate);

				new_component_span<gui_empty_t>(2);

				new_component<gui_label_t>("Productivity");
				add_component(&lb_productivity);
			}
			end_table();

			// indicator for possible boost by electricity, passengers, mail
			add_table(3,0)->set_spacing( scr_size(2,0) );
			{
				if ( fab->get_desc()->get_electric_boost() ) {
					boost_electric.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
					add_component(&boost_electric);
				}
				if ( fab->get_desc()->is_electricity_producer() ) {
					new_component<gui_label_t>("Electrical output: ");
				}
				else {
					new_component<gui_label_t>("Electrical demand: ");
				}
				add_component(&lb_electricity_demand);

				if (fab->get_building()) {
					// jobs
					if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
						new_component<gui_empty_t>();
						new_component<gui_label_t>("Jobs");
						add_table(1,2)->set_spacing(scr_size(0,0));
							add_table(2,1)->set_spacing( scr_size(D_H_SPACE,0) );
							{
								add_component(&lb_job_demand);

								if (skinverwaltung_t::alerts) {
									lb_staff_shortage.set_image(skinverwaltung_t::alerts->get_image_id(2));
								}
								lb_staff_shortage.set_visible(false);
								lb_staff_shortage.set_color(COL_STAFF_SHORTAGE);
								add_component(&lb_staff_shortage);
							}
							end_table();
							add_table(2,1);
							{
								staffing_bar.add_color_value(&staff_shortage_factor, COL_CAUTION);
								staffing_bar.add_color_value(&staffing_level, color_idx_to_rgb(COL_COMMUTER));
								staffing_bar.add_color_value(&staffing_level2, COL_STAFF_SHORTAGE);
								add_component(&staffing_bar);
								new_component<gui_fill_t>();
							}
							end_table();
						end_table();
					}

					boost_electric.set_rigid(true);
					boost_passenger.set_rigid(true);
					boost_mail.set_rigid(true);
					if (fab->get_desc()->get_pax_boost() ) {
						boost_passenger.set_image(skinverwaltung_t::passengers->get_image_id(0), true);
						add_component(&boost_passenger);
					}
					else {
						new_component<gui_empty_t>();
					}
					new_component<gui_label_t>("Visitor demand");
					add_component(&lb_visitor_demand);

					if (fab->get_desc()->get_mail_boost() ) {
						boost_mail.set_image(skinverwaltung_t::mail->get_image_id(0), true);
						add_component(&boost_mail);
					}
					else {
						new_component<gui_empty_t>();
					}
					new_component<gui_label_t>("Mail demand/output");
					add_component(&lb_mail_demand);
				}
			}
			end_table();
		}
		end_table();

		// right: view + city name
		add_table(1,2)->set_spacing(scr_size(0,0));
		{
			// world view object
			add_component(&view);
			view.set_obj(gb);

			add_table(2,1);
			{
				img_intown.set_image(skinverwaltung_t::intown->get_image_id(0), true);
				img_intown.set_rigid(false);
				img_intown.set_visible(false);
				add_component(&img_intown);
				add_component(&lb_city);
			}
			end_table();
		}
		end_table();

	}
	end_table();

	if (!fab->get_input().empty()) {
		new_component<gui_factory_storage_info_t>(fab, false);
		new_component<gui_empty_t>();
	}
	if (!fab->get_output().empty()) {
		new_component<gui_factory_storage_info_t>(fab, true);
		new_component<gui_empty_t>();
	}

	// tab panel: connections, chart panels, details
	switch_mode.add_listener(this);
	add_component(&switch_mode);
	scroll_info.set_maximize(true);
	switch_mode.add_tab(&scroll_info, translator::translate("Connections"));

	// connection information

	container_info.set_table_layout(1,0);
	container_info.new_component<gui_label_t>("Connected stops (freight)");
	container_info.add_component(&nearby_halts);
	container_info.new_component<gui_margin_t>(0, LINESPACE/2);
	container_info.add_table(2,1);
	{
		container_info.new_component<gui_fill_t>();
		bt_access_minimap.init(button_t::roundbox, "show_chain_on_minimap", scr_coord(0, 0), D_WIDE_BUTTON_SIZE);
		if (skinverwaltung_t::open_window) {
			bt_access_minimap.set_image(skinverwaltung_t::open_window->get_image_id(0));
			bt_access_minimap.set_image_position_right(true);
		}
		bt_access_minimap.set_tooltip("helptxt_access_minimap");
		bt_access_minimap.add_listener(this);
		container_info.add_component(&bt_access_minimap);
	}
	container_info.end_table();

	if (!fab->get_input().empty()) {
		tabs_factory_link.add_tab(&cont_suppliers, translator::translate("Suppliers"));
	}
	if (!fab->get_output().empty()) {
		tabs_factory_link.add_tab(&cont_consumers, translator::translate("Abnehmer"));
	}

	container_info.add_component(&tabs_factory_link);

	// initialize to zero, update_info will do the rest
	old_suppliers_count = 0;
	old_consumers_count = 0;

	update_info();

	// take-over chart tabs into our
	chart.set_factory(fab);
	switch_mode.take_tabs(chart.get_tab_panel());

	switch_mode.add_tab(&scrolly_details, translator::translate("Building info."));

	// factory description in tab
	{
		bool add_tab = false;
		details_buf.clear();

		// factory details
		char key[256];
		sprintf(key, "factory_%s_details", fab->get_desc()->get_name());
		const char * value = translator::translate(key);
		if(value && *value != 'f') {
			details_buf.append(value);
			add_tab = true;
		}

		if (add_tab) {
			switch_mode.add_tab(&container_details, translator::translate("Details"));
			container_details.set_table_layout(1,0);
			gui_flowtext_t* f = container_details.new_component<gui_flowtext_t>();
			f->set_text( (const char*)details_buf);
		}
	}
	container_details.add_component(&txt);
	fab->info_conn(info_buf);

	set_windowsize(get_min_windowsize());
	set_resizemode(gui_frame_t::diagonal_resize);
}


fabrik_info_t::~fabrik_info_t()
{
	rename_factory();
	fabname[0] = 0;
}


void fabrik_info_t::rename_factory()
{
	if(  fabname[0]  &&  welt->get_fab_list().is_contained(fab)  &&  strcmp(fabname, fab->get_name())  ) {
		// text changed and factory still exists => call tool
		cbuffer_t buf;
		buf.printf( "f%s,%s", fab->get_pos().get_str(), fabname );
		tool_t *tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
		tool->set_default_param( buf );
		welt->set_tool( tool, welt->get_public_player());
		// since init always returns false, it is safe to delete immediately
		delete tool;
	}
}


/**
 * Draw new component. The values to be passed refer to the window
 * i.e. It's the screen coordinates of the window where the
 * component is displayed.
 */
void fabrik_info_t::draw(scr_coord pos, scr_size size)
{
	if (world()->closed_factories_this_month.is_contained(fab))
	{
		return;
	}

	update_components();

	// boost stuff
	boost_electric.set_transparent(fab->get_prodfactor_electric()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));
	boost_passenger.set_transparent(fab->get_prodfactor_pax()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));
	boost_mail.set_transparent(fab->get_prodfactor_mail()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));

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
	lb_staff_shortage.set_visible(fab->is_staff_shortage());


	chart.update();

	gui_frame_t::draw(pos, size);
}


bool fabrik_info_t::is_weltpos()
{
	return ( welt->get_viewport()->is_on_center( get_weltpos(false) ) );
}


/**
 * This method is called if an action is triggered
 *
 * Returns true, if action is done and no more
 * components should be triggered.
 */
bool fabrik_info_t::action_triggered( gui_action_creator_t *comp, value_t)
{
	if(  comp == &switch_mode  ) {
		set_tab_opened();
	}
	if(  comp == &input  ) {
		rename_factory();
	}
	if(  comp==&bt_access_minimap  ) {
		map_frame_t *win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		if (!win) {
			create_win(-1, -1, new map_frame_t(), w_info, magic_reliefmap);
			win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		}
		win->activate_individual_network_mode( fab->get_pos().get_2d(), false );
		top_win(win);
	}
	return true;
}


void fabrik_info_t::set_tab_opened()
{
	resize(scr_coord(0, 0));
	tabstate = switch_mode.get_active_tab_index();
	const scr_coord_val margin_above_tab = switch_mode.get_pos().y + D_TAB_HEADER_HEIGHT + D_TITLEBAR_HEIGHT;
	scr_coord_val height = 0;
	switch (tabstate)
	{
		case 0: // info
		default:
			height = container_info.get_size().h;
			break;
		case 1: // goods chart
		case 2: // prod chart
			height = chart.get_size().h;
			break;
		case 3: // details
			height = container_details.get_size().h;
			break;
	}
	set_windowsize(scr_size(get_windowsize().w, min(display_get_height()-margin_above_tab, margin_above_tab+height)));
}


void fabrik_info_t::map_rotate90(sint16)
{
	// force update
	old_suppliers_count ++;
	old_consumers_count ++;
	update_components();
}

// update name and buffers
void fabrik_info_t::update_info()
{
	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name(fab->get_name());
	input.set_text( fabname, lengthof(fabname) );

	update_components();
}

// update all buffers
void fabrik_info_t::update_components()
{
	// update texts
	fab->info_conn( info_buf );

	// update labels
	if (fab->get_base_production()) {
		lb_operation_rate.buf().printf(": %.1f%% (%s: %.1f%%)", fab->get_stat(0,FAB_PRODUCTION)/100.0, translator::translate("Last Month"), fab->get_stat(1,FAB_PRODUCTION)/100.0);
		lb_operation_rate.update();

		lb_productivity.buf().printf(": %u%% ", fab->get_actual_productivity());
		const uint32 max_productivity = (100 * (fab->get_desc()->get_electric_boost() + fab->get_desc()->get_pax_boost() + fab->get_desc()->get_mail_boost())) >> DEFAULT_PRODUCTION_FACTOR_BITS;
		lb_productivity.buf().printf(translator::translate("(Max. %d%%)"), max_productivity + 100);
		lb_productivity.update();
	}

	if ( fab->get_desc()->is_electricity_producer() ) {
		lb_electricity_demand.buf().append(fab->get_scaled_electric_demand()>>POWER_TO_MW);
		lb_electricity_demand.buf().append(" MW");
	}
	else {
		lb_electricity_demand.buf().append((fab->get_scaled_electric_demand()*1000)>>POWER_TO_MW);
		lb_electricity_demand.buf().append(" KW");
	}
	lb_electricity_demand.update();

	if (gebaeude_t *gb=fab->get_building()) {
		if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
#ifdef DEBUG
			lb_job_demand.buf().printf(": %d/%d", gb->get_adjusted_jobs() - gb->check_remaining_available_jobs(), gb->get_adjusted_jobs());
#else
			lb_job_demand.buf().printf(": %d/%d", gb->get_adjusted_jobs() - max(0,gb->check_remaining_available_jobs()), gb->get_adjusted_jobs());
#endif
			lb_job_demand.set_fixed_width(lb_job_demand.get_min_size().w);
			lb_job_demand.update();
		}
		lb_visitor_demand.buf().printf(": %d", gb->get_adjusted_visitor_demand());
		lb_visitor_demand.update();

		lb_mail_demand.buf().printf(": %d", gb->get_adjusted_mail_demand());
		lb_mail_demand.update();
	}

	img_intown.set_visible(fab->get_city()!=NULL);
	if (fab->get_city() != NULL) {
		lb_city.buf().append(fab->get_city()->get_name());
	}
	lb_city.update();

	// consumers
	if(  fab->get_consumers().get_count() != old_consumers_count ) {
		cont_consumers.update_table();
		old_consumers_count = fab->get_consumers().get_count();
	}
	// suppliers
	if(  fab->get_suppliers().get_count() != old_suppliers_count ) {
		cont_suppliers.update_table();
		old_suppliers_count = fab->get_suppliers().get_count();
	}
	container_info.set_size(container_info.get_min_size());
	set_min_windowsize(scr_size(switch_mode.get_min_size().w, switch_mode.get_pos().y+D_TAB_HEADER_HEIGHT+D_TITLEBAR_HEIGHT));
	resize(scr_size(0,0));
	set_dirty();
}

/***************** Saveload stuff from here *****************/
void fabrik_info_t::rdwr( loadsave_t *file )
{
	// the factory first
	koord fabpos;
	if(  file->is_saving()  ) {
		fabpos = fab->get_pos().get_2d();
	}
	fabpos.rdwr( file );
	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	if(  file->is_loading()  ) {
		fab = fabrik_t::get_fab(fabpos );
		gebaeude_t* gb = welt->lookup_kartenboden( fabpos )->find<gebaeude_t>();

		if (fab != NULL  &&  gb != NULL) {
			init(fab, gb);
			cont_suppliers.set_fab(fab);
			cont_consumers.set_fab(fab);
			nearby_halts.set_fab(fab);

			container_details.init(gb, get_titlecolor());
		}
		win_set_magic(this, (ptrdiff_t)this);
	}
	chart.rdwr(file);
	scroll_info.rdwr(file);
	switch_mode.rdwr(file);

	if(  file->is_loading()  ) {
		reset_min_windowsize();
		set_windowsize(size);
		set_tab_opened();
	}
}
