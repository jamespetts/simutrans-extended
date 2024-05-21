/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "citylist_frame_t.h"
#include "citylist_stats_t.h"


#include "../dataobj/translator.h"
#include "../unicode.h"
#include "../simcolor.h"
#include "../dataobj/environment.h"
#include "components/gui_button_to_chart.h"


/**
 * This variable defines by which column the table is sorted
 * Values: 0 = Station number
 *         1 = Station name
 *         2 = Waiting goods
 *         3 = Station type
 */
citylist_stats_t::sort_mode_t citylist_frame_t::sortby = citylist_stats_t::SORT_BY_NAME;


const char *citylist_frame_t::sort_text[citylist_stats_t::SORT_MODES] = {
	"Name",
	"citicens",
	"Growth",
	"Jobs",
	"Passagierrate",
	"Public transport users",
	"ratio_pax",
	"sended",
	"ratio_mail",
	"Goods needed",
	"Goods supplied",
	"ratio_goods",
	"City size",
	"Population density",
	"by_region"
#ifdef DEBUG
	, "Unemployed"
	, "Homeless"
#endif // DEBUG
};

const char *citylist_frame_t::display_mode_text[citylist_stats_t::CITYLIST_MODES] =
{
	"cl_general",
	"citicens",
	"Jobs",
	"Visitor demand",
	"pax_traffic",
	"mail_traffic",
	"goods_traffic",
	"Needed",
	"Products"
#ifdef DEBUG
	, "DBG_demands"
#endif // DEBUG
};

const char citylist_frame_t::hist_type[karte_t::MAX_WORLD_COST][21] =
{
	"citicens",
	"Jobs",
	"Visitor demand",
	"Growth",
	"Towns",
	"Factories",
	"Convoys",
	"Verkehrsteilnehmer",
	"ratio_pax",
	"Passagiere",
	"ratio_mail",
	"Post",
	"ratio_goods",
	"Goods",
	"Car ownership"
};

const char citylist_frame_t::hist_type_tooltip[karte_t::MAX_WORLD_COST][256] =
{
	"The number of inhabitants of the region",
	"The number of jobs in the region",
	"The number of visitors demanded in the region",
	"The number of inhabitants by which the region has increased",
	"The number of urban areas in the region",
	"The number of industries in the region",
	"The total number of convoys in the region",
	"The number of private car trips in the region overall",
	"The percentage of passengers transported in the region overall",
	"The total number of passengers wanting to be transported in the region",
	"The amount of mail transported in the region overall",
	"The total amount of mail generated in the region",
	"The percentage of available goods that have been transported in the region",
	"The total number of mail/passengers/goods transported in the region",
	"The percentage of people who have access to a private car"
};

const uint8 citylist_frame_t::hist_type_color[karte_t::MAX_WORLD_COST] =
{
	COL_DARK_GREEN+1,
	COL_COMMUTER,
	COL_LIGHT_PURPLE,
	COL_DARK_GREEN,
	COL_GREY3,
	71 /*COL_GREEN*/,
	COL_TURQUOISE,
	COL_TRAFFIC,
	COL_LIGHT_BLUE,
	COL_PASSENGERS,
	COL_LIGHT_YELLOW,
	COL_YELLOW,
	COL_LIGHT_BROWN,
	COL_BROWN,
	COL_CAR_OWNERSHIP
};

const uint8 citylist_frame_t::hist_type_type[karte_t::MAX_WORLD_COST] =
{
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::STANDARD,
	gui_chart_t::PERCENT,
	gui_chart_t::STANDARD,
	gui_chart_t::PERCENT,
	gui_chart_t::TON_KM,
	gui_chart_t::PERCENT,
	gui_chart_t::TON_KM,
	gui_chart_t::PERCENT
};

char citylist_frame_t::name_filter[256] = "";


citylist_frame_t::citylist_frame_t() :
	gui_frame_t(translator::translate("City list")),
	scrolly(gui_scrolled_list_t::windowskin, citylist_stats_t::compare)
{
	set_table_layout(1, 0);

	add_table(3,1);
	{
		add_component(&citizens);

		fluctuation_world.set_show_border_value(false);
		add_component(&fluctuation_world);

		new_component<gui_fill_t>();
	}
	end_table();

#ifdef DEBUG
	add_table(4,1);
	{
		new_component<gui_label_t>("(DBG)wolrd's worker needs factor:");
		add_component(&lb_worker_shortage);
		new_component<gui_label_t>("(DBG)job_shortage:");
		add_component(&lb_job_shortage);
	}
	end_table();
#endif

	add_component(&main);
	main.add_tab(&list, translator::translate("City list"));

	list.set_table_layout(1, 0);

	list.add_table(2,2);
	{
		// 1st row
		list.new_component<gui_label_t>("hl_txt_sort");

		list.add_table(3,1);
		{
			if( skinverwaltung_t::search ) {
				list.new_component<gui_image_t>(skinverwaltung_t::search->get_image_id(0), 0, ALIGN_NONE, true)->set_tooltip(translator::translate("Filter:"));
			}
			else {
				list.new_component<gui_label_t>("Filter:");
			}
			name_filter_input.set_text(name_filter, lengthof(name_filter));
			name_filter_input.set_width(D_BUTTON_WIDTH);
			name_filter_input.set_search_box(true);
			list.add_component(&name_filter_input);

			filter_within_network.init(button_t::square_state, "Within own network");
			filter_within_network.set_tooltip("Show only cities within the active player's transportation network");
			filter_within_network.add_listener(this);
			filter_within_network.pressed = citylist_stats_t::filter_own_network;
			list.add_component(&filter_within_network);
		}
		list.end_table();

		// 2nd row
		list.add_table(2,1);
		{
			for (int i = 0; i < citylist_stats_t::SORT_MODES; i++) {
				sortedby.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(sort_text[i]), SYSCOL_TEXT);
			}
			sortedby.set_selection(citylist_stats_t::sort_mode);
			sortedby.set_width_fixed(true);
			sortedby.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
			sortedby.add_listener(this);
			list.add_component(&sortedby);

			// sort ascend/descend switching button
			sorteddir.init(button_t::sortarrow_state, "");
			sorteddir.set_tooltip(translator::translate("hl_btn_sort_order"));
			sorteddir.add_listener(this);
			sorteddir.pressed = citylist_stats_t::sortreverse;
			list.add_component(&sorteddir);
		}
		list.end_table();

		list.add_table(3,1);
		{
			list.new_component<gui_margin_t>(LINESPACE);
			if (!world()->get_settings().regions.empty()) {
				//region_selector
				region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate("All regions"), SYSCOL_TEXT);

				for (uint8 r = 0; r < world()->get_settings().regions.get_count(); r++) {
					region_selector.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(world()->get_settings().regions[r].name.c_str()), SYSCOL_TEXT);
				}
				region_selector.set_selection(citylist_stats_t::region_filter);
				region_selector.set_width_fixed(true);
				region_selector.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
				region_selector.add_listener(this);
				list.add_component(&region_selector);
			}
			else {
				list.new_component<gui_empty_t>();
			}

			for (uint8 i = 0; i < citylist_stats_t::CITYLIST_MODES; i++) {
				cb_display_mode.new_component<gui_scrolled_list_t::const_text_scrollitem_t>(translator::translate(display_mode_text[i]), SYSCOL_TEXT);
			}
			cb_display_mode.set_selection(citylist_stats_t::display_mode);
			cb_display_mode.set_width_fixed(true);
			cb_display_mode.set_size(scr_size(D_WIDE_BUTTON_WIDTH, D_EDIT_HEIGHT));
			cb_display_mode.add_listener(this);
			list.add_component(&cb_display_mode);
		}
		list.end_table();
	}
	list.end_table();

	list.add_component(&scrolly);
	fill_list();

	main.add_tab(&statistics, translator::translate("Chart"));

	statistics.set_table_layout(1, 0);
	statistics.add_component(&year_month_tabs);

	year_month_tabs.add_tab(&container_year, translator::translate("Years"));
	year_month_tabs.add_tab(&container_month, translator::translate("Months"));
	// .. put the same buttons in both containers
	button_t* buttons[karte_t::MAX_WORLD_COST];

	container_year.set_table_layout(1, 0);
	container_year.add_component(&chart);
	chart.set_dimension(12, karte_t::MAX_WORLD_COST*MAX_WORLD_HISTORY_YEARS);
	chart.set_background(SYSCOL_CHART_BACKGROUND);
	chart.set_ltr(env_t::left_to_right_graphs);
	chart.set_min_size(scr_size(0, 7*LINESPACE));

	container_year.add_table(4,0);
	for (int i = 0; i<karte_t::MAX_WORLD_COST; i++) {
		sint16 curve = chart.add_curve(color_idx_to_rgb(hist_type_color[i]), world()->get_finance_history_year(), karte_t::MAX_WORLD_COST, i,
			MAX_WORLD_HISTORY_YEARS, hist_type_type[i], false, true, (hist_type_type[i]==gui_chart_t::PERCENT && i != karte_t::WORLD_CAR_OWNERSHIP) ? 2 : 0,
			0, hist_type_type[i]==gui_chart_t::PERCENT ? gui_chart_t::cross : gui_chart_t::square);
		// add button
		buttons[i] = container_year.new_component<button_t>();
		buttons[i]->init(button_t::box_state_automatic | button_t::flexible, hist_type[i]);
		buttons[i]->set_tooltip(hist_type_tooltip[i]);
		buttons[i]->background_color = color_idx_to_rgb(hist_type_color[i]);
		buttons[i]->pressed = false;

		button_to_chart.append(buttons[i], &chart, curve);
	}
	container_year.end_table();

	container_month.set_table_layout(1,0);
	container_month.add_component(&mchart);
	mchart.set_dimension(12, karte_t::MAX_WORLD_COST*MAX_WORLD_HISTORY_MONTHS);
	mchart.set_background(SYSCOL_CHART_BACKGROUND);
	mchart.set_ltr(env_t::left_to_right_graphs);
	mchart.set_min_size(scr_size(0, 7*LINESPACE));

	container_month.add_table(4,0);
	for (int i = 0; i<karte_t::MAX_WORLD_COST; i++) {
		sint16 curve = mchart.add_curve(color_idx_to_rgb(hist_type_color[i]), world()->get_finance_history_month(), karte_t::MAX_WORLD_COST, i,
			MAX_WORLD_HISTORY_MONTHS, hist_type_type[i], false, true, (hist_type_type[i]==gui_chart_t::PERCENT && i != karte_t::WORLD_CAR_OWNERSHIP) ? 2 : 0);

		// add button
		container_month.add_component(buttons[i]);
		button_to_chart.append(buttons[i], &mchart, curve);
	}
	container_month.end_table();

	update_label();

	set_resizemode(diagonal_resize);
	scrolly.set_maximize(true);
	reset_min_windowsize();
}


void citylist_frame_t::update_label()
{
	citizens.buf().append(translator::translate("Total inhabitants:"));
	citizens.buf().append(world()->get_finance_history_month()[0], 0);
	citizens.update();

	fluctuation_world.set_value(world()->get_finance_history_month(1, karte_t::WORLD_GROWTH));

#ifdef DEBUG
	const sint64 world_jobs = world()->get_finance_history_month(0, karte_t::WORLD_JOBS);
	const sint64 monthly_job_demand_global = world()->calc_monthly_job_demand();

	// If this is negative, the new city will not be able to build a single house to begin with,
	// so citybuilding will not build anything and the process will be aborted.
	const sint64 res_needs_factor = (world_jobs * 100l) - (monthly_job_demand_global * 110l);
	lb_worker_shortage.buf().append(res_needs_factor,0);
	lb_worker_shortage.set_color(res_needs_factor<0? COL_DANGER:SYSCOL_TEXT);
	lb_worker_shortage.set_tooltip("This should not be a negative!");
	lb_worker_shortage.update();

	const sint64 job_needs_factor = (monthly_job_demand_global * 100l) - (world_jobs * 110l);
	lb_job_shortage.buf().append(job_needs_factor,0);
	lb_job_shortage.update();

#endif

}


void citylist_frame_t::fill_list()
{
	scrolly.clear_elements();
	strcpy(last_name_filter, name_filter);
	for(stadt_t* city : world()->get_cities()) {
		if (citylist_stats_t::region_filter && (citylist_stats_t::region_filter-1) != world()->get_region(city->get_pos())) {
			continue;
		}
		if (last_name_filter[0] != 0 && !utf8caseutf8(city->get_name(), last_name_filter)) {
			continue;
		}

		if (!citylist_stats_t::filter_own_network ||
			(citylist_stats_t::filter_own_network && city->is_within_players_network(world()->get_active_player()))) {
			scrolly.new_component<citylist_stats_t>(city);
		}
	}
	scrolly.sort(0);
	scrolly.set_size(scrolly.get_size());
}


bool citylist_frame_t::action_triggered( gui_action_creator_t *comp,value_t v)
{
	if(comp == &sortedby) {
		citylist_stats_t::sort_mode = max(0, v.i);
		scrolly.sort(0);
	}
	else if (comp == &region_selector) {
		citylist_stats_t::region_filter = max(0, v.i);
		fill_list();
	}
	else if (comp == &sorteddir) {
		citylist_stats_t::sortreverse = !citylist_stats_t::sortreverse;
		scrolly.sort(0);
		sorteddir.pressed = citylist_stats_t::sortreverse;
	}
	else if (comp == &cb_display_mode) {
		int tmp = cb_display_mode.get_selection();
		if (tmp < 0 || tmp >= cb_display_mode.count_elements())
		{
			tmp = 0;
		}
		cb_display_mode.set_selection(tmp);
		citylist_stats_t::display_mode = tmp;
		citylist_stats_t::recalc_wold_max();
		scrolly.sort(0);
		resize(scr_coord(0,0));
	}
	else if (comp == &filter_within_network) {
		citylist_stats_t::filter_own_network = !citylist_stats_t::filter_own_network;
		filter_within_network.pressed = citylist_stats_t::filter_own_network;
		fill_list();
		scrolly.sort(0);
	}
	return true;
}


void citylist_frame_t::draw(scr_coord pos, scr_size size)
{
	world()->update_history();

	if(  (sint32)world()->get_cities().get_count() != scrolly.get_count()  ||  strcmp(last_name_filter, name_filter)  ) {
		fill_list();
	}
	update_label();

	gui_frame_t::draw(pos, size);
}



void citylist_frame_t::rdwr(loadsave_t* file)
{
	scr_size size = get_windowsize();

	size.rdwr(file);
	scrolly.rdwr(file);
	year_month_tabs.rdwr(file);
	main.rdwr(file);
	//filterowner.rdwr(file);
	sortedby.rdwr(file);
	file->rdwr_str(name_filter, lengthof(name_filter));
	file->rdwr_byte(citylist_stats_t::sort_mode);
	file->rdwr_byte(citylist_stats_t::region_filter);
	file->rdwr_bool(citylist_stats_t::sortreverse);
	file->rdwr_bool(citylist_stats_t::filter_own_network);
	if (file->is_version_ex_atleast(14,50)) {
		file->rdwr_byte(citylist_stats_t::display_mode);
	}
	if (file->is_loading()) {
		sorteddir.pressed = citylist_stats_t::sortreverse;
		filter_within_network.pressed = citylist_stats_t::filter_own_network;
		sortedby.set_selection(citylist_stats_t::sort_mode);
		region_selector.set_selection(citylist_stats_t::region_filter);
		name_filter_input.set_text( name_filter, lengthof(name_filter) );
		cb_display_mode.set_selection(citylist_stats_t::display_mode);
		citylist_stats_t::recalc_wold_max();
		fill_list();
		set_windowsize(size);
	}
}
