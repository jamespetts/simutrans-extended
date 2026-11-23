/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "road_vehicle.h"

#include "simroadtraffic.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../boden/wege/strasse.h"
#include "../dataobj/schedule.h"
#include "../dataobj/translator.h"
#include "../descriptor/citycar_desc.h"
#include "../obj/crossing.h"
#include "../obj/roadsign.h"
#include "../player/simplay.h"
#include "../simware.h"
#include "../simworld.h"
#include "../simconvoi.h"
#include "../simhalt.h"
#include "../simmesg.h"

#include "../utils/cbuffer_t.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <xmmintrin.h>
#define ROAD_PREFETCH_T0(addr) _mm_prefetch(reinterpret_cast<const char *>(addr), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define ROAD_PREFETCH_T0(addr) __builtin_prefetch((addr), 0, 3)
#else
#define ROAD_PREFETCH_T0(addr) ((void)(addr))
#endif


/**
 * Forward distance over which the rendering-only lateral pull-in blend moves
 * from the travel lane to the stand, in vehicle steps. The lateral movement is
 * capped by the distance actually travelled, so the vehicle never appears to
 * move sideways faster than it moves forward. A through-stop stand uses the
 * shorter distance; a terminal's passing-lane stands need the longer one
 * because their lateral offset includes the whole passing-lane separation.
 */
static const uint32 PULL_IN_BLEND_STEPS = 64;

/// How far before the end of a through stop's halt tile the blend may start,
/// in vehicle steps. This leaves the 64-step blend room to finish before the
/// stop position at the end or the centre of the tile.
static const uint32 PULL_IN_APPROACH_STEPS = 128;

/// Fully at the stand in the 0 to 256 rendering blend.
static const uint16 PULL_IN_FACTOR_ONE = 256;


namespace road_space
{

/**
 * Standstill separation between two stopped road vehicles, in millimetres.
 * One metre is the separation the Highway Code asks a driver to leave when
 * stopping behind another vehicle, so that the vehicle in front can be seen
 * and passed if necessary (rule 246, "leave a safe distance").
 */
static const sint32 STANDSTILL_GAP_MM = 1000;

/**
 * Perception and reaction time used for a following vehicle when the vehicle
 * ahead is moving, in milliseconds. The two-second rule is the separation a
 * driver should keep at speed; only the perception and reaction part of it is
 * needed inside a moving queue, because the vehicle ahead can be seen
 * braking. 1.5 s is the usual perception and reaction time allowed in road
 * design (the value used for stopping sight distance in the Design Manual for
 * Roads and Bridges).
 */
static const sint32 REACTION_TIME_MS = 1500;

/**
 * Reaction distance of a follower travelling at @p speed, in millimetres.
 * The speed is in km/h, so the distance covered in REACTION_TIME_MS is
 * speed * 1000 m/km / 3600 s/hour * REACTION_TIME_MS ms / 1000 ms/s metres
 * = speed * REACTION_TIME_MS * 1000 / 3600 millimetres.
 * A follower at 50 km/h with a 1.5 s reaction time therefore keeps about
 * 20.8 m, not 20.8 mm.
 */
constexpr sint64 reaction_gap_mm(const sint32 speed)
{
	return ((sint64)speed_to_kmh(speed) * REACTION_TIME_MS * 1000) / 3600;
}

// Compile-time guard on the unit conversion: a moving follower at 50 km/h
// must keep a reaction distance of about 21 m.
static_assert(reaction_gap_mm(kmh_to_speed(50)) >= 20000, "reaction distance at 50 km/h is below 20 m");
static_assert(reaction_gap_mm(kmh_to_speed(50)) <= 21000, "reaction distance at 50 km/h is above 21 m");

bool allows_queueing(const grund_t *gr)
{
	if (gr == NULL)
	{
		return false;
	}
	// A way of the road waytype is always a strasse_t (weg_t::create()).
	const strasse_t *str = static_cast<const strasse_t *>(gr->get_weg(road_wt));
	if (str == NULL || str->is_crossing() || str->is_diagonal())
	{
		return false;
	}
	// is_straight() is true for north-south, east-west and single-direction
	// road tiles. is_threeway() is true for a tile with three or four arms,
	// that is, a T junction or a crossroads. Bends are neither.
	const ribi_t::ribi ribi = str->get_ribi_unmasked();
	return ribi_t::is_straight(ribi) || ribi_t::is_threeway(ribi);
}

bool is_junction(const grund_t *gr)
{
	if (gr == NULL)
	{
		return false;
	}
	// A way of the road waytype is always a strasse_t (weg_t::create()).
	const strasse_t *str = static_cast<const strasse_t *>(gr->get_weg(road_wt));
	return str != NULL && !str->is_crossing() && !str->is_diagonal()
		&& ribi_t::is_threeway(str->get_ribi_unmasked());
}

ribi_t::ribi entry_direction(ribi_t::ribi overall, ribi_t::ribi exit)
{
	if (exit != overall && ribi_t::is_single(exit) && (overall & exit) != 0)
	{
		return (ribi_t::ribi)(overall & ~exit);
	}
	return overall;
}

uint32 following_gap(const sint32 follower_speed, const bool leader_moving)
{
	sint64 gap_mm = STANDSTILL_GAP_MM;
	if (leader_moving && follower_speed > 0)
	{
		gap_mm += reaction_gap_mm(follower_speed);
	}
	const sint64 millimetres_per_tile = (sint64)world()->get_settings().get_meters_per_tile() * 1000;
	// Round up: a real gap must never shrink to nothing in vehicle steps.
	return (uint32)((gap_mm * VEHICLE_STEPS_PER_TILE + millimetres_per_tile - 1) / millimetres_per_tile);
}

sint32 safe_following_speed(const sint32 gap_steps, const sint32 leader_speed, const bool leader_moving)
{
	if (gap_steps < 0)
	{
		return SPEED_UNLIMITED;
	}
	// The equilibrium gap is the standstill gap plus the leader's reaction
	// distance. The follower may use the surplus over that gap to close it
	// over one reaction time, so that the speed increases as the gap opens
	// and falls as the follower closes it.
	const sint32 equilibrium_steps = (sint32)following_gap(leader_speed, leader_moving);
	const sint64 millimetres_per_tile = (sint64)world()->get_settings().get_meters_per_tile() * 1000;
	const sint64 surplus_steps = (sint64)gap_steps - (sint64)equilibrium_steps;
	const sint64 surplus_mm = surplus_steps * millimetres_per_tile / VEHICLE_STEPS_PER_TILE;
	// surplus_mm / REACTION_TIME_MS is mm/ms, which is m/s. Convert to km/h
	// and then to internal speed units.
	const sint64 surplus_kmh = (surplus_mm * 3600) / (REACTION_TIME_MS * 1000);
	const sint32 safe_speed = leader_speed + kmh_to_speed((sint32)surplus_kmh);
	return max(0, safe_speed);
}

sint16 advance_lateral_blend(const sint16 current, const sint16 target, const uint32 forward_steps, const uint32 blend_steps)
{
	if (forward_steps == 0 || blend_steps == 0)
	{
		return current;
	}
	// One call can never need to cover more than the full blend distance; this
	// also keeps the multiplication below within 32 bits after a large jump.
	const uint32 capped_steps = min(forward_steps, blend_steps);
	sint32 step = (sint32)((capped_steps * 256u) / blend_steps);
	if (step < 1)
	{
		step = 1;
	}
	if (current < target)
	{
		return (sint16)min((sint32)target, (sint32)current + step);
	}
	if (current > target)
	{
		return (sint16)max((sint32)target, (sint32)current - step);
	}
	return current;
}

/**
 * The length of one vehicle step on a tile, in 512ths of a straight step.
 * Vehicle lengths and following gaps are measured in straight steps, of which
 * a straight tile's travel path is VEHICLE_STEPS_PER_TILE. A diagonal tile's
 * travel path is longer on screen for the same number of steps, by the factor
 * diagonal_multiplier / 512, so a position in a diagonal tile's steps has to
 * be converted before it can be compared with a straight-tile position.
 */
static sint32 step_scale(const uint8 steps_next)
{
	// A zero steps_next is the marker for a vehicle that turns on one tile
	// (see vehicle_t::hop); it has no diagonal path to scale.
	return steps_next == VEHICLE_STEPS_PER_TILE - 1 || steps_next == 0
		? 512
		: (sint32)vehicle_base_t::get_diagonal_multiplier();
}

/// Whether two road users follow the same path through a tile. A user at the
/// end of its route has no exit direction; it is still on the same path as a
/// vehicle which entered by the same edge, so the entry direction alone is
/// compared in that case. This lets a vehicle enter a terminal tile behind a
/// vehicle which is already standing there.
static bool same_path(const user_t &a, const user_t &b)
{
	return a.entry_direction == b.entry_direction
		&& (a.exit_direction == ribi_t::none
			|| b.exit_direction == ribi_t::none
			|| a.exit_direction == b.exit_direction);
}

/// Reads the part of a road vehicle's state that the following rules need.
/// Returns false for objects which are not road vehicles or private cars.
/// A leading convoy vehicle contributes the whole convoy length, because the
/// convoy's rear is that vehicle's front minus the convoy length. A trailing
/// vehicle contributes only its own length, which is the rear of the part of
/// the convoy on its tile.
static bool get_user_state(const vehicle_base_t *v, user_t &out)
{
	const obj_t::typ typ = v->get_typ();
	if (typ == obj_t::road_vehicle)
	{
		const road_vehicle_t *rv = static_cast<const road_vehicle_t *>(v);
		const convoi_t *cnv = rv->get_convoi();
		if (cnv == NULL)
		{
			return false;
		}
		out.object = rv;
		out.convoi = cnv;
		out.exit_direction = rv->get_90direction();
		out.entry_direction = entry_direction(rv->get_direction(), out.exit_direction);
		out.speed = cnv->get_akt_speed();
		out.leading = rv->is_leading();
		out.length = out.leading
			? cnv->get_length_in_steps()
			: rv->get_desc()->get_length_in_steps();
		out.overtaking = cnv->is_overtaking();
		// A vehicle is beside the lane only once its whole body is inside the
		// tile it occupies. A vehicle which has just entered its stop tile still
		// has its rear at the tile boundary and must keep blocking traffic
		// there, even though it is already loading.
		out.pulled_over = rv->is_pulled_over() && (uint32)rv->get_steps() >= out.length;
		return true;
	}
	if (typ == obj_t::road_user)
	{
		const private_car_t *pc = static_cast<const private_car_t *>(v);
		out.object = pc;
		out.convoi = NULL;
		out.exit_direction = pc->get_90direction();
		out.entry_direction = entry_direction(pc->get_direction(), out.exit_direction);
		out.speed = pc->get_current_speed();
		out.leading = true;
		out.length = pc->get_length_in_steps();
		out.overtaking = pc->is_overtaking();
		out.pulled_over = false;
		return true;
	}
	return false;
}

/**
 * The following gap for one candidate inside following_limit(). The gap
 * depends only on the follower's own speed and on whether the leader is
 * moving, so a call needs at most two values; caching them here avoids a
 * conversion for every candidate on the tile.
 */
static sint32 following_gap_cached(const user_t &self, const sint32 other_speed, sint32 &moving_cache, sint32 &stopped_cache)
{
	if (other_speed > 0)
	{
		if (moving_cache < 0)
		{
			moving_cache = (sint32)following_gap(self.speed, true);
		}
		return moving_cache;
	}
	if (stopped_cache < 0)
	{
		stopped_cache = (sint32)following_gap(self.speed, false);
	}
	return stopped_cache;
}


sint32 following_limit(const grund_t *gr, const user_t &self, const uint8 steps, const uint8 steps_next, leader_t *leader)
{
	if (gr == NULL || self.object == NULL)
	{
		return -1;
	}
	sint32 limit = -1;
	sint32 nearest_gap = -1;
	sint32 nearest_speed = 0;
	bool nearest_moving = false;
	convoihandle_t nearest_convoy;
	sint32 gap_moving = -1;
	sint32 gap_stopped = -1;
	const karte_t *const welt = world();
	const koord3d self_pos = self.object->get_pos();
	const koord3d next_pos = self.object->get_pos_next();
	const uint8 occ_here = welt->get_road_occupancy(self_pos.get_2d());
	if (occ_here > 1u)
	{
		ROAD_PREFETCH_T0(gr);
	}
	const bool check_next = next_pos != koord3d::invalid && next_pos != self_pos;
	const uint8 occ_next = check_next ? welt->get_road_occupancy(next_pos.get_2d()) : 0u;
	if (occ_here <= 1u && occ_next == 0u)
	{
		if (leader != NULL)
		{
			leader->gap = -1;
			leader->speed = 0;
			leader->moving = false;
			leader->convoy = convoihandle_t();
		}
		return -1;
	}
	const uint32 way_gen = welt->get_road_way_generation();
	if (occ_here > 1u && occ_next > 0u)
	{
		if (const grund_t *const pf_next = self.object->peek_cached_next_grund(way_gen))
		{
			ROAD_PREFETCH_T0(pf_next);
		}
	}

	// Only touch the current tile's objlist when at least two road users share
	// this (x, y) coordinate AND at least two road users on gr share
	// self.entry_direction (skipping oncoming-only tiles completely).
	if (occ_here > 1u && self.object->get_cached_allows_queueing())
	{
		uint8 remaining = gr->get_road_user_count();
		const bool single_entry = ribi_t::is_single(self.entry_direction);
		const uint8 dir_count = single_entry ? gr->get_road_dir_count(self.entry_direction) : remaining;
		if (remaining > 1u && (!single_entry || dir_count > 1u))
		{
			const uint8 top = gr->get_top();
			const uint8 way_count = gr->get_way_count();
			obj_t *const *const objs = gr->get_objs_array_main_thread();
			if (objs != NULL)
			{
				const bool track_dir = single_entry && dir_count < 15u;
				uint8 dir_remaining = dir_count;
				// objlist_t places static objects before moving objects and sorts
				// moving objects by disp_lane (1 then 3 for road traffic). When
				// the tile has only ways and road users and our lane is disp_lane
				// <= 1, scanning forwards hits our lane first and exits before
				// touching oncoming disp_lane 3 vehicles; otherwise scan backwards
				// to skip static objects at the front of the list.
				const bool forward = (self.object->get_disp_lane() <= 1u)
					&& (top == (uint8)(way_count + remaining));
				const sint32 step = forward ? 1 : -1;
				const sint32 pos_end = forward ? (sint32)top : (sint32)way_count - 1;
				for (sint32 pos = forward ? (sint32)way_count : (sint32)top - 1;
					pos != pos_end && remaining > 0; pos += step)
				{
					obj_t *const obj = objs[pos];
					if (obj == self.object)
					{
						remaining--;
						if (track_dir && --dir_remaining == 0u)
						{
							break;
						}
						continue;
					}
					if (pos + step != pos_end)
					{
						ROAD_PREFETCH_T0(objs[pos + step]);
					}
					const obj_t::typ typ = obj->get_typ();
					if (typ == obj_t::road_user)
					{
						remaining--;
						const private_car_t *const pc = static_cast<const private_car_t *>(obj);
						const ribi_t::ribi odir = pc->get_direction();
						// The entry direction is a subset of the travel direction
						// bits, so reject non-overlapping directions before reading
						// exit direction or length.
						if (self.entry_direction != ribi_t::none && (odir & self.entry_direction) == 0)
						{
							continue;
						}
						if (track_dir)
						{
							dir_remaining--;
						}
						// A vehicle whose front is level with or behind our front is
						// not ahead of us. Check this before computing exit direction.
						const sint32 front = (sint32)pc->get_steps();
						if (front <= (sint32)steps)
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						// A private car is never pulled over.
						if (pc->is_overtaking() != self.overtaking || self.pulled_over)
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const ribi_t::ribi oexit = pc->get_90direction();
						const ribi_t::ribi oentry = ribi_t::is_single(odir) ? odir : entry_direction(odir, oexit);
						if (oentry != self.entry_direction
							|| (oexit != ribi_t::none && self.exit_direction != ribi_t::none && oexit != self.exit_direction))
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const sint32 other_speed = pc->get_current_speed();
						const sint32 rear = front - (sint32)pc->get_length_in_steps();
						const sint32 raw_gap = rear - (sint32)steps;
						if (nearest_gap < 0 || raw_gap < nearest_gap)
						{
							nearest_gap = raw_gap;
							nearest_speed = other_speed;
							nearest_moving = other_speed > 0;
							nearest_convoy = convoihandle_t();
						}
						const sint32 gap = following_gap_cached(self, other_speed, gap_moving, gap_stopped);
						const sint32 candidate = max(0, rear - gap);
						if (limit < 0 || candidate < limit)
						{
							limit = candidate;
						}
						if (track_dir && dir_remaining == 0u) break;
					}
					else if (typ == obj_t::road_vehicle)
					{
						remaining--;
						const road_vehicle_t *const rv = static_cast<const road_vehicle_t *>(obj);
						const ribi_t::ribi odir = rv->get_direction();
						if (self.entry_direction != ribi_t::none && (odir & self.entry_direction) == 0)
						{
							continue;
						}
						if (track_dir)
						{
							dir_remaining--;
						}
						const sint32 front = (sint32)rv->get_steps();
						if (front <= (sint32)steps)
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const convoi_t *const cnv = rv->get_convoi();
						if (cnv == NULL || cnv->is_overtaking() != self.overtaking)
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const ribi_t::ribi oexit = rv->get_90direction();
						const ribi_t::ribi oentry = ribi_t::is_single(odir) ? odir : entry_direction(odir, oexit);
						if (oentry != self.entry_direction
							|| (oexit != ribi_t::none && self.exit_direction != ribi_t::none && oexit != self.exit_direction))
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const uint32 other_len = rv->is_leading()
							? cnv->get_length_in_steps()
							: rv->get_desc()->get_length_in_steps();
						const bool other_pulled_over = rv->is_pulled_over() && (uint32)front >= other_len;
						if (other_pulled_over != self.pulled_over)
						{
							if (track_dir && dir_remaining == 0u) break;
							continue;
						}
						const sint32 other_speed = cnv->get_akt_speed();
						const sint32 rear = front - (sint32)other_len;
						const sint32 raw_gap = rear - (sint32)steps;
						if (nearest_gap < 0 || raw_gap < nearest_gap)
						{
							nearest_gap = raw_gap;
							nearest_speed = other_speed;
							nearest_moving = other_speed > 0;
							nearest_convoy = cnv->self;
						}
						const sint32 gap = following_gap_cached(self, other_speed, gap_moving, gap_stopped);
						const sint32 candidate = max(0, rear - gap);
						if (limit < 0 || candidate < limit)
						{
							limit = candidate;
						}
						if (track_dir && dir_remaining == 0u) break;
					}
				}
			}
		}
	}
	// A vehicle whose front has crossed into the next tile still has its rear on
	// this tile while its front is shorter than its length, and a vehicle wholly
	// on the next tile can still be the nearest leader for the speed rule.
	// Skip the tile lookup completely when road_occupancy reports zero road users
	// at next_pos, and skip the objlist scan when no road user on next shares
	// the entry direction from this tile.
	if (occ_next > 0u)
	{
		const grund_t *const next = self.object->get_cached_next_grund(next_pos, way_gen);
		if (next != NULL)
		{
			uint8 remaining = next->get_road_user_count();
			if (remaining > 0u)
			{
				// The step coordinate of a vehicle on the next tile starts at the
				// edge through which it entered. Only a vehicle which entered from
				// our tile has its steps measured from the same edge as ours.
				const ribi_t::ribi entry = ribi_type(self_pos, next_pos);
				const uint8 dir_count = next->get_road_dir_count(entry);
				if (dir_count > 0u)
				{
					const bool track_dir = ribi_t::is_single(entry) && dir_count < 15u;
					uint8 dir_remaining = dir_count;
					const sint32 our_scale = step_scale(steps_next);
					const uint8 next_top = next->get_top();
					const uint8 next_way_count = next->get_way_count();
					obj_t *const *const objs = next->get_objs_array_main_thread();
					obj_t *const single_obj = objs == NULL ? next->get_single_obj_main_thread() : NULL;
					const bool forward = objs != NULL
						&& (self.object->get_disp_lane() <= 1u)
						&& (next_top == (uint8)(next_way_count + remaining));
					const sint32 step = forward ? 1 : -1;
					const sint32 start_pos = objs != NULL
						? (forward ? (sint32)next_way_count : (sint32)next_top - 1)
						: (single_obj != NULL ? 0 : -1);
					const sint32 pos_end = objs != NULL
						? (forward ? (sint32)next_top : (sint32)next_way_count - 1)
						: -1;
					for (sint32 pos = start_pos; pos != pos_end && remaining > 0; pos += step)
					{
						obj_t *const obj = objs != NULL ? objs[pos] : single_obj;
						if (obj == self.object)
						{
							remaining--;
							continue;
						}
						if (objs != NULL && pos + step != pos_end)
						{
							ROAD_PREFETCH_T0(objs[pos + step]);
						}
						const obj_t::typ typ = obj->get_typ();
						if (typ == obj_t::road_user)
						{
							remaining--;
							const private_car_t *const pc = static_cast<const private_car_t *>(obj);
							const ribi_t::ribi odir = pc->get_direction();
							if ((odir & entry) == 0)
							{
								continue;
							}
							if (track_dir)
							{
								dir_remaining--;
							}
							if (pc->is_overtaking() != self.overtaking)
							{
								if (track_dir && dir_remaining == 0u) break;
								continue;
							}
							// When odir is a single cardinal direction, entry_direction(odir, exit)
							// is always odir regardless of exit, so skip get_90direction().
							const ribi_t::ribi oentry = ribi_t::is_single(odir) ? odir : entry_direction(odir, pc->get_90direction());
							if (oentry != entry)
							{
								if (track_dir && dir_remaining == 0u) break;
								continue;
							}
							const sint32 other_scale = step_scale(pc->get_steps_next());
							const sint32 rear = (sint32)VEHICLE_STEPS_PER_TILE * 512
								+ (sint32)pc->get_steps() * other_scale
								- (sint32)pc->get_length_in_steps() * 512;
							const sint32 other_speed = pc->get_current_speed();
							const sint32 raw_gap = rear / our_scale - (sint32)steps;
							if (nearest_gap < 0 || raw_gap < nearest_gap)
							{
								nearest_gap = raw_gap;
								nearest_speed = other_speed;
								nearest_moving = other_speed > 0;
								nearest_convoy = convoihandle_t();
							}
							if (rear < (sint32)VEHICLE_STEPS_PER_TILE * 512)
							{
								const sint32 gap = following_gap_cached(self, other_speed, gap_moving, gap_stopped);
								const sint32 candidate = max(0, (rear - gap * 512) / our_scale);
								if (limit < 0 || candidate < limit)
								{
									limit = candidate;
								}
							}
							if (track_dir && dir_remaining == 0u) break;
						}
						else if (typ == obj_t::road_vehicle)
						{
							remaining--;
							const road_vehicle_t *const rv = static_cast<const road_vehicle_t *>(obj);
							const ribi_t::ribi odir = rv->get_direction();
							if ((odir & entry) == 0)
							{
								continue;
							}
							if (track_dir)
							{
								dir_remaining--;
							}
							const convoi_t *const cnv = rv->get_convoi();
							if (cnv == NULL || cnv->is_overtaking() != self.overtaking)
							{
								if (track_dir && dir_remaining == 0u) break;
								continue;
							}
							const ribi_t::ribi oentry = ribi_t::is_single(odir) ? odir : entry_direction(odir, rv->get_90direction());
							if (oentry != entry)
							{
								if (track_dir && dir_remaining == 0u) break;
								continue;
							}
							const uint32 other_len = rv->is_leading()
								? cnv->get_length_in_steps()
								: rv->get_desc()->get_length_in_steps();
							const sint32 other_scale = step_scale(rv->get_steps_next());
							const sint32 other_steps = (sint32)rv->get_steps();
							const sint32 rear = (sint32)VEHICLE_STEPS_PER_TILE * 512
								+ other_steps * other_scale
								- (sint32)other_len * 512;
							const bool rear_on_this_tile = rear < (sint32)VEHICLE_STEPS_PER_TILE * 512;
							if (!rear_on_this_tile)
							{
								if (rv->is_pulled_over() && (uint32)other_steps >= other_len)
								{
									if (track_dir && dir_remaining == 0u) break;
									continue;
								}
								const sint32 other_speed = cnv->get_akt_speed();
								const sint32 raw_gap = rear / our_scale - (sint32)steps;
								if (nearest_gap < 0 || raw_gap < nearest_gap)
								{
									nearest_gap = raw_gap;
									nearest_speed = other_speed;
									nearest_moving = other_speed > 0;
									nearest_convoy = cnv->self;
								}
								if (track_dir && dir_remaining == 0u) break;
								continue;
							}
							const sint32 other_speed = cnv->get_akt_speed();
							const sint32 raw_gap = rear / our_scale - (sint32)steps;
							if (nearest_gap < 0 || raw_gap < nearest_gap)
							{
								nearest_gap = raw_gap;
								nearest_speed = other_speed;
								nearest_moving = other_speed > 0;
								nearest_convoy = cnv->self;
							}
							const sint32 gap = following_gap_cached(self, other_speed, gap_moving, gap_stopped);
							const sint32 candidate = max(0, (rear - gap * 512) / our_scale);
							if (limit < 0 || candidate < limit)
							{
								limit = candidate;
							}
							if (track_dir && dir_remaining == 0u) break;
						}
					}
				}
			}
		}
	}
	if (leader != NULL)
	{
		if (nearest_gap < 0)
		{
			leader->gap = -1;
			leader->speed = 0;
			leader->moving = false;
			leader->convoy = convoihandle_t();
		}
		else
		{
			leader->gap = max(0, nearest_gap);
			leader->speed = nearest_speed;
			leader->moving = nearest_moving;
			leader->convoy = nearest_convoy;
		}
	}
	return limit;
}

bool can_follow(const grund_t *gr, const user_t &self, const vehicle_base_t *leader)
{
	if (!allows_queueing(gr))
	{
		return false;
	}
	user_t other;
	// A trailing vehicle of a convoy only knows its own length, which is not
	// enough to prove that the whole convoy's rear is clear. Only a private car
	// or the leading vehicle of a convoy, whose front and convoy length give the
	// convoy's rear, may clear the blocker.
	if (!get_user_state(leader, other)
		|| !other.leading
		|| !same_path(other, self)
		|| other.overtaking != self.overtaking
		|| other.pulled_over != self.pulled_over)
	{
		return false;
	}
	// The follower's front enters the tile at position 0.
	const sint32 rear = (sint32)leader->get_steps() - (sint32)other.length;
	return rear > (sint32)following_gap(self.speed, other.speed > 0);
}

bool junction_exit_clear(const grund_t *exit, const user_t &self, ribi_t::ribi exit_direction)
{
	if (exit == NULL || self.object == NULL || exit->get_pos() == self.object->get_pos())
	{
		// The route ends on the junction, or the vehicle turns on the same tile.
		return true;
	}
	// A way of the road waytype is always a strasse_t (weg_t::create()).
	const strasse_t *str = static_cast<const strasse_t *>(exit->get_weg(road_wt));
	if (str == NULL)
	{
		// No road: the ordinary entry check reports the missing way.
		return true;
	}
	// How much of the exit tile the vehicle's rear needs in order to be clear of
	// the junction when it stops behind the queue. A vehicle longer than one
	// tile cannot clear a one-tile junction, so cap the requirement at one
	// tile's length; such a vehicle must be allowed to pass through.
	const uint32 standstill_gap = following_gap(0, false);
	const sint32 required = (sint32)min(self.length + standstill_gap, (uint32)VEHICLE_STEPS_PER_TILE);
	for (uint8 pos = 0; pos < exit->get_top(); pos++)
	{
		vehicle_base_t *const v = obj_cast<vehicle_base_t>(exit->obj_bei(pos));
		if (v == NULL || v == self.object)
		{
			continue;
		}
		user_t other;
		if (!get_user_state(v, other))
		{
			if (v->get_typ() != obj_t::pedestrian && v->get_waytype() == road_wt)
			{
				// A road user that is not a road vehicle or private car (for
				// example a moving object) blocks the way.
				return false;
			}
			continue;
		}
		if (other.pulled_over || other.overtaking != self.overtaking)
		{
			continue;
		}
		if (other.entry_direction == exit_direction)
		{
			// A vehicle on the same path as the one entering the junction. Its
			// rear must be far enough along the exit tile that the entering
			// vehicle can stop behind it without its own rear remaining in the
			// junction. Convert its front to straight steps first, because a
			// vehicle turning on a cardinal junction uses the diagonal step
			// count even though the way itself is not flagged diagonal.
			const sint32 other_scale = step_scale(v->get_steps_next());
			const sint32 front = (sint32)v->get_steps() * other_scale / 512;
			if (front - (sint32)other.length < required)
			{
				return false;
			}
		}
		else if (other.entry_direction != ribi_t::backward(exit_direction))
		{
			// A vehicle which entered the exit tile from another arm could
			// conflict with the path through the junction. Oncoming traffic is
			// in the other carriageway and does not block.
			return false;
		}
	}
	return true;
}

bool is_beside_lane(const vehicle_base_t *v)
{
	user_t state;
	return get_user_state(v, state) && state.pulled_over;
}

uint32 drive_until_limit(vehicle_base_t *v, const user_t &self, const uint8 steps, const uint8 steps_next, const uint32 distance, leader_t *leader)
{
	const sint32 limit = following_limit(v->get_grund(), self, steps, steps_next, leader);
	// At most one tile boundary per call, so that the limit is recomputed on
	// the new tile before any further distance is added.
	const sint32 tile_end = (sint32)steps_next + 1;
	const sint32 allowed = (limit < 0 ? tile_end : min(limit, tile_end)) - (sint32)steps;
	if (allowed <= 0)
	{
		return 0;
	}
	uint32 chunk = (uint32)allowed << YARDS_PER_VEHICLE_STEP_SHIFT;
	if (chunk > distance)
	{
		chunk = distance;
	}
	return v->vehicle_base_t::do_drive(chunk);
}

} // namespace road_space


uint32 road_vehicle_t::get_max_speed() { return cnv->get_min_top_speed(); }

road_vehicle_t::road_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle, pos, desc, player),
#else
    vehicle_t(pos, desc, player),
#endif
    stand_mask(0),
    pull_in_factor(0),
    pull_in_lateral(255),
    overtake_level(0),
    lateral_distance(0),
    lateral_last_distance(0)
{
	cnv = cn;
	is_checker = false;
	drives_on_left = welt->get_settings().is_drive_left();
	reset_measurements();
}

road_vehicle_t::road_vehicle_t() :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle),
#else
    vehicle_t(),
#endif
    stand_mask(0),
    pull_in_factor(0),
    pull_in_lateral(255),
    overtake_level(0),
    lateral_distance(0),
    lateral_last_distance(0)
{
	// This is blank - just used for the automatic road finder.
	cnv = NULL;
	is_checker = true;
}

road_vehicle_t::road_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::road_vehicle),
#else
    vehicle_t(),
#endif
    stand_mask(0),
    pull_in_factor(0),
    pull_in_lateral(255),
    overtake_level(0),
    lateral_distance(0),
    lateral_last_distance(0)
{
	rdwr_from_convoi(file);

	if(  file->is_loading()  ) {
		static const vehicle_desc_t *last_desc = NULL;

		if(is_leading) {
			last_desc = NULL;
		}
		// try to find a matching vehicle
		if(desc==NULL)
		{
			bool empty = true;
			const goods_desc_t* gd = NULL;
			ware_t ware;
			for (uint8 i = 0; i < number_of_classes; i++)
			{
				if (!fracht[i].empty())
				{
					ware = fracht[i].front();
					gd = ware.get_desc();
					empty = false;
					break;
				}
			}
			if (!gd)
			{
				// Important: This must be the same default ware as used in rdwr_from_convoi.
				gd = goods_manager_t::passengers;
			}

			dbg->warning("road_vehicle_t::road_vehicle_t()","try to find a fitting vehicle for %s.", gd->get_name() );
			desc = vehicle_builder_t::get_best_matching(road_wt, 0, (empty ? 0 : 50), is_leading?50:0, speed_to_kmh(speed_limit), gd, true, last_desc, is_last );
			if(desc) {
				DBG_MESSAGE("road_vehicle_t::road_vehicle_t()","replaced by %s",desc->get_name());
				// still wrong load ...
				calc_image();
			}
			if(!empty  &&  fracht[0].front().menge == 0) {
				// this was only there to find a matching vehicle
				fracht[0].remove_first();
			}
		}
		if(  desc  ) {
			last_desc = desc;
		}
	}
	fix_class_accommodations();
	is_checker = false;
	drives_on_left = welt->get_settings().is_drive_left();
	reset_measurements();
}


void road_vehicle_t::rotate90()
{
	vehicle_t::rotate90();
	calc_disp_lane();
}


void road_vehicle_t::calc_disp_lane()
{
	// driving in the back or the front
	ribi_t::ribi test_dir = welt->get_settings().is_drive_left() ? ribi_t::northeast : ribi_t::southwest;
	disp_lane = get_direction() & test_dir ? 1 : 3;
}


// need to reset halt reservation (if there was one)
// Main thread only: halt reservation slots are read by the main thread
// without synchronisation while convoy worker threads run route finding.
void road_vehicle_t::release_target_reservations()
{
	// The halt reservation is released when the next route is prepared, before
	// the convoy has actually left the stop. Keep the stand mask while the
	// convoy is still on the halt tile, so that it stays drawn at its stand and
	// the stand stays taken until the convoy leaves the tile.
	const bool keep_stand_mask = leading && stand_mask != 0 && target_halt.is_bound()
		&& get_grund() != NULL && get_grund()->get_halt() == target_halt;
	const uint8 mask = stand_mask;
	if(leading   &&  previous_direction!=ribi_t::none  &&  cnv  &&  target_halt.is_bound() ) {
		// now reserve our choice (beware: might be longer than one tile!)
		for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
			target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
		}
	}
	if (keep_stand_mask)
	{
		stand_mask = mask;
	}
	target_halt = halthandle_t(); // no block reserved
}


route_t::route_result_t road_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route)
{
	assert(cnv);
	// The target halt reservation must already have been released on the main
	// thread (convoi_t::prepare_for_routing/finish_rd or the caller of this
	// function): this may run on a convoy worker thread, which must not write
	// reservation state.
	drives_on_left = welt->get_settings().is_drive_left();	// reset driving settings
	const uint32 routing_weight = cnv != NULL ? cnv->get_highest_axle_load() : ((get_sum_weight() + 499) / 1000);
	route_t::route_result_t r = route->calc_route(welt, start, ziel, this, max_speed, routing_weight, is_tall, cnv->get_tile_length(), SINT64_MAX_VALUE, cnv->get_weight_summary().weight / 1000 );
	if(  r == route_t::valid_route_halt_too_short  ) {
		// The message system is main-thread only: defer it to convoi_t::step().
		cnv->defer_message(convoi_t::defer_halt_too_short, ziel);
	}
	return r;
}


bool road_vehicle_t::check_next_tile(const grund_t *bd) const
{
	strasse_t *str=(strasse_t *)bd->get_weg(road_wt);
	if(str==NULL || ((str->get_max_speed()==0 && speed_limit < INT_MAX)))
	{
		return false;
	}
	if(!is_checker)
	{
		bool electric = cnv != NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric;
		if(electric  &&  !str->is_electrified()) {
			return false;
		}
	}
	// check for signs
	if(str->has_sign()) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(rs!=NULL) {
			const roadsign_desc_t* rs_desc = rs->get_desc();
			if(get_desc() && rs_desc->get_min_speed()>0  &&  rs_desc->get_min_speed() > kmh_to_speed(get_desc()->get_topspeed())  )
			{
				return false;
			}
			if(  rs_desc->is_private_way()  &&  (rs->get_player_mask() & (1<<get_owner_nr()) ) == 0  ) {
				// private road
				return false;
			}
			// do not search further for a free stop beyond here
			if(target_halt.is_bound() && cnv && cnv->is_waiting() && (rs_desc->get_flags() & roadsign_desc_t::END_OF_CHOOSE_AREA)) {
				return false;
			}
		}
	}
	if(!is_checker)
	{
		return check_access(str) && check_way_constraints(*str);
	}
	return check_access(str);
}



// how expensive to go here (for way search)
int road_vehicle_t::get_cost(const grund_t *gr, const sint32 max_speed, ribi_t::ribi from)
{
	// first favor faster ways
	const weg_t *w=gr->get_weg(road_wt);
	if(!w) {
		return 0xFFFF;
	}

	// max_speed?
	bool electric = cnv != NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric;
	sint32 max_tile_speed = w->get_max_speed(electric);

	// add cost for going (with maximum speed, cost is 1)
	sint32 costs = (max_speed <= max_tile_speed) ? 10 : 40 - (30 * max_tile_speed) / max_speed;

	if (desc->get_override_way_speed())
	{
		costs = 1;
	}

	// Take traffic congestion into account in determining the cost. Use the same formula as for private cars.
	const uint32 congestion_percentage = w->get_congestion_percentage();
	if (congestion_percentage)
	{
		costs += (costs * congestion_percentage) / 200;
	}

	// effect of slope
	if(  gr->get_weg_hang()!=0  ) {
		// check if the slope is upwards, relative to the previous tile
		// 75 hardcoded, see get_cost_upslope()
		costs += 75 * get_sloping_upwards( gr->get_weg_hang(), from );
	}

	// It is now difficult to calculate here whether the vehicle is overweight, so do this in the route finder instead.

	if(w->is_diagonal())
	{
		// Diagonals are a *shorter* distance.
		costs = (costs * 5) / 7; // was: costs /= 1.4
	}
	return costs;
}


// this routine is called by find_route, to determined if we reached a destination
bool road_vehicle_t:: is_target(const grund_t *gr, const grund_t *prev_gr)
{
	//  just check, if we reached a free stop position of this halt
	if(gr->is_halt()  &&  gr->get_halt()==target_halt  &&  target_halt->get_empty_lane(gr,cnv->self)!=0) {
		// now we must check the predecessor => try to advance as much as possible
		if(prev_gr!=NULL) {
			const koord3d dir=gr->get_pos()-prev_gr->get_pos();
			ribi_t::ribi ribi = ribi_type(dir);
			if(  gr->get_weg(get_waytype())->get_ribi_maske() & ribi  ) {
				// one way sign wrong direction
				return false;
			}
			grund_t *to;
			if(  !gr->get_neighbour(to,road_wt,ribi)  ||  !(to->get_halt()==target_halt)  ||  (gr->get_weg(get_waytype())->get_ribi_maske() & ribi_type(dir))!=0  ||  target_halt->get_empty_lane(to,cnv->self)==0  ) {
				// end of stop: Is it long enough?
				uint16 tiles = cnv->get_tile_length();
				uint8 empty_lane = 3;
				while(  tiles>1  ) {
					if(  !gr->get_neighbour(to,get_waytype(),ribi_t::backward(ribi))  ||  !(to->get_halt()==target_halt)  ||  (empty_lane &= target_halt->get_empty_lane(to,cnv->self))==0  ) {
						return false;
					}
					gr = to;
					tiles --;
				}
				return true;
			}
			// can advance more
			return false;
		}
//DBG_MESSAGE("is_target()","success at %i,%i",gr->get_pos().x,gr->get_pos().y);
//		return true;
	}
	return false;
}


// to make smaller steps than the tile granularity, we have to use this trick
void road_vehicle_t::get_screen_offset( int &xoff, int &yoff, const sint16 raster_width, bool prev_based ) const
{
	(void)prev_based;
	vehicle_base_t::get_screen_offset( xoff, yoff, raster_width );

	// Every vehicle of a road convoy uses the leading vehicle's lateral state,
	// because the stand and the convoy's lane are recorded on the leading
	// vehicle.
	const road_vehicle_t *source = this;
	if (cnv != NULL)
	{
		const road_vehicle_t *front = obj_cast<road_vehicle_t>(cnv->front());
		if (front != NULL)
		{
			source = front;
		}
	}

	const uint16 factor = source->pull_in_factor;
	if (source->stand_mask != 0 || factor > 0)
	{
		// Draw the vehicle at the blend between the travel lane and its stand.
		// The lateral position of a held stand is read live; after the stand is
		// released, the last position is kept while the vehicle glides back.
		const uint8 lateral = source->stand_mask != 0
			? source->get_stand_lateral_position()
			: source->pull_in_lateral;
		const uint8 dir = ribi_t::get_dir(get_direction());
		if ((lateral & 2) != 0)
		{
			xoff += (tile_raster_scale_x(overtaking_base_offsets[dir][0], raster_width) * factor) / PULL_IN_FACTOR_ONE;
			yoff += (tile_raster_scale_x(overtaking_base_offsets[dir][1], raster_width) * factor) / PULL_IN_FACTOR_ONE;
		}
		if ((lateral & 1) != 0 && factor > 0)
		{
			add_pulled_over_offset(xoff, yoff, raster_width, factor);
		}
		return;
	}

	// eventually shift position to take care of overtaking
	const sint16 overtake = source->overtake_level;
	if (overtake != 0)
	{
		const uint8 dir = ribi_t::get_dir(get_direction());
		xoff += (tile_raster_scale_x(overtaking_base_offsets[dir][0], raster_width) * overtake) / PULL_IN_FACTOR_ONE;
		yoff += (tile_raster_scale_x(overtaking_base_offsets[dir][1], raster_width) * overtake) / PULL_IN_FACTOR_ONE;
	}
}


// chooses a route at a choose sign; returns true on success
bool road_vehicle_t::choose_route(sint32 &restart_speed, ribi_t::ribi start_direction, uint16 index)
{
	// are we heading to a target?
	route_t *rt = cnv->access_route();
	target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
	if(  target_halt.is_bound()  ) {

		// since convois can long than one tile, check is more difficult
		bool can_go_there = true;
		bool original_route = (rt->back() == cnv->get_schedule()->get_current_entry().pos);
		for(  uint32 length=0;  can_go_there  &&  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
			if(  grund_t *gr = welt->lookup( rt->at( rt->get_count()-length-1) )  ) {
				if (gr->get_halt().is_bound()) {
					can_go_there &= target_halt->is_reservable( gr, cnv->self );
				}
				else {
					// if this is the original stop, it is too short!
					can_go_there |= original_route;
				}
			}
		}
		if(  can_go_there  ) {
			// then reserve it ...
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<rt->get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( rt->at( rt->get_count()-length-1) ), cnv->self );
			}
		}
		else {
			// cannot go there => need slot search

			// if we fail, we will wait in a step, much more simulation friendly
			if(!cnv->is_waiting()) {
				restart_speed = -1;
				target_halt = halthandle_t();
				return false;
			}

			// check if there is a free position
			// this is much faster than waysearch
			if(  !target_halt->find_free_position(road_wt,cnv->self,obj_t::road_vehicle  )) {
				restart_speed = 0;
				target_halt = halthandle_t();
				return false;
			}

			// now it make sense to search a route
			route_t target_rt;
			koord3d next3d = rt->at(index);
			if(  !target_rt.find_route(welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), start_direction, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, 33, cnv->has_tall_vehicles())  ) {
				// nothing empty or not route with less than 33 tiles
				target_halt = halthandle_t();
				restart_speed = 0;
				return false;
			}

			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<target_rt.get_count();  length++  ) {
				target_halt->reserve_position( welt->lookup( target_rt.at( target_rt.get_count()-length-1) ), cnv->self );
			}
			rt->remove_koord_from( index );
			rt->append( &target_rt );
		}
	}
	return true;
}


bool road_vehicle_t::is_pulled_over() const
{
	if (cnv == NULL)
	{
		return false;
	}
	// The stand is recorded on the leading vehicle; every vehicle of the
	// convoy is drawn beside the travel lane while the convoy is at its stand.
	const road_vehicle_t *front = obj_cast<road_vehicle_t>(cnv->front());
	if (front == NULL || front->stand_mask == 0)
	{
		return false;
	}
	// Stand 0 of a terminal is the normal travel position, not a pulled-over
	// one: a vehicle there still blocks the lane.
	if (get_stand_lateral_position() == 0)
	{
		return false;
	}
	// A convoy which is loading or laid over is beside the lane. A convoy which
	// has stopped at its stand is also beside it: the leading vehicle is at its
	// tile limit and the convoy is stationary. That covers the wait for a
	// reversal or for a free route after loading has ended.
	const bool stopped_at_stand = front->steps >= front->steps_next
		&& cnv->get_akt_speed() == 0;
	return cnv->is_loading() || cnv->get_state() == convoi_t::LAYOVER || stopped_at_stand;
}


uint8 road_vehicle_t::get_stand_lateral_position() const
{
	if (cnv == NULL)
	{
		return 0;
	}
	const road_vehicle_t *front = obj_cast<road_vehicle_t>(cnv->front());
	if (front == NULL || front->stand_mask == 0)
	{
		return 0;
	}
	uint8 stand = 0;
	for (uint8 s = 0; s < 4; s++)
	{
		if ((front->stand_mask & (1 << s)) != 0)
		{
			stand = s;
			break;
		}
	}
	// A terminal's four stands are its four lateral positions. A through
	// stop's stand is a side of the road, and a vehicle loading there is
	// pulled over in its traffic lane.
	const grund_t *gr = get_grund();
	const halthandle_t halt = gr != NULL ? gr->get_halt() : halthandle_t();
	if (halt.is_bound() && halt->get_stand_count(gr) >= 4)
	{
		return stand;
	}
	return 1;
}


void road_vehicle_t::update_lateral_blend() const
{
	const bool first_update = pull_in_lateral == 255;
	const bool has_stand = stand_mask != 0;

	// A through stop's lateral position is always the traffic lane's pulled-over
	// position, so the blend can start on the approach. A terminal's stand
	// position is not known until the stand is reserved, so it starts then.
	bool approaching = false;
	if (!has_stand && cnv != NULL && target_halt.is_bound()
		&& (sint32)steps_next - (sint32)steps < (sint32)PULL_IN_APPROACH_STEPS)
	{
		const grund_t *next = welt->lookup(pos_next);
		if (next != NULL && next->get_halt() == target_halt
			&& target_halt->get_stand_count(next) < 4)
		{
			approaching = true;
		}
	}

	if (has_stand)
	{
		pull_in_lateral = get_stand_lateral_position();
	}
	else if (approaching)
	{
		pull_in_lateral = 1;
	}

	const bool pull_in = has_stand || approaching;

	// Pulling in at a stop takes precedence over the overtaking lane offset.
	sint16 overtake_target = 0;
	if (!pull_in && cnv != NULL)
	{
		if (cnv->is_overtaking())
		{
			overtake_target = (sint16)PULL_IN_FACTOR_ONE;
		}
		else if (cnv->is_overtaken())
		{
			overtake_target = road_space::OVERTAKEN_LATERAL_LEVEL;
		}
	}

	if (first_update)
	{
		// First update after creating or loading this vehicle. A vehicle that
		// already holds a stand is drawn at the stand immediately; otherwise it
		// starts in the travel lane.
		lateral_last_distance = lateral_distance;
		pull_in_factor = pull_in ? PULL_IN_FACTOR_ONE : 0;
		if (!pull_in)
		{
			pull_in_lateral = 0;
		}
		overtake_level = overtake_target;
		return;
	}

	const uint32 forward_yards = lateral_distance - lateral_last_distance;
	lateral_last_distance = lateral_distance;
	const uint32 forward_steps = forward_yards >> YARDS_PER_VEHICLE_STEP_SHIFT;
	if (forward_steps == 0)
	{
		// No forward movement, so no lateral movement: the vehicle must not
		// appear to slide sideways while it is standing.
		return;
	}

	const uint32 pull_in_blend_steps = (pull_in_lateral & 2) != 0
		? road_space::LANE_CHANGE_BLEND_STEPS
		: PULL_IN_BLEND_STEPS;
	pull_in_factor = (uint16)road_space::advance_lateral_blend(
		(sint16)pull_in_factor,
		pull_in ? (sint16)PULL_IN_FACTOR_ONE : 0,
		forward_steps,
		pull_in_blend_steps);
	overtake_level = road_space::advance_lateral_blend(
		overtake_level,
		overtake_target,
		forward_steps,
		road_space::LANE_CHANGE_BLEND_STEPS);

	if (pull_in_factor == 0 && !pull_in)
	{
		pull_in_lateral = 0;
	}
}


void road_vehicle_t::rdwr_from_convoi(loadsave_t *file)
{
	vehicle_t::rdwr_from_convoi(file);
	if (file->is_version_ex_atleast(15, 20))
	{
		uint8 mask = stand_mask;
		file->rdwr_byte(mask);
		if (file->is_loading())
		{
			stand_mask = mask & 0x0F;
			// A vehicle loaded at a stand is already pulled over; do not draw it
			// gliding in from the travel lane after the load.
			if (stand_mask != 0)
			{
				pull_in_factor = PULL_IN_FACTOR_ONE;
			}
		}
	}
	else if (file->is_loading())
	{
		stand_mask = 0;
	}
}


bool road_vehicle_t::apply_following_rule(const grund_t *gr, vehicle_base_t *&blocker, sint8 lane) const
{
	if (!leading || cnv == NULL)
	{
		return true;
	}
	road_space::user_t self;
	self.object = this;
	self.convoi = cnv;
	self.entry_direction = ribi_type(get_pos(), gr->get_pos());
	route_t const& r = *cnv->get_route();
	const bool has_exit = route_index < r.get_count() - 1u && r.at(route_index + 1u) != gr->get_pos();
	self.exit_direction = has_exit
		? ribi_type(gr->get_pos(), r.at(route_index + 1u))
		: self.entry_direction;
	self.speed = cnv->get_akt_speed();
	self.length = cnv->get_length_in_steps();
	// next_lane is 1 when the vehicle enters the passing lane, -1 when it
	// enters the traffic lane and 0 when the lane is unchanged.
	self.overtaking = lane > 0 ? true : (lane < 0 ? false : cnv->is_overtaking());
	self.pulled_over = false;
	if (blocker != NULL)
	{
		// A vehicle loading at a stop stand is beside the travel lane, so it
		// does not block a vehicle in the travel lane even though both are on
		// the tile. A vehicle ahead on the same path is a blocker only when it
		// is still too close.
		if (road_space::is_beside_lane(blocker) || road_space::can_follow(gr, self, blocker))
		{
			blocker = NULL;
		}
	}
	if (road_space::is_junction(gr))
	{
		const grund_t *exit = has_exit ? welt->lookup(r.at(route_index + 1u)) : NULL;
		return road_space::junction_exit_clear(exit, self, self.exit_direction);
	}
	return true;
}


bool road_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8 second_check_count)
{
	// check for traffic lights (only relevant for the first car in a convoi)
	assert(cnv->get_state() != convoi_t::ROUTING_1 && cnv->get_state() != convoi_t::ROUTING_2);
	if(  leading  ) {
		// no further check, when already entered a crossing (to allow leaving it)
		if(  !second_check_count  ) {
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  gr_current  &&  gr_current->ist_uebergang()  ) {
					return true;
				}
			}
			// always allow to leave traffic lights (avoid vehicles stuck on crossings directly after though)
			if(  const grund_t *gr_current = welt->lookup(get_pos())  ) {
				if(  const roadsign_t *rs = gr_current->find<roadsign_t>()  ) {
					if(  rs  &&  rs->get_desc()->is_traffic_light()  &&  !gr->ist_uebergang()  ) {
						return true;
					}
				}
			}
		}

		assert(gr);

		// Keep the immediate target tile: the following rule must be evaluated
		// against it even after the crossing loop has moved gr forward.
		const grund_t *const target_gr = gr;

		const strasse_t *str = (strasse_t *)gr->get_weg(road_wt);
		if(  !str  ||  gr->get_top() > 250  ) {
			// too many cars here or no street
			if(  !second_check_count  &&  !str) {
				cnv->suche_neue_route();
			}
			return false;
		}

		if (gr->is_height_restricted() && cnv->has_tall_vehicles())
		{
			cnv->suche_neue_route();
			return false;
		}

		route_t const& r = *cnv->get_route();
		const bool route_index_beyond_end_of_route = route_index >= r.get_count();

		// first: check roadsigns
		const roadsign_t *rs = NULL;
		if(  str->has_sign()  ) {
			rs = gr->find<roadsign_t>();

			if (rs && (!route_index_beyond_end_of_route)) {
				// since at the corner, our direction may be diagonal, we make it straight
				uint8 direction90 = ribi_type(get_pos(), pos_next);

				if (rs->get_desc()->is_traffic_light() && (rs->get_dir()&direction90) == 0) {
					// wait here
					restart_speed = 16;
					return false;
				}
				// check, if we reached a choose point
				else if(route_index + 1u < r.get_count())
				{
					// route position after road sign
					const koord3d pos_next_next = r.at(route_index + 1u);
					// since at the corner, our direction may be diagonal, we make it straight
					direction90 = ribi_type( pos_next, pos_next_next );

					if(  rs->is_free_route(direction90)  &&  !target_halt.is_bound()  ) {
						if(  second_check_count  ) {
							return false;
						}
						if(  !choose_route( restart_speed, direction90, route_index )  ) {
							return false;
						}
					}
				}
			}
		}

		// At a crossing, decide whether the convoi should go on passing lane.
		// side road -> main road from passing lane side: vehicle should enter passing lane on main road.
		next_lane = 0;
		if(  (str->get_ribi_unmasked() == ribi_t::all  ||  ribi_t::is_threeway(str->get_ribi_unmasked()))  &&  str->get_overtaking_mode() <= oneway_mode  ) {
			const strasse_t* str_prev = route_index == 0 ? NULL : (strasse_t *)welt->lookup(r.at(route_index - 1u))->get_weg(road_wt);
			const grund_t* gr_next = route_index < r.get_count() - 1u ? welt->lookup(r.at(route_index + 1u)) : NULL;
			const strasse_t* str_next = gr_next ? (strasse_t*)gr_next -> get_weg(road_wt) : NULL;
			if(str_prev && str_next && str_prev->get_overtaking_mode() > oneway_mode  && str_next->get_overtaking_mode() <= oneway_mode) {
				const koord3d pos_next2 = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
				if(  (!welt->get_settings().is_drive_left()  &&  ribi_t::rotate90l(get_90direction()) == calc_direction(pos_next,pos_next2))  ||  (welt->get_settings().is_drive_left()  &&  ribi_t::rotate90(get_90direction()) == calc_direction(pos_next,pos_next2))  ) {
					// next: enter passing lane.
					next_lane = 1;
				}
			}
		}

		const strasse_t* current_str = (strasse_t*)(welt->lookup(get_pos())->get_weg(road_wt));
		if( current_str && current_str->get_overtaking_mode()<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  ) {
			next_lane = -1;
		}

		vehicle_base_t *obj = NULL;
		uint32 test_index = route_index;

		// way should be clear for overtaking: we checked previously
		// calculate new direction
		koord3d next = (route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next);
		ribi_t::ribi curr_direction   = get_direction();
		ribi_t::ribi curr_90direction = calc_direction(get_pos(), pos_next);
		ribi_t::ribi next_direction   = calc_direction(get_pos(), next);
		ribi_t::ribi next_90direction = calc_direction(pos_next, next);
		obj = get_blocking_vehicle(gr, cnv, curr_direction, next_direction, next_90direction, NULL, next_lane);

		//If this convoi is overtaking, the convoi must avoid a head-on crash.
		if(  cnv->is_overtaking()  ){
			while(  test_index < route_index + 2u && test_index < r.get_count()  ){
				grund_t *grn = welt->lookup(r.at(test_index));
				if(  !grn  ) {
					cnv->suche_neue_route();
					return false;
				}
				for(  uint8 pos=1;  pos < grn->get_top();  pos++  ){
					if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(grn->obj_bei(pos))  ){
						if(  v->get_typ()==obj_t::pedestrian  ) {
							continue;
						}
						ribi_t::ribi other_direction=255;
						if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
							//ignore ourself
							if(  cnv == at->get_convoi()  ||  at->get_convoi()->is_overtaking()  ){
								continue;
							}
							other_direction = at->get_90direction();
						}
						//check for city car
						else if(  private_car_t* const caut = obj_cast<private_car_t>(v)  ) {
							if(  caut->is_overtaking()  ) {
								continue;
							}
							other_direction = v->get_90direction();
						}
						if(  other_direction != 255  ){
							//There is another car. We have to check if this convoi is facing or not.
							ribi_t::ribi this_direction = 0;
							if(  test_index-route_index==0  ) this_direction = get_90direction();
							if(  test_index-route_index==1  ) this_direction = get_next_90direction();
							if(  ribi_t::reverse_single(this_direction) == other_direction  ) {
								//printf("%s: crash avoid. (%d,%d)\n", cnv->get_name(), get_pos().x, get_pos().y);
								cnv->set_tiles_overtaking(0);
							}
						}
					}
				}
				test_index++;
			}
		}

		test_index = route_index + 1u; //reset test_index
		// we have to assume the lane that this vehicle goes in the intersection.
		sint8 lane_of_the_tile = next_lane;
		overtaking_mode_t mode_of_start_point = str->get_overtaking_mode();
		// check exit from crossings and intersections, allow to proceed after 4 consecutive
		while(  !obj   &&  str->is_crossing()  &&  test_index < r.get_count()  &&  test_index < route_index + 4u  ) {
			if(  str->is_crossing()  ) {
				crossing_t* cr = gr->find<crossing_t>(2);
				if(  !cr->request_crossing(this)  ) {
					restart_speed = 0;
					return false;
				}
			}

			// test next position
			gr = welt->lookup(r.at(test_index));
			if(  !gr  ) {
				// ground not existent (probably destroyed)
				if(  !second_check_count  ) {
					cnv->suche_neue_route();
				}
				return false;
			}

			str = (strasse_t *)gr->get_weg(road_wt);

			if(  !str  ) {
				// road not existent (probably destroyed)
				if(  !second_check_count  ) {
					cnv->suche_neue_route();
				}
				return false;
			}

			if(  gr->get_top() > 250  ) {
				// too many cars here or no street
				return false;
			}

			if(  mode_of_start_point<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  ) lane_of_the_tile = -1;

			// check cars
			curr_direction   = next_direction;
			curr_90direction = next_90direction;
			if(  test_index + 1u < r.get_count()  ) {
				next                 = r.at(test_index + 1u);
				next_direction   = calc_direction(r.at(test_index - 1u), next);
				next_90direction = calc_direction(r.at(test_index),      next);
				obj = get_blocking_vehicle(gr, cnv, curr_direction, next_direction, next_90direction, NULL, lane_of_the_tile);
			}
			else {
				next                 = r.at(test_index);
				next_90direction = calc_direction(r.at(test_index - 1u), next);
				if(  curr_direction == next_90direction  ||  !gr->is_halt()  ) {
					// check cars but allow to enter intersection if we are turning even when a car is blocking the halt on the last tile of our route
					// preserves old bus terminal behaviour
					obj = get_blocking_vehicle(gr, cnv, curr_direction, next_90direction, ribi_t::none, NULL, lane_of_the_tile);
				}
			}

			// first: check roadsigns
			const roadsign_t *rs = NULL;
			if (str->has_sign()) {
				rs = gr->find<roadsign_t>();
				const ribi_t::ribi dir = rs->get_dir();
				const bool is_traffic_light = rs->get_desc()->is_traffic_light();
				route_t const& r = *cnv->get_route();

				if (is_traffic_light || gr->get_weg(get_waytype())->get_ribi_maske() & dir)
				{
					uint8 direction90 = ribi_type(get_pos(), pos_next);
					if (rs && (!route_index_beyond_end_of_route)) {
						// Check whether if we reached a choose point
						if (rs->get_desc()->is_choose_sign() && !cnv->get_schedule()->get_current_entry().is_flag_set(schedule_entry_t::ignore_choose))
						{
							// route position after road sign
							const koord3d pos_next_next = r.at(route_index + 1u);
							// since at the corner, our direction may be diagonal, we make it straight
							direction90 = ribi_type(pos_next, pos_next_next);

							if (  rs->is_free_route(direction90) && !target_halt.is_bound()  ) {
								if (  second_check_count  ) {
									return false;
								}
								if (!choose_route(restart_speed, direction90, route_index)) {
									return false;
								}
							}

							const route_t *rt = cnv->get_route();
							// is our target occupied?
							target_halt = haltestelle_t::get_halt(rt->back(), get_owner());
							if (target_halt.is_bound()) {
								// since convois can long than one tile, check is more difficult
								bool can_go_there = true;
								for (uint32 length = 0; can_go_there && length < cnv->get_tile_length() && length + 1 < rt->get_count(); length++) {
									can_go_there &= target_halt->is_reservable(welt->lookup(rt->at(rt->get_count() - length - 1)), cnv->self);
								}
								if (can_go_there) {
									// then reserve it ...
									for (uint32 length = 0; length < cnv->get_tile_length() && length + 1 < rt->get_count(); length++) {
										target_halt->reserve_position(welt->lookup(rt->at(rt->get_count() - length - 1)), cnv->self);
									}
								}
								else {
									// cannot go there => need slot search

									// if we fail, we will wait in a step, much more simulation friendly
									if (!cnv->is_waiting()) {
										restart_speed = -1;
										target_halt = halthandle_t();
										return false;
									}

									// check if there is a free position
									// this is much faster than waysearch
									if (!target_halt->find_free_position(road_wt, cnv->self, obj_t::road_vehicle)) {
										restart_speed = 0;
										target_halt = halthandle_t();
										//DBG_MESSAGE("road_vehicle_t::can_enter_tile()","cnv=%d nothing free found!",cnv->self.get_id());
										return false;
									}

									// now it make sense to search a route
									route_t target_rt;
									koord3d next3d = r.at(test_index);
									if (!target_rt.find_route(welt, next3d, this, speed_to_kmh(cnv->get_min_top_speed()), curr_90direction, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, 33, cnv->has_tall_vehicles(), route_t::choose_signal)) {
										// nothing empty or not route with less than 33 tiles
										target_halt = halthandle_t();
										restart_speed = 0;
										return false;
									}

									// now reserve our choice (beware: might be longer than one tile!)
									for (uint32 length = 0; length < cnv->get_tile_length() && length + 1 < target_rt.get_count(); length++) {
										target_halt->reserve_position(welt->lookup(target_rt.at(target_rt.get_count() - length - 1)), cnv->self);
									}
									cnv->update_route(test_index, target_rt);
								}
							}
						}
					}
				}
			}
			else {
				rs = NULL;
			}

			test_index++;
		}

		if(  obj  &&  test_index > route_index + 1u  &&  !str->is_crossing()  ) {
			// found a car blocking us after checking at least 1 intersection or crossing
			// and the car is in a place we could stop. So if it can move, assume it will, so we will too.
			// but check only upto 8 cars ahead to prevent infinite recursion on roundabouts.
			if(  second_check_count >= 8  ) {
				return false;
			}
			if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
				convoi_t* const ocnv = car->get_convoi();
				sint32 dummy;
				if(  ocnv->get_state() == convoi_t::DRIVING && ocnv->front()->get_route_index() < ocnv->get_route()->get_count()  &&  ocnv->front()->can_enter_tile(dummy, second_check_count + 1 )  ) {
					return true;
				}
			}
		}

		// stuck message ...
		if(  obj  &&  !second_check_count  ) {
			// Process is different whether the road is for one-way or two-way
			sint8 overtaking_mode = str->get_overtaking_mode();
			if(  overtaking_mode <= oneway_mode  ) {
				// road is one-way.
				// The overtaking judge method itself works only when test_index==route_index+1, that means the front tile is not an intersection.
				// However, with halt_mode we want to simulate a bus terminal. Overtaking in a intersection is essential. So we make a exception to the test_index==route_index+1 condition, although it is not clear that this exception is safe or not!
				if(  (test_index == route_index + 1u) || (test_index == route_index + 2u  &&  overtaking_mode == halt_mode)  ) {
					// no intersections or crossings, we might be able to overtake this one ...
					if(  obj->get_overtaker()  ) {
						// not overtaking/being overtake: we need to make a more thought test!
						if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
							convoi_t* const ocnv = car->get_convoi();
							// yielding vehicle should not be overtaken by the vehicle whose maximum speed is same.
							bool yielding_factor = true;
							if(  ocnv->get_yielding_quit_index() != -1  &&  this->get_speed_limit() - ocnv->get_min_top_speed() < kmh_to_speed(10)  ) {
								yielding_factor = false;
							}
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking()  &&  !other_lane_blocked()  &&  yielding_factor  &&  cnv->can_overtake( ocnv, (ocnv->is_loading() ? 0 : ocnv->get_akt_speed()), ocnv->get_length_in_steps()+ocnv->front()->get_steps())  ) {
								return true;
							}
							strasse_t *str=(strasse_t *)gr->get_weg(road_wt);
							sint32 cnv_max_speed;
							sint32 other_max_speed;
							const bool electric = cnv->needs_electrification();
							if (desc->get_override_way_speed())
							{
								cnv_max_speed = cnv->get_min_top_speed() * kmh_to_speed(1);
							}
							else
							{
								cnv_max_speed = (int)fmin(cnv->get_min_top_speed(), str->get_max_speed(electric) * kmh_to_speed(1));
							}
							if (ocnv->front()->get_desc()->get_override_way_speed())
							{
								other_max_speed = ocnv->get_min_top_speed() * kmh_to_speed(1);
							}
							else
							{
								other_max_speed = (int)fmin(ocnv->get_min_top_speed(), str->get_max_speed(electric) * kmh_to_speed(1));
							}
							if(  cnv->is_overtaking() && kmh_to_speed(10) <  cnv_max_speed - other_max_speed  ) {
								// If the convoi is on passing lane and there is slower convoi in front of this, this convoi request the slower to go to traffic lane.
								ocnv->set_requested_change_lane(true);
							}
							//For the case that the faster convoi is on traffic lane.
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking() && kmh_to_speed(10) <  cnv_max_speed - other_max_speed  ) {
								if(  vehicle_base_t* const br = car->other_lane_blocked()  ) {
									if(  road_vehicle_t const* const blk = obj_cast<road_vehicle_t>(br)  ) {
										if(  car->get_direction() == blk->get_direction() && abs(car->get_convoi()->get_min_top_speed() - blk->get_convoi()->get_min_top_speed()) < kmh_to_speed(5)  ){
											//same direction && (almost) same speed vehicle exists.
											ocnv->yield_lane_space();
										}
									}
								}
							}
						}
						else if(  private_car_t* const caut = obj_cast<private_car_t>(obj)  ) {
							if(  cnv->get_lane_affinity() != -1  &&  next_lane<1  &&  !cnv->is_overtaking()  &&  !other_lane_blocked()  &&  cnv->can_overtake(caut, caut->get_current_speed(), VEHICLE_STEPS_PER_TILE)  ) {
								return true;
							}
						}
					}
				}
				// we have to wait ...
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					restart_speed = 0;
					cnv->reset_waiting();
					if(  cnv->is_overtaking()  &&  other_lane_blocked() == NULL  ) {
						cnv->set_tiles_overtaking(0);
					}
				}
				else {
					restart_speed = (cnv->get_akt_speed()*3)/4;
				}
			}
			else if(  overtaking_mode <= twoway_mode) {
				// road is two-way and overtaking is allowed on the stricter condition.
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					// Not giving stuck messages here is a problem as there might be a circular jam, which is
					// actually a common case. Do not reset waiting here.
					restart_speed = 0;
					//cnv->set_tiles_overtaking(0);
					//cnv->reset_waiting();
				}
				else {
					if(  test_index == route_index + 1u  ) {
						// no intersections or crossings, we might be able to overtake this one ...
						overtaker_t *over = obj->get_overtaker();
						if(  over  &&  !over->is_overtaken()  ) {
							if(  over->is_overtaking()  ) {
								// otherwise we would stop every time being overtaken
								return true;
							}
							// not overtaking/being overtake: we need to make a more thought test!
							if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(obj)  ) {
								convoi_t* const ocnv = car->get_convoi();
								if(  cnv->can_overtake( ocnv, (ocnv->is_loading() ? 0 : over->get_max_power_speed()), ocnv->get_length_in_steps()+ocnv->front()->get_steps())  ) {
									return true;
								}
							}
							else if(  private_car_t* const caut = obj_cast<private_car_t>(obj)  ) {
								if(  cnv->can_overtake(caut, caut->get_desc()->get_topspeed(), VEHICLE_STEPS_PER_TILE)  ) {
									return true;
								}
							}
						}
					}
					// we have to wait ...
					restart_speed = (cnv->get_akt_speed()*3)/4;
					//cnv->set_tiles_overtaking(0);
				}
			}
			else {
				// lane change is prohibited.
				if(  obj->is_stuck()  ) {
					// end of traffic jam, but no stuck message, because previous vehicle is stuck too
					restart_speed = 0;
					//cnv->set_tiles_overtaking(0);
					cnv->reset_waiting();
				}
				else {
					// we have to wait ...
					restart_speed = (cnv->get_akt_speed()*3)/4;
					//cnv->set_tiles_overtaking(0);
				}
			}
		}

		// On a queueing tile a vehicle ahead in the same lateral position only
		// blocks entry when it is too close; the movement cap in do_drive()
		// keeps the follower behind it once it is on the tile. This is checked
		// after the overtake decision above, so that a faster convoy first has
		// the chance to pass instead of settling into the queue. On a junction,
		// the exit tile must also have room for this vehicle, so that a vehicle
		// does not stop in the junction and block the crossing traffic.
		if (target_gr != NULL && (obj != NULL || road_space::is_junction(target_gr))
			&& !apply_following_rule(target_gr, obj, next_lane))
		{
			restart_speed = 0;
			return false;
		}

		const koord3d pos_next2 = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
		// We have to calculate offset.
		uint32 offset = 0;
		if( !route_index_beyond_end_of_route && str->get_pos()!=r.at(route_index)  ){
			for(uint32 test_index = route_index+1u; test_index<r.get_count(); test_index++){
				offset += 1;
				if(  str->get_pos()==r.at(test_index)  ) break;
			}
		}
		// If this vehicle is on passing lane and the next tile prohibites overtaking, this vehicle must wait until traffic lane become safe.
		// When condition changes, overtaking should be quitted once.
		if(  (cnv->is_overtaking()  &&  str->get_overtaking_mode()==prohibited_mode)  ||  (cnv->is_overtaking()  &&  str->get_overtaking_mode()>oneway_mode  &&  str->get_overtaking_mode()<=prohibited_mode  &&  static_cast<strasse_t*>(welt->lookup(get_pos())->get_weg(road_wt))->get_overtaking_mode()<=oneway_mode)  ) {
			if(  vehicle_base_t* v = other_lane_blocked(false, offset)  ) {
				if(  v->get_waytype() == road_wt  ) {
					restart_speed = 0;
					cnv->reset_waiting();
					cnv->set_next_cross_lane(true);
					return false;
				}
			}
			// There is no vehicle on traffic lane.
			// cnv->set_tiles_overtaking(0); is done in enter_tile()
		}
		// If the next tile is our destination and we are on passing lane of oneway mode road, we have to wait until traffic lane become safe.
		if(  cnv->is_overtaking()  &&  str->get_overtaking_mode()==oneway_mode  &&  route_index == r.get_count() - 1u  ) {
			halthandle_t halt = haltestelle_t::get_halt(welt->lookup(r.at(route_index))->get_pos(),cnv->get_owner());
			vehicle_base_t* v = other_lane_blocked(false, offset);
			if(  halt.is_bound()  &&  gr->get_weg_ribi(get_waytype())!=0  &&  v  &&  v->get_waytype() == road_wt  ) {
				restart_speed = 0;
				cnv->reset_waiting();
				cnv->set_next_cross_lane(true);
				return false;
			}
			// There is no vehicle on traffic lane.
		}
		// If the next tile is a intersection, lane crossing must be checked before entering.
		// The other vehicle is ignored if it is stopping to avoid stuck.
		const grund_t *gr = route_index_beyond_end_of_route ? NULL : welt->lookup(r.at(route_index));
		const strasse_t *stre = gr ? (strasse_t *)gr->get_weg(road_wt) : NULL;
		const ribi_t::ribi way_ribi = stre ? stre->get_ribi_unmasked() : ribi_t::none;
		if(  stre  &&  stre->get_overtaking_mode() <= oneway_mode  &&  (way_ribi == ribi_t::all  ||  ribi_t::is_threeway(way_ribi))  ) {
			if(  const vehicle_base_t* v = other_lane_blocked(true)  ) {
				if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
					if(  at->get_convoi()->get_akt_speed()!=0  &&  judge_lane_crossing(calc_direction(get_pos(),pos_next), calc_direction(pos_next,pos_next2), at->get_90direction(), cnv->is_overtaking(), false)  ) {
						// vehicle must stop.
						restart_speed = 0;
						cnv->reset_waiting();
						cnv->set_next_cross_lane(true);
						return false;
					}
				}
			}
		}
		// For the case that this vehicle is fixed to passing lane and is on traffic lane.
		if(  str->get_overtaking_mode() <= oneway_mode  &&  cnv->get_lane_affinity() == 1  &&  !cnv->is_overtaking()  ) {
			if(  vehicle_base_t* v = other_lane_blocked()  ) {
				if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(v)  ) {
					convoi_t* ocnv = car->get_convoi();
					if(  ocnv  &&  abs(cnv->get_min_top_speed() - ocnv->get_min_top_speed()) < kmh_to_speed(5)  ) {
						cnv->yield_lane_space();
					}
				}
				// citycars do not have the yielding mechanism.
			}
			else {
				// go on passing lane.
				cnv->set_tiles_overtaking(3);
			}
		}
		// If there is a vehicle that requests lane crossing, this vehicle must stop to yield space.
		if(  vehicle_base_t* v = other_lane_blocked(true)  ) {
			if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
				if(  at->get_convoi()->get_next_cross_lane()  &&  at==at->get_convoi()->back()  ) {
					// vehicle must stop.
					restart_speed = 0;
					cnv->reset_waiting();
					return false;
				}
			}
		}

		return obj==NULL;
	}

	return true;
}

overtaker_t* road_vehicle_t::get_overtaker()
{
	return cnv;
}

//return overtaker in "convoi_t"
convoi_t* road_vehicle_t::get_overtaker_cv()
{
	return cnv;
}

vehicle_base_t* road_vehicle_t::other_lane_blocked(const bool only_search_top, sint8 offset) const{
	// This function calculate whether the convoi can change lane.
	// only_search_top == false: check whether there's no car in -1 ~ +1 section.
	// only_search_top == true: check whether there's no car in front of this vehicle. (Not the same lane.)
	if(  leading  ){
		route_t const& r = *cnv->get_route();
		bool can_reach_tail = false;
		// we have to know the index of the tail of convoy.
		sint32 tail_index = -1;
		if(  cnv->get_vehicle_count()==1  ){
			tail_index = route_index==0 ? 0 : route_index-1;
		}else{
			for(uint32 test_index = 0; test_index < r.get_count(); test_index++){
				koord3d test_pos = r.at(test_index);
				if(test_pos==cnv->back()->get_pos()){
					tail_index = test_index;
					break;
				}
				if(test_pos==cnv->front()->get_pos()) break;
			}
		}
		for(sint32 test_index = route_index+offset < (sint32)r.get_count() ? route_index+offset : r.get_count() - 1u;; test_index--){
			grund_t *gr = welt->lookup(r.at(test_index));
			if(  !gr  ) {
				cnv->suche_neue_route();
				return NULL;
			}
			// this function cannot process vehicles on twoway and related mode road.
			const strasse_t* str = (strasse_t *)gr->get_weg(road_wt);
			if(  !str  ||  (str->get_overtaking_mode()>=twoway_mode  &&  str->get_overtaking_mode() <= prohibited_mode)  ) {
				break;
			}

			for(  uint8 pos=1;  pos < gr->get_top();  pos++  ) {
				if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
					if(  v->get_typ()==obj_t::pedestrian  ) {
						continue;
					}
					if(  road_vehicle_t const* const at = obj_cast<road_vehicle_t>(v)  ) {
						// ignore ourself
						if(  cnv == at->get_convoi()  ) {
							continue;
						}
						if(  cnv->is_overtaking() && at->get_convoi()->is_overtaking()  ){
							continue;
						}
						if(  !cnv->is_overtaking() && !(at->get_convoi()->is_overtaking())  ){
							//Prohibit going on passing lane when facing traffic exists.
							ribi_t::ribi other_direction = at->get_direction();
							route_t const& r = *cnv->get_route();
							koord3d next = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
							if(  calc_direction(next,get_pos()) == other_direction  ) {
								return v;
							}
							continue;
						}
						// the logic of other_lane_blocked cannot be applied to facing traffic.
						if(test_index>0) {
							const ribi_t::ribi this_prev_dir = calc_direction(r.at(test_index-1u), r.at(test_index));
							if(ribi_t::backward(this_prev_dir)&at->get_previous_direction()) {
								continue;
							}
						}
						// Ignore stopping convoi on the tile behind this convoi to change lane in traffic jam.
						if(  can_reach_tail  &&  at->get_convoi()->get_akt_speed() == 0  ) {
							continue;
						}
						if(  test_index==tail_index-1+offset  ||  test_index==tail_index+offset  ){
							uint8 tail_offset = 0;
							if(  test_index==tail_index-1+offset  ) tail_offset = 1;
							if(  test_index+tail_offset>=1  &&  test_index+tail_offset<(sint32)r.get_count()-1  &&   judge_lane_crossing(calc_direction(r.at(test_index-1u+tail_offset),r.at(test_index+tail_offset)), calc_direction(r.at(test_index+tail_offset),r.at(test_index+1u+tail_offset)),  v->get_90direction(), cnv->is_overtaking(), true)  ){
								return v;
							}
							continue;
						}
						return v;
					}
					else if(  private_car_t* const caut = obj_cast<private_car_t>(v)  ) {
						if(  cnv->is_overtaking() && caut->is_overtaking()  ){
							continue;
						}
						if(  !cnv->is_overtaking() && !(caut->is_overtaking())  ){
							//Prohibit going on passing lane when facing traffic exists.
							ribi_t::ribi other_direction = caut->get_direction();
							route_t const& r = *cnv->get_route();
							koord3d next = route_index < r.get_count() - 1u ? r.at(route_index + 1u) : pos_next;
							if(  calc_direction(next,get_pos()) == other_direction  ) {
								return v;
							}
							continue;
						}
						// Ignore stopping convoi on the tile behind this convoi to change lane in traffic jam.
						if(  can_reach_tail  &&  caut->get_current_speed() == 0  ) {
							continue;
						}
						if(  test_index==tail_index-1+offset  ||  test_index==tail_index+offset  ){
							uint8 tail_offset = 0;
							if(  test_index==tail_index-1+offset  ) tail_offset = 1;
							if(  test_index+tail_offset>=1  &&  test_index+tail_offset<(sint32)r.get_count()-1  &&   judge_lane_crossing(calc_direction(r.at(test_index-1u+tail_offset),r.at(test_index+tail_offset)), calc_direction(r.at(test_index+tail_offset),r.at(test_index+1u+tail_offset)),  v->get_90direction(), cnv->is_overtaking(), true)  ){
								return v;
							}
							continue;
						}
						return v;
					}
				}
			}
			if(  test_index==tail_index-1+offset  ){
				// we finished the examination of the tile behind the tail of convoy.
				break;
			}
			if(  r.at(test_index)==cnv->back()->get_pos()  ) {
				can_reach_tail = true;
			}
			if(  test_index == 0  ||  only_search_top  ) {
				break;
			}
		}
	}
	return NULL;
}

uint32 road_vehicle_t::do_drive(uint32 distance)
{
	if (!leading)
	{
		// The other vehicles of the convoy follow the leading vehicle exactly;
		// see convoi_t::sync_step, which drives them by the distance that the
		// leading vehicle reported.
		return vehicle_base_t::do_drive(distance);
	}
	uint32 distance_travelled = 0;
	bool leader_updated = false;
	road_space::leader_t nearest;
	while (distance > 0)
	{
		road_space::user_t self;
		self.object = this;
		self.convoi = cnv;
		self.exit_direction = get_90direction();
		self.entry_direction = road_space::entry_direction(get_direction(), self.exit_direction);
		self.speed = cnv->get_akt_speed();
		self.length = cnv->get_length_in_steps();
		self.overtaking = cnv->is_overtaking();
		self.pulled_over = is_pulled_over();
		const koord3d pos_before = get_pos();
		const uint32 travelled = road_space::drive_until_limit(this, self, steps, steps_next, distance, &nearest);
		leader_updated = true;
		if (nearest.gap >= 0)
		{
			// The leader state was measured before this call moved the
			// vehicle, so reduce its gap by the distance travelled.
			nearest.gap -= (sint32)(travelled >> YARDS_PER_VEHICLE_STEP_SHIFT);
			if (nearest.gap < 0)
			{
				nearest.gap = 0;
			}
		}
		distance_travelled += travelled;
		distance -= travelled;
		if (get_pos() == pos_before)
		{
			// Either the whole distance was travelled within the tile, or the
			// vehicle is held at its limit. In both cases the leader must not
			// be recomputed by adding more distance here.
			break;
		}
	}
	if (leader_updated)
	{
		following_leader = nearest;
	}
	add_distance(distance_travelled);
	// Render-only odometer for the lateral blends. Only the leading vehicle
	// moves by its own distance; the others follow it.
	lateral_distance += distance_travelled;
	return distance_travelled;
}

void road_vehicle_t::leave_tile()
{
	// A route ends on the halt tile, so the final hop can be within that same
	// tile. Keep the stand through such a hop; a hop to another tile leaves
	// the stand behind. The pull-in blend then fades as the vehicle moves away.
	if (leading && stand_mask != 0 && pos_next != get_pos())
	{
		stand_mask = 0;
	}
	vehicle_base_t::leave_tile();
}


void road_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);
	const int cargo = get_total_cargo();
	strasse_t *str = (strasse_t*)gr->get_weg(road_wt);
	if(str == NULL)
	{
		return;
	}
	str->book(cargo, WAY_STAT_GOODS);
	if (  leading  )  {
		// Reserve a stop stand on arrival at the end of the route when that is
		// a halt. Route finding only checks that a stand is free; the
		// reservation itself is made here, on the main thread, so that the
		// vehicle knows which lateral position to load at. The reservation is
		// released when the convoy departs (see release_target_reservations).
		const halthandle_t halt = gr->get_halt();
		if (cnv != NULL && halt.is_bound() && cnv->get_route() != NULL
			&& route_index >= cnv->get_route()->get_count() - 1) {
			if (halt->reserve_position(gr, cnv->self)) {
				// Remember the halt whose stand is held so that
				// release_target_reservations() frees it on departure, as it
				// does for a halt reserved during route finding.
				target_halt = halt;
			}
		}
		str->book(1, WAY_STAT_CONVOIS);
		cnv->update_tiles_overtaking();
		if(  next_lane==1  ) {
			cnv->set_tiles_overtaking(3);
			next_lane = 0;
		}
		//decide if overtaking convoi should go back to the traffic lane.
		if(  cnv->get_tiles_overtaking() == 1  &&  str->get_overtaking_mode() <= oneway_mode  ){
			vehicle_base_t* v = NULL;
			if(  cnv->get_lane_affinity() == 1  ||  (v = other_lane_blocked())!=NULL  ){
				//lane change denied
				cnv->set_tiles_overtaking(3);
				if(  cnv->is_requested_change_lane()  ||  cnv->get_lane_affinity() == -1  ) {
					//request the blocking convoi to reduce speed.
					if(  v  ) {
						if(  road_vehicle_t const* const car = obj_cast<road_vehicle_t>(v)  ) {
							if(  abs(cnv->get_min_top_speed() - car->get_convoi()->get_min_top_speed()) < kmh_to_speed(5)  ) {
								car->get_convoi()->yield_lane_space();
							}
						}
					}
					else {
						// perhaps this vehicle is in lane fixing.
						cnv->set_requested_change_lane(false);
					}
				}
			}
			else {
				//lane change accepted
				cnv->set_requested_change_lane(false);
			}
		}
		if(  cnv->get_yielding_quit_index() <= route_index  ) {
			cnv->quit_yielding_lane();
		}
		cnv->set_next_cross_lane(false); // since this convoi moved...
		// If there is one-way sign, calc lane_affinity. This should not be calculated in can_enter_tile().
		if(  roadsign_t* rs = gr->find<roadsign_t>()  ) {
			if(  rs->get_desc()->is_single_way()  ) {
				if(  cnv->calc_lane_affinity(rs->get_lane_affinity())  ) {
					// write debug code here.
				}
			}
		}
		if(  cnv->get_lane_affinity_end_index() == route_index  ) {
			cnv->reset_lane_affinity();
		}
		// If this tile is two-way ~ prohibited and the previous tile is oneway, the convoy have to move on traffic lane. Safety is confirmed in can_enter_tile().
		if(  pos_prev!=koord3d::invalid  ) {
			grund_t* prev_gr = welt->lookup(pos_prev);
			if(  prev_gr  ){
				strasse_t* prev_str = (strasse_t*)prev_gr->get_weg(road_wt);
				if(  (prev_str  &&  (prev_str->get_overtaking_mode()<=oneway_mode  &&  str->get_overtaking_mode()>oneway_mode  &&  str->get_overtaking_mode() <= prohibited_mode))  ||  str->get_overtaking_mode()==prohibited_mode  ){
					cnv->set_tiles_overtaking(0);
				}
			}
		}
	}
	drives_on_left = welt->get_settings().is_drive_left();	// reset driving settings
}

void road_vehicle_t::hop(grund_t* gr_to) {
	if(leading)
	{
		grund_t* gr = get_grund();
		if(gr)
		{
			strasse_t* str = (strasse_t*)gr->get_weg(road_wt);
			if(str)
			{
				if(get_last_stop_pos() != get_pos() && get_pos_next() != get_pos_prev())
				{
					flush_travel_times(str);
				}
				else {
					reset_measurements();
				}
			}
		}
	}
	vehicle_t::hop(gr_to);
}


schedule_t * road_vehicle_t::generate_new_schedule() const
{
	return new truck_schedule_t();
}


void road_vehicle_t::set_convoi(convoi_t *c)
{
	DBG_MESSAGE("road_vehicle_t::set_convoi()","%p",c);
	if(c!=NULL) {
		bool target=(bool)cnv; // only during loadtype: cnv==1 indicates, that the convoi did reserve a stop
		vehicle_t::set_convoi(c);
		if(target  &&  leading  &&  c->get_route()->empty()) {
			// reinitialize the target halt
			const route_t *rt = cnv->get_route();
			target_halt = haltestelle_t::get_halt( rt->back(), get_owner() );
			if(  target_halt.is_bound()  ) {
				for(  uint32 i=0;  i<c->get_tile_length()  &&  i+1<rt->get_count();  i++  ) {
					target_halt->reserve_position( welt->lookup( rt->at(rt->get_count()-i-1) ), cnv->self );
				}
			}
		}
	}
	else {
		if(  cnv  &&  leading  &&  target_halt.is_bound()  ) {
			// now reserve our choice (beware: might be longer than one tile!)
			for(  uint32 length=0;  length<cnv->get_tile_length()  &&  length+1<cnv->get_route()->get_count();  length++  ) {
				target_halt->unreserve_position( welt->lookup( cnv->get_route()->at( cnv->get_route()->get_count()-length-1) ), cnv->self );
			}
			target_halt = halthandle_t();
		}
		cnv = NULL;
	}
}
