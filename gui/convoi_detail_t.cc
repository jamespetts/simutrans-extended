/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "convoi_detail_t.h"
#include "components/gui_chart.h"
#include "components/gui_image.h"
#include "components/gui_colorbox.h"
#include "components/gui_divider.h"
#include "components/gui_schedule_item.h"

#include "../simconvoi.h"
#include "../simcolor.h"
#include "../simunits.h"
#include "../simworld.h"
#include "../simline.h"
#include "../simhalt.h"

#include "../dataobj/environment.h"
#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../dataobj/loadsave.h"

#include "../player/simplay.h"

#include "../utils/simstring.h"
#include "../utils/cbuffer_t.h"

#include "simwin.h"

#include "vehicle_class_manager.h"

#include "../display/simgraph.h"
#include "../display/viewport.h"

// for gui_vehicle_cargo_info_t
#include "../simcity.h"
#include "../obj/gebaeude.h"

#define LOADING_BAR_WIDTH 170
#define LOADING_BAR_HEIGHT 5
#define CHART_HEIGHT (100)
#define L_COL_ACCEL_FULL COL_ORANGE_RED
#define L_COL_ACCEL_EMPTY COL_DODGER_BLUE

sint16 convoi_detail_t::tabstate = -1;

class convoy_t;

static const uint8 physics_curves_color[MAX_PHYSICS_CURVES] =
{
	COL_WHITE,
	COL_GREEN-1,
	L_COL_ACCEL_FULL,
	L_COL_ACCEL_EMPTY,
	COL_PURPLE+1,
	COL_DARK_SLATEBLUE
};

static const uint8 curves_type[MAX_PHYSICS_CURVES] =
{
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::KMPH,
	gui_chart_t::FORCE,
	gui_chart_t::FORCE
};

static const gui_chart_t::chart_marker_t marker_type[MAX_PHYSICS_CURVES] = {
	gui_chart_t::none, gui_chart_t::cross, gui_chart_t::square, gui_chart_t::diamond,
	gui_chart_t::diamond, gui_chart_t::cross
};

static const char curve_name[MAX_PHYSICS_CURVES][64] =
{
	"",
	"Acceleration(actual)",
	"Acceleration(full load)",
	"Acceleration(empty)",
	"Tractive effort",
	"Running resistance"
};

static char const* const chart_help_text[] =
{
	"",
	"helptxt_actual_acceleration",
	"Acceleration graph when nothing is loaded on the convoy",
	"helptxt_vt_graph_full_load",
	"helptxt_fv_graph_tractive_effort",
	"Total force acting in the opposite direction of the convoy"
};

static char const* const txt_loaded_display[4] =
{
	"Hide loaded detail",
	"Unload halt",
	"Destination halt",
	"Final destination"
};



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

	for (uint8 ac = 0; ac < number_of_classes; ac++) { // accomo class index
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
			gui_label_buf_t *lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
			lb->buf().printf(" %s %s - %s", translator::translate("Loading time:"), min_loading_time_as_clock, max_loading_time_as_clock);
			lb->update();
		}
		end_table();

		add_table(4,1);
		{
			gui_label_buf_t *lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
			// [fare class name/catgname]
			if (number_of_classes>1) {
				lb->buf().append(goods_manager_t::get_translated_wealth_name(veh->get_cargo_type()->get_catg_index(), veh->get_reassigned_class(ac)));
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

			// [accomo class name]
			if (number_of_classes>1) {
				lb = new_component_span<gui_label_buf_t>(SYSCOL_EDIT_TEXT_DISABLED, gui_label_t::left);
				if (veh->get_reassigned_class(ac) != ac) {
					lb->buf().printf("(*%s)", goods_manager_t::get_translated_wealth_name(veh->get_cargo_type()->get_catg_index(), ac));
					// UI TODO: A.Carlotti has proposed making it possible to define an accommodation name that is unique to the class capacity of the vehicle.
					// But not yet implemented.
					//lb->buf().printf("%s:", veh->get_translated_accommodation_class_name(ac));
				}
				lb->update();
			}
			else {
				new_component<gui_empty_t>();
			}

			// [comfort(pax) / mixload prohibition(freight)]
			lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
			if (is_pass_veh) {
				lb->buf().printf(" %s %3i", translator::translate("Comfort:"), veh->get_comfort(veh->get_convoi()->get_catering_level(goods_manager_t::INDEX_PAS), veh->get_reassigned_class(ac)));
			}
			else if (veh->get_desc()->get_mixed_load_prohibition()) {
				lb->buf().append( translator::translate("(mixed_load_prohibition)") );
				lb->set_color(color_idx_to_rgb(COL_BRONZE)); // FIXME: color optimization for dark theme
			}
			lb->update();

			new_component<gui_fill_t>();
		}
		end_table();

		if( show_loaded_detail==hide_detail ) {
			continue;
		}

		if (!total_cargo) {
			// no cargo => empty
			new_component<gui_label_t>("leer", SYSCOL_TEXT_WEAK, gui_label_t::left);
			new_component<gui_empty_t>();
		}
		else {
			add_table(2,0);
			// The cargo list is displayed in the order of stops with reference to the schedule of the convoy.
			vector_tpl<vector_tpl<ware_t>> fracht_array(number_of_classes);
			slist_tpl<koord3d> temp_list; // check for duplicates
			for (uint8 i = 0; i < schedule->get_count(); i++) {
				fracht_array.clear();
				uint8 e; // schedule(halt) entry number
				if (veh->get_convoi()->get_reverse_schedule()) {
					e = (schedule->get_current_stop() + schedule->get_count() - i) % schedule->get_count();
					if (schedule->is_mirrored() && (schedule->get_current_stop() - i) <= 0) {
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
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
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
						lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
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
									gui_label_buf_t *lb_wealth_total = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
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
												new_component<gui_colorbox_t>(goods_color)->set_size( scr_size((LINESPACE>>1)+2, (LINESPACE>>1)+2) );

												// 2. goods name
												if (!w.is_passenger() && !w.is_mail()) {
													new_component<gui_label_t>(w.get_name());
												}
												else {
													new_component<gui_empty_t>();
												}

												// 3. goods amount and unit
												gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
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
													add_table( show_loaded_detail==by_destination_halt ? 4:7, 1);
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
															// destination building
															cbuffer_t dbuf;
															if (const grund_t* gr = world()->lookup_kartenboden(w.get_zielpos())) {
																if (const gebaeude_t* const gb = gr->get_building()) {
																	gb->get_description(dbuf);
																}
																else {
																	dbuf.append(translator::translate("Unknown destination"));
																}
																lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
																lb->buf().append(dbuf.get_str());
																lb->update();
																lb->set_fixed_width(lb->get_min_size().w+D_H_SPACE);

																const stadt_t* city = world()->get_city(w.get_zielpos());
																if (city) {
																	new_component<gui_image_t>(skinverwaltung_t::intown->get_image_id(0), 0, ALIGN_CENTER_V, true);
																}
																else {
																	new_component<gui_empty_t>();
																}

																// pos
																lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);

																if (city) {
																	lb->buf().append( city->get_name() );
																}
																lb->buf().printf(" %s ", w.get_zielpos().get_fullstr());
																lb->update();
															}
															else {
																new_component_span<gui_label_t>("Unknown destination", 3);
															}
														}

														// via halt(=w.get_ziel())
														new_component<gui_label_t>(show_loaded_detail==by_destination_halt ? " > " : " via");

														if (w.get_ziel().is_bound()) {
															const bool is_interchange = (w.get_ziel().get_rep()->registered_lines.get_count() + w.get_ziel().get_rep()->registered_convoys.get_count()) > 1;
															new_component<gui_schedule_entry_number_t>(-1, w.get_ziel().get_rep()->get_owner()->get_player_color1(),
																is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
																scr_size(LINESPACE+4, LINESPACE),
																w.get_ziel().get_rep()->get_basis_pos3d()
																);

															lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
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
														else {
															new_component_span<gui_label_t>("unknown", show_loaded_detail==by_destination_halt ? 3:2); // e.g. halt was removed
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
	gui_aligned_container_t::set_size(get_min_size());
}

void gui_vehicle_cargo_info_t::draw(scr_coord offset)
{
	if (veh->get_total_cargo() != total_cargo) {
		update();
	}
	set_visible(veh->get_cargo_max());
	if (veh->get_cargo_max()) {
		gui_aligned_container_t::set_size(get_min_size());
		gui_aligned_container_t::draw(offset);
	}
	else {
		gui_aligned_container_t::set_size(scr_size(0,0));
	}
}


// helper class
gui_acceleration_label_t::gui_acceleration_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x=D_H_SPACE;
	cbuffer_t buf;
	lazy_convoy_t &convoy = *cnv.get_rep();
	const sint32 friction = convoy.get_current_friction();
	const double starting_acceleration = convoy.calc_acceleration(weight_summary_t(cnv->get_weight_summary().weight, friction), 0).to_double() / 1000.0;
	const double starting_acceleration_max = convoy.calc_acceleration(weight_summary_t(cnv->get_vehicle_summary().weight, friction), 0).to_double() / 1000.0;
	const double starting_acceleration_min = convoy.calc_acceleration(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), 0).to_double() / 1000.0;

	buf.clear();
	if (starting_acceleration_min == starting_acceleration_max || starting_acceleration_max == 0.0) {
		buf.printf("%.2f km/h/s", starting_acceleration_min);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, starting_acceleration_min == 0.0 ? COL_DANGER : SYSCOL_TEXT, true);
	}
	else {
		if (!cnv->in_depot()) {
			// show actual value
			buf.printf("%.2f km/h/s", starting_acceleration);
			total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_x += D_H_SPACE;
			total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, "(", ALIGN_LEFT, SYSCOL_TEXT, true);
		}
		buf.clear();
		buf.printf("%.2f", starting_acceleration_min);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL-1), true);

		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		buf.printf("%.2f km/h/s", starting_acceleration_max);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY-1), true);
		if (!cnv->in_depot()) {
			total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, ")", ALIGN_LEFT, SYSCOL_TEXT, true);
		}
	}

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


gui_acceleration_time_label_t::gui_acceleration_time_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_time_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x = D_H_SPACE;
	cbuffer_t buf;
	vector_tpl<const vehicle_desc_t*> vehicles;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicles.append(cnv->get_vehicle(i)->get_desc());
	}
	potential_convoy_t convoy(vehicles);
	const sint32 friction = convoy.get_current_friction();
	const sint32 max_weight = convoy.get_vehicle_summary().weight + convoy.get_freight_summary().max_freight_weight;
	const sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
	const double min_time = convoy.calc_acceleration_time(weight_summary_t(cnv->get_vehicle_summary().weight, friction), min_speed);
	const double max_time = convoy.calc_acceleration_time(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), min_speed);

	buf.clear();
	if (min_time == 0.0) {
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, translator::translate("Unreachable"), ALIGN_LEFT, COL_DANGER, true);
	}
	else if (min_time == max_time) {
		buf.printf(translator::translate("%.2f sec"), min_time);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
	}
	else {
		buf.printf(translator::translate("%.2f sec"), min_time);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY-1), true);
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		if (max_time == 0.0) {
			buf.append(translator::translate("Unreachable"));
		}
		else {
			buf.printf(translator::translate("%.2f sec"), max_time);
		}
		total_x += display_proportional_clip_rgb(pos.x+offset.x+total_x, pos.y+offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL-1), true);
	}
	total_x += D_H_SPACE;

	buf.clear();
	buf.printf(translator::translate("@ %d km/h"), min_speed);
	total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), true);

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


gui_acceleration_dist_label_t::gui_acceleration_dist_label_t(convoihandle_t c)
{
	cnv = c;
}

void gui_acceleration_dist_label_t::draw(scr_coord offset)
{
	scr_coord_val total_x = D_H_SPACE;
	cbuffer_t buf;
	vector_tpl<const vehicle_desc_t*> vehicles;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicles.append(cnv->get_vehicle(i)->get_desc());
	}
	potential_convoy_t convoy(vehicles);
	const sint32 friction = convoy.get_current_friction();
	const sint32 max_weight = convoy.get_vehicle_summary().weight + convoy.get_freight_summary().max_freight_weight;
	const sint32 min_speed = convoy.calc_max_speed(weight_summary_t(max_weight, friction));
	const uint32 min_distance = convoy.calc_acceleration_distance(weight_summary_t(cnv->get_vehicle_summary().weight, friction), min_speed);
	const uint32 max_distance = convoy.calc_acceleration_distance(weight_summary_t(cnv->get_vehicle_summary().weight + cnv->get_freight_summary().max_freight_weight, friction), min_speed);

	buf.clear();
	if (min_distance == 0.0) {
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, translator::translate("Unreachable"), ALIGN_LEFT, COL_DANGER, true);
	}
	else if (min_distance == max_distance) {
		buf.printf(translator::translate("%u m"), min_distance);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
	}
	else {
		buf.printf(translator::translate("%u m"), min_distance);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_EMPTY - 1), true);
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, " - ", ALIGN_LEFT, SYSCOL_TEXT, true);
		buf.clear();
		if (max_distance == 0.0) {
			buf.append(translator::translate("Unreachable"));
		}
		else {
			buf.printf(translator::translate("%u m"), max_distance);
		}
		total_x += display_proportional_clip_rgb(pos.x + offset.x + total_x, pos.y + offset.y, buf, ALIGN_LEFT, color_idx_to_rgb(L_COL_ACCEL_FULL - 1), true);
	}

	scr_size size(total_x + D_H_SPACE, LINEASCENT);
	if (size != get_size()) {
		set_size(size);
	}
	gui_container_t::draw(offset);
}


// main class
convoi_detail_t::convoi_detail_t(convoihandle_t cnv) :
	gui_frame_t(""),
	veh_info(cnv),
	formation(cnv),
	cont_payload_info(cnv),
	maintenance(cnv),
	scrolly_formation(&formation),
	scrolly(&veh_info),
	scrolly_payload_info(&cont_payload_info),
	scrolly_maintenance(&maintenance)
{
	if (cnv.is_bound()) {
		init(cnv);
	}
}


void convoi_detail_t::init(convoihandle_t cnv)
{
	this->cnv = cnv;
	gui_frame_t::set_name(cnv->get_name());
	gui_frame_t::set_owner(cnv->get_owner());

	set_table_layout(1,0);
	add_table(3,2)->set_spacing(scr_size(0,0));
	{
		// 1st row
		add_component(&lb_vehicle_count);
		new_component<gui_fill_t>();

		class_management_button.init(button_t::roundbox, "class_manager", scr_coord(0, 0), scr_size(D_BUTTON_WIDTH*1.5, D_BUTTON_HEIGHT));
		class_management_button.set_tooltip("see_and_change_the_class_assignments");
		add_component(&class_management_button);
		class_management_button.add_listener(this);

		// 2nd row
		new_component_span<gui_empty_t>(2);

		for (uint8 i = 0; i < gui_convoy_formation_t::CONVOY_OVERVIEW_MODES; i++) {
			overview_selctor.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(gui_convoy_formation_t::cnvlist_mode_button_texts[i]), SYSCOL_TEXT);
		}
		overview_selctor.set_selection(gui_convoy_formation_t::convoy_overview_t::formation);
		overview_selctor.set_width_fixed(true);
		overview_selctor.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_EDIT_HEIGHT));
		overview_selctor.add_listener(this);
		add_component(&overview_selctor);
	}
	end_table();

	// 3rd row: convoy overview
	scrolly_formation.set_show_scroll_x(true);
	scrolly_formation.set_show_scroll_y(false);
	scrolly_formation.set_scroll_discrete_x(false);
	scrolly_formation.set_size_corner(false);
	scrolly_formation.set_maximize(true);
	add_component(&scrolly_formation);

	new_component<gui_margin_t>(0, D_V_SPACE);

	add_component(&tabs);
	tabs.add_tab(&scrolly, translator::translate("cd_spec_tab"));
	tabs.add_tab(&cont_payload_tab, translator::translate("cd_payload_tab"));
	tabs.add_tab(&cont_maintenance,  translator::translate("cd_maintenance_tab"));
	tabs.add_tab(&container_chart, translator::translate("cd_physics_tab"));
	tabs.add_listener(this);

	// content of payload info tab
	cont_payload_tab.set_table_layout(1,0);

	cont_payload_tab.add_table(4,3)->set_spacing(scr_size(0,0));
	{
		cont_payload_tab.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT, 0));

		cont_payload_tab.new_component<gui_label_t>("Loading time:");
		cont_payload_tab.add_component(&lb_loading_time);
		cont_payload_tab.new_component<gui_margin_t>(D_H_SPACE*2);

		cont_payload_tab.add_table(3,1)->set_spacing(scr_size(0, 0));
		{
			cont_payload_tab.new_component<gui_label_t>("Display loaded detail")->set_tooltip("Displays detailed information of the vehicle's load.");
			for(uint8 i=0; i<4; i++) {
				cb_loaded_detail.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(txt_loaded_display[i]), SYSCOL_TEXT);
			}
			cont_payload_tab.add_component(&cb_loaded_detail);
			cb_loaded_detail.add_listener(this);
			cb_loaded_detail.set_selection(1);
			cont_payload_tab.new_component<gui_fill_t>();
		}
		cont_payload_tab.end_table();

		if (cnv->get_catering_level(goods_manager_t::INDEX_PAS)) {
			cont_payload_tab.new_component<gui_label_t>("Catering level");
			cont_payload_tab.add_component(&lb_catering_level);
			cont_payload_tab.new_component<gui_margin_t>(D_H_SPACE*2);
			cont_payload_tab.new_component<gui_empty_t>();
		}
		if (cnv->get_catering_level(goods_manager_t::INDEX_MAIL)) {
			cont_payload_tab.new_component_span<gui_label_t>("traveling_post_office",4);
		}
	}
	cont_payload_tab.end_table();

	cont_payload_tab.add_component(&scrolly_payload_info);
	scrolly_payload_info.set_maximize(true);

	// content of maintenance tab
	cont_maintenance.set_table_layout(1,0);
	cont_maintenance.add_table(5,3)->set_spacing(scr_size(0,0));
	{
		cont_maintenance.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT,0));
		// 1st row
		cont_maintenance.new_component<gui_label_t>("Restwert:");
		cont_maintenance.add_component(&lb_value);
		cont_maintenance.new_component<gui_fill_t>();

		retire_button.init(button_t::roundbox, "Retire", scr_coord(BUTTON3_X, D_BUTTON_HEIGHT), D_BUTTON_SIZE);
		retire_button.set_tooltip("Convoi is sent to depot when all wagons are empty.");
		retire_button.add_listener(this);
		cont_maintenance.add_component(&retire_button);

		sale_button.init(button_t::roundbox, "Verkauf", scr_coord(0,0), D_BUTTON_SIZE);
		if (skinverwaltung_t::alerts) {
			sale_button.set_image(skinverwaltung_t::alerts->get_image_id(3));
		}
		sale_button.set_tooltip("Remove vehicle from map. Use with care!");
		sale_button.add_listener(this);
		cont_maintenance.add_component(&sale_button);

		// 2nd row
		cont_maintenance.new_component<gui_label_t>("Odometer:");
		cont_maintenance.add_component(&lb_odometer);
		cont_maintenance.new_component<gui_fill_t>();

		cont_maintenance.new_component<gui_empty_t>();
		withdraw_button.init(button_t::roundbox, "withdraw", scr_coord(0,0), D_BUTTON_SIZE);
		withdraw_button.set_tooltip("Convoi is sold when all wagons are empty.");
		withdraw_button.add_listener(this);
		cont_maintenance.add_component(&withdraw_button);
	}
	cont_maintenance.end_table();

	cont_maintenance.add_component(&scrolly_maintenance);
	scrolly_maintenance.set_maximize(true);

	container_chart.set_table_layout(1,0);
	container_chart.add_table(3,0)->set_spacing(scr_size(0,1));
	{
		container_chart.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_MARGIN_RIGHT,0));
		container_chart.new_component<gui_label_t>("Starting acceleration:")->set_tooltip(translator::translate("helptxt_starting_acceleration"));
		lb_acceleration = container_chart.new_component<gui_acceleration_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();

		container_chart.new_component<gui_label_t>("time_to_top_speed:")->set_tooltip(translator::translate("helptxt_acceleration_time"));
		lb_acc_time = container_chart.new_component<gui_acceleration_time_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();

		container_chart.new_component<gui_label_t>("distance_required_to_top_speed:")->set_tooltip(translator::translate("helptxt_acceleration_distance"));
		lb_acc_distance = container_chart.new_component<gui_acceleration_dist_label_t>(cnv);
		container_chart.new_component<gui_fill_t>();
	}
	container_chart.end_table();
	container_chart.add_component(&switch_chart);

	switch_chart.add_tab(&cont_accel, translator::translate("v-t graph"), NULL, translator::translate("helptxt_v-t_graph"));
	switch_chart.add_tab(&cont_force, translator::translate("f-v graph"), NULL, translator::translate("helptxt_f-v_graph"));

	cont_accel.set_table_layout(1,0);
	cont_accel.add_component(&accel_chart);
	accel_chart.set_dimension(SPEED_RECORDS, 10000);
	accel_chart.set_background(SYSCOL_CHART_BACKGROUND);
	accel_chart.set_min_size(scr_size(0, CHART_HEIGHT));

	cont_accel.add_table(4,0);
	{
		for (uint8 btn = 0; btn < MAX_ACCEL_CURVES; btn++) {
			for (uint8 i = 0; i < SPEED_RECORDS; i++) {
				accel_curves[i][btn] = 0;
			}
			sint16 curve = accel_chart.add_curve(color_idx_to_rgb(physics_curves_color[btn]), (sint64*)accel_curves, MAX_ACCEL_CURVES, btn, SPEED_RECORDS, curves_type[btn], false, true, btn==0 ? 0:1, NULL, marker_type[btn]);

			button_t *b = cont_accel.new_component<button_t>();
			b->init(button_t::box_state_automatic | button_t::flexible, curve_name[btn]);
			b->background_color = color_idx_to_rgb(physics_curves_color[btn]);
			b->set_tooltip(translator::translate(chart_help_text[btn]));
			// always show the max speed reference line and the full load acceleration graph is displayed by default
			b->pressed = (btn == 0) || (cnv->in_depot() && btn == 2);
			b->set_visible(btn!=0);

			btn_to_accel_chart.append(b, &accel_chart, curve);
			if (b->pressed) {
				accel_chart.show_curve(btn);
			}
		}
	}
	cont_accel.end_table();

	cont_force.set_table_layout(1,0);
	cont_force.add_component(&force_chart);
	force_chart.set_dimension(SPEED_RECORDS, 10000);
	force_chart.set_background(SYSCOL_CHART_BACKGROUND);
	force_chart.set_min_size(scr_size(0, CHART_HEIGHT));

	cont_force.add_table(4,0);
	{
		for (uint8 btn = 0; btn < MAX_FORCE_CURVES; btn++) {
			for (uint8 i = 0; i < SPEED_RECORDS; i++) {
				force_curves[i][btn] = 0;
			}
			sint16 force_curve = force_chart.add_curve(color_idx_to_rgb(physics_curves_color[MAX_ACCEL_CURVES + btn]), (sint64*)force_curves, MAX_FORCE_CURVES, btn, SPEED_RECORDS, curves_type[MAX_ACCEL_CURVES + btn], false, true, 3, NULL, marker_type[MAX_ACCEL_CURVES + btn]);

			button_t *bf = cont_force.new_component<button_t>();
			bf->init(button_t::box_state_automatic | button_t::flexible, curve_name[MAX_ACCEL_CURVES + btn]);
			bf->background_color = color_idx_to_rgb(physics_curves_color[MAX_ACCEL_CURVES + btn]);
			bf->set_tooltip(translator::translate(chart_help_text[MAX_ACCEL_CURVES + btn]));
			bf->pressed = (btn == 0);
			if (bf->pressed) {
				force_chart.show_curve(btn);
			}

			btn_to_force_chart.append(bf, &force_chart, force_curve);
		}
	}
	cont_force.end_table();

	if (cnv->in_depot()) {
		tabs.set_active_tab_index(3);
	}

	update_labels();

	reset_min_windowsize();
	set_windowsize(scr_size(D_DEFAULT_WIDTH, tabs.get_pos().y + container_chart.get_size().h));
	set_resizemode(diagonal_resize);
}

void convoi_detail_t::set_tab_opened()
{
	const scr_coord_val margin_above_tab = D_TITLEBAR_HEIGHT + tabs.get_pos().y;
	scr_coord_val ideal_size_h = margin_above_tab + D_MARGIN_BOTTOM;
	switch (tabstate)
	{
		case 0: // spec
		default:
			ideal_size_h += veh_info.get_size().h;
			break;
		case 1: // loaded detail
			ideal_size_h += cont_payload_tab.get_size().h;
			break;
		case 2: // maintenance
			ideal_size_h += cont_maintenance.get_size().h + D_V_SPACE*2;
			break;
		case 3: // chart
			ideal_size_h += container_chart.get_size().h + D_V_SPACE*2;
			break;
	}
	if (get_windowsize().h != ideal_size_h) {
		set_windowsize(scr_size(get_windowsize().w, min(display_get_height() - margin_above_tab, ideal_size_h)));
	}
}

void convoi_detail_t::update_labels()
{
	lb_vehicle_count.buf().printf("%s %i", translator::translate("Fahrzeuge:"), cnv->get_vehicle_count());
	if( cnv->front()->get_waytype()!=water_wt ) {
		lb_vehicle_count.buf().printf(" (%s %i)", translator::translate("Station tiles:"), cnv->get_tile_length());
	}
	lb_vehicle_count.update();

	// update the convoy overview panel
	formation.set_mode(overview_selctor.get_selection());

	// contents of payload tab
	{
		// convoy min - max loading time
		char min_loading_time_as_clock[32];
		char max_loading_time_as_clock[32];
		welt->sprintf_ticks(min_loading_time_as_clock, sizeof(min_loading_time_as_clock), cnv->calc_longest_min_loading_time());
		welt->sprintf_ticks(max_loading_time_as_clock, sizeof(max_loading_time_as_clock), cnv->calc_longest_max_loading_time());
		lb_loading_time.buf().printf(" %s - %s", min_loading_time_as_clock, max_loading_time_as_clock);
		lb_loading_time.update();

		// convoy (max) catering level
		if (cnv->get_catering_level(goods_manager_t::INDEX_PAS)) {
			lb_catering_level.buf().printf(": %i", cnv->get_catering_level(goods_manager_t::INDEX_PAS));
			lb_catering_level.update();
		}
	}

	// contents of maintenance tab
	{
		char number[64];
		number_to_string(number, (double)cnv->get_total_distance_traveled(), 0);
		lb_odometer.buf().append(" ");
		lb_odometer.buf().printf(translator::translate("%s km"), number);
		lb_odometer.update();

		// current resale value
		money_to_string(number, cnv->calc_sale_value() / 100.0);
		lb_value.buf().printf(" %s", number);
		lb_value.update();
	}

	if (tabstate == 1) {
		const sint64 seed_temp = cnv->is_reversed() + cnv->get_vehicle_count() + cnv->get_sum_weight() + cb_loaded_detail.get_selection();
		if (old_seed != seed_temp) {
			// something has changed => update
			old_seed = seed_temp;
			cont_payload_info.update_list();
		}
	}

	set_min_windowsize(scr_size(max(D_DEFAULT_WIDTH, get_min_windowsize().w), D_TITLEBAR_HEIGHT + tabs.get_pos().y + D_TAB_HEADER_HEIGHT));
	resize(scr_coord(0, 0));
}


void convoi_detail_t::draw(scr_coord pos, scr_size size)
{
	if(!cnv.is_bound()) {
		destroy_win(this);
		return;
	}

	if(cnv->get_owner()==welt->get_active_player()  &&  !welt->get_active_player()->is_locked()) {
		withdraw_button.enable();
		sale_button.enable();
		retire_button.enable();
		if ((cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS) && goods_manager_t::passengers->get_number_of_classes()>1)
			|| (cnv->get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL) && goods_manager_t::mail->get_number_of_classes()>1)) {
			class_management_button.enable();
		}
		else {
			class_management_button.disable();
		}
		if (cnv->in_depot()) {
			retire_button.disable();
			withdraw_button.disable();
		}
	}
	else {
		withdraw_button.disable();
		sale_button.disable();
		retire_button.disable();
		class_management_button.disable();
	}
	withdraw_button.pressed = cnv->get_withdraw();
	retire_button.pressed = cnv->get_depot_when_empty();
	class_management_button.pressed = win_get_magic(magic_class_manager);

	if (tabs.get_active_tab_index()==3) {
		// common existing_convoy_t for acceleration curve and weight/speed info.
		convoi_t &convoy = *cnv.get_rep();

		// create dummy convoy and calcurate theoretical acceleration curve
		vector_tpl<const vehicle_desc_t*> vehicles;
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
		{
			vehicles.append(cnv->get_vehicle(i)->get_desc());
		}
		potential_convoy_t empty_convoy(vehicles);
		potential_convoy_t dummy_convoy(vehicles);
		const sint32 min_weight = dummy_convoy.get_vehicle_summary().weight;
		const sint32 max_freight_weight = dummy_convoy.get_freight_summary().max_freight_weight;

		const sint32 akt_speed_soll = kmh_to_speed(convoy.calc_max_speed(convoy.get_weight_summary()));
		const sint32 akt_speed_soll_ = dummy_convoy.get_vehicle_summary().max_sim_speed;
		float32e8_t akt_v = 0;
		float32e8_t akt_v_min = 0;
		float32e8_t akt_v_max = 0;
		sint32 akt_speed = 0;
		sint32 akt_speed_min = 0;
		sint32 akt_speed_max = 0;
		sint32 sp_soll = 0;
		sint32 sp_soll_min = 0;
		sint32 sp_soll_max = 0;
		int i = SPEED_RECORDS - 1;
		long delta_t = 1000;
		sint32 delta_s = (welt->get_settings().ticks_to_seconds(delta_t)).to_sint32();
		accel_curves[i][1] = akt_speed;
		accel_curves[i][2] = akt_speed_min;
		accel_curves[i][3] = akt_speed_max;

		if (env_t::left_to_right_graphs) {
			accel_chart.set_seed(delta_s * (SPEED_RECORDS - 1));
			accel_chart.set_x_axis_span(delta_s);
		}
		else {
			accel_chart.set_seed(0);
			accel_chart.set_x_axis_span(0 - delta_s);
		}
		accel_chart.set_abort_display_x(0);

		uint32 empty_weight = convoy.get_vehicle_summary().weight;
		const sint32 max_speed = convoy.calc_max_speed(weight_summary_t(empty_weight, convoy.get_current_friction()));
		while (i > 0 && max_speed>0)
		{
			empty_convoy.calc_move(welt->get_settings(), delta_t, weight_summary_t(min_weight, empty_convoy.get_current_friction()), akt_speed_soll_, akt_speed_soll_, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed_min, sp_soll_min, akt_v_min);
			dummy_convoy.calc_move(welt->get_settings(), delta_t, weight_summary_t(min_weight+max_freight_weight, dummy_convoy.get_current_friction()), akt_speed_soll_, akt_speed_soll_, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed_max, sp_soll_max, akt_v_max);
			convoy.calc_move(welt->get_settings(), delta_t, akt_speed_soll, akt_speed_soll, SINT32_MAX_VALUE, SINT32_MAX_VALUE, akt_speed, sp_soll, akt_v);
			if (env_t::left_to_right_graphs) {
				accel_curves[--i][1] = cnv->in_depot() ? 0 : akt_speed;
				accel_curves[i][2] = akt_speed_max;
				accel_curves[i][3] = akt_speed_min;
			}
			else {
				accel_curves[SPEED_RECORDS-i][1] = cnv->in_depot() ? 0 : akt_speed;
				accel_curves[SPEED_RECORDS-i][2] = akt_speed_max;
				accel_curves[SPEED_RECORDS-i][3] = akt_speed_min;
				i--;
			}
		}
		// for max speed reference line
		for (i = 0; i < SPEED_RECORDS; i++) {
			accel_curves[i][0] = empty_convoy.get_vehicle_summary().max_sim_speed;
		}

		// force chart
		if (max_speed > 0) {
			const uint16 display_interval = (max_speed + SPEED_RECORDS-1) / SPEED_RECORDS;
			float32e8_t rolling_resistance = cnv->get_adverse_summary().fr;
			te_curve_abort_x = max(2,(uint8)((max_speed + (display_interval-1)) / display_interval));
			force_chart.set_abort_display_x(te_curve_abort_x);
			force_chart.set_dimension(te_curve_abort_x, 10000);

			if (env_t::left_to_right_graphs) {
				force_chart.set_seed(display_interval * (SPEED_RECORDS-1));
				force_chart.set_x_axis_span(display_interval);
				for (i = 0; i < max_speed; i++) {
					if (i % display_interval == 0) {
						force_curves[SPEED_RECORDS-i / display_interval-1][0] = cnv->get_force_summary(i*kmh2ms);
						force_curves[SPEED_RECORDS-i / display_interval-1][1] = cnv->calc_speed_holding_force(i*kmh2ms, rolling_resistance).to_sint32();
					}
				}
			}
			else {
				force_chart.set_seed(0);
				force_chart.set_x_axis_span(0 - display_interval);
				for (int i = 0; i < max_speed; i++) {
					if (i % display_interval == 0) {
						force_curves[i/display_interval][0] = cnv->get_force_summary(i*kmh2ms);
						force_curves[i/display_interval][1] = cnv->calc_speed_holding_force(i*kmh2ms, rolling_resistance).to_sint32();
					}
				}
			}
		}
	}

	update_labels();

	// all gui stuff set => display it
	gui_frame_t::draw(pos, size);
}



/**
 * This method is called if an action is triggered
 */
bool convoi_detail_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if(cnv.is_bound()) {
		if(comp==&sale_button) {
			cnv->call_convoi_tool( 'x', NULL );
			return true;
		}
		else if(comp==&withdraw_button) {
			cnv->call_convoi_tool( 'w', NULL );
			return true;
		}
		else if(comp==&retire_button) {
			cnv->call_convoi_tool( 'T', NULL );
			return true;
		}
		else if (comp == &class_management_button) {
			create_win(20, 40, new vehicle_class_manager_t(cnv), w_info, magic_class_manager + cnv.get_id());
			return true;
		}
		else if (comp == &overview_selctor) {
			update_labels();
			return true;
		}
		else if( comp==&cb_loaded_detail ) {
			cont_payload_info.set_display_mode(cb_loaded_detail.get_selection());
			return true;
		}
		else if (comp == &tabs) {
			const sint16 old_tab = tabstate;
			tabstate = tabs.get_active_tab_index();
			if (get_windowsize().h == get_min_windowsize().h || tabstate == old_tab) {
				set_tab_opened();
			}
			return true;
		}
	}
	return false;
}

bool convoi_detail_t::infowin_event(const event_t *ev)
{
	if (cnv.is_bound() && formation.getroffen(ev->cx - formation.get_pos().x, ev->cy - D_TITLEBAR_HEIGHT  - scrolly_formation.get_pos().y)) {
		if (IS_LEFTRELEASE(ev)) {
			cnv->show_info();
			return true;
		}
		else if (IS_RIGHTRELEASE(ev)) {
			world()->get_viewport()->change_world_position(cnv->get_pos());
			return true;
		}
	}
	return gui_frame_t::infowin_event(ev);
}

void convoi_detail_t::rdwr(loadsave_t *file)
{
	// convoy data
	if (  file->is_version_less(112, 3)  ) {
		// dummy data
		koord3d cnv_pos( koord3d::invalid);
		char name[128];
		name[0] = 0;
		cnv_pos.rdwr( file );
		file->rdwr_str( name, lengthof(name) );
	}
	else {
		// handle
		convoi_t::rdwr_convoihandle_t(file, cnv);
	}
	// window size, scroll position
	scr_size size = get_windowsize();
	sint32 xoff = scrolly.get_scroll_x();
	sint32 yoff = scrolly.get_scroll_y();
	sint32 formation_xoff = scrolly_formation.get_scroll_x();
	sint32 formation_yoff = scrolly_formation.get_scroll_y();

	size.rdwr( file );
	file->rdwr_long( xoff );
	file->rdwr_long( yoff );
	uint8 selected_tab = tabs.get_active_tab_index();
	if( file->is_version_ex_atleast(14,41) ) {
		file->rdwr_byte(selected_tab);
	}

	if(  file->is_loading()  ) {
		// convoy vanished
		if(  !cnv.is_bound()  ) {
			dbg->error( "convoi_detail_t::rdwr()", "Could not restore convoi detail window of (%d)", cnv.get_id() );
			destroy_win( this );
			return;
		}

		// now we can open the window ...
		scr_coord const& pos = win_get_pos(this);
		convoi_detail_t *w = new convoi_detail_t(cnv);
		create_win(pos.x, pos.y, w, w_info, magic_convoi_detail + cnv.get_id());
		w->set_windowsize( size );
		w->scrolly.set_scroll_position( xoff, yoff );
		w->scrolly_formation.set_scroll_position(formation_xoff, formation_yoff);
		w->tabs.set_active_tab_index(selected_tab);
		w->cont_payload_info.set_cnv(cnv);
		// we must invalidate halthandle
		cnv = convoihandle_t();
		destroy_win( this );
	}
}


// component for vehicle display
gui_vehicleinfo_t::gui_vehicleinfo_t(convoihandle_t cnv)
{
	this->cnv = cnv;
}


/*
 * Draw the component
 */
void gui_vehicleinfo_t::draw(scr_coord offset)
{
	// keep previous maximum width
	int x_size = get_size().w - 51 - pos.x;
	karte_t *welt = world();
	offset.y += LINESPACE/2;

	int total_height = 0;
	if (cnv.is_bound()) {
		cbuffer_t buf;
		const uint16 month_now = welt->get_timeline_year_month();
		uint8 vehicle_count = cnv->get_vehicle_count();

		// display total values
		if (vehicle_count > 1) {
			// vehicle min max. speed (not consider weight)
			buf.clear();
			buf.printf("%s %3d km/h\n", translator::translate("Max. speed:"), speed_to_kmh(cnv->get_min_top_speed()));
			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;

			// convoy power
			buf.clear();
			// NOTE: These value needs to be modified because these are multiplied by "gear"
			buf.printf(translator::translate("%s %4d kW, %d kN"), translator::translate("Power:"), cnv->get_sum_power() / 1000, cnv->get_starting_force().to_sint32() / 1000);

			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;

			// current brake force
			buf.clear();
			buf.printf("%s %.2f kN", translator::translate("Max. brake force:"), cnv->get_braking_force().to_double() / 1000.0);
			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;

			// convoy weight
			buf.clear();
			buf.printf("%s %.1ft (%.1ft)", translator::translate("Weight:"), cnv->get_weight_summary().weight / 1000.0, cnv->get_sum_weight() / 1000.0);
			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;

			// max axles load
			buf.clear();
			buf.printf("%s %dt", translator::translate("Max. axle load:"), cnv->get_highest_axle_load());
			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;

			// convoy applied livery scheme
			if (cnv->get_livery_scheme_index()) {
				buf.clear();
				buf.printf(translator::translate("Applied livery scheme: %s"), translator::translate(welt->get_settings().get_livery_scheme(cnv->get_livery_scheme_index())->get_name()));
				display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height, buf, ALIGN_LEFT, welt->get_settings().get_livery_scheme(cnv->get_livery_scheme_index())->is_available(month_now) ? SYSCOL_TEXT : COL_OBSOLETE, true);
				total_height += LINESPACE;
			}
			total_height += LINESPACE;
		}

		for (uint8 veh = 0; veh < vehicle_count; veh++) {
			vehicle_t *v = cnv->get_vehicle(veh);
			vehicle_as_potential_convoy_t convoy(*v->get_desc());
			const uint8 upgradable_state = v->get_desc()->has_available_upgrade(month_now);

			// first image
			scr_coord_val x, y, w, h;
			const image_id image = v->get_loaded_image();
			display_get_base_image_offset(image, &x, &y, &w, &h);
			display_base_img(image, 11 - x + pos.x + offset.x, pos.y + offset.y + total_height - y + 2 + LINESPACE, cnv->get_owner()->get_player_nr(), false, true);
			w = max(40, w + 4) + 11;

			// now add the other info
			int extra_y = 0;

			// cars number in this convoy
			sint8 car_number = cnv->get_car_numbering(veh);
			buf.clear();
			if (car_number < 0) {
				buf.printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number)); // This also applies to horses and tractors and push locomotives.
			}
			else {
				buf.append(car_number);
			}
			display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, upgradable_state == 2 ? COL_UPGRADEABLE : SYSCOL_TEXT_WEAK, true);
			buf.clear();

			// upgradable symbol
			if (upgradable_state && skinverwaltung_t::upgradable) {
				if (welt->get_settings().get_show_future_vehicle_info() || (!welt->get_settings().get_show_future_vehicle_info() && v->get_desc()->is_future(month_now) != 2)) {
					display_color_img(skinverwaltung_t::upgradable->get_image_id(upgradable_state - 1), pos.x + w + offset.x - D_FIXED_SYMBOL_WIDTH, pos.y + offset.y + total_height + extra_y + h + LINESPACE, 0, false, false);
				}
			}

			// name of this
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, translator::translate(v->get_desc()->get_name()), ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE + D_V_SPACE;
			w += D_H_SPACE;

			// power, tractive force, gear
			if (v->get_desc()->get_power() > 0) {
				buf.clear();
				buf.printf(translator::translate("Power/tractive force (%s): %4d kW / %d kN\n"),
					translator::translate(vehicle_desc_t::get_engine_type((vehicle_desc_t::engine_t)v->get_desc()->get_engine_type())),
					v->get_desc()->get_power(), v->get_desc()->get_tractive_effort());
				display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				extra_y += LINESPACE;
				buf.clear();
				buf.printf("%s %0.2f : 1", translator::translate("Gear:"), v->get_desc()->get_gear() / 64.0);
				display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				extra_y += LINESPACE;
			}

			// max speed
			buf.clear();
			buf.printf("%s %3d km/h\n", translator::translate("Max. speed:"), v->get_desc()->get_topspeed());
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			// weight
			buf.clear();
			buf.printf("%s %.1ft (%.1ft)", translator::translate("Weight:"), v->get_sum_weight()/1000.0, v->get_desc()->get_weight() / 1000.0);
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			// Axle load
			buf.clear();
			buf.printf("%s %dt", translator::translate("Axle load:"), v->get_desc()->get_axle_load());
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			// Brake force
			buf.clear();
			buf.printf("%s %4.1f kN", translator::translate("Max. brake force:"), convoy.get_braking_force().to_double() / 1000.0);
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			// Range
			buf.clear();
			buf.printf("%s: ", translator::translate("Range"));
			if (v->get_desc()->get_range() == 0)
			{
				buf.append(translator::translate("unlimited"));
			}
			else
			{
				buf.printf("%i km", v->get_desc()->get_range());
			}
			display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			//Catering - A vehicle can be a catering vehicle without carrying passengers.
			if (v->get_desc()->get_catering_level() > 0)
			{
				buf.clear();
				if (v->get_desc()->get_freight_type()->get_catg_index() == 1)
				{
					//Catering vehicles that carry mail are treated as TPOs.
					buf.printf("%s", translator::translate("This is a travelling post office"));
					display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
					extra_y += LINESPACE;
				}
				else
				{
					buf.printf(translator::translate("Catering level: %i"), v->get_desc()->get_catering_level());
					display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
					extra_y += LINESPACE;
				}
			}


			const way_constraints_t &way_constraints = v->get_desc()->get_way_constraints();
			// Permissive way constraints
			// (If vehicle has, way must have)
			// @author: jamespetts
			//for(uint8 i = 0; i < 8; i++)
			for (uint8 i = 0; i < way_constraints.get_count(); i++)
			{
				//if(v->get_desc()->permissive_way_constraint_set(i))
				if (way_constraints.get_permissive(i))
				{
					buf.clear();

					char tmpbuf[17];
					sprintf(tmpbuf, "Permissive %i-%hu", v->get_desc()->get_waytype(), (uint16)i);
					buf.printf("%s %s", translator::translate("\nMUST USE: "), translator::translate(tmpbuf));
					display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
					extra_y += LINESPACE;
				}
			}
			if (v->get_desc()->get_is_tall())
			{
				buf.clear();
				char tmpbuf1[13];
				sprintf(tmpbuf1, "\nMUST USE: ");
				char tmpbuf[46];
				sprintf(tmpbuf, "high_clearance_under_bridges_(no_low_bridges)");
				buf.printf("%s %s", translator::translate(tmpbuf1), translator::translate(tmpbuf));
				display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				extra_y += LINESPACE;
			}

			// Prohibitive way constraints
			// (If way has, vehicle must have)
			// @author: jamespetts
			for (uint8 i = 0; i < way_constraints.get_count(); i++)
			{
				if (way_constraints.get_prohibitive(i))
				{
					buf.clear();
					char tmpbuf[18];
					sprintf(tmpbuf, "Prohibitive %i-%hu", v->get_desc()->get_waytype(), (uint16)i);
					buf.printf("%s %s", translator::translate("\nMAY USE: "), translator::translate(tmpbuf));
					display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
					extra_y += LINESPACE;
				}
			}
			if (v->get_desc()->get_tilting())
			{
				buf.clear();
				char tmpbuf1[14];
				sprintf(tmpbuf1, "equipped_with");
				char tmpbuf[26];
				sprintf(tmpbuf, "tilting_vehicle_equipment");
				buf.printf("%s %s", translator::translate(tmpbuf1), translator::translate(tmpbuf));
				display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
				extra_y += LINESPACE;
			}

			// Friction
			if (v->get_frictionfactor() != 1)
			{
				buf.clear();
				buf.printf("%s %i", translator::translate("Friction:"), v->get_frictionfactor());
				display_proportional_clip_rgb(pos.x + w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, MONEY_PLUS, true);
			}
			extra_y += LINESPACE;

			//skip at least five lines
			total_height += max(extra_y + LINESPACE*2, 5 * LINESPACE);
		}
	}

	scr_size size(max(x_size + pos.x, get_size().w), total_height);
	if (size != get_size()) {
		set_size(size);
	}
}


// component for payload display
gui_convoy_payload_info_t::gui_convoy_payload_info_t(convoihandle_t cnv)
{
	this->cnv = cnv;

	set_table_layout(1,0);
	set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(0, D_V_SPACE));
	update_list();
}

void gui_convoy_payload_info_t::update_list()
{
	remove_all();
	if (cnv.is_bound()) {
		add_table(2,0)->set_alignment(ALIGN_TOP);
		for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
			const vehicle_t* veh = cnv->get_vehicle(i);
			// left part
			add_table(1,3)->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
			{
				const uint16 month_now = world()->get_timeline_year_month();
				// cars number in this convoy
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
				sint8 car_number = cnv->get_car_numbering(i);
				if (car_number < 0) {
					lb->buf().printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number)); // This also applies to horses and tractors and push locomotives.
				}
				else {
					lb->buf().append(car_number);
				}
				lb->set_color(veh->get_desc()->has_available_upgrade(month_now) ? COL_UPGRADEABLE : SYSCOL_TEXT_WEAK);
				lb->set_fixed_width((D_BUTTON_WIDTH*3)>>3);
				lb->update();

				// vehicle color bar
				const PIXVAL veh_bar_color = veh->get_desc()->is_obsolete(month_now) ? COL_OBSOLETE : (veh->get_desc()->is_future(month_now) || veh->get_desc()->is_retired(month_now)) ? COL_OUT_OF_PRODUCTION : COL_SAFETY;
				new_component<gui_vehicle_bar_t>(veh_bar_color, scr_size((D_BUTTON_WIDTH*3>>3)-6, VEHICLE_BAR_HEIGHT))
					->set_flags(
						veh->is_reversed() ? veh->get_desc()->get_basic_constraint_next() : veh->get_desc()->get_basic_constraint_prev(),
						veh->is_reversed() ? veh->get_desc()->get_basic_constraint_prev() : veh->get_desc()->get_basic_constraint_next(),
						veh->get_desc()->get_interactivity()
					);

				// goods category symbol
				if (veh->get_desc()->get_total_capacity() || veh->get_desc()->get_overcrowded_capacity()) {
					new_component<gui_image_t>(veh->get_desc()->get_freight_type()->get_catg_symbol(), 0, ALIGN_CENTER_H, true);
				}
				else {
					new_component<gui_empty_t>();
				}
			}
			end_table();

			// right part
			add_table(2,2);
			{
				gui_label_buf_t *lb = new_component_span<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left, 2);
				lb->buf().append(translator::translate(veh->get_desc()->get_name()));
				lb->update();
				new_component<gui_margin_t>(LINESPACE>>1);
				new_component<gui_vehicle_cargo_info_t>(cnv->get_vehicle(i), display_mode);
			}
			end_table();

			new_component_span<gui_border_t>(2);
		}
		end_table();
	}
	set_size(get_size());
}

void gui_convoy_payload_info_t::draw(scr_coord offset)
{
	gui_aligned_container_t::draw(offset);
}


// component for cargo display
gui_convoy_maintenance_info_t::gui_convoy_maintenance_info_t(convoihandle_t cnv)
{
	this->cnv = cnv;
}


void gui_convoy_maintenance_info_t::draw(scr_coord offset)
{
	// keep previous maximum width
	int x_size = get_size().w - 51 - pos.x;
	karte_t *welt = world();
	offset.y += LINESPACE>>1;
	offset.x += D_H_SPACE;

	int total_height = 0;
	if (cnv.is_bound()) {
		uint8 vehicle_count = cnv->get_vehicle_count();
		const uint16 month_now = welt->get_timeline_year_month();
		cbuffer_t buf;
		int extra_w = D_H_SPACE;

		if (cnv->get_replace()) {
			if (skinverwaltung_t::alerts) {
				display_color_img(skinverwaltung_t::alerts->get_image_id(1), pos.x + offset.x + extra_w, pos.y + offset.y + total_height + FIXED_SYMBOL_YOFF, 0, false, false);
			}
			buf.clear();
			buf.append(translator::translate("Replacing"));
			display_proportional_clip_rgb(pos.x + offset.x + extra_w + 14, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, false);
			total_height += (LINESPACE*3)>>1;
		}

		// display total values
		if (vehicle_count > 1) {
			// convoy maintenance
			buf.clear();
			buf.printf(translator::translate("Maintenance: %1.2f$/km, %1.2f$/month\n"), cnv->get_per_kilometre_running_cost() / 100.0, welt->calc_adjusted_monthly_figure(cnv->get_fixed_cost()) / 100.0);
			//txt_convoi_cost.printf(translator::translate("Cost: %8s (%.2f$/km, %.2f$/month)\n"), buf, (double)maint_per_km / 100.0, (double)maint_per_month / 100.0);
			display_proportional_clip_rgb(pos.x + offset.x + extra_w, pos.y + offset.y + total_height, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			total_height += LINESPACE;
		}

		// Bernd Gabriel, 16.06.2009: current average obsolescence increase percentage
		if (vehicle_count > 0)
		{
			any_obsoletes = false;
			/* Bernd Gabriel, 17.06.2009:
			The average percentage tells nothing about the real cost increase: If a cost-intensive
			loco is very old and at max increase (1 * 400% * 1000 cr/month, but 15 low-cost cars are
			brand new (15 * 100% * 100 cr/month), an average percentage of
			(1 * 400% + 15 * 100%) / 16 = 118.75% does not tell the truth. Actually we pay
			(1 * 400% * 1000 + 15 * 100% * 100) / (1 * 1000 + 15 * 100) = 220% twice as much as in the
			early years of the loco.

				uint32 percentage = 0;
				for (uint16 i = 0; i < vehicle_count; i++) {
					percentage += cnv->get_vehicle(i)->get_desc()->calc_running_cost(10000);
				}
				percentage = percentage / (vehicle_count * 100) - 100;
				if (percentage > 0)
				{
					sprintf( tmp, "%s: %d%%", translator::translate("Obsolescence increase"), percentage);
					display_proportional_clip_rgb( pos.x+10, offset_y, tmp, ALIGN_LEFT, COL_OBSOLETE, true );
					offset_y += LINESPACE;
				}
			On the other hand: a single effective percentage does not tell the truth as well. Supposed we
			calculate the percentage from the costs per km, the relations for the month costs can widely differ.
			Therefore I show different values for running and monthly costs:
			*/
			uint32 run_actual = 0, run_nominal = 0, run_percent = 0;
			uint32 mon_actual = 0, mon_nominal = 0, mon_percent = 0;
			for (uint8 i = 0; i < vehicle_count; i++) {
				const vehicle_desc_t *desc = cnv->get_vehicle(i)->get_desc();
				run_nominal += desc->get_running_cost();
				run_actual += desc->get_running_cost(welt);
				mon_nominal += welt->calc_adjusted_monthly_figure(desc->get_fixed_cost());
				mon_actual += welt->calc_adjusted_monthly_figure(desc->get_fixed_cost(welt));
			}
			buf.clear();
			if (run_nominal) run_percent = ((run_actual - run_nominal) * 100) / run_nominal;
			if (mon_nominal) mon_percent = ((mon_actual - mon_nominal) * 100) / mon_nominal;
			if (run_percent)
			{
				if (mon_percent)
				{
					buf.printf("%s: %d%%/km %d%%/mon", translator::translate("Obsolescence increase"), run_percent, mon_percent);
				}
				else
				{
					buf.printf("%s: %d%%/km", translator::translate("Obsolescence increase"), run_percent);
				}
			}
			else
			{
				if (mon_percent)
				{
					buf.printf("%s: %d%%/mon", translator::translate("Obsolescence increase"), mon_percent);
				}
			}
			if (buf.len() > 0)
			{
				if (skinverwaltung_t::alerts) {
					display_color_img(skinverwaltung_t::alerts->get_image_id(2), pos.x + offset.x + D_MARGIN_LEFT, pos.y + offset.y + total_height + FIXED_SYMBOL_YOFF, 0, false, false);
				}
				display_proportional_clip_rgb(pos.x + offset.x + D_MARGIN_LEFT + 13, pos.y + offset.y + total_height, buf, ALIGN_LEFT, COL_OBSOLETE, true);
				total_height += (LINESPACE*3)>>1;
				any_obsoletes = true;
			}
		}

		// convoy applied livery scheme
		if (cnv->get_livery_scheme_index()) {
			buf.clear();
			buf.printf(translator::translate("Applied livery scheme: %s"), translator::translate(welt->get_settings().get_livery_scheme(cnv->get_livery_scheme_index())->get_name()));
			display_proportional_clip_rgb(pos.x + offset.x + extra_w, pos.y + offset.y + total_height, buf, ALIGN_LEFT, welt->get_settings().get_livery_scheme(cnv->get_livery_scheme_index())->is_available(month_now) ? SYSCOL_TEXT : COL_OBSOLETE, true);
			total_height += LINESPACE;
		}

		total_height += LINESPACE;

		char number[64];
		for (uint8 veh = 0; veh < vehicle_count; veh++) {
			vehicle_t *v = cnv->get_vehicle(veh);
			const uint8 upgradable_state = v->get_desc()->has_available_upgrade(month_now);

			int extra_y = 0;
			const uint8 grid_width = D_BUTTON_WIDTH / 3;
			extra_w = grid_width;

			// cars number in this convoy
			PIXVAL veh_bar_color;
			sint8 car_number = cnv->get_car_numbering(veh);
			buf.clear();
			if (car_number < 0) {
				buf.printf("%.2s%d", translator::translate("LOCO_SYM"), abs(car_number)); // This also applies to horses and tractors and push locomotives.
			}
			else {
				buf.append(car_number);
			}
			display_proportional_clip_rgb(pos.x + offset.x + 1, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, upgradable_state == 2 ? COL_UPGRADEABLE : SYSCOL_TEXT_WEAK, true);
			buf.clear();

			// vehicle color bar
			veh_bar_color = v->get_desc()->is_future(month_now) || v->get_desc()->is_retired(month_now) ? COL_OUT_OF_PRODUCTION : COL_SAFETY;
			if (v->get_desc()->is_obsolete(month_now)) {
				veh_bar_color = COL_OBSOLETE;
			}
			display_veh_form_wh_clip_rgb(pos.x+offset.x+1,                  pos.y+offset.y+total_height+extra_y+LINESPACE, (grid_width-6)/2, VEHICLE_BAR_HEIGHT, veh_bar_color, true, false, v->is_reversed() ? v->get_desc()->get_basic_constraint_next() : v->get_desc()->get_basic_constraint_prev(), v->get_desc()->get_interactivity());
			display_veh_form_wh_clip_rgb(pos.x+offset.x+(grid_width-6)/2+1, pos.y+offset.y+total_height+extra_y+LINESPACE, (grid_width-6)/2, VEHICLE_BAR_HEIGHT, veh_bar_color, true, true,  v->is_reversed() ? v->get_desc()->get_basic_constraint_prev() : v->get_desc()->get_basic_constraint_next(), v->get_desc()->get_interactivity());

			// goods category symbol
			if (v->get_desc()->get_total_capacity() || v->get_desc()->get_overcrowded_capacity()) {
				display_color_img(v->get_cargo_type()->get_catg_symbol(), pos.x + offset.x + grid_width / 2 - 5, pos.y + offset.y + total_height + extra_y + LINESPACE * 2, 0, false, false);
			}

			// name of this
			//display_multiline_text(pos.x + offset.x, pos.y + offset.y + total_height + extra_y, translator::translate(v->get_desc()->get_name()), SYSCOL_TEXT, true);
			display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, translator::translate(v->get_desc()->get_name()), ALIGN_LEFT, SYSCOL_TEXT, true);
			// livery scheme info
			if ( strcmp( v->get_current_livery(), "default") ) {
				buf.clear();
				vector_tpl<livery_scheme_t*>* schemes = welt->get_settings().get_livery_schemes();
				livery_scheme_t* convoy_scheme = schemes->get_element(cnv->get_livery_scheme_index());
				PIXVAL livery_state_col = SYSCOL_TEXT;
				if (convoy_scheme->is_contained(v->get_current_livery(), month_now)) {
					// current livery belongs to convoy applied livery scheme and active
					buf.printf("(%s)", translator::translate(convoy_scheme->get_name()));
					// is current livery latest one?
					if(strcmp(convoy_scheme->get_latest_available_livery(month_now, v->get_desc()), v->get_current_livery()))
					{
						livery_state_col = COL_UPGRADEABLE;
					}
				}
				else if (convoy_scheme->is_contained(v->get_current_livery())) {
					buf.printf("(%s)", translator::translate(convoy_scheme->get_name()));
					livery_state_col = COL_OBSOLETE;
				}
				else {
					// current livery does not belong to convoy applied livery scheme
					// note: livery may belong to more than one livery scheme
					bool found_active_scheme = false;
					livery_state_col = color_idx_to_rgb(COL_BROWN);
					cbuffer_t temp_buf;
					int cnt = 0;
					ITERATE_PTR(schemes, i)
					{
						livery_scheme_t* scheme = schemes->get_element(i);
						if (scheme->is_contained(v->get_current_livery())) {
							if (scheme->is_available(month_now)) {
								found_active_scheme = true;
								if (cnt) { buf.append(", "); }
								buf.append(translator::translate(scheme->get_name()));
								cnt++;
							}
							else if(!found_active_scheme){
								if (cnt) { buf.append(", "); }
								temp_buf.append(translator::translate(scheme->get_name()));
								cnt++;
							}
						}
					}
					if (!found_active_scheme) {
						buf = temp_buf;
						livery_state_col = color_idx_to_rgb(COL_DARK_BROWN);
					}
				}
				extra_y += LINESPACE;
				display_proportional_clip_rgb(pos.x + extra_w + offset.x + D_H_SPACE, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, livery_state_col, true);
			}
			extra_y += LINESPACE + D_V_SPACE;
			extra_w += D_H_SPACE;

			// age
			buf.clear();
			{
				const sint32 month = v->get_purchase_time();
				uint32 age_in_months = welt->get_current_month() - month;
				buf.printf("%s %s  (", translator::translate("Manufactured:"), translator::get_year_month(month));
				buf.printf(age_in_months < 2 ? translator::translate("%i month") : translator::translate("%i months"), age_in_months);
				buf.append(")");
			}
			display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			// Bernd Gabriel, 16.06.2009: current average obsolescence increase percentage
			uint32 percentage = v->get_desc()->calc_running_cost(100) - 100;
			if (percentage > 0)
			{
				buf.clear();
				buf.printf("%s: %d%%", translator::translate("Obsolescence increase"), percentage);
				display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, COL_OBSOLETE, true);
				extra_y += LINESPACE;
			}

			// value
			money_to_string(number, v->calc_sale_value() / 100.0);
			buf.clear();
			buf.printf("%s %s", translator::translate("Restwert:"), number);
			display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, MONEY_PLUS, true);
			extra_y += LINESPACE;

			// maintenance
			buf.clear();
			buf.printf(translator::translate("Maintenance: %1.2f$/km, %1.2f$/month\n"), v->get_desc()->get_running_cost() / 100.0, v->get_desc()->get_adjusted_monthly_fixed_cost() / 100.0);
			display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, MONEY_PLUS, true);
			extra_y += LINESPACE;

			// Revenue
			int len = 5 + display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, translator::translate("Base profit per km (when full):"), ALIGN_LEFT, SYSCOL_TEXT, true);
			// Revenue for moving 1 unit 1000 meters -- comes in 1/4096 of simcent, convert to simcents
			// Excludes TPO/catering revenue, class and comfort effects.  FIXME --neroden
			sint64 fare = v->get_cargo_type()->get_total_fare(1000); // Class needs to be added here (Ves?)
																	 // Multiply by capacity, convert to simcents, subtract running costs
			sint64 profit = (v->get_cargo_max()*fare + 2048ll) / 4096ll/* - v->get_running_cost(welt)*/;
			money_to_string(number, profit / 100.0);
			display_proportional_clip_rgb(pos.x + extra_w + offset.x + len, pos.y + offset.y + total_height + extra_y, number, ALIGN_LEFT, SYSCOL_TEXT, true);
			extra_y += LINESPACE;

			if (sint64 cost = welt->calc_adjusted_monthly_figure(v->get_desc()->get_maintenance())) {
				int len = display_proportional_clip_rgb(pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y, translator::translate("Maintenance"), ALIGN_LEFT, SYSCOL_TEXT, true);
				len += display_proportional_clip_rgb(pos.x + extra_w + offset.x + len, pos.y + offset.y + total_height + extra_y, ": ", ALIGN_LEFT, SYSCOL_TEXT, true);
				money_to_string(number, cost / (100.0));
				display_proportional_clip_rgb(pos.x + extra_w + offset.x + len, pos.y + offset.y + total_height + extra_y, number, ALIGN_LEFT, MONEY_MINUS, true);
				extra_y += LINESPACE;
			}

			// upgrade info
			if (upgradable_state)
			{
				int found = 0;
				PIXVAL upgrade_state_color = COL_UPGRADEABLE;
				for (uint8 i = 0; i < v->get_desc()->get_upgrades_count(); i++)
				{
					if (const vehicle_desc_t* desc = v->get_desc()->get_upgrades(i)) {
						if (!welt->get_settings().get_show_future_vehicle_info() && desc->is_future(month_now)==1) {
							continue; // skip future information
						}
						found++;
						if (found == 1) {
							if (skinverwaltung_t::upgradable) {
								display_color_img(skinverwaltung_t::upgradable->get_image_id(upgradable_state-1), pos.x + extra_w + offset.x, pos.y + offset.y + total_height + extra_y + FIXED_SYMBOL_YOFF, 0, false, false);
							}
							buf.clear();
							buf.printf("%s:", translator::translate("this_vehicle_can_upgrade_to"));
							display_proportional_clip_rgb(pos.x + extra_w + offset.x + 14, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
							extra_y += LINESPACE;
						}

						const uint16 intro_date = desc->is_future(month_now) ? desc->get_intro_year_month() : 0;

						if (intro_date) {
							upgrade_state_color = color_idx_to_rgb(MN_GREY0);
						}
						else if (desc->is_retired(month_now)) {
							upgrade_state_color = COL_OUT_OF_PRODUCTION;
						}
						else if (desc->is_obsolete(month_now)) {
							upgrade_state_color = COL_OBSOLETE;
						}
						display_veh_form_wh_clip_rgb(pos.x+extra_w+offset.x+D_MARGIN_LEFT,                        pos.y+offset.y+total_height+extra_y+1, VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT, upgrade_state_color, true, false, desc->get_basic_constraint_prev(), desc->get_interactivity());
						display_veh_form_wh_clip_rgb(pos.x+extra_w+offset.x+D_MARGIN_LEFT+VEHICLE_BAR_HEIGHT*2-1, pos.y+offset.y+total_height+extra_y+1, VEHICLE_BAR_HEIGHT*2, VEHICLE_BAR_HEIGHT, upgrade_state_color, true, true,  desc->get_basic_constraint_next(), desc->get_interactivity());

						buf.clear();
						buf.append(translator::translate(v->get_desc()->get_upgrades(i)->get_name()));
						if (intro_date) {
							buf.printf(", %s %s", translator::translate("Intro. date:"), translator::get_year_month(intro_date));
						}
						display_proportional_clip_rgb(pos.x + extra_w + offset.x + D_MARGIN_LEFT + VEHICLE_BAR_HEIGHT*4, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, upgrade_state_color, true);
						extra_y += LINESPACE;
						// 2nd row
						buf.clear();
						money_to_string(number, desc->get_upgrade_price() / 100);
						buf.printf("%s %s,  ", translator::translate("Upgrade price:"), number);
						buf.printf(translator::translate("Maintenance: %1.2f$/km, %1.2f$/month\n"), desc->get_running_cost() / 100.0, desc->get_adjusted_monthly_fixed_cost() / 100.0);
						display_proportional_clip_rgb(pos.x + extra_w + offset.x + D_MARGIN_LEFT + grid_width, pos.y + offset.y + total_height + extra_y, buf, ALIGN_LEFT, SYSCOL_TEXT, true);
						extra_y += LINESPACE + 2;
					}
				}
			}

			extra_y += LINESPACE;

			//skip at least five lines
			total_height += max(extra_y + LINESPACE, 5 * LINESPACE);
		}
	}

	scr_size size(max(x_size + pos.x, get_size().w), total_height);
	if (size != get_size()) {
		set_size(size);
	}
}
