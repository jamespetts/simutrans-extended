/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_destination_building_info.h"

#include "gui_image.h"
#include "gui_label.h"
#include "../simwin.h"
#include "../../obj/gebaeude.h"
#include "../../simcity.h"


gui_destination_building_info_t::gui_destination_building_info_t(koord zielpos, bool yesno)
{
	is_freight = yesno;

	set_table_layout(4,1);
	if( const grund_t* gr = world()->lookup_kartenboden(zielpos) ) {
		if( const gebaeude_t* const gb = gr->get_building() ) {
			// (1) button
			if( is_freight && gb->get_is_factory() ) {
				fab = gb->get_fabrik();
				button.init(button_t::imagebox_state, NULL);
				// UI TODO: Factory symbol preferred over general freight symbol
				button.set_image(skinverwaltung_t::open_window ? skinverwaltung_t::open_window->get_image_id(0) : skinverwaltung_t::goods->get_image_id(0));
				button.add_listener(this);
			}
			else {
				button.init(button_t::posbutton_automatic, NULL);
				button.set_targetpos(zielpos);
			}
			add_component(&button);

			// (2) name
			gui_label_buf_t *lb = new_component<gui_label_buf_t>();
			if (fab) {
				lb->buf().append( translator::translate(fab->get_name()) );
			}
			else {
				cbuffer_t dbuf;
				gb->get_description(dbuf);
				lb->buf().append(dbuf.get_str());
			}
			lb->update();

			// (3) city
			const stadt_t* city = world()->get_city(zielpos);
			if (city) {
				new_component<gui_image_t>(skinverwaltung_t::intown->get_image_id(0), 0, ALIGN_CENTER_V, true);
			}
			else {
				new_component<gui_empty_t>();
			}

			// (4) pos
			lb = new_component<gui_label_buf_t>();

			if (city) {
				lb->buf().append(city->get_name());
			}
			lb->buf().printf(" %s ", zielpos.get_fullstr());
			lb->update();
		}
		else {
			// Maybe the building was demolished.
			new_component<gui_label_t>("Unknown destination");
		}
	}
	else {
		// Terrain or map size changed or error
		new_component<gui_label_t>("Invalid coordinate");
	}

	set_size(get_min_size());
}

bool gui_destination_building_info_t::action_triggered( gui_action_creator_t *comp, value_t )
{
	if( comp==&button && is_freight ) {
		if( world()->access_fab_list().is_contained(fab) ) {
			// open fab info
			fab->show_info();
		}
		else {
			remove_all();
			is_freight = false;
			new_component<gui_label_t>("Unknown destination"); 	// Maybe the factory was closed.
		}
		return true;
	}
	return false;
}

void gui_destination_building_info_t::draw(scr_coord offset)
{
	if( is_freight && fab ) {
		button.pressed = win_get_magic((ptrdiff_t)fab);
	}
	gui_aligned_container_t::draw(offset);
}
