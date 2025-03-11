/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "gui_waytype_tab_panel.h"

#include "../../simskin.h"
#include "../../simhalt.h"
#include "../../simworld.h"
#include "../../simline.h"

#include "../../bauer/wegbauer.h"

#include "../../boden/wege/kanal.h"
#include "../../boden/wege/maglev.h"
#include "../../boden/wege/monorail.h"
#include "../../boden/wege/narrowgauge.h"
#include "../../boden/wege/runway.h"
#include "../../boden/wege/schiene.h"
#include "../../boden/wege/strasse.h"

#include "../../dataobj/translator.h"

#include "../../descriptor/skin_desc.h"

// Control display order
static waytype_t waytypes[simline_t::MAX_LINE_TYPE] =
{ ignore_wt, maglev_wt, monorail_wt, track_wt, narrowgauge_wt, tram_wt, road_wt, water_wt, air_wt };


void gui_waytype_tab_panel_t::init_tabs(gui_component_t* c)
{
	uint8 max_idx = 0;
	for (uint8 i = 0; i < simline_t::MAX_LINE_TYPE; i++) {
		if (way_builder_t::is_active_waytype(waytypes[i])) {
			add_tab(c, get_translated_waytype_name(waytypes[i]), skinverwaltung_t::get_waytype_skin(waytypes[i]), get_translated_waytype_name(waytypes[i]), world()->get_settings().get_waytype_color(waytypes[i]));
			tabs_to_waytype[max_idx++] = waytypes[i];
		}
	}
}


void gui_waytype_tab_panel_t::set_active_tab_waytype(waytype_t wt)
{
	for(uint32 i=0;  i < get_count();  i++  ) {
		if (wt == tabs_to_waytype[i]) {
			set_active_tab_index(i);
			return;
		}
	}
	// assume invalid type
	set_active_tab_index(0);
}


haltestelle_t::stationtyp gui_waytype_tab_panel_t::get_active_tab_stationtype() const
{
	switch(get_active_tab_waytype()) {
		case air_wt:         return haltestelle_t::airstop;
		case road_wt:        return haltestelle_t::loadingbay | haltestelle_t::busstop;
		case track_wt:       return haltestelle_t::railstation;
		case water_wt:       return haltestelle_t::dock;
		case monorail_wt:    return haltestelle_t::monorailstop;
		case maglev_wt:      return haltestelle_t::maglevstop;
		case tram_wt:        return haltestelle_t::tramstop;
		case narrowgauge_wt: return haltestelle_t::narrowgaugestop;
		default:             return haltestelle_t::invalid;

	}
}

char const * gui_waytype_tab_panel_t::get_translated_waytype_name(waytype_t wt)
{
	switch (wt) {
		case air_wt:
		case road_wt:
		case track_wt:
		case water_wt:
		case monorail_wt:
		case maglev_wt:
		case tram_wt:
		case narrowgauge_wt:
			return simline_t::get_linetype_name(simline_t::waytype_to_linetype(wt));
		case ignore_wt:      return translator::translate("All");
		default:             return nullptr;
	}
	return nullptr;
}
