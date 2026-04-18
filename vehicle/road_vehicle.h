/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_ROAD_VEHICLE_H
#define VEHICLE_ROAD_VEHICLE_H


#include "vehicle.h"


/**
 * A class for road vehicles. Manages the look of the vehicles
 * and the navigability of tiles.
 * @see vehicle_t
 */
class road_vehicle_t : public vehicle_t, public traffic_vehicle_t
{
private:
	// called internally only from can_enter_tile()
	// returns true on success
	bool choose_route(sint32 &restart_speed, ribi_t::ribi start_direction, uint16 index);

public:
	bool check_next_tile(const grund_t *bd) const OVERRIDE;

protected:
	bool is_checker;

public:
	void enter_tile(grund_t*) OVERRIDE;
	virtual void hop(grund_t*) OVERRIDE;
	virtual uint32 do_drive(uint32 distance) OVERRIDE;

	void rotate90() OVERRIDE;

	void calc_disp_lane();

	waytype_t get_waytype() const OVERRIDE { return road_wt; }

	road_vehicle_t(loadsave_t *file, bool first, bool last);
	road_vehicle_t();
	road_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv); // start und schedule

	uint32 get_max_speed() override;

	void set_convoi(convoi_t *c) OVERRIDE;

	// how expensive to go here (for way search)
	int get_cost(const grund_t *, const sint32 max_speed, ribi_t::ribi from) OVERRIDE;

	virtual route_t::route_result_t calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route) OVERRIDE;

	bool can_enter_tile(const grund_t *gr_next, sint32 &restart_speed, uint8 second_check_count) OVERRIDE;

	// returns true for the way search to an unknown target.
	bool is_target(const grund_t *,const grund_t *) OVERRIDE;

	// since we must consider overtaking, we use this for offset calculation
	virtual void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width, bool prev_based ) const;
	virtual void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const OVERRIDE { get_screen_offset(xoff,yoff,raster_width,false); }

#ifndef INLINE_OBJ_TYPE
	obj_t::typ get_typ() const OVERRIDE { return road_vehicle; }
#endif

	schedule_t * generate_new_schedule() const OVERRIDE;

	overtaker_t* get_overtaker() OVERRIDE;
	virtual convoi_t* get_overtaker_cv() OVERRIDE;

	virtual vehicle_base_t* other_lane_blocked(const bool only_search_top = false, sint8 offset = 0) const;
	virtual vehicle_base_t* other_lane_blocked_offset() const { return other_lane_blocked(false,1); }
};



#endif
