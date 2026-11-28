/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "tooltip.h"

#include "simwin.h"
#include "gui_theme.h"

#include "../simskin.h"
#include "../descriptor/skin_desc.h"
#include "../simcolor.h"
#include "../dataobj/environment.h"
#include "../display/simgraph.h"
#include "../utils/simstring.h"
#include "../macros.h"

#include <stdio.h>


#define TOOLTIP_PADDING 6
#define TOOLTIP_LINE_GAP 3
#define TOOLTIP_GROUP_GAP 6
#define TOOLTIP_INDENT 24
#define TOOLTIP_ICON_GAP 5
#define TOOLTIP_TAIL_WIDTH 14
#define TOOLTIP_TAIL_LENGTH 10
#define TOOLTIP_RADIUS 8
#define TOOLTIP_MAX_WIDTH 420


static image_id get_symbol_image(tooltip_symbol_t symbol)
{
	const skin_desc_t *desc = NULL;
	switch(symbol)
	{
		case TOOLTIP_SYMBOL_COST:         desc = skinverwaltung_t::tooltip_cost; break;
		case TOOLTIP_SYMBOL_FORGE:        desc = skinverwaltung_t::tooltip_forge; break;
		case TOOLTIP_SYMBOL_MAINTENANCE:  desc = skinverwaltung_t::tooltip_maintenance; break;
		case TOOLTIP_SYMBOL_SPEED:        desc = skinverwaltung_t::tooltip_speed != NULL ? skinverwaltung_t::tooltip_speed : skinverwaltung_t::fastforwardsymbol; break;
		case TOOLTIP_SYMBOL_WEIGHT:       desc = skinverwaltung_t::tooltip_weight; break;
		case TOOLTIP_SYMBOL_DURABILITY:   desc = skinverwaltung_t::tooltip_durability; break;
		case TOOLTIP_SYMBOL_LAND:         desc = skinverwaltung_t::tooltip_land != NULL ? skinverwaltung_t::tooltip_land : skinverwaltung_t::intown; break;
		case TOOLTIP_SYMBOL_TREES:        desc = skinverwaltung_t::tooltip_trees; break;
		case TOOLTIP_SYMBOL_STRUCTURES:   desc = skinverwaltung_t::tooltip_structures; break;
		case TOOLTIP_SYMBOL_DISTANCE:     desc = skinverwaltung_t::tooltip_distance != NULL ? skinverwaltung_t::tooltip_distance : skinverwaltung_t::travel_time; break;
		case TOOLTIP_SYMBOL_TIME:         desc = skinverwaltung_t::service_frequency; break;
		case TOOLTIP_SYMBOL_PASSENGERS:   desc = skinverwaltung_t::passengers; break;
		case TOOLTIP_SYMBOL_MAIL:         desc = skinverwaltung_t::mail; break;
		case TOOLTIP_SYMBOL_GOODS:        desc = skinverwaltung_t::goods; break;
		case TOOLTIP_SYMBOL_POWER:        desc = skinverwaltung_t::electricity; break;
		case TOOLTIP_SYMBOL_RESTRICTION:
			if(skinverwaltung_t::tooltip_restriction != NULL) {
				return skinverwaltung_t::tooltip_restriction->get_image_id(0);
			}
			return skinverwaltung_t::alerts != NULL ? skinverwaltung_t::alerts->get_image_id(2) : IMG_EMPTY;
		case TOOLTIP_SYMBOL_TOTAL:        desc = skinverwaltung_t::tooltip_total != NULL ? skinverwaltung_t::tooltip_total : skinverwaltung_t::tooltip_cost; break;
		default: break;
	}
	return desc != NULL ? desc->get_image_id(0) : IMG_EMPTY;
}


static image_id get_line_image(const tooltip_line_t &line)
{
	return line.custom_icon != IMG_EMPTY ? line.custom_icon : get_symbol_image(line.symbol);
}


static scr_size get_image_size(image_id image)
{
	if(image == IMG_EMPTY)
	{
		return scr_size(0, 0);
	}
	scr_coord_val xoff, yoff, w, h;
	display_get_image_offset(image, &xoff, &yoff, &w, &h);
	return scr_size(w, h);
}


static void draw_image_at(image_id image, scr_coord_val x, scr_coord_val y, bool dirty)
{
	scr_coord_val xoff, yoff, w, h;
	display_get_image_offset(image, &xoff, &yoff, &w, &h);
	// display_color_img adds the image's stored x/y offset; subtract it so the
	// visible sprite lands on the requested position.
	display_color_img(image, x - xoff, y - yoff, 0, false, dirty);
}


sint32 tooltip_t::get_max_width()
{
	const sint32 screen_width = display_get_width() - 2 * D_MARGIN_LEFT;
	return max(160, min(screen_width, TOOLTIP_MAX_WIDTH));
}


sint32 tooltip_t::get_icon_width(const tooltip_line_t &line)
{
	if(line.custom_icon != IMG_EMPTY)
	{
		const sint32 width = get_image_size(line.custom_icon).w;
		return width > 0 ? width : env_t::iconsize.w;
	}
	if(line.symbol != TOOLTIP_SYMBOL_NONE)
	{
		const image_id image = get_symbol_image(line.symbol);
		if(image != IMG_EMPTY)
		{
			const sint32 width = get_image_size(image).w;
			return width > 0 ? width : 24;
		}
	}
	return 0;
}


static sint32 get_icon_height(const tooltip_line_t &line)
{
	if(line.custom_icon != IMG_EMPTY)
	{
		const sint32 height = get_image_size(line.custom_icon).h;
		return height > 0 ? height : env_t::iconsize.h;
	}
	if(line.symbol != TOOLTIP_SYMBOL_NONE)
	{
		const image_id image = get_symbol_image(line.symbol);
		if(image != IMG_EMPTY)
		{
			const sint32 height = get_image_size(image).h;
			return height > 0 ? height : 24;
		}
	}
	return 0;
}


sint32 tooltip_t::count_wrapped_lines(const char *text, sint32 max_width)
{
	if(text == NULL  ||  *text == 0  ||  max_width <= 0)
	{
		return 0;
	}

	sint32 line_count = 1;
	sint32 line_width = 0;
	const char *p = text;
	const sint32 space_width = display_calc_proportional_string_len_width(" ", 1);

	while(*p != 0)
	{
		while(*p == ' ')
		{
			p++;
		}
		const char *word_start = p;
		while(*p != 0  &&  *p != ' ')
		{
			p++;
		}
		const size_t word_len = (size_t)(p - word_start);
		if(word_len == 0)
		{
			break;
		}
		const sint32 word_width = display_calc_proportional_string_len_width(word_start, word_len);
		if(line_width > 0  &&  line_width + space_width + word_width > max_width)
		{
			line_count++;
			line_width = word_width;
		}
		else
		{
			line_width += (line_width > 0 ? space_width : 0) + word_width;
		}
	}
	return line_count;
}


static uint32 draw_wrapped_text(scr_coord_val x, scr_coord_val y, const char *text, sint32 max_width, PIXVAL color, bool dirty)
{
	if(text == NULL  ||  *text == 0  ||  max_width <= 0)
	{
		return 0;
	}

	cbuffer_t line;
	sint32 line_width = 0;
	uint32 line_count = 1;
	const char *p = text;
	const sint32 space_width = display_calc_proportional_string_len_width(" ", 1);

	while(*p != 0)
	{
		while(*p == ' ')
		{
			p++;
		}
		const char *word_start = p;
		while(*p != 0  &&  *p != ' ')
		{
			p++;
		}
		const size_t word_len = (size_t)(p - word_start);
		if(word_len == 0)
		{
			break;
		}
		const sint32 word_width = display_calc_proportional_string_len_width(word_start, word_len);
		if(line_width > 0  &&  line_width + space_width + word_width > max_width)
		{
			display_text_proportional_len_clip_rgb(x, y, line.get_str(), ALIGN_LEFT | DT_CLIP, color, dirty, line.len());
			line.clear();
			line_width = 0;
			line_count++;
			y += LINESPACE;
		}
		if(line.len() > 0)
		{
			line.append(" ", 1);
			line_width += space_width;
		}
		line.append(word_start, word_len);
		line_width += word_width;
	}

	if(line.len() > 0)
	{
		display_text_proportional_len_clip_rgb(x, y, line.get_str(), ALIGN_LEFT | DT_CLIP, color, dirty, line.len());
	}
	return line_count;
}


tooltip_line_t &tooltip_t::add(tooltip_symbol_t symbol, const char *text)
{
	lines.append(tooltip_line_t(symbol, IMG_EMPTY, text));
	return lines.back();
}


tooltip_line_t &tooltip_t::add_icon(image_id icon, const char *text)
{
	lines.append(tooltip_line_t(TOOLTIP_SYMBOL_NONE, icon, text));
	return lines.back();
}


tooltip_line_t &tooltip_t::add_value(tooltip_symbol_t symbol, const char *label, sint64 value, const char *unit)
{
	char buf[256];
	if(unit != NULL  &&  *unit != 0)
	{
		snprintf(buf, sizeof(buf), "%s: %lld %s", label, (long long)value, unit);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s: %lld", label, (long long)value);
	}
	return add(symbol, buf);
}


tooltip_line_t &tooltip_t::add_money(tooltip_symbol_t symbol, const char *label, sint64 simucents, const char *unit)
{
	if(simucents < 0)
	{
		simucents = -simucents;
	}
	char money[128];
	money_to_string(money, (double)simucents / 100.0);

	char buf[256];
	if(unit != NULL  &&  *unit != 0)
	{
		snprintf(buf, sizeof(buf), "%s: %s %s", label, money, unit);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%s: %s", label, money);
	}
	return add(symbol, buf);
}


static sint32 corner_inset(sint32 row, sint32 height, sint32 radius)
{
	const sint32 edge = min(row, height - 1 - row);
	if(edge >= radius)
	{
		return 0;
	}
	static const sint32 inset[TOOLTIP_RADIUS] = { 8, 4, 2, 1, 1, 0, 0, 0 };
	return inset[edge];
}


void tooltip_t::draw(scr_coord pos, scr_coord anchor, bool dirty) const
{
	if(lines.empty())
	{
		return;
	}

	const sint32 max_width = get_max_width();

	// The title line may carry a toolbar icon much larger than the per-line
	// symbols. Give it its own text offset so it does not push every other line
	// to the right.
	sint32 symbol_column = 0;
	for(const tooltip_line_t &line : lines)
	{
		if(line.custom_icon == IMG_EMPTY)
		{
			const sint32 icon_width = get_icon_width(line);
			if(icon_width > 0)
			{
				symbol_column = max(symbol_column, icon_width + TOOLTIP_ICON_GAP);
			}
		}
	}

	sint32 content_width = 0;
	sint32 height = 2 * TOOLTIP_PADDING;
	for(const tooltip_line_t &line : lines)
	{
		const sint32 text_offset = (line.custom_icon != IMG_EMPTY ? get_icon_width(line) + TOOLTIP_ICON_GAP : symbol_column) + line.indent * TOOLTIP_INDENT;
		const sint32 text_max_width = max(40, max_width - 2 * TOOLTIP_PADDING - text_offset);
		const sint32 raw_width = display_calc_proportional_string_len_width(line.text.get_str(), line.text.len());
		content_width = max(content_width, text_offset + min(raw_width, text_max_width));
		const sint32 wrapped = count_wrapped_lines(line.text.get_str(), text_max_width);
		const sint32 line_height = max(LINESPACE, get_icon_height(line));
		if(line.total)
		{
			height += TOOLTIP_GROUP_GAP;
		}
		height += max(1, wrapped) * line_height + TOOLTIP_LINE_GAP;
		if(line.title  ||  line.total)
		{
			height += TOOLTIP_GROUP_GAP;
		}
	}

	sint32 width = 2 * TOOLTIP_PADDING + content_width;
	width = max(width, 2 * TOOLTIP_PADDING + 40);
	width = min(width, max_width);
	height = max(height, 2 * TOOLTIP_PADDING + LINESPACE);

	scr_size size(width, height);
	win_clamp_xywh_position(pos, size, true);

	const PIXVAL body_color = env_t::tooltip_color;
	const PIXVAL border_color = display_blend_colors(body_color, color_idx_to_rgb(COL_BLACK), 35);
	const PIXVAL highlight_color = display_blend_colors(body_color, color_idx_to_rgb(COL_WHITE), 25);
	const PIXVAL shadow_color = display_blend_colors(body_color, color_idx_to_rgb(COL_BLACK), 45);
	const PIXVAL text_color = env_t::tooltip_textcolor;
	const PIXVAL rule_color = display_blend_colors(body_color, text_color, 50);

	enum tail_side_t { TAIL_NONE, TAIL_TOP, TAIL_BOTTOM, TAIL_LEFT, TAIL_RIGHT };
	tail_side_t tail_side = TAIL_NONE;
	if(anchor.y < pos.y)
	{
		tail_side = TAIL_TOP;
	}
	else if(anchor.y >= pos.y + height)
	{
		tail_side = TAIL_BOTTOM;
	}
	else if(anchor.x < pos.x)
	{
		tail_side = TAIL_LEFT;
	}
	else if(anchor.x >= pos.x + width)
	{
		tail_side = TAIL_RIGHT;
	}

	// A soft shadow one pixel beyond the bottom and right edges.
	for(sint32 row = TOOLTIP_RADIUS; row < height; row++)
	{
		const sint32 inset = corner_inset(row, height, TOOLTIP_RADIUS);
		display_fillbox_wh_clip_rgb(pos.x + width - inset, pos.y + row + 1, 1, 1, shadow_color, dirty);
	}
	for(sint32 col = TOOLTIP_RADIUS; col < width; col++)
	{
		display_fillbox_wh_clip_rgb(pos.x + col, pos.y + height, 1, 1, shadow_color, dirty);
	}

	// Rounded body: the outer pass is the border, the inner pass the fill.
	for(sint32 row = 0; row < height; row++)
	{
		const sint32 inset = corner_inset(row, height, TOOLTIP_RADIUS);
		display_fillbox_wh_clip_rgb(pos.x + inset, pos.y + row, width - 2 * inset, 1, border_color, dirty);
	}
	for(sint32 row = 1; row < height - 1; row++)
	{
		const sint32 inset = corner_inset(row, height, TOOLTIP_RADIUS) + 1;
		display_fillbox_wh_clip_rgb(pos.x + inset, pos.y + row, width - 2 * inset, 1, body_color, dirty);
	}
	display_fillbox_wh_clip_rgb(pos.x + TOOLTIP_RADIUS, pos.y + 1, width - 2 * TOOLTIP_RADIUS, 1, highlight_color, dirty);

	if(tail_side == TAIL_TOP  ||  tail_side == TAIL_BOTTOM)
	{
		const sint32 half_tail = TOOLTIP_TAIL_WIDTH / 2;
		const sint32 tail_x = clamp(anchor.x - pos.x, TOOLTIP_RADIUS + half_tail, width - TOOLTIP_RADIUS - half_tail);
		const sint32 base_y = tail_side == TAIL_TOP ? pos.y : pos.y + height - 1;
		const sint32 tip_y = tail_side == TAIL_TOP ? base_y - TOOLTIP_TAIL_LENGTH : base_y + TOOLTIP_TAIL_LENGTH;

		for(sint32 row = 0; row < TOOLTIP_TAIL_LENGTH; row++)
		{
			const sint32 row_width = (TOOLTIP_TAIL_WIDTH * (row + 1)) / TOOLTIP_TAIL_LENGTH;
			const sint32 row_x = tail_x - row_width / 2;
			const sint32 row_y = tail_side == TAIL_TOP ? tip_y + row : base_y + 1 + row;
			display_fillbox_wh_clip_rgb(row_x, row_y, row_width, 1, body_color, dirty);
		}
		display_fillbox_wh_clip_rgb(tail_x - half_tail, base_y, TOOLTIP_TAIL_WIDTH, 1, body_color, dirty);
		display_direct_line_rgb(tail_x, tip_y, tail_x - half_tail, base_y, border_color);
		display_direct_line_rgb(tail_x, tip_y, tail_x + half_tail, base_y, border_color);
	}
	else if(tail_side == TAIL_LEFT  ||  tail_side == TAIL_RIGHT)
	{
		const sint32 half_tail = TOOLTIP_TAIL_WIDTH / 2;
		const sint32 tail_y = clamp(anchor.y - pos.y, TOOLTIP_RADIUS + half_tail, height - TOOLTIP_RADIUS - half_tail);
		const sint32 base_x = tail_side == TAIL_LEFT ? pos.x : pos.x + width - 1;
		const sint32 tip_x = tail_side == TAIL_LEFT ? base_x - TOOLTIP_TAIL_LENGTH : base_x + TOOLTIP_TAIL_LENGTH;

		for(sint32 col = 0; col < TOOLTIP_TAIL_LENGTH; col++)
		{
			const sint32 col_height = (TOOLTIP_TAIL_WIDTH * (col + 1)) / TOOLTIP_TAIL_LENGTH;
			const sint32 col_x = tail_side == TAIL_LEFT ? tip_x + col : base_x + 1 + col;
			const sint32 col_y = tail_y - col_height / 2;
			display_fillbox_wh_clip_rgb(col_x, col_y, 1, col_height, body_color, dirty);
		}
		display_fillbox_wh_clip_rgb(base_x, tail_y - half_tail, 1, TOOLTIP_TAIL_WIDTH, body_color, dirty);
		display_direct_line_rgb(tip_x, tail_y, base_x, tail_y - half_tail, border_color);
		display_direct_line_rgb(tip_x, tail_y, base_x, tail_y + half_tail, border_color);
	}

	sint32 line_y = pos.y + TOOLTIP_PADDING;

	for(const tooltip_line_t &line : lines)
	{
		const sint32 indent = line.indent * TOOLTIP_INDENT;
		const sint32 half_rule_width = (width - 2 * TOOLTIP_PADDING) / 2;
		if(line.total)
		{
			line_y += TOOLTIP_GROUP_GAP;
			display_fillbox_wh_clip_rgb(pos.x + TOOLTIP_PADDING, line_y - TOOLTIP_GROUP_GAP / 2, half_rule_width, 1, rule_color, dirty);
		}

		const image_id image = get_line_image(line);
		const scr_size icon_size = get_image_size(image);
		const sint32 icon_height = get_icon_height(line);
		const sint32 line_height = max(LINESPACE, icon_height);
		const sint32 text_offset = (line.custom_icon != IMG_EMPTY ? get_icon_width(line) + TOOLTIP_ICON_GAP : symbol_column) + indent;
		const sint32 text_max_width = max(40, max_width - 2 * TOOLTIP_PADDING - text_offset);
		const sint32 text_x = pos.x + TOOLTIP_PADDING + text_offset;

		if(image != IMG_EMPTY)
		{
			draw_image_at(image, pos.x + TOOLTIP_PADDING + indent, line_y + (line_height - icon_size.h) / 2, dirty);
		}

		const uint32 wrapped = draw_wrapped_text(text_x, line_y + (line_height - LINESPACE) / 2, line.text.get_str(), text_max_width, text_color, dirty);
		line_y += max(1, (sint32)wrapped) * line_height + TOOLTIP_LINE_GAP;
		if(line.title)
		{
			display_fillbox_wh_clip_rgb(pos.x + TOOLTIP_PADDING, line_y + 1, width - 2 * TOOLTIP_PADDING, 1, rule_color, dirty);
			line_y += TOOLTIP_GROUP_GAP;
		}
		else if(line.total)
		{
			display_fillbox_wh_clip_rgb(pos.x + TOOLTIP_PADDING, line_y + 1, half_rule_width, 1, rule_color, dirty);
			line_y += TOOLTIP_GROUP_GAP;
		}
	}
}
