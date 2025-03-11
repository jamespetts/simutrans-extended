#include "gui_vehicle_cargoinfo.h"

#include "gui_divider.h"
#include "gui_image.h"
#include "gui_label.h"
#include "gui_schedule_item.h"
#include "gui_colorbox.h"
#include "gui_destination_building_info.h"
#include "gui_vehicle_capacitybar.h"

#include "../../simconvoi.h"
#include "../../simcolor.h"
#include "../../simfab.h" // show_info

#include "../../dataobj/translator.h"
#include "../../halthandle_t.h"
#include "../../player/simplay.h"


#define LOADING_BAR_WIDTH 170
#define LOADING_BAR_HEIGHT 5

bool gui_cargo_info_t::sort_reverse = false;

static int compare_amount(const ware_t &a, const ware_t &b) {
	int comp = b.menge - a.menge;
	return gui_cargo_info_t::sort_reverse ? -comp : comp;
}

static int compare_index(const ware_t &a, const ware_t &b) {
	int comp = a.index - b.index;
	if (comp == 0) {
		comp = b.get_class() - a.get_class();
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_cargo_info_t::sort_reverse ? -comp : comp;
}

static int compare_via(const ware_t &a, const ware_t &b) {
	int comp = STRICMP(a.get_zwischenziel()->get_name(), b.get_zwischenziel()->get_name());
	if (comp == 0) {
		comp = STRICMP(a.get_ziel()->get_name(), b.get_ziel()->get_name());
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_cargo_info_t::sort_reverse ? -comp : comp;
}

static int compare_last_transfer(const ware_t &a, const ware_t &b) {
	int comp = STRICMP(a.get_last_transfer()->get_name(), b.get_last_transfer()->get_name());
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_cargo_info_t::sort_reverse ? -comp : comp;
}


gui_capacity_occupancy_bar_t::gui_capacity_occupancy_bar_t(vehicle_t *v, uint8 ac)
{
	veh=v;
	switch (veh->get_cargo_type()->get_catg_index())
	{
		case goods_manager_t::INDEX_PAS:
			a_class = min(goods_manager_t::passengers->get_number_of_classes()-1, ac);
			break;
		case goods_manager_t::INDEX_MAIL:
			a_class = min(goods_manager_t::mail->get_number_of_classes()-1, ac);
			break;
		default:
			a_class = 0;
			break;
	}
}

void gui_capacity_occupancy_bar_t::display_loading_bar(scr_coord_val xp, scr_coord_val yp, scr_coord_val w, scr_coord_val h, PIXVAL color, uint16 loading, uint16 capacity, uint16 overcrowd_capacity)
{
	if (capacity > 0 || overcrowd_capacity > 0) {
		// base
		display_fillbox_wh_clip_rgb(xp+1, yp, w*capacity / (capacity+overcrowd_capacity), h, color_idx_to_rgb(COL_GREY4), false);
		// dsiplay loading bar
		//display_cylinderbar_wh_clip_rgb(xp+1, yp, min(w*loading / (capacity+overcrowd_capacity), w*capacity / (capacity+overcrowd_capacity)), h, color, true);
		display_fillbox_wh_clip_rgb(xp+1, yp, min(w*loading / (capacity+overcrowd_capacity), w * capacity / (capacity+overcrowd_capacity)), h, color, true);
		display_blend_wh_rgb(xp+1, yp , w*loading / (capacity+overcrowd_capacity), 3, color_idx_to_rgb(COL_WHITE), 15);
		display_blend_wh_rgb(xp+1, yp+1, w*loading / (capacity+overcrowd_capacity), 1, color_idx_to_rgb(COL_WHITE), 15);
		display_blend_wh_rgb(xp+1, yp+h-1, w*loading / (capacity+overcrowd_capacity), 1, color_idx_to_rgb(COL_BLACK), 10);
		if (overcrowd_capacity && (loading > (capacity - overcrowd_capacity))) {
			display_fillbox_wh_clip_rgb(xp+1 + w * capacity / (capacity+overcrowd_capacity) + 1, yp-1, min(w*loading / (capacity+overcrowd_capacity), w * (loading-capacity) / (capacity+overcrowd_capacity)), h + 2, color_idx_to_rgb(COL_OVERCROWD), true);
			display_blend_wh_rgb(xp+1 + w*capacity / (capacity+overcrowd_capacity) + 1, yp, min(w*loading / (capacity+overcrowd_capacity), w * (loading-capacity) / (capacity+overcrowd_capacity)), 3, color_idx_to_rgb(COL_WHITE), 15);
			display_blend_wh_rgb(xp+1 + w*capacity / (capacity+overcrowd_capacity) + 1, yp + 1, min(w*loading / (capacity+overcrowd_capacity), w * (loading-capacity) / (capacity+overcrowd_capacity)), 1, color_idx_to_rgb(COL_WHITE), 15);
			display_blend_wh_rgb(xp+1 + w*capacity / (capacity+overcrowd_capacity) + 1, yp + h - 1, min(w*loading / (capacity+overcrowd_capacity), w * (loading-capacity) / (capacity+overcrowd_capacity)), 2, color_idx_to_rgb(COL_BLACK), 10);
		}

		// frame
		display_ddd_box_clip_rgb(xp, yp - 1, w * capacity / (capacity+overcrowd_capacity) + 2, h + 2, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY0));
		// overcrowding frame
		if (overcrowd_capacity) {
			display_direct_line_dotted_rgb(xp+1 + w, yp - 1, xp+1 + w * capacity / (capacity+overcrowd_capacity) + 1, yp - 1, 1, 1, color_idx_to_rgb(MN_GREY0));  // top
			display_direct_line_dotted_rgb(xp+1 + w, yp - 2, xp+1 + w, yp + h, 1, 1, color_idx_to_rgb(MN_GREY0));  // right. start from dot
			display_direct_line_dotted_rgb(xp+1 + w, yp + h, xp+1 + w * capacity / (capacity+overcrowd_capacity) + 1, yp + h, 1, 1, color_idx_to_rgb(MN_GREY0)); // bottom
		}
	}
}

void gui_capacity_occupancy_bar_t::draw(scr_coord offset)
{
	if (!veh) {
		return;
	}
	offset += pos;
	if (veh->get_accommodation_capacity(a_class) > 0 || veh->get_overcrowded_capacity(a_class)) {
		switch (veh->get_cargo_type()->get_catg_index())
		{
			case goods_manager_t::INDEX_PAS:
			case goods_manager_t::INDEX_MAIL:
				display_loading_bar(offset.x, offset.y, size.w, LOADING_BAR_HEIGHT, veh->get_cargo_type()->get_color(), veh->get_total_cargo_by_class(a_class), veh->get_fare_capacity(veh->get_reassigned_class(a_class)), veh->get_overcrowded_capacity(a_class));
				break;
			default:
				// draw the "empty" loading bar
				display_loading_bar(offset.x, offset.y, size.w, LOADING_BAR_HEIGHT, color_idx_to_rgb(COL_GREY4), 0, 1, 0);

				int bar_start_offset = 0;
				int cargo_sum = 0;
				FOR(slist_tpl<ware_t>, const ware, veh->get_cargo(0))
				{
					goods_desc_t const* const wtyp = ware.get_desc();
					cargo_sum += ware.menge;

					// draw the goods loading bar
					int bar_end_offset = cargo_sum * size.w / veh->get_desc()->get_total_capacity();
					PIXVAL goods_color = wtyp->get_color();
					if (bar_end_offset - bar_start_offset) {
						display_cylinderbar_wh_clip_rgb(offset.x+bar_start_offset+1, offset.y, bar_end_offset - bar_start_offset, LOADING_BAR_HEIGHT, goods_color, true);
					}
					bar_start_offset += bar_end_offset - bar_start_offset;
				}
				break;
		}
	}
}

scr_size gui_capacity_occupancy_bar_t::get_min_size() const
{
	return scr_size(LOADING_BAR_WIDTH, LOADING_BAR_HEIGHT);
}

scr_size gui_capacity_occupancy_bar_t::get_max_size() const
{
	return scr_size(size_fixed ? LOADING_BAR_WIDTH : scr_size::inf.w, LOADING_BAR_HEIGHT);
}


gui_vehicle_cargo_info_t::gui_vehicle_cargo_info_t(vehicle_t *v, uint8 filter_mode)
{
	veh=v;
	show_loaded_detail = filter_mode;
	schedule = veh->get_convoi()->get_schedule();
	set_table_layout(1,0);

	set_alignment(ALIGN_LEFT | ALIGN_CENTER_V);
	update();
}

void gui_vehicle_cargo_info_t::update()
{
	total_cargo = veh->get_total_cargo();
	remove_all();

	const uint8 number_of_classes = goods_manager_t::get_classes_catg_index(veh->get_cargo_type()->get_catg_index());
	const bool is_pass_veh = veh->get_cargo_type()->get_catg_index() == goods_manager_t::INDEX_PAS;
	const bool is_mail_veh = veh->get_cargo_type()->get_catg_index() == goods_manager_t::INDEX_MAIL;

	for (uint8 ac = 0; ac < number_of_classes; ac++) { // accommo class index
		if (!veh->get_desc()->get_capacity(ac) && !veh->get_overcrowded_capacity(ac)) {
			continue; // no capacity for this accommo class
		}

		add_table(2,1);
		{
			new_component<gui_capacity_occupancy_bar_t>(veh, ac);
			//Loading time
			char min_loading_time_as_clock[32];
			char max_loading_time_as_clock[32];
			world()->sprintf_ticks(min_loading_time_as_clock, sizeof(min_loading_time_as_clock), veh->get_desc()->get_min_loading_time());
			world()->sprintf_ticks(max_loading_time_as_clock, sizeof(max_loading_time_as_clock), veh->get_desc()->get_max_loading_time());
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf(" %s %s - %s", translator::translate("Loading time:"), min_loading_time_as_clock, max_loading_time_as_clock);
			lb->update();
		}
		end_table();

		add_table(4,1);
		{
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			// [fare class name/catgname]
			if (number_of_classes>1) {
				lb->buf().append(goods_manager_t::get_translated_fare_class_name(veh->get_cargo_type()->get_catg_index(), veh->get_reassigned_class(ac)));
			}
			else {
				lb->buf().append(translator::translate(veh->get_desc()->get_freight_type()->get_catg_name()));
			}
			lb->buf().append(":");
			// [capacity]
			lb->buf().printf("%4d/%3d", veh->get_total_cargo_by_class(ac),veh->get_desc()->get_capacity(ac));
			if (veh->get_overcrowded_capacity(ac)) {
				lb->buf().printf("(%u)", veh->get_overcrowded_capacity(ac));
			}
			lb->update();

			// [accommo class name]
			if (number_of_classes>1) {
				lb = new_component<gui_label_buf_t>(SYSCOL_EDIT_TEXT_DISABLED, gui_label_t::left);
				if (veh->get_reassigned_class(ac) != ac) {
					lb->buf().printf("(*%s)", goods_manager_t::get_default_accommodation_class_name(veh->get_cargo_type()->get_catg_index(), ac));
				}
				lb->update();
			}
			else {
				new_component<gui_empty_t>();
			}

			// [comfort(pax) / mixload prohibition(freight)]
			if (is_pass_veh) {
				add_table(3,1);
				{
					if (is_pass_veh && skinverwaltung_t::comfort) {
						new_component<gui_image_t>(skinverwaltung_t::comfort->get_image_id(0), 0, ALIGN_NONE, true);
					}
					lb = new_component<gui_label_buf_t>();
					lb->buf().printf("%s %3i", translator::translate("Comfort:"), veh->get_comfort(veh->get_convoi()->get_catering_level(goods_manager_t::INDEX_PAS), veh->get_reassigned_class(ac)));
					lb->update();
					// Check for reduced comfort
					const sint64 comfort_diff = veh->get_comfort(veh->get_convoi()->get_catering_level(goods_manager_t::INDEX_PAS), veh->get_reassigned_class(ac)) - veh->get_desc()->get_adjusted_comfort(veh->get_convoi()->get_catering_level(goods_manager_t::INDEX_PAS),ac);
					if (comfort_diff!=0) {
						add_table(2,1)->set_spacing(scr_size(1,0));
						{
							new_component<gui_fluctuation_triangle_t>(comfort_diff);
							lb = new_component<gui_label_buf_t>(comfort_diff>0 ? SYSCOL_UP_TRIANGLE : SYSCOL_DOWN_TRIANGLE, gui_label_t::left);
							lb->buf().printf("%i", comfort_diff);
							lb->update();
						}
						end_table();
					}
				}
				end_table();
			}
			else if (veh->get_desc()->get_mixed_load_prohibition()) {
				lb = new_component<gui_label_buf_t>();
				lb->buf().append( translator::translate("(mixed_load_prohibition)") );
				lb->set_color(color_idx_to_rgb(COL_BRONZE)); // FIXME: color optimization for dark theme
				lb->update();
			}

			new_component<gui_fill_t>();
		}
		end_table();

		if( show_loaded_detail==hide_detail ) {
			continue;
		}

		gui_aligned_container_t *tbl = add_table(2, 0);
		tbl->set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT, D_V_SPACE));
		tbl->set_table_frame(true, true);
		{
			if (!total_cargo) {
				// no cargo => empty
				new_component<gui_label_t>("leer", SYSCOL_TEXT_WEAK, gui_label_t::left);
				new_component<gui_fill_t>();
			}
			else {
				add_table(2,0)->set_spacing(scr_size(0,1));
				// The cargo list is displayed in the order of stops with reference to the schedule of the convoy.
				vector_tpl<vector_tpl<ware_t>> fracht_array(number_of_classes);
				slist_tpl<koord3d> temp_list; // check for duplicates
				for (uint8 i = 0; i < schedule->get_count(); i++) {
					fracht_array.clear();
					uint8 e; // schedule(halt) entry number
					if (veh->get_convoi()->get_reverse_schedule() || (schedule->is_mirrored() && veh->get_convoi()->is_reversed())) {
						e = (schedule->get_current_stop() + schedule->get_count() - i) % schedule->get_count();
						if (schedule->is_mirrored() && (schedule->get_current_stop()<i) ) {
							break;
						}
					}
					else {
						e = (schedule->get_current_stop() + i) % schedule->get_count();
						if (schedule->is_mirrored() && (schedule->get_current_stop() + i) >= schedule->get_count()) {
							break;
						}
					}
					halthandle_t const halt = haltestelle_t::get_halt(schedule->entries[e].pos, veh->get_convoi()->get_owner());
					if (!halt.is_bound()) {
						continue;
					}
					if (temp_list.is_contained(halt->get_basis_pos3d())) {
						break; // The convoy came to the same station twice.
					}
					temp_list.append(halt->get_basis_pos3d());

					// ok, now count cargo
					uint16 sum_of_heading_to_this_halt = 0;

					// build the cargo list heading to this station by "wealth" class
					for (uint8 wc = 0; wc < number_of_classes; wc++) { // wealth class
						vector_tpl<ware_t> this_iteration_vector(veh->get_cargo(ac).get_count());
						FOR(slist_tpl<ware_t>, ware, veh->get_cargo(ac)) {
							if (ware.get_zwischenziel().is_bound() && ware.get_zwischenziel() == halt) {
								if (ware.get_class() == wc) {
									// merge items of the same class to the same destination
									bool merge = false;
									FOR(vector_tpl<ware_t>, &recorded_ware, this_iteration_vector) {
										if (ware.get_index() == recorded_ware.get_index() &&
											ware.get_class() == recorded_ware.get_class() &&
											ware.is_commuting_trip == recorded_ware.is_commuting_trip
											)
										{
											if( show_loaded_detail==by_final_destination  &&  ware.get_zielpos()!=recorded_ware.get_zielpos() ) {
												continue;
											}
											if( show_loaded_detail==by_destination_halt &&  ware.get_ziel()!=recorded_ware.get_ziel() ) {
												continue;
											}
											recorded_ware.menge += ware.menge;
											merge = true;
											break;
										}
									}
									if (!merge) {
										this_iteration_vector.append(ware);
									}
									sum_of_heading_to_this_halt += ware.menge;
								}
							}
						}
						fracht_array.append(this_iteration_vector);
					}

					// now display the list
					if (sum_of_heading_to_this_halt) {
						// via halt
						new_component<gui_margin_t>(LINESPACE); // most left
						add_table(5,1)->set_spacing(scr_size(D_H_SPACE,0));
						{
							gui_label_buf_t *lb = new_component<gui_label_buf_t>();
							lb->buf().printf("%u%s ", sum_of_heading_to_this_halt, translator::translate(veh->get_cargo_type()->get_mass()));
							lb->update();
							lb->set_fixed_width(lb->get_min_size().w+D_H_SPACE);
							new_component<gui_label_t>("To:");
							// schedule number
							const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;
							new_component<gui_schedule_entry_number_t>(e, halt->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								halt->get_basis_pos3d()
								);

							// stop name
							lb = new_component<gui_label_buf_t>();
							lb->buf().append(halt->get_name());
							lb->update();

							new_component<gui_fill_t>();
						}
						end_table();

						new_component<gui_empty_t>(); // most left
						add_table(2,1); // station list
						{
							new_component<gui_margin_t>(LINESPACE); // left margin

							add_table(2,0)->set_alignment(ALIGN_TOP); // wealth list
							{
								for (uint8 wc = 0; wc < number_of_classes; wc++) { // wealth class
									uint32 wealth_sum = 0;
									if (fracht_array[wc].get_count()) {
										gui_label_buf_t *lb_wealth_total = new_component<gui_label_buf_t>();
										lb_wealth_total->set_size(scr_size(0,0));
										add_table(1,0); // for destination list
										{
											add_table(4,0)->set_spacing(scr_size(D_H_SPACE,1)); // for destination list
											{
												FOR(vector_tpl<ware_t>, w, fracht_array[wc]) {
													if (!w.menge) {
														continue;
													}
													// 1. goods color box
													const PIXVAL goods_color = (w.is_passenger() && w.is_commuting_trip) ? color_idx_to_rgb(COL_COMMUTER) : w.get_desc()->get_color();
													new_component<gui_colorbox_t>(goods_color)->set_size(GOODS_COLOR_BOX_SIZE);

													// 2. goods name
													if (!w.is_passenger() && !w.is_mail()) {
														new_component<gui_label_t>(w.get_name());
													}
													else {
														new_component<gui_empty_t>();
													}

													// 3. goods amount and unit
													gui_label_buf_t *lb = new_component<gui_label_buf_t>();
													lb->buf().printf("%u", w.menge);
													if (w.is_passenger()) {
														if (w.menge == 1) {
															lb->buf().printf(" %s", w.is_commuting_trip ? translator::translate("commuter") : translator::translate("visitor"));
														}
														else {
															lb->buf().printf(" %s", w.is_commuting_trip ? translator::translate("commuters") : translator::translate("visitors"));
														}
													}
													else {
														lb->buf().append(translator::translate(veh->get_cargo_type()->get_mass()));
													}
													lb->update();
													lb->set_fixed_width(lb->get_min_size().w);

													// 4. destination halt
													if(  show_loaded_detail==by_final_destination ||
														(show_loaded_detail==by_destination_halt  &&  w.get_ziel()!=w.get_zwischenziel()) ){
														add_table( show_loaded_detail==by_destination_halt ? 4:5, 1);
														{
															// final destination building
															if (show_loaded_detail == by_final_destination) {
																if (is_pass_veh || is_mail_veh) {
																	if (skinverwaltung_t::on_foot) {
																		new_component<gui_image_t>(skinverwaltung_t::on_foot->get_image_id(0), 0, ALIGN_CENTER_V, true);
																	}
																	else {
																		new_component<gui_label_t>(" > ");
																	}
																}
																else {
																	new_component<gui_image_t>(skinverwaltung_t::goods->get_image_id(0), 0, ALIGN_CENTER_V, true);
																}
																new_component<gui_destination_building_info_t>(w.get_zielpos(), w.is_freight());
															}

															// via halt(=w.get_ziel())
															if (show_loaded_detail == by_destination_halt) {
																new_component<gui_label_t>(" > ");
															}
															else if (w.get_ziel().is_bound() && w.get_ziel() != w.get_zwischenziel()) {
																new_component<gui_label_t>(" via");
															}

															if (w.get_ziel().is_bound() && w.get_ziel()!=w.get_zwischenziel()) {
																const bool is_interchange = (w.get_ziel().get_rep()->registered_lines.get_count() + w.get_ziel().get_rep()->registered_convoys.get_count()) > 1;
																new_component<gui_schedule_entry_number_t>(-1, w.get_ziel().get_rep()->get_owner()->get_player_color1(),
																	is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
																	scr_size(LINESPACE+4, LINESPACE),
																	w.get_ziel().get_rep()->get_basis_pos3d()
																	);

																lb = new_component<gui_label_buf_t>();
																lb->buf().printf("%s", w.get_ziel()->get_name());
																lb->update();

																if( show_loaded_detail==by_destination_halt ) {
																	// distance
																	const uint32 distance = shortest_distance(w.get_zwischenziel().get_rep()->get_basis_pos(), w.get_ziel().get_rep()->get_basis_pos()) * world()->get_settings().get_meters_per_tile();
																	lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
																	if (distance < 1000) {
																		lb->buf().printf("%um", distance);
																	}
																	else if (distance < 20000) {
																		lb->buf().printf("%.1fkm", (double)distance / 1000.0);
																	}
																	else {
																		lb->buf().printf("%ukm", distance / 1000);
																	}
																	lb->update();
																}
															}
														}
														end_table();
													}
													else {
														new_component<gui_fill_t>();
													}

													wealth_sum += w.menge;
												}
											}
											end_table();
										}
										end_table();

										if (number_of_classes > 1 && wealth_sum) {
											lb_wealth_total->buf().printf("%s: %u%s", goods_manager_t::get_translated_wealth_name(veh->get_cargo_type()->get_catg_index(), wc), wealth_sum, translator::translate(veh->get_cargo_type()->get_mass()));
											lb_wealth_total->set_visible(true);
										}
										else {
											lb_wealth_total->set_visible(false);
											lb_wealth_total->set_rigid(true);
											lb_wealth_total->set_size(scr_size(5,0));
										}
										lb_wealth_total->update();
										lb_wealth_total->set_size(lb_wealth_total->get_min_size());

										new_component_span<gui_empty_t>(2); // vertical margin between loading wealth classes
									}
								}
							}
							end_table();
						}
						end_table();

						new_component_span<gui_empty_t>(2); // vertical margin between stations
					}
				}
				end_table();
			}
		}
		end_table();
	}
	gui_aligned_container_t::set_size(get_min_size());
}

void gui_vehicle_cargo_info_t::draw(scr_coord offset)
{
	if (veh->get_total_cargo() != total_cargo) {
		update();
	}
	set_visible(veh->get_desc()->get_total_capacity() || veh->get_desc()->get_overcrowded_capacity());
	if (veh->get_desc()->get_total_capacity() || veh->get_desc()->get_overcrowded_capacity()) {
		gui_aligned_container_t::set_size(get_min_size());
		gui_aligned_container_t::draw(offset);
	}
	else {
		gui_aligned_container_t::set_size(scr_size(0,0));
	}
}


gui_cargo_info_t::gui_cargo_info_t(convoihandle_t cnv)
{
	this->cnv = cnv;
}

void gui_cargo_info_t::init(uint8 info_depth_from, uint8 info_depth_to, bool divide_by_wealth, uint8 sort_mode, uint8 catg_index, uint8 fare_class)
{
	if( cnv.is_bound() ) {
		set_table_layout(4,0);
		set_alignment(ALIGN_LEFT | ALIGN_CENTER_V);
		set_spacing(scr_size(D_H_SPACE, 2));

		uint16 max_goods_count=0; // for colorbar width

		slist_tpl<ware_t> cargoes;

		for( uint8 v=0; v<cnv->get_vehicle_count(); ++v ) {
			const vehicle_t* veh = cnv->get_vehicle(v);
			const uint8 g_classes = veh->get_desc()->get_number_of_classes();

			// goods/fare class filter
			if (catg_index!=goods_manager_t::INDEX_NONE) {
				if (veh->get_desc()->get_freight_type()->get_catg_index() != catg_index) {
					continue;
				}
				if( !veh->get_fare_capacity(fare_class) &&  !veh->get_overcrowded_capacity(fare_class)  ) {
					continue;
				}
			}

			for(uint8 ac=0; ac<g_classes; ++ac) {
				if (catg_index!=goods_manager_t::INDEX_NONE  &&  veh->get_reassigned_class(ac)!=fare_class) {
					continue;
				}
				FOR(slist_tpl<ware_t>, ware, veh->get_cargo(ac)) {
					bool merged = false;
					FOR(slist_tpl<ware_t>, &recorded_ware, cargoes) {
						// If meet the conditions for not being able to merge, go to the next step.

						if( recorded_ware.get_index() != ware.get_index()) continue;
						// Compare goods type and class and check if cargo can be merged
						if (divide_by_wealth) {
							if( ware.is_passenger()  &&  recorded_ware.is_commuting_trip!=ware.is_commuting_trip ) continue;
							if(g_classes  &&  recorded_ware.get_class()!=ware.get_class() ) continue;
						}

						// Compare origin and destination and check if cargo can be merged
						//      zwischenziel=via stop, ziel=destination stop, zielpos=destination building
						if( info_depth_to    &&  recorded_ware.get_zwischenziel()!=ware.get_zwischenziel() ) continue;
						if( info_depth_to>1  &&  recorded_ware.get_ziel()!=ware.get_ziel() ) continue;
						if( info_depth_to==3 &&  recorded_ware.get_zielpos()!=ware.get_zielpos() ) continue;

						if( info_depth_from    &&  recorded_ware.get_last_transfer()!=ware.get_last_transfer() ) continue;
						if( info_depth_from==2 &&  recorded_ware.get_origin()!=ware.get_origin() ) continue;

						// mergeable
						recorded_ware.menge += ware.menge;
						max_goods_count = max(max_goods_count, recorded_ware.menge);
						merged = true;
						break;
					}

					if(!merged){
						max_goods_count = max(max_goods_count, ware.menge);
						if (!divide_by_wealth  &&   g_classes) {
							ware.set_class(0); // Unify unnecessary data for sorting
						}
						cargoes.append(ware);
					}
				}
			}
		}

		// Do this only if the display is valid.
		// Otherwise, the sorting parameter has been corrupted at merging
		// and sorting will not be correct.
		switch (sort_mode)
		{
			case by_amount:
				cargoes.sort(compare_amount);
				break;
			case by_via:
				if( info_depth_to ){
					cargoes.sort(compare_via);
				}
				break;
			case by_origin:
				if( info_depth_from ) {
					cargoes.sort(compare_last_transfer);
				}
				break;
			case by_category:
			default:
				cargoes.sort(compare_index);
				break;
		}

		if (max_goods_count) {
			FOR(slist_tpl<ware_t>, &ware, cargoes) {
				// col1, horizontal color bar
				const scr_coord_val width = (HALT_WAITING_BAR_MAX_WIDTH*ware.menge+max_goods_count-1)/max_goods_count;
				const PIXVAL barcolor = (divide_by_wealth && ware.is_commuting_trip) ? color_idx_to_rgb(COL_COMMUTER) : color_idx_to_rgb(goods_manager_t::get_info(ware.get_index())->get_color_index());
				add_table(2,2)->set_spacing(NO_SPACING);
				{
					new_component<gui_margin_t>(HALT_WAITING_BAR_MAX_WIDTH-width);
					new_component<gui_capacity_bar_t>(scr_size(width, GOODS_COLOR_BOX_HEIGHT), barcolor)->set_show_frame(false);

					// extra 2nd row for control alignment
					new_component_span<gui_margin_t>(0, (info_depth_from&&info_depth_to) ? max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT):0, 2);
				}
				end_table();

				// col2 cargo type, amount
				add_table(3,2)->set_spacing(scr_size(D_V_SPACE,0));
				{
					// col2-1
					gui_label_buf_t *lb = new_component<gui_label_buf_t>();
					lb->buf().printf("%3u%s", ware.menge, translator::translate(ware.get_mass()));
					lb->update();
					lb->set_fixed_width(lb->get_min_size().w);

					// col2-2
					if (catg_index==goods_manager_t::INDEX_NONE) {
						// show category  symbol
						new_component<gui_image_t>(goods_manager_t::get_info(ware.get_index())->get_catg_symbol(), 0, 0, true);
					}
					else {
						new_component<gui_empty_t>();
					}

					// col2-3
					lb = new_component<gui_label_buf_t>();
					// goods name
					if (ware.is_passenger() && divide_by_wealth) {
						if (ware.menge == 1) {
							lb->buf().printf(" %s", ware.is_commuting_trip ? translator::translate("commuter") : translator::translate("visitor"));
						}
						else {
							lb->buf().printf(" %s", ware.is_commuting_trip ? translator::translate("commuters") : translator::translate("visitors"));
						}
					}
					else {
						lb->buf().append( translator::translate(ware.get_name()) );
					}
					if( (goods_manager_t::get_info(ware.get_index())->get_number_of_classes()>1)  &&  divide_by_wealth ) {
						lb->buf().printf(" (%s)", goods_manager_t::get_translated_wealth_name(ware.get_index(), ware.get_class()));
					}
					lb->update();

					// extra 2nd row for control alignment
					new_component_span<gui_margin_t>(0, (info_depth_from&&info_depth_to) ? max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT):0, 3);
				}
				end_table();

				// col3
				add_table(1,2)->set_spacing(NO_SPACING);
				{
					// upper row: origin/from
					add_table(4,1)->set_spacing(scr_size(D_H_SPACE,0));
					{
						if( info_depth_from  &&  ware.get_last_transfer().is_bound() ) {
							new_component<gui_label_t>( (info_depth_from>1 && ware.get_origin().is_bound() && (ware.get_last_transfer()==ware.get_origin())) ?
								(ware.is_passenger() ? "Origin:" : "Shipped from:") : (ware.is_passenger() ? "Boarding from:" : "Loaded at:")
								);
							bool is_interchange = (ware.get_last_transfer().get_rep()->registered_lines.get_count() + ware.get_last_transfer().get_rep()->registered_convoys.get_count()) > 1;
							new_component<gui_schedule_entry_number_t>(cnv->get_schedule()->get_entry_index(ware.get_last_transfer(), cnv->get_owner(), !cnv->is_reversed()),
								ware.get_last_transfer().get_rep()->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								ware.get_last_transfer().get_rep()->get_basis_pos3d()
								);
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
							lb->buf().append(ware.get_last_transfer()->get_name());
							lb->update();

							if( info_depth_from>1  &&  ware.get_origin().is_bound()  &&  (ware.get_last_transfer()!=ware.get_origin()) ) {
								add_table(3,1);
								{
									new_component<gui_label_t>(ware.is_passenger() ? "Origin:" : "Shipped from:");
									is_interchange = (ware.get_origin().get_rep()->registered_lines.get_count() + ware.get_origin().get_rep()->registered_convoys.get_count()) > 1;
									new_component<gui_schedule_entry_number_t>(-1, ware.get_origin().get_rep()->get_owner()->get_player_color1(),
										is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
										scr_size(LINESPACE + 4, LINESPACE),
										ware.get_origin().get_rep()->get_basis_pos3d()
										);
									lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
									lb->buf().append(ware.get_origin()->get_name());
									lb->update();
								}
								end_table();
							}
						}
					}
					end_table();

					// lower row: via/to
					if( info_depth_to  &&  ware.get_zwischenziel().is_bound() ) {
						add_table(5,1)->set_spacing(scr_size(D_H_SPACE,0));
						{
							if( info_depth_from ) new_component<gui_margin_t>(LINESPACE); // add left margin for lower row
							const bool display_goal_halt = (info_depth_to>1)  &&  ware.get_ziel().is_bound()  &&  (ware.get_zwischenziel()!=ware.get_ziel());
							new_component<gui_label_t>(display_goal_halt ? "Via:" : "To:");
							bool is_interchange = (ware.get_zwischenziel().get_rep()->registered_lines.get_count() + ware.get_zwischenziel().get_rep()->registered_convoys.get_count()) > 1;
							new_component<gui_schedule_entry_number_t>(cnv->get_schedule()->get_entry_index(ware.get_zwischenziel(), cnv->get_owner(), cnv->is_reversed()),
								ware.get_zwischenziel().get_rep()->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								ware.get_zwischenziel().get_rep()->get_basis_pos3d()
								);
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
							lb->buf().append(ware.get_zwischenziel()->get_name());
							if( display_goal_halt ) {
								lb->buf().append(" > ");
							}
							lb->update();
							add_table(4,1);
							{
								if( display_goal_halt ) {
									new_component<gui_label_t>("To:");
									is_interchange = (ware.get_ziel().get_rep()->registered_lines.get_count() + ware.get_ziel().get_rep()->registered_convoys.get_count()) > 1;
									new_component<gui_schedule_entry_number_t>(-1, ware.get_ziel().get_rep()->get_owner()->get_player_color1(),
										is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
										scr_size(LINESPACE + 4, LINESPACE),
										ware.get_ziel().get_rep()->get_basis_pos3d()
										);
									lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
									lb->buf().append(ware.get_ziel()->get_name());
									lb->update();
								}
								if( info_depth_to==3 ) {
									// final destination building
									add_table(3,1);
									{
										new_component<gui_label_t>(" ...");
										if( skinverwaltung_t::on_foot && (ware.is_passenger() || ware.is_mail()) ) {
											new_component<gui_image_t>(skinverwaltung_t::on_foot->get_image_id(0), 0, ALIGN_CENTER_V, true);
										}
										new_component<gui_destination_building_info_t>(ware.get_zielpos(), ware.is_freight());
									}
									end_table();
								}
							}
							end_table();
						}
						end_table();
					}
				}
				end_table();

				// col4
				new_component<gui_fill_t>();
			}
		}
		else {
			new_component<gui_margin_t>(LINEASCENT);
			new_component<gui_label_t>("leer", SYSCOL_TEXT_WEAK);
			new_component<gui_fill_t>();
		}
		set_size(get_min_size());
	}
}

gui_convoy_cargo_info_t::gui_convoy_cargo_info_t(convoihandle_t cnv)
{
	this->cnv=cnv;
	update();
}

void gui_convoy_cargo_info_t::set_mode(uint8 depth_from, uint8 depth_to, bool wealth_yesno, bool separated_fare, uint8 mode)
{
	info_depth_from = depth_from;
	info_depth_to   = depth_to;
	divide_by_wealth = wealth_yesno;
	separate_by_fare = separated_fare;
	sort_mode = mode;
	update();
}

void gui_convoy_cargo_info_t::update()
{
	remove_all();
	if( !cnv.is_bound() ) { return; }

	old_vehicle_count = cnv->get_vehicle_count();
	old_total_cargo   = cnv->get_total_cargo();

	set_table_layout(1,0);
	set_table_frame(true, true);
	set_alignment(ALIGN_LEFT | ALIGN_TOP);

	if( !info_depth_from  &&  !info_depth_to  ) {
		// simple
		new_component<gui_convoy_loading_info_t>(linehandle_t(), cnv, true);
	}
	else if (separate_by_fare) {

		const minivec_tpl<uint8> &goods_catg_index = cnv->get_goods_catg_index();
		for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
			if (!goods_catg_index.is_contained(catg_index)) {
				continue;
			}
			const uint8 g_classes = goods_manager_t::get_classes_catg_index(catg_index);
			for (uint8 i = 0; i < g_classes; i++) {
				if (cnv->get_unique_fare_capacity(catg_index,i)) {
					add_table(4,1);
					{
						new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(),0,0, true);

						const uint16 fare_capacity = cnv->get_unique_fare_capacity(catg_index, i);
						const uint16 total_cargo   = cnv->get_total_cargo_by_fare_class(catg_index, i);
						const uint16 loading_rate  = fare_capacity ? 1000*total_cargo/fare_capacity : 0;

						gui_label_buf_t *lb = new_component<gui_label_buf_t>();
						if( g_classes > 1 ) {
							lb->buf().append(goods_manager_t::get_translated_fare_class_name(catg_index,i));
						}
						else {
							lb->buf().append(translator::translate(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name()));
						}

						lb->buf().printf(": %i/%i ", total_cargo, fare_capacity);
						if( total_cargo ) {
							lb->buf().printf("%s ", translator::translate("loaded"));
						}
						lb->update();

						if( loading_rate>1000  &&  SYMBOL_OVERCROWDING) {
							new_component<gui_image_t>(SYMBOL_OVERCROWDING, 0, ALIGN_CENTER_V, true);
						}

						// loading rate
						lb = new_component<gui_label_buf_t>(total_cargo > fare_capacity ? SYSCOL_OVERCROWDED : SYSCOL_TEXT);
						if (loading_rate>0) {
							lb->buf().printf("(%.1f%%)", (float)loading_rate/10.0);
						}
						else {
							lb->buf().append("(0%)");
						}
						lb->update();
					}
					end_table();

					new_component<gui_cargo_info_t>(cnv)->init(info_depth_from, info_depth_to, divide_by_wealth, sort_mode, catg_index, i);
					new_component<gui_border_t>();
				}
			}
		}

	}
	else {
		new_component<gui_cargo_info_t>(cnv)->init(info_depth_from, info_depth_to, divide_by_wealth, sort_mode);
	}
	new_component<gui_fill_t>(false, true);

	set_size(get_min_size());
}

void gui_convoy_cargo_info_t::draw(scr_coord offset)
{
	if( cnv.is_bound() ) {
		bool need_update = false;
		if( cnv->get_vehicle_count() != old_vehicle_count ) {
			need_update = true;
		}
		else if( cnv->get_total_cargo() != old_total_cargo ) {
			need_update = true;
		}

		if (need_update) update();
		gui_aligned_container_t::draw(offset);
	}
}
