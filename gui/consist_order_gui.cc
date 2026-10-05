/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "consist_order_gui.h"
#include "convoi_detail_t.h"
#include "convoy_item.h"
#include "simwin.h"
#include "messagebox.h"
#include "vehicle_detail.h"
#include "../bauer/goods_manager.h"
#include "../bauer/vehikelbauer.h"
#include "../display/viewport.h"
#include "../descriptor/goods_desc.h"
#include "../simhalt.h"
#include "../simconvoi.h"
#include "../simdepot.h"
#include "../simworld.h"
#include "../vehicle/vehicle.h"
#include "components/gui_convoy_assembler.h"
#include "components/gui_divider.h"
#include "components/gui_table.h"
#include "components/gui_textarea.h"
#include "components/gui_waytype_image_box.h"
#include "../player/finance.h"


#define L_OWN_VEHICLE_COUNT_WIDTH (proportional_string_width("8,888") + D_H_SPACE)
#define L_OWN_VEHICLE_LABEL_OFFSET_LEFT (L_OWN_VEHICLE_COUNT_WIDTH + VEHICLE_BAR_HEIGHT*4+D_H_SPACE)

int vehicle_scrollitem_t::sort_mode = 0;
bool vehicle_scrollitem_t::sortreverse = false;

scr_size consist_list_t::get_min_size() const
{
	scr_size base = gui_scrolled_list_t::get_min_size();
	scr_coord_val wide = 0, tall = 0;
	for(  sint32 i = 0;  i < get_count();  i++  ) {
		if(  const scrollitem_t* const item = get_element(i)  ) {
			const scr_size m = item->get_min_size();
			scr_coord_val w = m.w;
			// Editable items report a fixed D_BUTTON_WIDTH instead of their
			// text width (e.g. convoy names); floor at the real text width.
			if(  const char* const t = item->get_text()  ) {
				w = max(w, (scr_coord_val)(2 * D_H_SPACE + display_calc_proportional_string_len_width(t, strlen(t))));
			}
			wide = max(wide, w);
			tall = max(tall, m.h);
		}
	}
	if(  wide > 0  ) {
		// Fit the widest item plus the list's own chrome: container side
		// margins and the vertical scrollbar, which is normally visible.
		wide += 2 * D_H_SPACE + D_SCROLLBAR_WIDTH;
	}
	else {
		// Empty list: keep a sane absolute floor so the column never collapses.
		wide = D_BUTTON_WIDTH * 2;
	}
	base.w = max(base.w, wide);
	if(  tall > 0  ) {
		base.h = max(base.h, min_rows * tall);
	}
	return base;
}


void consist_list_t::refresh()
{
	sort(0);
	// Row breathing room: top margin above the first row plus a small gap
	// between rows. sort() via reset_container_size() zeroes both.
	container.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_H_SPACE, 0));
	container.set_spacing(scr_size(D_H_SPACE, D_V_SPACE / 2));
	container.set_size(container.get_min_size());
	set_scroll_position(0, 0);
}


// Consist-copier list rows: identical to convoy rows except the empty-list
// placeholder, which reads "Consist not found" here instead of the shared
// "convoy not found" text used by the depot and line windows.
class consist_scrollitem_t : public convoy_scrollitem_t
{
public:
	consist_scrollitem_t(convoihandle_t c = convoihandle_t(), bool auto_text_color = false) : convoy_scrollitem_t(c, auto_text_color) { }
	char const* get_text() const OVERRIDE;
};


char const* consist_scrollitem_t::get_text() const
{
	const convoihandle_t cnv = get_convoy();
	return cnv.is_bound() ? cnv->get_name() : "Consist not found";
}

// sort option for Consist Copier
// TODO: add sort option
static const char *cc_sort_text[1] = {
	"Name"
};

// sort option for Vehicle Picker
static const char *vp_sort_text[vehicle_scrollitem_t::SORT_MODES] = {
	"Name",
	"by_own",
	"Leistung",
	"cl_btn_sort_max_speed",
	"Intro. date",
	"Role"
};


vehicle_scrollitem_t::vehicle_scrollitem_t(own_vehicle_t own_veh_)
	: gui_label_t(own_veh_.veh_type == nullptr ? "Vehicle not found" : own_veh_.veh_type->get_name(), SYSCOL_TEXT)
{
	own_veh = own_veh_;

	set_focusable(true);
	focused = false;
	selected = false;

	if( own_veh.veh_type!=nullptr ) {
		label.buf().printf("%5u", own_veh.count);
		label.update();
		label.set_fixed_width(proportional_string_width("8,888"));

		// vehicle color bar
		uint16 month_now = world()->get_current_month();
		colorbar.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar_edge.set_flags(own_veh.veh_type->get_basic_constraint_prev(), own_veh.veh_type->get_basic_constraint_next(), own_veh.veh_type->get_interactivity());
		colorbar.init(own_veh.veh_type->get_vehicle_status_color());
		colorbar_edge.init(SYSCOL_LIST_TEXT_SELECTED_FOCUS, colorbar.get_size()+scr_size(2,2));
	}
	else {
		set_color(SYSCOL_EDIT_TEXT_DISABLED);
	}
}


scr_size vehicle_scrollitem_t::get_min_size() const
{
	return scr_size(L_OWN_VEHICLE_LABEL_OFFSET_LEFT + gui_label_t::get_min_size().w,LINESPACE);
}


void vehicle_scrollitem_t::draw(scr_coord offset)
{
	scr_coord_val left = D_H_SPACE;
	if( own_veh.veh_type!=nullptr ) {
		if (selected) {
			display_fillbox_wh_clip_rgb(pos.x+offset.x, pos.y+offset.y, get_size().w, get_size().h+1, (focused ? SYSCOL_LIST_BACKGROUND_SELECTED_F : SYSCOL_LIST_BACKGROUND_SELECTED_NF), true);
			color = SYSCOL_LIST_TEXT_SELECTED_FOCUS;
			label.set_color(SYSCOL_LIST_TEXT_SELECTED_FOCUS);
			colorbar_edge.draw(pos + offset + scr_coord(L_OWN_VEHICLE_COUNT_WIDTH-1, D_GET_CENTER_ALIGN_OFFSET(colorbar.get_size().h, get_size().h)-1));
		}
		else {
			color = SYSCOL_TEXT;
			label.set_color(SYSCOL_TEXT);
		}

		label.draw(pos+offset + scr_coord(left,0));
		colorbar.draw(pos+offset + scr_coord(L_OWN_VEHICLE_COUNT_WIDTH, D_GET_CENTER_ALIGN_OFFSET(colorbar.get_size().h, get_size().h)));
		left = L_OWN_VEHICLE_LABEL_OFFSET_LEFT;
	}
	gui_label_t::draw(offset+scr_coord(left,0));
}


bool vehicle_scrollitem_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	const vehicle_scrollitem_t *a = dynamic_cast<const vehicle_scrollitem_t*>(aa);
	const vehicle_scrollitem_t *b = dynamic_cast<const vehicle_scrollitem_t*>(bb);
	int cmp = 0;
	switch (sort_mode) {
		case by_own:
			cmp = a->own_veh.count - b->own_veh.count;
			return sortreverse ? cmp > 0 : cmp < 0;
		case by_power:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_power);
			break;
		case by_max_speed:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_speed);
			break;
		case by_intro_date:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_intro_date);
			break;
		case by_role:
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_role);
			break;
		default: // by_name
			cmp = vehicle_builder_t::compare_vehicles(a->get_vehicle(), b->get_vehicle(), (vehicle_builder_t::sort_mode_t)vehicle_builder_t::sb_name);
			break;
	}

	return sortreverse ? cmp <= 0 : cmp > 0;
}



bool gui_vehicle_element_list_t::infowin_event(const event_t *ev)
{
	int sel_index = index_at(scr_coord(0,0) - pos, ev->mouse_pos.x, ev->mouse_pos.y);
	if( sel_index != -1 ) {
		if( (IS_LEFTCLICK(ev) || IS_LEFTDBLCLK(ev)) || (IS_RIGHTCLICK(ev) || IS_RIGHTDBLCLK(ev))) {
			value_t p;
			p.i = (IS_LEFTCLICK(ev) || IS_LEFTDBLCLK(ev)) ? sel_index : -(++sel_index);
			call_listeners(p);
			return true;
		}
	}
	return false;
}


gui_consist_order_shifter_t::gui_consist_order_shifter_t(consist_order_t *order_, uint32 index)
{
	order = order_;
	slot_index = index;

	//set_margin(scr_size(D_H_SPACE,D_V_SPACE), scr_size(D_H_SPACE, 0));
	set_table_layout(3,1);
	set_spacing(scr_size(0, D_V_SPACE));

	bt_forward.init(button_t::roundbox_left, "<");
	bt_forward.enable(slot_index > 0);
	bt_forward.add_listener(this);
	add_component(&bt_forward);

	bt_remove.init(button_t::box, "X");
	bt_remove.background_color=color_idx_to_rgb(COL_RED);
	bt_remove.set_tooltip(translator::translate("Remove this slot from the consist order."));
	bt_remove.add_listener(this);
	add_component(&bt_remove);

	bt_backward.init(button_t::roundbox_right, ">");
	bt_backward.enable(slot_index < order->get_count()-1);
	bt_backward.add_listener(this);
	add_component(&bt_backward);
}

bool gui_consist_order_shifter_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if (slot_index >= order->get_count()) return false;

	if (comp == &bt_remove) {
		order->remove_order(slot_index);
	}
	if (comp == &bt_forward && slot_index > 0) {
		order->move_element(slot_index, slot_index - 1);
	}
	if (comp == &bt_backward) {
		// move_element ignores the out-of-range target when already last
		order->move_element(slot_index, slot_index + 1);
	}
	return true;
}


// Compact one-line description of a rule-based alternative for the slot list.
static void rule_summary(cbuffer_t &buf, uint32 number, const vehicle_description_element &rule, uint8 catg)
{
	buf.clear();
	buf.printf("%s %u (", translator::translate("Rule"), number);
	if (rule.engine_type >= 10) {
		buf.append(translator::translate("Any"));
	}
	else {
		buf.append(translator::translate(vehicle_builder_t::engine_type_names[rule.engine_type + 1]));
	}
	if (rule.must_carry_class) {
		if ((catg == goods_manager_t::INDEX_PAS || catg == goods_manager_t::INDEX_MAIL)
			&& rule.must_carry_class < goods_manager_t::get_classes_catg_index(catg)) {
			const char *name = goods_manager_t::get_translated_wealth_name(catg, rule.must_carry_class);
			if (name && name[0]) {
				buf.append(", ");
				buf.append(name);
			}
			else {
				buf.printf(", class %u+", rule.must_carry_class);
			}
		}
		else {
			buf.printf(", class %u+", rule.must_carry_class);
		}
	}
	uint32 limits = 0;
	if (rule.min_catering || rule.max_catering != 255) limits++;
	if (rule.min_range || rule.max_range != UINT32_MAX_VALUE) limits++;
	if (rule.min_brake_force || rule.max_brake_force != 65535) limits++;
	if (rule.min_power || rule.max_power != UINT32_MAX_VALUE) limits++;
	if (rule.min_tractive_effort || rule.max_tractive_effort != UINT32_MAX_VALUE) limits++;
	if (rule.min_topspeed || rule.max_topspeed != UINT32_MAX_VALUE) limits++;
	if (rule.min_weight || rule.max_weight != UINT32_MAX_VALUE) limits++;
	if (rule.min_axle_load || rule.max_axle_load != UINT32_MAX_VALUE) limits++;
	if (rule.min_capacity || rule.max_capacity != 65535) limits++;
	if (rule.min_running_cost || rule.max_running_cost != UINT32_MAX_VALUE) limits++;
	if (rule.min_fixed_cost || rule.max_fixed_cost != UINT32_MAX_VALUE) limits++;
	if (rule.min_fuel_per_km || rule.max_fuel_per_km != UINT32_MAX_VALUE) limits++;
	if (rule.min_staff_hundredths || rule.max_staff_hundredths != UINT32_MAX_VALUE) limits++;
	if (rule.min_drivers || rule.max_drivers != UINT32_MAX_VALUE) limits++;
	if (limits) {
		buf.printf(", %u limits", limits);
	}
	else {
		buf.append(translator::translate(", match-all"));
	}
	if (rule.empty) {
		buf.append(translator::translate(", may be empty"));
	}
	buf.append(")");
}


gui_vehicle_description_element_t::gui_vehicle_description_element_t(consist_order_t *order_, uint32 index, waytype_t wt) :
	vde(&vde_vec),
	scrolly(&vde, false, true)
{
	order = order_;
	slot_index = index;
	way_type = wt;

	if (slot_index < order->get_count()) {
		set_table_layout(1,0);
		set_alignment(ALIGN_TOP);

		// Slot-level "may be empty" toggle (A3). The pre-rework widget was
		// per-alternative, but this widget is per-slot, so the toggle reads
		// and writes the empty flag on every alternative in the slot.
		// square_automatic flips pressed itself; the handler only stores it.
		bt_can_empty.init(button_t::square_automatic, "This slot may be empty");
		bt_can_empty.set_tooltip(translator::translate("Allow this slot to be left empty when the consist is assembled."));
		bt_can_empty.add_listener(this);
		add_component(&bt_can_empty);
		// Category label indented like the slot contents.
		{
			gui_aligned_container_t *catg_row = add_table(2,1);
			catg_row->set_alignment(ALIGN_TOP | ALIGN_CENTER_V);
			new_component<gui_margin_t>(D_H_SPACE, 0);
			catg_row->add_component(&lb_catg);
			end_table();
		}

		// constraints check indicator
		gui_aligned_container_t *tbl = add_table(2,1);
		tbl->set_spacing(scr_size(0, 0));
		tbl->set_force_equal_columns(true);
		{
			state_prev.set_show_frame(false);
			state_next.set_show_frame(false);
			add_component(&state_prev);
			add_component(&state_next);
		}
		end_table();
		const scr_coord grid = gui_convoy_assembler_t::get_grid(way_type);
		vde.set_grid(grid);
		vde.set_placement(gui_convoy_assembler_t::get_placement(way_type));
		vde.set_player_nr(world()->get_active_player_nr());
		vde.set_max_width(grid.x+ 2 * gui_image_list_t::BORDER);
		vde.add_listener(this);

		scrolly.set_maximize(true);
		// image list height
		scrolly.set_min_height(grid.y+D_H_SPACE*2);
		gui_aligned_container_t *tbl_orders = add_table(1,2);
		tbl_orders->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
		tbl_orders->set_table_frame(false,true);
		tbl_orders->set_margin(scr_size(0,0), scr_size(D_SCROLLBAR_WIDTH,0));
		{
			add_component(&scrolly);
			new_component<gui_fill_t>(false, true);
		}
		end_table();

		// Rule alternatives are indented under the slot like its images, so
		// wrap them (header + list) in a container with a left margin.
		gui_aligned_container_t *rule_wrap = add_table(1,0);
		rule_wrap->set_table_layout(1,0);
		rule_wrap->set_margin(scr_size(D_H_SPACE,0), scr_size(0,0));
		rule_wrap->set_alignment(ALIGN_TOP);
		{
			rule_header = rule_wrap->new_component<gui_label_t>("Rule-based alternatives:");
			rule_table = rule_wrap->new_component<gui_aligned_container_t>();
			rule_table->set_table_layout(1,0);
			rule_table->set_alignment(ALIGN_TOP);
		}
		end_table();

		bt_add_rule.init(button_t::roundbox, "Add rule");
		bt_add_rule.set_tooltip(translator::translate("Add a rule-based alternative to this slot"));
		bt_add_rule.add_listener(this);
		add_component(&bt_add_rule);

		update();
	}
}

void gui_vehicle_description_element_t::update()
{
	clear_ptr_vector(vde_vec);
	if (slot_index < order->get_count()) {
		//uint16 livery_scheme_index = world()->get_player(player_nr)->get_favorite_livery_scheme_index((uint8)simline_t::waytype_to_linetype(way_type));
		consist_order_element_t elem = order->access_order(slot_index);
		old_count = elem.get_count();
		// Slot-level display: checked iff every alternative allows empty.
		// Individual alternatives are edited in the rule editor window.
		if (old_count == 0) {
			bt_can_empty.pressed = false;
			bt_can_empty.disable();
		}
		else {
			bt_can_empty.enable();
			bool all_empty = true;
			for (uint32 i = 0; i < old_count; i++) {
				if (!elem.get_vehicle_description(i).empty) {
					all_empty = false;
					break;
				}
			}
			bt_can_empty.pressed = all_empty;
		}

		// Slot goods category (replaces the dead btn_goods key, which is in no translation file).
		lb_catg.buf().clear();
		const uint8 slot_catg = elem.get_catg_index();
		if (slot_catg != goods_manager_t::INDEX_NONE && slot_catg < goods_manager_t::get_max_catg_index()) {
			lb_catg.buf().printf("%s: %s", translator::translate("Category"), translator::translate(goods_manager_t::get_info_catg_index(slot_catg)->get_catg_name()));
		}
		else {
			lb_catg.buf().printf("%s: %s", translator::translate("Category"), translator::translate("none"));
		}
		lb_catg.update();

		// update images
		uint32 specifics = 0;
		for (uint8 i = 0; i < elem.get_count(); i++) {
			if (const vehicle_desc_t* veh_type = elem.get_vehicle_description(i).specific_vehicle) {
				specifics++;
				gui_image_list_t::image_data_t* img_data = new gui_image_list_t::image_data_t(veh_type->get_name(), veh_type->get_base_image());
				// The vehicle state bar color here is determined only by the timeline
				const PIXVAL state_col = veh_type->get_vehicle_status_color();
				img_data->lcolor = state_col;
				img_data->rcolor = state_col;
				img_data->basic_coupling_constraint_prev = veh_type->get_basic_constraint_prev();
				img_data->basic_coupling_constraint_next = veh_type->get_basic_constraint_next();
				img_data->interactivity = veh_type->get_interactivity();
				vde_vec.append(img_data);
			}
			// Rule-based alternatives (specific_vehicle == nullptr) are listed below, not as images.
		}

		// rebuild the rule-alternative rows
		rule_table->remove_all();
		rule_alt_indices.clear();
		rule_edit_buttons.clear();
		rule_remove_buttons.clear();
		scrolly.set_visible(specifics > 0);
		uint32 rule_no = 0;
		for (uint32 i = 0; i < elem.get_count(); i++) {
			if (elem.get_vehicle_description(i).specific_vehicle) {
				continue;
			}
			rule_alt_indices.append(i);
			rule_table->add_table(3,1);
			{
				cbuffer_t summary;
				rule_summary(summary, ++rule_no, elem.get_vehicle_description(i), elem.get_catg_index());
				gui_label_buf_t *lb = rule_table->new_component<gui_label_buf_t>();
				lb->buf().append(summary);
				lb->update();
				rule_table->add_component(lb);

				button_t *bt_edit = rule_table->new_component<button_t>();
				bt_edit->init(button_t::roundbox, "Edit");
				bt_edit->set_tooltip(translator::translate("Edit this rule alternative"));
				bt_edit->add_listener(this);
				rule_table->add_component(bt_edit);
				rule_edit_buttons.append(bt_edit);

				button_t *bt_del = rule_table->new_component<button_t>();
				bt_del->init(button_t::roundbox, "Remove");
				bt_del->set_tooltip(translator::translate("Remove this rule alternative from the slot"));
				bt_del->add_listener(this);
				rule_table->add_component(bt_del);
				rule_remove_buttons.append(bt_del);
			}
			rule_table->end_table();
		}
		rule_table->set_size(rule_table->get_min_size());
		rule_header->set_visible(!rule_alt_indices.empty());

		// update connection statuses
		state_prev.set_color(order->get_constraint_state_color(slot_index, false));
		state_next.set_color(order->get_constraint_state_color(slot_index));

	}
	set_size(get_min_size());
}

void gui_vehicle_description_element_t::show_vehicle_detail(uint32 index)
{
	if (slot_index < order->get_count()) {
		consist_order_element_t elem = order->access_order(slot_index);
		if (const vehicle_desc_t* veh_type = elem.get_vehicle_description(index).specific_vehicle) {
			vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail_for_consist_order));
			if (!win) {
				create_win(new vehicle_detail_t(veh_type), w_info, magic_vehicle_detail_for_consist_order);
			}
			else {
				win->set_vehicle(veh_type);
				top_win(win, false);
			}
		}
	}
}


void gui_vehicle_description_element_t::open_rule_editor(uint32 alt_index)
{
	consist_rule_editor_t *win = dynamic_cast<consist_rule_editor_t*>(win_get_magic(magic_consist_rule_editor));
	if (!win) {
		create_win(new consist_rule_editor_t(world()->get_active_player(), order, slot_index, alt_index, way_type), w_info, magic_consist_rule_editor);
	}
	else {
		win->retarget(world()->get_active_player(), order, slot_index, alt_index, way_type);
		top_win(win, false);
	}
}

void gui_vehicle_description_element_t::draw(scr_coord offset)
{
	if (slot_index < order->get_count()) {
		consist_order_element_t elem = order->access_order(slot_index);
		if (elem.get_count() != old_count) {
			update();
		}
		gui_aligned_container_t::draw(offset);
	}
}

bool gui_vehicle_description_element_t::action_triggered(gui_action_creator_t *comp, value_t p)
{
	if (order->get_count() <= slot_index) { return false; }

	if (comp == &bt_can_empty) {
		// square_automatic has already flipped pressed; store it slot-wide.
		consist_order_element_t &elem = order->access_order(slot_index);
		for (uint32 i = 0; i < elem.get_count(); i++) {
			elem.access_vehicle_description(i).set_empty(bt_can_empty.pressed);
		}
	}
	else if (comp == &bt_add_rule) {
		consist_order_element_t &elem = order->access_order(slot_index);
		elem.append_rule();
		order->touch();
		open_rule_editor(elem.get_count() - 1);
	}
	else if (comp == &vde && slot_index < order->get_count()) {
		if (p.i<0) {
			uint32 index = -1 - p.i;
			consist_order_element_t &elem = order->access_order(slot_index);
			if(index<elem.get_count()){
				elem.remove_vehicle_description_at(index);
			}
		}
		else {
			show_vehicle_detail((uint32)p.i);
		}
	}
	else {
		for (uint32 k = 0; k < rule_edit_buttons.get_count(); k++) {
			if (comp == rule_edit_buttons[k]) {
				open_rule_editor(rule_alt_indices[k]);
				break;
			}
			if (comp == rule_remove_buttons[k]) {
				consist_order_element_t &elem = order->access_order(slot_index);
				elem.remove_vehicle_description_at(rule_alt_indices[k]);
				order->touch();
				break;
			}
		}
	}
	return true;
}


cont_order_overview_t::cont_order_overview_t(consist_order_t *order, waytype_t wt)
{
	this->order = order;
	way_type = wt;
	init_table();
}


// Number inputs hold sint32 only, and every line's editable range is bounded
// well below that (see rule_line_defs::edit_max), so no sentinel is displayed.
static sint32 show_u32_bound(uint32 v)
{
	return v >= (uint32)SINT32_MAX_VALUE ? SINT32_MAX_VALUE : (sint32)v;
}


// One checkbox row per constrainable line in the rule editor, in
// consist_rule_editor_t::bt_use/num_min/num_max order.
enum rule_line_index {
	line_class = 0,
	line_catering,
	line_range,
	line_brake,
	line_power,
	line_effort,
	line_speed,
	line_weight,
	line_axle,
	line_capacity,
	line_runcost,
	line_fixcost,
	line_fuel,
	line_staff,
	line_drivers
};

struct rule_line_def_t {
	const char *name;
	uint32 type_max;    // value meaning "no limit" in the data
	uint32 edit_max;    // largest value offered in the number inputs
	uint32 default_min; // finite range installed when a line is ticked
	uint32 default_max;
};

// Finite defaults are arbitrary starting points for newly checked lines,
// per user request; the user narrows them as needed.
static const rule_line_def_t rule_line_defs[consist_rule_editor_t::LINE_COUNT] = {
	{ "Minimum class carried", 255, 255, 1, 1 },
	{ "Catering level", 5, 5, 1, 5 },
	{ "Range (km)", UINT32_MAX_VALUE, 999999, 0, 1000 },
	{ "Brake force (kN)", 65535, 65535, 0, 500 },
	{ "Power (kW)", UINT32_MAX_VALUE, 999999, 0, 2000 },
	{ "Tractive effort (kN)", UINT32_MAX_VALUE, 999999, 0, 500 },
	{ "Top speed (km/h)", UINT32_MAX_VALUE, 9999, 0, 200 },
	{ "Weight (t)", UINT32_MAX_VALUE, 99999, 0, 1000 },
	{ "Axle load (t)", UINT32_MAX_VALUE, 9999, 0, 30 },
	{ "Capacity", 65535, 65535, 0, 200 },
	{ "Running cost", UINT32_MAX_VALUE, 99999999, 0, 1000000 },
	{ "Fixed cost", UINT32_MAX_VALUE, 99999999, 0, 1000000 },
	{ "Fuel per km", UINT32_MAX_VALUE, 9999999, 0, 100000 },
	{ "Staff", UINT32_MAX_VALUE, 999999, 0, 10000 },
	{ "Drivers", UINT32_MAX_VALUE, 999, 0, 10 },
};


consist_rule_editor_t::consist_rule_editor_t(player_t *player_, consist_order_t *order_, uint32 slot, uint32 alt, waytype_t wt) :
	gui_frame_t(translator::translate("Set vehicle by rules"))
{
	player = player_;
	set_owner(player);

	set_table_layout(1,0);
	set_alignment(ALIGN_TOP);

	add_table(2,1);
	{
		new_component<gui_label_t>("Traction type");
		// Index 0 is "any" (matcher wildcard); the rest follow the engine enum.
		engine_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Any"), SYSCOL_TEXT);
		for (uint8 i = 0; i < 10; i++) {
			engine_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[i + 1]), SYSCOL_TEXT);
		}
		engine_selector.add_listener(this);
		add_component(&engine_selector);
	}
	end_table();

	add_table(2,1);
	{
		new_component<gui_label_t>("Goods category");
		catg_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Any"), SYSCOL_TEXT);
		catg_values.append(consist_order_element_t::any_catg_index);
		catg_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("none"), SYSCOL_TEXT);
		catg_values.append(goods_manager_t::INDEX_NONE);
		for (uint8 i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
			if (i == goods_manager_t::INDEX_NONE) {
				continue;
			}
			const goods_desc_t* info = goods_manager_t::get_info_catg_index(i);
			catg_selector.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(info->get_catg_name()), SYSCOL_TEXT, info->get_catg_symbol());
			catg_values.append(i);
		}
		catg_selector.add_listener(this);
		add_component(&catg_selector);
	}
	end_table();

	bt_empty.init(button_t::square_automatic, "This alternative may be empty");
	bt_empty.set_tooltip(translator::translate("Allow this alternative to be left empty when the consist is assembled."));
	bt_empty.add_listener(this);
	add_component(&bt_empty);

	add_table(2,1);
	{
		new_component<gui_label_t>("Minimum class carried");
		class_selector.add_listener(this);
		add_component(&class_selector);
	}
	end_table();

	// One checkbox row per constrainable line. Unticked: the inputs are replaced
	// by a greyed "any", so no sentinel ("unlimited") number is ever displayed.
	// Ticked: a finite default range is installed for the user to narrow.
	for (uint8 l = 1; l < LINE_COUNT; l++) {
		add_table(3,1);
		{
			bt_use[l].init(button_t::square_automatic, rule_line_defs[l].name);
			bt_use[l].set_tooltip(translator::translate("Constrain this line (unchecked means any value)"));
			bt_use[l].add_listener(this);
			add_component(&bt_use[l]);
			num_min[l].add_listener(this);
			add_component(&num_min[l]);
			num_max[l].add_listener(this);
			add_component(&num_max[l]);
			lb_any[l].set_color(SYSCOL_TEXT_WEAK);
			lb_any[l].set_text(translator::translate("any"));
			add_component(&lb_any[l]);
		}
		end_table();
	}

	add_table(2,1);
	{
		bt_ok.init(button_t::roundbox, "OK");
		bt_ok.set_tooltip(translator::translate("Apply these rules"));
		bt_ok.add_listener(this);
		add_component(&bt_ok);
		bt_cancel.init(button_t::roundbox, "Cancel");
		bt_cancel.set_tooltip(translator::translate("Discard changes"));
		bt_cancel.add_listener(this);
		add_component(&bt_cancel);
	}
	end_table();

	retarget(player_, order_, slot, alt, wt);
	set_resizemode(diagonal_resize);
	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));
}


void consist_rule_editor_t::retarget(player_t *player_, consist_order_t *order_, uint32 slot, uint32 alt, waytype_t wt)
{
	player = player_;
	set_owner(player);
	order = order_;
	slot_index = slot;
	alt_index = alt;
	waytype = wt;
	// Edit a copy; OK writes it back, Cancel (or re-targeting) discards it.
	if (structured()) {
		edit_elem = order->access_order(slot_index).get_vehicle_description(alt_index);
		edit_catg = order->access_order(slot_index).get_catg_index();
		init_line_used();
	}
	refresh();
	reset_min_windowsize();
	resize(scr_size(0,0));
}


// Slot and alternative still exist (content comes from the working copy).
bool consist_rule_editor_t::structured() const
{
	return order != nullptr
		&& slot_index < order->get_count()
		&& alt_index < order->access_order(slot_index).get_count();
}


bool consist_rule_editor_t::valid() const
{
	return structured()
		&& order->access_order(slot_index).get_vehicle_description(alt_index).specific_vehicle == nullptr;
}


void consist_rule_editor_t::get_line(uint8 line, uint32 &mn, uint32 &mx) const
{
	switch (line) {
		case line_class: mn = mx = edit_elem.must_carry_class; break;
		case line_catering: mn = edit_elem.min_catering; mx = edit_elem.max_catering; break;
		case line_range: mn = edit_elem.min_range; mx = edit_elem.max_range; break;
		case line_brake: mn = edit_elem.min_brake_force; mx = edit_elem.max_brake_force; break;
		case line_power: mn = edit_elem.min_power; mx = edit_elem.max_power; break;
		case line_effort: mn = edit_elem.min_tractive_effort; mx = edit_elem.max_tractive_effort; break;
		case line_speed: mn = edit_elem.min_topspeed; mx = edit_elem.max_topspeed; break;
		case line_weight: mn = edit_elem.min_weight; mx = edit_elem.max_weight; break;
		case line_axle: mn = edit_elem.min_axle_load; mx = edit_elem.max_axle_load; break;
		case line_capacity: mn = edit_elem.min_capacity; mx = edit_elem.max_capacity; break;
		case line_runcost: mn = edit_elem.min_running_cost; mx = edit_elem.max_running_cost; break;
		case line_fixcost: mn = edit_elem.min_fixed_cost; mx = edit_elem.max_fixed_cost; break;
		case line_fuel: mn = edit_elem.min_fuel_per_km; mx = edit_elem.max_fuel_per_km; break;
		case line_staff: mn = edit_elem.min_staff_hundredths; mx = edit_elem.max_staff_hundredths; break;
		default: mn = edit_elem.min_drivers; mx = edit_elem.max_drivers; break;
	}
}


void consist_rule_editor_t::set_line(uint8 line, uint32 mn, uint32 mx)
{
	const uint32 tmax = rule_line_defs[line].type_max;
	mn = min(mn, tmax);
	mx = min(mx, tmax);
	switch (line) {
		case line_class: edit_elem.must_carry_class = (uint8)mn; break;
		case line_catering: edit_elem.min_catering = (uint8)mn; edit_elem.max_catering = (uint8)mx; break;
		case line_range: edit_elem.min_range = mn; edit_elem.max_range = mx; break;
		case line_brake: edit_elem.min_brake_force = (uint16)mn; edit_elem.max_brake_force = (uint16)mx; break;
		case line_power: edit_elem.min_power = mn; edit_elem.max_power = mx; break;
		case line_effort: edit_elem.min_tractive_effort = mn; edit_elem.max_tractive_effort = mx; break;
		case line_speed: edit_elem.min_topspeed = mn; edit_elem.max_topspeed = mx; break;
		case line_weight: edit_elem.min_weight = mn; edit_elem.max_weight = mx; break;
		case line_axle: edit_elem.min_axle_load = mn; edit_elem.max_axle_load = mx; break;
		case line_capacity: edit_elem.min_capacity = (uint16)mn; edit_elem.max_capacity = (uint16)mx; break;
		case line_runcost: edit_elem.min_running_cost = mn; edit_elem.max_running_cost = mx; break;
		case line_fixcost: edit_elem.min_fixed_cost = mn; edit_elem.max_fixed_cost = mx; break;
		case line_fuel: edit_elem.min_fuel_per_km = mn; edit_elem.max_fuel_per_km = mx; break;
		case line_staff: edit_elem.min_staff_hundredths = mn; edit_elem.max_staff_hundredths = mx; break;
		default: edit_elem.min_drivers = mn; edit_elem.max_drivers = mx; break;
	}
}


// Seed the explicit per-line flags from the stored bounds on first display, so
// an existing constrained rule shows as ticked. Class is a dropdown, not a row.
void consist_rule_editor_t::init_line_used()
{
	for (uint8 l = 1; l < LINE_COUNT; l++) {
		uint32 mn = 0, mx = 0;
		get_line(l, mn, mx);
		line_used[l] = (mn != 0 || mx != rule_line_defs[l].type_max);
	}
}


void consist_rule_editor_t::refresh_line(uint8 line)
{
	if (line == line_class) {
		return; // class uses the dropdown, handled in refresh()
	}
	const rule_line_def_t &def = rule_line_defs[line];
	const bool on = line_used[line];
	bt_use[line].pressed = on;
	uint32 mn = 0, mx = 0;
	get_line(line, mn, mx);
	// Inputs only exist while the line constrains; "any" stands in otherwise.
	num_min[line].set_visible(on);
	num_max[line].set_visible(on);
	lb_any[line].set_visible(!on);
	if (!on) {
		return;
	}
	const uint32 cap_u = max((uint32)1, line_edit_max[line]);
	const sint32 cap = (sint32)min(cap_u, (uint32)SINT32_MAX_VALUE);
	num_min[line].set_limits(0, cap);
	num_max[line].set_limits(0, cap);
	num_min[line].set_value((sint32)min(mn, (uint32)cap));
	num_max[line].set_value((sint32)min(mx, (uint32)cap));
	num_min[line].enable(true);
	num_max[line].enable(true);
	// Restore normal colouring (a previous validate() may have reddened it).
	num_min[line].set_color(SYSCOL_EDIT_TEXT);
	num_max[line].set_color(SYSCOL_EDIT_TEXT);
}


void consist_rule_editor_t::refresh()
{
	if (!structured()) {
		return;
	}
	engine_selector.set_selection(edit_elem.engine_type >= 10 ? 0 : edit_elem.engine_type + 1);

	sint32 sel = 0;
	for (uint32 i = 0; i < catg_values.get_count(); i++) {
		if (catg_values[i] == edit_catg) {
			sel = i;
			break;
		}
	}
	catg_selector.set_selection(sel);

	bt_empty.pressed = edit_elem.empty;

	rebuild_class_list();

	// Size the per-line inputs to the real vehicle pool: a line's maximum is
	// the largest value that exists among buyable/newly-available vehicles,
	// so defaults and the visible "top" are attainable figures.
	for (uint8 l = 1; l < LINE_COUNT; l++) {
		uint32 pmn = 0, pmx = 0;
		line_edit_max[l] = pool_attr_bounds(l, pmn, pmx) && pmx > 0
			? pmx
			: rule_line_defs[l].edit_max;
	}

	for (uint8 l = 1; l < LINE_COUNT; l++) {
		refresh_line(l);
	}
	validate();
}


// Class options follow the slot category. Passengers/mail use the canonical
// pakset class names (p_class[]/m_class[], e.g. "Low", "Medium"); the pakset
// decides how many there are, so nothing here assumes five. Class 0 is the
// lowest class and, as a minimum, means "unrestricted" (the matcher treats
// must_carry_class 0 as no constraint), so it is offered as that instead.
uint8 consist_rule_editor_t::class_option_count() const
{
	if (edit_catg == goods_manager_t::INDEX_PAS || edit_catg == goods_manager_t::INDEX_MAIL) {
		return goods_manager_t::get_classes_catg_index(edit_catg);
	}
	uint8 m = 0;
	if (waytype != invalid_wt) {
		for (auto const desc : vehicle_builder_t::get_info(waytype)) {
			if (edit_catg == consist_order_element_t::any_catg_index
				|| desc->get_freight_type()->get_catg_index() == edit_catg) {
				m = max(m, desc->get_number_of_classes());
			}
		}
	}
	return m;
}


void consist_rule_editor_t::rebuild_class_list()
{
	class_selector.clear_elements();
	clear_ptr_vector(class_numeric_labels);
	const bool named = (edit_catg == goods_manager_t::INDEX_PAS || edit_catg == goods_manager_t::INDEX_MAIL);
	class_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("Unrestricted"), SYSCOL_TEXT);
	const uint8 count = class_option_count();
	// Selection N means class N (0 = unrestricted), so options run 1..count-1.
	for (uint8 i = 1; i < count; i++) {
		const char *name = named ? goods_manager_t::get_translated_wealth_name(edit_catg, i) : NULL;
		if (name && name[0]) {
			class_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(name), SYSCOL_TEXT);
		}
		else {
			cbuffer_t *entry = new cbuffer_t();
			entry->printf("Class %u+", i);
			class_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>((const char *)*entry, SYSCOL_TEXT);
			// The combo stores the pointer, so the text must outlive it.
			class_numeric_labels.append(entry);
		}
	}
	class_selector.set_selection(min(edit_elem.must_carry_class, count ? count - 1 : 0));
}


// Pool membership for validation: this waytype, introduced (not in the future);
// retired vehicles count as "in the past". Without a timeline, everything counts.
bool consist_rule_editor_t::pool_desc_ok(const vehicle_desc_t *desc) const
{
	if (waytype == invalid_wt) {
		return false;
	}
	if (world()->get_settings().get_use_timeline()
		&& desc->get_intro_year_month() > world()->get_current_month()) {
		return false;
	}
	if (edit_catg != consist_order_element_t::any_catg_index
		&& desc->get_freight_type()->get_catg_index() != edit_catg) {
		return false;
	}
	return true;
}


// Span of one attribute across all pool vehicles (introduce-able now or in
// the past, not retired, of this way type and category). Used for tick
// defaults and input maxima so every displayed bound is attainable.
bool consist_rule_editor_t::pool_attr_bounds(uint8 line, uint32 &mn, uint32 &mx) const
{
	if (waytype == invalid_wt) {
		return false;
	}
	mn = UINT32_MAX_VALUE;
	mx = 0;
	bool found = false;
	for (auto const desc : vehicle_builder_t::get_info(waytype)) {
		if (!pool_desc_ok(desc)) {
			continue;
		}
		uint32 v = 0;
		switch (line) {
			case line_catering: v = desc->get_catering_level(); break;
			case line_range:    v = desc->get_range(); break;
			case line_brake:    v = desc->get_brake_force(); break;
			case line_power:    v = desc->get_power(); break;
			case line_effort:   v = desc->get_tractive_effort(); break;
			case line_speed:    v = desc->get_topspeed(); break;
			case line_weight:   v = desc->get_weight(); break;
			case line_axle:     v = desc->get_axle_load(); break;
			case line_capacity: v = (uint32)desc->get_total_capacity(); break;
			case line_runcost:  v = desc->get_running_cost(); break;
			case line_fixcost:  v = desc->get_fixed_cost(); break;
			case line_fuel:     v = desc->get_fuel_per_km(); break;
			case line_staff:    v = desc->get_total_staff_hundredths(); break;
			case line_drivers:  v = desc->get_total_drivers(); break;
			default: return false;
		}
		if (!found || v < mn) mn = v;
		if (!found || v > mx) mx = v;
		found = true;
	}
	return found;
}


// Whether some pool vehicle can satisfy one checked line of the working copy,
// keeping the other lines and the engine/category context. An empty rule
// alternative matches trivially at runtime, so it is always satisfiable.
bool consist_rule_editor_t::rule_line_matchable(uint8 line) const
{
	if (edit_elem.empty) {
		return true;
	}
	if (waytype == invalid_wt) {
		return true; // no pool to check against: do not block the player
	}
	vehicle_description_element probe = edit_elem;
	for (uint8 l = 0; l < LINE_COUNT; l++) {
		if (l != line) {
			// Relax: class to 0, ranges to 0/type-max.
			switch (l) {
				case line_class: probe.must_carry_class = 0; break;
					case line_catering: probe.min_catering = 0; probe.max_catering = 255; break;
					case line_range: probe.min_range = 0; probe.max_range = UINT32_MAX_VALUE; break;
					case line_brake: probe.min_brake_force = 0; probe.max_brake_force = 65535; break;
					case line_power: probe.min_power = 0; probe.max_power = UINT32_MAX_VALUE; break;
					case line_effort: probe.min_tractive_effort = 0; probe.max_tractive_effort = UINT32_MAX_VALUE; break;
					case line_speed: probe.min_topspeed = 0; probe.max_topspeed = UINT32_MAX_VALUE; break;
					case line_weight: probe.min_weight = 0; probe.max_weight = UINT32_MAX_VALUE; break;
					case line_axle: probe.min_axle_load = 0; probe.max_axle_load = UINT32_MAX_VALUE; break;
					case line_capacity: probe.min_capacity = 0; probe.max_capacity = 65535; break;
					case line_runcost: probe.min_running_cost = 0; probe.max_running_cost = UINT32_MAX_VALUE; break;
					case line_fixcost: probe.min_fixed_cost = 0; probe.max_fixed_cost = UINT32_MAX_VALUE; break;
					case line_fuel: probe.min_fuel_per_km = 0; probe.max_fuel_per_km = UINT32_MAX_VALUE; break;
					case line_staff: probe.min_staff_hundredths = 0; probe.max_staff_hundredths = UINT32_MAX_VALUE; break;
					default: probe.min_drivers = 0; probe.max_drivers = UINT32_MAX_VALUE; break;
				}
		}
	}
	if (waytype == invalid_wt) {
		return false;
	}
	for (auto const desc : vehicle_builder_t::get_info(waytype)) {
		if (!pool_desc_ok(desc)) {
			continue;
		}
		if (!vehicle_t::desc_matches_rule(desc, edit_catg, probe)) {
			continue;
		}
		// Class carries the matcher approximation: the rule class must be below
		// the vehicle's class count. This never false-reds; the runtime also
		// needs the instance's class reassignments to cooperate, which a
		// descriptor check cannot prove.
		if (probe.must_carry_class && probe.must_carry_class >= desc->get_number_of_classes()) {
			continue;
		}
		return true;
	}
	return false;
}


void consist_rule_editor_t::validate()
{
	if (!structured()) {
		return;
	}
	bool all_ok = true;
	bool first = true;
	reason_buf.clear();
	for (uint8 l = 0; l < LINE_COUNT; l++) {
		const bool active = l == line_class ? edit_elem.must_carry_class != 0 : line_used[l];
		const bool ok = !active || rule_line_matchable(l);
		if (l != line_class && line_used[l]) {
			// gui_numberinput_t::set_value() repaints itself on every change
			// (and greys when disabled), so the red marking is re-applied last.
			num_min[l].set_color(ok ? SYSCOL_EDIT_TEXT : color_idx_to_rgb(COL_RED));
			num_max[l].set_color(ok ? SYSCOL_EDIT_TEXT : color_idx_to_rgb(COL_RED));
			bt_use[l].set_tooltip(ok
				? translator::translate("Constrain this line (unchecked means any value)")
				: translator::translate("No vehicle available now or in the past can meet these limits"));
		}
		if (!ok) {
			all_ok = false;
			if (!first) {
				reason_buf.append(", ");
			}
			first = false;
			reason_buf.append(translator::translate(rule_line_defs[l].name));
		}
	}
	bt_ok.enable(all_ok);
	if (all_ok) {
		bt_ok.set_tooltip(translator::translate("Apply these rules"));
	}
	else {
		cbuffer_t full;
		full.printf("%s %s", translator::translate("Cannot apply: no matching vehicle for:"), (const char *)reason_buf);
		reason_buf.clear();
		reason_buf.append(full);
		bt_ok.set_tooltip(reason_buf);
	}
}


void consist_rule_editor_t::draw(scr_coord pos, scr_size size)
{
	if (!valid()) {
		destroy_win(this);
		return;
	}
	gui_frame_t::draw(pos, size);
}


bool consist_rule_editor_t::action_triggered(gui_action_creator_t *comp, value_t)
{
	if (!structured()) {
		return false;
	}
	if (comp == &engine_selector) {
		const sint32 sel = engine_selector.get_selection();
		edit_elem.engine_type = sel <= 0 ? vehicle_desc_t::MAX_TRACTION_TYPE : (uint8)(sel - 1);
		validate();
	}
	else if (comp == &catg_selector) {
		const sint32 sel = catg_selector.get_selection();
		if (sel >= 0 && (uint32)sel < catg_values.get_count()) {
			edit_catg = catg_values[sel];
		}
		refresh();
	}
	else if (comp == &class_selector) {
		const sint32 sel = class_selector.get_selection();
		edit_elem.must_carry_class = sel <= 0 ? 0 : (uint8)sel;
		validate();
	}
	else if (comp == &bt_empty) {
		// square_automatic has already flipped pressed; store it.
		edit_elem.set_empty(bt_empty.pressed);
		validate();
	}
	else if (comp == &bt_ok) {
		if (!valid()) {
			return false;
		}
		order->access_order(slot_index).access_vehicle_description(alt_index) = edit_elem;
		order->access_order(slot_index).set_catg_index(edit_catg);
		order->touch();
		destroy_win(this);
	}
	else if (comp == &bt_cancel) {
		destroy_win(this);
	}
	else {
		for (uint8 l = 1; l < LINE_COUNT; l++) {
			if (comp == &bt_use[l]) {
				// square_automatic has already flipped pressed.
				line_used[l] = bt_use[l].pressed;
				if (line_used[l]) {
					// Default to the real span of this attribute across available
					// vehicles of this way type (the minimum and maximum attainable
					// values), not the hard-coded type ceilings. A degenerate scan
					// result (no vehicle, or all vehicles equal to the attribute's
					// zero default) falls back to a sane finite range, so a ticked
					// line never narrows itself to "0..0" and validates.
					uint32 pmn = 0, pmx = 0;
					if (pool_attr_bounds(l, pmn, pmx) && (pmx > 0 || pmn > 0)) {
						set_line(l, pmn, pmx);
					}
					else {
						set_line(l, rule_line_defs[l].default_min, rule_line_defs[l].default_max);
					}
				}
				else {
					set_line(l, 0, rule_line_defs[l].type_max);
				}
				refresh_line(l);
				validate();
				// Toggling hides/shows the number inputs in this row: the window
				// must be re-laid-out or the row overlaps.
				reset_min_windowsize();
				set_windowsize(get_min_windowsize());
				resize(scr_size(0,0));
				return false;
			}
			if (comp == &num_min[l] || comp == &num_max[l]) {
				uint32 mn = 0, mx = 0;
				get_line(l, mn, mx);
				mn = (uint32)max(num_min[l].get_value(), 0);
				mx = (uint32)max(num_max[l].get_value(), 0);
				// Keep the stored range inside the pool span so it is attainable.
				if (mn > line_edit_max[l]) mn = line_edit_max[l];
				if (mx > line_edit_max[l]) mx = line_edit_max[l];
				set_line(l, mn, mx);
				refresh_line(l);
				validate();
				reset_min_windowsize();
				set_windowsize(get_min_windowsize());
				resize(scr_size(0,0));
				return false;
			}
		}
		return false;
	}
	return false;
}

void cont_order_overview_t::init_table()
{
	remove_all();
	old_count = order->get_count();
	old_mod_count = order->get_mod_count();
	set_table_layout(2,0);
	set_alignment(ALIGN_TOP);
	set_margin(scr_size(D_MARGIN_LEFT, 0), scr_size(D_MARGIN_RIGHT, D_SCROLLBAR_HEIGHT));
	if (!old_count) {
		// order is empty
		buf.clear();
		buf.append(translator::translate("Select the vehicles that you want to make up this consist at this stop\nand onwards on this schedule."));
		new_component<gui_textarea_t>(&buf);
	}
	else{
		const uint16 sel= abs(selected_index)-1;
		const bool is_append_mode = (selected_index>0);
		add_table(old_count+is_append_mode, 1);
		{
			for (uint8 col = 0; col < old_count; col++) {
				if (is_append_mode && (col == sel)) {
					gui_colored_label_t *lb = new_component<gui_colored_label_t>(SYSCOL_TD_BACKGROUND_HIGHLIGHT);
					lb->set_padding(scr_size(3,3));
					lb->buf().append("+");
					lb->update();
				}
				gui_aligned_container_t *tbl_orders = add_table(1, 3);
				tbl_orders->set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
				tbl_orders->set_spacing(scr_size(0,0));
				new_component<gui_consist_order_shifter_t>(order, col);
				new_component<gui_margin_t>(0,D_V_SPACE);
				new_component<gui_consist_order_element_t>(order, col, way_type, !is_append_mode && (col == sel));
				end_table();
			}
		}
		end_table();
		new_component<gui_fill_t>();
	}
	set_size(get_min_size());
}

gui_consist_order_element_t::gui_consist_order_element_t(consist_order_t *order, uint32 index, waytype_t wt, bool sel)
{
	selected = sel;
	set_table_layout(1,2);
	set_alignment(ALIGN_TOP | ALIGN_CENTER_H);
	set_spacing(scr_size(0,0));
	set_margin(scr_size(D_H_SPACE,0), scr_size(0,0));
	set_table_frame(true);

	gui_colored_label_t *th = new_component<gui_colored_label_t>(selected ? SYSCOL_TH_TEXT_SELECTED : SYSCOL_TH_TEXT_TOP, gui_label_t::centered, selected ? SYSCOL_TH_BACKGROUND_SELECTED : SYSCOL_TH_BACKGROUND_TOP);
	th->buf().printf(" %u ", index+1);
	th->set_underline(selected);
	th->set_padding(scr_size(0,3));
	th->update();

	new_component<gui_vehicle_description_element_t>(order, index, wt);
	set_size(get_min_size());
}

void gui_consist_order_element_t::draw(scr_coord offset)
{
	if (selected) {
		// draw background
		display_fillbox_wh_clip_rgb(pos.x + offset.x, pos.y + offset.y, size.w, size.h, SYSCOL_TD_BACKGROUND_HIGHLIGHT, false);
	}
	gui_aligned_container_t::draw(offset);
}

void cont_order_overview_t::draw(scr_coord offset)
{
	if (order) {
		if (order->get_count() != old_count || order->get_mod_count() != old_mod_count) {
			init_table();
		}
		gui_aligned_container_t::draw(offset);
	}
}


consist_order_frame_t::consist_order_frame_t(player_t* player, schedule_t *schedule, uint16 entry_id)
	: gui_frame_t(translator::translate("consist_order")),
	halt_number(255),
	cont_order_overview(&order, schedule->get_waytype()),
	scl_vehicles(gui_scrolled_list_t::listskin, vehicle_scrollitem_t::compare),
	scl_convoys(gui_scrolled_list_t::listskin),
	scroll_order(&cont_order, true, false),
	img_convoy(convoihandle_t()),
	formation(convoihandle_t(), false),
	scrollx_formation(&formation, true, false)
{
	if (player && schedule) {
		this->player = player;
		set_owner(player);
		init(schedule, entry_id);
		init_table();
	}
}


void consist_order_frame_t::save_order()
{
	if (player) {
		schedule->orders.remove(unique_entry_id);
		if (order.get_count()) {
			schedule->orders.put(unique_entry_id, order);
		}
	}
}


void consist_order_frame_t::init(schedule_t *schedule, uint16 entry_id)
{
	// Save unsaved edits to the previous target first: save_order() writes
	// through the member schedule pointer, so this must run before rebinding
	// (including a same-entry-id switch between two schedules).
	if (unique_entry_id != 65535 && (unique_entry_id != entry_id || this->schedule != schedule)) {
		save_order();
	}
	this->schedule = schedule;
	unique_entry_id = entry_id;
	order = schedule->orders.get(unique_entry_id);
}


void consist_order_frame_t::retarget(schedule_t *schedule, uint16 entry_id)
{
	// Waypoint/depot-tile entries have no halt and cannot carry a consist
	// order: keep showing the current target instead of rebinding to nothing
	// (update() destroys the window on an unbound halt).
	if (!haltestelle_t::get_halt(schedule->entries[schedule->get_current_stop()].pos, player).is_bound()) {
		return;
	}
	init(schedule, entry_id);
	cont_order_overview.set_waytype(schedule->get_waytype());
	destroy_win(magic_consist_rule_editor);
	update();
	cont_order_overview.set_selected_index(0);
	build_vehicle_list();
	init_input_value_range();
	reset_min_windowsize();
	resize(scr_size(0,0));
}

void consist_order_frame_t::init_table()
{
	bt_filter_halt_convoy.pressed = false;
	bt_filter_line_convoy.pressed = true;
	bt_filter_single_vehicle.pressed = false;
	cont_convoy_filter.set_visible(false);
	update();

	set_table_layout(1, 0);
	set_alignment(ALIGN_TOP);

#ifdef DEBUG
	gui_label_buf_t *lb = new_component<gui_label_buf_t>(COL_DANGER);
	lb->buf().printf("(debug)entry-id: %u", unique_entry_id);
	lb->update();
#endif

	const waytype_t way_type = schedule->get_waytype();
	add_table(3,1);
	{
		new_component<gui_waytype_image_box_t>(way_type);
		add_component(&halt_number);
		add_component(&lb_halt);
	}
	end_table();

	// [OVERVIEW] (orders)
	cont_order.set_table_layout(2,0);
	cont_order.set_margin(scr_size(D_H_SPACE, D_V_SPACE), scr_size(D_SCROLLBAR_WIDTH, 0));
	cont_order.set_alignment(ALIGN_TOP);
	{
		cont_order.add_component(&cont_order_overview);
		cont_order.new_component<gui_fill_t>();
		cont_order.new_component<gui_fill_t>(false, true);
	}
	const scr_coord grid = gui_convoy_assembler_t::get_grid(way_type);
	scroll_order.set_min_height(grid.y + LINESPACE + D_BUTTON_HEIGHT*3 + D_INDICATOR_HEIGHT + D_V_SPACE*5 + D_SCROLLBAR_HEIGHT);
	scroll_order.set_maximize(true);
	add_component(&scroll_order);

	new_component<gui_divider_t>();

	// filter, sort
	freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("--------------"), SYSCOL_TEXT);
	freight_type_c.set_selection(0);
	for (uint8 i = 0; i < goods_manager_t::get_max_catg_index(); i++) {
		if (i==goods_manager_t::INDEX_NONE) {
			continue;
		}

		const goods_desc_t* info = goods_manager_t::get_info_catg_index(i);
		freight_type_c.new_component<gui_scrolled_list_t::img_label_scrollitem_t>(translator::translate(info->get_catg_name()), SYSCOL_TEXT, info->get_catg_symbol());
	}
	freight_type_c.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("none"), SYSCOL_TEXT);
	freight_type_c.add_listener(this);
	freight_type_c.set_size(scr_size(D_BUTTON_WIDTH*2, D_EDIT_HEIGHT));
	freight_type_c.set_width_fixed(true);

	add_table(3,1);
	{
		new_component<gui_label_t>("clf_chk_waren")->set_tooltip(translator::translate("Show only vehicles carrying the selected type of goods"));
		add_component(&freight_type_c);
		new_component<gui_fill_t>();
	}
	end_table();

	// [VEHICLE PICKER]
	old_vehicle_assets = player->get_finance()->get_history_veh_year(finance_t::translate_waytype_to_tt(schedule->get_waytype()), 0, ATV_NEW_VEHICLE);
	cont_picker_frame.set_table_layout(2,2);
	cont_picker_frame.set_alignment(ALIGN_TOP);
	{
		cont_picker_frame.new_component<gui_label_t>("Filter:");
		cont_picker_frame.add_table(4,1);
		{
			edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("add_elem_at"), SYSCOL_TEXT); // add consist_order_element_t at certain position
			edit_action_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("append_vde"), SYSCOL_TEXT); // append the vehicle as a vehicle description element
			edit_action_selector.set_selection(0);
			edit_action_selector.add_listener(this);
			cont_picker_frame.add_component(&edit_action_selector);

			init_input_value_range();
			numimp_append_target.set_value(1);
			numimp_append_target.add_listener(this);
			cont_picker_frame.add_component(&numimp_append_target);
			cont_order_overview.set_selected_index(1);

			bt_add_vehicle.init(button_t::roundbox, "Add vehicle");
			bt_add_vehicle.set_tooltip(translator::translate("Add the selected vehicle to the consist order"));
			bt_add_vehicle.enable( selected_vehicle!=NULL );
			bt_add_vehicle.add_listener(this);
			cont_picker_frame.add_component(&bt_add_vehicle);
			bt_add_rule_slot.init(button_t::roundbox, "Add rule slot");
			bt_add_rule_slot.set_tooltip(translator::translate("Add a new slot with a rule-based alternative"));
			bt_add_rule_slot.add_listener(this);
			cont_picker_frame.add_component(&bt_add_rule_slot);
			cont_picker_frame.new_component<gui_fill_t>();
		}
		cont_picker_frame.end_table();


		cont_picker_frame.add_table(2,7);
		{
			bt_connectable_vehicle_filter.init(button_t::square_state, "show_only_appendable");
			bt_connectable_vehicle_filter.set_tooltip(translator::translate("Show only vehicles that can be added to the end of the selected order."));
			bt_connectable_vehicle_filter.pressed = true;
			bt_connectable_vehicle_filter.add_listener(this);
			cont_picker_frame.add_component(&bt_connectable_vehicle_filter, 2);

		cont_picker_frame.new_component<gui_label_t>("powered_filter")->set_tooltip(translator::translate("Show only powered or unpowered vehicles"));
		// Two inclusive checkboxes preserve the old three dropdown states
		// (both on = all, one on = only that class). square_automatic flips
		// pressed itself; the filter only rebuilds the list.
		cont_picker_frame.add_table(1,2);
		{
			bt_show_powered.init(button_t::square_automatic, "Show powered vehicles");
			bt_show_powered.set_tooltip(translator::translate("Include powered vehicles in the list."));
			bt_show_powered.pressed = true;
			bt_show_powered.add_listener(this);
			cont_picker_frame.add_component(&bt_show_powered);

			bt_show_unpowered.init(button_t::square_automatic, "Show unpowered vehicles");
			bt_show_unpowered.set_tooltip(translator::translate("Include unpowered vehicles in the list."));
			bt_show_unpowered.pressed = true;
			bt_show_unpowered.add_listener(this);
			cont_picker_frame.add_component(&bt_show_unpowered);
		}
		cont_picker_frame.end_table();

			cont_picker_frame.new_component<gui_label_t>("engine_type")->set_tooltip(translator::translate("Show only vehicles with the selected engine type"));
			engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All"), SYSCOL_TEXT);
			for (uint8 i = 1; i < 11; i++) {
				engine_filter.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vehicle_builder_t::engine_type_names[(vehicle_desc_t::engine_t)i]), SYSCOL_TEXT);
			}
			engine_filter.set_selection(0);
			engine_filter.add_listener(this);
			cont_picker_frame.add_component(&engine_filter);

			if (world()->get_settings().get_allow_buying_obsolete_vehicles()) {
				bt_outdated.init(button_t::square_state, "Show outdated");
				bt_outdated.set_tooltip("Show also vehicles no longer in production.");
				bt_outdated.pressed = true;
				bt_outdated.add_listener(this);
				cont_picker_frame.add_component(&bt_outdated, 2);
			}
			if (world()->get_settings().get_allow_buying_obsolete_vehicles() == 1) {
				bt_obsolete.init(button_t::square_state, "Show obsolete");
				bt_obsolete.set_tooltip("Show also vehicles whose maintenance costs have increased due to obsolescence.");
				bt_obsolete.pressed = true;
				bt_obsolete.add_listener(this);
				cont_picker_frame.add_component(&bt_obsolete, 2);
			}

			bt_show_unidirectional.init(button_t::square_state, "Show unidirectional vehicle");
			bt_show_unidirectional.set_tooltip("Show also unidirectional vehicles.");
			bt_show_unidirectional.pressed = true;
			bt_show_unidirectional.add_listener(this);
			cont_picker_frame.add_component(&bt_show_unidirectional, 2);
		}
		cont_picker_frame.end_table();

		build_vehicle_list();

		cont_picker_frame.add_table(1,2)->set_alignment(ALIGN_TOP);
		{
			cont_picker_frame.add_table(3, 2);
			{
				cont_picker_frame.new_component<gui_label_t>("cl_txt_sort")->set_tooltip(translator::translate("Sort the vehicle list by the selected criterion"));
				for (uint8 i = 0; i < vehicle_scrollitem_t::SORT_MODES; i++) {
					vp_sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(vp_sort_text[i]), SYSCOL_TEXT);
				}
				vp_sortedby.set_selection(0);
				vp_sortedby.add_listener(this);
				cont_picker_frame.add_component(&vp_sortedby);
				bt_sort_order_veh.init(button_t::sortarrow_state, "");
				bt_sort_order_veh.set_tooltip(translator::translate("Toggle ascending/descending order"));
				bt_sort_order_veh.add_listener(this);
				bt_sort_order_veh.pressed = vehicle_scrollitem_t::sortreverse;
				cont_picker_frame.add_component(&bt_sort_order_veh);
			}
			cont_picker_frame.end_table();

			scl_vehicles.set_maximize(true);
			scl_vehicles.add_listener(this);
			cont_picker_frame.add_component(&scl_vehicles);
		}
		cont_picker_frame.end_table();
	}

	// [CONVOY COPIER]
	cont_convoy_copier.set_table_layout(2,1);
	cont_convoy_copier.set_alignment(ALIGN_TOP);
	{
		// selector (left)
		cont_convoy_copier.add_table(1,3);
		{
			cont_convoy_filter.set_table_layout(3,1);
			cont_convoy_filter.set_table_frame(true, true);
			{
				cont_convoy_filter.new_component<gui_empty_t>(); // left margin
				cont_convoy_filter.add_table(1,0);
				{
					cont_convoy_filter.new_component<gui_empty_t>(); // top margin
					bt_filter_halt_convoy.init(button_t::square_state, "filter_halt_consist");
					bt_filter_halt_convoy.set_tooltip(translator::translate("Narrow down to consists that use this stop"));
					bt_filter_halt_convoy.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_halt_convoy);

					bt_filter_line_convoy.init(button_t::square_state, "filter_line_consist");
					bt_filter_line_convoy.set_tooltip(translator::translate("Narrow down to only consists belonging to this line"));
					bt_filter_line_convoy.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_line_convoy);

					bt_filter_single_vehicle.init(button_t::square_state, "filter_single_vehicle_consist");
					bt_filter_single_vehicle.set_tooltip(translator::translate("Exclude consists made up of one vehicle"));
					bt_filter_single_vehicle.add_listener(this);
					cont_convoy_filter.add_component(&bt_filter_single_vehicle);

					// electric / bidirectional
					// TODO: name filter
					// TODO: home depot filter
					cont_convoy_filter.new_component<gui_empty_t>(); // bottom margin
				}
				cont_convoy_filter.end_table();
				cont_convoy_filter.new_component<gui_empty_t>(); // right margin
			}
			cont_convoy_filter.set_size(cont_convoy_filter.get_min_size());
			cont_convoy_filter.set_rigid(false);

			cont_convoy_copier.add_table(2,1)->set_alignment(ALIGN_TOP);
			{
				bt_show_hide_convoy_filter.init(button_t::roundbox, "+");
				bt_show_hide_convoy_filter.set_tooltip(translator::translate("Show or hide the consist filter options"));
				bt_show_hide_convoy_filter.set_width(display_get_char_width('+') + D_BUTTON_PADDINGS_X);
				bt_show_hide_convoy_filter.add_listener(this);
				cont_convoy_copier.add_component(&bt_show_hide_convoy_filter);
				cont_convoy_copier.add_table(1,3)->set_spacing(scr_size(0,0));
				{
					lb_open_convoy_filter.init("open_consist_filter_option");
					lb_open_convoy_filter.set_rigid(false);
					cont_convoy_copier.add_component(&lb_open_convoy_filter);
					cont_convoy_copier.add_component(&cont_convoy_filter);
					cont_convoy_copier.new_component<gui_margin_t>(cont_convoy_filter.get_min_size().w);
				}
				cont_convoy_copier.end_table();
			}
			cont_convoy_copier.end_table();

			cont_convoy_copier.add_table(3,2);
			{
				cont_convoy_copier.new_component<gui_label_t>("cl_txt_sort")->set_tooltip(translator::translate("Sort the consist list by the selected criterion"));
				// TODO: add sort option
				//cc_sortedby
				for (uint8 i = 0; i < 1; i++) {
					cc_sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(cc_sort_text[i]), SYSCOL_TEXT);
				}
				cc_sortedby.set_selection(0);
				cc_sortedby.add_listener(this);
				cont_convoy_copier.add_component(&cc_sortedby);
				bt_sort_order_cnv.init(button_t::sortarrow_state, "");
				bt_sort_order_cnv.set_tooltip(translator::translate("Toggle ascending/descending order"));
				bt_sort_order_cnv.add_listener(this);
				bt_sort_order_cnv.pressed = false;
				cont_convoy_copier.add_component(&bt_sort_order_cnv);
			}
			cont_convoy_copier.end_table();

			scl_convoys.set_maximize(true);
			scl_convoys.add_listener(this);
			cont_convoy_copier.add_component(&scl_convoys);
		}
		cont_convoy_copier.end_table();

		// details (right)
		cont_convoy_copier.add_table(1,0);
		{
			bt_copy_convoy.init(button_t::roundbox, "copy_consist_order");
			bt_copy_convoy.set_tooltip(translator::translate("Set the order of this consist to the selected order"));
			bt_copy_convoy.add_listener(this);
			cont_convoy_copier.add_component(&bt_copy_convoy);

			cont_convoy_copier.add_component(&line_label);
			cont_convoy_copier.add_component(&img_convoy);
			scrollx_formation.set_maximize(true);
			cont_convoy_copier.add_component(&scrollx_formation);
			cont_convoy_copier.add_component(&lb_vehicle_count);

			bt_convoy_detail.init(button_t::roundbox, "Details");
			if (skinverwaltung_t::open_window) {
				bt_convoy_detail.set_image(skinverwaltung_t::open_window->get_image_id(0));
				bt_convoy_detail.set_image_position_right(true);
			}
			bt_convoy_detail.set_tooltip("Vehicle details");
			bt_convoy_detail.add_listener(this);
			cont_convoy_copier.add_component(&bt_convoy_detail);

			cont_convoy_copier.new_component<gui_fill_t>();
		}
		cont_convoy_copier.end_table();
	}

	tabs.add_tab(&cont_convoy_copier, translator::translate("Consist copier"));
	tabs.add_tab(&cont_picker_frame, translator::translate("Vehicle picker"));
	tabs.add_listener(this);
	add_component(&tabs);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	resize(scr_size(0,0));
	set_resizemode(diagonal_resize);
}


void consist_order_frame_t::update_convoy_info()
{
	line_label.set_line(linehandle_t());
	if( selected_convoy.is_bound() ) {
		lb_vehicle_count.buf().printf("%s %i", translator::translate("Fahrzeuge:"), selected_convoy->get_vehicle_count());
		if (selected_convoy->front()->get_waytype() != water_wt) {
			lb_vehicle_count.buf().printf(" (%s %i)", translator::translate("Station tiles:"), selected_convoy->get_tile_length());
		}
		if (selected_convoy->get_line().is_bound()) {
			line_label.set_line(selected_convoy->get_line());
		}
	}
	bt_copy_convoy.enable(selected_convoy.is_bound());
	bt_convoy_detail.enable(selected_convoy.is_bound());
	lb_vehicle_count.update();

	img_convoy.set_cnv(selected_convoy);
	formation.set_cnv(selected_convoy);
	cont_convoy_copier.set_size(cont_convoy_copier.get_min_size());
	// adjust the size if the tab is open
	if (tabs.get_active_tab_index() == 1) {
		reset_min_windowsize();
		resize(scr_size(0,0));
	}
}

void consist_order_frame_t::init_input_value_range()
{
	numimp_append_target.enable(!(edit_action_selector.get_selection() == 1 && !order.get_count()));
	const bool is_append_mode = (edit_action_selector.get_selection() == 0);
	const uint32 max_vehicles = is_append_mode ? order.get_count() + 1 : max(1, order.get_count());
	if (numimp_append_target.get_value()>=max_vehicles) {
		cont_order_overview.set_selected_index(is_append_mode ? max_vehicles : -max_vehicles);
		numimp_append_target.set_value(max_vehicles);
	}
	numimp_append_target.set_limits(1, (sint32)max_vehicles);
}


void consist_order_frame_t::draw(scr_coord pos, scr_size size)
{
	if (player != welt->get_active_player() || !schedule->get_count()) { destroy_win(this); }
	if( schedule->get_count() != old_entry_count ) {
		init_input_value_range();
		update();
	}
	else if (order.get_count() != old_order_count || order.get_mod_count() != old_order_mod_count) {
		build_vehicle_list();
		init_input_value_range();
	}
	gui_frame_t::draw(pos, size);
}


bool consist_order_frame_t::infowin_event(const event_t *ev)
{
	if (ev->ev_class == INFOWIN && ev->ev_code == WIN_CLOSE) {
		// The editor edits this window's working copy; it must not outlive it.
		destroy_win(magic_consist_rule_editor);
		save_order();
		return false;
	}
	return gui_frame_t::infowin_event(ev);
}


bool consist_order_frame_t::action_triggered(gui_action_creator_t *comp, value_t v)
{
	// [CONVOY COPIER]
	if( comp==&scl_convoys  &&  own_convoys.get_count() ) {
		scl_convoys.get_selection();
		convoy_scrollitem_t *item = (convoy_scrollitem_t*)scl_convoys.get_element(v.i);
		selected_convoy = item->get_convoy();
		update_convoy_info();
	}
	else if( comp==&bt_filter_halt_convoy ){
		bt_filter_halt_convoy.pressed ^= 1;
		if( bt_filter_halt_convoy.pressed ) {
			bt_filter_line_convoy.pressed = false;
		}
		build_vehicle_list();
	}
	else if( comp==&bt_filter_line_convoy){
		bt_filter_line_convoy.pressed ^= 1;
		if( bt_filter_line_convoy.pressed ) {
			bt_filter_halt_convoy.pressed = false;
		}
		build_vehicle_list();
	}
	else if( comp==&bt_filter_single_vehicle ){
		bt_filter_single_vehicle.pressed = !bt_filter_single_vehicle.pressed;
		build_vehicle_list();
	}
	else if( comp==&bt_show_hide_convoy_filter ) {
		lb_open_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		cont_convoy_filter.set_visible(!lb_open_convoy_filter.is_visible());
		bt_show_hide_convoy_filter.set_text(lb_open_convoy_filter.is_visible() ? "+" : "-");

	}
	else if( comp==&bt_sort_order_cnv ) {
		bt_sort_order_cnv.pressed ^= 1;
		// TODO: execute sorting
	}
	else if(  comp==&bt_copy_convoy  ) {
		if( !selected_convoy.is_bound() ) {
			create_win(new news_img("No valid convoy selected!"), w_time_delete, magic_none);
		}
		else {
			order.set_convoy_order(selected_convoy);
		}
		init_input_value_range();
		edit_action_selector.set_selection(0);
		numimp_append_target.set_value(order.get_count()+1);
		cont_order_overview.set_selected_index(order.get_count() + 1);
	}
	else if(  comp==&bt_convoy_detail  ) {
		if (selected_convoy.is_bound()) {
			create_win({ 20, 20 }, new convoi_detail_t(selected_convoy), w_info, magic_convoi_detail + selected_convoy.get_id());
		}
		return true;
	}
	else if( comp==&freight_type_c ) {
		const int selection_temp = freight_type_c.get_selection();
		if (selection_temp == freight_type_c.count_elements()-1) {
			filter_catg = goods_manager_t::INDEX_NONE;
		}
		else {
			switch( selection_temp )
			{
				case 0:
					filter_catg = 255; // all
					break;
				case 1:
					filter_catg = goods_manager_t::INDEX_PAS;
					break;
				case 2:
					filter_catg = goods_manager_t::INDEX_MAIL;
					break;
				default:
					filter_catg = freight_type_c.get_selection();
					break;
			}
		}
		build_vehicle_list();
	}
	//else if (comp == &cc_sortedby) {
	//	// TODO: add sort option
	//
	//}

	// [VEHICLE PICKER]
	else if( comp==&scl_vehicles ) {
		scl_vehicles.get_selection();
		vehicle_scrollitem_t *item = (vehicle_scrollitem_t*)scl_vehicles.get_element(v.i);
		selected_vehicle = item->get_vehicle();
		if( selected_vehicle==NULL ) {
			bt_add_vehicle.disable();
		}
		else {
			open_vehicle_detail(selected_vehicle);
			bt_add_vehicle.enable();
		}
		cont_picker_frame.set_size(cont_picker_frame.get_min_size());
		// adjust the size if the tab is open
		if (tabs.get_active_tab_index() == 0) {
			reset_min_windowsize();
			resize(scr_size(0,0));
		}
	}
	else if( comp==&vp_sortedby ) {
		vehicle_scrollitem_t::sort_mode = v.i;
		scl_vehicles.refresh();
	}
	else if( comp==&bt_sort_order_veh ) {
		vehicle_scrollitem_t::sortreverse = !vehicle_scrollitem_t::sortreverse;
		scl_vehicles.refresh();
		bt_sort_order_veh.pressed = vehicle_scrollitem_t::sortreverse;
	}
	else if( comp==&bt_connectable_vehicle_filter ) {
		bt_connectable_vehicle_filter.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&bt_outdated ) {
		bt_outdated.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&bt_obsolete ) {
		bt_obsolete.pressed ^= 1;
		build_vehicle_list();
	}
	else if(comp==&bt_show_unidirectional ) {
		bt_show_unidirectional.pressed ^= 1;
		build_vehicle_list();
	}
	else if( comp==&bt_show_powered || comp==&bt_show_unpowered || comp==&engine_filter ) {
		// square_automatic buttons already flipped pressed; combos report selection.
		build_vehicle_list();
	}
	else if( comp==&numimp_append_target ) {
		build_vehicle_list();
		const sint32 value = numimp_append_target.get_value();
		cont_order_overview.set_selected_index((edit_action_selector.get_selection()==0) ? value : -value);
	}
	else if(  comp==&edit_action_selector  ) {
		init_input_value_range();
		const sint32 value = numimp_append_target.get_value();
		cont_order_overview.set_selected_index((edit_action_selector.get_selection()==0) ? value : -value);
	}
	else if( comp==&bt_add_vehicle ) {
		if (!selected_vehicle) {
			create_win(new news_img("No vehicle selected!"), w_time_delete, magic_none);
			return true;
		}
		uint32 append_target_index = (uint32)numimp_append_target.get_value();
		if (append_target_index<1 || append_target_index > order.get_count()+1) {
			append_target_index=order.get_count();
		}

		if (edit_action_selector.get_selection()==0 || !order.get_count()) {
			// add consist_order_element_t at certain position
			consist_order_element_t new_elem;
			new_elem.append_vehicle(selected_vehicle);
			order.insert_at(append_target_index-1, new_elem);
			const sint32 new_index = max(2, append_target_index + 1);
			numimp_append_target.set_value(new_index);
			cont_order_overview.set_selected_index((edit_action_selector.get_selection() == 0) ? new_index : -new_index);
		}
		else {
			// append the vehicle as a vehicle description element
			order.append_vehicle_at(append_target_index-1, selected_vehicle);
		}

		init_input_value_range();
	}
	else if( comp==&bt_add_rule_slot ) {
		uint32 append_target_index = (uint32)numimp_append_target.get_value();
		if (append_target_index<1 || append_target_index > order.get_count()+1) {
			append_target_index=order.get_count()+1;
		}
		consist_order_element_t new_elem;
		new_elem.append_rule();
		new_elem.set_catg_index(consist_order_element_t::any_catg_index);
		order.insert_at(append_target_index-1, new_elem);
		order.touch();
		init_input_value_range();
		open_rule_editor(&order, append_target_index-1, 0);
	}
	// others
	else if (comp == &tabs) {
		if (tabs.get_active_tab_index() == 0) {
			cont_order_overview.set_selected_index(0);
		}
		else {
			const sint32 value = numimp_append_target.get_value();
			cont_order_overview.set_selected_index((edit_action_selector.get_selection() == 0) ? value : -value);
		}
	}
	resize(scr_size(0,0));
	return false;
}


// TODO: filter, consider connection
// v-shape filter, catg filter
// TODO: Consider vehicle reversal
// NOTE: The execution of consist orders entails, for reversed consists, de-reversing it, adding the new vehicle and then reversing it again.
void consist_order_frame_t::build_vehicle_list()
{
	const bool search_only_halt_convoy = bt_filter_halt_convoy.pressed;
	const bool search_only_line_convoy = bt_filter_line_convoy.pressed;
	const bool search_only_appendable_vehicle = bt_connectable_vehicle_filter.pressed;

	own_vehicles.clear();
	scl_vehicles.clear_elements();
	const vehicle_desc_t *old_vehicle = selected_vehicle;
	selected_vehicle = NULL;
	scl_vehicles.set_selection(-1);

	own_convoys.clear();
	scl_convoys.clear_elements();
	scl_convoys.set_selection(-1);

	old_order_count = order.get_count();
	old_order_mod_count = order.get_mod_count();

	// Vehicles that have already been determined to be unconnectable
	slist_tpl<const vehicle_desc_t *>unconnectable_vehicles;
	uint32 append_target_index = min((uint32)numimp_append_target.get_value()-1, order.get_count());

	// list only own vehicles
	for (auto const cnv : world()->convoys()) {
		if((cnv->get_owner() == player || cnv->get_owner()->allows_access_to(player->get_player_nr()) && player->allows_access_to(cnv->get_owner()->get_player_nr())) && cnv->front()->get_waytype()==schedule->get_waytype())
		{
			// count own vehicle
			for (uint8 i = 0; i < cnv->get_vehicle_count(); i++) {
				const vehicle_desc_t *veh_type = cnv->get_vehicle(i)->get_desc();
				// filter
				if (is_filtered(veh_type)) continue;

				// serach for already own
				bool found = false;
				for (auto &own_veh : own_vehicles) {
					if (own_veh.veh_type == veh_type) {
						own_veh.count++;
						found = true;
						break;
					}
				}
				if (!found){
					// Exclude same vehicle
					if (edit_action_selector.get_selection() == 1 && order.get_count() && append_target_index < order.get_count()) {
						if(order.get_order(append_target_index).has_same_vehicle(veh_type)) {
							unconnectable_vehicles.append(veh_type);
							continue;
						}
					}

					if (search_only_appendable_vehicle) {
						// Check if appendable
						if (unconnectable_vehicles.is_contained(veh_type)) {
							continue;
						}
						// prev
						if (append_target_index==0 && edit_action_selector.get_selection() == 1) {
							if (!(veh_type->get_basic_constraint_prev()&vehicle_desc_t::can_be_head)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (append_target_index && !order.get_order(append_target_index-1).can_connect(veh_type, true)) {
							unconnectable_vehicles.append(veh_type);
							continue;
						}
						// next
						uint32 target_order = edit_action_selector.get_selection()==0 ? append_target_index : append_target_index+1;
						if (target_order<order.get_count()) {
							if (!order.get_order(target_order).can_connect(veh_type, false)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (edit_action_selector.get_selection() == 1) {
							if (!veh_type->can_lead(NULL)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
					}
					own_vehicle_t temp;
					temp.count = 1;
					temp.veh_type = veh_type;
					own_vehicles.append(temp);
				}
			}

			// append convoy
			if( bt_filter_single_vehicle.pressed  &&  cnv->get_vehicle_count()<2 ) {
				continue;
			}
			if (!search_only_halt_convoy && !search_only_line_convoy) {
				// filter
				if( filter_catg!=255  &&  filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
					continue;
				}
				own_convoys.append(cnv);
			}
		}
	}

	// also count vehicles that stored at depots
	for( auto const depot : depot_t::get_depot_list() ) {
		if( depot->get_owner() == player ) {
			for( auto const veh : depot->get_vehicle_list() ) {
				const vehicle_desc_t *veh_type = veh->get_desc();
				// filter
				if (is_filtered(veh_type)) continue;

				// serach for already own
				bool found = false;
				for (auto &own_veh : own_vehicles) {
					if (own_veh.veh_type == veh_type) {
						own_veh.count++;
						found = true;
						break;
					}
				}
				if (!found){
					if (search_only_appendable_vehicle) {
						// Check if appendable
						if (unconnectable_vehicles.is_contained(veh_type)) {
							continue;
						}
						// prev
						if (append_target_index==1) {
							if (!veh_type->can_follow(NULL)) continue;
						}
						else if (append_target_index > 1 && append_target_index <= order.get_count()) {
							if (!order.get_order(append_target_index - 1).can_connect(veh_type, false)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						// next
						if (edit_action_selector.get_selection() == 0) {
							if (append_target_index >= order.get_count()) {
								if (!veh_type->can_lead(NULL)) continue;
							}
							else if (!order.get_order(append_target_index-1).can_connect(veh_type)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
						else if (edit_action_selector.get_selection() == 1) {
							if (append_target_index >= order.get_count()-1) {
								if (!veh_type->can_lead(NULL)) continue;
							}
							else if (!order.get_order(append_target_index).can_connect(veh_type)) {
								unconnectable_vehicles.append(veh_type);
								continue;
							}
						}
					}
					own_vehicle_t temp;
					temp.count = 1;
					temp.veh_type = veh_type;
					own_vehicles.append(temp);
				}
			}
		}
	}

	if( search_only_halt_convoy || search_only_line_convoy ) {
		// halt line
		for (uint32 i=0; i < halt->registered_lines.get_count(); ++i) {
			const linehandle_t line = halt->registered_lines[i];
			if( line->get_owner()==player  &&  line->get_schedule()->get_waytype()==schedule->get_waytype() ) {
				if (search_only_line_convoy && !schedule->matches(world(),line->get_schedule())) {
					continue;
				}
				for( uint32 j=0; j < line->count_convoys(); ++j ) {
					convoihandle_t const cnv = line->get_convoy(j);
					if (cnv->in_depot()) {
						continue;
					}
					// filter
					if( filter_catg!=255  &&  filter_catg != goods_manager_t::INDEX_NONE && !cnv->get_goods_catg_index().is_contained(filter_catg)) {
						continue;
					}
					if( bt_filter_single_vehicle.pressed  &&  cnv->get_vehicle_count()<2 ) {
						continue;
					}
					// Exclude convoys consisting of the same vehicle in the same line
					bool alredey_append = false;
					uint8 k = j;
					while (k > 0 && !alredey_append) {
						--k;
						if (cnv->has_same_vehicles(line->get_convoy(k))) {
							alredey_append=true;
						}
					}
					if (alredey_append) {
						continue;
					}
					own_convoys.append(cnv);
				}
			}
		}
	}
	if (search_only_halt_convoy) {
		// halt convoy
		for (uint32 i=0; i < halt->registered_convoys.get_count(); ++i) {
			const convoihandle_t cnv = halt->registered_convoys[i];
			if( cnv->get_owner()==player  &&cnv->front()->get_waytype()==schedule->get_waytype() ) {
				// filter
				if( filter_catg!=255  &&  filter_catg!=goods_manager_t::INDEX_NONE  &&  !cnv->get_goods_catg_index().is_contained(filter_catg) ) {
					continue;
				}
				own_convoys.append(cnv);
			}
		}
	}

	// update selector
	for (auto &own_veh : own_vehicles) {
		if (own_veh.veh_type == old_vehicle) {
			selected_vehicle = old_vehicle;
		}
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_veh);
		if (own_veh.veh_type == selected_vehicle) {
			// reselect
			scl_vehicles.set_selection(scl_vehicles.get_count()-1);
		}
	}
	if( !own_vehicles.get_count() ) {
		scl_vehicles.new_component<vehicle_scrollitem_t>(own_vehicle_t());
	}
	// NOTE: list refresh (sort + margins + scroll reset) happens once at the
	// end of this function via refresh(), after both lists are filled.
	if (!selected_vehicle) {
		// close vehicle details
		destroy_win(magic_vehicle_detail_for_consist_order);
	}

	bool found=false;
	for (auto &own_cnv : own_convoys) {
		scl_convoys.new_component<consist_scrollitem_t>(own_cnv, false);
		// select the same one again
		if (selected_convoy==own_cnv) {
			scl_convoys.set_selection(scl_convoys.get_count()-1);
			found = true;
		}
	}
	if (!own_convoys.get_count()) {
		scl_convoys.new_component<consist_scrollitem_t>(convoihandle_t(), false);
	}
	// Reset the containers to the current content and restart at the top;
	// without the reset the convoy container keeps a stale (taller) size after
	// filtering down, and without the scroll reset stale offsets push rows down.
	scl_vehicles.refresh();
	scl_convoys.refresh();
	if (!found) {
		// The selected one has been lost from the list. So turn off the display as well. Otherwise the execute button will cause confusion.
		selected_convoy = convoihandle_t();
	}
	update_convoy_info();
	reset_min_windowsize();
	resize(scr_size(0,0));
}


bool consist_order_frame_t::is_filtered(const vehicle_desc_t *veh_type)
{
	const int filter_engine_type = engine_filter.get_selection();
	if (filter_catg != 255 && veh_type->get_freight_type()->get_catg_index() != filter_catg) {
		return true;
	}
	// Inclusive pair, consistent with the outdated/obsolete toggles below:
	// an unchecked class is hidden; both off shows nothing.
	if (!bt_show_powered.pressed && veh_type->get_power()) {
		return true;
	}
	else if (!bt_show_unpowered.pressed && !veh_type->get_power()) {
		return true;
	}
	if (filter_engine_type > 0 && (uint8)veh_type->get_engine_type() != filter_engine_type - 1) {
		return true;
	}
	if (!bt_outdated.pressed && veh_type->get_vehicle_status_color() == SYSCOL_OUT_OF_PRODUCTION) {
		return true;
	}
	if (!bt_obsolete.pressed && veh_type->get_vehicle_status_color() == SYSCOL_OBSOLETE) {
		return true;
	}
	if (!bt_show_unidirectional.pressed && !veh_type->is_bidirectional()) {
		return true;
	}

	return false;
}

// reflesh labels, call when entry changed
void consist_order_frame_t::update()
{
	halt = halthandle_t();
	old_entry_count = schedule->get_count();
	// serach entry and halt
	uint8 entry_idx = 255;
	if (old_entry_count) {
		for (uint i = 0; i < schedule->entries.get_count(); i++) {
			if( unique_entry_id==schedule->entries[i].unique_entry_id ) {
				entry_idx = i;
				halt = haltestelle_t::get_halt(schedule->entries[i].pos, player);
				if( halt.is_bound() ) {
					uint8 halt_symbol_style = 0;
					if ((halt->registered_lines.get_count() + halt->registered_convoys.get_count()) > 1) {
						halt_symbol_style = gui_schedule_entry_number_t::number_style::interchange;
					}
					halt_number.init(i, halt->get_owner()->get_player_color1(), halt_symbol_style, schedule->entries[i].pos);
				}
				break;
			}
		}
		if( !halt.is_bound() ) { destroy_win(this); }

		lb_halt.buf().append(halt->get_name());
	}
	init_input_value_range();
	lb_halt.update();
	resize(scr_size(0,0));
}


void consist_order_frame_t::open_rule_editor(consist_order_t *edit_order, uint32 slot_index, uint32 alt_index)
{
	consist_rule_editor_t *win = dynamic_cast<consist_rule_editor_t*>(win_get_magic(magic_consist_rule_editor));
	if (!win) {
		create_win(new consist_rule_editor_t(player, edit_order, slot_index, alt_index, schedule->get_waytype()), w_info, magic_consist_rule_editor);
	}
	else {
		win->retarget(player, edit_order, slot_index, alt_index, schedule->get_waytype());
		top_win(win, false);
	}
}


void consist_order_frame_t::open_vehicle_detail(const vehicle_desc_t* veh_type) const
{
	if (veh_type) {
		vehicle_detail_t *win = dynamic_cast<vehicle_detail_t*>(win_get_magic(magic_vehicle_detail_for_consist_order));
		if (!win) {
			// try to open to the right
			scr_coord sc = win_get_pos(this);
			scr_coord lc = sc;
			lc.x += get_windowsize().w;
			if (lc.x > display_get_width()) {
				lc.x = max(0, display_get_width() - 100);
			}
			create_win(lc, new vehicle_detail_t(selected_vehicle), w_info, magic_vehicle_detail_for_consist_order, true);
			top_win(this, false); // Keyscroll should be enabled continuously. This window must remain on topmost.
		}
		else {
			win->set_vehicle(selected_vehicle);
			////top_win(win, false); // NOTE: Keyscroll should be enabled continuously. So don't bring the new window to topmost.
		}
	}
}
