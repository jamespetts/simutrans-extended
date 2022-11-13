/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_VEHICLE_DETAIL_H
#define GUI_VEHICLE_DETAIL_H


#include "simwin.h"
#include "gui_frame.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_table.h"

class vehicle_desc_t;


// draw vehicle capacity table
class gui_vehicle_capacity_t : public gui_aligned_container_t
{
public:
	gui_vehicle_capacity_t(const vehicle_desc_t *veh_type);
};


class vehicle_detail_t : public gui_frame_t, private action_listener_t
{
private:
	const vehicle_desc_t *veh_type;

	void init_table();

public:
	vehicle_detail_t(const vehicle_desc_t *v);

	const vehicle_desc_t* get_vehicle() { return veh_type; }

	const char *get_help_filename() const OVERRIDE {return "vehicle_detail.txt"; }

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;
};


#endif
