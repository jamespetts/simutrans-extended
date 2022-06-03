/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "pier_deck.h"
#include "../descriptor/ground_desc.h"
#include "../simworld.h"

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
