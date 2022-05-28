/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_VEHICLE_H
#define VEHICLE_VEHICLE_H


#include <limits>
#include <string>
#include "../simtypes.h"
#include "../simworld.h"
#include "../obj/simobj.h"
#include "../halthandle_t.h"
#include "../convoihandle_t.h"
#include "../ifc/simtestdriver.h"
#include "../boden/grund.h"
#include "../descriptor/vehicle_desc.h"
#include "../vehicle/overtaker.h"
#include "../tpl/slist_tpl.h"
#include "../tpl/array_tpl.h"
#include "../dataobj/route.h"


#include "../tpl/fixed_list_tpl.h"

class convoi_t;
class schedule_t;
class signal_t;
class ware_t;
class schiene_t;
class strasse_t;
//class karte_ptr_t;

// for aircraft:
// length of the holding pattern.
#define HOLDING_PATTERN_LENGTH 16
// offset of end tile of the holding pattern before touchdown tile.
#define HOLDING_PATTERN_OFFSET 3
/*----------------------- Movables ------------------------------------*/

class traffic_vehicle_t
{
	private:
		sint64 time_at_last_hop; // ticks
		uint32 dist_travelled_since_last_hop; // yards
		virtual uint32 get_max_speed() { return 0; } // returns y/t
	protected:
		inline void reset_measurements()
		{
			dist_travelled_since_last_hop = 0; //yards
			time_at_last_hop = world()->get_ticks(); //ticks
		}
		inline void add_distance(uint32 distance) { dist_travelled_since_last_hop += distance; } // yards
		void flush_travel_times(strasse_t*); // calculate travel times, write to way, reset measurements
};

/**
 * Base class for all vehicles
 */
class vehicle_base_t : public obj_t
{
	// BG, 15.02.2014: gr and weg are cached in enter_tile() and reset to NULL in leave_tile().
	grund_t* gr;
	weg_t* weg;
public:
	inline grund_t* get_grund() const
	{
		if (!gr)
			return welt->lookup(get_pos());
		return gr;
	}
	inline weg_t* get_weg() const
	{
		if (!weg)
		{
			// gr and weg are both initialized in enter_tile(). If there is a gr but no weg, then e.g. for ships there IS no way.
			if (!gr)
			{
				// get a local pointer only. Do not assign to instances gr that has to be done by enter_tile() only.
				grund_t* gr2 = get_grund();
				if (gr2)
					return gr2->get_weg(get_waytype());
			}
		}
		return weg;
	}
protected:
	// offsets for different directions
	static sint8 dxdy[16];

	// to make the length on diagonals configurable
	// Number of vehicle steps along a diagonal...
	// remember to subtract one when stepping down to 0
	static uint8 diagonal_vehicle_steps_per_tile;
	static uint8 old_diagonal_vehicle_steps_per_tile;
	static uint16 diagonal_multiplier;

	// [0]=xoff [1]=yoff
	static sint8 overtaking_base_offsets[8][2];

	/**
	 * Actual travel direction in screen coordinates
	 */
	ribi_t::ribi direction;

	// true on slope (make calc_height much faster)
	uint8 use_calc_height:1;

	// if true, use offsets to emulate driving on other side
	uint8 drives_on_left:1;

	/**
	* Thing is moving on this lane.
	* Possible values:
	* (Back)
	* 0 - sidewalk (going on the right side to w/sw/s)
	* 1 - road     (going on the right side to w/sw/s)
	* 2 - middle   (everything with waytype != road)
	* 3 - road     (going on the right side to se/e/../nw)
	* 4 - sidewalk (going on the right side to se/e/../nw)
	* (Front)
	*/
	uint8 disp_lane : 3;

	sint8 dx, dy;

	// number of steps in this tile (255 per tile)
	uint8 steps, steps_next;

	/**
	 * Next position on our path
	 */
	koord3d pos_next;

	/**
	 * Offsets for uphill/downhill.
	 * Have to be multiplied with -TILE_HEIGHT_STEP/2.
	 * To obtain real z-offset, interpolate using steps, steps_next.
	 */
	uint8 zoff_start:4, zoff_end:4;

	// cached image
	image_id image;

	// The current livery of this vehicle.
	// @author: jamespetts, April 2011
	std::string current_livery;

	/**
	 * this vehicle will enter passing lane in the next tile -> 1
	 * this vehicle will enter traffic lane in the next tile -> -1
	 * Unclear -> 0
	 * @author THLeaderH
	 */
	sint8 next_lane;

	/**
	 * Vehicle movement: check whether this vehicle can enter the next tile (pos_next).
	 * @returns NULL if check fails, otherwise pointer to the next tile
	 */
	virtual grund_t* hop_check() = 0;

	/**
	 * Vehicle movement: change tiles, calls leave_tile and enter_tile.
	 * @param gr pointer to ground of new position (never NULL)
	 */
	virtual void hop(grund_t* gr) = 0;

	virtual void update_bookkeeping(uint32 steps) = 0;

	void calc_image() OVERRIDE = 0;

	virtual void unreserve_runway() { return; }

	// check for road vehicle, if next tile is free
	vehicle_base_t *get_blocking_vehicle(const grund_t *gr, const convoi_t *cnv, const uint8 current_direction, const uint8 next_direction, const uint8 next_90direction, const private_car_t *pcar, sint8 lane_on_the_tile );

	// If true, two vehicles might crash by lane crossing.
	bool judge_lane_crossing( const uint8 current_direction, const uint8 next_direction, const uint8 other_next_direction, const bool is_overtaking, const bool forced_to_change_lane ) const;

	// only needed for old way of moving vehicles to determine position at loading time
	bool is_about_to_hop( const sint8 neu_xoff, const sint8 neu_yoff ) const;

	// Players are able to reassign classes of accommodation in vehicles manually
	// during the game. Track these reassignments here with this array.
	uint8 *class_reassignments;

public:
	// only called during load time: set some offsets
	static void set_diagonal_multiplier( uint32 multiplier, uint32 old_multiplier );
	static uint16 get_diagonal_multiplier() { return diagonal_multiplier; }
	static uint8 get_diagonal_vehicle_steps_per_tile() { return diagonal_vehicle_steps_per_tile; }

	static void set_overtaking_offsets( bool driving_on_the_left );

	// if true, this convoi needs to restart for correct alignment
	bool need_realignment() const;

	virtual uint32 do_drive(uint32 dist); // basis movement code

	inline void set_image( image_id b ) { image = b; }
	image_id get_image() const OVERRIDE {return image;}

	sint16 get_hoff(const sint16 raster_width = 1) const;
	uint8 get_steps() const {return steps;} // number of steps pass on the current tile.
	uint8 get_steps_next() const {return steps_next;} // total number of steps to pass on the current tile - 1. Mostly VEHICLE_STEPS_PER_TILE - 1 for straight route or diagonal_vehicle_steps_per_tile - 1 for a diagonal route.

	uint8 get_disp_lane() const { return disp_lane; }

	// to make smaller steps than the tile granularity, we have to calculate our offsets ourselves!
	virtual void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const;

	/**
	 * Vehicle movement: calculates z-offset of vehicles on slopes,
	 * handles vehicles that are invisible in tunnels.
	 * @param gr vehicle is on this ground
	 * @note has to be called after loading to initialize z-offsets
	 */
	void calc_height(grund_t *gr = NULL);

	void rotate90() OVERRIDE;

	template<class K1, class K2>
	static ribi_t::ribi calc_direction(const K1& from, const K2& to)
	{
		return ribi_type(from, to);
	}

	ribi_t::ribi calc_set_direction(const koord3d& start, const koord3d& ende);
	uint16 get_tile_steps(const koord &start, const koord &ende, /*out*/ ribi_t::ribi &direction) const;

	ribi_t::ribi get_direction() const {return direction;}

	ribi_t::ribi get_90direction() const {return ribi_type(get_pos(), get_pos_next());}

	koord3d get_pos_next() const {return pos_next;}

	waytype_t get_waytype() const OVERRIDE = 0;

	void set_class_reassignment(uint8 original_class, uint8 new_class);

	// true, if this vehicle did not moved for some time
	virtual bool is_stuck() { return true; }

	/**
	 * Vehicle movement: enter tile, add this to the ground.
	 * @pre position (obj_t::pos) needs to be updated prior to calling this functions
	 * @return pointer to ground (never NULL)
	 */
	virtual void enter_tile(grund_t*);

	/**
	 * Vehicle movement: leave tile, release reserved crossing, remove vehicle from the ground.
	 */
	virtual void leave_tile();

	virtual overtaker_t *get_overtaker() { return NULL; }
	virtual convoi_t* get_overtaker_cv() { return NULL; }

#ifdef INLINE_OBJ_TYPE
protected:
	vehicle_base_t(typ type);
	vehicle_base_t(typ type, koord3d pos);
#else
	vehicle_base_t();

	vehicle_base_t(koord3d pos);
#endif

	virtual bool is_flying() const { return false; }
};


template<> inline vehicle_base_t* obj_cast<vehicle_base_t>(obj_t* const d)
{
	return d->is_moving() ? static_cast<vehicle_base_t*>(d) : 0;
}


/**
 * Class for all vehicles with route
 */
class vehicle_t : public vehicle_base_t, public test_driver_t
{
private:
	/**
	* Date of purchase in months
	*/
	sint32 purchase_time;

	/**
	* For the more physical acceleration model friction is introduced
	* frictionforce = gamma*speed*weight
	* since the total weight is needed a lot of times, we save it
	*/
	uint32 sum_weight;

	grund_t* hop_check() OVERRIDE;

	/**
	 * Calculate friction caused by slopes and curves.
	 */
	virtual void calc_drag_coefficient(const grund_t *gr);

	sint32 calc_modified_speed_limit(koord3d position, ribi_t::ribi current_direction, bool is_corner);

	bool load_freight_internal(halthandle_t halt, bool overcrowd, bool *skip_vehicles, bool use_lower_classes);

	// Cornering settings.

	fixed_list_tpl<sint16, 192> pre_corner_direction;

	sint16 direction_steps;

	uint8 hill_up;
	uint8 hill_down;
	bool is_overweight;

	// Whether this individual vehicle is reversed.
	bool reversed;

	uint16 diagonal_costs;
	uint16 base_costs;

	/// This is the last tile on which this vehicle stopped: useful
	/// for logging traffic congestion
	koord3d last_stopped_tile;

protected:
	void hop(grund_t*) OVERRIDE;

	void update_bookkeeping(uint32 steps) OVERRIDE;

	// current limit (due to track etc.)
	sint32 speed_limit;

	ribi_t::ribi previous_direction;

	//uint16 target_speed[16];

	//const koord3d *lookahead[16];

	// for target reservation and search
	halthandle_t target_halt;

	/** The friction is calculated new every step, so we save it too
	*/
	sint16 current_friction;

	/**
	* Current index on the route
	*/
	uint16 route_index;

	uint16 total_freight; // since the sum is needed quite often, it is cached (not differentiated by class)
	slist_tpl<ware_t> *fracht;  // list of goods being transported (array for each class)

	const vehicle_desc_t *desc;

	convoi_t *cnv;  // != NULL if the vehicle is part of a Convoi

	/**
	 * Previous position on our path
	 */
	koord3d pos_prev;

	uint8 number_of_classes;

	bool leading:1; // true, if vehicle is first vehicle of a convoi
	bool last:1;    // true, if vehicle is last vehicle of a convoi
	bool smoke:1;
	bool check_for_finish:1; // true, if on the last tile
	bool has_driven:1;

	void calc_image() OVERRIDE;

	bool check_access(const weg_t* way) const;

	/// Register this vehicle as having stopped on a tile, if it has not already done so.
	void log_congestion(strasse_t* road);

public:
	sint32 calc_speed_limit(const weg_t *weg, const weg_t *weg_previous, fixed_list_tpl<sint16, 192>* cornering_data, uint32 bridge_tiles, ribi_t::ribi current_direction, ribi_t::ribi previous_direction);

	bool check_next_tile(const grund_t* ) const OVERRIDE {return false;}

	bool check_way_constraints(const weg_t &way) const;

	uint8 hop_count;

//public:
	// the coordinates, where the vehicle was loaded the last time
	koord3d last_stop_pos;

	convoi_t *get_convoi() const { return cnv; }

	void rotate90() OVERRIDE;

	ribi_t::ribi get_previous_direction() const { return previous_direction; }

	/**
	 * Method checks whether next tile is free to move on.
	 * Looks up next tile, and calls @ref can_enter_tile(const grund_t*, sint32&, uint8).
	 */
	bool can_enter_tile(sint32 &restart_speed, uint8 second_check_count);

	/**
	 * Method checks whether next tile is free to move on.
	 * @param gr_next next tile, must not be NULL
	 */
	virtual bool can_enter_tile(const grund_t *gr_next, sint32 &restart_speed, uint8 second_check_count) = 0;

	void enter_tile(grund_t*) OVERRIDE;

	void leave_tile() OVERRIDE;

	waytype_t get_waytype() const OVERRIDE = 0;

	/**
	* Determine the direction bits for this kind of vehicle.
	*/
	ribi_t::ribi get_ribi(const grund_t* gr) const OVERRIDE { return gr->get_weg_ribi(get_waytype()); }

	sint32 get_purchase_time() const {return purchase_time;}

	void get_smoke(bool yesno ) { smoke = yesno;}

	virtual route_t::route_result_t calc_route(koord3d start, koord3d ziel, sint32 max_speed_kmh, bool is_tall, route_t* route);
	uint16 get_route_index() const {return route_index;}
	void set_route_index(uint16 value) { route_index = value; }
	const koord3d get_pos_prev() const {return pos_prev;}

	virtual route_t::route_result_t reroute(const uint16 reroute_index, const koord3d &ziel);

	/**
	* Get the base image.
	*/
	image_id get_base_image() const { return desc->get_base_image(current_livery.c_str()); }

	/**
	 * @return image with base direction and freight image taken from loaded cargo
	 */
	image_id get_loaded_image() const;

	/**
	* @return vehicle description object
	*/
	const vehicle_desc_t *get_desc() const {return desc; }
	void set_desc(const vehicle_desc_t* value);

	/**
	* @return die running_cost in Cr/100Km
	*/
	int get_running_cost() const { return desc->get_running_cost(); }
	int get_running_cost(const karte_t* welt) const { return desc->get_running_cost(welt); }

	/**
	* @return fixed maintenance costs in Cr/100months
	* @author Bernd Gabriel
	*/
	uint32 get_fixed_cost() const { return desc->get_fixed_cost(); }
	uint32 get_fixed_cost(karte_t* welt) const { return desc->get_fixed_cost(welt); }

	/**
	* Play sound, when the vehicle is visible on screen
	*/
	void play_sound() const;

	/**
	 * Prepare vehicle for new ride.
	 * Sets route_index, pos_next, steps_next.
	 * If @p recalc is true this sets position and recalculates/resets movement parameters.
	 */
	void initialise_journey( uint16 start_route_index, bool recalc );

	void set_direction_steps(sint16 value) { direction_steps = value; }

	void fix_class_accommodations();

	inline koord3d get_last_stop_pos() const { return last_stop_pos;  }

#ifdef INLINE_OBJ_TYPE
protected:
	vehicle_t(typ type);
	vehicle_t(typ type, koord3d pos, const vehicle_desc_t* desc, player_t* player);
public:
#else
	vehicle_t();
	vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player);
#endif

	~vehicle_t();

	/// Note that the original class is the accommodation index *not* the previously re-assigned class (if different)
	void set_class_reassignment(uint8 original_class, uint8 new_class);

	void make_smoke() const;

	void show_info() OVERRIDE;

	/**
	 * return friction constant: changes in hill and curves; may even negative downhill *
	 */
	inline sint16 get_frictionfactor() const { return current_friction; }

	/**
	 * Return total weight including freight (in kg!)
	 */
	inline uint32 get_total_weight() const { return sum_weight; }

	bool get_is_overweight() { return is_overweight; }

	// returns speedlimit of ways (and if convoi enters station etc)
	// the convoi takes care of the max_speed of the vehicle
	// In Extended this is mostly for entering stations etc.,
	// as the new physics engine handles ways
	sint32 get_speed_limit() const { return speed_limit; }
	static inline sint32 speed_unlimited() {return (std::numeric_limits<sint32>::max)(); }

	const slist_tpl<ware_t> & get_cargo(uint8 g_class) const { return fracht[g_class];}   // list of goods being transported (indexed by accommodation class)

	/**
	 * Rotate freight target coordinates, has to be called after rotating factories.
	 */
	void rotate90_freight_destinations(const sint16 y_size);

	/**
	* Calculate the total quantity of goods moved
	*/
	uint16 get_total_cargo() const { return total_freight; }

	uint16 get_total_cargo_by_class(uint8 g_class) const;

	uint16 get_reassigned_class(uint8 g_class) const;

	uint8 get_number_of_fare_classes() const;

	/**
	* Calculate transported cargo total weight in KG
	*/
	uint32 get_cargo_weight() const;

	/**
	* get the type of cargo this vehicle can transport
	*/
	const goods_desc_t* get_cargo_type() const { return desc->get_freight_type(); }

	/**
	* Get the maximum capacity
	*/
	uint16 get_cargo_max() const {return desc->get_total_capacity(); }

	ribi_t::ribi get_next_90direction() const;

	const char * get_cargo_mass() const;

	/**
	* create an info text for the freight
	* e.g. to display in a info window
	*/
	void get_cargo_info(cbuffer_t & buf) const;

	// Check for straightness of way.
	//@author jamespetts

	enum direction_degrees {
		North = 360,
		Northeast = 45,
		East = 90,
		Southeast = 135,
		South = 180,
		Southwest = 225,
		West = 270,
		Northwest = 315,
	};

	direction_degrees get_direction_degrees(ribi_t::ribi) const;

	sint16 compare_directions(sint16 first_direction, sint16 second_direction) const;

	/**
	* Delete all vehicle load
	*/
	void discard_cargo();

	/**
	* Payment is done per hop. It iterates all goods and calculates
	* the income for the last hop. This method must be called upon
	* every stop.
	* @return income total for last hop
	*/
	//sint64  calc_gewinn(koord start, koord end, convoi_t* cnv) const;


	// sets or querey begin and end of convois
	void set_leading(bool value) {leading = value;}
	bool is_leading() const {return leading;}

	void set_last(bool value) {last = value;}
	bool is_last() const {return last;}

	// marks the vehicle as really used
	void set_driven() { has_driven = true; }

	virtual void set_convoi(convoi_t *c);

	/**
	 * Unload freight to halt
	 * @return sum of unloaded goods
	 */
	uint16 unload_cargo(halthandle_t halt, sint64 & revenue_from_unloading, array_tpl<sint64> & apportioned_revenues );

	/**
	 * Load freight from halt
	 * @return amount loaded
	 */
	uint16 load_cargo(halthandle_t halt)  { bool dummy; (void)dummy; return load_cargo(halt, false, &dummy, &dummy, true); }
	uint16 load_cargo(halthandle_t halt, bool overcrowd, bool *skip_convois, bool *skip_vehicles, bool use_lower_classes);

	/**
	* Remove freight that no longer can reach it's destination
	* i.e. because of a changed schedule
	*/
	void remove_stale_cargo();

	/**
	* Generate a matching schedule for the vehicle type
	*/
	virtual schedule_t *generate_new_schedule() const = 0;

	const char *is_deletable(const player_t *player) OVERRIDE;

	void rdwr(loadsave_t *file) OVERRIDE;
	virtual void rdwr_from_convoi(loadsave_t *file);

	uint32 calc_sale_value() const;

	// true, if this vehicle did not moved for some time
	bool is_stuck() OVERRIDE;

	// this routine will display a tooltip for lost, on depot order, and stuck vehicles
#ifdef MULTI_THREAD
	void display_overlay(int xpos, int ypos) const OVERRIDE;
#else
	void display_after(int xpos, int ypos, bool dirty) const OVERRIDE;
#endif

	bool is_reversed() const { return reversed; }
	void set_reversed(bool value);

	// Gets modified direction, used for drawing
	// vehicles in reverse formation.
	ribi_t::ribi get_direction_of_travel() const;

	uint32 get_sum_weight() const { return sum_weight; }

	uint16 get_overcrowded_capacity(uint8 g_class) const;
	// @author: jamespetts
	uint16 get_overcrowding(uint8 g_class) const;

	// @author: jamespetts
	uint8 get_comfort(uint8 catering_level = 0, uint8 g_class = 0) const;

	uint16 get_accommodation_capacity(uint8 g_class, bool include_lower_classes = false) const;
	uint16 get_fare_capacity(uint8 g_class, bool include_lower_classes = false) const;

	// update player's fixed maintenance
	void finish_rd() OVERRIDE;
	void before_delete();

	void set_current_livery(const char* liv) { current_livery = liv; }
	const char* get_current_livery() const { return current_livery.c_str(); }

	virtual sint32 get_takeoff_route_index() const { return INVALID_INDEX; }
	virtual sint32 get_touchdown_route_index() const { return INVALID_INDEX; }
};


template<> inline vehicle_t* obj_cast<vehicle_t>(obj_t* const d)
{
	return dynamic_cast<vehicle_t*>(d);
}


sint16 get_friction_of_waytype(waytype_t waytype);

#endif
