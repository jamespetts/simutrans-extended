/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/*
 * Vehicle info including availability
 */

#include "sortable_table.h"

#include "../../simworld.h"
#include "../../simcolor.h"
#include "../../display/simgraph.h"

#include "../../dataobj/translator.h"

#include "../../utils/simstring.h"

#include "../../descriptor/intro_dates.h"

scr_coord_val table_cell_item_t::check_height(scr_coord_val new_height)
{
	size.h = max(new_height, size.h);
	draw_offset.y = (size.h - min_size.h) / 2;
	return size.h;
}

void table_cell_item_t::set_size(scr_size new_size)
{
	set_width(new_size.w);
	check_height(new_size.h);
}

void table_cell_item_t::set_width(scr_coord_val new_width)
{
	size.w = new_width + L_CELL_PADDING*2;
	switch (align) {
		case centered:
			draw_offset.x=(size.w-min_size.w)/2;
			break;
		case right:
			draw_offset.x = size.w - min_size.w - L_CELL_PADDING;
			break;
		case left:
		default:
			draw_offset.x = L_CELL_PADDING;
			break;
	}
}

void table_cell_item_t::draw(scr_coord offset)
{
	offset+=pos;
	if (highlight) { // for filter
		display_fillbox_wh_clip_rgb(offset.x+1, offset.y, size.w, size.h, SYSCOL_TD_BACKGROUND_HIGHLIGHT, false);
	}
	// draw border
	display_ddd_box_clip_rgb(offset.x, offset.y-1, size.w+1, size.h+1, SYSCOL_TD_BORDER, SYSCOL_TD_BORDER);
}



text_cell_t::text_cell_t(const char* text_, PIXVAL col, align_t align_)
{
	text= text_;
	color= col;
	align=align_;
	min_size = scr_size(proportional_string_width(translator::translate(text)), LINESPACE);
	set_size(min_size);
}

void text_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	offset+=pos;
	display_proportional_clip_rgb(offset.x+ draw_offset.x, offset.y+ draw_offset.y, translator::translate(text), ALIGN_LEFT, color, false);
}

coord_cell_t::coord_cell_t(const char* text, koord coord_, PIXVAL color, align_t align)
	: text_cell_t((text==NULL && coord_!=koord::invalid) ? coord.get_fullstr() : text, color, align)
{
	coord = coord_;
	min_size = scr_size(proportional_string_width(translator::translate(get_text())), LINESPACE);
	set_size(min_size);
}


value_cell_t::value_cell_t(sint64 value_, gui_chart_t::chart_suffix_t suffix, align_t align_, PIXVAL col)
{
	color = col;
	align = align_;
	value=value_;

	switch(suffix)
	{
		case gui_chart_t::MONEY:
			buf.append_money(value/100.0);
			break;
		case gui_chart_t::PERCENT:
			buf.append(value / 100.0, 2);
			buf.append("%");
			//color = value >= 0 ? (value > 0 ? MONEY_PLUS : SYSCOL_TEXT_UNUSED) : MONEY_MINUS;
			break;
		case gui_chart_t::KMPH:
			buf.append(value);
			buf.append(" km/h");
			break;
		case gui_chart_t::KW:
			if (value==0) {
				buf.append("-");
				align = centered;
				color = COL_INACTIVE;
			}
			else {
				buf.append(value);
				buf.append(" kW");
			}
			break;
		case gui_chart_t::TONNEN:
			if (value == 0) {
				buf.append("-");
				align = centered;
				color = COL_INACTIVE;
			}
			else {
				buf.printf("%.1ft", (float)value/1000.0);
			}
			break;
		case gui_chart_t::DATE:
			if (value == DEFAULT_RETIRE_DATE*12) {
				// empty cell
				////buf.append("-");
				////color = COL_INACTIVE;
			}
			else {
				buf.append(translator::get_short_date((uint16)value / 12, (uint16)value % 12));
			}
			break;
		case gui_chart_t::TIME:
		{
			if (value==0) {
				buf.append("--:--:--");
				color = COL_INACTIVE;
			}
			else {
				char as_clock[32];
				world()->sprintf_ticks(as_clock, sizeof(as_clock), value);
				buf.printf("%s", as_clock);
			}
			break;
		}
		case gui_chart_t::FORCE:
			if (value == 0) {
				buf.append("-");
				align = centered;
				color = COL_INACTIVE;
			}
			else {
				buf.append(value);
				buf.append(" kN");
			}
			break;
		case gui_chart_t::DISTANCE:
		{
			buf.printf("%.1fkm", (float)(value*world()->get_settings().get_meters_per_tile()/1000.0));
			break;
		}
		case gui_chart_t::STANDARD:
		default:
			buf.append(value);
			break;
	}
	min_size = scr_size(proportional_string_width(buf), LINESPACE);
	set_size(min_size);
}

void value_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	offset += pos;
	display_proportional_clip_rgb(offset.x + draw_offset.x, offset.y + draw_offset.y, translator::translate(buf), ALIGN_LEFT, color, false);
}


values_cell_t::values_cell_t(sint64 value_, sint64 sub_value_, PIXVAL col)
	: value_cell_t(value_)
{
	buf.clear();
	sub_buf.clear();

	color = col;
	min_size.h = LINESPACE;
	align = centered;

	sub_value=sub_value_;

	bool two_lines = false;

	if (value==0 && sub_value==0) {
		buf.append("-");
		color = SYSCOL_TEXT_WEAK;
	}
	if (value != 0) {
		buf.printf("%i", value);
	}
	if (sub_value != 0) {
		if (value != 0) {
			// display two lines
			sub_buf.printf("(%i)", sub_value);
			min_size.h += LINESPACE;
			two_lines = true;
		}
		else {
			buf.printf("(%i)", sub_value);
		}
	}
	if (two_lines) {
		sub_draw_offset_x = proportional_string_width(sub_buf) / 2;
		min_size.w = max(proportional_string_width(buf), sub_draw_offset_x * 2 + 1);
	}
	else {
		min_size.w = proportional_string_width(buf);
	}
	set_size(min_size);
}

void values_cell_t::set_width(scr_coord_val new_width)
{
	const scr_coord_val row1_width = proportional_string_width(buf);

	size.w = new_width + L_CELL_PADDING * 2;
	draw_offset.x = (size.w - row1_width) / 2;
}

void values_cell_t::draw(scr_coord offset)
{
	table_cell_item_t::draw(offset);

	offset += pos;
	display_proportional_clip_rgb(offset.x + draw_offset.x, offset.y + draw_offset.y, buf, ALIGN_LEFT, color, false);
	display_proportional_clip_rgb(offset.x + size.w / 2 - sub_draw_offset_x, offset.y + draw_offset.y + LINESPACE, sub_buf, ALIGN_LEFT, color, false);
}


gui_sort_table_row_t::gui_sort_table_row_t()
{
	row_height=LINESPACE+D_V_SPACE+1; // default is minimum height of row
}

gui_sort_table_row_t::~gui_sort_table_row_t()
{
	clear_ptr_vector(owned_cells);
}

void gui_sort_table_row_t::add_component(gui_component_t* comp)
{
	gui_container_t::add_component(comp);
	row_height = max(row_height, dynamic_cast<table_cell_item_t*>(comp)->check_height(row_height));
	min_widths.append(comp->get_size().w);
}

void gui_sort_table_row_t::set_highlight(sint32 i, bool highlight)
{
	if (owned_cells.get_count()<=i) return;
	owned_cells[i]->set_highlight(highlight);
}

void gui_sort_table_row_t::set_col(uint col, scr_coord_val width)
{
	assert(col < owned_cells.get_count());
	table_cell_item_t* item = owned_cells.get_element(col);
	item->set_width(width);
	item->set_pos(scr_coord(size.w, 0));
	size.w+= item->get_size().w; // added padding
}

scr_coord_val gui_sort_table_row_t::get_min_width(uint col) {
	assert(col < owned_cells.get_count());
	return owned_cells.get_element(col)->get_min_width();
}

void gui_sort_table_row_t::draw(scr_coord offset)
{
	// draw selected background
	if (selected) {
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, get_size().w, get_size().h, (focused ? SYSCOL_TH_BACKGROUND_SELECTED : SYSCOL_TR_BACKGROUND_SELECTED), true);
	}
	gui_container_t::draw(offset);
}

sint64 gui_sort_table_row_t::compare_value(const table_cell_item_t* a, const table_cell_item_t* b)
{
	sint64 cmp = 0;
	const value_cell_t* cell_a = dynamic_cast<const value_cell_t*>(a);
	const value_cell_t* cell_b = dynamic_cast<const value_cell_t*>(b);
	return cell_a->get_value() - cell_b->get_value();
}

sint64 gui_sort_table_row_t::compare_values(const table_cell_item_t* a, const table_cell_item_t* b)
{
	sint64 cmp=0;
	const values_cell_t* cell_a = dynamic_cast<const values_cell_t*>(a);
	const values_cell_t* cell_b = dynamic_cast<const values_cell_t*>(b);
	cmp = cell_a->get_value() - cell_b->get_value();
	if (cmp == 0) {
		cmp = cell_a->get_sub_value() - cell_b->get_sub_value();
	}
	return cmp;
}

int gui_sort_table_row_t::compare_text(const table_cell_item_t* a, const table_cell_item_t* b)
{
	const text_cell_t* cell_a = dynamic_cast<const text_cell_t*>(a);
	const text_cell_t* cell_b = dynamic_cast<const text_cell_t*>(b);
	const char* a_text = translator::translate(cell_a->get_text());
	const char* b_text = translator::translate(cell_b->get_text());
	return STRICMP(a_text, b_text);
}

int gui_sort_table_row_t::compare_coord(const table_cell_item_t* a, const table_cell_item_t* b)
{
	int cmp = 0;
	const coord_cell_t* cell_a = dynamic_cast<const coord_cell_t*>(a);
	const coord_cell_t* cell_b = dynamic_cast<const coord_cell_t*>(b);
	cmp = cell_a->get_coord().x - cell_b->get_coord().x;
	if (cmp == 0) {
		cmp = cell_a->get_coord().y - cell_b->get_coord().y;
	}
	return cmp;
}


int gui_sort_table_row_t::compare(const table_cell_item_t* a, const table_cell_item_t* b)
{
	sint64 cmp = 0;
	const uint8 data_type = a->get_type();
	if (data_type != b->get_type()) return 0;

	switch (data_type) {
		case table_cell_item_t::cell_value:
			cmp = gui_sort_table_row_t::compare_value(a, b);
			break;
		case table_cell_item_t::cell_values:
			cmp = gui_sort_table_row_t::compare_values(a, b);
			break;
		case table_cell_item_t::cell_text:
			cmp = gui_sort_table_row_t::compare_text(a, b);
			break;
		case table_cell_item_t::cell_coord:
			cmp = gui_sort_table_row_t::compare_coord(a, b);
			break;
		case table_cell_item_t::cell_no_sorting:
		default:
			// nothing to do
			break;
	}

	return cmp;
}
