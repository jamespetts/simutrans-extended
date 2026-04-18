/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_AIR_VEHICLE_H
#define VEHICLE_AIR_VEHICLE_H


#include "vehicle.h"


/**
 * A class for aircrafts. Manages the look of the vehicles
 * and the navigability of tiles.
 * @see vehicle_t
 */
class air_vehicle_t : public vehicle_t
{
public:
	enum flight_state {
		taxiing							= 0,
		departing						= 1,
		flying							= 2,
		landing							= 3,
		looking_for_parking				= 4,
		circling						= 5,
		taxiing_to_halt					= 6,
		awaiting_clearance_on_runway	= 7
	};

private:
	// only used for is_target() (do not need saving)
	ribi_t::ribi approach_dir;

	// Used to re-run the routing algorithm without
	// checking runway length in order to display
	// the correct error message.
	bool ignore_runway_length = false;

	// The tick value a which this aircraft will start moving.
	sint64 go_on_ticks;

#ifdef USE_DIFFERENT_WIND
	static uint8 get_approach_ribi( koord3d start, koord3d ziel );
#endif
	//// only used for route search and approach vectors of get_ribi() (do not need saving)
	//koord3d search_start;
	//koord3d search_end;

	flight_state state; // functions needed for the search without destination from find_route

	sint16 flying_height;
	sint16 target_height;
	uint32 search_for_stop, touchdown, takeoff;

	sint16 altitude_level; // for AFHP
	sint16 landing_distance; // for AFHP

	void calc_altitude_level(sint32 speed_limit_kmh){
		altitude_level = max(5, speed_limit_kmh/33);
		altitude_level = min(altitude_level, 30);
		landing_distance = altitude_level - 1;
	}
	// BG, 07.08.2012: extracted from calc_route()
	route_t::route_result_t calc_route_internal(
		karte_t *welt,
		const koord3d &start,
		const koord3d &ziel,
		sint32 max_speed,
		uint32 weight,
		air_vehicle_t::flight_state &state,
		sint16 &flying_height,
		sint16 &target_height,
		bool &runway_too_short,
		bool &airport_too_close_to_the_edge,
		uint32 &takeoff,
		uint32 &touchdown,
		uint32 &search_for_stop,
		route_t &route);

protected:
	// jumps to next tile and correct the height ...
	void hop(grund_t*) OVERRIDE;

	bool check_next_tile(const grund_t *bd) const OVERRIDE;

	void enter_tile(grund_t*) OVERRIDE;
	void leave_tile() OVERRIDE;

	int block_reserver( uint32 start, uint32 end, bool reserve ) const;

	// find a route and reserve the stop position
	bool find_route_to_stop_position();

	void unreserve_runway() OVERRIDE;

public:
	air_vehicle_t(loadsave_t *file, bool is_leading, bool is_last);
	air_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv); // start and schedule

	// to shift the events around properly
	void get_event_index( flight_state &state_, uint32 &takeoff_, uint32 &stopsearch_, uint32 &landing_ ) { state_ = state; takeoff_ = takeoff; stopsearch_ = search_for_stop; landing_ = touchdown; }
	void set_event_index( flight_state state_, uint32 takeoff_, uint32 stopsearch_, uint32 landing_ ) { state = state_; takeoff = takeoff_; search_for_stop = stopsearch_; touchdown = landing_; }

	// since we are drawing ourselves, we must mark ourselves dirty during deletion
	virtual ~air_vehicle_t();

	waytype_t get_waytype() const OVERRIDE { return air_wt; }

	// returns true for the way search to an unknown target.
	bool is_target(const grund_t *,const grund_t *) OVERRIDE;

	//bool can_takeoff_here(const grund_t *gr, ribi_t::ribi test_dir, uint8 len) const;

	// return valid direction
	ribi_t::ribi get_ribi(const grund_t* ) const OVERRIDE;

	// how expensive to go here (for way search)
	int get_cost(const grund_t *, const sint32 max_speed, ribi_t::ribi from) OVERRIDE;

	bool can_enter_tile(const grund_t *gr_next, sint32 &restart_speed, uint8) OVERRIDE;

	void set_convoi(convoi_t *c) OVERRIDE;

	route_t::route_result_t calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route) OVERRIDE;

	// BG, 08.08.2012: extracted from can_enter_tile()
	route_t::route_result_t reroute(const uint16 reroute_index, const koord3d &ziel) OVERRIDE;

#ifdef INLINE_OBJ_TYPE
#else
	typ get_typ() const OVERRIDE { return air_vehicle; }
#endif

	schedule_t *generate_new_schedule() const OVERRIDE;

	void rdwr_from_convoi(loadsave_t *file) OVERRIDE;

	int get_flyingheight() const {return flying_height-get_hoff()-2;}

	void force_land() { flying_height = 0; target_height = 0; state = taxiing_to_halt; }

	// image: when flying empty, on ground the plane
	image_id get_image() const OVERRIDE {return !is_on_ground() ? IMG_EMPTY : image;}

	// image: when flying the shadow, on ground empty
	image_id get_outline_image() const OVERRIDE {return !is_on_ground() ? image : IMG_EMPTY;}

	// shadow has black color (when flying)
	FLAGGED_PIXVAL get_outline_colour() const OVERRIDE {return !is_on_ground() ? TRANSPARENT75_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK) : 0;}

#ifdef MULTI_THREAD
	// this draws the "real" aircrafts (when flying)
	void display_after(int xpos, int ypos, const sint8 clip_num) const OVERRIDE;

	// this routine will display a tooltip for lost, on depot order, and stuck vehicles
	void display_overlay(int xpos, int ypos) const OVERRIDE;
#else
	// this draws the "real" aircrafts (when flying)
	void display_after(int xpos, int ypos, bool dirty) const OVERRIDE;
#endif

	// the drag calculation happens it calc_height
	void calc_drag_coefficient(const grund_t*) OVERRIDE {}

	bool is_on_ground() const { return flying_height==0  &&  !(state==circling  ||  state==flying); }

	// Used for running cost calculations
	bool is_using_full_power() const { return state != circling && state != taxiing; }
	const char *is_deletable(const player_t *player) OVERRIDE;


	virtual bool is_flying() const OVERRIDE { return !is_on_ground(); }

	bool runway_too_short;
	bool airport_too_close_to_the_edge;
	bool is_runway_too_short() {return runway_too_short; }
	bool is_airport_too_close_to_the_edge() { return airport_too_close_to_the_edge; }
	virtual sint32 get_takeoff_route_index() const OVERRIDE { return (sint32) takeoff; }
	virtual sint32 get_touchdown_route_index() const OVERRIDE { return (sint32) touchdown; }
};

#endif
