/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_TOOLTIP_H
#define GUI_TOOLTIP_H


#include "../simtypes.h"
#include "../display/simimg.h"
#include "../display/scr_coord.h"
#include "../tpl/vector_tpl.h"
#include "../utils/cbuffer_t.h"


/**
 * Symbols shown at the start of a tooltip line.
 * The renderer resolves each symbol to a theme image. If the theme has no
 * image for the symbol, the line is drawn without a symbol.
 */
enum tooltip_symbol_t
{
	TOOLTIP_SYMBOL_NONE,
	TOOLTIP_SYMBOL_COST,
	TOOLTIP_SYMBOL_FORGE,
	TOOLTIP_SYMBOL_MAINTENANCE,
	TOOLTIP_SYMBOL_SPEED,
	TOOLTIP_SYMBOL_WEIGHT,
	TOOLTIP_SYMBOL_DURABILITY,
	TOOLTIP_SYMBOL_LAND,
	TOOLTIP_SYMBOL_TREES,
	TOOLTIP_SYMBOL_STRUCTURES,
	TOOLTIP_SYMBOL_DISTANCE,
	TOOLTIP_SYMBOL_TIME,
	TOOLTIP_SYMBOL_PASSENGERS,
	TOOLTIP_SYMBOL_MAIL,
	TOOLTIP_SYMBOL_GOODS,
	TOOLTIP_SYMBOL_POWER,
	TOOLTIP_SYMBOL_RESTRICTION,
	TOOLTIP_SYMBOL_TOTAL
};


struct tooltip_line_t
{
	tooltip_line_t() : symbol(TOOLTIP_SYMBOL_NONE), custom_icon(IMG_EMPTY), indent(0), title(false), total(false) {}
	tooltip_line_t(tooltip_symbol_t symbol_, image_id custom_icon_, const char *text_) :
		symbol(symbol_),
		custom_icon(custom_icon_),
		text(text_),
		indent(0),
		title(false),
		total(false)
	{}

	tooltip_symbol_t symbol;
	/// Used instead of the symbol image when not IMG_EMPTY.
	image_id custom_icon;
	cbuffer_t text;
	/// Indentation level; the renderer multiplies this by the indent width.
	uint8 indent;
	/// Draws the line as a title with a separator below it.
	bool title;
	/// Draws a separator above the line, for a total that follows its components.
	bool total;
};


/**
 * Ordered lines of text shown in a speech bubble.
 * The text of each line is already translated and formatted.
 */
class tooltip_t
{
private:
	vector_tpl<tooltip_line_t> lines;

	static sint32 get_max_width();
	static sint32 get_icon_width(const tooltip_line_t &line);
	static sint32 count_wrapped_lines(const char *text, sint32 max_width);

public:
	void clear() { lines.clear(); }
	bool empty() const { return lines.empty(); }

	tooltip_line_t &add(tooltip_symbol_t symbol, const char *text);
	tooltip_line_t &add_icon(image_id icon, const char *text);
	tooltip_line_t &add_value(tooltip_symbol_t symbol, const char *label, sint64 value, const char *unit);
	tooltip_line_t &add_money(tooltip_symbol_t symbol, const char *label, sint64 simucents, const char *unit);

	/**
	 * Draws the bubble at @p pos, clamped to the screen. The tail points at
	 * @p anchor. If @p anchor lies inside the clamped bubble, no tail is drawn.
	 */
	void draw(scr_coord pos, scr_coord anchor, bool dirty) const;
};


#endif
