/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_FABRIK_INFO_H
#define GUI_FABRIK_INFO_H


#include "simwin.h"

#include "building_info.h"
#include "factory_chart.h"
#include "components/action_listener.h"
#include "components/gui_scrollpane.h"
#include "components/gui_textarea.h"
#include "components/gui_textinput.h"
#include "components/gui_obj_view_t.h"
#include "components/gui_container.h"
#include "components/gui_colorbox.h"
#include "components/gui_image.h"
#include "components/gui_tab_panel.h"
#include "../utils/cbuffer_t.h"
#include "components/gui_speedbar.h"
#include "components/gui_factory_storage_info.h"

class welt_t;
class fabrik_t;
class gebaeude_t;
class button_t;


/**
 * Helper class: one row of entries in consumer / supplier table
 */
class gui_factory_connection_container_t : public gui_aligned_container_t
{
	fabrik_t *fab;
	bool is_supplier_display; // which display is needed? - input or output
	uint32 old_connection_count;

public:
	gui_factory_connection_container_t(fabrik_t *factory, bool is_supplier_display);

	void update_table();
	// for reload
	void set_fab(fabrik_t *f) {
		this->fab = f;
		update_table();
	}

	void draw(scr_coord offset) OVERRIDE;
};

/**
 * Info window for factories
 */
class fabrik_info_t : public gui_frame_t, public action_listener_t
{
private:
	fabrik_t *fab;

	cbuffer_t details_buf;

	static sint16 tabstate;

	factory_chart_t chart;

	gui_speedbar_fixed_length_t staffing_bar;
	sint32 staffing_level;
	sint32 staffing_level2;
	sint32 staff_shortage_factor;

	obj_view_t view;

	char fabname[256];
	gui_textinput_t input;

	gui_label_with_symbol_t lb_staff_shortage;
	gui_label_buf_t lb_operation_rate, lb_productivity, lb_electricity_demand, lb_job_demand, lb_visitor_demand, lb_mail_demand, lb_city, lb_alert;

	gui_tab_panel_t switch_mode, tabs_factory_link;

	gui_image_t boost_electric, boost_passenger, boost_mail, img_intown;

	gui_aligned_container_t container_info;
	gui_building_stats_t container_details;

	gui_factory_connection_container_t cont_suppliers, cont_consumers;
	gui_factory_nearby_halt_info_t nearby_halts;

	uint32 old_suppliers_count, old_consumers_count;

	gui_scrollpane_t scroll_info, scrolly_details;

	button_t bt_access_minimap;

	void rename_factory();

	void set_tab_opened();

	void update_components();
public:
	// refreshes text, images, indicator
	void update_info();

	void update_factory_link(bool force=false);

	fabrik_info_t(fabrik_t* fab = NULL, const gebaeude_t* gb = NULL);

	virtual ~fabrik_info_t();

	void init(fabrik_t* fab, const gebaeude_t* gb);

	fabrik_t* get_factory() { return fab; }

	/**
	 * Set the window associated helptext
	 * @return the filename for the helptext, or NULL
	 */
	const char *get_help_filename() const OVERRIDE {return "industry_info.txt";}

	koord3d get_weltpos(bool) OVERRIDE { return fab->get_pos(); }

	bool is_weltpos() OVERRIDE;

	/**
	* Draw new component. The values to be passed refer to the window
	* i.e. It's the screen coordinates of the window where the
	* component is displayed.
	*/
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	// rotated map need new info ...
	void map_rotate90( sint16 ) OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;

	uint32 get_rdwr_id() OVERRIDE { return magic_factory_info; }
};

#endif
