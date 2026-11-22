/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_BASE_INFO_H
#define GUI_BASE_INFO_H


#include "gui_frame.h"
#include "components/gui_fixedwidth_textarea.h"
#include "../utils/cbuffer_t.h"
#include "../dataobj/koord3d.h"

class gui_textarea_with_embedded_element_t;

/**
 * Parses an information-window link target of the form "(x,y)" or "(x,y,z)".
 * @param target receives the coordinates; z is -1 when only two were given.
 * @return false when @p link is not a coordinate link.
 */
bool parse_coordinate_link(const char *link, koord3d &target);

/**
 * Moves the viewport and the cursor to an information-window coordinate link
 * target in world coordinates. A target without a z coordinate, or whose z
 * has no ground, uses the ground tile at that position.
 * @return false when @p link is not a coordinate link or is outside the map.
 */
bool jump_to_coordinate_link(const char *link);

/**
 * Base class to show window with fixed-width text with an embedded object.
 */
class base_infowin_t : public gui_frame_t
{
protected:
	/// buffers the text
	cbuffer_t buf;

	/// displays the text
	gui_fixedwidth_textarea_t textarea;

	// special container to handle text area with embedded component
	gui_textarea_with_embedded_element_t *container;

	/// recalcs size of text, adjusts window-size if necessary
	void recalc_size();

	/// adds @p other as embedded element within textarea
	void set_embedded(gui_component_t *other);

	gui_component_t *get_embedded() const;

public:
	base_infowin_t(const char *name, const player_t *player=NULL);
};


#endif
