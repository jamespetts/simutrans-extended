/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "gui_convoi_button.h"
#include "gui_divider.h"
#include "gui_line_network.h"
#include "gui_schedule_item.h"
#include "gui_waytype_image_box.h"

#include "../../simcolor.h"
#include "../../simworld.h"
#include "../../simhalt.h"

#include "../schedule_list.h"
#include "../simwin.h"
#include "../minimap.h"
#include "../map_frame.h"
#include "../linelist_stats_t.h"

#include "../../player/simplay.h"

#include "../../tpl/minivec_tpl.h"


#define L_MAX_TRANSFER_DISP_PER_HALT ((int)(L_CONNECTION_LINES_WIDTH/5))

gui_connection_lines_t::gui_connection_lines_t()
{
	set_size(scr_size(L_CONNECTION_LINES_WIDTH, D_ENTRY_NO_HEIGHT));
}

void gui_connection_lines_t::draw(scr_coord offset)
{
	scr_coord_val row_height = lines.get_count() ? size.h/lines.get_count() : 0;
	uint32 n=0;
	scr_coord_val start_y = pos.y+offset.y+max(3,(row_height/2)-lines.get_count());
	FORX(slist_tpl<connection_line_t>, cnx_line, lines, ++n) {
		uint8 line_width = cnx_line.is_convoy ? 1 : 3;
		if (n==0) {
			start_y -= line_width;
			if (cnx_line.is_own) {
				display_fillbox_wh_clip_rgb(pos.x+offset.x, start_y, L_CONNECTION_LINES_WIDTH, line_width, cnx_line.color, true);
				start_y += line_width;
			}
			else {
				for (uint8 i = 0; i < line_width; i++) {
					display_direct_line_dotted_rgb(pos.x+offset.x, start_y, pos.x+offset.x+L_CONNECTION_LINES_WIDTH, start_y, 3, 3, cnx_line.color);
					start_y++;
				}
			}
		}
		else {
			const scr_coord_val end_y = pos.y+offset.y+row_height*(n+1)-row_height/2;
			scr_coord_val yoff=0;
			if (cnx_line.is_own) {
				display_fillbox_wh_clip_rgb(pos.x+offset.x, start_y, L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count(), line_width, cnx_line.color, true);
				display_fillbox_wh_clip_rgb(pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count(), start_y, line_width, end_y-start_y, cnx_line.color, true);
				yoff += end_y - start_y;
				display_fillbox_wh_clip_rgb(pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count(), start_y+yoff, L_CONNECTION_LINES_WIDTH-L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count(), line_width, cnx_line.color, true);
				start_y += line_width;
			}
			else {
				for (uint8 i = 0; i < line_width; i++) {
					display_direct_line_dotted_rgb(pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count(), start_y+i, pos.x+offset.x, start_y+i, 3, 3, cnx_line.color);
				}
				for (uint8 i = 0; i < line_width; i++) {
					display_direct_line_dotted_rgb(pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count()-i, start_y,
						pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count()-i, end_y,
						3, 3, cnx_line.color);
				}
				yoff += end_y - start_y;
				for (uint8 i = 0; i < line_width; i++) {
					display_direct_line_dotted_rgb(pos.x+offset.x+L_CONNECTION_LINES_WIDTH*(lines.get_count()-n)/lines.get_count()-line_width+1, start_y+yoff,
						pos.x+offset.x+L_CONNECTION_LINES_WIDTH, start_y+yoff,
						3, 3, cnx_line.color);
					start_y++;
				}
			}
		}
		start_y+=line_width;
	}
}


gui_transfer_line_t::gui_transfer_line_t(linehandle_t line, koord pos)
{
	this->line = line;
	cnv = convoihandle_t();
	transfer_pos=pos;

	init_table();

}

gui_transfer_line_t::gui_transfer_line_t(convoihandle_t cnv, koord pos)
{
	this->cnv = cnv;
	line = linehandle_t();
	transfer_pos = pos;

	init_table();
}

void gui_transfer_line_t::init_table()
{
	if( line.is_null()  &&  cnv.is_null() ) return;

	set_table_layout(6,1);
	set_spacing(scr_size(D_H_SPACE, 0));

	new_component<gui_waytype_image_box_t>(line.is_bound() ? line->get_schedule()->get_waytype() : cnv->get_schedule()->get_waytype());

	if( cnv.is_bound() ) {
		new_component<gui_convoi_button_t>(cnv);
		new_component<gui_label_t>(cnv->get_name());
	}
	else { // line
		// see action_triggered
		//bt_access_info.init(button_t::posbutton, NULL);
		//bt_access_info.set_rigid(false);
		//bt_access_info.add_listener(this);
		//add_component(&bt_access_info);
		new_component<gui_line_label_t>(line);
	}

	image_id bt_icon = skinverwaltung_t::open_window ? skinverwaltung_t::open_window->get_image_id(0) :
		IMG_EMPTY;
	if (bt_icon != IMG_EMPTY) {
		bt_access_minimap.init(button_t::imagebox, NULL);
		bt_access_minimap.set_image(bt_icon);
	}
	else {
		bt_access_minimap.init(button_t::roundbox, "access_minimap");
	}
	bt_access_minimap.set_tooltip("helptxt_access_minimap");
	bt_access_minimap.add_listener(this);
	add_component(&bt_access_minimap);
	if (skinverwaltung_t::service_frequency) {
		new_component<gui_image_t>(skinverwaltung_t::service_frequency->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Service frequency"));
	}
	add_component(&lb_service_frequency);
}

void gui_transfer_line_t::draw(scr_coord offset)
{
	if( line.is_null()  &&  cnv.is_null() ) return;

	const sint64 service_frequency = line.is_bound() ? line->get_service_frequency() : cnv->get_average_round_trip_time();
	if (service_frequency)
	{
		char as_clock[32];
		world()->sprintf_ticks(as_clock, sizeof(as_clock), service_frequency);
		lb_service_frequency.buf().printf(" %s", as_clock);
		lb_service_frequency.set_color(line->get_state() & simline_t::line_missing_scheduled_slots ? color_idx_to_rgb(COL_DARK_TURQUOISE) : SYSCOL_TEXT);
		lb_service_frequency.set_tooltip(line->get_state() & simline_t::line_missing_scheduled_slots ? translator::translate("line_missing_scheduled_slots") : "");
	}
	else {
		lb_service_frequency.buf().append("--:--");
	}
	lb_service_frequency.update();

	//bt_access_info.set_visible(line->get_owner()==world()->get_active_player());

	gui_aligned_container_t::draw(offset);
}

bool gui_transfer_line_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( comp==&bt_access_minimap ) {
		map_frame_t *win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		if (!win) {
			create_win(-1, -1, new map_frame_t(), w_info, magic_reliefmap);
			win = dynamic_cast<map_frame_t*>(win_get_magic(magic_reliefmap));
		}
		win->activate_individual_network_mode(transfer_pos);
		if (line->count_convoys() > 0) {
			minimap_t::get_instance()->set_selected_cnv(line->get_convoy(0));
		}
		else {
			minimap_t::get_instance()->set_selected_cnv(convoihandle_t());
		}
		top_win(win);
		return true;
	}
	// TODO:
	// This behavior destroys itself and can only be achieved if the line waits for a window individually (ad standard).
	/*
	if( comp==&bt_access_info ) {
		schedule_list_gui_t *sl = dynamic_cast<schedule_list_gui_t*>(win_get_magic((ptrdiff_t)(magic_line_management_t + line->get_owner()->get_player_nr())));
		if (sl) {
			sl->show_lineinfo(line);
			top_win(sl);
		}
		return true;
	}*/
	return false;
}

/*
bool gui_transfer_line_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	//access line info
	if (!swallowed  &&  ev->my > 0 && ev->my < size.h &&  line.is_bound()) {
		if ( IS_LEFTRELEASE(ev) ) {
		}
		return false;
	}
	return swallowed;
}*/


gui_line_transfer_guide_t::gui_line_transfer_guide_t(halthandle_t transfer_halt, linehandle_t line_, bool yesno, uint8 catg_index)
{
	halt = transfer_halt;
	line = line_;
	cnv  = convoihandle_t();
	filter_same_company = yesno;
	filter_catg = catg_index;
}

gui_line_transfer_guide_t::gui_line_transfer_guide_t(halthandle_t transfer_halt, convoihandle_t cnv_, bool yesno, uint8 catg_index)
{
	halt = transfer_halt;
	line = linehandle_t();
	cnv  = cnv_;
	filter_same_company = yesno;
	filter_catg = catg_index;
}

void gui_line_transfer_guide_t::update()
{
	remove_all();
	set_table_layout(2,1);
	set_spacing(scr_size(0,0));
	set_margin(scr_size(0,0), scr_size(0,3));

	cont_lines.clear();
	add_component(&cont_lines);

	const player_t *player = line.is_bound() ? line->get_owner() : cnv->get_owner();
	add_table(1,0)->set_spacing(scr_size(0,2));
	{
		for (uint32 i = 0; i < halt->registered_lines.get_count(); i++) {
			const linehandle_t connected_line = halt->registered_lines[i];

			// Exclude the same line
			if (line.is_bound() && line==connected_line) continue;
			if (cnv.is_bound() && cnv->get_line().is_bound() && cnv->get_line()==connected_line) continue;

			if( filter_catg!=goods_manager_t::INDEX_NONE  &&  !connected_line->get_goods_catg_index().is_contained(filter_catg) ) {
				continue; // filter not match
			}
			if( filter_same_company && (player!=connected_line->get_owner() ) ) {
				continue; // filter not match
			}

			bool can_transfer = false; // true if at least 1 category is match
			FOR(minivec_tpl<uint8>, const catg_index, line.is_bound() ? line->get_goods_catg_index():cnv->get_goods_catg_index()) {
				if( connected_line->get_goods_catg_index().is_contained(catg_index) ) {
					can_transfer=true;
					break;
				}
			}
			if (!can_transfer) continue;
			connection_line_t connection_line = {
				connected_line->get_line_color()==0 ? color_idx_to_rgb(connected_line->get_owner()->get_player_color1()+3) : connected_line->get_line_color(),
				(player==connected_line->get_owner()),
				false
			};
			cont_lines.append_line(connection_line);

			add_table(2,1)->set_spacing(scr_size(0,0));
			{
				new_component<gui_matching_catg_img_t>(line.is_bound() ? line->get_goods_catg_index() : cnv->get_goods_catg_index(), connected_line->get_goods_catg_index());
				new_component<gui_transfer_line_t>(connected_line, halt->get_basis_pos());
			}
			end_table();
		}

		for (uint32 i = 0; i < halt->registered_convoys.get_count(); ++i) {
			const convoihandle_t connected_cnv = halt->registered_convoys[i];
			if( cnv.is_bound() && cnv==connected_cnv ) continue;

			if( filter_catg!=goods_manager_t::INDEX_NONE  &&  !connected_cnv->get_goods_catg_index().is_contained(filter_catg) ) {
				continue; // filter not match
			}
			if( filter_same_company && (player!=connected_cnv->get_owner() ) ) {
				continue; // filter not match
			}

			bool can_transfer = false; // true if at least 1 category is match
			FOR(minivec_tpl<uint8>, const catg_index, line.is_bound() ? line->get_goods_catg_index():cnv->get_goods_catg_index()) {
				if( connected_cnv->get_goods_catg_index().is_contained(catg_index) ) {
					can_transfer=true;
					break;
				}
			}
			if (!can_transfer) continue;
			connection_line_t connection_line = {
				color_idx_to_rgb( connected_cnv->get_owner()->get_player_color1()+2 ),
				(player==connected_cnv->get_owner()),
				true
			};
			cont_lines.append_line(connection_line);

			add_table(2,1)->set_spacing(scr_size(0, 0));
			{
				new_component<gui_matching_catg_img_t>(line.is_bound() ? line->get_goods_catg_index() : cnv->get_goods_catg_index(), connected_cnv->get_goods_catg_index());
				new_component<gui_transfer_line_t>(connected_cnv, halt->get_basis_pos());
			}
			end_table();
		}
	}
	end_table();
}

void gui_line_transfer_guide_t::draw(scr_coord offset)
{
	if( halt.is_bound()  &&  (line.is_bound() || cnv.is_bound())  ) {
		const uint32 seed_temp = halt->registered_lines.get_count() + halt->registered_convoys.get_count();
		if( seed_temp != seed ) {
			seed = seed_temp;
			update();
		}
		set_size(gui_aligned_container_t::get_min_size());

		gui_aligned_container_t::draw(offset);
	}
}


gui_line_network_t::gui_line_network_t(linehandle_t line)
{
	this->line = line;
	cnv = convoihandle_t();

	if (line.is_bound()) {
		init_table();
	}
}

gui_line_network_t::gui_line_network_t(convoihandle_t cnv)
{
	this->cnv = cnv;
	line = linehandle_t();

	if (cnv.is_bound()) {
		init_table();
	}
}

void gui_line_network_t::init_table()
{
	remove_all();
	set_table_layout(5,0);
	set_alignment(ALIGN_TOP);
	set_spacing( scr_size(0,0) );
	if (!line.is_bound() && !cnv.is_bound()) return;

	const minivec_tpl<uint8> &goods_catg_index = cnv.is_bound() ? cnv->get_goods_catg_index() : line->get_goods_catg_index();
	if( goods_catg_index.get_count()==1 ) {
		// Force the filter to be disabled
		filter_catg = goods_manager_t::INDEX_NONE;
	}
	// filter controller
	new_component_span<gui_empty_t>(3);
	add_table(3,1);
	{
		if( goods_catg_index.get_count()>1 ) {
			filter_goods_catg.clear_elements();
			filter_goods_catg.set_width(LINEASCENT*20);
			filter_goods_catg.set_width_fixed(true);
			filter_goods_catg.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All freight types"), SYSCOL_TEXT);
			filter_goods_catg.set_selection(0);
			for (uint8 catg_idx = 0; catg_idx < goods_manager_t::get_max_catg_index(); catg_idx++)
			{
				if (!goods_catg_index.is_contained(catg_idx)) {
					continue;
				}
				filter_goods_catg.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(goods_manager_t::get_info_catg_index(catg_idx)->get_catg_name()), SYSCOL_TEXT, goods_manager_t::get_info_catg_index(catg_idx)->get_catg_symbol());
				if (filter_catg==catg_idx) {
					filter_goods_catg.set_selection(filter_goods_catg.count_elements()-1);
				}
			}

			filter_goods_catg.add_listener(this);
			add_component(&filter_goods_catg);
		}

		bt_own_network.init(button_t::square_state, "Show only own network"); // TODO: add translation
		bt_own_network.pressed = filter_own_network;
		bt_own_network.add_listener(this);

		add_component(&bt_own_network);

		new_component<gui_fill_t>();
	}
	end_table();
	new_component<gui_empty_t>();

	new_component_span<gui_border_t>(5);

	schedule_t *schedule = cnv.is_bound() ? cnv->get_schedule() : line->get_schedule();
	const player_t *player = line.is_bound() ? line->get_owner() : cnv->get_owner();
	uint8 entry_idx = 0;
	FORX(minivec_tpl<schedule_entry_t>, const& i, schedule->entries, ++entry_idx) {
		halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
		if (!halt.is_bound()) { continue; }

		add_table(2,1);
		{
			new_component<gui_fill_t>();
			const bool can_serve = line.is_bound() ? halt->can_serve(line) : halt->can_serve(cnv);
			gui_label_buf_t *lb = new_component<gui_label_buf_t>(can_serve ? SYSCOL_TEXT : COL_INACTIVE, gui_label_t::right);
			if (!can_serve) {
				lb->buf().printf("(%s)", halt->get_name());
			}
			else {
				lb->buf().append(halt->get_name());
			}
			lb->update();
			lb->set_fixed_width(lb->get_min_size().w);
		}
		end_table();
		new_component<gui_margin_t>(5);

		const bool is_interchange = (halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1;

		add_table(1,2)->set_spacing(scr_size(0,0));
		{
			new_component<gui_schedule_entry_number_t>(entry_idx, halt->get_owner()->get_player_color1(),
				is_interchange ? gui_schedule_entry_number_t::number_style::interchange : gui_schedule_entry_number_t::number_style::halt,
				scr_size(D_ENTRY_NO_WIDTH, max(D_POS_BUTTON_HEIGHT, D_ENTRY_NO_HEIGHT)),
				halt->get_basis_pos3d()
			);

			if( entry_idx < schedule->entries.get_count()-1 ) {
				const uint8 line_style = schedule->is_mirrored() ? gui_colored_route_bar_t::doubled : gui_colored_route_bar_t::solid;

				// Almost similar code is in times_history_container
				PIXVAL base_color;
				const uint8 temp_col_index = line.is_bound() ? line->get_line_color_index() : !cnv.is_bound() ? 255 : cnv->get_line().is_bound() ? cnv->get_line()->get_line_color_index() : 255;
				switch (temp_col_index) {
					case 254:
						base_color = color_idx_to_rgb(player->get_player_color1()+4);
						break;
					case 255:
						base_color = color_idx_to_rgb(player->get_player_color1()+2);
						break;
					default:
						base_color = line_color_idx_to_rgb(temp_col_index);
						break;
				}

				if (!is_dark_color(base_color)) {
					base_color = display_blend_colors(base_color, color_idx_to_rgb(COL_BLACK), 15);
				}

				new_component<gui_colored_route_bar_t>(base_color, line_style, true);
			}
		}
		end_table();

		if(is_interchange) {
			if (line.is_bound()) {
				new_component<gui_line_transfer_guide_t>(halt, line, filter_own_network, filter_catg);
			}
			else {
				new_component<gui_line_transfer_guide_t>(halt, cnv, filter_own_network, filter_catg);
			}
		}
		else {
			new_component<gui_empty_t>();
		}
		new_component<gui_fill_t>();
	}
}


void gui_line_network_t::draw(scr_coord offset)
{
	// The size of the component may change automatically
	// as the number of connecting routes increases or decreases.
	gui_aligned_container_t::set_size(gui_aligned_container_t::get_min_size());

	gui_aligned_container_t::draw(offset);
}

bool gui_line_network_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	if( comp==&bt_own_network ) {
		filter_own_network = !filter_own_network;
		init_table();
		return true;
	}
	if( comp==&filter_goods_catg ) {
		if (filter_goods_catg.get_selection() == 0) {
			filter_catg = goods_manager_t::INDEX_NONE;
		}
		else {
			const minivec_tpl<uint8> &goods_catg_index = cnv.is_bound() ? cnv->get_goods_catg_index() : line->get_goods_catg_index();
			uint8 count = 1;
			for (uint8 catg_index = 0; catg_index < goods_manager_t::get_max_catg_index(); catg_index++) {
				if (!goods_catg_index.is_contained(catg_index)) {
					continue;
				}
				if (count==filter_goods_catg.get_selection()) {
					filter_catg = catg_index;
					break;
				}
				count++;
			}
		}
		init_table();
		return true;
	}

	return false;
}
