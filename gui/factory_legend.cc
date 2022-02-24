/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "factory_legend.h"
#include "factorylist_frame_t.h"
#include "simwin.h"
#include "minimap.h"

#include "../simfab.h"

const char *factory_type_text[3] =
{
	"gl_prod",
	"Manufacturers",
	"gl_con"
};

const enum button_t::type factory_type_button_style[3] =
{
	button_t::roundbox_left_state, button_t::roundbox_middle_state, button_t::roundbox_right_state
};

static bool compare_factories(const factory_desc_t* const a, const factory_desc_t* const b)
{
	const bool a_producer_only = a->get_supplier_count() == 0;
	const bool b_producer_only = b->get_supplier_count() == 0;
	const bool a_consumer_only = a->get_product_count() == 0;
	const bool b_consumer_only = b->get_product_count() == 0;

	if (a_producer_only != b_producer_only) {
		return a_producer_only; // producers to the front
	}
	else if (a_consumer_only != b_consumer_only) {
		return !a_consumer_only; // consumers to the end
	}
	else {
		// both of same type, sort by name
		return strcmp(translator::translate(a->get_name()), translator::translate(b->get_name())) < 0;
	}
}

/**
 * Entries in factory legend: show color indicator + name
 */
class legend_entry_t : public gui_component_t
{
	const factory_desc_t *fac_desc;
	gui_label_t label;
	bool filtered;
public:
	legend_entry_t(const factory_desc_t *f, bool filtered_=false) : fac_desc(f), filtered(filtered_) {
		label.set_text(f->get_name());
		label.set_color(filtered ? SYSCOL_TEXT_INACTIVE : SYSCOL_TEXT);
	}

	scr_size get_min_size() const OVERRIDE
	{
		return  label.get_min_size() + scr_size(D_INDICATOR_BOX_WIDTH + D_H_SPACE, 0);
	}

	scr_size get_max_size() const OVERRIDE
	{
		return scr_size( scr_size::inf.w, label.get_max_size().h );
	}

	void draw(scr_coord offset) OVERRIDE
	{
		scr_coord pos = get_pos() + offset;
		if (!filtered) {
			display_ddd_box_clip_rgb( pos.x, pos.y+D_GET_CENTER_ALIGN_OFFSET(D_INDICATOR_BOX_HEIGHT,LINESPACE)-1, D_INDICATOR_BOX_WIDTH, D_INDICATOR_HEIGHT+2, SYSCOL_TEXT, SYSCOL_TEXT );
		}
		display_fillbox_wh_clip_rgb( pos.x+1, pos.y+D_GET_CENTER_ALIGN_OFFSET(D_INDICATOR_BOX_HEIGHT,LINESPACE), D_INDICATOR_BOX_WIDTH-2, D_INDICATOR_BOX_HEIGHT, fac_desc->get_color(), false );
		label.draw( pos+scr_size(D_INDICATOR_BOX_WIDTH+D_H_SPACE,0) );
	}

	bool infowin_event(const event_t *ev) OVERRIDE
	{
		if (IS_RIGHTRELEASE(ev)) {
			factorylist_frame_t *win = dynamic_cast<factorylist_frame_t*>(win_get_magic(magic_factorylist));
			if (!win) {
				create_win(-1, -1, new factorylist_frame_t(), w_info, magic_factorylist);
				win = dynamic_cast<factorylist_frame_t*>(win_get_magic(magic_factorylist));
			}
			win->set_text_filter( translator::translate(fac_desc->get_name()) );
			top_win(win);
			return true;
		}
		return gui_component_t::infowin_event(ev);
	}
};


factory_legend_t::factory_legend_t(map_frame_t *parent) :
	gui_frame_t(translator::translate("Factory legend"), NULL),
	main_frame(parent),
	lb_input(translator::translate("Verbrauch")),
	lb_output(translator::translate("Produktion")),
	scrolly_left(&cont_left),
	scrolly_right(&cont_right)
{
	set_table_layout(1,0);
	set_margin(scr_size(D_MARGIN_LEFT,D_V_SPACE), scr_size(0,0));
	add_table(2,1);
	{
		bt_open_2nd_column.init(button_t::roundbox_state, double_column ? "-" : "+");
		bt_open_2nd_column.set_width(display_get_char_width('+') + D_BUTTON_PADDINGS_X);
		bt_open_2nd_column.add_listener(this);
		add_component(&bt_open_2nd_column);

		cont_filter_buttons.set_table_layout(3,1);
		cont_filter_buttons.set_spacing( scr_size(0,0) );
		cont_filter_buttons.set_rigid(false);
		cont_filter_buttons.set_visible(false);
		for (uint8 i=0; i<MAX_IND_TYPE; i++) {
			filter_buttons[i].init(factory_type_button_style[i], factory_type_text[i]);
			filter_buttons[i].add_listener(this);
			filter_buttons[i].pressed = true;
			cont_filter_buttons.add_component(&filter_buttons[i]);
		}
		add_component(&cont_filter_buttons);
	}
	end_table();

	hide_mismatch.init(button_t::square_state, "Hide mismatch");
	hide_mismatch.add_listener(this);
	hide_mismatch.pressed = false;
	add_component(&hide_mismatch);

	add_table(2,2)->set_alignment(ALIGN_TOP);
	lb_input.set_rigid(false);
	lb_input.set_visible(false);
	lb_output.set_rigid(false);
	lb_output.set_visible(false);
	lb_input.set_fixed_width(lb_input.get_size().w);
	lb_output.set_fixed_width(lb_output.get_size().w);
	if (skinverwaltung_t::input_output) {
		lb_input.set_image(  skinverwaltung_t::input_output->get_image_id(0) );
		lb_output.set_image( skinverwaltung_t::input_output->get_image_id(1) );
	}
	add_component(&lb_output);
	add_component(&lb_input);

	cont_left.set_table_layout(1,0);
	cont_left.set_spacing(scr_size(0,0));
	cont_left.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_ARROW_UP_WIDTH+D_H_SPACE, D_ARROW_UP_WIDTH));
	cont_right.set_table_layout(1,0);
	cont_right.set_spacing(scr_size(0,0));
	cont_right.set_margin(scr_size(D_H_SPACE,D_V_SPACE), scr_size(D_ARROW_UP_WIDTH+D_H_SPACE, D_ARROW_UP_WIDTH));
	cont_right.set_rigid(false);
	cont_right.set_visible(false);
	scrolly_left.set_maximize(true);
	scrolly_right.set_maximize(true);
	add_component(&scrolly_left);
	scrolly_right.set_rigid(false);
	scrolly_right.set_visible(false);
	add_component(&scrolly_right);
	end_table();

	update_factory_legend();
	set_windowsize(scr_size(get_min_windowsize().w, min(main_frame->get_windowsize().h, cont_left.get_client().h-cont_left.get_pos().y)));
	set_resizemode(vertical_resize);
}


void factory_legend_t::update_factory_legend()
{
	cont_left.remove_all();
	cont_right.remove_all();

	vector_tpl<const factory_desc_t*> factory_types;
	// generate list of factory types
	FOR(vector_tpl<fabrik_t*>, const f, world()->get_fab_list()) {
		if (f->get_desc()->get_distribution_weight() > 0) {
			factory_types.insert_unique_ordered(f->get_desc(), compare_factories);
		}
	}

	// add corresponding legend entries
	bool filter_by_catg = (minimap_t::get_instance()->freight_type_group_index_showed_on_map != nullptr && minimap_t::get_instance()->freight_type_group_index_showed_on_map != goods_manager_t::none);
	PIXVAL prev_color = NULL;
	const char *prev_name = {};
	FOR(vector_tpl<const factory_desc_t*>, f, factory_types) {
		if (prev_name && !strcmp(translator::translate(f->get_name()), prev_name) && f->get_color()==prev_color) {
			continue;
		}

		// type filter
		if (!filter_buttons[PRODUCER].pressed && !f->get_supplier_count() && f->get_product_count()) {
			continue;
		}
		if (!filter_buttons[MANUFACTURER].pressed && f->get_supplier_count() && f->get_product_count()) {
			continue;
		}
		if (!filter_buttons[CONSUMER].pressed && f->get_supplier_count() && !f->get_product_count()) {
			continue;
		}

		// category check
		if (double_column) {
			// left=output
			if (!old_catg_index || (f->get_product_count() && !(hide_mismatch.pressed && filter_by_catg && !f->has_goods_catg_demand(old_catg_index, 2)))) {
				cont_left.new_component<legend_entry_t>(f, (filter_by_catg && !f->has_goods_catg_demand(old_catg_index,2)));
			}

			// right=input
			if (!old_catg_index || (f->get_supplier_count() && !(hide_mismatch.pressed && filter_by_catg && !f->has_goods_catg_demand(old_catg_index,1)))) {
				cont_right.new_component<legend_entry_t>(f, (filter_by_catg && !f->has_goods_catg_demand(old_catg_index,1)));
			}
		}
		else if ( !old_catg_index || !filter_by_catg || (f->has_goods_catg_demand(old_catg_index) || (!f->has_goods_catg_demand(old_catg_index) && filter_by_catg && !hide_mismatch.pressed))) {
			cont_left.new_component<legend_entry_t>( f, (filter_by_catg &&!f->has_goods_catg_demand(old_catg_index)));
		}
		prev_color = f->get_color();
		prev_name = translator::translate(f->get_name());
	}

	cont_left.set_size(cont_left.get_min_size());
	reset_min_windowsize();
	set_windowsize(scr_size(get_min_windowsize().w, get_windowsize().h));
	resize(scr_coord(0,0));
}


bool factory_legend_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if( comp==&bt_open_2nd_column ) {
		bt_open_2nd_column.pressed = !bt_open_2nd_column.pressed;
		double_column = bt_open_2nd_column.pressed;
		bt_open_2nd_column.set_text(double_column ? "-" : "+");
		cont_filter_buttons.set_visible(double_column);
		scrolly_right.set_visible(double_column);
		lb_input.set_visible(double_column);
		lb_output.set_visible(double_column);
		update_factory_legend();
		return true;
	}
	else if( comp==&hide_mismatch ) {
		hide_mismatch.pressed = !hide_mismatch.pressed;
		update_factory_legend();
		return true;
	}
	else {
		for (int i = 0; i < MAX_IND_TYPE; i++) {
			if (comp == filter_buttons + i) {
				filter_buttons[i].pressed ^= 1;

				if (!filter_buttons[0].pressed && !filter_buttons[1].pressed && !filter_buttons[2].pressed) {
					filter_buttons[0].pressed = true;
					filter_buttons[1].pressed = true;
					filter_buttons[2].pressed = true;
				}
				update_factory_legend();
				return true;
			}
		}
	}
	return false;
}

void factory_legend_t::draw(scr_coord pos, scr_size size)
{
	if (!main_frame || !win_get_magic(magic_reliefmap) ) {
		destroy_win(this);
	}

	if (minimap_t::get_instance()->freight_type_group_index_showed_on_map) {
		uint8 temp_index = minimap_t::get_instance()->freight_type_group_index_showed_on_map->get_catg_index();
		if (minimap_t::get_instance()->freight_type_group_index_showed_on_map == goods_manager_t::none) {
			//temp_index = 255;
			temp_index = goods_manager_t::INDEX_NONE;
		}
		if (temp_index != old_catg_index) {
			old_catg_index = temp_index;
			update_factory_legend();
		}
	}
	else if (old_catg_index != goods_manager_t::INDEX_NONE) {
		old_catg_index = goods_manager_t::INDEX_NONE;
		update_factory_legend();
	}

	gui_frame_t::draw(pos, size);
}
