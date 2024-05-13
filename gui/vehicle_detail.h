/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_VEHICLE_DETAIL_H
#define GUI_VEHICLE_DETAIL_H


#include "simwin.h"
#include "gui_frame.h"
#include "components/gui_button.h"
#include "components/gui_colorbox.h"
#include "components/gui_scrollpane.h"
#include "components/gui_label.h"
#include "components/gui_image.h"
#include "components/gui_table.h"
#include "components/gui_tab_panel.h"

class vehicle_desc_t;


// draw vehicle capacity table
class gui_vehicle_capacity_t : public gui_aligned_container_t
{
public:
	gui_vehicle_capacity_t(const vehicle_desc_t *veh_type);

	void init_table(const vehicle_desc_t *veh_type);
};


// A simple vehicle information display component for accessing details
class gui_vehicle_detail_access_t : public gui_aligned_container_t
{
	const vehicle_desc_t *veh_type;
public:
	gui_vehicle_detail_access_t(const vehicle_desc_t *veh_type);

	bool infowin_event(event_t const*) OVERRIDE;
};


class vehicle_detail_t : public gui_frame_t, private action_listener_t
{
private:
	const vehicle_desc_t *veh_type;

	uint16 month_now; // auto update flag

	gui_label_buf_t lb_name;
	gui_image_t img_vehicle;
	gui_vehicle_bar_t colorbar;

	gui_tab_panel_t tabs;

	button_t bt_prev, bt_next;

	gui_vehicle_capacity_t cont_capacity;

	gui_aligned_container_t cont_spec, cont_maintenance, cont_upgrade, cont_livery;
	gui_scrollpane_t scroll_livery, scroll_upgrade;

	void init_table();
	void update_components();

public:
	vehicle_detail_t(const vehicle_desc_t *v);

	void set_vehicle(const vehicle_desc_t *v) { veh_type = v; update_components(); }
	const vehicle_desc_t* get_vehicle() { return veh_type; }

	const char *get_help_filename() const OVERRIDE {return "vehicle_detail.txt"; }

	bool action_triggered(gui_action_creator_t *comp, value_t) OVERRIDE;

	void draw(scr_coord pos, scr_size size) OVERRIDE;
};


#endif
