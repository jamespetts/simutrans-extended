/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "fabrik_info.h"

#include "components/gui_label.h"

#include "help_frame.h"

#include "../simfab.h"
#include "../simcolor.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../simcity.h"
#include "simwin.h"
#include "../simmenu.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../simskin.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../utils/simstring.h"
#include "map_frame.h"


/*
static const char factory_status_type[fabrik_t::MAX_FAB_STATUS][64] =
{
	"", "", "", "", "", // status not problematic
	"Storage full",
	"Inactive", "Shipment stuck",
	"Material shortage", "No material",
	"stop_some_goods_arrival", "Fully stocked",
	"fab_stuck",
	"missing_connection", "missing_connection", "material_not_available"
};

static const int fab_alert_level[fabrik_t::MAX_FAB_STATUS] =
{
	0, 0, 0, 0, 0, // status not problematic
	1,
	2, 2,
	2, 2,
	3, 3,
	4,
	4, 4, 4
};

sint16 fabrik_info_t::tabstate = 0;
*/

fabrik_info_t::fabrik_info_t(fabrik_t* fab_, const gebaeude_t* gb) :
	gui_frame_t(""),
	fab(fab_),
	chart(NULL),
	view(scr_size( max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	prod(&prod_buf),
	txt(&info_buf),
	storage(fab_),
	container_details(gb, get_titlecolor()),
	scroll_info(&container_info),
	scrolly_details(&container_details),
	cont_suppliers(fab_, true),
	cont_consumers(fab_, false),
	nearby_halts(fab_)
{
	if (fab) {
		init(fab, gb);
	}
}


void fabrik_info_t::init(fabrik_t* fab_, const gebaeude_t* /*gb*/)
{
	fab = fab_;
	// window name
	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name(fab->get_name());
	set_owner(fab->get_owner());

	set_table_layout(1,0);
	add_table(2,1)->set_alignment(ALIGN_LEFT | ALIGN_TOP);
	{
		// left
		add_table(1,0);
		{
			// input name
			input.set_text( fabname, lengthof(fabname) );
			input.add_listener(this);
			add_component(&input);

			// top part: production number & details, boost indicators, factory view
			add_table(6,0)->set_alignment(ALIGN_TOP);
			{
				// production details per input/output
				fab->info_prod( prod_buf );
				prod.recalc_size();
				add_component( &prod );

				new_component<gui_fill_t>();

				// indicator for possible boost by electricity, passengers, mail
				if (fab->get_desc()->get_electric_boost() ) {
					boost_electric.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
					add_component(&boost_electric);

				}
				if (fab->get_desc()->get_pax_boost() ) {
					boost_passenger.set_image(skinverwaltung_t::passengers->get_image_id(0), true);
					add_component(&boost_passenger);
				}
				if (fab->get_desc()->get_mail_boost() ) {
					boost_mail.set_image(skinverwaltung_t::mail->get_image_id(0), true);
					add_component(&boost_mail);
				}
			}
			end_table();
		}
		end_table();


		// right: view+staffing bar
		add_table(1,3)->set_spacing(scr_size(0,0));
		{
			// world view object
			add_component(&view);
			view.set_obj(gb);

			if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
				staff_shortage_factor = 0;
				if (fab->get_sector() == fabrik_t::end_consumer) {
					staff_shortage_factor = world()->get_settings().get_minimum_staffing_percentage_consumer_industry();
				}
				else if (!(world()->get_settings().get_rural_industries_no_staff_shortage() && fab->get_sector() == fabrik_t::resource)) {
					staff_shortage_factor = world()->get_settings().get_minimum_staffing_percentage_full_production_producer_industry();
				}
				staffing_bar.add_color_value(&staff_shortage_factor, COL_CAUTION);
				staffing_bar.add_color_value(&staffing_level, goods_manager_t::passengers->get_color());
				staffing_bar.add_color_value(&staffing_level2, COL_STAFF_SHORTAGE);
				add_component(&staffing_bar);
				staffing_bar.set_width(view.get_size().w);
			}
			else {
				new_component<gui_empty_t>();
			}

			lb_staff_shortage.set_text("staff_shortage");
			lb_staff_shortage.set_visible(false);
			lb_staff_shortage.set_color(COL_STAFF_SHORTAGE);
			add_component(&lb_staff_shortage);
		}
		end_table();

	}
	end_table();

	storage.recalc_size();
	add_component(&storage);

	// tab panel: connections, chart panels, details
	add_component(&switch_mode);
	scroll_info.set_maximize(true);
	switch_mode.add_tab(&scroll_info, translator::translate("Connections"));

	// connection information

	container_info.set_table_layout(1,0);
	container_info.new_component<gui_label_t>("Connected stops (freight)");
	container_info.add_component(&nearby_halts);
	container_info.new_component<gui_margin_t>(0, LINESPACE/2);
	container_info.add_table(2,1);
	{
		container_info.new_component<gui_fill_t>();
		bt_access_minimap.init(button_t::roundbox, "show_chain_on_minimap", scr_coord(0, 0), D_WIDE_BUTTON_SIZE);
		if (skinverwaltung_t::open_window) {
			bt_access_minimap.set_image(skinverwaltung_t::open_window->get_image_id(0));
			bt_access_minimap.set_image_position_right(true);
		}
		bt_access_minimap.set_tooltip("helptxt_access_minimap");
		bt_access_minimap.add_listener(this);
		container_info.add_component(&bt_access_minimap);
	}
	container_info.end_table();

	if (!fab->get_input().empty()) {
		tabs_factory_link.add_tab(&cont_suppliers, translator::translate("Suppliers"));
	}
	if (!fab->get_output().empty()) {
		tabs_factory_link.add_tab(&cont_consumers, translator::translate("Abnehmer"));
	}

	container_info.add_component(&tabs_factory_link);

	container_details.add_component(&txt);
	fab->info_conn(info_buf);

	// initialize to zero, update_info will do the rest
	old_suppliers_count = 0;
	old_consumers_count = 0;

	update_info();

	// take-over chart tabs into our
	chart.set_factory(fab);
	switch_mode.take_tabs(chart.get_tab_panel());

	switch_mode.add_tab(&scrolly_details, translator::translate("Building info."));

	// factory description in tab
	{
		bool add_tab = false;
		details_buf.clear();

		// factory details
		char key[256];
		sprintf(key, "factory_%s_details", fab->get_desc()->get_name());
		const char * value = translator::translate(key);
		if(value && *value != 'f') {
			details_buf.append(value);
			add_tab = true;
		}

		if (add_tab) {
			switch_mode.add_tab(&container_details, translator::translate("Details"));
			container_details.set_table_layout(1,0);
			gui_flowtext_t* f = container_details.new_component<gui_flowtext_t>();
			f->set_text( (const char*)details_buf);
		}
	}
	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(gui_frame_t::diagonal_resize);
}


fabrik_info_t::~fabrik_info_t()
{
	rename_factory();
	fabname[0] = 0;
}


void fabrik_info_t::rename_factory()
{
	if(  fabname[0]  &&  welt->get_fab_list().is_contained(fab)  &&  strcmp(fabname, fab->get_name())  ) {
		// text changed and factory still exists => call tool
		cbuffer_t buf;
		buf.printf( "f%s,%s", fab->get_pos().get_str(), fabname );
		tool_t *tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
		tool->set_default_param( buf );
		welt->set_tool( tool, welt->get_public_player());
		// since init always returns false, it is safe to delete immediately
		delete tool;
	}
}


/**
 * Draw new component. The values to be passed refer to the window
 * i.e. It's the screen coordinates of the window where the
 * component is displayed.
 */
void fabrik_info_t::draw(scr_coord pos, scr_size size)
{
	if (world()->closed_factories_this_month.is_contained(fab))
	{
		return;
	}

	update_components();

	// boost stuff
	boost_electric.set_transparent(fab->get_prodfactor_electric()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));
	boost_passenger.set_transparent(fab->get_prodfactor_pax()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));
	boost_mail.set_transparent(fab->get_prodfactor_mail()>0 ? 0 : TRANSPARENT50_FLAG | OUTLINE_FLAG | color_idx_to_rgb(COL_BLACK));

	//indicator_color.set_color( color_idx_to_rgb(fabrik_t::status_to_color[fab->get_status()]) );
	if (fab->get_desc()->get_building()->get_class_proportions_sum_jobs()) {
		staffing_level = fab->get_staffing_level_percentage();
		staffing_level2 = staff_shortage_factor > staffing_level ? staffing_level : 0;
	}
	lb_staff_shortage.set_visible(fab->is_staff_shortage());


	chart.update();

	gui_frame_t::draw(pos, size);
}


bool fabrik_info_t::is_weltpos()
{
	return ( welt->get_viewport()->is_on_center( get_weltpos(false) ) );
}


/**
 * This method is called if an action is triggered
 *
 * Returns true, if action is done and no more
 * components should be triggered.
 */
bool fabrik_info_t::action_triggered( gui_action_creator_t *comp, value_t)
{
	if(  comp == &input  ) {
		rename_factory();
	}
	if(  comp==&bt_access_minimap  ) {
		map_frame_t *win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		if (!win) {
			create_win(-1, -1, new map_frame_t(), w_info, magic_reliefmap);
			win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		}
		win->activate_individual_network_mode( fab->get_pos().get_2d(), false );
		top_win(win);
		return true;
	}
	return true;
}


void fabrik_info_t::map_rotate90(sint16)
{
	// force update
	old_suppliers_count ++;
	old_consumers_count ++;
	update_components();
}

// update name and buffers
void fabrik_info_t::update_info()
{
	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name(fab->get_name());
	input.set_text( fabname, lengthof(fabname) );

	update_components();
}

// update all buffers
void fabrik_info_t::update_components()
{
	// update texts
	fab->info_prod( prod_buf );
	fab->info_conn( info_buf );

	// consumers
	if(  fab->get_consumers().get_count() != old_consumers_count ) {
		cont_consumers.update_table();
		old_consumers_count = fab->get_consumers().get_count();
	}
	// suppliers
	if(  fab->get_suppliers().get_count() != old_suppliers_count ) {
		cont_suppliers.update_table();
		old_suppliers_count = fab->get_suppliers().get_count();
	}
	container_info.set_size(container_info.get_min_size());
	set_dirty();
}

/***************** Saveload stuff from here *****************/
void fabrik_info_t::rdwr( loadsave_t *file )
{
	// the factory first
	koord fabpos;
	if(  file->is_saving()  ) {
		fabpos = fab->get_pos().get_2d();
	}
	fabpos.rdwr( file );
	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	if(  file->is_loading()  ) {
		fab = fabrik_t::get_fab(fabpos );
		gebaeude_t* gb = welt->lookup_kartenboden( fabpos )->find<gebaeude_t>();

		if (fab != NULL  &&  gb != NULL) {
			init(fab, gb);
			storage.set_fab(fab);
			cont_suppliers.set_fab(fab);
			cont_consumers.set_fab(fab);
			nearby_halts.set_fab(fab);

			storage.recalc_size();
			container_details.init(gb, get_titlecolor());
			//set_tab_opened();
		}
		win_set_magic(this, (ptrdiff_t)this);
	}
	chart.rdwr(file);
	scroll_info.rdwr(file);
	switch_mode.rdwr(file);

	if(  file->is_loading()  ) {
		reset_min_windowsize();
		set_windowsize(size);
	}
}
