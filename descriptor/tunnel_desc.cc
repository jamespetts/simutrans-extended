/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../dataobj/ribi.h"
#include "tunnel_desc.h"
#include "../network/checksum.h"
#include "../dataobj/environment.h"

int tunnel_desc_t::slope_indices[81] = {
	-1, // 0:
	-1, // 1:
	-1, // 2:
	-1, // 3:
	 1, // 4:north slope
	-1, // 5:
	-1, // 6:
	-1, // 7:
	 1, // 8:north slope
	-1, // 9:
	-1, // 10:
	-1, // 11:
	 2, // 12:west slope
	-1, // 13:
	-1, // 14:
	-1, // 15:
	-1, // 16:
	-1, // 17:
	-1, // 18:
	-1, // 19:
	-1, // 20:
	-1, // 21:
	-1, // 22:
	-1, // 23:
	 2, // 24:west slope
	-1, // 25:
	-1, // 26:
	-1, // 27:
	 3, // 28:east slope
	-1, // 29:
	-1, // 30:
	-1, // 31:
	-1, // 32:
	-1, // 33:
	-1, // 34:
	-1, // 35:
	 0, // 36:south slope
	-1, // 37:
	-1, // 38:
	-1, // 39:
	-1, // 40:
	-1, // 41:
	-1, // 42:
	-1, // 43:
	-1, // 44:
	-1, // 45:
	-1, // 46:
	-1, // 47:
	-1, // 48:
	-1, // 49:
	-1, // 50:
	-1, // 51:
	-1, // 52:
	-1, // 53:
	-1, // 54:
	-1, // 55:
	 3, // 56:east slope
	-1, // 57:
	-1, // 58:
	-1, // 59:
	-1, // 60:
	-1, // 61:
	-1, // 62:
	-1, // 63:
	-1, // 64:
	-1, // 65:
	-1, // 66:
	-1, // 67:
	-1, // 68:
	-1, // 69:
	-1, // 70:
	-1, // 71:
	 0, // 72:south slope
	-1, // 73:
	-1, // 74:
	-1, // 75:
	-1, // 76:
	-1, // 77:
	-1, // 78:
	-1, // 79:
	-1  // 80:
};

void tunnel_desc_t::calc_checksum(checksum_t *chk) const
{
	obj_desc_transport_infrastructure_t::calc_checksum(chk);

	//Extended settings
	//chk->input(max_axle_load);
	chk->input(way_constraints.get_permissive());
	chk->input(way_constraints.get_prohibitive());
	chk->input(depth_cost);
	chk->input(depth2_cost);
	chk->input(depth_limit);
	chk->input(is_half_height);
	chk->input(subsea_cost);
	chk->input(subsea_maintenance);
	chk->input(subwaterline_cost);
	chk->input(subwaterline_maintenance);
	chk->input(subbuilding_cost);
	chk->input(subway_cost);
}


waytype_t tunnel_desc_t::get_finance_waytype() const
{
	return ((get_way_desc() && (get_way_desc()->get_styp() == type_tram)) ? tram_wt : get_waytype()) ;
}

bool tunnel_desc_t::check_way_slope(slope_t::type slope) const{
	return !((env_t::pak_height_conversion_factor == 1  &&  !is_one_high(slope))
			|| (env_t::pak_height_conversion_factor == 2
				&& ((!get_is_half_height() && is_one_high(slope)))));
}
