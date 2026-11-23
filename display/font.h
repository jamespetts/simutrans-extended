/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DISPLAY_FONT_H
#define DISPLAY_FONT_H


#include "../simtypes.h"
#include "../unicode.h"

#include <vector>


/**
 * Terminology:
 *  - glyph:        display data of a single character
 *  - font:         a collection of glyphs (usually called a font face)
 *  - width,height: size of the glyph
 *  - advance:      number of pixels between the start of a glyph and the next glyph
 *                  (not necessarily equal to width)
 */
class font_t
{
public:
	/// glyph data, with each bitmap stored separately
	struct glyph_t
	{
		glyph_t();

		uint8 *bitmap; ///< 8 bit alpha per pixel, 32 = opaque
		sint16 height;
		sint16 width;
		uint8 advance;
		sint16 top;
		sint16 left;
	};

public:
	font_t();
	~font_t();

	font_t(const font_t&);
	font_t& operator=(const font_t&);

public:
	/// @returns true on success
	bool load_from_file(const char *fname);
	bool is_loaded() const { return !glyphs.empty(); }

	const char *get_fname() const { return fname; }
	sint16 get_linespace() const { return linespace; }
	sint16 get_descent() const { return descent; } ///< Note: Because this value is in grid coordinates, it is NEGATIVE.
	sint16 get_ascent() const { return linespace + descent; }

	/// @returns true if this is a valid (defined) glyph
	bool is_valid_glyph(utf32 c) const
	{ return is_loaded() && c < get_num_glyphs() && glyphs[c].advance != 0xFF; }

	/// @returns size in pixels between the start of this glyph and the next glyph
	uint8 get_glyph_advance(utf32 c) const;

	/// @returns glyph data of a character
	const glyph_t& get_glyph(utf32 c) const;

private:
	/// Load a BDF font
	bool load_from_bdf(FILE *fin);

	/// Load a .fnt file
	bool load_from_fnt(FILE *fin);

#ifdef USE_FREETYPE
	/// Load a freetype font
	bool load_from_freetype(const char *fname, int pixel_height);
#endif

	/// Frees all glyph bitmaps and empties the glyph array
	void release_glyphs();

	/// Gives the cent code point the dollar glyph artwork in the legacy bitmap fonts
	void alias_cent_glyph();

	void print_debug() const;

	uint32 get_num_glyphs() const { return glyphs.size(); }

private:
	char fname[PATH_MAX];
	sint16 linespace;
	sint16 descent;
public:	// for simgraph has_character()
	std::vector<glyph_t> glyphs;
};

#endif
