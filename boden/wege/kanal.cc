/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../../simworld.h"
#include "../../display/simimg.h"

#include "../../descriptor/ground_desc.h"
#include "../../descriptor/way_desc.h"

#include "../../bauer/wegbauer.h"
#include "../../dataobj/translator.h"
#include "kanal.h"

const way_desc_t *kanal_t::default_kanal=NULL;



kanal_t::kanal_t(loadsave_t *file) :  weg_t(water_wt)
{
	rdwr(file);
}



kanal_t::kanal_t() : weg_t (water_wt)
{
	set_desc(default_kanal);
}



void kanal_t::rdwr(loadsave_t *file)
{
	weg_t::rdwr(file);

	if(file->is_version_less(87, 1)) {
		set_desc(default_kanal);
		return;
	}

	if(file->is_saving()) {
		const char *s = get_desc()->get_name();
		file->rdwr_str(s);
		if(file->get_extended_version() >= 12)
		{
			s = replacement_way ? replacement_way->get_name() : "";
			file->rdwr_str(s);
		}
	}
	else {
		char bname[128];
		file->rdwr_str(bname, lengthof(bname));
		const way_desc_t *desc = way_builder_t::get_desc(bname);


		const way_desc_t* loaded_replacement_way = NULL;
		if(file->get_extended_version() >= 12)
		{
			char rbname[128];
			file->rdwr_str(rbname, lengthof(rbname));
			loaded_replacement_way = way_builder_t::get_desc(rbname);
		}

		const sint32 old_max_speed = get_max_speed();
		const uint32 old_max_axle_load = get_max_axle_load();
		const uint32 old_bridge_weight_limit = get_bridge_weight_limit();
		if(desc==NULL) {
			desc = way_builder_t::get_desc(translator::compatibility_name(bname));
			if(desc==NULL) {
				desc = default_kanal;
				if (desc == NULL) {
					dbg->fatal("kanal_t::rdwr", "Trying to load canal but pakset has no water ways!");
				}
				welt->add_missing_paks( bname, karte_t::MISSING_WAY );
			}
			dbg->warning("kanal_t::rdwr()", "Unknown canal '%s' replaced by '%s' (old_max_speed %i)", bname, desc->get_name(), old_max_speed );
		}

		set_desc(desc, file->get_extended_version() >= 12);

		if(file->get_extended_version() >= 12)
		{
			replacement_way = loaded_replacement_way;
		}
		if(old_max_speed>0) {
			set_max_speed(old_max_speed);
		}
		if(old_max_axle_load > 0)
		{
			set_max_axle_load(old_max_axle_load);
		}
		if(old_bridge_weight_limit > 0)
		{
			set_bridge_weight_limit(old_bridge_weight_limit);
		}
	}
}
