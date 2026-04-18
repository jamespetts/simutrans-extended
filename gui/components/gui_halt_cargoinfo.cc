/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_halt_cargoinfo.h"
#include "gui_schedule_item.h"
#include "gui_destination_building_info.h"
#include "gui_waytype_image_box.h"
#include "gui_convoi_button.h"
#include "../../tpl/vector_tpl.h"
#include "gui_colorbox.h"
#include "../../dataobj/environment.h"
#include "../../dataobj/translator.h"
#include "gui_image.h"
#include "gui_divider.h"
#include "../halt_list_stats.h" // capped arrow
#include "../../simline.h"
#include "../linelist_stats_t.h" // gui_line_label_t
#include "../../utils/simstring.h"


#define TRANSFER_MODE_IN (1)
#define TRANSFER_MODE_OUT (2)

bool gui_halt_cargoinfo_t::sort_reverse = false;

static int compare_amount(const ware_t &a, const ware_t &b) {
	int comp = b.menge - a.menge;
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_index(const ware_t &a, const ware_t &b) {
	int comp = a.index - b.index;
	if (comp == 0) {
		comp = b.get_class() - a.get_class();
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_via(const ware_t &a, const ware_t &b) {
	int comp = 0;
	if (a.get_zwischenziel().is_bound() && b.get_zwischenziel().is_bound()) {
		comp = STRICMP(a.get_zwischenziel()->get_name(), b.get_zwischenziel()->get_name());
	}
	if (comp == 0 && a.get_ziel().is_bound() && b.get_ziel().is_bound()) {
		comp = STRICMP(a.get_ziel()->get_name(), b.get_ziel()->get_name());
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_origin(const ware_t &a, const ware_t &b) {
	int comp = 0;
	if(a.get_origin().is_bound() && b.get_origin().is_bound()) {
		comp = STRICMP(a.get_origin()->get_name(), b.get_origin()->get_name());
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_via_distance(const ware_t &a, const ware_t &b) {
	int comp = 0;
	if (a.get_zwischenziel().is_bound() && b.get_zwischenziel().is_bound()) {
		const uint32 dist_a = shortest_distance(a.get_zwischenziel().get_rep()->get_basis_pos(), a.get_last_transfer().get_rep()->get_basis_pos());
		const uint32 dist_b = shortest_distance(b.get_zwischenziel().get_rep()->get_basis_pos(), b.get_last_transfer().get_rep()->get_basis_pos());
		comp = dist_b - dist_a;
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_origin_distance(const ware_t &a, const ware_t &b) {
	int comp = 0;
	if (a.get_origin().is_bound() && b.get_origin().is_bound()) {
		const uint32 dist_a = shortest_distance(a.get_origin().get_rep()->get_basis_pos(), a.get_last_transfer().get_rep()->get_basis_pos());
		const uint32 dist_b = shortest_distance(b.get_origin().get_rep()->get_basis_pos(), b.get_last_transfer().get_rep()->get_basis_pos());
		comp = dist_b - dist_a;
	}
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_destination(const ware_t &a, const ware_t &b) {
	const uint32 dist_a = shortest_distance(a.get_zielpos(), a.get_last_transfer().get_rep()->get_basis_pos());
	const uint32 dist_b = shortest_distance(b.get_zielpos(), b.get_last_transfer().get_rep()->get_basis_pos());
	int comp = dist_b - dist_a;
	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}

static int compare_route(const ware_t &a, const ware_t &b) {
	int comp = 0;

	const uint8 catg_a = a.get_desc()->get_catg_index();
	const uint8 catg_b = b.get_desc()->get_catg_index();

	halthandle_t const v1 = a.get_zwischenziel();
	halthandle_t const v2 = b.get_zwischenziel();

	linehandle_t const wl1 = a.get_last_transfer()->get_preferred_line(v1, catg_a, goods_manager_t::get_classes_catg_index(catg_a) - 1);
	linehandle_t const wl2 = b.get_last_transfer()->get_preferred_line(v2, catg_b, goods_manager_t::get_classes_catg_index(catg_b) - 1);
	if (wl1.is_bound() && wl2.is_bound()) {
		comp = strcmp(wl1->get_name(), wl2->get_name());
	}
	else if (wl1.is_bound()) {
		comp = 1;
	}
	else if (wl2.is_bound()) {
		comp = -1;
	}
	else {
		convoihandle_t const cv1 = a.get_last_transfer()->get_preferred_convoy(v1, catg_a, goods_manager_t::get_classes_catg_index(catg_a) - 1);
		convoihandle_t const cv2 = b.get_last_transfer()->get_preferred_convoy(v2, catg_b, goods_manager_t::get_classes_catg_index(catg_b) - 1);
		if (cv1.is_bound() && cv2.is_bound()) {
			comp = strcmp(cv1->get_name(), cv2->get_name());
		}
		else if (cv1.is_bound()) {
			comp = 1;
		}
		else if (cv2.is_bound()) {
			comp = -1;
		}
	}

	if (comp == 0) {
		comp = b.menge - a.menge;
	}
	return gui_halt_cargoinfo_t::sort_reverse ? -comp : comp;
}


gui_halt_waiting_table_t::gui_halt_waiting_table_t(slist_tpl<ware_t> const& warray, uint8 filter_bits, uint8 merge_condition_bits, uint32 border, uint8 transfer_mode, const schedule_t *schedule)
{
	set_table_layout(4,0);
	set_table_frame(true, true);
	set_spacing(scr_size(D_H_SPACE, 2));

	const bool divide_by_wealth = !(merge_condition_bits & haltestelle_t::ignore_class);
	const bool double_row = !(merge_condition_bits & haltestelle_t::ignore_origin_stop)
		&& ((transfer_mode == TRANSFER_MODE_OUT && !(merge_condition_bits&haltestelle_t::ignore_destination))
		|| (transfer_mode != TRANSFER_MODE_OUT && (!(merge_condition_bits & haltestelle_t::ignore_via_stop) || !(merge_condition_bits & haltestelle_t::ignore_route))));

	if (!border) {
		border = 10;
		for (auto const& ware : warray) {
			border = max(border,ware.menge);
		}
	}

	// top margin
	new_component_span<gui_margin_t>(0, D_V_SPACE>>1, 4);

	for (auto const& ware : warray) {
		// col1, horizontal color bar
		const scr_coord_val width = min(HALT_WAITING_BAR_MAX_WIDTH, (HALT_WAITING_BAR_MAX_WIDTH*ware.menge + border - 1) / border + 2);

		const PIXVAL barcolor = (divide_by_wealth && ware.is_commuting_trip) ? color_idx_to_rgb(COL_COMMUTER) : color_idx_to_rgb(goods_manager_t::get_info(ware.get_index())->get_color_index());
		add_table(3, 2)->set_spacing(NO_SPACING);
		{
			if (ware.menge > border) {
				new_component<gui_capped_arrow_t>(true);
			}
			else {
				new_component<gui_margin_t>(L_CAPPED_ARROW_WIDTH);
			}
			new_component<gui_margin_t>(HALT_WAITING_BAR_MAX_WIDTH - width + 2);
			new_component<gui_capacity_bar_t>(scr_size(width, GOODS_COLOR_BOX_HEIGHT), barcolor)->set_show_frame(false);

			// extra 2nd row for control alignment
			new_component_span<gui_margin_t>(0, double_row ? max(D_POS_BUTTON_HEIGHT, LINESPACE - 2) : 0, 3);
		}
		end_table();

		// col2 cargo type, amount
		add_table(2, 2)->set_spacing(scr_size(D_V_SPACE, 0));
		{
			// col2-1
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			lb->buf().printf("%3u%s", ware.menge, translator::translate(ware.get_mass()));
			lb->update();
			lb->set_fixed_width(lb->get_min_size().w);


			// col2-2
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
				lb->buf().append(translator::translate(ware.get_name()));
			}
			if ((goods_manager_t::get_info(ware.get_index())->get_number_of_classes() > 1) && divide_by_wealth) {
				lb->buf().printf(" (%s)", goods_manager_t::get_translated_wealth_name(ware.get_index(), ware.get_class()));
			}
			lb->update();

			// extra 2nd row for control alignment
			new_component_span<gui_margin_t>(0, double_row ? max(D_POS_BUTTON_HEIGHT, LINESPACE - 2) : 0, 2);
		}
		end_table();

		// col3
		const bool show_origin = !(merge_condition_bits & haltestelle_t::ignore_origin_stop) && ware.get_origin().is_bound();
		add_table(1, 0)->set_spacing(NO_SPACING);
		{
			// upper row: origin
			if (show_origin) {
				add_table(3, 1);
				{
					new_component<gui_label_t>(ware.is_passenger() ? "Origin:" : "Shipped from:");
					bool is_interchange = (ware.get_origin().get_rep()->registered_lines.get_count() + ware.get_origin().get_rep()->registered_convoys.get_count()) > 1;
					new_component<gui_schedule_entry_number_t>(-1, ware.get_origin().get_rep()->get_owner()->get_player_color1(),
						is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
						scr_size(LINESPACE+2, LINESPACE-2),
						ware.get_origin().get_rep()->get_basis_pos3d()
						);
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
					lb->buf().append(ware.get_origin()->get_name());
					lb->update();
				}
				end_table();
			}

			// lower row: route, via/to
			if (ware.get_zwischenziel().is_bound()) {
				add_table(5, 1)->set_spacing(scr_size(D_H_SPACE, 0));
				{
					if (show_origin && double_row) new_component<gui_margin_t>(LINESPACE); // add left margin for lower row
					const bool display_goal_halt = (!(merge_condition_bits & haltestelle_t::ignore_goal_stop)) && ware.get_ziel().is_bound() && (ware.get_zwischenziel() != ware.get_ziel()) && (transfer_mode != TRANSFER_MODE_OUT);

					if(merge_condition_bits & haltestelle_t::ignore_route && transfer_mode != TRANSFER_MODE_OUT) {
						new_component<gui_label_t>(display_goal_halt ? "Via:" : "To:");
					}
					else if (transfer_mode != TRANSFER_MODE_OUT) {
						// Show route info.
						add_table(2,1);
						{
							PIXVAL line_color=SYSCOL_TEXT;
							scr_coord_val arrow_h=LINEASCENT-5;
							bool found_route=false;
							halthandle_t const current_halt = ware.get_last_transfer();
							if (current_halt.is_bound()) {
								linehandle_t line = current_halt->get_preferred_line(ware.get_zwischenziel(), ware.get_desc()->get_catg_index(), goods_manager_t::get_classes_catg_index(ware.get_index())-1);
								if (line.is_bound()) {
									found_route = true;
									arrow_h += 2;
									line_color = line->get_line_color_index() > 253 ? color_idx_to_rgb(line->get_owner()->get_player_color1() + 3) : line->get_line_color();
									new_component<gui_line_label_t>(line);
								}

								if (!found_route) {
									convoihandle_t cnv = current_halt->get_preferred_convoy(ware.get_zwischenziel(), ware.get_desc()->get_catg_index(), goods_manager_t::get_classes_catg_index(ware.get_index())-1);
									if (cnv.is_bound()) {
										found_route = true;
										line_color = color_idx_to_rgb(cnv->get_owner()->get_player_color1() + 3);
										new_component<gui_label_t>(cnv->get_name());
									}
									else {
										// The cargo may no longer have a route to its destination.
										new_component<gui_label_t>("Undecided", COL_DANGER); // "No Route"
									}
								}
							}

							// draw right arrow
							if (!(merge_condition_bits & haltestelle_t::ignore_via_stop) && found_route) {
								new_component<gui_vehicle_bar_t>(line_color, scr_size(LINESPACE*3, arrow_h))->set_flags( 0, vehicle_desc_t::can_be_head | vehicle_desc_t::can_be_tail, 3);
							}
							else {
								new_component<gui_empty_t>();
							}
						}
						end_table();
					}

					if (!(merge_condition_bits & haltestelle_t::ignore_via_stop)) {
						if (transfer_mode != TRANSFER_MODE_OUT) {
							uint8 entry_num = 255;
							uint8 entry_num_r = 255;
							const halthandle_t via_halt = ware.get_zwischenziel();
							if (schedule) {
								entry_num = schedule->get_entry_index(via_halt, via_halt->get_owner(), false);
								if (schedule->is_mirrored()) {
									entry_num_r = schedule->get_entry_index(via_halt, via_halt->get_owner(), true);
								}
							}
							const bool is_interchange = (ware.get_zwischenziel().get_rep()->registered_lines.get_count() + ware.get_zwischenziel().get_rep()->registered_convoys.get_count()) > 1;
							new_component<gui_schedule_entry_number_t>(entry_num, ware.get_zwischenziel().get_rep()->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								(entry_num==255) ? scr_size(LINESPACE+4, LINESPACE) : scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								ware.get_zwischenziel().get_rep()->get_basis_pos3d()
								);
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
							lb->buf().append(ware.get_zwischenziel()->get_name());
							if (display_goal_halt) {
								lb->buf().append(" > ");
							}
							lb->update();
						}
						else if ((merge_condition_bits & haltestelle_t::ignore_destination) && (merge_condition_bits & haltestelle_t::ignore_origin_stop)) {
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT_WEAK);
							lb->buf().printf("%s %s", translator::translate("To:"), translator::translate("This stop"));
							lb->update();
						}
						add_table(4, 1);
						{
							if (display_goal_halt) {
								new_component<gui_label_t>("To:");
								const bool is_interchange = (ware.get_ziel().get_rep()->registered_lines.get_count() + ware.get_ziel().get_rep()->registered_convoys.get_count()) > 1;
								new_component<gui_schedule_entry_number_t>(-1, ware.get_ziel().get_rep()->get_owner()->get_player_color1(),
									is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
									scr_size(LINESPACE+2, LINESPACE-2),
									ware.get_ziel().get_rep()->get_basis_pos3d()
									);
								gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT);
								lb->buf().append(ware.get_ziel()->get_name());
								lb->update();
							}

							if (!(merge_condition_bits & haltestelle_t::ignore_destination)) {
								// final destination building
								add_table(3, 1);
								{
									new_component<gui_label_t>(" ...");
									if (skinverwaltung_t::on_foot && (ware.is_passenger() || ware.is_mail())) {
										new_component<gui_image_t>(skinverwaltung_t::on_foot->get_image_id(0), 0, ALIGN_CENTER_V, true);
									}
									new_component<gui_destination_building_info_t>(ware.get_zielpos(), ware.is_freight());
								}
								end_table();
							}
						}
						end_table();
					}
				}
				end_table();
			}
		}
		end_table();

		// col4
		new_component<gui_fill_t>();
	}

	// bottom margin
	new_component_span<gui_margin_t>(0, D_V_SPACE>>1, 4);

	set_size(get_min_size());
}

gui_halt_cargoinfo_t::gui_halt_cargoinfo_t(halthandle_t halt_)
{
	halt = halt_;
	set_table_layout(1,0);
	set_table_frame(true);
	set_alignment(ALIGN_LEFT | ALIGN_TOP);
	update();
}


void gui_halt_cargoinfo_t::sort_cargo(slist_tpl<ware_t> & cargoes, uint8 sort_mode)
{
	switch (sort_mode)
	{
		case by_via:
			cargoes.sort(compare_via);
			break;
		case by_origin:
			cargoes.sort(compare_origin);
			break;
		case by_category:
			cargoes.sort(compare_index);
			break;
		case by_via_distance:
			cargoes.sort(compare_via_distance);
			break;
		case by_origin_distance:
			cargoes.sort(compare_origin_distance);
			break;
		case by_destination:
			cargoes.sort(compare_destination);
			break;
		case by_route:
			cargoes.sort(compare_route);
			break;
		default:
		case by_amount:
			cargoes.sort(compare_amount);
			break;
	}
}


uint32 gui_halt_cargoinfo_t::list_by_catg(uint8 filter_bits, uint8 merge_condition_bits, uint8 sort_mode, linehandle_t line, convoihandle_t cnv, uint8 entry_start, uint8 entry_end)
{
	slist_tpl<ware_t> cargoes;
	uint32 catg_sum=0;

	for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {

		if (catg_index == goods_manager_t::INDEX_PAS) {
			if (!(filter_bits&SHOW_WAITING_PAX)) continue;
		}
		else if (catg_index == goods_manager_t::INDEX_MAIL) {
			if (!(filter_bits&SHOW_WAITING_MAIL)) continue;
		}
		else if (!(filter_bits&SHOW_WAITING_GOODS)) continue;

		cargoes.clear();

		uint32 sum = halt->get_ware(cargoes, catg_index, merge_condition_bits, 0, line, cnv, entry_start, entry_end);

		uint32 border = 10;
		border = max(border,sum);
		if (catg_index == goods_manager_t::INDEX_PAS) {
			border = max(50,halt->get_capacity(0));
		}
		else if(catg_index == goods_manager_t::INDEX_MAIL){
			border = max(50, halt->get_capacity(1));
		}
		else {
			border = max(50, halt->get_capacity(2));
		}

		if (sum) {
			catg_sum += sum;
			add_table(3,1);
			{
				new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), 0, 0, true);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>();
				lb->buf().append(translator::translate(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name()));

				lb->buf().printf(": %4i %s", sum, translator::translate("waiting"));
				lb->update();

				new_component<gui_fill_t>();
			}
			end_table();

			sort_cargo(cargoes, sort_mode);

			const schedule_t* schedule = line.is_bound() ? line->get_schedule() : cnv.is_bound() ? cnv->get_schedule() : NULL;
			new_component<gui_halt_waiting_table_t>(cargoes, filter_bits, merge_condition_bits, border, 0, schedule);
		}

		// not support route display
		if (filter_bits&SHOW_TRANSFER_IN && !line.is_bound() && !cnv.is_bound() ) {
			const bool found_waiting = sum > 0;

			cargoes.clear();
			sum = halt->get_ware(cargoes, catg_index, merge_condition_bits, TRANSFER_MODE_IN, line, cnv, entry_start, entry_end);
			if (sum) {
				catg_sum += sum;
				border = max(border, sum);
				if (!found_waiting) {
					new_component<gui_divider_t>();
				}
				add_table(4, 1);
				{
					// category name
					if (!found_waiting) {
						new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), 0, 0, true);
						gui_label_buf_t *lb = new_component<gui_label_buf_t>();
						lb->buf().append(translator::translate(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name()));
						lb->buf().append(":");
						lb->update();
						lb->set_fixed_width(lb->get_min_size().w);
					}
					else {
						new_component<gui_margin_t>(GOODS_SYMBOL_CELL_WIDTH);
						new_component<gui_margin_t>(proportional_string_width(translator::translate(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name())));
					}

					gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT_STRONG);
					lb->buf().printf(" %4i %s", sum, translator::translate("transferring"));
					lb->update();

					new_component<gui_fill_t>();
				}
				end_table();

				sort_cargo(cargoes, sort_mode);
				//const schedule_t* schedule = line.is_bound() ? line->get_schedule() : cnv.is_bound() ? cnv->get_schedule() : NULL;
				new_component<gui_halt_waiting_table_t>(cargoes, filter_bits, merge_condition_bits, border, TRANSFER_MODE_IN/*, schedule*/);
			}
		}
	}
	return catg_sum;
}


void gui_halt_cargoinfo_t::list_by_route(uint8 filter_bits, uint8 merge_condition_bits, uint8 sort_mode, linehandle_t line, convoihandle_t cnv)
{
	if (line.is_null() && cnv.is_null()) {
		return;
	}

	const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;
	minivec_tpl<uint8> entry_idx_of_this_stop;
	uint32 sum = 0;
	const schedule_t *schedule = line.is_bound() ? line->get_schedule() : cnv->get_schedule();
	player_t *player= line.is_bound() ? line->get_owner() : cnv->get_owner();
	for (uint8 entry_idx = 0; entry_idx < schedule->entries.get_count(); entry_idx++) {
		halthandle_t entry_halt = haltestelle_t::get_halt(schedule->entries[entry_idx].pos, player);
		if (halt == entry_halt) {
			entry_idx_of_this_stop.append(entry_idx);
		}
	}

	uint8 loop_count = 0;
	for (uint8 j = 0; j < entry_idx_of_this_stop.get_count(); j++) {
		// schedule direction
		for (uint8 dir = 0; dir < 2; dir++) {
			if (dir == 1 && (!schedule->is_mirrored() || (schedule->is_mirrored() && (entry_idx_of_this_stop[j] == 0 || entry_idx_of_this_stop[j] == schedule->entries.get_count() - 1)))) {
				break;
			}

			uint8 entry_start = dir == 1 ? 0 : (entry_idx_of_this_stop[j] + 1) % schedule->entries.get_count();
			if (!loop_count) {
				add_table(4, 1);
				{
					if (line.is_bound()) {
						new_component<gui_line_button_t>(line);
						// Line labels with color of player
						new_component<gui_line_label_t>(line);
					}
					else {
						new_component<gui_convoi_button_t>(cnv);
						// convoy ID
						add_table(2,1);
						{
							char buf[128];
							sprintf(buf, "%u", cnv->self.get_id());
							new_component<gui_vehicle_number_t>(buf, color_idx_to_rgb(cnv->get_owner()->get_player_color1() + 3));
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(PLAYER_FLAG | color_idx_to_rgb(cnv->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
							lb->buf().append(cnv->access_internal_name());
							lb->update();
						}
						end_table();
					}

					// schedule number"s" of this stop
					if (merge_condition_bits & haltestelle_t::ignore_via_stop) {
						entry_start = 0;
						add_table(0, 1);
						for (uint8 k = 0; k < entry_idx_of_this_stop.get_count(); k++) {
							new_component<gui_schedule_entry_number_t>(entry_idx_of_this_stop[k], halt->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								halt->get_basis_pos3d());
						}
						end_table();
					}

					new_component<gui_fill_t>();
				}
				end_table();
			}

			uint8 entry_end = (dir == 1 && entry_idx_of_this_stop[0] != 0) ? entry_idx_of_this_stop[0] - 1 : schedule->entries.get_count() - 1;
			if (entry_idx_of_this_stop.get_count() != 1) {
				if (j == entry_idx_of_this_stop.get_count() - 1 && !schedule->is_mirrored()) {
					entry_end = entry_idx_of_this_stop[0] == 0 ? schedule->entries.get_count() - 1 : entry_idx_of_this_stop[0] - 1;
				}
			}

			if (!(merge_condition_bits & haltestelle_t::ignore_via_stop)
				&&
				!(entry_idx_of_this_stop.get_count() == 1
					&& (!schedule->is_mirrored() || (schedule->is_mirrored() && (entry_idx_of_this_stop[0] == 0 || entry_idx_of_this_stop[0] == schedule->entries.get_count() - 1))))) {
				// display the route direction
				uint8 next_stop_index = entry_idx_of_this_stop[j];
				bool rev = dir > 0;
				schedule->increment_index_until_next_halt(player, &next_stop_index, &rev);
				const halthandle_t next_halt = haltestelle_t::get_halt(schedule->entries[next_stop_index].pos, player);

				if (next_halt.is_bound()) {
					add_table(2, 1);
					{
						new_component<gui_schedule_entry_number_t>(entry_idx_of_this_stop[j], halt->get_owner()->get_player_color1(),
							is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
							scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
							halt->get_basis_pos3d());

						gui_label_buf_t *lb = new_component<gui_label_buf_t>();
						lb->buf().printf(translator::translate("for %s"), next_halt->get_name());
						lb->update();
					}
					end_table();
				}

			}

			sum += list_by_catg(filter_bits, merge_condition_bits | haltestelle_t::ignore_route, sort_mode, line, cnv, entry_start, entry_end);

			loop_count++;
			if (!sum) {
				gui_aligned_container_t *tbl = add_table(2, 1);
				{
					tbl->set_table_frame(true, true);
					tbl->set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE), scr_size(0, D_V_SPACE));
					new_component<gui_label_t>((filter_bits&SHOW_WAITING_PAX && filter_bits&SHOW_WAITING_MAIL && filter_bits&SHOW_WAITING_GOODS) ? "no goods waiting" : "no goods found", SYSCOL_TEXT_WEAK);
					new_component<gui_fill_t>();
				}
				end_table();
			}
			if ((merge_condition_bits & haltestelle_t::ignore_via_stop)) {
				break; // display only once
			}
		}
		if ((merge_condition_bits & haltestelle_t::ignore_via_stop)) {
			break; // display only once
		}
	}
	new_component<gui_divider_t>();
}

void gui_halt_cargoinfo_t::update(uint8 filter_bits, uint8 merge_condition_bits, uint8 sort_mode, bool route_mode, int player_nr, uint16 wt_filter_bits)
{
	remove_all();
	if (!halt.is_bound()) { return; }

	if (!route_mode) {
		list_by_catg(filter_bits, merge_condition_bits, sort_mode);
	}
	else {
		for (uint8 lt = 1; lt < simline_t::MAX_LINE_TYPE; lt++) {
			// waytype filter
			if (!(wt_filter_bits & (1 << lt - 1))) {
				continue;
			}
			uint waytype_route_cnt = 0;
			for (uint32 i = 0; i < halt->registered_lines.get_count(); i++) {
				const linehandle_t line = halt->registered_lines[i];
				if (line->get_linetype() != lt) {
					continue;
				}
				if (player_nr != -1 && line->get_owner()->get_player_nr() != player_nr) {
					continue;
				}
				// Draw waytype if it is the first
				if (!waytype_route_cnt) {
					waytype_route_cnt++;

					add_table(3, 1);
					{
						new_component<gui_waytype_image_box_t>(simline_t::linetype_to_waytype(line->get_linetype()));
						new_component<gui_label_t>(translator::translate(line->get_linetype_name()));
						new_component<gui_fill_t>();
					}
					end_table();
				}

				list_by_route(filter_bits, merge_condition_bits, sort_mode, line);
			}

			for (uint32 i = 0; i < halt->registered_convoys.get_count(); i++) {
				const convoihandle_t cnv = halt->registered_convoys[i];
				if (cnv->get_schedule()->get_type() != lt) {
					continue;
				}
				if (player_nr != -1 && cnv->get_owner()->get_player_nr() != player_nr) {
					continue;
				}
				// Draw waytype if it is the first
				if (!waytype_route_cnt) {
					waytype_route_cnt++;

					add_table(3, 1);
					{
						const waytype_t wt = simline_t::linetype_to_waytype((simline_t::linetype)cnv->get_schedule()->get_type());
						new_component<gui_waytype_image_box_t>(wt);
						new_component<gui_label_t>(gui_waytype_tab_panel_t::get_translated_waytype_name(wt));
						new_component<gui_fill_t>();
					}
					end_table();
				}

				list_by_route(filter_bits, merge_condition_bits, sort_mode, linehandle_t(), cnv);
			}
		}
	}

	if (!route_mode && filter_bits&SHOW_TRANSFER_OUT) {
		slist_tpl<ware_t> cargoes;
		bool got_one = false;

		new_component<gui_divider_t>();
		add_table(2, 1);
		{
			new_component<gui_heading_t>("transfers", SYSCOL_TH_BORDER, SYSCOL_TH_BORDER, 0)->set_width(proportional_string_width(translator::translate("transfers")) + LINESPACE + D_MARGINS_X + D_BUTTON_PADDINGS_X);
			new_component<gui_fill_t>();
		}
		end_table();

		got_one = false;
		for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
			if (catg_index == goods_manager_t::INDEX_PAS) {
				if (!(filter_bits&SHOW_WAITING_PAX)) continue;
			}
			else if (catg_index == goods_manager_t::INDEX_MAIL) {
				if (!(filter_bits&SHOW_WAITING_MAIL)) continue;
			}
			else if (!(filter_bits&SHOW_WAITING_GOODS)) continue;

			cargoes.clear();
			const uint32 sum = halt->get_ware(cargoes, catg_index, merge_condition_bits, 2);

			if (sum) {
				if (got_one) {
					new_component<gui_divider_t>();
				}
				got_one = true;
				add_table(3, 1);
				{
					new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), 0, 0, true);
					gui_label_buf_t *lb = new_component<gui_label_buf_t>();
					lb->buf().append(translator::translate(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name()));

					lb->buf().printf(": %i %s", sum, translator::translate("transferring"));
					lb->update();

					new_component<gui_fill_t>();
				}
				end_table();

				sort_cargo(cargoes, sort_mode);
				new_component<gui_halt_waiting_table_t>(cargoes, filter_bits, merge_condition_bits, 0, TRANSFER_MODE_OUT);
			}
		}
	}

	new_component<gui_fill_t>(false, true);
	set_size(get_min_size());
}
