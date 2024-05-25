/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "line_waiting_status.h"
#include "halt_info.h"
#include "components/gui_divider.h"
#include "components/gui_image.h"
#include "components/gui_schedule_item.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../simdepot.h"
#include "../obj/gebaeude.h" // depot name
#include "../vehicle/vehicle.h"  // get_route_index
#include "../display/viewport.h" // change_world_position

gui_convoy_access_arrow_t::gui_convoy_access_arrow_t(convoihandle_t cnv_)
{
	cnv = cnv_;
	if(cnv.is_bound()) {
		set_table_layout(2,1);
		const uint16 loading_rate = cnv->get_cargo_max() ? cnv->get_total_cargo() * 100 / cnv->get_cargo_max() : 0;
		PIXVAL state_col = cnv->get_overcrowded() ? SYSCOL_OVERCROWDED
			: loading_rate == 0 ? SYSCOL_EMPTY : loading_rate == 100 ? COL_WARNING : COL_SAFETY;
		new_component<gui_convoy_arrow_t>(state_col, cnv->get_reverse_schedule());
		gui_label_buf_t *lb = new_component<gui_label_buf_t>();
		lb->buf().printf("%i%% ", loading_rate);
		lb->update();
		lb->set_fixed_width(lb->get_min_size().w);
		set_size(get_min_size());
		tooltip_buf.clear();
		tooltip_buf.printf("%s, %u/%u", cnv->get_name(), cnv->get_total_cargo(), cnv->get_cargo_max());
	}
}

void gui_convoy_access_arrow_t::draw(scr_coord offset)
{
	if (getroffen(get_mouse_x() - offset.x, get_mouse_y() - offset.y)) {
		const scr_coord_val by = offset.y + pos.y;
		const scr_coord_val bh = size.h;

		win_set_tooltip(get_mouse_x() + TOOLTIP_MOUSE_OFFSET_X, by + bh + TOOLTIP_MOUSE_OFFSET_Y, tooltip_buf, this);
	}
	gui_aligned_container_t::draw(offset);
}



bool gui_convoy_access_arrow_t::infowin_event(const event_t * ev)
{
	if (cnv.is_bound()) {
		if (IS_LEFTRELEASE(ev)) {
			if (IS_SHIFT_PRESSED(ev)) {
				cnv->show_detail();
			}
			else {
				cnv->show_info();
			}
			return true;
		}
		else if (IS_RIGHTRELEASE(ev)) {
			world()->get_viewport()->change_world_position(cnv->get_pos());
			return true;
		}
	}
	return false;
}



void gui_line_convoy_location_t::check_convoy()
{
	vector_tpl<convoihandle_t> located_convoys;
	for (uint32 icnv = 0; icnv < line->count_convoys(); icnv++){
		convoihandle_t cnv = line->get_convoy(icnv);
		if( cnv->get_route()->get_count() == cnv->front()->get_route_index() ) {
			// stopping at the stop...
			continue;
		}

		uint8 cnv_section_at = cnv->get_schedule()->get_current_stop();
		const uint8 entries= line->get_schedule()->entries.get_count();
		const player_t *player = line->get_owner();
		for (uint8 i = 0; i< entries; i++) {
			if (cnv->get_reverse_schedule()) {
				cnv_section_at = (cnv->get_schedule()->get_current_stop()+i+1) % entries;
			}
			else {
				cnv_section_at = (entries+cnv->get_schedule()->get_current_stop()-1-i) % entries;
			}
			const koord3d check_pos = line->get_schedule()->entries[cnv_section_at].pos;
			halthandle_t halt = haltestelle_t::get_halt(check_pos, player);

			if (cnv->get_reverse_schedule()) {
				cnv_section_at = (cnv_section_at-1) % entries;
			}
			if (halt.is_bound()) {
				break;
			}
			else {
				if (!cnv->get_reverse_schedule() && cnv->front()->get_route_index() > cnv->get_route()->index_of(check_pos)) {
					break; // convoy is in this section
				}
				else if (cnv->get_reverse_schedule() && cnv->front()->get_route_index() > cnv->get_route()->index_of(check_pos)) {
					break; // convoy is in this section
				}
			}
		}

		if (cnv_section_at==section) {
			located_convoys.append(cnv);
		}
	}
	if (located_convoys.get_count() != convoy_count) {
		convoy_count = located_convoys.get_count();
		remove_all();
		for (uint32 icnv = 0; icnv < located_convoys.get_count(); icnv++) {
			new_component<gui_convoy_access_arrow_t>(located_convoys.get_element(icnv));
		}
		new_component<gui_fill_t>();
		set_size(gui_aligned_container_t::get_size());
	}
}


void gui_line_convoy_location_t::draw(scr_coord offset)
{
	if (line.is_bound()) {
		check_convoy();
	}
	gui_aligned_container_t::draw(offset);
}



gui_halt_waiting_catg_t::gui_halt_waiting_catg_t(halthandle_t h, uint8 catg, linehandle_t l, bool yesno, uint8 start, uint8 this_entry_, uint8 end)
{
	halt = h;
	line = l;
	catg_index = catg;
	divide_by_class = yesno;
	start_entry = start;
	this_entry = this_entry_ ==255 ? end : this_entry_;
	end_entry = this_entry_==255 ? 255 : end;
	set_table_layout(1,1);
	set_size(scr_size(D_LABEL_WIDTH, (line.is_bound() && this_entry && this_entry != end_entry-1 && line->get_schedule()->is_mirrored()) ? D_LABEL_HEIGHT*2+D_V_SPACE : D_LABEL_HEIGHT));
	update();
}

void gui_halt_waiting_catg_t::update()
{
	uint8 g_class = goods_manager_t::get_classes_catg_index(catg_index) - 1;
	haltestelle_t::connexions_map *connexions = halt->get_connexions(catg_index, g_class);

	remove_all();
	if (connexions->empty()) {
		new_component<gui_label_t>("-", COL_INACTIVE);
	}
	else if (!update_seed) {
		add_table(0,1);
		{
			new_component<gui_margin_t>(GOODS_COLOR_BOX_HEIGHT);
			new_component<gui_label_t>("0", SYSCOL_TEXT_WEAK);
		}
		end_table();
	}
	else {
		const bool check_direction = line.is_bound() && end_entry;
		const bool both_directions = check_direction && line->get_schedule()->is_mirrored() && this_entry && this_entry != end_entry-1;
		bool overcrowded = (halt->get_status_color(catg_index == goods_manager_t::INDEX_PAS ? 0 : catg_index == goods_manager_t::INDEX_MAIL ? 1 : 2) == SYSCOL_OVERCROWDED);
		for (uint8 dir = 0; dir < 2; dir++) {
			if (dir==1 && !both_directions) break;

			uint8 to = both_directions ? dir==0 ? this_entry - 1 : end_entry : this_entry;
			add_table(0, 1)->set_spacing(scr_size(0,D_V_SPACE>>1));
			{
				bool got_one = false;
				for (uint8 j = 0; j < goods_manager_t::get_count(); j++) {
					const goods_desc_t *wtyp = goods_manager_t::get_info(j);
					if (wtyp->get_catg_index() != catg_index) {
						continue;
					}
					const uint32 sum = check_direction ? halt->get_ware_summe_for(wtyp, line, 255, dir ? this_entry + 1 : start_entry, to)
						: line.is_bound() ? halt->get_ware_summe(wtyp, line) : halt->get_ware_summe(wtyp);
					if (sum > 0) {
						if (got_one) {
							new_component<gui_label_t>(", ", SYSCOL_TEXT);
						}
						else if (both_directions) {
							new_component<gui_convoy_arrow_t>(SYSCOL_TEXT, (dir==0));
							new_component<gui_margin_t>(D_H_SPACE);
						}

						const PIXVAL goods_color = wtyp->get_color();
						new_component<gui_colorbox_t>(goods_color)->set_size(GOODS_COLOR_BOX_SIZE);
						gui_label_buf_t *lb = new_component<gui_label_buf_t>(overcrowded ? SYSCOL_OVERCROWDED : SYSCOL_TEXT);
						const uint8 number_of_classes = wtyp->get_number_of_classes();
						if (divide_by_class && number_of_classes>1) {
							bool got_one_class = false;
							for (uint8 wealth = 0; wealth<number_of_classes; wealth++) {
								const uint32 csum = check_direction ? halt->get_ware_summe_for(wtyp, line, number_of_classes ? wealth : 255, dir ? this_entry+1 : start_entry, to)
									: line.is_bound() ? halt->get_ware_summe(wtyp, line, number_of_classes ? wealth:255) : halt->get_ware_summe(wtyp, wealth);
								if (csum) {
									if (got_one_class) lb->buf().append(", ");
									lb->buf().printf("%s %d", goods_manager_t::get_translated_wealth_name(catg_index,wealth), csum);
									got_one_class = true;
								}
							}
						}
						else {
							lb->buf().printf("%s %d", translator::translate(wtyp->get_name()), sum);
						}
						lb->update();

						got_one = true;
					}
				}
				if (!got_one) {
					scr_coord_val margin = (both_directions ? L_CONVOY_ARROW_WIDTH + D_H_SPACE*2 : 0)+GOODS_COLOR_BOX_HEIGHT;
					new_component<gui_margin_t>(margin);
					new_component<gui_label_t>("0", SYSCOL_TEXT_WEAK);
				}
			}
			end_table();
		}
	}
	set_size(scr_size(gui_aligned_container_t::get_min_size().w, size.h));
}

void gui_halt_waiting_catg_t::draw(scr_coord offset)
{
	if (halt.is_bound()) {
		// update seed
		uint32 temp;
		if (catg_index == goods_manager_t::INDEX_PAS) {
			temp = halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS));
		}
		else if (catg_index == goods_manager_t::INDEX_MAIL) {
			temp = halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL));
		}
		else {
			// freight
			temp = halt->get_finance_history(0, HALT_WAITING) - halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) - halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL));
		}

		if(temp!= update_seed) {
			update_seed = temp;
			update();
		}
		gui_aligned_container_t::draw(offset);
	}
}


gui_line_waiting_status_t::gui_line_waiting_status_t(linehandle_t line_)
{
	line = line_;

	set_table_layout(1, 0);
	set_table_frame(true,true);
	init();
}

void gui_line_waiting_status_t::init()
{
	remove_all();
	if (line.is_bound()) {
		schedule = line->get_schedule();
		if( !schedule->get_count() ) return; // nothing to show

		uint8 cols; // table cols
		cols = line->get_goods_catg_index().get_count()+show_name+1;
		const bool mirrored = schedule->is_mirrored();
		PIXVAL base_color;
		switch (line->get_line_color_index()) {
			case 254:
				base_color = color_idx_to_rgb(line->get_owner()->get_player_color1() + 4);
				break;
			case 255:
				base_color = color_idx_to_rgb(line->get_owner()->get_player_color1() + 2);
				break;
			default:
				base_color = line_color_idx_to_rgb(line->get_line_color_index());
				break;
		}
		add_table(1,1); // main table
		{
			gui_aligned_container_t *tbl = add_table(cols, 0);
			tbl->set_margin(scr_size(D_MARGIN_LEFT, D_MARGIN_TOP), scr_size(D_MARGIN_LEFT, D_MARGIN_TOP));
			tbl->set_spacing(scr_size(D_H_SPACE, 0));
			{
				// header
				new_component<gui_empty_t>();
				if (show_name) {
					new_component<gui_label_t>("stations");
				}
				for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
					if (line->get_goods_catg_index().is_contained(catg_index) ) {
						add_table(2,1);
						{
							new_component<gui_image_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), 0, ALIGN_NONE, true);
							new_component<gui_label_t>(goods_manager_t::get_info_catg_index(catg_index)->get_catg_name());
						}
						end_table();
					}
				}

				new_component<gui_empty_t>();
				// border
				new_component<gui_border_t>();
				for (uint8 i = 1; i < cols-1; ++i) {
					new_component<gui_border_t>();
				}

				uint8 entry_idx = 0;
				for(auto const& i : schedule->entries){
					halthandle_t const halt = haltestelle_t::get_halt(i.pos, line->get_owner());
					uint8 line_style = mirrored ? gui_colored_route_bar_t::doubled : gui_colored_route_bar_t::solid;
					// 1st row
					if( halt.is_bound() ) {
						const bool both_directions = line_style==gui_colored_route_bar_t::doubled && entry_idx>0 && entry_idx< schedule->entries.get_count()-1;
						minivec_tpl<uint8> entry_idx_of_this_stop;
						add_table(1,0)->set_spacing(NO_SPACING);
						{
							if (both_directions) {
								new_component<gui_colored_route_bar_t>(base_color, line_style, true);
							}
							const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;
							new_component<gui_schedule_entry_number_t>(entry_idx, halt->get_owner()->get_player_color1(),
								is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								i.pos
							);
							if (both_directions) {
								new_component<gui_colored_route_bar_t>(base_color, line_style, true);
							}
						}
						end_table();

						uint8 start_entry = both_directions ? entry_idx-1:0;
						uint8 end_entry = both_directions ? entry_idx+1: schedule->entries.get_count();
						if (both_directions) {

							for (; start_entry > 0; start_entry--) {
								halthandle_t entry_halt = haltestelle_t::get_halt(schedule->entries[start_entry].pos, line->get_owner());
								if (entry_halt.is_bound() && halt==entry_halt) {
									start_entry++;
									break;
								}
							}

							for (; end_entry < schedule->entries.get_count(); end_entry++) {
								halthandle_t entry_halt = haltestelle_t::get_halt(schedule->entries[end_entry].pos, line->get_owner());
								if (entry_halt.is_bound() && halt == entry_halt) {
									end_entry--;
									break;
								}
							}
						}
						else if (line_style != gui_colored_route_bar_t::doubled) {
							start_entry = (entry_idx+1)%schedule->entries.get_count();
							for (uint8 j = 0; j < schedule->entries.get_count(); j++) {
								halthandle_t entry_halt = haltestelle_t::get_halt(schedule->entries[j].pos, line->get_owner());
								if (halt == entry_halt) {
									entry_idx_of_this_stop.append(j);
									if (j > entry_idx) {
										end_entry = min(j, end_entry); break;
									}
								}
							}
							if (entry_idx_of_this_stop.get_count()>1 && end_entry == schedule->entries.get_count()) {
								end_entry = (entry_idx_of_this_stop[0]+schedule->entries.get_count()-1)% schedule->entries.get_count();
							}
						}

						if (show_name) {
							const bool can_serve = halt->can_serve(line);
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(can_serve ? SYSCOL_TEXT : COL_INACTIVE);
							if (!can_serve) {
								lb->buf().printf("(%s)", halt->get_name());
							}
							else {
								lb->buf().append(halt->get_name());
							}
							lb->update();
							lb->set_fixed_width(lb->get_min_size().w);
						}


						for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
							if (line->get_goods_catg_index().is_contained(catg_index)) {
								new_component<gui_halt_waiting_catg_t>(halt, catg_index, filter_by_line ? line : linehandle_t(), divide_by_class,
									start_entry, entry_idx_of_this_stop.get_count() > 1 ? 255 : entry_idx, end_entry);
							}
						}
					}
					else {
						// waypoint or depot
						const grund_t* gr = world()->lookup(i.pos);
						depot_t *depot = gr ? gr->get_depot() : nullptr;
						const bool is_waypoint = depot == nullptr;
						if (is_waypoint) {
							new_component<gui_waypoint_box_t>(base_color, line_style, i.pos);
						}
						else {
							new_component<gui_schedule_entry_number_t>(entry_idx, base_color, gui_schedule_entry_number_t::depot,
								scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
								i.pos);
						}


						if (show_name) {
							gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT_WEAK);
							if (is_waypoint) {

								if (gr) {
									if (const char *label_text = gr->get_text()) {
										lb->buf().printf(" %s", label_text);
									}
									else {
										lb->buf().printf(" %s %s", translator::translate("Wegpunkt"), i.pos.get_2d().get_fullstr());
									}
								}
								else {
									lb->buf().printf("%s", translator::translate("Invalid coordinate"));
								}
							}
							else {
								// depot
								lb->buf().printf("%s", depot->get_name());
							}
							lb->update();
							lb->set_fixed_width(lb->get_min_size().w);
						}

						if (cols==3) {
							new_component<gui_empty_t>();
						}
						else if (cols>3) {
							new_component_span<gui_empty_t>(cols - 2);
						}
					}

					// 2nd row
					if (entry_idx== schedule->entries.get_count()-1) {
						if (mirrored) {
							break;
						}
						else {
							line_style = gui_colored_route_bar_t::dashed;
						}
					}
					new_component<gui_colored_route_bar_t>(base_color, line_style, true);
					new_component_span<gui_line_convoy_location_t>(line, entry_idx, cols-1);

					entry_idx++;
				}
			}
			end_table();
		}
		end_table();
		update_time = world()->get_ticks();
	}
}

void gui_line_waiting_status_t::draw(scr_coord offset)
{
	if (line.is_bound()) {
		if (!line->get_schedule()->matches(world(), schedule) || world()->get_ticks() - update_time > 10000) {
			// We have to deal with changing the stop name and switching between waypoint and stop,
			// but since it is difficult to constantly monitor changes outside the component,
			// we deal with it by automatically updating the entire table at regular intervals.
			init();
		}
		// need to recheck child components size
		set_size(gui_aligned_container_t::get_size());
	}
	gui_aligned_container_t::draw(offset);
}
