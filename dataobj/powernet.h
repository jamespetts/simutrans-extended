/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_POWERNET_H
#define DATAOBJ_POWERNET_H


#include "../simtypes.h"
#include "../tpl/slist_tpl.h"


/** @file powernet.h Data structure to manage a net of powerlines - a powernet */


/**
 * Data class for power networks. A two phase queue to store
 * and hand out power.
 */
class powernet_t
{
public:
	/**
	 * Must be called when a new map is started or loaded. Clears the table of networks.
	 */
	static void new_world();

	/// Steps all powernets
	static void step_all(uint32 delta_t);

private:
	static slist_tpl<powernet_t *> powernet_list;

	/// Max power capacity of each network, only purpose: avoid integer overflows
	static const uint64 max_capacity;

	/// Power supply in next step
	uint64 next_supply;
	/// Power supply in current step
	uint64 this_supply;
	/// Power demand in next step
	uint64 next_demand;
	/// Power demand in current step
	uint64 this_demand;

	// Just transfers power demand and supply to current step
	void step(uint32 delta_t);

public:
	powernet_t();
	~powernet_t();

	uint64 get_max_capacity() const { return max_capacity; }

	/// add to power supply for next step, respect max_capacity
	void add_supply(const uint32 p);

	/// @returns current power supply
	uint64 get_supply() const { return this_supply; }

	/// add to power demand for next step, respect max_capacity
	void add_demand(const uint32 p);

	/// @returns current power demand
	uint64 get_demand() const { return this_demand; }
};

#endif
