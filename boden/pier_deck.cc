/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_deck.h"
#include "../descriptor/ground_desc.h"
#include "../simworld.h"
#include "wege/weg.h"

pier_deck_t::pier_deck_t(koord3d pos, slope_t::type grund_slope, slope_t::type way_slope) : grund_t(pos)
{
    slope = grund_slope;
    is_dummy=false;
    this->way_slope = way_slope;
}

void pier_deck_t::calc_image_internal(const bool){
	clear_back_image();
	set_image(IMG_EMPTY);
}

void pier_deck_t::rdwr(loadsave_t *file){
	xml_tag_t t( file, "pier_deck_t");

	grund_t::rdwr(file);

	file->rdwr_byte(way_slope);

	if(  file->is_loading()  ) {
		// way_slope is only read here, AFTER grund_t::rdwr has loaded (and
		// calculated speed limits for) the ways on this tile; those calculations
		// read get_weg_hang(), so redo them now that it holds the file's value.
		// (grund_t::rdwr skips them on pier deck tiles for exactly this reason.)
		for (uint8 i = 0; i < get_top(); i++) {
			obj_t* o = obj_bei(i);
			if (o && o->get_typ() == obj_t::way) {
				static_cast<weg_t*>(o)->calc_speed_limit(this, true);
			}
		}
	}

}

void pier_deck_t::rotate90(){
	way_slope = slope_t::rotate90( way_slope );
	grund_t::rotate90();
}

sint8 pier_deck_t::get_weg_yoff() const{
	return 0;
}

void pier_deck_t::info(cbuffer_t &buf) const{
	grund_t::info(buf);
}
