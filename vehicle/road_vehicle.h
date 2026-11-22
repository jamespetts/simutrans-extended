/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_ROAD_VEHICLE_H
#define VEHICLE_ROAD_VEHICLE_H


#include "vehicle.h"

class grund_t;

/**
 * Road-space rules shared by player road vehicles and private cars.
 *
 * A vehicle's position on a tile is the value of vehicle_base_t::get_steps():
 * 0 at the tile entry and steps_next at the tile exit. That value is the
 * position of the front of the vehicle; the body extends backwards from it by
 * the vehicle's length in vehicle steps.
 *
 * Vehicles occupy one of four lateral positions: the traffic lane or the
 * passing lane, each in its normal position or pulled over to the side. Only
 * vehicles in the same lateral position and travelling in the same direction
 * follow one another. Elsewhere one vehicle per lane is kept, as before.
 */
namespace road_space
{
	/**
	 * Whether this tile may hold more than one vehicle in one lateral
	 * position: a straight road tile which is neither an intersection nor a
	 * crossing. Diagonal road tiles are excluded.
	 */
	bool allows_queueing(const grund_t *gr);

	/// The state of one road user that the following rules need.
	struct user_t
	{
		const vehicle_base_t *object; ///< the vehicle itself, to skip it
		const convoi_t *convoi;       ///< owning convoy, or NULL for a private car
		ribi_t::ribi direction;       ///< direction of travel
		sint32 speed;                 ///< current speed, internal units
		uint32 length;                ///< length in vehicle steps
		bool overtaking;              ///< travelling in the passing lane
		bool pulled_over;             ///< stopped at a stop stand beside the travel lane
	};

	/// The distance in vehicle steps that a follower must keep clear behind
	/// the vehicle ahead, in the same lateral position. @p leader_moving
	/// selects the reaction-time headway instead of the standstill gap.
	uint32 following_gap(const sint32 follower_speed, const bool leader_moving);

	/// The position in vehicle steps that the user described by @p self may not
	/// pass on @p gr, or -1 when no vehicle in the same lateral position
	/// constrains it.
	sint32 following_limit(const grund_t *gr, const user_t &self, const uint8 steps);

	/// Whether the blocker @p leader is far enough ahead on @p gr that the user
	/// described by @p self may enter the tile and follow it.
	bool can_follow(const grund_t *gr, const user_t &self, const vehicle_base_t *leader);

	/**
	 * Drives @p v by at most one tile boundary, stopping at the limit given by
	 * following_limit() on the tile it is on. The caller must refresh @p self
	 * and re-read steps after a hop, and must not add more distance than the
	 * value returned.
	 * @return the distance travelled in yards.
	 * @note This is a helper for the road-specific do_drive() overrides. It is
	 * never called for a vehicle that is not leading its convoy, because a
	 * convoy follows the leading vehicle exactly.
	 */
	uint32 drive_until_limit(vehicle_base_t *v, const user_t &self, const uint8 steps, const uint8 steps_next, const uint32 distance);
}


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

	// Clears a blocker that this vehicle may follow onto a queueing tile.
	void apply_following_rule(const grund_t *gr, vehicle_base_t *&blocker, sint8 lane) const;

public:
	bool check_next_tile(const grund_t *bd) const OVERRIDE;

	/// Whether this vehicle is standing at a stop stand beside the travel lane.
	bool is_pulled_over() const;

protected:
	bool is_checker;

	/**
	 * The stop stand reserved for this convoy's leading vehicle, as a four-bit
	 * mask (see haltestelle_t::get_empty_lane), or zero when no stand is held.
	 * Only the leading vehicle records it. Saved with the vehicle, behind
	 * Extended save revision 20.
	 */
	uint8 stand_mask;

public:
	void enter_tile(grund_t*) OVERRIDE;
	virtual void hop(grund_t*) OVERRIDE;
	virtual uint32 do_drive(uint32 distance) OVERRIDE;

	void rdwr_from_convoi(loadsave_t *file) OVERRIDE;

	uint8 get_stand_mask() const { return stand_mask; }
	void set_stand_mask(uint8 mask) { stand_mask = mask; }

	/**
	 * The lateral position (0 to 3, see the road_space comment) at which this
	 * vehicle is drawn while it loads at its stand. A through stop gives one
	 * stand on each side, so the vehicle is pulled over in its traffic lane
	 * (position 1); a terminal uses the four lateral positions directly.
	 */
	uint8 get_stand_lateral_position() const;

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

	// Releases the target halt position reservation. Main thread only;
	// see vehicle_t::release_target_reservations.
	void release_target_reservations() OVERRIDE;

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
