/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_RAIL_VEHICLE_H
#define VEHICLE_RAIL_VEHICLE_H


#include "vehicle.h"


/**
 * A class for rail vehicles (trains). Manages the look of the vehicles
 * and the navigability of tiles.
 * @see vehicle_t
 */
class rail_vehicle_t : public vehicle_t
{
protected:
	bool check_next_tile(const grund_t *bd) const OVERRIDE;

	void enter_tile(grund_t*) OVERRIDE;

	sint32 activate_choose_signal(uint16 start_index, uint16 &next_signal_index, uint32 brake_steps, uint16 modified_sighting_distance_tiles, route_t* route, sint32 modified_route_index);

	working_method_t working_method = drive_by_sight;

public:
	waytype_t get_waytype() const OVERRIDE { return track_wt; }

	void rdwr_from_convoi(loadsave_t *file) OVERRIDE;

	// since we might need to unreserve previously used blocks, we must do this before calculation a new route
	route_t::route_result_t calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route) OVERRIDE;

	// how expensive to go here (for way search)
	int get_cost(const grund_t *, const sint32, koord) OVERRIDE;

	uint32 get_cost_upslope() const OVERRIDE { return 75; } // Standard is 15

	// returns true for the way search to an unknown target.
	bool is_target(const grund_t *,const grund_t *) OVERRIDE;

	// handles all block stuff and route choosing ...
	bool can_enter_tile(const grund_t *gr_next, sint32 &restart_speed, uint8) OVERRIDE;

	// reserves or un-reserves all blocks and returns the handle to the next block (if there)
	// returns true on successful reservation (the specific number being the number of blocks ahead clear,
	// needed for setting signal aspects in some cases).
	sint32 block_reserver(route_t *route, uint16 start_index, uint16 modified_sighting_distance_tiles, uint16 &next_signal, int signal_count, bool reserve, bool force_unreserve, bool is_choosing = false, bool is_from_token = false, bool is_from_starter = false, bool is_from_directional = false, uint32 brake_steps = 1, uint16 first_one_train_staff_index = INVALID_INDEX, bool from_call_on = false, bool *break_loop = NULL);

	// Finds the next signal without reserving any tiles.
	// Used for time interval (with and without telegraph) signals on plain track.
	void find_next_signal(route_t* route, uint16 start_index, uint16 &next_signal);

	void leave_tile() OVERRIDE;

	/// Unreserve behind the train using the current route
	void unreserve_in_rear();

	/// Unreserve behind the train (irrespective of route) all station tiles in rear
	void unreserve_station();

	void clear_token_reservation(signal_t* sig, rail_vehicle_t* w, schiene_t* sch);

#ifdef INLINE_OBJ_TYPE
protected:
	rail_vehicle_t(typ type, loadsave_t *file, bool is_leading, bool is_last);
	rail_vehicle_t(typ type, koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t *cnv); // start und schedule
	void init(loadsave_t *file, bool is_leading, bool is_last);
public:
#else
	typ get_typ() const OVERRIDE { return rail_vehicle; }
#endif

	rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last);
	rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t *cnv); // start und schedule
	virtual ~rail_vehicle_t();

	void set_convoi(convoi_t *c) OVERRIDE;

	virtual schedule_t * generate_new_schedule() const OVERRIDE;

	working_method_t get_working_method() const { return working_method; }
	void set_working_method(working_method_t value);
};



/**
 * very similar to normal railroad, so we can implement it here completely
 * @see vehicle_t
 */
class monorail_rail_vehicle_t : public rail_vehicle_t
{
public:
	waytype_t get_waytype() const OVERRIDE { return monorail_wt; }

#ifdef INLINE_OBJ_TYPE
	// all handled by rail_vehicle_t
	monorail_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(monorail_vehicle, file,is_leading, is_last) {}
	monorail_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(monorail_vehicle, pos, desc, player, cnv) {}
#else
	// all handled by rail_vehicle_t
	monorail_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(file,is_leading, is_last) {}
	monorail_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(pos, desc, player, cnv) {}

	typ get_typ() const OVERRIDE { return monorail_vehicle; }
#endif

	schedule_t * generate_new_schedule() const OVERRIDE;
};



/**
 * very similar to normal railroad, so we can implement it here completely
 * @see vehicle_t
 */
class maglev_rail_vehicle_t : public rail_vehicle_t
{
public:
	waytype_t get_waytype() const OVERRIDE { return maglev_wt; }

#ifdef INLINE_OBJ_TYPE
	// all handled by rail_vehicle_t
	maglev_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(maglev_vehicle, file, is_leading, is_last) {}
	maglev_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(maglev_vehicle, pos, desc, player, cnv) {}
#else
	// all handled by rail_vehicle_t
	maglev_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(file, is_leading, is_last) {}
	maglev_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(pos, desc, player, cnv) {}

	typ get_typ() const OVERRIDE { return maglev_vehicle; }
#endif

	schedule_t * generate_new_schedule() const OVERRIDE;
};



/**
 * very similar to normal railroad, so we can implement it here completely
 * @see vehicle_t
 */
class narrowgauge_rail_vehicle_t : public rail_vehicle_t
{
public:
	waytype_t get_waytype() const OVERRIDE { return narrowgauge_wt; }

#ifdef INLINE_OBJ_TYPE
	// all handled by rail_vehicle_t
	narrowgauge_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(narrowgauge_vehicle, file, is_leading, is_last) {}
	narrowgauge_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(narrowgauge_vehicle, pos, desc, player, cnv) {}
#else
	// all handled by rail_vehicle_t
	narrowgauge_rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) : rail_vehicle_t(file, is_leading, is_last) {}
	narrowgauge_rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv) : rail_vehicle_t(pos, desc, player, cnv) {}

	typ get_typ() const OVERRIDE { return narrowgauge_vehicle; }
#endif

	schedule_t * generate_new_schedule() const OVERRIDE;
};


#endif
