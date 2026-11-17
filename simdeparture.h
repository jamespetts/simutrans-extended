/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMDEPARTURE_H
#define SIMDEPARTURE_H


#include "simtypes.h"
#include "simconst.h"
#include "tpl/vector_tpl.h"


/**
 * Accumulated travel data since a convoy left a stop.
 *
 * The same record is used for the convoy's own departure bookkeeping and for
 * through departures carried by a vehicle across a consist recombination. The
 * distances at index MAX_PLAYER_COUNT and above are special values:
 * MAX_PLAYER_COUNT is the overall distance in the unit used by the schedule
 * (the sum of the leg distances), MAX_PLAYER_COUNT + 1 is the distance over
 * ways with no owner, and the remaining indices are per-player way distances
 * in vehicle steps.
 */
struct departure_data_t
{
public:
	/**
	 * Departure time in internal ticks
	 */
	sint64 departure_time;

	/**
	 * Accumulated distance since the convoy departed from
	 * this stop, indexed by the player number of the way
	 * over which the convoy has passed. If the way is
	 * ownerless, it is recorded as belonging as being
	 * MAX_PLAYER_COUNT + 1.
	 *
	 * accumulated_distance_since_departure[MAX_PLAYER_COUNT]
	 * is a special value for overall distance, denominated
	 * in a different unit to the others, which others are
	 * denominated in steps
	 */
private:
	uint32 accumulated_distance_since_departure[MAX_PLAYER_COUNT + 2];

public:

	departure_data_t()
	{
		departure_time = 0ll;
		init_distances();
	}

	/**
	 * Method for adding the total distance at intermediate halts
	 */
	void add_overall_distance(uint32 distance)
	{
		accumulated_distance_since_departure[MAX_PLAYER_COUNT] += distance;
	}

	/**
	 * Method to get the overall distance. This should be the basis
	 * for measuring the total revenue.
	 */
	uint32 get_overall_distance() const
	{
		return accumulated_distance_since_departure[MAX_PLAYER_COUNT];
	}

	uint32 get_way_distance(uint8 index) const
	{
		return accumulated_distance_since_departure[index];
	}


	/**
	 * Method to increment by one the distance recorded as
	 * travelled by a vehicle over a particular way, indexed
	 * by the player ID of the way. This is used for revenue
	 * apportionment.
	 */
	void increment_way_distance(uint8 player, uint32 steps)
	{
		accumulated_distance_since_departure[player] += steps;
	}

	/**
	 * Method for setting values in the array ab initio.
	 * Used when loading from a saved game only.
	 */
	void set_distance(uint8 index, uint32 value)
	{
		accumulated_distance_since_departure[index] = value;
	}

	/**
	 * Method for initialising the value of the overall
	 * distances to zero
	 */
	void init_distances()
	{
		for(int i = 0; i < MAX_PLAYER_COUNT + 2; i ++)
		{
			accumulated_distance_since_departure[i] = 0;
		}
	}
};


/**
 * A through journey record carried by a vehicle that has crossed a consist
 * recombination. The halt id is the packet's last transfer stop, which is the
 * start of the through leg. The departure data is advanced by every convoy the
 * vehicle travels in, so the record covers the whole leg when the packet
 * finally leaves the vehicle.
 */
struct through_departure_t
{
	uint16 halt_id;
	/// True when the record starts at the actual boarding stop and contains
	/// the distance travelled since then. A record created without source
	/// departure data must not be used for revenue: after the next leg its
	/// distance would be non-zero but would still omit the earlier travel.
	bool history_known;
	departure_data_t departure;
};

typedef vector_tpl<through_departure_t> through_departures_t;


#endif
