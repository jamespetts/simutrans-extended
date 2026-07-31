/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "road_vehicle.h"

#include "simroadtraffic.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../boden/wege/strasse.h"
#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../descriptor/citycar_desc.h"
#include "../obj/crossing.h"
#include "../obj/roadsign.h"
#include "../player/simplay.h"
#include "../simware.h"
#include "../simworld.h"
#include "../simconvoi.h"
#include "../simhalt.h"
#include "../simmesg.h"

#include "../utils/cbuffer_t.h"


uint32 road_vehicle_t::get_max_speed() { return cnv->get_min_top_speed(); }

road_vehicle_t::road_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle, pos, desc, player)
#else
    vehicle_t(pos, desc, player)
#endif
{
	cnv = cn;
	is_checker = false;
	drives_on_left = welt->get_settings().is_drive_left();
	reset_measurements();
}

road_vehicle_t::road_vehicle_t() :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle)
#else
    vehicle_t()
#endif
{
	// This is blank - just used for the automatic road finder.
	cnv = NULL;
	is_checker = true;
}

road_vehicle_t::road_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle)
#else
    vehicle_t()
#endif
{
	rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_leading) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL)
		{
			bool empty = true;
			const goods_desc_t* gd = NULL;
			ware_t ware;
			for (uint8 i = 0; i < number_of_classes; i++)
			{
				if (!fracht[i].empty())
				{
					ware = fracht[i].front();
					gd = ware.get_desc();
					empty = false;
					break;
				}
			}
			if (!gd)
			{
				// Important: This must be the same default ware as used in rdwr_from_convoi.
				gd = goods_manager_t::passengers;
			}

			dbg->warning("road_vehicle_t::road_vehicle_t()","try to find a fitting vehicle for %s.", gd->get_name() );
			desc = vehicle_builder_t::get_best_matching(road_wt, 0, (empty ? 0 : 50), is_leading?50:0, speed_to_kmh(speed_limit), gd, true, last_desc, is_last );
			if(desc) {
				DBG_MESSAGE("road_vehicle_t::road_vehicle_t()","replaced by %s",desc->get_name());
				// still wrong load ...
				calc_image();
			}
			if(!empty  &&  fracht[0].front().menge == 0) {
				// this was only there to find a matching vehicle
				fracht[0].remove_first();
			}
		}
		if(  desc  ) {
			last_desc = desc;
		}
	}
	fix_class_accommodations();
	is_checker = false;
	drives_on_left = welt->get_settings().is_drive_left();
	reset_measurements();
}


void road_vehicle_t::rotate90()
{
	vehicle_t::rotate90();
	calc_disp_lane();
}


void road_vehicle_t::calc_disp_lane()
{
	// driving in the back or the front
	ribi_t::ribi test_dir = welt->get_settings().is_drive_left() ? ribi_t::northeast : ribi_t::southwest;
	disp_lane = get_direction() & test_dir ? 1 : 3;
}


// need to reset halt reservation (if there was one)
route_t::route_result_t road_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route)
{
	assert(cnv);
	// free target reservation
	drives_on_left = welt->get_settings().is_drive_left();	// reset driving settings
	if(leading   &&  previous_direction!=ribi_t::none  &&  cnv  &&  target_halt.is_bound() ) {
		// now reserve our choice (beware: might be longer than one tile!)
		for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
			target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
		}
	}
	target_halt = halthandle_t(); // no block reserved
	const uint32 routing_weight = cnv != NULL ? cnv->get_highest_axle_load() : ((get_sum_weight() + 499) / 1000);
	route_t::route_result_t r = route->calc_route(welt, start, ziel, this, max_speed, routing_weight, is_tall, cnv->get_tile_length(), SINT64_MAX_VALUE, cnv->get_weight_summary().weight / 1000 );
	if(  r == route_t::valid_route_halt_too_short  ) {
		cbuffer_t buf;
		buf.printf( translator::translate("Vehicle %s cannot choose because stop too short!"), cnv->get_name());
		welt->get_message()->add_message( (const char *)buf, ziel.get_2d(), message_t::traffic_jams, PLAYER_FLAG | cnv->get_owner()->get_player_nr(), cnv->front()->get_base_image() );
	}
	return r;
}


bool road_vehicle_t::check_next_tile(const grund_t *bd) const
{
	strasse_t *str=(strasse_t *)bd->get_weg(road_wt);
	if(str==NULL || ((str->get_max_speed()==0 && speed_limit < INT_MAX)))
	{
		return false;
	}
	if(!is_checker)
	{
		bool electric = cnv != NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric;
		if(electric  &&  !str->is_electrified()) {
			return false;
		}
	}
	// check for signs
	if(str->has_sign()) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(rs!=NULL) {
			const roadsign_desc_t* rs_desc = rs->get_desc();
			if(get_desc() && rs_desc->get_min_speed()>0  &&  rs_desc->get_min_speed() > kmh_to_speed(get_desc()->get_topspeed())  )
			{
				return false;
			}
			if(  rs_desc->is_private_way()  &&  (rs->get_player_mask() & (1<<get_owner_nr()) ) == 0  ) {
				// private road
				return false;
			}
			// do not search further for a free stop beyond here
			if(target_halt.is_bound() && cnv && cnv->is_waiting() && (rs_desc->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA)) {
				return false;
			}
		}
	}
	if(!is_checker)
	{
		return check_access(str) && check_way_constraints(*str);
	}
	return check_access(str);
}



// how expensive to go here (for way search)
int road_vehicle_t::get_cost(const grund_t *gr, const sint32 max_speed, ribi_t::ribi from)
{
	// first favor faster ways
	const weg_t *w=gr->get_weg(road_wt);
	if(!w) {
		return 0xFFFF;
	}

	// max_speed?
	bool electric = cnv != NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric;
	sint32 max_tile_speed = w->get_max_speed(electric);

	// add cost for going (with maximum speed, cost is 1)
	sint32 costs = (max_speed <= max_tile_speed) ? 10 : 40 - (30 * max_tile_speed) / max_speed;

	if (desc->get_override_way_speed())
	{
		costs = 1;
	}

	// Take traffic congestion into account in determining the cost. Use the same formula as for private cars.
	const uint32 congestion_percentage = w->get_congestion_percentage();
	if (congestion_percentage)
	{
		costs += (costs * congestion_percentage) / 200;
	}

	// effect of slope
	if(  gr->get_weg_hang()!=0  ) {
		// check if the slope is upwards, relative to the previous tile
		// 75 hardcoded, see get_cost_upslope()
		costs += 75 * get_sloping_upwards( gr->get_weg_hang(), from );
	}

	// It is now difficult to calculate here whether the vehicle is overweight, so do this in the route finder instead.

	if(w->is_diagonal())
	{
		// Diagonals are a *shorter* distance.
		costs = (costs * 5) / 7; // was: costs /= 1.4
	}
	return costs;
}


// this routine is called by find_route, to determined if we reached a destination
bool road_vehicle_t:: is_target(const grund_t *gr, const grund_t *prev_gr)
{
	//  just check, if we reached a free stop position of this halt
	if(gr->is_halt()  &&  gr->get_halt()==target_halt  &&  target_halt->get_empty_lane(gr,cnv->self)!=0) {
		// now we must check the predecessor => try to advance as much as possible
		if(prev_gr!=NULL) {
			const koord3d dir=gr->get_pos()-prev_gr->get_pos();
			ribi_t::ribi ribi = ribi_type(dir);
			if(  gr->get_weg(get_waytype())->get_ribi_maske() & ribi  ) {
				// one way sign wrong direction
				return false;
			}
			grund_t *to;
			if(  !gr->get_neighbour(to,road_wt,ribi)  ||  !(to->get_halt()==target_halt)  ||  (gr->get_weg(get_waytype())->get_ribi_maske() & ribi_type(dir))!=0  ||  target_halt->get_empty_lane(to,cnv->self)==0  ) {
				// end of stop: Is it long enough?
				uint16 tiles = cnv->get_tile_length();
				uint8 empty_lane = 3;
				while(  tiles>1  ) {
					if(  !gr->get_neighbour(to,get_waytype(),ribi_t::backward(ribi))  ||  !(to->get_halt()==target_halt)  ||  (empty_lane &= target_halt->get_empty_lane(to,cnv->self))==0  ) {
						return false;
					}
					gr = to;
					tiles --;
				}
				return true;
			}
			// can advance more
			return false;
		}
//DBG_MESSAGE("is_target()","success at %i,%i",gr->get_pos().x,gr->get_pos().y);
//		return true;
	}
	return false;
}


// to make smaller steps than the tile granularity, we have to use this trick
void road_vehicle_t::get_screen_offset( int &xoff, int &yoff, const sint16 raster_width, bool prev_based ) const
{
	vehicle_base_t::get_screen_offset( xoff, yoff, raster_width );

	// eventually shift position to take care of overtaking
	if(cnv) {
		sint8 tiles_overtaking = prev_based ? cnv->get_prev_tiles_overtaking() : cnv->get_tiles_overtaking();
		if(  tiles_overtaking>0  ) {
			/* This means the convoy is overtaking other vehicles. */
			xoff += tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][0], raster_width);
			yoff += tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][1], raster_width);
		}
		else if(  tiles_overtaking<0  ) {
			/* This means the convoy is overtaken by other vehicles. */
			xoff -= tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][0], raster_width)/5;
			yoff -= tile_raster_scale_x(overtaking_base_offsets[ribi_t::get_dir(get_direction())][1], raster_width)/5;
		}
	}
}


// chooses a route at a choose sign; returns true on success
bool road_vehicle_t::choose_route(sint32 &restart_speed, ribi_t::ribi start_direction, uint16 index)
{
	// are we heading to a target?
	route_t *rt = cnv->access_route();
	target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
	if(  target_halt.is_bound()  ) {

		// since convois can long than one tile, check is more difficult
		bool can_go_there = true;
		bool original_route = (rt->back() == cnv->get_schedule()->get_current_entry().pos);
		for(  uint32 length=0;  can_go_there  &&  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
			if(  grund_t *gr = welt->lookup( rt->at( rt->get_count()-length-1) )  ) {
				if (gr->get_halt().is_bound()) {
					can_go_there &= target_halt->is_reservable( gr, cnv->self );
				}
				else {
					// if this is the original stop, it is too short!
					can_go_there |= original_route;
				}
			}
		}
		if(  can_go_there  ) {
			// then reserve it ...
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( rt->at( rt->get_count()-length-1) ), cnv->self );
			}
		}
		else {
			// cannot go there => need slot search

			// if we fail, we will wait in a step, much more simulation friendly
			if(!cnv->is_waiting()) {
				restart_speed = -1;
				target_halt = halthandle_t();
				return false;
			}

			// check if there is a free position
			// this is much faster than waysearch
			if(  !target_halt->find_free_position(road_wt,cnv->self,obj_t::road_vehicle  )) {
				restart_speed = 0;
				target_halt = halthandle_t();
				return false;
			}

			// now it make sense to search a route
			route_t target_rt;
			koord3d next3d = rt->at(index);
			if(  !target_rt.find_route(welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), start_direction, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, 33, cnv->has_tall_vehicles())  ) {
				// nothing empty or not route with less than 33 tiles
				target_halt = halthandle_t();
				restart_speed = 0;
				return false;
			}

			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<target_rt.get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( target_rt.at( target_rt.get_count()-length-1) ), cnv->self );
			}
			rt->remove_koord_from( index );
			rt->append( &target_rt );
		}
	}
	return true;
}


bool road_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8 second_check_count)
{
	// check for traffic lights (only relevant for the first car in a convoi)
	assert(cnv->get_state() != convoi_t::ROUTING_1 && cnv->get_state() != convoi_t::ROUTING_2);
	if(  leading  ) {
		// no further check, when already entered a crossing (to allow leaving it)
		if(  !second_check_count  ) {
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  gr_current  &&  gr_current->ist_uebergang()  ) {
					return true;
				}
			}
			// always allow to leave traffic lights (avoid vehicles stuck on crossings directly after though)
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  const roadsign_t *rs = gr_current->find<roadsign_t>()  ) {
					if(  rs  &&  rs->get_desc()->is_traffic_light()  &&  !gr->ist_uebergang()  ) {
						return true;
					}
				}
			}
		}

		assert(gr);

		const strasse_t *str = (strasse_t *)gr->get_weg(road_wt);
		if(  !str  ||  gr->get_top() > 250  ) {
			// too many cars here or no street
			if(  !second_check_count  &&  !str) {
				cnv->suche_neue_route();
			}
			return false;
		}

		if (gr->is_height_restricted() && cnv->has_tall_vehicles())
		{
			cnv->suche_neue_route();
			return false;
		}

		route_t const& r = *cnv->get_route();
		const bool route_index_beyond_end_of_route = route_index >= r.get_count();

		// first: check roadsigns
		const roadsign_t *rs = NULL;
		if(  str->has_sign()  ) {
			rs = gr->find<roadsign_t>();

			if (rs && (!route_index_beyond_end_of_route)) {
				// since at the corner, our direction may be diagonal, we make it straight
				uint8 direction90 = ribi_type(get_pos(), pos_next);

				if (rs->get_desc()->is_traffic_light() && (rs->get_dir()&direction90) == 0) {
					// wait here
					restart_speed = 16;
					return false;
				}
				// check, if we reached a choose point
				else if(route_index + 1u < r.get_count())
				{
					// route position after road sign
					const koord3d pos_next_next = r.at(route_index + 1u);
					// since at the corner, our direction may be diagonal, we make it straight
					direction90 = ribi_type( pos_next, pos_next_next );

					if(  rs->is_free_route(direction90)  &&  !target_halt.is_bound()  ) {
						if(  second_check_count  ) {
							return false;
						}
						if(  !choose_route( restart_speed, direction90, route_index )  ) {
							return false;
						}
					}
				}
			}
		}

		// At a crossing, decide whether the convoi should go on passing lane.
		// side road -> main road from passing lane side: vehicle should enter passing lane on main road.
		next_lane = 0;
		if(  (str->get_ribi_unmasked() == ribi_t::all  ||  ribi_t::is_threeway(str->get_ribi_unmasked()))  &&  str->get_overtaking_mode() <= oneway_mode  ) {
			const strasse_t* str_prev = route_index == 0 ? NULL : (strasse_t *)welt->lookup(r.at(route_index - 1u))->get_weg(road_wt);
			const grund_t* gr_next = route_index < r.get_count() - 1u ? welt->lookup(r.at(route_index + 1u)) : NULL;
			const strasse_t* str_next = gr_next ? (strasse_t*)gr_next -> get_weg(road_wt) : NULL;
			if(str_prev && str_next && str_prev->get_overtaking_mode() > oneway_mode  && str_next->get_overtaking_mode() <= oneway_mode) {
				const koord3d pos_next2 = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
				if(  (!welt->get_settings().is_drive_left()  &&  ribi_t::rotate90l(get_90direction()) == calc_direction(pos_next,pos_next2))  ||  (welt->get_settings().is_drive_left()  &&  ribi_t::rotate90(get_90direction()) == calc_direction(pos_next,pos_next2))  ) {
					// next: enter passing lane.
					next_lane = 1;
				}
			}
		}

		const strasse_t* current_str = (strasse_t*)(welt->lookup(get_pos())->get_weg(road_wt));
		if( current_str && current_str->get_overtaking_mode()<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  ) {
			next_lane = -1;
		}

		vehicle_base_t *obj = NULL;
		uint32 test_index = route_index;

		// way should be clear for overtaking: we checked previously
		// calculate new direction
		koord3d next = (route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next);
		ribi_t::ribi curr_direction   = get_direction();
		ribi_t::ribi curr_90direction = calc_direction(get_pos(), pos_next);
		ribi_t::ribi next_direction   = calc_direction(get_pos(), next);
		ribi_t::ribi next_90direction = calc_direction(pos_next, next);
		obj = get_blocking_vehicle(gr, cnv, curr_direction, next_direction, next_90direction, NULL, next_lane);

		//If this convoi is overtaking, the convoi must avoid a head-on crash.
		if(  cnv->is_overtaking()  ){
			while(  test_index < route_index + 2u && test_index < r.get_count()  ){
				grund_t *grn = welt->lookup(r.at(test_index));
				if(  !grn  ) {
					cnv->suche_neue_route();
					return false;
				}
				for(  uint8 pos=1;  pos < grn->get_top();  pos++  ){
					if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(grn->obj_bei(pos))  ){
						if(  v->get_typ()==obj_t::pedestrian  ) {
							continue;
						}
						ribi_t::ribi other_direction=255;
						if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
							//ignore ourself
							if(  cnv == at->get_convoi()  ||  at->get_convoi()->is_overtaking()  ){
								continue;
							}
							other_direction = at->get_90direction();
						}
						//check for city car
						else if(  private_car_t* const caut = obj_cast<private_car_t>(v)  ) {
							if(  caut->is_overtaking()  ) {
								continue;
							}
							other_direction = v->get_90direction();
						}
						if(  other_direction != 255  ){
							//There is another car. We have to check if this convoi is facing or not.
							ribi_t::ribi this_direction = 0;
							if(  test_index-route_index==0  ) this_direction = get_90direction();
							if(  test_index-route_index==1  ) this_direction = get_next_90direction();
							if(  ribi_t::reverse_single(this_direction) == other_direction  ) {
								//printf("%s: crash avoid. (%d,%d)\n", cnv->get_name(), get_pos().x, get_pos().y);
								cnv->set_tiles_overtaking(0);
							}
						}
					}
				}
				test_index++;
			}
		}

		test_index = route_index + 1u; //reset test_index
		// we have to assume the lane that this vehicle goes in the intersection.
		sint8 lane_of_the_tile = next_lane;
		overtaking_mode_t mode_of_start_point = str->get_overtaking_mode();
		// check exit from crossings and intersections, allow to proceed after 4 consecutive
		while(  !obj   &&  str->is_crossing()  &&  test_index < r.get_count()  &&  test_index < route_index + 4u  ) {
			if(  str->is_crossing()  ) {
				crossing_t* cr = gr->find<crossing_t>(2);
				if(  !cr->request_crossing(this)  ) {
					restart_speed = 0;
					return false;
				}
			}

			// test next position
			gr = welt->lookup(r.at(test_index));
			if(  !gr  ) {
				// ground not existent (probably destroyed)
				if(  !second_check_count  ) {
					cnv->suche_neue_route();
				}
				return false;
			}

			str = (strasse_t *)gr->get_weg(road_wt);

			if(  !str  ) {
				// road not existent (probably destroyed)
				if(  !second_check_count  ) {
					cnv->suche_neue_route();
				}
				return false;
			}

			if(  gr->get_top() > 250  ) {
				// too many cars here or no street
				return false;
			}

			if(  mode_of_start_point<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  ) lane_of_the_tile = -1;

			// check cars
			curr_direction   = next_direction;
			curr_90direction = next_90direction;
			if(  test_index + 1u < r.get_count()  ) {
				next                 = r.at(test_index + 1u);
				next_direction   = calc_direction(r.at(test_index - 1u), next);
				next_90direction = calc_direction(r.at(test_index),      next);
				obj = get_blocking_vehicle(gr, cnv, curr_direction, next_direction, next_90direction, NULL, lane_of_the_tile);
			}
			else {
				next                 = r.at(test_index);
				next_90direction = calc_direction(r.at(test_index - 1u), next);
				if(  curr_direction == next_90direction  ||  !gr->is_halt()  ) {
					// check cars but allow to enter intersection if we are turning even when a car is blocking the halt on the last tile of our route
					// preserves old bus terminal behaviour
					obj = get_blocking_vehicle(gr, cnv, curr_direction, next_90direction, ribi_t::none, NULL, lane_of_the_tile);
				}
			}

			// first: check roadsigns
			const roadsign_t *rs = NULL;
			if (str->has_sign()) {
				rs = gr->find<roadsign_t>();
				const ribi_t::ribi dir = rs->get_dir();
				const bool is_traffic_light = rs->get_desc()->is_traffic_light();
				route_t const& r = *cnv->get_route();

				if (is_traffic_light || gr->get_weg(get_waytype())->get_ribi_maske() & dir)
				{
					uint8 direction90 = ribi_type(get_pos(), pos_next);
					if (rs && (!route_index_beyond_end_of_route)) {
						// Check whether if we reached a choose point
						if (rs->get_desc()->is_choose_sign())
						{
							// route position after road sign
							const koord3d pos_next_next = r.at(route_index + 1u);
							// since at the corner, our direction may be diagonal, we make it straight
							direction90 = ribi_type(pos_next, pos_next_next);

							if (  rs->is_free_route(direction90) && !target_halt.is_bound()  ) {
								if (  second_check_count  ) {
									return false;
								}
								if (!choose_route(restart_speed, direction90, route_index)) {
									return false;
								}
							}

							const route_t *rt = cnv->get_route();
							// is our target occupied?
							target_halt = haltestelle_t::get_halt(rt->back(), get_owner());
							if (target_halt.is_bound()) {
								// since convois can long than one tile, check is more difficult
								bool can_go_there = true;
								for (uint32 length = 0; can_go_there && length < cnv->get_tile_length() && length + 1 < rt->get_count(); length++) {
									can_go_there &= target_halt->is_reservable(welt->lookup(rt->at(rt->get_count() - length - 1)), cnv->self);
								}
								if (can_go_there) {
									// then reserve it ...
									for (uint32 length = 0; length < cnv->get_tile_length() && length + 1 < rt->get_count(); length++) {
										target_halt->reserve_position(welt->lookup(rt->at(rt->get_count() - length - 1)), cnv->self);
									}
								}
								else {
									// cannot go there => need slot search

									// if we fail, we will wait in a step, much more simulation friendly
									if (!cnv->is_waiting()) {
										restart_speed = -1;
										target_halt = halthandle_t();
										return false;
									}

									// check if there is a free position
									// this is much faster than waysearch
									if (!target_halt->find_free_position(road_wt, cnv->self, obj_t::road_vehicle)) {
										restart_speed = 0;
										target_halt = halthandle_t();
										//DBG_MESSAGE("road_vehicle_t::can_enter_tile()","cnv=%d nothing free found!",cnv->self.get_id());
										return false;
									}

									// now it make sense to search a route
									route_t target_rt;
									koord3d next3d = r.at(test_index);
									if (!target_rt.find_route(welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), curr_90direction, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, 33, cnv->has_tall_vehicles(), route_t::choose_signal)) {
										// nothing empty or not route with less than 33 tiles
										target_halt = halthandle_t();
										restart_speed = 0;
										return false;
									}

									// now reserve our choice (beware: might be longer than one tile!)
									for (uint32 length = 0; length < cnv->get_tile_length() && length + 1 < target_rt.get_count(); length++) {
										target_halt->reserve_position(welt->lookup(target_rt.at(target_rt.get_count() - length - 1)), cnv->self);
									}
									cnv->update_route(test_index, target_rt);
								}
							}
						}
					}
				}
			}
			else {
				rs = NULL;
			}

			test_index++;
		}

		if(  obj  &&  test_index > route_index + 1u  &&  !str->is_crossing()  ) {
			// found a car blocking us after checking at least 1 intersection or crossing
			// and the car is in a place we could stop. So if it can move, assume it will, so we will too.
			// but check only upto 8 cars ahead to prevent infinite recursion on roundabouts.
			if(  second_check_count >= 8  ) {
				return false;
			}
			if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
				convoi_t* const ocnv = car->get_convoi();
				sint32 dummy;
				if(  ocnv->get_state() == convoi_t::DRIVING && ocnv->front()->get_route_index() < ocnv->get_route()->get_count()  &&  ocnv->front()->can_enter_tile(dummy, second_check_count + 1 )  ) {
					return true;
				}
			}
		}

		// stuck message ...
		if(  obj  &&  !second_check_count  ) {
			// Process is different whether the road is for one-way or two-way
			sint8 overtaking_mode = str->get_overtaking_mode();
			if(  overtaking_mode <= oneway_mode  ) {
				// road is one-way.
				// The overtaking judge method itself works only when test_index==route_index+1, that means the front tile is not an intersection.
				// However, with halt_mode we want to simulate a bus terminal. Overtaking in a intersection is essential. So we make a exception to the test_index==route_index+1 condition, although it is not clear that this exception is safe or not!
				if(  (test_index == route_index + 1u) || (test_index == route_index + 2u  &&  overtaking_mode == halt_mode)  ) {
					// no intersections or crossings, we might be able to overtake this one ...
					if(  obj->get_overtaker()  ) {
						// not overtaking/being overtake: we need to make a more thought test!
						if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
							convoi_t* const ocnv = car->get_convoi();
							// yielding vehicle should not be overtaken by the vehicle whose maximum speed is same.
							bool yielding_factor = true;
							if(  ocnv->get_yielding_quit_index() != -1  &&  this->get_speed_limit() - ocnv->get_min_top_speed() < kmh_to_speed(10)  ) {
								yielding_factor = false;
							}
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking()  &&  !other_lane_blocked()  &&  yielding_factor  &&  cnv->can_overtake( ocnv, (ocnv->is_loading() ? 0 : ocnv->get_akt_speed()), ocnv->get_length_in_steps()+ocnv->front()->get_steps())  ) {
								return true;
							}
							strasse_t *str=(strasse_t *)gr->get_weg(road_wt);
							sint32 cnv_max_speed;
							sint32 other_max_speed;
							const bool electric = cnv->needs_electrification();
							if (desc->get_override_way_speed())
							{
								cnv_max_speed = cnv->get_min_top_speed() * kmh_to_speed(1);
							}
							else
							{
								cnv_max_speed = (int)fmin(cnv->get_min_top_speed(), str->get_max_speed(electric) * kmh_to_speed(1));
							}
							if (ocnv->front()->get_desc()->get_override_way_speed())
							{
								other_max_speed = ocnv->get_min_top_speed() * kmh_to_speed(1);
							}
							else
							{
								other_max_speed = (int)fmin(ocnv->get_min_top_speed(), str->get_max_speed(electric) * kmh_to_speed(1));
							}
							if(  cnv->is_overtaking() && kmh_to_speed(10) <  cnv_max_speed - other_max_speed  ) {
								// If the convoi is on passing lane and there is slower convoi in front of this, this convoi request the slower to go to traffic lane.
								ocnv->set_requested_change_lane(true);
							}
							//For the case that the faster convoi is on traffic lane.
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking() && kmh_to_speed(10) <  cnv_max_speed - other_max_speed  ) {
								if(  vehicle_base_t* const br = car->other_lane_blocked()  ) {
									if(  road_vehicle_t const* const blk = obj_cast<road_vehicle_t>(br)  ) {
										if(  car->get_direction() == blk->get_direction() && abs(car->get_convoi()->get_min_top_speed() - blk->get_convoi()->get_min_top_speed()) < kmh_to_speed(5)  ){
											//same direction && (almost) same speed vehicle exists.
											ocnv->yield_lane_space();
										}
									}
								}
							}
						}
						else if(  private_car_t* const caut = obj_cast<private_car_t>(obj)  ) {
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking()  &&  !other_lane_blocked()  &&  cnv->can_overtake(caut, caut->get_current_speed(), VEHICLE_STEPS_PER_TILE)  ) {
								return true;
							}
						}
					}
				}
				// we have to wait ...
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					restart_speed = 0;
					cnv->reset_waiting();
					if(  cnv->is_overtaking()  &&  other_lane_blocked() == NULL  ) {
						cnv->set_tiles_overtaking(0);
					}
				}
				else {
					restart_speed = (cnv->get_akt_speed()*3)/4;
				}
			}
			else if(  overtaking_mode <= twoway_mode) {
				// road is two-way and overtaking is allowed on the stricter condition.
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					// Not giving stuck messages here is a problem as there might be a circular jam, which is
					// actually a common case. Do not reset waiting here.
					restart_speed = 0;
					//cnv->set_tiles_overtaking(0);
					//cnv->reset_waiting();
				}
				else {
					if(  test_index == route_index + 1u  ) {
						// no intersections or crossings, we might be able to overtake this one ...
						overtaker_t *over = obj->get_overtaker();
						if(  over  &&  !over->is_overtaken()  ) {
							if(  over->is_overtaking()  ) {
								// otherwise we would stop every time being overtaken
								return true;
							}
							// not overtaking/being overtake: we need to make a more thought test!
							if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
								convoi_t* const ocnv = car->get_convoi();
								if(  cnv->can_overtake( ocnv, (ocnv->is_loading() ? 0 : over->get_max_power_speed()), ocnv->get_length_in_steps()+ocnv->front()->get_steps())  ) {
									return true;
								}
							}
							else if(  private_car_t* const caut = obj_cast<private_car_t>(obj)  ) {
								if(  cnv->can_overtake(caut, caut->get_desc()->get_topspeed(), VEHICLE_STEPS_PER_TILE)  ) {
									return true;
								}
							}
						}
					}
					// we have to wait ...
					restart_speed = (cnv->get_akt_speed()*3)/4;
					//cnv->set_tiles_overtaking(0);
				}
			}
			else {
				// lane change is prohibited.
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					restart_speed = 0;
					//cnv->set_tiles_overtaking(0);
					cnv->reset_waiting();
				}
				else {
					// we have to wait ...
					restart_speed = (cnv->get_akt_speed()*3)/4;
					//cnv->set_tiles_overtaking(0);
				}
			}
		}

		const koord3d pos_next2 = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
		// We have to calculate offset.
		uint32 offset = 0;
		if( !route_index_beyond_end_of_route && str->get_pos()!=r.at(route_index)  ){
			for(uint32 test_index = route_index+1u; test_index<r.get_count(); test_index++){
				offset += 1;
				if(  str->get_pos()==r.at(test_index)  ) break;
			}
		}
		// If this vehicle is on passing lane and the next tile prohibites overtaking, this vehicle must wait until traffic lane become safe.
		// When condition changes, overtaking should be quitted once.
		if(  (cnv->is_overtaking()  &&  str->get_overtaking_mode()==prohibited_mode)  ||  (cnv->is_overtaking()  &&  str->get_overtaking_mode()>oneway_mode  &&  str->get_overtaking_mode()<=prohibited_mode  &&  static_cast<strasse_t*>(welt->lookup(get_pos())->get_weg(road_wt))->get_overtaking_mode()<=oneway_mode)  ) {
			if(  vehicle_base_t* v = other_lane_blocked(false, offset)  ) {
				if(  v->get_waytype() == road_wt  ) {
					restart_speed = 0;
					cnv->reset_waiting();
					cnv->set_next_cross_lane(true);
					return false;
				}
			}
			// There is no vehicle on traffic lane.
			// cnv->set_tiles_overtaking(0); is done in enter_tile()
		}
		// If the next tile is our destination and we are on passing lane of oneway mode road, we have to wait until traffic lane become safe.
		if(  cnv->is_overtaking()  &&  str->get_overtaking_mode()==oneway_mode  &&  route_index == r.get_count() - 1u  ) {
			halthandle_t halt = haltestelle_t::get_halt(welt->lookup(r.at(route_index))->get_pos(),cnv->get_owner());
			vehicle_base_t* v = other_lane_blocked(false, offset);
			if(  halt.is_bound()  &&  gr->get_weg_ribi(get_waytype())!=0  &&  v  &&  v->get_waytype() == road_wt  ) {
				restart_speed = 0;
				cnv->reset_waiting();
				cnv->set_next_cross_lane(true);
				return false;
			}
			// There is no vehicle on traffic lane.
		}
		// If the next tile is a intersection, lane crossing must be checked before entering.
		// The other vehicle is ignored if it is stopping to avoid stuck.
		const grund_t *gr = route_index_beyond_end_of_route ? NULL : welt->lookup(r.at(route_index));
		const strasse_t *stre = gr ? (strasse_t *)gr->get_weg(road_wt) : NULL;
		const ribi_t::ribi way_ribi = stre ? stre->get_ribi_unmasked() : ribi_t::none;
		if(stre && (way_ribi == ribi_t::all  ||  ribi_t::is_threeway(way_ribi))){

			//Check to see if the tile after the intersection is occupied.
			unsigned int intersection_offset = 1u;
			const grund_t *next_gr = NULL;
			const strasse_t *next_stre = NULL;
			ribi_t::ribi next_way_ribi = ribi_t::none;
			while(true){ //find the next non-intersection tile on our route (this has to account for multiple intersections in a row)
				next_gr = route_index_beyond_end_of_route ? NULL : welt->lookup(r.at(route_index + intersection_offset));
				if(!next_gr){ 
					break; //if it's end of the route, stop
				}
				next_stre = next_gr ? (strasse_t *)next_gr->get_weg(road_wt) : NULL;
				if(next_stre){
					next_way_ribi = next_stre->get_ribi_unmasked();
					if(!ribi_t::is_threeway(next_way_ribi)){ 
						break; //we've found the next non-intersection tile on our route
					}
				}
				intersection_offset++;
			}

			if(next_gr && next_stre){ //assuming we haven't reached the end of our route:
				ribi_t::ribi next_90direction_2 = calc_direction(welt->lookup(r.at(route_index+intersection_offset - 1u))->get_pos(), next_gr->get_pos());

				//get the direction the convoy will be facing at the end of the intersection(s)
				vehicle_base_t *obj2 = get_blocking_vehicle(next_gr, cnv, curr_direction, next_direction, next_90direction_2, NULL, next_lane);
				//check to see if there is someone blocking us at the end of the intersection(s)

				if(obj2){//there is a blocking convoy!
					//Vehicle must stop.
					restart_speed = 0;
					cnv->reset_waiting();
					return false;
				}
				
			}
			if(stre->get_overtaking_mode() <= oneway_mode){
				if(  const vehicle_base_t* v = other_lane_blocked(true)  ) {
					if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
						if(  at->get_convoi()->get_akt_speed()!=0  &&  judge_lane_crossing(calc_direction(get_pos(),pos_next), calc_direction(pos_next,pos_next2), at->get_90direction(), cnv->is_overtaking(), false)  ) {
							// vehicle must stop.
							restart_speed = 0;
							cnv->reset_waiting();
							cnv->set_next_cross_lane(true);
							return false;
						}
					}
				}
			}
		}
		// For the case that this vehicle is fixed to passing lane and is on traffic lane.
		if(  str->get_overtaking_mode() <= oneway_mode  &&  cnv->get_lane_affinity() == 1  &&  !cnv->is_overtaking()  ) {
			if(  vehicle_base_t* v = other_lane_blocked()  ) {
				if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(v)  ) {
					convoi_t* ocnv = car->get_convoi();
					if(  ocnv  &&  abs(cnv->get_min_top_speed() - ocnv->get_min_top_speed()) < kmh_to_speed(5)  ) {
						cnv->yield_lane_space();
					}
				}
				// citycars do not have the yielding mechanism.
			}
			else {
				// go on passing lane.
				cnv->set_tiles_overtaking(3);
			}
		}
		// If there is a vehicle that requests lane crossing, this vehicle must stop to yield space.
		if(  vehicle_base_t* v = other_lane_blocked(true)  ) {
			if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
				if(  at->get_convoi()->get_next_cross_lane()  &&  at==at->get_convoi()->back()  ) {
					// vehicle must stop.
					restart_speed = 0;
					cnv->reset_waiting();
					return false;
				}
			}
		}

		return obj==NULL;
	}

	return true;
}

overtaker_t* road_vehicle_t::get_overtaker()
{
	return cnv;
}

//return overtaker in "convoi_t"
convoi_t* road_vehicle_t::get_overtaker_cv()
{
	return cnv;
}

vehicle_base_t* road_vehicle_t::other_lane_blocked(const bool only_search_top, sint8 offset) const{
	// This function calculate whether the convoi can change lane.
	// only_search_top == false: check whether there's no car in -1 ~ +1 section.
	// only_search_top == true: check whether there's no car in front of this vehicle. (Not the same lane.)
	if(  leading  ){
		route_t const& r = *cnv->get_route();
		bool can_reach_tail = false;
		// we have to know the index of the tail of convoy.
		sint32 tail_index = -1;
		if(  cnv->get_vehicle_count()==1  ){
			tail_index = route_index==0 ? 0 : route_index-1;
		}else{
			for(uint32 test_index = 0; test_index < r.get_count(); test_index++){
				koord3d test_pos = r.at(test_index);
				if(test_pos==cnv->back()->get_pos()){
					tail_index = test_index;
					break;
				}
				if(test_pos==cnv->front()->get_pos()) break;
			}
		}
		for(sint32 test_index = route_index+offset < (sint32)r.get_count() ? route_index+offset : r.get_count() - 1u;; test_index--){
			grund_t *gr = welt->lookup(r.at(test_index));
			if(  !gr  ) {
				cnv->suche_neue_route();
				return NULL;
			}
			// this function cannot process vehicles on twoway and related mode road.
			const strasse_t* str = (strasse_t *)gr->get_weg(road_wt);
			if(  !str  ||  (str->get_overtaking_mode()>=twoway_mode  &&  str->get_overtaking_mode() <= prohibited_mode)  ) {
				break;
			}

			for(  uint8 pos=1;  pos < gr->get_top();  pos++  ) {
				if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
					if(  v->get_typ()==obj_t::pedestrian  ) {
						continue;
					}
					if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
						// ignore ourself
						if(  cnv == at->get_convoi()  ) {
							continue;
						}
						if(  cnv->is_overtaking() && at->get_convoi()->is_overtaking()  ){
							continue;
						}
						if(  !cnv->is_overtaking() && !(at->get_convoi()->is_overtaking())  ){
							//Prohibit going on passing lane when facing traffic exists.
							ribi_t::ribi other_direction = at->get_direction();
							route_t const& r = *cnv->get_route();
							koord3d next = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
							if(  calc_direction(next,get_pos()) == other_direction  ) {
								return v;
							}
							continue;
						}
						// the logic of other_lane_blocked cannot be applied to facing traffic.
						if(test_index>0) {
							const ribi_t::ribi this_prev_dir = calc_direction(r.at(test_index-1u), r.at(test_index));
							if(ribi_t::backward(this_prev_dir)&at->get_previous_direction()) {
								continue;
							}
						}
						// Ignore stopping convoi on the tile behind this convoi to change lane in traffic jam.
						if(  can_reach_tail  &&  at->get_convoi()->get_akt_speed() == 0  ) {
							continue;
						}
						if(  test_index==tail_index-1+offset  ||  test_index==tail_index+offset  ){
							uint8 tail_offset = 0;
							if(  test_index==tail_index-1+offset  ) tail_offset = 1;
							if(  test_index+tail_offset>=1  &&  test_index+tail_offset<(sint32)r.get_count()-1  &&   judge_lane_crossing(calc_direction(r.at(test_index-1u+tail_offset),r.at(test_index+tail_offset)), calc_direction(r.at(test_index+tail_offset),r.at(test_index+1u+tail_offset)),  v->get_90direction(), cnv->is_overtaking(), true)  ){
								return v;
							}
							continue;
						}
						return v;
					}
					else if(  private_car_t* const caut = obj_cast<private_car_t>(v)  ) {
						if(  cnv->is_overtaking() && caut->is_overtaking()  ){
							continue;
						}
						if(  !cnv->is_overtaking() && !(caut->is_overtaking())  ){
							//Prohibit going on passing lane when facing traffic exists.
							ribi_t::ribi other_direction = caut->get_direction();
							route_t const& r = *cnv->get_route();
							koord3d next = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
							if(  calc_direction(next,get_pos()) == other_direction  ) {
								return v;
							}
							continue;
						}
						// Ignore stopping convoi on the tile behind this convoi to change lane in traffic jam.
						if(  can_reach_tail  &&  caut->get_current_speed() == 0  ) {
							continue;
						}
						if(  test_index==tail_index-1+offset  ||  test_index==tail_index+offset  ){
							uint8 tail_offset = 0;
							if(  test_index==tail_index-1+offset  ) tail_offset = 1;
							if(  test_index+tail_offset>=1  &&  test_index+tail_offset<(sint32)r.get_count()-1  &&   judge_lane_crossing(calc_direction(r.at(test_index-1u+tail_offset),r.at(test_index+tail_offset)), calc_direction(r.at(test_index+tail_offset),r.at(test_index+1u+tail_offset)),  v->get_90direction(), cnv->is_overtaking(), true)  ){
								return v;
							}
							continue;
						}
						return v;
					}
				}
			}
			if(  test_index==tail_index-1+offset  ){
				// we finished the examination of the tile behind the tail of convoy.
				break;
			}
			if(  r.at(test_index)==cnv->back()->get_pos()  ) {
				can_reach_tail = true;
			}
			if(  test_index == 0  ||  only_search_top  ) {
				break;
			}
		}
	}
	return NULL;
}

uint32 road_vehicle_t::do_drive(uint32 distance)
{
	uint32 distance_travelled = vehicle_base_t::do_drive(distance);
	if(leading)
	{
		add_distance(distance_travelled);
	}
	return distance_travelled;
}

void road_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);
	const int cargo = get_total_cargo();
	strasse_t *str = (strasse_t*)gr->get_weg(road_wt);
	if(str == NULL)
	{
		return;
	}
	str->book(cargo, WAY_STAT_GOODS);
	if (  leading  )  {
		str->book(1, WAY_STAT_CONVOIS);
		cnv->update_tiles_overtaking();
		if(  next_lane==1  ) {
			cnv->set_tiles_overtaking(3);
			next_lane = 0;
		}
		//decide if overtaking convoi should go back to the traffic lane.
		if(  cnv->get_tiles_overtaking() == 1  &&  str->get_overtaking_mode() <= oneway_mode  ){
			vehicle_base_t* v = NULL;
			if(  cnv->get_lane_affinity() == 1  ||  (v = other_lane_blocked())!=NULL  ){
				//lane change denied
				cnv->set_tiles_overtaking(3);
				if(  cnv->is_requested_change_lane()  ||  cnv->get_lane_affinity() == -1  ) {
					//request the blocking convoi to reduce speed.
					if(  v  ) {
						if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(v)  ) {
							if(  abs(cnv->get_min_top_speed() - car->get_convoi()->get_min_top_speed()) < kmh_to_speed(5)  ) {
								car->get_convoi()->yield_lane_space();
							}
						}
					}
					else {
						// perhaps this vehicle is in lane fixing.
						cnv->set_requested_change_lane(false);
					}
				}
			}
			else {
				//lane change accepted
				cnv->set_requested_change_lane(false);
			}
		}
		if(  cnv->get_yielding_quit_index() <= route_index  ) {
			cnv->quit_yielding_lane();
		}
		cnv->set_next_cross_lane(false); // since this convoi moved...
		// If there is one-way sign, calc lane_affinity. This should not be calculated in can_enter_tile().
		if(  roadsign_t* rs = gr->find<roadsign_t>()  ) {
			if(  rs->get_desc()->is_single_way()  ) {
				if(  cnv->calc_lane_affinity(rs->get_lane_affinity())  ) {
					// write debug code here.
				}
			}
		}
		if(  cnv->get_lane_affinity_end_index() == route_index  ) {
			cnv->reset_lane_affinity();
		}
		// If this tile is two-way ~ prohibited and the previous tile is oneway, the convoy have to move on traffic lane. Safety is confirmed in can_enter_tile().
		if(  pos_prev!=koord3d::invalid  ) {
			grund_t* prev_gr = welt->lookup(pos_prev);
			if(  prev_gr  ){
				strasse_t* prev_str = (strasse_t*)prev_gr->get_weg(road_wt);
				if(  (prev_str  &&  (prev_str->get_overtaking_mode()<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  &&  str->get_overtaking_mode() <= prohibited_mode))  ||  str->get_overtaking_mode()==prohibited_mode  ){
					cnv->set_tiles_overtaking(0);
				}
			}
		}
	}
	drives_on_left = welt->get_settings().is_drive_left();	// reset driving settings
}

void road_vehicle_t::hop(grund_t* gr_to) {
	if(leading)
	{
		grund_t* gr = get_grund();
		if(gr)
		{
			strasse_t* str = (strasse_t*)gr->get_weg(road_wt);
			if(str)
			{
				if(get_last_stop_pos() != get_pos() && get_pos_next() != get_pos_prev())
				{
					flush_travel_times(str);
				}
				else {
					reset_measurements();
				}
			}
		}
	}
	vehicle_t::hop(gr_to);
}


schedule_t * road_vehicle_t::generate_new_schedule() const
{
	return new truck_schedule_t();
}


void road_vehicle_t::set_convoi(convoi_t *c)
{
	DBG_MESSAGE("road_vehicle_t::set_convoi()","%p",c);
	if(c!=NULL) {
		bool target=(bool)cnv; // only during loadtype: cnv==1 indicates, that the convoi did reserve a stop
		vehicle_t::set_convoi(c);
		if(target  &&  leading  &&  c->get_route()->empty()) {
			// reinitialize the target halt
			const route_t *rt = cnv->get_route();
			target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
			if(  target_halt.is_bound()  ) {
				for(  uint32 i=0;  i<c->get_tile_length()  &&  i+1<rt->get_count();  i++  ) {
					target_halt->reserve_position( welt->lookup( rt->at(rt->get_count()-i-1) ), cnv->self );
				}
			}
		}
	}
	else {
		if(  cnv  &&  leading  &&  target_halt.is_bound()  ) {
			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
				target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
			}
			target_halt = halthandle_t();
		}
		cnv = NULL;
	}
}
