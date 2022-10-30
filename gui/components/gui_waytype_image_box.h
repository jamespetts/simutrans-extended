/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_WAYTYPE_IMAGE_BOX_H
#define GUI_COMPONENTS_GUI_WAYTYPE_IMAGE_BOX_H


#include "../../simtypes.h"
#include "gui_image.h"


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

#endif
