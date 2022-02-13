/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_halt_indicator.h"

#include "../../bauer/goods_manager.h"
#include "../../dataobj/translator.h"
#include "../../simcolor.h"

#include "../gui_theme.h"


gui_halt_capacity_bar_t::gui_halt_capacity_bar_t(halthandle_t h, uint8 ft)
{
	if (ft > 2) { return; }
	freight_type = ft;
	halt = h;
}

void gui_halt_capacity_bar_t::draw(scr_coord offset)
{
	uint32 wainting_sum = 0;
	uint32 transship_in_sum = 0;
	uint32 leaving_sum = 0;
	bool overcrowded = false;
	const uint32 capacity= halt->get_capacity(freight_type);
	if (!capacity) { return; }
	switch (freight_type) {
		case 0:
			wainting_sum = halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_PAS));
			overcrowded = halt->is_overcrowded(goods_manager_t::INDEX_PAS);
			transship_in_sum = halt->get_transferring_goods_sum(goods_manager_t::get_info(goods_manager_t::INDEX_PAS)) - halt->get_leaving_goods_sum(goods_manager_t::get_info(goods_manager_t::INDEX_PAS));
			break;
		case 1:
			wainting_sum = halt->get_ware_summe(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL));
			overcrowded = halt->is_overcrowded(goods_manager_t::INDEX_MAIL);
			transship_in_sum = halt->get_transferring_goods_sum(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL)) - halt->get_leaving_goods_sum(goods_manager_t::get_info(goods_manager_t::INDEX_MAIL));
			break;
		case 2:
			for (uint8 g1 = 0; g1 < goods_manager_t::get_max_catg_index(); g1++) {
				if (g1 == goods_manager_t::INDEX_PAS || g1 == goods_manager_t::INDEX_MAIL)
				{
					continue;
				}
				const goods_desc_t *wtyp = goods_manager_t::get_info(g1);
				switch (g1) {
				case 0:
					wainting_sum += halt->get_ware_summe(wtyp);
					break;
				default:
					const uint8 count = goods_manager_t::get_count();
					for (uint32 g2 = 3; g2 < count; g2++) {
						goods_desc_t const* const wtyp2 = goods_manager_t::get_info(g2);
						if (wtyp2->get_catg_index() != g1) {
							continue;
						}
						wainting_sum += halt->get_ware_summe(wtyp2);
						transship_in_sum += halt->get_transferring_goods_sum(wtyp2, 0);
						leaving_sum += halt->get_leaving_goods_sum(wtyp2, 0);
					}
					break;
				}
			}
			overcrowded = ((wainting_sum + transship_in_sum) > capacity);
			transship_in_sum -= leaving_sum;
			break;
		default:
			return;
	}
	display_ddd_box_clip_rgb(pos.x + offset.x, pos.y + offset.y, HALT_CAPACITY_BAR_WIDTH + 2, GOODS_COLOR_BOX_HEIGHT, color_idx_to_rgb(MN_GREY0), color_idx_to_rgb(MN_GREY4));
	display_fillbox_wh_clip_rgb(pos.x+offset.x + 1, pos.y+offset.y + 1, HALT_CAPACITY_BAR_WIDTH, GOODS_COLOR_BOX_HEIGHT - 2, color_idx_to_rgb(MN_GREY2), true);
	// transferring (to this station) bar
	display_fillbox_wh_clip_rgb(pos.x+offset.x + 1, pos.y+offset.y + 1, min(100, (transship_in_sum + wainting_sum) * 100 / capacity), 6, color_idx_to_rgb(MN_GREY1), true);

	const PIXVAL col = overcrowded ? color_idx_to_rgb(COL_OVERCROWD) : COL_CLEAR;
	uint8 waiting_factor = min(100, wainting_sum * 100 / capacity);

	display_cylinderbar_wh_clip_rgb(pos.x+offset.x + 1, pos.y+offset.y + 1, HALT_CAPACITY_BAR_WIDTH * waiting_factor / 100, 6, col, true);

	set_size(scr_size(HALT_CAPACITY_BAR_WIDTH+2, GOODS_COLOR_BOX_HEIGHT));
	gui_container_t::draw(offset);
}


gui_halt_handled_goods_images_t::gui_halt_handled_goods_images_t(halthandle_t h, bool only_freight)
{
	halt = h;
	show_only_freight = only_freight;
	// It is necessary to specify the correct height first to eliminate the strange position movement immediately after display.
	set_size(scr_size(D_H_SPACE*2, D_LABEL_HEIGHT));
}

void gui_halt_handled_goods_images_t::draw(scr_coord offset)
{
	scr_coord_val xoff = D_H_SPACE;
	const uint8 start = show_only_freight ? goods_manager_t::INDEX_NONE+1 : 0;
	for (uint8 i = start; i<goods_manager_t::get_max_catg_index(); i++) {
		uint8 g_class = goods_manager_t::get_classes_catg_index(i) - 1;
		haltestelle_t::connexions_map *connexions = halt->get_connexions(i, g_class);

		if (!connexions->empty())
		{
			display_color_img_with_tooltip(goods_manager_t::get_info_catg_index(i)->get_catg_symbol(), pos.x+offset.x + xoff, pos.y + offset.y + D_GET_CENTER_ALIGN_OFFSET(10, D_LABEL_HEIGHT), 0, false, false, translator::translate(goods_manager_t::get_info_catg_index(i)->get_catg_name()));
			xoff += 14;
		}
	}
	set_size(scr_size(xoff + D_H_SPACE*2, D_LABEL_HEIGHT));
	gui_container_t::draw(offset);
}
