/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMWARE_H
#define SIMWARE_H


#include "halthandle_t.h"
#include "dataobj/koord.h"
#include "descriptor/goods_desc.h"

class goods_manager_t;
class karte_t;
class player_t;

/** Class to handle goods packets (and their destinations) */
class ware_t
{
	friend class goods_manager_t;

private:
	/// private lookup table to speedup
	static const goods_desc_t *index_to_desc[256];

public:
	/// amount of goods
	uint32 amount;

	/// type of good, used as index into index_to_desc
	uint32 index: 8;

	// Necessary to determine whether to book
	// jobs taken on arrival.
	bool is_commuting_trip : 1;

	/// The class of mail/passengers. Not used for goods.
	uint8 g_class;

	/// The percentage of the maximum tolerable journey time for
	/// any given level of comfort that this packet of passengers
	/// (if passengers) will travel in a lower class of accommodation
	/// than available on a convoy.
	uint16 comfort_preference_percentage;

private:
	/**
	 * Handle of target station.
	 */
	halthandle_t destination;

	/**
	 * Handle of station, where the packet has to leave convoy.
	 */
	halthandle_t next_transfer;

	/**
	 * A handle to the ultimate origin.
	 * @author: jamespetts
	 */
	halthandle_t origin;

	/**
	 * A handle to the previous transfer
	 * for the purposes of distance calculation
	 * @author: jamespetts, November 2011
	 */
	halthandle_t last_transfer;

	/**
	 * Target position (factory, etc)
	 */
	koord destination_pos;

	/**
	 * Update target (destination_pos) for factory-going goods (after loading or rotating)
	 */
	void update_factory_target();

public:
	inline const halthandle_t &get_destination() const { return destination; }
	void set_destination(const halthandle_t &_destination) { destination = _destination; }

	inline const halthandle_t &get_next_transfer() const { return next_transfer; }
	inline halthandle_t &access_next_transfer() { return next_transfer; }
	void set_next_transfer(const halthandle_t &_next_transfer) { next_transfer = _next_transfer; }

	koord get_destination_pos() const { return destination_pos; }
	void set_destination_pos(const koord _destination_pos) { destination_pos = _destination_pos; }

	void reset() { amount = 0; destination = next_transfer = origin = last_transfer = halthandle_t(); destination_pos = koord::invalid; }

	ware_t();
	ware_t(const goods_desc_t *typ);
	ware_t(const goods_desc_t *typ, halthandle_t o);
	ware_t(loadsave_t *file);

	/// @returns the non-translated name of the ware.
	inline const char *get_name() const { return get_desc()->get_name(); }
	inline const char *get_mass() const { return get_desc()->get_mass(); }
	inline uint8 get_catg() const { return get_desc()->get_catg(); }
	inline uint8 get_index() const { return index; }
	inline uint8 get_class() const { return g_class; }
	inline void set_class(uint8 value) { g_class = value; }

	//@author: jamespetts
	inline halthandle_t get_origin() const { return origin; }
	void set_origin(halthandle_t value) { origin = value; }
	inline halthandle_t get_last_transfer() const { return last_transfer; }
	void set_last_transfer(halthandle_t value) { last_transfer = value; }

	inline const goods_desc_t* get_desc() const { return index_to_desc[index]; }
	void set_desc(const goods_desc_t* type);

	void rdwr(loadsave_t *file);

	void finish_rd(karte_t *welt);

	// find out the category ...
	inline bool is_passenger() const { return index == 0; }
	inline bool is_mail() const { return index == 1; }
	inline bool is_freight() const { return index > 2; }

	// The time at which this packet arrived at the current station
	// @author: jamespetts
	sint64 arrival_time;

	int operator==(const ware_t &w) {
		return amount == w.amount &&
			next_transfer == w.next_transfer &&
			arrival_time == w.arrival_time &&
			index == w.index &&
			destination == w.destination &&
			destination_pos == w.destination_pos &&
			origin == w.origin &&
			last_transfer == w.last_transfer &&
			g_class == w.g_class;
	}

	bool can_merge_with(const ware_t &w)
	{
		return next_transfer == w.next_transfer &&
			index == w.index &&
			destination == w.destination &&
			destination_pos == w.destination_pos &&
			origin == w.origin &&
			last_transfer == w.last_transfer &&
			g_class == w.g_class;
	}

	bool operator <= (const ware_t &w)
	{
		// Used only for the binary heap
		return arrival_time <= w.arrival_time;
	}

	int operator!=(const ware_t &w) { return !(*this == w); }

	/**
	 * Adjust target coordinates.
	 * Must be called after factories have been rotated!
	 */
	void rotate90( sint16 y_size );
};

#endif
