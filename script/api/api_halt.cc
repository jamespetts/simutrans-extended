/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "api.h"

/** @file api_halt.cc exports halt/station related functions. */

#include "get_next.h"

#include "api_obj_desc_base.h"
#include "../api_class.h"
#include "../api_function.h"
#include "../../simhalt.h"
#include "../../simworld.h"
#include "../../dataobj/environment.h"
#include "../../dataobj/translator.h"
#include "../../network/network_socket_list.h"

halthandle_t get_halt_from_koord3d(koord3d pos, const player_t *player ); // api_schedule.cc, interfaces haltestelle_t::get_halt


namespace script_api {

	declare_specialized_param(haltestelle_t::tile_t, "t|x|y", "tile_x");
// 	declare_specialized_param(haltestelle_t::connection_t, "t|x|y", "halt_x");


	SQInteger param<haltestelle_t::tile_t>::push(HSQUIRRELVM vm, haltestelle_t::tile_t const& v)
	{
		return param<grund_t*>::push(vm, v.grund);
	}

// 	SQInteger param<haltestelle_t::connection_t>::push(HSQUIRRELVM vm, haltestelle_t::connection_t const& v)
// 	{
// 		return param<halthandle_t>::push(vm, v.halt);
// 	}
};


using namespace script_api;

vector_tpl<sint64> const& get_halt_stat(const haltestelle_t *halt, sint32 INDEX)
{
	static vector_tpl<sint64> v;
	v.clear();
	if (halt  &&  0<=INDEX  &&  INDEX<MAX_HALT_COST) {
		for(uint16 i = 0; i < MAX_MONTHS; i++) {
			v.append( halt->get_finance_history(i, INDEX) );
		}
	}
	return v;
}


SQInteger world_get_next_halt(HSQUIRRELVM vm)
{
	return generic_get_next(vm, haltestelle_t::get_alle_haltestellen().get_count());
}


SQInteger world_get_halt_by_index(HSQUIRRELVM vm)
{
	sint32 index = param<sint32>::get(vm, -1);
	const vector_tpl<halthandle_t>& list = haltestelle_t::get_alle_haltestellen();
	halthandle_t halt = (0<=index  &&  (uint32)index<list.get_count()) ?  list[index] : halthandle_t();
	return param<halthandle_t>::push(vm, halt);
}


SQInteger halt_export_convoy_list(HSQUIRRELVM vm)
{
	halthandle_t halt = param<halthandle_t>::get(vm, 1);
	if (halt.is_bound()) {
		push_instance(vm, "convoy_list_x");
		set_slot(vm, "halt_id", halt.get_id());
		return 1;
	}
	else {
		sq_raise_error(vm, "Invalid halt id %d", halt.get_id());
		return SQ_ERROR;
	}
}


/**
 * Reads the halt_id slot of a laid-over list instance.
 */
static halthandle_t laid_over_halt(HSQUIRRELVM vm)
{
	uint16 id = 0;
	halthandle_t halt;
	if (SQ_SUCCEEDED(get_slot(vm, "halt_id", id, 1))) {
		halt.set_id(id);
	}
	return halt;
}

SQInteger halt_export_laid_over_list(HSQUIRRELVM vm)
{
	halthandle_t halt = param<halthandle_t>::get(vm, 1);
	if (halt.is_bound()) {
		push_instance(vm, "laid_over_list_x");
		set_slot(vm, "halt_id", halt.get_id());
		return 1;
	}
	else {
		sq_raise_error(vm, "Invalid halt id %d", halt.get_id());
		return SQ_ERROR;
	}
}

SQInteger laid_over_list_count(HSQUIRRELVM vm)
{
	halthandle_t halt = laid_over_halt(vm);
	if (!halt.is_bound()) {
		return SQ_ERROR;
	}
	return param<uint32>::push(vm, halt->get_laid_over().get_count());
}

SQInteger laid_over_list_next(HSQUIRRELVM vm)
{
	halthandle_t halt = laid_over_halt(vm);
	if (!halt.is_bound()) {
		return SQ_ERROR;
	}
	return generic_get_next(vm, halt->get_laid_over().get_count());
}

SQInteger laid_over_list_get(HSQUIRRELVM vm)
{
	halthandle_t halt = laid_over_halt(vm);
	if (!halt.is_bound()) {
		return SQ_ERROR;
	}
	const sint32 index = param<sint32>::get(vm, 2);
	if (index < 0) {
		sq_pushnull(vm);
		return 1;
	}
	slist_tpl<convoihandle_t>::iterator iter = halt->get_laid_over().begin();
	for (sint32 i = 0; i < index  &&  iter != halt->get_laid_over().end(); i++) {
		++iter;
	}
	if (iter != halt->get_laid_over().end()) {
		return param<convoihandle_t>::push(vm, *iter);
	}
	sq_pushnull(vm);
	return 1;
}


call_tool_init halt_set_name(halthandle_t halt, const char* name)
{
	if (!halt.is_bound()) {
		return "Invalid halt provided";
	}
	return command_rename(halt->get_owner(), 'h', halt.get_id(), name);
}


SQInteger halt_export_line_list(HSQUIRRELVM vm)
{
	halthandle_t halt = param<halthandle_t>::get(vm, 1);
	if (halt.is_bound()) {
		push_instance(vm, "line_list_x");
		set_slot(vm, "halt_id", halt.get_id());
		return 1;
	}
	else {
		sq_raise_error(vm, "Invalid halt id %d", halt.get_id());
		return SQ_ERROR;
	}
}


uint32 halt_get_capacity(const haltestelle_t *halt, const goods_desc_t *desc)
{
	// passenger has index 0, mail 1, everything else >=2
	return halt  &&  desc  ?  halt->get_capacity(min( desc->get_catg_index(), 2 )) : 0;
}

// 0: not connected
// 1: connected
// -1: undecided
sint8 is_halt_connected(const haltestelle_t *a, halthandle_t b, const goods_desc_t *desc)
{
	if (desc == 0  ||  a == NULL  ||  !b.is_bound()) {
		return 0;
	}
	return a->is_connected(b, desc->get_catg_index());
}


// compare two halts to find out if they are the same
SQInteger halt_compare(halthandle_t a, halthandle_t b)
{
	return (SQInteger)a.get_id() - (SQInteger)b.get_id();
}


/**
 * A scenario script runs on the server only. The cargo methods below mutate
 * the saved world directly instead of travelling as a network command, so a
 * connected client would not apply them and would desynchronise. Refuse the
 * call rather than create a silent desync. A client that joins later receives
 * the server's complete saved state, so mutating the world is safe while no
 * client is connected.
 */
static bool script_may_change_cargo()
{
	return !env_t::networkmode
		|| (env_t::server
			&& socket_list_t::get_connected_clients() == 0
			&& socket_list_t::get_playing_clients() == 0);
}


/**
 * Adds a normal cargo packet to this halt's waiting list.
 *
 * The packet is ordinary saved cargo: it participates in routing, loading,
 * collection and the ordinary fare calculation, and it is written to savegames
 * with the halt. The arrival time is preserved exactly as supplied, so a
 * scenario can model cargo that has already waited for a chosen interval.
 *
 * Usage:
 * @code
 * local ok = halt.add_cargo(goods, 100, destination, next_transfer,
 *                           world.get_time().ticks)
 * // optional: origin, delivery position, class
 * halt.add_cargo(goods, 100, destination, next_transfer, arrival_time,
 *                origin, coord(9, 5), 0)
 * @endcode
 *
 * @param goods goods type
 * @param amount quantity in goods units; must be a positive 32-bit quantity
 * @param destination ultimate destination halt
 * @param next_transfer halt at which the packet must leave its next convoy;
 *                      an unbound halt (for example halt_x(0)) means the destination
 * @param arrival_time tick at which the packet arrived at this halt
 * @param origin optional halt at which the journey began (default: this halt)
 * @param destination_pos optional delivery position, normally a consumer
 *                        industry position (default: the destination halt's position)
 * @param class optional cargo class; zero for goods (default: 0)
 * @returns true, or raises a script error if the arguments are not valid
 */
SQInteger halt_add_cargo(HSQUIRRELVM vm)
{
	if (!script_may_change_cargo())
	{
		return sq_raise_error(vm, "%s", translator::translate("Cargo cannot be added while a network client is connected"));
	}

	const halthandle_t halt = param<halthandle_t>::get(vm, 1);
	if (!halt.is_bound())
	{
		return sq_raise_error(vm, "%s", translator::translate("Invalid halt"));
	}
	const goods_desc_t *desc = param<const goods_desc_t*>::get(vm, 2);
	if (desc == NULL)
	{
		return sq_raise_error(vm, "%s", translator::translate("Unknown goods type"));
	}
	const sint64 amount_value = param<sint64>::get(vm, 3);
	if (amount_value <= 0 || amount_value > (sint64)UINT32_MAX_VALUE)
	{
		return sq_raise_error(vm, "%s", translator::translate("Cargo amount must be a positive 32-bit quantity"));
	}
	const uint32 amount = (uint32)amount_value;
	const halthandle_t ziel = param<halthandle_t>::get(vm, 4);
	if (!ziel.is_bound())
	{
		return sq_raise_error(vm, "%s", translator::translate("Invalid cargo destination halt"));
	}
	const halthandle_t zwischenziel = param<halthandle_t>::get(vm, 5);
	const sint64 arrival_time = param<sint64>::get(vm, 6);
	halthandle_t origin;
	koord zielpos = koord::invalid;
	sint64 class_value = 0;
	const SQInteger argc = sq_gettop(vm);
	if (argc >= 7)
	{
		origin = param<halthandle_t>::get(vm, 7);
	}
	if (argc >= 8)
	{
		zielpos = param<koord>::get(vm, 8);
	}
	if (argc >= 9)
	{
		class_value = param<sint64>::get(vm, 9);
	}
	if (!halt->is_enabled(desc))
	{
		return sq_raise_error(vm, "%s", translator::translate("This halt cannot store that cargo category"));
	}
	if (!ziel->is_enabled(desc))
	{
		return sq_raise_error(vm, "%s", translator::translate("The destination halt cannot receive that cargo category"));
	}
	if (class_value < 0 || class_value >= desc->get_number_of_classes())
	{
		return sq_raise_error(vm, translator::translate("Cargo class %lld is outside the range of the goods type"), (long long)class_value);
	}
	const uint8 g_class = (uint8)class_value;
	if (zielpos != koord::invalid && !welt->is_within_limits(zielpos))
	{
		return sq_raise_error(vm, "%s", translator::translate("Cargo destination position is outside the map"));
	}
	if (!halt->add_cargo(desc, amount, ziel, zwischenziel, arrival_time, origin, zielpos, g_class))
	{
		return sq_raise_error(vm, "%s", translator::translate("Cannot add cargo with these arguments"));
	}
	return param<bool>::push(vm, true);
}


/**
 * Discards all waiting and transferring cargo at this halt without booking
 * revenue or refunds. Factory transit statistics are decremented for the
 * discarded packets, and the current-wait snapshot is cleared while the
 * retained waiting-time samples are left alone. This is for deterministic
 * scenario setup.
 *
 * A scenario script runs on the server only, so this method is refused while a
 * network client is connected. @see halt_add_cargo
 */
SQInteger halt_clear_cargo(HSQUIRRELVM vm)
{
	if (!script_may_change_cargo())
	{
		return sq_raise_error(vm, "%s", translator::translate("Cargo cannot be cleared while a network client is connected"));
	}
	const halthandle_t halt = param<halthandle_t>::get(vm, 1);
	if (!halt.is_bound())
	{
		return sq_raise_error(vm, "%s", translator::translate("Invalid halt"));
	}
	halt->clear_cargo();
	return param<bool>::push(vm, true);
}


// vector_tpl<haltestelle_t::connection_t> const& halt_get_connections(const haltestelle_t *halt, const goods_desc_t* freight)
// {
// 	static vector_tpl<haltestelle_t::connection_t> dummy;
// 	dummy.clear();
// 	return freight ? halt->get_connections(freight->get_catg_index()) : dummy;
// }


void export_halt(HSQUIRRELVM vm)
{
	/**
	 * Implements iterator to iterate through the list of all halts on the map.
	 *
	 * Usage:
	 * @code
	 * local list = halt_list_x()
	 * foreach(halt in list) {
	 *     ... // halt is an instance of the halt_x class
	 * }
	 * @endcode
	 */
	create_class(vm, "halt_list_x");
	/**
	 * Meta-method to be used in foreach loops. Do not call them directly.
	 */
	register_function(vm, world_get_next_halt,     "_nexti",  2, "x o|i");
	/**
	 * Meta-method to be used in foreach loops. Do not call them directly.
	 * @typemask halt_x()
	 */
	register_function(vm, world_get_halt_by_index, "_get",    2, "xi");
	end_class(vm);

	/**
	 * Class to access halts / stations / bus stops.
	 */
	begin_class(vm, "halt_x", "extend_get,ingame_object");

	/**
	 * @returns if object is still valid.
	 */
	export_is_valid<const haltestelle_t*>(vm); //register_function("is_valid")
	/**
	 * Station name.
	 * @returns name
	 */
	register_method(vm, &haltestelle_t::get_name, "get_name");
	/**
	 * Sets station name.
	 * @ingroup rename_func
	 */
	register_method(vm, &halt_set_name, "set_name", true);

	/**
	 * Station owner.
	 * @returns owner
	 */
	register_method(vm, &haltestelle_t::get_owner, "get_owner");

	/**
	 * compare classes using metamethods
	 * @param halt the other halt
	 * @returns difference in the unique id of the halthandle
	 */
	register_method(vm, &halt_compare, "_cmp", true);

	/**
	 * Quick check if there is connection for certain freight to the other halt.
	 * @param halt the other halt
	 * @param freight_type freight type
	 * @return 0 - not connected, 1 - connected, -1 - undecided (call this method again later)
	 */
	register_method(vm, &is_halt_connected, "is_connected", true);

	/**
	 * Does this station accept this type of good?
	 * @param freight_type freight type
	 * @returns the answer to this question
	 */
	register_method<bool (haltestelle_t::*)(const goods_desc_t*) const>(vm, &haltestelle_t::is_enabled, "accepts_good", false);

	/**
	 * Get a monthly halt statistic.
	 * @param index one of the halt_stats constants
	 * @returns array with one entry per stored month (MAX_MONTHS), index [0]
	 *          corresponds to the current month; an invalid index returns an
	 *          empty array
	 */
	register_method(vm, &get_halt_stat, "get_stat", true);
	/**
	 * Get monthly statistics of the number of convoys that arrived at this
	 * halt. The name is historical; get_convoys reads the same series.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_arrived", freevariable<sint32>(HALT_CONVOIS_ARRIVED), true);
	/**
	 * Get monthly statistics of the total number of cargo units waiting at
	 * this halt, including passengers, mail and goods.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_waiting", freevariable<sint32>(HALT_WAITING), true);
	/**
	 * Get monthly statistics of the number of visitors who boarded or alighted
	 * here. Visitors are passengers on a non-commuting trip.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_visitors", freevariable<sint32>(HALT_VISITORS), true);
	/**
	 * Get monthly statistics of the number of commuters who boarded or
	 * alighted here. Commuters are passengers on a commuting trip.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_commuters", freevariable<sint32>(HALT_COMMUTERS), true);
	/**
	 * Get monthly statistics of number of happy passengers.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_happy", freevariable<sint32>(HALT_HAPPY), true);
	/**
	 * Get monthly statistics of number of unhappy passengers.
	 *
	 * These passengers could not start their journey as station was crowded.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_unhappy", freevariable<sint32>(HALT_UNHAPPY), true);
	/**
	 * Get monthly statistics of number of passengers with no-route.
	 *
	 * These passengers could not start their journey as they find no route to their destination.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_noroute", freevariable<sint32>(HALT_NOROUTE), true);
	/**
	 * Get monthly statistics of the number of passengers whose estimated
	 * journey time exceeded their tolerance.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_too_slow", freevariable<sint32>(HALT_TOO_SLOW), true);
	/**
	 * Get monthly statistics of the number of passengers who waited longer
	 * than their tolerance at this halt.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_too_waiting", freevariable<sint32>(HALT_TOO_WAITING), true);
	/**
	 * Get monthly statistics of number of convoys that stopped at this station.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_convoys", freevariable<sint32>(HALT_CONVOIS_ARRIVED), true);
	/**
	 * Get monthly statistics of the mail that started at this halt and
	 * reached its destination.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_mail_delivered", freevariable<sint32>(HALT_MAIL_DELIVERED), true);
	/**
	 * Get monthly statistics of the mail generated at this halt for which no
	 * route was found.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_mail_noroute", freevariable<sint32>(HALT_MAIL_NOROUTE), true);
	/**
	 * Get monthly statistics of the mail handling volume at this halt.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_mail_handling_volume", freevariable<sint32>(HALT_MAIL_HANDLING_VOLUME), true);
	/**
	 * Get monthly statistics of the goods handling volume at this halt.
	 * @returns array, index [0] corresponds to current month
	 */
	register_method_fv(vm, &get_halt_stat, "get_goods_handling_volume", freevariable<sint32>(HALT_GOODS_HANDLING_VOLUME), true);
	/**
	 * Exports list of convoys that stop at this halt.
	 * @typemask convoy_list_x()
	 */
	register_function(vm, &halt_export_convoy_list, "get_convoy_list", 1, param<halthandle_t>::typemask());
	/**
	 * Exports the stop's laid-over (loose-stock) convoys. Read-only.
	 * @typemask laid_over_list_x()
	 */
	register_function(vm, &halt_export_laid_over_list, "get_laid_over_list", 1, param<halthandle_t>::typemask());
	/**
	 * Exports list of lines that serve this halt.
	 * @typemask line_list_x()
	 */
	register_function(vm, &halt_export_line_list, "get_line_list", 1, param<halthandle_t>::typemask());
	/**
	 * Get list of tiles belonging to this station.
	 */
	register_method(vm, &haltestelle_t::get_tiles, "get_tile_list");
	/**
	 * Get list of factories connected to this station.
	 */
	register_method(vm, &haltestelle_t::get_fab_list, "get_factory_list");
	/**
	 * Returns amount of @p freight at this halt that is going to @p target
	 * @param freight freight type
	 * @param target coordinate of target
	 */
	register_method(vm, &haltestelle_t::get_ware_fuer_zielpos, "get_freight_to_dest");
	/**
	 * Adds a normal cargo packet to this halt's waiting list for scenario
	 * setup. The arrival time is preserved. The packet is ordinary saved cargo.
	 * Refused while a network client is connected, because this is not a
	 * network command.
	 * @param goods goods type
	 * @param amount quantity; must be a positive 32-bit quantity
	 * @param destination ultimate destination halt
	 * @param next_transfer halt at which the packet leaves its next convoy;
	 *                      an unbound halt (for example halt_x(0)) means the destination
	 * @param arrival_time tick at which the packet arrived here
	 * @param origin optional halt at which the journey began (default: this halt)
	 * @param destination_pos optional delivery position (default: destination halt position)
	 * @param class optional cargo class (default: 0)
	 * @returns true; raises a script error on invalid arguments
	 * @typemask bool(halt_x, good_desc_x, integer, halt_x, halt_x, integer, halt_x, coord, integer)
	 */
	register_function(vm, halt_add_cargo, "add_cargo", -6, "x t|x|y i t|x|y t|x|y i t|x|y t|x|y i");
	/**
	 * Discards all waiting and transferring cargo at this halt without booking
	 * revenue or refunds, for deterministic scenario setup. Refused while a
	 * network client is connected, because this is not a network command.
	 * @returns true; raises a script error on an invalid halt
	 */
	register_function(vm, halt_clear_cargo, "clear_cargo", 1, "x");
// 	/**
// 	 * Returns amount of @p freight at this halt that scheduled to @p stop
// 	 * @param freight freight type
// 	 * @param stop next transfer stop
// 	 */
// 	register_method(vm, &haltestelle_t::get_ware_fuer_zwischenziel, "get_freight_to_halt");
	/**
	 * Returns capacity of this halt for the given @p freight
	 * @param freight freight type
	 */
	register_method(vm, &halt_get_capacity, "get_capacity", true);
// 	/**
// 	 * Returns list of connected halts for the specific @p freight type.
// 	 * @param freight freight type
// 	 */
// 	register_method(vm, &halt_get_connections, "get_connections", true);
	/**
	 * Returns halt at given position.
	 * @param pos coordinate
	 * @param pl player that wants to use halt here
	 * @return halt instance
	 */
	STATIC register_method(vm, &get_halt_from_koord3d, "get_halt", false, true);

	end_class(vm);

	/**
	 * Read-only list of a stop's laid-over convoys.
	 *
	 * Usage:
	 * @code
	 * foreach(cnv in halt.get_laid_over_list()) {
	 *     ... // cnv is a convoy_x
	 * }
	 * @endcode
	 */
	begin_class(vm, "laid_over_list_x", 0);
	register_function(vm, laid_over_list_next,  "_nexti", 2, "x o|i");
	register_function(vm, laid_over_list_get,   "_get",   2, "xi");
	register_function(vm, laid_over_list_count, "get_count", 1, "x");
	end_class(vm);
}
