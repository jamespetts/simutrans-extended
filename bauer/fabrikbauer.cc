/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <algorithm>
#include "../simdebug.h"
#include "fabrikbauer.h"
#include "hausbauer.h"
#include "../simworld.h"
#include "../simintr.h"
#include "../simfab.h"
#include "../simmesg.h"
#include "../utils/simrandom.h"
#include "../simcity.h"
#include "../simhalt.h"
#include "../player/simplay.h"

#include "../boden/grund.h"
#include "../boden/wege/runway.h" // for avoiding building next to runways

#include "../dataobj/settings.h"
#include "../dataobj/environment.h"
#include "../network/pakset_info.h"
#include "../dataobj/translator.h"

#include "../tpl/vector_tpl.h"
#include "../tpl/weighted_vector_tpl.h"
#include "../tpl/array_tpl.h"
#include "../finder/building_placefinder.h"

#include "../utils/cbuffer_t.h"

#include "../descriptor/objversion.h"

#include "../gui/minimap.h" // to update map after construction of new industry

#include "../ifc/simtestdriver.h" // for verifying suitable locations for water factories
#include "../dataobj/route.h" // for verifying suitable locations for water factories

karte_ptr_t factory_builder_t::welt;

/// Default factory spacing
static int max_factory_spacing_general = 40;

/// all factories and their exclusion areas
static array_tpl<uint8> fab_map;

/// width of the factory map
static sint32 fab_map_w=0;


/**
 * Marks factories and their exclusion region in the position map.
 */
static void add_factory_to_fab_map(karte_t const* const welt, fabrik_t const* const fab)
{
	koord3d      const& pos     = fab->get_pos();
	sint16       const  spacing = welt->get_settings().get_min_factory_spacing();
	building_desc_t const& bdsc  = *fab->get_desc()->get_building();
	uint8        const  rotate  = fab->get_rotate();
	sint16       const  start_y = max(0, pos.y - spacing);
	sint16       const  start_x = max(0, pos.x - spacing);
	sint16       const  end_y   = min(welt->get_size().y - 1, pos.y + bdsc.get_y(rotate) + spacing);
	sint16       const  end_x   = min(welt->get_size().x - 1, pos.x + bdsc.get_x(rotate) + spacing);
	for (sint16 y = start_y; y < end_y; ++y) {
		for (sint16 x = start_x; x < end_x; ++x) {
			fab_map[fab_map_w * y + x / 8] |= 1 << (x % 8);
		}
	}
}


/**
 * Creates map with all factories and exclusion area.
 */
void init_fab_map( karte_t *welt )
{
	fab_map_w = ((welt->get_size().x+7)/8);
	fab_map.resize( fab_map_w*welt->get_size().y );
	for( int i=0;  i<fab_map_w*welt->get_size().y;  i++ ) {
		fab_map[i] = 0;
	}
	for(fabrik_t* const f : welt->get_fab_list()) {
		add_factory_to_fab_map(welt, f);
	}
	if(  welt->get_settings().get_max_factory_spacing_percent()  ) {
		max_factory_spacing_general = (welt->get_size_max() * welt->get_settings().get_max_factory_spacing_percent()) / 100l;
	}
	else {
		max_factory_spacing_general = welt->get_settings().get_max_factory_spacing();
	}
}


/**
 * @param x,y world position, needs to be valid coordinates
 * @returns true, if factory coordinate
 */
inline bool is_factory_at(sint16 x, sint16 y)
{
	uint32 idx = (fab_map_w*y)+(x/8);
	return idx < fab_map.get_count()  &&  (fab_map[idx]&(1<<(x%8)))!=0;
}


void factory_builder_t::new_world()
{
	init_fab_map( welt );
}


/**
 * Searches for a suitable building site using find_place().
 * The site is chosen so that there is a street next to the building site.
 */
class factory_site_searcher_t: public building_placefinder_t  {

	factory_desc_t::site_t site;
public:
	factory_site_searcher_t(karte_t* welt, factory_desc_t::site_t site_) : building_placefinder_t(welt), site(site_) {}

	bool is_area_ok(koord pos, sint16 w, sint16 h, climate_bits cl, uint16 allowed_regions) const OVERRIDE
	{
		if (  site != factory_desc_t::Water  ) {
			// If this is not a water site factory, then
			if ( !building_placefinder_t::is_area_ok(pos, w, h, cl, allowed_regions)  ) {
				// We need a clear space to build, first of all
				return false;
			}
		}

		// Whether we've found a suitable road, shore, or river.
		// Consider counting the number we find instead.
		bool road_found = false;
		if (site != factory_desc_t::City) {
			// Don't look for the road if we don't care.
			road_found = true;
		}
		bool shore_found = false;
		if (site != factory_desc_t::shore && site != factory_desc_t::shore_city) {
			// Don't look for the shore if we don't care.
			shore_found = true;
		}
		bool river_found = false;
		if (site != factory_desc_t::river && site != factory_desc_t::river_city) {
			// Don't look for the river if we don't care.
			river_found = true;
		}
		if(  welt->get_settings().get_river_number() <= 0  ) {
			// On a map with no rivers, don't restrict to spaces near rivers
			river_found = true;
		}

		// For forests, count the trees.
		uint16 mincond = 0;
		uint16 condmet = 0;
		if (!welt->get_settings().get_no_trees() && site == factory_desc_t::forest) {
			// check for trees unless the map was generated without trees
			mincond = w*h+w+h; // at least w*h+w+h trees, i.e. few tiles with more than one tree
		}

		// Keep away from the edge of the map.
		sint16 edge_avoidance = stadt_t::get_edge_avoidance();
		if ( pos.x < edge_avoidance || pos.x + w > welt->get_size().x - edge_avoidance) {
			// Too close to the map edge left or right
			return false;
		} else if (pos.y < edge_avoidance || pos.y + h > welt->get_size().y - edge_avoidance) {
			// Too close to the map edge top or bottom
			return false;
		}

		// needs to run one tile wider than the factory on all sides
		for (sint16 y = -1;  y <= h; y++) {
			for (sint16 x = -1; x <= w; x++) {
				koord k(pos + koord(x,y));
				grund_t *gr = welt->lookup_kartenboden(k);
				if (!gr) {
					// We want to keep the factory at least 1 away from the edge of the map.
					return false;
				}
				// Do not build in the exclusion zone of another factory.
				if( is_factory_at(k.x, k.y)  ) {
					return false;
				}
				if(  0 <= x  &&  x < w  &&  0 <= y  &&  y < h  ) {
					// Inside the target for factory building itself.
					// For water factories, check for water (no shore in sight!)
					if(site==factory_desc_t::Water) {
						if( !gr->is_water()  ||  gr->get_grund_hang()!=slope_t::flat) {
							return false;
						}
					}
					// Don't build under bridges, powerlines, etc.
					// Is there something top like elevated monorails?
					if(  gr->get_leitung()!=NULL  ||  welt->lookup(gr->get_pos()+koord3d(0,0,1)  )!=NULL) {
						// something on top (monorail or power lines)
						// Note: consider loosening this requirement to allow building under very tall elevateds
						return false;
					}
					// count the trees on the tiles the factory will be built on top of, for forest factories
					if(  site==factory_desc_t::forest  &&  condmet < mincond  ) {
						for(uint8 i=0; i< gr->get_top(); i++) {
							if (gr->obj_bei(i)->get_typ() == obj_t::baum) {
								condmet++;
							}
						}
					}
				}
				else if ( (-1==x  ||  x==w)  &&  (-1==y  ||  y==h)  ) {
					// corner tile
					// Stay away from the corner of runways (yes, even for water industries):
					runway_t* rw = (runway_t*)gr->get_weg(air_wt);
			    if (rw && rw->get_desc()->get_styp() == type_runway && !(rw->get_owner_nr() == PLAYER_UNOWNED && rw->is_degraded() && rw->get_max_speed() == 0)) {
						// Do not care about degraded, unowned runways, but stay away from the side of others
						return false;
					}
					// we do NOT want to count a corner tile match as a match for road, shore, or river!
					continue;
				}
				else if (  -1==x || x==w || -1==y || y==h  ) {
					// border tile, and not corner (we checked corners first)
					// Stay away from the side of runways (yes, even for water industries):
					runway_t* rw = (runway_t*)gr->get_weg(air_wt);
			    if (rw && rw->get_desc()->get_styp() == type_runway && !(rw->get_owner_nr() == PLAYER_UNOWNED && rw->is_degraded() && rw->get_max_speed() == 0)) {
						// Do not care about degraded, unowned runways, but stay away from the side of others
						return false;
					}
					// check for road, shore, river
					// short-circuit if we have already found road, shore, river, or don't care
					road_found = road_found || gr->hat_weg(road_wt);
					shore_found = shore_found || welt->get_climate(k) == water_climate;
					if (!river_found) {
						weg_t* river = gr->get_weg(water_wt);
						river_found = river_found || (river  &&  river->get_desc()->get_styp()==type_river);
					}
				}
			}
		}
		// For a city building we require a pre-existing road,
		// but for river_city and shore_city we don't, we'll build the road afterwards.
		switch (site) {
			case factory_desc_t::City:
				return road_found;
				break;
			case factory_desc_t::shore:
			case factory_desc_t::shore_city:
				return shore_found;
				break;
			case factory_desc_t::river:
			case factory_desc_t::river_city:
				return river_found;
				break;
			case factory_desc_t::forest:
				// Enough trees?
				return condmet >= mincond;
				break;
			case factory_desc_t::Land:
			case factory_desc_t::Water:
			default:
				return true;
				break;
		}
		return true;
	}
};


stringhashtable_tpl<const factory_desc_t *, N_BAGS_MEDIUM> factory_builder_t::desc_table;
stringhashtable_tpl<factory_desc_t *, N_BAGS_MEDIUM> factory_builder_t::modifiable_table;


/**
 * Compares factory descriptors by their name.
 * Used in get_random_consumer().
 * @return true, if @p a < @p b, false otherwise.
 */
static bool compare_fabrik_desc(const factory_desc_t* a, const factory_desc_t* b)
{
	const int diff = strcmp( a->get_name(), b->get_name() );
	return diff < 0;
}


// returns a random consumer
const factory_desc_t *factory_builder_t::get_random_consumer(bool electric, climate_bits cl, uint16 allowed_regions, uint16 timeline, const goods_desc_t* input, bool force_consumer_only)
{
	// get a random city factory
	weighted_vector_tpl<const factory_desc_t *> consumer;

	for(auto const& i : desc_table) {
		factory_desc_t const* const current = i.value;
		// only insert end consumers, if applicable, with the requested input goods.
		if (  (current->is_consumer_only() || !force_consumer_only ) &&
			!current->is_producer_only() &&
			current->get_building()->is_allowed_climate_bits(cl)  &&
			current->get_building()->is_allowed_region_bits(allowed_regions) &&
			(electric ^ !current->is_electricity_producer())  &&
			current->get_building()->is_available(timeline)  &&
			(input == NULL || current->get_accepts_these_goods(input))
			)
		{
				consumer.insert_unique_ordered(current, current->get_distribution_weight(), compare_fabrik_desc);
		}
	}
	// no consumer installed?
	if (consumer.empty()) {
		if (input != NULL) {
			DBG_MESSAGE("factory_builder_t::get_random_consumer()", "No suitable consumer found for input %s", input->get_name());
		}
		else {
			DBG_MESSAGE("factory_builder_t::get_random_consumer()", "No suitable consumer found");
		}

		return NULL;
	}
	// now find a random one
	factory_desc_t const* const fd = pick_any_weighted(consumer);
	DBG_MESSAGE("factory_builder_t::get_random_consumer()", "consumer %s found.", fd->get_name());
	return fd;
}

bool factory_builder_t::is_final_good(bool electric, climate_bits cl, uint16 allowed_regions, uint16 timeline, const goods_desc_t* good) {


	for (auto const& i : desc_table) {
		factory_desc_t const* const current = i.value;
		// only insert end consumers, if applicable, with the requested input goods.
		if ((!current->is_consumer_only()) &&
			current->get_building()->is_allowed_climate_bits(cl) &&
			current->get_building()->is_allowed_region_bits(allowed_regions) &&
			(electric ^ !current->is_electricity_producer()) &&
			current->get_building()->is_available(timeline) &&
			(good == NULL || current->get_accepts_these_goods(good))
			)
		{
			return false;
		}
	}
	return true;
}


const factory_desc_t *factory_builder_t::get_desc(const char *factory_name)
{
	return desc_table.get(factory_name);
}


void factory_builder_t::register_desc(factory_desc_t *desc)
{
	uint16 p=desc->get_productivity();
	if(p&0x8000) {
		koord k=desc->get_building()->get_size();

		// to be compatible with old factories, since new code only steps once per factory, not per tile
		desc->set_productivity( (p&0x7FFF)*k.x*k.y );
DBG_DEBUG("factory_builder_t::register_desc()","Correction for old factory: Increase production from %i by %i",p&0x7FFF,k.x*k.y);
	}
	if(  const factory_desc_t *old_desc = desc_table.remove(desc->get_name())  ) {
		dbg->doubled( "factory", desc->get_name() );
		delete old_desc;
	}
	desc_table.put(desc->get_name(), desc);
	modifiable_table.put(desc->get_name(), desc);
}


bool factory_builder_t::successfully_loaded()
{
	for(auto const& i : desc_table) {
		factory_desc_t const* const current = i.value;
		if(  field_group_desc_t * fg = const_cast<field_group_desc_t *>(current->get_field_group())  ) {
			// initialize weighted vector for the field class indices
			fg->init_field_class_indices();
		}
		// check for crossconnects
		for(  int j=0;  j < current->get_supplier_count();  j++  ) {
			weighted_vector_tpl<const factory_desc_t *>producer;
			find_producer( producer, current->get_supplier(j)->get_input_type(), 0 );
			if(  producer.is_contained(current)  ) {
				// must not happen else
				dbg->fatal( "factory_builder_t::build_link()", "Factory %s output %s cannot be its own input!", i.key, current->get_supplier(j)->get_input_type()->get_name() );
			}
		}
		checksum_t *chk = new checksum_t();
		current->calc_checksum(chk);
		pakset_info_t::append(current->get_name(), obj_factory, chk);
	}
	return true;
}


int factory_builder_t::count_producers(const goods_desc_t *ware, uint16 timeline)
{
	int count=0;

	// iterate over all factories and check if they produce this good...
	for(auto const& t : desc_table) {
		factory_desc_t const* const tmp = t.value;
		for (uint i = 0; i < tmp->get_product_count(); i++) {
			const factory_product_desc_t *product = tmp->get_product(i);
			if(  product->get_output_type()==ware  &&  tmp->get_distribution_weight()>0  &&  tmp->get_building()->is_available(timeline)  ) {
				count++;
			}
		}
	}
DBG_MESSAGE("factory_builder_t::count_producers()","%i producer for good '%s' found.", count, translator::translate(ware->get_name()));
	return count;
}


/*
 * Finds a random producer producing @p ware.
 * @param timeline the current time(months)
 */
void factory_builder_t::find_producer(weighted_vector_tpl<const factory_desc_t *> &producer, const goods_desc_t *ware, uint16 timeline )
{
	// find all producers
	producer.clear();
	for(auto const t : desc_table) {
		factory_desc_t const* const tmp = t.value;
		if (  tmp->get_distribution_weight()>0  &&  tmp->get_building()->is_available(timeline)  ) {
			for(  uint i=0; i<tmp->get_product_count();  i++  ) {
				const factory_product_desc_t *product = tmp->get_product(i);
				if(  product->get_output_type()==ware  ) {
					producer.insert_unique_ordered(tmp, tmp->get_distribution_weight(), compare_fabrik_desc);
					break;
				}
			}
		}
	}

	// no producer installed?
	if(  producer.empty()  ) {
		dbg->error("factory_builder_t::find_producer()", "no producer for good '%s' was found.", translator::translate(ware->get_name()));
	}
	DBG_MESSAGE("factory_builder_t::find_producer()", "%i producer for good '%s' found.", producer.get_count(), translator::translate(ware->get_name()) );
}


bool factory_builder_t::check_construction_site(koord pos, koord size, factory_desc_t::site_t site, bool is_factory, climate_bits cl, uint16 regions_allowed)
{
	// We used to check is_factory here, but the code ended up being identical.
	// This gets called for factories and for attractions.
	// Attractions will get called with the "Land" site type from distribute_attractions
	// (though consider implementing whale-watching, etc.?)
	return factory_site_searcher_t(welt, site).is_area_ok(pos, size.x, size.y, cl, regions_allowed);
}


/**
 * This can only drive in the ocean.  This version triggers the jps behavior intentionally.
 */
class ocean_test_driver_t : public test_driver_t
{
public:
	bool check_next_tile(const grund_t* gr) const OVERRIDE { return gr->is_water(); };
	ribi_t::ribi get_ribi(const grund_t* gr) const OVERRIDE { return gr->get_weg_ribi_unmasked(water_wt); };
	waytype_t get_waytype() const OVERRIDE { return water_wt; }; // To trigger jps "jump" behavior; otherwise use invalid_wt
	int get_cost(const grund_t *, const sint32, ribi_t::ribi) OVERRIDE { return 1; };
	bool is_target(const grund_t *, const grund_t *) OVERRIDE { return false; }; // unused
};


/**
 * Is there a way to take a ship from start_pos to end_pos?
 * For computational reasons, this will often report "false" when there actually is a way,
 * when the routefinder can't find it.
 * However, it should never report "true" when there isn't a way.
 */
bool factory_builder_t::has_ocean_route(const koord start_pos, const koord end_pos) {
		const grund_t* start_gr = welt->lookup_kartenboden(start_pos);
		const grund_t* end_gr = welt->lookup_kartenboden(end_pos);
		if (  !start_gr || !start_gr->is_water() || !end_gr || !end_gr->is_water()  ) {
			return false;
		}
		const koord3d start_pos_3d = start_gr->get_pos();
		const koord3d end_pos_3d = end_gr->get_pos();

		route_t water_route;
		ocean_test_driver_t ocean_test_driver;

		// Use the special ocean route finder.  This only works if the start and end are both water tiles.
		// It attempts to find a route in a straight line, and uses the regular route finder to go around "short" land gaps.
		route_t::route_result_t result = water_route.calc_ocean_route(welt, start_pos_3d, end_pos_3d, &ocean_test_driver);

		// Succeed if the result was a valid route.  Otherwise return false.
		return (result == route_t::valid_route);
}


koord3d factory_builder_t::find_random_construction_site(koord pos, int radius, koord size, factory_desc_t::site_t site, const building_desc_t *desc, bool ignore_climates, uint32 max_iterations)
{
	bool is_factory = desc->get_type()==building_desc_t::factory;

	// These variables are used only for water factories such as fisheries
	bool water = site == factory_desc_t::Water;
    bool found_water = false;
	koord water_start_pos = koord::invalid;

	if(water) {
		// to ensure at least 3x3 water around (maybe this should be the station catchment area+1?)
		size += koord(6,6);

		// We want to make sure there is a WATER path from the water factory being built (e.g. fishery)
		// to its destination factory already existing at pos (e.g. fishing port)
		// This requires that the already-existing destination factory has a water tile adjacent to it.  So find one.
        //
        // This works great for fisheries, but not for oil rigs.
        // To allow oil rigs to work, we don't do the water path check if the destination factory isn't water-adjacent.
		for(int i = 0; i < 8; i++) {
			water_start_pos = pos + koord::neighbours[i];
			const grund_t* water_start_gr = welt->lookup_kartenboden(water_start_pos);
			if (water_start_gr && water_start_gr->is_water()) {
				// Found a water tile.
				found_water = true;
				break;
			}
		}
	}

	climate_bits climates = !ignore_climates ? desc->get_allowed_climate_bits() : ALL_CLIMATES;

	koord central_pos = pos;
	// If the radius is VERY LARGE, then this code can be quite slow, and may test the same
	// locations multiple times, most of which are not actually on the map.  Reduce chances of this.
	koord grid_size = welt->get_size();
	if (     (sint32) central_pos.x - radius < 0 && (sint32) central_pos.x + radius >= grid_size.x
	      && (sint32) central_pos.y - radius < 0 && (sint32) central_pos.y + radius >= grid_size.y )
	{
		// We're searching the whole map.  Pick a tile in the center (rounded right/down)
		// and shrink the radius to cover the whole map but not much more.
		central_pos.x = grid_size.x / 2;
		central_pos.y = grid_size.y / 2;
		radius = welt->get_size_max() / 2;
	}

	uint32 diam   = 2*radius + 1;
	uint32 area   = diam * diam;
	uint32 index  = simrand(area, "find_random_construction_site");
	koord k;

	max_iterations = min( area/(size.x*size.y)+1, max_iterations );
	const uint32 a = diam+1;
	const uint32 c = 37; // very unlikely to have this as a factor in somewhere ...

	// in order to stop on the first occurence, one has to iterate over all tiles in a reproducable but random enough manner
	for(  uint32 i = 0;  i<max_iterations; i++,  index = (a*index+c) % area  ) {

		// so it is guaranteed that the iteration hits all tiles and does not repeat itself
		k = koord( central_pos.x - radius + (index % diam), central_pos.y - radius + (index / diam) );

		if (!welt->is_within_limits(k)) {
			// Fast path if we're off the map, which can be nearly 3/4 of the time
			continue;
		}

		// check place (it will actually check an grosse.x/y size rectangle, so we can iterate over less tiles)
		if(  factory_builder_t::check_construction_site(k, size, site, is_factory, climates, desc->get_allowed_region_bits())  ) {
			// then accept first hit
			if (site != factory_desc_t::Water && site != factory_desc_t::Land) {
				DBG_MESSAGE("factory_builder_t::find_random_construction_site","Found spot for %d at %s / %d\n", site, k.get_str(), max_iterations);
			}
			if (!water || !found_water) {
				// Success, return
				return welt->lookup_kartenboden(k)->get_pos();
			}
			// Special checks for water factories connecting to coastal factories
			if (water && found_water) {
				// Make sure there is a OCEAN path from the water factory (e.g. fishery) to its destination factory (e.g. fishing port)
				// Find the 3d location of our proposed water factory tile (remember the 3-tile offset)
				koord water_end_pos = k + koord(3,3);
				if (  has_ocean_route(water_start_pos, water_end_pos)  ) {
					return welt->lookup_kartenboden(water_end_pos)->get_pos();
				}
			}
		}
	}
	// nothing found
	if (site != factory_desc_t::Land) {
		DBG_MESSAGE("factory_builder_t::find_random_construction_site","No spot found for location %d of %s near %s / %d\n", site, desc->get_name(), pos.get_str(), max_iterations);
	}
	return koord3d::invalid;
}


// Create a certain number of tourist attractions
void factory_builder_t::distribute_attractions(int max_number)
{
	// current number of tourist attractions constructed
	int current_number=0;

	// select without timeline disappearing dates
	if(hausbauer_t::get_random_attraction(welt->get_timeline_year_month(),true,temperate_climate)==NULL) {
		// nothing at all?
		return;
	}

	// very fast, so we do not bother updating progress bar
	dbg->message("factory_builder_t::distribute_attractions()", "Distributing %i tourist attractions", max_number);

	int retrys = max_number*4;
	while(current_number<max_number  &&  retrys-->0) {
		koord3d pos=koord3d( koord::koord_random(welt->get_size().x,welt->get_size().y),1);
		const building_desc_t *attraction=hausbauer_t::get_random_attraction(welt->get_timeline_year_month(),true,(climate)simrand((int)arctic_climate+1, "void factory_builder_t::distribute_attractions"));

		// no attractions for that climate or too new
		if(attraction==NULL  ||  (welt->use_timeline()  &&  attraction->get_intro_year_month()>welt->get_current_month()) ) {
			continue;
		}

		int	rotation=simrand(attraction->get_all_layouts()-1, "void factory_builder_t::distribute_attractions");
		pos = find_random_construction_site(pos.get_2d(), 20, attraction->get_size(rotation), factory_desc_t::Land, attraction, false, 0x0FFFFFFF);	// so far -> land only
		if(welt->lookup(pos)) {
			// space found, build attraction
			gebaeude_t* gb = hausbauer_t::build(welt->get_public_player(), pos, rotation, attraction);
			current_number ++;
			retrys = max_number*4;
			const planquadrat_t* tile = welt->access(gb->get_pos().get_2d());
			stadt_t* city = tile ? tile->get_city() : NULL;
			if(city)
			{
				city->add_building_to_list(gb->access_first_tile());
				gb->set_stadt(city);
			}
			else
			{
				welt->add_building_to_world_list(gb->access_first_tile());
				if (welt->get_settings().get_auto_connect_industries_and_attractions_by_road())
				{
					gb->connect_by_road_to_nearest_city();
				}
			}
		}

	}
	// update an open map
	minimap_t::get_instance()->calc_map_size();
}

/**
 * Compares cities by their distance to an origin.
 */
class RelativeDistanceOrdering
{
private:
	const koord m_origin;

public:
	RelativeDistanceOrdering(const koord& origin)
		: m_origin(origin)
	{ /* nothing */ }

	/// @returns true if @p a is closer to the origin than @p b, otherwise false.
	bool operator()(const stadt_t *a, const stadt_t *b) const
	{
		return koord_distance(m_origin, a->get_pos()) < koord_distance(m_origin, b->get_pos());
	}
};


/*
 * Builds a single new factory.
 */
fabrik_t* factory_builder_t::build_factory(koord3d* parent, const factory_desc_t* info, sint32 initial_prod_base, int rotate, koord3d pos, player_t* owner)
{
	fabrik_t * fab = new fabrik_t(pos, owner, info, initial_prod_base);

	if(welt->get_settings().using_fab_contracts()){
		for(auto &output : fab->get_output()){
			output.set_using_contracts();
			output.reset_total_contracts();
		}
		for(auto &input : fab->get_input()){
			input.reset_total_contracts();
		}
	}

	// now build factory
	fab->build(rotate, true /*add fields*/, initial_prod_base != -1 /* force initial prodbase ? */);
	welt->add_fab(fab);
	add_factory_to_fab_map(welt, fab);

	// TODO: Consider adding road connexions here.

	// We need to adjust the jobs/visitor demand based on the extent to which the range has altered base production.
	// The relevant ratio is fab->prodbase : fab->get_desc()->get_productivity().

	const uint32 percentage = (fab->get_base_production() * 100) / max(1, fab->get_desc()->get_productivity());
	if (percentage > 100)
	{
		fab->get_building()->set_adjusted_jobs((fab->get_building()->get_adjusted_jobs() * percentage) / 100);
		fab->get_building()->set_adjusted_visitor_demand((fab->get_building()->get_adjusted_visitor_demand() * percentage) / 100);
		fab->get_building()->set_adjusted_mail_demand((fab->get_building()->get_adjusted_mail_demand() * percentage) / 100);
	}

	// Update local roads
	fab->mark_connected_roads(false);

	// Add the factory to the world list
	fab->add_to_world_list();

	// Adjust the actual industry density
	//welt->increase_actual_industry_density(100 / info->get_distribution_weight());
	if (fab->get_desc()->is_consumer_only()) {
		welt->increase_actual_industry_density(100 / info->get_distribution_weight());
	}
	if(parent) {
		fab->add_consumer(parent->get_2d());
	}

	// make all water station
	if(info->get_placement() == factory_desc_t::Water) {
		const building_desc_t *desc = info->get_building();
		koord dim = desc->get_size(rotate);

		// create water halt
		halthandle_t halt = haltestelle_t::create(pos.get_2d(), welt->get_public_player());
		if(halt.is_bound()) {

			// add all other tiles of the factory to the halt
			for(  int x=pos.x;  x<pos.x+dim.x;  x++  ) {
				for(  int y=pos.y;  y<pos.y+dim.y;  y++  ) {
					if(welt->is_within_limits(x,y)) {
						grund_t *gr = welt->lookup_kartenboden(x,y);
						// build the halt on factory tiles on open water only,
						// since the halt can't be removed otherwise
						if(gr->is_water()  &&  !(gr->hat_weg(water_wt))  &&  gr->find<gebaeude_t>()) {
							halt->add_grund( gr );
						}
					}
				}
			}
			halt->set_name( translator::translate(info->get_name()) );
			halt->recalc_station_type();
			// Must recalc nearby halts after the halt is set up
			fab->recalc_nearby_halts();
		}
	}
	else {
		// Must recalc nearby halts after the halt is set up
		fab->recalc_nearby_halts();
	}

	// This must be done here because the building is not valid on generation, so setting the building's
	// jobs, population and mail figures based on the factory's cannot be done.
	fab->update_scaled_pax_demand();
	fab->update_scaled_mail_demand();
	return fab;
}


// Check if we have to rotate the factories before building this tree
bool factory_builder_t::can_factory_tree_rotate( const factory_desc_t *desc )
{
	// we are finished: we cannot rotate
	if(!desc->get_building()->can_rotate()) {
		return false;
	}

	// now check all suppliers if they can rotate
	for(  int i=0;  i<desc->get_supplier_count();  i++   ) {

		const goods_desc_t *ware = desc->get_supplier(i)->get_input_type();

		// unfortunately, for every for iteration we have to check all factories ...
		for(auto const& t : desc_table) {
			factory_desc_t const* const tmp = t.value;
			// now check if we produce this good...
			for (uint i = 0; i < tmp->get_product_count(); i++) {
				if(tmp->get_product(i)->get_output_type()==ware  &&  tmp->get_distribution_weight()>0) {

					if(!can_factory_tree_rotate( tmp )) {
						return false;
					}
					// check next factory
					break;
				}
			}
		}
	}
	// all good -> true;
	return true;
}


/*
 * Builds a new full chain of factories. Precondition before calling this function:
 * @p pos is suitable for factory construction and number of chains
 * is the maximum number of good types for which suppliers chains are built
 * (meaning there are no unfinished factory chains).
 * number_of_chains < 0 means make ALL the chains.
 */
int factory_builder_t::build_link(koord3d* parent, const factory_desc_t* info, sint32 initial_prod_base, int rotate, koord3d* pos, player_t* player, int number_of_chains, bool ignore_climates)
{
	int n = 1;
	int org_rotation = -1;

	if(info==NULL) {
		// no industry found
		return 0;
	}

	factory_desc_t::site_t site = info->get_placement();

	// no cities at all?
	if ((site == factory_desc_t::City || site == factory_desc_t::shore_city || site == factory_desc_t::river_city) &&  welt->get_cities().empty()) {
		return 0;
	}

	// if a factory is not rotate-able, rotate the world until we can save it
	if(welt->cannot_save()  &&  parent==NULL  &&  !can_factory_tree_rotate(info)  ) {
		org_rotation = welt->get_settings().get_rotation();
		for(  int i=0;  i<3  &&  welt->cannot_save();  i++  ) {
			pos->rotate90( welt->get_size().y-info->get_building()->get_y(rotate) );
			welt->rotate90();
		}
		assert( !welt->cannot_save() );
	}

	// Industries in town needs different place search
	if (site == factory_desc_t::City || site == factory_desc_t::shore_city || site == factory_desc_t::river_city) {

		koord size=info->get_building()->get_size(0);

		// build consumer (factory) in town
		stadt_t *city = welt->find_nearest_city(pos->get_2d());

		const climate_bits cl = ignore_climates ? ALL_CLIMATES : info->get_building()->get_allowed_climate_bits();
		const uint16 regions_allowed = info->get_building()->get_allowed_region_bits();

		/* Three variants:
		 * A:
		 * A building site, preferably close to the town hall with a street next to it.
		 * This could be a temporary problem, if a city has no such site and the search
		 * continues to the next city.
		 * Otherwise seems to me the most realistic.
		 */
		bool is_rotate=info->get_building()->get_all_layouts()>1  &&  size.x!=size.y  &&  info->get_building()->can_rotate();
		// first try with standard orientation
		koord k = factory_site_searcher_t(welt, site).find_place(city->get_pos(), size.x, size.y, cl, regions_allowed);

		// second try: rotated
		koord k1 = koord::invalid;
		if (is_rotate  &&  (k == koord::invalid  ||  simrand(256, " factory_builder_t::build_link")<128)) {
			k1 = factory_site_searcher_t(welt, site).find_place(city->get_pos(), size.y, size.x, cl, regions_allowed);
		}

                int streetdir = 0;
                if (size.x == 1 && size.y == 1) {
                  static int const neighbours_to_senw[] = { 0x0c, 0x08, 0x09, 0x01, 0x03, 0x02, 0x06, 0x04 };
                  static int const neighbours_to_diag[] = { 4, 8, 1, 8, 1, 2, 4, 2 };
                  for ( int i = 1;  i < 8;  i+=2  ) {
                    grund_t *gr2 = welt->lookup_kartenboden(k + koord::neighbours[i]);
                    if ( gr2  &&  gr2->get_weg_hang() == gr2->get_grund_hang()  &&  gr2->get_weg(road_wt) != NULL  ) {
                      // update directions - note this is SENW, conversion from neighbours to SENW is
                      // neighbours SENW   Bits in building_layout
                      // 3          0      0x01
                      // 5          1      0x02
                      // 7          2      0x04
                      // 1          3      0x08
                      streetdir |= neighbours_to_senw[i];
                    }
                  }
                  if (streetdir == 0) { // No adjacent streets; check diagonally
                    for(  int i = 0;  i < 8;  i+=2  ) {
                      grund_t *gr2 = welt->lookup_kartenboden(k + koord::neighbours[i]);
                      if(  gr2  &&  gr2->get_weg_hang() == gr2->get_grund_hang()  &&  gr2->get_weg(road_wt) != NULL  ) {
                        int ribi_ns = ((int)gr2->get_weg_ribi_unmasked(road_wt) & 0x05) ? 1 : 0;
                        streetdir |= neighbours_to_diag[ i + ribi_ns ];
                      }
                    }
                  }
                }

                if (streetdir == 0) { // No adjacent streets; choose random rotation
                  rotate = simrand( info->get_building()->get_all_layouts(), " factory_builder_t::build_link" );
                } else {
                  // Copied from simtool.cc, this converts senw to desired rotations.
                  static int const building_layout[] = { 0, 0, 1, 4, 2, 0, 5, 1, 3, 7, 1, 0, 6, 3, 2, 0 };
                  rotate = building_layout[streetdir];
                }

		if (k1 == koord::invalid) {
			if (size.x != size.y) {
				rotate &= 2; // rotation must be even number
			}
		}
		else {
			k = k1;
			rotate |= 1; // rotation must be odd number
		}

		if (!info->get_building()->can_rotate()) {
			rotate = 0;
		}

		INT_CHECK( "fabrikbauer 588" );

		/* B:
		 * Also good.  The final factories stand possibly somewhat outside of the city however not far away.
		 * (does not obey climates though!)
		 */
#if 0
		k = find_random_construction_site(welt, welt->lookup(city->get_pos())->get_boden()->get_pos(), 3, land_bau.dim).get_2d();
#endif

		/* C:
		 * A building site, as near as possible to the city hall.
		 *  If several final factories land in one city, they are often
		 * often hidden behind a row of houses, cut off from roads.
		 */
#if 0
		k = building_placefinder_t(welt).find_place(city->get_pos(), land_bau.dim.x, land_bau.dim.y, info->get_building()->get_allowed_climate_bits(), &is_rotate);
#endif

		if(k != koord::invalid) {
			*pos = welt->lookup_kartenboden(k)->get_pos();
		}
		else {
			return 0;
		}
	}

	DBG_MESSAGE("factory_builder_t::build_link","Construction of %s at (%i,%i).",info->get_name(),pos->x,pos->y);
	INT_CHECK("fabrikbauer 594");

	const fabrik_t *our_fab=build_factory(parent, info, initial_prod_base, rotate, *pos, player);

	INT_CHECK("fabrikbauer 596");

	// now build supply chains for all products (if number_of_chains<0) or some (if it's 1 or more)
	for(int i=0; i<info->get_supplier_count()  &&  (number_of_chains<0 || i<number_of_chains); i++) {
		n += build_chain_link( our_fab, info, i, player);
	}

	// everything built -> update map if needed
	if(parent==NULL) {
		DBG_MESSAGE("factory_builder_t::build_link()","update karte");

		// update the map if needed
		minimap_t::get_instance()->calc_map_size();

		INT_CHECK( "fabrikbauer 730" );

		// must rotate back?
		if(org_rotation>=0) {
			for (int i = 0; i < 4 && welt->get_settings().get_rotation() != org_rotation; ++i) {
				pos->rotate90( welt->get_size().y-1 );
				welt->rotate90();
			}
			welt->update_map();
		}
	}

	return n;
}


int factory_builder_t::build_chain_link(const fabrik_t* origin_fab, const factory_desc_t* info, int supplier_nr, player_t* player, bool no_new_industries)
{
	int new_fabs_built_count = 0;	// number of additional factories
	/* first we try to connect to existing factories and will do some
	 * cross-connect (if wanted)
	 * We must take care to add capacity for cross-connected factories!
	 */
	const factory_supplier_desc_t *supplier = info->get_supplier(supplier_nr);
	const goods_desc_t *ware = supplier->get_input_type();
	const int producer_count=count_producers( ware, welt->get_timeline_year_month() );

	if(producer_count==0) {
		dbg->error("factory_builder_t::build_chain_link()","No producer for %s found, chain incomplete!",ware->get_name() );
		return 0;
	}

	// how much do we need?
	sint32 consumption = origin_fab->get_base_production() * (supplier ? supplier->get_consumption() : 1);

	slist_tpl<factories_to_crossconnect_t> factories_to_correct;
	slist_tpl<fabrik_t *> new_factories;	      // since the cross-correction must be done later
	slist_tpl<fabrik_t *> crossconnected_suppliers;	// also done after the construction of new chains

	int supplier_count = supplier->get_supplier_count();
	int suppliers_found = 0;	// number of found producers

	DBG_MESSAGE("factory_builder_t::build_link", "supplier_count %i, supplier_count %i (need %i of %s)", info->get_supplier_count(), supplier_count, consumption, ware->get_name());

	// We only observe the max_distance_to_supplier if the supplier can exist in our region.
	const uint8 origin_region = welt->get_region(origin_fab->get_pos().get_2d());

	uint16 max_distance_to_supplier = origin_fab->get_desc()->get_max_distance_to_supplier();
	if (max_distance_to_supplier < 65535)
	{
		// Check whether any types of factory that can be built at this time in this factory's region can supply this input good.
		weighted_vector_tpl<const factory_desc_t*>producer;
		find_producer(producer, ware, welt->get_timeline_year_month());
		bool local_supplier_unavailable = true;
		for(const factory_desc_t* producer_type : producer) {
			if (producer_type->get_building()->is_allowed_region(origin_region))
			{
				local_supplier_unavailable = false;
				break;
			}
		}
		if (local_supplier_unavailable)
		{
			max_distance_to_supplier = 65535;
		}
	}

	// search if there already is one or two (cross-connect everything if possible)
	for(auto const fab : welt->get_fab_list())
	{
		// Try to find matching factories for this consumption, but don't find more than two times number of factories requested.
		//if ((supplier_count != 0 || consumption <= 0) && supplier_count < suppliers_found + 1) break;

		// For reference, origin_fab is the consumer and fab is the potential producer already in the game.

		// connect to an existing one if this is a producer
		if(fab->get_output_stock(ware) > -1)
		{
			// The water connectivity check.  Prevent fishing ports from linking to fisheries in the wrong ocean.
			bool water_connect_check_ok = true;
			if (fab->get_desc()->get_placement() == factory_desc_t::Water) {
				koord water_start_pos = koord::invalid;
				// Find a water tile next to the consumer (e.g. fishing port)
				bool found_water = false;
				koord pos = origin_fab->get_pos().get_2d();
				for(int i = 0; i < 8; i++) {
					water_start_pos = pos + koord::neighbours[i];
					const grund_t* water_start_gr = welt->lookup_kartenboden(water_start_pos);
					if (water_start_gr && water_start_gr->is_water()) {
						// Found a water tile.
						found_water = true;
						break;
					}
				}
				if (!found_water) {
					// Destination factory isn't next to the water, so don't worry about it and connect anyway.
					goto past_water_connect_check;
				}
				if (found_water) {
					// Fail the water connect check if we don't find an ocean path.
					// This will force the fishing port to build its own fishery instead of cross-connecting.
					bool ocean_path_found = has_ocean_route(water_start_pos, fab->get_pos().get_2d());
					water_connect_check_ok = ocean_path_found;
				}
			}
			past_water_connect_check:

			// The distance checks
			const uint32 distance = shortest_distance(fab->get_pos().get_2d(), origin_fab->get_pos().get_2d());
			bool distance_long_enough = distance >= (uint32)welt->get_settings().get_min_factory_spacing();
			bool distance_short_enough = distance <= fab->get_desc()->get_max_distance_to_consumer() && distance <= max_distance_to_supplier;
			if(water_connect_check_ok && distance_long_enough && distance_short_enough)
			{
				// ok, this would match
				// but can she supply enough?

				// now guess how much this factory can supply
				const factory_desc_t* const fab_desc = fab->get_desc();
				for (uint product_num = 0; product_num < fab_desc->get_product_count(); product_num++) {
					const factory_product_desc_t *const product_desc = fab_desc->get_product(product_num);
					if (product_desc->get_output_type() == ware && (fab->get_consumers().get_count() < 10 || welt->get_settings().using_fab_contracts())) { // does not make sense to split into more ...
						sint32 production_left = fab->get_base_production() * product_desc->get_factor();

						// decrease remaining production by supplier demand
						if(welt->get_settings().using_fab_contracts()){
							production_left-=fab->get_output(ware)->get_total_contracts();
						}else{
							for(auto const & consumer_pos : fab->get_consumers()) {
								if (production_left <= 0) break;
								fabrik_t* const consumer = fabrik_t::get_fab(consumer_pos);
								for(int supplier_num=0; supplier_num < consumer->get_desc()->get_supplier_count(); supplier_num++) {
									const factory_supplier_desc_t *this_supplier = consumer->get_desc()->get_supplier(supplier_num);
									if(this_supplier->get_input_type() == ware) {
										production_left -= consumer->get_base_production() * this_supplier->get_consumption();
										break;
									}
								}
							}
						}

						// here is actually capacity left (or sometimes just connect anyway)!
						if (production_left > 0 ||simrand(100, "factory_builder_t::build_link") < (uint32)welt->get_settings().get_crossconnect_factor()) {

							if(production_left>0) {
								consumption -= production_left;
								fab->add_consumer(origin_fab->get_pos().get_2d(),ware,production_left);
								DBG_MESSAGE("factory_builder_t::build_link", "supplier %s can supply approx %i of %s to us", fab_desc->get_name(), production_left, ware->get_name());
							}
							else {
								/* we steal something; however the total capacity
								 * needed is the same. Therefore, we just keep book
								 * from whose factories from how many we stole */
								crossconnected_suppliers.append(fab);
								for(auto const & consumer_pos : fab->get_consumers()) {
									fabrik_t* consumer = fabrik_t::get_fab(consumer_pos);
									slist_tpl<factories_to_crossconnect_t>::iterator i = std::find(factories_to_correct.begin(), factories_to_correct.end(), factories_to_crossconnect_t(consumer, 0));
									if (i == factories_to_correct.end()) {
										factories_to_correct.append(factories_to_crossconnect_t(consumer, 1));
									}
									else {
										i->demand += 1;
									}
								}
								// the needed production to be built does not change!
							}
							suppliers_found ++;
						}
						break; // since we found the product ...
					}
				}
			}
		}
	}

	INT_CHECK( "fabrikbauer 670" );

	if(!no_new_industries)
	{
		/* try to add all types of factories until demand is satisfied
		 */
		weighted_vector_tpl<const factory_desc_t *>producer;
		find_producer( producer, ware, welt->get_timeline_year_month() );
		if(  producer.empty()  ) {
			// can happen with timeline
			if(!info->is_consumer_only()) {
				dbg->error( "factory_builder_t::build_link()", "no producer for %s!", ware->get_name() );
				return 0;
			}
			else {
				// only consumer: Will do with partly covered chains
				dbg->error( "factory_builder_t::build_link()", "no producer for %s!", ware->get_name() );
			}
		}

		bool ignore_climates = false;	// ignore climates after some retrys
		int retry=25;	// and not more than 25 (happens mostly in towns)
		while((supplier_count > suppliers_found || supplier_count == 0) && consumption > 0 && retry > 0  ) {

			const factory_desc_t *producer_d = pick_any_weighted( producer );

			int rotate = simrand(producer_d->get_building()->get_all_layouts()-1, "factory_builder_t::build_link");
			koord3d parent_pos = origin_fab->get_pos();
			// ignore climates after 40 tries

			INT_CHECK("fabrikbauer 697");
			const int max_distance_to_consumer = producer_d->get_max_distance_to_consumer() == 0 ? max_factory_spacing_general : producer_d->get_max_distance_to_consumer();
			const int max_distance = min(max_distance_to_supplier, min(max_factory_spacing_general, max_distance_to_consumer));
			koord3d build_pos = find_random_construction_site(origin_fab->get_pos().get_2d(), max_distance, producer_d->get_building()->get_size(rotate), producer_d->get_placement(), producer_d->get_building(), ignore_climates, 200000 );
			if(build_pos == koord3d::invalid  ) {
				// this factory cannot build in the desired vincinity
				producer.remove( producer_d );
				if(  producer.empty()  ) {
					if(  ignore_climates  ) {
						// absolutely no place to construct something here
						break;
					}
					find_producer( producer, ware, welt->get_timeline_year_month() );
					ignore_climates = true;
				}
				continue;
			}

			INT_CHECK("fabrikbauer 697");

			DBG_MESSAGE("factory_builder_t::build_link", "Try to built supplier %s at (%i,%i) r=%i for %s.", producer_d->get_name(), build_pos.x, build_pos.y, rotate, info->get_name());
			new_fabs_built_count += build_link(&parent_pos, producer_d, -1 /*random prodbase */, rotate, &build_pos, player, 10000, ignore_climates);
			suppliers_found ++;

			INT_CHECK( "fabrikbauer 702" );

			// now subtract current supplier
			fabrik_t *fab = fabrik_t::get_fab(build_pos.get_2d() );
			if(fab==NULL) {
				DBG_MESSAGE("factory_builder_t::build_link", "Failed to build at %s", build_pos.get_str() );
				retry --;
				continue;
			}
			new_factories.append(fab);

			// connect new supplier to us
			const factory_desc_t* const fab_desc = fab->get_desc();
			for (uint product_num = 0; product_num < fab->get_desc()->get_product_count(); product_num++) {
				const factory_product_desc_t *const product = fab_desc->get_product(product_num);
				if (product->get_output_type() == ware) {
					sint32 production = fab->get_base_production() * product->get_factor();
					// the take care of how much this factory could supply
					consumption -= production;
					DBG_MESSAGE("factory_builder_t::build_link", "new supplier %s can supply approx %i of %s to us", fab_desc->get_name(), production, ware->get_name());
					break;
				}
			}
		}
	}

	// now we add us to all cross-connected factories
	for(auto const this_supplier : crossconnected_suppliers) {
		this_supplier->add_consumer(origin_fab->get_pos().get_2d());
	}

	if(!no_new_industries)
	{
		/* now the cross-connect part:
		 * connect also the factories we stole from before ...
		 */
		for(fabrik_t* const fab : new_factories) {
			for(slist_tpl<factories_to_crossconnect_t>::iterator fabs_to_correct = factories_to_correct.begin(), end = factories_to_correct.end(); fabs_to_correct != end;)
			{
				fabs_to_correct->demand -= 1;
				const uint count = fab->get_desc()->get_product_count();
				bool supplies_correct_goods = false;
				for(uint x = 0; x < count; x ++)
				{
					const factory_product_desc_t* test_prod = fab->get_desc()->get_product(x);
					if(fabs_to_correct->fab->get_produced_goods()->is_contained(test_prod->get_output_type()))
					{
						supplies_correct_goods = true;
						break;
					}
				}
				if(supplies_correct_goods)
				{
					fab->add_consumer(fabs_to_correct->fab->get_pos().get_2d());
					fabs_to_correct->fab->add_supplier(fab->get_pos().get_2d());
				}
				if (fabs_to_correct->demand < 0)
				{
					fabs_to_correct = factories_to_correct.erase(fabs_to_correct);
				}
				else
				{
					++fabs_to_correct;
				}
			}
		}
	}

	INT_CHECK( "fabrikbauer 723" );

	return new_fabs_built_count;
}


/* This method is called whenever it is time for industry growth.
 * If there is still a pending consumer, it will first complete another chain for it
 * If not, it will decide to either built a power station (if power is needed)
 * or built a new consumer near the indicated position, unless, as a 25% chance,
 * a new consumer is to be added in any event. This latter change is needed to make
 * sure that enough consumer industries get built.
 * @return: number of factories built
 */
int factory_builder_t::increase_industry_density( bool tell_me, bool do_not_add_beyond_target_density, bool power_stations_only, density_options force_consumer )
{
	int nr = 0;

	// TODO: This is a somewhat hackish mechanism for ensuring that there are sufficient consumer industries.
	// In the future, the building of pure consumer industries other than power stations should be part of
	// the city growth system and taken out of this entirely. That would leave this system free to complete
	// industry chains as needed.
	bool force_add_consumer = force_consumer == CONSUMER_ONLY || (force_consumer == NEUTRAL && 75 > simrand(100, "factory_builder_t::increase_industry_density()"));

	weighted_vector_tpl<const goods_desc_t*> oversupplied_goods;

	// Build a list of all industries with incomplete supply chains.
	if(!power_stations_only && !welt->get_fab_list().empty())
	{
		// A collection of all consumer industries that are not fully linked to suppliers.
		struct unlinked_consumer_t{
			fabrik_t* fab;
			uint32 idx;
			unlinked_consumer_t(fabrik_t *f,uint32 i){fab=f; idx=i;}
			fabrik_t* operator->(){return fab;}
			bool operator!=(unlinked_consumer_t b){return fab!=b.fab;}
		};

		slist_tpl<unlinked_consumer_t> unlinked_consumers;

		if(welt->get_settings().using_fab_contracts()){
			for(auto fab : welt->get_fab_list()){
				// First, re-link industries as necessary without building new.
				if (fab->disconnect_supplier(koord::invalid)) // This does not remove anything, but checks for missing suppliers
				{
					force_add_consumer = false;
				}
				fab->disconnect_consumer(koord::invalid); // This does not remove anything, but checks for missing consumers

				//renegotiate contracts before checking for oversupply or under consumption
				fab->negotiate_contracts();

				for(uint32 i = 0; i < fab->get_input().get_count(); i++){
					const sint64 pfactor=fab->get_desc()->get_supplier(i)->get_consumption();
					const sint32 monthly_prod=fab->get_monthly_production(pfactor);
					const sint32 monthly_cont=fab->get_input()[i].get_total_contracts();
					if(monthly_cont * 9 < monthly_prod * 8){
						unlinked_consumers.append(unlinked_consumer_t(fab,i));
					}
				}

				for(uint32 i = 0; i < fab->get_output().get_count(); i++){
					const sint64 pfactor=fab->get_desc()->get_product(i)->get_factor();
					const sint32 monthly_prod=fab->get_monthly_production(pfactor);
					const sint32 monthly_cont=fab->get_output()[i].get_total_contracts();
					if(monthly_prod * 9 > monthly_cont * 8){
						oversupplied_goods.append_unique(fab->get_output()[i].get_typ(),monthly_prod-monthly_cont);
					}
				}

			}
		}else{
			slist_tpl<const goods_desc_t*> missing_goods;

			for(auto fab : welt->get_fab_list())
			{
				// First, re-link industries as necessary without building new.
				if (fab->disconnect_supplier(koord::invalid)) // This does not remove anything, but checks for missing suppliers
				{
					force_add_consumer = false;
				}
				fab->disconnect_consumer(koord::invalid); // This does not remove anything, but checks for missing consumers

				sint32 available_for_consumption;
				sint32 consumption_level;

				for(uint16 l = 0; l < fab->get_desc()->get_supplier_count(); l ++)
				{
					// Check the list of possible suppliers for this factory type.
					const factory_supplier_desc_t* supplier_type = fab->get_desc()->get_supplier(l);
					const goods_desc_t* input_type = supplier_type->get_input_type();

					missing_goods.append_unique(input_type);
					auto suppliers = fab->get_suppliers(input_type);

					if (suppliers.get_count() == 0) {
						DBG_MESSAGE("factory_builder_t::increase_industry_density()", "found undersupplied factory %s with 0 suppliers for input %s", fab->get_name(), input_type->get_name());
						unlinked_consumers.append_unique(unlinked_consumer_t(fab, l));
						continue;
					}

					// Check how much of this product that the current factory needs
					consumption_level = fab->get_base_production() * (supplier_type ? supplier_type->get_consumption() : 1);

					//adjust consumption_level for any consumers this factory is hooked up to (if applicable) since a factory with a low number of consumers won't need as many suppliers
					if (!fab->get_desc()->is_consumer_only()) {
						sint32 output_prod_level = 0;
						sint32 output_consumption = 0;

						for (int i = 0; i < fab->get_desc()->get_product_count(); i++) {
							const goods_desc_t* output_type = fab->get_desc()->get_product(i)->get_output_type();
							output_prod_level += fab->get_base_production() * fab->get_desc()->get_product(output_type)->get_factor();

							for (auto consumer_koord : fab->get_consumers(output_type)) {
								//for each consumer, sum up the production from competing suppliers, and add the leftover fraction to output_consumption
								fabrik_t* consumer = fabrik_t::get_fab(consumer_koord);

								if (!consumer) {
									continue;
								}
								sint32 consumer_consumption = consumer->get_base_production() * consumer->get_desc()->get_supplier(output_type)->get_consumption();
								sint32 competing_supplier_prod = 0;
								for (auto competing_supplier_koord : consumer->get_suppliers(output_type)) {
									const fabrik_t* competing_supplier = fabrik_t::get_fab(competing_supplier_koord);
									if (competing_supplier) {
										competing_supplier_prod += competing_supplier->get_base_production() * competing_supplier->get_desc()->get_product(output_type)->get_factor();
									}
								}
								output_consumption += consumer_consumption * output_prod_level / competing_supplier_prod;
							}

						}
						if (output_consumption < output_prod_level) {
							consumption_level = consumption_level * output_consumption / output_prod_level;
						}
					}
					available_for_consumption = 0;

					for(auto supplier_koord : suppliers)
					{
						if (available_for_consumption >= consumption_level)
						{
							break;
						}

						// Check whether the factory's actual suppliers supply any of this product.
						fabrik_t* supplier = fabrik_t::get_fab(supplier_koord);
						if(!supplier)
						{
							continue;
						}
						if(auto consumer_type = supplier->get_desc()->get_product(input_type))
						{
							// Check to see whether this existing supplier is able to supply *enough* of this product
							const sint32 total_output_supplier = supplier->get_base_production() * consumer_type->get_factor();

							sint32 used_output = 0;
							for(auto competing_consumers : supplier->get_consumers(input_type))
							{
								if(const fabrik_t* competing_consumer = fabrik_t::get_fab(competing_consumers)){
									if (competing_consumer != fab) {

										//sum up production from alternative suppliers to the competing consumer
										sint32 alt_supplier_prod = 0;
										for (auto alt_supplier_koord : competing_consumer->get_suppliers(input_type)) {
											const fabrik_t* alt_supplier = fabrik_t::get_fab(alt_supplier_koord);
											alt_supplier_prod += alt_supplier->get_base_production() * alt_supplier->get_desc()->get_product(input_type)->get_factor();
										}
										used_output += competing_consumer->get_base_production() * (supplier->get_base_production() * supplier->get_desc()->get_product(input_type)->get_factor()) / alt_supplier_prod;
										//const factory_supplier_desc_t* alternative_supplier_to_consumer = competing_consumer->get_desc()->get_supplier(input_type);
										//used_output += competing_consumer->get_base_production() * (alternative_supplier_to_consumer ? alternative_supplier_to_consumer->get_consumption() : 1);
									}
								}
							}
							const sint32 remaining_output = total_output_supplier - used_output;

							if (remaining_output > 0)
							{
								available_for_consumption += remaining_output;
							}
							else {
								available_for_consumption += total_output_supplier * consumption_level / (used_output + consumption_level);
							}
							if(available_for_consumption >= consumption_level)
							{
								// If the suppliers between them do supply enough of the product, do not list it as missing.
								missing_goods.remove(input_type);
								uint32 old_weight = 0;
								if (oversupplied_goods.is_contained(input_type))
								{

									old_weight = oversupplied_goods.weight_at(oversupplied_goods.index_of(input_type));
									// Avoid duplication
									oversupplied_goods.remove(input_type);
								}
								// Get the total distribution_weight of consumers of this product
								uint32 total_consumer_weight = 0;
								for (auto const& i : desc_table) {
									factory_desc_t const* const current = i.value;
									// Only insert end consumers, if applicable, with the requested input goods.
									if (!current->is_electricity_producer() &&
										current->get_building()->is_available(welt->get_timeline_year_month()) &&
										(current->get_accepts_these_goods(input_type))
										)
									{
										
										total_consumer_weight += current->get_distribution_weight();								
										
									}
								}
								//DBG_MESSAGE("factory_builder_t::increase_industry_density()", "appending good %s to oversupplied_goods (%ld * %ld) / %ld = %ld", input_type->get_name(), available_for_consumption, total_consumer_weight, consumption_level, (available_for_consumption - consumption_level) * total_consumer_weight);
								//oversupplied_goods.append(input_type, old_weight + ((available_for_consumption - consumption_level) * total_consumer_weight));

								//oversupplied_goods.append(input_type, max(old_weight, ((available_for_consumption * total_consumer_weight) / consumption_level))); // Middle ground between oversupply percentage and consumer weighting
								sint32 global_production = get_global_production(input_type);
								sint32 global_consumption = get_global_consumption(input_type);
								if ((global_production - global_consumption) > 0 && global_consumption > 0) { //if global_consumption == 0, then that means the only downstream consumption of the good is from power stations and we should disregard it
									oversupplied_goods.append_unique(input_type, (global_production * total_consumer_weight) / global_consumption);
									DBG_MESSAGE("factory_builder_t::increase_industry_density()", "appending good %s to oversupplied_goods with weight %ld (new total weight: %ld)", input_type->get_name(), (global_production * total_consumer_weight) / global_consumption, oversupplied_goods.get_sum_weight());

								}
								else if (global_consumption == 0) {
									DBG_MESSAGE("factory_builder_t::increase_industry_density()", "rejected oversupplied good %s due to global_consumption == 0 (global production: %ld)", input_type->get_name(), global_production);
								}

							}
						}
					} // Actual suppliers
					if (available_for_consumption < consumption_level) {
						for (int i = 0; i < fab->get_desc()->get_supplier_count(); i++) {
							if(fab->get_input()[i].get_typ() == input_type){
								//DBG_MESSAGE("factory_builder_t::increase_industry_density()", "found undersupplied factory %s with %ld/%ld production for input %s", fab->get_name(), available_for_consumption, consumption_level, input_type->get_name());
								unlinked_consumers.append_unique(unlinked_consumer_t(fab, i));
							}
						}
					}
				} // Possible suppliers
				missing_goods.clear();
			} // All industries
		}

		// ok, found consumer
		if(!force_add_consumer && force_consumer == FILL_MISSING_ONLY && !unlinked_consumers.empty())
		{
			DBG_MESSAGE("factory_builder_t::increase_industry_density()", "number of unlinked consumers: %d", unlinked_consumers.get_count());
			for(auto unlinked_consumer : unlinked_consumers)
			{

				uint16 missing_goods_index = unlinked_consumer.idx;

				// First: do we have to continue unfinished factory chains?
				if(missing_goods_index < unlinked_consumer->get_desc()->get_supplier_count())
				{
					DBG_MESSAGE("factory_builder_t::increase_industry_density()", "processing missing_good %s for factory %s", unlinked_consumer->get_desc()->get_supplier(missing_goods_index)->get_input_type()->get_name(), unlinked_consumer->get_name());
					int org_rotation = -1;
					// rotate until we can save it, if one of the factory is non-rotateable ...
					if(welt->cannot_save()  &&  !can_factory_tree_rotate(unlinked_consumer->get_desc()) )
					{
						org_rotation = welt->get_settings().get_rotation();
						for(  int i=0;  i<3  &&  welt->cannot_save();  i++  )
						{
							welt->rotate90();
						}
						assert( !welt->cannot_save() );
					}

					const uint32 last_suppliers = unlinked_consumer->get_suppliers().get_count();
					nr += build_chain_link(unlinked_consumer.fab, unlinked_consumer->get_desc(), missing_goods_index, welt->get_public_player());
					// must rotate back?
					if(org_rotation>=0) {
						for (int i = 0; i < 4 && welt->get_settings().get_rotation() != org_rotation; ++i) {
							welt->rotate90();
						}
						welt->update_map();
					}

					// only return, if successful
					if(nr > 0)
					{

						DBG_MESSAGE( "factory_builder_t::increase_industry_density()", "added ware %s to factory %s with %i factories built (number of suppliers: %i)", unlinked_consumer->get_desc()->get_supplier(missing_goods_index)->get_input_type()->get_name(), unlinked_consumer->get_name(), nr, unlinked_consumer->get_suppliers().get_count() );
						// tell the player
						if(tell_me) {
							stadt_t *s = welt->find_nearest_city( unlinked_consumer->get_pos().get_2d() );
							const char *stadt_name = s ? s->get_name() : "simcity";
							cbuffer_t buf;
							buf.printf( translator::translate("Factory chain extended\nfor %s near\n%s built with\n%i factories."), translator::translate(unlinked_consumer->get_name()), stadt_name, nr );
							welt->get_message()->add_message(buf, unlinked_consumer->get_pos().get_2d(), message_t::industry, CITY_KI, unlinked_consumer->get_desc()->get_building()->get_tile(0)->get_background(0, 0, 0));
						}
						minimap_t::get_instance()->calc_map();
						return nr;
					}
				}
			}
		}
	}
	if (force_consumer == FILL_MISSING_ONLY) { //if we've gotten here with FILL_MISSING_ONLY then there are no more undersupplied factory chains to fill in
		DBG_MESSAGE("factory_builder_t::increase_industry_density()", "FILL_MISSING_ONLY returning 0");
		return 0;
	}

	// ok, no chains to finish, thus we must start anew

	// first decide whether a new powerplant is needed or not
	uint32 total_electric_demand = 1;
	uint32 electric_productivity = 0;

	for(auto const fab : welt->get_fab_list())
	{
		if(fab->get_desc()->is_electricity_producer())
		{
			electric_productivity += fab->get_scaled_electric_demand();
		}
		else
		{
			total_electric_demand += fab->get_scaled_electric_demand();
		}
	}

	const weighted_vector_tpl<stadt_t*>& staedte = welt->get_cities();

	for(weighted_vector_tpl<stadt_t*>::const_iterator i = staedte.begin(), end = staedte.end(); i != end; ++i)
	{
		total_electric_demand += (*i)->get_power_demand();
	}

	// now decide producer of electricity or normal ...
	const sint64 promille = ((sint64)electric_productivity * 4000l) / total_electric_demand;
	const sint64 target_promille = (sint64)welt->get_settings().get_electric_promille();
	int no_electric = force_add_consumer || (promille >= target_promille) ? 1 : 0;
	DBG_MESSAGE( "factory_builder_t::increase_industry_density()", "production of electricity/total electrical demand is %i/%i (%i o/oo)", electric_productivity, total_electric_demand, promille );

	// Determine whether to fill in oversupplied goods with a consumer industry, or generate one entirely randomly
	const goods_desc_t* input_for_consumer = NULL;
	//DBG_MESSAGE("factory_builder_t::increase_industry_density()", "Oversupplied goods sum weight: %ld (%s)", oversupplied_goods.get_sum_weight(), oversupplied_goods.get_sum_weight() > 0 ? "true":"false");
	if (!oversupplied_goods.empty() && (oversupplied_goods.get_sum_weight() > 0) && ((simrand(100, "factory_builder_t::increase_industry_density()") < 20) || force_consumer == 2))
	{
		for (uint16 i = 0; i < oversupplied_goods.get_count(); i++) {
			DBG_MESSAGE("factory_builder_t::increase_industry_density()", "oversupplied good: %s (weight %ld)", oversupplied_goods[i]->get_name(), oversupplied_goods.weight_at(i));
		}
		int tries = 5;
		while(tries > 0){ //attempt to only select goods that are in 'real' oversupply (based on actual production, not contracts/etc)
			const uint32 pick = simrand(oversupplied_goods.get_sum_weight(), "factory_builder_t::increase_industry_density()");
			const goods_desc_t* new_input = oversupplied_goods.at_weight(pick);
			if(get_global_oversupply(new_input) > 0){
				if (force_consumer == CONSUMER_ONLY && get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), new_input, false)) {
					input_for_consumer = new_input;
				}
				else if (get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(),new_input) ){
					input_for_consumer = new_input;
				}
				break;
			}
			tries--;
		}

	}

	if (input_for_consumer) {
		DBG_MESSAGE("factory_builder_t::increase_industry_density()", "Chose oversupplied good %s", input_for_consumer->get_name());
	}
	if (force_consumer == CONSUMER_ONLY && !input_for_consumer) {
		//if we can't pick a valid consumer input then return
		return 0;
	}

	while(  no_electric<2  ) {
		bool ignore_climates = false;

		for(int retries = 20; retries > 0; retries--)
		{
			if (retries < 5)
			{
				// Give up trying to find the right consumer after trying too many times.
				input_for_consumer = NULL;
			}

			const factory_desc_t* consumer;

			if (force_consumer == CONSUMER_ONLY) {
				consumer = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), input_for_consumer, false);
			}
			else {
				consumer = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), input_for_consumer);
			}

			const factory_desc_t* consumer2 = NULL;
			const goods_desc_t* good2 = NULL;
			if(consumer)
			{
				DBG_MESSAGE("factory_builder_t::increase_industry_density()", "chose random consumer %s", consumer->get_name());
				if (!consumer->is_consumer_only()) { //logic for making new manufacturers
					//first we check if this consumer is making a good that is already overproduced, if so we just add a new consumer for that good (to prevent adding multiple undercapacity factories)
					for (uint16 i = 0; i < consumer->get_product_count(); i++) {
						const goods_desc_t* newgood = consumer->get_product(i)->get_output_type();
						const factory_desc_t* newconsumer = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), newgood);
						if (get_global_oversupply(newgood) > 0 && newconsumer) {
							consumer = newconsumer;
							DBG_MESSAGE("factory_builder_t::increase_industry_density()", "manufacturer was oversupplying, changed to downstream consumer %s", consumer->get_name());
						}
					}
					if (!consumer->is_consumer_only()) { //if not, we have a reason to add a new manufacturer
						//if we really are making a new manufacturer, we need a second consumer in order to properly terminate the chain
						uint16 productnum = simrand(consumer->get_product_count(), "factory_builder_t::increase_industry_density()");
						good2 = consumer->get_product(productnum)->get_output_type();
						consumer2 = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), good2);
						if (!consumer2) { //catching cases where not every factory output has consumers available
							for (int i = 0; i < consumer->get_product_count(); i++) {
								good2 = consumer->get_product(i)->get_output_type();
								consumer2 = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), good2);
								if (consumer2) {
									break;
								}
							}
						}

						if (!consumer2) {//if there are no consumers whatsoever unlocked for a manufacturer (pakset problem), panic and try to find a consumer-only
							dbg->warning("factory_builder_t::increase_industry_density()", "Could not find available consumer for products of %s, falling back to consumer-only", consumer->get_name());
							consumer = get_random_consumer(no_electric == 0, ALL_CLIMATES, 65535, welt->get_timeline_year_month(), input_for_consumer);
							if (!consumer) {
								continue;
							}
						}
					}
				}
				if(do_not_add_beyond_target_density && !consumer->is_electricity_producer())
				{
					// Make sure that consumer industries are not added beyond target density.
					if ((consumer2 && 100U / consumer2->get_distribution_weight() > (welt->get_target_industry_density() - welt->get_actual_industry_density())
						|| (!consumer2 && 100U / consumer->get_distribution_weight() > (welt->get_target_industry_density() - welt->get_actual_industry_density())))) {
						continue;
					}
				}
				koord3d pos, pos2;
				int rotation, rotation2;
				find_valid_factory_pos(&pos, &rotation, consumer, ignore_climates);

				if (consumer2) {
					find_valid_factory_pos(&pos2, &rotation2, consumer2, ignore_climates);
				}

				if(welt->lookup(pos)) {
					// Space found...
					if (force_consumer == CONSUMER_ONLY) { //
						if (get_global_oversupply(input_for_consumer) > 0) { //double check that the good we have selected really is oversupplied

							if (!consumer->is_consumer_only() && welt->lookup(pos2)) {
								nr += build_link(NULL, consumer, -1 /* random prodbase */, rotation, &pos, welt->get_public_player(), 0, ignore_climates);
								nr += build_link(&pos, consumer2, -1, rotation2, &pos2, welt->get_public_player(), 0, ignore_climates);

								//cbuffer_t buf2;
								//buf2.printf("Adding link for good2 %s with factory %s (real oversupply %ld).\n", good2->get_name(), consumer2->get_name(), get_real_oversupply(good2));
								//welt->get_message()->add_message(buf2, pos2.get_2d(), message_t::industry, CITY_KI);
							}
							else {
								nr += build_link(NULL, consumer, -1 /* random prodbase */, rotation, &pos, welt->get_public_player(), 0, ignore_climates);

								/*cbuffer_t buf;
								buf.printf("Adding link for good %s with factory %s (real oversupply %ld).\n", input_for_consumer->get_name(), consumer->get_name(), get_real_oversupply(input_for_consumer));
								welt->get_message()->add_message(buf, pos.get_2d(), message_t::industry, CITY_KI);*/
							}
						}
					}
					else {
						nr += build_link(NULL, consumer, -1 /* random prodbase */, rotation, &pos, welt->get_public_player(), 0, ignore_climates);
					}
					if(nr>0) {
						fabrik_t *our_fab = fabrik_t::get_fab( pos.get_2d() );
						minimap_t::get_instance()->calc_map_size();

						// tell the player
						if(tell_me) {
							stadt_t *s = welt->find_nearest_city(pos.get_2d());
							const char *stadt_name = s ? s->get_name() : translator::translate("nowhere");
							cbuffer_t buf;
							buf.printf( translator::translate("New factory chain\nfor %s near\n%s built with\n%i factories."), translator::translate(our_fab->get_name()), stadt_name, nr );
							welt->get_message()->add_message(buf, pos.get_2d(), message_t::industry, CITY_KI, our_fab->get_desc()->get_building()->get_tile(0)->get_background(0, 0, 0));
						}


						return nr;
					}
				}
				else if(  retries==1  &&  !ignore_climates  ) {
					// from now on, we will ignore climates to avoid broken chains
					ignore_climates = true;
					retries = 20;
				}
			}
		}
		// try building normal factories if building power plants was not successful
		no_electric++;
	}

	// we should not reach here, because it means neither land nor city industries exist ...
	dbg->warning( "factory_builder_t::increase_industry_density()", "No suitable city industry found => pak missing something?" );
	return 0;
}

void factory_builder_t::find_valid_factory_pos(koord3d *pos, int *rotation, const factory_desc_t *factory_type, bool ignore_climates) {
	const factory_desc_t::site_t site = factory_type->get_placement();
	const bool in_city = site == factory_desc_t::City || site == factory_desc_t::shore_city || site == factory_desc_t::river_city;
	if (in_city && welt->get_cities().empty())
	{
		// we cannot build this factory here
		return;
	}
	koord testpos = in_city ? pick_any_weighted(welt->get_cities())->get_pos() : koord::koord_random(welt->get_size().x, welt->get_size().y);
	*pos = welt->lookup_kartenboden(testpos)->get_pos();
	*rotation = simrand(factory_type->get_building()->get_all_layouts() - 1, "factory_builder_t::increase_industry_density()");
	if (!in_city)
	{
		// find somewhere on the map
		*pos = find_random_construction_site(koord(welt->get_size().x / 2, welt->get_size().y / 2), welt->get_size_max() / 2, factory_type->get_building()->get_size(*rotation), factory_type->get_placement(), factory_type->get_building(), ignore_climates, 10000);
	}
	else {
		// or within the city limit
		const stadt_t* city = pick_any_weighted(welt->get_cities());
		koord diff = city->get_rechtsunten() - city->get_linksoben();
		*pos = find_random_construction_site(city->get_center(), max(diff.x, diff.y) / 2, factory_type->get_building()->get_size(*rotation), factory_type->get_placement(), factory_type->get_building(), ignore_climates, 1000);
	}
}

sint32 factory_builder_t::get_global_oversupply(const goods_desc_t* good) {
	return get_global_production(good) - get_global_consumption(good);
}

sint32 factory_builder_t::get_global_production(const goods_desc_t* good) {
	sint32 total_prod = 0;

	for (auto const fab : welt->get_fab_list()) { //double-checking that there actually is overconsumption of the good
		for (uint32 i = 0; i < fab->get_output().get_count(); i++) {
			const goods_desc_t* factory_good = fab->get_output()[i].get_typ();
			if (factory_good == good) { //does this factory produce the good we want?
				const uint16 pfactor = fab->get_desc()->get_product(i)->get_factor();
				const sint32 monthly_prod = fab->get_monthly_production(pfactor);
				total_prod += monthly_prod;
			}
		}
	}
	return total_prod;
}

sint32 factory_builder_t::get_global_consumption(const goods_desc_t* good) {
	//DBG_MESSAGE("factory_builder_t::get_global_consumption()", "getting global consumption for good %s", good->get_name());
	sint32 total_cons = 0;
	for (auto const fab : welt->get_fab_list()) { //double-checking that there actually is overconsumption of the good
		for (uint32 i = 0; i < fab->get_input().get_count(); i++) {
			const goods_desc_t* factory_good = fab->get_input()[i].get_typ();
			if (factory_good == good) {
				const uint16 cfactor = fab->get_desc()->get_supplier(i)->get_consumption();
				uint32 monthly_cons = fab->get_monthly_production(cfactor);


				if (!fab->get_desc()->is_consumer_only()) { //account for downstream consumption and adjust this factory's effective consumption accordingly
					uint32 output_prod = 0;
					uint32 output_cons = 0;
					for (auto factory_output : fab->get_output()) {
						uint32 this_cons = get_global_consumption(factory_output.get_typ());
						if (this_cons > 0) { //only count goods that are actually consumed at all (eg if a refinery exists purely to make fuel oil then unused gasoline production doesn't matter)
							output_prod += get_global_production(factory_output.get_typ());
							output_cons += this_cons;
						}
						
					}
					if (output_prod > 0) {
						monthly_cons = (monthly_cons * output_cons) / output_prod;
					}
					else {
						monthly_cons = 0;
					}
					
				}
				if (!fab->get_desc()->is_electricity_producer()) {
					total_cons += monthly_cons;
				}
			}
		}

	}
	return total_cons;
}


bool factory_builder_t::power_stations_available()
{
	weighted_vector_tpl<const factory_desc_t*> power_stations;

	for(auto const& i : desc_table) {
		const factory_desc_t* current = i.value;
		if(!current->is_electricity_producer()
			|| (welt->use_timeline()
				&& (current->get_building()->get_intro_year_month() > welt->get_timeline_year_month()
					|| current->get_building()->get_retire_year_month() < welt->get_timeline_year_month()))
			)
		{
			continue;
		}
		return true;
	}
	return false;
}
