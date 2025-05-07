/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

/** @file zeiger.cc object to mark tiles */

#include <stdio.h>

#include "../simworld.h"
#include "simobj.h"
#include "../simhalt.h"
#include "../boden/grund.h"
#include "../boden/wege/strasse.h"
#include "../dataobj/environment.h"
#include "zeiger.h"

#ifdef INLINE_OBJ_TYPE
zeiger_t::zeiger_t(loadsave_t *file) : obj_no_info_t(obj_t::zeiger)
#else
zeiger_t::zeiger_t(loadsave_t *file) : obj_no_info_t()
#endif
{
	image = IMG_EMPTY;
	foreground_image = IMG_EMPTY;
	area = koord(0,0);
	offset = koord(0,0);
	rdwr(file);
}


zeiger_t::zeiger_t(koord3d pos, player_t *player) :
#ifdef INLINE_OBJ_TYPE
	obj_no_info_t(obj_t::zeiger, pos)
#else
	obj_no_info_t(pos)
#endif
{
	set_owner( player );
	image = IMG_EMPTY;
	foreground_image = IMG_EMPTY;
	area = koord(0,0);
	offset = koord(0,0);
}


/**
 * We want to be able to highlight the current tile.
 * Unmarks area around old and marks area around new position.
 * Use this routine to change position.
 */
void zeiger_t::change_pos(koord3d k )
{
	if(  k != get_pos()  ) {
		// remove from old position
		// and clear mark
		grund_t *gr = welt->lookup( get_pos() );
		if(gr==NULL) {
			gr = welt->lookup_kartenboden( get_pos().get_2d() );
		}
		if(gr) {
			if(  gr->get_halt().is_bound()  ) {
				gr->get_halt()->mark_unmark_coverage( false );
			}
			welt->mark_area( get_pos()- offset, area, false );
		}
		if(  get_pos().x >= welt->get_size().x-1  ||  get_pos().y >= welt->get_size().y-1  ) {
			// the raise and lower tool actually can go to size!
			welt->set_background_dirty();
			// this removes crap form large cursors overlapping into the nirvana
		}
		mark_image_dirty( get_image(), 0 );
		mark_image_dirty( get_front_image(), 0 );
		set_flag( obj_t::dirty );

		obj_t::set_pos( k );
		if(  get_yoff() == Z_PLAN  ) {
			gr = welt->lookup( k );
			if(  gr == NULL  ) {
				gr = welt->lookup_kartenboden( k.get_2d() );
			}
			if(gr) {
				if(  gr->get_halt().is_bound()  &&  env_t::station_coverage_show  ) {
					if (gr->get_halt()->get_pax_enabled() || gr->get_halt()->get_mail_enabled()) {
						gr->get_halt()->mark_unmark_coverage( true );
					}
					else if(gr->get_halt()->get_ware_enabled()) {
						gr->get_halt()->mark_unmark_coverage(true, true);
					}
				}
				welt->mark_area( k-offset, area, true );
			}
		}
	}
}


void zeiger_t::set_image( image_id b )
{
	// mark dirty
	mark_image_dirty( image, 0 );
	mark_image_dirty( b, 0 );
	image = b;
}

void zeiger_t::set_after_image( image_id b )
{
	// mark dirty
	mark_image_dirty( foreground_image, 0 );
	mark_image_dirty( b, 0 );
	foreground_image = b;
}


void zeiger_t::set_area(koord new_area, bool center, koord new_offset)
{
	if (center) {
		new_offset = new_area/2;
	}

	if(new_area==area  &&  new_offset==offset) {
		return;
	}
	welt->mark_area( get_pos()-offset, area, false );
	area = new_area;
	offset = new_offset;
	welt->mark_area( get_pos()-offset, area, true );
}

bool zeiger_t::has_managed_lifecycle() const {
	return true;
}

road_preview_t::road_preview_t(koord3d pos, player_t* player, uint8 ribi_unmasked, uint8 dir, bool diagonal, sint8 slope, FLAGGED_PIXVAL outline_colour)
	: zeiger_t(pos, player)
{
	this->ribi_unmasked = ribi_unmasked;
	this->dir = dir;
	this->is_diagonal = diagonal;
	this->slope= slope;
	this->outline_colour = outline_colour;
}

void road_preview_t::display_overlay(int xpos, int ypos) const
{
	const int raster_width = get_current_tile_raster_width();
	display_signal_direction_rgb(xpos + ((raster_width * 5) >> 3), ypos + ((raster_width * 5) >> 3), get_current_tile_raster_width(), ribi_unmasked, dir, 253, is_diagonal, ribi_t::all, slope);
}


schedule_marker_t::schedule_marker_t(koord3d pos, player_t *player, waytype_t wt_) :
	obj_no_info_t(obj_t::schedule_marker, pos)
{
	wt = wt_;
	set_owner(player);
}

void schedule_marker_t::display_overlay(int xpos, int ypos) const
{
	char buf[4];
	sprintf(buf, "%i", num+1);
	const PIXVAL base_color = (style==gui_schedule_entry_number_t::depot) ? color_idx_to_rgb(91) : color;
	// If the base color is too bright, use a black or darkened text color
	const sint8 brightness_factor = get_color_brightness_index(base_color);
	const PIXVAL text_color = (style == gui_schedule_entry_number_t::depot) ? color_idx_to_rgb(COL_WHITE) :
		style==gui_schedule_entry_number_t::waypoint ? (brightness_factor<14 ? color_idx_to_rgb(COL_WHITE) : color_idx_to_rgb(COL_BLACK)) :
		(brightness_factor<28 ? color : display_blend_colors(base_color, color_idx_to_rgb(COL_BLACK), (brightness_factor-28)>>1));
	const scr_coord_val width_half = max(LINEASCENT, proportional_string_width(buf)+D_H_SPACE*2+4)>>1;
	const sint16 raster_tile_width = get_tile_raster_width();
	const scr_coord_val leg_height = max(raster_tile_width>>4, 9);
	const scr_coord_val box_height = LINEASCENT+8+is_mirrored*2;

	xpos += raster_tile_width>>1; // tile center
	ypos += (raster_tile_width*5)>>3;
	if( weg_t *way = welt->lookup(get_pos())->get_weg(wt) ) {
		const uint8 way_ribi = way->get_ribi_unmasked();
		// adjust offset for diagonal way
		if (way_ribi == ribi_t::southwest) {
			xpos -= raster_tile_width>>2;
		}
		if (way_ribi == ribi_t::northeast) {
			xpos += raster_tile_width>>2;
		}
	}

	if (is_selected) {
		// highlight
		ypos -= (leg_height+box_height-1);
		display_blend_wh_rgb(xpos-width_half,   ypos-2,            width_half*2+1, 1, color_idx_to_rgb(COL_YELLOW), 40);
		display_blend_wh_rgb(xpos-width_half,   ypos+box_height+1, width_half*2+1, 1, color_idx_to_rgb(COL_YELLOW), 40);
		display_blend_wh_rgb(xpos-width_half-2, ypos, 1, box_height, color_idx_to_rgb(COL_YELLOW), 40);
		display_blend_wh_rgb(xpos+width_half+2, ypos, 1, box_height, color_idx_to_rgb(COL_YELLOW), 40);
		display_blend_wh_rgb(xpos-width_half-1, ypos-1, width_half*2+3, box_height+2, color_idx_to_rgb(COL_YELLOW), 60);
		ypos += (leg_height+box_height-1);
	}

	// draw base
	for( uint8 i=0; i<leg_height; i++ ) {
		display_fillbox_wh_clip_rgb(xpos-i/3, ypos-i, (i/3)*2+1, 1, base_color, false);
	}
	ypos -= (leg_height + box_height - 1);
	display_filled_roundbox_clip(xpos-width_half, ypos, width_half*2+1, box_height, base_color, false);
	if (style == gui_schedule_entry_number_t::halt) {
		display_filled_roundbox_clip(xpos-width_half+2, ypos+2, width_half*2-3, box_height-4, is_selected ? color_idx_to_rgb(COL_YELLOW) : color_idx_to_rgb(COL_WHITE), false);
	}

	mark_rect_dirty_wc(xpos-width_half-2, ypos-2-reverse_here*(LINEASCENT+1), xpos+width_half+2, ypos+box_height+1);

	// text
	if (reverse_here) {
		display_shadow_proportional_rgb(xpos-proportional_string_width("<"), ypos-LINEASCENT-1, color_idx_to_rgb(COL_WHITE), color_idx_to_rgb(COL_BLACK), "<<", false);
	}
	if (is_mirrored) {
		display_fillbox_wh_clip_rgb(xpos-width_half+4, ypos+LINEASCENT+4, width_half*2-7, 1, text_color, false);
	}
	xpos -= proportional_string_width(buf)>>1;
	display_proportional_rgb(xpos+1, ypos+3, buf, ALIGN_TOP, text_color, false);
}
