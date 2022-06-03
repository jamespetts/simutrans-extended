/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SCHEDULE_ENTRY_H
#define DATAOBJ_SCHEDULE_ENTRY_H


#include "koord3d.h"

/**
 * A schedule entry.
 */
struct schedule_entry_t
{
public:
	schedule_entry_t() {}

	schedule_entry_t(koord3d const& pos,
		uint16 const minimum_loading,
		sint8 const waiting_time_shift,
		sint16 spacing_shift,
		sint8 reverse,
		uint16 flags,
		uint16 unique_entry_id,
		uint16 condition_bitfield_broadcaster,
		uint16 condition_bitfield_receiver,
		uint16 target_id_condition_trigger,
		uint16 target_id_couple,
		uint16 target_id_uncouple,
		uint16 target_unique_entry_uncouple) :
		pos(pos),
		minimum_loading(minimum_loading),
		spacing_shift(spacing_shift),
		waiting_time_shift(waiting_time_shift),
		reverse(reverse),
		flags(flags),
		unique_entry_id(unique_entry_id),
		condition_bitfield_broadcaster(condition_bitfield_broadcaster),
		condition_bitfield_receiver(condition_bitfield_receiver),
		target_id_condition_trigger(target_id_condition_trigger),
		target_id_couple(target_id_couple),
		target_id_uncouple(target_id_uncouple),
		target_unique_entry_uncouple(target_unique_entry_uncouple)
	{}

	/**
	 * target position
	 */
	koord3d pos;

	enum schedule_entry_flag
	{
		/* General*/
		wait_for_time					= (1u << 0),
		lay_over						= (1u << 1),
		ignore_choose					= (1u << 2),
		force_range_stop				= (1u << 3),

		/* Conditional triggers */
		conditional_depart_before_wait	= (1u << 4),
		conditional_depart_after_wait	= (1u << 5),
		conditional_skip				= (1u << 6),
		send_trigger					= (1u << 7),
		cond_trigger_is_line_or_cnv		= (1u << 8),
		clear_stored_triggers_on_dep	= (1u << 9),
		trigger_one_only				= (1u << 10),

		/* Re-combination */
		couple							= (1u << 11),
		uncouple						= (1u << 12),
		couple_target_is_line_or_cnv	= (1u << 13),
		uncouple_target_is_line_or_cnv	= (1u << 14),
		uncouple_target_sch_is_reversed = (1u << 15)
	};

	// These are used in place of minimum_loading to control
	// what a vehicle does when it goes to the depot.
	enum depot_flag
	{
		delete_entry			= (1u << 0),
		store					= (1u << 1),
		maintain_or_overhaul	= (1u << 2)
	};

	/*
	* A bitfield of flags of the type schedule_entry_flag (supra)
	*/
	uint16 flags;

	/**
	 * Wait for % load at this stop
	 * (ignored on waypoints)
	 */
	uint16 minimum_loading;

	/**
	 * spacing shift
	 */
	sint16 spacing_shift;

	/*
	* A unique reference for this schedule entry that
	* remains constant even when the sequence (and
	* therefore the index) of this entry changes
	*/
	uint16 unique_entry_id;

	/*
	* A bitfield of the conditions to trigger
	* (in the range 1u << 0 to 1u << 15)
	* when this convoy arrives at this stop
	* or depot.
	*/
	uint16 condition_bitfield_broadcaster;

	/*
	* A bitfield of the conditions all of
	* which must be matched by the convoy's
	* list of stored triggers before the
	* convoy will leave the stop or depot
	*/
	uint16 condition_bitfield_receiver;

	/*
	* The ID of the line or convoy that will
	* receive the trigger when a convoy with
	* this schedule arrives at the stop
	* with this entry.
	*/
	uint16 target_id_condition_trigger;

	/*
	* The ID of the line or convoy to which
	* the convoy with this schedule will attempt
	* to couple on reaching this destination.
	*/
	uint16 target_id_couple;

	/*
	* The ID of the line or convoy the schedule of
	* which the convoy formed by the uncoupled vehicles
	* from the convoy with this schedule will adopt on
	* reaching the destination specified in this entry.
	*/
	uint16 target_id_uncouple;

	/*
	* The unique ID of the point in the schedule specified
	* in target_id_couple from which the convoy comprising
	* the vehicles uncoupled at this schedule entry will
	* start.
	*
	* There is no equivalent for couple because the
	* convoy to which this convoy will be coupled will
	* alerady be at the correct point in its schedule.
	*/
	uint16 target_unique_entry_uncouple;

	/**
	* maximum waiting time in 1/2^(16-n) parts of a month
	* (only active if minimum_loading!=0)
	*/
	sint8 waiting_time_shift;

	/**
	 * Whether a convoy needs to reverse after this entry.
	 * 0 = no; 1 = yes; -1 = undefined
	 * @author: jamespetts
	 */
	sint8 reverse;

	bool is_flag_set(schedule_entry_flag flag) const { return flag & flags; }

	void set_flag(schedule_entry_flag flag) { flags |= flag; }

	void clear_flag(schedule_entry_flag flag) { flags &= ~flag; }
};

inline bool operator ==(const schedule_entry_t &a, const schedule_entry_t &b)
{
	return a.pos == b.pos  &&  a.minimum_loading == b.minimum_loading  &&  a.waiting_time_shift == b.waiting_time_shift;
}


#endif
