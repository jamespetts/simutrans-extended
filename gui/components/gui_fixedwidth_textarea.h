/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_FIXEDWIDTH_TEXTAREA_H
#define GUI_COMPONENTS_GUI_FIXEDWIDTH_TEXTAREA_H


#include "gui_component.h"
#include "gui_container.h"
#include "gui_action_creator.h"
#include "../../display/simgraph.h"
#include "../../utils/cbuffer_t.h"
#include "../../tpl/slist_tpl.h"

class cbuffer_t;

/**
 * One hyperlink inside a text buffer, as a half-open byte range of the plain
 * text with the markup removed.
 */
struct gui_text_link_t
{
	/// Offset of the first byte of the link text in the plain text.
	size_t start;
	/// Offset one past the last byte of the link text in the plain text.
	size_t end;
	/// The href target.
	cbuffer_t target;
};

/**
 * Removes <a href="...">...</a> markup from @p text.
 * @param plain receives the text with the link markup removed.
 * @param links receives one entry per link, with byte offsets into @p plain.
 * @return false when @p text contains no link markup. In that case @p plain
 * and @p links are left empty and the caller must keep using @p text itself,
 * so that a buffer without markup is laid out exactly as before.
 */
bool parse_text_links(const char *text, cbuffer_t &plain, slist_tpl<gui_text_link_t> &links);

/**
 * A fixed-width, automatically line-wrapping text-area,
 * optionally with a reserved area in the upper right corner.
 * It does *not* add 10px margins from the top and the left.
 * Core code borrowed from ding_infowin_t::calc_draw_info() with adaptation.
 *
 * A buffer containing <a href="...">...</a> links is displayed as its plain
 * text with the linked parts underlined; a left release over a link calls the
 * component's listeners with the target string as the value.
 */
class gui_fixedwidth_textarea_t : public gui_component_t, public gui_action_creator_t
{
private:
	/// One drawn rectangle of a link. A link spanning several lines has one
	/// rectangle per line.
	struct link_rect_t
	{
		scr_rect rect;
		uint32 link_index;
	};

	/// Pointer to the text to be displayed. The text is *not* copied.
	cbuffer_t* buf;

	/// The text with link markup removed, valid when plain_text_used is set.
	mutable cbuffer_t plain;
	/// The links found in buf, with offsets into plain.
	mutable slist_tpl<gui_text_link_t> links;
	/// Whether plain and links describe the current buf.
	mutable bool plain_text_used;

	/// The clickable rectangles recorded by the last draw.
	mutable slist_tpl<link_rect_t> link_rects;

	/// Reserved area in the upper right corner
	scr_size reserved_area;

	/**
	 * For calculating text height and/or displaying the text.
	 */
	scr_size calc_display_text(const scr_coord offset, const bool draw) const;

public:
	gui_fixedwidth_textarea_t(cbuffer_t* buf, const sint16 width);

	// call this after buffer size changes
	void recalc_size();

	// this sets a maximum width. The actual width may be smaller, depending on the text
	void set_width(scr_coord_val width) OVERRIDE;

	void set_reserved_area(const scr_size area);

	void draw(scr_coord offset) OVERRIDE;

	bool infowin_event(const event_t *ev) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;
};

#endif
