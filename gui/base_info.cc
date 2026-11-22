/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "base_info.h"

#include <stdio.h>

#include "../simworld.h"
#include "../display/viewport.h"
#include "../obj/zeiger.h"


bool parse_coordinate_link(const char *link, koord3d &target)
{
	if (link == NULL || link[0] != '(')
	{
		return false;
	}
	int x = -1, y = -1, z = -1;
	int n = sscanf(link, "(%i,%i,%i)", &x, &y, &z);
	if (n < 3)
	{
		z = -1;
		n = sscanf(link, "(%i,%i)", &x, &y);
	}
	if (n < 2)
	{
		return false;
	}
	target = koord3d(x, y, z);
	return true;
}


bool jump_to_coordinate_link(const char *link)
{
	koord3d target;
	if (!parse_coordinate_link(link, target) || !world()->is_within_limits(target.get_2d()))
	{
		return false;
	}
	if (target.z < 0 || world()->lookup(target) == NULL)
	{
		const grund_t *gr = world()->lookup_kartenboden(target.get_2d());
		if (gr == NULL)
		{
			return false;
		}
		target = gr->get_pos();
	}
	world()->get_viewport()->change_world_position(target);
	world()->get_zeiger()->change_pos(target);
	return true;
}


/**
 * Auxiliary class to handle a gui_fixedwidth_textarea_t element with
 * some embedded element.
 */
class gui_textarea_with_embedded_element_t : public gui_container_t
{
	gui_fixedwidth_textarea_t *textarea;
	/// the embedded component
	gui_component_t *embedded;
public:
	gui_textarea_with_embedded_element_t(gui_fixedwidth_textarea_t *ta)
	{
		textarea = ta;
		add_component(textarea);
		textarea->set_pos( scr_coord(0,0) );
		embedded = NULL;
	}

	scr_size get_min_size() const OVERRIDE { return textarea->get_min_size(); }
	scr_size get_max_size() const OVERRIDE { return scr_size(scr_size::inf.w, textarea->get_min_size().h); }

	void set_size(scr_size size) OVERRIDE
	{
		gui_container_t::set_size(size);
		textarea->set_size(size);
		// align right
		if (embedded) {
			embedded->set_pos( scr_coord( size.w - embedded->get_size().w, 0) );
		}
	}

	void set_embedded(gui_component_t *other)
	{
		if (embedded) {
			remove_component(embedded);
		}
		embedded = other;
		if (embedded) {
			add_component(embedded);
			textarea->set_reserved_area( embedded->get_size() + scr_size(D_H_SPACE,D_V_SPACE) );
			// align right
			if (embedded) {
				embedded->set_pos( scr_coord( size.w - embedded->get_size().w, 0) );
			}
		}
		else {
			textarea->set_reserved_area( scr_size(0,0) );
		}
	}

	gui_component_t *get_embedded() const { return embedded; }
};


base_infowin_t::base_infowin_t(const char *name, const player_t *player) :
	gui_frame_t(name, player),
	textarea(&buf, D_DEFAULT_WIDTH-D_MARGIN_LEFT-D_MARGIN_RIGHT)
{
	set_table_layout(1,0);

	buf.clear();

	container = new_component<gui_textarea_with_embedded_element_t>(&textarea);
}


void base_infowin_t::set_embedded(gui_component_t *other)
{
	container->set_embedded(other);
	recalc_size();
}


gui_component_t *base_infowin_t::get_embedded() const
{
	return container->get_embedded();
}


void base_infowin_t::recalc_size()
{
	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
}
