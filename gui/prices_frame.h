/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_PRICES_FRAME_H
#define GUI_PRICES_FRAME_H


#include "gui_frame.h"
#include "components/gui_button.h"
#include "components/gui_chart.h"
#include "components/gui_label.h"
#include "components/gui_scrollpane.h"
#include "components/gui_tab_panel.h"
#include "components/gui_table.h"
#include "components/action_listener.h"
#include "simwin.h"
#include "../player/finance.h"

/**
 * History of the prices.tab price factors (inflation indices), interest rates,
 * corporation tax rates, fuel prices and staff wages.
 * Passive display window: reads karte_t price/fuel/staff data; no simulation interaction.
 * Layout: the top tabs choose tables or graphs; sub-tabs choose the item group. Each table
 * holds the years as rows (26 rows, scrolled vertically) and the series of the selected
 * group as columns, drawn with the gui_table_* cells (theme backgrounds, grid borders,
 * alternating row bands); each graph shows one chart of the selected group with a toggle
 * button per curve. Fuel types with no cost anywhere in the displayed span are omitted.
 */
class prices_frame_t : public gui_frame_t, private action_listener_t
{
public:
	// Years of history displayed; the same span as the player finance yearly graphs.
	static const uint8 YEARS_DISPLAYED = MAX_PLAYER_HISTORY_YEARS;

private:
	enum series_kind_t { sk_index, sk_rate, sk_fuel, sk_staff, MAX_KINDS };
	enum rate_id_t { rate_base = 0, rate_overdraft, rate_tax };

	struct series_t {
		series_kind_t kind;
		uint8 id;                 // price_type / rate_id_t / traction type / staff type
		const char* name;         // translation key (static literal or name_buf)
		char name_buf[64];        // storage for composed names (staff types)
		char desc_key[48];        // storage for the staff-type description translation key
		uint8 color_idx;
		gui_chart_t* chart;
		uint32 curve_id;
		uint8 chart_stride;       // number of series in the section (row width of its chart table)
		sint64* chart_values;     // &chart_table[column of this series]; YEARS_DISPLAYED entries, stride chart_stride
		button_t* toggle;
		gui_table_cell_buf_t* cells[YEARS_DISPLAYED];
	};

	// One item group: price indices, interest and tax rates, fuel prices, staff wages.
	struct section_t {
		const char* title;        // translation key
		bool present;
		uint8 first_series;
		uint8 series_count;
		int curve_type;
		int precision;
		gui_aligned_container_t cont;         // table tab content
		gui_scrollpane_t scrolly;
		gui_aligned_container_t cont_chart;   // graphs tab content
		gui_scrollpane_t scrolly_chart;
		gui_chart_t chart;
		// Separate note labels per tab: one component instance cannot live in two
		// containers (each parent would overwrite its position on resize).
		gui_label_buf_t note_table;
		gui_label_buf_t note_chart;
		gui_table_header_buf_t* year_cells[YEARS_DISPLAYED];

		section_t() :
			present(false),
			first_series(0),
			series_count(0),
			curve_type(gui_chart_t::STANDARD),
			precision(0),
			scrolly(&cont, false, true),
			scrolly_chart(&cont_chart, false, true)
		{
			for (uint8 j = 0; j < YEARS_DISPLAYED; j++) {
				year_cells[j] = NULL;
			}
		}
	};

	static const uint8 series_colors[10];

	vector_tpl<series_t> series;
	section_t sections[MAX_KINDS];

	// Chart value tables, layout [year][series within section], sized once in
	// build_series() and never resized afterwards (the chart curves hold pointers into them).
	vector_tpl<sint64> index_chart_table;
	vector_tpl<sint64> rate_chart_table;
	vector_tpl<sint64> fuel_chart_table;
	vector_tpl<sint64> staff_chart_table;

	gui_tab_panel_t tabs;
	gui_tab_panel_t sub_tabs_table;
	gui_tab_panel_t sub_tabs_charts;

	// "Recent" (last YEARS_DISPLAYED years) versus "all time" (since the game
	// start, decimated): a mutually exclusive pair; bt_all_time.pressed selects
	// the whole-history span.
	button_t bt_recent;
	button_t bt_all_time;

	uint32 last_month;

	// Frame decorations (border + tab headers + scrollbars) measured once on the
	// first draw, when create_win has finally sized and laid out the window. The
	// content minimum varies per sub-tab; the decoration around it does not.
	scr_size chrome_size;
	bool fitted;

	void build_series();
	void build_table(section_t& s);
	void build_chart(section_t& s);
	void update_values();

	// True when the fuel type has a non-zero cost anywhere in the displayed span.
	bool fuel_series_in_use(uint8 engine_type) const;

	// Number of years between two displayed rows: 1 for the recent view, or a
	// larger step that decimates the whole-game span down to ~YEARS_DISPLAYED
	// points for the all-time view.
	sint32 year_step() const;

	// Snap the window to the active view's content (tall narrow tables of varying
	// width, short wide graphs); called on tab switches and once on the first draw.
	void fit_to_content();

public:
	prices_frame_t();

	/**
	 * Set the window associated helptext
	 * @return the filename for the helptext, or NULL
	 */
	const char * get_help_filename() const OVERRIDE { return "prices.txt"; }

	void draw(scr_coord pos, scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	// since no information is needed to restore this, returning magic is enough
	uint32 get_rdwr_id() OVERRIDE { return magic_prices_frame; }
};

#endif
