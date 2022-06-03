/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef BAUER_PIER_BUILDER_H
#define BAUER_PIER_BUILDER_H


#include "../tpl/stringhashtable_tpl.h"
#include "../dataobj/koord3d.h"

class pier_desc_t;
class grund_t;
class karte_ptr_t;
class player_t;
class tool_selector_t;

class pier_builder_t {
private:
    pier_builder_t() {}

    static karte_ptr_t welt;

    static stringhashtable_tpl<pier_desc_t *, N_BAGS_MEDIUM> desc_table;

    static const char * check_below_ways(player_t *player, koord3d pos, const pier_desc_t *desc, const uint8 rotation, bool halfheight);

    static const char * check_for_buildings(const grund_t *gr, const pier_desc_t *desc, const uint8 rotation);

    struct pier_finder_params{
        pier_finder_params(){
            above_way_ribi=0;
            below_way_ribi=0;
            above_slope=0;
            ground_slope=0;
            support_needed=0;
            support_avail=0;
            middle_mask_taken=0;
            deck_obj_present=0;
            sub_obj_present=-1;
            need_clearence=false;
            allow_low_waydeck=false;
            is_wet=false;
            on_deck=false;
            stackable=false;
            autogroup=0xFF;
            max_cost=0x7FFFFFFF;
            max_maintenance=0x7FFFFFFF;
            min_axle_load=0xFFFF;
            requre_low_waydeck=false;
            existing_above_ribi=0;
            notallowed=false;
            nodeck=false;
            pos_lsbs=0;
            altitude=0;
        }

        uint64 support_needed;
        uint64 support_avail;
        uint64 middle_mask_taken;
        uint32 deck_obj_present;
        uint32 sub_obj_present;
        sint32 max_cost;
        sint32 max_maintenance;
        uint16 min_axle_load;
        uint32 autogroup;
        uint8 pos_lsbs;
        sint8 altitude;
        ribi_t::ribi above_way_ribi;
        ribi_t::ribi existing_above_ribi;
        ribi_t::ribi below_way_ribi;
        slope_t::type above_slope;
        slope_t::type ground_slope;

        bool need_clearence;
        bool allow_low_waydeck;
        bool is_wet;
        bool on_deck;
        bool stackable;
        bool requre_low_waydeck;
        bool notallowed;
        bool nodeck;
    };

    struct pier_finder_match{
        const pier_desc_t* desc;
        uint32 match;
        uint8 rotation;
        uint8 aux;
    };

    static void get_params_from_ground(pier_finder_params &params, const grund_t *gr, player_t *owner);

    static void get_params_from_pos(pier_finder_params &params, koord3d pos, player_t *owner);

    static inline bool get_desc_context(pier_desc_t const *& descriptor, uint8& rotation, pier_finder_params params, bool allow_inexact=false, vector_tpl<pier_finder_match> *matches=0, pier_finder_match *best_match=0, uint32 add_match=0);

public:

    struct pier_route_elem{
        const pier_desc_t* desc;
        koord3d pos;
        uint8 rotation;
        uint8 aux;
    };

    /**
     * @brief lookup_deck_pos find the position of the deck on a given ground
     * @param gr ground to check
     * @return position of pier deck
     */
    static koord3d lookup_deck_pos(const grund_t *gr);

	/**
	 * Registers a new pier type and adds it to the list of build tools.
	 *
	 * @param desc Description of the bridge to register.
	 */
	static void register_desc(pier_desc_t *desc);

	/**
	 * Method to retrieve pier descriptor
	 * @param name name of the pier
	 * @return bridge descriptor or NULL if not found
	 */
	static const pier_desc_t *get_desc(const char *name);

	/**
	 * @brief get_desc_bad_load obtain pier requirements from context after a bad load
	 * @param pos position of pier
	 * @param owner owner of pier
	 * @return the best matching pier
	 */
	static const pier_desc_t *get_desc_bad_load(koord3d pos,player_t *owner,uint8 &rotation);

	/**
	 * @brief get_desc_from_tos get the pier based on the top level of pier
	 * @param tos original pier to substitute
	 * @param rotation rotation to substitute
	 * @param pos position to place
	 * @param owner owner of pier
	 * @param upper_layer is the upper layer of piers
	 */
	static bool get_desc_from_tos(pier_desc_t const *& tos, uint8 &rotation, koord3d pos, player_t *owner, sint8 topz, bool upper_layer, ribi_t::ribi alt_tos_ribi=ribi_t::none);

	/**
	 * @brief calc_route calculate piers needed for to get from start to end
	 * @param route
	 * @param player
	 * @param start
	 * @param end
	 * @return
	 */
	static bool calc_route(vector_tpl<pier_route_elem> &route, player_t* player, const pier_desc_t* base_desc, koord3d start, koord3d end, sint8 start_height);

	static bool append_route_stack(vector_tpl<pier_route_elem> &route, player_t* player, const pier_desc_t* base_desc, koord3d topdeck, ribi_t::ribi deckribi);

	/**
	 * build a single pier
	 * @param player The player wanting to build the pier
	 * @param pos the position of the pier
	 * @param desc Descriptor of the pier
	 * @param rotation The orientation of the pier
	 * @return NULL on Sucess or error message
	 */
	static const char *build( player_t *player, koord3d pos, const pier_desc_t *desc, const uint8 rotation);

	/**
	 * Remove a pier
	 * @param player player removing the pier
	 * @param pos position of the pier
	 * @return NULL on sucess or error message
	 */
	static const char *remove( player_t *player, koord3d pos);

	/**
	 * fill_menu fill menu of all piers
	 * @param tool_selector gui object of toolbar
	 * @param mode 'A' for auto only, 'M' for manual Only
	 */
	static void fill_menu(tool_selector_t *tool_selector, char mode=')', uint32 filter=0);
};

#endif // PIER_BUILDER_H
