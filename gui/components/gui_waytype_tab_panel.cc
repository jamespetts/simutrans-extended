/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "gui_waytype_tab_panel.h"

#include "../../simskin.h"
#include "../../simhalt.h"
#include "../../simworld.h"

#include "../../bauer/vehikelbauer.h"

#include "../../boden/wege/kanal.h"
#include "../../boden/wege/maglev.h"
#include "../../boden/wege/monorail.h"
#include "../../boden/wege/narrowgauge.h"
#include "../../boden/wege/runway.h"
#include "../../boden/wege/schiene.h"
#include "../../boden/wege/strasse.h"

#include "../../dataobj/translator.h"

#include "../../descriptor/skin_desc.h"


void gui_waytype_tab_panel_t::init_tabs(gui_component_t* c)
{
	uint8 max_idx = 0;
	add_tab(c, translator::translate("All"), 0, translator::translate("All"), world()->get_settings().get_waytype_color(ignore_wt));
	tabs_to_waytype[max_idx++] = ignore_wt;

	// now add all specific tabs
	if (maglev_t::default_maglev) {
		add_tab(c, translator::translate("Maglev"), skinverwaltung_t::maglevhaltsymbol, translator::translate("Maglev"), world()->get_settings().get_waytype_color(maglev_wt));
		tabs_to_waytype[max_idx++] = maglev_wt;
	}
	if (monorail_t::default_monorail) {
		add_tab(c, translator::translate("Monorail"), skinverwaltung_t::monorailhaltsymbol, translator::translate("Monorail"), world()->get_settings().get_waytype_color(monorail_wt));
		tabs_to_waytype[max_idx++] = monorail_wt;
	}
	if (schiene_t::default_schiene) {
		add_tab(c, translator::translate("Train"), skinverwaltung_t::zughaltsymbol, translator::translate("Train"), world()->get_settings().get_waytype_color(track_wt));
		tabs_to_waytype[max_idx++] = track_wt;
	}
	if (narrowgauge_t::default_narrowgauge) {
		add_tab(c, translator::translate("Narrowgauge"), skinverwaltung_t::narrowgaugehaltsymbol, translator::translate("Narrowgauge"), world()->get_settings().get_waytype_color(narrowgauge_wt));
		tabs_to_waytype[max_idx++] = narrowgauge_wt;
	}
	if (!vehicle_builder_t::get_info(tram_wt).empty()) {
		add_tab(c, translator::translate("Tram"), skinverwaltung_t::tramhaltsymbol, translator::translate("Tram"), world()->get_settings().get_waytype_color(tram_wt));
		tabs_to_waytype[max_idx++] = tram_wt;
	}
	if (strasse_t::default_strasse) {
		add_tab(c, translator::translate("Truck"), skinverwaltung_t::autohaltsymbol, translator::translate("Truck"), world()->get_settings().get_waytype_color(road_wt));
		tabs_to_waytype[max_idx++] = road_wt;
	}
	if (!vehicle_builder_t::get_info(water_wt).empty()) {
		add_tab(c, translator::translate("Ship"), skinverwaltung_t::schiffshaltsymbol, translator::translate("Ship"), world()->get_settings().get_waytype_color(water_wt));
		tabs_to_waytype[max_idx++] = water_wt;
	}
	if (runway_t::default_runway) {
		add_tab(c, translator::translate("Air"), skinverwaltung_t::airhaltsymbol, translator::translate("Air"), world()->get_settings().get_waytype_color(air_wt));
		tabs_to_waytype[max_idx++] = air_wt;
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
