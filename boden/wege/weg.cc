/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <tuple>

#include "../../tpl/slist_tpl.h"

#include "weg.h"

#include "schiene.h"
#include "strasse.h"
#include "monorail.h"
#include "maglev.h"
#include "narrowgauge.h"
#include "kanal.h"
#include "runway.h"

#include "../grund.h"
#include "../../simmesg.h"
#include "../../simworld.h"
#include "../../display/simimg.h"
#include "../../simhalt.h"
#include "../../obj/simobj.h"
#include "../../player/simplay.h"
#include "../../obj/wayobj.h"
#include "../../obj/roadsign.h"
#include "../../obj/signal.h"
#include "../../obj/crossing.h"
#include "../../obj/bruecke.h"
#include "../../obj/tunnel.h"
#include "../../obj/gebaeude.h" // for ::should_city_adopt_this
#include "../../obj/pier.h"
#include "../../utils/cbuffer_t.h"
#include "../../dataobj/environment.h" // TILE_HEIGHT_STEP
#include "../../dataobj/translator.h"
#include "../../dataobj/loadsave.h"
#include "../../dataobj/environment.h"
#include "../../descriptor/way_desc.h"
#include "../../descriptor/tunnel_desc.h"
#include "../../descriptor/roadsign_desc.h"
#include "../../descriptor/building_desc.h" // for ::should_city_adopt_this
#include "../../utils/simstring.h"
#include "../../simcity.h"

#include "../../bauer/wegbauer.h"

#ifdef MULTI_THREAD
#include "../../utils/simthread.h"
static pthread_mutex_t weg_calc_image_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutexattr_t mutex_attributes;
//static pthread_rwlockattr_t rwlock_attributes;

pthread_mutex_t weg_t::private_car_route_map::route_map_mtx = PTHREAD_MUTEX_INITIALIZER;
#endif


/**
 * Alle instantiierten Wege
 */
vector_tpl <weg_t *> alle_wege;

static slist_tpl<std::tuple<weg_t*, uint32, uint32>> pending_road_travel_time_updates;
/**
 * Get list of all ways
 */
const vector_tpl <weg_t *> & weg_t::get_alle_wege()
{
	return alle_wege;
}

uint32 weg_t::get_all_ways_count()
{
	return alle_wege.get_count();
}

void weg_t::clear_list_of__ways()
{
	alle_wege.clear();
}


// returns a way with matching waytype
weg_t* weg_t::alloc(waytype_t wt)
{
	weg_t *weg = NULL;
	switch(wt) {
		case tram_wt:
		case track_wt:
			weg = new schiene_t();
			break;
		case monorail_wt:
			weg = new monorail_t();
			break;
		case maglev_wt:
			weg = new maglev_t();
			break;
		case narrowgauge_wt:
			weg = new narrowgauge_t();
			break;
		case road_wt:
			weg = new strasse_t();
			break;
		case water_wt:
			weg = new kanal_t();
			break;
		case air_wt:
			weg = new runway_t();
			break;
		default:
			// keep compiler happy; should never reach here anyway
			assert(0);
			break;
	}
	return weg;
}


// returns a string with the "official name of the waytype"
const char *weg_t::waytype_to_string(waytype_t wt)
{
	switch(wt) {
		case tram_wt:        return "tram_track";
		case track_wt:       return "track";
		case monorail_wt:    return "monorail_track";
		case maglev_wt:      return "maglev_track";
		case narrowgauge_wt: return "narrowgauge_track";
		case road_wt:        return "road";
		case water_wt:       return "water";
		case air_wt:         return "air";
		default:
			// keep compiler happy; should never reach here anyway
			break;
	}
	return "invalid waytype";
}


sint32 weg_t::get_max_speed(bool needs_electrification) const
{
	if( needs_electrification && is_electrified() ) {
		if( grund_t* gr = welt->lookup(get_pos()) ) {
			// "is_electrified()==true" means tile has an overhead_line wayobj
			return min(max_speed, gr->get_wayobj(get_waytype())->get_desc()->get_topspeed());
		}
	}
	return max_speed;
}

void weg_t::set_desc(const way_desc_t *b, bool from_saved_game)
{
	if(desc)
	{
		// Remove the old maintenance cost
		sint32 old_maint = get_desc()->get_maintenance();
		check_diagonal();
		if(is_diagonal())
		{
			old_maint *= 10;
			old_maint /= 14;
		}
		player_t::add_maintenance(get_owner(), -old_maint, get_desc()->get_finance_waytype());
	}

	desc = b;

	if (!from_saved_game)
	{
		// Add the new maintenance cost
		// Do not set this here if loading a saved game,
		// as this is already set in finish_rd
		sint32 maint = get_desc()->get_maintenance();
		if (is_diagonal())
		{
			maint *= 10;
			maint /= 14;
		}
		player_t::add_maintenance(get_owner(), maint, get_desc()->get_finance_waytype());
	}

	grund_t* gr = welt->lookup(get_pos());
	if(!gr)
	{
		gr = welt->lookup_kartenboden(get_pos().get_2d());
	}
	const bruecke_t *bridge = gr ? gr->find<bruecke_t>() : NULL;
	const tunnel_t *tunnel = gr ? gr->find<tunnel_t>() : NULL;
	bool on_pier = gr ? gr->get_typ()==grund_t::pierdeck : false;

	const slope_t::type hang = gr ? gr->get_weg_hang() : slope_t::flat;

#ifdef MULTI_THREAD_CONVOYS
	if (env_t::networkmode)
	{
		// In network mode, we cannot have set_desc running concurrently with
		// convoy path-finding because  whether the convoy path-finder is called
		// on this tile of way before or after this function is indeterminate.
		world()->await_convoy_threads();
	}
#endif

	// Check degraded unowned tram lines, and remove them
	// This includes disused railway crossings
	if (!from_saved_game && get_waytype() == road_wt)
	{
		const weg_t* tramway = gr ? gr->get_weg(track_wt) : NULL;
		if (tramway && tramway->get_remaining_wear_capacity() == 0 && tramway->get_owner() == NULL)
		{
			// Fully degraded, unowned tram line: delete
			gr->remove_everything_from_way(get_owner(), track_wt, ribi_t::none);
			gr->mark_image_dirty();
		}
	}

	if(hang != slope_t::flat)
	{
		const uint slope_height = (hang & 7) ? 1 : 2;
		if(slope_height == 1)
		{
			if(bridge)
			{
				max_speed = min(desc->get_topspeed_gradient_1(), bridge->get_desc()->get_topspeed_gradient_1());
			}
			else if(tunnel)
			{
				max_speed = min(desc->get_topspeed_gradient_1(), tunnel->get_desc()->get_topspeed_gradient_1());
			}
			else
			{
				max_speed = desc->get_topspeed_gradient_1();
			}
		}
		else
		{
			if(bridge)
			{
				max_speed = min(desc->get_topspeed_gradient_2(), bridge->get_desc()->get_topspeed_gradient_2());
			}
			else if(tunnel)
			{
				max_speed = min(desc->get_topspeed_gradient_2(), tunnel->get_desc()->get_topspeed_gradient_2());
			}
			else
			{
				max_speed = desc->get_topspeed_gradient_2();
			}
		}
	}
	else
	{
		if(bridge)
			{
				max_speed = min(desc->get_topspeed(), bridge->get_desc()->get_topspeed());
			}
		else if(tunnel)
			{
				max_speed = min(desc->get_topspeed(), tunnel->get_desc()->get_topspeed());
			}
		else if(on_pier)
			{
				max_speed = pier_t::get_speed_limit_deck_total(gr,desc->get_topspeed());
			}
			else
			{
				max_speed = desc->get_topspeed();
			}
	}

	const sint32 city_road_topspeed = welt->get_city_road()->get_topspeed();

	if(hat_gehweg() && desc->get_wtyp() == road_wt)
	{
		max_speed = min(max_speed, city_road_topspeed);
	}

	max_axle_load = desc->get_max_axle_load();
	if(on_pier){
		if(desc->get_wtyp() == road_wt){ //roads can have one vehicle in each direction
			uint16 pier_max_load = pier_t::get_max_axle_load_deck_total(gr) / 2;
			if(pier_max_load < max_axle_load){
				max_axle_load = pier_max_load;
			}
		}else{
			max_axle_load = pier_t::get_max_axle_load_deck_total(gr, max_axle_load);
		}
	}

	// Clear the old constraints then add all sources of constraints again.
	// (Removing will not work in cases where a way and another object,
	// such as a bridge, tunnel or wayobject, share a constraint).
	clear_way_constraints();
	add_way_constraints(desc->get_way_constraints()); // Add the way's own constraints
	if(bridge)
	{
		add_way_constraints(bridge->get_desc()->get_way_constraints());
	}
	if(tunnel)
	{
		add_way_constraints(tunnel->get_desc()->get_way_constraints());
	}
	const wayobj_t* wayobj = gr ? gr->get_wayobj(get_waytype()) : NULL;
	if(wayobj)
	{
		add_way_constraints(wayobj->get_desc()->get_way_constraints());
	}

	if(desc->is_mothballed())
	{
		degraded = true;
		remaining_wear_capacity = 0;
		replacement_way = NULL;
		if(!from_saved_game)
		{
			// We need to know when the way was degraded as well as upgraded
			last_renewal_month_year = welt->get_timeline_year_month();
		}
	}
	else if(!from_saved_game)
	{
		remaining_wear_capacity = desc->get_wear_capacity();
		degraded = false;
		replacement_way = desc;
		last_renewal_month_year = welt->get_timeline_year_month();
		const grund_t* gr = welt->lookup(get_pos());
		if(gr)
		{
			roadsign_t* rs = gr->find<roadsign_t>();
			if(!rs)
			{
				rs =  gr->find<signal_t>();
			}
			if(rs && rs->get_desc()->is_retired(welt->get_timeline_year_month()))
			{
				// Upgrade obsolete signals and signs when upgrading the underlying way if possible.
				rs->upgrade(welt->lookup_kartenboden(get_pos().get_2d())->get_hoehe() != get_pos().z);
			}
		}
	}
}


/**
 * initializes statistic array
 */
void weg_t::init_statistics()
{
	for(  int type=0;  type<MAX_WAY_STATISTICS;  type++  ) {
		for(  int month=0;  month<MAX_WAY_STAT_MONTHS;  month++  ) {
			statistics[month][type] = 0;
		}
	}
	for(  int type=0;  type<MAX_WAY_TRAVEL_TIMES;  type++  ) {
		for(  int month=0;  month<MAX_WAY_STAT_MONTHS;  month++  ) {
			travel_times[month][type] = 0;
		}
	}
	creation_month_year = welt->get_timeline_year_month();
}


/**
 * Initializes all member variables
 */
void weg_t::init()
{
	ribi = ribi_maske = ribi_t::none;
	max_speed = 450;
	max_axle_load = 1000;
	bridge_weight_limit = UINT32_MAX_VALUE;
	desc = 0;
	init_statistics();
	alle_wege.append(this);
	flags = 0;
	image = IMG_EMPTY;
	foreground_image = IMG_EMPTY;
	public_right_of_way = false;
	degraded = false;
	remaining_wear_capacity = 100000000;
	replacement_way = NULL;
#ifdef MULTI_THREAD
	pthread_mutexattr_init(&mutex_attributes);
	//int error = pthread_rwlockattr_init(&rwlock_attributes);
	//assert(error == 0);
	//int error = pthread_rwlock_init(&private_car_store_route_rwlock, &rwlock_attributes);
	//assert(error == 0);
#endif
	for(uint32 j=0; j<2; j++){
		for(uint32 i=0; i<5; i++){
			private_car_routes[j][i].set_route_map_elem(j);
		}
	}
}


weg_t::~weg_t()
{
	if (!welt->is_destroying())
	{
#ifdef MULTI_THREAD
		welt->await_private_car_threads();
#endif
		// This is possibly unnecessary and may lead to crashes
		//delete_all_routes_from_here();

		alle_wege.remove(this);
		player_t *player = get_owner();
		if (player  &&  desc)
		{
			sint32 maint = desc->get_maintenance();
			if (is_diagonal())
			{
				maint *= 10;
				maint /= 14;
			}
			player_t::add_maintenance(player, -maint, desc->get_finance_waytype());
		}
	}

}


void weg_t::rdwr(loadsave_t *file)
{
	xml_tag_t t( file, "weg_t" );

	// save owner
	if(  file->is_version_atleast(99, 6)  ) {
		sint8 spnum=get_owner_nr();
		file->rdwr_byte(spnum);
		set_owner_nr(spnum);
	}

	// all connected directions
	uint8 dummy8 = ribi;
	file->rdwr_byte(dummy8);
	if(  file->is_loading()  ) {
		ribi = dummy8 & 15; // before: high bits was maske
		ribi_maske = 0; // maske will be restored by signal/roadsing
	}

	sint16 dummy16=max_speed;
	file->rdwr_short(dummy16);
	max_speed=dummy16;

	if(  file->is_version_atleast(89, 0)  ) {
		dummy8 = flags;
		file->rdwr_byte(dummy8);
		if(  file->is_loading()  ) {
			// all other flags are restored afterwards
			flags = dummy8 & HAS_SIDEWALK;
		}
	}

	const uint32 max_stat_types = file->get_extended_version() >= 15 || (file->get_extended_version() == 14 && file->get_extended_revision() >= 19) ? MAX_WAY_STATISTICS : 2;

	for(uint32 type = 0; type < max_stat_types; type++)
	{
		for(uint32 month = 0; month < MAX_WAY_STAT_MONTHS; month++)
		{
			sint32 w = statistics[month][type];
			file->rdwr_long(w);
			statistics[month][type] = (sint16)w;
		}
	}


	if (file->is_loading() && ((file->get_extended_version() < 14) || (file->get_extended_version() == 14 && file->get_extended_revision() < 19)))
	{
		// Very early version - initialise the travel time statistics
		for(uint32 type = 0; type < MAX_WAY_TRAVEL_TIMES; type++)
		{
			for (uint32 month = 0; month < MAX_WAY_STAT_MONTHS; month++)
			{
				travel_times[month][type] = 0;
			}
		}
	}

	// NOTE: Revision 24 refers to the travel-time-congestion patch - if other patches that increment this number are merged first, then this must be updated too.
	else if(file->is_loading() && file->get_extended_version() == 14 && file->get_extended_revision() >= 19 && file->get_extended_revision() < 24)
	{
		// Older version - estimate travel time statistics from deprecated stopped vehicle statistics
		// Use 10 seconds as the base time to cross the way ? the actual value is not important at all but the ratio is.
		uint32 mul = (uint32)welt->get_seconds_to_ticks(10);

		for (uint32 month = 0; month < MAX_WAY_STAT_MONTHS; month++)
		{
			uint32 w = travel_times[month][WAY_TRAVEL_TIME_ACTUAL];

			// Get the now-deprecated stopped vehicles count
			file->rdwr_long(w);

			travel_times[month][WAY_TRAVEL_TIME_IDEAL] = statistics[month][WAY_STAT_CONVOIS] * mul;

			// We'll estimate a stopped vehicle to take twice longer than usual to cross the tile
			travel_times[month][WAY_TRAVEL_TIME_ACTUAL] = (statistics[month][WAY_STAT_CONVOIS] + (uint32)w) * mul;
		}
	}

	// NOTE: Revision 24 refers to the travel-time-congestion patch - if other patches that increment this number are merged first, then this must be updated too.
	else if ((file->get_extended_version() >= 15) || (file->get_extended_version() == 14 && file->get_extended_revision() >= 24))
	{
		for(uint32 type = 0; type < MAX_WAY_TRAVEL_TIMES; type++)
		{
			for (uint32 month = 0; month < MAX_WAY_STAT_MONTHS; month++)
			{
				uint32 w = travel_times[month][type];
				file->rdwr_long(w);
				travel_times[month][type] = (uint32)w;
			}
		}
	}

	if(file->get_extended_version() >= 1)
	{
		if (max_axle_load > 32768) {
			dbg->error("weg_t::rdwr()", "Max axle load out of range");
		}
		uint16 wdummy16 = max_axle_load;
		file->rdwr_short(wdummy16);
		max_axle_load = wdummy16;
	}

	if(file->get_extended_version() >= 12)
	{
		bool prow = public_right_of_way;
		file->rdwr_bool(prow);
		public_right_of_way = prow;
#ifdef SPECIAL_RESCUE_12_3
		if(file->is_saving())
		{
			uint32 rwc = remaining_wear_capacity;
			file->rdwr_long(rwc);
			remaining_wear_capacity = rwc;
			uint16 cmy = creation_month_year;
			file->rdwr_short(cmy);
			creation_month_year = cmy;
			uint16 lrmy = last_renewal_month_year;
			file->rdwr_short(lrmy);
			last_renewal_month_year = lrmy;
			bool deg = degraded;
			file->rdwr_bool(deg);
			degraded = deg;
		}
#else
		uint32 rwc = remaining_wear_capacity;
		file->rdwr_long(rwc);
		remaining_wear_capacity = rwc;
		uint16 cmy = creation_month_year;
		file->rdwr_short(cmy);
		creation_month_year = cmy;
		uint16 lrmy = last_renewal_month_year;
		file->rdwr_short(lrmy);
		last_renewal_month_year = lrmy;
		bool deg = degraded;
		file->rdwr_bool(deg);
		degraded = deg;
#endif

		if (file->get_extended_version() >= 15 || (file->get_extended_version() >= 14 && file->get_extended_revision() >= 19))
		{
			const uint32 route_array_number = file->get_extended_version() >= 15 || file->get_extended_revision() >= 20 ? 2 : 1;

			if (file->is_saving())
			{
				for (uint32 i = 0; i < route_array_number; i++)
				{
					for(uint32 j=0; j<5; j++) {
						private_car_routes[i][j].rdwr(file);
					}
				}
			}
			else // Loading
			{
				for (uint32 i = 0; i < route_array_number; i++)
				{
					// Unfortunately, the way private car routes are stored has changed a number of times in an effort to save memory.
					if((file->get_extended_version()==14 && file->get_extended_revision() >= 19) || file->get_extended_version() > 14) {
						if(file->get_extended_version() == 14 && file->get_extended_revision() < 37) {
							uint32 private_car_routes_count = 0;
							file->rdwr_long(private_car_routes_count);
							for (uint32 j = 0; j < private_car_routes_count; j++) {
								koord destination;
								destination.rdwr(file);
								if (file->get_extended_revision() < 33) {
									// Koord3d representation
									koord3d next_tile;
									next_tile.rdwr(file);
									private_car_routes[i][get_map_idx(next_tile)].insert_unique(destination);
								} else {
									// Integer-neighbour representation
									uint8 next_tile_neighbour;
									file->rdwr_byte(next_tile_neighbour);
									private_car_routes[i][get_map_idx(private_car_t::neighbour_from_int(get_pos(), next_tile_neighbour))].insert_unique(destination);
								}
							}
						} else {
							// Container membership representation
							for(uint8 j=0; j<5; j++) {
								private_car_routes[i][j].rdwr(file);
							}

							if(file->is_version_ex_less(14,39)) {
								// Correct for nsew->nesw change
								std::swap(private_car_routes[i][1],private_car_routes[i][2]);
							}
						}
					}
				}
			}
		}
	}
}

void weg_t::private_car_route_map::rdwr(loadsave_t *file){
	if(file->is_saving()){

		uint32 count=get_count();
		//single or null
		if(count < 2){
			file->rdwr_long(count);
			if(count==1){
				single_koord.rdwr(file);
			}
			return;
		}
		//use negative coords to store index and link mode
		if(link_mode == link_mode_master){
			count+=2;
		}else{
			count=2;
		}
		file->rdwr_long(count);
		koord idx1,idx2;
		idx1.x = -2;
		idx1.y = static_cast<sint16>((idx >> 16) & 0xFFFF);
		idx1.rdwr(file);
		idx2.x = -1 - (sint16)link_mode;
		idx2.y = static_cast<sint16>((idx) & 0xFFFF);
		idx2.rdwr(file);
		//save the destination list
		if(link_mode==link_mode_master){
			for(uint32 k = 0; k < get_count(); k++){
				route_maps[route_map_elem][idx].get_by_index(k).rdwr(file);
			}
		}
	}else{
		uint32 count=0;
		file->rdwr_long(count);
		resize(0);
		if(count == 1){
			single_koord.rdwr(file);
			link_mode = link_mode_single;
		}else if(count >= 2){
			koord idx1,idx2;
			idx1.rdwr(file);
			idx2.rdwr(file);
			if(idx1.x == -2){
				//newer file, extract index and link mode
				idx =  uint32(static_cast<uint16>(idx1.y)) << 16;
				idx |= uint32(static_cast<uint16>(idx2.y));
				link_mode = -1 - idx2.x;
				if(link_mode == link_mode_master){
					route_maps[route_map_elem].store_at(idx,ordered_vector_tpl<koord,uint32>(count-2));
				}
			}else{
				//older file, just append destinations
				resize(count);
				insert_unique(idx1);
				insert_unique(idx2);
			}
			//for master or old file, read remaining destinations
			for(uint32 k=2; k<count; k++){
				koord dest; dest.rdwr(file);
				insert_unique(dest);
			}
		}
	}
}

weg_t::runway_directions weg_t::get_runway_directions() const
{
	bool runway_36_18 = false;
	bool runway_09_27 = false;

	switch (get_ribi())
	{
	case 1: // north
	case 4: // south
	case 5: // north-south
	case 7: // north-south-east
	case 13: // north-south-west
		runway_36_18 = true;
		break;
	case 2: // east
	case 8: // west
	case 10: // east-west
	case 11: // east-west-north
	case 14: // east-west-south
		runway_09_27 = true;
		break;
	case 15: // all
		runway_36_18 = true;
		runway_09_27 = true;
		break;
	default:
		runway_36_18 = false;
		runway_09_27 = false;
		break;
	}

	return runway_directions(runway_36_18, runway_09_27);
}

uint32 weg_t::get_runway_length(bool runway_36_18) const
{
	if(get_waytype() != air_wt)
	{
		return 0;
	}

	uint32 runway_tiles = 0;
	koord3d pos = get_pos();
	bool more_runway_left = true;
	bool counting_runway_forward = true;

	if (runway_36_18 == true)
	{
		for (uint32 i = 0; more_runway_left == true; i++)
		{
			grund_t *gr = welt->lookup_kartenboden(pos.x, pos.y);
			runway_t * sch1 = gr ? (runway_t *)gr->get_weg(air_wt) : NULL;
			if (counting_runway_forward == true)
			{
				if (sch1 != NULL)
				{
					pos.y += 1;
					if ((sch1->get_ribi_unmasked() == 1))
					{
						counting_runway_forward = false;
						pos = get_pos();
					}
				}
				else
				{
					counting_runway_forward = false;
					pos = get_pos();
				}
			}

			else
			{
				if (sch1 != NULL)
				{
					pos.y -= 1;
					if ((sch1->get_ribi_unmasked() == 4))
					{
						more_runway_left = false;
						runway_tiles = i;
					}
				}
				else
				{
					more_runway_left = false;
					runway_tiles = i;
				}
			}
		}
		return runway_tiles;
	}

	// From here on in, we are testing the 9/27 direction

	runway_tiles = 0;
	pos = get_pos();
	more_runway_left = true;
	counting_runway_forward = true;

	for (uint32 i = 0; more_runway_left == true; i++)
	{
		grund_t *gr = welt->lookup_kartenboden(pos.x, pos.y);
		runway_t * sch1 = gr ? (runway_t *)gr->get_weg(air_wt) : NULL;
		if (counting_runway_forward == true)
		{
			if (sch1 != NULL)
			{
				pos.x += 1;
				if ((sch1->get_ribi_unmasked() == 8))
				{
					counting_runway_forward = false;
					pos = get_pos();
				}
			}
			else
			{
				counting_runway_forward = false;
				pos = get_pos();
			}
		}

		else
		{
			if (sch1 != NULL)
			{
				pos.x -= 1;
				if ((sch1->get_ribi_unmasked() == 2))
				{
					more_runway_left = false;
					runway_tiles = i;
				}
			}
			else
			{
				more_runway_left = false;
				runway_tiles = i;
			}
		}
	}
	return runway_tiles;
}

/**
 * called during map rotation
 */
void weg_t::rotate90()
{
	obj_t::rotate90();
	ribi = ribi_t::rotate90( ribi );
	ribi_maske = ribi_t::rotate90( ribi_maske );
}


/**
 * counts signals on this tile;
 * It would be enough for the signals to register and unregister themselves, but this is more secure ...
 */
void weg_t::count_sign()
{
	// Either only sign or signal please ...
	flags &= ~(HAS_SIGN|HAS_SIGNAL|HAS_CROSSING);
	const grund_t *gr=welt->lookup(get_pos());
	if(gr) {
		uint8 i = 1;
		// if there is a crossing, the start index is at least three ...
		if(  gr->ist_uebergang()  ) {
			flags |= HAS_CROSSING;
			i = 3;
			const crossing_t* cr = gr->find<crossing_t>();
			sint32 top_speed = cr ? cr->get_desc()->get_maxspeed(cr->get_desc()->get_waytype(0) == get_waytype() ? 0 : 1) : SINT32_MAX_VALUE;
			if(  top_speed < max_speed  ) {
				max_speed = top_speed;
			}
		}
		// since way 0 is at least present here ...
		for( ;  i<gr->get_top();  i++  ) {
			obj_t *obj=gr->obj_bei(i);
			// sign for us?
			if(  roadsign_t const* const sign = obj_cast<roadsign_t>(obj)  ) {
				if(  sign->get_desc()->get_wtyp() == get_desc()->get_wtyp()  ) {
					// here is a sign ...
					flags |= HAS_SIGN;
					return;
				}
			}
			if(  signal_t const* const signal = obj_cast<signal_t>(obj)  ) {
				if(  signal->get_desc()->get_wtyp() == get_desc()->get_wtyp()  ) {
					// here is a signal ...
					flags |= HAS_SIGNAL;
					return;
				}
			}
		}
	}
}


void weg_t::set_images(image_type typ, uint8 ribi, bool snow, bool switch_nw)
{
	switch(typ) {
		case image_flat:
		default:
			set_image( desc->get_image_id( ribi, snow ) );
			set_after_image( desc->get_image_id( ribi, snow, true ) );
			break;
		case image_slope:
			set_image( desc->get_slope_image_id( (slope_t::type)ribi, snow ) );
			set_after_image( desc->get_slope_image_id( (slope_t::type)ribi, snow, true ) );
			break;
		case image_switch:
			set_image( desc->get_image_nr_switch(ribi, snow, switch_nw) );
			set_after_image( desc->get_image_nr_switch(ribi, snow, switch_nw, true) );
			break;
		case image_diagonal:
			set_image( desc->get_diagonal_image_id(ribi, snow) );
			set_after_image( desc->get_diagonal_image_id(ribi, snow, true) );
			break;
	}
}


// much faster recalculation of season image
bool weg_t::check_season(const bool calc_only_season_change)
{
	if(  calc_only_season_change  ) { // nothing depends on season, only snowline
		return true;
	}

	// no way to calculate this or no image set (not visible, in tunnel mouth, etc)
	if(  desc == NULL  ||  image == IMG_EMPTY  ) {
		return true;
	}

	grund_t *from = welt->lookup( get_pos() );

	// use snow image if above snowline and above ground
	bool snow = (from->ist_karten_boden()  ||  !from->ist_tunnel())  &&  (get_pos().z  + from->get_weg_yoff()/TILE_HEIGHT_STEP >= welt->get_snowline()  ||  welt->get_climate( get_pos().get_2d() ) == arctic_climate);
	bool old_snow = (flags&IS_SNOW) != 0;
	if(  !(snow ^ old_snow)  ) {
		// season is not changing ...
		return true;
	}

	// set snow flake
	flags &= ~IS_SNOW;
	if(  snow  ) {
		flags |= IS_SNOW;
	}

	slope_t::type hang = from->get_weg_hang();
	if(  hang != slope_t::flat  ) {
		set_images( image_slope, hang, snow );
		return true;
	}

	if(  is_diagonal()  ) {
		if( desc->get_diagonal_image_id(ribi, snow) != IMG_EMPTY  ||
			desc->get_diagonal_image_id(ribi, snow, true) != IMG_EMPTY)
		{
			set_images(image_diagonal, ribi, snow);
		}
		else
		{
			set_images(image_flat, ribi, snow);
		}
	}
	else if(  ribi_t::is_threeway( ribi )  &&  desc->has_switch_image()  ) {
		// there might be two states of the switch; remember it when changing seasons
		if(  image == desc->get_image_nr_switch( ribi, old_snow, false )  ) {
			set_images( image_switch, ribi, snow, false );
		}
		else if(  image == desc->get_image_nr_switch( ribi, old_snow, true )  ) {
			set_images( image_switch, ribi, snow, true );
		}
		else {
			set_images( image_flat, ribi, snow );
		}
	}
	else {
		set_images( image_flat, ribi, snow );
	}

	return true;
}


#ifdef MULTI_THREAD
void weg_t::lock_mutex()
{
	pthread_mutex_lock( &weg_calc_image_mutex );
}


void weg_t::unlock_mutex()
{
	pthread_mutex_unlock( &weg_calc_image_mutex );
}
#endif


void weg_t::calc_image()
{
#ifdef MULTI_THREAD
	pthread_mutex_lock( &weg_calc_image_mutex );
#endif

#ifdef DEBUG_PRIVATE_CAR_ROUTES
	if (private_car_routes[private_car_routes_currently_reading_element].empty())
	{
		set_image(IMG_EMPTY);
		set_after_image(IMG_EMPTY);
#ifdef MULTI_THREAD
		pthread_mutex_unlock(&weg_calc_image_mutex);
#endif
		return;
	}
#endif

	grund_t *from = welt->lookup(get_pos());
	grund_t *to;
	image_id old_image = image;
	bool bridge_has_own_way_graphics = false;
	if(  from==NULL  ||  desc==NULL  ) {
		// no ground, in tunnel
		set_image(IMG_EMPTY);
		set_after_image(IMG_EMPTY);
		if(  from==NULL  ) {
			dbg->error( "weg_t::calc_image()", "Own way at %s not found!", get_pos().get_str() );
		}
#ifdef MULTI_THREAD
		pthread_mutex_unlock( &weg_calc_image_mutex );
#endif
		return; // otherwise crashing during enlargement
	}
	else if(  from->ist_tunnel() &&  from->ist_karten_boden()  &&  corner_se(from->get_grund_hang()) > 0
		&&  (grund_t::underground_mode==grund_t::ugm_none || (grund_t::underground_mode==grund_t::ugm_level && from->get_hoehe()<grund_t::underground_level))  ) {
		// in tunnel mouth, no underground mode
		// TODO: Consider special treatment of tunnel portal images here.
		set_image(IMG_EMPTY);
		set_after_image(IMG_EMPTY);
	}

	else {
		// use snow image if above snowline and above ground
		bool snow = (from->ist_karten_boden() || !from->ist_tunnel()) && (get_pos().z + from->get_weg_yoff() / TILE_HEIGHT_STEP >= welt->get_snowline() || welt->get_climate(get_pos().get_2d()) == arctic_climate);
		flags &= ~IS_SNOW;
		if(  snow  ) {
			flags |= IS_SNOW;
		}
		if(  from->ist_bruecke() && from->obj_bei(0)==this  ){
			//This checks whether we should show the way graphics on the bridge
			//if the bridge has own way graphics, we don't show the way graphics on the bridge
			const bruecke_t *bridge = from ? from->find<bruecke_t>() : NULL;
			if(  bridge  ){
				if(  bridge->get_desc()->get_has_own_way_graphics()  ){
					bridge_has_own_way_graphics=true;
				}
			}
		}

		slope_t::type hang = from->get_weg_hang();
		if(hang != slope_t::flat) {
			// on slope
			if(bridge_has_own_way_graphics){
				set_image(IMG_EMPTY);
				set_after_image(IMG_EMPTY);
			}else{
				set_images(image_slope, hang, snow);
			}
		}

		else {
			static int recursion = 0; /* Communicate among different instances of this method */

			// flat way
			if( bridge_has_own_way_graphics){
				set_image(IMG_EMPTY);
				set_after_image(IMG_EMPTY);
			}
			else {
				set_images(image_flat, ribi, snow);
			}

			// recalc image of neighbors also when this changed to non-diagonal
			if(recursion == 0) {
				recursion++;
				for(int r = 0; r < 4; r++) {
					if(  from->get_neighbour(to, get_waytype(), ribi_t::nesw[r])  ) {
						// can fail on water tiles
						if(  weg_t *w=to->get_weg(get_waytype())  )  {
							// and will only change the outcome, if it has a diagonal image ...
							if(  w->get_desc()->has_diagonal_image()  ) {
								w->calc_image();
							}
						}
					}
				}
				recursion--;
			}

			// try diagonal image
			if(  desc->has_diagonal_image()  ) {
				check_diagonal();

				// now apply diagonal image
				if(is_diagonal()) {
					if( desc->get_diagonal_image_id(ribi, snow) != IMG_EMPTY  ||
					    desc->get_diagonal_image_id(ribi, snow, true) != IMG_EMPTY) {
						set_images(image_diagonal, ribi, snow);
					}
				}
			}
		}
	}
	if(  image!=old_image  ) {
		sint8 yoff = from ? from->get_weg_yoff() : 0;
		mark_image_dirty(old_image, yoff);
		mark_image_dirty(image, from->get_weg_yoff());
	}
#ifdef MULTI_THREAD
	pthread_mutex_unlock( &weg_calc_image_mutex );
#endif
}


// checks, if this way qualifies as diagonal
void weg_t::check_diagonal()
{
	bool diagonal = false;
	flags &= ~IS_DIAGONAL;

	const ribi_t::ribi ribi = get_ribi_unmasked();
	if(  !ribi_t::is_bend(ribi)  ) {
		// This is not a curve, it can't be a diagonal
		return;
	}

	grund_t *from = welt->lookup(get_pos());
	grund_t *to;

	if(from->get_typ()==grund_t::pierdeck){
		flags |= IS_DIAGONAL;
		return;
	}

	ribi_t::ribi r1 = ribi_t::none;
	ribi_t::ribi r2 = ribi_t::none;

	// get the ribis of the ways that connect to us
	// r1 will be 45 degree clockwise ribi (eg northeast->east), r2 will be anticlockwise ribi (eg northeast->north)
	if(  from->get_neighbour(to, get_waytype(), ribi_t::rotate45(ribi))  ) {
		r1 = to->get_weg_ribi_unmasked(get_waytype());
	}

	if(  from->get_neighbour(to, get_waytype(), ribi_t::rotate45l(ribi))  ) {
		r2 = to->get_weg_ribi_unmasked(get_waytype());
	}

	// diagonal if r1 or r2 are our reverse and neither one is 90 degree rotation of us
	diagonal = (r1 == ribi_t::backward(ribi) || r2 == ribi_t::backward(ribi)) && r1 != ribi_t::rotate90l(ribi) && r2 != ribi_t::rotate90(ribi);

	if(  diagonal  ) {
		flags |= IS_DIAGONAL;
	}
}


/**
 * new month
 */
void weg_t::new_month()
{
	for (int type=0; type<MAX_WAY_STATISTICS; type++) {
		for (int month=MAX_WAY_STAT_MONTHS-1; month>0; month--) {
			statistics[month][type] = statistics[month-1][type];
		}
		statistics[0][type] = 0;
	}

	for (int type=0; type<MAX_WAY_TRAVEL_TIMES; type++) {
		for (int month=MAX_WAY_STAT_MONTHS-1; month>0; month--) {
			travel_times[month][type] = travel_times[month-1][type];
		}
		travel_times[0][type] = 0;
	}
	wear_way(desc->get_monthly_base_wear());
}


// correct speed and maintenance
void weg_t::finish_rd()
{
	player_t *player=get_owner();
	if(player && desc)
	{
		sint32 maint = desc->get_maintenance();
		check_diagonal();
		if(is_diagonal())
		{
			maint *= 10;
			maint /= 14;
		}
		player_t::add_maintenance( player,  maint, desc->get_finance_waytype() );
	}
}


// returns NULL, if removal is allowed
// players can remove public owned ways (Depracated)
const char *weg_t::is_deletable(const player_t *player)
{
	if(  get_owner_nr()==PUBLIC_PLAYER_NR  ) {
		return NULL;
	}
	return obj_t::is_deletable(player);
}

bool weg_t::is_low_clearence(const player_t *player, bool permissive){
	if(this->is_deletable(player)){
		return false;
	}
	if(this->is_public_right_of_way()){
		return false;
	}
	if(!this->desc->is_low_clearence(permissive)){
		return false;
	}
	if(grund_t *gr = welt->lookup(this->get_pos())){
		for(int i = 0; i < gr->get_top(); i++){
			obj_t *ob=gr->obj_bei(i);
			if(ob->get_typ()==obj_t::wayobj){
				wayobj_t* wo = (wayobj_t*)ob;
				if(wo->get_desc()->get_is_tall()){
					return false;
				}
			}else if(ob->get_typ()==obj_t::tunnel){
				tunnel_t* tu = (tunnel_t*)ob;
				if(!tu->get_desc()->get_is_half_height()){
					return false;
				}
			}
		}
	}
	return true;
}

/**
 *  Check whether the city should adopt the road.
 *  (Adopting the road sets a speed limit and builds a sidewalk.)
 */
bool weg_t::should_city_adopt_this(const player_t* player)
{
	if(!welt->get_settings().get_towns_adopt_player_roads())
	{
		return false;
	}
	if(get_waytype() != road_wt)
	{
		// Cities only adopt roads
		return false;
	}
	if(get_desc()->get_styp() == type_elevated)
	{
		// It would be too profitable for players if cities adopted elevated roads
		return false;
	}
	const grund_t* gr = welt->lookup_kartenboden(get_pos().get_2d());
	if(gr && get_pos().z < gr->get_hoehe())
	{
		// It would be too profitable for players if cities adopted tunnels
		return false;
	}
	if(player && player->is_public_service())
	{
		// Do not adopt public service roads, so that players can't mess with them
		return false;
	}
	const koord & pos = this->get_pos().get_2d();
	if(!welt->get_city(pos))
	{
		// Don't adopt roads outside the city limits.
		// Note, this also returns false on invalid coordinates
		return false;
	}

	bool has_neighbouring_building = false;
	for(uint8 i = 0; i < 8; i ++)
	{
		// Look for neighbouring buildings at ground level
		const grund_t* const gr = welt->lookup_kartenboden(pos + koord::neighbours[i]);
		if(!gr)
		{
			continue;
		}
		const gebaeude_t* const neighbouring_building = gr->find<gebaeude_t>();
		if(!neighbouring_building)
		{
			continue;
		}
		const building_desc_t* const desc = neighbouring_building->get_tile()->get_desc();
		// Most buildings count, including station extension buildings.
		// But some do *not*, namely platforms and depots.
		switch(desc->get_type())
		{
			case building_desc_t::city_res:
			case building_desc_t::city_com:
			case building_desc_t::city_ind:
				has_neighbouring_building = true;
				break;
			default:
				; // keep looking
		}
		switch(desc->get_type())
		{
			case building_desc_t::attraction_city:
			case building_desc_t::attraction_land:
			case building_desc_t::monument: // monument
			case building_desc_t::factory: // factory
			case building_desc_t::townhall: // town hall
			case building_desc_t::generic_extension:
			case building_desc_t::headquarters: // HQ
			case building_desc_t::dock: // dock
			case building_desc_t::flat_dock:
				has_neighbouring_building = (bool)welt->get_city(pos);
				break;
			case building_desc_t::depot:
			case building_desc_t::signalbox:
			case building_desc_t::generic_stop:
			default:
				; // continue checking
		}
	}
	// If we found a neighbouring building, we will adopt the road.
	return has_neighbouring_building;
}

uint32 weg_t::get_condition_percent() const
{
	if(desc->get_wear_capacity() == 0)
	{
		// Necessary to avoid divisions by zero on mothballed ways
		return 0;
	}
	// Necessary to avoid overflow. Speed not important as this is for the UI.
	// Running calculations should use fractions (e.g., "if(remaining_wear_capacity < desc->get_wear_capacity() / 6)").
	const sint64 remaining_wear_capacity_percent = (sint64)remaining_wear_capacity  * 100ll;
	const sint64 intermediate_result = remaining_wear_capacity_percent / (sint64)desc->get_wear_capacity();
	return (uint32)intermediate_result;
}

void weg_t::wear_way(uint32 wear)
{
	if(!wear || remaining_wear_capacity == UINT32_MAX_VALUE)
	{
		// If ways are defined with UINT32_MAX_VALUE,
		// this feature is intended to be disabled.
		return;
	}
	if(remaining_wear_capacity > wear)
	{
		const uint32 degridation_fraction = welt->get_settings().get_way_degradation_fraction();
		remaining_wear_capacity -= wear;
		if(remaining_wear_capacity < desc->get_wear_capacity() / degridation_fraction)
		{
			if(!renew())
			{
				degrade();
			}
		}
	}
	else if(!is_degraded())
	{
		remaining_wear_capacity = 0;
		if(!renew())
		{
			degrade();
		}
	}
}

bool weg_t::renew()
{
	if(!replacement_way)
	{
		return false;
	}

	player_t* const owner = get_owner();

	bool success = false;
	const sint64 price = desc->get_upgrade_group() == replacement_way->get_upgrade_group() ? replacement_way->get_way_only_cost() : replacement_way->get_value();
	if(welt->get_city(get_pos().get_2d()) || (owner && (owner->can_afford(price) || owner->is_public_service())))
	{
		// Unowned ways in cities are assumed to be owned by the city and will be renewed by it.
		waytype_t wt = replacement_way->get_waytype();
		const uint16 time = welt->get_timeline_year_month();
		const bool public_city_road = get_waytype() == road_wt && (owner == NULL || get_owner()->is_public_service()) && welt->get_city(get_pos().get_2d());
		const way_desc_t* latest_city_road = welt->get_settings().get_city_road_type(time);
		bool is_current = !time || (replacement_way->get_intro_year_month() <= time && time < replacement_way->get_retire_year_month());
		if (public_city_road && desc != latest_city_road)
		{
			replacement_way = latest_city_road;
		}
		else if(!is_current)
		{
			way_constraints_of_vehicle_t constraints;
			constraints.set_permissive(desc->get_way_constraints().get_permissive());
			constraints.set_prohibitive(desc->get_way_constraints().get_prohibitive());
			replacement_way = way_builder_t::weg_search(wt, replacement_way->get_topspeed(), (sint32)replacement_way->get_axle_load(), time, (systemtype_t)replacement_way->get_styp(), replacement_way->get_wear_capacity(), constraints);
		}

		if(!replacement_way)
		{
			// If the way search cannot find a replacement way, use the current way as a fallback.
			replacement_way = desc;
		}

		set_desc(replacement_way);
		success = true;
		if(owner)
		{
			owner->book_way_renewal(price, wt);
		}
	}
	else if(owner && owner == welt->get_active_player() && !owner->get_has_been_warned_about_no_money_for_renewals())
	{
		welt->get_message()->add_message(translator::translate("Not enough money to carry out essential way renewal work.\n"), get_pos().get_2d(), message_t::warnings, owner->get_player_nr());
		owner->set_has_been_warned_about_no_money_for_renewals(true); // Only warn once a month.
	}
	else if (public_right_of_way && wtyp == road_wt)
	{
		// Roads that are public rights of way should be renewed with the latest type.
		const grund_t* gr = welt->lookup(get_pos());
		const way_desc_t* way_desc = gr ? gr->get_default_road(this) : NULL;
		bool default_way_is_better_than_current_way = false;
		if(way_desc)
		{
			default_way_is_better_than_current_way == way_desc->get_topspeed() > desc->get_topspeed();
			default_way_is_better_than_current_way &= way_desc->get_max_axle_load() >= desc->get_max_axle_load();

			bool no_worse_stats = way_desc->get_topspeed() >= desc->get_topspeed() && way_desc->get_max_axle_load() >= desc->get_max_axle_load();

			default_way_is_better_than_current_way &= !no_worse_stats;
		}
		set_desc(way_desc && (!owner || default_way_is_better_than_current_way) ? way_desc : desc);
		success = true;
	}

	return success;
}

void weg_t::degrade()
{
	if(public_right_of_way)
	{
		// Do not degrade public rights of way, as these should remain passable.
		// Instead, take them out of private ownership and renew them with the default way.
		const bool initially_unowned = get_owner() == NULL;
		set_owner(NULL);
		if(wtyp == road_wt)
		{
			if (!initially_unowned && welt->get_timeline_year_month())
			{
				const grund_t* gr = welt->lookup(get_pos());
				const way_desc_t* way_desc = gr ? gr->get_default_road(this) : NULL;
				set_desc(way_desc ? way_desc : desc);
			}
			else
			{
				// If the timeline is not enabled, renew with the same type, or else the default is just the first way that happens to be in the array and might be something silly like a bridleway
				set_desc(desc);
			}
		}
		else
		{
			set_desc(desc);
		}
	}
	else
	{
		if(remaining_wear_capacity)
		{
			// There is some wear left, but this way is in a degraded state. Reduce the speed limit.
			if(!degraded)
			{
				// Only do this once, or else this will carry on reducing for ever.
				max_speed /= 2;
				degraded = true;
			}
		}
		else
		{
			// Totally worn out: impassable.
			max_speed = 0;
			degraded = true;
			const way_desc_t* mothballed_type = way_builder_t::way_search_mothballed(get_waytype(), (systemtype_t)desc->get_styp());
			if(mothballed_type)
			{
				set_desc(mothballed_type);
				calc_image();
			}
		}
	}
}

signal_t *weg_t::get_signal(ribi_t::ribi direction_of_travel) const
{
	signal_t* sig = welt->lookup(get_pos())->find<signal_t>();
	if (sig && sig->get_desc()->get_working_method() == one_train_staff)
	{
		// This allows a single one train staff cabinet to work for trains passing in both directions.
		return sig;
	}
	ribi_t::ribi way_direction = (ribi_t::ribi)ribi_maske;
	if((direction_of_travel & way_direction) == 0)
	{
		return sig;
	}
	else return NULL;
}

//#define NO_PRIVATE_CAR_DESTINATION_LINKING

vector_tpl<ordered_vector_tpl<koord,uint32> > weg_t::private_car_route_map::route_maps[2];

void weg_t::private_car_route_map::clear(){
	if(link_mode==link_mode_master){
		route_maps[route_map_elem][idx].clear();
	}else{
		link_mode=link_mode_NULL;
	}
}

void weg_t::private_car_route_map::pre_reset(){
	if(link_mode==link_mode_master){
		route_maps[route_map_elem][idx].clear();
	}
	link_mode=link_mode_NULL;
}

bool weg_t::private_car_route_map::contains(koord elem) const{
	if(link_mode==link_mode_NULL){
		return false;
	}
	if(link_mode==link_mode_single){
		bool result= single_koord==elem;
		return result;
	}
	bool result=route_maps[route_map_elem][idx].contains(elem);
	return result;
}

bool weg_t::private_car_route_map::insert_unique(koord elem, private_car_route_map* link_to, uint8 link_dir){
	if(link_mode==link_mode_NULL){
		single_koord=elem;
		link_mode=link_mode_single;
		return true;
	}
	if(link_mode==link_mode_single){
		if(single_koord==elem){
			return false;
		}
		if(link_to
				&& link_to->link_mode!=link_mode_NULL
				&& link_to->link_mode!=link_mode_single
				&& link_to->contains(single_koord)
				&& link_to->contains(elem)){
			link_mode=link_dir;
			idx=link_to->idx;
			return true;
		}
		route_maps[route_map_elem].append(ordered_vector_tpl<koord,uint32>());
		uint32 new_idx=route_maps[route_map_elem].get_count()-1;
		route_maps[route_map_elem][new_idx].insert_unique(single_koord);
		idx=new_idx;
		link_mode=link_mode_master;
	}
	if(link_mode==link_mode_master){
		bool result = route_maps[route_map_elem][idx].insert_unique(elem);
		return result;
	}
	if(link_to){
		if(link_mode==link_dir
				&& link_to->link_mode!=link_mode_NULL
				&& link_to->link_mode!=link_mode_single){
			idx=link_to->idx;
			return false;
		}
		route_maps[route_map_elem].append(ordered_vector_tpl<koord,uint32>(route_maps[route_map_elem][idx]));
		uint32 new_idx=route_maps[route_map_elem].get_count()-1;
		if(link_to->link_mode==link_mode_single){
			route_maps[route_map_elem][new_idx].insert_unique(link_to->single_koord);
		}else if(link_to->link_mode!=link_mode_NULL){
			route_maps[route_map_elem][new_idx].set_union(route_maps[route_map_elem][link_to->idx]);
		}
		idx=new_idx;
		link_mode=link_mode_master;
		bool result = route_maps[route_map_elem][idx].insert_unique(elem);
		return result;
	}
	return false;
}

koord& weg_t::private_car_route_map::get_by_index(uint32_t i){
	if(link_mode==link_mode_single){
		koord& result=single_koord;
		return result;
	}
	koord& result = route_maps[route_map_elem][idx][i];
	return result;
}

const koord& weg_t::private_car_route_map::get_by_index(uint32_t i) const {
	if(link_mode==link_mode_single){
		const koord& result=single_koord;
		return result;
	}
	const koord& result = route_maps[route_map_elem][idx][i];
	return result;
}
koord& weg_t::private_car_route_map::operator[](uint32 i){
	return get_by_index(i);
}

const koord& weg_t::private_car_route_map::operator[](uint32 i) const {
	return get_by_index(i);
}

uint32 weg_t::private_car_route_map::get_count() const {
	if(link_mode==link_mode_NULL){
		return 0;
	}
	if(link_mode==link_mode_single){
		return 1;
	}
	const uint32 result = route_maps[route_map_elem][idx].get_count();
	return result;
}

bool weg_t::private_car_route_map::is_empty() const {
	if(link_mode==link_mode_NULL){
		return true;
	}
	if(link_mode==link_mode_single){
		return false;
	}
	const bool result = route_maps[route_map_elem][idx].is_empty();
	return result;
}

bool weg_t::private_car_route_map::remove(koord elem){

	if(link_mode==link_mode_NULL){
		return false;
	}
	if(link_mode==link_mode_single){
		if(single_koord==elem){
			link_mode=link_mode_NULL;
			return true;
		}
		return false;
	}
	bool result = route_maps[route_map_elem][idx].remove(elem);
	return result;
}

void weg_t::private_car_route_map::resize(uint32 new_size){
	if((link_mode==link_mode_single || link_mode==link_mode_NULL) && new_size<=1){
		return;
	}
	if(link_mode!=link_mode_master){
		ordered_vector_tpl<koord,uint32> to_append=(link_mode==link_mode_single || link_mode==link_mode_NULL)
				? ordered_vector_tpl<koord,uint32>() : ordered_vector_tpl<koord,uint32>(route_maps[route_map_elem][idx]);
		route_maps[route_map_elem].append(to_append);
		idx=route_maps[route_map_elem].get_count()-1;
		if(link_mode==link_mode_single){
			route_maps[route_map_elem][idx].insert_unique(single_koord);
		}
		link_mode=link_mode_master;
	}
	route_maps[route_map_elem][idx].resize(new_size);
}

void weg_t::private_car_route_map::reset(uint8 map_elem){
	route_maps[map_elem].clear();
	route_maps[map_elem].resize(0);
}

weg_t::private_car_route_map* weg_t::private_car_backtrace_last_route_map=NULL;
uint8 weg_t::private_car_backtrace_last_idx=0;

void weg_t::private_car_backtrace_begin(){
	private_car_route_map::route_map_lock();
	private_car_backtrace_last_route_map=NULL;
}

void weg_t::private_car_backtrace_end(){
	private_car_route_map::route_map_unlock();
}

void weg_t::private_car_backtrace_add(koord destination, koord3d next_tile){
	uint8 writing_elem=get_private_car_routes_currently_writing_element();
	auto map = private_car_routes[writing_elem];
	const uint8 map_idx = get_map_idx(next_tile);

	if(!map[map_idx].contains(destination)) {
#ifdef NO_PRIVATE_CAR_DESTINATION_LINKING
		for(uint8 i=0;i<5;i++) {
			if(i != map_idx && map[i].remove(destination)) {
				break;
			}
		}
		map[map_idx].insert_unique(destination);
#else
		map[map_idx].insert_unique(destination,private_car_backtrace_last_route_map,private_car_backtrace_last_idx);
#endif
	}
}

void weg_t::private_car_backtrace_inc(koord3d next_tile){
	uint8 writing_elem=get_private_car_routes_currently_writing_element();
	auto map = private_car_routes[writing_elem];
	const uint8 map_idx = get_map_idx(next_tile);
	private_car_backtrace_last_route_map=map+map_idx;
	private_car_backtrace_last_idx=map_idx;
}

void weg_t::add_private_car_route(koord destination, koord3d next_tile)
{

	uint8 writing_elem=get_private_car_routes_currently_writing_element();
	private_car_route_map::route_map_lock();
	auto map = private_car_routes[writing_elem];
	const uint8 map_idx = get_map_idx(next_tile);

	if(!map[map_idx].contains(destination)) {
		for(uint8 i=0;i<5;i++) {
			if(i != map_idx && map[i].remove(destination)) {
				break;
			}
		}
		map[map_idx].insert_unique(destination);
	}

	private_car_route_map::route_map_unlock();

#ifdef DEBUG_PRIVATE_CAR_ROUTES
	calc_image();
#endif
}

uint8 weg_t::get_map_idx(const koord3d &next_tile) const {
	const ribi_t::ribi dir = ribi_type(get_pos(), next_tile);
	if(next_tile != koord3d::invalid) {
		for (uint8 j = 0; j < 4; j++) {
			if (dir == ribi_t::nesw[j]) {
				return j;
			}
		}
	}
	return (uint8) 4;
}

//never called
void weg_t::delete_all_routes_from_here(bool reading_set)
{
	const uint32 routes_index = reading_set ? private_car_routes_currently_reading_element : get_private_car_routes_currently_writing_element();

	vector_tpl<koord> destinations_to_delete;

	for(uint8 i=0;i<5;i++) {
		auto &map = private_car_routes[routes_index][i];
		if (!map.is_empty()) {
			for(uint32 j=0; j<map.get_count();j++) {
				destinations_to_delete.append(map[j]);
			}
		}
	}
		FOR(vector_tpl<koord>, dest, destinations_to_delete)
		{
			// This must be done in a two stage process to avoid memory corruption as the delete_route_to function will affect the very hashtable being iterated.
			delete_route_to(dest, reading_set);
		}
#ifdef DEBUG_PRIVATE_CAR_ROUTES
	calc_image();
#endif
}

//never called
void weg_t::delete_route_to(koord destination, bool reading_set)
{
	koord3d next_tile = get_pos();
	koord3d this_tile = next_tile;
	while (next_tile != koord3d::invalid && next_tile != koord3d(0, 0, 0))
	{
		const grund_t* gr = welt->lookup(next_tile);

		next_tile = koord3d::invalid;
		if (gr)
		{
			weg_t* const w = gr->get_weg(road_wt);
			if (w)
			{

				next_tile = w->get_next_on_private_car_route_to(destination, reading_set);

				w->remove_private_car_route(destination, reading_set);
			}
		}
		if (this_tile == next_tile)
		{
			break;
		}
		this_tile = next_tile;
	}
}

//never called
void weg_t::remove_private_car_route(koord destination, bool reading_set)
{
	const uint32 routes_index = reading_set ? private_car_routes_currently_reading_element : get_private_car_routes_currently_writing_element();
	private_car_route_map::route_map_lock();
	for(uint8 i=0;i<5;i++) {
		if(private_car_routes[routes_index][i].remove(destination)) {
			break;
		}
	}
	private_car_route_map::route_map_unlock();
}

void weg_t::add_travel_time_update(weg_t* w, uint32 actual, uint32 ideal)
{
	pending_road_travel_time_updates.append(std::make_tuple(w, actual, ideal));
}

void weg_t::apply_travel_time_updates() {
	while(!pending_road_travel_time_updates.empty() ) {
		weg_t* str;
		uint32 actual;
		uint32 ideal;
		std::tie(str, actual, ideal) = pending_road_travel_time_updates.remove_first();
		if(str) {
			str->update_travel_times(actual,ideal);
		}
	}
}

void weg_t::clear_travel_time_updates() {
	pending_road_travel_time_updates.clear();
}

koord3d weg_t::get_next_on_private_car_route_to(koord dest, bool reading_set, uint8 startdir) const {
#ifdef NO_PRIVATE_CAR_DESTINATION_LINKING
	startdir=0;
#endif
	auto map = private_car_routes[reading_set ? private_car_routes_currently_reading_element : get_private_car_routes_currently_writing_element()];
	if(map[4].contains(dest)){
		return koord3d::invalid;
	}
	for(uint8 i=startdir; i<4+startdir; i++) {
		if(map[i&3].contains(dest)) {
			grund_t* to;
			if(welt->lookup(get_pos())->get_neighbour(to, waytype_t::road_wt,ribi_t::nesw[i&3])) {
				return to->get_pos();
			}
		}
	}
	return koord3d();
}

bool weg_t::has_private_car_route(koord dest) const {
	return get_next_on_private_car_route_to(dest) != koord3d();
}
