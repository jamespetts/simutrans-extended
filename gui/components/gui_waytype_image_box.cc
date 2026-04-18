/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "gui_waytype_image_box.h"

#include "../../simskin.h"

#include "../gui_theme.h"


gui_waytype_image_box_t::gui_waytype_image_box_t(waytype_t wt, bool yesno) :
	gui_image_t(IMG_EMPTY, 0, 0, true)
{
	flexible = yesno;
	set_waytype(wt);
}


void gui_waytype_image_box_t::set_waytype(waytype_t wt)
{
	if (wt!=invalid_wt && skinverwaltung_t::get_waytype_skin(wt)) {
		set_image(skinverwaltung_t::get_waytype_skin(wt)->get_image_id(0), true);
	}
	else {
		set_image(IMG_EMPTY);
	}
	bgcol = world()->get_settings().get_waytype_color(wt);
	set_size(scr_size( gui_image_t::get_size().w, gui_image_t::get_size().h) );
}

void gui_waytype_image_box_t::draw(scr_coord offset)
{
	if( bgcol!=0 ) {
		display_filled_roundbox_clip(pos.x+offset.x,   pos.y+offset.y,   size.w,   size.h, SYSCOL_SHADOW, false);
		display_filled_roundbox_clip(pos.x+offset.x+1, pos.y+offset.y+1, size.w-2, size.h-2, bgcol, false);
	}
	gui_image_t::draw(offset);
}


void gui_waytype_image_box_t::set_size( scr_size size_par )
{
	if( id  !=  IMG_EMPTY ) {

		scr_coord_val x,y,w,h;
		display_get_base_image_offset( id, &x, &y, &w, &h );

		remove_offset = scr_coord(-x,-y);

		size_par = scr_size( max(x+w+remove_offset.x+6, size_par.w), max(y+h+remove_offset.y+4,size_par.h) );
		remove_offset = remove_offset+scr_size((size_par.w-w)/2,(size_par.h-h)/2);
	}

	gui_component_t::set_size(size_par);
}

scr_size gui_waytype_image_box_t::get_min_size() const
{
	return flexible ? scr_size( min( size.w,gui_component_t::get_min_size().w ), max( size.h,max(LINESPACE, D_TAB_HEADER_HEIGHT-2) ) ) : size;
}


void gui_waytype_button_t::set_waytype(waytype_t wt)
{
	this->wt = wt;
	set_image(wt == invalid_wt ? IMG_EMPTY : skinverwaltung_t::get_waytype_skin(wt)->get_image_id(0));
	enable(way_builder_t::is_active_waytype(wt));
	if (enabled()) {
		background_color = world()->get_settings().get_waytype_color(wt);
	}
	else {
		background_color = 44373; // gray
	}

	set_tooltip(gui_waytype_tab_panel_t::get_translated_waytype_name(wt));

	scr_coord_val x, y, w, h=0;
	if (wt != invalid_wt) {

		display_get_base_image_offset(skinverwaltung_t::get_waytype_skin(wt)->get_image_id(0), &x, &y, &w, &h);

		scr_coord remove_offset = scr_coord(-x, -y);
		scr_size size_par;
	}

	set_size( scr_size( max(w+6, D_BUTTON_HEIGHT<<1)+2, max(h-y+4, D_TAB_HEADER_HEIGHT)));
}

bool gui_waytype_button_t::infowin_event(const event_t *ev)
{
	bool swallowed = button_t::infowin_event(ev);
	if (swallowed) {
		if (pressed) {
			background_color = world()->get_settings().get_waytype_color(wt);
		}
		else {
			background_color = 44373; // gray
		}
	}
	return swallowed;
}
