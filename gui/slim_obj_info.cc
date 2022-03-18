/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "slim_obj_info.h"

#include "../simworld.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../descriptor/pedestrian_desc.h"
#include "../descriptor/tree_desc.h"
#include "../descriptor/groundobj_desc.h"
#include "../obj/baum.h"
#include "../obj/groundobj.h"
#include "../utils/simstring.h"
#include "../vehicle/movingobj.h"


slim_obj_info_t::slim_obj_info_t(const obj_t* obj) :
	gui_frame_t(translator::translate(obj->get_name()), NULL),
	view(obj, scr_size(max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8)))
{
	set_table_layout(1, 0);
	set_alignment(ALIGN_CENTER_H);
	const char *maker = NULL;
	stickable = obj->get_typ() != obj_t::pedestrian;
	switch (obj->get_typ())
	{
		case obj_t::pedestrian:
		{
			const pedestrian_t *ped = static_cast<const pedestrian_t *>(obj);
			maker = ped->get_desc()->get_copyright();
			break;
		}
		case obj_t::baum:
		{
			old_date = 0;
			const baum_t *baum = static_cast<const baum_t *>(obj);
			maker = baum->get_desc()->get_copyright();
			new_component<gui_label_t>(baum->get_desc()->get_name());
			break;
		}
		case obj_t::groundobj:
		{
			const groundobj_t *g_obj = static_cast<const groundobj_t *>(obj);
			maker = g_obj->get_desc()->get_copyright();
			new_component<gui_label_t>(g_obj->get_desc()->get_name());

			label.buf().append(translator::translate("cost for removal"));
			char buffer[128];
			money_to_string( buffer, g_obj->get_desc()->get_value()/100.0 );
			label.buf().append( buffer );
			label.update();

			break;
		}

		case obj_t::movingobj:
		{
			const movingobj_t *m_obj = static_cast<const movingobj_t *>(obj);
			maker = m_obj->get_desc()->get_copyright();
			new_component<gui_label_t>(m_obj->get_desc()->get_name());

			label.buf().append(translator::translate("cost for removal"));
			char buffer[128];
			money_to_string( buffer, m_obj->get_desc()->get_value()/100.0 );
			label.buf().append( buffer );
			label.update();

			break;
		}
		default:
			break;
	}

	add_component(&view);

	if (obj->get_typ() != obj_t::pedestrian) {
		label.set_align(gui_label_t::left);
		add_component(&label);
	}

	if (maker) {
		add_table(2, 2)->set_spacing(scr_size(0, 0)); {
			new_component_span<gui_label_t>("Constructed by", 2);
			new_component<gui_margin_t>(LINESPACE);
			new_component<gui_label_t>(maker);
		} end_table();
	}
	set_windowsize();
}

void slim_obj_info_t::set_windowsize()
{
	const scr_coord_val titlebar_min_width = D_GADGET_WIDTH*(1+stickable*2)+2+2+D_H_SPACE+proportional_string_width(get_name());
	reset_min_windowsize();
	gui_frame_t::set_windowsize(scr_size(max(get_min_windowsize().w, titlebar_min_width), get_min_windowsize().h));
}

void slim_obj_info_t::draw(scr_coord pos, scr_size size)
{
	if(old_date!=65535 && old_date != world()->get_timeline_year_month()) {
		// update tree age label
		if (const baum_t *baum = static_cast<const baum_t *>(view.get_obj())) {
			old_date = world()->get_timeline_year_month();
			const uint32 age = baum->get_age();
			label.buf().printf( translator::translate("%i years %i months old."), age/12, (age%12) );
			label.update();
			set_windowsize();
		}
	}
	gui_frame_t::draw(pos, size);
	pos += view.get_pos() + scr_size(D_V_SPACE + 2, D_TITLEBAR_HEIGHT + 2);
	display_proportional_clip_rgb(pos.x+1, pos.y+1, view.get_obj()->get_pos().get_2d().get_fullstr(), ALIGN_LEFT, color_idx_to_rgb(COL_BLACK), true);
	display_proportional_clip_rgb(pos.x,   pos.y,   view.get_obj()->get_pos().get_2d().get_fullstr(), ALIGN_LEFT, color_idx_to_rgb(COL_WHITE), true);
}


