/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "simconvoi.h"
#include "vehicle/vehicle.h"
#include "gui/simwin.h"
#include "player/simplay.h"
#include "player/finance.h"
#include "simworld.h"
#include "simcity.h"
#include "simdepot.h"
#include "simline.h"
#include "simlinemgmt.h"
#include "simmenu.h"
#include "path_explorer.h"

#include "gui/depot_frame.h"
#include "gui/messagebox.h"

#include "dataobj/schedule.h"
#include "dataobj/loadsave.h"
#include "dataobj/translator.h"
#include "dataobj/replace_data.h"

#include "bauer/hausbauer.h"
#include "obj/gebaeude.h"

#include "bauer/vehikelbauer.h"

#include "descriptor/building_desc.h"

#include "utils/cbuffer_t.h"

#include "vehicle/air_vehicle.h"
#include "vehicle/rail_vehicle.h"
#include "vehicle/road_vehicle.h"
#include "vehicle/water_vehicle.h"


// Split a convoy's vehicles into those that can be overhauled, those that are
// only maintained, and the due overhauls that the owner cannot currently afford.
static void classify_vehicle_for_maintenance(vehicle_t *v, vector_tpl<vehicle_t*> &overhaul, vector_tpl<vehicle_t*> &maintain, vector_tpl<vehicle_t*> *unaffordable)
{
	if (v->is_overhaul_needed() && !v->get_do_not_overhaul())
	{
		if (v->can_afford_overhaul())
		{
			overhaul.append(v);
			return;
		}
		if (unaffordable)
		{
			unaffordable->append(v);
		}
	}
	maintain.append(v);
}

slist_tpl<depot_t *> depot_t::all_depots;


#ifdef INLINE_OBJ_TYPE
depot_t::depot_t(obj_t::typ type, loadsave_t *file) : gebaeude_t(type)
#else
depot_t::depot_t(loadsave_t *file) : gebaeude_t()
#endif
{
	rdwr(file);
	if(file->is_version_less(88, 2)) {
		set_yoff(0);
	}
	all_depots.append(this);
	welt->sync.add(this);
	last_selected_line = linehandle_t();
	command_pending = false;
}


#ifdef INLINE_OBJ_TYPE
depot_t::depot_t(obj_t::typ type, koord3d pos, player_t *player, const building_tile_desc_t *t) :
    gebaeude_t(type, pos, player, t)
#else
depot_t::depot_t(koord3d pos, player_t *player, const building_tile_desc_t *t) :
	gebaeude_t(pos, player, t)
#endif
{
	all_depots.append(this);
	last_selected_line = linehandle_t();
	command_pending = false;
	strcpy(name, "unnamed");
	add_to_world_list();
	welt->sync.add(this);
}


depot_t::~depot_t()
{
	destroy_win((ptrdiff_t)this);
	all_depots.remove(this);
	const grund_t* gr = welt->lookup(get_pos());
	if(gr)
	{
		welt->remove_building_from_world_list(this);
		// No need to remove this from the city statistics here, as this will be done by the gebaeude_t parent object.
	}
	if(!welt->is_destroying())
	{
		// Hand queued convoys back to the world sync list: with the depot gone their countdown
		// no longer runs here (maintained convoys are kept out of welt.sync while queued), and
		// their sync_step self-heals them to INITIAL when it finds no depot.
		for(auto const c : under_maintenance)
		{
			if(c.is_bound())
			{
				welt->sync.add(c.get_rep());
			}
		}
	}
	welt->sync.remove(this);
}


// finds the next/previous depot relative to the current position
depot_t *depot_t::find_depot( koord3d start, const obj_t::typ depot_type, const player_t *player, bool forward)
{
	depot_t *found = NULL;
	koord3d found_pos = forward ? koord3d(welt->get_size().x+1,welt->get_size().y+1,welt->get_groundwater()) : koord3d(-1,-1,-1);
	sint32 found_hash = forward ? 0x7FFFFFF : -1;
	sint32 start_hash = start.x + (8192 * start.y);
	for(auto const d : all_depots)
	{
		if(d->get_typ()==depot_type  &&  d->get_owner()==player) {
			// ok, the right type of depot
			const koord3d pos = d->get_pos();
			if(pos==start) {
				// ignore the start point
				continue;
			}
			sint32 hash = (pos.x + (8192 * pos.y));
			if(forward) {
				if(hash>start_hash  ||  (hash==start_hash  &&  pos.z>start.z)) {
				// found a suitable one
					if(hash<found_hash  ||  (hash==found_hash  &&  pos.z<found_pos.z)) {
						// which is closer ...
						found = d;
						found_pos = pos;
						found_hash = hash;
					}
				}
			}
			else {
				// search to start of the map
				if(hash<start_hash  ||  (hash==start_hash  &&  pos.z<start.z)) {
				// found a suitable one
					if(hash>found_hash  ||  (hash==found_hash  &&  pos.z>found_pos.z)) {
						// which is closer ...
						found = d;
						found_pos = pos;
						found_hash = hash;
					}
				}
			}
		}
	}
	return found;
}

depot_t *depot_t::find_nearest_depot( koord3d start, const vehicle_t *test_vehicle )
{
	depot_t *found = NULL;
	uint32 found_distance = 0xFFFFFFFFu;
	for(auto const d : all_depots)
	{
		if(d->is_suitable_for(test_vehicle))
		{
			const uint32 distance = shortest_distance(start.get_2d(), d->get_pos().get_2d());
			if(distance < found_distance)
			{
				found = d;
				found_distance = distance;
			}
		}
	}
	return found;
}

unsigned depot_t::get_max_convoy_length(waytype_t wt)
{
	if (wt==road_wt || wt==water_wt) {
		return convoi_t::max_vehicle;
	}
	if (wt==air_wt) {
		return 1;
	}
	return convoi_t::max_rail_vehicle;
}

// again needed for server
void depot_t::call_depot_tool( char tool, convoihandle_t cnv, const char *extra, uint16 livery_scheme_index)
{
	// call depot tool
	tool_t *tmp_tool = create_tool( TOOL_CHANGE_DEPOT | SIMPLE_TOOL );
	cbuffer_t buf;
	buf.printf( "%c,%s,%hu,%hu", tool, get_pos().get_str(), cnv.get_id(), livery_scheme_index );
	if(  extra  ) {
		buf.append( "," );
		buf.append( extra );
	}
	tmp_tool->set_default_param(buf);
	welt->set_tool( tmp_tool, get_owner() );
	// since init always returns false, it is safe to delete immediately
	delete tmp_tool;
}


/* this is called on two occasions:
 * first a convoy reaches the depot during its journey (or on emergency stop)
 * second during loading a convoi is stored in a depot => only store it again
 */
void depot_t::convoi_arrived(convoihandle_t acnv, uint16 flags)
{
	if (flags & schedule_entry_t::delete_entry)
	{
		// Volker: remove depot from schedule
		schedule_t *schedule = acnv->get_schedule();
		for (int i = 0; i < schedule->get_count(); i++) {
			// only if convoi found
			if (schedule->entries[i].pos == get_pos()) {
				schedule->set_current_stop(i);
				schedule->remove();
				acnv->set_schedule(schedule);
			}
		}

		if (flags & schedule_entry_t::store)
		{
			// A convoy stored in a depot leaves its line; a convoy visiting only
			// for maintenance or an overhaul keeps its line, as with a scheduled
			// (conditionally skipped) depot entry.
			if (acnv->get_line().is_bound())
			{
				acnv->unset_line();
				acnv->unregister_stops();
			}
			else
			{
				acnv->unregister_stops();
			}

			// There is no need to re-run the path explorer
			// if this convoy is going to the depot routinely
#ifdef MULTI_THREAD
			world()->await_path_explorer();
#endif
			path_explorer_t::refresh_all_categories(false);
		}
	}

	// A new visit starts a new Depot Assembly attempt.
	awaiting_assembly.remove(acnv);
	assembly_reported.remove(acnv);

	vector_tpl<vehicle_t*> vehicles_to_overhaul;
	vector_tpl<vehicle_t*> vehicles_to_maintain;
	vector_tpl<vehicle_t*> unaffordable_overhauls;

	// Clean up the vehicles -- get rid of freight, etc.  Do even when loading, just in case.
	for(unsigned i=0; i<acnv->get_vehicle_count(); i++)
	{
		vehicle_t *v = acnv->get_vehicle(i);
		// Hajo: reset vehicle data. A depot arrival is assumed to be empty, but
		// automatic maintenance visits do not yet enforce that; see
		// ai/ex-15/depot-maintenance.md.
		v->discard_cargo();
		if (flags & schedule_entry_t::store)
		{
			v->set_pos(koord3d::invalid);
			v->set_leading(i == 0);
			v->set_last(i + 1 == acnv->get_vehicle_count());
		}

		if(flags & schedule_entry_t::maintain_or_overhaul)
		{
			classify_vehicle_for_maintenance(v, vehicles_to_overhaul, vehicles_to_maintain, &unaffordable_overhauls);
		}
	}

	// Once one vehicle is due for an overhaul, include the other vehicles in
	// the consist that are close enough to their own overhaul interval. This
	// avoids a train returning to the depot repeatedly to overhaul one vehicle
	// at a time. The threshold is per line; lineless convoys use the global
	// setting.
	if (!vehicles_to_overhaul.empty())
	{
		const uint8 grouping_percentage = acnv->get_line().is_bound()
			? acnv->get_line()->get_overhaul_grouping_percentage()
			: welt->get_settings().get_overhaul_grouping_percentage();

		if (grouping_percentage > 0)
		{
			for (uint32 i = vehicles_to_maintain.get_count(); i-- > 0;)
			{
				vehicle_t *v = vehicles_to_maintain[i];
				if (!v->get_do_not_overhaul() && v->is_overhaul_nearly_due(grouping_percentage) && v->can_afford_overhaul())
				{
					vehicles_to_maintain.remove_at(i);
					vehicles_to_overhaul.append(v);
				}
			}
		}
	}

	const bool hold_for_unaffordable_overhaul = acnv->is_maintenance_urgently_needed() && !unaffordable_overhauls.empty();
	if (hold_for_unaffordable_overhaul)
	{
		// An urgently overdue convoy must enter the depot, but it must not be
		// overhauled on credit. Leave it in the depot until its owner can pay.
		// Move past the scheduled depot entry so that a manual restart leaves
		// the depot instead of routing back into it.
		acnv->advance_past_current_depot_entry();
		acnv->set_state(convoi_t::INITIAL);
		acnv->report_unaffordable_overhaul_held(get_pos().get_2d());
		vehicles_to_overhaul.clear();
		vehicles_to_maintain.clear();
	}

	const replace_data_t* rpl = acnv->get_replace();
	bool replaced = false;
	sint32 upgrade_wait = 0;
	if (!hold_for_unaffordable_overhaul && rpl && rpl->get_replacing_vehicles() && rpl->get_replacing_vehicles()->get_count() > 0 && ((!rpl->get_use_home_depot() || acnv->get_home_depot() == get_pos())))
	{
		// If the home depot is required and this is not it, do not replace.

		// An on_maintenance replacement is triggered by any maintenance visit,
		// including a visit made only for overhauls. An on_overhaul replacement
		// requires at least one vehicle actually due for an overhaul.
		if (unaffordable_overhauls.empty() && (flags & schedule_entry_t::maintain_or_overhaul) &&
			(rpl->get_replace_at() == replace_data_t::on_maintenance ||
			 (rpl->get_replace_at() == replace_data_t::on_overhaul && !vehicles_to_overhaul.empty())))
		{
			acnv->replace_now(&upgrade_wait);
			// replace_now() clears the replacement data only when it actually
			// replaced the convoy; a missing home depot leaves it in place.
			replaced = !acnv->get_replace();
			vehicles_to_overhaul.clear();
			vehicles_to_maintain.clear();
			unaffordable_overhauls.clear();

			// We must repeat this as replacing will probably have invalidated the vectors
			for (unsigned i = 0; i < acnv->get_vehicle_count(); i++)
			{
				classify_vehicle_for_maintenance(acnv->get_vehicle(i), vehicles_to_overhaul, vehicles_to_maintain, &unaffordable_overhauls);
			}
		}
	}

	for (uint32 i = vehicles_to_overhaul.get_count(); i-- != 0;)
	{
		vehicle_t *v = vehicles_to_overhaul[i];
		// Store the duration on the vehicle because the queue is filled after
		// the routine-maintenance group has been released to another Convoy.
		v->set_depot_job_duration(v->get_overhaul_duration());
		if (!v->overhaul(acnv.get_rep()))
		{
			// The owner could no longer afford the overhaul. Leave the
			// vehicle due and move it to the routine-maintenance group.
			vehicles_to_maintain.append(v);
			vehicles_to_overhaul.remove_at(i);
		}
	}

	for (auto veh : vehicles_to_maintain)
	{
		// Routine-maintenance duration uses active_service_time, which
		// maintain() resets.
		veh->set_depot_job_duration(veh->get_maintenance_duration());
		veh->maintain();
	}

	// A Depot Consist Order re-forms the Convoy from the Depot Vehicle Pool.
	// The Lead Vehicle stays in the Original Convoy; every other vehicle goes
	// to the pool with its own maintenance job. A blank order leaves the Convoy
	// together and is handled by the per-vehicle queue only.
	bool has_depot_order = false;
	const consist_order_t *depot_order = NULL;
	if (acnv->get_schedule() != NULL && !acnv->get_schedule()->empty())
	{
		const uint16 entry_id = acnv->get_schedule()->get_current_entry().unique_entry_id;
		if (acnv->get_schedule()->entry_has_consist_order(entry_id))
		{
			const consist_order_t &order = acnv->get_schedule()->get_consist_order(entry_id);
			if (order.get_count() > 0)
			{
				has_depot_order = true;
				depot_order = &order;
			}
		}
	}

	bool decomposed = false;
	if (has_depot_order && depot_order != NULL && !hold_for_unaffordable_overhaul && !replaced)
	{
		vector_tpl<vehicle_t*> forward;
		acnv->get_forward_consist(forward);
		vehicle_t *lead = forward.empty() ? NULL : forward[0];
		if (lead != NULL)
		{
			vector_tpl<vehicle_t*> to_move;
			for (uint8 i = 0; i < acnv->get_vehicle_count(); i++)
			{
				vehicle_t *v = acnv->get_vehicle(i);
				if (v != lead)
				{
					to_move.append(v);
				}
			}
			for (auto v : to_move)
			{
				acnv->remove_vehicle(v);
				v->set_pos(get_pos());
				v->set_leading(false);
				v->set_last(false);
				if (depot_order->is_available_to_any())
				{
					v->clear_loose_recipient();
				}
				else if (depot_order->is_available_to_same())
				{
					if (acnv->get_line().is_bound())
					{
						v->set_loose_recipient(acnv->get_line().get_id(), false, true);
					}
					else
					{
						v->set_loose_recipient(acnv.get_id(), true, true);
					}
				}
				else
				{
					v->set_loose_recipient(depot_order->get_available_to_id(), depot_order->get_available_to_is_convoy());
				}
				vehicles.append(v);
				queue_pool_vehicle_maintenance(v, v->get_depot_job_duration());
			}
			lead->set_leading(true);
			lead->set_last(true);
			decomposed = true;
		}
	}

	if(!vehicles_to_overhaul.empty())
	{
		acnv->set_state(convoi_t::OVERHAUL);
		register_for_maintenance(acnv);
		queue_convoy_maintenance(acnv, vehicles_to_overhaul, vehicles_to_maintain);
	}
	else if (!vehicles_to_maintain.empty())
	{
		acnv->set_state(convoi_t::MAINTENANCE);
		register_for_maintenance(acnv);
		queue_convoy_maintenance(acnv, vehicles_to_overhaul, vehicles_to_maintain);
	}
	else if (decomposed)
	{
		// The Lead Vehicle has no work of its own, but the order still has to
		// be assembled before the Convoy may leave.
		acnv->set_state(convoi_t::MAINTENANCE);
		register_for_maintenance(acnv);
	}

	if (has_depot_order)
	{
		// Hold the Convoy until Depot Assembly can satisfy the order.
		if (!awaiting_assembly.is_contained(acnv))
		{
			awaiting_assembly.append(acnv);
		}
		acnv->set_wait_lock(1);
	}

	if (replaced)
	{
		// Buying replacement stock is much quicker than overhauling it: the
		// stock is bought rather than built, so only the shunting needed to
		// swap the consist is charged. A vehicle that was upgraded in place
		// counts as an overhaul instead and keeps the overhaul duration.
		const sint64 shunting_wait = welt->get_seconds_to_ticks(welt->get_settings().get_shunting_time_seconds());
		add_convoy_extra_wait(acnv, max(shunting_wait, (sint64)upgrade_wait));
	}

	// convoi_t::step() runs the post-arrival state switch in the same call that
	// entered the depot. A positive wait keeps it from advancing the schedule
	// before depot_t::sync_step() can apply the per-vehicle queue.
	if (!vehicles_to_overhaul.empty() || !vehicles_to_maintain.empty())
	{
		const sint64 remaining = convoy_maintenance_remaining(acnv);
		acnv->set_wait_lock(remaining > 0 ? (sint32)min(remaining, (sint64)SINT32_MAX_VALUE) : 0);
	}

	// Maintained convoys are stepped via depot_t::sync_step. convoi_t::sync_step
	// returns SYNC_REMOVE while the convoy is in MAINTENANCE or OVERHAUL, which
	// removes it from the world sync list after the current pass without
	// removing an object from the list being iterated. convoi_t::start re-adds
	// the convoy on departure.

	// this part stores the convoi in the depot
	convois.append(acnv);
	depot_frame_t *depot_frame = dynamic_cast<depot_frame_t *>(win_get_magic( (ptrdiff_t)this ));
	if(depot_frame) {
		depot_frame->action_triggered(NULL,(long int)0);
	}
	if (flags & schedule_entry_t::store)
	{
		acnv->set_home_depot(get_pos());
	}
	DBG_MESSAGE("depot_t::convoi_arrived()", "convoi %d, %p entered depot", acnv.get_id(), acnv.get_rep());
}


void depot_t::show_info()
{
	create_win( new depot_frame_t(this), w_info, (ptrdiff_t)this );
}


vehicle_t* depot_t::buy_vehicle(const vehicle_desc_t* info, uint16 livery_scheme_index)
{
	DBG_DEBUG("depot_t::buy_vehicle()", info->get_name());
	vehicle_t* veh = vehicle_builder_t::build(get_pos(), get_owner(), NULL, info, false, livery_scheme_index);
	DBG_DEBUG("depot_t::buy_vehicle()", "vehiclebauer %p", veh);
	vehicles.append(veh);
	DBG_DEBUG("depot_t::buy_vehicle()", "appended %i vehicle", vehicles.get_count());
	return veh;
}

void depot_t::upgrade_vehicle(convoihandle_t cnv, const vehicle_desc_t* vb)
{
	if(!cnv.is_bound())
	{
		return;
	}

	for(uint16 i = 0; i < cnv->get_vehicle_count(); i ++)
	{
		for(uint8 c = 0; c < cnv->get_vehicle(i)->get_desc()->get_upgrades_count(); c ++)
		{
			if(cnv->get_vehicle(i)->get_desc()->get_upgrades(c) == vb)
			{
				// An upgraded vehicle replaces the old one. A pending depot job
				// belongs to the old vehicle and cannot be transferred.
				remove_vehicle_from_maintenance_queue(cnv->get_vehicle(i));
				vehicle_t* new_veh = vehicle_builder_t::build(get_pos(), get_owner(), NULL, vb, true, cnv->get_livery_scheme_index());
				cnv->upgrade_vehicle(i, new_veh);
				if(cnv->get_vehicle(i)->get_desc()->get_trailer_count() == 1 && cnv->get_vehicle(i)->get_desc()->get_power() != 0)
				{
					// We need to upgrade tenders, too.
					const vehicle_desc_t* new_desc = new_veh->get_desc()->get_trailer(0);
					if (new_desc)
					{
						vehicle_t* new_veh_2 = vehicle_builder_t::build(get_pos(), get_owner(), NULL, new_desc, true);
						cnv->upgrade_vehicle(i + 1, new_veh_2);
						// The above assumes that tenders are free, which they are in Pak128.Britain, the cost being built into the locomotive.
						// The below ought work more accurately, but does not work properly, for some reason.

						/*if(cnv->get_vehicle(i + 1)->get_desc()->get_upgrades_count() >= c && cnv->get_vehicle(i + 1)->get_desc()->get_upgrades_count() > 0)
						{
							cnv->get_vehicle(i + 1)->set_desc(cnv->get_vehicle(i + 1)->get_desc()->get_upgrades(c));
						}
						else if(cnv->get_vehicle(i + 1)->get_desc()->get_upgrades_count() > 0)
						{
							cnv->get_vehicle(i + 1)->set_desc(cnv->get_vehicle(i + 1)->get_desc()->get_upgrades(0));
						}*/
					}
				}
				//Check whether this is a Garrett type vehicle (this is code for the exceptional case where a Garrett is upgraded to another Garrett)
				if(cnv->get_vehicle(0)->get_desc()->get_power() == 0 && cnv->get_vehicle(0)->get_desc()->get_total_capacity() == 0)
				{
					// Possible Garrett
					const uint8 count = cnv->get_vehicle(0)->get_desc()->get_trailer_count();
					if(count > 0 && cnv->get_vehicle(1)->get_desc()->get_power() > 0 && cnv->get_vehicle(1)->get_desc()->get_trailer_count() > 0)
					{
						// Garrett detected - need to upgrade all three vehicles.
						vehicle_t* new_veh_2 = vehicle_builder_t::build(get_pos(), get_owner(), NULL, new_veh->get_desc()->get_trailer(0), true);
						cnv->upgrade_vehicle(i + 1, new_veh_2);
						vehicle_t* new_veh_3 = vehicle_builder_t::build(get_pos(), get_owner(), NULL, new_veh->get_desc()->get_trailer(0), true);
						cnv->upgrade_vehicle(i + 2, new_veh_3);
					}
				}
				// Only upgrade one at a time.
				// Could add UI feature in future to allow upgrading all at once.
				return;
			}
		}
	}
}


void depot_t::append_vehicle(convoihandle_t &cnv, vehicle_t* veh, bool infront, bool local_execution)
{
	/* create  a new convoi, if necessary */
	if (!cnv.is_bound()) {
		cnv = add_convoi( local_execution );
	}
	veh->set_pos(get_pos());
	cnv->add_vehicle(veh, infront ? 0 : cnv->get_vehicle_count());
	vehicles.remove(veh);
}


void depot_t::remove_vehicle(convoihandle_t cnv, int ipos)
{
	vehicle_t* veh = cnv->remove_vehicle_at( ipos );
	if(  veh  ) {
		vehicles.append( veh );
	}
}


void depot_t::remove_vehicles_to_end(convoihandle_t cnv, int ipos)
{
	while(  vehicle_t* veh = cnv->remove_vehicle_at( ipos )  ) {
		vehicles.append( veh );
	}
}


void depot_t::sell_vehicle(vehicle_t* veh)
{
	remove_vehicle_from_maintenance_queue(veh);
	vehicles.remove(veh);
	sint64 cost = veh->calc_sale_value();
	get_owner()->book_new_vehicle(cost, get_pos().get_2d(), get_waytype() );
	get_owner()->book_vehicle_number( -1, get_waytype() );
	DBG_MESSAGE("depot_t::sell_vehicle()", "this=%p sells %p", this, veh);
	veh->before_delete();
	delete veh;
}


// returns the indest of the old/newest vehicle in a list
//@author: isidoro
vehicle_t* depot_t::find_oldest_newest(const vehicle_desc_t* desc, bool old, vector_tpl<vehicle_t*> *avoid)
{
	vehicle_t* found_veh = NULL;
	for (auto const veh : vehicles)
	{
		if(veh != NULL && veh->get_desc() == desc)
		{
			// joy of XOR, finally a line where I could use it!
			if(avoid == NULL || (!avoid->is_contained(veh) && (found_veh == NULL ||
					old ^ (found_veh->get_purchase_time() > veh->get_purchase_time())))) // Used when replacing to avoid specifying the same vehicle twice
			{
				found_veh = veh;
			}
		}
	}
	return found_veh;
}


convoihandle_t depot_t::add_convoi(bool local_execution)
{
	convoi_t* new_cnv = new convoi_t(get_owner());
	new_cnv->set_home_depot(get_pos());
	convois.append(new_cnv->self);
	depot_frame_t *win = dynamic_cast<depot_frame_t *>(win_get_magic( (ptrdiff_t)this ));
	if(  win  &&  local_execution  ) {
		win->activate_convoi( new_cnv->self );
	}
	return new_cnv->self;
}


bool depot_t::check_obsolete_inventory(convoihandle_t cnv)
{
	bool ok = true;
	slist_tpl<vehicle_t*> veh_tmp_list;

	for(  int i = 0;  i < cnv->get_vehicle_count();  i++  ) {
		const vehicle_desc_t* const vb = cnv->get_vehicle(i)->get_desc();
		if(  vb  ) {
			// search storage for matching vehicle
			vehicle_t* veh = NULL;
			for(  slist_tpl<vehicle_t*>::iterator i = vehicles.begin();  i != vehicles.end();  ++i  ) {
				if(  (*i)->get_desc() == vb  ) {
					// found in storage, remove to temp list while searching for next vehicle
					veh = *i;
					vehicles.erase(i);
					veh_tmp_list.append( veh );
					break;
				}
			}
			if(  !veh  ) {
				// need to buy new
				if(  vb->is_retired( welt->get_timeline_year_month() )  ) {
					// is obsolete, return false
					ok = false;
					break;
				}
			}
		}
	}

	// put vehicles back into storage
	vehicles.append_list( veh_tmp_list );

	return ok;
}


convoihandle_t depot_t::copy_convoi(convoihandle_t old_cnv, bool local_execution)
{
	if(  old_cnv.is_bound()  &&  !convoihandle_t::is_exhausted()  &&
		old_cnv->get_vehicle_count() > 0  &&  get_waytype() == old_cnv->front()->get_desc()->get_waytype() )
	{
		convoihandle_t new_cnv = add_convoi( false );
		new_cnv->set_name(old_cnv->get_internal_name());
		const uint16 convoy_livery_index = old_cnv->get_livery_scheme_index();
		new_cnv->set_livery_scheme_index(convoy_livery_index);
		int vehicle_count = old_cnv->get_vehicle_count();
		for (int i = 0; i < vehicle_count; i++)
		{
			const vehicle_desc_t * info = old_cnv->get_vehicle(i)->get_desc();
			if (info != NULL)
			{
				// search in depot for an existing vehicle of correct type
				vehicle_t* new_vehicle = get_oldest_vehicle(info);
				if(new_vehicle == NULL)
				{
					// no vehicle of correct type in depot, must buy it:
					//first test affordability.
					sint64 total_price = info->get_value();
					if(!get_owner()->can_afford(total_price))
					{
						create_win( new news_img(NOTICE_INSUFFICIENT_FUNDS), w_time_delete, magic_none);
						if(!new_cnv.is_bound())
						{
							return new_cnv;
						}

						if(new_cnv->get_vehicle_count() == 0)
						{
							disassemble_convoi(new_cnv, true);
							return convoihandle_t();
						}

						break; // ... and what happens with the first few vehicles, if you: return convoihandle_t();

					}
					// buy new vehicle
					uint16 vehicle_livery_index = convoy_livery_index;
					if (info->get_livery_count() > 1) {
						// check livery
						vector_tpl<livery_scheme_t*>* schemes = world()->get_settings().get_livery_schemes();
						livery_scheme_t* convoy_scheme = world()->get_settings().get_livery_scheme(convoy_livery_index);
						if (convoy_scheme) {
							if (convoy_scheme->is_contained(old_cnv->get_vehicle(i)->get_current_livery())) {
								// Copy the livery of the old vehicle as it is
								// If it is old it will be replaced with new one
							}
							else {
								const char *livery_name = old_cnv->get_vehicle(i)->get_current_livery();
								// The player intentionally set a different livery scheme for this vehicle
								// =>should copy it if it is available
								for (uint32 j = 0; j < schemes->get_count(); j++) {
									livery_scheme_t* scheme = schemes->get_element(j);
									if (scheme->is_contained(livery_name)) {
										// This livery scheme has vehicle's livery => Check if this is available now
										if (scheme->is_available(welt->get_timeline_year_month())) {
											if (!strcmp(livery_name, scheme->get_latest_available_livery(welt->get_timeline_year_month(), info))) {
												// This is the latest livery so it can be copied
												vehicle_livery_index = j;
												break;
											}
										}
									}
								}
							}
						}
					}
					new_vehicle = vehicle_builder_t::build(get_pos(), get_owner(), NULL, info, false, vehicle_livery_index);
				}

				// Reassign the classes:
				uint8 classes_to_check = info->get_number_of_classes();
				for (int j = 0; j < classes_to_check; j++)
				{
					if (old_cnv->get_vehicle(i)->get_reassigned_class(j) != j)
					{
						new_vehicle->set_class_reassignment(j, old_cnv->get_vehicle(i)->get_reassigned_class(j));
					}
				}
				// append new vehicle
				append_vehicle(new_cnv, new_vehicle, false, local_execution);
			}
		}
		if (old_cnv->get_line().is_bound())
		{
			if(!new_cnv.is_bound())
			{
				new_cnv = add_convoi(local_execution);
			}
			new_cnv->set_line(old_cnv->get_line());
			new_cnv->get_schedule()->set_current_stop( old_cnv->get_schedule()->get_current_stop() );
		}
		else
		{
			if (old_cnv->get_schedule() != NULL)
			{
				if(!new_cnv.is_bound())
				{
					new_cnv = add_convoi(local_execution);
				}
				new_cnv->set_schedule(old_cnv->get_schedule()->copy());
			}
		}
		if (new_cnv.is_bound())
		{
			new_cnv->set_name(old_cnv->get_internal_name());

			// make this the current selected convoi
			depot_frame_t *win = dynamic_cast<depot_frame_t *>(win_get_magic( (ptrdiff_t)this ));
			if(  win  ) {
				if(  local_execution  ) {
					win->activate_convoi( new_cnv );
				}
				else {
					win->update_data();
				}
			}
		}

		return new_cnv;
	}
	return convoihandle_t();
}


bool depot_t::disassemble_convoi(convoihandle_t cnv, bool sell)
{
	if(  cnv.is_bound()  ) {
		if(  !sell  ) {
			// store vehicles in depot
			while(  vehicle_t* const v = cnv->remove_vehicle_at(0)  ) {
				v->discard_cargo();
				v->set_leading(false);
				v->set_last(false);
				vehicles.append(v);
			}
		}

		// remove from depot lists
		remove_convoi( cnv );

		// and remove from welt
		cnv->self_destruct();
		return true;
	}
	return false;
}


bool depot_t::start_all_convoys()
{
	uint32 i = 0;
	while(  i < convois.get_count()  ) {
		if(  !start_convoi( convois.at(i), false )  ) {
			i++;
		}
	}
	return (convois.get_count() == 0);
}


bool depot_t::start_convoi(convoihandle_t cnv, bool local_execution)
{
	// close schedule window if not yet closed
	if(cnv.is_bound() &&  cnv->get_schedule()!=NULL) {
		if(!cnv->get_schedule()->is_editing_finished()) {
			// close the schedule window
			destroy_win((ptrdiff_t)cnv->get_schedule());
		}
	}

	if(cnv.is_bound()) {
		// Only a convoy that is stored or is about to leave can be started. Starting one in another state (e.g.
		// under maintenance) would do nothing in convoi_t::start but still take it off the depot's lists.
		const int state = cnv->get_state();
		if(state != convoi_t::INITIAL && state != convoi_t::ROUTING_1 && state != convoi_t::ROUTE_JUST_FOUND) {
			return false;
		}
	}

	// convoi not in depot anymore, maybe user double-clicked on start-button
	if(/*local_execution &&*/ !convois.is_contained(cnv)) { // The local_execution condition caused timebase issues and excessive speeds of vehicles
		return false;
	}

	if (cnv.is_bound() && cnv->front()->get_owner() == NULL)
	{
		// It it not clear why the version for this on loading does not work.
		cnv->front()->set_owner(cnv->get_owner());
		cnv->back()->set_owner(cnv->get_owner());
	}
	if (cnv.is_bound() && cnv->get_schedule() && !cnv->get_schedule()->empty())
	{
		// if next schedule entry is this depot => advance to next entry
		koord3d cur_pos = cnv->get_schedule()->get_current_entry().pos;
		if (cur_pos == get_pos())
		{
			cnv->get_schedule()->advance();
			cur_pos = cnv->get_schedule()->get_current_entry().pos;
		}

		bool convoy_unpowered = cnv->is_unpowered();

		if(convoy_unpowered)
		{
			// HACK: Not sure what is causing the basic problem with cnv->get_sum_power() reporting 0 with some very large aircraft (currently only 747s).

			bool power = false;
			bool speed = false;
			vector_tpl<const vehicle_desc_t*> vehicle_types;
			const uint8 number_of_vehicles = cnv->get_vehicle_count();
			for(uint8 i = 0; i < number_of_vehicles; i++)
			{
				if(cnv->get_vehicle(i)->get_desc()->get_power())
				{
					power = true;
				}
				vehicle_types.append((cnv->get_vehicle(i)->get_desc()));
			}
			if(power)
			{
				potential_convoy_t convoy(vehicle_types);
				const vehicle_summary_t &vsum = convoy.get_vehicle_summary();
				const sint32 friction = convoy.get_current_friction();
				//const double rolling_resistance = convoy.get_resistance_summary().to_double();
				//const uint32 number_of_vehicles = vehicle_types.get_count();
				const uint32 max_speed = convoy.calc_max_speed(weight_summary_t(vsum.weight, friction));
				speed = max_speed;
			}

			convoy_unpowered = !(power && speed);
		}

		// calc_route no longer releases target reservations itself (reservation
		// state is main-thread only, released ahead of route finding): do it here.
		cnv->front()->release_target_reservations();

		// check if convoy is complete
		if(convoy_unpowered || !cnv->check_validity())
		{
			if (local_execution)
			{
				create_win( new news_img("Diese Zusammenstellung kann nicht fahren!\n"), w_time_delete, magic_none);
			}
		}
		else if(  !cnv->front()->calc_route(this->get_pos(), cur_pos, cnv->get_min_top_speed(), cnv->has_tall_vehicles(), cnv->access_route())) {
			// no route to go ...
			if(local_execution) {
				static cbuffer_t buf;
				buf.clear();
				buf.printf( translator::translate("Vehicle %s can't find a route!"), cnv->get_name() );
				create_win( new news_img(buf), w_time_delete, magic_none);
			}
		}
		else if (!(cnv->front()->get_desc()->get_basic_constraint_prev(cnv->front()->is_reversed()) & vehicle_desc_t::can_be_head)) {
			// Is there a cab at the front end of convoy?
			create_win(new news_img("Cannot start: no cab at the front of the convoy."), w_time_delete, magic_none);
		}
		else
		{
			// convoi can start now
			cnv->start(this);
			clear_convoy_maintenance_jobs(cnv);

			// remove from depot lists
			remove_convoi( cnv );
			return true;
		}
	}
	else {
		if (local_execution) {
			create_win( new news_img("Noch kein Fahrzeug\nmit Fahrplan\nvorhanden\n"), w_time_delete, magic_none);
		}

		if (!cnv.is_bound()) {
			dbg->warning("depot_t::start_convoi()","No convoi to start!");
		} else if (!cnv->get_schedule()) {
			dbg->warning("depot_t::start_convoi()","No schedule for convoi.");
		} else if (!cnv->get_schedule()->is_editing_finished()) {
			dbg->warning("depot_t::start_convoi()","Schedule is incomplete/not finished");
		}
	}
	return false;
}


void depot_t::remove_convoi( convoihandle_t cnv )
{
	depot_frame_t *win = dynamic_cast<depot_frame_t *>(win_get_magic( (ptrdiff_t)this ));
	if(  win  ) {
		// get currently selected convoi to restore selection if not removed
		int icnv = win->get_icnv();
		convoihandle_t c = icnv > -1 ? get_convoi( icnv ) : convoihandle_t();

		icnv = convois.index_of( cnv );
		convois.remove( cnv );

		if(  c == cnv  ) {
			// removing currently selected, select next in list or last instead
			c = !convois.empty() ? convois.at( min((uint32)icnv, convois.get_count() - 1) ) : convoihandle_t();
		}
		win->activate_convoi( c );
	}
	else {
		convois.remove( cnv );
	}

	under_maintenance.remove(cnv);
}


// attention! this will not be used for railway depots when loading! They will be loaded by hand ...
void depot_t::rdwr(loadsave_t *file)
{
	gebaeude_t::rdwr(file);

	rdwr_vehicle(vehicles, file);
	if (file->is_version_less(81, 33)) {
		// wagons are stored extra, just add them to vehicles
		assert(file->is_loading());
		rdwr_vehicle(vehicles, file);
	}

	rdwr_maintenance_queue(file);

	if (file->is_version_ex_atleast(15, 24))
	{
		file->rdwr_long(next_maintenance_queue_index);
	}
	else if (file->is_loading())
	{
		next_maintenance_queue_index = 0;
	}
}

void depot_t::rdwr_maintenance_queue(loadsave_t* file)
{
	if (file->is_version_ex_atleast(15, 0))
	{
		// Load/save the queue of maintained vehicles in order
		if (file->is_saving())
		{
			uint32 under_maintenance_count = under_maintenance.get_count();
			file->rdwr_long(under_maintenance_count);

			for (uint32 i = 0; i < under_maintenance_count; i++)
			{
				uint16 cnv_id = under_maintenance[i].get_id();
				file->rdwr_short(cnv_id);
			}
		}
		else if (file->is_loading())
		{
			uint32 under_maintenance_count;
			file->rdwr_long(under_maintenance_count);

			convoihandle_t cnv = convoihandle_t();
			for (uint32 i = 0; i < under_maintenance_count; i++)
			{
				uint16 cnv_id;
				file->rdwr_short(cnv_id);
				cnv.set_id(cnv_id);
				under_maintenance.append(cnv);
			}
		}

	}
}


void depot_t::validate_maintenance_queue()
{
	for (uint32 i = under_maintenance.get_count(); i-- != 0;)
	{
		convoihandle_t c = under_maintenance[i];
		const bool keep = c.is_bound() && c->get_vehicle_count() > 0
			&& (c->get_state() == convoi_t::MAINTENANCE || c->get_state() == convoi_t::OVERHAUL)
			&& c->front()->get_pos() == get_pos();
		if (!keep)
		{
			under_maintenance.remove_at(i);
		}
	}
	for (auto const c : convois)
	{
		if (c.is_bound() && c->get_vehicle_count() > 0
			&& (c->get_state() == convoi_t::MAINTENANCE || c->get_state() == convoi_t::OVERHAUL)
			&& c->front()->get_pos() == get_pos()
			&& !under_maintenance.is_contained(c))
		{
			under_maintenance.append(c);
		}
	}
	rebuild_maintenance_queue();
}


void depot_t::rdwr_vehicle(slist_tpl<vehicle_t *> &list, loadsave_t *file)
{
// read/write vehicles in the depot, which are not part of a convoi.

	sint32 count;

	if(file->is_saving()) {
		count = list.get_count();
		DBG_MESSAGE("depot_t::vehicle_laden()","saving %d vehicles",count);
	}
	file->rdwr_long(count);

	if(file->is_loading()) {

		// no house definition for this => use a normal hut ...
		if(  this->get_tile()==NULL  ) {
			dbg->error( "depot_t::rdwr()", "tile for depot not found!" );
			set_tile( (*hausbauer_t::get_citybuilding_list( building_desc_t::city_res ))[0]->get_tile(0), true );
		}

		DBG_MESSAGE("depot_t::vehicle_laden()","loading %d vehicles",count);
		for (int i = 0; i < count; i++) {
			obj_t::typ typ = (obj_t::typ)file->rd_obj_id();

			vehicle_t* v = NULL;
			const bool first = false;
			const bool last = false;

			switch (typ) {
			case old_road_vehicle:
			case road_vehicle: v = new road_vehicle_t(file, first, last);    break;
			case old_waggon:
			case rail_vehicle:    v = new rail_vehicle_t(file, first, last);       break;
			case old_schiff:
			case water_vehicle:    v = new water_vehicle_t(file, first, last);       break;
			case old_aircraft:
			case air_vehicle: v = new air_vehicle_t(file, first, last);  break;
			case old_monorail_vehicle:
			case monorail_vehicle: v = new monorail_rail_vehicle_t(file, first, last);  break;
			case maglev_vehicle:   v = new maglev_rail_vehicle_t(file, first, last);  break;
			case narrowgauge_vehicle: v = new narrowgauge_rail_vehicle_t(file, first, last);  break;
			default:
				dbg->fatal("depot_t::vehicle_laden()", "invalid vehicle type $%X", typ);
			}
			const vehicle_desc_t* desc = v->get_desc();
			if (desc) {
				DBG_MESSAGE("depot_t::vehicle_laden()", "loaded %s", desc->get_name());
				list.insert(v);
			}
			else {
				dbg->error("depot_t::vehicle_laden()", "vehicle has no desc => ignored");
			}

			if (v->get_owner() == NULL)
			{
				// Vehicles should not be unowned in depots; assume that the owner of the depot owns the vehicle.
				v->set_owner(get_owner());
			}
		}
	}
	else
	{
		for (auto const v : list)
		{
			file->wr_obj_id(v->get_typ());
			v->rdwr_from_convoi(file);
		}
	}

	if (file->is_version_ex_atleast(14, 56))
	{
		file->rdwr_str(name, lengthof(name));
	}
	else
	{
		strcpy(name, "unnamed");
	}
}

void depot_t::set_name(const char* value)
{
	if (sizeof(*value) > sizeof(name))
	{
		dbg->error("void depot_t::set_name(char* value)", "Name too long");
	}
	strcpy(name, value);

	depot_frame_t *win = dynamic_cast<depot_frame_t *>(win_get_magic((ptrdiff_t)this));
	if (win) {
		win->reset_depot_name();
	}
}

const char* depot_t::get_name() const
{
	if (strcmp(name, "unnamed"))
	{
		return name;
	}
	else
	{
		return translator::translate(gebaeude_t::get_name());
	}
}

/**
 * @return NULL when OK, otherwise an error message
 */
const char * depot_t::is_deletable(const player_t *player)
{
	if(player!=get_owner()  &&  player!=welt->get_public_player()) {
		return NOTICE_OWNED_BY_OTHER_PLAYER;
	}
	if (!vehicles.empty()) {
		return "There are still vehicles\nstored in this depot!\n";
	}

	for (auto const c : convois)
	{
		if (c.is_bound() && c->get_vehicle_count() > 0) {
			return "There are still vehicles\nstored in this depot!\n";
		}
	}

	for (auto const c : under_maintenance)
	{
		if (c.is_bound() && c->get_vehicle_count() > 0)
		{
			return "There are still vehicles\nstored in this depot!\n";
		}
	}
	return NULL;
}


vehicle_t* depot_t::get_oldest_vehicle(const vehicle_desc_t* desc)
{
	vehicle_t* oldest_veh = NULL;
	for(auto const veh : get_vehicle_list())
	{
		if (veh->get_desc() == desc) {
			if (oldest_veh == NULL ||
					oldest_veh->get_purchase_time() > veh->get_purchase_time()) {
				oldest_veh = veh;
			}
		}
	}
	return oldest_veh;
}


// true if already stored here
bool depot_t::is_contained(const vehicle_desc_t *info)
{
	for (auto const v :get_vehicle_list())
	{
		if (v->get_desc() == info) {
			return true;
		}
	}
	return false;
}

void depot_t::update_win()
{
	depot_frame_t *depot_frame = dynamic_cast<depot_frame_t *>(win_get_magic( (ptrdiff_t)this ));
	if(depot_frame) {
		depot_frame->set_convoy();
	}
}

/**
 * The player must pay monthly fixed maintenance costs for the vehicles in the depot.
 * This method is called by the world (karte_t) once per month.
 * @author Bernd Gabriel
 * @date 27.06.2009
 */
void depot_t::new_month()
{
	sint64 fixed_cost_costs = 0;
	if (vehicle_count() > 0)
	{
		for(auto const v : get_vehicle_list())
		{
			if (v->get_is_mothballed())
			{
				continue;
			}
			fixed_cost_costs += v->get_desc()->get_fixed_cost(welt);
		}
	}
	if (fixed_cost_costs)
	{
		get_owner()->book_vehicle_maintenance( -fixed_cost_costs, get_waytype() );
	}

	const planquadrat_t* tile = welt->access(get_pos().get_2d());
	stadt_t* city = tile ? tile->get_city() : NULL;
	if(city && get_stadt() == NULL)
	{
		// The depot has joined a city by dint of growth.
		set_stadt(city);
		city->update_city_stats_with_building(this, false);
	}
	// Cannot check for city shrinkage without risking
	// a crash when cities are deleted.

	// since vehicles may have become obsolete
	update_all_win();
}

void depot_t::update_all_win()
{
	for (auto const d : all_depots)
	{
		d->update_win();
	}
}

uint16 depot_t::get_traction_types() const
{
	return this->get_tile()->get_desc()->get_enabled();
}

/**
 * Is this depot suitable for this vehicle?
 * Must be same waytype, same owner, suitable traction, etc.
 * @param test_vehicle -- must not be NULL
 * @param traction_types
 *   - 0 if we don't want to filter by traction type
 *   - a bitmask of possible traction types; we need only match one
 */
bool depot_t::is_suitable_for( const vehicle_t * test_vehicle, const uint16 traction_types /* = 0 */ ) const
{
	assert(test_vehicle != NULL);

	// Owner must be the same
	if (  this->get_owner() != test_vehicle->get_owner()  ) {
		return false;
	}

	// Right type of vehicle?  No trams in train depots, etc...
	const waytype_t my_waytype = this->get_wegtyp();
	// Subtle point here: the vehicle waytype is 'train' for trams,
	// but the vehicle desc waytype is tram.
	// Change this if we want to allow trams into train depots and vice versa.
	const waytype_t vehicle_waytype = test_vehicle->get_desc()->get_waytype();
	if (  vehicle_waytype != my_waytype  ) {
		 return false;
	}

	if (traction_types != 0 ) {
		// If traction types were specified, then *one* of them must match
		// *one* of the types supported by this depot
		if ( ! (traction_types & get_traction_types()) ) {
			return false;
		}
	}
	// Passed all the tests
	return true;
}

void depot_t::add_to_world_list(bool)
{
	welt->add_building_to_world_list(this);
	const planquadrat_t* tile = welt->access(get_pos().get_2d());
	stadt_t* city = tile ? tile->get_city() : NULL;
	if(city)
	{
		set_stadt(city);
		city->update_city_stats_with_building(this, false);
	}
}

void depot_t::register_for_maintenance(convoihandle_t cnv)
{
	under_maintenance.append(cnv);
}

void depot_t::prioritise_for_maintenance(convoihandle_t cnv)
{
	if (!under_maintenance.remove(cnv))
	{
		return;
	}
	under_maintenance.insert_at(0, cnv);
}

void depot_t::depriortise_for_maintenance(convoihandle_t cnv)
{
	if (!under_maintenance.remove(cnv))
	{
		return;
	}
	under_maintenance.append(cnv);
}

uint8 depot_t::count_in_progress_depot_jobs() const
{
	uint8 count = 0;
	for (auto const v : maintenance_queue)
	{
		if (v && v->get_depot_job_state() == vehicle_t::depot_job_in_progress)
		{
			count++;
		}
	}
	return count;
}

void depot_t::reproject_depot_jobs()
{
	const sint64 now = welt->get_ticks();
	uint16 capacity = get_tile()->get_desc()->get_max_vehicles_under_maintenance();
	if (capacity < 1)
	{
		capacity = 1;
	}

	// One free time per maintenance place. An in-progress job occupies one
	// place until its release time; a waiting job is projected onto the place
	// that becomes free first. This is a projection for display and for the
	// Convoy wait; the actual start time is set when the job starts.
	vector_tpl<sint64> server_free;
	for (uint16 i = 0; i < capacity; i++)
	{
		server_free.append(now);
	}

	for (auto v : maintenance_queue)
	{
		if (!v)
		{
			continue;
		}
		if (v->get_depot_job_state() == vehicle_t::depot_job_in_progress)
		{
			uint16 best = 0;
			for (uint16 i = 1; i < capacity; i++)
			{
				if (server_free[i] < server_free[best])
				{
					best = i;
				}
			}
			server_free[best] = max(server_free[best], v->get_depot_release_time());
		}
		else if (v->get_depot_job_state() == vehicle_t::depot_job_waiting)
		{
			uint16 best = 0;
			for (uint16 i = 1; i < capacity; i++)
			{
				if (server_free[i] < server_free[best])
				{
					best = i;
				}
			}
			const sint64 start = max(now, server_free[best]);
			const sint64 release = start + v->get_depot_job_duration();
			v->set_depot_release_time(release);
			server_free[best] = release;
		}
	}
}

void depot_t::start_depot_jobs()
{
	uint16 capacity = get_tile()->get_desc()->get_max_vehicles_under_maintenance();
	if (capacity < 1)
	{
		capacity = 1;
	}
	uint16 in_progress = count_in_progress_depot_jobs();
	if (in_progress >= capacity)
	{
		return;
	}

	const sint64 now = welt->get_ticks();
	bool changed = false;
	for (auto v : maintenance_queue)
	{
		if (!v || v->get_depot_job_state() != vehicle_t::depot_job_waiting)
		{
			continue;
		}
		if (in_progress >= capacity)
		{
			break;
		}
		v->start_depot_job(now + v->get_depot_job_duration());
		in_progress++;
		changed = true;
	}

	if (changed)
	{
		reproject_depot_jobs();
	}
}

void depot_t::complete_depot_jobs()
{
	const sint64 now = welt->get_ticks();
	bool changed = false;
	for (uint32 i = 0; i < maintenance_queue.get_count();)
	{
		vehicle_t *v = maintenance_queue[i];
		if (!v)
		{
			maintenance_queue.remove_at(i);
			changed = true;
			continue;
		}
		if (v->get_depot_job_state() == vehicle_t::depot_job_in_progress && v->get_depot_release_time() <= now)
		{
			v->complete_depot_job();
			changed = true;
			if (!v->get_convoi())
			{
				// A loose pool vehicle becomes an Available Vehicle as soon as
				// its work is complete. It has no Convoy to wait for.
				maintenance_queue.remove_at(i);
				v->clear_depot_job();
				continue;
			}
		}
		i++;
	}

	if (changed)
	{
		reproject_depot_jobs();
	}
}

void depot_t::refresh_depot_queue()
{
	reproject_depot_jobs();
	start_depot_jobs();
}

void depot_t::queue_convoy_maintenance(convoihandle_t cnv, const vector_tpl<vehicle_t*> &overhauled, const vector_tpl<vehicle_t*> &maintained)
{
	if (!cnv.is_bound())
	{
		return;
	}

	// Queue in the Convoy's consist order, not in the classification order, so
	// that the queue is the same on every peer and after a load.
	bool queued = false;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicle_t *v = cnv->get_vehicle(i);
		if (!v->has_depot_job() && (overhauled.is_contained(v) || maintained.is_contained(v)))
		{
			v->set_depot_job(vehicle_t::depot_job_waiting, next_maintenance_queue_index++, v->get_depot_job_duration());
			maintenance_queue.append(v);
			queued = true;
		}
	}

	if (queued)
	{
		refresh_depot_queue();
	}
}

void depot_t::queue_pool_vehicle_maintenance(vehicle_t *v, sint64 duration)
{
	if (!v || v->has_depot_job())
	{
		return;
	}
	v->set_depot_job(vehicle_t::depot_job_waiting, next_maintenance_queue_index++, duration);
	maintenance_queue.append(v);
	refresh_depot_queue();
}

void depot_t::remove_vehicle_from_maintenance_queue(vehicle_t *v)
{
	if (!v)
	{
		return;
	}
	maintenance_queue.remove(v);
	v->clear_depot_job();
	refresh_depot_queue();
}

void depot_t::clear_convoy_maintenance_jobs(convoihandle_t cnv)
{
	if (!cnv.is_bound())
	{
		return;
	}
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicle_t *v = cnv->get_vehicle(i);
		maintenance_queue.remove(v);
		v->clear_depot_job();
	}
	refresh_depot_queue();
}

void depot_t::add_convoy_extra_wait(convoihandle_t cnv, sint64 extra)
{
	if (!cnv.is_bound() || extra <= 0)
	{
		return;
	}
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		vehicle_t *v = cnv->get_vehicle(i);
		if (v->has_depot_job() && !v->is_depot_job_complete())
		{
			v->set_depot_job_duration(v->get_depot_job_duration() + extra);
			refresh_depot_queue();
			return;
		}
	}
}

bool depot_t::convoy_maintenance_complete(convoihandle_t cnv) const
{
	if (!cnv.is_bound())
	{
		return true;
	}
	if (awaiting_assembly.is_contained(cnv))
	{
		return false;
	}
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		const vehicle_t *v = cnv->get_vehicle(i);
		if (v->has_depot_job() && !v->is_depot_job_complete())
		{
			return false;
		}
	}
	return true;
}

sint64 depot_t::convoy_maintenance_remaining(convoihandle_t cnv) const
{
	if (!cnv.is_bound())
	{
		return 0;
	}
	if (awaiting_assembly.is_contained(cnv))
	{
		return 1;
	}
	const sint64 now = welt->get_ticks();
	sint64 remaining = 0;
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		const vehicle_t *v = cnv->get_vehicle(i);
		if (v->is_depot_job_complete())
		{
			continue;
		}
		if (v->get_depot_job_state() == vehicle_t::depot_job_in_progress)
		{
			if (v->get_depot_release_time() > now)
			{
				remaining = max(remaining, v->get_depot_release_time() - now);
			}
		}
		else if (v->get_depot_job_state() == vehicle_t::depot_job_waiting)
		{
			const sint64 release = v->get_depot_release_time();
			remaining = max(remaining, release > now ? release - now : v->get_depot_job_duration());
		}
	}
	return remaining;
}

void depot_t::rebuild_maintenance_queue()
{
	maintenance_queue.clear();

	uint32 max_index = 0;
	const auto add_vehicle = [&](vehicle_t *v) {
		if (v && v->has_depot_job())
		{
			maintenance_queue.append(v);
			max_index = max(max_index, v->get_depot_queue_index() + 1);
			if (v->get_depot_job_state() == vehicle_t::depot_job_in_progress && v->get_depot_release_time() == 0)
			{
				v->set_depot_release_time(welt->get_ticks() + v->get_depot_job_duration());
			}
		}
	};

	for (auto const v : vehicles)
	{
		add_vehicle(v);
	}
	for (auto const c : convois)
	{
		if (c.is_bound())
		{
			for (uint8 i = 0; i < c->get_vehicle_count(); i++)
			{
				add_vehicle(c->get_vehicle(i));
			}
		}
	}

	// Insertion sort by queue index. The queue is small and this avoids
	// depending on the saved list order.
	for (uint32 i = 1; i < maintenance_queue.get_count(); i++)
	{
		vehicle_t *v = maintenance_queue[i];
		uint32 j = i;
		while (j > 0 && maintenance_queue[j - 1]->get_depot_queue_index() > v->get_depot_queue_index())
		{
			maintenance_queue[j] = maintenance_queue[j - 1];
			j--;
		}
		maintenance_queue[j] = v;
	}

	if (next_maintenance_queue_index < max_index)
	{
		next_maintenance_queue_index = max_index;
	}
	refresh_depot_queue();

	// A Convoy held for an unsatisfied order must resume its assembly attempt
	// after a load. The order is stored in the schedule; the Lead Vehicle's job
	// state tells whether its own work is done.
	awaiting_assembly.clear();
	assembly_reported.clear();
	for (auto const c : under_maintenance)
	{
		if (!c.is_bound() || !c->get_schedule() || c->get_schedule()->empty())
		{
			continue;
		}
		const schedule_entry_t &entry = c->get_schedule()->get_current_entry();
		if (entry.pos != get_pos() || !c->get_schedule()->entry_has_consist_order(entry.unique_entry_id))
		{
			continue;
		}
		const consist_order_t &order = c->get_schedule()->get_consist_order(entry.unique_entry_id);
		if (order.get_count() == 0)
		{
			continue;
		}
		vector_tpl<vehicle_t*> forward;
		c->get_forward_consist(forward);
		if (!forward.empty() && (!forward[0]->has_depot_job() || forward[0]->is_depot_job_complete()))
		{
			awaiting_assembly.append(c);
		}
	}
}

void depot_t::try_assemble_depot_orders()
{
	for (uint32 i = 0; i < under_maintenance.get_count(); i++)
	{
		const convoihandle_t cnv = under_maintenance[i];
		if (!cnv.is_bound() || !cnv->get_schedule() || cnv->get_schedule()->empty())
		{
			continue;
		}
		const schedule_entry_t &entry = cnv->get_schedule()->get_current_entry();
		if (entry.pos != get_pos() || !cnv->get_schedule()->entry_has_consist_order(entry.unique_entry_id))
		{
			continue;
		}
		const consist_order_t &order = cnv->get_schedule()->get_consist_order(entry.unique_entry_id);
		if (order.get_count() == 0)
		{
			continue;
		}
		vector_tpl<vehicle_t*> forward;
		cnv->get_forward_consist(forward);
		if (forward.empty())
		{
			continue;
		}
		vehicle_t *lead = forward[0];
		if (lead->has_depot_job() && !lead->is_depot_job_complete())
		{
			continue;
		}
		if (!awaiting_assembly.is_contained(cnv))
		{
			awaiting_assembly.append(cnv);
		}
		if (try_assemble_convoy(cnv))
		{
			awaiting_assembly.remove(cnv);
			assembly_reported.remove(cnv);
		}
	}
}

bool depot_t::try_assemble_convoy(convoihandle_t cnv)
{
	if (!cnv.is_bound() || cnv->get_vehicle_count() == 0 || !cnv->get_schedule() || cnv->get_schedule()->empty())
	{
		return false;
	}
	const schedule_entry_t &entry = cnv->get_schedule()->get_current_entry();
	if (entry.pos != get_pos() || !cnv->get_schedule()->entry_has_consist_order(entry.unique_entry_id))
	{
		return false;
	}
	const consist_order_t &order = cnv->get_schedule()->get_consist_order(entry.unique_entry_id);
	if (order.get_count() == 0)
	{
		return false;
	}

	vector_tpl<vehicle_t*> shadow;
	cnv->get_forward_consist(shadow);
	if (shadow.empty())
	{
		return false;
	}
	vehicle_t *lead = shadow[0];
	if (lead->has_depot_job() && !lead->is_depot_job_complete())
	{
		return false;
	}

	vector_tpl<vehicle_t*> available;
	for (auto v : vehicles)
	{
		if (!v->has_depot_job() || v->is_depot_job_complete())
		{
			available.append(v);
		}
	}

	vector_tpl<vehicle_t*> final_consist;
	vector_tpl<bool> final_reversed;
	const convoi_t::consist_order_process_result result =
		cnv->match_consist_order(order, available, shadow, final_consist, final_reversed);
	if (result == convoi_t::fail || final_consist.empty() || final_consist[0] != lead)
	{
		if (!assembly_reported.is_contained(cnv))
		{
			assembly_reported.append(cnv);
			dbg->warning("depot_t::try_assemble_convoy()", "Convoy %s cannot satisfy the Depot Consist Order at %s", cnv->get_name(), get_pos().get_str());
			if (cnv->get_owner())
			{
				cnv->get_owner()->report_vehicle_problem(cnv, get_pos());
			}
		}
		return false;
	}

	vector_tpl<vehicle_t*> added;
	for (uint32 i = 1; i < final_consist.get_count(); i++)
	{
		vehicle_t *v = final_consist[i];
		if (v->get_convoi() == cnv.get_rep())
		{
			continue;
		}
		if (v->get_convoi() != NULL)
		{
			for (auto a : added)
			{
				cnv->remove_vehicle(a);
				vehicles.append(a);
				a->clear_depot_job();
			}
			return false;
		}
		v->set_reversed(final_reversed[i]);
		v->set_pos(get_pos());
		vehicles.remove(v);
		if (!cnv->add_vehicle(v, (uint8)i))
		{
			vehicles.append(v);
			for (auto a : added)
			{
				cnv->remove_vehicle(a);
				vehicles.append(a);
				a->clear_depot_job();
			}
			return false;
		}
		added.append(v);
	}

	lead->set_reversed(final_reversed[0]);
	for (uint8 i = 0; i < cnv->get_vehicle_count(); i++)
	{
		cnv->get_vehicle(i)->set_leading(i == 0);
		cnv->get_vehicle(i)->set_last(i + 1 == cnv->get_vehicle_count());
	}

	if (!cnv->check_validity())
	{
		for (auto a : added)
		{
			cnv->remove_vehicle(a);
			vehicles.append(a);
			a->set_pos(get_pos());
			a->clear_depot_job();
		}
		lead->set_leading(true);
		lead->set_last(true);
		if (!assembly_reported.is_contained(cnv))
		{
			assembly_reported.append(cnv);
			dbg->warning("depot_t::try_assemble_convoy()", "Convoy %s assembled an invalid Depot Consist Order at %s", cnv->get_name(), get_pos().get_str());
			if (cnv->get_owner())
			{
				cnv->get_owner()->report_vehicle_problem(cnv, get_pos());
			}
		}
		return false;
	}

	clear_convoy_maintenance_jobs(cnv);
	cnv->set_wait_lock(0);
	return true;
}

sync_result depot_t::sync_step(uint32 delta_t)
{
	// A convoy leaves MAINTENANCE or OVERHAUL before it leaves the depot: the
	// state changes to ROUTING_1 in convoi_t::step(). This queue must drop the
	// entry at once: start_convoi() or convoi_t::destroy() removes the convoy
	// from the other depot lists later, but an entry left here would be stepped
	// after the convoy has moved on or has been destroyed. Drop entries whose
	// convoihandle_t no longer refers to a live convoy, and entries whose convoy
	// has left those two states, so that every remaining entry is safe to
	// dereference.
	for (uint32 i = 0; i < under_maintenance.get_count();)
	{
		const convoihandle_t c = under_maintenance[i];
		if (!c.is_bound())
		{
			under_maintenance.remove_at(i);
			continue;
		}
		const int state = c->get_state();
		if (state != convoi_t::MAINTENANCE && state != convoi_t::OVERHAUL)
		{
			under_maintenance.remove_at(i);
			continue;
		}
		++i;
	}

	complete_depot_jobs();
	start_depot_jobs();
	try_assemble_depot_orders();

	for (uint32 i = 0; i < under_maintenance.get_count();)
	{
		const convoihandle_t c = under_maintenance[i];
		if (!c.is_bound() || (c->get_state() != convoi_t::MAINTENANCE && c->get_state() != convoi_t::OVERHAUL))
		{
			under_maintenance.remove_at(i);
			continue;
		}
		c->sync_step(delta_t);
		// A Convoy leaving the depot removes itself from this list, so only
		// advance when the entry is still at the current index.
		if (i < under_maintenance.get_count() && under_maintenance[i] == c)
		{
			++i;
		}
	}

	return sync_result::SYNC_OK;
}
