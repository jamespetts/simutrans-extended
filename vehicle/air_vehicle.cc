/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "air_vehicle.h"


#include "../boden/wege/runway.h"
#include "../simhalt.h"
#include "../simconvoi.h"
#include "../simworld.h"
#include "../dataobj/environment.h"
#include "../simware.h"
#include "../bauer/vehikelbauer.h"
#include "../dataobj/schedule.h"


// this routine is called by find_route, to determined if we reached a destination
bool air_vehicle_t:: is_target(const grund_t *gr,const grund_t *)
{
	if(state!=looking_for_parking)
	{
		// search for the end of the runway
		const weg_t *w=gr->get_weg(air_wt);
		if(w  &&  w->get_desc()->get_styp()==type_runway)
		{
			// ok here is a runway
			ribi_t::ribi ribi= w->get_ribi_unmasked();
			//int success = 1;
			if(ribi_t::is_single(ribi)  &&  (ribi&approach_dir)!=0)
			{
				// pointing in our direction

				// Check for length
				const uint16 min_runway_length_meters = desc->get_minimum_runway_length();
				uint16 min_runway_length_tiles = ignore_runway_length ? 0 : min_runway_length_meters / welt->get_settings().get_meters_per_tile();
				if (!ignore_runway_length && min_runway_length_meters % welt->get_settings().get_meters_per_tile())
				{
					// Without this, this will be rounded down incorrectly.
					min_runway_length_tiles ++;
				}
				uint32 runway_length_tiles;

				bool runway_36_18 = false;
				bool runway_09_27 = false;

				switch (ribi)
				{
				case 1: // north
				case 4: // south
				case 5: // north-south
				case 7: // north-south-east
				case 13: // north-south-west
					runway_36_18 = true;
					break;
				case 2: // east
				case 8: // west
				case 10: // east-west
				case 11: // east-west-north
				case 14: // east-west-south
					runway_09_27 = true;
					break;
				case 15: // all
					runway_36_18 = true;
					runway_09_27 = true;
					break;
				default:
					runway_36_18 = false;
					runway_09_27 = false;
					break;
				}

				if (!runway_36_18 && !runway_09_27)
				{
					dbg->warning("bool air_vehicle_t:: is_target(const grund_t *gr,const grund_t *)", "Invalid runway directions for %u at %u, %u", cnv->self.get_id(), w->get_pos().x, w->get_pos().y);
					return false;
				}

				runway_length_tiles = w->get_runway_length(runway_36_18);

				return runway_length_tiles >= min_runway_length_tiles;
			}
		}
	}
	else {
		// otherwise we just check, if we reached a free stop position of this halt
		if(gr->get_halt()==target_halt  &&  target_halt->is_reservable(gr,cnv->self)) {
			return true;
		}
	}
	return false;
}


// for flying things, everywhere is good ...
// another function only called during route searching
ribi_t::ribi air_vehicle_t::get_ribi(const grund_t *gr) const
{
	switch(state) {
		case taxiing:
		case looking_for_parking:
		case awaiting_clearance_on_runway:
			return gr->get_weg_ribi(air_wt);

		case taxiing_to_halt:
		{
			// we must invert all one way signs here, since we start from the target position here!
			weg_t *w = gr->get_weg(air_wt);
			if(w) {
				ribi_t::ribi r = w->get_ribi_unmasked();
				if(  ribi_t::ribi mask = w->get_ribi_maske()  ) {
					r &= mask;
				}
				return r;
			}
			return ribi_t::none;
		}

		case landing:
		case departing:
		{
			ribi_t::ribi dir = gr->get_weg_ribi(air_wt);
			if(dir==0) {
				return ribi_t::all;
			}
			return dir;
		}

		case flying:
		case circling:
			return ribi_t::all;
	}
	return ribi_t::none;
}


// how expensive to go here (for way search)
int air_vehicle_t::get_cost(const grund_t *gr, const sint32, koord)
{
	// first favor faster ways
	const weg_t *w=gr->get_weg(air_wt);
	int costs = 0;

	if(state==flying) {
		if(w==NULL) {
			costs += 1;
		}
		else {
			if(w->get_desc()->get_styp()==type_flat) {
				costs += 25;
			}
		}
	}
	else {
		// only, if not flying ...
		assert(w);
		if (w->get_desc()->get_styp() == type_runway)
		{
			// Prefer to taxi on taxiways
			costs += 5;
		}
		if(w->get_desc()->get_styp()==type_flat) {
			costs += 3;
		}
		else {
			costs += 2;
		}
	}

	return costs;
}


// whether the ground is drivable or not depends on the current state of the airplane
bool air_vehicle_t::check_next_tile(const grund_t *bd) const
{
	switch (state) {
		case taxiing:
		case taxiing_to_halt:
		case looking_for_parking:
//DBG_MESSAGE("check_next_tile()","at %i,%i",bd->get_pos().x,bd->get_pos().y);
			return (bd->hat_weg(air_wt)  &&  bd->get_weg(air_wt)->get_max_speed()>0);

		case landing:
		case departing:
		case flying:
		case circling:
		{
//DBG_MESSAGE("air_vehicle_t::check_next_tile()","(cnv %i) in idx %i",cnv->self.get_id(),route_index );
			// here a height check could avoid too high mountains
			return true;
		}

		case awaiting_clearance_on_runway:
		{
			return false;
		}
	}
	return false;
}



/* finds a free stop, calculates a route and reserve the position
 * else return false
 */
bool air_vehicle_t::find_route_to_stop_position()
{
	// check for skipping circle
	route_t *rt=cnv->access_route();

//DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","can approach? (cnv %i)",cnv->self.get_id());

	grund_t const* const last = welt->lookup(rt->back());
	target_halt = last ? last->get_halt() : halthandle_t();
	if(!target_halt.is_bound()) {
		return true; // no halt to search
	}

	// then: check if the search point is still on a runway (otherwise just proceed)
	grund_t const* const target = welt->lookup(rt->at(search_for_stop));
	if(target==NULL  ||  !target->hat_weg(air_wt)) {
		target_halt = halthandle_t();
		DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","no runway found at (%s)",rt->at(search_for_stop).get_str());
		return true; // no runway any more ...
	}

	// is our target occupied?
//	DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","state %i",state);
	if(!target_halt->find_free_position(air_wt,cnv->self,obj_t::air_vehicle)  ) {
		target_halt = halthandle_t();
		DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","no free position found!");
		return false;
	}
	else {
		// calculate route to free position:

		// if we fail, we will wait in a step, much more simulation friendly
		// and the route finder is not re-entrant!
		if(!cnv->is_waiting()) {
			target_halt = halthandle_t();
			return false;
		}

		// now search a route
//DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","some free: find route from index %i",search_for_stop);
		route_t target_rt;
		flight_state prev_state = state;
		state = looking_for_parking;
		if(!target_rt.find_route( welt, rt->at(search_for_stop), this, 500, ribi_t::all, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, 100, cnv->has_tall_vehicles())) {
DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()","found no route to free one");
			// circle slowly another round ...
			target_halt = halthandle_t();
			state = prev_state;
			return false;
		}
		state = prev_state;

		// now reserve our choice ...
		target_halt->reserve_position(welt->lookup(target_rt.back()), cnv->self);
		//DBG_MESSAGE("air_vehicle_t::find_route_to_stop_position()", "found free stop near %i,%i,%i", target_rt.back().x, target_rt.back().y, target_rt.back().z);
		cnv->update_route(search_for_stop, target_rt);
		cnv->set_next_stop_index(INVALID_INDEX);
		return true;
	}
}


// main routine: searches the new route in up to three steps
// must also take care of stops under traveling and the like
route_t::route_result_t air_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool /*is_tall*/, route_t* route)
{
	if(leading) {
		// free target reservation
		if(  target_halt.is_bound() ) {
			if (grund_t* const target = welt->lookup(cnv->get_route()->back())) {
				target_halt->unreserve_position(target,cnv->self);
			}
		}
		// free runway reservation
		if(route_index>=takeoff  &&  route_index<touchdown-21  &&  state!=flying) {
			block_reserver( takeoff, takeoff+100, false );
		}
		else if(route_index>=touchdown-1  &&  state!=taxiing) {
			block_reserver( touchdown - landing_distance, search_for_stop+1, false );
		}
	}
	target_halt = halthandle_t(); // no block reserved

	takeoff = touchdown = search_for_stop = INVALID_INDEX;
	const route_t::route_result_t result = calc_route_internal(welt, start, ziel, max_speed, cnv->get_highest_axle_load(), state, flying_height, target_height, runway_too_short, airport_too_close_to_the_edge, takeoff, touchdown, search_for_stop, *route);
	cnv->set_next_stop_index(INVALID_INDEX);
	return result;
}


// BG, 07.08.2012: calculates a potential route without modifying any aircraft data.
/*
Allows partial routing for route extending or re-routing e.g. if runway not available
as well as calculating a complete route from gate to gate.

There are several flight phases in a "normal" complete route:
1) taxiing from start gate to runway
2) starting on the runway
3) flying to the destination runway
4) circling in the holding pattern
5) landing on destination runway
6) taxiing to destination gate
Both start and end point might be somewhere in one of these states.
Start state <= end state, of course.

As coordinates are koord3d, we are able to detect being in the air or on ground, aren't we?.
Try to handle the transition tiles like (virtual) waypoints and the partial (ground) routes as blocks:
- start of departure runway,
- takeoff,
- holding "switch",
- touchdown,
- end of required length of arrival runway
*/
route_t::route_result_t air_vehicle_t::calc_route_internal(
	karte_t *welt,
	const koord3d &start,            // input: start of (partial) route to calculate
	const koord3d &ziel,             // input: end of (partial) route to calculate
	sint32 max_speed,                // input: in the air
	uint32 weight,                   // input: gross weight of aircraft in kg (typical aircrafts don't have trailers)
	air_vehicle_t::flight_state &state, // input/output: at start
	sint16 &flying_height,               // input/output: at start
	sint16 &target_height,           // output: at end of takeoff
	bool &runway_too_short,          // output: either departure or arrival runway
	bool &airport_too_close_to_the_edge, // output: either airport locates close to the edge or not
	uint32 &takeoff,                 // output: route index to takeoff tile at departure airport
	uint32 &touchdown,               // output: scheduled route index to touchdown tile at arrival airport
	uint32 &search_for_stop,                  // output: scheduled route index to end of (required length of) arrival runway.
	route_t &route)                  // output: scheduled route from start to ziel
{
	//DBG_MESSAGE("air_vehicle_t::calc_route_internal()","search route from %i,%i,%i to %i,%i,%i",start.x,start.y,start.z,ziel.x,ziel.y,ziel.z);

	const bool vtol = get_desc()->get_minimum_runway_length() == 0;
	runway_too_short = false;
	airport_too_close_to_the_edge = false;
	search_for_stop = takeoff = touchdown = INVALID_INDEX;
	if(vtol)
	{
		takeoff = 0;
	}

	const grund_t* gr_start = welt->lookup(start);
	const weg_t *w_start = gr_start ? gr_start->get_weg(air_wt) : NULL;
	bool start_in_air = flying_height || w_start == NULL;

	const weg_t *w_ziel = welt->lookup(ziel)->get_weg(air_wt);
	bool end_in_air = w_ziel == NULL;

	if(!start_in_air)
	{
		// see, if we find a direct route: We are finished
		state = air_vehicle_t::taxiing;
		calc_altitude_level( get_desc()->get_topspeed() );
		route_t::route_result_t success = route.calc_route(welt, start, ziel, this, max_speed, weight, false, 0);
		if(success == route_t::valid_route)
		{
			// ok, we can taxi to our location
			return route_t::valid_route;
		}
	}

	koord3d search_start, search_end;
	if(start_in_air || vtol || (w_start && w_start->get_desc()->get_styp()==type_runway && ribi_t::is_single(w_start->get_ribi())))
	{
		// we start here, if we are in the air or at the end of a runway
		search_start = start;
		start_in_air = true;
		route.clear();
		//DBG_MESSAGE("air_vehicle_t::calc_route()","start in air at %i,%i,%i",search_start.x,search_start.y,search_start.z);
	}
	else {
		// not found and we are not on the takeoff tile (where the route search will fail too) => we try to calculate a complete route, starting with the way to the runway

		// second: find start runway end
		state = taxiing;
#ifdef USE_DIFFERENT_WIND
		approach_dir = get_approach_ribi( ziel, start ); // reverse
		//DBG_MESSAGE("air_vehicle_t::calc_route()","search runway start near %i,%i,%i with corner in %x",start.x,start.y,start.z, approach_dir);
#else
		approach_dir = ribi_t::northeast; // reverse
		DBG_MESSAGE("air_vehicle_t::calc_route()","search runway start near (%s)",start.get_str());
#endif
		if (!route.find_route(welt, start, this, max_speed, ribi_t::all, weight, cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, welt->get_settings().get_max_route_steps(), cnv->has_tall_vehicles()))
		{
			// There might be no route at all, or the runway might be too short. Test this case.
			ignore_runway_length = true;
			if (!route.find_route(welt, start, this, max_speed, ribi_t::all, weight, cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, welt->get_settings().get_max_route_steps(), cnv->has_tall_vehicles()))
			{
				DBG_MESSAGE("air_vehicle_t::calc_route()", "failed");
				ignore_runway_length = false;
				return route_t::no_route;
			}
			else
			{
				ignore_runway_length = false;
				runway_too_short = true;
			}
		}
		// save the route
		search_start = route.back();
		//DBG_MESSAGE("air_vehicle_t::calc_route()","start at ground (%s)",search_start.get_str());
	}

	// second: find target runway end

	state = taxiing_to_halt; // only used for search

#ifdef USE_DIFFERENT_WIND
	approach_dir = get_approach_ribi( start, ziel ); // reverse
	//DBG_MESSAGE("air_vehicle_t::calc_route()","search runway target near %i,%i,%i in corners %x",ziel.x,ziel.y,ziel.z,approach_dir);
#else
	approach_dir = ribi_t::southwest; // reverse
	//DBG_MESSAGE("air_vehicle_t::calc_route()","search runway target near %i,%i,%i in corners %x",ziel.x,ziel.y,ziel.z);
#endif
	route_t end_route;

	if(!end_route.find_route( welt, ziel, this, max_speed, ribi_t::all, weight, cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, welt->get_settings().get_max_route_steps(), cnv->has_tall_vehicles())) {
		// well, probably this is a waypoint
		end_in_air = true;
		search_end = ziel;
	}
	else {
		// save target route
		search_end = end_route.back();
	}
	//DBG_MESSAGE("air_vehicle_t::calc_route()","end at ground (%s)",search_end.get_str());

	// create target route
	if(!start_in_air)
	{
		takeoff = route.get_count()-1;
		koord start_dir(welt->lookup(search_start)->get_weg_ribi(air_wt));
		if(start_dir!=koord(0,0)) {
			// add the start
			ribi_t::ribi start_ribi = ribi_t::backward(ribi_type(start_dir));
			const grund_t *gr=NULL;
			// add the start
			int endi = 1;
			int over = landing_distance/3;
			// now add all runway + 3 ...
			do {
				if(!welt->is_within_limits(search_start.get_2d()+(start_dir*endi)) ) {
					break;
				}
				gr = welt->lookup_kartenboden(search_start.get_2d()+(start_dir*endi));
				if(over<landing_distance/3  ||  (gr->get_weg_ribi(air_wt)&start_ribi)==0) {
					over --;
				}
				endi ++;
				route.append(gr->get_pos());
			} while(  over>0  );
			// out of map
			if(gr==NULL) {
				dbg->error("air_vehicle_t::calc_route()","out of map!");
				return route_t::no_route;
			}
			// need some extra step to avoid 180 deg turns
			if( start_dir.x!=0  &&  sgn(start_dir.x)!=sgn(search_end.x-search_start.x)  ) {
				route.append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord(0,(search_end.y>search_start.y) ? 1 : -1 ) )->get_pos() );
				route.append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord(0,(search_end.y>search_start.y) ? 2 : -2 ) )->get_pos() );
			}
			else if( start_dir.y!=0  &&  sgn(start_dir.y)!=sgn(search_end.y-search_start.y)  ) {
				route.append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord((search_end.x>search_start.x) ? 1 : -1 ,0) )->get_pos() );
				route.append( welt->lookup_kartenboden(gr->get_pos().get_2d()+koord((search_end.x>search_start.x) ? 2 : -2 ,0) )->get_pos() );
			}
		}
		else {
			// init with startpos
			dbg->error("air_vehicle_t::calc_route()","Invalid route calculation: start is on a single direction field ...");
		}
		state = taxiing;
		flying_height = 0;
		target_height = (sint16)start.z*TILE_HEIGHT_STEP;
	}
	else {
		// init with current pos (in air ... )
		route.clear();
		route.append( start );
		state = flying;
		calc_altitude_level( desc->get_topspeed() ); // added for AFHP
		if(flying_height==0) {
			flying_height = 3*TILE_HEIGHT_STEP;
		}
		takeoff = 0;
		//		target_height = ((sint16)start.z+3)*TILE_HEIGHT_STEP;
		target_height = ((sint16)start.z+altitude_level)*TILE_HEIGHT_STEP;
	}
//DBG_MESSAGE("air_vehicle_t::calc_route()","take off ok");

	koord3d landing_start=search_end;
	if(!end_in_air) {
		// now find way to start of landing pos
		ribi_t::ribi end_ribi = welt->lookup(search_end)->get_weg_ribi(air_wt);
		koord end_dir(end_ribi);
		end_ribi = ribi_t::backward(end_ribi);
		if(end_dir!=koord(0,0)) {
			// add the start
			const grund_t *gr;
			int endi = 1;
			//			int over = landing_distance;
			int over = landing_distance;
			do {
				if(!welt->is_within_limits(search_end.get_2d()+(end_dir*endi)) ) {
					break;
				}
				gr = welt->lookup_kartenboden(search_end.get_2d()+(end_dir*endi));
				if(over< landing_distance  ||  (gr->get_weg_ribi(air_wt)&end_ribi)==0) {
					over --;
				}
				endi ++;
				landing_start = gr->get_pos();
			} while(  over>0  );
		}
	}
	else {
		search_for_stop = touchdown = INVALID_INDEX;
	}

	// just some straight routes ...
	if(!route.append_straight_route(welt,landing_start)) {
		// should never fail ...
		dbg->error( "air_vehicle_t::calc_route()", "No straight route found!" );
		return route_t::no_route;
	}

	if(!end_in_air) {

		// find starting direction
		int offset = 0;
		switch(welt->lookup(search_end)->get_weg_ribi(air_wt)) {
			case ribi_t::north: offset = 0; break;
			case ribi_t::west: offset = 4; break;
			case ribi_t::south: offset = 8; break;
			case ribi_t::east: offset = 12; break;
		}

		// now make a curve
		koord circlepos=landing_start.get_2d();
		static const koord circle_koord[HOLDING_PATTERN_LENGTH]={ koord(0,1), koord(0,1), koord(1,0), koord(0,1), koord(1,0), koord(1,0), koord(0,-1), koord(1,0), koord(0,-1), koord(0,-1), koord(-1,0), koord(0,-1), koord(-1,0), koord(-1,0), koord(0,1), koord(-1,0) };

		// circle to the left
		for(  int  i=0;  i < HOLDING_PATTERN_LENGTH;  i++  ) {
			circlepos += circle_koord[(offset + i + HOLDING_PATTERN_LENGTH) % HOLDING_PATTERN_LENGTH];
			if(welt->is_within_limits(circlepos)) {
				route.append( welt->lookup_kartenboden(circlepos)->get_pos() );
			}
			else {
				// could only happen during loading old savegames;
				// in new versions it should not possible to build a runway here
				route.clear();
				dbg->error("air_vehicle_t::calc_route()","airport too close to the edge! (Cannot go to %i,%i!)",circlepos.x,circlepos.y);
				airport_too_close_to_the_edge = true;
				return route_t::no_route;
			}
		}

		//touchdown = route.get_count() + 2; //2 means HOLDING_PATTERN_OFFSET-1
		touchdown = route.get_count() + landing_distance - 1;
		route.append_straight_route(welt,search_end);


		// now the route to ziel search point (+1, since it will check before entering the tile ...)
		search_for_stop = route.get_count()-1;

		// define the endpoint on the runway
		uint32 runway_tiles = search_for_stop - touchdown;
		uint32 min_runway_tiles = desc->get_minimum_runway_length() / welt->get_settings().get_meters_per_tile() + 1;
		sint32 excess_of_tiles = runway_tiles - min_runway_tiles;
		search_for_stop -= excess_of_tiles;
		// now we just append the rest
		for( int i=end_route.get_count()-2;  i>=0;  i--  ) {
			route.append(end_route.at(i));
		}
	}

	//DBG_MESSAGE("air_vehicle_t::calc_route_internal()","departing=%i  touchdown=%i   search_for_stop=%i   total=%i  state=%i",takeoff, touchdown, search_for_stop, route.get_count()-1, state );
	return route_t::valid_route;
}


// BG, 08.08.2012: extracted from can_enter_tile()
route_t::route_result_t air_vehicle_t::reroute(const uint16 reroute_index, const koord3d &ziel)
{
	// new aircraft state after successful routing:
	air_vehicle_t::flight_state xstate = state;
	sint16 xflughoehe = flying_height;
	sint16 xtarget_height;
	bool xrunway_too_short;
	bool xairport_too_close_to_the_edge;
	uint32 xtakeoff;   // new route index to takeoff tile at departure airport
	uint32 xtouchdown; // new scheduled route index to touchdown tile at arrival airport
	uint32 xsuchen;    // new scheduled route index to end of (required length of) arrival runway.
	route_t xroute;    // new scheduled route from position at reroute_index to ziel

	route_t &route = *cnv->get_route();
	route_t::route_result_t done = calc_route_internal(welt, route.at(reroute_index), ziel,
																	speed_to_kmh(cnv->get_min_top_speed()), cnv->get_highest_axle_load(),
																	xstate, xflughoehe, xtarget_height,
																	xrunway_too_short, xairport_too_close_to_the_edge,
																	xtakeoff, xtouchdown, xsuchen, xroute);
	if (done)
	{
		// convoy replaces existing route starting at reroute_index with found route.
		cnv->update_route(reroute_index, xroute);
		cnv->set_next_stop_index(INVALID_INDEX);
		if (reroute_index == route_index)
		{
			state = xstate;
			flying_height = xflughoehe;
			target_height = xtarget_height;
			runway_too_short = xrunway_too_short;
		}
		if (takeoff >= reroute_index)
			takeoff = xtakeoff != INVALID_INDEX ? reroute_index + xtakeoff : INVALID_INDEX;
		if (touchdown >= reroute_index)
			touchdown = xtouchdown != INVALID_INDEX ? reroute_index + xtouchdown : INVALID_INDEX;
		if (search_for_stop >= reroute_index)
			search_for_stop = xsuchen != INVALID_INDEX ? reroute_index + xsuchen : INVALID_INDEX;
	}
	return done;
}


/* reserves runways (reserve true) or removes the reservation
 * finishes when reaching end of runway
 * @return true if the reservation is successful
 */
int air_vehicle_t::block_reserver( uint32 start, uint32 end, bool reserve ) const
{
	const route_t *route = cnv->get_route();
	if (route->empty()) {
		return 0;
	}

	bool start_now = false;

	uint16 runway_tiles = end - start;
	uint16 runway_meters = runway_tiles * welt->get_settings().get_meters_per_tile();
	const uint16 min_runway_length_meters = desc->get_minimum_runway_length();

	int success = runway_meters >= min_runway_length_meters ? 1 : 2;

	for(  uint32 i=start;  success  &&  i<end  &&  i<route->get_count();  i++) {
		grund_t *gr = welt->lookup(route->at(i));
		runway_t * sch1 = gr ? (runway_t *)gr->get_weg(air_wt) : NULL;

		if(sch1==NULL) {
			if(reserve) {
				if(!start_now) {
					// touched down here
					start = i;
				}
				else {
					// most likely left the ground here ...
					end = i;
					break;
				}
			}
		}
		else {
			// we un-reserve also nonexistent tiles! (may happen during deletion)
			if(reserve) {
				start_now = true;
				if(!sch1->reserve(cnv->self,ribi_t::none)) {
					// unsuccessful => must unreserve all
					success = 0;
					end = i;
					break;
				}

				// We want to reserve the whole length of the runway to prevent conflicts.
				// Currently, this only works on takeoff. Landing is more difficult, as
				// the path finder does not necessarily find a path along the whole length of the runway.
				/*
				// reserve to the minimum runway length...
				if (state != taxiing && state != awaiting_clearance_on_runway)
				{
					uint16 current_runway_length_meters = ((i + 1) - start) * welt->get_settings().get_meters_per_tile();
					if (i > start&& current_runway_length_meters > min_runway_length_meters) {
						success = success == 0 ? 0 : runway_meters >= min_runway_length_meters ? 1 : 2;
						return success;
					}
				}
				*/

				// end of runway?
				if(i>start  &&  ribi_t::is_single(sch1->get_ribi_unmasked())  )
				{
					runway_tiles = (i + 1) - start;
					runway_meters = runway_tiles * welt->get_settings().get_meters_per_tile();
					success = success == 0 ? 0 : runway_meters >= min_runway_length_meters ? 1 : 2;
					return success;
				}
			}
			else if(!sch1->unreserve(cnv->self)) {
				if(start_now) {
					// reached a reserved or free track => finished
					return success;
				}
			}
			else {
				// un-reserve from here (only used during sale, since there might be reserved tiles not freed)
				start_now = true;
			}
		}
	}

	// un-reserve if not successful
	if(  !success  &&  reserve  ) {
		for(  uint32 i=start;  i<end;  i++  ) {
			grund_t *gr = welt->lookup(route->at(i));
			if (gr) {
				runway_t* sch1 = (runway_t *)gr->get_weg(air_wt);
				if (sch1) {
					sch1->unreserve(cnv->self);
				}
			}
		}
	}
	return success;
}

// handles all the decisions on the ground and in the air

bool air_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	restart_speed = -1;

	assert(cnv->get_state() != convoi_t::ROUTING_1 && cnv->get_state() != convoi_t::ROUTING_2);
	assert(gr);
	if(gr->get_top()>250) {
		// too many objects here
		return false;
	}

	route_t &route = *cnv->get_route();
	convoi_t::route_infos_t &route_infos = cnv->get_route_infos();

	uint16 next_block = cnv->get_next_stop_index() - 1;
	uint16 last_index = route.get_count() - 1;
	if(next_block > 65000)
	{
		convoi_t &convoy = *cnv;
		const sint32 brake_steps = convoy.calc_min_braking_distance(welt->get_settings(), convoy.get_weight_summary(), cnv->get_akt_speed());
		const sint32 route_steps = route_infos.calc_steps(route_infos.get_element(route_index).steps_from_start, route_infos.get_element(last_index).steps_from_start);
		const grund_t* gr = welt->lookup(cnv->get_schedule()->get_current_entry().pos);
		if(route_steps <= brake_steps && (!gr || !gr->get_depot())) // Do not recalculate a route if the route ends in a depot.
		{
			// we need a longer route to decide, whether we will have to throttle:
			schedule_t *schedule = cnv->get_schedule();
			uint8 index = schedule->get_current_stop();
			bool reversed = cnv->get_reverse_schedule();
			schedule->increment_index(&index, &reversed);
			if (reroute(last_index, schedule->entries[index].pos))
			{
				if (reversed)
					schedule->advance_reverse();
				else
					schedule->advance();
				cnv->set_next_stop_index(INVALID_INDEX);
				last_index = route.get_count() - 1;
			}
		}
	}

	runway_t* rw = (runway_t*)gr->get_weg(air_wt);

	if(  route_index < takeoff  &&  route_index > 1  &&  takeoff < last_index  ) {
		// check, if tile occupied by a plane on ground
		for(  uint8 i = 1;  i<gr->get_top();  i++  ) {
			obj_t *obj = gr->obj_bei(i);
			if(  obj->get_typ()==obj_t::air_vehicle  &&  ((air_vehicle_t *)obj)->is_on_ground()  ) {
				restart_speed = 0;
				return false;
			}
		}
		// need to reserve runway?
		if(rw==NULL) {
			cnv->suche_neue_route();
			return false;
		}
		// next tile a runway => then reserve
		if(rw->get_desc()->get_styp()==type_elevated) {
			// try to reserve the runway
			//const uint16 runway_length_tiles = ((search_for_stop+1) - touchdown) - takeoff;
			const int runway_state = block_reserver(takeoff,takeoff+100,true);
			if(runway_state != 1)
			{
				// runway already blocked or too short
				restart_speed = 0;

				if(runway_state == 2)
				{
					// Runway too short - explain to player
					runway_too_short = true;
				}
				else
				{
					runway_too_short = false;
				}

				return false;
			}
		}
		runway_too_short = false;
		return true;
	}

	if(state == taxiing)
	{
		// enforce on ground for taxiing
		flying_height = 0;

		// Do not enter reserved runway if not already on a runway (check whether already on a runway to avoid deadlocks)
		const grund_t* gr_current = welt->lookup(get_pos());
		const runway_t* rw_current = gr_current ? (runway_t*)gr_current->get_weg(air_wt) : nullptr;
		if (rw && rw->get_desc()->get_styp() == type_runway && (!rw_current || rw_current->get_desc()->get_styp() != type_runway) && rw->is_reserved() && rw->get_reserved_convoi() != cnv->self)
		{
			restart_speed = 0;
			return false;
		}
	}

	if(  route_index == takeoff  &&  state == taxiing  ) {
		// try to reserve the runway if not already done
		if(route_index==2  &&  !block_reserver(takeoff,takeoff+100,true)) {
			// runway blocked, wait at start of runway
			restart_speed = 0;
			return false;
		}
		// stop shortly at the end of the runway
		state = awaiting_clearance_on_runway;
		go_on_ticks = welt->get_ticks() + welt->get_seconds_to_ticks(10);
		const ribi_t::ribi old_direction = direction;
		direction = calc_direction(route.at(takeoff), route.at(takeoff - 1));
		calc_image();
		direction = old_direction;
		restart_speed = 0;
		return false;
	}

	if (state == awaiting_clearance_on_runway)
	{
		if (welt->get_ticks() >= go_on_ticks)
		{
			state = departing;
		}
		else
		{
			restart_speed = 0;
			return false;
		}
	}

//DBG_MESSAGE("air_vehicle_t::can_enter_tile()","index %i<>%i",route_index,touchdown);

	// check for another circle ...
	//	if(  route_index == touchdown - HOLDING_PATTERN_OFFSET )
	//circling now!
	if(  route_index == touchdown - landing_distance) {
		const int runway_state = block_reserver( touchdown, search_for_stop+1 , true );
		if( runway_state != 1 )
		{
			if(runway_state == 2)
			{
				// Runway too short - explain to player
				runway_too_short = true;
				// TODO: Find some way of recalculating the runway
				// length here. Recalculating the route produces
				// crashes.
			}
			else
			{
				runway_too_short = false;
			}

			route_index -= HOLDING_PATTERN_LENGTH;
			return true;
		}
		state = landing;
		return true;
		runway_too_short = false;
	}

	if(  route_index == touchdown - HOLDING_PATTERN_LENGTH - landing_distance &&  state != circling  )
	{
		// just check, if the end of runway is free; we will wait there
		//		std::cout << "reserve 2: "<<state<<" "<< touchdown <<" "<<search_for_stop+1<< std::endl;
		const int runway_state = block_reserver( touchdown , search_for_stop+1 , true );
		if(runway_state == 1)
		{
			route_index += HOLDING_PATTERN_LENGTH;
			// can land => set landing height
			state = landing;
			runway_too_short = false;
		}
		else
		{
			if(runway_state == 2)
			{
				// Runway too short - explain to player
				runway_too_short = true;
			}
			else
			{
				runway_too_short = false;
			}

			// circle slowly next round
			state = circling;
			cnv->must_recalc_data();
		}
	}

	if(route_index==search_for_stop &&  state==landing) {

		// we will wait in a step, much more simulation friendly
		// and the route finder is not re-entrant!
		if(!cnv->is_waiting()) {
			return false;
		}

		// nothing free here?
		if(find_route_to_stop_position()) {
			// stop reservation successful
			state = taxiing;
			restart_speed = kmh_to_speed(10);
			return true;
		}
		restart_speed = 0;
		return false;
	}

	if(state==looking_for_parking) {
		state = taxiing;
	}

	if(state == taxiing  &&  gr->is_halt()  &&  gr->find<air_vehicle_t>()) {
		// the next step is a parking position. We do not enter, if occupied!
		restart_speed = 0;
		return false;
	}

	return true;
}


// this must also change the internal modes for the calculation
void air_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(is_on_ground())
	{
		runway_t *w=(runway_t *)gr->get_weg(air_wt);
		if(w)
		{
			const int cargo = get_total_cargo();
			w->book(cargo, WAY_STAT_GOODS);
			if (leading)
			{
				w->book(1, WAY_STAT_CONVOIS);
				w->reserve(cnv->self, get_direction());
			}
		}
	}
}

void air_vehicle_t::leave_tile()
{
	vehicle_t::leave_tile();
	if (is_on_ground() && state == taxiing || state == taxiing_to_halt || state == looking_for_parking)
	{
		grund_t* gr = welt->lookup(get_pos());
		runway_t* w = (runway_t*)gr->get_weg(air_wt);
		if (w)
		{
			w->unreserve(this);
		}
	}
}


air_vehicle_t::air_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
#ifdef INLINE_OBJ_TYPE
	vehicle_t(obj_t::air_vehicle)
#else
    vehicle_t()
#endif
{
	rdwr_from_convoi(file);
	calc_altitude_level( desc->get_topspeed() );
	runway_too_short = false;
	airport_too_close_to_the_edge = false;

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_leading) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL) {
			const goods_desc_t* gd = NULL;
			ware_t w;
			for (uint8 i = 0; i < number_of_classes; i++)
			{
				if (!fracht[i].empty())
				{
					w = fracht[i].front();
					gd = w.get_desc();
					break;
				}
			}
			if (!gd)
			{
				// Important: This must be the same default ware as used in rdwr_from_convoi.
				gd = goods_manager_t::passengers;
			}

			dbg->warning("air_vehicle_t::air_vehicle_t()", "try to find a fitting vehicle for %s.", gd->get_name());
			desc = vehicle_builder_t::get_best_matching(air_wt, 0, 101, 1000, 800, gd, true, last_desc, is_last );
			if(desc) {
				calc_image();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
	fix_class_accommodations();
}


air_vehicle_t::air_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::air_vehicle, pos, desc, player)
#else
	vehicle_t(pos, desc, player)
#endif
{
	cnv = cn;
	state = taxiing;
	flying_height = 0;
	go_on_ticks = 0;
	target_height = pos.z;
	runway_too_short = false;
	airport_too_close_to_the_edge = false;
	calc_altitude_level( desc->get_topspeed() );
}


air_vehicle_t::~air_vehicle_t()
{
	// mark aircraft (after_image) dirty, since we have no "real" image
	const int raster_width = get_current_tile_raster_width();
	sint16 yoff = tile_raster_scale_y(-flying_height - get_hoff() - 2, raster_width);

	mark_image_dirty( image, yoff);
	mark_image_dirty( image, 0 );
}


void air_vehicle_t::set_convoi(convoi_t *c)
{
	DBG_MESSAGE("air_vehicle_t::set_convoi()","%p",c);
	if(leading  &&  (uint64)cnv > 1ll) {
		// free stop reservation
		route_t const& r = *cnv->get_route();
		if(target_halt.is_bound()) {
			target_halt->unreserve_position(welt->lookup(r.back()), cnv->self);
			target_halt = halthandle_t();
		}
		if (!r.empty()) {
			// free runway reservation
			if(route_index>=takeoff  &&  route_index<touchdown-4  &&  state!=flying) {
				block_reserver( takeoff, takeoff+100, false );
			}
			else if(route_index>=touchdown-1  &&  state!=taxiing) {
				//				block_reserver( touchdown, search_for_stop+1, false );
				//				std::cout << "unreserve 4: "<<state<<" "<< touchdown << std::endl;
				block_reserver( touchdown, search_for_stop+1, false );
			}
		}
	}
	// maybe need to restore state?
	if(c!=NULL) {
		bool target=(bool)cnv;
		vehicle_t::set_convoi(c);
		if(leading) {
			if(target) {
				// reinitialize the target halt
				grund_t* const target=welt->lookup(cnv->get_route()->back());
				target_halt = target->get_halt();
				if(target_halt.is_bound()) {
					target_halt->reserve_position(target,cnv->self);
				}
			}
			// restore reservation
			if(  grund_t *gr = welt->lookup(get_pos())  ) {
				if(  weg_t *weg = gr->get_weg(air_wt)  ) {
					if(  weg->get_desc()->get_styp()==type_runway  ) {
						// but only if we are on a runway ...
						if(  route_index>=takeoff  &&  route_index<touchdown-21  &&  state!=flying  ) {
							block_reserver( takeoff, takeoff+100, true );
						}
						else if(  route_index>=touchdown-1  &&  state!=taxiing  ) {
							//							std::cout << "reserve 5: "<<state<<" "<< touchdown << std::endl;
							block_reserver( touchdown, search_for_stop+1, true );
						}
					}
				}
			}
		}
	}
	else {
		vehicle_t::set_convoi(NULL);
	}
}


schedule_t *air_vehicle_t::generate_new_schedule() const
{
	return new airplane_schedule_();
}

void air_vehicle_t::rdwr_from_convoi(loadsave_t *file)
{
	xml_tag_t t( file, "air_vehicle_t" );

	// initialize as vehicle_t::rdwr_from_convoi calls get_image()
	if (file->is_loading()) {
		state = taxiing;
		flying_height = 0;
	}
	vehicle_t::rdwr_from_convoi(file);

	file->rdwr_enum(state);
	file->rdwr_short(flying_height);
	flying_height &= ~(TILE_HEIGHT_STEP-1);
	file->rdwr_short(target_height);
	file->rdwr_long(search_for_stop);
	file->rdwr_long(touchdown);
	file->rdwr_long(takeoff);
	if (file->is_version_ex_atleast(14, 54))
	{
		file->rdwr_longlong(go_on_ticks);
	}
	else
	{
		go_on_ticks = 0;
	}
}



#ifdef USE_DIFFERENT_WIND
// well lots of code to make sure, we have at least two different directions for the runway search
uint8 air_vehicle_t::get_approach_ribi( koord3d start, koord3d ziel )
{
	uint8 dir = ribi_type(ziel-start); // reverse
	// make sure, there are at last two directions to choose, or you might en up with not route
	if(ribi_t::is_single(dir)) {
		dir |= (dir<<1);
		if(dir>16) {
			dir += 1;
		}
	}
	return dir&0x0F;
}
#endif


void air_vehicle_t::hop(grund_t* gr)
{
	if(  !get_flag(obj_t::dirty)  ) {
		mark_image_dirty( image, 0 );
		set_flag( obj_t::dirty );
	}

	sint32 new_speed_limit = speed_unlimited();
	sint32 new_friction = 0;

	// take care of in-flight height ...
	const sint16 h_cur = (sint16)get_pos().z*TILE_HEIGHT_STEP;
	const sint16 h_next = (sint16)pos_next.z*TILE_HEIGHT_STEP;

	switch(state) {
		case departing: {
			flying_height = 0;
			target_height = h_cur;
			new_friction = max( 1, 28/(1+(route_index-takeoff)*2) ); // 9 5 4 3 2 2 1 1...

			// take off, when a) end of runway or b) last tile of runway or c) has reached minimum runway length
			weg_t *weg=welt->lookup(get_pos())->get_weg(air_wt);
			const sint16 runway_tiles_so_far = route_index - takeoff;
			const sint16 runway_meters_so_far = runway_tiles_so_far * welt->get_settings().get_meters_per_tile();
			const uint16 min_runway_length_meters = desc->get_minimum_runway_length();

			if(  (weg==NULL  ||  // end of runway (broken runway)
				 weg->get_desc()->get_styp()!=type_runway  ||  // end of runway (grass now ... )
				 (route_index>takeoff+1  &&  ribi_t::is_single(weg->get_ribi_unmasked())) )  ||  // single ribi at end of runway
				 (min_runway_length_meters && runway_meters_so_far >= min_runway_length_meters)   //  has reached minimum runway length
			) {
				state = flying;
				play_sound();
				new_friction = 1;
				block_reserver( takeoff, takeoff+100, false );
				calc_altitude_level( desc->get_topspeed() );
				flying_height = h_cur - h_next;
				target_height = h_cur+TILE_HEIGHT_STEP*(altitude_level+(sint16)get_pos().z);//modified
			}
			break;
		}
		case circling: {
			new_speed_limit = kmh_to_speed(desc->get_topspeed())/3;
			new_friction = 4;
			// do not change height any more while circling
			flying_height += h_cur;
			flying_height -= h_next;
			break;
		}
		case flying: {
			// since we are at a tile border, round up to the nearest value
			flying_height += h_cur;
			if(  flying_height < target_height  ) {
				flying_height = (flying_height+TILE_HEIGHT_STEP) & ~(TILE_HEIGHT_STEP-1);
			}
			else if(  flying_height > target_height  ) {
				flying_height = (flying_height-TILE_HEIGHT_STEP);
			}
			flying_height -= h_next;
			// did we have to change our flight height?
			if(  target_height-h_next > TILE_HEIGHT_STEP*altitude_level*4/3 + (sint16)get_pos().z  ) {
				// Move down
				target_height -= TILE_HEIGHT_STEP*2;
			}
			else if(  target_height-h_next < TILE_HEIGHT_STEP*altitude_level*2/3 + (sint16)get_pos().z   ) {
				// Move up
				target_height += TILE_HEIGHT_STEP*2;
			}
			break;
		}
		case landing: {
			new_speed_limit = kmh_to_speed(desc->get_topspeed())/3; // ==approach speed
			new_friction = 8;
			flying_height += h_cur;
			if(  flying_height < target_height  ) {
				flying_height = (flying_height+TILE_HEIGHT_STEP) & ~(TILE_HEIGHT_STEP-1);
			}
			else if(  flying_height > target_height  ) {
				flying_height = (flying_height-TILE_HEIGHT_STEP);
			}

			if (route_index >= touchdown)  {
				// come down, now!
				target_height = h_next;

				// touchdown!
				if (flying_height==h_next) {
					const sint32 taxi_speed = kmh_to_speed( min( 60, desc->get_topspeed()/4 ) );
					if(  cnv->get_akt_speed() <= taxi_speed  ) {
						new_speed_limit = taxi_speed;
						new_friction = 16;
					}
					else {
						const sint32 runway_left = search_for_stop - route_index;
						new_speed_limit = min( new_speed_limit, runway_left*runway_left*taxi_speed ); // ...approach 540 240 60 60
						const sint32 runway_left_fr = max( 0, 6-runway_left );
						new_friction = max( new_friction, min( desc->get_topspeed()/12, 4 + 4*(runway_left_fr*runway_left_fr+1) )); // ...8 8 12 24 44 72 108 152
					}
				}
			}
			else {
				// runway is on this height
				const sint16 runway_height = cnv->get_route()->at(touchdown).z*TILE_HEIGHT_STEP;

				// we are too low, ascent asap
				if (flying_height < runway_height + TILE_HEIGHT_STEP) {
					target_height = runway_height + TILE_HEIGHT_STEP;
				}
				// too high, descent
				else if (flying_height + h_next - h_cur > runway_height + (sint16)(touchdown-route_index-1)*TILE_HEIGHT_STEP) {
					target_height = runway_height +  (touchdown-route_index-1)*TILE_HEIGHT_STEP;
				}
			}
			flying_height -= h_next;
			break;
		}
		default: {
			new_speed_limit = kmh_to_speed( min( 60, desc->get_topspeed()/4 ) );
			new_friction = 16;
			flying_height = 0;
			target_height = h_next;
			break;
		}
	}

	// hop to next tile
	vehicle_t::hop(gr);

	speed_limit = new_speed_limit;
	current_friction = new_friction;
}

void air_vehicle_t::unreserve_runway()
{
	// Unreserve whole runway when leaving runway
	block_reserver(touchdown, search_for_stop + 1, false);
}


// this routine will display the aircraft (if in flight)
#ifdef MULTI_THREAD
void air_vehicle_t::display_after(int xpos_org, int ypos_org, const sint8 clip_num) const
#else
void air_vehicle_t::display_after(int xpos_org, int ypos_org, bool is_global) const
#endif
{
	if(  image != IMG_EMPTY  &&  !is_on_ground()  ) {
		int xpos = xpos_org, ypos = ypos_org;

		const int raster_width = get_current_tile_raster_width();
		const sint16 z = get_pos().z;
		if(  z + flying_height/TILE_HEIGHT_STEP - 1 > grund_t::underground_level  ) {
			return;
		}
		const sint16 target = target_height - ((sint16)z*TILE_HEIGHT_STEP);
		sint16 current_flughohe = flying_height;
		if(  current_flughohe < target  ) {
			current_flughohe += (steps*TILE_HEIGHT_STEP) >> 8;
		}
		else if(  current_flughohe > target  ) {
			current_flughohe -= (steps*TILE_HEIGHT_STEP) >> 8;
		}

		sint8 hoff = get_hoff();
		ypos += tile_raster_scale_y(get_yoff()-current_flughohe-hoff-2, raster_width);
		xpos += tile_raster_scale_x(get_xoff(), raster_width);
		get_screen_offset( xpos, ypos, raster_width );

		display_swap_clip_wh(CLIP_NUM_VAR);
		// will be dirty
		// the aircraft!!!
		display_color( image, xpos, ypos, get_owner_nr(), true, true/*get_flag(obj_t::dirty)*/  CLIP_NUM_PAR);
#ifndef MULTI_THREAD
		vehicle_t::display_after( xpos_org, ypos_org - tile_raster_scale_y( current_flughohe - hoff - 2, raster_width ), is_global );
#endif
		display_swap_clip_wh(CLIP_NUM_VAR);
	}
#ifdef MULTI_THREAD
}
void air_vehicle_t::display_overlay(int xpos_org, int ypos_org) const
{
	if(  image != IMG_EMPTY  &&  !is_on_ground()  ) {
		const int raster_width = get_current_tile_raster_width();
		const sint16 z = get_pos().z;
		if(  z + flying_height/TILE_HEIGHT_STEP - 1 > grund_t::underground_level  ) {
			return;
		}
		const sint16 target = target_height - ((sint16)z*TILE_HEIGHT_STEP);
		sint16 current_flughohe = flying_height;
		if(  current_flughohe < target  ) {
			current_flughohe += (steps*TILE_HEIGHT_STEP) >> 8;
		}
		else if(  current_flughohe > target  ) {
			current_flughohe -= (steps*TILE_HEIGHT_STEP) >> 8;
		}

		vehicle_t::display_overlay( xpos_org, ypos_org - tile_raster_scale_y( current_flughohe - get_hoff() - 2, raster_width ) );
	}
#endif
	else if(  is_on_ground()  ) {
		// show loading tooltips on ground
#ifdef MULTI_THREAD
		vehicle_t::display_overlay( xpos_org, ypos_org );
#else
		vehicle_t::display_after( xpos_org, ypos_org, is_global );
#endif
	}
}


const char *air_vehicle_t::is_deletable(const player_t *player)
{
	if (is_on_ground()) {
		return vehicle_t::is_deletable(player);
	}
	return NULL;
}
