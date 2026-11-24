/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "font.h"

#include "../macros.h"
#include "../simdebug.h"
#include "../sys/simsys.h"
#include "../simtypes.h"
#include "../utils/simstring.h"

#ifdef USE_FREETYPE
#include "../dataobj/environment.h"
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>


// maximum dimensions of a BDF glyph, in pixels
#define BDF_GLYPH_HEIGHT (23)
#define BDF_GLYPH_WIDTH  (32)


font_t::glyph_t::glyph_t() :
	bitmap(NULL),
	height(0),
	width(0),
	advance(0xFF),
	top(0),
	left(0)
{
}


font_t::font_t() :
	linespace (0),
	descent(0)
{
	fname[0] = 0;
}


font_t::~font_t()
{
	release_glyphs();
}


font_t::font_t(const font_t &other) :
	linespace(other.linespace),
	descent(other.descent)
{
	tstrncpy( fname, other.fname, lengthof(fname) );
	glyphs.resize(other.glyphs.size());
	for(  size_t i = 0;  i < glyphs.size();  i++  ) {
		const glyph_t &src = other.glyphs[i];
		glyphs[i] = src;
		glyphs[i].bitmap = NULL;
		if(  src.bitmap  ) {
			const size_t size = (size_t)src.width * src.height;
			glyphs[i].bitmap = (uint8 *)malloc(size);
			memcpy( glyphs[i].bitmap, src.bitmap, size );
		}
	}
}


font_t& font_t::operator=(const font_t &other)
{
	if(  this != &other  ) {
		release_glyphs();
		linespace = other.linespace;
		descent   = other.descent;
		tstrncpy( fname, other.fname, lengthof(fname) );
		glyphs.resize(other.glyphs.size());
		for(  size_t i = 0;  i < glyphs.size();  i++  ) {
			const glyph_t &src = other.glyphs[i];
			glyphs[i] = src;
			glyphs[i].bitmap = NULL;
			if(  src.bitmap  ) {
				const size_t size = (size_t)src.width * src.height;
				glyphs[i].bitmap = (uint8 *)malloc(size);
				memcpy( glyphs[i].bitmap, src.bitmap, size );
			}
		}
	}
	return *this;
}


void font_t::release_glyphs()
{
	for(  size_t i = 0;  i < glyphs.size();  i++  ) {
		free( glyphs[i].bitmap );
	}
	glyphs.clear();
}


void font_t::alias_cent_glyph()
{
	// The legacy bitmap fonts draw a cent sign in the dollar glyph, but have no
	// glyph at U+00A2, which is the money symbol used by the interface.
	if(  glyphs.size() <= (uint32)'$'  ||  glyphs['$'].advance == 0xFF  ) {
		return;
	}
	if(  glyphs.size() <= 0xA2  ) {
		glyphs.resize(0xA3);
	}

	glyph_t &cent = glyphs[0xA2];
	free( cent.bitmap );
	cent = glyphs['$'];
	cent.bitmap = NULL;

	if(  glyphs['$'].bitmap  ) {
		const size_t size = (size_t)glyphs['$'].width * glyphs['$'].height;
		cent.bitmap = (uint8 *)malloc(size);
		memcpy( cent.bitmap, glyphs['$'].bitmap, size );
	}
}


/// Decodes a single line of a glyph into a 32 bit wide, 23 row temporary cell
static void dsp_decode_bdf_data_row(uint8 cell[BDF_GLYPH_HEIGHT][4], int y, int xoff, const char *str)
{
	size_t len = strlen(str);
	while(  len > 0  &&  (str[len-1] == '\n'  ||  str[len-1] == '\r')  ) {
		len--;
	}
	if(  len > 8  ) {
		len = 8;
	}

	uint32 data = 0;
	for(  size_t i = 0;  i < len;  i++  ) {
		const char c = str[i];
		data = (data << 4) | (uint32)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
	}

	// align the row so that its first pixel is the most significant bit
	const int bits = (int)len * 4;
	if(  bits < 32  ) {
		data <<= 32 - bits;
	}

	if(  xoff > 0  ) {
		data >>= xoff;
	}

	cell[y][0] = (uint8)(data >> 24);
	cell[y][1] = (uint8)(data >> 16);
	cell[y][2] = (uint8)(data >> 8);
	cell[y][3] = (uint8)data;
}


/// Reads a single glyph.
/// @returns index of glyph successfully read, or -1 on error
static sint32 dsp_read_bdf_glyph(FILE *bdf_file, std::vector<font_t::glyph_t> &data, int glyph_limit, int f_height, int f_desc)
{
	sint32 glyph_nr = 0;
	int g_width = 0, h = 0, g_desc = 0;
	int glyph_advance = -1;
	int xoff = 0;
	uint8 cell[BDF_GLYPH_HEIGHT][4];
	memset(cell, 0, sizeof(cell));

	while(  !feof(bdf_file)  ) {
		char str[256];

		if(  fgets(str, sizeof(str), bdf_file)==NULL  &&  !feof(bdf_file)  ) {
			return -1;
		}

		// encoding (sint8 number) in decimal
		if(  strstart(str, "ENCODING")  ) {
			glyph_nr = atoi(str + 8);
			if(  glyph_nr <= 0  ||  glyph_nr >= glyph_limit  ) {
				dbg->error("dsp_read_bdf_glyph", "Unexpected glyph (%i) for %i glyph font!\n", glyph_nr, glyph_limit);
				glyph_nr = 0;
			}
			memset(cell, 0, sizeof(cell));
			continue;
		}

		// information over size and coding
		if(  strstart(str, "BBX")  ) {
			sscanf(str + 3, "%d %d %d %d", &g_width, &h, &xoff, &g_desc);
			continue;
		}

		// information over size and coding
		if(  strstart(str, "DWIDTH")  ) {
			glyph_advance = atoi(str + 6);
			continue;
		}

		// start if bitmap data
		if (strstart(str, "BITMAP")) {
			const int top = f_height + f_desc - h - g_desc;

			// maximum size BDF_GLYPH_HEIGHT pixels
			h = min(h + top, BDF_GLYPH_HEIGHT);

			// read for height times
			for (int y = top; y < h; y++) {
				if(  fgets(str, sizeof(str), bdf_file)==NULL  &&  !feof(bdf_file)  ) {
					return -1;
				}

				if(  y>=0  ) {
					dsp_decode_bdf_data_row(cell, y, xoff, str);
				}
			}
			continue;
		}

		// finally add width information (width = 0: not there!)
		if(  strstart(str, "ENDCHAR")  ) {
			int start_h = 0;

			// find the start offset
			while(  start_h < BDF_GLYPH_HEIGHT  &&  cell[start_h][0]==0  &&  cell[start_h][1]==0  &&  cell[start_h][2]==0  &&  cell[start_h][3]==0  ) {
				start_h++;
			}

			int end_h = BDF_GLYPH_HEIGHT;
			while(  end_h > start_h  &&  cell[end_h-1][0]==0  &&  cell[end_h-1][1]==0  &&  cell[end_h-1][2]==0  &&  cell[end_h-1][3]==0  ) {
				end_h--;
			}

			font_t::glyph_t &glyph = data[glyph_nr];

			glyph.top    = end_h > start_h ? start_h : 0;
			glyph.left   = 0;
			glyph.width  = end_h > start_h ? min(g_width + xoff, BDF_GLYPH_WIDTH) : 0;
			glyph.height = end_h - start_h;
			if(  glyph_advance == -1  ) {
				// no screen width: should not happen, but we can recover
				dbg->warning( "dsp_read_bdf_glyph", "BDF warning: Glyph %i has no advance (screen width) assigned!\n", glyph_nr);
				glyph_advance = g_width + 1;
			}

			glyph.advance = (uint8)clamp(glyph_advance, 1, 255);

			if(  glyph.width > 0  &&  glyph.height > 0  ) {
				free( glyph.bitmap );
				glyph.bitmap = (uint8 *)calloc((size_t)glyph.width * glyph.height, 1);
				for(  int y = start_h;  y < end_h;  y++  ) {
					for(  int x = 0;  x < glyph.width;  x++  ) {
						if(  cell[y][x/8] & (0x80 >> (x%8))  ) {
							glyph.bitmap[(y-start_h)*glyph.width + x] = 32;
						}
					}
				}
			}
			else {
				free( glyph.bitmap );
				glyph.bitmap = NULL;
			}

			// finished
			return glyph_nr;
		}
	}

	return -1;
}


bool font_t::load_from_bdf(FILE *bdf_file)
{
	dbg->message("font_t::load_from_bdf", "Loading BDF font '%s'", fname);
	release_glyphs();

	uint32 f_height = 0;
	int f_desc = 0;
	int f_numglyphs = 0;
	sint32 max_glyph = 0;

	while(  !feof(bdf_file)  ) {
		char str[256];

		if(  fgets(str, sizeof(str), bdf_file)==NULL  &&  !feof(bdf_file)  ) {
			return false;
		}

		if(  strstart(str, "FONTBOUNDINGBOX")  ) {
			sscanf(str + 15, "%*d %d %*d %d", &f_height, &f_desc);
			continue;
		}

		if(  strstart(str, "CHARS")  &&  str[5] <= ' '  ) {
			// the characters 0xFFFF and 0xFFFE are guaranteed to be non-unicode characters
			f_numglyphs = atoi(str + 5) <= 0x100 ? 0x100 : 0xFFFE;

			glyphs.resize(max(f_numglyphs, 0));
			glyphs[(uint32)' '].advance = clamp(f_height / 2, 3u, 255u);
			continue;
		}

		if(  strstart(str, "STARTCHAR")  &&  f_numglyphs > 0  ) {
			const sint32 glyph = dsp_read_bdf_glyph(bdf_file, glyphs, f_numglyphs, f_height, f_desc);
			max_glyph = max(max_glyph, glyph);
			continue;
		}
	}

	// ok, was successful?
	if(  f_numglyphs <= 0  ) {
		return false;
	}

	// init default glyph for missing glyphs (just a box)

	const int real_font_height = min((int)f_height, BDF_GLYPH_HEIGHT);
	const int box_height = max(real_font_height - 2, 3);

	glyphs[0].width   = 7;
	glyphs[0].height  = box_height;
	glyphs[0].top     = 1;
	glyphs[0].left    = 0;
	glyphs[0].advance = 8;
	glyphs[0].bitmap  = (uint8 *)calloc(7 * box_height, 1);

	for(  int y = 0;  y < box_height;  y++  ) {
		for(  int x = 0;  x < 7;  x++  ) {
			if(  y == 0  ||  y == box_height-1  ||  x == 0  ||  x == 6  ) {
				glyphs[0].bitmap[y*7 + x] = 32;
			}
		}
	}

	linespace = f_height;
	descent   = f_desc;

	// Use only needed amount
	glyphs.resize( (uint32)max_glyph+1 );

	alias_cent_glyph();

	return true;
}


#ifdef USE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_TRUETYPE_TABLES_H
#include FT_BITMAP_H


bool font_t::load_from_freetype(const char *fname, int pixel_height)
{
	dbg->message( "font_t::load_from_freetype", "trying to load '%s' in size %d", fname, pixel_height);

	FT_Library ft_library = NULL;
	if(  FT_Init_FreeType(&ft_library) != FT_Err_Ok  ) {
		dbg->error( "font_t::load_from_freetype", "Freetype initialization failed" );
		return false;
	}

	// Ok, we guessed something about the filename, now actually load it
	FT_Face face;

	if(  FT_New_Face( ft_library, fname, 0, &face ) != FT_Err_Ok  ) {
		dbg->error( "font_t::load_from_freetype", "Cannot load %s", fname );
		FT_Done_FreeType( ft_library );
		return false;
	}

	if(  FT_Set_Pixel_Sizes( face, 0, pixel_height ) != FT_Err_Ok  ) {
		dbg->error( "font_t::load_from_freetype", "Cannot set pixel size %d for %s", pixel_height, fname);
		FT_Done_Face(face);
		FT_Done_FreeType(ft_library);
		return false;
	}

	release_glyphs();
	glyphs.resize(0x10000);

	const sint16 ascent = face->size->metrics.ascender/64;
	linespace           = face->size->metrics.height/64;
	descent             = face->size->metrics.descender/64;

	tstrncpy( this->fname, fname, lengthof(this->fname) );

	uint32 num_glyphs = 0;

	// 8 bit bitmap used for converting glyphs that are not rendered in greyscale
	FT_Bitmap bitmap_8;
	// FreeType 2.10 renamed FT_Bitmap_New to FT_Bitmap_Init. The old name
	// remains available as an alias, so it also compiles against the older
	// headers shipped with some Linux distributions and macOS SDKs.
	FT_Bitmap_New(&bitmap_8);

	for(  uint32 glyph_nr=0;  glyph_nr<0xFFFF;  glyph_nr++  ) {

		if(  glyph_nr > 0  &&  FT_Get_Char_Index( face, glyph_nr ) == 0  ) {
			// glyph not in font => nothing to render
			glyphs[glyph_nr].advance = 0xFF;
			continue;
		}

		/* load glyph image into the slot (erase previous one) */
		if(  FT_Load_Char( face, glyph_nr, FT_LOAD_RENDER ) != FT_Err_Ok  ) {
			// glyph not there ...
			glyphs[glyph_nr].advance = 0xFF;
			continue;
		}

		const FT_Error error = FT_Render_Glyph( face->glyph, FT_RENDER_MODE_NORMAL );
		if(  error != FT_Err_Ok  ) {
			// glyph not there ...
			glyphs[glyph_nr].advance = 0xFF;
			continue;
		}

		// use only needed amount
		num_glyphs = glyph_nr+1;

		glyph_t &glyph = glyphs[glyph_nr];

		FT_Bitmap *bitmap = &face->glyph->bitmap;
		bool one_bit_pp = false;

		// some fonts (e.g. bitmap fonts) do not render with 8 bit alpha
		if(  bitmap->pixel_mode != FT_PIXEL_MODE_GRAY  ) {
			if(  FT_Bitmap_Convert( ft_library, &face->glyph->bitmap, &bitmap_8, 4 ) != FT_Err_Ok  ) {
				glyph.advance = 0xFF;
				continue;
			}
			bitmap = &bitmap_8;
			one_bit_pp = true;
		}

		// set glyph size
		glyph.height  = bitmap->rows;
		glyph.width   = bitmap->width;
		glyph.advance = (uint8)min(255, (face->glyph->advance.x + 31) / 64);

		// the bitmaps are all top aligned. Bitmap top is the ascent
		// above the base line
		// to find the real top position, we must take the font ascent
		// and reduce it by the glyph ascent
		glyph.top  = ascent - face->glyph->bitmap_top - 1;
		glyph.left = face->glyph->bitmap_left;

		// transform glyph to Simutrans bitmap
		if(  glyph.height > 0  &&  glyph.width > 0  ) {
			glyph.bitmap = (uint8 *)calloc((size_t)glyph.height * glyph.width, 1);

			if(  !one_bit_pp  ) {
				// 8 bit alpha per pixel
				for(  int y = 0;  y < glyph.height;  y++  ) {
					for(  int x = 0;  x < glyph.width;  x++  ) {
						const uint16 alpha = bitmap->buffer[y * bitmap->pitch + x];
						// the text blitter wants alpha in 0..32
						glyph.bitmap[y * glyph.width + x] = (uint8)((alpha * 32) / 255);
					}
				}
			}
			else {
				// 1 bit per pixel
				for(  int y = 0;  y < glyph.height;  y++  ) {
					for(  int x = 0;  x < glyph.width;  x++  ) {
						if(  bitmap->buffer[y * bitmap->pitch + x]  ) {
							glyph.bitmap[y * glyph.width + x] = 32;
						}
					}
				}
			}
		}

		FT_Bitmap_Done( ft_library, &bitmap_8 );

		// find out if invalid character
		if(  glyph_nr  &&  glyph.height>0  &&  glyphs[0].height == glyph.height  &&  glyphs[0].width == glyph.width  &&  memcmp(glyphs[0].bitmap, glyph.bitmap, glyph.height * glyph.width) == 0  ) {
			// same bitmap as character zero ?!?!
			glyph.advance = 0xFF;
		}
	}

	if(  num_glyphs<0x80  ) {
		FT_Done_Face( face );
		FT_Done_FreeType( ft_library );
		return false;
	}

	// hack for not rendered full width space
	if(  glyphs[0x3000].advance == 0xFF  &&  glyphs[0x3001].advance != 0xFF  ) {
		glyphs[0x3000].advance = glyphs[0x3001].advance;
		glyphs[0x3000].width = 0;
		glyphs[0x3000].top = 0;
	}

	if(  glyphs[' '].advance == 0xFF  ) {
		glyphs[' '].advance = glyphs['n'].advance;
	}

	// Use only needed amount
	glyphs.resize(num_glyphs);

	FT_Done_Face( face );
	FT_Done_FreeType( ft_library );
	return true;
}

#endif


void font_t::print_debug() const
{
	dbg->debug("font_t::print_debug", "Loaded font %s with %i glyphs\n", get_fname(), get_num_glyphs());
	dbg->debug("font_t::print_debug", "height: %i, descent: %i", linespace, descent );

	for(  uint32 glyph_nr = ' ';  glyph_nr < 128  &&  glyph_nr < get_num_glyphs();  glyph_nr++  ) {
		const glyph_t &glyph = glyphs[glyph_nr];
		dbg->debug("font_t::print_debug", "glyph %c: width %i, height %i, top %i, left %i, advance %i", glyph_nr, glyph.width, glyph.height, glyph.top, glyph.left, glyph.advance );
	}
}


bool font_t::load_from_file(const char *srcfilename)
{
	tstrncpy( fname, srcfilename, lengthof(fname) );

	FILE *fontfile = dr_fopen(fname, "rb");

	if(  fontfile == NULL  ) {
		dbg->error("font_t::load_from_file", "Cannot open '%s'", fname);
		return false;
	}

	const int res = fgetc(fontfile);
	if(  res==EOF  ) {
		dbg->error("font_t::load_from_file", "Cannot parse font '%s'", fname);
		fclose(fontfile);
		return false;
	}

	const uint8 c = res & 0xFF;

	// binary => the assume dumped prop file
	if(  c < ' '  &&  strstr(fname, ".fnt")  ) {
		rewind(fontfile);
		return load_from_fnt(fontfile);
	}

	bool ok = load_from_bdf(fontfile);
	fclose(fontfile);

#ifdef USE_FREETYPE
	if(  !ok  ) {
		ok = load_from_freetype( fname, env_t::fontsize );
	}
#endif

#if MSG_LEVEL>=4
	if(  ok  ) {
		print_debug();
	}
#endif
	return ok;
}


bool font_t::load_from_fnt(FILE *f)
{
	assert(ftell(f) == 0);

	// read classical prop font
	dbg->message("font_t::load_from_fnt", "Loading font '%s'", fname);

	uint8 npr_fonttab[3072];

	if(  fread(npr_fonttab, sizeof(npr_fonttab), 1, f) != 1  ) {
		dbg->error( "font_t::load_from_fnt" "%s wrong size for old format prop font!", fname );
		fclose(f);
		return false;
	}

	fclose(f);

	// convert to new standard font
	release_glyphs();
	glyphs.resize(0x100);

	linespace = 11;
	descent   = -2;

	for(  int i = 0; i < 0x100; i++  ) {
		glyphs[i].advance = npr_fonttab[0x100 + i];
		if(  glyphs[i].advance == 0  ) {
			glyphs[i].advance = 0xFF; // undefined glyph
		}

		glyphs[i].top    = 0;
		glyphs[i].left   = 0;
		glyphs[i].height = 10;
		glyphs[i].width  = npr_fonttab[i];
		if(  glyphs[i].width == 0  ) {
			glyphs[i].height = 0;
		}
		glyphs[i].bitmap = glyphs[i].width > 0 ? (uint8 *)calloc((size_t)glyphs[i].width * glyphs[i].height, 1) : NULL;

		// expand the 1 bit per pixel rows of the old font to 8 bit alpha
		for(  int y = 0;  y < 10;  y++  ) {
			const uint8 row = npr_fonttab[512 + i * 10 + y];
			for(  int x = 0;  x < glyphs[i].width  &&  x < 8;  x++  ) {
				if(  row & (0x80 >> x)  ) {
					glyphs[i].bitmap[y * glyphs[i].width + x] = 32;
				}
			}
		}
	}

	glyphs[(uint32)' '].advance = 4;
	glyphs[(uint32)' '].width = 0;
	glyphs[(uint32)' '].height = 0;
	free( glyphs[(uint32)' '].bitmap );
	glyphs[(uint32)' '].bitmap = NULL;

	alias_cent_glyph();

	dbg->message( "font_t::load_from_fnt", "%s successfully loaded as old format prop font!", fname);
	return true;
}


uint8 font_t::get_glyph_advance(utf32 c) const
{
	if(  !is_loaded()  ) {
		return 0;
	}
	else if(  c >= get_num_glyphs()  ||  glyphs[c].advance == 0xFF  ) {
		return glyphs[0].advance;
	}

	return glyphs[c].advance;
}


const font_t::glyph_t& font_t::get_glyph(utf32 c) const
{
	static glyph_t dummy;
	if(  is_loaded()  ) {
		if(  c < get_num_glyphs()  &&  glyphs[c].advance != 0xFF  ) {
			return glyphs[c];
		}
		else {
			return glyphs[0];
		}
	}
	return dummy;
}
