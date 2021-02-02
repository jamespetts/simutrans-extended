/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "overtaker.h"

vehicle_base_t *overtaker_t::get_blocking_vehicle(const grund_t *gr, const uint8 current_direction, const uint8 next_direction, const uint8 next_90direction, sint8 lane_on_the_tile) const {
	const bool overtaking = lane_on_the_tile == 1 ? true : lane_on_the_tile == -1 ? false : is_overtaking();

	for(  uint8 pos=1;  pos < gr->get_top();  pos++  ) {
		if(  vehicle_base_t* const v = obj_cast<vehicle_base_t>(gr->obj_bei(pos))  ) {
			if(  v->get_typ()==obj_t::pedestrian  ) {
				continue;
			}

			overtaker_t* other_overtaker = v->get_overtaker();
			if(!other_overtaker || other_overtaker == this) { continue; }

			const uint8 other_direction=v->get_direction();

			if(other_direction==255) { continue; }


			const bool other_moving = other_overtaker->get_current_speed() > kmh_to_speed(1);
			const bool other_overtaking = other_overtaker->is_overtaking();

			auto is_across = [](ribi_t::ribi a, ribi_t::ribi b) {
				return a == (world()->get_settings().is_drive_left() ? ribi_t::rotate45l(b) : ribi_t::rotate45(b));
			};

			auto is_backward = [](ribi_t::ribi a, ribi_t::ribi b) {
				return a == ribi_t::backward(b);
			};

			if(  next_direction == other_direction && !ribi_t::is_threeway(gr->get_weg_ribi(road_wt)) && other_overtaking == overtaking) {
				// only consider cars on same lane.
				// cars going in the same direction and no crossing => that mean blocking ...
				return v;
			}

			const ribi_t::ribi other_90direction = (gr->get_pos().get_2d() == v->get_pos_next().get_2d()) ? other_direction : ribi_type(gr->get_pos(),v->get_pos_next());
			if(  other_90direction == next_90direction && other_overtaking == overtaking) {
				// Want to exit in same as other   ~50% of the time
				return v;
			}

			const bool across = is_across(next_direction, next_90direction); // turning across the opposite directions lane
			const bool other_across = is_across(other_direction, other_90direction); // other is turning across the opposite directions lane
			if(  other_direction == next_direction && !(other_across || across) && other_overtaking == overtaking) {
				// only consider cars on same lane.
				// entering same straight waypoint as other ~18%
				return v;
			}

			const bool straight = next_direction == next_90direction; // driving straight
			const ribi_t::ribi current_90direction = straight ? ribi_t::backward(next_90direction) : (~(next_direction|ribi_t::backward(next_90direction)))&0x0F;
			const bool other_straight = other_direction == other_90direction; // other is driving straight
			const bool other_exit_same_side = current_90direction == other_90direction; // other is exiting same side as we're entering
			const bool other_exit_opposite_side = is_backward(current_90direction, other_90direction); // other is exiting side across from where we're entering
			if(  across  &&  ((ribi_t::is_perpendicular(current_90direction,other_direction)  &&  other_moving)  ||  (other_across  &&  other_exit_opposite_side)  ||  ((other_across  ||  other_straight)  &&  other_exit_same_side  &&  other_moving) ) )  {
				// other turning across in front of us from orth entry dir'n   ~4%
				return v;
			}

			const bool headon = is_backward(current_direction, other_direction); // we're meeting the other headon
			const bool other_exit_across = is_across(other_90direction, next_90direction); // other is exiting by turning across the opposite directions lane
			if(  straight  &&  (ribi_t::is_perpendicular(current_90direction,other_direction)  ||  (other_across  &&  other_moving  &&  (other_exit_across  ||  (other_exit_same_side  &&  !headon))) ) ) {
				// other turning across in front of us, but allow if other is stopped - duplicating historic behaviour   ~2%
				return v;
			}
			else if(  other_direction == current_direction &&  current_90direction == ribi_t::none && other_overtaking == overtaking) {
				// entering same diagonal waypoint as other   ~1%
				return v;
			}
		}
	}

	// way is free
	return NULL;
}

