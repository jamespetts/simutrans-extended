/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <algorithm>

#include "vehicle.h"

#include "../simworld.h"
#include "../simware.h"
#include "../dataobj/translator.h"
#include "../simfab.h"
#include "../player/simplay.h"
#include "../dataobj/schedule.h"
#include "../gui/minimap.h"
#include "../obj/crossing.h"
#include "../obj/wolke.h"
#include "../utils/cbuffer_t.h"
#include "../dataobj/environment.h"
#include "../obj/zeiger.h"
#include "../utils/simstring.h"

#include "../boden/wege/strasse.h"
#include "../simcity.h"
#include "../player/finance.h"
#include "../simintr.h"
#include "../utils/simrandom.h"

#include "../bauer/vehikelbauer.h"

#include "road_vehicle.h"
#include "air_vehicle.h"

#include "../path_explorer.h"
#include "../freight_list_sorter.h"

void traffic_vehicle_t::flush_travel_times(strasse_t* str)
{
	if(get_max_speed() && str->get_max_speed() && dist_travelled_since_last_hop > (128 << YARDS_PER_VEHICLE_STEP_SHIFT))
	{
		weg_t::add_travel_time_update(str, world()->get_ticks() - time_at_last_hop, dist_travelled_since_last_hop / min(get_max_speed(), kmh_to_speed(str->get_max_speed())));
	}
	reset_measurements();
}

/* get dx and dy from dir (just to remind you)
 * any vehicle (including city cars and pedestrians)
 * will go this distance per sync step.
 * (however, the real dirs are only calculated during display, these are the old ones)
 */
sint8 vehicle_base_t::dxdy[ 8*2 ] = {
	-2,  1, // s
	-2, -1, // w
	-4,  0, // sw
	 0,  2, // se
	 2, -1, // n
	 2,  1, // e
	 4,  0, // ne
	 0, -2  // nw
};


// Constants
uint8 vehicle_base_t::old_diagonal_vehicle_steps_per_tile = 128;
uint8 vehicle_base_t::diagonal_vehicle_steps_per_tile = 181;
uint16 vehicle_base_t::diagonal_multiplier = 724;

// set only once, before loading!
void vehicle_base_t::set_diagonal_multiplier( uint32 multiplier, uint32 old_diagonal_multiplier )
{
	diagonal_multiplier = (uint16)multiplier;
	diagonal_vehicle_steps_per_tile = (uint8)(130560u/diagonal_multiplier) + 1;
	if(old_diagonal_multiplier == 0)
	{
		old_diagonal_vehicle_steps_per_tile = diagonal_vehicle_steps_per_tile;
	}
	else
	{
		old_diagonal_vehicle_steps_per_tile = (uint8)(130560u/old_diagonal_multiplier) + 1;
	}
}


// if true, convoi, must restart!
bool vehicle_base_t::need_realignment() const
{
	return old_diagonal_vehicle_steps_per_tile!=diagonal_vehicle_steps_per_tile  &&  ribi_t::is_bend(direction);
}

// [0]=xoff [1]=yoff
static sint8 driveleft_base_offsets[8][2] =
{
	{  12,  6 },
	{ -12,  6 },
	{   0,  6 },
	{  12,  0 },
	{ -12, -6 },
	{  12, -6 },
	{   0, -6 },
	{ -12,  0 }
};

// [0]=xoff [1]=yoff
sint8 vehicle_base_t::overtaking_base_offsets[8][2];

// recalc offsets for overtaking
void vehicle_base_t::set_overtaking_offsets( bool driving_on_the_left )
{
	sint8 sign = driving_on_the_left ? -1 : 1;
	// a tile has the internal size of
	const sint8 XOFF=12;
	const sint8 YOFF=6;

	overtaking_base_offsets[0][0] = sign * XOFF;
	overtaking_base_offsets[1][0] = -sign * XOFF;
	overtaking_base_offsets[2][0] = 0;
	overtaking_base_offsets[3][0] = sign * XOFF;
	overtaking_base_offsets[4][0] = -sign * XOFF;
	overtaking_base_offsets[5][0] = sign * XOFF;
	overtaking_base_offsets[6][0] = 0;
	overtaking_base_offsets[7][0] = sign * (-XOFF-YOFF);

	overtaking_base_offsets[0][1] = sign * YOFF;
	overtaking_base_offsets[1][1] = sign * YOFF;
	overtaking_base_offsets[2][1] = sign * YOFF;
	overtaking_base_offsets[3][1] = 0;
	overtaking_base_offsets[4][1] = -sign * YOFF;
	overtaking_base_offsets[5][1] = -sign * YOFF;
	overtaking_base_offsets[6][1] = -sign * YOFF;
	overtaking_base_offsets[7][1] = 0;
}



/**
 * Checks if this vehicle must change the square upon next move
 * THIS IS ONLY THERE FOR LOADING OLD SAVES!
 */
bool vehicle_base_t::is_about_to_hop( const sint8 neu_xoff, const sint8 neu_yoff ) const
{
	const sint8 y_off_2 = 2*neu_yoff;
	const sint8 c_plus  = y_off_2 + neu_xoff;
	const sint8 c_minus = y_off_2 - neu_xoff;

	return ! (c_plus < OBJECT_OFFSET_STEPS*2  &&  c_minus < OBJECT_OFFSET_STEPS*2  &&  c_plus > -OBJECT_OFFSET_STEPS*2  &&  c_minus > -OBJECT_OFFSET_STEPS*2);
}


#ifdef INLINE_OBJ_TYPE
vehicle_base_t::vehicle_base_t(typ type):
	obj_t(type)
#else
vehicle_base_t::vehicle_base_t():
	obj_t()
#endif
	, gr(NULL)
	, weg(NULL)
	, direction(ribi_t::none)
	, use_calc_height(true)
	, drives_on_left(false)
	, disp_lane(2)
	, dx(0)
	, dy(0)
	, steps(0)
	, steps_next(VEHICLE_STEPS_PER_TILE -1)
	, pos_next(koord3d::invalid)
	, zoff_start(0)
	, zoff_end(0)
	, image(IMG_EMPTY)
	, next_lane(0)
	, class_reassignments(NULL)
{
	set_flag( obj_t::is_vehicle );
}


#ifdef INLINE_OBJ_TYPE
vehicle_base_t::vehicle_base_t(typ type, koord3d pos):
	obj_t(type, pos)
#else
vehicle_base_t::vehicle_base_t(koord3d pos):
	obj_t(pos)
#endif
	, gr(NULL)
	, weg(NULL)
	, direction(ribi_t::none)
	, use_calc_height(true)
	, drives_on_left(false)
	, disp_lane(2)
	, dx(0)
	, dy(0)
	, steps(0)
	, steps_next(VEHICLE_STEPS_PER_TILE -1)
	, pos_next(pos)
	, zoff_start(0)
	, zoff_end(0)
	, image(IMG_EMPTY)
	, next_lane(0)
	, class_reassignments(NULL)
{
	set_flag( obj_t::is_vehicle );
}


void vehicle_base_t::rotate90()
{
	obj_t::rotate90();
	// directions are counterclockwise to ribis!
	direction = ribi_t::rotate90( direction );
	pos_next.rotate90( welt->get_size().y-1 );
	// new offsets
	sint8 new_dx = -dy*2;
	dy = dx/2;
	dx = new_dx;
}



void vehicle_base_t::leave_tile()
{
	// first: release crossing
	// This needs to be refreshed here even though this value is cached:
	// otherwise, it can sometimes be incorrect, which could lead to vehicles failing to release crossings.
	gr = welt->lookup(get_pos());
	weg = NULL;
	if(  gr  &&  gr->ist_uebergang()  ) {
		crossing_t *cr = gr->find<crossing_t>(2);
		grund_t *gr2 = welt->lookup(pos_next);
		if(cr) {
			if(gr2==NULL  ||  gr2==gr  ||  !gr2->ist_uebergang()  ||  cr->get_logic()!=gr2->find<crossing_t>(2)->get_logic()) {
				cr->release_crossing(this);
			}
		} else {
			dbg->warning("vehicle_base_t::leave_tile()", "No crossing found at %s", gr->get_pos().get_str());
		}
	}

	// then remove from ground (or search whole map, if failed)
	if(!get_flag(not_on_map)  &&  (gr==NULL  ||  !gr->obj_remove(this)) ) {

		// was not removed (not found?)
		dbg->error("vehicle_base_t::leave_tile()","'typ %i' %p could not be removed from %d %d", get_typ(), this, get_pos().x, get_pos().y);
		DBG_MESSAGE("vehicle_base_t::leave_tile()","checking all plan squares");

		// check, whether it is on another height ...
		const planquadrat_t *pl = welt->access( get_pos().get_2d() );
		if(  pl  ) {
			gr = pl->get_boden_von_obj(this);
			if(  gr  ) {
				gr->obj_remove(this);
				dbg->warning("vehicle_base_t::leave_tile()","removed vehicle typ %i (%p) from %d %d",get_typ(), this, get_pos().x, get_pos().y);
			}
			gr = NULL;
			return;
		}

		koord k;
		bool ok = false;

		for(k.y=0; k.y<welt->get_size().y; k.y++) {
			for(k.x=0; k.x<welt->get_size().x; k.x++) {
				grund_t *gr = welt->access( k )->get_boden_von_obj(this);
				if(gr && gr->obj_remove(this)) {
					dbg->warning("vehicle_base_t::leave_tile()","removed vehicle typ %i (%p) from %d %d",get_typ(), this, k.x, k.y);
					ok = true;
				}
			}
		}

		if(!ok) {
			dbg->error("vehicle_base_t::leave_tile()","'%s' %p was not found on any map square!",get_name(), this);
		}
	}
	gr = NULL;
}


void vehicle_base_t::enter_tile(grund_t* gr)
{
	if(!gr) {
		dbg->error("vehicle_base_t::enter_tile()","'%s' new position (%i,%i,%i)!",get_name(), get_pos().x, get_pos().y, get_pos().z );
		gr = welt->lookup_kartenboden(get_pos().get_2d());
		set_pos( gr->get_pos() );
	}
	if(!gr->obj_add(this))
	{
		dbg->error("vehicle_base_t::enter_tile()","'%s failed to be added to the object list", get_name());
	}
	weg = gr->get_weg(get_waytype());
}



/* THE routine for moving vehicles
 * it will drive on as long as it can
 * @return the distance actually travelled
 */
uint32 vehicle_base_t::do_drive(uint32 distance)
{
	uint32 steps_to_do = distance >> YARDS_PER_VEHICLE_STEP_SHIFT;

	if(  steps_to_do == 0  ) {
		// ok, we will not move in this steps
		return 0;
	}
	// ok, so moving ...
	if(  !get_flag(obj_t::dirty)  ) {
		mark_image_dirty( image, 0 );
		set_flag( obj_t::dirty );
	}

	grund_t *gr = NULL; // if hopped, then this is new position

	uint32 steps_target = steps_to_do + (uint32)steps;

	uint32 distance_travelled; // Return value

	if(  steps_target > (uint32)steps_next  ) {
		// We are going far enough to hop.

		// We'll be adding steps_next+1 for each hop, as if we
		// started at the beginning of this tile, so for an accurate
		// count of steps done we must subtract the location we started with.
		sint32 steps_done = -steps;
		bool has_hopped = false;
		koord3d pos_prev;

		// Hop as many times as possible.
		while(  steps_target > steps_next  &&  (gr = hop_check())  ) {
			// now do the update for hopping
			steps_target -= steps_next+1;
			steps_done += steps_next+1;
			pos_prev = get_pos();
			hop(gr);
			use_calc_height = true;
			has_hopped = true;
		}

		if	(has_hopped)
		{
			if (dx == 0)
			{
				if (dy > 0)
				{
					set_xoff( pos_prev.x != get_pos().x ? -OBJECT_OFFSET_STEPS : OBJECT_OFFSET_STEPS );
				}
				else
				{
					set_xoff( pos_prev.x != get_pos().x ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
				}
			}
			else
			{
				set_xoff( dx < 0 ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
			}

			if (dy == 0)
			{
				if (dx > 0)
				{
					set_yoff( pos_prev.y != get_pos().y ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );
				}
				else
				{
					set_yoff( pos_prev.y != get_pos().y ? -OBJECT_OFFSET_STEPS/2 : OBJECT_OFFSET_STEPS/2 );
				}
			}
			else
			{
				set_yoff( dy < 0 ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );
			}
		}

		if(  steps_next == 0  ) {
			// only needed for aircrafts, which can turn on the same tile
			// the indicate the turn with this here
			steps_next = VEHICLE_STEPS_PER_TILE - 1;
			steps_target = VEHICLE_STEPS_PER_TILE - 1;
			steps_done -= VEHICLE_STEPS_PER_TILE - 1;
		}

		// Update internal status, how far we got within the tile.
		if(  steps_target <= steps_next  ) {
			steps = steps_target;
		}
		else {
			// could not go as far as we wanted (hop_check failed) => stop at end of tile
			steps = steps_next;
		}

		steps_done += steps;
		distance_travelled = steps_done << YARDS_PER_VEHICLE_STEP_SHIFT;

	}
	else {
		// Just travel to target, it's on same tile
		steps = steps_target;
		distance_travelled = distance & YARDS_VEHICLE_STEP_MASK; // round down to nearest step
	}

	if(use_calc_height) {
		calc_height(gr);
	}
	// remaining steps
	update_bookkeeping(distance_travelled  >> YARDS_PER_VEHICLE_STEP_SHIFT );
	return distance_travelled;
}

// For reversing: length-8*(paksize/16)

// to make smaller steps than the tile granularity, we have to use this trick
void vehicle_base_t::get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const
{
	sint32 adjusted_steps = steps;
	const vehicle_t* veh = obj_cast<vehicle_t>(this);
	const int dir = ribi_t::get_dir(get_direction());
	if (veh  &&  veh->is_reversed())
	{
		adjusted_steps += (VEHICLE_STEPS_PER_TILE / 2 - veh->get_desc()->get_length_in_steps());
		//adjusted_steps += (sint32)(env_t::reverse_base_offsets[dir][2] * VEHICLE_STEPS_PER_TILE) / veh->get_desc()->get_length_in_steps();
		adjusted_steps += env_t::reverse_base_offsets[dir][2];
	}

	// vehicles needs finer steps to appear smoother
	sint32 display_steps = (uint32)adjusted_steps*(uint16)raster_width;

	if(dx && dy) {
		display_steps &= 0xFFFFFC00;
	}
	else {
		display_steps = (display_steps*diagonal_multiplier)>>10;
	}

	xoff += (display_steps*dx) >> 10;
	yoff += ((display_steps*dy) >> 10) + (get_hoff(raster_width)) / (4 * 16);

	if (veh && veh->is_reversed())
	{
		xoff += tile_raster_scale_x(env_t::reverse_base_offsets[dir][0], raster_width);
		yoff += tile_raster_scale_y(env_t::reverse_base_offsets[dir][1], raster_width);
	}

	if(  drives_on_left  ) {
		xoff += tile_raster_scale_x( driveleft_base_offsets[dir][0], raster_width );
		yoff += tile_raster_scale_y( driveleft_base_offsets[dir][1], raster_width );
	}
}


uint16 vehicle_base_t::get_tile_steps(const koord &start, const koord &ende, /*out*/ ribi_t::ribi &direction) const
{
	static const ribi_t::ribi ribis[3][3] = {
			{ribi_t::northwest, ribi_t::north,  ribi_t::northeast},
			{ribi_t::west,     ribi_t::none, ribi_t::east},
			{ribi_t::southwest, ribi_t::south,  ribi_t::southeast}
	};
	const sint8 di = sgn(ende.x - start.x);
	const sint8 dj = sgn(ende.y - start.y);
	direction = ribis[dj + 1][di + 1];
	return di == 0 || dj == 0 ? VEHICLE_STEPS_PER_TILE : diagonal_vehicle_steps_per_tile;
}


// calcs new direction and applies it to the vehicles
ribi_t::ribi vehicle_base_t::calc_set_direction(const koord3d& start, const koord3d& ende)
{
	ribi_t::ribi direction = ribi_t::none;

	const sint8 di = ende.x - start.x;
	const sint8 dj = ende.y - start.y;

	if(dj < 0 && di == 0) {
		direction = ribi_t::north;
		dx = 2;
		dy = -1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(dj > 0 && di == 0) {
		direction = ribi_t::south;
		dx = -2;
		dy = 1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di < 0 && dj == 0) {
		direction = ribi_t::west;
		dx = -2;
		dy = -1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di >0 && dj == 0) {
		direction = ribi_t::east;
		dx = 2;
		dy = 1;
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	} else if(di > 0 && dj > 0) {
		direction = ribi_t::southeast;
		dx = 0;
		dy = 2;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else if(di < 0 && dj < 0) {
		direction = ribi_t::northwest;
		dx = 0;
		dy = -2;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else if(di > 0 && dj < 0) {
		direction = ribi_t::northeast;
		dx = 4;
		dy = 0;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	} else {
		direction = ribi_t::southwest;
		dx = -4;
		dy = 0;
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	}
	// we could artificially make diagonals shorter: but this would break existing game behaviour
	return direction;
}


// this routine calculates the new height
// beware of bridges, tunnels, slopes, ...
void vehicle_base_t::calc_height(grund_t *gr)
{
	use_calc_height = false;   // assume, we are only needed after next hop
	zoff_start = zoff_end = 0; // assume flat way

	if(gr==NULL) {
		gr = welt->lookup(get_pos());
	}
	if(gr==NULL) {
		// slope changed below a moving thing?!?
		return;
	}
	else if (gr->ist_tunnel() && gr->ist_karten_boden() && !is_flying()) {
		use_calc_height = true; // to avoid errors if underground mode is switched
		if(  grund_t::underground_mode == grund_t::ugm_none  ||
			(grund_t::underground_mode == grund_t::ugm_level  &&  gr->get_hoehe() < grund_t::underground_level)
		) {
			// need hiding? One of the few uses of XOR: not half driven XOR exiting => not hide!
			ribi_t::ribi hang_ribi = ribi_type( gr->get_grund_hang() );
			if((steps<(steps_next/2))  ^  ((hang_ribi&direction)!=0)  ) {
				set_image(IMG_EMPTY);
			}
			else {
				calc_image();
			}
		}
	}
	else {
		// force a valid image above ground, with special handling of tunnel entraces
		if (get_image() == IMG_EMPTY) {
			if (!gr->ist_tunnel() && gr->ist_karten_boden()) {
				calc_image();
			}
		}
		// will not work great with ways, but is very short!
		slope_t::type hang = gr->get_weg_hang();
		if(  hang  ) {
			const uint slope_height = is_one_high(hang) ? 1 : 2;
			ribi_t::ribi hang_ribi = ribi_type(hang);
			if(  ribi_t::doubles(hang_ribi)  ==  ribi_t::doubles(direction)) {
				zoff_start = hang_ribi & direction                      ? 2*slope_height : 0;  // 0 .. 4
				zoff_end   = hang_ribi & ribi_t::backward(direction) ? 2*slope_height : 0;  // 0 .. 4
			}
			else {
				// only for shadows and movingobjs ...
				zoff_start = hang_ribi & direction                      ? slope_height+1 : 0;  // 0 .. 3
				zoff_end   = hang_ribi & ribi_t::backward(direction) ? slope_height+1 : 0;  // 0 .. 3
			}
		}
		else {
			zoff_start = (gr->get_weg_yoff() * 2) / TILE_HEIGHT_STEP;
			zoff_end = zoff_start;
		}
	}
}

sint16 vehicle_base_t::get_hoff(const sint16 raster_width) const
{
	sint16 h_start = -(sint8)TILE_HEIGHT_STEP * (sint8)zoff_start;
	sint16 h_end   = -(sint8)TILE_HEIGHT_STEP * (sint8)zoff_end;
	return ((h_start*steps + h_end*(256 - steps))*raster_width) >> 9;
}


/* true, if one could pass through this field
 * also used for citycars, thus defined here
 */
vehicle_base_t *vehicle_base_t::get_blocking_vehicle(const grund_t *gr, const convoi_t *cnv, const uint8 current_direction, const uint8 next_direction, const uint8 next_90direction, const private_car_t *pcar, sint8 lane_on_the_tile )
{
	bool cnv_overtaking = false; //whether this convoi is on passing lane.
	if(  cnv  ) {
		cnv_overtaking = cnv -> is_overtaking();
	}
	if(  pcar  ) {
		cnv_overtaking = pcar -> is_overtaking();
	}
	switch (lane_on_the_tile) {
		case 1:
		cnv_overtaking = true; //treat as convoi is overtaking.
		break;
		case -1:
		cnv_overtaking = false; //treat as convoi is not overtaking.
		break;
	}
	// Search vehicle
	for(  uint8 pos=1;  pos < gr->get_top();  pos++  ) {
		if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
			if(  v->get_typ()==obj_t::pedestrian  ) {
				continue;
			}

			// check for car
			uint8 other_direction=255;
			bool other_moving = false;
			bool other_overtaking = false; //whether the other convoi is on passing lane.
			if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
				// ignore ourself
				if(  cnv == at->get_convoi()  ) {
					continue;
				}
				other_direction = at->get_direction();
				other_moving = at->get_convoi()->get_akt_speed() > kmh_to_speed(1);
				other_overtaking = at->get_convoi()->is_overtaking();
			}
			// check for city car
			else if(  v->get_waytype() == road_wt  ) {
				other_direction = v->get_direction();
				if(  private_car_t const* const sa = obj_cast<private_car_t>(v)  ){
					if(  pcar == sa  ) {
						continue; // ignore ourself
					}
					other_moving = sa->get_current_speed() > 1;
					other_overtaking = sa->is_overtaking();
				}
			}

			// ok, there is another car ...
			if(  other_direction != 255  ) {
				if(  next_direction == other_direction  &&  !ribi_t::is_threeway(gr->get_weg_ribi(road_wt))  &&  cnv_overtaking == other_overtaking  ) {
					// only consider cars on same lane.
					// cars going in the same direction and no crossing => that mean blocking ...
					return v;
				}

				const ribi_t::ribi other_90direction = (gr->get_pos().get_2d() == v->get_pos_next().get_2d()) ? other_direction : calc_direction(gr->get_pos(),v->get_pos_next());
				if(  other_90direction == next_90direction  &&  cnv_overtaking == other_overtaking  ) {
					// Want to exit in same as other   ~50% of the time
					return v;
				}

				const bool across = next_direction == (drives_on_left ? ribi_t::rotate45l(next_90direction) : ribi_t::rotate45(next_90direction)); // turning across the opposite directions lane
				const bool other_across = other_direction == (drives_on_left ? ribi_t::rotate45l(other_90direction) : ribi_t::rotate45(other_90direction)); // other is turning across the opposite directions lane
				if(  other_direction == next_direction  &&  !(other_across || across)  &&  cnv_overtaking == other_overtaking  ) {
					// only consider cars on same lane.
					// entering same straight waypoint as other ~18%
					return v;
				}

				const bool straight = next_direction == next_90direction; // driving straight
				const ribi_t::ribi current_90direction = straight ? ribi_t::backward(next_90direction) : (~(next_direction|ribi_t::backward(next_90direction)))&0x0F;
				const bool other_straight = other_direction == other_90direction; // other is driving straight
				const bool other_exit_same_side = current_90direction == other_90direction; // other is exiting same side as we're entering
				const bool other_exit_opposite_side = ribi_t::backward(current_90direction) == other_90direction; // other is exiting side across from where we're entering
				if(  across  &&  ((ribi_t::is_perpendicular(current_90direction,other_direction)  &&  other_moving)  ||  (other_across  &&  other_exit_opposite_side)  ||  ((other_across  ||  other_straight)  &&  other_exit_same_side  &&  other_moving) ) )  {
					// other turning across in front of us from orth entry dir'n   ~4%
					return v;
				}

				const bool headon = ribi_t::backward(current_direction) == other_direction; // we're meeting the other headon
				const bool other_exit_across = (drives_on_left ? ribi_t::rotate90l(next_90direction) : ribi_t::rotate90(next_90direction)) == other_90direction; // other is exiting by turning across the opposite directions lane
				if(  straight  &&  (ribi_t::is_perpendicular(current_90direction,other_direction)  ||  (other_across  &&  other_moving  &&  (other_exit_across  ||  (other_exit_same_side  &&  !headon))) ) ) {
					// other turning across in front of us, but allow if other is stopped - duplicating historic behaviour   ~2%
					return v;
				}
				else if(  other_direction == current_direction  &&  current_90direction == ribi_t::none  &&  cnv_overtaking == other_overtaking  ) {
					// entering same diagonal waypoint as other   ~1%
					return v;
				}

				// else other car is not blocking   ~25%
			}
		}
	}

	// way is free
	return NULL;
}

bool vehicle_base_t::judge_lane_crossing( const uint8 current_direction, const uint8 next_direction, const uint8 other_next_direction, const bool is_overtaking, const bool forced_to_change_lane ) const
{
	bool on_left = !(is_overtaking==welt->get_settings().is_drive_left());
	// go straight = 0, turn right = -1, turn left = 1.
	sint8 this_turn;
	if(  next_direction == ribi_t::rotate90(current_direction)  ) {
		this_turn = -1;
	}
	else if(  next_direction == ribi_t::rotate90l(current_direction)  ) {
		this_turn = 1;
	}
	else {
		// go straight?
		this_turn = 0;
	}
	sint8 other_turn;
	if(  other_next_direction == ribi_t::rotate90(current_direction)  ) {
		other_turn = -1;
	}
	else if(  other_next_direction == ribi_t::rotate90l(current_direction)  ) {
		other_turn = 1;
	}
	else {
		// go straight?
		other_turn = 0;
	}
	if(  on_left  ) {
		if(  forced_to_change_lane  &&  other_turn - this_turn >= 0  ) {
			return true;
		}
		else if(  other_turn - this_turn > 0  ) {
			return true;
		}
	}
	else {
		if(  forced_to_change_lane  &&  other_turn - this_turn <= 0  ) {
			return true;
		}
		else if(  other_turn - this_turn < 0  ) {
			return true;
		}
	}
	return false;
}


sint16 vehicle_t::compare_directions(sint16 first_direction, sint16 second_direction) const
{
	//Returns difference between two directions in degrees
	sint16 difference = 0;
	if(first_direction > 360 || second_direction > 360 || first_direction < 0 || second_direction < 0) return 0;
	//If directions > 360, this means that they are not supposed to be checked.
	if(first_direction > 180 && second_direction < 180)
	{
		sint16 tmp = first_direction - second_direction;
		if(tmp > 180) difference = 360 - tmp;
		else difference = tmp;
	}
	else if(first_direction < 180 && second_direction > 180)
	{
		sint16 tmp = second_direction - first_direction;
		if(tmp > 180) difference = 360 - tmp;
		else difference = tmp;
	}
	else if(first_direction > second_direction)
	{
		difference = first_direction - second_direction;
	}
	else
	{
		difference = second_direction - first_direction;
	}
	return difference;
}

void vehicle_t::rotate90()
{
	vehicle_base_t::rotate90();
	previous_direction = ribi_t::rotate90( previous_direction );
	last_stop_pos.rotate90( welt->get_size().y-1 );
}


void vehicle_t::rotate90_freight_destinations(const sint16 y_size)
{
	// now rotate the freight
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		FOR(slist_tpl<ware_t>, &tmp, fracht[i])
		{
			tmp.rotate90(y_size);
		}
	}
}



void vehicle_t::set_convoi(convoi_t *c)
{
	/* cnv can have three values:
	 * NULL: not previously assigned
	 * 1 (only during loading): convoi wants to reserve the whole route
	 * other: previous convoi (in this case, currently always c==cnv)
	 *
	 * if c is NULL, then the vehicle is removed from the convoi
	 * (the rail_vehicle_t::set_convoi etc. routines must then remove a
	 *  possibly pending reservation of stops/tracks)
	 */
	assert(  c==NULL  ||  cnv==NULL  ||  cnv==(convoi_t *)1  ||  c==cnv);
	cnv = c;
	if(cnv) {
		// we need to re-establish the finish flag after loading
		if(leading) {
			route_t const& r = *cnv->get_route();
			check_for_finish = r.empty() || route_index >= r.get_count() || get_pos() == r.at(route_index);
		}
		if(  pos_next != koord3d::invalid  ) {
			route_t const& r = *cnv->get_route();
			if (!r.empty() && route_index < r.get_count() - 1) {
				grund_t const* const gr = welt->lookup(pos_next);
				if (!gr || !gr->get_weg(get_waytype())) {
					if (!(water_wt == get_waytype()  &&  gr  &&  gr->is_water())) { // ships on the open sea are valid
						pos_next = r.at(route_index + 1U);
					}
				}
			}
		}
		// just correct freight destinations
		for (uint8 i = 0; i < number_of_classes; i++)
		{
			FOR(slist_tpl<ware_t>, &c, fracht[i])
			{
				c.finish_rd(welt);
			}
		}
	}
}

/**
 * Unload freight to halt
 * @return sum of unloaded goods
 */
uint16 vehicle_t::unload_cargo(halthandle_t halt, sint64 & revenue_from_unloading, array_tpl<sint64> & apportioned_revenues)
{
	uint16 sum_menge = 0, sum_delivered = 0, index = 0;

	revenue_from_unloading = 0;

	if(  !halt.is_bound()  ) {
		return 0;
	}

	if(  halt->is_enabled( get_cargo_type() )  ) {
		for (uint8 j = 0; j < number_of_classes; j++)
		{
			if (  !fracht[j].empty()  ) {
				for (slist_tpl<ware_t>::iterator i = fracht[j].begin(), end = fracht[j].end(); i != end; )
				{
					const ware_t& tmp = *i;

					halthandle_t end_halt = tmp.get_ziel();
					halthandle_t via_halt = tmp.get_zwischenziel();

					// check if destination or transfer is still valid
					if (!end_halt.is_bound() || !via_halt.is_bound())
					{
						DBG_MESSAGE("vehicle_t::unload_cargo()", "destination of %d %s is no longer reachable", tmp.menge, translator::translate(tmp.get_name()));
						total_freight -= tmp.menge;
						sum_weight -= tmp.menge * tmp.get_desc()->get_weight_per_unit();
						i = fracht[j].erase(i);
					}
					else if (end_halt == halt || via_halt == halt)
					{
						// here, only ordinary goods should be processed

						if (halt != end_halt && welt->get_settings().is_avoid_overcrowding() && tmp.is_passenger() && !halt->is_within_walking_distance_of(via_halt) && halt->is_overcrowded(tmp.get_desc()->get_index()))
						{
							// The avoid_overcrowding setting is activated
							// Halt overcrowded - discard passengers, and collect no revenue.
							// Experimetal 7.2 - also calculate a refund.

							if (tmp.get_origin().is_bound() && get_owner()->get_finance()->get_account_balance() > 0)
							{
								// Cannot refund unless we know the origin.
								// Also, ought not refund unless the player is solvent.
								// Players ought not be put out of business by refunds, as this makes gameplay too unpredictable,
								// especially in online games, where joining one player's network to another might lead to a large
								// influx of passengers which one of the networks cannot cope with.
								const uint16 distance = shortest_distance(halt->get_basis_pos(), tmp.get_origin()->get_basis_pos());
								const uint32 distance_meters = (uint32)distance * welt->get_settings().get_meters_per_tile();
								// Refund is approximation.
								const sint64 refund_amount = (tmp.menge * tmp.get_desc()->get_refund(distance_meters) + 2048ll) / 4096ll;

								revenue_from_unloading -= refund_amount;

								// Book refund to the convoi & line
								// (the revenue accounting in simconvoi.cc will take care of the profit booking)
								cnv->book(-refund_amount, convoi_t::CONVOI_REFUNDS);
							}

							// Add passengers to unhappy (due to overcrowding) passengers.
							halt->add_pax_unhappy(tmp.menge);

							total_freight -= tmp.menge;
						}
						else
						{
							assert(halt.is_bound());

							const uint32 menge = tmp.menge;
							halt->liefere_an(tmp); //"supply" (Babelfish)
							sum_menge += menge;
							index = tmp.get_index(); // Note that there is only one freight type per vehicle
							total_freight -= tmp.menge;
							cnv->invalidate_weight_summary();

							if (tmp.is_passenger()) {
								if (tmp.is_commuting_trip) {
									halt->book(tmp.menge, HALT_COMMUTERS);
								}
								else {
									halt->book(tmp.menge, HALT_VISITORS);
								}
							}
							else if (tmp.is_mail()) {
								halt->book(menge, HALT_MAIL_HANDLING_VOLUME);
							}
							else {
								const sint64 unloading_volume = menge * tmp.get_desc()->get_weight_per_unit();
								halt->book(unloading_volume / 10, HALT_GOODS_HANDLING_VOLUME);
							}

							// Calculate the revenue for each packet.
							// Also, add to the "apportioned revenues" for way tolls.

							// Don't charge a higher class that the passenger is willing to pay, in case the accommodation was reassigned enroute.
							const uint8 class_of_accommodation = min(class_reassignments[j], tmp.get_class());

							revenue_from_unloading += menge > 0 ? cnv->calc_revenue(tmp, apportioned_revenues, class_of_accommodation) : 0;

							// book delivered goods to destination
							if (end_halt == halt)
							{
								sum_delivered += menge;
								if (tmp.is_passenger())
								{
									// New for Extended 7.2 - add happy passengers
									// to the origin station and transported passengers/mail
									// to the origin city only *after* they arrive at their
									// destinations.
									if (tmp.get_origin().is_bound())
									{
										// Check required because Simutrans-Standard saved games
										// do not have origins. Also, the start halt might not
										// be in (or be fully in) a city.
										tmp.get_origin()->add_pax_happy(menge);
										koord origin_pos = tmp.get_origin()->get_basis_pos();
										const planquadrat_t* tile = welt->access(origin_pos);
										stadt_t* origin_city = tile ? tile->get_city() : NULL;
										if (!origin_city)
										{
											// The origin stop is not within a city.
											// If the stop is located outside the city, but the passengers
											// come from a city, they will not record as transported.
											origin_pos = tmp.get_origin()->get_init_pos();
											tile = welt->access(origin_pos);
											origin_city = tile ? tile->get_city() : NULL;
										}

										if (!origin_city)
										{
											for (uint8 i = 0; i < 16; i++)
											{
												koord pos(origin_pos + origin_pos.second_neighbours[i]);
												const planquadrat_t* tile = welt->access(pos);
												origin_city = tile ? tile->get_city() : NULL;
												if (origin_city)
												{
													break;
												}
											}
										}

										if (origin_city)
										{
											origin_city->add_transported_passengers(menge);
										}
									}
								}
								else if (tmp.is_mail())
								{
									if (tmp.get_origin().is_bound())
									{
										// Check required because Simutrans-Standard saved games
										// do not have origins. Also, the start halt might not
										// be in (or be fully in) a city.
										tmp.get_origin()->add_mail_delivered(menge);
										koord origin_pos = tmp.get_origin()->get_basis_pos();
										const planquadrat_t* tile = welt->access(origin_pos);
										stadt_t* origin_city = tile ? tile->get_city() : NULL;
										if (!origin_city)
										{
											// The origin stop is not within a city.
											// If the stop is located outside the city, but the passengers
											// come from a city, they will not record as transported.
											origin_pos = tmp.get_origin()->get_init_pos();
											tile = welt->access(origin_pos);
											origin_city = tile ? tile->get_city() : NULL;
										}

										if (!origin_city)
										{
											for (uint8 i = 0; i < 16; i++)
											{
												koord pos(origin_pos + origin_pos.second_neighbours[i]);
												const planquadrat_t* tile = welt->access(pos);
												origin_city = tile ? tile->get_city() : NULL;
												if (origin_city)
												{
													break;
												}
											}
										}
										if (origin_city)
										{
											origin_city->add_transported_mail(menge);
										}
									}
								}
							}
						}
						i = fracht[j].erase(i);
					}
					else {
						++i;
					}
				}
			}
		}
	}

	if(  sum_menge  ) {
		if(  sum_delivered  ) {
			// book delivered goods to destination
			get_owner()->book_delivered( sum_delivered, get_desc()->get_waytype(), index );
		}
	}
	return sum_menge;
}


/**
 * Load freight from halt
 * @return amount loaded
 */
bool vehicle_t::load_freight_internal(halthandle_t halt, bool overcrowd, bool *skip_vehicles, bool use_lower_classes)
{
	const uint16 total_capacity = desc->get_total_capacity() + (overcrowd ? desc->get_overcrowded_capacity() : 0);
	bool other_classes_available = false;
	uint8 goods_restriction = goods_manager_t::INDEX_NONE;
	if (total_freight < total_capacity)
	{
		schedule_t *schedule = cnv->get_schedule();
		slist_tpl<ware_t> freight_add;
		uint16 capacity_this_class;
		uint32 freight_this_class;
		uint8 lowest_class_with_nonzero_capacity = 255;

		*skip_vehicles = true;
		if (!fracht[0].empty() && desc->get_mixed_load_prohibition()) {
			FOR(slist_tpl<ware_t>, const& w, fracht[0])
			{
				goods_restriction = w.index;
				break;
			}
		}
		for (uint8 i = 0; i < number_of_classes; i++)
		{
			capacity_this_class = get_accommodation_capacity(i);
			if (capacity_this_class == 0)
			{
				continue;
			}

			if (lowest_class_with_nonzero_capacity == 255)
			{
				lowest_class_with_nonzero_capacity = i;
				if (overcrowd)
				{
					capacity_this_class += desc->get_overcrowded_capacity();
				}
			}

			if (desc->get_freight_type() == goods_manager_t::passengers || desc->get_freight_type() == goods_manager_t::mail)
			{
				freight_this_class = 0;
				if (!fracht[i].empty())
				{
					FOR(slist_tpl<ware_t>, const& w, fracht[i])
					{
						freight_this_class += w.menge;
					}
				}
			}
			else
			{
				// Things other than passengers and mail need not worry about class.
				freight_this_class = total_freight;
			}

			const sint32 capacity_left_this_class = capacity_this_class - freight_this_class;

			// use_lower_classes as passed to this method indicates whether the higher class accommodation is full, hence
			// the need for higher class passengers/mail to use lower class accommodation.
			if (capacity_left_this_class >= 0)
			{
				*skip_vehicles &= halt->fetch_goods(freight_add, desc->get_freight_type(), capacity_left_this_class, schedule, cnv->get_owner(), cnv, overcrowd, class_reassignments[i], use_lower_classes, other_classes_available, desc->get_mixed_load_prohibition(), goods_restriction);
				if (!freight_add.empty())
				{
					cnv->invalidate_weight_summary();
					for (slist_tpl<ware_t>::iterator iter_z = freight_add.begin(); iter_z != freight_add.end(); )
					{
						ware_t &ware = *iter_z;
						total_freight += ware.menge;

						// could this be joined with existing freight?
						FOR(slist_tpl<ware_t>, &tmp, fracht[i])
						{
							// New system: only merges if origins are alike.
							// @author: jamespetts
							if (ware.can_merge_with(tmp))
							{
								tmp.menge += ware.menge;
								ware.menge = 0;
								break;
							}
						}

						// if != 0 we could not join it to existing => load it
						if (ware.menge != 0)
						{
							++iter_z;
							// we add list directly
						}
						else
						{
							iter_z = freight_add.erase(iter_z);
						}
					}

					if (!freight_add.empty())
					{
						// We now DON'T have to unpick which class was reassigned to i.
						// i is the accommodation class.
						fracht[i].append_list(freight_add);
					}
				}
			}
		}
	}
	return (total_freight < total_capacity && !other_classes_available);
}

void vehicle_t::fix_class_accommodations()
{
	if (!desc || desc->get_total_capacity() == 0)
	{
		// Vehicle ought to be empty - perhaps we should check this.
		return;
	}
	// Any wares in invalid accommodation will be moved to the highest
	// available accommodation. If all accommodation is full, the lowest
	// class of accommodation will be overloaded (as with normal
	//overcrowding).
	vector_tpl<ware_t> wares_to_move;
	sint32 pos = -1;
	sint32 lowest_available_class = -1;

	array_tpl<sint32> load(number_of_classes);
	array_tpl<sint32> capacity(number_of_classes);

	for (uint32 i = 0; i < number_of_classes; i++)
	{
		capacity[i] = get_accommodation_capacity(i);
		load[i] = 0;
		if (capacity[i] > 0 && lowest_available_class == -1)
		{
			lowest_available_class = i;
			continue;
		}
		FOR(slist_tpl<ware_t>, &tmp, fracht[i])
		{
			load[i] += tmp.menge;
			if (load[i] > capacity[i])
			{
				ware_t moved_ware = tmp;
				moved_ware.menge = load[i] - capacity[i];
				tmp.menge -= load[i] - capacity[i];
				load[i] = capacity[i];
				wares_to_move.append(moved_ware);
				pos++;
			}
		}
	}
	sint32 i = number_of_classes - 1;
	while (pos >= 0)
	{
		if (load[i] >= capacity[i] && i != lowest_available_class)
		{
			i--;
			continue;
		}
		load[i] += wares_to_move[pos].menge;
		if (load[i] > capacity[i] && i != lowest_available_class)
		{
			ware_t ware_copy = wares_to_move[pos];
			ware_copy.menge = wares_to_move[pos].menge + capacity[i] - load[i];
			wares_to_move[pos].menge = load[i] - capacity[i];
			fracht[i].append(ware_copy);
			load[i] = capacity[i];
			i--;
		}
		else
		{
			fracht[i].append(wares_to_move[pos]);
			pos--;
		}
	}
}


/**
 * Remove freight that no longer can reach it's destination
 * i.e. because of a changed schedule
 */
void vehicle_t::remove_stale_cargo()
{
	DBG_DEBUG("vehicle_t::remove_stale_cargo()", "called");

	// and now check every piece of ware on board,
	// if its target is somewhere on
	// the new schedule, if not -> remove
	//slist_tpl<ware_t> kill_queue;
	vector_tpl<ware_t> kill_queue;
	total_freight = 0;

	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (!fracht[i].empty())
		{
			FOR(slist_tpl<ware_t>, &tmp, fracht[i])
			{

				bool found = false;

				if (tmp.get_zwischenziel().is_bound()) {
					// the original halt exists, but does we still go there?
					FOR(minivec_tpl<schedule_entry_t>, const& i, cnv->get_schedule()->entries) {
						if (haltestelle_t::get_halt(i.pos, cnv->get_owner()) == tmp.get_zwischenziel()) {
							found = true;
							break;
						}
					}
				}
				if (!found) {
					// the target halt may have been joined or there is a closer one now, thus our original target is no longer valid
					const sint32 offset = cnv->get_schedule()->get_current_stop();
					const sint32 max_count = cnv->get_schedule()->entries.get_count();
					for (sint32 j = 0; j < max_count; j++) {
						// try to unload on next stop
						halthandle_t halt = haltestelle_t::get_halt(cnv->get_schedule()->entries[(i + offset) % max_count].pos, cnv->get_owner());
						if (halt.is_bound()) {
							if (halt->is_enabled(tmp.get_index())) {
								// ok, lets change here, since goods are accepted here
								tmp.access_zwischenziel() = halt;
								if (!tmp.get_ziel().is_bound()) {
									// set target, to prevent that unload_cargo drops cargo
									tmp.set_ziel(halt);
								}
								found = true;
								break;
							}
						}
					}
				}

				if (!found)
				{
					kill_queue.append(tmp);
				}
				else {
					// since we need to point at factory (0,0), we recheck this too
					koord k = tmp.get_zielpos();
					fabrik_t *fab = fabrik_t::get_fab(k);
					tmp.set_zielpos(fab ? fab->get_pos().get_2d() : k);

					total_freight += tmp.menge;
				}
			}

			FOR(vector_tpl<ware_t>, const& c, kill_queue) {
				fabrik_t::update_transit(c, false);
				fracht[i].remove(c);
				cnv->invalidate_weight_summary();
			}
		}
	}
	sum_weight =  get_cargo_weight() + desc->get_weight();
}


void vehicle_t::play_sound() const
{
	if(  desc->get_sound() >= 0  &&  !welt->is_fast_forward()  ) {
		welt->play_sound_area_clipped(get_pos().get_2d(), desc->get_sound(), TRAFFIC_SOUND, get_waytype() );
	}
}


/**
 * Prepare vehicle for new ride.
 * Sets route_index, pos_next, steps_next.
 * If @p recalc is true this sets position and recalculates/resets movement parameters.
 */
void vehicle_t::initialise_journey(uint16 start_route_index, bool recalc)
{
	route_index = start_route_index + 1;
	check_for_finish = false;
	use_calc_height = true;

	if(welt->is_within_limits(get_pos().get_2d())) {
		mark_image_dirty( get_image(), 0 );
	}

	route_t const& r = *cnv->get_route();
	if(!recalc) {
		// always set pos_next
		pos_next = r.at(route_index);
		assert(get_pos() == r.at(start_route_index));
	}
	else {
		// set pos_next
		if (route_index < r.get_count()) {
			pos_next = r.at(route_index);
		}
		else {
			// already at end of route
			check_for_finish = true;
		}
		set_pos(r.at(start_route_index));

		// recalc directions
		previous_direction = direction;
		direction = calc_set_direction( get_pos(), pos_next );

		zoff_start = zoff_end = 0;
		steps = 0;

		// reset lane yielding
		cnv->quit_yielding_lane();

		set_xoff( (dx<0) ? OBJECT_OFFSET_STEPS : -OBJECT_OFFSET_STEPS );
		set_yoff( (dy<0) ? OBJECT_OFFSET_STEPS/2 : -OBJECT_OFFSET_STEPS/2 );

		calc_image();

		if(previous_direction != direction)
		{
			pre_corner_direction.clear();
		}
	}

	if ( ribi_t::is_single(direction) ) {
		steps_next = VEHICLE_STEPS_PER_TILE - 1;
	}
	else {
		steps_next = diagonal_vehicle_steps_per_tile - 1;
	}
}


#ifdef INLINE_OBJ_TYPE
vehicle_t::vehicle_t(typ type, koord3d pos, const vehicle_desc_t* desc, player_t* player) :
	vehicle_base_t(type, pos)
#else
vehicle_t::vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player) :
	vehicle_base_t(pos)
#endif
	, purchase_time(welt->get_current_month())
	, sum_weight(desc->get_weight())
	, direction_steps(16)
	, hill_up(0)
	, hill_down(0)
	, is_overweight(false)
	, reversed(false)
	, diagonal_costs(0)
	, base_costs(0)
	, last_stopped_tile(koord3d::invalid)
	, speed_limit(speed_unlimited())
	, previous_direction(ribi_t::none)
	, current_friction(4)
	, route_index(1)
	, total_freight(0)
	, fracht(NULL)
	, desc(desc)
	, cnv(NULL)
	, number_of_classes(desc->get_number_of_classes())
	, leading(false)
	, last(false)
	, smoke(true)
	, check_for_finish(false)
	, has_driven(false)
	, hop_count(0)
	, last_stop_pos(koord3d::invalid)
{
	set_owner( player );

	current_livery = "default";

	fracht = new slist_tpl<ware_t>[number_of_classes];
	class_reassignments = new uint8[number_of_classes];
	for (uint32 i = 0; i < number_of_classes; i++)
	{
		// Initialise these with default values.
		class_reassignments[i] = i;
	}
}


#ifdef INLINE_OBJ_TYPE
vehicle_t::vehicle_t(typ type) :
	vehicle_base_t(type)
#else
vehicle_t::vehicle_t() :
	vehicle_base_t()
#endif
	, purchase_time(welt->get_current_month())
	, sum_weight(10000UL)
	, direction_steps(16)
	, hill_up(0)
	, hill_down(0)
	, is_overweight(false)
	, reversed(false)
	, diagonal_costs(0)
	, base_costs(0)
	, last_stopped_tile(koord3d::invalid)
	, speed_limit(speed_unlimited())
	, previous_direction(ribi_t::none)
	, current_friction(4)
	, route_index(1)
	, total_freight(0)
	, fracht(NULL)
	, desc(NULL)
	, cnv(NULL)
	, number_of_classes(0) // This cannot be set substantively here because we do not know the number of classes yet, which is set in desc.
	, leading(false)
	, last(false)
	, smoke(true)
	, check_for_finish(false)
	, has_driven(false)
	, hop_count(0)
	, last_stop_pos(koord3d::invalid)
{
	current_livery = "default";

	class_reassignments = NULL;
}

void vehicle_t::set_desc(const vehicle_desc_t* value)
{
	// Used when upgrading vehicles.

	// Empty the vehicle (though it should already be empty).
	// We would otherwise have to check passengers occupied valid accommodation.
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (!fracht[i].empty())
		{
			FOR(slist_tpl<ware_t>, &tmp, fracht[i]) {
				fabrik_t::update_transit(tmp, false);
			}
			fracht[i].clear();
		}
	}

	desc = value;
}


route_t::route_result_t vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route)
{
	return route->calc_route(welt, start, ziel, this, max_speed, cnv != NULL ? cnv->get_highest_axle_load() : ((get_sum_weight() + 499) / 1000), is_tall, 0, SINT64_MAX_VALUE, cnv != NULL ? cnv->get_weight_summary().weight / 1000 : get_total_weight());
}

route_t::route_result_t vehicle_t::reroute(const uint16 reroute_index, const koord3d &ziel)
{
	route_t xroute;    // new scheduled route from position at reroute_index to ziel
	route_t *route = cnv ? cnv->get_route() : NULL;
	const bool live = route == NULL;

	route_t::route_result_t done = route ? calc_route(route->at(reroute_index), ziel, speed_to_kmh(cnv->get_min_top_speed()), cnv->has_tall_vehicles(), &xroute) : route_t::no_route;
	if(done == route_t::valid_route && live)
	{
		// convoy replaces existing route starting at reroute_index with found route.
		cnv->update_route(reroute_index, xroute);
	}
	return done;
}

grund_t* vehicle_t::hop_check()
{
	// the leading vehicle will do all the checks
	if(leading) {
		if(check_for_finish) {
			// so we are there yet?
			cnv->ziel_erreicht();
			if(cnv->get_state()==convoi_t::INITIAL) {
				// to avoid crashes with airplanes
				use_calc_height = false;
			}
			return NULL;
		}

		// now check, if we can go here
		grund_t *bd = welt->lookup(pos_next);
		if(bd==NULL  ||  !check_next_tile(bd)  ||  cnv->get_route()->empty()) {
			// way (weg) not existent (likely destroyed) or no route ...
			cnv->suche_neue_route();
			return NULL;
		}

		// check for one-way sign etc.
		const waytype_t wt = get_waytype();
		if(  air_wt != wt  &&  route_index < cnv->get_route()->get_count()-1  ) {
			uint8 dir;
			const weg_t* way = bd->get_weg(wt);
			if(way && way->has_signal() && (wt == track_wt || wt == tram_wt || wt == narrowgauge_wt || wt == maglev_wt || wt == monorail_wt))
			{
				dir = bd->get_weg_ribi_unmasked(wt);
			}
			else
			{
				dir = get_ribi(bd);
			}
			koord3d nextnext_pos = cnv->get_route()->at(route_index+1);
			if ( nextnext_pos == get_pos() ) {
				dbg->error("vehicle_t::hop_check", "route contains point (%s) twice for %s", nextnext_pos.get_str(), cnv->get_name());
			}
			uint8 new_dir = ribi_type(nextnext_pos-pos_next);
			if((dir&new_dir)==0) {
				// new one way sign here?
				cnv->suche_neue_route();
				return NULL;
			}
			// check for recently built bridges/tunnels or reverse branches (really slows down the game, so we do this only on slopes)
			if(  bd->get_weg_hang()  ) {
				grund_t *from;
				if(  !bd->get_neighbour( from, get_waytype(), ribi_type( get_pos(), pos_next ) )  ) {
					// way likely destroyed or altered => reroute
					cnv->suche_neue_route();
					return NULL;
				}
			}
		}

		sint32 restart_speed = -1;
		// can_enter_tile() berechnet auch die Geschwindigkeit
		// mit der spaeter weitergefahren wird
		// "can_enter_tile() calculates the speed later continued with the" (Babelfish)
		if(cnv->get_checked_tile_this_step() != get_pos() && !can_enter_tile(bd, restart_speed, 0))
		{
			// stop convoi, when the way is not free
			cnv->warten_bis_weg_frei(restart_speed);

			// don't continue
			return NULL;
		}
		// we cache it here, hop() will use it to save calls to karte_t::lookup
		return bd;
	}
	else {
		// this is needed since in convoi_t::vorfahren the flag leading is set to null
		if(check_for_finish) {
			return NULL;
		}
	}
	return welt->lookup(pos_next);
}

bool vehicle_t::can_enter_tile(sint32 &restart_speed, uint8 second_check_count)
{
	cnv->set_checked_tile_this_step(get_pos());
	grund_t *gr = welt->lookup(pos_next);
	if (gr)
	{
		bool ok = can_enter_tile( gr, restart_speed, second_check_count );
		if (!ok)
		{
			cnv->set_checked_tile_this_step(koord3d::invalid);
		}
		return ok;
	}
	else {
		if(  !second_check_count  ) {
			cnv->suche_neue_route();
		}
		cnv->set_checked_tile_this_step(koord3d::invalid);
		return false;
	}
}

void vehicle_t::leave_tile()
{
	vehicle_base_t::leave_tile();
#ifndef DEBUG_ROUTES
	if(last  &&  minimap_t::get_instance()->is_visible) {
			minimap_t::get_instance()->calc_map_pixel(get_pos().get_2d());
	}
#endif
}


/** this routine add a vehicle to a tile and will insert it in the correct sort order to prevent overlaps
 */
void vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_base_t::enter_tile(gr);

	if(leading  &&  minimap_t::get_instance()->is_visible  ) {
		minimap_t::get_instance()->calc_map_pixel( get_pos().get_2d() );
	}
}


void vehicle_t::hop(grund_t* gr)
{
	//const grund_t *gr_prev = get_grund();
	const weg_t * weg_prev = get_weg();

	leave_tile();

	pos_prev = get_pos();
	set_pos( pos_next );  // next field
	if(route_index<cnv->get_route()->get_count()-1) {
		route_index ++;
		pos_next = cnv->get_route()->at(route_index);
	}
	else {
		route_index ++;
		check_for_finish = true;
	}
	previous_direction = direction;

	// check if arrived at waypoint, and update schedule to next destination
	// route search through the waypoint is already complete
	if(  get_pos()==cnv->get_schedule_target()  ) {
		if(  route_index >= cnv->get_route()->get_count()  ) {
			// we end up here after loading a game or when a waypoint is reached which crosses next itself
			cnv->set_schedule_target( koord3d::invalid );
		}
		else if(welt->lookup(cnv->get_route()->at(route_index))->get_halt() != welt->lookup(cnv->get_route()->at(cnv->get_route()->get_count() - 1))->get_halt())
		{
			cnv->get_schedule()->advance();
			const koord3d ziel = cnv->get_schedule()->get_current_entry().pos;
			cnv->set_schedule_target( cnv->is_waypoint(ziel) ? ziel : koord3d::invalid );
		}
	}

	// this is a required hack for aircrafts! Aircrafts can turn on a single square, and this confuses the previous calculation!
	if(!check_for_finish  &&  pos_prev==pos_next) {
		direction = calc_set_direction( get_pos(), pos_next);
		steps_next = 0;
	}
	else {
		if(  pos_next!=get_pos()  ) {
			direction = calc_set_direction( pos_prev, pos_next );
		}
//		else if(  (  check_for_finish  &&  welt->lookup(pos_next)  &&  ribi_t::is_straight(welt->lookup(pos_next)->get_weg_ribi_unmasked(get_waytype()))  )  ||  welt->lookup(pos_next)->is_halt())
		else
		{
			grund_t* gr_next = welt->lookup(pos_next);
			if ( gr_next && ( ( check_for_finish && ribi_t::is_straight(gr_next->get_weg_ribi_unmasked(get_waytype())) ) || gr_next->is_halt()) )
				// allow diagonal stops at waypoints on diagonal tracks but avoid them on halts and at straight tracks...
				direction = calc_set_direction( pos_prev, pos_next );
		}
	}

	// change image if direction changes
	if (previous_direction != direction) {
		calc_image();
	}

	enter_tile(gr); //"Enter field" (Google)
	weg_t *weg = get_weg();
	if(  weg  )	{
		//const grund_t *gr_prev = welt->lookup(pos_prev);
		//const weg_t * weg_prev = gr_prev != NULL ? gr_prev->get_weg(get_waytype()) : NULL;

		// Calculating the speed limit on the fly has the advantage of using up to date data - but there is no algorithm for taking into account the number of tiles of a bridge and thus the bridge weight limit using this mehtod.
		//speed_limit = calc_speed_limit(weg, weg_prev, &pre_corner_direction, direction, previous_direction);
		if (get_route_index() < cnv->get_route_infos().get_count())
		{
			speed_limit = cnv->get_route_infos().get_element(get_route_index()).speed_limit;
		}
		else
		{
			// This does not take into account bridge length (assuming any bridge to be infinitely long)
			speed_limit = calc_speed_limit(weg, weg_prev, &pre_corner_direction, cnv->get_tile_length(), direction, previous_direction);
		}

		// Weight limit needed for GUI flag
		const uint32 max_axle_load = weg->get_max_axle_load() > 0 ? weg->get_max_axle_load() : 1;
		const uint32 bridge_weight_limit = weg->get_bridge_weight_limit() > 0 ? weg->get_bridge_weight_limit() : 1;
		// Necessary to prevent division by zero exceptions if
		// weight limit is set to 0 in the file.

		// This is just used for the GUI display, so only set to true if the weight limit is set to enforce by speed restriction.
		is_overweight = ((cnv->get_highest_axle_load() > max_axle_load || cnv->get_weight_summary().weight / 1000 > bridge_weight_limit) && (welt->get_settings().get_enforce_weight_limits() == 1 || welt->get_settings().get_enforce_weight_limits() == 3));
		if (is_overweight && bridge_weight_limit)
		{
			// This may be triggered incorrectly if the convoy is longer than the bridge and the part of the convoy that can fit onto the bridge at any one time is not overweight. Check for this.
			const sint32 calced_speed_limit = calc_speed_limit(weg, weg_prev, &pre_corner_direction, cnv->get_tile_length(), direction, previous_direction);
			if (speed_limit > calced_speed_limit || calced_speed_limit > calc_speed_limit(weg, weg_prev, &pre_corner_direction, 1, direction, previous_direction))
			{
				is_overweight = false;
			}
		}

		if(weg->is_crossing())
		{
			crossing_t *crossing = gr->find<crossing_t>(2);
			if (crossing)
				crossing->add_to_crossing(this);
		}
	}
	else
	{
		speed_limit = speed_unlimited();
	}

	if(  check_for_finish  &  leading && (direction==ribi_t::north  || direction==ribi_t::west))
	{
		steps_next = (steps_next/2)+1;
	}

	// speedlimit may have changed
	cnv->must_recalc_data();

	calc_drag_coefficient(gr);
	if(weg)
	{
		weg->wear_way(desc->get_way_wear_factor());
	}
	hop_count ++;
	gr->set_all_obj_dirty();
}

/* Calculates the modified speed limit of the current way,
 * taking into account the curve and weight limit.
 * @author: jamespetts/Bernd Gabriel
 */
sint32 vehicle_t::calc_speed_limit(const weg_t *w, const weg_t *weg_previous, fixed_list_tpl<sint16, 192>* cornering_data, uint32 bridge_tiles, ribi_t::ribi current_direction, ribi_t::ribi previous_direction)
{
	if (weg_previous)
	{
		cornering_data->add_to_tail(get_direction_degrees(ribi_t::get_dir(previous_direction)));
	}
	if(w == NULL)
	{
		return speed_limit;
	}
	const bool is_corner = current_direction != previous_direction;
	const bool is_slope = welt->lookup(w->get_pos())->get_weg_hang() != slope_t::flat;
	const bool slope_specific_speed = w->get_desc()->get_topspeed_gradient_1() < w->get_desc()->get_topspeed() || w->get_desc()->get_topspeed_gradient_2() < w->get_desc()->get_topspeed();

	const bool is_tilting = desc->get_tilting();
	const sint32 base_limit = desc->get_override_way_speed() && !(slope_specific_speed && is_slope) ? SINT32_MAX_VALUE : kmh_to_speed(w->get_max_speed(desc->get_engine_type()==vehicle_desc_t::electric));
	const uint32 max_axle_load = w->get_max_axle_load();
	const uint32 bridge_weight_limit = w->get_bridge_weight_limit();
	const sint32 total_weight = cnv->get_weight_summary().weight / 1000;
	sint32 overweight_speed_limit = base_limit;
	sint32 corner_speed_limit = base_limit;
	sint32 new_limit = base_limit;
	const uint32 highest_axle_load = cnv->get_highest_axle_load();

	// Reduce speed for overweight vehicles

	uint32 adjusted_convoy_weight = cnv->get_tile_length() == 0 ? total_weight : (total_weight * max(bridge_tiles - 2, 1)) / cnv->get_tile_length();
	adjusted_convoy_weight = min(adjusted_convoy_weight, total_weight);

	if((highest_axle_load > max_axle_load || adjusted_convoy_weight > bridge_weight_limit) && (welt->get_settings().get_enforce_weight_limits() == 1 || welt->get_settings().get_enforce_weight_limits() == 3))
	{
		if((max_axle_load != 0 && (highest_axle_load * 100) / max_axle_load <= 110) && (bridge_weight_limit != 0 && (adjusted_convoy_weight * 100) / bridge_weight_limit <= 110))
		{
			// Overweight by up to 10% - reduce speed limit to a third.
			overweight_speed_limit = base_limit / 3;
		}
		else if((max_axle_load == 0 || (highest_axle_load * 100) / max_axle_load > 110) || (bridge_weight_limit == 0 || (adjusted_convoy_weight * 100) / bridge_weight_limit > 110))
		{
			// Overweight by more than 10% - reduce speed limit by a factor of 10.
			overweight_speed_limit = base_limit / 10;
		}
	}

	waytype_t waytype = cnv->get_schedule()->get_waytype();

	if(!is_corner || direction_steps == 0 || waytype == air_wt)
	{
		// If we are not counting corners, do not attempt to calculate their speed limit.
		return overweight_speed_limit;
	}
	else
	{
		// Curve radius computed: only by the time that a 90 or
		// greter degree bend, or a pair of 45 degree bends is
		// found can an accurate radius be computed.

		sint16 direction_difference = 0;
		const sint16 direction = get_direction_degrees(ribi_t::get_dir(current_direction));
		uint16 tmp;

		sint32 counter = 0;
		sint32 steps_to_second_45 = 0;
		sint32 steps_to_90 = 0;
		sint32 steps_to_135 = 0;
		sint32 steps_to_180 = 0;
		const uint16 meters_per_tile = welt->get_settings().get_meters_per_tile();
		int direction_changes = 0;
		sint16 previous_direction = direction;
		for(int i = cornering_data->get_count() - 1; i >= 0 && counter <= direction_steps; i --)
		{
			counter ++;
			if(previous_direction != cornering_data->get_element(i))
			{
				// For detecting pairs of 45 degree corners
				direction_changes ++;
				if(direction_changes == 2)
				{
					steps_to_second_45 = counter;
				}
			}
			previous_direction = cornering_data->get_element(i);
			tmp = vehicle_t::compare_directions(direction, cornering_data->get_element(i));
			if(tmp > direction_difference)
			{
				direction_difference = tmp;
				switch(direction_difference)
				{
					case 90:
						steps_to_90 = counter;
						break;
					case 135:
						steps_to_135 = counter;
						break;
					case 180:
						steps_to_180 = counter;
						break;
					default:
						break;
				}
			}
		}

		// Calculate the radius on the basis of the most severe curve detected.
		int radius = 0;
		sint32 corner_limit_kmh = SINT32_MAX_VALUE;

		// This is the maximum lateral force, denominated in fractions of G.
		// If the number is 10, the maximum allowed lateral force is 1/10th or 0.1G.
		const sint32 corner_force_divider = welt->get_settings().get_corner_force_divider(waytype);

		if(steps_to_180)
		{
			// A 180 degree curve can be made in a minimum of 4 tiles and will have a minimum radius of half the meters_per_tile value
			// The steps_to_x values are the *manhattan* distance, which is exactly twice the actual radius. Thus, halve this here.
			steps_to_180 = max(1, steps_to_180 / 2);

			radius = (steps_to_180 * meters_per_tile) / 2;
			// See here for formula: https://books.google.co.uk/books?id=NbYqQSQcE2MC&pg=PA30&lpg=PA30&dq=curve+radius+speed+limit+formula+rail&source=imglist&ots=mbfC3lCnX4&sig=qClyuNSarnvL-zgOj4HlTVgYOr8&hl=en&sa=X&ei=sBGwVOSGHMyBU4mHgNAC&ved=0CCYQ6AEwATgK#v=onepage&q=curve%20radius%20speed%20limit%20formula%20rail&f=false
			corner_limit_kmh = sqrt_i32((87 * radius) / corner_force_divider);
		}

		if(steps_to_135)
		{
			// A 135 degree curve can be made in a minimum of 4 tiles and will have a minimum radius of 2/3rds the meters_per_tile value
			// The steps_to_x values are the *manhattan* distance, which is exactly twice the actual radius.
			// This was formerly halved, but then multiplied by 2 again, which was redundant, so remove both instead.

			radius = (steps_to_135 * meters_per_tile) / 3;
			corner_limit_kmh = min(corner_limit_kmh, sqrt_i32((87 * radius) / corner_force_divider));
		}

		if(steps_to_90)
		{
			// A 90 degree curve can be made in a minimum of 3 tiles and will have a minimum radius of the meters_per_tile value
			// The steps_to_x values are the *manhattan* distance, which is exactly twice the actual radius. Thus, halve this here.
			radius = (steps_to_90 * meters_per_tile) / 2;

			corner_limit_kmh = min(corner_limit_kmh, sqrt_i32((87 * radius) / corner_force_divider));
		}

		if(steps_to_second_45 && !steps_to_90)
		{
			// Go here only if this is a pair of *self-correcting* 45 degree turns, not a pair
			// that between them add up to 90 degrees, the algorithm for which is above.
			uint32 assumed_radius = welt->get_settings().get_assumed_curve_radius_45_degrees();

			// If assumed_radius == 0, then do not impose any speed limit for 45 degree turns alone.
			if(assumed_radius > 0)
			{
				// A pair of self-correcting 45 degree corners can be made in a minimum of 4 tiles and will have a minimum radius of twice the meters per tile value
				// However, this is too harsh for most uses, so set the assumed radius as the minimum here.
				// There is no need to divide steos_to_second_45 by 2 only to multiply it by 2 again.
				radius = max(assumed_radius, (steps_to_second_45 * meters_per_tile));

				corner_limit_kmh = min(corner_limit_kmh, sqrt_i32((87 * radius) / corner_force_divider));
			}
		}

		if(radius > 0)
		{
			const int tilting_min_radius_effect = welt->get_settings().get_tilting_min_radius_effect();

			sint32 corner_limit = kmh_to_speed(corner_limit_kmh);

			// Adjust for tilting.
			// Tilting only makes a difference to reasonably wide corners.
			if(is_tilting && radius > tilting_min_radius_effect)
			{
				// Tilting trains can take corners faster
				corner_limit = (corner_limit * 130) / 100;
			}

			// Now apply the adjusted corner limit
			if (direction_difference > 0)
			{
				corner_speed_limit = min(base_limit, corner_limit);
			}
		}
	}

	// Overweight penalty not to be made cumulative to cornering penalty
	if(corner_speed_limit < overweight_speed_limit)
	{
		new_limit = corner_speed_limit;
	}
	else
	{
		new_limit = overweight_speed_limit;
	}

	sint8 trim_size = cornering_data->get_count() - direction_steps;
	cornering_data->trim_from_head((trim_size >= 0) ? trim_size : 0);

	//Speed limit must never be 0.
	return max(kmh_to_speed(1), new_limit);
}

/** gets the waytype specific friction on straight flat way.
 * extracted from vehicle_t::calc_drag_coefficient()
 */
sint16 get_friction_of_waytype(waytype_t waytype)
{
	switch(waytype)
	{
		case road_wt:
			return 4;
		default:
			return 1;
	}
}


/* calculates the current friction coefficient based on the current track
 * flat, slope, (curve)...
 * @author prissi, HJ, Dwachs
 */
void vehicle_t::calc_drag_coefficient(const grund_t *gr) //,const int h_alt, const int h_neu)
{

	if(gr == NULL)
	{
		return;
	}

	const waytype_t waytype = get_waytype();
	const sint16 base_friction = get_friction_of_waytype(waytype);
	//current_friction = base_friction;

	// Old method - not realistic. Now uses modified speed limit. Preserved optionally.
	// curve: higher friction
	if(previous_direction != direction)
	{
		//The level (if any) of additional friction to apply around corners.
		const uint8 curve_friction_factor = welt->get_settings().get_curve_friction_factor(waytype);
		current_friction += curve_friction_factor;
	}

	// or a hill?
	// Cumulative drag for hills: @author: jamespetts
	// See here for an explanation of the additional resistance
	// from hills: https://en.wikibooks.org/wiki/Fundamentals_of_Transportation/Grade
	const slope_t::type hang = gr->get_weg_hang();
	if(hang != slope_t::flat)
	{
		// Bernd Gabriel, Nov, 30 2009: at least 1 partial direction must match for uphill (op '&'), but not the
		// complete direction. The hill might begin in a curve and then '==' accidently accelerates the vehicle.
		const uint slope_height = is_one_high(hang) ? 1 : 2;
		if(ribi_type(hang) & direction)
		{
			// Uphill

			//  HACK: For some reason yet to be discerned, some road vehicles struggled with hills to the extent of
			// stalling (reverting to the minimum speed of 1km/h). Until we find out where the problem ultimately lies,
			// assume that hills are less teep for road vehicles as for anything else.
			const sint16 abf_1 = get_waytype() == road_wt ? 18 : 40;
			const sint16 abf_2 = get_waytype() == road_wt ? 35 : 80;

			const sint16 acf_1 = get_waytype() == road_wt ? 10 : 23;
			const sint16 acf_2 = get_waytype() == road_wt ? 19 : 47;

			const sint16 additional_base_friction = slope_height == 1 ? abf_1 : abf_2;
			const sint16 additional_current_friction = slope_height == 1 ? acf_1 : acf_2;
			current_friction = min(base_friction + additional_base_friction, current_friction + additional_current_friction);
		}
		else
		{
			// Downhill
			const sint16 subtractional_base_friction = slope_height == 1 ? 24 : 48;
			const sint16 subtractional_current_friction = slope_height == 1 ? 14 : 28;
			current_friction = max(base_friction - subtractional_base_friction, current_friction - subtractional_current_friction);
		}
	}
	else
	{
		current_friction = max(base_friction, current_friction - 13);
	}
}

vehicle_t::direction_degrees vehicle_t::get_direction_degrees(ribi_t::dir direction) const
{
	switch(direction)
	{
	case ribi_t::dir_north :
		return vehicle_t::North;
	case ribi_t::dir_northeast :
		return vehicle_t::Northeast;
	case ribi_t::dir_east :
		return vehicle_t::East;
	case ribi_t::dir_southeast :
		return vehicle_t::Southeast;
	case ribi_t::dir_south :
		return vehicle_t::South;
	case ribi_t::dir_southwest :
		return vehicle_t::Southwest;
	case ribi_t::dir_west :
		return vehicle_t::West;
	case ribi_t::dir_northwest :
		return vehicle_t::Northwest;
	default:
		return vehicle_t::North;
	};
	return vehicle_t::North;
}

void vehicle_t::make_smoke() const
{
	// does it smoke at all?
	if(  smoke  &&  desc->get_smoke()  ) {
		// Hajo: only produce smoke when heavily accelerating or steam engine
		if(  (cnv->get_akt_speed() < (sint32)((cnv->get_vehicle_summary().max_sim_speed * 7u) >> 3) && (route_index < cnv->get_route_infos().get_count() - 4)) ||  desc->get_engine_type() == vehicle_desc_t::steam  ) {
			grund_t* const gr = welt->lookup( get_pos() );
			if(  gr  ) {
				wolke_t* const abgas = new wolke_t( get_pos(), get_xoff() + ((dx * (sint16)((uint16)steps * OBJECT_OFFSET_STEPS)) >> 8), get_yoff() + ((dy * (sint16)((uint16)steps * OBJECT_OFFSET_STEPS)) >> 8) + get_hoff(), desc->get_smoke() );
				if(  !gr->obj_add( abgas )  ) {
					abgas->set_flag( obj_t::not_on_map );
					delete abgas;
				}
				else {
					welt->sync_way_eyecandy.add( abgas );
				}
			}
		}
	}
}


const char *vehicle_t::get_cargo_mass() const
{
	return get_cargo_type()->get_mass();
}


/**
 * Calculate transported cargo total weight in KG
 */
uint32 vehicle_t::get_cargo_weight() const
{
	uint32 weight = 0;
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		FOR(slist_tpl<ware_t>, const& c, fracht[i])
		{
			weight += c.menge * c.get_desc()->get_weight_per_unit();
		}
	}
	return weight;
}


void vehicle_t::get_cargo_info(cbuffer_t & buf) const
{
	vector_tpl<vector_tpl<ware_t>> fracht_array(number_of_classes);
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		vector_tpl<ware_t> this_iteration_vector(fracht->get_count());
		FOR(slist_tpl<ware_t>, w, fracht[i])
		{
			this_iteration_vector.append(w);
		}
		fracht_array.append(this_iteration_vector);
	}

	INT_CHECK("simconvoi 2643");

	buf.clear();

	if (get_cargo_type() == goods_manager_t::passengers || get_cargo_type() == goods_manager_t::mail)
	{
		uint8 freight_info_order = 9; // = by_accommodation_detail
		ware_t cargo_type = get_cargo_type();
		for (uint8 i = 0; i < number_of_classes; i++)
		{
			freight_list_sorter_t::sort_freight(fracht_array[i], buf, (freight_list_sorter_t::sort_mode_t)freight_info_order, NULL, "loaded", get_reassigned_class(i), get_accommodation_capacity(i), &cargo_type, true);
		}
	}
	else
	{
		uint8 freight_info_order = 6; // = by_destination_detail
		slist_tpl <ware_t>capacity;
		ware_t ware = get_cargo_type();
		ware.menge = desc->get_total_capacity();
		capacity.insert(ware);
		freight_list_sorter_t::sort_freight(fracht_array[0], buf, (freight_list_sorter_t::sort_mode_t)freight_info_order, &capacity, "loaded", 0, 0, NULL, true);
	}
}

/**
 * Delete all vehicle load
 */
void vehicle_t::discard_cargo()
{
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		FOR(slist_tpl<ware_t>, w, fracht[i])
		{
			fabrik_t::update_transit(w, false);
		}
		fracht[i].clear();
	}
	sum_weight =  desc->get_weight();
}

uint16 vehicle_t::load_cargo(halthandle_t halt, bool overcrowd, bool *skip_convois, bool *skip_vehicles, bool use_lower_classes)
{
	const uint16 start_freight = total_freight;
	if(halt.is_bound() && halt->gibt_ab(desc->get_freight_type()))
	{
		*skip_convois = load_freight_internal(halt, overcrowd, skip_vehicles, use_lower_classes);
	}
	else if(desc->get_total_capacity() > 0)
	{
		*skip_convois = true; // don't try to load anymore from a stop that can't supply
	}
	sum_weight = get_cargo_weight() + desc->get_weight();
	calc_image();
	return total_freight - start_freight;
}

void vehicle_t::calc_image()
{
	const goods_desc_t* gd;
	bool empty = true;
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (!fracht[i].empty())
		{
			gd = fracht[i].front().get_desc();
			empty = false;
			break;
		}
	}

	image_id old_image=get_image();
	if (empty)
	{
		set_image(desc->get_image_id(ribi_t::get_dir(get_direction_of_travel()), NULL, current_livery.c_str()));
	}
	else
	{
		set_image(desc->get_image_id(ribi_t::get_dir(get_direction_of_travel()), gd, current_livery.c_str()));
	}
	if(old_image!=get_image()) {
		set_flag(obj_t::dirty);
	}
}

image_id vehicle_t::get_loaded_image() const
{
	const goods_desc_t* gd;
	bool empty = true;
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (!fracht[i].empty())
		{
			gd = fracht[i].front().get_desc();
			empty = false;
			break;
		}
	}
	if (reversed)
	{
		return desc->get_image_id(ribi_t::dir_north, empty ? goods_manager_t::none : gd, current_livery.c_str());
	}
	else
	{
		return desc->get_image_id(ribi_t::dir_south, empty ? goods_manager_t::none : gd, current_livery.c_str());
	}
}


// true, if this vehicle did not moved for some time
bool vehicle_t::is_stuck()
{
	return cnv==NULL  ||  cnv->is_waiting();
}

void vehicle_t::update_bookkeeping(uint32 steps)
{
	   // Only the first vehicle in a convoy does this,
	   // or else there is double counting.
	   // NOTE: As of 9.0, increment_odometer() also adds running costs for *all* vehicles in the convoy.
		if (leading) cnv->increment_odometer(steps);
}

ribi_t::ribi vehicle_t::get_direction_of_travel() const
{
	ribi_t::ribi dir = get_direction();
	if(reversed)
	{
		switch(dir)
		{
		case ribi_t::north:
			dir = ribi_t::south;
			break;

		case ribi_t::east:
			dir = ribi_t::west;
			break;

		case ribi_t::northeast:
			dir = ribi_t::southwest;
			break;

		case ribi_t::south:
			dir = ribi_t::north;
			break;

		case ribi_t::southeast:
			dir = ribi_t::northwest;
			break;

		case ribi_t::west:
			dir = ribi_t::east;
			break;

		case ribi_t::northwest:
			dir = ribi_t::southeast;
			break;

		case ribi_t::southwest:
			dir = ribi_t::northeast;
			break;
		};
	}
	return dir;
}

void vehicle_t::set_reversed(bool value)
{
	if(desc->is_bidirectional())
	{
		reversed = value;
	}
}

uint16 vehicle_t::get_total_cargo_by_class(uint8 g_class) const
{
	uint16 carried = 0;
	if (g_class >= number_of_classes)
	{
		return 0;
	}

	FOR(slist_tpl<ware_t>, const& ware, fracht[g_class])
	{
		carried += ware.menge;
	}

	return carried;
}

uint16 vehicle_t::get_reassigned_class(uint8 g_class) const
{
	uint16 reassigned_class = class_reassignments[g_class];
	return reassigned_class;
}


uint8 vehicle_t::get_number_of_accommodation_classes() const
{
	uint8 accommodation_classes = 0;
	for (uint8 i = 0; i < number_of_classes; i++) {
		if (get_fare_capacity(i)) {
			accommodation_classes++;
		}
	}
	if (!accommodation_classes && desc->get_overcrowded_capacity()) {
		accommodation_classes++;
	}
	return accommodation_classes;
}


uint16 vehicle_t::get_overcrowded_capacity(uint8 g_class) const
{
	if (g_class >= number_of_classes)
	{
		return 0;
	}
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (desc->get_capacity(i) > 0)
		{
			return i == g_class ? desc->get_overcrowded_capacity() : 0;
		}
	}
	return 0;
}

uint16 vehicle_t::get_overcrowding(uint8 g_class) const
{
	const uint16 carried = get_total_cargo_by_class(g_class);
	const uint16 capacity = get_accommodation_capacity(g_class); // Do not count a vehicle as overcrowded if the higher class passengers can travel in lower class accommodation and still get a seat.

	return carried - capacity > 0 ? carried - capacity : 0;
}

uint16 vehicle_t::get_accommodation_capacity(uint8 g_class, bool include_lower_classes) const
{
	if (!include_lower_classes) {
		return desc->get_capacity(g_class);
	}

	uint16 cap = 0;
	for (uint i=0; i <= g_class; i++)
	{
		cap += desc->get_capacity(i);
	}
	return cap;
}

uint16 vehicle_t::get_fare_capacity(uint8 g_class, bool include_lower_classes) const
{
	// Take into account class reassignments.
	uint16 cap = 0;
	for (uint8 i = 0; i < desc->get_number_of_classes(); i++)
	{
		if (class_reassignments[i] == g_class || (include_lower_classes && class_reassignments[i] < g_class))
		{
			cap += desc->get_capacity(i);
		}
	}

	return cap;
}

uint8 vehicle_t::get_comfort(uint8 catering_level, uint8 g_class) const
{
	// If we have more than one class of accommodation that has been reassigned
	// to carry the same class of passengers, take an average of the various types
	// weighted by capacity.
	uint32 comfort_sum = 0;
	uint32 capacity_this_class = 0;

	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (class_reassignments[i] == g_class)
		{
			comfort_sum += desc->get_adjusted_comfort(catering_level, i) * get_accommodation_capacity(i);
			capacity_this_class += get_accommodation_capacity(i);
		}
	}

	uint8 base_comfort;

	if (comfort_sum == 0)
	{
		return 0;
	}


	base_comfort = (uint8)(comfort_sum / capacity_this_class);

	if(base_comfort == 0)
	{
		return 0;
	}
	else if(total_freight <= capacity_this_class)
	{
		// Not overcrowded - return base level
		return base_comfort;
	}

	// Else
	// Overcrowded - adjust comfort. Standing passengers
	// are very uncomfortable (no more than 10).
	const uint8 standing_comfort = (base_comfort < 20) ? (base_comfort / 2) : 10;
	uint16 passenger_count = 0;
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		if (class_reassignments[i] == g_class)
		{
			FOR(slist_tpl<ware_t>, const& ware, fracht[i])
			{
				if(ware.is_passenger())
				{
					passenger_count += ware.menge;
				}
			}
		}
	}
	//assert(passenger_count <= total_freight); // FIXME: This assert is often triggered after commit 6500dcc. It is not clear why.

	const uint16 total_seated_passengers = passenger_count < capacity_this_class ? passenger_count : capacity_this_class;
	const uint16 total_standing_passengers = passenger_count > total_seated_passengers ? passenger_count - total_seated_passengers : 0;
	// Avoid division if we can
	if(total_standing_passengers == 0)
	{
		return base_comfort;
	}
	// Else
	// Average comfort of seated and standing
	return ((total_seated_passengers * base_comfort) + (total_standing_passengers * standing_comfort)) / passenger_count;
}



void vehicle_t::rdwr(loadsave_t *file)
{
	// this is only called from objlist => we save nothing ...
	assert(  file->is_saving()  );
	(void)file;
}


void vehicle_t::rdwr_from_convoi(loadsave_t *file)
{
	xml_tag_t r( file, "vehicle_t" );

	obj_t::rdwr(file);
	uint8 saved_number_of_classes = number_of_classes;

	if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 22)
	{
		// We needed to save this in case "desc" could not be found on re-loading because
		// of some pakset changes. Now we save this for compatibility, and also in
		// case the number of classes in the pakset changes.

		file->rdwr_byte(saved_number_of_classes);
	}
	else if (file->is_loading())
	{
		saved_number_of_classes = 1;
	}

	sint32 total_fracht_count = 0;
	sint32* fracht_count = new sint32[saved_number_of_classes];
	bool create_dummy_ware = false;

	if (file->is_saving())
	{
		for (uint8 i = 0; i < saved_number_of_classes; i++)
		{
			fracht_count[i] = fracht[i].get_count();
			total_fracht_count += fracht_count[i];
		}

		// we try to have one freight count in class zero to guess the right freight
		// when no desc is given
		if (total_fracht_count == 0 && (desc->get_freight_type() != goods_manager_t::none) && desc->get_total_capacity() > 0)
		{
			total_fracht_count = 1;
			fracht_count[0] = 1;
			create_dummy_ware = true;
		}
	}

	if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 22)
	{
		file->rdwr_bool(create_dummy_ware);
	}

	// since obj_t does no longer save positions
	if(  file->is_version_atleast(101, 0)  ) {
		koord3d pos = get_pos();
		pos.rdwr(file);
		set_pos(pos);
	}

	sint8 hoff = file->is_saving() ? get_hoff() : 0;

	if(file->is_version_less(86, 6)) {
		sint32 l;
		file->rdwr_long(purchase_time);
		file->rdwr_long(l);
		dx = (sint8)l;
		file->rdwr_long(l);
		dy = (sint8)l;
		file->rdwr_long(l);
		hoff = (sint8)(l*TILE_HEIGHT_STEP / 16);
		file->rdwr_long(speed_limit);
		file->rdwr_enum(direction);
		file->rdwr_enum(previous_direction);
		file->rdwr_long(total_fracht_count);
		file->rdwr_long(l);
		route_index = (uint16)l;
		purchase_time = (purchase_time >> welt->ticks_per_world_month_shift) + welt->get_settings().get_starting_year();
	DBG_MESSAGE("vehicle_t::rdwr_from_convoi()","bought at %i/%i.",(purchase_time%12)+1,purchase_time/12);
	}
	else {
		// changed several data types to save runtime memory
		file->rdwr_long(purchase_time);
		if(file->is_version_less(99, 18)) {
			file->rdwr_byte(dx);
			file->rdwr_byte(dy);
		}
		else {
			file->rdwr_byte(steps);
			file->rdwr_byte(steps_next);
			if(steps_next==old_diagonal_vehicle_steps_per_tile - 1  &&  file->is_loading()) {
				// reset diagonal length (convoi will be reset anyway, if game diagonal is different)
				steps_next = diagonal_vehicle_steps_per_tile - 1;
			}
		}
		sint16 dummy16 = ((16 * (sint16)hoff) / TILE_HEIGHT_STEP);
		file->rdwr_short(dummy16);
		hoff = (sint8)((TILE_HEIGHT_STEP*(sint16)dummy16) / 16);
		file->rdwr_long(speed_limit);
		file->rdwr_enum(direction);
		file->rdwr_enum(previous_direction);
		if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 22)
		{
			for (uint8 i = 0; i < saved_number_of_classes; i++)
			{
				file->rdwr_long(fracht_count[i]);
			}
		}
		else
		{
			file->rdwr_long(total_fracht_count);
		}
		file->rdwr_short(route_index);
		// restore dxdy information
		dx = dxdy[ ribi_t::get_dir(direction)*2];
		dy = dxdy[ ribi_t::get_dir(direction)*2+1];
	}

	// convert steps to position
	if(file->is_version_less(99, 18)) {
		sint8 ddx=get_xoff(), ddy=get_yoff()-hoff;
		sint8 i=1;
		dx = dxdy[ ribi_t::get_dir(direction)*2];
		dy = dxdy[ ribi_t::get_dir(direction)*2+1];

		while(  !is_about_to_hop(ddx+dx*i,ddy+dy*i )  &&  i<16 ) {
			i++;
		}
		i--;
		set_xoff( ddx-(16-i)*dx );
		set_yoff( ddy-(16-i)*dy );
		if(file->is_loading()) {
			if(dx && dy) {
				steps = min( VEHICLE_STEPS_PER_TILE - 1, VEHICLE_STEPS_PER_TILE - 1-(i*16) );
				steps_next = VEHICLE_STEPS_PER_TILE - 1;
			}
			else {
				// will be corrected anyway, if in a convoi
				steps = min( diagonal_vehicle_steps_per_tile - 1, diagonal_vehicle_steps_per_tile - 1-(uint8)(((uint16)i*(uint16)(diagonal_vehicle_steps_per_tile - 1))/8) );
				steps_next = diagonal_vehicle_steps_per_tile - 1;
			}
		}
	}

	// information about the target halt
	if(file->is_version_atleast(88, 7)) {
		bool target_info;
		if(file->is_loading()) {
			file->rdwr_bool(target_info);
			cnv = (convoi_t *)target_info; // will be checked during convoi reassignment
		}
		else {
			target_info = target_halt.is_bound();
			file->rdwr_bool(target_info);
		}
	}
	else {
		if(file->is_loading()) {
			cnv = NULL; // no reservation too
		}
	}
	if( (file->is_version_less(112, 9) && file->get_extended_version()==0) || file->get_extended_version()<14 ) {
		// Standard version number was increased in Extended without porting this change
		koord3d pos_prev(koord3d::invalid);
		pos_prev.rdwr(file);
	}

	if(file->is_version_less(99, 5)) {
		koord3d dummy;
		dummy.rdwr(file); // current pos (is already saved as ding => ignore)
	}
	pos_next.rdwr(file);

	if(file->is_saving()) {
		const char *s = desc->get_name();
		file->rdwr_str(s);
	}
	else {
		char s[256];
		file->rdwr_str(s, lengthof(s));
		desc = vehicle_builder_t::get_info(s);
		if(desc==NULL) {
			desc = vehicle_builder_t::get_info(translator::compatibility_name(s));
		}
		if(desc==NULL) {
			welt->add_missing_paks( s, karte_t::MISSING_VEHICLE );
			dbg->warning("vehicle_t::rdwr_from_convoi()","no vehicle pak for '%s' search for something similar", s);
		}
	}

	if(file->is_saving())
	{
		if (create_dummy_ware)
		{
			// create dummy freight for savegame compatibility
			ware_t ware( desc->get_freight_type() );
			ware.menge = 0;
			ware.set_ziel( halthandle_t() );
			ware.set_zwischenziel( halthandle_t() );
			ware.set_zielpos( get_pos().get_2d() );
			ware.rdwr(file);
		}

		for (uint8 i = 0; i < saved_number_of_classes; i++)
		{
			FOR(slist_tpl<ware_t>, ware, fracht[i])
			{
				ware.rdwr(file);
			}
		}

	}
	else // Loading
	{
		slist_tpl<ware_t> *temp_fracht = new slist_tpl<ware_t>[saved_number_of_classes];

		if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 22)
		{
			for (uint8 i = 0; i < saved_number_of_classes; i++)
			{
				for (uint32 j = 0; j < fracht_count[i]; j++)
				{
					ware_t ware(file);
					if ((desc == NULL || ware.menge > 0) && welt->is_within_limits(ware.get_zielpos()) && ware.get_desc())
					{
						// also add, of the desc is unknown to find matching replacement
						temp_fracht[i].insert(ware);
							// restore in-transit information
							fabrik_t::update_transit(ware, true);
					}
					else if (ware.menge > 0)
					{
						if (ware.get_desc())
						{
							dbg->error("vehicle_t::rdwr_from_convoi()", "%i of %s to %s ignored!", ware.menge, ware.get_name(), ware.get_zielpos().get_str());
						}
						else
						{
							dbg->error("vehicle_t::rdwr_from_convoi()", "%i of unknown to %s ignored!", ware.menge, ware.get_zielpos().get_str());
						}
					}
				}
			}
		}
		else // Older, pre-classes version
		{
			// We must initialise fracht[] here, as only now do we
			// know the correct number of classes when loading an
			// older saved game: for those games, the number of
			// clases defaults to 1 and we have to read this from
			// desc.

			for (int i = 0; i < total_fracht_count; i++)
			{
				// From an older version with no classes, so put all the ware
				// packets in class zero.
				ware_t ware(file);
				if ((desc == NULL || ware.menge > 0) && welt->is_within_limits(ware.get_zielpos()) && ware.get_desc())
				{
					// also add, of the desc is unknown to find matching replacement
					temp_fracht[0].insert(ware);
						// restore in-transit information
						fabrik_t::update_transit(ware, true);
				}
				else if (ware.menge > 0) {
					if (ware.get_desc()) {
						dbg->error("vehicle_t::rdwr_from_convoi()", "%i of %s to %s ignored!", ware.menge, ware.get_name(), ware.get_zielpos().get_str());
					}
					else {
						dbg->error("vehicle_t::rdwr_from_convoi()", "%i of unknown to %s ignored!", ware.menge, ware.get_zielpos().get_str());
					}
				}
			}
		}

		// Work out the number of classes we should have, based
		// on desc (if known) else contained goods (if known),
		// else assume passengers. This must be consistent with the
		// algorithm used to replace missing vehicles.
		if (desc == NULL)
		{
			const goods_desc_t* gd = NULL;
			ware_t ware;
			for (uint8 i = 0; i < saved_number_of_classes; i++)
			{
				if (!temp_fracht[i].empty())
				{
					ware = temp_fracht[i].front();
					gd = ware.get_desc();
					break;
				}
			}
			number_of_classes = gd ? gd->get_number_of_classes() : goods_manager_t::passengers->get_number_of_classes();
		}
		else
		{
			number_of_classes = desc->get_number_of_classes();
		}

		fracht = new slist_tpl<ware_t>[number_of_classes];

		for (uint8 i = 0; i < saved_number_of_classes; i++)
		{
			fracht[min(i, number_of_classes-1)].append_list(temp_fracht[i]);
		}
		delete[]temp_fracht;
	}

	delete[]fracht_count;

	// skip first last info (the convoi will know this better than we!)
	if(file->is_version_less(88, 7)) {
		bool dummy = 0;
		file->rdwr_bool(dummy);
		file->rdwr_bool(dummy);
	}

	// koordinate of the last stop
	if(file->is_version_atleast(99, 15)) {
		// This used to be 2d, now it's 3d.
		if(file->get_extended_version() < 12) {
			if(file->is_saving()) {
				koord last_stop_pos_2d = last_stop_pos.get_2d();
				last_stop_pos_2d.rdwr(file);
			}
			else {
				// loading.  Assume ground level stop (could be wrong, but how would we know?)
				koord last_stop_pos_2d = koord::invalid;
				last_stop_pos_2d.rdwr(file);
				const grund_t* gr = welt->lookup_kartenboden(last_stop_pos_2d);
				if (gr) {
					last_stop_pos = koord3d(last_stop_pos_2d, gr->get_hoehe());
				}
				else {
					// no ground?!?
					last_stop_pos = koord3d::invalid;
				}
			}
		}
		else {
			// current version, 3d
			last_stop_pos.rdwr(file);
		}
	}

	if(file->get_extended_version() >= 1)
	{
		file->rdwr_bool(reversed);
	}
	else
	{
		reversed = false;
	}

	if(  file->is_version_atleast(110, 0)  ) {
		bool hd = has_driven;
		file->rdwr_bool( hd );
		has_driven = hd;
	}
	else {
		if (file->is_loading()) {
			has_driven = false;
		}
	}

	if(  file->is_version_atleast(110, 0) && file->get_extended_version() >=9  ) {
		// Existing values now saved in order to prevent network desyncs
		file->rdwr_short(direction_steps);
		// "Current revenue" is obsolete, but was used in this file version
		sint64 current_revenue = 0;
		file->rdwr_longlong(current_revenue);

		if(file->is_saving())
		{
			uint8 count = pre_corner_direction.get_count();
			file->rdwr_byte(count);
			sint16 dir;
			ITERATE(pre_corner_direction, n)
			{
				dir = pre_corner_direction[n];
				file->rdwr_short(dir);
			}
		}
		else
		{
			pre_corner_direction.clear();
			uint8 count;
			file->rdwr_byte(count);
			for(uint8 n = 0; n < count; n++)
			{
				sint16 dir;
				file->rdwr_short(dir);
				pre_corner_direction.add_to_tail(dir);
			}
		}
	}

	if (file->is_version_atleast(110, 6) && file->get_extended_version() >= 9) {
		file->rdwr_string(current_livery);
	}
	else if(file->is_loading())
	{
		current_livery = "default";
	}

	if (file->is_loading())
	{
		// Set sensible defaults.
		class_reassignments = new uint8[number_of_classes];
		for (uint8 i = 0; i < number_of_classes; i++)
		{
			class_reassignments[i] = i;
		}
	}

	if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 22)
	{
		for (uint8 i = 0; i < saved_number_of_classes; i++)
		{
			uint8 cr = class_reassignments[i];
			file->rdwr_byte(cr);
			if (i < number_of_classes)
			{
				class_reassignments[i] = min(cr, saved_number_of_classes - 1);
			}
		}
	}

	if (file->get_extended_version() >= 15 || (file->get_extended_version() == 14 && file->get_extended_revision() >= 19))
	{
		last_stopped_tile.rdwr(file);
	}
	else
	{
		last_stopped_tile = koord3d::invalid;
	}

	if (file->is_loading())
	{
		leading = last = false;	// dummy, will be set by convoi afterwards
		if (desc) {
			calc_image();

			// full weight after loading
			sum_weight = get_cargo_weight() + desc->get_weight();
		}
		// recalc total freight
		total_freight = 0;

		for (uint8 i = 0; i < number_of_classes; i++)
		{
			FOR(slist_tpl<ware_t>, const& c, fracht[i])
			{
				total_freight += c.menge;
			}
		}
	}
}


uint32 vehicle_t::calc_sale_value() const
{
	// Use float32e8 for reliable and accurate computation
	float32e8_t value ( desc->get_value() );
	if(has_driven)
	{
		// "Drive it off the lot" loss: expressed in mills
		value *= (1000 - welt->get_settings().get_used_vehicle_reduction());
		value /= 1000;
	}
	// General depreciation
	// after 20 years, it has only half value
	// Multiply by .997**number of months
	// Make sure to use OUR version of pow().
	float32e8_t age_in_months = welt->get_current_month() - get_purchase_time();
	static const float32e8_t base_of_exponent(997, 1000);
	value *= pow(base_of_exponent, age_in_months);

	// Convert back to integer
	return (uint32) value;
}

void
vehicle_t::show_info()
{
	if(  cnv != NULL  ) {
		cnv->show_info();
	} else {
		dbg->warning("vehicle_t::show_info()","cnv is null, can't open convoi window!");
	}
}

#if 0
void vehicle_t::info(cbuffer_t & buf) const
{
	if(cnv) {
		cnv->info(buf);
	}
}
#endif

const char *vehicle_t:: is_deletable(const player_t *)
{
	return "Vehicles cannot be removed";
}

ribi_t::ribi vehicle_t::get_next_90direction() const {
	const route_t* route = cnv->get_route();
	if(  route  &&  route_index < route->get_count() - 1u  ) {
		const koord3d pos_next2 = route->at(route_index + 1u);
		return calc_direction(pos_next, pos_next2);
	}
	return ribi_t::none;
}

bool vehicle_t::check_way_constraints(const weg_t &way) const
{
	return missing_way_constraints_t(desc->get_way_constraints(), way.get_way_constraints()).check_next_tile();
}

bool vehicle_t::check_access(const weg_t* way) const
{
	if(get_owner() && get_owner()->is_public_service())
	{
		// The public player can always connect to ways.
		return true;
	}
	const grund_t* const gr = welt->lookup(get_pos());
	const weg_t* const current_way = gr ? gr->get_weg(get_waytype()) : nullptr;
	if(current_way == nullptr)
	{
		return true;
	}
	if (desc->get_engine_type() == vehicle_desc_t::MAX_TRACTION_TYPE && desc->get_topspeed() == 8888)
	{
		// This is a wayobj checker - only allow on ways actually owned by the player or wholly unowned.
		return way && (way->get_owner() == nullptr || (way->get_owner() == get_owner()));
	}
	return way && (way->is_public_right_of_way() || way->get_owner() == nullptr || way->get_owner() == get_owner() || get_owner() == nullptr || way->get_owner() == current_way->get_owner() || way->get_owner()->allows_access_to(get_owner()->get_player_nr()));
}


vehicle_t::~vehicle_t()
{
	if(!welt->is_destroying()) {
		// remove vehicle's marker from the minimap
		minimap_t::get_instance()->calc_map_pixel(get_pos().get_2d());
	}

	delete[] class_reassignments;
	delete[] fracht;
}

void vehicle_t::set_class_reassignment(uint8 original_class, uint8 new_class)
{
	if (original_class >= number_of_classes || new_class >= number_of_classes)
	{
		dbg->error("vehicle_t::set_class_reassignment()", "Attempt to set class out of range");
		return;
	}
	const bool different = class_reassignments[original_class] != new_class;
	class_reassignments[original_class] = new_class;
	if (different)
	{
		for (uint8 i = min(original_class, new_class); i < max(original_class, new_class); i++)
		{
			path_explorer_t::refresh_class_category(get_desc()->get_freight_type()->get_catg(), i);
		}
	}
}


#ifdef MULTI_THREAD
void vehicle_t::display_overlay(int xpos, int ypos) const
{
	if(  cnv  &&  leading  ) {
#else
void vehicle_t::display_after(int xpos, int ypos, bool is_global) const
{
	if(  is_global  &&  cnv  &&  leading  ) {
#endif
		PIXVAL color = 0; // not used, but stop compiler warning about uninitialized
		char tooltip_text[1024];
		tooltip_text[0] = 0;
		uint8 tooltip_display_level = env_t::show_vehicle_states;
		// only show when mouse over vehicle
		if(  welt->get_zeiger()->get_pos()==get_pos()  ) {
			tooltip_display_level = 4;
		}else if(tooltip_display_level ==2 && cnv->get_owner() == welt->get_active_player()) {
			tooltip_display_level = 3;
		}

		// now find out what has happened
		switch(cnv->get_state()) {
			case convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH:
			case convoi_t::WAITING_FOR_CLEARANCE:
			case convoi_t::CAN_START:
			case convoi_t::CAN_START_ONE_MONTH:
				if(tooltip_display_level >=3  ) {
					tstrncpy( tooltip_text, translator::translate("Waiting for clearance!"), lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;

			case convoi_t::EMERGENCY_STOP:

				if (tooltip_display_level > 0) {
					char emergency_stop_time[64];
					cnv->snprintf_remaining_emergency_stop_time(emergency_stop_time, sizeof(emergency_stop_time));
					sprintf(tooltip_text, translator::translate("emergency_stop %s left"), emergency_stop_time/*, lengthof(tooltip_text) */);
					color = color_idx_to_rgb(COL_RED);
				}
				break;

			case convoi_t::LOADING:
				if( tooltip_display_level >=3 )
				{
					char waiting_time[64];
					cnv->snprintf_remaining_loading_time(waiting_time, sizeof(waiting_time));
					if(cnv->get_schedule()->get_current_entry().wait_for_time)
					{
						sprintf( tooltip_text, translator::translate("Waiting for schedule. %s left"), waiting_time);
					}
					else if(cnv->get_loading_limit())
					{
						if(!cnv->is_wait_infinite() && strcmp(waiting_time, "0:00"))
						{
							sprintf( tooltip_text, translator::translate("Loading (%i->%i%%), %s left!"), cnv->get_loading_level(), cnv->get_loading_limit(), waiting_time);
						}
						else
						{
							sprintf( tooltip_text, translator::translate("Loading (%i->%i%%)!"), cnv->get_loading_level(), cnv->get_loading_limit());
						}
					}
					else
					{
						sprintf( tooltip_text, translator::translate("Loading. %s left!"), waiting_time);
					}
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;
			case convoi_t::WAITING_FOR_LOADING_THREE_MONTHS:
			case convoi_t::WAITING_FOR_LOADING_FOUR_MONTHS:
				if (tooltip_display_level > 0) {
					sprintf(tooltip_text, translator::translate("Loading (%i->%i%%) Long Time"), cnv->get_loading_level(), cnv->get_loading_limit());
					color = COL_WARNING;
				}
				break;

			case convoi_t::EDIT_SCHEDULE:
			case convoi_t::ROUTING_2:
			case convoi_t::ROUTE_JUST_FOUND:
				if( tooltip_display_level >=3 ) {
					tstrncpy( tooltip_text, translator::translate("Schedule changing!"), lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_LIGHT_YELLOW);
				}
				break;

			case convoi_t::DRIVING:
				if( tooltip_display_level >=3 ) {
					grund_t const* const gr = welt->lookup(cnv->get_route()->back());
					if(  gr  &&  gr->get_depot()  ) {
						tstrncpy( tooltip_text, translator::translate("go home"), lengthof(tooltip_text) );
						color = color_idx_to_rgb(COL_GREEN);
					}
					else if(  cnv->get_no_load()  ) {
						tstrncpy( tooltip_text, translator::translate("no load"), lengthof(tooltip_text) );
						color = color_idx_to_rgb(COL_GREEN);
					}
				}
				break;

			case convoi_t::LEAVING_DEPOT:
				if( tooltip_display_level >=3 ) {
					tstrncpy( tooltip_text, translator::translate("Leaving depot!"), lengthof(tooltip_text) );
					color = color_idx_to_rgb(COL_GREEN);
				}
				break;

				case convoi_t::REVERSING:
				if( tooltip_display_level >=3 )
				{
					char reversing_time[64];
					cnv->snprintf_remaining_reversing_time(reversing_time, sizeof(reversing_time));
					switch (cnv->get_terminal_shunt_mode()) {
						case convoi_t::rearrange:
						case convoi_t::shunting_loco:
							sprintf(tooltip_text, translator::translate("Shunting. %s left"), reversing_time);
							break;
						case convoi_t::change_direction:
							sprintf(tooltip_text, translator::translate("Changing direction. %s left"), reversing_time);
							break;
						default:
							sprintf(tooltip_text, translator::translate("Reversing. %s left"), reversing_time);
							break;
					}
					color = color_idx_to_rgb(COL_YELLOW);
				}
				break;

			case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS:
			case convoi_t::CAN_START_TWO_MONTHS:
				if (tooltip_display_level > 0) {
					tstrncpy(tooltip_text, translator::translate("clf_chk_stucked"), lengthof(tooltip_text));
					color = COL_WARNING;
				}
				break;

			case convoi_t::NO_ROUTE:
				if (tooltip_display_level > 0) {
					tstrncpy(tooltip_text, translator::translate("clf_chk_noroute"), lengthof(tooltip_text));
					color = color_idx_to_rgb(COL_RED);
				}
				break;

			case convoi_t::NO_ROUTE_TOO_COMPLEX:
				if (tooltip_display_level > 0) {
					tstrncpy(tooltip_text, translator::translate("clf_chk_noroute_too_complex"), lengthof(tooltip_text));
					color = color_idx_to_rgb(COL_RED);
				}
				break;

			case convoi_t::OUT_OF_RANGE:
				if (tooltip_display_level > 0) {
					tstrncpy(tooltip_text, translator::translate("out of range"), lengthof(tooltip_text));
					color = color_idx_to_rgb(COL_RED);
				}
				break;
		}
		if(is_overweight)
		{
			if (tooltip_display_level > 0) {
				sprintf(tooltip_text, translator::translate("Too heavy"), cnv->get_name());
				color = COL_WARNING;
			}
		}

		if(get_waytype() == air_wt && static_cast<const air_vehicle_t *>(this)->runway_too_short)
		{
			if (tooltip_display_level > 0) {
				sprintf(tooltip_text, translator::translate("Runway too short, require %dm"), desc->get_minimum_runway_length());
				color = COL_WARNING;
			}
		}

		if(get_waytype() == air_wt && static_cast<const air_vehicle_t *>(this)->airport_too_close_to_the_edge)
		{
			if (tooltip_display_level > 0) {
				sprintf(tooltip_text, "%s", translator::translate("Airport too close to the edge"));
				color = COL_WARNING;
			}
		}

		// something to show?
		if (!tooltip_text[0] && !env_t::show_cnv_nameplates && !env_t::show_cnv_loadingbar) {
			return;
		}
		const int raster_width = get_current_tile_raster_width();
		get_screen_offset(xpos, ypos, raster_width);
		xpos += tile_raster_scale_x(get_xoff(), raster_width);
		ypos += tile_raster_scale_y(get_yoff(), raster_width) + 14;

		// convoy(line) nameplate
		if (cnv && (env_t::show_cnv_nameplates%4 == 3 || (env_t::show_cnv_nameplates%4 == 2 && cnv->get_owner() == welt->get_active_player())
			|| ((env_t::show_cnv_nameplates%4 == 1 || env_t::show_cnv_nameplates%4 == 2) && welt->get_zeiger()->get_pos() == get_pos()) ))
		{
			char nameplate_text[1024];
			// show the line name, including when the convoy is coupled.
			linehandle_t lh = cnv->get_line();
			if (env_t::show_cnv_nameplates & 4 ) {
				// convoy ID
				sprintf(nameplate_text, "%u", cnv->self.get_id());
			}
			else if (lh.is_bound()) {
				// line name
				tstrncpy(nameplate_text, lh->get_name(), lengthof(nameplate_text));
			}
			else {
				// the convoy belongs to no line -> show convoy name
				tstrncpy(nameplate_text, cnv->get_name(), lengthof(nameplate_text));
			}
			const PIXVAL col_val = color_idx_to_rgb(lh.is_bound() ? cnv->get_owner()->get_player_color1()+3 : cnv->get_owner()->get_player_color1()+1);

			const int width = proportional_string_width(nameplate_text)+7;
			if (ypos > LINESPACE + 32 && ypos + LINESPACE < display_get_clip_wh().yy) {
				const scr_coord_val yoff = LOADINGBAR_HEIGHT + WAITINGBAR_HEIGHT + LINESPACE/2 + 2;
				// line letter code
				if (lh.is_bound() && lh->get_line_color_index()!=255) {
					const PIXVAL linecol = lh->get_line_color();
					xpos += display_line_lettercode_rgb(xpos, ypos-yoff-(LINEASCENT+6)/2, linecol, lh->get_line_lettercode_style(), lh->get_linecode_l(), lh->get_linecode_r(), true);
					xpos += 2;
				}

				if (env_t::show_cnv_nameplates & 4) {
					const int bar_height     = LINEASCENT+4;
					const int bar_width_half = (width+bar_height)/4*2+2;
					scr_coord_val idplate_yoff = ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT - bar_height;
					display_veh_form_wh_clip_rgb(xpos,                  idplate_yoff,     bar_width_half+1, bar_height,   color_idx_to_rgb(COL_WHITE), true, false, vehicle_desc_t::can_be_head, HAS_POWER | BIDIRECTIONAL);
					display_veh_form_wh_clip_rgb(xpos+1,                idplate_yoff+1,   bar_width_half,   bar_height-2, col_val, true, false, vehicle_desc_t::can_be_head, HAS_POWER | BIDIRECTIONAL);
					display_veh_form_wh_clip_rgb(xpos+1+bar_width_half, idplate_yoff,     bar_width_half+1, bar_height,   color_idx_to_rgb(COL_WHITE), true, true,  vehicle_desc_t::can_be_head|vehicle_desc_t::can_be_tail, HAS_POWER | BIDIRECTIONAL);
					display_veh_form_wh_clip_rgb(xpos+1+bar_width_half, idplate_yoff+1,   bar_width_half,   bar_height-2, col_val, true, true,  vehicle_desc_t::can_be_head|vehicle_desc_t::can_be_tail, HAS_POWER | BIDIRECTIONAL);
					if (LINESPACE-LINEASCENT>4) {
						idplate_yoff -= (LINESPACE-LINEASCENT-4);
					}
					display_proportional_clip_rgb(xpos+(bar_width_half*2-width+7+2)/2, idplate_yoff+1, nameplate_text, ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), true);
				}
				else {
					// line/convoy name
					display_ddd_proportional_clip(xpos, ypos-yoff, width, 0, col_val, is_dark_color(col_val) ? color_idx_to_rgb(COL_WHITE) : color_idx_to_rgb(COL_BLACK), nameplate_text, true);
					// (*)display_ddd_proportional_clip's height is LINESPACE/2+1+1
				}
			}
		}

		// loading bar
		int extra_y = 0; // yoffset from the default tooltip position
		sint64 waiting_time_per_month = 0;
		uint8 waiting_bar_col = COL_APRICOT;
		if (cnv && (env_t::show_cnv_loadingbar == 3 || (env_t::show_cnv_loadingbar == 2 && cnv->get_owner() == welt->get_active_player())
			|| ((env_t::show_cnv_loadingbar == 1 || env_t::show_cnv_loadingbar == 2) && welt->get_zeiger()->get_pos() == get_pos()) ))
		{
			switch (cnv->get_state()) {
				case convoi_t::LOADING:
					waiting_time_per_month = int(0.9 + cnv->calc_remaining_loading_time()*200 / welt->ticks_per_world_month);
					break;
				case convoi_t::REVERSING:
					waiting_time_per_month = int(0.9 + cnv->get_wait_lock()*200 / welt->ticks_per_world_month);
					break;
				case convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS:
				case convoi_t::CAN_START_TWO_MONTHS:
					waiting_time_per_month = 100;
					waiting_bar_col = COL_ORANGE;
					if (skinverwaltung_t::alerts) {
						display_color_img(skinverwaltung_t::alerts->get_image_id(3), xpos - 15, ypos - D_FIXED_SYMBOL_WIDTH, 0, false, true);
					}
					break;
				case convoi_t::NO_ROUTE:
				case convoi_t::NO_ROUTE_TOO_COMPLEX:
				case convoi_t::OUT_OF_RANGE:
					waiting_time_per_month = 100;
					waiting_bar_col = COL_RED+1;
					if (skinverwaltung_t::pax_evaluation_icons) {
						display_color_img(skinverwaltung_t::pax_evaluation_icons->get_image_id(4), xpos - 15, ypos - D_FIXED_SYMBOL_WIDTH, 0, false, true);
					}
					break;
				default:
					break;
			}

			display_ddd_box_clip_rgb(xpos-2, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT, LOADINGBAR_WIDTH+2, LOADINGBAR_HEIGHT, color_idx_to_rgb(MN_GREY2), color_idx_to_rgb(MN_GREY0));
			sint32 colored_width = cnv->get_loading_level() > 100 ? 100 : cnv->get_loading_level();
			if (cnv->get_loading_limit() && cnv->get_state() == convoi_t::LOADING) {
				display_fillbox_wh_clip_rgb(xpos - 1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + 1, cnv->get_loading_limit(), LOADINGBAR_HEIGHT-2, COL_IN_TRANSIT, true);
			}
			else if (cnv->get_loading_limit()) {
				display_blend_wh_rgb(xpos-1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + 1, cnv->get_loading_limit(), LOADINGBAR_HEIGHT-2, COL_IN_TRANSIT, 60);
			}
			else {
				display_blend_wh_rgb(xpos-1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + 1, LOADINGBAR_WIDTH, LOADINGBAR_HEIGHT-2, color_idx_to_rgb(MN_GREY2), colored_width ? 65 : 40);
			}
			display_cylinderbar_wh_clip_rgb(xpos-1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + extra_y + 1, colored_width, LOADINGBAR_HEIGHT-2, color_idx_to_rgb(COL_GREEN-1), true);

			// overcrowding
			if (cnv->get_overcrowded() && skinverwaltung_t::pax_evaluation_icons) {
				display_color_img(skinverwaltung_t::pax_evaluation_icons->get_image_id(1), xpos + LOADINGBAR_WIDTH + 5, ypos - D_FIXED_SYMBOL_WIDTH, 0, false, true);
			}

			extra_y += LOADINGBAR_HEIGHT;

			// waiting gauge
			if (waiting_time_per_month) {
				colored_width = waiting_time_per_month > 100 ? 100 : waiting_time_per_month;
				display_ddd_box_clip_rgb(xpos - 2, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + extra_y, colored_width + 2, WAITINGBAR_HEIGHT, color_idx_to_rgb(MN_GREY2), color_idx_to_rgb(MN_GREY0));
				display_cylinderbar_wh_clip_rgb(xpos - 1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + extra_y + 1, colored_width, WAITINGBAR_HEIGHT - 2, color_idx_to_rgb(waiting_bar_col), true);
				if (waiting_time_per_month > 100) {
					colored_width = waiting_time_per_month-100;
					display_cylinderbar_wh_clip_rgb(xpos - 1, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + extra_y + 1, colored_width, WAITINGBAR_HEIGHT - 2, color_idx_to_rgb(waiting_bar_col-2), true);
				}
				extra_y += WAITINGBAR_HEIGHT;
			}
		}

		// normal tooltip
		if (tooltip_text[0]) {
			const int width = proportional_string_width(tooltip_text) + 7;
			if (ypos > LINESPACE + 32 && ypos + LINESPACE < display_get_clip_wh().yy) {
				display_ddd_proportional_clip(xpos, ypos - LOADINGBAR_HEIGHT - WAITINGBAR_HEIGHT + LINESPACE/2+2 + extra_y, width, 0, color, color_idx_to_rgb(COL_BLACK), tooltip_text, true);
				// (*)display_ddd_proportional_clip's height is LINESPACE/2+1+1
			}
		}
	}
}

// BG, 06.06.2009: added
void vehicle_t::finish_rd()
{
}

// BG, 06.06.2009: added
void vehicle_t::before_delete()
{
}

