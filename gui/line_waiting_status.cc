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

		const uint8 cnv_section_at = cnv->get_reverse_schedule() ? cnv->get_schedule()->get_current_stop()
			: cnv->get_schedule()->get_current_stop() == 0 ? cnv->get_schedule()->entries.get_count()-1 : cnv->get_schedule()->get_current_stop() - 1;

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



gui_halt_waiting_catg_t::gui_halt_waiting_catg_t(halthandle_t h, uint8 catg, linehandle_t l, bool yesno)
{
	halt = h;
	line = l;
	catg_index = catg;
	divide_by_class = yesno;
	set_table_layout(1,0);
	update();
}

void gui_halt_waiting_catg_t::update()
{
	uint8 g_class = goods_manager_t::get_classes_catg_index(catg_index) - 1;
	haltestelle_t::connexions_map *connexions = halt->get_connexions(catg_index, g_class);

	remove_all();
	add_table(0,1);
	{
		if (connexions->empty()) {
			new_component<gui_label_t>("-", COL_INACTIVE);
		}
		else if (!update_seed) {
			new_component<gui_margin_t>(GOODS_COLOR_BOX_HEIGHT);
			new_component<gui_label_t>("0", SYSCOL_TEXT_WEAK);
		}
		else {
			bool got_one = false;
			bool overcrowded = (halt->get_status_color(catg_index == goods_manager_t::INDEX_PAS ? 0 : catg_index == goods_manager_t::INDEX_MAIL ? 1 : 2) == SYSCOL_OVERCROWDED);
			for (uint8 j = 0; j < goods_manager_t::get_count(); j++) {
				const goods_desc_t *wtyp = goods_manager_t::get_info(j);
				if (wtyp->get_catg_index() != catg_index) {
					continue;
				}
				const uint32 sum = line.is_bound() ? halt->get_ware_summe(wtyp, line) : halt->get_ware_summe(wtyp);
				if (sum > 0) {
					if (got_one) {
						new_component<gui_label_t>(", ", SYSCOL_TEXT);
					}

					const PIXVAL goods_color = wtyp->get_color();
					new_component<gui_colorbox_t>(goods_color)->set_size(GOODS_COLOR_BOX_SIZE);
					gui_label_buf_t *lb = new_component<gui_label_buf_t>(overcrowded ? SYSCOL_OVERCROWDED : SYSCOL_TEXT);
					const uint8 number_of_classes = wtyp->get_number_of_classes();
					if (divide_by_class && number_of_classes>1) {
						bool got_one_class = false;
						for (uint8 wealth = 0; wealth<number_of_classes; wealth++) {
							const uint32 csum = line.is_bound() ? halt->get_ware_summe(wtyp, line, number_of_classes ? wealth:255) : halt->get_ware_summe(wtyp, wealth);
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
				new_component<gui_margin_t>(GOODS_COLOR_BOX_HEIGHT);
				new_component<gui_label_t>("0", SYSCOL_TEXT_WEAK);
			}
		}
	}
	end_table();
	set_size(get_min_size());
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
				FORX(minivec_tpl<schedule_entry_t>, const& i, schedule->entries, ++entry_idx) {
					halthandle_t const halt = haltestelle_t::get_halt(i.pos, line->get_owner());
					if( !halt.is_bound() ) { continue; }

					// 1st row
					const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;
					new_component<gui_schedule_entry_number_t>(entry_idx, halt->get_owner()->get_player_color1(),
						is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
						scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
						halt->get_basis_pos3d()
					);

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
							new_component<gui_halt_waiting_catg_t>(halt, catg_index, filter_by_line ? line : linehandle_t(), divide_by_class);
						}
					}

					// 2nd row
					uint8 line_style = schedule->is_mirrored() ? gui_colored_route_bar_t::doubled : gui_colored_route_bar_t::solid;
					if (entry_idx== schedule->entries.get_count()-1) {
						if (mirrored) {
							continue;
						}
						else {
							line_style = gui_colored_route_bar_t::dashed;
						}
					}
					new_component<gui_colored_route_bar_t>(base_color, line_style, true);
					new_component_span<gui_line_convoy_location_t>(line, entry_idx, cols-1);
				}
			}
			end_table();
		}
		end_table();
	}
}

void gui_line_waiting_status_t::draw(scr_coord offset)
{
	if (line.is_bound()) {
		if (!line->get_schedule()->matches(world(), schedule)) {
			init();
		}
		// need to recheck child components size
		set_size(gui_aligned_container_t::get_size());
	}
	gui_aligned_container_t::draw(offset);
}
