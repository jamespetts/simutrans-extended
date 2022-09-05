/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "linelist_stats_t.h"

#include "../simhalt.h"

#include "../dataobj/environment.h"
#include "../dataobj/translator.h"
#include "../player/simplay.h"
#include "../utils/cbuffer_t.h"

#include "components/gui_colorbox.h"
#include "components/gui_image.h"
#include "components/gui_label.h"
#include "components/gui_line_lettercode.h"

gui_line_handle_catg_img_t::gui_line_handle_catg_img_t(linehandle_t line)
{
	this->line = line;
}

void gui_line_handle_catg_img_t::draw(scr_coord offset)
{
	if( line.is_null() ) {
		return;
	}
	scr_coord_val offset_x = 2;
	size.w = line->get_goods_catg_index().get_count() * D_FIXED_SYMBOL_WIDTH+4;
	offset += pos;
	FOR(minivec_tpl<uint8>, const catg_index, line->get_goods_catg_index()) {
		display_color_img(goods_manager_t::get_info_catg_index(catg_index)->get_catg_symbol(), offset.x + offset_x + 2, offset.y + 3, 0, false, true);
		offset_x += D_FIXED_SYMBOL_WIDTH+2;
	}
}

scr_size gui_line_handle_catg_img_t::get_min_size() const
{
	return scr_size(size.w, D_FIXED_SYMBOL_WIDTH + 2);
}

scr_size gui_line_handle_catg_img_t::get_max_size() const
{
	return scr_size(scr_size::inf.w, D_FIXED_SYMBOL_WIDTH + 2);
}


gui_line_label_t::gui_line_label_t(linehandle_t l)
{
	line = l;
	set_table_layout(2,1);
	set_spacing(scr_size(D_H_SPACE,0));
	set_margin( scr_size(0,0), scr_size(0,0) );

	update_check();
}

void gui_line_label_t::update_check()
{
	bool need_update = false;

	// check 1 - numeric thing
	const uint16 seed = line->get_line_lettercode_style() + line->get_line_color_index() + line->get_owner()->get_player_color1();
	if (old_seed != seed) {
		old_seed = seed;
		need_update = true;
	}

	// check 2 - texts
	cbuffer_t buf;
	buf.printf("%s%s%s", line->get_linecode_l(), line->get_linecode_r(), line->get_name());
	if ( strcmp(buf.get_str(), name_buf.get_str())!=0 ) {
		name_buf.clear();
		name_buf.append(buf.get_str());
		need_update = true;
	}

	if (need_update) {
		update();
	}
}

void gui_line_label_t::update()
{
	remove_all();
	name_buf.clear();
	if( line.is_bound() ) {
		if (line->get_line_color_index() == 255) {
			new_component<gui_margin_t>(0);
		}
		else {
			new_component<gui_line_lettercode_t>(line->get_line_color())->set_line(line);
		}
		gui_label_buf_t *lb = new_component<gui_label_buf_t>(PLAYER_FLAG | color_idx_to_rgb(line->get_owner()->get_player_color1() + env_t::gui_player_color_dark));
		lb->buf().append(line->get_name());
		lb->update();
	}
	set_size(get_min_size());
}

void gui_line_label_t::draw(scr_coord offset)
{
	if( line.is_null() ) {
		return;
	}
	update_check();
	gui_aligned_container_t::draw(offset);
}
