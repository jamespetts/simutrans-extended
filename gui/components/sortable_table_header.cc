/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Vehicle info including availability
 */

#include "sortable_table_header.h"
#include "sortable_table.h"

#include "../../simworld.h"
#include "../../simcolor.h"
#include "../../display/simgraph.h"
#include "../../player/simplay.h"

#include "../../dataobj/translator.h"

#include "../../utils/simstring.h"



sortable_header_cell_t::sortable_header_cell_t(char const* const text_, bool yesno)
{
	enabled=yesno;
	text = text_;
	min_width = proportional_string_width(translator::translate(text)) + 10 /* arrow width */;
	size.h=D_BUTTON_HEIGHT;
}

void sortable_header_cell_t::draw(scr_coord offset)
{
	if (selected) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x + 1, pos.y + offset.y + 1, get_size().w - 1, get_size().h - 2, SYSCOL_TH_BACKGROUND_SELECTED, false);
	}
	PIXVAL text_color = selected ? SYSCOL_TH_TEXT_SELECTED : SYSCOL_TH_TEXT_TOP;
	display_ddd_box_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w+1, get_size().h, SYSCOL_TH_BORDER, SYSCOL_TH_BORDER);

	// draw text
	const scr_rect area(offset + pos, size-scr_size(5,0));
	if (text) {
		display_proportional_ellipsis_rgb(area, translator::translate(text), ALIGN_CENTER_H | ALIGN_CENTER_V | DT_CLIP, text_color, false);
	}
	if (enabled && selected) {
		// draw an arrow
		scr_coord_val xoff_arrow_center = pos.x + offset.x + size.w - D_H_SPACE - 3;
		display_fillbox_wh_clip_rgb(xoff_arrow_center, pos.y+offset.y + D_V_SPACE-1, 1, size.h - D_V_SPACE*2+2, SYSCOL_TH_TEXT_SELECTED, false);
		if (reversed) {
			// asc
			display_fillbox_wh_clip_rgb(xoff_arrow_center-1, pos.y+offset.y + D_V_SPACE,   3, 1, SYSCOL_TH_TEXT_SELECTED, false);
			display_fillbox_wh_clip_rgb(xoff_arrow_center-2, pos.y+offset.y + D_V_SPACE+1, 5, 1, SYSCOL_TH_TEXT_SELECTED, false);
		}
		else {
			// desc
			display_fillbox_wh_clip_rgb(xoff_arrow_center-1, pos.y+offset.y + size.h - D_V_SPACE - 1, 3, 1, SYSCOL_TH_TEXT_SELECTED, false);
			display_fillbox_wh_clip_rgb(xoff_arrow_center-2, pos.y+offset.y + size.h - D_V_SPACE - 2, 5, 1, SYSCOL_TH_TEXT_SELECTED, false);
		}
	}
}

void sortable_header_cell_t::set_width(scr_coord_val new_width)
{
	size.w = new_width + L_CELL_PADDING * 2;
}


gui_sort_table_header_t::gui_sort_table_header_t()
{
	set_size(scr_size(D_H_SPACE, D_BUTTON_HEIGHT));

}

gui_sort_table_header_t::~gui_sort_table_header_t()
{
	clear_ptr_vector(owned_cells);
}

void gui_sort_table_header_t::add_component(gui_component_t* comp)
{
	comp->set_focusable(true);
	gui_container_t::add_component(comp);
}

void gui_sort_table_header_t::set_col(uint col, scr_coord_val width)
{
	assert(col < components.get_count());
	sortable_header_cell_t*item = dynamic_cast<sortable_header_cell_t*>(components.get_element(col));
	item->set_width(width);
	item->set_pos(scr_coord(size.w, 0));
	size.w += item->get_size().w; // added padding
}

scr_coord_val gui_sort_table_header_t::get_min_width(uint col)
{
	if(col>=owned_cells.get_count()) return 0;
	return owned_cells.get_element(col)->get_min_width();
}

void gui_sort_table_header_t::set_selection(uint32 col)
{
	if (selected_col == col) {
	}

	selected_col=col;
	uint idx=0;
	for (auto& cell : owned_cells) {
		if (idx == col) {
			reversed ^= 1;
			cell->selected = true;
			set_focus(cell);
		}
		else {
			cell->selected = false;
			cell->reversed = false;
		}
		idx++;
	}
}

void gui_sort_table_header_t::draw(scr_coord offset)
{
	// draw background
	display_fillbox_wh_clip_rgb(pos.x + offset.x + D_H_SPACE, pos.y + offset.y, get_size().w-D_SCROLLBAR_WIDTH-D_H_SPACE, get_size().h, SYSCOL_TH_BACKGROUND_TOP, true);
	gui_container_t::draw(offset);
}

bool gui_sort_table_header_t::infowin_event(const event_t* ev)
{
	bool swallowed = gui_container_t::infowin_event(ev);

	if (IS_LEFTRELEASE(ev) && getroffen(ev->mouse_pos)) {
		uint idx=0;
		for (auto& cell : owned_cells) {
			if (cell->getroffen(ev->mouse_pos)) {
				set_focus(cell);
				if (selected_col == idx) {
					// already selected => inverse reversed state
					cell->reversed^=1;
				}
				selected_col=idx;
				reversed=cell->reversed;
				call_listeners((long)selected_col);
				break;
			}
			idx++;
		}
	}

	return swallowed;
}

