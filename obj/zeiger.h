/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef OBJ_ZEIGER_H
#define OBJ_ZEIGER_H


#include "simobj.h"
#include "../simtypes.h"
#include "../display/simimg.h"

/** @file zeiger.h object to mark tiles */

/**
 * These objects mark tiles for tools. It marks the current tile
 * pointed to by mouse pointer (and possibly an area around it).
 */
class zeiger_t : public obj_no_info_t
{
private:
	koord area, offset;
	/// images
	image_id image, foreground_image;

public:
	zeiger_t(loadsave_t *file);
	zeiger_t(koord3d pos, player_t *player);

	void change_pos(koord3d k);

	const char *get_name() const OVERRIDE {return "Zeiger";}
#ifdef INLINE_OBJ_TYPE
#else
	typ get_typ() const OVERRIDE { return zeiger; }
#endif

	/**
	 * Set area to be marked around cursor
	 * @param area size of marked area
	 * @param center true if cursor is centered within marked area
	 * @param offset if center==false then cursor is at position @p offset
	 */
	void set_area( koord area, bool center, koord offset = koord(0,0) );

	/// set back image
	void set_image( image_id b );
	/// get back image
	image_id get_image() const OVERRIDE {return image;}

	/// set front image
	void set_after_image( image_id b );
	/// get front image
	image_id get_front_image() const OVERRIDE {return foreground_image;}

	bool has_managed_lifecycle() const OVERRIDE;
};

class road_preview_t : public zeiger_t
{
	uint8 ribi_unmasked;
	uint8 dir;
	bool is_diagonal;
	sint8 slope;
	FLAGGED_PIXVAL outline_colour;

public:
	road_preview_t(koord3d pos, player_t* player, uint8 ribi_unmasked, uint8 dir, bool diagonal, sint8 slope, FLAGGED_PIXVAL outline_colour=0);

	void display_overlay(int xpos, int ypos) const OVERRIDE;

	FLAGGED_PIXVAL get_outline_colour() const OVERRIDE { return outline_colour; }
};

#include "../gui/components/gui_schedule_item.h"
class schedule_marker_t : public obj_no_info_t
{
private:
	uint8 num=0;    // number
	uint8 style= gui_schedule_entry_number_t::halt;  // marker style = number_style
	PIXVAL color= color_idx_to_rgb(COL_GREY2); // line color, but ignored for depot
	waytype_t wt;

	bool is_selected  = false;
	bool reverse_here = false;
	bool is_mirrored  = false;

public:
	schedule_marker_t(koord3d pos, player_t *player, waytype_t wt_);

	// set the necessary data in the marker from schedule entry
	void set_entry_data(uint8 schedule_number, uint8 number_style, bool mirrored = false, bool reverse_here_ = false) {
		num = schedule_number;
		style = number_style;
		is_mirrored = mirrored;
		reverse_here = reverse_here_;
	}
	void set_color(PIXVAL c) { color = c; }

	obj_t::typ get_typ() const { return schedule_marker; }

	image_id get_image() const OVERRIDE { return IMG_EMPTY; };

	//void change_pos(koord3d k);

	// higilight this marker
	void set_selected(bool yesno) { is_selected = yesno; }

	void display_overlay(int xpos, int ypos) const OVERRIDE;
};

#endif
