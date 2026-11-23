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
		bool leading = true;          ///< front vehicle of a convoy, or a private car
		bool overtaking;              ///< travelling in the passing lane
		bool pulled_over;             ///< stopped at a stop stand beside the travel lane
	};

	/// The distance in vehicle steps that a follower must keep clear behind
	/// the vehicle ahead, in the same lateral position. @p leader_moving
	/// selects the reaction-time headway instead of the standstill gap.
	uint32 following_gap(const sint32 follower_speed, const bool leader_moving);

	/**
	 * The highest speed a follower may use at @p gap_steps behind a leader.
	 * The standstill gap and reaction time of following_gap() give the
	 * equilibrium gap at the leader's speed; the follower may use the distance
	 * beyond that gap to close it over one reaction time. The result is in
	 * internal speed units and is clamped to zero; it may exceed the caller's
	 * desired speed, in which case the caller applies no cap. A negative
	 * @p gap_steps means that there is no leader and returns SPEED_UNLIMITED.
	 */
	sint32 safe_following_speed(const sint32 gap_steps, const sint32 leader_speed, const bool leader_moving);

	/// The nearest vehicle ahead of a follower in the same lateral position,
	/// as found by following_limit(). A gap of -1 means that no vehicle ahead
	/// constrains the follower.
	struct leader_t
	{
		sint16 gap = -1;     ///< steps from the follower's front to the leader's rear
		uint16 speed = 0;    ///< the leader's speed, internal units
		bool moving = false; ///< whether the leader is moving
		convoihandle_t convoy; ///< the leader convoy, or unbound for a private car
	};

	/// Forward distance in vehicle steps over which a full lane offset is
	/// blended when a road vehicle changes lane or overtakes.
	const uint32 LANE_CHANGE_BLEND_STEPS = 192;

	/// Lateral blend level for a vehicle that is being overtaken, in 256ths of
	/// the full lane offset and opposite to the overtaking direction.
	const sint16 OVERTAKEN_LATERAL_LEVEL = -51;

	/**
	 * Advances a signed lateral blend from @p current towards @p target by no
	 * more than the forward distance travelled. @p current and @p target are in
	 * 256ths of the full lateral offset; @p forward_steps is the forward distance
	 * since the last call and @p blend_steps is the forward distance over which
	 * the full offset is covered. This keeps the lateral movement no faster than
	 * the forward movement, so a vehicle never appears to slide sideways.
	 */
	sint16 advance_lateral_blend(const sint16 current, const sint16 target, const uint32 forward_steps, const uint32 blend_steps);

	/**
	 * The position in vehicle steps that the user described by @p self may not
	 * pass on @p gr, or -1 when no vehicle in the same lateral position
	 * constrains it. @p steps is the user's front position on @p gr and
	 * @p steps_next the last step of that tile. A vehicle whose front has
	 * entered the tile in front of @p gr is also considered while its rear is
	 * still on @p gr. When @p leader is not NULL, it receives the nearest
	 * vehicle ahead in the same lateral position.
	 */
	sint32 following_limit(const grund_t *gr, const user_t &self, const uint8 steps, const uint8 steps_next, leader_t *leader = NULL);

	/**
	 * Whether the blocker @p leader is far enough ahead on @p gr that the user
	 * described by @p self may enter the tile and follow it. Only a private car
	 * or the leading vehicle of a convoy can clear a blocker: a trailing convoy
	 * vehicle's own length does not prove that the convoy's rear is clear.
	 */
	bool can_follow(const grund_t *gr, const user_t &self, const vehicle_base_t *leader);

	/// Whether @p v is a road vehicle that is standing at a stop stand beside
	/// the travel lane, so that it does not block traffic in position 0.
	bool is_beside_lane(const vehicle_base_t *v);

	/**
	 * Drives @p v by at most one tile boundary, stopping at the limit given by
	 * following_limit() on the tile it is on. The caller must refresh @p self
	 * and re-read steps after a hop, and must not add more distance than the
	 * value returned. When @p leader is not NULL, it receives the nearest
	 * vehicle ahead in the same lateral position.
	 * @return the distance travelled in yards.
	 * @note This is a helper for the road-specific do_drive() overrides. It is
	 * never called for a vehicle that is not leading its convoy, because a
	 * convoy follows the leading vehicle exactly.
	 */
	uint32 drive_until_limit(vehicle_base_t *v, const user_t &self, const uint8 steps, const uint8 steps_next, const uint32 distance, leader_t *leader = NULL);
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
	/// Automatic-road-finder placeholder vehicle.
	uint8 is_checker : 1;

	/**
	 * The stop stand reserved for this convoy's leading vehicle, as a four-bit
	 * mask (see haltestelle_t::get_empty_lane), or zero when no stand is held.
	 * Only the leading vehicle records it. Packed with is_checker so that the
	 * member costs no object memory. Saved with the vehicle, behind Extended
	 * save revision 20.
	 */
	uint8 stand_mask : 4;

	/**
	 * Rendering-only blend of the lateral pull-in position: 0 is the travel
	 * lane and 256 is fully at the stand. update_lateral_blend() advances it
	 * with the forward distance travelled, so a vehicle glides in and out of
	 * the stand instead of jumping. Only the leading vehicle updates it; every
	 * vehicle of the convoy reads the leading vehicle's value. Not saved and
	 * not synced.
	 */
	mutable uint16 pull_in_factor;
	/// Lateral position (0 to 3) that pull_in_factor is blending towards.
	/// 255 marks the blend as not yet initialised after creation or loading.
	mutable uint8 pull_in_lateral;

	/**
	 * Rendering-only signed blend of the overtaking lane offset: 0 is the
	 * normal lane, 256 is the passing lane and OVERTAKEN_LATERAL_LEVEL is the
	 * small offset for a vehicle being overtaken. It is advanced with the
	 * forward distance travelled by the same update as the pull-in blend.
	 */
	mutable sint16 overtake_level;

	/// Render-only cumulative forward distance in yards, and the value at the
	/// last lateral blend update. Their difference caps the lateral movement so
	/// that it never exceeds the vehicle's forward movement.
	mutable uint32 lateral_distance;
	mutable uint32 lateral_last_distance;

	/**
	 * The nearest vehicle ahead in the same lateral position, from the last
	 * call to do_drive(). It is derived from the map each step and is not
	 * saved. convoi_t::calc_acceleration() uses it to limit the leading
	 * vehicle's desired speed to the speed of the vehicle in front.
	 */
	road_space::leader_t following_leader;

public:
	void enter_tile(grund_t*) OVERRIDE;
	void leave_tile() OVERRIDE;
	virtual void hop(grund_t*) OVERRIDE;
	virtual uint32 do_drive(uint32 distance) OVERRIDE;

	void rdwr_from_convoi(loadsave_t *file) OVERRIDE;

	/// The nearest vehicle ahead recorded by the last do_drive() call.
	const road_space::leader_t &get_following_leader() const { return following_leader; }

	/**
	 * Advances the rendering-only pull-in and overtaking blends from the
	 * forward distance travelled. Called on the leading vehicle once per sync
	 * step on the main thread.
	 */
	void update_lateral_blend() const;

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
