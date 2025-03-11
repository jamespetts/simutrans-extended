/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_WAYTYPE_IMAGE_BOX_H
#define GUI_COMPONENTS_GUI_WAYTYPE_IMAGE_BOX_H


#include "../../simtypes.h"
#include "gui_image.h"
#include "gui_button.h"
#include "../../descriptor/skin_desc.h"
#include "../../simworld.h"
#include "../../bauer/wegbauer.h"
#include "gui_waytype_tab_panel.h"

// panel that show the available waytypes
class gui_waytype_image_box_t :
	public gui_image_t
{
private:
	PIXVAL bgcol = 0;
	bool flexible;

public:
	gui_waytype_image_box_t(waytype_t wt=invalid_wt, bool flexible=false);
	void set_size(scr_size size_par) OVERRIDE;

	void set_waytype(waytype_t wt);

	void draw( scr_coord offset ) OVERRIDE;

	scr_size get_min_size() const OVERRIDE;
	scr_size get_max_size() const OVERRIDE { return flexible ? scr_size(scr_size::inf.w, get_min_size().h) : get_min_size(); }
};

/**
 * waytype symbol button
 */
class gui_waytype_button_t : public button_t
{
	waytype_t wt;

public:
	gui_waytype_button_t(waytype_t wt_ = invalid_wt) : button_t() {
		wt = wt_;
		init(button_t::box_state, NULL);
		set_waytype(wt);
	}

	void set_waytype(waytype_t wt);

	bool infowin_event(event_t const*) OVERRIDE;

	scr_size get_min_size() const OVERRIDE { return size; }
	scr_size get_max_size() const OVERRIDE { return get_min_size(); }
};


#endif
