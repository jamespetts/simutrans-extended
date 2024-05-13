/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SCHEDULE_H
#define DATAOBJ_SCHEDULE_H


#include "schedule_entry.h"
#include "../dataobj/consist_order_t.h"

#include "../halthandle_t.h"

#include "../tpl/minivec_tpl.h"
#include "../tpl/koordhashtable_tpl.h"
#include "../tpl/inthashtable_tpl.h"

#include "../simskin.h"
#include "../display/simimg.h"
#include "../descriptor/skin_desc.h"

#define TIMES_HISTORY_SIZE 3

static const char* const schedule_type_text[] =
{
	"All", "Truck", "Train", "Ship", "Air", "Monorail", "Tram", "Maglev", "Narrowgauge"
};

class cbuffer_t;
class grund_t;
class player_t;
class karte_t;


/**
 * Class to hold schedule of vehicles in Simutrans.
 */
class schedule_t
{
	bool  editing_finished;
	uint8 current_stop;

	static schedule_entry_t dummy_entry;

	/**
	 * Fix up current_stop value, which we may have made out of range
	 */
	void make_current_stop_valid() {
		uint8 count = entries.get_count();
		if(  count == 0  ) {
			current_stop = 0;
		}
		else if(  current_stop >= count  ) {
			current_stop = count-1;
		}
	}

protected:
	schedule_t() : editing_finished(false), current_stop(0), bidirectional(false), mirrored(false), same_spacing_shift(true), spacing(0) {}

public:
	enum schedule_type {
		schedule             = 0,
		truck_schedule       = 1,
		train_schedule       = 2,
		ship_schedule        = 3,
		airplane_schedule    = 4,
		monorail_schedule    = 5,
		tram_schedule        = 6,
		maglev_schedule      = 7,
		narrowgauge_schedule = 8
	};

	minivec_tpl<schedule_entry_t> entries;

	/*
	* The consist orders for this schedule, indexed by
	* the unique ID of each schedule entry.
	*
	* These are separate from schedule entries because
	* they are much more heavyweight objects, and
	* schedule entries are often copied by value.
	*
	* Query: Would move constructors make a difference here?
	* This would need a great deal of checking and re-factoring
	* to verify and implement.
	*/
	inthashtable_tpl<uint16, consist_order_t, N_BAGS_SMALL> orders;

	/**
	 * Returns error message if stops are not allowed
	 */
	virtual char const* get_error_msg() const = 0;

	/**
	 * Returns true if this schedule allows stop at the
	 * given tile.
	 */
	virtual	bool is_stop_allowed(const grund_t *gr) const;

	bool empty() const { return entries.empty(); }

	uint8 get_count() const { return entries.get_count(); }

	virtual schedule_type get_type() const = 0;

	virtual waytype_t get_waytype() const = 0;

	// remove this entry
	void remove_entry( uint8 entry );

	// move the entry to front (and wrap)
	void move_entry_forward( uint8 entry );

	// move the entry to back (and wrap)
	void move_entry_backward( uint8 entry );

	/**
	 * Get current stop of the schedule.
	 */
	uint8 get_current_stop() const { return current_stop; }

	/// returns the current stop, always a valid entry
	schedule_entry_t const& get_current_entry() const { return current_stop >= entries.get_count() ? dummy_entry : entries[current_stop]; }

private:

	uint16 get_next_free_unique_id() const;

public:
	/**
	 * Set the current stop of the schedule .
	 * If new value is bigger than stops available, the max stop will be used.
	 */
	void set_current_stop(uint8 new_current_stop) {
		current_stop = new_current_stop;
		make_current_stop_valid();
	}

	/// advance current_stop by one
	void advance() {
		if(  !entries.empty()  ) {
			current_stop = (current_stop+1)%entries.get_count();
		}
	}
	// decrement entry by one
	void advance_reverse() {
		if(  !entries.empty()  ) {
			current_stop = current_stop ? current_stop-1 : entries.get_count()-1;
		}
	}

	/**
	 * Sets the current entry to a reversing type
	 */
	void set_reverse(sint8 reverse = 1, sint16 index = -1)
	{
		uint8 inx = index == -1 ? current_stop : (uint8)index;
 		if(!entries.empty())
		{
			entries[inx].reverse = reverse;
		}
	}

	/**
	 * Increment or decrement the given index according to the given direction.
	 * Also switches the direction if necessary.
	 * @author yobbobandana
	 */
	void increment_index(uint8 *index, bool *reversed) const;

	/**
	 * Same as increment_index(), but skips waypoints.
	 * @author suitougreentea
	 */
	void increment_index_until_next_halt(player_t* player, uint8 *index, bool *reversed) const;

	/*
	 * Calculate the distance via waypoint, not the straight line distance between stations
	 * Currently it is for the schedule UI and does not consider reversed. Please change if necessary
	 * Also, next "halt" is not correct because the tile coordinates do not always match. So needed to calculate the distance by coordinates
	 */
	uint32 calc_distance_to_next_halt(player_t *player, uint8 index) const;

	/***
	 * "Completed"
	 */
	inline bool is_editing_finished() const { return editing_finished; }
	void finish_editing() { editing_finished = true; }
	void start_editing() { editing_finished = false; }
	inline int get_spacing() const { return spacing; }
	inline void set_spacing( int s ) { spacing = s; }

	virtual ~schedule_t() {}

	/**
	 * returns a halthandle for the next halt in the schedule (or unbound)
	 */
	halthandle_t get_next_halt( player_t *player, halthandle_t halt ) const;

	/**
	 * returns a halthandle for the previous halt in the schedule (or unbound)
	 */
	halthandle_t get_prev_halt( player_t *player ) const;

	// This is used to display the outline the schedule.
	halthandle_t get_origin_halt(player_t *player) const;
	halthandle_t get_destination_halt(player_t *player) const;

	/**
	* Inserts a coordinate at current_stop into the schedule.
	*/
	bool insert(const grund_t* gr, uint16 minimum_loading = 0, uint8 waiting_time_shift = 0, sint16 spacing_shift = 0, uint32 flags = 0, uint16 condition_bitfield_broadcaster = 0, uint16 condition_bitfield_receiver = 0, uint16 target_id_condition_trigger = 0, uint16 target_id_couple = 0, uint16 target_id_uncouple = 0, uint16 target_unique_entry_uncouple = 0, bool show_failure = false, uint16 max_speed_kmh = 65535);

	/**
	* Appends a coordinate to the schedule.
	*/
	bool append(const grund_t* gr, uint16 minimum_loading = 0, uint8 waiting_time_shift = 0, sint16 spacing_shift = 0, uint32 flags = 0, uint16 condition_bitfield_broadcaster = 0, uint16 condition_bitfield_receiver = 0, uint16 target_id_condition_trigger = 0, uint16 target_id_couple = 0, uint16 target_id_uncouple = 0, uint16 target_unique_entry_uncouple = 0, uint16 max_speed_kmh = 65535);

	/**
	 * Cleanup a schedule, removes double entries.
	 */
	void cleanup();

	/**
	 * Remove current_stop entry from the schedule.
	 */
	bool remove();

	void rdwr(loadsave_t *file);

	void rotate90( sint16 y_size );

	/**
	 * if the passed in schedule matches "this", then return true
	 */
	bool matches(karte_t *welt, const schedule_t *schedule);

	inline bool is_bidirectional() const { return bidirectional; }
	inline bool is_mirrored() const { return mirrored; }
	inline bool is_same_spacing_shift() const { return same_spacing_shift; }
	void set_bidirectional(bool bidirec = true ) { bidirectional = bidirec; }
	void set_mirrored(bool mir = true ) { mirrored = mir; }
	void set_same_spacing_shift(bool s = true) { same_spacing_shift = s; }

	/*
	 * Compare this schedule with another, ignoring order and exact positions and waypoints.
	 */
	bool similar( const schedule_t *schedule, const player_t *player );

	/**
	 * Calculates a return way for this schedule.
	 * Will add elements 1 to end in reverse order to schedule.
	 */
	void add_return_way();

	virtual schedule_t* copy() = 0;//{ return new schedule_t(this); }

	// copy all entries from schedule src to this and adjusts current_stop
	void copy_from(const schedule_t *src);

	// fills the given buffer with a schedule
	void sprintf_schedule( cbuffer_t &buf ) const;

	// converts this string into a schedule
	bool sscanf_schedule( const char * );

	/** Checks whetehr the given stop is contained in the schedule
	 * @author: jamespetts, September 2011
	 */
	bool is_contained(koord3d pos);

	// for GUI: To get the station number of the destination of the cargo
	uint8 get_entry_index(halthandle_t halt, player_t *player, bool reverse) const;

	bool check_consist_orders_for_match(uint16 entry_id_this, const schedule_t* other_schedule, uint16 entry_id_other_schedule) const;

	bool entry_has_consist_order(uint16 unique_id) const;

	const consist_order_t& get_consist_order(uint16 unique_id);

	convoihandle_t get_couple_target(uint16 unique_id, halthandle_t halt);
	// TODO: Implement get_uncouple_target

	// These hashtables represent what categories of goods are carried
	// to and from each entry according to this schedule's consist orders.
	// The key is the unique ID and the value is the category.
	//
	// Where this schedule has no consist orders, these are blank, and
	// the goods that this consist carries are instead determinable
	// from the actual consists running on this schedule.
	inthashtable_tpl<uint16, vector_tpl<uint8>, N_BAGS_SMALL> catg_carried_to;
	inthashtable_tpl<uint16, vector_tpl<uint8>, N_BAGS_SMALL> catg_carried_from;

	const inthashtable_tpl<uint16, vector_tpl<uint8>, N_BAGS_SMALL>& get_catg_carried_to() const { return catg_carried_to; }
	const inthashtable_tpl<uint16, vector_tpl<uint8>, N_BAGS_SMALL>& get_catg_carried_from() const { return catg_carried_from; }

	inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> passenger_min_class_carried_to;
	inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> passenger_min_class_carried_from;

	const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL>& get_passenger_min_class_carried_to() const { return passenger_min_class_carried_to; }
	const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL>& get_passenger_min_class_carried_from() const { return passenger_min_class_carried_from; }

	inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> mail_min_class_carried_to;
	inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> mail_min_class_carried_from;

	const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL>& get_mail_min_class_carried_to() const { return mail_min_class_carried_to; }
	const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL>& get_mail_min_class_carried_from() const { return mail_min_class_carried_from; }

	uint8 get_entry_from_unique_id(uint16 unique_id) const;
	uint16 get_unique_id_from_entry(uint8 entry) const;

	image_id get_schedule_type_symbol() const
	{
		switch (get_type())
		{
			case schedule_t::truck_schedule:
				return skinverwaltung_t::autohaltsymbol->get_image_id(0);
			case schedule_t::train_schedule:
				return skinverwaltung_t::zughaltsymbol->get_image_id(0);
			case schedule_t::ship_schedule:
				return skinverwaltung_t::schiffshaltsymbol->get_image_id(0);
			case schedule_t::airplane_schedule:
				return skinverwaltung_t::airhaltsymbol->get_image_id(0);
			case schedule_t::monorail_schedule:
				return skinverwaltung_t::monorailhaltsymbol->get_image_id(0);
			case schedule_t::tram_schedule:
				return skinverwaltung_t::tramhaltsymbol->get_image_id(0);
			case schedule_t::maglev_schedule:
				return skinverwaltung_t::maglevhaltsymbol->get_image_id(0);
			case schedule_t::narrowgauge_schedule:
				return skinverwaltung_t::narrowgaugehaltsymbol->get_image_id(0);
			case schedule_t::schedule:
			default:
				return IMG_EMPTY;
				break;
		}
		return IMG_EMPTY;
	}

	inline char const *get_schedule_type_name() const
	{
		return schedule_type_text[get_type()];
	};

	// Distance to return to the first scheduled point
	uint32 get_travel_distance() const;

	/**
	 * Append description of entry to buf.
	 * If @p max_chars > 0 then append short version, without loading level and position.
	 */
	static void gimme_short_stop_name(cbuffer_t& buf, karte_t* welt, player_t const* const player_, const schedule_t *schedule, int i, int max_chars);
	static void gimme_stop_name(cbuffer_t & buf, karte_t* welt, const player_t *player_, const schedule_entry_t &entry, bool no_control_tower);

	// True if this schedule has at least 1 consist order
	bool has_consist_orders() const;

	// A helper method for updating the hashtables of consist orders
	void parse_orders();

	// True if any consist order provides for this category of goods to be carried anywhere along this schedule's route
	bool carries_catg(uint8 catg_index) const;

	// Returns the lowest class carried for this category (mail/passengers) provided by any consist order along this schedule's route.
	uint8 min_class_carried(uint8 catg_index) const;

private:
	bool bidirectional;
	bool mirrored;
	bool same_spacing_shift;

	/*
	* The number of convoys per month, now in 12ths:
	* e.g., if this number is 12, there will be 1
	* departure per game month (12/12 = 1) per
	* timed stop in this schedule.
	*/
	sint16 spacing;
};

/**
 * Schedules with stops on tracks.
 */
class train_schedule_t : public schedule_t
{
public:
	train_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new train_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Zughalt muss auf\nSchiene liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return train_schedule; }

	waytype_t get_waytype() const OVERRIDE { return track_wt; }
};

/**
 * Schedules with stops on tram tracks.
 */
class tram_schedule_t : public train_schedule_t
{
public:
	tram_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new tram_schedule_t(); s->copy_from(this); return s; }

	schedule_type get_type() const OVERRIDE { return tram_schedule; }

	waytype_t get_waytype() const OVERRIDE { return tram_wt; }
};


/**
 * Schedules with stops on roads.
 */
class truck_schedule_t : public schedule_t
{
public:
	truck_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new truck_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Autohalt muss auf\nStrasse liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return truck_schedule; }

	waytype_t get_waytype() const OVERRIDE { return road_wt; }
};


/**
 * Schedules with stops on water.
 */
class ship_schedule_t : public schedule_t
{
public:
	ship_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new ship_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Schiffhalt muss im\nWasser liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return ship_schedule; }

	waytype_t get_waytype() const OVERRIDE { return water_wt; }
};


/**
 * Schedules for airplanes.
 */
class airplane_schedule_ : public schedule_t
{
public:
	airplane_schedule_() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new airplane_schedule_(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Flugzeughalt muss auf\nRunway liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return airplane_schedule; }

	waytype_t get_waytype() const OVERRIDE { return air_wt; }
};

/**
 * Schedules with stops on mono-rails.
 */
class monorail_schedule_t : public schedule_t
{
public:
	monorail_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new monorail_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Monorailhalt muss auf\nMonorail liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return monorail_schedule; }

	waytype_t get_waytype() const OVERRIDE { return monorail_wt; }
};

/**
 * Schedules with stops on maglev tracks.
 */
class maglev_schedule_t : public schedule_t
{
public:
	maglev_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new maglev_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "Maglevhalt muss auf\nMaglevschiene liegen!\n"; }

	schedule_type get_type() const OVERRIDE { return maglev_schedule; }

	waytype_t get_waytype() const OVERRIDE { return maglev_wt; }
};

/**
 * Schedules with stops on narrowgauge tracks.
 */
class narrowgauge_schedule_t : public schedule_t
{
public:
	narrowgauge_schedule_t() {}
	schedule_t* copy() OVERRIDE { schedule_t *s = new narrowgauge_schedule_t(); s->copy_from(this); return s; }
	const char *get_error_msg() const OVERRIDE { return "On narrowgauge track only!\n"; }

	schedule_type get_type() const OVERRIDE { return narrowgauge_schedule; }

	waytype_t get_waytype() const OVERRIDE { return narrowgauge_wt; }
};

class departure_point_t
{
public:
	union { sint16 entry;    sint16 x; };
	union { sint16 reversed; sint16 y; };

	departure_point_t(uint8 e, bool rev)
	{
		entry = e;
		reversed = rev;
	}

	departure_point_t()
	{
		entry = 0;
		reversed = 0;
	}
};

static inline bool operator == (const departure_point_t &a, const departure_point_t &b)
{
	// only this works with O3 optimisation!
	return (a.entry - b.entry) == 0 && a.reversed == b.reversed;
}

static inline bool operator != (const departure_point_t &a, const departure_point_t &b)
{
	// only this works with O3 optimisation!
	return (a.entry - b.entry) != 0 || a.reversed != b.reversed;
}

static inline bool operator == (const departure_point_t& a, int b)
{
	// For hashtable use.
	return b == 0 && a == departure_point_t(0, true);
}

class times_history_data_t {
private:
	uint32 history[TIMES_HISTORY_SIZE];
public:
	times_history_data_t();
	uint32 get_entry(uint16 index) const;
	void put(uint32 time);
	void set(uint16 index, uint32 time);
    uint32 get_average_seconds() const;
};

typedef koordhashtable_tpl<departure_point_t, times_history_data_t, N_BAGS_SMALL> times_history_map;

#endif
