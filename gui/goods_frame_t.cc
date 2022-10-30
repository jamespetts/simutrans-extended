/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <algorithm>

#include "goods_frame_t.h"
#include "components/gui_image.h"
#include "components/gui_scrollpane.h"

#include "../bauer/goods_manager.h"
#include "../descriptor/goods_desc.h"
#include "../dataobj/translator.h"

// For waytype_t
#include "../simtypes.h"

#include "../simcolor.h"
#include "simwin.h"
#include "../simworld.h"
#include "../simconvoi.h"

// For revenue stuff
#include "../descriptor/goods_desc.h"

/**
 * This variable defines the current speed for the purposes of calculating
 * journey time, which in turn affects comfort. Adaopted from the old speed
 * bonus code, which was put into its present form by Neroden circa 2013.
 */
uint32 goods_frame_t::vehicle_speed = 50;

/**
 * This variable defines by which column the table is sorted
 * Values: 0 = Unsorted (passengers and mail first)
 *         1 = Alphabetical
 *         2 = Revenue
 */
goods_frame_t::sort_mode_t goods_frame_t::sortby = by_number;
static uint8 default_sortmode = 0;

/**
 * This variable defines the sort order (ascending or descending)
 * Values: 1 = ascending, 2 = descending)
 */
bool goods_frame_t::sortreverse = false;

uint32 goods_frame_t::distance_meters = 1000;
uint16 goods_frame_t::distance = 1;
uint8 goods_frame_t::comfort = 50;
uint8 goods_frame_t::catering_level = 0;
uint8 goods_frame_t::selected_goods = goods_manager_t::INDEX_PAS;
uint8 goods_frame_t::g_class = 0;
uint8 goods_frame_t::display_mode = 0;

const char *goods_frame_t::sort_text[SORT_MODES] = {
	"gl_btn_unsort",
	"gl_btn_sort_name",
	"gl_btn_sort_revenue",
	"gl_btn_sort_catg",
	"gl_btn_sort_weight"
};

/**
 * This variable controls whether all goods are displayed, or
 * just the ones relevant to the current game
 * Values: false = all goods shown, true = relevant goods shown
 */
bool goods_frame_t::filter_goods = false;

goods_frame_t::goods_frame_t() :
	gui_frame_t( translator::translate("gl_title") ),
	goods_stats(),
	scrolly(&goods_stats)
{
	set_table_layout(1, 0);

	add_table(3,1)->set_alignment(ALIGN_TOP);
	{
		show_hide_input.init(button_t::roundbox, "+");
		show_hide_input.set_width(display_get_char_width('+') + D_BUTTON_PADDINGS_X);
		show_hide_input.add_listener(this);
		add_component(&show_hide_input);

		lb_collapsed.set_text("Open the fare calculation input field");
		add_component(&lb_collapsed);

		speed[0] = 0;

		input_container.set_table_layout(2,5);
		input_container.new_component<gui_label_t>("distance");

		distance_txt[0] = 0;
		comfort_txt[0] = 0;
		catering_txt[0] = 0;
		class_txt[0] = 0;
		distance_meters = (sint32)1000 * distance;

		distance_input.set_limits( 1, 9999 );
		distance_input.set_value( distance );
		distance_input.wrap_mode( false );
		distance_input.add_listener( this );
		input_container.add_component(&distance_input);

		input_container.add_table(2,1);
		{
			input_container.new_component<gui_image_t>(skinverwaltung_t::comfort ? skinverwaltung_t::comfort->get_image_id(0) : IMG_EMPTY, 0, ALIGN_NONE, true);
			input_container.new_component<gui_label_t>("Comfort");
		}
		input_container.end_table();
		comfort_input.set_limits( 1, 255 );
		comfort_input.set_value( comfort );
		comfort_input.wrap_mode( false );
		comfort_input.add_listener( this );
		input_container.add_component(&comfort_input);

		input_container.new_component<gui_label_t>("Catering level");
		catering_input.set_limits( 0, 5 );
		catering_input.set_value( catering_level );
		catering_input.wrap_mode( false );
		catering_input.add_listener( this );
		input_container.add_component(&catering_input);

		input_container.new_component<gui_label_t>("Average speed");
		speed_input.set_limits(19, 9999);
		speed_input.set_value(vehicle_speed);
		speed_input.wrap_mode(false);
		speed_input.add_listener(this);
		input_container.add_component(&speed_input);

		input_container.new_component<gui_label_t>("Class");
		class_input.set_limits(0, max(goods_manager_t::passengers->get_number_of_classes() - 1, goods_manager_t::mail->get_number_of_classes() - 1)); // TODO: Extrapolate this to show the class names as well as just the number
		class_input.set_value(g_class);
		class_input.wrap_mode(false);
		class_input.add_listener(this);
		input_container.add_component(&class_input);

		input_container.set_visible(false);

		add_component(&input_container);
	}
	end_table();

	// sort mode
	cont_goods_list.set_table_layout(1,0);
	cont_goods_list.add_table(3,1);
	{
		cont_goods_list.add_table(2,2);
		{
			cont_goods_list.new_component_span<gui_label_t>("hl_txt_sort", 2);

			for (int i = 0; i < SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(default_sortmode);
			sortedby.set_width_fixed(true);
			sortedby.set_size(scr_size((D_BUTTON_WIDTH*3)>>1, D_EDIT_HEIGHT));
			sortedby.add_listener(this);
			cont_goods_list.add_component(&sortedby);

			// sort asc/desc switching button
			sort_order.init(button_t::sortarrow_state, "");
			sort_order.set_tooltip(translator::translate("hl_btn_sort_order"));
			sort_order.add_listener(this);
			sort_order.pressed = sortreverse;
			cont_goods_list.add_component(&sort_order);
		}
		cont_goods_list.end_table();

		cont_goods_list.new_component<gui_margin_t>(LINESPACE);


		cont_goods_list.add_table(3,2)->set_spacing(scr_size(0,D_V_SPACE));
		{
			mode_switcher[0].init(button_t::roundbox_left_state,  "gl_normal");
			mode_switcher[1].init(button_t::roundbox_middle_state, NULL);
			mode_switcher[2].init(button_t::roundbox_right_state,  NULL);
			if (skinverwaltung_t::input_output) {
				mode_switcher[1].set_image(skinverwaltung_t::input_output->get_image_id(1));
				mode_switcher[2].set_image(skinverwaltung_t::input_output->get_image_id(0));
			}
			else {
				mode_switcher[1].set_text("gl_prod");
				mode_switcher[2].set_text("gl_con");
			}
			mode_switcher[1].set_tooltip("Show producers");
			mode_switcher[2].set_tooltip("Show consumers");

			for (uint8 i = 0; i < 3; i++) {
				mode_switcher[i].pressed = display_mode == i;
				mode_switcher[i].set_width(D_BUTTON_WIDTH>>1);
				mode_switcher[i].add_listener(this);
				cont_goods_list.add_component(&mode_switcher[i]);
			}

			filter_goods_toggle.init(button_t::square_state, "Show only used");
			filter_goods_toggle.set_tooltip(translator::translate("Only show goods which are currently handled by factories"));
			filter_goods_toggle.add_listener(this);
			filter_goods_toggle.pressed = filter_goods;
			cont_goods_list.add_component(&filter_goods_toggle,3);
		}
		cont_goods_list.end_table();
	}
	cont_goods_list.end_table();

	cont_goods_list.add_component(&scrolly);
	scrolly.set_maximize(true);

	sort_list();

	// chart
	cont_fare_chart.set_table_layout(1,0);

	cont_fare_chart.add_table(3,1);
	{
		// goods selector
		FOR(vector_tpl<goods_desc_t const*>, const i, world()->get_goods_list()) {
			goods_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(i->get_name()), SYSCOL_TEXT);
		}
		goods_selector.set_selection(0);
		goods_selector.add_listener(this);
		cont_fare_chart.add_component(&goods_selector);

		lb_no_speed_bonus.init("no_speed_bonus");
		lb_no_speed_bonus.set_rigid(true);
		cont_fare_chart.add_component(&lb_no_speed_bonus);
		cont_fare_chart.new_component<gui_fill_t>();
	}
	cont_fare_chart.end_table();
	cont_fare_chart.add_component(&lb_selected_class);

	cont_fare_short.set_table_layout(2,1);
	cont_fare_short.set_alignment(ALIGN_BOTTOM);
	cont_fare_short.add_component(&chart_s);
	cont_fare_short.new_component<gui_label_t>("[km]", SYSCOL_TEXT_HIGHLIGHT);
	chart_s.set_ltr(2);
	chart_s.set_dimension(FARE_RECORDS, 10000);
	chart_s.set_background(SYSCOL_CHART_BACKGROUND);
	chart_s.set_x_axis_span(5);
	chart_s.show_curve(0);
	chart_s.set_min_size(scr_size(0, 6*LINESPACE));

	cont_fare_long.set_table_layout(2,1);
	cont_fare_long.set_alignment(ALIGN_BOTTOM);
	cont_fare_long.add_component(&chart_l);
	cont_fare_long.new_component<gui_label_t>("[km]", SYSCOL_TEXT_HIGHLIGHT);
	chart_l.set_ltr(2);
	chart_l.set_dimension(FARE_RECORDS, 10000);
	chart_l.set_background(SYSCOL_CHART_BACKGROUND);
	chart_l.set_x_axis_span(50);
	chart_l.show_curve(0);
	chart_l.set_min_size(scr_size(0, 6*LINESPACE));

	update_fare_charts();


	tabs_chart.add_tab(&cont_fare_short, translator::translate("fare_short"));
	tabs_chart.add_tab(&cont_fare_long,  translator::translate("fare_long"));
	cont_fare_chart.add_component(&tabs_chart);

	tabs.add_tab(&cont_goods_list, translator::translate("Goods list"));
	tabs.add_tab(&cont_fare_chart, translator::translate("fare_chart"));

	// comfort chart
#ifdef DEBUG
	cont_comfort_chart.set_table_layout(1,0);
	cont_comfort_chart.new_component<gui_label_t>("(Max. comfortable journey time: ");
	cont_comfort_chart.new_component<gui_margin_t>(1,LINEASCENT>>1);
	cont_comfort_chart.add_component(&comfort_chart);
	comfort_chart.set_ltr(2);
	comfort_chart.set_dimension(COMFORT_RECORDS, 86400);
	comfort_chart.set_background(SYSCOL_CHART_BACKGROUND);
	comfort_chart.set_x_axis_span(5);
	comfort_chart.show_curve(0);
	comfort_chart.set_min_size(scr_size(0, 6*LINESPACE));
	for (uint8 i = 0; i < COMFORT_RECORDS; i++) {
		comfort_curve[i] = world()->get_settings().max_tolerable_journey(i*5);
	}
	comfort_chart.add_curve(15911, (sint64*)comfort_curve, 1, 0, COMFORT_RECORDS, gui_chart_t::TIME, true, false, 0);
	tabs.add_tab(&cont_comfort_chart, translator::translate("comfort_chart"));
#endif

	add_component(&tabs);

	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	set_resizemode(diagonal_resize);
}


bool goods_frame_t::compare_goods(goods_desc_t const* const w1, goods_desc_t const* const w2)
{
	int order = 0;

	switch (sortby)
	{
		case by_number:
			order = w1->get_index() - w2->get_index();
			break;
		case by_revenue:
			{
				sint64 price[2];
				const uint16 journey_tenths = (uint16)tenths_from_meters_and_kmh(distance_meters, vehicle_speed);
				price[0] = w1->get_total_fare(distance_meters, 0, comfort, catering_level, min(g_class, w1->get_number_of_classes() - 1), journey_tenths);
				price[1] = w2->get_total_fare(distance_meters, 0, comfort, catering_level, min(g_class, w2->get_number_of_classes() - 1), journey_tenths);

				order = price[0] - price[1];
			}
			break;
		case by_category:
			order = w1->get_catg() - w2->get_catg();
			break;
		case by_weight:
			order = w1->get_weight_per_unit() - w2->get_weight_per_unit();
		default: ; // make compiler happy, order will be determined below anyway
	}
	if(  order==0  ) {
		// sort by name if not sorted or not unique
		order = strcmp(translator::translate(w1->get_name()), translator::translate(w2->get_name()));
	}
	return sortreverse ? order > 0 : order < 0;
}


// creates the list and pass it to the child function good_stats, which does the display stuff ...
void goods_frame_t::sort_list()
{
	// Fetch the list of goods produced by the factories that exist in the current game
	const vector_tpl<const goods_desc_t*> &goods_in_game = welt->get_goods_list();

	good_list.clear();
	for(unsigned int i=0; i<goods_manager_t::get_count(); i++) {
		const goods_desc_t * wtyp = goods_manager_t::get_info(i);

		// Skip goods not in the game
		// Do not skip goods which don't generate income -- it makes it hard to debug paks
		// Do skip the special good "None"
		if(  (wtyp != goods_manager_t::none) && (!filter_goods || goods_in_game.is_contained(wtyp))  ) {
			good_list.insert_ordered(wtyp, compare_goods);
		}
	}

	goods_stats.update_goodslist(good_list, vehicle_speed, goods_frame_t::distance_meters, goods_frame_t::comfort, goods_frame_t::catering_level, g_class, goods_frame_t::display_mode);
}


void goods_frame_t::update_fare_charts()
{
	chart_s.remove_curves();
	chart_l.remove_curves();

	const goods_desc_t * wtyp = welt->get_goods_list()[selected_goods];

	lb_no_speed_bonus.set_visible( (wtyp->get_speed_bonus()==0) );
	if (selected_goods < goods_manager_t::INDEX_NONE) {
		// fare class name
		lb_selected_class.buf().append(goods_manager_t::get_translated_fare_class_name(selected_goods, g_class));
		lb_selected_class.set_color(SYSCOL_TEXT);
	}
	else {
		lb_selected_class.buf().append(translator::translate("has_no_class"));
		lb_selected_class.set_color(SYSCOL_TEXT_INACTIVE);
	}
	lb_selected_class.update();

	uint32 temp_speed = vehicle_speed;
	if (temp_speed <= 0) {
		// Negative and zero speeds will be due to roundoff errors
		temp_speed = 1;
	}

	for (uint8 i = 0; i < FARE_RECORDS; i++) {
		const uint32 dist_s = 5000 * i;
		const sint64 journey_tenths_s = tenths_from_meters_and_kmh(dist_s, temp_speed);
		const sint64 journey_tenths_l = tenths_from_meters_and_kmh(dist_s * 10, temp_speed);

		sint64 revenue_s = wtyp->get_total_fare(dist_s, 0u, comfort, catering_level, min(g_class, wtyp->get_number_of_classes() - 1), journey_tenths_s);
		sint64 revenue_l = wtyp->get_total_fare(dist_s * 10, 0u, comfort, catering_level, min(g_class, wtyp->get_number_of_classes() - 1), journey_tenths_l);

		// Convert to simucents.  Should be very fast.
		fare_curve_s[i] = (revenue_s + 2048) / 4096;
		fare_curve_l[i] = (revenue_l + 2048) / 4096;
	}
	chart_s.add_curve(wtyp->get_color(), (sint64*)fare_curve_s, 1, 0, FARE_RECORDS, gui_chart_t::MONEY, true, false, 2);
	chart_l.add_curve(wtyp->get_color(), (sint64*)fare_curve_l, 1, 0, FARE_RECORDS, gui_chart_t::MONEY, true, false, 2);
}


/**
 * This method is called if an action is triggered
 */
bool goods_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if(comp == &sortedby) {
		// sort by what
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			sortby = (goods_frame_t::sort_mode_t)tmp;
		}
		else {
			sortedby.set_selection(0);
			sortby = goods_frame_t::by_number;
		}
		default_sortmode = (uint8)tmp;
		sort_list();
	}
	else if (comp == &sort_order) {
		// order
		sortreverse ^= 1;
		sort_list();
		sort_order.pressed = sortreverse;
	}
	else if (comp == &mode_switcher[0] || comp == &mode_switcher[1] || comp == &mode_switcher[2]) {
		// switch the list display mode
		display_mode = comp==&mode_switcher[0] ? 0 : comp==&mode_switcher[1] ? 1 : 2;
		for (uint8 i=0; i<3; i++) {
			mode_switcher[i].pressed = i==display_mode;
		}
		sort_list();
	}
	else if (comp == &speed_input) {
		vehicle_speed = v.i;
		sort_list();
		update_fare_charts();
	}
	else if (comp == &distance_input) {
		distance = v.i;
		distance_meters = (sint32)1000 * distance;
		sort_list();
	}
	else if (comp == &comfort_input) {
		comfort = v.i;
		sort_list();
		if (selected_goods == goods_manager_t::INDEX_PAS) {
			update_fare_charts();
		}
	}
	else if (comp == &catering_input) {
		catering_level = v.i;
		sort_list();
		if (selected_goods<goods_manager_t::INDEX_NONE) {
			update_fare_charts();
		}
	}
	else if (comp == &class_input) {
		g_class = v.i;
		sort_list();
		if (selected_goods < goods_manager_t::INDEX_NONE) {
			update_fare_charts();
		}
	}
	else if(comp == &filter_goods_toggle) {
		filter_goods = !filter_goods;
		filter_goods_toggle.pressed = filter_goods;
		sort_list();
	}
	else if (comp == &show_hide_input) {
		show_input = !show_input;
		show_hide_input.set_text(show_input ? "-" : "+");
		show_hide_input.pressed = show_input;
		input_container.set_visible(show_input);
		lb_collapsed.set_visible(!show_input);
		reset_min_windowsize();
	}
	else if (comp == &goods_selector) {
		selected_goods=goods_selector.get_selection();
		update_fare_charts();
	}

	return true;
}


/**
 * Draw the component
 */
void goods_frame_t::draw(scr_coord pos, scr_size size)
{
	gui_frame_t::draw(pos, size);
}


uint32 goods_frame_t::get_rdwr_id()
{
	return magic_goodslist;
}


void goods_frame_t::rdwr(loadsave_t *file)
{
	file->rdwr_short(distance);
	file->rdwr_byte(comfort);
	file->rdwr_byte(catering_level);
	file->rdwr_long(vehicle_speed);
	file->rdwr_byte(g_class);
	file->rdwr_bool(sort_order.pressed);
	file->rdwr_bool(filter_goods_toggle.pressed);
	uint8 s = default_sortmode;
	file->rdwr_byte(s);
	sint16 b = sortby;
	file->rdwr_short(b);

	if (file->is_loading()) {
		sortedby.set_selection(s);
		sortby = (sort_mode_t)b;
		sortreverse = sort_order.pressed;
		sort_list();
		distance_input.set_value(distance);
		comfort_input.set_value(comfort);
		catering_input.set_value(catering_level);
		speed_input.set_value(vehicle_speed);
		class_input.set_value(g_class);
		filter_goods = filter_goods_toggle.pressed;
		default_sortmode = s;
	}
}
