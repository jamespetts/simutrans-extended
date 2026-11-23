/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_SIMROADTRAFFIC_H
#define VEHICLE_SIMROADTRAFFIC_H


#include "road_vehicle.h"
#include "overtaker.h"

#include "../tpl/slist_tpl.h"
#include "../tpl/stringhashtable_tpl.h"
#include "../ifc/sync_steppable.h"

class citycar_desc_t;
class karte_t;

/**
 * Base class for traffic participants with random movement
 *
 * Transport vehicles are defined in vehicle.h, because they greatly
 * differ from the vehicles defined herein for the individual traffic
 * (pedestrians, citycars, movingobj aka flock of sheep).
 */
class road_user_t : public vehicle_base_t, public sync_steppable
{
protected:
	/**
	 * Distance count
	 */
	uint32 weg_next;

	/* ms until destruction
	 */
	sint32 time_to_life;

	/** Necessary to keep track of the
	 * distance travelled sine the last
	 * time that this vehicle paid
	 * a toll for using a player's way
	 */
	uint8 tiles_since_last_increment;

protected:
	waytype_t get_waytype() const OVERRIDE { return road_wt; }

	void hop(grund_t *gr) OVERRIDE;
	virtual void update_bookkeeping(uint32) OVERRIDE {};

#ifdef INLINE_OBJ_TYPE
	road_user_t(typ type);

	/**
	 * Creates thing at position given by @p gr.
	 * Does not add it to the tile!
	 * @param random number to compute initial direction.
	 */
	road_user_t(typ type, grund_t* gr, uint16 random);
#else
	road_user_t();

	/**
	 * Creates thing at position given by @p gr.
	 * Does not add it to the tile!
	 * @param random number to compute initial direction.
	 */
	road_user_t(grund_t* gr, uint16 random);
#endif

public:
	virtual ~road_user_t();

	/**
	 * Open a new observation window for the object.
	 */
	void show_info() OVERRIDE;

	void rdwr(loadsave_t *file) OVERRIDE;

	// finalizes direction
	void finish_rd() OVERRIDE;

	void set_time_to_life(uint32 value) { time_to_life = value; }

	// we allow to remove all cars etc.
	const char *is_deletable(const player_t *) OVERRIDE { return NULL; }
};


class private_car_t : public road_user_t, public overtaker_t, public traffic_vehicle_t
{
private:

	koord origin;
	const citycar_desc_t *desc;

	// time to life in blocks
	koord target;
	koord3d pos_next_next;

	/**
	 * Actual speed
	 */
	uint16 current_speed;

	uint32 ms_traffic_jam;

	koord3d last_tile_marked_as_stopped;

	grund_t* hop_check() OVERRIDE;

	void calc_disp_lane();

	// Clears a blocker that this car may follow onto a queueing tile.
	void apply_following_rule(grund_t *gr, vehicle_base_t *&blocker) const;

	/**
	 * Rendering-only signed blend of the overtaking lane offset: 0 is the
	 * normal lane, 256 is the passing lane and
	 * road_space::OVERTAKEN_LATERAL_LEVEL is the small offset when being
	 * overtaken. It is advanced with the forward distance travelled so that
	 * the car never appears to slide sideways faster than it moves forward.
	 * Not saved and not synced.
	 */
	mutable sint16 overtake_level;

	/// Render-only cumulative forward distance in yards, and the value at the
	/// last blend update.
	mutable uint32 lateral_distance;
	mutable uint32 lateral_last_distance;

	/**
	 * Advances current_speed towards the way and vehicle limit by the existing
	 * per-tile increment (max_speed >> 2) applied over the number of ticks a
	 * tile takes at the vehicle's top speed. This makes the car respond to a
	 * higher safe speed immediately instead of waiting for the next tile
	 * boundary, which at low speed can take a long time.
	 */
	void accelerate(uint32 delta_t);

	/**
	 * Re-evaluates the following rule for a stationary car.
	 * A car held behind another vehicle does not run do_drive(), so nothing
	 * else refreshes overtaker_t::following_speed while it waits, and the
	 * vehicle ahead may move away at any time. Sets current_speed to the
	 * restart speed and returns true when the gap has opened enough for the
	 * car to move. Returns false when there is no vehicle ahead, when the
	 * vehicle ahead is still too close, or when the car is at the end of its
	 * tile and has already had one entry attempt for the current gap.
	 */
	bool try_resume_behind_leader();

	/**
	 * Advances the rendering-only overtaking blend from the forward distance
	 * travelled. Called from do_drive().
	 */
	void update_lateral_blend() const;

public:
	uint32 do_drive(uint32 distance) OVERRIDE;

	/// Length of this car in vehicle steps.
	uint32 get_length_in_steps() const;

protected:
	void rdwr(loadsave_t *file) OVERRIDE;

	void calc_image() OVERRIDE;

public:
	private_car_t(loadsave_t *file);

	/**
	 * Creates citycar at position given by @p gr with the given descriptor.
	 * Does not add car to the tile!
	 */
	private_car_t(grund_t* gr, koord target, const citycar_desc_t *desc);

	virtual ~private_car_t();

	void rotate90() OVERRIDE;

	static stringhashtable_tpl<const citycar_desc_t *, N_BAGS_MEDIUM> table;

	const citycar_desc_t *get_desc() const { return desc; }

	sync_result sync_step(uint32 delta_t) OVERRIDE;

	void hop(grund_t *gr) OVERRIDE;
	bool can_enter_tile(grund_t *gr);

	void enter_tile(grund_t* gr) OVERRIDE;

	uint16 get_current_speed() const {return current_speed;}

	uint32 get_max_speed() override;
	inline sint32 get_max_power_speed() OVERRIDE {return get_max_speed();}

	const char *get_name() const OVERRIDE {return "Verkehrsteilnehmer";}
	//typ get_typ() const OVERRIDE { return road_user; }

	/**
	 * @param[out] buf a description string for the object
	 * e.g. for the observation window/dialog
	 * @see simwin
	 */
	void info(cbuffer_t & buf) const OVERRIDE;

	// true, if this vehicle did not moved for some time
	bool is_stuck() OVERRIDE { return current_speed==0;}

	/** this function builds the list of the allowed citycars
	 * it should be called every month and in the beginning of a new game
	 */
	static void build_timeline_list(karte_t *welt);
	static bool list_empty();
	/// Whether any car available in the current era is available to this
	/// passenger wealth class. Consumes no random numbers.
	static bool class_list_empty(uint8 g_class);
	/// Whether any era-filtered car available to this passenger wealth class
	/// defines a passenger capacity. A pool without capacity data keeps the
	/// historic traffic-level car fill; a mixed pool uses per-car fill and
	/// treats a car without capacity as carrying one passenger. Consumes no
	/// random numbers.
	static bool class_has_capacity(uint8 g_class);
	/// Picks a car type for this passenger wealth class from the era-filtered
	/// pool of cars available to that class. Null if the class has no cars.
	static const citycar_desc_t *pick_citycar_desc(uint8 g_class);
	/// Picks a car type from the union of every class's pool, for callers
	/// which have no passenger class (the map editor tool and the fallback
	/// when a saved car's descriptor is no longer in the pakset).
	static const citycar_desc_t *pick_any_citycar_desc();
	/// Distribution-weighted mean top speed in km/h of the era-filtered cars
	/// available to this class, or 0 if the class has none.
	static sint32 get_average_speed(uint8 g_class);
	/// Distribution-weighted mean top speed in km/h of every era-filtered car.
	static sint32 get_average_speed();

	static bool register_desc(const citycar_desc_t *desc);
	static bool successfully_loaded();

	// since we must consider overtaking, we use this for offset calculation
	void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width, bool prev_based ) const;
	virtual void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const OVERRIDE { get_screen_offset(xoff,yoff,raster_width,false); }

	overtaker_t *get_overtaker() OVERRIDE { return this; }

	// Overtaking for city cars
	bool can_overtake(overtaker_t *other_overtaker, sint32 other_speed, sint16 steps_other) OVERRIDE;

	virtual vehicle_base_t* other_lane_blocked(const bool only_search_top) const;
	vehicle_base_t* is_there_car(grund_t *gr) const; // This is a helper function of other_lane_blocked

	virtual void reflesh(sint8,sint8) OVERRIDE;

	void * operator new(size_t s);
	void operator delete(void *p);

	typedef enum : uint8 {
		end_of_route = 125,
		no_route = 126,
		invalid_route = 255,
	} private_car_route_neighbour_t;

	static koord3d neighbour_from_int(koord3d from, uint8 i);

	static uint8 int_from_neighbour(koord3d from, koord3d to);
};

#endif
