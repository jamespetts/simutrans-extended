/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "prices_frame.h"

#include "../simcolor.h"
#include "../simworld.h"
#include "../display/simgraph.h"
#include "../descriptor/vehicle_desc.h"
#include "../dataobj/settings.h"
#include "../dataobj/translator.h"


// Distinct fixed colours cycled over the series of each chart.
const uint8 prices_frame_t::series_colors[10] =
{
	COL_DARK_RED,
	COL_DODGER_BLUE,
	COL_DARK_GREEN,
	COL_ORANGE,
	COL_PURPLE,
	COL_DARK_TURQUOISE,
	COL_BROWN,
	COL_MAGENTA,
	COL_DARK_YELLOW,
	COL_DARK_SLATEBLUE
};


static const char* const section_title[4] =
{
	"Price indices",
	"Interest and tax rates",
	"Fuel prices",
	"Staff wages"
};


static const char* rate_row_name[3] =
{
	"Base rate",
	"Overdraft rate",
	"Corporation tax"
};


static const uint8 rate_row_color[3] =
{
	COL_INTEREST,
	COL_RED,
	COL_DARK_ORCHID
};


prices_frame_t::prices_frame_t() :
	gui_frame_t(translator::translate("Prices and rates")),
	last_month(welt->get_last_month()),
	chrome_size(0, 0),
	fitted(false)
{
	set_table_layout(1, 0);

	if (welt->get_timeline_year_month() == 0)
	{
		// Timeline disabled: all prices stay at their base values and years do not
		// advance, so there is no history to display.
		new_component<gui_label_t>("Prices and rates are only available when the timeline is enabled.");
		set_resizemode(diagonal_resize);
		reset_min_windowsize();
		set_windowsize(get_min_windowsize());
		return;
	}

	sections[sk_index].kind = sk_index;
	sections[sk_rate].kind = sk_rate;
	sections[sk_fuel].kind = sk_fuel;
	sections[sk_staff].kind = sk_staff;
	for (uint8 k = 0; k < MAX_KINDS; k++) {
		sections[k].title = section_title[k];
		sections[k].has_note = true;
	}
	sections[sk_index].curve_type = gui_chart_t::PERCENT;
	sections[sk_index].precision = 0;
	sections[sk_rate].curve_type = gui_chart_t::PERCENT;
	sections[sk_rate].precision = 0;
	sections[sk_fuel].curve_type = gui_chart_t::MONEY;
	sections[sk_fuel].precision = 2;
	sections[sk_staff].curve_type = gui_chart_t::MONEY;
	sections[sk_staff].precision = 2;

	build_series();

	for (uint8 k = 0; k < MAX_KINDS; k++) {
		section_t& s = sections[k];
		if (!s.present) {
			continue;
		}
		build_table(s);
		build_chart(s);
		sub_tabs_table.add_tab(&s.scrolly, translator::translate(s.title));
		sub_tabs_charts.add_tab(&s.scrolly_chart, translator::translate(s.title));
	}

	tabs.add_tab(&sub_tabs_table, translator::translate("Tables"));
	tabs.add_tab(&sub_tabs_charts, translator::translate("Graphs"));
	tabs.add_listener(this);
	sub_tabs_table.add_listener(this);
	sub_tabs_charts.add_listener(this);
	add_component(&tabs);

	update_values();

	set_resizemode(diagonal_resize);
	reset_min_windowsize();
	set_windowsize(get_min_windowsize());
	// The window is not fitted here: create_win() subsequently floors every new
	// window to D_DEFAULT_WIDTH x D_DEFAULT_HEIGHT, so fitting must wait until the
	// first draw (see draw()).
}


// Display names for the price-index series. The canonical price_type strings double
// as config/prices.tab keys and must stay byte-stable, so the dialog maps them to
// presentable (translatable) labels instead of renaming them.
static const char* index_series_display_key(uint8 pt)
{
	switch (pt) {
		case price_type::general:             return "General";
		case price_type::passenger_fare:      return "Passenger fares";
		case price_type::mail_rate:           return "Mail rates";
		case price_type::goods_rate:          return "Goods rates";
		case price_type::vehicle_purchase:    return "Vehicle purchase";
		case price_type::vehicle_maintenance: return "Vehicle maintenance";
		case price_type::buildings:           return "Buildings";
		case price_type::infrastructure:      return "Infrastructure";
		case price_type::city_land:           return "City land";
		case price_type::country_land:        return "Country land";
		default:                              return karte_t::get_price_type_name(pt);
	}
}


bool prices_frame_t::fuel_series_in_use(uint8 engine_type) const
{
	const sint32 current_year = welt->get_timeline_year_month() / 12;
	const sint32 current_monthyear = welt->get_timeline_year_month();
	for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
		const sint32 monthyear = (j == 0) ? current_monthyear : (current_year - (sint32)j) * 12 + 11;
		if (welt->get_fuel_cost(monthyear, engine_type) != 0) {
			return true;
		}
	}
	return false;
}


void prices_frame_t::build_series()
{
	// Count the rows of every section first and size the series vector once: entries
	// hold pointers into their own name_buf, so the vector must not reallocate afterwards.
	// The count must match the series actually added below exactly; any gap leaves
	// uninitialised entries that update_values() would then dereference.
	uint8 counts[MAX_KINDS] = { 0, 0, 0, 0 };

	for (uint8 pt = 0; pt < price_type::corporation_tax; pt++) {
		if (karte_t::is_price_type_defined(pt)) {
			counts[sk_index]++;
		}
	}
	if (karte_t::is_price_type_defined(price_type::base_rate)) {
		counts[sk_rate] += 2;
	}
	if (karte_t::is_price_type_defined(price_type::corporation_tax)) {
		counts[sk_rate]++;
	}
	for (uint8 et = 0; et < vehicle_desc_t::MAX_TRACTION_TYPE; et++) {
		// Battery traction has no fuel.tab cost set of its own: its energy cost is derived
		// per vehicle from the electric price and the vehicle's battery_round_trip_efficiency
		// (vehicle_desc_t::get_fuel_cost_per_unit), so it has no price row of its own.
		// Traction types with no cost anywhere in the displayed span carry no information
		// and are omitted from both the tables and the graphs (same test as the add pass).
		if (et != vehicle_desc_t::battery && karte_t::is_fuel_cost_defined(et) && fuel_series_in_use(et)) {
			counts[sk_fuel]++;
		}
	}
	for (uint16 st = 0; st < 255; st++) {
		if (karte_t::is_staff_type_defined((uint8)st)) {
			counts[sk_staff]++;
		}
	}

	uint32 total = 0;
	for (uint8 k = 0; k < MAX_KINDS; k++) {
		section_t& s = sections[k];
		s.present = counts[k] > 0;
		s.first_series = (uint8)total;
		s.series_count = counts[k];
		total += counts[k];
	}
	series.set_count(total);

	index_chart_table.set_count(YEARS_DISPLAYED * (uint32)counts[sk_index]);
	rate_chart_table.set_count(YEARS_DISPLAYED * (uint32)counts[sk_rate]);
	fuel_chart_table.set_count(YEARS_DISPLAYED * (uint32)counts[sk_fuel]);
	staff_chart_table.set_count(YEARS_DISPLAYED * (uint32)counts[sk_staff]);
	for (uint8 k = 0; k < MAX_KINDS; k++) {
		vector_tpl<sint64>& table = k == sk_index ? index_chart_table : k == sk_rate ? rate_chart_table : k == sk_fuel ? fuel_chart_table : staff_chart_table;
		for (uint32 i = 0; i < table.get_count(); i++) {
			table[i] = 0;
		}
	}

	uint32 k = 0;

	// Price indices: all price types except corporation tax and base rate, which
	// are absolute rates rather than inflation multipliers and appear in the rates section.
	{
		uint8 col = 0;
		for (uint8 pt = 0; pt < price_type::corporation_tax; pt++) {
			if (!karte_t::is_price_type_defined(pt)) {
				continue;
			}
			series_t& s = series[k++];
			s.kind = sk_index;
			s.id = pt;
			s.name = translator::translate(index_series_display_key(pt));
			s.name_buf[0] = 0;
			s.color_idx = series_colors[col++ % 10];
		}
	}

	if (karte_t::is_price_type_defined(price_type::base_rate)) {
		for (uint8 r = rate_base; r <= rate_overdraft; r++) {
			series_t& s = series[k++];
			s.kind = sk_rate;
			s.id = r;
			s.name = rate_row_name[r];
			s.name_buf[0] = 0;
			s.color_idx = rate_row_color[r];
		}
	}
	if (karte_t::is_price_type_defined(price_type::corporation_tax)) {
		series_t& s = series[k++];
		s.kind = sk_rate;
		s.id = rate_tax;
		s.name = rate_row_name[rate_tax];
		s.name_buf[0] = 0;
		s.color_idx = rate_row_color[rate_tax];
	}

	{
		uint8 col = 0;
		for (uint8 et = 0; et < vehicle_desc_t::MAX_TRACTION_TYPE; et++) {
			if (et == vehicle_desc_t::battery || !karte_t::is_fuel_cost_defined(et) || !fuel_series_in_use(et)) {
				continue;
			}
			series_t& s = series[k++];
			s.kind = sk_fuel;
			s.id = et;
			s.name = vehicle_desc_t::get_engine_type_string(et);
			s.name_buf[0] = 0;
			s.color_idx = series_colors[col++ % 10];
		}
	}

	{
		uint8 col = 0;
		for (uint16 st = 0; st < 255; st++) {
			if (!karte_t::is_staff_type_defined((uint8)st)) {
				continue;
			}
			// Staff types are pakset-defined numbers. The canonical translation scheme
			// composes the full key ("Staff type 0") and translates that, so language
			// files can name each type.
			series_t& s = series[k++];
			s.kind = sk_staff;
			s.id = (uint8)st;
			snprintf(s.name_buf, sizeof(s.name_buf), "Staff type %i", (sint32)st);
			s.name = translator::translate(s.name_buf);
			s.color_idx = series_colors[col++ % 10];
		}
	}

	// Point each series into its section's chart table column. The tables are never
	// resized after this point, so the pointers (also held by the chart curves) stay valid.
	for (uint32 i = 0; i < series.get_count(); i++) {
		series_t& s = series[i];
		section_t& sec = sections[s.kind];
		const uint8 col = (uint8)(i - sec.first_series);
		vector_tpl<sint64>& table = s.kind == sk_index ? index_chart_table : s.kind == sk_rate ? rate_chart_table : s.kind == sk_fuel ? fuel_chart_table : staff_chart_table;
		s.chart_stride = sec.series_count;
		s.chart_values = sec.series_count > 0 ? &table[col] : NULL;
		s.chart = NULL;
		s.curve_id = 0;
		s.toggle = NULL;
		s.header = NULL;
		for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
			s.cells[j] = NULL;
		}
	}
}


void prices_frame_t::build_table(section_t& s)
{
	// Single-column outer container: the note sizes the window but never distorts
	// the data columns (a spanning note would widen every column equally).
	s.cont.set_table_layout(1, 0);
	s.cont.set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE), scr_size(D_MARGIN_RIGHT + D_SCROLLBAR_WIDTH, D_MARGIN_BOTTOM + D_SCROLLBAR_HEIGHT));
	// More series columns than fit the window width are reached by horizontal scrolling.
	s.scrolly.set_show_scroll_x(true);

	if (s.has_note) {
		s.cont.add_component(&s.note_table);
		// Breathing room between the note and the data grid (finance-window idiom).
		s.cont.new_component<gui_margin_t>(0, LINESPACE / 2);
	}

	const uint8 cols = 1 + s.series_count;
	gui_aligned_container_t* const grid = s.cont.add_table(cols, 0);
	grid->set_spacing(scr_size(0, 0));

	// Column headers: the year corner, then one column per series.
	grid->new_component<gui_table_header_t>("Year", SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	for (uint8 c = 0; c < s.series_count; c++) {
		series_t& ser = series[s.first_series + c];
		ser.header = grid->new_component<gui_table_header_t>(ser.name, SYSCOL_TH_BACKGROUND_TOP, gui_label_t::centered, true, true);
	}

	// One row per year, newest first; the year rows scroll vertically. Data rows
	// alternate two theme data shades so rows stay readable across wide grids.
	for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
		const PIXVAL band = (j & 1) ? SYSCOL_TD_BACKGROUND_SUM : SYSCOL_TD_BACKGROUND;
		s.year_cells[j] = grid->new_component<gui_table_header_buf_t>("", SYSCOL_TH_BACKGROUND_LEFT, gui_label_t::centered, true, true);
		s.year_cells[j]->set_min_width(proportional_string_width("8888"));
		for (uint8 c = 0; c < s.series_count; c++) {
			series_t& ser = series[s.first_series + c];
			ser.cells[j] = grid->new_component<gui_table_cell_buf_t>("", band, gui_label_t::right, true);
			ser.cells[j]->set_min_width(proportional_string_width(s.kind == sk_index || s.kind == sk_rate ? "88,888%" : "88,888.88$"));
		}
	}
	s.cont.end_table();
}


void prices_frame_t::build_chart(section_t& s)
{
	s.cont_chart.set_table_layout(1, 0);
	s.cont_chart.set_margin(scr_size(D_MARGIN_LEFT, D_V_SPACE), scr_size(D_MARGIN_RIGHT + D_SCROLLBAR_WIDTH, D_MARGIN_BOTTOM + D_SCROLLBAR_HEIGHT));
	s.cont_chart.set_spacing(scr_size(D_H_SPACE, D_V_SPACE));

	// No heading here: the active sub-tab already names the section.
	if (s.has_note) {
		s.cont_chart.add_component(&s.note_chart);
	}

	// Curve toggle buttons, five per row
	const uint8 button_rows = (s.series_count + 4) / 5;
	s.cont_chart.add_table(5, button_rows);
	uint8 placed = 0;
	for (uint8 c = 0; c < s.series_count; c++) {
		series_t& ser = series[s.first_series + c];
		button_t* b = s.cont_chart.new_component<button_t>();
		b->init(button_t::box_state_automatic | button_t::flexible, ser.name);
		b->background_color = color_idx_to_rgb(ser.color_idx);
		b->pressed = true;
		b->add_listener(this);
		ser.toggle = b;
		placed++;
	}
	for (uint8 e = placed; e < button_rows * 5; e++) {
		s.cont_chart.new_component<gui_empty_t>();
	}
	s.cont_chart.end_table();

	// Room for the y-axis maximum label, which the chart draws above its own top edge.
	s.cont_chart.new_component<gui_margin_t>(LINESPACE / 2);

	s.chart.set_background(SYSCOL_CHART_BACKGROUND);
	// Graphs favour a long horizontal view: a wide chart keeps 26 years readable.
	s.chart.set_min_size(scr_size(36 * LINESPACE, 15 * LINESPACE));
	s.chart.set_dimension(YEARS_DISPLAYED, 10000);
	s.chart.set_seed(welt->get_last_year());

	for (uint8 c = 0; c < s.series_count; c++) {
		series_t& ser = series[s.first_series + c];
		ser.chart = &s.chart;
		ser.curve_id = s.chart.add_curve(color_idx_to_rgb(ser.color_idx), ser.chart_values, ser.chart_stride, 0, YEARS_DISPLAYED, s.curve_type, true, false, s.precision);
	}

	s.cont_chart.add_component(&s.chart);
}


void prices_frame_t::update_values()
{
	// Year rows: row 0 is the current (possibly partway through) year at the current
	// month; older rows are complete years, valued at their December.
	const sint32 current_year = welt->get_timeline_year_month() / 12;
	const sint32 current_monthyear = welt->get_timeline_year_month();
	// The inflation baseline is the first month of the game, as chosen at map creation,
	// so that the 100% reference point never shifts while the window is open.
	const sint32 start_monthyear = (sint32)welt->get_settings().get_starting_year() * 12 + (sint32)welt->get_settings().get_starting_month();
	const sint32 start_year = welt->get_settings().get_starting_year();
	const sint16 overdraft_margin = welt->get_settings().get_overdraft_percent_above_base_rate();

	for (uint8 k = 0; k < MAX_KINDS; k++) {
		section_t& s = sections[k];
		if (!s.present) {
			continue;
		}

		s.chart.set_seed(current_year);

		for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
			s.year_cells[j]->buf().printf("%i", current_year - (sint32)j);
			s.year_cells[j]->update();
		}

		switch (s.kind) {
			case sk_index:
				s.note_table.buf().printf("Price indices as %% of %i values.", start_year);
				s.note_chart.buf().printf("Price indices as %% of %i values.", start_year);
				break;
			case sk_rate:
				s.note_table.buf().printf("Annual rates in %%.");
				s.note_chart.buf().printf("Annual rates in %%.");
				break;
			case sk_fuel:
				s.note_table.buf().printf("Fuel cost per unit.");
				s.note_chart.buf().printf("Fuel cost per unit.");
				break;
			case sk_staff:
				s.note_table.buf().printf("Monthly wage per staff member.");
				s.note_chart.buf().printf("Monthly wage per staff member.");
				break;
			default:
				break;
		}
		s.note_table.update();
		s.note_chart.update();
	}

	for (uint32 i = 0; i < series.get_count(); i++) {
		series_t& s = series[i];

		// Index rows are rebased to the first month of the game.
		sint64 start_index = 0;
		if (s.kind == sk_index) {
			start_index = welt->get_inflation_adjusted_price(start_monthyear, 100, (price_type)s.id);
		}

		for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
			const sint32 monthyear = (j == 0) ? current_monthyear : (current_year - (sint32)j) * 12 + 11;
			sint64 value = 0;

			switch (s.kind) {
				case sk_index:
				{
					const sint64 index = welt->get_inflation_adjusted_price(monthyear, 100, (price_type)s.id);
					value = (start_index > 0) ? (index * 100) / start_index : 0;
					s.cells[j]->buf().append((double)value, 0);
					s.cells[j]->buf().append("%");
					break;
				}
				case sk_rate:
					if (s.id == rate_tax) {
						value = welt->get_inflation_adjusted_price(monthyear, 100, price_type::corporation_tax);
					}
					else {
						value = welt->get_inflation_adjusted_price(monthyear, 100, price_type::base_rate);
						if (s.id == rate_overdraft) {
							value += overdraft_margin;
						}
					}
					s.cells[j]->buf().append((double)value, 0);
					s.cells[j]->buf().append("%");
					break;
				case sk_fuel:
					value = welt->get_fuel_cost(monthyear, s.id);
					s.cells[j]->buf().append_money(value / 100.0);
					break;
				case sk_staff:
					value = welt->get_staff_salary(monthyear, s.id);
					s.cells[j]->buf().append_money(value / 100.0);
					break;
				default:
					break;
			}

			s.cells[j]->update();
			if (s.chart_values != NULL) {
				s.chart_values[(uint32)j * s.chart_stride] = value;
			}
		}
	}
}


void prices_frame_t::draw(scr_coord pos, scr_size size)
{
	if (!fitted) {
		// First draw: the window now has its real size and layout.
		fit_to_content();
	}

	if (series.get_count() > 0 && welt->get_last_month() != last_month) {
		last_month = welt->get_last_month();
		update_values();
	}

	gui_frame_t::draw(pos, size);
}


void prices_frame_t::fit_to_content()
{
	// Tables are tall (26 year rows) and vary in width by series count; graphs are
	// shorter and deliberately wide. Snap the window to the newly active sub-tab so
	// narrow sections open narrow, wide ones (e.g. staff wages) widen, and graphs
	// keep their long horizontal shape. Manual resizing stays user-controlled until
	// the next switch.
	const bool graphs = tabs.get_active_tab_index() == 1;
	gui_component_t* const active = graphs ? sub_tabs_charts.get_aktives_tab() : sub_tabs_table.get_aktives_tab();
	const section_t* sec = NULL;
	for (uint8 k = 0; k < MAX_KINDS; k++) {
		if (!sections[k].present) {
			continue;
		}
		if ((graphs ? (gui_component_t*)&sections[k].scrolly_chart : (gui_component_t*)&sections[k].scrolly) == active) {
			sec = &sections[k];
			break;
		}
	}
	if (sec == NULL) {
		return;
	}
	if (!fitted) {
		// First fit runs from draw(), after create_win() has sized and laid out the
		// window. Measure the decoration from the tables viewport: it is the
		// initially laid-out one, whereas the graphs viewport may never have been
		// shown if the user/automation switched tabs before the first draw. The
		// decoration is identical for both views. A degenerate viewport means the
		// layout is not ready yet, so try again next draw.
		const section_t* ref = NULL;
		for (uint8 k = 0; k < MAX_KINDS; k++) {
			if (sections[k].present) {
				ref = &sections[k];
				break;
			}
		}
		if (ref == NULL) {
			return;
		}
		const gui_scrollpane_t& sp = ref->scrolly;
		if (sp.get_size().w <= D_DEFAULT_WIDTH / 4 || sp.get_size().h <= D_DEFAULT_HEIGHT / 4) {
			return;
		}
		const scr_size cur = get_windowsize();
		chrome_size = scr_size(cur.w - sp.get_size().w, cur.h - sp.get_size().h);
		fitted = true;
	}
	const gui_aligned_container_t& content = graphs ? sec->cont_chart : sec->cont;
	const scr_size want = content.get_min_size();
	// Never narrower than the sub-tab row, or the "Price indices … Staff wages"
	// tabs would be cut off and force left/right arrow scrolling.
	const gui_tab_panel_t& sub = graphs ? sub_tabs_charts : sub_tabs_table;
	const scr_coord_val subtab_bar_w = sub.get_required_size().w + D_H_SPACE;
	scr_coord_val w = max(want.w, subtab_bar_w) + chrome_size.w;
	scr_coord_val h = want.h + chrome_size.h;
	w = clamp(w, (scr_coord_val)(8 * D_TITLEBAR_HEIGHT), (scr_coord_val)display_get_width());
	h = clamp(h, (scr_coord_val)(4 * D_TITLEBAR_HEIGHT), (scr_coord_val)display_get_height());
	// Lower the minimum past the new size where needed so it sticks; never raise it.
	scr_size mn = get_min_windowsize();
	mn.w = min(mn.w, w);
	mn.h = min(mn.h, h);
	set_min_windowsize(mn);
	set_windowsize(scr_size(w, h));
}


bool prices_frame_t::action_triggered(gui_action_creator_t* comp, value_t)
{
	if (comp == &tabs || comp == &sub_tabs_table || comp == &sub_tabs_charts) {
		fit_to_content();
		return true;
	}
	for (uint32 i = 0; i < series.get_count(); i++) {
		series_t& s = series[i];
		if (s.toggle != NULL && comp == s.toggle) {
			if (s.toggle->pressed) {
				s.chart->show_curve(s.curve_id);
			}
			else {
				s.chart->hide_curve(s.curve_id);
			}
			return true;
		}
	}
	return false;
}
