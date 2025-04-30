/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef BAUER_FABRIKBAUER_H
#define BAUER_FABRIKBAUER_H


#include "../tpl/stringhashtable_tpl.h"
#include "../tpl/weighted_vector_tpl.h"
#include "../dataobj/koord3d.h"
#include "../descriptor/factory_desc.h"

class stadt_t;
class karte_ptr_t;
class player_t;
class fabrik_t;

enum density_options {
	NEUTRAL, NO_FORCE, CONSUMER_ONLY, FILL_MISSING_ONLY, FILL_UNDERSUPPLIED
};


/**
 * This class builds factories. Never construct factories directly
 * but always by calling factory_builder_t::build() (for a single factory)
 * or factory_builder_t::baue_hierachie() (for a full chain of suppliers).
 */
class factory_builder_t
{
private:
	static karte_ptr_t welt;

	/**
	 * @class factories_to_crossconnect_t
	 * Used for cross-connection checks between factories.
	 * This is necessary for finding producers for factory supply.
	 */
	class factories_to_crossconnect_t {
	public:
		fabrik_t *fab; ///< The factory
		sint32 demand; ///< To how many factories this factory needs to connect to

		factories_to_crossconnect_t() { fab = NULL; demand = 0; }
		factories_to_crossconnect_t(fabrik_t *f, sint32 d) { fab = f; demand = d; }

		bool operator == (const factories_to_crossconnect_t& x) const { return fab == x.fab; }
		bool operator != (const factories_to_crossconnect_t& x) const { return fab != x.fab; }
	};

	/// Table of all factories that can be built
	static stringhashtable_tpl<const factory_desc_t *, N_BAGS_MEDIUM> desc_table;

	/// @returns the number of producers producing @p ware
	static int count_producers(const goods_desc_t *ware, uint16 timeline);

	/**
	 * Finds a random producer producing @p ware.
	 * @param timeline the current time (months)
	 */
	static void find_producer(weighted_vector_tpl<const factory_desc_t *> &producer, const goods_desc_t *ware, uint16 timeline );

public:
	/// This is only for the set_scale function in simworld.cc
	static stringhashtable_tpl<factory_desc_t *, N_BAGS_MEDIUM> modifiable_table;

	/// Registers the factory description so the factory can be built in-game.
	static void register_desc(factory_desc_t *desc);

	/**
	 * Initializes weighted vector for farm field class indices.
	 * @returns true
	 */
	static bool successfully_loaded();

	/**
	 * Tells the factory builder a new map is being loaded or generated.
	 * In this case the list of all factory positions must be reinitialized.
	 */
	static void new_world();

	/// Creates a certain number of tourist attractions.
	static void distribute_attractions(int max_number);

	/// @returns a factory description for a factory name
	static const factory_desc_t *get_desc(const char *factory_name);

	/// @returns the table containing all factory descriptions
	static const stringhashtable_tpl<const factory_desc_t*, N_BAGS_MEDIUM>& get_factory_table() { return desc_table; }

	/**
	 * @param electric true to limit search to electricity producers only
	 * @param cl allowed climates
	 * @returns a random consumer
	 */
	static const factory_desc_t *get_random_consumer(bool electric, climate_bits cl, uint16 allowed_regions, uint16 timeline, const goods_desc_t* input = NULL, bool force_consumer_only = true);

	static bool is_final_good(bool electric, climate_bits cl, uint16 allowed_regions, uint16 timeline, const goods_desc_t* good);

	/**
	 * Builds a single new factory.
	 *
	 * @param parent location of the parent factory
	 * @param info Description for the new factory
	 * @param initial_prod_base Initial base production (-1 to disable)
	 * @param rotate building rotation (0..3)
	 * @returns The newly constructed factory.
	 */
	static fabrik_t* build_factory(koord3d* parent, const factory_desc_t* info, sint32 initial_prod_base, int rotate, koord3d pos, player_t* owner);

	/**
	 * Builds a new full chain of factories. Precondition before calling this function:
	 * @p pos is suitable for factory construction and number of chains
	 * is the maximum number of good types for which suppliers chains are built
	 * (meaning there are no unfinished factory chains).
	 */
	static int build_link(koord3d* parent, const factory_desc_t* info, sint32 initial_prod_base, int rotate, koord3d* pos, player_t* player, int number_of_chains, bool ignore_climates);

	/**
	 * Helper function for baue_hierachie(): builds the connections (chain) for one single product)
	 * @returns number of factories built
	 */
	static int build_chain_link(const fabrik_t* origin_fab, const factory_desc_t* info, int supplier_nr, player_t* player, bool no_new_industries = false);

	/**
	 * This function is called whenever it is time for industry growth.
	 * If there is still a pending consumer, this method will first complete another chain for it.
	 * If not, it will decide to either build a power station (if power is needed)
	 * or build a new consumer near the indicated position.
	 * Force consumer: 0 - neutral; 1 - disallow forcing; 2 - always force consumer
	 * @returns number of factories built
	 */
	static int increase_industry_density(bool tell_me, bool do_not_add_beyond_target_density = false, bool power_station_only = false, density_options force_consumer = NEUTRAL );

	static bool power_stations_available();

private:
	/**
	 * Checks if the site at @p pos is suitable for construction.
	 * @param size Size of the building site
	 * @param cl allowed climates
	 */
	static bool check_construction_site(koord pos, koord size, factory_desc_t::site_t site, bool is_factory, climate_bits cl, uint16 regions_allowed);

	/**
	 * Support routine to see if two water points can be connected by ocean,
	 * for use in factory placement for water factories like fisheries
	 */
	static bool has_ocean_route(koord start_pos, koord end_pos);

	/**
	 * Find a random site to place a factory.
	 * @param radius Radius of the search circle around @p pos
	 * @param size size of the building site
	 */
	static koord3d find_random_construction_site(koord pos, int radius, koord size, factory_desc_t::site_t site, const building_desc_t *desc, bool ignore_climates, uint32 max_iterations);

	/**
	 * Checks if all factories in this factory tree can be rotated.
	 * This method is called recursively on all potential suppliers.
	 * @returns true if all factories in this tree can be rotated.
	 */
	static bool can_factory_tree_rotate( const factory_desc_t *desc );

	/**
	 * Checks 'real' overproduction of a given good, based on the total global production and total global consumption of it.
	 * @returns actual amount of global production minus actual amount of global consumption of the good.
	 */
	static sint32 get_global_oversupply(const goods_desc_t* good);

	/**
	 * Counts up total production of a given good.
	 * @returns actual amount of global production for the good
	 */
	static sint32 get_global_production(const goods_desc_t* good);

	/**
	 * Counts up total consumption of a good, taking into account downstream bottlenecks.
	 * @returns actual amount of global consumption of the good.
	 */
	static sint32 get_global_consumption(const goods_desc_t* good);

	/**
	 * Adjusts the consumption of a factory taking into account its downstream consumers, using the output it has the highest % consumption of.
	 * For instance, if a factory produces 100t of good A and 80t of good B, but good A has 10t of consumption and good B has 20t, then the overall adjustment is (20/80)=25%
	 * @returns consumption * the fraction of the factory's production that is actually used
	 */
	static sint32 adjust_input_consumption(const fabrik_t* factory, sint32 consumption);

	/**
	 * Finds a valid position for a factory type, and deposits the position and rotation in the pointers provided.
	 */
	static void find_valid_factory_pos(koord3d* pos, int* rotation, const factory_desc_t* factory_type, bool ignore_climates);
};

#endif
