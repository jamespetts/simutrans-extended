/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string.h>

#include "../gui_theme.h"
#include "gui_fixedwidth_textarea.h"
#include "../../dataobj/translator.h"
#include "../../utils/cbuffer_t.h"
#include "../../unicode.h"
#include "../../simcolor.h"


bool parse_text_links(const char *text, cbuffer_t &plain, slist_tpl<gui_text_link_t> &links)
{
	plain.clear();
	links.clear();
	if (text == NULL || strstr(text, "<a") == NULL)
	{
		return false;
	}

	const char *p = text;
	bool found = false;
	while (*p != 0)
	{
		const char *open = strstr(p, "<a");
		if (open == NULL)
		{
			plain.append(p);
			break;
		}
		const char *tag_end = strchr(open, '>');
		const char *close = tag_end != NULL ? strstr(tag_end + 1, "</a>") : NULL;
		if (tag_end == NULL || close == NULL)
		{
			// An incomplete tag is not markup; keep it as plain text.
			plain.append(p);
			break;
		}

		// The href target, if the opening tag has one. A link without a target
		// is displayed as ordinary text and reports a click with an empty
		// target, which the window ignores.
		cbuffer_t target;
		const char *href = strstr(open, "href");
		if (href != NULL && href < tag_end)
		{
			const char *q = href + 4;
			while (*q == ' ' || *q == '=')
			{
				q++;
			}
			const char quote = (*q == '"' || *q == '\'') ? *q : 0;
			if (quote != 0)
			{
				q++;
			}
			const char *end = q;
			while (*end != 0 && *end != '>' && (quote != 0 ? *end != quote : *end != ' '))
			{
				end++;
			}
			target.append(q, (size_t)(end - q));
		}

		plain.append(p, (size_t)(open - p));
		gui_text_link_t link;
		link.start = (size_t)plain.len();
		plain.append(tag_end + 1, (size_t)(close - (tag_end + 1)));
		link.end = (size_t)plain.len();
		link.target = target;
		links.append(link);
		found = true;
		p = close + 4;
	}
	return found;
}


gui_fixedwidth_textarea_t::gui_fixedwidth_textarea_t(cbuffer_t* buf_, const sint16 width) :
	plain_text_used(false),
	reserved_area(0, 0)
{
	buf = buf_;
	set_width(width);
}

void gui_fixedwidth_textarea_t::recalc_size()
{
	scr_size newsize = calc_display_text(scr_coord::invalid, false);
	if (newsize.h != size.h) {
		gui_component_t::set_size( newsize );
	}
}



void gui_fixedwidth_textarea_t::set_width(const scr_coord_val width)
{
	if(  width>0  ) {
		// height is simply reset to 0 as it requires recalculation anyway
		size = scr_size(width,0);
		set_visible(true);

		scr_size newsize = calc_display_text(scr_coord::invalid, false);
		gui_component_t::set_size( newsize );
	}
}



void gui_fixedwidth_textarea_t::set_reserved_area(const scr_size area)
{
	if(  area.w>=0  &&  area.h>=0  ) {
		reserved_area = area;
	}
}



scr_size gui_fixedwidth_textarea_t::get_min_size() const
{
	scr_size size = calc_display_text(scr_coord(0,0), false);
	size.clip_lefttop(reserved_area);
	return size;
}


scr_size gui_fixedwidth_textarea_t::get_max_size() const
{
	return scr_size::inf;
}



/* calculates the height of the text that flows around the world_view
 * if draw is true, it will also draw the text
 * borrowed from ding_infowin_t::calc_draw_info() with adaptation
 */
scr_size gui_fixedwidth_textarea_t::calc_display_text(const scr_coord offset, const bool draw) const
{
	// A buffer with link markup is laid out as its plain text, with the linked
	// parts underlined. A buffer without markup is used unchanged, so existing
	// information windows keep their exact layout.
	plain_text_used = parse_text_links(*buf, plain, links);
	const char *text = plain_text_used ? (const char*)plain : (const char*)*buf;
	if (draw)
	{
		link_rects.clear();
	}

	scr_coord_val x=0, word_x=0, y = 0, new_width=get_size().w;

	const utf8 *p = (const utf8 *)text;
	const utf8 *line_start = p;
	const utf8 *word_start = p;
	const utf8 *line_end  = p;

	// pass 1 (and not drawing): find out if we can shrink width
	if(*text  &&  !draw  &&   reserved_area.w > 0   ) {
		scr_coord_val new_lines = 0;
		scr_coord_val x_size = 0;

		if ((text != NULL) && (*text != '\0')) {
			const char* line = text;
			const char* next;

			do {
				next = strchr(line, '\n');
				const size_t len = next ? next - line : 99999;
				// we are in the image area
				const int px_len = display_calc_proportional_string_len_width(line, len) + reserved_area.w;

				if (px_len > x_size) {
					x_size = px_len;
				}

				new_lines += LINESPACE;
			} while (new_lines<reserved_area.h  &&  next != NULL && ((void)(line = next + 1), *line != 0));
		}
		if (x_size < new_width) {
			new_width = x_size;
		}
	}

	// pass 2: height caluclation and drawing (if requested)

	// also in unicode *c==0 is end
	while(  *p!= UNICODE_NUL  ||  p!=line_end  ) {

		// force at end of text or newline
		const scr_coord_val max_width = ( y < reserved_area.h ) ? new_width-reserved_area.w : new_width;

		// smaller than the allowed width?
		do {
			// end of line?
			utf32 next_char = utf8_decoder_t::decode(p);

			if(next_char==0  ||  next_char=='\n') {
				line_end = p-1;
				if(  next_char == 0  ) {
					p--;
				}
				word_start = p;
				word_x = 0;
				break;
			}
			// Space: Maybe break here
			else if(  next_char==' '  ||  (next_char >= 0x3000  &&   next_char<0xFE70)  ) {
				// ignore space at start of line
				if(next_char!=' '  ||  x>0) {
					x += (scr_coord_val)display_get_char_width( next_char );
				}
				word_start = p;
				word_x = 0;
			}
			else {
				// normal char: retrieve and calculate width
				int ch_width = display_get_char_width( next_char );
				x += ch_width;
				word_x += ch_width;
			}
		} while(  x<=max_width  );

		// spaces at the end can be omitted
		line_end = word_start;
		if(line_end==line_start) {
			// too long word for a single line => break the word
			word_start = line_end = p;
			word_x = 0;
		}
		else if(word_start[-1]==' '  ||  word_start[-1]=='\n') {
			line_end --;
		}

		// start of new line or end of text
		if(draw  &&  (line_end-line_start)!=0) {
			display_text_proportional_len_clip_rgb( offset.x, offset.y+y, (const char *)line_start, ALIGN_LEFT | DT_CLIP, SYSCOL_TEXT, true, (size_t)(line_end - line_start) );

			// Record a clickable rectangle for every link part on this line and
			// underline it.
			if (plain_text_used && !links.empty())
			{
				const size_t line_begin = (size_t)((const char*)line_start - text);
				const size_t line_length = (size_t)((const char*)line_end - (const char*)line_start);
				uint32 link_index = 0;
				for (gui_text_link_t const &link : links)
				{
					if (link.end > line_begin && link.start < line_begin + line_length)
					{
						const size_t seg_begin = (link.start > line_begin ? link.start : line_begin) - line_begin;
						const size_t seg_end = (link.end < line_begin + line_length ? link.end : line_begin + line_length) - line_begin;
						const scr_coord_val x1 = offset.x + display_calc_proportional_string_len_width((const char*)line_start, seg_begin);
						const scr_coord_val x2 = offset.x + display_calc_proportional_string_len_width((const char*)line_start, seg_end);
						link_rect_t rect;
						rect.rect = scr_rect(x1, offset.y + y, x2 - x1, LINESPACE);
						rect.link_index = link_index;
						link_rects.append(rect);
						display_fillbox_wh_clip_rgb(x1, offset.y + y + LINESPACE - 1, x2 - x1, 1, SYSCOL_TEXT_HIGHLIGHT, false);
					}
					link_index++;
				}
			}
		}
		y += LINESPACE;
		// back to start of new line
		line_start = word_start;
		x = word_x;
		word_x = 0;
	}

	// reset component height where necessary
	return scr_size(new_width, y);
}


bool gui_fixedwidth_textarea_t::infowin_event(const event_t *ev)
{
	if (IS_LEFTRELEASE(ev))
	{
		const scr_coord evpos(ev->click_pos.x, ev->click_pos.y);
		for (link_rect_t const &r : link_rects)
		{
			if (r.rect.contains(evpos))
			{
				uint32 link_index = 0;
				for (gui_text_link_t const &link : links)
				{
					if (link_index++ == r.link_index)
					{
						call_listeners((void const*)link.target);
						return true;
					}
				}
			}
		}
	}
	return false;
}


void gui_fixedwidth_textarea_t::draw(scr_coord offset)
{
	size = calc_display_text(offset + get_pos(), true);
}
