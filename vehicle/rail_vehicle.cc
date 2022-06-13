/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "rail_vehicle.h"

#include "simroadtraffic.h"

#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../simware.h"
#include "../simconvoi.h"
#include "../simworld.h"
#include "../boden/wege/schiene.h"
#include "../simdepot.h"
#include "../obj/roadsign.h"
#include "../obj/signal.h"
#include "../dataobj/schedule.h"
#include "../obj/crossing.h"

#include "../utils/cbuffer_t.h"
#include "../simfab.h"
#include "../simlinemgmt.h"
#include "../simmesg.h"
#include "../simsignalbox.h"
#include "../player/simplay.h"

#include "../path_explorer.h"


/* from now on rail vehicles (and other vehicles using blocks) */


#ifdef INLINE_OBJ_TYPE
rail_vehicle_t::rail_vehicle_t(typ type, loadsave_t *file, bool is_leading, bool is_last) :
    vehicle_t(type)
{
	init(file, is_leading, is_last);
}

rail_vehicle_t::rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
    vehicle_t(obj_t::rail_vehicle)
{
	init(file, is_leading, is_last);
}

void rail_vehicle_t::init(loadsave_t *file, bool is_leading, bool is_last)
#else
rail_vehicle_t::rail_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
    vehicle_t()
#endif
{
	rail_vehicle_t::rdwr_from_convoi(file);

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

			int power = (is_leading || empty || gd == goods_manager_t::none) ? 500 : 0;
			const goods_desc_t* w = empty ? goods_manager_t::none : gd;
			dbg->warning("rail_vehicle_t::rail_vehicle_t()","try to find a fitting vehicle for %s.", power>0 ? "engine": w->get_name() );
			if(last_desc!=NULL  &&  last_desc->can_follow(last_desc)  &&  last_desc->get_freight_type()==w  &&  (!is_last  ||  last_desc->get_trailer(0)==NULL)) {
				// same as previously ...
				desc = last_desc;
			}
			else {
				// we have to search
				desc = vehicle_builder_t::get_best_matching(get_waytype(), 0, w!=goods_manager_t::none?5000:0, power, speed_to_kmh(speed_limit), w, false, last_desc, is_last );
			}
			if(desc) {
DBG_MESSAGE("rail_vehicle_t::rail_vehicle_t()","replaced by %s",desc->get_name());
				calc_image();
			}
			else {
				dbg->error("rail_vehicle_t::rail_vehicle_t()","no matching desc found for %s!",w->get_name());
			}
			if (!empty && !fracht->empty() && fracht[0].front().menge == 0) {
				// this was only there to find a matching vehicle
				fracht[0].remove_first();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
	fix_class_accommodations();
}

#ifdef INLINE_OBJ_TYPE
rail_vehicle_t::rail_vehicle_t(typ type, koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
    vehicle_t(type, pos, desc, player)
{
    cnv = cn;
	working_method = drive_by_sight;
}
#endif

rail_vehicle_t::rail_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::rail_vehicle, pos, desc, player)
#else
	vehicle_t(pos, desc, player)
#endif
{
	cnv = cn;
	working_method = drive_by_sight;
}


rail_vehicle_t::~rail_vehicle_t()
{
	if (cnv && leading) {
		route_t & r = *cnv->get_route();
		if (!r.empty() && route_index < r.get_count()) {
			// free all reserved blocks
			uint16 dummy = 0;
			block_reserver(&r, cnv->back()->get_route_index(), dummy, dummy, target_halt.is_bound() ? 100000 : 1, false, false);
		}
	}
	grund_t *gr = welt->lookup(get_pos());
	if(gr) {
		schiene_t * sch = (schiene_t *)gr->get_weg(get_waytype());
		if(sch && !get_flag(obj_t::not_on_map)) {
			sch->unreserve(this);
		}
	}
}


void rail_vehicle_t::set_convoi(convoi_t *c)
{
	if(c!=cnv) {
		DBG_MESSAGE("rail_vehicle_t::set_convoi()","new=%p old=%p",c,cnv);
		if(leading) {
			if(cnv!=NULL  &&  cnv!=(convoi_t *)1) {
				// free route from old convoi

				route_t& r = *cnv->get_route();
				if(  !r.empty()  &&  route_index + 1U < r.get_count() - 1  ) {

					uint16 dummy = 0;
					block_reserver(&r, cnv->back()->get_route_index(), dummy, dummy, 100000, false, false);
					target_halt = halthandle_t();
				}
			}
			else if(c->get_next_reservation_index() == 0 && c->get_next_stop_index() == 0 && c->get_state() != convoi_t::REVERSING)
			{
				assert(c!=NULL);
				// eventually search new route
				route_t& r = *c->get_route();
				if(  (r.get_count()<=route_index  ||  r.empty()  ||  get_pos()==r.back())  &&  c->get_state()!=convoi_t::INITIAL  &&  !c->is_loading()  &&  c->get_state()!=convoi_t::SELF_DESTRUCT  ) {
					check_for_finish = true;
					dbg->warning("rail_vehicle_t::set_convoi()", "convoi %i had a too high route index! (%i of max %i)", c->self.get_id(), route_index, r.get_count() - 1);
				}
				// set default next stop index
				c->set_next_stop_index( max(route_index,1)-1 );
				// need to reserve new route?
				if(  !check_for_finish  &&  c->get_state()!=convoi_t::SELF_DESTRUCT  &&  (c->get_state()==convoi_t::DRIVING  ||  c->get_state()>=convoi_t::LEAVING_DEPOT)  ) {
					sint32 num_index = cnv==(convoi_t *)1 ? 1001 : 0; // only during loadtype: cnv==1 indicates, that the convoi did reserve a stop
					uint16 next_signal;
					cnv = c;
					if(  block_reserver(&r, max(route_index,1)-1, world()->get_settings().get_sighting_distance_tiles(), next_signal, num_index, true, false)  ) {
						c->set_next_stop_index(next_signal);
					}
				}
			}
		}
		vehicle_t::set_convoi(c);
	}
}


// need to reset halt reservation (if there was one)
route_t::route_result_t rail_vehicle_t::calc_route(koord3d start, koord3d ziel, sint32 max_speed, bool is_tall, route_t* route)
{
	if(leading  &&  route_index<cnv->get_route()->get_count()) {
		// free all reserved blocks
		if (cnv->get_allow_clear_reservation() && (working_method == one_train_staff || working_method == token_block))
		{
			// These cannot sensibly be resumed inside a section
			cnv->set_working_method(drive_by_sight);
			cnv->unreserve_route();
			cnv->reserve_own_tiles();
		}
		else
		{
			uint16 dummy = 0;
			block_reserver(cnv->get_route(), cnv->back()->get_route_index(), dummy, dummy, target_halt.is_bound() ? 100000 : 1, false, true);
		}
	}
	cnv->set_next_reservation_index( 0 ); // nothing to reserve
	target_halt = halthandle_t(); // no block reserved
	// use length > 8888 tiles to advance to the end of terminus stations
	const sint16 tile_length = (cnv->get_schedule()->get_current_entry().reverse == 1 ? 8888 : 0) + cnv->get_true_tile_length();
	route_t::route_result_t r = route->calc_route(welt, start, ziel, this, max_speed, cnv != NULL ? cnv->get_highest_axle_load() : ((get_sum_weight() + 499) / 1000), is_tall, tile_length, SINT64_MAX_VALUE, cnv ? cnv->get_weight_summary().weight / 1000 : get_total_weight());
	cnv->set_next_stop_index(0);
 	if(r == route_t::valid_route_halt_too_short)
	{
		cbuffer_t buf;
		buf.printf( translator::translate("Vehicle %s cannot choose because stop too short!"), cnv ? cnv->get_name() : "Invalid convoy");
		world()->get_message()->add_message( (const char *)buf, ziel.get_2d(), message_t::warnings, PLAYER_FLAG | cnv->get_owner()->get_player_nr(), cnv->front()->get_base_image() );
	}
	return r;
}


bool rail_vehicle_t::check_next_tile(const grund_t *bd) const
{
	if(!bd) return false;
	schiene_t const* const sch = obj_cast<schiene_t>(bd->get_weg(get_waytype()));
	if(  !sch  ) {
		return false;
	}

	// diesel and steam engines can use electrified track as well.
	// also allow driving on foreign tracks ...
	const bool needs_no_electric = !(cnv!=NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric);

	if((!needs_no_electric && !sch->is_electrified()) || (sch->get_max_speed(needs_no_electric) == 0 && speed_limit < INT_MAX) || (cnv ? !cnv->check_way_constraints_of_all_vehicles(*sch) : !check_way_constraints(*sch)))
	{
		return false;
	}

	if (depot_t *depot = bd->get_depot()) {
		if (depot->get_waytype() != desc->get_waytype()  ||  depot->get_owner() != get_owner()) {
			return false;
		}
	}
	// now check for special signs
	if(sch->has_sign()) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(rs && rs->get_desc()->get_wtyp()==get_waytype()  ) {
			if(  cnv != NULL  &&  rs->get_desc()->get_min_speed() > 0  &&  rs->get_desc()->get_min_speed() > cnv->get_min_top_speed()  ) {
				// below speed limit
				return false;
			}
			if(  rs->get_desc()->is_private_way()  &&  (rs->get_player_mask() & (1<<get_owner_nr()) ) == 0  ) {
				// private road
				return false;
			}
		}
	}

	bool check_reservation = true;

	const koord dir = bd->get_pos().get_2d() - get_pos().get_2d();
	const ribi_t::ribi ribi = ribi_type(dir);

	if(target_halt.is_bound() && cnv && cnv->is_waiting())
	{
		// we are searching a stop here:
		// ok, we can go where we already are ...
		if(bd->get_pos()==get_pos()) {
			return true;
		}
		// but we can only use empty blocks ...
		// now check, if we could enter here
		check_reservation = sch->can_reserve(cnv->self, ribi);
	}

	if(cnv && cnv->get_is_choosing())
	{
		check_reservation = sch->can_reserve(cnv->self, ribi);
	}

	return check_reservation && check_access(sch);
}


// how expensive to go here (for way search)
int rail_vehicle_t::get_cost(const grund_t *gr, const sint32 max_speed, koord from_pos)
{
	// first favor faster ways
	const weg_t *w = gr->get_weg(get_waytype());
	if(  w==NULL  ) {
		// only occurs when deletion during waysearch
		return 9999;
	}

	// add cost for going (with maximum speed, cost is 10)
	const bool needs_no_electric = !(cnv != NULL ? cnv->needs_electrification() : desc->get_engine_type() == vehicle_desc_t::electric);
	const sint32 max_tile_speed = w->get_max_speed(needs_no_electric);
	int costs = (max_speed <= max_tile_speed) ? 10 : 40 - (30 * max_tile_speed) / max_speed;
	if (desc->get_override_way_speed())
	{
		costs = 1;
	}

	// effect of slope
	if(  gr->get_weg_hang()!=0  ) {
		// check if the slope is upwards relative to the previous tile
		from_pos -= gr->get_pos().get_2d();
		// 125 hardcoded, see get_cost_upslope()
		costs += 125 * slope_t::get_sloping_upwards( gr->get_weg_hang(), from_pos.x, from_pos.y );
	}

	// Strongly prefer routes for which the vehicle is not overweight.
	uint32 max_axle_load = w->get_max_axle_load();
	uint32 bridge_weight_limit = w->get_bridge_weight_limit();
	if(welt->get_settings().get_enforce_weight_limits() == 3 || (welt->get_settings().get_enforce_weight_limits() == 1 && cnv && (cnv->get_highest_axle_load() > max_axle_load || (cnv->get_weight_summary().weight / 1000) > bridge_weight_limit) ) )
	{
		costs += 400;
	}

	if(w->is_diagonal())
	{
		// Diagonals are a *shorter* distance.
		costs = (costs * 5) / 7; // was: costs /= 1.4
	}

	return costs;
}


// this routine is called by find_route, to determined if we reached a destination
bool rail_vehicle_t::is_target(const grund_t *gr,const grund_t *prev_gr)
{
	const schiene_t * sch1 = (const schiene_t *) gr->get_weg(get_waytype());
	ribi_t::ribi ribi_maybe;
	if(prev_gr)
	{
		ribi_maybe = ribi_type(gr->get_pos(), prev_gr->get_pos());
	}
	else
	{
		ribi_maybe = ribi_t::all;
	}
	// first check blocks, if we can go there
	if(  sch1->can_reserve(cnv->self, ribi_maybe)  ) {
		//  just check, if we reached a free stop position of this halt
		if(  gr->is_halt()  &&  gr->get_halt()==target_halt  ) {
			// now we must check the predecessor ...
			if(  prev_gr!=NULL  ) {
				const koord3d dir=gr->get_pos()-prev_gr->get_pos();
				const ribi_t::ribi ribi = ribi_type(dir);
				signal_t* sig = gr->find<signal_t>();
				if(!sig && gr->get_weg(get_waytype())->get_ribi_maske() & ribi)
				{
					// Unidirectional signals allow routing in both directions but only act in one direction. Check whether this is one of those.
					// one way sign wrong direction
					return false;
				}
				grund_t *to;
				if(  !gr->get_neighbour(to,get_waytype(),ribi)  ||  !(to->get_halt()==target_halt)  ||  (!sig && to->get_weg(get_waytype())->get_ribi_maske() & ribi_type(dir))!=0  ) {
					// end of stop: Is it long enough?
					// end of stop could be also signal!
					uint16 tiles = cnv->get_tile_length();
					while(  tiles>1  ) {
						if(  (!sig && gr->get_weg(get_waytype())->get_ribi_maske() & ribi)  ||  !gr->get_neighbour(to,get_waytype(),ribi_t::backward(ribi))  ||  !(to->get_halt()==target_halt)  ) {
							return false;
						}
						gr = to;
						tiles --;
					}
					return true;
				}
			}
		}
	}
	return false;
}

sint32 rail_vehicle_t::activate_choose_signal(const uint16 start_block, uint16 &next_signal_index, uint32 brake_steps, uint16 modified_sighting_distance_tiles, route_t* route, sint32 modified_route_index)
{
	const schedule_t* schedule = cnv->get_schedule();
	grund_t const* target = welt->lookup(schedule->get_current_entry().pos);

	if(target == NULL)
	{
		cnv->suche_neue_route();
		return 0;
	}

	bool choose_ok = true;

	// TODO: Add option in the convoy's schedule to skip choose signals, and implement this here.

	// check whether there is another choose signal or end_of_choose on the route
	uint32 break_index = start_block + 1;
	for(uint32 idx = break_index; choose_ok && idx < route->get_count(); idx++)
	{
		grund_t *gr = welt->lookup(route->at(idx));
		if(!gr)
		{
			choose_ok = false;
			break_index = idx;
			break;
		}
		if(gr->get_halt() == target->get_halt())
		{
			break_index = idx;
			break;
		}
		weg_t *way = gr->get_weg(get_waytype());
		if(!way)
		{
			choose_ok = false;
			break_index = idx;
			break;
		}
		if(way->has_sign())
		{
			roadsign_t *rs = gr->find<roadsign_t>(1);
			if(rs && rs->get_desc()->get_wtyp() == get_waytype())
			{
				if(rs->get_desc()->is_end_choose_signal())
				{
					if (!(gr->is_halt() && gr->get_halt() != target->get_halt()))
					{
						// Ignore end of choose signals on platforms: these make no sense
						// and cause problems.
						target = gr;
						break_index = idx;
						break;
					}
				}
			}
		}
		if(way->has_signal())
		{
			signal_t *sig = gr->find<signal_t>(1);
 			ribi_t::ribi ribi = ribi_type(route->at(max(1u, modified_route_index) - 1u));
			if(!(gr->get_weg(get_waytype())->get_ribi_maske() & ribi) && gr->get_weg(get_waytype())->get_ribi_maske() != ribi_t::backward(ribi)) // Check that the signal is facing in the right direction.
			{
				if(sig && sig->get_desc()->is_choose_sign())
				{
					// second choose signal on route => not choosing here
					choose_ok = false;
				}
			}
		}
	}

	if(!choose_ok)
	{
		// No need to check the block reserver here as in the old is_choose_signal_clear() method,
		// as this is now called from the block reserver.
		return 0;
	}

	// Choose logic hereafter
	target_halt = target->get_halt();

	// The standard route is not available (which we can assume from the method having been called): try a new route.

	// We are in a step and can use the route search
	route_t target_rt;
	const uint16 first_block = start_block == 0 ? start_block : start_block - 1;
	const uint16 second_block = start_block == 0 ? start_block + 1 : start_block;
	uint16 third_block = start_block == 0 ? start_block + 2 : start_block + 1;
	third_block = min(third_block, route->get_count() - 1);
	const koord3d first_tile = route->at(first_block);
	const koord3d second_tile = route->at(second_block);
	const koord3d third_tile = route->at(third_block);
	const grund_t* third_ground = welt->lookup(third_tile);
	uint8 direction = ribi_type(first_tile, second_tile);

	if (third_ground)
	{
		const schiene_t* railway = (schiene_t*)third_ground->get_weg(get_waytype());
		if (railway && railway->is_junction())
		{
			ribi_t::ribi old_direction = direction;
			direction |= ribi_t::all;
			direction ^= ribi_t::backward(old_direction);
		}
	}
	direction |= ribi_type(second_tile, third_tile);
	direction |= welt->lookup(second_tile)->get_weg(get_waytype())->get_ribi_unmasked();
	cnv->set_is_choosing(true);
	bool can_find_route;

	if(target_halt.is_bound())
	{
		// The target is a stop.
		uint32 route_depth;
		const sint32 route_steps = welt->get_settings().get_max_choose_route_steps();
		if(route_steps)
		{
			route_depth = route_steps;
		}
		else
		{
			route_depth = (welt->get_size().x + welt->get_size().y) * 1000;
		}

		can_find_route = target_rt.find_route(welt, route->at(start_block), this, speed_to_kmh(cnv->get_min_top_speed()), direction, cnv->get_highest_axle_load(), cnv->get_tile_length(), cnv->get_weight_summary().weight / 1000, route_depth, cnv->has_tall_vehicles(), route_t::choose_signal);
	}
	else
	{
		// The target is an end of choose sign along the route.
		can_find_route = target_rt.calc_route(welt, route->at(start_block), target->get_pos(), this, speed_to_kmh(cnv->get_min_top_speed()), cnv->get_highest_axle_load(), cnv->has_tall_vehicles(), cnv->get_tile_length(), SINT64_MAX_VALUE, cnv->get_weight_summary().weight / 1000, koord3d::invalid, direction) == route_t::valid_route;
		// This route only takes us to the end of choose sign, so we must calculate the route again beyond that point to the actual destination then concatenate them.
		if(can_find_route)
		{
			// Merge this part route with the remaining route
			route_t intermediate_route;
			intermediate_route.append(route);
			intermediate_route.remove_koord_to(break_index);
			target_rt.append(&intermediate_route);
		}
	}
	sint32 blocks;
	if(!can_find_route)
	{
		// nothing empty or not route with less than welt->get_settings().get_max_choose_route_steps() tiles (if applicable)
		target_halt = halthandle_t();
		cnv->set_is_choosing(false);
		return 0;
	}
	else
	{
		// try to reserve the whole route
		cnv->update_route(start_block, target_rt);
		blocks = block_reserver(route, start_block, modified_sighting_distance_tiles, next_signal_index, 100000, true, false, true, false, false, false, brake_steps);
		if(!blocks)
		{
			target_halt = halthandle_t();
		}
	}
	cnv->set_is_choosing(false);
	return blocks;
}


bool rail_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	assert(leading);
	assert(cnv->get_state() != convoi_t::ROUTING_1 && cnv->get_state() != convoi_t::ROUTING_2);
	cnv = get_convoi(); // This should not be necessary, but for some unfathomable reason, cnv is sometimes not equal to get_convoi(), even though get_convoi() just returns cnv (!?!)
	uint16 next_signal = INVALID_INDEX;
	schedule_entry_t destination = cnv->get_schedule()->get_current_entry();

	bool destination_is_nonreversing_waypoint = !destination.reverse && !haltestelle_t::get_halt(destination.pos, get_owner()).is_bound() && (!welt->lookup(destination.pos) || !welt->lookup(destination.pos)->get_depot());

	bool exiting_one_train_staff = false;

	schiene_t *w = (schiene_t*)gr->get_weg(get_waytype());
	if(w == NULL)
	{
		return false;
	}

	// We need gr_current because gr is pos_next
	grund_t *gr_current = welt->lookup(get_pos());
	schiene_t *w_current = gr_current ?  (schiene_t*)gr_current->get_weg(get_waytype()) : NULL;

	if(w_current == NULL)
	{
		return false;
	}

	ribi_t::ribi ribi = ribi_type(cnv->get_route()->at(max(1u, min(cnv->get_route()->get_count() - 1u, route_index)) - 1u), cnv->get_route()->at(min(cnv->get_route()->get_count() - 1u, route_index + 1u)));
	bool nonadjacent_one_train_staff_cabinet = false;

	if(working_method == one_train_staff && cnv->get_state() != convoi_t::LEAVING_DEPOT)
	{
		signal_t* signal = w->get_signal(ribi);
		if(signal && signal->get_desc()->get_working_method() == one_train_staff)
		{
			// Ignore cabinets distant from the triggering cabinet
			const koord3d first_pos = cnv->get_last_signal_pos();
			if (shortest_distance(get_pos().get_2d(), first_pos.get_2d()) < 3)
			{
				signal->set_state(roadsign_t::call_on); // Do not use the same cabinet to switch back to drive by sight immediately after releasing.
				clear_token_reservation(signal, this, w);
				cnv->set_working_method(drive_by_sight);
				exiting_one_train_staff = true;
			}
			else if(first_pos != koord3d::invalid)
			{
				nonadjacent_one_train_staff_cabinet = true;
			}
		}
	}

	signal_t* signal_current = NULL;
	if (!nonadjacent_one_train_staff_cabinet)
	{
		signal_current = w_current->get_signal(ribi);
		if (signal_current && signal_current->get_desc()->get_working_method() == one_train_staff)
		{
			// Ignore cabinets distant from the triggering cabinet
			const koord3d first_pos = cnv->get_last_signal_pos();
			if (first_pos != koord3d::invalid && shortest_distance(get_pos().get_2d(), first_pos.get_2d()) >= 3)
			{
				nonadjacent_one_train_staff_cabinet = true;
				signal_current = NULL;
			}
		}
	}

	nonadjacent_one_train_staff_cabinet == false ? w_current->get_signal(ribi) : NULL;

	if(cnv->get_state() == convoi_t::CAN_START)
	{
		if(working_method != token_block && working_method != one_train_staff)
		{
			cnv->set_working_method(drive_by_sight);
		}

		if(signal_current && working_method != token_block)
		{
			if(working_method != one_train_staff && (signal_current->get_desc()->get_working_method() != one_train_staff || signal_current->get_pos() != cnv->get_last_signal_pos()))
			{
				cnv->set_working_method(signal_current->get_desc()->get_working_method());
			}
		}
	}

	halthandle_t this_halt = haltestelle_t::get_halt(get_pos(), get_owner());
	bool this_halt_has_station_signals = this_halt.is_bound() && this_halt->get_station_signals_count();

	const bool starting_from_stand = cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH
		|| cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS
		|| cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE
		|| cnv->get_state() == convoi_t::CAN_START
		|| cnv->get_state() == convoi_t::CAN_START_ONE_MONTH
		|| cnv->get_state() == convoi_t::CAN_START_TWO_MONTHS
		|| cnv->get_state() == convoi_t::REVERSING;

	// Check for starter signals at the station.
	if (!signal_current && starting_from_stand && this_halt.is_bound() && !this_halt_has_station_signals)
	{
		const uint32 route_count = cnv->get_route()->get_count();
		for (uint32 i = route_index; i < route_count; i ++)
		{
			const koord3d tile_to_check_ahead = cnv->get_route()->at(min(route_count - 1u, i));
			grund_t *gr_ahead = welt->lookup(tile_to_check_ahead);
			weg_t *way = gr_ahead ? gr_ahead->get_weg(get_waytype()) : NULL;
			if (!way)
			{
				// This may happen if a way has been removed since the route was calculated. Must recalculate the route.
				break;
			}
			uint16 modified_route_index = min(route_index + i, route_count - 1u);
			ribi_t::ribi ribi = ribi_type(cnv->get_route()->at(max(1u, modified_route_index) - 1u), cnv->get_route()->at(min(cnv->get_route()->get_count() - 1u, modified_route_index + 1u)));
			signal_t* sig = way->get_signal(ribi);
			if (sig)
			{
				signal_current = sig;
				// Check for non-adjacent one train staff cabinets - these should be ignored.
				const koord3d first_pos = cnv->get_last_signal_pos();
				if (shortest_distance(get_pos().get_2d(), first_pos.get_2d()) < 3)
				{
					break;
				}
				else if (first_pos != koord3d::invalid)
				{
					nonadjacent_one_train_staff_cabinet = true;
				}
			}
			if (gr_ahead->get_halt() != this_halt)
			{
				break;
			}
		}
	}

	if(signal_current && (signal_current->get_desc()->get_working_method() == time_interval || signal_current->get_desc()->get_working_method() == time_interval_with_telegraph) && signal_current->get_state() == roadsign_t::danger && !signal_current->get_desc()->is_choose_sign() && signal_current->get_no_junctions_to_next_signal() && (signal_current->get_desc()->get_working_method() != one_train_staff || !starting_from_stand))
	{
		restart_speed = 0;
		return false;
	}

	if (signal_current && signal_current->get_desc()->get_working_method() == one_train_staff && cnv->get_state() == convoi_t::DRIVING && signal_current->get_state() != signal_t::call_on)
	{
		// This should only be encountered when a train comes upon a one train staff cabinet having previously stopped at a double block signal or having started from a station.
		if (working_method == drive_by_sight)
		{
			cnv->set_last_signal_pos(koord3d::invalid);
			const bool ok = block_reserver(cnv->get_route(), max(route_index, 1), welt->get_settings().get_sighting_distance_tiles(), next_signal, 0, true, false);
			if (!ok)
			{
				restart_speed = 0;
				return false;
			}
		}
		else
		{
			cnv->set_working_method(one_train_staff);
		}
	}

	if((destination_is_nonreversing_waypoint || starting_from_stand) && working_method != one_train_staff && (signal_current || this_halt_has_station_signals) && (this_halt_has_station_signals || !signal_current->get_desc()->get_permissive() || signal_current->get_no_junctions_to_next_signal() == false))
	{
		bool allow_block_reserver = true;
		if (this_halt_has_station_signals)
		{
			// We need to check whether this is a station signal that does not protect any junctions: if so, just check its state, do not call the block reserver.
			enum station_signal_status { none, forward, inverse };
			station_signal_status station_signal = station_signal_status::none;
			signal_t* sig = NULL;
			for(uint32 k = 0; k < this_halt->get_station_signals_count(); k ++)
			{
				grund_t* gr_check = welt->lookup(this_halt->get_station_signal(k));
				if(gr_check)
				{
					weg_t* way_check = gr_check->get_weg(get_waytype());
					sig = way_check ? way_check->get_signal(ribi) : NULL;
					if(sig)
					{
						station_signal = station_signal_status::forward;
						break;
					}
					else
					{
						// Check the opposite direction as station signals work in the exact opposite direction
						sig = gr_check->get_weg(get_waytype())->get_signal(ribi_t::backward(ribi));
						if(sig)
						{
							station_signal = station_signal_status::inverse;
							break;
						}
					}
				}
			}

			if (sig)
			{
				if ((sig->get_desc()->get_working_method() == working_method_t::time_interval || sig->get_desc()->get_working_method() == working_method_t::time_interval_with_telegraph) && sig->get_no_junctions_to_next_signal() && !sig->get_desc()->is_choose_sign())
				{
					// A time interval signal on plain track - do not engage the block reserver
					allow_block_reserver = false;
					const sint64 last_passed = this_halt->get_train_last_departed(ribi);

					const sint64 caution_interval_ticks = welt->get_seconds_to_ticks(welt->get_settings().get_time_interval_seconds_to_caution());
					const sint64 clear_interval_ticks =  welt->get_seconds_to_ticks(welt->get_settings().get_time_interval_seconds_to_clear());
					const sint64 ticks = welt->get_ticks();

					cnv->set_working_method(sig->get_desc()->get_working_method());

					if(last_passed + caution_interval_ticks > ticks)
					{
						// Danger
						sig->set_state(signal_t::danger);
						restart_speed = 0;
						return false;
					}
					else if(last_passed + clear_interval_ticks > ticks)
					{
						// Caution
						cnv->set_maximum_signal_speed(min(kmh_to_speed(w_current->get_max_speed()) / 2, sig->get_desc()->get_max_speed() / 2));
						sig->set_state(station_signal == station_signal_status::inverse ? roadsign_t::caution : roadsign_t::caution_no_choose);
						find_next_signal(cnv->get_route(),  max(route_index, 1) - 1, next_signal);
						if (next_signal <= route_index)
						{
							find_next_signal(cnv->get_route(),  max(route_index, 1), next_signal);
						}
					}
					else
					{
						// Clear
						cnv->set_maximum_signal_speed(kmh_to_speed(sig->get_desc()->get_max_speed()));
						sig->set_state(station_signal == station_signal_status::inverse ? roadsign_t::clear : roadsign_t::clear_no_choose);
						find_next_signal(cnv->get_route(),  max(route_index, 1) - 1, next_signal);
						if (next_signal <= route_index)
						{
							find_next_signal(cnv->get_route(),  max(route_index, 1), next_signal);
						}
					}
				}
			}
			else
			{
				// There are no station signals facing in the correct direction
				this_halt_has_station_signals = false;
			}

		}

		if (allow_block_reserver && !block_reserver(cnv->get_route(), max(route_index, 1) - 1, welt->get_settings().get_sighting_distance_tiles(), next_signal, 0, true, false))
		{
			restart_speed = 0;
			return false;
		}
		if (allow_block_reserver && next_signal <= route_index)
		{
			const bool ok = block_reserver(cnv->get_route(), max(route_index, 1), welt->get_settings().get_sighting_distance_tiles(), next_signal, 0, true, false);
			if (!ok)
			{
				restart_speed = 0;
				return false;
			}
		}
		if(!destination_is_nonreversing_waypoint || (cnv->get_state() != convoi_t::CAN_START && cnv->get_state() != convoi_t::CAN_START_ONE_MONTH && cnv->get_state() != convoi_t::CAN_START_TWO_MONTHS))
		{
			cnv->set_next_stop_index(next_signal);
		}
		if (working_method != one_train_staff && signal_current && (signal_current->get_desc()->get_working_method() != one_train_staff || (signal_current->get_pos() != cnv->get_last_signal_pos() && signal_current->get_pos() == get_pos())))
		{
			if (working_method == token_block && signal_current->get_desc()->get_working_method() != token_block)
			{
				clear_token_reservation(w_current->get_signal(ribi), this, w_current);
			}
			cnv->set_working_method(signal_current->get_desc()->get_working_method());
		}
		return true;
	}

	if(starting_from_stand && working_method != one_train_staff)
 	{
		cnv->set_next_stop_index(max(route_index, 1) - 1);
		if(steps < steps_next && !this_halt.is_bound())
		{
			// not yet at tile border => can drive to signal safely
 			return true;
 		}
	}

	if(gr->get_top() > 250)
	{
		// too many objects here
		return false;
	}

	//const signal_t* signal_next_tile = welt->lookup(cnv->get_route()->at(min(cnv->get_route()->get_count() - 1u, route_index)))->find<signal_t>();

	if(starting_from_stand && cnv->get_next_stop_index() <= route_index /*&& !signal_next_tile*/ && !signal_current && working_method != drive_by_sight && working_method != moving_block)
	{
		// If we are starting from stand, have no reservation beyond here and there is no signal, assume that it has been deleted and revert to drive by sight.
		// This might also occur when a train in the time interval working method has had a collision from the rear and has gone into an emergency stop.
		if (working_method == token_block || working_method == one_train_staff)
		{
			cnv->set_next_stop_index(INVALID_INDEX);
		}
		else
		{
			cnv->set_working_method(drive_by_sight);
		}
	}

	const koord3d dir = gr->get_pos() - get_pos();
	ribi = ribi_type(dir);
	if(!w->can_reserve(cnv->self, ribi))
	{
		restart_speed = 0;
		if(((working_method == time_interval || working_method == time_interval_with_telegraph) && cnv->get_state() == convoi_t::DRIVING && !(signal_current && signal_current->get_state() == roadsign_t::danger)))
		{
			const sint32 emergency_stop_duration = welt->get_seconds_to_ticks(welt->get_settings().get_time_interval_seconds_to_caution() / 2);
			convoihandle_t c = w->get_reserved_convoi();
			const koord3d ground_pos = gr->get_pos();
			for(sint32 i = 0; i < c->get_vehicle_count(); i ++)
			{
				if(c->get_vehicle(i)->get_pos() == ground_pos)
				{
					cnv->set_state(convoi_t::EMERGENCY_STOP);
					cnv->set_wait_lock(emergency_stop_duration + 2000); // We add 2,000ms to what we assume is the rear train to ensure that the front train starts first.
					cnv->set_working_method(drive_by_sight);
					cnv->unreserve_route();
					cnv->reserve_own_tiles();
					if(c.is_bound() && !this_halt_has_station_signals)
					{
						c->set_state(convoi_t::EMERGENCY_STOP);
						c->set_wait_lock(emergency_stop_duration);
						/*c->unreserve_route();
						c->reserve_own_tiles();*/
					}
					break;
				}
			}
		}
		else if (working_method == drive_by_sight)
		{
			// Check for head-on collisions in drive by sight mode so as to fix deadlocks automatically.
			convoihandle_t c = w->get_reserved_convoi();
			if (c.is_bound() && (c->get_state() == convoi_t::DRIVING || c->get_state() == convoi_t::WAITING_FOR_CLEARANCE|| c->get_state() == convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH || c->get_state() == convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS) &&
				(cnv->get_state() == convoi_t::DRIVING || cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE|| cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_ONE_MONTH || cnv->get_state() == convoi_t::WAITING_FOR_CLEARANCE_TWO_MONTHS) )
			{
				ribi_t::ribi other_convoy_direction = c->front()->get_direction();
				const depot_t* dep = gr->get_depot();
				if (c->front()->get_pos_next() == get_pos() && get_pos_next() == c->front()->get_pos() && !dep && other_convoy_direction != get_direction())
				{
					// Opposite directions detected
					// We must decide whether this or the other convoy should reverse.
					convoihandle_t lowest_numbered_convoy;
					convoihandle_t highest_numbered_convoy;
					if (cnv->self.get_id() < c->self.get_id())
					{
						lowest_numbered_convoy = cnv->self;
						highest_numbered_convoy = c->self;
					}
					else
					{
						lowest_numbered_convoy = c->self;
						highest_numbered_convoy = cnv->self;
					}

					const koord this_pos = lowest_numbered_convoy->get_pos().get_2d();
					const koord lowest_numbered_last_stop_pos = lowest_numbered_convoy->get_schedule()->get_prev_halt(lowest_numbered_convoy->get_owner()).is_bound() ? lowest_numbered_convoy->get_schedule()->get_prev_halt(lowest_numbered_convoy->get_owner())->get_next_pos(this_pos) : this_pos;
					const koord highest_numbered_last_stop_pos = highest_numbered_convoy->get_schedule()->get_prev_halt(highest_numbered_convoy->get_owner()).is_bound() ? highest_numbered_convoy->get_schedule()->get_prev_halt(highest_numbered_convoy->get_owner())->get_next_pos(this_pos) : this_pos;

					const uint16 lowest_numbered_convoy_distance_to_stop = shortest_distance(this_pos, lowest_numbered_last_stop_pos);
					const uint16 highest_numbered_convoy_distance_to_stop = shortest_distance(this_pos, highest_numbered_last_stop_pos);

					if (lowest_numbered_convoy_distance_to_stop <= highest_numbered_convoy_distance_to_stop)
					{
						// The lowest numbered convoy reverses.
						if (lowest_numbered_convoy == cnv->self)
						{
							cnv->is_reversed() ? cnv->get_schedule()->advance() : cnv->get_schedule()->advance_reverse();
							cnv->suche_neue_route();
						}
					}
					else
					{
						// The highest numbered convoy reverses
						if (highest_numbered_convoy == cnv->self)
						{
							cnv->is_reversed() ? cnv->get_schedule()->advance() : cnv->get_schedule()->advance_reverse();
							cnv->suche_neue_route();
						}
					}
				}
			}
		}
		return false;
	}

	convoi_t &convoy = *cnv;
	sint32 brake_steps = convoy.calc_min_braking_distance(welt->get_settings(), convoy.get_weight_summary(), cnv->get_akt_speed());
	route_t &route = *cnv->get_route();
	convoi_t::route_infos_t &route_infos = cnv->get_route_infos();

	const uint16 sighting_distance_tiles = welt->get_settings().get_sighting_distance_tiles();
	uint16 modified_sighting_distance_tiles = sighting_distance_tiles;

	sint32 last_index = max (0, route_index - 1);
	for(uint32 i = route_index; i <= min(route_index + sighting_distance_tiles, route.get_count() - 1); i++)
	{
		koord3d i_pos = route.at(i);
		ribi_t::ribi old_dir = calc_direction(route.at(route_index), route.at(min(route.get_count() - 1, route_index + 1)));
		ribi_t::ribi new_dir = calc_direction(route.at(last_index), i_pos);

		const grund_t* gr_new = welt->lookup(i_pos);
		grund_t* gr_bridge = welt->lookup(koord3d(i_pos.x, i_pos.y, i_pos.z + 1));
		if(!gr_bridge)
		{
			gr_bridge = welt->lookup(koord3d(i_pos.x, i_pos.y, i_pos.z + 2));
		}

		slope_t::type old_hang = gr->get_weg_hang();
		slope_t::type new_hang = gr_new ? gr_new->get_weg_hang() : old_hang;

		bool corner			= !(old_dir & new_dir);
		bool different_hill = old_hang != new_hang;
		bool overbridge		= gr_bridge && gr_bridge->ist_bruecke();

		if(w_current->is_diagonal())
		{
			const grund_t* gr_diagonal = welt->lookup(i_pos);
			const schiene_t* sch2 = gr_diagonal ? (schiene_t*)gr_diagonal->get_weg(get_waytype()) : NULL;
			if(sch2 && sch2->is_diagonal())
			{
				ribi_t::ribi adv_dir = calc_direction(route.at(i), route.at(min(i + 1, route.get_count() - 1)));
				ribi_t::ribi mix_dir = adv_dir | new_dir;
				corner = !(mix_dir & old_dir);
			}
			else
			{
				corner = true;
			}
		}

		if(corner || different_hill || overbridge)
		{
			modified_sighting_distance_tiles = max(i - route_index, 1);
			break;
		}
		last_index = i;
	}

	// is there any signal/crossing to be reserved?
	sint32 next_block = (sint32)cnv->get_next_stop_index() - 1;
	last_index = route.get_count() - 1;

	if(next_block > last_index && !exiting_one_train_staff && !(working_method == one_train_staff && next_block >= INVALID_INDEX)) // last_index is a waypoint and we need to keep routing.
	{
		const sint32 route_steps = route_infos.get_element(last_index).steps_from_start - (route_index < route_infos.get_count() ? route_infos.get_element(route_index).steps_from_start : 0);
		bool way_is_free = route_steps >= brake_steps || brake_steps <= 0 || route_steps == 0; // If brake_steps <= 0 and way_is_free == false, weird excess block reservations can occur that cause blockages.
		if(!way_is_free)
		{
			// We need a longer route to decide whether we shall have to start braking
			route_t target_rt;
			schedule_t *schedule = cnv->get_schedule();
			const koord3d start_pos = route.at(last_index);
			uint8 index = schedule->get_current_stop();
			bool rev = cnv->get_reverse_schedule();
			schedule->increment_index(&index, &rev);
			const koord3d next_ziel = schedule->entries[index].pos;

			way_is_free = target_rt.calc_route(welt, start_pos, next_ziel, this, speed_to_kmh(cnv->get_min_top_speed()), cnv->get_highest_axle_load(), cnv->has_tall_vehicles(), cnv->get_tile_length(), welt->get_settings().get_max_route_steps(), cnv->get_weight_summary().weight / 1000) != route_t::valid_route;
			if(!way_is_free)
			{
				if(schedule->entries[schedule->get_current_stop()].reverse == -1)
				{
					schedule->entries[schedule->get_current_stop()].reverse = cnv->check_destination_reverse(NULL, &target_rt);
					linehandle_t line = cnv->get_line();
					if(line.is_bound())
					{
						simlinemgmt_t::update_line(line);
					}
				}
				if(schedule->entries[schedule->get_current_stop()].reverse == 0 && haltestelle_t::get_halt(schedule->entries[schedule->get_current_stop()].pos, get_owner()).is_bound() == false)
				{
					// Extending the route if the convoy needs to reverse would interfere with tile reservations.
					// This convoy can pass a waypoint without reversing/stopping. Append route to the next stop/waypoint.

					// This is still needed after the new (November 2015) system as there are some (possibly
					// transitional) cases in which next_block is still ahead of the calculated route.

					if(rev)
					{
						schedule->advance_reverse();
					}
					else
					{
						schedule->advance();
					}
					cnv->update_route(last_index, target_rt);
					if(working_method == cab_signalling)
					{
						// With cab signalling, the train can always detect the aspect of the next stop signal without a distant (pre-signal).
						way_is_free = block_reserver( &route, last_index, modified_sighting_distance_tiles, next_signal, 0, true, false );
					}
					else if(working_method == absolute_block || working_method == track_circuit_block)
					{
						// With absolute or track circuit block signalling, it is necessary to be within the sighting distance or use a distant or MAS.
						if(next_signal - route_index <= modified_sighting_distance_tiles)
						{
							way_is_free = block_reserver( &route, last_index, modified_sighting_distance_tiles, next_signal, 0, true, false );
						}
					}

					last_index = route.get_count() - 1;
					cnv->set_next_stop_index(next_signal);
					next_block = cnv->get_next_stop_index() - 1;
				}
			}
		}
		// no obstacle in the way => drive on ...
		if (way_is_free || next_block > last_index)
			return true;
	}

	bool do_not_set_one_train_staff = false;
	bool modify_check_tile = false;
	if((next_block < route_index && (working_method == one_train_staff || next_block < route_index - 1)))
	{
		modify_check_tile = true;
		do_not_set_one_train_staff = working_method == one_train_staff;
		cnv->set_working_method(working_method != one_train_staff ? drive_by_sight : one_train_staff);
	}

	if(working_method == absolute_block || working_method == track_circuit_block || working_method == drive_by_sight || working_method == token_block || working_method == time_interval || working_method == time_interval_with_telegraph)
	{
		// Check for signals at restrictive aspects within the sighting distance to see whether they can now clear whereas they could not before.
		for(uint16 tiles_to_check = 1; tiles_to_check <= modified_sighting_distance_tiles; tiles_to_check++)
		{
			const uint32 route_count = route.get_count() - 1u;
			const uint32 check_ahead_tile = route_index + tiles_to_check;
			const koord3d tile_to_check_ahead = cnv->get_route()->at(std::min(route_count, check_ahead_tile));
			grund_t *gr_ahead = welt->lookup(tile_to_check_ahead);
			weg_t *way = gr_ahead ? gr_ahead->get_weg(get_waytype()) : NULL;
			if(!way)
			{
				// This may happen if a way has been removed since the route was calculated. Must recalculate the route.
				cnv->suche_neue_route();
				return false;
			}
			uint16 modified_route_index = std::min(check_ahead_tile, cnv->get_route()->get_count() - 1u);
			ribi_t::ribi ribi = ribi_type(cnv->get_route()->at(max(1u,modified_route_index)-1u), cnv->get_route()->at(std::min(cnv->get_route()->get_count()-1u,modified_route_index+1u)));
			signal_t* signal = way->get_signal(ribi);

			// There might be a station signal here, which might be operative despite facing in the opposite direction.
			if(!signal && haltestelle_t::get_halt(tile_to_check_ahead, NULL).is_bound())
			{
				signal = way->get_signal(ribi_t::backward(ribi));
				if(signal && !signal->get_desc()->is_longblock_signal())
				{
					signal = NULL;
				}
			}

			if(signal && (signal->get_state() == signal_t::caution
				|| signal->get_state() == signal_t::preliminary_caution
				|| signal->get_state() == signal_t::advance_caution
				|| ((working_method == time_interval || working_method == time_interval_with_telegraph || working_method == track_circuit_block) && signal->get_state() == signal_t::clear)
				|| ((working_method == time_interval || working_method == time_interval_with_telegraph || working_method == track_circuit_block) && signal->get_state() == signal_t::clear_no_choose)
				|| signal->get_state() == signal_t::caution_no_choose
				|| signal->get_state() == signal_t::preliminary_caution_no_choose
				|| signal->get_state() == signal_t::advance_caution_no_choose
				|| (working_method == token_block && signal->get_state() == signal_t::danger)))
			{
				if (!starting_from_stand && (signal->get_desc()->get_working_method() == token_block || signal->get_desc()->get_working_method() == one_train_staff))
				{
					// These signals should only clear when a train is starting from them.
					break;
				}

				// We come accross a signal at caution: try (again) to free the block ahead.
				const bool ok = block_reserver(cnv->get_route(), route_index, modified_sighting_distance_tiles - (tiles_to_check - 1), next_signal, 0, true, false);
				cnv->set_next_stop_index(next_signal);
				if(!ok && working_method != drive_by_sight && starting_from_stand && this_halt.is_bound())
				{
					return false;
				}
				break;
				// Setting the aspect of the signal is done inside the block reserver
			}
		}
	}

	if(working_method == token_block && w_current->has_sign())
	{
		roadsign_t* rs = gr->find<roadsign_t>();
		if(rs && rs->get_desc()->is_single_way())
		{
			// If we come upon a single way sign, clear the reservation so far, as we know that we are now on unidirectional track again.
			clear_token_reservation(NULL, this, w_current);
		}
	}

	sint32 max_element = cnv->get_route_infos().get_count() - 1u;

	if(w_current->has_signal())
	{
		// With cab signalling, even if we need do nothing else at this juncture, we may need to change the working method.
		const uint16 check_route_index = next_block <= 0 ? 0 : next_block - 1u;
		const uint32 cnv_route_count = cnv->get_route()->get_count() - 1u;
		ribi_t::ribi ribi = next_block < INVALID_INDEX ? ribi_type(cnv->get_route()->at(max(1u, std::min(cnv_route_count, (uint32)check_route_index)) - 1u), cnv->get_route()->at(std::min((uint32)max_element, min(cnv_route_count - 1u, ((uint32)check_route_index + 1u))))) : ribi_t::all;
		signal_t* signal = w_current->get_signal(ribi);

		if (signal && working_method == one_train_staff && starting_from_stand)
		{
			const bool ok = block_reserver(cnv->get_route(), route_index - 1, modified_sighting_distance_tiles, next_signal, 0, true, false);
			cnv->set_next_stop_index(next_signal);
			if (!ok)
			{
				return false;
			}
		}

		else if(signal && (working_method == cab_signalling || working_method == moving_block || (signal->get_desc()->get_working_method() == cab_signalling && working_method != drive_by_sight) || signal->get_desc()->get_working_method() == moving_block))
		{
			if ((working_method == cab_signalling || working_method == moving_block) && signal->get_desc()->get_working_method() != cab_signalling && signal->get_desc()->get_working_method() != moving_block)
			{
				// Transitioning out of a signalling system in which the stopping distance is not based on the position of the signals: run the block reserver again in case it was not run on this signal previously.
				block_reserver(cnv->get_route(), next_block, modified_sighting_distance_tiles, next_signal, 0, true, false, cnv->get_is_choosing());
			}
			cnv->set_working_method(signal->get_desc()->get_working_method());
		}

	}

	if(next_block > max_element)
	{
		next_block = cnv->get_next_stop_index() - 1;
	}

	bool call_on = false;

	const sint32 route_steps = brake_steps > 0 && route_index <= route_infos.get_count() - 1 ? cnv->get_route_infos().get_element((next_block > 0 ? std::min(next_block - 1, max_element) : 0)).steps_from_start - cnv->get_route_infos().get_element(route_index).steps_from_start : -1;
	if(route_steps <= brake_steps || brake_steps < 0)
	{
		sint32 check_tile = modify_check_tile ? route_index + 1 : next_block; // This might otherwise end up as -1 without the ? block, which would be an attempt to check the *previous* tile, which would be silly.
		if((check_tile > 0) && check_tile >= cnv->get_route()->get_count()) // Checking if check_tile > 0 is necessary because get_count() is a uint32
		{
			check_tile = cnv->get_route()->get_count() - 1;
		}
		koord3d block_pos = cnv->get_route()->at(check_tile);

		grund_t *gr_next_block = welt->lookup(block_pos);
		const schiene_t *sch1 = gr_next_block ? (const schiene_t *)gr_next_block->get_weg(get_waytype()) : NULL;
		if(w_current == NULL)
		{
			// way (weg) not existent (likely destroyed)
			cnv->suche_neue_route();
			return false;
		}

		// next check for signal
		if(sch1 && sch1->has_signal())
		{
			const uint16 check_route_index = next_block <= 0 ? 0 : next_block - 1u;
			max_element = std::min((uint32)max_element, cnv->get_route()->get_count() - 1u);
			ribi_t::ribi ribi = ribi_type(cnv->get_route()->at(max(1u, (std::min((uint32)max_element, (uint32)check_route_index))) - 1u), cnv->get_route()->at(std::min((uint32)max_element, check_route_index + 1u)));
			signal_t* signal = sch1->get_signal(ribi);

			if(signal && ((signal->get_desc()->get_working_method() == cab_signalling) || (check_tile - route_index <= sighting_distance_tiles)))
			{
				working_method_t old_working_method = working_method;
				if(working_method != token_block && working_method != one_train_staff)
				{
					if(signal->get_desc()->get_working_method() != one_train_staff || (!do_not_set_one_train_staff && (signal->get_pos() == get_pos()) && (signal->get_state() != roadsign_t::call_on)))
					{
						cnv->set_working_method(signal->get_desc()->get_working_method());
						if (signal->get_desc()->get_working_method() == one_train_staff)
						{
							cnv->set_last_signal_pos(signal->get_pos());
						}
					}
				}

				if(working_method == cab_signalling
					|| signal->get_desc()->is_pre_signal()
					|| ((working_method == token_block
					|| working_method == time_interval
					|| working_method == time_interval_with_telegraph
					|| working_method == track_circuit_block
					|| working_method == absolute_block) && next_block - route_index <= max(modified_sighting_distance_tiles - 1, 1))
					|| (working_method == one_train_staff && shortest_distance(get_pos().get_2d(), signal->get_pos().get_2d()) < 2))
 				{
					// Brake for the signal unless we can see it somehow. -1 because this is checked on entering the tile.
					const bool allow_block_reserver = ((working_method != time_interval && working_method != time_interval_with_telegraph) || !signal->get_no_junctions_to_next_signal() || signal->get_desc()->is_choose_sign() || signal->get_state() != roadsign_t::danger) && ((signal->get_desc()->get_working_method() != one_train_staff && signal->get_desc()->get_working_method() != token_block) || route_index == next_block);
					if(allow_block_reserver && !block_reserver(cnv->get_route(), next_block, modified_sighting_distance_tiles, next_signal, 0, true, false, cnv->get_is_choosing()))
					{
						restart_speed = 0;
						signal->set_state(roadsign_t::danger);
						if (old_working_method == drive_by_sight)
						{
							cnv->set_working_method(old_working_method);
						}
						if(starting_from_stand && (working_method == absolute_block || working_method == track_circuit_block || working_method == cab_signalling))
						{
							// Check for permissive working. Only check here, as the convoy must be brought to a stand before a call on.
							if(signal->get_desc()->get_permissive() && signal->get_no_junctions_to_next_signal())
							{
								// Permissive working allowed: call on.
								signal->set_state(roadsign_t::call_on);
								cnv->set_working_method(drive_by_sight);
								call_on = true;
							}
						}
						cnv->set_next_stop_index(next_signal == INVALID_INDEX ? next_block : next_signal);
						if(signal->get_state() != roadsign_t::call_on)
						{
							cnv->set_is_choosing(false);
							return cnv->get_next_stop_index() > route_index;
						}
					}
					else
					{
						if(allow_block_reserver)
						{
							if ((next_signal == next_block) && (next_signal == route_index - 1))
							{
								return false;
							}
							else
							{
								cnv->set_next_stop_index(next_signal);
							}
						}
						else
						{
							return cnv->get_next_stop_index() > route_index;
						}
					}
					if(!signal->get_desc()->is_choose_sign())
					{
						cnv->set_is_choosing(false);
					}
				}
			}
		}
		else if(working_method == one_train_staff)
		{
			// Check for non-adjacent one train staff cabinets - these should be ignored.
			const koord3d first_pos = cnv->get_last_signal_pos();
			if (shortest_distance(get_pos().get_2d(), first_pos.get_2d()) < 3)
			{
				cnv->set_next_stop_index(next_signal);
			}
			else if(first_pos != koord3d::invalid)
			{
				nonadjacent_one_train_staff_cabinet = true;
			}
		}
	}

	if(w_current->has_signal())
	{
		if(working_method != one_train_staff || (welt->lookup(w_current->get_pos())->find<signal_t>()->get_desc()->get_working_method() == one_train_staff && !nonadjacent_one_train_staff_cabinet))
		{
			cnv->set_last_signal_pos(w_current->get_pos());
		}
	}

	// This is necessary if a convoy is passing through a passing loop without stopping so as to set token block working
	// properly on the token block signal at the exit of the loop.
	if(welt->lookup(get_pos())->get_weg(get_waytype())->has_signal())
	{
		signal_t* signal = get_weg()->get_signal(ribi);

		if(signal && signal->get_desc()->get_working_method() == token_block)
		{
			cnv->set_working_method(token_block);
		}
	}
	if(working_method == drive_by_sight || working_method == moving_block)
	{
		bool ok = block_reserver(cnv->get_route(), route_index, modified_sighting_distance_tiles, next_signal, 0, true, false, false, false, false, false, brake_steps, (uint16)65530U, call_on);
		ok |= route_index == route.get_count() || next_signal > route_index;
		if (!ok && working_method == one_train_staff)
		{
			cnv->set_working_method(drive_by_sight);
		}
		cnv->set_next_stop_index(next_signal);
		if (exiting_one_train_staff && get_working_method() == one_train_staff)
		{
			cnv->set_working_method(drive_by_sight);
		}
		return ok;
	}

	return true;
}

void rail_vehicle_t::find_next_signal(route_t* route, uint16 start_index, uint16 &next_signal_index)
{
	if(start_index >= route->get_count())
	{
		// Cannot start reserving beyond the end of the route.
		cnv->set_next_reservation_index(max(route->get_count(), 1) - 1);
		return;
	}

	sint32 count = 0;
	uint32 i = start_index;
	next_signal_index = INVALID_INDEX;
	signal_t* signal = NULL;
	koord3d pos = route->at(start_index);
	for (; count >= 0 && i < route->get_count(); i++)
	{
		pos = route->at(i);
		grund_t *gr = welt->lookup(pos);
		schiene_t* sch1 = gr ? (schiene_t *)gr->get_weg(get_waytype()) : NULL;

		if (sch1 && sch1->has_signal())
		{
			ribi_t::ribi ribi = ribi_type(route->at(max(1u, i) - 1u), route->at(min(route->get_count() - 1u, i + 1u)));
			signal = gr ? gr->get_weg(get_waytype())->get_signal(ribi) : NULL;
			if (signal && !signal->get_desc()->is_pre_signal())
			{
				next_signal_index = i;
				return;
			}
		}
	}
}

/*
 * reserves or un-reserves all blocks and returns whether it succeeded.
 * if count is larger than 1, (and defined) maximum welt->get_settings().get_max_choose_route_steps() tiles will be checked
 * (freeing or reserving a choose signal path)
 * if (!reserve && force_unreserve) then un-reserve everything till the end of the route
 * @author prissi
 */
sint32 rail_vehicle_t::block_reserver(route_t *route, uint16 start_index, uint16 modified_sighting_distance_tiles, uint16 &next_signal_index, int count, bool reserve, bool force_unreserve, bool is_choosing, bool is_from_token, bool is_from_starter, bool is_from_directional, uint32 brake_steps, uint16 first_one_train_staff_index, bool from_call_on, bool *break_loop)
{
	bool success = true;
	sint32 max_tiles = 2 * welt->get_settings().get_max_choose_route_steps(); // max tiles to check for choosesignals

	slist_tpl<grund_t *> signs;	// switch all signals on the route
	slist_tpl<signal_t*> pre_signals;
	slist_tpl<signal_t*> combined_signals;

	if(start_index >= route->get_count())
	{
		// Cannot start reserving beyond the end of the route.
		cnv->set_next_reservation_index(max(route->get_count(), 1) - 1);
		return 0;
	}

	bool starting_at_signal = false;
	if(route->at(start_index) == get_pos() && reserve && start_index < route->get_count() - 1)
	{
		starting_at_signal = true;
	}

	// perhaps find an early platform to stop at
	uint16 platform_size_needed = 0;
	uint16 platform_size_found = 0;
	ribi_t::ribi ribi_last = ribi_t::none;
	halthandle_t dest_halt = halthandle_t();
	uint16 early_platform_index = INVALID_INDEX;
	uint16 first_stop_signal_index = INVALID_INDEX;
	const schedule_t* schedule = NULL;
	if(cnv != NULL)
	{
		schedule = cnv->get_schedule();
	}
	else
	{
		return 0;
	}
	bool do_early_platform_search =	schedule != NULL
		&& (schedule->is_mirrored() || schedule->is_bidirectional())
		&& schedule->get_current_entry().minimum_loading == 0;

	if(do_early_platform_search)
	{
		platform_size_needed = cnv->get_tile_length();
		dest_halt = haltestelle_t::get_halt(cnv->get_schedule()->get_current_entry().pos, cnv->get_owner());
	}
	if(!reserve)
	{
		cnv->set_next_reservation_index(start_index);
	}

	// find next block segment enroute
	uint32 i = start_index - (count == 100001 ? 1 : 0);
	next_signal_index = INVALID_INDEX;
	bool unreserve_now = false;

	enum set_by_distant
	{
		uninitialised, not_set, set
	};

	koord3d pos = route->at(start_index);
	koord3d last_pos = start_index > 0 ? route->at(start_index - 1) : pos;
	const halthandle_t this_halt = haltestelle_t::get_halt(pos, get_owner());
	halthandle_t last_step_halt;
	uint16 this_stop_signal_index = INVALID_INDEX;
	uint16 last_pre_signal_index = INVALID_INDEX;
	uint16 last_stop_signal_index = INVALID_INDEX;
	uint16 restore_last_stop_signal_index = INVALID_INDEX;
	koord3d last_stop_signal_pos = koord3d::invalid;
	uint16 last_longblock_signal_index = INVALID_INDEX;
	uint16 last_combined_signal_index = INVALID_INDEX;
	uint16 last_choose_signal_index = INVALID_INDEX;
	uint16 last_token_block_signal_index = INVALID_INDEX;
	uint16 last_bidirectional_signal_index = INVALID_INDEX;
	uint16 last_stop_signal_before_first_bidirectional_signal_index = INVALID_INDEX;
	uint16 last_non_directional_index = start_index;
	uint16 first_oneway_sign_index = INVALID_INDEX;
	uint16 last_oneway_sign_index = INVALID_INDEX;
	uint16 last_station_tile = INVALID_INDEX;
	uint16 first_double_block_signal_index = INVALID_INDEX;
	uint32 stop_signals_since_last_double_block_signal = 0;
	halthandle_t stop_at_station_signal;
	const halthandle_t destination_halt = haltestelle_t::get_halt(route->at(route->get_count() - 1), get_owner());
	bool do_not_clear_distant = false;
	working_method_t next_signal_working_method = working_method;
	working_method_t old_working_method = working_method;
	set_by_distant working_method_set_by_distant_only = uninitialised;
	bool next_signal_protects_no_junctions = false;
	koord3d signalbox_last_distant_signal = koord3d::invalid;
	bool last_distant_signal_was_intermediate_block = false;
	sint32 remaining_aspects = -1;
	sint32 choose_return = 0;
	bool reached_end_of_loop = false;
	bool no_junctions_to_next_signal = true;
	signal_t* previous_signal = NULL;
	bool end_of_block = false;
	bool not_entirely_free = false;
	bool directional_only = is_from_directional;
	bool previous_telegraph_directional = false;
	bool directional_reservation_succeeded = true;
	bool one_train_staff_loop_complete = false;
	bool this_signal_is_nonadjacent_one_train_staff_cabinet = false;
	bool time_interval_reservation = false;
	bool reserving_beyond_a_train = false;
	enum ternery_uncertainty {
		is_true, is_false, is_uncertain
	};
	ternery_uncertainty previous_time_interval_reservation = is_uncertain;
	bool set_first_station_signal = false;
	bool time_interval_after_double_block = false;
	const uint32 station_signals = this_halt.is_bound() ? this_halt->get_station_signals_count() : 0;
	signal_t* first_station_signal = NULL;
	enum station_signal_status { none, forward, inverse };
	station_signal_status station_signal = station_signal_status::none;
	uint32 steps_so_far = 0;
	roadsign_t::signal_aspects next_time_interval_state = roadsign_t::danger;
	roadsign_t::signal_aspects first_time_interval_state = roadsign_t::advance_caution; // A time interval signal will never be in advance caution, so this is a placeholder to indicate that this value has not been set.
	signal_t* station_signal_to_clear_for_entry = NULL;

	if(working_method == drive_by_sight)
	{
		const sint32 max_speed_drive_by_sight = get_desc()->get_waytype() == tram_wt ? welt->get_settings().get_max_speed_drive_by_sight_tram() : welt->get_settings().get_max_speed_drive_by_sight();
		if(max_speed_drive_by_sight)
		{
			cnv->set_maximum_signal_speed(max_speed_drive_by_sight);
		}
	}

	for( ; success && count >= 0 && i < route->get_count(); i++)
	{
		station_signal = none;
		this_stop_signal_index = INVALID_INDEX;
		last_pos = pos;
		pos = route->at(i);
		if(!directional_only)
		{
			last_non_directional_index = i;
		}
		grund_t *gr = welt->lookup(pos);
		schiene_t* sch1 = gr ? (schiene_t *)gr->get_weg(get_waytype()) : NULL;

		if (sch1 == NULL && reserve)
		{
			if (i < route->get_count() - 1)
			{
				// A way tile has been deleted; the route needs recalculating.
				cnv->suche_neue_route();
			}
			break;
		}
		// we un-reserve also nonexistent tiles! (may happen during deletion)

		if (reserve && sch1->is_diagonal())
		{
			steps_so_far += diagonal_vehicle_steps_per_tile;
		}
		else
		{
			steps_so_far += VEHICLE_STEPS_PER_TILE;
		}

		if((working_method == drive_by_sight && (i - (start_index - 1)) > modified_sighting_distance_tiles) && (!this_halt.is_bound() || (haltestelle_t::get_halt(pos, get_owner())) != this_halt))
		{
			// In drive by sight mode, do not reserve further than can be seen (taking account of obstacles);
			// but treat signals at the end of the platform as a signal at which the train is now standing.
			next_signal_index = i;
			break;
		}

		if(working_method == moving_block && !directional_only && last_choose_signal_index >= INVALID_INDEX && !is_choosing)
		{
			next_signal_index = i;

			// Do not reserve further than the braking distance in moving block mode.
			if(steps_so_far > brake_steps + (VEHICLE_STEPS_PER_TILE * 2))
			{
				break;
			}
		}

		if(is_choosing && welt->get_settings().get_max_choose_route_steps())
		{
			max_tiles--;
			if(max_tiles < 0)
			{
				break;
			}
		}

		if(reserve)
		{
			if(sch1->is_junction())
			{
				no_junctions_to_next_signal = false;
			}
			if(sch1->is_crossing() && !directional_only)
			{
				crossing_t* cr = gr->find<crossing_t>(2);
				if(cr)
				{
					// Only reserve if the crossing is clear.
					if(i < first_stop_signal_index)
					{
						success = cr->request_crossing(this, true) || next_signal_working_method == one_train_staff;
					}
					else
					{
						not_entirely_free = !cr->request_crossing(this, true) && next_signal_working_method != one_train_staff;
						if(not_entirely_free && (working_method != time_interval || ((i - (start_index - 1)) < modified_sighting_distance_tiles)))
						{
							count --;
							next_signal_index = last_stop_signal_index;
						}
					}
				}
			}

			roadsign_t* rs = gr->find<roadsign_t>();
			ribi_t::ribi ribi = ribi_type(route->at(max(1u,i)-1u), route->at(min(route->get_count()-1u,i+1u)));

			if(working_method == moving_block)
			{
				// Continue in moving block if in range of the most recent moving block signal.
				const grund_t* gr_last_signal = welt->lookup(cnv->get_last_signal_pos());
				const signal_t* sg = gr_last_signal ? gr_last_signal->find<signal_t>() : NULL;
				if(!sg || sg->get_desc()->get_max_distance_to_signalbox() < shortest_distance(pos.get_2d(), cnv->get_last_signal_pos().get_2d()))
				{
					// Out of range of the moving block beacon/signal; revert to drive by sight
					set_working_method(drive_by_sight);
					break;
				}
			}

			halthandle_t check_halt = haltestelle_t::get_halt(pos, NULL);

			if(check_halt.is_bound() && (check_halt == this_halt))
			{
				last_station_tile = i;
			}

			if (rs && rs->get_desc()->is_single_way())
			{
				if (first_oneway_sign_index >= INVALID_INDEX)
				{
					if (directional_only)
					{
						// Stop a directional reservation when a one way sign is encountered
						if (next_signal_working_method != time_interval_with_telegraph)
						{
							count--;
						}
						if (is_from_directional)
						{
							reached_end_of_loop = true;
							if (break_loop)
							{
								*break_loop = true;
							}
						}
						next_signal_index = last_stop_signal_index;
					}
					if (last_bidirectional_signal_index < INVALID_INDEX || last_longblock_signal_index < INVALID_INDEX)
					{
						first_oneway_sign_index = i;
						last_oneway_sign_index = i;
					}
				}
				else
				{
					last_oneway_sign_index = i;
				}
			}

			const uint32 station_signals_ahead = check_halt.is_bound() ? check_halt->get_station_signals_count() : 0;
			bool station_signals_in_advance = false;
			const bool signal_here = sch1->has_signal() || station_signals;
			bool station_signal_to_clear_only = false;

			if(!signal_here && station_signals_ahead)
			{
				halthandle_t destination_halt = haltestelle_t::get_halt(cnv->get_schedule()->get_current_entry().pos, get_owner());
				station_signals_in_advance = check_halt != this_halt && check_halt != destination_halt;
				if (check_halt != this_halt && check_halt == destination_halt)
				{
					station_signal_to_clear_only = true;
				}
			}

			if(signal_here || station_signals_in_advance || station_signal_to_clear_only)
			{
				signal_t* signal = NULL;
				halthandle_t station_signal_halt;
				uint32 check_station_signals = 0;

				if(station_signals && last_station_tile >= i)
				{
					// This finds a station signal on the station at which the train is currently standing
					check_station_signals = station_signals;
					station_signal_halt = this_halt;
				}
				else if(station_signals_ahead || station_signal_to_clear_only)
				{
					// This finds a station signal in a station in advance
					check_station_signals = station_signals_ahead;
					station_signal_halt = check_halt;
				}

				if(check_station_signals)
				{
					// This is a signal that applies to the whole station for the relevant direction. Find it if it is there.

					for(uint32 k = 0; k < check_station_signals; k ++)
					{
						grund_t* gr_check = welt->lookup(station_signal_halt->get_station_signal(k));
						if(gr_check)
						{
							weg_t* way_check = gr_check->get_weg(get_waytype());
							signal = way_check ? way_check->get_signal(ribi) : NULL;
							if(signal)
							{
								station_signal = forward;
								break;
							}
							else
							{
								// Check the opposite direction as station signals work in the exact opposite direction
								ribi_t::ribi ribi_backwards = ribi_t::backward(ribi);
								signal = gr_check->get_weg(get_waytype())->get_signal(ribi_backwards);
								if(signal)
								{
									station_signal = inverse;
									break;
								}
							}
						}
					}
					if (station_signal_to_clear_only)
					{
						station_signal_to_clear_for_entry = signal;
						goto station_signal_to_clear_only_point;
					}
				}
				else
				{
					// An ordinary signal on plain track
					signal = gr->get_weg(get_waytype())->get_signal(ribi);
					// But, do not select a station signal here, as this will lead to capricious behaviour depending on whether the train passes the track on which the station signal happens to be situated or not.
					if(signal && check_halt.is_bound() && signal->get_desc()->is_station_signal())
					{
						signal = NULL;
					}
				}

				if(signal && (signal != first_station_signal || !set_first_station_signal))
				{
					if(station_signal)
					{
						first_station_signal = signal;
						set_first_station_signal = true;
					}

					if(!directional_only && (signal->get_desc()->is_longblock_signal() || signal->is_bidirectional()))
					{
						last_bidirectional_signal_index = i;
					}

					if(previous_signal && !previous_signal->get_desc()->is_choose_sign())
					{
						previous_signal->set_no_junctions_to_next_signal(no_junctions_to_next_signal);
					}

					previous_signal = signal;
					if(working_method != time_interval_with_telegraph || last_longblock_signal_index >= INVALID_INDEX || signal->get_desc()->get_working_method() != time_interval)
					{
						// In time interval with telegraph method, do not change the working method to time interval when reserving beyond a longblock signal in the time interval with telegraph method.
						working_method_t nwm = signal->get_desc()->get_working_method();
						if (first_stop_signal_index < INVALID_INDEX && (nwm == track_circuit_block || nwm == cab_signalling) && next_signal_working_method != cab_signalling && next_signal_working_method != track_circuit_block && remaining_aspects < 0)
						{
							// Set remaining_aspects correctly when transitioning to track circuit block or cab signalling from another working method
							remaining_aspects = signal->get_desc()->get_aspects();
						}

						next_signal_working_method = nwm;
					}

					next_signal_protects_no_junctions = signal->get_no_junctions_to_next_signal();

					if (destination_halt == check_halt)
					{
						next_signal_protects_no_junctions = false;
					}
					if(working_method == drive_by_sight && sch1->can_reserve(cnv->self, ribi) && (signal->get_pos() != cnv->get_last_signal_pos() || signal->get_desc()->get_working_method() != one_train_staff))
					{
						if (signal->get_desc()->get_working_method() == one_train_staff && pos != get_pos())
						{
							// Do not try to reserve beyond a one train staff cabinet unless the train is at the cabinet.
							next_signal_index = i;
							count --;
						}
						else
						{
							set_working_method(next_signal_working_method);
							if (signal->get_desc()->is_pre_signal() && working_method_set_by_distant_only == uninitialised)
							{
								working_method_set_by_distant_only = set;
							}
							else if (!signal->get_desc()->is_pre_signal())
							{
								working_method_set_by_distant_only = not_set;
							}
						}
					}

					if(next_signal_working_method == one_train_staff && first_one_train_staff_index == INVALID_INDEX)
					{
						// Do not set this when already in the one train staff working method unless this is an adjacent cabinet
						const koord3d first_pos = cnv->get_last_signal_pos();
						if (working_method != one_train_staff || shortest_distance(pos.get_2d(), first_pos.get_2d()) < 3)
						{
							first_one_train_staff_index = i;
						}
						else if (working_method == one_train_staff && first_pos != koord3d::invalid)
						{
							this_signal_is_nonadjacent_one_train_staff_cabinet = true;
						}
					}

					if(next_signal_working_method == one_train_staff && (first_one_train_staff_index != i || (is_from_token && first_one_train_staff_index < INVALID_INDEX)))
					{
						// A second one train staff cabinet. Is this the same as or a neighbour of the first?
						const koord3d first_pos = cnv->get_last_signal_pos();
						if(shortest_distance(pos.get_2d(), first_pos.get_2d()) < 3)
						{
							one_train_staff_loop_complete = true;
						}
						else if(first_pos != koord3d::invalid)
						{
							// Any non-adjacent one train staff cabinets should be ignored,
							// or else the one train staff method effectively becomes token block.
							this_signal_is_nonadjacent_one_train_staff_cabinet = true;
						}
					}

					if((next_signal_working_method == drive_by_sight && working_method != drive_by_sight) || one_train_staff_loop_complete || (((working_method != one_train_staff && (first_one_train_staff_index < INVALID_INDEX && first_one_train_staff_index > start_index))) && (first_double_block_signal_index == INVALID_INDEX || first_double_block_signal_index != last_stop_signal_index)))
					{
						// Do not reserve through end of signalling signs (unless already in drive by side mode) or one train staff cabinets
						next_signal_index = i;
						count --;
					}

					if(time_interval_after_double_block)
					{
						next_signal_index = i;
						count --;
					}

					if(!signal->get_desc()->is_pre_signal() && !this_signal_is_nonadjacent_one_train_staff_cabinet) // Stop signal or multiple aspect signal
					{
						if (signal->get_desc()->get_double_block() == true && first_double_block_signal_index == INVALID_INDEX)
						{
							first_double_block_signal_index = i;
							stop_signals_since_last_double_block_signal = 0;
						}
						else if(first_double_block_signal_index < INVALID_INDEX)
						{
							stop_signals_since_last_double_block_signal++;
						}

						if(signal->get_desc()->get_working_method() == time_interval || signal->get_desc()->get_working_method() == time_interval_with_telegraph)
						{
							// Note that this gives the pure time interval state, ignoring whether the signal protects any junctions.
							sint64 last_passed;
							if(station_signal && station_signal_halt.is_bound())
							{
								last_passed = station_signal_halt->get_train_last_departed(ribi);
							}
							else
							{
								last_passed = signal->get_train_last_passed();
							}
							const sint64 caution_interval_ticks = welt->get_seconds_to_ticks(welt->get_settings().get_time_interval_seconds_to_caution());
							const sint64 clear_interval_ticks =  welt->get_seconds_to_ticks(welt->get_settings().get_time_interval_seconds_to_clear());
							const sint64 ticks = welt->get_ticks();

							if(last_passed + caution_interval_ticks > ticks)
							{
								next_time_interval_state = roadsign_t::danger;
							}
							else if(last_passed + clear_interval_ticks > ticks)
							{
								next_time_interval_state = station_signal == inverse ? roadsign_t::caution : roadsign_t::caution_no_choose;
							}
							else
							{
								next_time_interval_state = station_signal == inverse ? roadsign_t::clear : roadsign_t::clear_no_choose;
							}

							if(first_time_interval_state == roadsign_t::advance_caution)
							{
								first_time_interval_state = next_time_interval_state;
							}

							// Sometimes the signal's state might be incorrect (e.g. if it was formerly incorrectly registered as protecting a junction),
							// so set it here to correct in so far as necessary (but only if it is on plain track, otherwise this will need to be set
							// separately).
							if (signal->get_no_junctions_to_next_signal())
							{
								signal->set_state(next_time_interval_state);
							}

							if (first_double_block_signal_index < INVALID_INDEX && first_double_block_signal_index == last_stop_signal_index)
							{
								// We are checking here whether to clear the double block stop signal before a time interval signal.
								if (next_time_interval_state == roadsign_t::danger)
								{
									next_signal_index = first_double_block_signal_index;
									count--;
									not_entirely_free = true;
									success = false;
								}
								else
								{
									time_interval_after_double_block = true;
								}
							}
						}

						if(last_bidirectional_signal_index >= INVALID_INDEX)
						{
							last_stop_signal_before_first_bidirectional_signal_index = i;
						}
						if((count || pre_signals.get_count() || next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling || ((first_stop_signal_index >= INVALID_INDEX && next_signal_working_method != time_interval && next_signal_working_method != time_interval_with_telegraph) || !next_signal_protects_no_junctions)) && !directional_only)
						{
							signs.append(gr);
						}
						if (pre_signals.get_count() || combined_signals.get_count())
						{
							// Do not reserve after a stop signal not covered by a distant or combined signal
							// (or multiple aspect signals with the requisite number of aspects).
							if(next_signal_working_method == absolute_block || next_signal_working_method == token_block)
							{
								if(last_combined_signal_index < INVALID_INDEX && pre_signals.empty())
								{
									// Treat the last combined signal as a distant signal.
									// Check whether it can be used from the current box.
									signal_t* last_combined_signal = combined_signals.back();
									const signalbox_t* sb = NULL;
									const grund_t* gr_signalbox = welt->lookup(signal->get_signalbox());
									if(gr_signalbox)
									{
										const gebaeude_t* gb = gr_signalbox->get_building();
										if(gb && gb->get_tile()->get_desc()->is_signalbox())
										{
											sb = static_cast<const signalbox_t *>(gb);
										}
									}
									if(sb && sb->can_add_signal(signal) && !directional_only)
									{
										// This is compatible: treat it as a distant signal.
										pre_signals.append(last_combined_signal);
										last_pre_signal_index = i;
										signalbox_last_distant_signal = signal->get_signalbox();
										last_distant_signal_was_intermediate_block = signal->get_desc()->get_intermediate_block();
										signs.append_unique(gr);
									}
									else if (first_double_block_signal_index != last_stop_signal_index)
									{
										// The last combined signal is not compatible with this signal's signalbox:
										// do not treat it as a distant signal.
										count --;
										end_of_block = true;
										if(!is_from_token)
										{
											next_signal_index = last_stop_signal_index;
										}
									}
								}
								else if(signalbox_last_distant_signal != signal->get_signalbox() ||
									// XOR - allow intermediate block in advance or in rear, but do not allow them to be chained.
									((signal->get_desc()->get_intermediate_block() ^ last_distant_signal_was_intermediate_block) && first_double_block_signal_index != last_stop_signal_index))
								{
									count --;
									end_of_block = true;
								}
							}
						}

						if(!directional_only && (next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling) && remaining_aspects <= 2 && remaining_aspects >= 0)
						{
							if((last_bidirectional_signal_index < INVALID_INDEX || last_longblock_signal_index < INVALID_INDEX) && first_oneway_sign_index >= INVALID_INDEX)
							{
								directional_only = true;
								last_stop_signal_index = i;
								last_stop_signal_pos = pos;
							}
							else if(first_double_block_signal_index != last_stop_signal_index && !(last_bidirectional_signal_index < INVALID_INDEX && last_oneway_sign_index < last_bidirectional_signal_index))
							{
								count --;
							}

							if(first_double_block_signal_index != last_stop_signal_index)
							{
								end_of_block = true;
							}
						}

						if(stop_at_station_signal.is_bound() && stop_at_station_signal != check_halt)
						{
							// We have gone beyond the end of the station where we need to be stopping for the station signal.
							next_signal_index = i - 1;
							count --;
							last_stop_signal_index = i - 1;
							last_stop_signal_pos = route->at(i - 1);
							end_of_block = true;
						}

						if(!directional_only && (next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph) && last_longblock_signal_index < INVALID_INDEX && last_longblock_signal_index < this_stop_signal_index)
						{
							if(next_signal_working_method == time_interval_with_telegraph)
							{
								directional_only = true;
								next_signal_index = i;
							}
							else if(first_double_block_signal_index != last_stop_signal_index)
							{
								next_signal_index = i;
								count --;
							}
							last_stop_signal_index = i;
							last_stop_signal_pos = pos;
							end_of_block = true;
						}

						if(!directional_only && next_signal_working_method == time_interval_with_telegraph && signal->get_no_junctions_to_next_signal() && signal->get_desc()->is_longblock_signal() && (!check_halt.is_bound() || check_halt->get_station_signals_count() == 0))
						{
							directional_only = true;
						}

						if((!is_from_token || first_stop_signal_index >= INVALID_INDEX) && !directional_only)
						{
							next_signal_index = i;
						}

						if(signal->get_desc()->is_combined_signal())
						{
							// We cannot know yet whether it ought to be treated as a distant
							// without knowing whether it is close enough to the signalbox
							// controlling the next stop signal on this route.
							combined_signals.append(signal);
							last_combined_signal_index = i;
						}
						else if(!directional_only && ((next_signal_working_method == moving_block && working_method != moving_block) || ((next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling) && remaining_aspects >= 0 && remaining_aspects <= 2)))
						{
							// If there are no more caution aspects, or this is a transition to moving block signalling do not reserve any further at this juncture.
							if(((last_bidirectional_signal_index < INVALID_INDEX || last_longblock_signal_index < INVALID_INDEX) && (first_oneway_sign_index >= INVALID_INDEX || (!directional_only && (last_bidirectional_signal_index < INVALID_INDEX && last_oneway_sign_index < last_bidirectional_signal_index)))) || (next_signal_working_method == moving_block && working_method != moving_block))
							{
								directional_only = true;
								restore_last_stop_signal_index = last_stop_signal_index;
								last_stop_signal_index = i;
							}
							else if(first_double_block_signal_index != last_stop_signal_index)
							{
								count --;
							}
						}

						if(working_method == moving_block && ((last_bidirectional_signal_index < INVALID_INDEX || last_longblock_signal_index < INVALID_INDEX) && first_oneway_sign_index >= INVALID_INDEX))
						{
							directional_only = true;
						}

						if((working_method == time_interval || working_method == time_interval_with_telegraph) && last_pre_signal_index < INVALID_INDEX && !next_signal_protects_no_junctions)
						{
							const koord3d last_pre_signal_pos = route->at(last_pre_signal_index);
							grund_t *gr_last_pre_signal = welt->lookup(last_pre_signal_pos);
							signal_t* last_pre_signal = gr_last_pre_signal->find<signal_t>();
							if(last_pre_signal)
							{
								pre_signals.append(last_pre_signal);
							}
						}

						if(signal->get_desc()->is_choose_sign())
						{
							last_choose_signal_index = i;
							if(directional_only && last_bidirectional_signal_index < i && first_double_block_signal_index != last_stop_signal_index)
							{
								// If a choose signal is encountered, assume that there is no need to check whether this is free any further.
								count --;
								directional_reservation_succeeded = true;
							}
						}


						if(first_stop_signal_index >= INVALID_INDEX)
						{
							first_stop_signal_index = i;
							if(next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph)
							{
								if(next_signal_protects_no_junctions)
								{
									if(signal->get_state() == roadsign_t::danger)
									{
										count --;
									}
									else if(signal->get_state() == roadsign_t::caution || signal->get_state() == roadsign_t::caution_no_choose)
									{
										// Half line speed allowed for caution indications on time interval stop signals on straight track
										cnv->set_maximum_signal_speed(min(kmh_to_speed(sch1->get_max_speed()) / 2, signal->get_desc()->get_max_speed() / 2));
									}
									else if(signal->get_state() == roadsign_t::clear || signal->get_state() == roadsign_t::clear_no_choose)
									{
										cnv->set_maximum_signal_speed(signal->get_desc()->get_max_speed());
									}
								}
								else // Junction signal
								{
									// Must proceed with great caution in basic time interval and some caution even with a telegraph (see LT&S Signalling, p. 5)
									if(signal->get_desc()->is_longblock_signal() && (station_signal || next_signal_working_method == time_interval_with_telegraph))
									{
										if(next_time_interval_state == roadsign_t::danger || (last_longblock_signal_index < INVALID_INDEX && i > first_stop_signal_index))
										{
											next_signal_index = i;
											count --;
										}
										else if(next_time_interval_state == roadsign_t::caution || next_time_interval_state == roadsign_t::caution_no_choose)
										{
											cnv->set_maximum_signal_speed(min(kmh_to_speed(sch1->get_max_speed()) / 2, signal->get_desc()->get_max_speed() / 2));
										}
										else if(next_time_interval_state == roadsign_t::clear || next_time_interval_state == roadsign_t::clear_no_choose)
										{
											cnv->set_maximum_signal_speed(signal->get_desc()->get_max_speed());
										}
									}
									else if(next_signal_working_method == time_interval)
									{
										cnv->set_maximum_signal_speed(min(kmh_to_speed(sch1->get_max_speed()) / 2, signal->get_desc()->get_max_speed() / 2));
									}
									else // With telegraph
									{
										// The same as for caution
										cnv->set_maximum_signal_speed(min(kmh_to_speed(sch1->get_max_speed()) / 2, signal->get_desc()->get_max_speed() / 2));
									}
								}
							}
							else
							{
								cnv->set_maximum_signal_speed(signal->get_desc()->get_max_speed());
							}
							if(next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling)
							{
								remaining_aspects = signal->get_desc()->get_aspects();
							}
							if(is_from_token && (working_method == token_block || working_method == absolute_block || next_signal_working_method == token_block || next_signal_working_method == absolute_block) && first_double_block_signal_index != last_stop_signal_index)
							{
								// Do not try to reserve beyond the first signal if this is called recursively from a token block signal.
								count --;
							}
						}
						else if((next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling) && !directional_only)
						{
							remaining_aspects = min(remaining_aspects - 1, signal->get_desc()->get_aspects());
						}
						else if(next_signal_working_method == absolute_block && pre_signals.empty() && first_double_block_signal_index != last_stop_signal_index)
						{
							// No distant signals: just reserve one block beyond the first stop signal and no more (unless the last signal is a double block signal).
							count --;
						}
						else if(next_signal_working_method == time_interval && (last_longblock_signal_index >= INVALID_INDEX || !next_signal_protects_no_junctions) && signal->get_state() == roadsign_t::danger && next_signal_protects_no_junctions && pre_signals.empty())
						{
							next_time_interval_state = signal->get_state();
							if(station_signal)
							{
								stop_at_station_signal = check_halt;
							}
							else
							{
								next_signal_index = i;
								count --;
							}
						}
						else if(next_signal_working_method == time_interval_with_telegraph || next_signal_working_method == time_interval)
						{
							if(signal->get_desc()->is_longblock_signal() && (last_longblock_signal_index < INVALID_INDEX || working_method == time_interval_with_telegraph))
							{

								if(next_time_interval_state == roadsign_t::danger || (last_longblock_signal_index < INVALID_INDEX && i > first_stop_signal_index))
								{
									if(station_signal)
									{
										stop_at_station_signal = check_halt;
									}
									else
									{
										next_signal_index = i;
										count --;
									}
								}
							}
							else
							{
								if((working_method == time_interval_with_telegraph && i > first_stop_signal_index && (last_longblock_signal_index >= INVALID_INDEX)) || !next_signal_protects_no_junctions)
								{
									if(station_signal)
									{
										stop_at_station_signal = check_halt;
									}
									else if((!directional_only || signal->get_desc()->get_working_method() != time_interval) && !signal->get_no_junctions_to_next_signal())
									{
										next_signal_index = i;
										count --;
									}
								}
								else if(working_method == time_interval && i > first_stop_signal_index)
								{
									if(station_signal)
									{
										stop_at_station_signal = check_halt;
									}
									else
									{
										next_signal_index = i;
										count --;
									}
								}
							}
						}
						else if((working_method == time_interval || working_method == time_interval_with_telegraph) && (next_signal_working_method == absolute_block || next_signal_working_method == token_block || next_signal_working_method == track_circuit_block || next_signal_working_method == moving_block || next_signal_working_method == cab_signalling))
						{
							// When transitioning out of the time interval working method, do not attempt to reserve beyond the first stop signal of a different working method until the last time interval signal before the first signal of the other method has been passed.
							next_signal_index = i;
							count --;
						}

						if(signal->get_desc()->get_working_method() == token_block)
						{
							last_token_block_signal_index = i;
							const bool platform_starter = (this_halt.is_bound() && (haltestelle_t::get_halt(signal->get_pos(), get_owner())) == this_halt)
								&& (haltestelle_t::get_halt(get_pos(), get_owner()) == this_halt) && (cnv->get_akt_speed() == 0);

							// Do not reserve through a token block signal: the train must stop to take the token.
							if(((last_token_block_signal_index > first_stop_signal_index || (!starting_at_signal && !platform_starter)) && (first_double_block_signal_index != last_stop_signal_index)) ||
								(first_double_block_signal_index == INVALID_INDEX && last_stop_signal_index == INVALID_INDEX && !starting_at_signal && !platform_starter))
							{
								count --;
								end_of_block = true;
								do_not_clear_distant = true;
							}
						}

						if(!directional_only)
						{
							this_stop_signal_index = i;
						}
						if(signal->get_desc()->is_longblock_signal() || (station_signal && last_longblock_signal_index == INVALID_INDEX))
						{
							last_longblock_signal_index = i;
						}

						// Any junctions previously found no longer apply to the next signal, unless this is a pre-signal
						no_junctions_to_next_signal = true;

					}
					else if(!directional_only) // Distant signal or repeater
					{
						if(next_signal_working_method == absolute_block || next_signal_working_method == token_block)
						{
							const grund_t* gr_last_signal = welt->lookup(cnv->get_last_signal_pos());
							signal_t* last_signal = gr_last_signal ? gr_last_signal->find<signal_t>() : NULL;
							koord3d last_signalbox_pos = last_signal ? last_signal->get_signalbox() : koord3d::invalid;

							if(signalbox_last_distant_signal == koord3d::invalid
								&& i - start_index <= modified_sighting_distance_tiles
								&& (last_signalbox_pos == koord3d::invalid
								 || last_signalbox_pos != signal->get_signalbox()
								 || (!last_signal || !signal->get_desc()->get_intermediate_block())
								 || (signal->get_desc()->get_intermediate_block() ^ last_signal->get_desc()->get_intermediate_block())) // Cannot be two intermediate blocks in a row.
								&& (pre_signals.empty() || first_stop_signal_index == INVALID_INDEX))
							{
								pre_signals.append(signal);
								last_pre_signal_index = i;
								signalbox_last_distant_signal = signal->get_signalbox();
								last_distant_signal_was_intermediate_block = signal->get_desc()->get_intermediate_block();
							}
						}
						else if(next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling)
						{
							// In this mode, distant signals are regarded as mere repeaters.
							if(remaining_aspects < 2 && pre_signals.empty())
							{
								remaining_aspects = 3;
							}
							if(first_stop_signal_index == INVALID_INDEX)
							{
								pre_signals.append(signal);
								last_pre_signal_index = i;
							}
						}
						else if((next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph) && last_pre_signal_index >= INVALID_INDEX)
						{
							last_pre_signal_index = i;
						}
					}
				}
			}

			station_signal_to_clear_only_point:

			if(next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph)
			{
				uint16 time_interval_starting_point;
				if(next_signal_working_method == time_interval && last_station_tile < INVALID_INDEX && set_first_station_signal && (last_station_tile > last_stop_signal_index || last_station_tile > this_stop_signal_index))
				{
					time_interval_starting_point = last_station_tile;
				}
				else if(last_stop_signal_index < INVALID_INDEX)
				{
					time_interval_starting_point = last_stop_signal_index;
				}
				else
				{
					time_interval_starting_point = i;
				}
				time_interval_reservation =
					   (!next_signal_protects_no_junctions || first_double_block_signal_index < INVALID_INDEX)
					&& ((this_stop_signal_index < INVALID_INDEX || last_stop_signal_index < INVALID_INDEX) || last_choose_signal_index < INVALID_INDEX) && (count >= 0 || i == start_index)
					&& ((i - time_interval_starting_point <= welt->get_settings().get_sighting_distance_tiles()) || next_signal_working_method == time_interval_with_telegraph
						|| (check_halt.is_bound() && check_halt == last_step_halt && (next_signal_working_method == time_interval_with_telegraph || previous_time_interval_reservation == is_true || previous_time_interval_reservation == is_uncertain))); // Time interval with telegraph signals have no distance limit for reserving.
			}

			const bool telegraph_directional = (time_interval_reservation && previous_telegraph_directional) || (next_signal_working_method == time_interval_with_telegraph && (((last_longblock_signal_index == last_stop_signal_index && first_stop_signal_index == last_longblock_signal_index) && last_longblock_signal_index < INVALID_INDEX) || (first_double_block_signal_index < INVALID_INDEX && next_signal_protects_no_junctions)));
			const schiene_t::reservation_type rt = directional_only || (telegraph_directional && i > modified_sighting_distance_tiles) ? schiene_t::directional : schiene_t::block;
			bool transitioning_from_time_interval = (working_method == time_interval || working_method == time_interval_with_telegraph) && (next_signal_working_method == absolute_block || next_signal_working_method == track_circuit_block || next_signal_working_method == token_block);
			bool attempt_reservation = directional_only || time_interval_reservation || previous_telegraph_directional || ((next_signal_working_method != time_interval && next_signal_working_method != time_interval_with_telegraph && ((next_signal_working_method != drive_by_sight && !transitioning_from_time_interval) || i < start_index + modified_sighting_distance_tiles + 1)) && (!stop_at_station_signal.is_bound() || stop_at_station_signal == check_halt));
			previous_telegraph_directional = telegraph_directional;
			previous_time_interval_reservation = time_interval_reservation ? is_true : is_false;
			if(!reserving_beyond_a_train && attempt_reservation && !sch1->reserve(cnv->self, ribi_type(route->at(max(1u,i)-1u), route->at(min(route->get_count()-1u,i+1u))), rt, (working_method == time_interval || working_method == time_interval_with_telegraph)))
			{
				not_entirely_free = true;
				if (from_call_on)
				{
					next_signal_working_method = drive_by_sight;
				}

				if (directional_only && restore_last_stop_signal_index < INVALID_INDEX)
				{
					last_stop_signal_index = next_signal_index = restore_last_stop_signal_index;
				}

				if (first_double_block_signal_index < INVALID_INDEX && stop_signals_since_last_double_block_signal < 2)
				{
					// If the last but one signal is a double block signal, do not allow the train to pass beyond that signal
					// even if the route to the next signal is free
					last_stop_signal_index = first_double_block_signal_index;
					last_stop_signal_pos = route->at(first_double_block_signal_index);
					if (next_signal_index > last_stop_signal_index)
					{
						next_signal_index = last_stop_signal_index;
					}
				}

				if(next_signal_index == i && (next_signal_working_method == absolute_block || next_signal_working_method == token_block || next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling))
				{
					// If the very last tile of a block is not free, do not enter the block.
					next_signal_index = last_stop_signal_index < INVALID_INDEX || this_stop_signal_index == INVALID_INDEX ? last_stop_signal_index : this_stop_signal_index;
				}
				if((next_signal_working_method == drive_by_sight || next_signal_working_method == moving_block) && !directional_only && last_choose_signal_index >= INVALID_INDEX && !is_choosing)
				{
					next_signal_index = i - 1;
					break;
				}

				if(((next_signal_working_method == absolute_block || next_signal_working_method == token_block || next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph) && first_stop_signal_index < i) && !directional_only)
				{
					// Because the distant signal applies to all signals controlled by the same signalbox, the driver cannot know that the route
					// will be clear beyond the *first* stop signal after the distant.
					do_not_clear_distant = true;
					if(next_signal_working_method == absolute_block || next_signal_working_method == token_block)
					{
						if((pre_signals.empty() && first_stop_signal_index > start_index + modified_sighting_distance_tiles))
						{
							next_signal_index = first_stop_signal_index;
						}
						if(next_signal_index == start_index && !is_choosing)
						{
							success = false;
							directional_reservation_succeeded = false;
						}
						break;
					}
				}

				if((next_signal_working_method == track_circuit_block
					|| next_signal_working_method == cab_signalling)
					&& last_stop_signal_index < INVALID_INDEX
					&& !directional_only
					&& ((next_signal_index <= last_stop_signal_before_first_bidirectional_signal_index
					&& last_stop_signal_before_first_bidirectional_signal_index < INVALID_INDEX)
					|| sch1->can_reserve(cnv->self, ribi_type(route->at(max(1u,i)-1u), route->at(min(route->get_count()-1u,i+1u))), schiene_t::directional)))
				{
					next_signal_index = last_stop_signal_index;
					break;
				}
				if((directional_only || next_signal_index > last_stop_signal_before_first_bidirectional_signal_index) && i < first_oneway_sign_index)
				{
					// Do not allow the train to proceed along a section of bidirectionally signalled line unless
					// a directional reservation can be secured the whole way.
					next_signal_index = last_stop_signal_before_first_bidirectional_signal_index;
				}
				success = false;
				directional_reservation_succeeded = false;
				if(next_signal_index > first_stop_signal_index && last_stop_signal_before_first_bidirectional_signal_index >= INVALID_INDEX && i < first_oneway_sign_index)
				{
					next_signal_index = first_stop_signal_index;
					break;
				}
			}
			else if(attempt_reservation ||
				(next_signal_working_method == time_interval || (next_signal_working_method == time_interval_with_telegraph
				&& !next_signal_protects_no_junctions && this_stop_signal_index == i && !directional_only)))
			{
				if(this_stop_signal_index != INVALID_INDEX && !directional_only)
				{
					last_stop_signal_index = this_stop_signal_index;
					last_stop_signal_pos = route->at(this_stop_signal_index);
				}

				if(attempt_reservation && !directional_only)
				{
					if(sch1->is_crossing())
					{
						crossing_t* cr = gr->find<crossing_t>(2);
						if(cr)
						{
							// Actually reserve the crossing
							cr->request_crossing(this);
						}
					}
				}

				if(end_of_block && !is_from_token && !directional_only)
				{
					next_signal_index = last_stop_signal_index;
					break;
				}

			}

			last_step_halt = check_halt;

			// Do not attempt to reserve beyond a train ahead.
			if(reserving_beyond_a_train || ((next_signal_working_method != time_interval_with_telegraph || next_time_interval_state == roadsign_t::danger || attempt_reservation) && !directional_only && sch1->get_reserved_convoi().is_bound() && sch1->get_reserved_convoi() != cnv->self && sch1->get_reserved_convoi()->get_pos() == pos))
			{
				if (attempt_reservation)
				{
					success = false;
					if (stop_at_station_signal == check_halt)
					{
						next_signal_index = first_stop_signal_index;
					}
					break;
				}
				else
				{
					// This is necessary in case attempt reservation is enabled in a subsequent step.
					reserving_beyond_a_train = true;
				}
			}

			// check if there is an early platform available to stop at
			if(do_early_platform_search)
			{
				if(early_platform_index == INVALID_INDEX)
				{
					if(gr->get_halt().is_bound() && gr->get_halt() == dest_halt)
					{
						if(ribi == ribi_last)
						{
							platform_size_found++;
						}
						else
						{
							platform_size_found = 1;
						}

						if(platform_size_found >= platform_size_needed)
						{
							// Now check to make sure that the actual destination tile is not just a few tiles down the same platform
							uint16 route_ahead_check = i;
							koord3d current_pos = pos;
							while(current_pos != cnv->get_schedule()->get_current_entry().pos && haltestelle_t::get_halt(current_pos, cnv->get_owner()) == dest_halt && route_ahead_check < route->get_count())
							{
								current_pos = route->at(route_ahead_check);
								route_ahead_check++;
								platform_size_found++;
							}
							if(current_pos != cnv->get_schedule()->get_current_entry().pos)
							{
								early_platform_index = i;
							}
							else
							{
								platform_size_found = 0;
								do_early_platform_search = false;
							}
						}
					}
					else
					{
						platform_size_found = 0;
					}
				}
				else if(ribi_last == ribi && gr->get_halt().is_bound() && gr->get_halt() == dest_halt)
				{
					// A platform was found, but it continues so go on to its end
					early_platform_index = i;
				}
				else
				{
					// A platform was found, and has ended - check where this convoy should stop.
					if(platform_size_needed < platform_size_found && !schedule->get_current_entry().reverse)
					{
						// Do not go to the end, but stop part way along the platform.
						const uint16 difference = platform_size_found - platform_size_needed;
						early_platform_index -= (difference / 2u);
						if(difference > 2)
						{
							early_platform_index ++;
						}
					}
					sch1->unreserve(cnv->self);
					route->remove_koord_from(early_platform_index);
					if(next_signal_index > early_platform_index && !is_from_token && next_signal_working_method != one_train_staff)
					{
						next_signal_index = INVALID_INDEX;
					}
					success = true;
					reached_end_of_loop = true;
					break;
				}
				ribi_last = ribi;
			} // Early platform search
		} //if(reserve)

		else if(sch1) // Unreserve
		{
			if(!sch1->unreserve(cnv->self))
			{
				if(unreserve_now)
				{
					// reached an reserved or free track => finished
					return 0;
				}
			}
			else
			{
				// un-reserve from here (used during sale, since there might be reserved tiles not freed)
				unreserve_now = !force_unreserve;
			}
			if(sch1->has_signal())
			{
				ribi_t::ribi direction_of_travel = ribi_type(route->at(max(1u,i)-1u), route->at(min(route->get_count()-1u,i+1u)));
				signal_t* signal = sch1->get_signal(direction_of_travel);
				if(signal && !signal->get_no_junctions_to_next_signal())
				{
					if(signal->get_desc()->is_pre_signal())
					{
						signal->set_state(roadsign_t::caution);
					}
					else
					{
						signal->set_state(roadsign_t::danger);
					}
				}
			}
			if(sch1->is_crossing())
			{
				crossing_t* cr = gr->find<crossing_t>();
				if(cr) {
					cr->release_crossing(this);
				}
			}
		} // Unreserve

		if(i >= route->get_count() - 1)
		{
			reached_end_of_loop = true;
			if(!is_from_token)
			{
				next_signal_index = INVALID_INDEX;
			}
		}
	} // For loop

	if(!reserve)
	{
		return 0;
	}
	// here we go only with reserve

	koord3d signal_pos;
	if (i >= route->get_count())
	{
		if (next_signal_index == i)
		{
			// This deals with a very specific problem in which the route is truncated
			signal_pos = pos;
		}
		else
		{
			signal_pos = koord3d::invalid;
		}
	}
	else
	{
		signal_pos = next_signal_index < INVALID_INDEX ? route->at(next_signal_index) : koord3d::invalid;
	}
	bool platform_starter = (this_halt.is_bound() && (haltestelle_t::get_halt(signal_pos, get_owner())) == this_halt) && (haltestelle_t::get_halt(get_pos(), get_owner()) == this_halt) && first_stop_signal_index == last_stop_signal_index;

	// If we are in token block or one train staff mode, one train staff mode or making directional reservations, reserve to the end of the route if there is not a prior signal.
	// However, do not call this if we are in the block reserver already called from this method to prevent infinite recursion.
	const bool bidirectional_reservation = (working_method == track_circuit_block || working_method == cab_signalling || working_method == moving_block)
		&& last_bidirectional_signal_index < INVALID_INDEX && (last_oneway_sign_index >= INVALID_INDEX || last_oneway_sign_index < last_bidirectional_signal_index);
	const bool one_train_staff_onward_reservation = working_method == one_train_staff || start_index == first_one_train_staff_index;
	if(!is_from_token && !is_from_directional && (((working_method == token_block || (first_double_block_signal_index < INVALID_INDEX && stop_signals_since_last_double_block_signal)) && last_token_block_signal_index < INVALID_INDEX) || bidirectional_reservation || one_train_staff_onward_reservation) && next_signal_index == INVALID_INDEX)
	{
		route_t target_rt;
		schedule_t *schedule = cnv->get_schedule();
		uint8 schedule_index = schedule->get_current_stop();
		bool rev = cnv->get_reverse_schedule();
		bool no_reverse = schedule->entries[schedule_index].reverse == 0;
		schedule->increment_index(&schedule_index, &rev);
		koord3d cur_pos = route->back();
		uint16 next_next_signal = INVALID_INDEX;
		bool route_success;
		sint32 token_block_blocks = 0;
		if(no_reverse || one_train_staff_onward_reservation)
		{
			bool break_loop_recursive = false;
			if (one_train_staff_onward_reservation && working_method != one_train_staff)
			{
				set_working_method(one_train_staff);
			}
			do
			{
				// Search for route until the next signal is found.
				route_success = target_rt.calc_route(welt, cur_pos, cnv->get_schedule()->entries[schedule_index].pos, this, speed_to_kmh(cnv->get_min_top_speed()), cnv->get_highest_axle_load(), cnv->has_tall_vehicles(), 8888 + cnv->get_tile_length(), SINT64_MAX_VALUE, cnv->get_weight_summary().weight / 1000) == route_t::valid_route;

				if(route_success)
				{
					if (one_train_staff_onward_reservation && first_one_train_staff_index < INVALID_INDEX && !this_signal_is_nonadjacent_one_train_staff_cabinet)
					{
						cnv->set_last_signal_pos(route->at(first_one_train_staff_index));
					}

					token_block_blocks = block_reserver(&target_rt, 1, modified_sighting_distance_tiles, next_next_signal, 0, true, false, false, !bidirectional_reservation, false, bidirectional_reservation, brake_steps, one_train_staff_onward_reservation ? first_one_train_staff_index : INVALID_INDEX, false, &break_loop_recursive);
				}

				if(token_block_blocks && next_next_signal < INVALID_INDEX)
				{
					// There is a signal in a later part of the route to which we can reserve now.
					if(bidirectional_reservation)
					{
						if(signal_t* sg = welt->lookup(target_rt.at(next_next_signal))->get_weg(get_waytype())->get_signal(ribi_type((target_rt.at(next_next_signal - 1)), target_rt.at(next_next_signal))))
						{
							if(!sg->is_bidirectional() || sg->get_desc()->is_longblock_signal())
							{
								break;
							}
						}
					}
					else
					{
						if(!one_train_staff_onward_reservation)
						{
							cnv->set_next_stop_index(cnv->get_route()->get_count() - 1u);
						}
						break;
					}
				}

				no_reverse = schedule->entries[schedule_index].reverse != 1;

				if(token_block_blocks)
				{
					// prepare for next leg of schedule
					cur_pos = target_rt.back();
					schedule->increment_index(&schedule_index, &rev);
				}
				else
				{
					success = false;
					if (first_stop_signal_index < INVALID_INDEX && next_signal_index >= INVALID_INDEX)
					{
						next_signal_index = first_stop_signal_index;
					}
				}
			} while((schedule_index != cnv->get_schedule()->get_current_stop()) && token_block_blocks && no_reverse && !break_loop_recursive);
		}



		if(token_block_blocks && !bidirectional_reservation)
		{
			if(cnv->get_next_stop_index() - 1 <= route_index)
			{
				if (one_train_staff_onward_reservation && next_signal_index >= INVALID_INDEX)
				{
					cnv->set_next_stop_index(next_next_signal);
				}
				else
				{
					cnv->set_next_stop_index(cnv->get_route()->get_count() - 1);
				}
			}
		}
	}

	if(last_stop_signal_index < INVALID_INDEX && last_bidirectional_signal_index < INVALID_INDEX && first_oneway_sign_index >= INVALID_INDEX && directional_reservation_succeeded && end_of_block)
	{
		 //TODO: Consider whether last_stop_signal_index should be assigned to last_bidirectional_signal_index
		 next_signal_index = last_stop_signal_index;
		 platform_starter = (this_halt.is_bound() && i < route->get_count() && (haltestelle_t::get_halt(route->at(last_stop_signal_index), get_owner())) == this_halt) && (haltestelle_t::get_halt(get_pos(), get_owner()) == this_halt);
	}


	/*if (no_junctions_to_last_signal && no_junctions_to_next_signal && reached_end_of_loop && success && last_stop_signal_index < INVALID_INDEX && i > (last_stop_signal_index + 1))
	{
		const grund_t* gr_signal = welt->lookup(last_stop_signal_pos);
		signal_t* signal = gr_signal->find<signal_t>();
		if (signal && !signal->get_desc()->is_choose_sign())
		{
			signal->set_no_junctions_to_next_signal(true);
		}
	}*/

	bool choose_route_identical_to_main_route = false;

	// free, in case of un-reserve or no success in reservation
	// or alternatively free that section reserved beyond the last signal to which reservation can take place
	if(!success || !directional_reservation_succeeded || ((next_signal_index < INVALID_INDEX) && (next_signal_working_method == absolute_block || next_signal_working_method == token_block || next_signal_working_method == track_circuit_block || next_signal_working_method == cab_signalling || ((next_signal_working_method == time_interval || next_signal_working_method == time_interval_with_telegraph) && !next_signal_protects_no_junctions))))
	{
		const bool will_choose = (last_choose_signal_index < INVALID_INDEX) && !is_choosing && not_entirely_free && (last_choose_signal_index == first_stop_signal_index) && !is_from_token;
		// free reservation
		uint16 curtailment_index;
		bool do_not_increment_curtailment_index_directional = false;
		if(!success)
		{
			const uint16 next_stop_index = cnv->get_next_stop_index();

			if (working_method_set_by_distant_only == set)
			{
				working_method = old_working_method;
			}
			if (next_stop_index == start_index + 1)
			{
				curtailment_index = start_index;
			}
			else if (!directional_reservation_succeeded && next_signal_working_method == time_interval_with_telegraph && next_signal_index < INVALID_INDEX)
			{
				curtailment_index = next_signal_index;
			}
			else
			{
				curtailment_index = max(start_index, cnv->get_next_stop_index());
			}
		}
		else if(!directional_reservation_succeeded)
		{
			curtailment_index = last_non_directional_index;
			do_not_increment_curtailment_index_directional = true;
		}
		else if(will_choose && first_double_block_signal_index > next_signal_index)
		{
			curtailment_index = last_choose_signal_index;
		}
		else
		{
			curtailment_index = next_signal_index;
		}

		if(!directional_only && !do_not_increment_curtailment_index_directional)
		{
			curtailment_index ++;
		}

		if(next_signal_index < INVALID_INDEX && (next_signal_index == start_index || platform_starter) && !is_from_token && !is_choosing)
		{
			// Cannot go anywhere either because this train is already on the tile of the last signal to which it can go, or is in the same station as it.
			success = false;
		}

		if(route->get_count() <= start_index)
		{
			// Occasionally, the route may be recalculated here when there is a combination of a choose signal and misplaced bidirectional or longblock signal without
			// the start index changing. This can cause errors in the next line.
			start_index = 0;
		}

		if((working_method == token_block || working_method == one_train_staff) && success == false)
		{
			cnv->unreserve_route();
			cnv->reserve_own_tiles();
		}
		else if(curtailment_index < i)
		{
			const halthandle_t halt_current = haltestelle_t::get_halt(get_pos(), get_owner());
			for(uint32 j = curtailment_index; j < route->get_count(); j++)
			{
				grund_t* gr_this = welt->lookup(route->at(j));
				schiene_t * sch1 = gr_this ? (schiene_t *)gr_this->get_weg(get_waytype()) : NULL;
				const halthandle_t halt_this = haltestelle_t::get_halt(route->at(j), get_owner());
				if(sch1 && (sch1->is_reserved(schiene_t::block)
					|| (!directional_reservation_succeeded
					&& sch1->is_reserved(schiene_t::directional)))
					&& sch1->get_reserved_convoi() == cnv->self
					&& sch1->get_pos() != get_pos()
					&& (!halt_current.is_bound() || halt_current != halt_this))
				{
					sch1->unreserve(cnv->self);
					if(sch1->has_signal())
					{
						signs.remove(gr_this);
					}
					if(sch1->is_crossing())
					{
						crossing_t* cr = gr_this->find<crossing_t>();
						if(cr) {
							cr->release_crossing(this);
						}
					}
				}
			}
		}

		if(will_choose)
		{
			// This will call the block reserver afresh from the last choose signal with choose logic enabled.
			sint32 modified_route_index;
			const uint32 route_count = route->get_count();
			if(is_from_token || is_from_directional)
			{
				modified_route_index = min(route_count, (route_index - route_count));
			}
			else
			{
				modified_route_index = min(route_index, route_count);
			}

			const koord3d check_tile_mid = route->at(route_count / 2u);
			const koord3d check_tile_end = route->back();

			choose_return = activate_choose_signal(last_choose_signal_index, next_signal_index, brake_steps, modified_sighting_distance_tiles, route, modified_route_index);

			if (success && choose_return == 0)
			{
				// Restore the curtailed route above: the original route works, but the choose route does not.
				bool re_reserve_succeeded = true;
				const uint32 route_count = route->get_count();
				uint32 limit = min(next_signal_index, route_count);
				for (uint32 n = curtailment_index; n < limit; n++)
				{
					grund_t* gr_this = welt->lookup(route->at(n));
					schiene_t * sch1 = gr_this ? (schiene_t *)gr_this->get_weg(get_waytype()) : NULL;
					re_reserve_succeeded &= sch1->reserve(cnv->self, ribi_t::all);
				}

				if (re_reserve_succeeded)
				{
					const koord3d last_choose_pos = route->at(last_choose_signal_index);
					grund_t* const signal_ground = welt->lookup(last_choose_pos);
					if (signal_ground)
					{
						signs.append_unique(signal_ground);
					}
				}
			}

			if (route_count == route->get_count())
			{
				// Check whether the choose signal route really is a new route.
				choose_route_identical_to_main_route = check_tile_mid == route->at(route_count / 2u) && check_tile_end == route->back();
			}
		}

		if(!success && !choose_return)
		{
			cnv->set_next_reservation_index(curtailment_index);
			if (last_stop_signal_index > start_index && last_pre_signal_index < last_stop_signal_index)
			{
				if (working_method != token_block && working_method != one_train_staff)
				{
					cnv->set_working_method(drive_by_sight);
				}
				return 1;
			}
			return 0;
		}
	}

	// Clear signals on the route.
	if(!is_from_token && !is_from_directional)
	{
		// Clear any station signals
		if(first_station_signal)
		{
			if(first_station_signal->get_desc()->get_working_method() == absolute_block && success)
			{
				first_station_signal->set_state(station_signal == inverse ? roadsign_t::clear : roadsign_t::clear_no_choose);
			}
			else
			{
				first_station_signal->set_state(first_time_interval_state);
			}
		}
		if (station_signal_to_clear_for_entry)
		{
			// Clear the station signal when a train is arriving.
			// TODO: Consider whether to make this optional on a setting in the signal's .dat file
			if (station_signal_to_clear_for_entry->get_desc()->get_working_method() != absolute_block && station_signal_to_clear_for_entry->get_desc()->get_aspects() > 2)
			{
				station_signal_to_clear_for_entry->set_state(station_signal == inverse ? roadsign_t::caution : roadsign_t::caution_no_choose);
			}
			else
			{
				station_signal_to_clear_for_entry->set_state(station_signal == inverse ? roadsign_t::clear : roadsign_t::clear_no_choose);
			}
		}

		sint32 counter = (signs.get_count() - 1) + choose_return;
		bool time_interval_junction_signal = false;
		if(next_signal_working_method == time_interval && last_stop_signal_index > start_index + modified_sighting_distance_tiles && success && !not_entirely_free)
		{
			// A signal in the plain time interval method protecting a junction should clear even if the route all the way to the next signal is not clear, provided that the route within the sighting distance is clear.
			counter ++;
			time_interval_junction_signal = true;
		}

		bool last_signal_was_track_circuit_block = false;

		halthandle_t train_halt = haltestelle_t::get_halt(cnv->get_pos(), cnv->get_owner());
		halthandle_t signal_halt;

		FOR(slist_tpl<grund_t*>, const g, signs)
		{
			if(signal_t* const signal = g->find<signal_t>())
			{
				signal_halt = haltestelle_t::get_halt(g->get_pos(), cnv->get_owner());
				if(((counter -- > 0 || (pre_signals.empty() && (!starting_at_signal || signs.get_count() == 1)) ||
					(reached_end_of_loop && (early_platform_index == INVALID_INDEX ||
						last_stop_signal_index < early_platform_index))) && (signal->get_desc()->get_working_method() != token_block ||
							starting_at_signal || signal_halt == train_halt || ((start_index == first_stop_signal_index) && (first_stop_signal_index == last_stop_signal_index)))
					&& ((route->at(route->get_count() - 1) != signal->get_pos()) || signal->get_desc()->get_working_method() == token_block)))
				{
					const bool use_no_choose_aspect = choose_route_identical_to_main_route || (signal->get_desc()->is_choose_sign() && !is_choosing && choose_return == 0);

					if(signal->get_desc()->get_working_method() == absolute_block || signal->get_desc()->get_working_method() == token_block)
					{
						// There is no need to set a combined signal to clear here, as this is set below in the pre-signals clearing loop.
						if (!last_signal_was_track_circuit_block || count >= 0 || next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
						{
							signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::clear ? roadsign_t::clear_no_choose : signal->get_desc()->is_combined_signal() ? roadsign_t::caution : roadsign_t::clear);
						}
					}

					if((signal->get_desc()->get_working_method() == time_interval || signal->get_desc()->get_working_method() == time_interval_with_telegraph) && (end_of_block || i >= last_stop_signal_index + 1 || time_interval_junction_signal))
					{
						if((signal->get_desc()->get_working_method() == time_interval_with_telegraph || signal->get_desc()->get_working_method() == time_interval) && signal->get_desc()->is_longblock_signal())
						{
							// Longblock signals in time interval can clear fully.
							// We also assume that these will not also be choose signals.
							signal->set_state(next_time_interval_state);
						}
						else
						{
							signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::caution ? roadsign_t::caution_no_choose : roadsign_t::caution);
						}
					}

					if(signal->get_desc()->get_working_method() == track_circuit_block || signal->get_desc()->get_working_method() == cab_signalling)
					{
						if(count >= 0 || next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
						{
							// Do not clear the last signal in the route, as nothing is reserved beyond it, unless there are no more signals beyond at all (count == 0)
							sint32 add_value = 0;
							if (reached_end_of_loop && (signs.get_count() == 1 || (signs.get_count() > 0 && this_stop_signal_index >= INVALID_INDEX)))
							{
								add_value = 1;
							}

							if (choose_route_identical_to_main_route)
							{
								add_value -= choose_return;
							}

							switch(signal->get_desc()->get_aspects())
							{
							case 2:
							default:
								if(next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
								{
									signal->set_state(use_no_choose_aspect ? roadsign_t::clear_no_choose : roadsign_t::clear);
								}
								break;
							case 3:
								if(counter + add_value >= 1)
								{
									signal->set_state((use_no_choose_aspect && signal->get_state() != roadsign_t::clear) || signal->get_state() == roadsign_t::caution ? roadsign_t::clear_no_choose : roadsign_t::clear);
								}
								else if(next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
								{
									if (signal->get_state() == roadsign_t::danger || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::caution))
									{
										signal->set_state(use_no_choose_aspect ? roadsign_t::caution_no_choose : roadsign_t::caution);
									}
								}
								break;
							case 4:
								if(counter + add_value >= 2)
								{
									signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::clear && signal->get_state() != roadsign_t::caution && signal->get_state() != roadsign_t::preliminary_caution ? roadsign_t::clear_no_choose : roadsign_t::clear);
								}
								else if(counter + add_value >= 1)
								{
									if(signal->get_state() == roadsign_t::danger || signal->get_state() == roadsign_t::caution || signal->get_state() == roadsign_t::caution_no_choose || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::preliminary_caution))
									{
										signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::caution && signal->get_state() != roadsign_t::preliminary_caution ? roadsign_t::preliminary_caution_no_choose : roadsign_t::preliminary_caution);
									}
								}
								else if(next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
								{
									if (signal->get_state() == roadsign_t::danger || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::caution))
									{
										signal->set_state(use_no_choose_aspect ? roadsign_t::caution_no_choose : roadsign_t::caution);
									}
								}
								break;
							case 5:
								if(counter + add_value >= 3)
								{
									signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::caution && signal->get_state() != roadsign_t::clear && signal->get_state() != roadsign_t::preliminary_caution && signal->get_state() != roadsign_t::advance_caution ? roadsign_t::clear_no_choose : roadsign_t::clear);
								}
								else if(counter + add_value >= 2)
								{
									if(signal->get_state() == roadsign_t::danger || signal->get_state() == roadsign_t::caution || signal->get_state() == roadsign_t::preliminary_caution || signal->get_state() == roadsign_t::caution_no_choose || signal->get_state() == roadsign_t::preliminary_caution_no_choose || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::advance_caution))
									{
										signal->set_state(use_no_choose_aspect && signal->get_state() != roadsign_t::caution && signal->get_state() != roadsign_t::clear && signal->get_state() != roadsign_t::preliminary_caution && signal->get_state() != roadsign_t::advance_caution  ? roadsign_t::advance_caution_no_choose : roadsign_t::advance_caution);
									}
								}
								else if(counter + add_value >= 1)
								{
									if (signal->get_state() == roadsign_t::danger || signal->get_state() == roadsign_t::caution || signal->get_state() == roadsign_t::caution_no_choose || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::preliminary_caution))
									{
										signal->set_state((use_no_choose_aspect && signal->get_state() != roadsign_t::caution && signal->get_state() != roadsign_t::clear && signal->get_state() != roadsign_t::preliminary_caution && signal->get_state() != roadsign_t::advance_caution)  || signal->get_state() == roadsign_t::caution ? roadsign_t::preliminary_caution_no_choose : roadsign_t::preliminary_caution);
									}
								}
								else if(next_signal_index > route->get_count() - 1 || signal->get_pos() != route->at(next_signal_index))
								{
									if (signal->get_state() == roadsign_t::danger || (choose_route_identical_to_main_route && choose_return && signal->get_state() == roadsign_t::caution))
									{
										signal->set_state(use_no_choose_aspect ? roadsign_t::caution_no_choose : roadsign_t::caution);
									}
								}
								break;
							}
						}
					}
				}
				last_signal_was_track_circuit_block = signal->get_desc()->get_working_method() == track_circuit_block;
			}
			else
			{
				counter --;
			}
		}

		FOR(slist_tpl<signal_t*>, const signal, pre_signals)
		{
			if(!do_not_clear_distant && !(not_entirely_free && next_signal_index == first_stop_signal_index))
			{
				signal->set_state(roadsign_t::clear);
			}
		}
	}

	if(next_signal_index != INVALID_INDEX && (next_signal_working_method == track_circuit_block || next_signal_working_method == absolute_block || next_signal_working_method == cab_signalling || next_signal_working_method == token_block))
	{
		if(platform_starter && !is_from_token && !is_from_starter && !is_choosing)
		{
			// This is a platform starter signal for this station: do not move until it clears.
			/*const koord3d signal_dir = signal_pos - (route->at(min(next_signal_index + 1u, route->get_count() - 1u)));
			const ribi_t::ribi direction_of_travel = ribi_type(signal_dir);
			const signal_t* sig = welt->lookup(signal_pos)->get_weg(get_waytype())->get_signal(direction_of_travel); */
			signal_t* sig = welt->lookup(signal_pos)->find<signal_t>();
			if(sig && sig->get_state() == signal_t::danger)
			{
				sint32 success_2 = block_reserver(cnv->get_route(), modified_sighting_distance_tiles, next_signal_index, next_signal_index, 0, true, false, false, false, true);
				next_signal_index = cnv->get_next_stop_index() - 1;
				return success_2;
			}
		}
	}

	if(cnv && !is_from_token && !is_from_directional)
	{
		cnv->set_next_reservation_index(last_non_directional_index);
	}

	// Trim route to the early platform index.
	if(early_platform_index < INVALID_INDEX)
	{
		route->remove_koord_from(early_platform_index);
	}

	return reached_end_of_loop || working_method != track_circuit_block ? (!combined_signals.empty() && !pre_signals.empty() ? 2 : 1) + choose_return : directional_only && success ? 1 : (sint32)signs.get_count() + choose_return;
}

void rail_vehicle_t::clear_token_reservation(signal_t* sig, rail_vehicle_t* w, schiene_t* sch)
{
	route_t* route = cnv ? cnv->get_route() : NULL;
	if(cnv && sig && (sig->get_desc()->get_working_method() != token_block && sig->get_desc()->get_working_method() != one_train_staff) && cnv->get_state() != convoi_t::REVERSING)
	{
		convoi_t* cnv_w = w->get_convoi();
		if (cnv_w)
		{
			w->get_convoi()->set_working_method(sig->get_desc()->get_working_method());
		}
	}
	if(cnv && cnv->get_needs_full_route_flush())
	{
		// The route has been recalculated since token block mode was entered, so delete all the reservations.
		// Do not unreserve tiles ahead in the route (i.e., those not marked stale), however.
		const waytype_t waytype = sch->get_waytype();
		FOR(vector_tpl<weg_t*>, const way, weg_t::get_alle_wege())
		{
			if(way->get_waytype() == waytype)
			{
				schiene_t* const sch = obj_cast<schiene_t>(way);
				if(sch->get_reserved_convoi() == cnv->self && sch->is_stale())
				{
					sch->unreserve(w);
				}
			}
		}
		cnv->set_needs_full_route_flush(false);
	}
	else
	{
		uint32 break_now = 0;
		const bool is_one_train_staff = sig && sig->get_desc()->get_working_method() == one_train_staff;
		bool last_tile_was_break = false;
		for(int i = route_index - 1; i >= 0; i--)
		{
			grund_t* gr_route = route ? welt->lookup(route->at(i)) : NULL;
			schiene_t* sch_route = gr_route ? (schiene_t *)gr_route->get_weg(get_waytype()) : NULL;
			if ((!is_one_train_staff && (sch_route && !sch_route->is_reserved())) || (sch_route && sch_route->get_reserved_convoi() != cnv->self))
			{
				if (!last_tile_was_break)
				{
					break_now++;
				}
				last_tile_was_break = true;
			}
			else
			{
				last_tile_was_break = false;
			}
			if(sch_route && (!cnv || cnv->get_state() != convoi_t::REVERSING))
			{
				sch_route->unreserve(cnv->self);
			}
			if (break_now > 1)
			{
				break;
			}
		}
	}
}

void rail_vehicle_t::unreserve_in_rear()
{
	route_t* route = cnv ? cnv->get_route() : NULL;

	route_index = min(route_index, route ? route->get_count() - 1 : route_index);

	for (int i = route_index - 1; i >= 0; i--)
	{
		const koord3d this_pos = route->at(i);
		grund_t* gr_route = welt->lookup(this_pos);
		schiene_t* sch_route = gr_route ? (schiene_t *)gr_route->get_weg(get_waytype()) : NULL;
		if (sch_route)
		{
			sch_route->unreserve(cnv->self);
		}
	}
}

void rail_vehicle_t::unreserve_station()
{
	grund_t* gr = welt->lookup(get_pos());
	if (!gr)
	{
		return;
	}
	bool in_station = gr->get_halt().is_bound();

	route_t* route = cnv ? cnv->get_route() : NULL;
	route_index = min(route_index, route ? route->get_count() - 1 : route_index);
	if (route->get_count() < route_index || route->empty())
	{
		// The route has been recalculated, so we cannot
		// unreserve this in the usual way.
		// Instead, brute-force the unreservation.
		cnv->unreserve_route();
		cnv->reserve_own_tiles();
		return;
	}
	const koord3d this_pos = route->at(route_index);
	const koord3d last_pos = route->at(route_index - 1);
	const koord3d dir = this_pos - last_pos;
	const ribi_t::ribi direction_of_travel = ribi_type(dir);
	const ribi_t::ribi reverse_direction = ribi_t::backward(direction_of_travel);
	bool is_previous;

	grund_t* gr_prev = gr;
	while (in_station)
	{
		is_previous = gr_prev->get_neighbour(gr_prev, get_waytype(), reverse_direction);
		if (is_previous)
		{
			in_station = gr_prev->get_halt().is_bound();
			schiene_t* sch = (schiene_t*)gr_prev->get_weg(get_waytype());
			sch->unreserve(cnv->self);
		}
		else
		{
			// Sometimes, where a train leaves a station in
			// a different direction to that in which it entered
			// it (e.g., if the next tile after the end of the
			// station is a junction and the train is at the very
			// end of the station when it moves), the code cannot
			// search backwards. In this case, just use the old
			// brute force method, which is inefficient but works.
			cnv->unreserve_route();
			cnv->reserve_own_tiles();
			break;
		}
	}
}


/* beware: we must un-reserve rail blocks... */
void rail_vehicle_t::leave_tile()
{
	grund_t *gr = get_grund();
	schiene_t *sch0 = (schiene_t *) get_weg();
	vehicle_t::leave_tile();
	// fix counters
	if(last)
	{
		if(gr)
		{
			if(sch0)
			{
				rail_vehicle_t* w = cnv ? (rail_vehicle_t*)cnv->front() : NULL;

				const halthandle_t this_halt = gr->get_halt();
				const halthandle_t dest_halt = haltestelle_t::get_halt((cnv && cnv->get_schedule() ? cnv->get_schedule()->get_current_entry().pos : koord3d::invalid), get_owner());
				const bool at_reversing_destination = dest_halt.is_bound() && this_halt == dest_halt && cnv->get_schedule() && cnv->get_schedule()->get_current_entry().reverse == 1;

				route_t* route = cnv ? cnv->get_route() : NULL;
				koord3d this_tile;
				koord3d previous_tile;
				if (route)
				{
					// It can very occasionally happen that a train due to reverse at a station
					// passes through another, unconnected part of that station before it stops.
					// When this happens, an island of reservation can remain which can block
					// other trains.
					// Detect this case when it has just begun to occur and rectify it.
					if (route_index > route->get_count() - 1)
					{
						route_index = route->get_count() - 1;
					}
					const uint16 ri = min(route_index, route->get_count() - 1u);
					this_tile = cnv->get_route()->at(ri);
					previous_tile = cnv->get_route()->at(max(1u, ri) - 1u);

					if (cnv->get_schedule() && cnv->get_schedule()->get_current_entry().reverse)
					{
						grund_t* gr_previous_tile = welt->lookup(previous_tile);
						grund_t* gr_this_tile = welt->lookup(this_tile);
						if (gr_previous_tile && gr_this_tile)
						{
							if (gr_previous_tile->get_halt().is_bound() && gr_previous_tile->get_halt() == dest_halt && !gr_this_tile->get_halt().is_bound() && working_method != one_train_staff && working_method != token_block)
							{
								unreserve_in_rear();
							}
						}
					}
				}

				if((!cnv || cnv->get_state() != convoi_t::REVERSING) && !at_reversing_destination)
				{
					if (w && (w->get_working_method() == token_block || (w->get_working_method() == one_train_staff)))
					{
						// We mark these for later unreservation.
						sch0->set_stale();
					}
					else
					{
						sch0->unreserve(this);
					}
				}

				// The end of the train is passing a signal. Check whether to re-set its aspect
				// or, in the case of a distant signal in absolute or token block working, reset
				// it only when the tail of the train has passed the next home signal.
				if(sch0->has_signal())
				{
					signal_t* sig;
					if(route)
					{
						const koord3d dir = this_tile - previous_tile;
						ribi_t::ribi direction_of_travel = ribi_type(dir);
						sig = sch0->get_signal(direction_of_travel);
					}
					else
					{
						sig = gr->find<signal_t>();
					}

					// Ignore non-adjacent one train staff cabinets
					if (sig && cnv && sig->get_desc()->get_working_method() == one_train_staff)
					{
						const koord3d first_pos = cnv->get_last_signal_pos();
						if (shortest_distance(get_pos().get_2d(), first_pos.get_2d()) >= 3)
						{
							sig = NULL;
						}
					}

					if(sig)
					{
						sig->set_train_last_passed(welt->get_ticks());
						if(sig->get_no_junctions_to_next_signal() && !sig->get_desc()->is_pre_signal() && (sig->get_desc()->get_working_method() == time_interval || sig->get_desc()->get_working_method() == time_interval_with_telegraph))
						{
							welt->add_time_interval_signal_to_check(sig);
						}
						if(!route)
						{
							if(sig->get_desc()->is_pre_signal())
							{
								sig->set_state(roadsign_t::caution);
							}
							else
							{
								sig->set_state(roadsign_t::danger);
							}
							return;
						}
						else if(w &&
							(w->get_working_method() == token_block
							|| (w->get_working_method() == one_train_staff
							&& sig->get_desc()->get_working_method() == one_train_staff))
							&& !sig->get_desc()->is_pre_signal()
							&& cnv->get_state() != convoi_t::REVERSING)
						{
							// On passing a signal, clear all the so far uncleared reservation when in token block mode.
							// If the signal is not a token block signal, clear token block mode. This assumes that token
							// block signals will be placed at the entrance and stop signals at the exit of single line
							// sections.
							clear_token_reservation(sig, w, sch0);

							if (sig->get_desc()->get_working_method() == drive_by_sight)
							{
								cnv->set_working_method(drive_by_sight);
								cnv->set_next_stop_index(route_index + 1);
							}
						}
						else if((sig->get_desc()->get_working_method() == track_circuit_block
							|| sig->get_desc()->get_working_method() == cab_signalling
							|| (w && (w->get_working_method() == track_circuit_block
							|| w->get_working_method() == cab_signalling)))
							&& !sig->get_desc()->is_pre_signal())
						{
							// Must reset all "automatic" signals behind this convoy to less restrictive states unless they are of the normal danger type.
							koord3d last_pos = get_pos();
							uint32 signals_count = 0;
							for(int i = route_index - 1; i >= 0; i--)
							{
								const koord3d this_pos = route->at(i);
								const koord3d dir = last_pos - this_pos;
								ribi_t::ribi ribi_route = ribi_type(dir);
								grund_t* gr_route = welt->lookup(this_pos);
								schiene_t* sch_route = gr_route ? (schiene_t *)gr_route->get_weg(get_waytype()) : NULL;
								if(!sch_route || (!sch_route->can_reserve(cnv->self, ribi_route) && gr_route->get_convoi_vehicle()))
								{
									// Cannot go further back than this in any event
									break;
								}
								if(!cnv || cnv->get_state() != convoi_t::REVERSING)
								{
									signal_t* signal_route = sch_route->get_signal(ribi_route);
									if(!signal_route || signal_route == sig)
									{
										continue;
									}
									if(!signal_route->get_desc()->is_pre_signal() && (!signal_route->get_no_junctions_to_next_signal() || signal_route->get_desc()->is_choose_sign() || signal_route->get_desc()->get_normal_danger()))
									{
										break;
									}

									working_method_t swm = signal_route->get_desc()->get_working_method();
									if (swm != track_circuit_block && swm != cab_signalling)
									{
										// Only re-set track circuit block type signals to permissive aspects here.
										break;
									}

									switch(signals_count)
									{
									case 0:
										if(signal_route->get_desc()->get_aspects() > 2 || signal_route->get_desc()->is_pre_signal())
										{
											signal_route->set_state(roadsign_t::caution);
										}
										else
										{
											signal_route->set_state(roadsign_t::clear);
										}
										break;
									case 1:
										if(signal_route->get_desc()->get_aspects() > 3)
										{
											signal_route->set_state(roadsign_t::preliminary_caution);
										}
										else
										{
											signal_route->set_state(roadsign_t::clear);
										}
									break;
									case 2:
										if(signal_route->get_desc()->get_aspects() > 4)
										{
											signal_route->set_state(roadsign_t::advance_caution);
										}
										else
										{
											signal_route->set_state(roadsign_t::clear);
										}
										break;
									default:
										signal_route->set_state(roadsign_t::clear);
									};
									signals_count++;
								}
							}
						}

						if(!sig->get_desc()->is_pre_signal())
						{
							sig->set_state(roadsign_t::danger);
						}

						if(route && !sig->get_desc()->is_pre_signal() && (w && (w->get_working_method() == absolute_block || w->get_working_method() == token_block || w->get_working_method() == time_interval || w->get_working_method() == time_interval_with_telegraph)))
						{
							// Set distant signals in the rear to caution only after the train has passed the stop signal.
							int count = 0;
							for(int i = min(route_index, route->get_count() - 1); i > 0; i--)
							{
								const koord3d current_pos = route->at(i);
								grund_t* gr_route = welt->lookup(current_pos);
								ribi_t::ribi ribi;
								if(i == 0)
								{
									ribi = ribi_t::all;
								}
								else
								{
									ribi = ribi_type(route->at(i - 1), current_pos);
								}
								const weg_t* wg = gr_route ? gr_route->get_weg(get_waytype()) : NULL;
								signal_t* sig_route = wg ? wg->get_signal(ribi) : NULL;
								//signal_t* sig_route = gr_route->find<signal_t>();
								if(sig_route)
								{
									count++;
								}
								if(sig_route && sig_route->get_desc()->is_pre_signal())
								{
									sig_route->set_state(roadsign_t::caution);
									if(sig_route->get_desc()->get_working_method() == time_interval || sig_route->get_desc()->get_working_method() == time_interval_with_telegraph)
									{
										sig_route->set_train_last_passed(welt->get_ticks());
										if(sig->get_no_junctions_to_next_signal())
										{
											// Junction signals reserve within sighting distance in ordinary time interval mode, or to the next signal in time interval with telegraph mode.
											welt->add_time_interval_signal_to_check(sig_route);
										}
									}
									break;
								}
								if(count > 1)
								{
									break;
								}
							}
						}
					}
				}

				// Alternatively, there might be a station signal.
				// Set the timings only on leaving the stop.
				halthandle_t last_tile_halt = cnv && route_index - 2 < cnv->get_route()->get_count() ? haltestelle_t::get_halt(cnv->get_route()->at(route_index - 2), get_owner()) : halthandle_t();
				const uint32 station_signals_count = last_tile_halt.is_bound() ? last_tile_halt->get_station_signals_count() : 0;
				if(station_signals_count && !this_halt.is_bound())
				{
					ribi_t::ribi direction = ribi_type(cnv->get_route()->at(0u), cnv->get_route()->at(1u));
					last_tile_halt->set_train_last_departed(welt->get_ticks(), direction);
					for(uint j = 0; j < station_signals_count; j ++)
					{
						koord3d station_signal_pos = last_tile_halt->get_station_signal(j);
						grund_t* gr_ssp = welt->lookup(station_signal_pos);
						if(gr_ssp)
						{
							weg_t* station_signal_way = gr_ssp->get_weg(get_waytype());
							if(station_signal_way)
							{
								signal_t* station_signal = station_signal_way->get_signal(direction);
								if(!station_signal)
								{
									// There might be a signal in the inverse direction.
									ribi_t::ribi backwards = ribi_t::backward(direction);
									station_signal = station_signal_way->get_signal(backwards);
								}
								if(station_signal)
								{
									station_signal->set_state(roadsign_t::danger);
								}
							}
						}
					}
				}
			}
		}
	}
}


void rail_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(  schiene_t *sch0 = (schiene_t *) get_weg()  ) {
		// way statistics
		const int cargo = get_total_cargo();
		sch0->book(cargo, WAY_STAT_GOODS);
		if(leading) {
			sch0->book(1, WAY_STAT_CONVOIS);
			if(cnv->get_state() != convoi_t::REVERSING)
			{
				sch0->reserve( cnv->self, get_direction() );
			}
		}
	}
}


void rail_vehicle_t::rdwr_from_convoi(loadsave_t* file)
{
	xml_tag_t t(file, "rail_vehicle_t");

	vehicle_t::rdwr_from_convoi(file);
#ifdef SPECIAL_RESCUE_12_5
	if (file->get_extended_version() >= 12 && file->is_saving())
#else
	if (file->get_extended_version() >= 12)
#endif
	{
		uint8 wm = (uint8)working_method;
		file->rdwr_byte(wm);
		working_method = (working_method_t)wm;
	}
}


schedule_t * rail_vehicle_t::generate_new_schedule() const
{
	return desc->get_waytype()==tram_wt ? new tram_schedule_t() : new train_schedule_t();
}


schedule_t * monorail_rail_vehicle_t::generate_new_schedule() const
{
	return new monorail_schedule_t();
}


schedule_t * maglev_rail_vehicle_t::generate_new_schedule() const
{
	return new maglev_schedule_t();
}


schedule_t * narrowgauge_rail_vehicle_t::generate_new_schedule() const
{
	return new narrowgauge_schedule_t();
}
