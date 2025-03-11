/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_VEHICLE_CAPACITYBAR_H
#define GUI_COMPONENTS_GUI_VEHICLE_CAPACITYBAR_H


#include "gui_aligned_container.h"

#include "../../convoihandle_t.h"
#include "../../simconvoi.h"
#include "../../simline.h"

 // UI TODO: Incorporate a mode without capacity display into the convoy assembler
class gui_convoy_loading_info_t : public gui_aligned_container_t
{
	convoihandle_t cnv;
	linehandle_t line;

	uint8 old_vehicle_count = 0;
	sint32 old_weight = -1;
	bool old_reversed = false;

	bool show_loading;

	void update_list();

	uint16 get_unique_fare_capacity(uint8 catg_index, uint8 g_class);
	uint16 get_total_cargo_by_fare_class(uint8 catg_index, uint8 g_class);
	uint16 get_overcrowded_capacity(uint8 g_class);
	uint16 get_overcrowded(uint8 g_class);

public:
	gui_convoy_loading_info_t(linehandle_t line, convoihandle_t cnv, bool show_loading_info = true);

	void set_convoy(convoihandle_t c) { cnv = c; line = linehandle_t(); update_list(); }
	void set_line(linehandle_t l) { line = l; cnv = convoihandle_t(); update_list(); }

	void draw(scr_coord offset) OVERRIDE;
};



#endif
