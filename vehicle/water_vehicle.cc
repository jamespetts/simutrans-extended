/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "water_vehicle.h"

#include "../simware.h"
#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../obj/crossing.h"
#include "../obj/roadsign.h"
#include "../dataobj/schedule.h"

#include "../dataobj/environment.h"
#include "../path_explorer.h"


water_vehicle_t::water_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cn) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::water_vehicle, pos, desc, player)
#else
	vehicle_t(pos, desc, player)
#endif
{
	cnv = cn;
}

water_vehicle_t::water_vehicle_t(loadsave_t *file, bool is_leading, bool is_last) :
#ifdef INLINE_OBJ_TYPE
    vehicle_t(obj_t::water_vehicle)
#else
    vehicle_t()
#endif
{
	vehicle_t::rdwr_from_convoi(file);

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
			ware_t w;
			for (uint8 i = 0; i < number_of_classes; i++)
			{
				if (!fracht[i].empty())
				{
					w = fracht[i].front();
					gd = w.get_desc();
					empty = false;
					break;
				}
			}
			if (!gd)
			{
				// Important: This must be the same default ware as used in rdwr_from_convoi.
				gd = goods_manager_t::passengers;
			}

			dbg->warning("water_vehicle_t::water_vehicle_t()", "try to find a fitting vehicle for %s.", gd->get_name());
			desc = vehicle_builder_t::get_best_matching(water_wt, 0, empty ? 0 : 30, 100, 40, gd, true, last_desc, is_last );
			if(desc) {
				calc_image();
			}
		}
		// update last desc
		if(  desc  ) {
			last_desc = desc;
		}
	}
	fix_class_accommodations();
}


void water_vehicle_t::enter_tile(grund_t* gr)
{
	vehicle_t::enter_tile(gr);

	if(  weg_t *ch = gr->get_weg(water_wt)  ) {
		// we are in a channel, so book statistics
		ch->book(get_total_cargo(), WAY_STAT_GOODS);
		if (leading)  {
			ch->book(1, WAY_STAT_CONVOIS);
		}
	}
}


bool water_vehicle_t::check_next_tile(const grund_t *bd) const
{
	const weg_t *w = bd->get_weg(water_wt);
	if (cnv && cnv->has_tall_vehicles())
	{
		if (w)
		{
			if (bd->is_height_restricted())
			{
				cnv->suche_neue_route();
				return false;
			}
		}
		else
		{
			// Check for low bridges on open water
			const grund_t* gr_above = world()->lookup(get_pos() + koord3d(0, 0, 1));
			if (env_t::pak_height_conversion_factor == 2 && gr_above && gr_above->get_weg_nr(0))
			{
				return false;
			}
		}
	}

	if(bd->is_water() || !w)
	{
		// If there are permissive constraints, this vehicle cannot
		// traverse open seas, but it may use lakes.
		if(bd->get_hoehe() > welt->get_groundwater())
		{
			return true;
		}
		else
		{
			return desc->get_way_constraints().get_permissive() == 0;
		}
	}
	// channel can have more stuff to check
#ifdef ENABLE_WATERWAY_SIGNS
	if(  w  &&  w->has_sign()  ) {
		const roadsign_t* rs = bd->find<roadsign_t>();
		if(  rs->get_desc()->get_wtyp()==get_waytype()  ) {
			if(  cnv !=NULL  &&  rs->get_desc()->get_min_speed() > 0  &&  rs->get_desc()->get_min_speed() > cnv->get_min_top_speed()  ) {
				// below speed limit
				return false;
			}
			if(  rs->get_desc()->is_private_way()  &&  (rs->get_player_mask() & (1<<get_owner_nr()) ) == 0  ) {
				// private road
				return false;
			}
		}
	}
#endif
	const uint8 convoy_vehicle_count = cnv ? cnv->get_vehicle_count() : 1;
	bool can_clear_way_constraints = true;
	if(convoy_vehicle_count < 2)
	{
		return(check_access(w) && w->get_max_speed() > 0 && check_way_constraints(*w));
	}
	else
	{
		// We must check the way constraints of each vehicle in the convoy
		if(check_access(w) && w->get_max_speed() > 0)
		{
			for(sint32 i = 0; i < convoy_vehicle_count; i++)
			{
				if(!cnv->get_vehicle(i)->check_way_constraints(*w))
				{
					can_clear_way_constraints = false;
					break;
				}
			}
		}
		else
		{
			return false;
		}
	}
	return can_clear_way_constraints;
}


/** Since slopes are handled different for ships
 */
void water_vehicle_t::calc_drag_coefficient(const grund_t *gr)
{
	if(gr == NULL)
	{
		return;
	}

	// flat water
	current_friction = get_friction_of_waytype(water_wt);
	if(gr->get_weg_hang()) {
		// hill up or down => in lock => deccelarte
		current_friction += 15;
	}

	if(previous_direction != direction) {
		// curve: higher friction
		current_friction *= 2;
	}
}


bool water_vehicle_t::can_enter_tile(const grund_t *gr, sint32 &restart_speed, uint8)
{
	restart_speed = -1;

	if (leading)
	{
		assert(gr);

		if (!check_tile_occupancy(gr))
		{

			return false;
		}

		const weg_t* w = gr->get_weg(water_wt);

		if (w && w->is_crossing()) {
			// ok, here is a draw/turn-bridge ...
			crossing_t* cr = gr->find<crossing_t>();
			if(!cr->request_crossing(this)) {
				restart_speed = 0;
				return false;
			}
		}
	}
	return true;
}

bool water_vehicle_t::check_tile_occupancy(const grund_t* gr)
{
	if (gr->get_top() > 200)
	{
		return false;
	}

	const uint8 base_max_vehicles_on_tile = 127;
	const weg_t *w = gr->get_weg(water_wt);
	uint8 max_water_vehicles_on_tile = w ? w->get_desc()->get_max_vehicles_on_tile() : base_max_vehicles_on_tile;
	uint8 water_vehicles_on_tile = gr->get_top();

	if(water_vehicles_on_tile > max_water_vehicles_on_tile)
	{
		int relevant_water_vehicles_on_tile = 0;
		if(max_water_vehicles_on_tile < base_max_vehicles_on_tile && water_vehicles_on_tile < base_max_vehicles_on_tile)
		{
			for(sint32 n = gr->get_top(); n-- != 0;)
			{
				const obj_t *obj = gr->obj_bei(n);
				if(obj && obj->get_typ() == obj_t::water_vehicle)
				{
					const vehicle_t* water_craft = (const vehicle_t*)obj;
					if(water_craft->get_convoi()->is_loading())
					{
						continue;
					}
					const bool has_superior_loading_level = cnv->get_loading_level() > water_craft->get_convoi()->get_loading_level();
					const bool has_inferior_id = water_craft->get_convoi()->self.get_id() < get_convoi()->self.get_id();
					if(!has_superior_loading_level && has_inferior_id)
					{
						relevant_water_vehicles_on_tile ++;
						if(relevant_water_vehicles_on_tile >= max_water_vehicles_on_tile)
						{
							// Too many water vehicles already here.
							return false;
						}
					}
					else if(water_craft->get_convoi()->get_state() == convoi_t::DRIVING)
					{
						water_craft->get_convoi()->set_state(convoi_t::WAITING_FOR_CLEARANCE);
					}
				}
			}
		}
	}
	return true;
}

schedule_t * water_vehicle_t::generate_new_schedule() const
{
	return new ship_schedule_t();
}

