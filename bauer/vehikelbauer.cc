/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <algorithm>
#include "../player/simplay.h"
#include "../simdebug.h"
#include "../utils/simrandom.h"
#include "../simtypes.h"

#include "../simconvoi.h"

#include "../dataobj/environment.h"
#include "../dataobj/tabfile.h"
#include "../dataobj/loadsave.h"
#include "../dataobj/translator.h"
#include "../dataobj/livery_scheme.h"

#include "../descriptor/vehicle_desc.h"

#include "vehikelbauer.h"

#include "../tpl/stringhashtable_tpl.h"
#include "../vehicle/air_vehicle.h"
#include "../vehicle/rail_vehicle.h"
#include "../vehicle/road_vehicle.h"
#include "../vehicle/water_vehicle.h"

#include "../simline.h"

const char* vehicle_builder_t::engine_type_names[11] =
{
  "unknown",
  "steam",
  "diesel",
  "electric",
  "bio",
  "sail",
  "fuel_cell",
  "hydrogene",
  "battery",
  "petrol",
  "turbine"
};

const char *vehicle_builder_t::vehicle_sort_by[vehicle_builder_t::sb_length] =
{
	"Unsorted",
	"Name",
	"Price",
	"Maintenance:",
	"Capacity:",
	"Max. speed:",
	"Range",
	"Power:",
	"Tractive Force:",
	"Axle load:",
	"Intro. date:",
	"Retire. date:",
	"Comfort"
};

static stringhashtable_tpl< vehicle_desc_t*, N_BAGS_SMALL> name_fahrzeuge;

// index 0 aur, 1...8 at normal waytype index
#define GET_WAYTYPE_INDEX(wt) ((int)(wt)>8 ? 0 : (wt))
static slist_tpl<vehicle_desc_t*> typ_fahrzeuge[vehicle_builder_t::sb_length][9];
static uint8 tmp_sort_idx;


void vehicle_builder_t::rdwr_speedbonus(loadsave_t *file)
{
	// Former speed bonus settings, now deprecated.
	// This is necessary to load old saved games that
	// contained speed bonus data.
	for(uint32 j = 0; j < 8; j++)
	{
		uint32 count = 1;
		file->rdwr_long(count);

		for(uint32 i=0; i<count; i++)
		{
			sint64 dummy = 0;
			uint32 dummy_2 = 0;
			file->rdwr_longlong(dummy);
			file->rdwr_long(dummy_2);
		}
	}
}


vehicle_t* vehicle_builder_t::build(koord3d k, player_t* player, convoi_t* cnv, const vehicle_desc_t* vb, bool upgrade, uint16 livery_scheme_index )
{
	vehicle_t* v;
	static karte_ptr_t welt;
	switch (vb->get_waytype()) {
		case road_wt:     v = new road_vehicle_t(      k, vb, player, cnv); break;
		case monorail_wt: v = new monorail_rail_vehicle_t(k, vb, player, cnv); break;
		case track_wt:
		case tram_wt:     v = new rail_vehicle_t(         k, vb, player, cnv); break;
		case water_wt:    v = new water_vehicle_t(         k, vb, player, cnv); break;
		case air_wt:      v = new air_vehicle_t(       k, vb, player, cnv); break;
		case maglev_wt:   v = new maglev_rail_vehicle_t(  k, vb, player, cnv); break;
		case narrowgauge_wt:v = new narrowgauge_rail_vehicle_t(k, vb, player, cnv); break;

		default:
			dbg->fatal("vehicle_builder_t::build()", "cannot built a vehicle with waytype %i", vb->get_waytype());
	}

	if(cnv)
	{
		livery_scheme_index = cnv->get_livery_scheme_index();
	}

	if(livery_scheme_index >= welt->get_settings().get_livery_schemes()->get_count())
	{
		// To prevent errors when loading a game with fewer livery schemes than that just played.
		livery_scheme_index = 0;
	}

	const livery_scheme_t* const scheme = welt->get_settings().get_livery_scheme(livery_scheme_index);
	uint16 date = welt->get_timeline_year_month();
	if(scheme)
	{
		const char* livery = scheme->get_latest_available_livery(date, vb);
		if(livery)
		{
			v->set_current_livery(livery);
			player->set_favorite_livery_scheme_index((uint8)simline_t::waytype_to_linetype(vb->get_waytype()), livery_scheme_index);
		}
		else
		{
			bool found = false;
			for(uint32 j = 0; j < welt->get_settings().get_livery_schemes()->get_count(); j ++)
			{
				const livery_scheme_t* const new_scheme = welt->get_settings().get_livery_scheme(j);
				const char* new_livery = new_scheme->get_latest_available_livery(date, vb);
				if(new_livery)
				{
					v->set_current_livery(new_livery);
					found = true;
					break;
				}
			}
			if(!found)
			{
				v->set_current_livery("default");
			}
		}
	}
	else
	{
		v->set_current_livery("default");
	}

	sint64 price = 0;

	if(upgrade)
	{
		price = vb->get_upgrade_price();
	}
	else
	{
		price = vb->get_value();
	}
	player->book_new_vehicle(-price, k.get_2d(), vb->get_waytype() );

	return v;
}



bool vehicle_builder_t::register_desc(vehicle_desc_t *desc)
{
	// register waytype list
	const int wt_idx = GET_WAYTYPE_INDEX( desc->get_waytype() );

	// first hashtable
	vehicle_desc_t *old_desc = name_fahrzeuge.get( desc->get_name() );
	if(  old_desc  ) {
		dbg->doubled("vehicle", desc->get_name());
		name_fahrzeuge.remove( desc->get_name() );
	}
	name_fahrzeuge.put(desc->get_name(), desc);

	// now add it to sorter (may be more than once!)
	for(  int sort_idx = 0;  sort_idx < vehicle_builder_t::sb_length;  sort_idx++  ) {
		if(  old_desc  ) {
			typ_fahrzeuge[sort_idx][wt_idx].remove(old_desc);
		}
		typ_fahrzeuge[sort_idx][wt_idx].append(desc);
	}
	// we cannot delete old_desc, since then xref-resolving will crash

	return true;
}


// compare funcions to sort vehicle in the list
static int compare_freight(const vehicle_desc_t* a, const vehicle_desc_t* b)
{
	int cmp = a->get_freight_type()->get_catg() - b->get_freight_type()->get_catg();
	if (cmp != 0) return cmp;
	if (a->get_freight_type()->get_catg() == 0) {
		cmp = a->get_freight_type()->get_index() - b->get_freight_type()->get_index();
	}
	if (cmp==0) {
		cmp = a->get_min_accommodation_class() - b->get_min_accommodation_class();
	}
	if (cmp==0) {
		cmp = a->get_max_accommodation_class() - b->get_max_accommodation_class();
	}
	return cmp;
}
static int compare_capacity(const vehicle_desc_t* a, const vehicle_desc_t* b) { return a->get_capacity() - b->get_capacity(); }
static int compare_engine(const vehicle_desc_t* a, const vehicle_desc_t* b) {
	return (a->get_capacity() + a->get_power() == 0 ? (uint8)vehicle_desc_t::steam : a->get_engine_type()) - (b->get_capacity() + b->get_power() == 0 ? (uint8)vehicle_desc_t::steam : b->get_engine_type());
}
static int compare_price(const vehicle_desc_t* a, const vehicle_desc_t* b) { return a->get_base_price() - b->get_base_price(); }
static int compare_topspeed(const vehicle_desc_t* a, const vehicle_desc_t* b) { return a->get_topspeed() - b->get_topspeed(); }
static int compare_power(const vehicle_desc_t* a, const vehicle_desc_t* b) {return (a->get_power() == 0 ? 0x7FFFFFF : a->get_power()) - (b->get_power() == 0 ? 0x7FFFFFF : b->get_power());}
static int compare_tractive_effort(const vehicle_desc_t* a, const vehicle_desc_t* b) { return (a->get_power() == 0 ? 0x7FFFFFF : a->get_tractive_effort()) - (b->get_power() == 0 ? 0x7FFFFFF : b->get_tractive_effort()); }
static int compare_intro_year_month(const vehicle_desc_t* a, const vehicle_desc_t* b) {return a->get_intro_year_month() - b->get_intro_year_month();}
static int compare_retire_year_month(const vehicle_desc_t* a, const vehicle_desc_t* b) {return a->get_retire_year_month() - b->get_retire_year_month();}


// default compare function with mode parameter
bool vehicle_builder_t::compare_vehicles(const vehicle_desc_t* a, const vehicle_desc_t* b, sort_mode_t mode)
{
	int cmp = 0;
	switch(mode) {
		//case sb_freight:
		//	cmp = compare_freight(a, b);
		//	if (cmp != 0) return cmp < 0;
		//	break;
		case sb_name:
			cmp = strcmp(translator::translate(a->get_name()), translator::translate(b->get_name()));
			if (cmp != 0) return cmp < 0;
			break;
		case sb_intro_date:
			cmp = compare_intro_year_month(a, b);
			if (cmp != 0) return cmp < 0;
			/* FALLTHROUGH */
		case sb_retire_date:
			cmp = compare_retire_year_month(a, b);
			if (cmp != 0) return cmp < 0;
			cmp = compare_intro_year_month(a, b);
			if (cmp != 0) return cmp < 0;
			break;
		case sb_value:
			cmp = compare_price(a, b);
			if (cmp != 0) return cmp < 0;
			break;
		case sb_running_cost:
			cmp = a->get_running_cost() - b->get_running_cost();
			if (cmp != 0) return cmp < 0;
			break;
		case sb_speed:
			cmp = compare_topspeed(a, b);
			if (cmp != 0) return cmp < 0;
			break;
		case sb_range:
		{
			const uint16 a_range = a->get_range()==0 ? 65535 : a->get_range();
			const uint16 b_range = b->get_range()==0 ? 65535 : b->get_range();
			cmp = a_range - b_range;
			if (cmp != 0) return cmp < 0;
			break;
		}
		case sb_power:
			cmp = compare_power(a, b);
			if (cmp != 0) return cmp < 0;
			/* FALLTHROUGH */
		case sb_tractive_force:
			cmp = compare_tractive_effort(a, b);
			if (cmp == 0) {
				cmp = compare_power(a, b);
			}
			if (cmp != 0) return cmp < 0;
			break;
		case sb_axle_load:
		{
			const uint16 a_axle_load = a->get_waytype() == water_wt ? 0 : a->get_axle_load();
			const uint16 b_axle_load = b->get_waytype() == water_wt ? 0 : b->get_axle_load();
			cmp = a_axle_load - b_axle_load;
			if (cmp == 0) {
				cmp = a->get_weight() - b->get_weight();
			}
			if (cmp != 0) return cmp < 0;
			break;
		}
		case sb_capacity:
			cmp = a->get_total_capacity() - b->get_total_capacity();
			if (cmp == 0) {
				cmp = a->get_overcrowded_capacity() - b->get_overcrowded_capacity();
			}
			if (cmp != 0) return cmp < 0;
			break;
		case sb_min_comfort:
		{
			uint16 a_comfort = 65535;
			uint16 b_comfort = 65535;
			if (a->get_freight_type() == goods_manager_t::passengers) {
				for (uint8 i = 0; i < a->get_number_of_classes(); i++) {
					if (a->get_capacity(i)>0) {
						a_comfort = a->get_comfort(i);
						break;
					}
				}
			}
			if (b->get_freight_type() == goods_manager_t::passengers) {
				for (uint8 i = 0; i < b->get_number_of_classes(); i++) {
					if (b->get_capacity(i) > 0) {
						b_comfort = b->get_comfort(i);
						break;
					}
				}
			}
			cmp = a_comfort - b_comfort;
			if( cmp == 0) {
				cmp = a->get_catering_level() - b->get_catering_level();
			}
			if (cmp != 0) return cmp < 0;
			break;
		}
		default:
		case best:
			break;
	}
	// Sort by:
	//  1. cargo category
	//  2. cargo (if special freight)
	//  3. engine_type
	//  4. speed
	//  5. power
	//  6. intro date
	//  7. name
	cmp = compare_freight(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_capacity(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_engine(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_topspeed(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_power(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_tractive_effort(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = compare_intro_year_month(a, b);
	if (cmp != 0) return cmp < 0;
	cmp = strcmp(translator::translate(a->get_name()), translator::translate(b->get_name()));
	return cmp < 0;
}


static bool compare( const vehicle_desc_t* a, const vehicle_desc_t* b )
{
	return vehicle_builder_t::compare_vehicles( a, b, (vehicle_builder_t::sort_mode_t)tmp_sort_idx );
}


bool vehicle_builder_t::successfully_loaded()
{
	// first: check for bonus tables
	DBG_MESSAGE("vehicle_builder_t::sort_lists()","called");
	for (  int sort_idx = 0; sort_idx < vehicle_builder_t::sb_length; sort_idx++  ) {
		for(  int wt_idx=0;  wt_idx<9;  wt_idx++  ) {
			tmp_sort_idx = sort_idx;
			slist_tpl<vehicle_desc_t*>& typ_liste = typ_fahrzeuge[sort_idx][wt_idx];
			uint count = typ_liste.get_count();
			if (count == 0) {
				continue;
			}
			vehicle_desc_t** const tmp     = new vehicle_desc_t*[count];
			vehicle_desc_t** const tmp_end = tmp + count;
			for(  vehicle_desc_t** tmpptr = tmp;  tmpptr != tmp_end;  tmpptr++  ) {
				*tmpptr = typ_liste.remove_first();
				(*tmpptr)->fix_number_of_classes();
			}

			std::sort(tmp, tmp_end, compare);

			for(  vehicle_desc_t** tmpptr = tmp;  tmpptr != tmp_end;  tmpptr++  ) {
				typ_liste.append(*tmpptr);
			}
			delete [] tmp;
		}
	}
	return true;
}



const vehicle_desc_t *vehicle_builder_t::get_info(const char *name)
{
	return name_fahrzeuge.get(name);
}

slist_tpl<vehicle_desc_t*> const & vehicle_builder_t::get_info(waytype_t typ, uint8 sortkey)
{
	return typ_fahrzeuge[sortkey][GET_WAYTYPE_INDEX(typ)];
}


/**
 * extended search for vehicles for AI
 * checks also timeline and constraints
 * tries to get best with but adds a little random action
 */
const vehicle_desc_t *vehicle_builder_t::vehicle_search( waytype_t wt, const uint16 month_now, const uint32 target_weight, const sint32 target_speed, const goods_desc_t * target_freight, bool include_electric, bool not_obsolete )
{
	if(  (target_freight!=NULL  ||  target_weight!=0)  &&  !typ_fahrzeuge[0][GET_WAYTYPE_INDEX(wt)].empty()  )
	{
		struct best_t {
			uint32 power;
			uint16 payload_per_maintenance;
			long index;
		} best, test;

		best.power = 0;
		best.payload_per_maintenance = 0;
		best.index = -100000;

		const vehicle_desc_t *desc = NULL;
		for (vehicle_desc_t const* const test_desc : typ_fahrzeuge[0][GET_WAYTYPE_INDEX(wt)]) {
			// no constricts allow for rail vehicles concerning following engines
			if(wt==track_wt  &&  !test_desc->can_follow_any()  ) {
				continue;
			}
			// do not buy incomplete vehicles
			if(wt==road_wt && !test_desc->can_lead(NULL)) {
				continue;
			}

			// engine, but not allowed to lead a convoi, or no power at all or no electrics allowed
			if(target_weight) {
				if(test_desc->get_power()==0  ||  !test_desc->can_follow(NULL)  ||  (!include_electric  &&  test_desc->get_engine_type()==vehicle_desc_t::electric) ) {
					continue;
				}
			}

			// check for wegetype/too new
			if(test_desc->get_waytype()!=wt  ||  test_desc->is_future(month_now)  ) {
				continue;
			}

			if(  not_obsolete  &&  test_desc->is_retired(month_now)  ) {
				// not using vintage cars here!
				continue;
			}

			test.power = (test_desc->get_power()*test_desc->get_gear())/64;
			if(target_freight) {
				// this is either a railcar/trailer or a truck/boat/plane
				if(  test_desc->get_total_capacity()==0  ||  !test_desc->get_freight_type()->is_interchangeable(target_freight)  ) {
					continue;
				}

				test.index = -100000;
				test.payload_per_maintenance = test_desc->get_total_capacity() / max(test_desc->get_running_cost(), 1);

				sint32 difference=0;	// smaller is better
				// assign this vehicle if we have not found one yet, or we only found one too weak
				if(  desc!=NULL  ) {
					// is it cheaper to run? (this is most important)
					difference += best.payload_per_maintenance < test.payload_per_maintenance ? -20 : 20;
					if(  target_weight>0  ) {
						// is it strongerer?
						difference += best.power < test.power ? -10 : 10;
					}
					// it is faster? (although we support only up to 120km/h for goods)
					difference += (desc->get_topspeed() < test_desc->get_topspeed())? -10 : 10;
					// it is cheaper? (not so important)
					difference += (desc->get_value() > test_desc->get_value())? -5 : 5;
					// add some malus for obsolete vehicles
					if(test_desc->is_retired(month_now)) {
						difference += 5;
					}
				}
				// ok, final check
				if(  desc==NULL  ||  difference<(int)simrand(25, "vehicle_builder_t::vehicle_search")    ) {
					// then we want this vehicle!
					desc = test_desc;
					best = test;
					DBG_MESSAGE( "vehicle_builder_t::vehicle_search","Found car %s", desc ? desc->get_name() : "null");
				}
			}
			else {
				// engine/tugboat/truck for trailer
				if(  test_desc->get_total_capacity()!=0  ||  !test_desc->can_follow(NULL)  ) {
					continue;
				}
				// finally, we might be able to use this vehicle
				sint32 speed = test_desc->get_topspeed();
				uint32 max_weight = test.power/( (speed*speed)/2500 + 1 );

				// we found a useful engine
				test.index = (test.power*100)/max(test_desc->get_running_cost(), 1) + test_desc->get_topspeed() - (sint16)test_desc->get_weight() - (sint32)(test_desc->get_value()/25000);
				// too slow?
				if(speed < target_speed) {
					test.index -= 250;
				}
				// too weak to to reach full speed?
				if(  max_weight < target_weight+test_desc->get_weight()  ) {
					test.index += max_weight - (sint32)(target_weight+test_desc->get_weight());
				}
				test.index += simrand(100, "vehicle_builder_t::vehicle_search");
				if(  test.index > best.index  ) {
					// then we want this vehicle!
					desc = test_desc;
					best = test;
					DBG_MESSAGE( "vehicle_builder_t::vehicle_search","Found engine %s",desc->get_name());
				}
			}
		}
		if (desc)
		{
			return desc;
		}
	}
	// no vehicle found!
	DBG_MESSAGE( "vehicle_builder_t::vehicle_search()","could not find a suitable vehicle! (speed %i, weight %i)",target_speed,target_weight);
	return NULL;
}



/**
 * extended search for vehicles for replacement on load time
 * tries to get best match (no random action)
 * if prev_desc==NULL, then the convoi must be able to lead a convoi
 */
const vehicle_desc_t *vehicle_builder_t::get_best_matching( waytype_t wt, const uint16 month_now, const uint32 target_weight, const uint32 target_power, const sint32 target_speed, const goods_desc_t * target_freight, bool not_obsolete, const vehicle_desc_t *prev_veh, bool is_last )
{
	const vehicle_desc_t *desc = NULL;
	sint32 desc_index =- 100000;

	if(  !typ_fahrzeuge[0][GET_WAYTYPE_INDEX(wt)].empty()  )
	{
		for(vehicle_desc_t const* const test_desc : typ_fahrzeuge[0][GET_WAYTYPE_INDEX(wt)]) {
			if(target_power>0  &&  test_desc->get_power()==0) {
				continue;
			}

			// will test for first (prev_veh==NULL) or matching following vehicle
			if(!test_desc->can_follow(prev_veh)) {
				continue;
			}

			// not allowed as last vehicle
			if(is_last  &&  test_desc->get_trailer_count()>0  &&  test_desc->get_trailer(0)!=NULL  ) {
				continue;
			}

			// not allowed as non-last vehicle
			if(!is_last  &&  test_desc->get_trailer_count()==1  &&  test_desc->get_trailer(0)==NULL  ) {
				continue;
			}

			// check for waytype too
			if(test_desc->get_waytype()!=wt  ||  test_desc->is_future(month_now)  ) {
				continue;
			}

			// ignore vehicles that need electrification
			if(test_desc->get_power()>0  &&  test_desc->get_engine_type()==vehicle_desc_t::electric) {
				continue;
			}

			// likely tender => replace with some engine ...
			if(target_freight==0  &&  target_weight==0) {
				if(  test_desc->get_total_capacity()!=0  ) {
					continue;
				}
			}

			if(  not_obsolete  &&  test_desc->is_retired(month_now)  ) {
				// not using vintage cars here!
				continue;
			}

			uint32 power = (test_desc->get_power()*test_desc->get_gear())/64;
			if(target_freight)
			{
				// this is either a railcar/trailer or a truck/boat/plane
				if(  test_desc->get_total_capacity()==0  ||  !test_desc->get_freight_type()->is_interchangeable(target_freight)  ) {
					continue;
				}

				sint32 difference=0;	// smaller is better
				// assign this vehicle, if we have none found one yet, or we found only a too week one
				if(  desc!=NULL  )
				{
					// it is cheaper to run? (this is most important)
					difference += (desc->get_total_capacity()*1000)/(1+desc->get_running_cost()) < (test_desc->get_total_capacity()*1000)/(1+test_desc->get_running_cost()) ? -20 : 20;
					if(  target_weight>0  )
					{
						// it is strongere?
						difference += (desc->get_power()*desc->get_gear())/64 < power ? -10 : 10;
					}

					// it is faster? (although we support only up to 120km/h for goods)
					difference += (desc->get_topspeed() < test_desc->get_topspeed())? -10 : 10;
					// it is cheaper? (not so important)
					difference += (desc->get_value() > test_desc->get_value())? -5 : 5;
					// add some malus for obsolete vehicles
					if(test_desc->is_retired(month_now))
					{
						difference += 5;
					}
				}
				// ok, final check
				if(  desc==NULL  ||  difference<12    )
				{
					// then we want this vehicle!
					desc = test_desc;
					DBG_MESSAGE( "vehicle_builder_t::get_best_matching","Found car %s", desc ? desc->get_name() : "null");
				}
			}
			else {
				// finally, we might be able to use this vehicle
				sint32 speed = test_desc->get_topspeed();
				uint32 max_weight = power/( (speed*speed)/2500 + 1 );

				// we found a useful engine
				sint32 current_index = (power * 100) / (1 + test_desc->get_running_cost()) + test_desc->get_topspeed() - (sint16)test_desc->get_weight() - (sint32)(test_desc->get_value()/25000);
				// too slow?
				if(speed < target_speed) {
					current_index -= 250;
				}
				// too weak to to reach full speed?
				if(  max_weight < target_weight+test_desc->get_weight()  ) {
					current_index += max_weight - (sint32)(target_weight+test_desc->get_weight());
				}
				current_index += 50;
				if(  current_index > desc_index  ) {
					// then we want this vehicle!
					desc = test_desc;
					desc_index = current_index;
					DBG_MESSAGE( "vehicle_builder_t::get_best_matching","Found engine %s",desc->get_name());
				}
			}
		}
	}
	// no vehicle found!
	if(  desc==NULL  ) {
		DBG_MESSAGE( "vehicle_builder_t::get_best_matching()","could not find a suitable vehicle! (speed %i, weight %i)",target_speed,target_weight);
	}
	return desc;
}
