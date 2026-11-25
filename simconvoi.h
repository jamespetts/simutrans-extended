/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMCONVOI_H
#define SIMCONVOI_H


#include <atomic>

#include "simtypes.h"
#include "simunits.h"
#include "simcolor.h"
#include "linehandle_t.h"

#include "ifc/sync_steppable.h"

#include "dataobj/route.h"
#include "dataobj/schedule.h"
#include "bauer/goods_manager.h"
#include "vehicle/overtaker.h"
#include "tpl/array_tpl.h"
#include "tpl/fixed_list_tpl.h"
#include "tpl/inthashtable_tpl.h"
#include "tpl/journey_times_map.h"
#include "tpl/koordhashtable_tpl.h"
#include "tpl/minivec_tpl.h"

#include "convoihandle_t.h"
#include "halthandle_t.h"

#include "simconst.h"
#include "simdeparture.h"

#include "obj/simobj.h"
#include "convoy.h"

/*
 * Waiting time for infinite loading (ms)
 */
#define WAIT_INFINITE 9223372036854775807ll
#define MAX_MONTHS               12 // Max history

class weg_t;
class depot_t;
class karte_ptr_t;
class player_t;
class vehicle_t;
class vehicle_desc_t;
class schedule_t;
class cbuffer_t;
class ware_t;
class replace_data_t;
class departure_point_t;

/**
 * Base class for all vehicle consists. Convoys can be referenced by handles, see halthandle_t.
 */
class convoi_t : public sync_steppable, public overtaker_t, public lazy_convoy_t
{
public:
	enum convoi_cost_t {            // Ext|Std|Description
		CONVOI_CAPACITY = 0,        //  0 |   | the distance (km) travelled by vacant seats
		CONVOI_PAX_DISTANCE,        //  1 |   | the distance (km) travelled by passengers
		CONVOI_AVERAGE_SPEED,       //  2 |   | the average speed of the convoy per rolling month
		CONVOI_COMFORT,             //  3 |   | the aggregate comfort rating of this convoy
		CONVOI_REVENUE,             //  4 | 2 | the income this CONVOI generated
		CONVOI_OPERATIONS,          //  5 | 3 | the cost of operations this CONVOI generated
		CONVOI_PROFIT,              //  6 | 4 | total profit of this convoi
		CONVOI_DISTANCE,            //  7 | 5 | total distance traveled this month
		CONVOI_REFUNDS,             //  8 |   | the refunds passengers waiting for this convoy (only when not attached to a line) have received.
//		CONVOI_MAXSPEED,            //    | 6 | average max. possible speed
		CONVOI_WAYTOLL,             //  9 | 7 |
		CONVOI_MAIL_DISTANCE,       // 10 |   | the distance (km) travelled by mail
		CONVOI_PAYLOAD_DISTANCE,    // 11 |   | moving 1 ton of cargo a distance of 1 km
		CONVOI_MAXIMUM_TOLERABLE_JOURNEY, // 12 | | the maximum tolerable journey time in seconds
		MAX_CONVOI_COST             // 13 | 8 |
	};

	/** Constants */
	enum { max_vehicle=8, max_rail_vehicle = 64 };

	enum states {
		INITIAL, // INITIAL means stored in the depot
		EDIT_SCHEDULE,
		ROUTING_1,
		ROUTING_2,
		DUMMY5,
		NO_ROUTE,
		DRIVING,
		LOADING,
		WAITING_FOR_CLEARANCE,
		WAITING_FOR_CLEARANCE_ONE_MONTH,
		CAN_START,
		CAN_START_ONE_MONTH,
		SELF_DESTRUCT,
		WAITING_FOR_CLEARANCE_TWO_MONTHS,
		CAN_START_TWO_MONTHS,
		LEAVING_DEPOT,
		ENTERING_DEPOT,
		REVERSING,
		OUT_OF_RANGE,
		EMERGENCY_STOP,
		ROUTE_JUST_FOUND,
		NO_ROUTE_TOO_COMPLEX,
		WAITING_FOR_LOADING_THREE_MONTHS,
		WAITING_FOR_LOADING_FOUR_MONTHS,
		REPLENISHING,
		MAINTENANCE,
		OVERHAUL,
		AWAITING_TRIGGER,
		LAYOVER,
		SHUNTING,
		AWAITING_RECOMBINATION,
		MAX_STATES
	};

	/**
	 * Kinds of message that may be deferred by convoy worker threads for the
	 * main thread to post (see deferred_message). The message system is
	 * main-thread/GUI only: workers must never call it.
	 */
	enum deferred_message_kind {
		defer_none = 0,
		defer_no_route,        // posted via player_t::report_vehicle_problem
		defer_halt_too_short,  // "Vehicle %s cannot choose because stop too short!"
		defer_unaffordable_overhaul_skip // "Convoy %s cannot afford the overhaul; the depot visit is skipped."
	};

	/**
	 * Records a message request from a convoy worker thread; posted and
	 * cleared by the main thread in step(). Safe without a lock: the worker
	 * writes during its start/await window and the main thread reads only
	 * after the step-head await_convoy_threads.
	 */
	void defer_message(deferred_message_kind kind, koord3d pos) { deferred_message = kind; deferred_message_pos = pos; }

	enum terminal_shunt_mode {
		wye = 0,
		rearrange = 1,
		shunting_loco = 2,
		change_direction = 3
	};

	enum { free = 0, head_and_tail = 1, half_intermediate = 2, INTERMEDIATE = 64, both_intermediate = 65, one_sided_partner = 66, unique_partner = 67, COUPLING_FIXED = 128, unconnectable = 255 };

	/**
	* time, when a convoi waiting for full load will drive on
	* @author prissi
	*/
	sint64 go_on_ticks;

// BG, 31.12.2012: virtual methods of lazy_convoy_t:
private:
	weight_summary_t weight;
	static const sint32 timings_reduction_point = 6;
	bool re_ordered; // Whether this convoy's vehicles are currently arranged in reverse order.

	/**
	 * Last result of convoy_t::calc_max_physical_speed() and the weight
	 * summary it was computed for. calc_fuel_consumption() asks for the same
	 * value once per powered vehicle and per tile hop, with the convoy's own
	 * weight and friction every time, so the cubic solve is done once for all
	 * of them. Invalidated with the cached summaries it depends on (see
	 * cd_max_physical_speed).
	 */
	mutable sint32 cached_max_physical_speed;
	mutable weight_summary_t cached_max_physical_weight;

protected:
	virtual void update_vehicle_summary(vehicle_summary_t &vehicle) OVERRIDE;
	virtual void update_freight_summary(freight_summary_t &freight) OVERRIDE;
	virtual void update_weight_summary(weight_summary_t &weight);
	virtual float32e8_t get_brake_summary(/*const float32e8_t &speed*/ /* in m/s */) OVERRIDE;
	virtual float32e8_t get_power_summary(const float32e8_t &speed /* in m/s */) OVERRIDE;
public:
	virtual void update_adverse_summary(adverse_summary_t &adverse) OVERRIDE;
	virtual float32e8_t get_force_summary(const float32e8_t &speed /* in m/s */) OVERRIDE;
	virtual sint16 get_current_friction() OVERRIDE;

	/**
	 * Cached calculation of the convoy's maximum physical speed in km/h for
	 * the given weight. Repeated calls with the same weight summary reuse the
	 * previous result; the cache is cleared whenever one of the summaries the
	 * calculation depends on is invalidated.
	 */
	sint32 calc_max_physical_speed(const weight_summary_t &weight) OVERRIDE;

	// weight_summary becomes invalid, when vehicle_summary or envirion_summary
	// becomes invalid.
	inline void invalidate_weight_summary()
	{
		is_valid &= ~(cd_weight_summary|cd_max_physical_speed);
	}

	// weight_summary is valid if (is_valid & cd_weight_summary != 0)
	inline void validate_weight_summary() {
		if (!(is_valid & cd_weight_summary))
		{
			is_valid |= cd_weight_summary;
			update_weight_summary(weight);
		}
	}

	// weight_summary needs recaching only, if it is going to be used.
	inline const weight_summary_t &get_weight_summary() {
#ifdef MULTI_THREAD
		if(  convoy_summary_compute_thread_local  ) {
			// Convoy worker thread: do not write the shared cache
			// (convoy.h: the main thread reads/writes it concurrently).
			static thread_local weight_summary_t thread_weight;
			update_weight_summary(thread_weight);
			return thread_weight;
		}
#endif
		validate_weight_summary();
		return weight;
	}

	inline void calc_move(const settings_t &settings, long delta_t, sint32 akt_speed_soll, sint32 next_speed_limit, sint32 steps_til_limit, sint32 steps_til_brake, sint32 &akt_speed, sint32 &sp_soll, float32e8_t &akt_v)
	{
		validate_weight_summary();
		convoy_t::calc_move(settings, delta_t, weight, akt_speed_soll, next_speed_limit, steps_til_limit, steps_til_brake, akt_speed, sp_soll, akt_v);
	}

// BG, 31.12.2012: end of virtual methods of lazy_convoy_t.

private:
	/**
	* Route of this convoi - a sequence of coordinates. Actually
	* the path of the first vehicle
	*/
	route_t route;

	/**
	* assigned line
	*/
	linehandle_t line;

	/**
	* holds id of line with pending update
	* -1 if no pending update
	*/
	linehandle_t line_update_pending;

	/**
	* Name of the convoi.
	* @see set_name
	*/
	uint8 name_offset;
	char name_and_id[128];

	/**
	* All vehicle-schedule pointers point here
	*/
	schedule_t *schedule;

	// Added by : Knightly
	// Purpose  : To hold the original schedule before opening schedule window
	schedule_t *old_schedule;
	koord3d schedule_target;

	/**
	* loading_level was minimum_loading before. Actual percentage loaded for loadable vehicles (station length!).
	* needed as int, since used by the gui
	*/
	sint32 loading_level;

	/**
	* At which loading level is the train allowed to start? 0 during driving.
	* needed as int, since used by the gui
	*/
	sint32 loading_limit;

	/**
	 * Free seats for passengers, calculated in convoi_t::calc_loading
	 * @author Inkelyad
	 */
	sint32 free_seats;

	/**
	* The vehicles of this convoi
	*/
	array_tpl<vehicle_t*> vehicle;

	/*
	 * a list of all catg_index, which can be transported by this convoy.
	 */
	minivec_tpl<uint8> goods_catg_index;

	/**
	* Convoi owner
	*/
	player_t *owner;

	/**
	* Current map
	*/
	static karte_ptr_t welt;

	/**
	* the convoi is being withdrawn from service
	*/
	bool withdraw;

	/**
	* nothing will be loaded onto this convoi
	*/
	bool no_load;

	/**
	* If the convoy is marked for automatic replacing,
	* this will point to the dataset for it; otherwise,
	* it is NULL.
	* @author: jamespetts, March 2010
	*/
	replace_data_t *replace;

	/**
	* send to depot when empty
	* @author isidoro
	*/
	bool depot_when_empty;

	/**
	* the convoi traverses its schedule in reverse order
	* @author yobbobandana
	*/
	bool reverse_schedule;

	/**
	* the convoi caches its freight info; it is only recalculation after loading or resorting
	*/
	// TODO: Not currently used but could be used for another flag. Delete if not used.
	bool freight_info_resort;

	// true, if at least one vehicle of a convoi is obsolete
	bool has_obsolete;

	// true, if there is at least one engine that requires catenary
	bool is_electric;

	// True if this is on token block working and the route has been
	// renewed during the journey.
	bool needs_full_route_flush;

	/**
	* Number of vehicles in this convoi.
	*/
	uint8 vehicle_count;

	/* Number of steps the current convoi did already
	 * (only needed for leaving/entering depot)
	 */
	sint16 steps_driven;

	/**
	 * this give the index of the next signal or the end of the route
	 * convois will slow down before it, if this is not a waypoint or the cannot pass
	 * The slowdown is done by the vehicle routines
	 */
	uint16 next_stop_index;

	/**
	 * this give the index until which the route has been reserved. It is used for
	 * restoring reservations after loading a game.
	 */
	uint16 next_reservation_index;

	/**
	 * The convoi is not processed every sync step for various actions
	 * (like waiting before signals, loading etc.) Such action will only
	 * continue after a waiting time larger than wait_lock
	 */
	sint32 wait_lock;

	/**
	 * threaded_step needs to be able to set wait_lock indirectly, because
	 * it can run after an indeterminate number of sync_steps.
	 */
	sint32 wait_lock_next_step;

	/**
	 * Worker-to-main message deferral slots: written by convoy worker
	 * threads during route finding, read and cleared by the main thread in
	 * step() (after the step-head await, so no synchronisation is needed).
	 * Transient UI state: not saved.
	 */
	deferred_message_kind deferred_message;
	koord3d deferred_message_pos;

	/**
	 * The flag whether this convoi is requested to change lane by the convoi behind this.
	 * @author THLeaderH
	 */
	bool requested_change_lane;

	/**
	* accumulated profit over a year
	*/
	sint64 jahresgewinn;

	/* the odometer */
	// (In *km*).
	sint64 total_distance_traveled;

	// The number of steps travelled since
	// the odometer was last incremented.
	// Used for converting tiles to km.
	// @author: jamespetts
	sint64 steps_since_last_odometer_increment;

	/**
	* Necessary for registering departure and waiting times.
	* last_stop_pos cannot be used because sea-going ships do not
	* stop on a halt tile.
	*/
	uint16 last_stop_id;

	// needed for speed control/calculation
	sint32 akt_speed;	        // current speed
	sint32 akt_speed_soll;		// Target speed
	/**
	 * The target speed before the road following rule caps it at the speed of
	 * the vehicle in front. The overtaking decision uses this value, because a
	 * follower that has already slowed to the leader's speed is still able to
	 * overtake if its own desired speed is higher. Derived each step; not
	 * saved.
	 */
	sint32 akt_speed_soll_unlimited;
	/**
	 * Whether the following-speed cap was active on the previous step. Used to
	 * try an overtake once when a slower leader is first reached, before the
	 * convoy is slowed to the leader's speed. Derived each step; not saved.
	 */
	bool following_cap_active;
	sint32 sp_soll;				// steps to go
	sint32 previous_delta_v;	// Stores the previous delta_v value; otherwise these digits are lost during calculation and vehicle do not accelrate
	float32e8_t v; // current speed in m/s.

	uint32 next_wolke;	// time to next smoke

	// Atomic: convoy worker threads read this (the threaded_step gate) while the
	// main thread may write it (sync_step state transitions, new_month).
	std::atomic<states> state;

	ribi_t::ribi alte_direction; //"Old direction" (Google)

	/**
	* The index number of the livery scheme of the current convoy
	* in the livery schemes vector in einstellungen_t.
	* @author: jamespetts, April 2011
	*/
	uint16 livery_scheme_index;

	/**
	* Initialize all variables with default values.
	* Each constructor must call this method first!
	*/
	void init(player_t *player);

	/**
	* Calculate route from Start to Target Coordinate
	*/
	bool drive_to();

	/** This was formerly part of
	 * drive_to(), but is separated
	 * in order to allow multi-threading
	 * to work in network mode.
	 */
	bool prepare_for_routing();

	/**
	* Setup vehicles for moving in same direction than before
	* if the direction is the same as before
	*/
	bool can_go_alte_direction();

	/**
	* Mark first and last vehicle.
	*/
	void set_erstes_letztes();

	// returns the index of the vehicle at position length (16=1 tile)
	int get_vehicle_at_length(uint16);

	/**
	* Recalculates loading level and limit.
	* While driving loading_limit will be set to 0.
	*/
	void calc_loading();

	/* Calculates (and sets) akt_speed
	 * needed for driving, entering and leaving a depot)
	 */
	void calc_acceleration(uint32 delta_t);

	/**
	* struct holds new financial history for convoi
	*/
	sint64 financial_history[MAX_MONTHS][MAX_CONVOI_COST];

	/**
	* initialize the financial history
	*/
	void init_financial_history();

	/**
	* the koordinate of the home depot of this convoi
	* the last depot visited is considered being the home depot
	*/
	koord3d home_depot;

	/*
	 * The position of the last signal passed by this convoy
	 */
	koord3d last_signal_pos;

	/**
	 * The other convoy in a committed choose-signal coupling approach, and
	 * whether this convoy is the mover. The link is reciprocal on both
	 * convoys and is saved from Extended save revision 13 so that the
	 * approach survives save, reload and network transfer. A convoy has at
	 * most one approach link, and a target at most one mover.
	 */
	convoihandle_t recombination_approach_partner;
	bool recombination_approach_is_mover;

	/**
	 * The coupling-point offset within the shared contact tile, in vehicle
	 * steps from the start of the tile, or -1 when no contact hop is pending.
	 * Transient: it is recomputed from the approach link and the two vehicle
	 * positions, so it is not saved.
	 */
	sint16 recombination_contact_steps;

	/**
	 * The cached result of the choose-signal coupling pre-check. The choose
	 * signal re-tests the target on every step until it can route to it, and
	 * the joint solver is much more expensive than the fallback route search,
	 * so the verdict is reused until either consist or the stop's laid-over
	 * pool changes. Transient: it is a function of live state and is not
	 * saved.
	 */
	convoihandle_t recombination_precheck_partner;
	uint32 recombination_precheck_signature;
	bool recombination_precheck_passed;

	/**
	 * The schedule entry whose consist order last failed at the current stop,
	 * so that the failure is reported once instead of on every step spent
	 * waiting for a coupling target. Cleared on arrival at each stop.
	 * Transient: it is a UI-reporting aid, not simulation state.
	 */
	uint16 recombination_failure_reported_entry = 0;

	// Helper function: used in init and replacing
	void reset();

	// Helper function: used in enter_depot and destructor.
	void close_windows();

	// Reverses the order of the convoy.
	// @author: jamespetts
	void reverse_order(bool rev);
public:
	// Reorder the vehicle array
	// Can be executed even with a vehicle array that does not belong to convoy for UI
	template <typename vehicle_collection> static void execute_reverse_order(vehicle_collection &vehicles, uint8 vehicle_count, bool rev, bool dry_run = false);
private:
	bool reversable;
	bool reversed;

	uint32 highest_axle_load;
	uint32 longest_min_loading_time;
	uint32 longest_max_loading_time;
	uint32 current_loading_time;

	uint16 min_range;

	/**
	 * Time in ticks since this convoy last departed from
	 * any given stop, plus accumulated distance since the last
	 * stop, indexed here by timetable entry.
	 * @author: jamespetts, August 2011. Replaces the original
	 * "last_departure_time" member.
	 * Modified October 2011 to include accumulated distance.
	 */
	typedef koordhashtable_tpl<departure_point_t, departure_data_t, N_BAGS_SMALL> departure_map;
	departure_map departures;

	/*
	 * This is a table of the departures to each point in the schedule
	 * whose times have already been booked. This makes sure that only
	 * the shortest distance between each pair of points in a schedule
	 * is used. For example, on a schedule A>B>C>D with reversing, this
	 * system ensures that, when reaching C on the way back, the departure
	 * from A, already registered at C on the way out, is not again
	 * booked at C on the way back with the additional time since going
	 * via D has elapsed. The key is the ID for the pair of stops, and
	 * the value is the last departure time booked between those stops.
	 */
	typedef koordhashtable_tpl<id_pair, sint64, N_BAGS_SMALL> departure_time_map;
	departure_time_map departures_already_booked;

	/**
	* This records the journey time from each point in the schedule to the
	* next point in the schedule. This is used for predicting when each
	* convoy will arrive at each stop in its schedule by concatenating
	* strings of these and adding the waiting time for each stop.
	*/
	typedef koordhashtable_tpl<departure_point_t, average_tpl<uint16>, N_BAGS_SMALL> timings_map;
	timings_map journey_times_between_schedule_points;

	// @author: suitougreentea
	times_history_map journey_times_history;

	// When we arrived at current stop
	// @author Inkelyad
	sint64 arrival_time;

	//When convoy was at first stop.
	//Used in average round trip time calculations.
	fixed_list_tpl<sint64, MAX_CONVOI_COST> arrival_to_first_stop;
	// @author: jamespetts
	uint32 rolling_average[MAX_CONVOI_COST];
	uint16 rolling_average_count[MAX_CONVOI_COST];

	/**To prevent repeat bookings of journey time
	 * and comfort when a vehicle is waiting for
	 * load at a stop.
	 */
	uint8 current_stop;

	/**
	 * The number of times that this
	 * convoy has attempted to find
	 * a route to its destination
	 * and failed.
	 * @author: jamespetts, November 2011
	 */
	uint8 no_route_retry_count;

	/**
	 * Get obsolescence from vehicle list.
	 * Extracted from new_month().
	 * Used to recalculate convoi_t::has_obsolete
	 *
	 * @author: Bernd Gabriel
	 */
	bool calc_obsolescence(uint16 timeline_year_month);

	// matches two halts; if the pos is not identical, maybe the halt still is
	bool matches_halt( const koord3d pos1, const koord3d pos2 );

	/**
	 * Register the convoy with the stops in the schedule
	 */
	void register_stops();

	uint32 move_to(uint16 start_index);

	/**
	* Advance the schedule cursor.
	* Also toggles the reverse_schedule flag if necessary.
	* @author yobbobandana
	*/
	void advance_schedule();
	void advance_schedule_internal();

	/**
	 * At a stop of a mirrored schedule whose next entries are conditionally skipped depots that
	 * will be skipped, the schedule would turn at the skipped entry and return to this stop.
	 * Instead, turn here, as at any other end of a mirrored schedule.
	 */
	void turn_at_skipped_depot();

	/**
	 * Measure and record the times that
	 * goods of all types have been waiting
	 * before boarding this convoy
	 * @author: jamespetts
	 */
	void book_waiting_times();

	/**
	 * Measure and record the departure
	 * time of this convoy from the current
	 * stop (if the convoy is currently at
	 * a stop)
	 * @author: jamespetts
	 */
	void book_departure_time(sint64 time);

	/**
	 * Whether this convoy is in the process
	 * of trying to reserve a path from a
	 * choose signal. Only relevant for rail
	 * convoys, as this is for the block
	 * reserver.
	 * @author: jamespetts
	 */
	bool is_choosing:1;

	// This is true if this convoy has not stopped since it emerged
	// from a depot. This is useful for ensuring that a stop's reversing
	// status is not set incorrectly.
	bool last_stop_was_depot:1;

	// Early decision for the next conditional depot visit. These bits
	// share the allocation unit of the two bitfields above, so they add
	// no size to convoi_t.
	//
	// conditional_depot_present: the schedule has at least one
	// conditional depot entry.
	// conditional_depot_decided: the decision for the next conditional
	// depot has been made at the first halt after the previous depot.
	// conditional_depot_visit: that decision is to visit.
	//
	// The decision covers only the next conditional depot. A schedule may
	// contain more than one; the later ones keep the late decision in
	// convoi_t::advance_schedule(). Tracking a specific depot would need a
	// schedule-index byte. Whether that is worth the memory is an open
	// question (see ai/ex-15/depot-maintenance.md).
	bool conditional_depot_present:1;
	bool conditional_depot_decided:1;
	bool conditional_depot_visit:1;

	// The maximum speed allowed by the current signalling system
	sint32 max_signal_speed;

	// The classes of passengers/mail carried by this line
	// Cached to reduce recalculation times in the path
	// explorer.
	minivec_tpl<uint8> passenger_classes_carried;
	minivec_tpl<uint8> mail_classes_carried;

	/*
	* A bitfield of up to 16 conditions that have been
	* triggered for this convoy.
	*/
	uint16 conditions_bitfield;
	/**
	 * the route index of the point to quit yielding lane
	 * == -1 means this convoi isn't yielding.
	 * @author teamhimeH
	 */
	sint32 yielding_quit_index;

	// 0: not fixed, -1: fixed to traffic lane, 1: fixed to passing lane
	sint8 lane_affinity;
	uint32 lane_affinity_end_index = INVALID_INDEX;

	// true, if this vehicle will cross lane and block other vehicles.
	bool next_cross_lane;

	// Flag to set to false during certain signalling operations to disable reservation clearing.
	bool allow_clear_reservation = true;

	// Display-only: set when this convoy entered an occupied block under a
	// choose signal's call-on aspect, cleared on arrival or when the working
	// method changes away from drive_by_sight.
	bool call_on = false;


	// This obviates the need to call can_enter_tile
	// more than once in a step on the same tile
	koord3d checked_tile_this_step = koord3d::invalid;

	// The last time that salary was paid on staff in this convoy.
	// Add layover, maintenance and overhaul ticks to this number to discount layover time.
	sint64 last_salary_point_ticks = 0;

public:
	/**
	 * Some precalculated often used infos about a tile of the convoy's route.
	 * @author B. Gabriel
	 */
	class route_info_t
	{
	public:
		sint32 speed_limit;
		uint32 steps_from_start; // steps including this tile's length, which is VEHICLE_STEPS_PER_TILE for a straight way and diagonal_vehicle_steps_per_tile for a diagonal way.
		ribi_t::ribi direction;
	};

	class route_infos_t : public vector_tpl<route_info_t>
	{
		// BG, 05.08.2012: pay attention to the aircrafts' holding pattern!
		// It starts at route_index = touchdown - HOLDING_PATTERN_LENGTH - HOLDING_PATTERN_OFFSET
		// and has a length of HOLDING_PATTERN_LENGTH.
		sint32 hp_start_index; // -1: not an aircraft or aircraft has passed the start of the holding pattern.
		sint32 hp_end_index;   // -1: not an aircraft or aircraft has passed the start of the holding pattern.
		sint32 hp_start_step;  // -1: not an aircraft or aircraft has passed the start of the holding pattern.
		sint32 hp_end_step;   // -1: not an aircraft or aircraft has passed the start of the holding pattern.
	public:
		void set_holding_pattern_indexes(sint32 current_route_index, sint32 touchdown_route_index);

		inline sint32 get_holding_pattern_start_index() const { return hp_start_index; }
		inline sint32 get_holding_pattern_end_index()   const { return hp_end_index; }
		inline sint32 get_holding_pattern_start_step()  const { return hp_start_step; }
		inline sint32 get_holding_pattern_end_step()    const { return hp_end_step; }

		// BG 05.08.2012: calc number of steps. Ignores the holding pattern, if start_step starts before it and end_step ends after it.
		// In any other case the holding pattern is part off the route or is irrelevant.
		inline sint32 calc_steps(sint32 start_step, sint32 end_step)
		{
			if (hp_start_step >= 0 && start_step < hp_start_step && hp_end_step <= end_step)
			{
				// assume they will not send us into the holding pattern: skip it.
				return (end_step - start_step) - (hp_end_step - hp_start_step);
			}
			return end_step - start_step;
		}

		// BG 05.08.2012: calc number of tiles. Ignores the holding pattern, if start_index starts before it and end_index ends after it.
		// In any other case the holding pattern is part off the route or is irrelevant.
		inline sint32 calc_tiles(sint32 start_index, sint32 end_index)
		{
			if (hp_start_index >= 0 && start_index < hp_start_index && hp_end_index <= end_index)
			{
				// assume they will not send us into the holding pattern: skip it.
				return (end_index - start_index) - (hp_end_index - hp_start_index);
			}
			return end_index - start_index;
		}
	};
#ifdef DEBUG_PHYSICS
	sint32 next_speed_limit;
	sint32 steps_til_limit;
	sint32 steps_til_brake;
#endif

private:
	/**
	  * List of upcoming speed limits; for braking purposes.
	  * @author: jamespetts, September 2011
	  */
	route_infos_t route_infos;

public:
	obj_t::typ get_depot_type() const;

	/**
	* Convoi haelt an Haltestelle und setzt quote fuer Fracht
	*/
	void hat_gehalten(halthandle_t halt);

	/**
	 * remove all track reservations (trains only)
	 */
	void unreserve_route();

	route_t* get_route() { return &route; }
	const route_t* get_route() const { return &route; }
	route_t* access_route() { return &route; }
	route_t::route_result_t calc_route(koord3d start, koord3d ziel, sint32 max_speed);
	void update_route(uint32 index, const route_t &replacement); // replace route with replacement starting at index.
	void replace_route(const route_t &replacement); // Completely replace the route with that passed as a parameter.

	const koord3d get_schedule_target() const { return schedule_target; }
	void set_schedule_target( koord3d t ) { schedule_target = t; }

	// Sorting purpose for GUI
	// Additional numbers will be assigned if convoy is moving backwards from the end of the schedule
	uint16 get_current_schedule_order() const;

	/**
	* get line
	*/
	inline linehandle_t get_line() const {return line;}

	/* true, if electrification needed for this convoi */
	inline bool needs_electrification() const { return is_electric; }

	/**
	* set line
	*/
	void set_line(linehandle_t );

	/*
	* Clears the average speed of this vehicle for this month and last.
	* Used when re-assigning a line to avoid stale data being used in
	* service frequency and other computations.
	*/
	void clear_average_speed();

	// updates a line schedule and tries to find the best next station to go
	void check_pending_updates();

	// true if this is a waypoint
	bool is_waypoint( koord3d ) const;

	/* changes the state of a convoi via tool_t; mandatory for networkmode!
	 * for list of commands and parameter see tool_t::tool_change_convoi_t
	 */
	void call_convoi_tool( const char function, const char *extra = NULL );

	/**
	* set state: only use by tool_t::tool_change_convoi_t
	*/
	void set_state( uint16 new_state ) { assert(new_state<MAX_STATES); state = (states)new_state; }

	/**
	* get state
	*/
	int get_state() const { return state; }

	// In any of these states, user interaction should not be possible.
	bool is_locked() const { return state == EDIT_SCHEDULE || state == ROUTING_2 || state == ROUTE_JUST_FOUND; }

	bool is_loading() const { return state == LOADING || state == WAITING_FOR_LOADING_THREE_MONTHS || state == WAITING_FOR_LOADING_FOUR_MONTHS; }

	/**
	* true if in waiting state (maybe also due to starting)
	*/
	bool is_waiting() const { return (state>=WAITING_FOR_CLEARANCE  &&  state<=CAN_START_TWO_MONTHS)  &&  state!=SELF_DESTRUCT; }

	/**
	* reset state to no error message
	*/
	inline void reset_waiting() { state=WAITING_FOR_CLEARANCE; }

	/**
	* The handle for ourselves. In Anlehnung an 'this' aber mit
	* allen checks beim Zugriff.
	*/
	convoihandle_t self;

	/**
	 * The profit in this year
	 */
	inline const sint64 & get_jahresgewinn() const {return jahresgewinn;}

	const sint64 & get_total_distance_traveled() const { return total_distance_traveled; }

	/**
	 * returns the total running cost for all vehicles in convoi
	 */
	sint32 get_running_cost() const;

	// Gets the running cost per kilometre
	// for GUI purposes.
	// @author: jamespetts
	sint32 get_per_kilometre_running_cost() const;

	/**
	* returns the total monthly fixed maintenance cost for all vehicles in convoi
	* @author Bernd Gabriel
	*/
	uint32 get_fixed_cost() const;

	/**
	 * returns the total new purchase cost for all vehicles in convoy
	 */
	sint64 get_purchase_cost() const;

	/**
	* Constructor for loading from file,
	*/
	convoi_t(loadsave_t *file);

	convoi_t(player_t* player);

	virtual ~convoi_t();

	/**
	* Load or save this convoi data
	*/
	void rdwr(loadsave_t *file);

	/**
	 * method to load/save convoihandle_t
	 */
	static void rdwr_convoihandle_t(loadsave_t *file, convoihandle_t &cnv);

	void finish_rd();

	void rotate90( const sint16 y_size );

	/**
	* Called if a vehicle enters a depot
	*/
	void enter_depot(depot_t *dep, uint16 flags = 0);

	/**
	* Return the internal name of the convois
	* @return Name of the convois
	*/
	inline const char *get_internal_name() const {return name_and_id+name_offset;}

	/**
	* Allows editing ...
	* @return Name of the Convois
	*/
	inline char *access_internal_name() {return name_and_id+name_offset;}

	/**
	* Return the name of the convois
	* @return Name of the convois
	*/
	inline const char *get_name() const {return name_and_id;}

	/**
	* Sets the name. Copies name into this->name and translates it.
	*/
	void set_name(const char *name, bool with_new_id = true);

	/**
	 * Return the position of the convois.
	 * @return Position of the convois
	 */
	koord3d get_pos() const;

	/**
	 * @return current speed, this might be different from topspeed
	 *         actual currently set speed.
	 */
	inline sint32 get_akt_speed() const { return akt_speed; }
	inline sint32 get_akt_speed_soll() const { return akt_speed_soll; }

	/**
	 * @return total power of this convoi
	 */
	inline uint32 get_sum_power() { return get_continuous_power().to_sint32(); }
	inline sint32 get_min_top_speed() {return get_vehicle_summary().max_sim_speed;}
	inline sint32 get_max_power_speed() OVERRIDE {return get_min_top_speed();}

	/// @returns weight of the convoy's vehicles (excluding freight)
	inline sint64 get_sum_weight() {return get_vehicle_summary().weight;}

	/// @returns weight of convoy including freight
	//inline const sint64 & get_sum_gesamtweight() const {return sum_gesamtweight;}

	/** Get power index in kW multiplied by gear.
	 * Get effective power in kW by dividing by GEAR_FACTOR, which is 64.
	 * @author Bernd Gabriel, Nov, 14 2009
	 */
	//inline const sint32 & get_power_index() { return sum_gear_and_power; }

	uint32 get_length() const;

	/**
	 * @return length of convoi in the correct units for movement
	 */
	uint32 get_length_in_steps() const { return get_length() * VEHICLE_STEPS_PER_CARUNIT; }

	/**
	 * Add the costs for travelling one tile
	 */
	void add_running_cost(sint64 cost, const weg_t *weg);

	// Increment the odometer,
	// adjusting for the distance scale.
	void increment_odometer(uint32 steps);

	/**
	 * moving the vehicles of a convoi and acceleration/deceleration
	 * all other stuff => convoi_t::step()
	 */
	sync_result sync_step(uint32 delta_t) OVERRIDE;

	/**
	 * All things like route search or loading, that may take a little
	 */
	void step();

	/**
	* All the difficult tasks that can be multi-threaded.
	* This excludes anything that might call the block reserver,
	* which cannot be multi-threaded because it is critical to preserve
	* between network connected clients the order in which convoys call
	* the block reserver.
	*/
	void threaded_step();

	/**
	* sets a new convoi in route
	*/
	void start(depot_t* dep = nullptr);

	void ziel_erreicht(); ///< Called, when the first vehicle reaches the target

	/**
	* When a vehicle has detected a problem
	* force calculate a new route
	*/
	void suche_neue_route();

	/**
	* Wait until vehicle 0 reports free route
	* will be called during a hop_check, if the road/track is blocked
	*/
	void warten_bis_weg_frei(sint32 restart_speed);

	/**
	* @return Vehicle count
	*/
	inline uint8 get_vehicle_count() const { return vehicle_count; }

	/**
	 * @return Vehicle at position i
	 */
	inline vehicle_t* get_vehicle(uint16 i) const { return vehicle[i]; }

	// Returns bits of traction types that this convoy has
	uint16 get_traction_types() const;

	// Upgrades a vehicle in the convoy.
	void upgrade_vehicle(uint16 i, vehicle_t* v);

	// Moves a vehicle in this consist to a new position.
	// Returns the vehicle, if any, that was already at that position.
	// If swap is true, the vehicle positions are switched.
	vehicle_t* move_vehicle(uint8 old_pos, uint8 new_pos, bool swap);

	// Returns the index of the passed vehicle.
	// 255 = not in cnv
	uint8 get_vehicle_index(vehicle_t*) const;

	// Helper method for adding/removing/upgrading vehicles
	void recalc_metrics();

	vehicle_t* front() const { return *vehicle.begin(); }

	vehicle_t* back() const { return vehicle.begin()[vehicle_count - 1]; }

	typedef array_tpl<vehicle_t*>::const_iterator const_iterator;
	inline array_tpl<vehicle_t*>::const_iterator begin() const { return vehicle.begin(); }
	inline array_tpl<vehicle_t*>::const_iterator end() const { return vehicle.begin() + vehicle_count; }

	/**
	* Adds a vehicle at the start or end of the convoi.
	*/
	bool add_vehicle(vehicle_t *v, uint8 pos = 255);

	/**
	* Removes vehicles at position i
	*/
	vehicle_t* remove_vehicle_at(unsigned short i);

	// Removes the specified vehicle
	void remove_vehicle(vehicle_t* v);

	// Replaces vehicle at the set position with the specified vehicle.
	// Returns the old vehicle
	vehicle_t* substitute_vehicle(vehicle_t* new_vehicle, uint8 index);

private:

	// Updates the name of the consist if it has the name of the pevious leading vehicle
	// The new name will be that of the current leading vehicle.
	void update_default_name(vehicle_t* previous_lead_vehicle);

public:

	const minivec_tpl<uint8> &get_goods_catg_index() const { return goods_catg_index; }

	// recalculates the good transported by this convoy and (in case of changes) will start schedule recalculation
	void recalc_catg_index();

	/**
	* Sets a schedule
	*/
	bool set_schedule(schedule_t *f);

	/**
	* @return Current schedule
	*/
	inline schedule_t* get_schedule() const { return schedule; }

	/**
	* Creates a new schedule if there isn't one already.
	*/
	schedule_t * create_schedule();

	/**
	 * Unregister the convoy from the stops in the schedule
	 */
	void unregister_stops();

	// remove wrong freight when schedule changes etc.
	void check_freight();

	/**
	* @return Owner of this convoi
	*/

	player_t * get_owner() const { return owner; }

	void set_owner(player_t* value) { owner = value; }

	/**
	* Opens an information window
	* @see simwin
	*/
	void show_info();
	void show_detail();

	/**
	* Get whether the convoi is traversing its schedule in reverse.
	*/
	inline bool get_reverse_schedule() const { return reverse_schedule; }

	/**
	* Set whether the convoi is traversing its schedule in reverse.
	*/
	void set_reverse_schedule(bool reverse) { reverse_schedule = reverse; }

	void set_is_choosing(bool value) { is_choosing = value; }
	bool get_is_choosing() const { return is_choosing; }

	void set_last_stop_was_depot(bool value) { last_stop_was_depot = value; }
	bool get_last_stop_was_depot() const { return last_stop_was_depot; }

	void set_maximum_signal_speed(sint32 value) { max_signal_speed = value; }
	sint32 get_max_signal_speed() const { return max_signal_speed; }

	inline void set_wait_lock(sint32 value) { wait_lock = value; }
	inline sint32 get_wait_lock() { return wait_lock; }

	bool check_destination_reverse(route_t* current_route = NULL, route_t* target_rt = NULL);

	// Reserve the tiles on which the convoy is standing to prevent collisions.
	void reserve_own_tiles(bool unreserve = false);

	bool has_tall_vehicles();
	bool has_tilting_vehicles();

	inline bool get_allow_clear_reservation() const { return allow_clear_reservation; }

	bool all_vehicles_are_buildable() const;

	bool check_way_constraints_of_all_vehicles(const weg_t& way) const;

	void set_working_method(working_method_t value);

	// Instigates a replacement according to the attached replace data.
	// If upgrade_wait is not null, it receives the longest overhaul duration
	// of any vehicle that was upgraded in place rather than bought.
	bool replace_now(sint32 *upgrade_wait = nullptr);

private:
	journey_times_map average_journey_times;
public:

	// we need to detect the change in the dialog that is already open when the fare class changes
	void force_update_fare_related_dialogs();

	/**
	* Opens the schedule window
	* @see simwin
	*/
	void open_schedule_window( bool show );

	/**
	* Check whether this consist is valid
	*/
	bool check_validity();

	// If false, this consist cannot move.
	bool is_unpowered();

	// True if this consist is valid and powered - this is sufficient for it to be driven
	bool is_valid_and_powered() { return check_validity() && !is_unpowered(); }

	/**
	* Control loading and unloading
	*/
	void laden();

	// Unloads the payload from a single vehicle in this consist
	uint16 unload_individual_vehicle_at(uint8 index, halthandle_t halt, bool will_lay_over, bool recombination_pass = false, bool destinations_only = false, bool force_discharge = false);

	// Book fuel consumption of individual vehicles
	void book_fuel_consumption();

	/**
	* Setup vehicles before starting to move
	*/
	void vorfahren();

	/**
	* Calculate the total value of the convoi as the sum of all vehicle values.
	*/
	sint64 calc_sale_value() const;

	/**
	* Check if this convoi has entered a depot.
	*/
	inline bool in_depot() const { return state == INITIAL; }

	/**
	* True for the states in which a convoy that is standing on a depot tile is inside that depot
	* (stored off the map and listed by the depot) although it is not in the INITIAL state:
	* scheduled depot stops, maintenance/overhaul and waiting for a trigger.
	*/
	static inline bool is_inside_depot_state(int s)
	{
		return s == ENTERING_DEPOT || s == MAINTENANCE || s == OVERHAUL || s == AWAITING_TRIGGER;
	}

	/**
	* loading_level was minimum_loading before. Actual percentage loaded of loadable
	* vehicles.
	*/
	inline const sint32 &get_loading_level() const { return loading_level; }

	/**
	* At which loading level is the train allowed to start? 0 during driving.
	*/
	inline const sint32 &get_loading_limit() const { return loading_limit; }

	/**
	 * Format remaining loading time from go_on_ticks
	 */
	void snprintf_remaining_loading_time(char *p, size_t size) const;

	/**
	* Calculates the remaining laoding time from go_on_ticks
	*/
	sint64 calc_remaining_loading_time() const;

	/**
	 * Format remaining reversing and emergency stop time from go_on_ticks
	 */
	void snprintf_remaining_wait_lock(char *p, size_t size) const;

	/**
	 * How many free seats for passengers in convoy? Used in overcrowded loading
	 * @auhor Inkelyad
	 */
	inline const sint32 &get_free_seats() const { return free_seats; }

	/**
	* Schedule convois for self destruction. Will be executed
	* upon next sync step
	*/
	void self_destruct();

	/**
	* Helper method to remove convois from the map that cannot
	* removed normally (i.e. by sending to a depot) anymore.
	* This is a workaround for bugs in the game.
	*/
	void destroy();

	/**
	* Debug info to stderr
	*/
	void dump() const;

	/**
	* book a certain amount into the convois financial history
	* is called from vehicle during un/load
	*/
	void book(sint64 amount, convoi_cost_t cost_type);

	/**
	* return a pointer to the financial history
	*/
	inline sint64* get_finance_history() { return *financial_history; }

	/**
	* return a specified element from the financial history
	*/
	inline sint64 get_finance_history(int month, convoi_cost_t cost_type) const { return financial_history[month][cost_type]; }
	sint64 get_stat_converted(int month, convoi_cost_t cost_type) const;

	// retrun yearly passenger load factor from finance history. up to 1 decimal place
	uint32 get_load_factor_pax() const;

	/**
	* only purpose currently is to roll financial history
	*/
	void new_month();

	/**
	 * Method for yearly action
	 */
	void new_year();

	inline void set_update_line(linehandle_t l) { line_update_pending = l; }

	void set_home_depot(koord3d hd) { home_depot = hd; }

	inline koord3d get_home_depot() { return home_depot; }

	inline void set_last_signal_pos(koord3d p) { last_signal_pos = p; }
	inline koord3d get_last_signal_pos() const { return last_signal_pos; }

	/**
	 * this give the index of the next signal or the end of the route
	 * convois will slow down before it, if this is not a waypoint or the cannot pass
	 * The slowdown is done by the vehicle routines
	 */
	uint16 get_next_stop_index() const {return next_stop_index;}
	void set_next_stop_index(uint16 n);

	/* including this route_index, the route was reserved the last time
	 * currently only used for tracks
	 */
	uint16 &get_next_reservation_index() { return next_reservation_index; }
	void set_next_reservation_index(uint16 n);

	/* the current state of the convoi */
	PIXVAL get_status_color() const;

	// returns tiles needed for this convoi. This includes 0-8/16 extra padding.
	uint16 get_tile_length() const;

	// returns total value of vehicle length of this convoy. (not include any padding)
	uint16 get_true_tile_length() const;

	// returns vehicle average age in month
	uint16 get_average_age();

	// get cached obsolescence.
	inline bool has_obsolete_vehicles() const { return has_obsolete; }

	inline bool get_withdraw() const { return withdraw; }

	void set_withdraw(bool new_withdraw);

	inline bool get_no_load() const { return no_load; }

	inline void set_no_load(bool new_no_load) { no_load = new_no_load; }

	/**
	* unset line -> remove cnv from line
	* @author hsiegeln
	*/
	void unset_line();

	replace_data_t* get_replace() const { return replace; }

	void set_replace(replace_data_t *new_replace);

	void clear_replace();

	bool get_depot_when_empty() const { return depot_when_empty; }

	void set_depot_when_empty(bool new_dwe, bool manually_sent = false);

	// True if the convoy has the same vehicles
	bool has_same_vehicles(convoihandle_t other) const;

	// Go to depot, if possible
	bool go_to_depot(bool show_success, bool use_home_depot = false, bool maintain = false, bool manually_sent = false);

	// True if convoy has no cargo
	//@author: isidoro
	bool has_no_cargo() const;

	void must_recalc_data() { invalidate_adverse_summary(); }

	// just a guess of the speed
	//uint32 get_average_kmh();

	// Overtaking for convois
	virtual bool can_overtake(overtaker_t *other_overtaker, sint32 other_speed, sint16 steps_other) OVERRIDE;

	/*
	 * Functions related to requested_change_lane
	 * @author THLeaderH
	 */
	bool is_requested_change_lane() const { return requested_change_lane; }
	void set_requested_change_lane(bool x) { requested_change_lane = x; }
	void yield_lane_space();
	sint32 get_yielding_quit_index() const { return yielding_quit_index; }
	void quit_yielding_lane() { yielding_quit_index = -1; }

	/*
	* Functions related to lane fixing
	* @author THLeaderH
	*/
	bool calc_lane_affinity(uint8 lane_affinity_sign); // If true, lane fixing started.
	uint32 get_lane_affinity_end_index() const { return lane_affinity_end_index; }
	sint8 get_lane_affinity() const { return lane_affinity; }
	void reset_lane_affinity() { lane_affinity = 0; }

	bool get_next_cross_lane() const { return next_cross_lane; }
	void set_next_cross_lane(bool n) { next_cross_lane = n; }

	virtual void reflesh(sint8,sint8) OVERRIDE;

	// Returns the maximum catering level of the category type given in the convoy.
	// NOTE: This does not give the value of any vehicles with self-contained catering.
	// @author: jamespetts
	uint8 get_catering_level(uint8 type) const;

	//@author: jamespetts
	inline bool get_reversable() const { return reversable; }
	inline bool is_reversed() const { return reversed; }

	//@author: jamespetts
	uint32 calc_highest_axle_load();
	inline uint32 get_highest_axle_load() const { return highest_axle_load; }

	void calc_min_range();
	inline uint16 get_min_range() const { return min_range; }

	//@author: jamespetts
	uint32 calc_longest_min_loading_time();
	uint32 calc_longest_max_loading_time();

	uint32 calc_current_loading_time(uint16 load_charge);
	inline uint32 get_current_loading_time() const { return current_loading_time; }

	koord3d get_checked_tile_this_step() const { return checked_tile_this_step; }
	void set_checked_tile_this_step(koord3d value) { checked_tile_this_step = value; }

	/**
	 * Calculate the number of tiles over which this convoy
	 * needs to check for corner radii based on its maximum
	 * speed.
	 */
	void calc_direction_steps();

	// @author: jamespetts
	// Returns the number of standing passengers (etc.) in this convoy.
	uint16 get_overcrowded() const;
	uint16 get_overcrowded(uint8 fare_class) const;

	// Returns the total regardless of fare class
	uint16 get_overcrowded_capacity() const;
	// Returns only the overcrowded capacity for the specified fare class
	uint16 get_overcrowded_capacity(uint8 fare_class) const;

	// Time-based comfort in tenths of minutes for the given class.
	uint16 get_maximum_comfortable_journey(uint8 g_class) const;
	uint16 get_maximum_tolerable_journey(uint8 g_class) const;
	// Capacity-weighted means over all passenger classes, for display.
	uint16 get_mean_maximum_comfortable_journey() const;
	uint16 get_mean_maximum_tolerable_journey() const;

	/** This method calculates the revenue of each
	 * ware packet as it is unloaded.
	 *
	 * It also calculates allocations of revenue to different
	 * players based on track usage.
	 * @author: jamespetts, neroden, Knightly
	 */
	sint64 calc_revenue(const ware_t &ware, const vehicle_t *v, array_tpl<sint64> & apportioned_revenues, uint8 g_class);

	uint16 get_livery_scheme_index() const;
	void set_livery_scheme_index(uint16 value) { livery_scheme_index = value; }

	// Applies the livery scheme to the vehicles owned by the convoy's owner;
	// vehicles owned by another player keep their existing livery. Returns the
	// number of vehicles skipped on ownership grounds.
	uint32 apply_livery_scheme();
	sint64 get_average_round_trip_time() {
		uint8 items = arrival_to_first_stop.get_count();
		if (items>1) {
			return (arrival_to_first_stop[items-1] - arrival_to_first_stop[0])/(items-1);
		} else {
			return 0;
		}
	}

	uint32 calc_reverse_delay() const;

	static uint16 get_waiting_minutes(uint32 waiting_ticks);

	bool is_wait_infinite() const { return go_on_ticks == WAIT_INFINITE; }

	route_infos_t& get_route_infos();

	void set_blank_route();

	void set_akt_speed(sint32 akt_speed) {
		this->akt_speed = akt_speed;
#ifndef NETTOOL
		if (akt_speed > 8)
			v = speed_to_v(akt_speed/2);
		else
			v = speed_to_v(akt_speed);
#endif
	}

	/** For going to a depot automatically
	 *  when stuck - will teleport if necessary.
	 */
	void emergency_go_to_depot(bool show_success = true, bool maintain = false);

	journey_times_map& get_average_journey_times();
	inline const journey_times_map& get_average_journey_times_this_convoy_only() const { return average_journey_times; }
	inline times_history_map& get_journey_times_history() { return journey_times_history; }

	bool get_needs_full_route_flush() const { return needs_full_route_flush; }
	void set_needs_full_route_flush(bool value) { needs_full_route_flush = value; }

	/**
	 * Clears the departure data.
	 * Used when the line changes
	 * its shcedule.
	 */
	void clear_departures();

	void clear_estimated_times();

	void calc_classes_carried();

	uint16 get_total_cargo() const;
	// Exclude overcrowding capacity
	uint16 get_cargo_max() const;

	uint16 get_total_cargo_by_fare_class(uint8 catg, uint8 g_class) const;
	uint16 get_unique_fare_capacity(uint8 catg, uint8 g_class) const;

	bool carries_this_or_lower_class(uint8 catg, uint8 g_class) const;

	bool carries_this_category(uint8 catg_index) const;

	const minivec_tpl<uint8>* get_classes_carried(uint8 catg) const
	{
		if (catg == goods_manager_t::INDEX_PAS)
		{
			return &passenger_classes_carried;
		}
		if (catg == goods_manager_t::INDEX_MAIL)
		{
			return &mail_classes_carried;
		}
		else
		{
			return NULL;
		}
	}

	// Returns this convoy's reversing method. (v14.8 - Jan, 2020 @Ranran)
	template <typename vehicle_collection> static uint8 get_terminal_shunt_mode(const vehicle_collection &vehicles, uint8 vehicle_count);
	uint8 get_terminal_shunt_mode() const {
		return get_terminal_shunt_mode(vehicle, vehicle_count);
	}
	// Train formation checks
	template <typename vehicle_collection> static uint8 get_front_loco_count(const vehicle_collection &vehicles, uint8 vehicle_count);
	template <typename vehicle_collection> static uint8 check_new_tail(const vehicle_collection &vehicles, uint8 start=1, uint8 end=1);
	template <typename vehicle_collection> static uint8 check_need_turntable(const vehicle_collection &vehicles, uint8 vehicle_count);

	// return a number numbered by position in convoy. This is affected by the number of locomotives and reversals.
	// The locomotive on the front side is returned a negative value.
	sint16 get_car_numbering(uint8 car_no) const;

	// @returns vehicle length removed at the same time from the convoy. Feb, 2020 @Ranran
	uint8 calc_auto_removal_length(uint8 car_no) const;
	uint8 get_auto_removal_vehicle_count(uint8 car_no) const;

private:
	// returns level of coupling constraints between vehicles
	uint8 check_couple_constraint_level(uint8 car_no, bool rear_side) const;

protected:

	// Run this (1) monthly; and (2) on re-combination. Books staff salaries.
	void book_salaries();

public:

	// Also used in UI as a reference value
	uint32 get_salaries(sint64 percentage_of_month);

	bool check_triggered_condition(uint16 value) const { return value & conditions_bitfield; }
	void set_triggered_conditions(uint16 value) { conditions_bitfield |= value; }
	void clear_triggered_conditions(uint16 value) { conditions_bitfield &= ~value; }
	void reset_all_triggers() { conditions_bitfield = 0; }

	bool is_maintenance_needed() const;
	bool is_maintenance_urgently_needed() const;
	bool get_conditional_depot_visit() const { return conditional_depot_present && conditional_depot_decided && conditional_depot_visit; }

	// Evaluates the next conditional depot visit at a halt, once per cycle.
	void update_conditional_depot_state();

	// Recomputes whether the schedule has a conditional depot and clears
	// the early decision. Called when the schedule changes and after a
	// depot visit.
	void reset_conditional_depot_state();

	// True when the next non-halt schedule entry is a depot, so the
	// current halt is the last chance to empty the convoy before the visit.
	bool is_last_halt_before_depot() const;

	// Returns true if any vehicle in this consist requires an overhaul.
	bool is_overhaul_needed() const;

	// True when every vehicle that is due for an overhaul can pay for it.
	bool can_afford_overhaul_visit() const;
	void report_unaffordable_overhaul_held(const koord &pos) const;

	void check_departure(halthandle_t halt = halthandle_t());

	// Advance past the current schedule entry when the convoy is standing on
	// that entry's depot tile. Used when a convoy is held in a depot without
	// completing the maintenance visit.
	void advance_past_current_depot_entry();

	sint64 get_arrival_time() const { return arrival_time; }

	void enter_layover(halthandle_t halt);

	void exit_layover();

	enum consist_order_process_result { fail, succeed, no_change_needed };

	// True when the given goods category uses the passenger system at a
	// recombination: passengers always, mail when the consist has a TPO,
	// freight never.
	bool uses_passenger_system(const goods_desc_t *goods) const;

	// Compute the next division's committed formation for the given stop when
	// it is derivable from the current consist and the two authored orders.
	// The plan is cleared at the first loading pass of each stop.
	void ensure_recombination_plan(halthandle_t halt);

	// True when the vehicle has a committed portion for the next division and
	// that portion is a scheduled consist (retained or split), not loose stock.
	bool has_committed_portion(const vehicle_t *v) const;

	// True when the vehicle's committed portion serves the packet from the
	// current stop. Without a committed portion, the current consist's own
	// schedule is used.
	bool portion_serves_packet(const vehicle_t *v, const ware_t &w, halthandle_t halt) const;

	// When the vehicle is assigned to the split-off portion, return that
	// portion's schedule, starting entry, direction and starting stop. Returns
	// false for a retained or loose vehicle, for which the current schedule is
	// the serving schedule.
	bool get_committed_serving_schedule(const vehicle_t *v, const schedule_t *&schedule, uint8 &entry, bool &reverse, halthandle_t &start_halt) const;

	// True when a recombination entry lies between the current stop and the
	// given halt in the current direction.
	bool recombination_between(halthandle_t requirement) const;

	// The schedule context against which a candidate vehicle's cargo is tested
	// when an order places that vehicle in a portion. `entry` is the schedule
	// entry at which the portion starts; `halt` is the stop at which a packet
	// with that next transfer or destination is delivered.
	struct cargo_service_context_t
	{
		const schedule_t *schedule;
		uint8 entry;
		bool reverse;
		const player_t *owner;
		halthandle_t halt;

		cargo_service_context_t() : schedule(nullptr), entry(0), reverse(false), owner(nullptr) {}
	};

	// The return value is whether the consist order can be processed at the current time (i.e., whether there are
	// sufficient of the right number of vehicles to do so).
	// If split_candidate is not null, the vehicles displaced from this convoy are collected
	// into one newly created laid-over convoy instead of being appended to an existing
	// laid-over convoy, and that new convoy's handle is stored there. If turn_ticks is not
	// null, it receives the extra shunting time needed to turn the vehicles whose chosen
	// orientation differs from their current one. If coupling_operations is not null, it
	// receives the number of couplings and uncouplings that the commit performs; the caller
	// converts that count into the shunting delay.
	consist_order_process_result process_consist_order(const consist_order_t &order, halthandle_t halt, depot_t* dep, convoihandle_t joining_convoy, convoihandle_t *split_candidate = nullptr, uint32 *turn_ticks = nullptr, uint32 *coupling_operations = nullptr, const cargo_service_context_t *cargo_service = nullptr);

	// True when any vehicle supplies power: a powerless consist cannot run.
	bool has_power() const;

	// True when a route failure is attributable to the vehicle combination - the
	// consist has no traction, or only traction that needs electrification - and
	// not to the network, so retrying the same route search cannot help.
	bool is_route_failure_from_combination() const { return !has_power() || needs_electrification(); }

	// How the convoy named by the current entry's couple or uncouple target
	// stands in relation to a choose-signal approach at the entry's stop.
	enum recombination_target_state_t
	{
		recombination_target_none,      // no target, or it is not yet committed to this stop
		recombination_target_committed, // the target has passed the last signal before the stop but is not stationary
		recombination_target_available  // the target is stationary at the stop
	};

	// Resolve the coupling target named by the current entry for the given
	// stop. The target is chosen deterministically among the named convoy or the
	// named line's convoys: an available target outranks a committed one, then
	// the earliest arrival_time wins, then the lowest convoy id.
	recombination_target_state_t get_recombination_target_state(halthandle_t halt, convoihandle_t &target) const;

	// Test, without changing any state, whether the coupling that the current
	// entry configures can be completed with the given target at the stop: a
	// paired exchange must solve both end-state orders jointly, and a one-sided
	// receive must complete this convoy's order into a powered consist. The
	// choose signal calls this before routing the mover to the target's rear
	// tile, so that a convoy is never routed to couple when the orders cannot
	// produce valid powered consists. On failure the reason is reported once
	// for the examined situation; the verdict is cached until either consist or
	// the stop's laid-over pool changes.
	bool can_approach_coupling_target(halthandle_t halt, const convoihandle_t &target);

	// The convoy this convoy is approaching to couple with, or an unbound
	// handle when there is no committed approach.
	convoihandle_t get_recombination_approach_partner() const { return recombination_approach_partner; }

	// True when this convoy is the mover in the committed approach.
	bool is_recombination_approach_mover() const { return recombination_approach_is_mover; }

	// The ground of the tile that contains the approach partner's coupling
	// point when this leading vehicle may hop into it to make physical contact,
	// or NULL when the next tile is not that tile or the geometry is not
	// supported. On success, records the coupling point within the tile.
	grund_t *get_recombination_contact_ground(const vehicle_t *mover);

	// The coupling point within the contact tile in vehicle steps, or -1.
	// The value is consumed by the hop that uses it.
	sint16 take_recombination_contact_steps();

	// The tile that contains this convoy's coupling point: the rear vehicle's
	// front position minus its length, carried back through as many tiles as
	// the length requires. Invalid when the convoy has no vehicles.
	koord3d get_recombination_contact_pos() const;

	// True when the given holder and tile are the claimed approach partner's
	// coupling-point tile, which is the shared contact tile of this mover.
	bool is_recombination_contact_tile(const convoihandle_t &holder, const koord3d &pos) const;

	// Record a reciprocal coupling approach with the given partner. Any
	// previous link is cleared on both sides. If the partner is already
	// approaching a third convoy, the link is not taken and this convoy's
	// previous link is cleared.
	void set_recombination_approach(convoihandle_t partner, bool is_mover);

	// Clear this convoy's approach link and the partner's reciprocal link.
	void clear_recombination_approach();

	// Display-only state set when the convoy enters an occupied block under a
	// choose signal's call-on aspect.
	bool is_under_call_on() const { return call_on; }
	void set_call_on(bool value) { call_on = value; }

	private:
		// The time needed to turn one vehicle at a stop.
		static uint32 vehicle_turn_delay_ticks(const vehicle_t *v);

		// The coupling point at this convoy's rear: the tile that contains it
		// and the offset within that tile in vehicle steps. Returns false when
		// the convoy has no vehicles.
		bool get_recombination_contact_point(koord3d &pos, sint16 &steps) const;

		// Replace this convoy's vehicles with the given final consist. Returns the
		// extra shunting time needed to turn vehicles. If coupling_operations is
		// not null, it receives the number of couplings and uncouplings performed.
		uint32 commit_recombined_consist(vector_tpl<vehicle_t*> const& vehicles, vector_tpl<bool> const& vehicles_reversed, halthandle_t halt, uint16 recipient_id, bool recipient_is_convoy, convoihandle_t *split_candidate, convoihandle_t joining_convoy, bool recipient_is_any, bool recipient_is_same, uint32 *coupling_operations = nullptr);

		// A division's vehicle-to-portion plan, derived from the current forward
		// consist and the two end-state orders. Valid only when both orders can
		// be satisfied from the current consist alone.
		struct recombination_plan_t
		{
			bool checked;
			bool valid;
			uint16 halt_id;
			uint16 source_unique_id;
			uint16 target_unique_id;
			bool target_reversed;
			linehandle_t target_line;
			convoihandle_t target_convoy;
			vector_tpl<vehicle_t*> retained;
			vector_tpl<bool> retained_reversed;
			vector_tpl<vehicle_t*> split;
			vector_tpl<bool> split_reversed;

			recombination_plan_t() : checked(false), valid(false), halt_id(0), source_unique_id(0), target_unique_id(0), target_reversed(false) {}
		};

		recombination_plan_t recombination_plan;

		// The requirement of a packet for wagon grouping: its next transfer, or
		// its destination when no next transfer is set.
		static halthandle_t packet_requirement(const ware_t &w);

		// After a division, move passengers and TPO mail to a portion that
		// serves them and discharge the remainder at the stop.
		void redistribute_passengers_after_division(halthandle_t halt, convoihandle_t portion, bool mail_uses_passenger_system, const vector_tpl<vehicle_t*> &original_consist);

		// Discharge loaded wagons whose goods need different portions after an
		// edit, at the first division where the requirements diverge.
		void discharge_divergent_wagons(halthandle_t halt, const schedule_t *target_schedule, uint8 target_entry, bool target_reverse, const player_t *target_owner, bool mail_uses_passenger_system);

		// Carry out the current entry's consist order, exchanging with the given
		// coupling target (unbound: assemble from the stop's pool). Sets SHUNTING
		// when the consist changes. If coupling_operations is not null, it
		// receives the number of couplings and uncouplings the commit performed.
		consist_order_process_result process_current_entry_consist_order(halthandle_t halt, convoihandle_t joining_convoy, convoihandle_t *split_candidate = nullptr, uint32 *coupling_operations = nullptr);

		// The departure data for the last call at the given stop in this
		// convoy's schedule, walking backwards from the current stop. Returns
		// false when the stop is not on the schedule or has not been departed.
		bool get_departure_data_for_halt(halthandle_t halt, departure_data_t &dep) const;

		// Give a vehicle that is about to leave this convoy a through journey
		// record for each boarding stop in its cargo that has none. Called
		// before removal so the distance and per-player way distances travelled
		// in this convoy are not lost.
		void capture_through_departures(vehicle_t *v);

		// True when this convoy's remaining schedule can take the packet from
		// the given stop to its next transfer or destination, or can deliver it
		// at that stop. Used to keep through cargo from being carried past a
		// stop by a convoy that does not serve it.
		bool serves_packet_from_stop(const ware_t &w, halthandle_t stop) const;

		// True when every packet aboard the vehicle has a continuation on this
		// convoy's schedule from the given stop. A vehicle that fails this test
		// must not be taken into the consist with its load.
		bool vehicle_cargo_is_served(const vehicle_t *v, halthandle_t halt) const;

	public:
		// Repair a packet's next transfer to the first later stop on this
		// convoy's schedule that serves its planned transfer or destination.
		// Returns false when neither is served. Does not change last_transfer.
		bool repair_packet_route(ware_t &ware, halthandle_t stop) const;

	private:
		// Unload cargo left at the stop by a recombination. Scheduled consists
		// repair onward routes first and discharge only a packet that arrives here
		// or has no continuation. Newly loose stock has no carrier yet, so only
		// cargo whose final destination is this stop is discharged.
		uint16 unload_cargo_for_stop(halthandle_t halt, bool newly_loose = false, bool force_discharge = false);

		// A couple order whose target does not resolve has a broken target: log
		// the error and leave the order unexecuted. It is never assembled from
		// the stop's pool.
		void report_broken_couple_target(halthandle_t halt);

		// A consist order that could not be carried out leaves the consist
		// unchanged; say so once, rather than failing silently.
		void report_consist_order_failed(halthandle_t halt) const;

		// An uncouple entry's resolved target: its line (also used when the
		// named convoy is bound to a line), its lineless convoy, and the target
		// entry's schedule and index.
		struct uncouple_target_t
		{
			linehandle_t line;
			convoihandle_t convoy;
			schedule_t *schedule;
			uint8 entry_index;

			uncouple_target_t() : schedule(nullptr), entry_index(255) {}
		};

		// Build the plan for a resolved division. The caller has already set
		// the stop and current-entry identity on the plan. Returns false when
		// either order cannot be satisfied from the current consist alone.
		bool build_recombination_plan(const schedule_entry_t &division, const uncouple_target_t &target);

		// Plan the division at the current entry. Used only while processing
		// that division; the loader plans the next division instead.
		void ensure_current_division_plan(halthandle_t halt);

		// Resolve an uncouple entry's target as path_explorer.cc does: a line
		// names that line's schedule; a convoy bound to a line names the line;
		// a lineless convoy names its own schedule. The target entry must exist
		// in that schedule, be at the given stop and carry a consist order.
		bool resolve_uncouple_target(const schedule_entry_t &entry, halthandle_t halt, uncouple_target_t &target);

		// Form the split-off portion: apply the target entry's consist order to
		// the newly created laid-over convoy, then give that convoy the target
		// schedule at the target entry and return it to normal loading. The
		// portion waits for the source commit's couplings, given by
		// source_coupling_operations, as well as its own.
		void form_divided_portion(convoihandle_t portion, halthandle_t halt, const schedule_entry_t &source_entry, const uncouple_target_t &target, uint32 source_coupling_operations);

		// How a pair entry's counterpart stands at a stop.
		enum paired_exchange_state_t
		{
			paired_not_configured, // not a pair entry: ordinary coupling, division or pool assembly
			paired_ready,          // the counterpart convoy is waiting: execute the exchange
			paired_partner_absent, // a valid pair whose counterpart has not arrived yet: wait
			paired_invalid         // configured as a pair but cannot be carried out
		};

		// Why a paired exchange was refused. A report names the reason once.
		enum pair_failure_t
		{
			pair_failure_none,
			pair_failure_target_missing,
			pair_failure_both_flags,
			pair_failure_missing_order,
			pair_failure_invalid_counterpart,
			pair_failure_waytype,
			pair_failure_self_target,
			pair_failure_lay_over,
			pair_failure_unsatisfiable,
			pair_failure_access,
			pair_failure_budget
		};

		// A resolved paired exchange: which side executes it, the counterpart
		// convoy when it is waiting, and the two consist orders.
		struct paired_exchange_t
		{
			bool own_is_receiver;
			convoihandle_t counterpart;
			const consist_order_t *own_order;
			const consist_order_t *counterpart_order;
			pair_failure_t failure;

			paired_exchange_t() : own_is_receiver(false), own_order(nullptr), counterpart_order(nullptr), failure(pair_failure_none) {}
		};

		// Classify the current entry. A pair is a couple entry whose counterpart
		// entry at the same stop carries uncouple, or the reverse; the two entries
		// must name each other and both must have a consist order. Readiness
		// requires the counterpart convoy to be waiting in AWAITING_RECOMBINATION.
		paired_exchange_state_t resolve_paired_exchange(halthandle_t halt, paired_exchange_t &pair) const;

		// True when the given convoy waits at the stop for the given counterpart
		// entry in AWAITING_RECOMBINATION, travelling in the required direction.
		bool is_waiting_pair_counterpart(const convoihandle_t &candidate, const schedule_entry_t &counterpart_entry, const schedule_t *counterpart_schedule, bool counterpart_reversed, halthandle_t halt) const;

		// Solve both end-state orders jointly and commit both consists as one
		// exchange. Returns false and reports once when the pair cannot be carried
		// out; in that case neither consist is changed and both convoys return to
		// loading.
		bool execute_paired_exchange(halthandle_t halt, const paired_exchange_t &pair);

		// The convoy's vehicles in forward order: a reversed consist is
		// de-reversed for matching against a consist order. Does not change state.
		void get_forward_consist(vector_tpl<vehicle_t*> &consist) const;

		// The vehicles an order may draw on: this convoy's own, plus the joining
		// convoy's, or, with no joining convoy, the stop's laid-over pool (or the
		// depot's list). Returns false when neither a stop nor a depot was given
		// for a pool assembly.
		bool get_recombination_available_vehicles(halthandle_t halt, depot_t* dep, convoihandle_t joining_convoy, vector_tpl<vehicle_t*> &available, const cargo_service_context_t *cargo_service = nullptr) const;

		// Match one order against the available vehicles. Does not change any
		// state. On success, final_consist holds the forward-oriented result
		// before any re-reversal, and final_reversed holds the chosen
		// orientation of each vehicle in the same order.
		consist_order_process_result match_consist_order(const consist_order_t &order, const vector_tpl<vehicle_t*> &available, const vector_tpl<vehicle_t*> &shadow, vector_tpl<vehicle_t*> &final_consist, vector_tpl<bool> &final_reversed, const cargo_service_context_t *cargo_service = nullptr) const;

		// Solve a pair without changing any state: search for a disjoint pair of
		// coupling-valid powered final consists satisfying both orders. Returns
		// false and sets failure when no arrangement exists or the search reaches
		// its computation limit. finals holds each forward-oriented result and
		// final_reversed its chosen per-vehicle orientations.
		bool solve_paired_exchange(convoi_t *const convoys[2], const consist_order_t *const orders[2], halthandle_t halt, vector_tpl<vehicle_t*> finals[2], vector_tpl<bool> final_reversed[2], bool changed[2], pair_failure_t &failure) const;

		// A translated phrase completing "because ..."; nullptr when the reason
		// has no shared phrasing (none, or the broken-target report).
		const char *paired_failure_reason(pair_failure_t failure) const;

		// Report that a coupling approach is held at a choose signal because
		// the two consists cannot be rearranged as required. Names both owners.
		void report_coupling_hold(halthandle_t halt, convoihandle_t target, const char *reason) const;

		// Replace the two consists with the solved forward-oriented final consists.
		// Applies the chosen per-vehicle orientations and adds their reversal time
		// to the shunting delay. Only vehicles that become loose stock are
		// discharged at the stop.
		void commit_paired_exchange(convoi_t *const convoys[2], const consist_order_t *const orders[2], const vector_tpl<vehicle_t*> finals[2], const vector_tpl<bool> final_reversed[2], const bool changed[2], halthandle_t halt);

		void report_paired_exchange_failure(halthandle_t halt, convoihandle_t counterpart, pair_failure_t failure) const;

		// Does the given entry's coupling or division target name this convoy or
		// its line?
		bool entry_names_this_convoy(const schedule_entry_t &entry, bool use_couple_target) const;

		// True when this convoy's current route to the given stop has passed the
		// last signal before the first tile of that stop.
		bool has_passed_last_signal_before_halt(halthandle_t halt) const;

		// Carry out the current entry's recombination orders at a stop: a paired
		// exchange, the ordinary one-sided coupling receive, a division, or
		// targetless pool assembly. Called on arrival and while waiting for a
		// counterpart in AWAITING_RECOMBINATION.
		void process_recombination_at_stop(halthandle_t halt, bool first_run);
};

#endif
