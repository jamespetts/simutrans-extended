/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../obj/leitung2.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "factory_chart.h"
#include "components/gui_image.h"

#define CHART_HEIGHT (90)



static const char *const input_type[MAX_FAB_GOODS_STAT] =
{
	"Storage", "Arrived", "Consumed", "In Transit"
};

static const char *const output_type[MAX_FAB_GOODS_STAT] =
{
	"Storage", "Delivered", "Produced", "In Transit"
};

static const gui_chart_t::convert_proc goods_convert[MAX_FAB_GOODS_STAT] =
{
	convert_goods, NULL, convert_goods, NULL
};

static const char *const prod_type[MAX_FAB_STAT+1] =
{
	"Operation rate", "Power usage",
	"Electricity", "Jobs", "Post",
	"", "", "Commuters", "", "Post",
	"Post", "Consumers",
	"Power output" // put this at the end
};

static const char *const ref_type[MAX_FAB_REF_LINE] =
{
	"Electricity", "Jobs", "Post",
	"Electricity", "Jobs", "Post",
};

static const uint8 prod_color[MAX_FAB_STAT] =
{
	COL_BROWN, COL_ELECTRICITY - 1,
	COL_LIGHT_RED, COL_LIGHT_TURQUOISE, COL_ORANGE,
	0, 0, COL_LIGHT_PURPLE, 0, COL_LIGHT_YELLOW,
	COL_YELLOW, COL_GREY3
};

static const gui_chart_t::convert_proc prod_convert[MAX_FAB_STAT] =
{
	NULL, convert_power, convert_boost, convert_boost, convert_boost, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};


static const uint8 chart_type[MAX_FAB_STAT] =
{
	gui_chart_t::PERCENT, gui_chart_t::STANDARD, gui_chart_t::PERCENT, gui_chart_t::PERCENT,
	gui_chart_t::PERCENT, gui_chart_t::STANDARD, gui_chart_t::STANDARD, gui_chart_t::STANDARD,
	gui_chart_t::STANDARD, gui_chart_t::STANDARD, gui_chart_t::STANDARD, gui_chart_t::STANDARD
};

static const gui_chart_t::chart_marker_t marker_type[MAX_FAB_REF_LINE] =
{
	gui_chart_t::cross, gui_chart_t::cross, gui_chart_t::cross, gui_chart_t::diamond, gui_chart_t::diamond, gui_chart_t::diamond
};

static const gui_chart_t::convert_proc ref_convert[MAX_FAB_REF_LINE] =
{
	convert_boost, convert_boost, convert_boost, convert_power, NULL, NULL
};

static const uint8 ref_chart_type[MAX_FAB_REF_LINE] =
{
	gui_chart_t::PERCENT, gui_chart_t::PERCENT, gui_chart_t::PERCENT, gui_chart_t::KW, gui_chart_t::STANDARD, gui_chart_t::STANDARD
};

static const uint8 ref_color[MAX_FAB_REF_LINE] =
{
	COL_RED+2, COL_TURQUOISE, COL_ORANGE_RED,
	COL_RED, COL_DODGER_BLUE, COL_LEMON_YELLOW-2
};

static const char *const label_text[MAX_PROD_LABEL+1] =
{
	"Boost (%)", "Max Boost (%)", "Demand", "Arrived", "sended", "(KW)", "(MW)"
};

// Mappings from cell position to buttons, labels, charts
static const uint8 prod_cell_button[] =
{
	FAB_PRODUCTION, FAB_POWER,            MAX_FAB_STAT,      MAX_FAB_STAT,
	MAX_FAB_STAT,   FAB_BOOST_ELECTRIC,   FAB_BOOST_PAX,     FAB_BOOST_MAIL,
	MAX_FAB_STAT,   MAX_FAB_STAT,         MAX_FAB_STAT,      MAX_FAB_STAT,
	MAX_FAB_STAT,   MAX_FAB_STAT,         MAX_FAB_STAT,      MAX_FAB_STAT,
	MAX_FAB_STAT,   FAB_CONSUMER_ARRIVED, FAB_PAX_ARRIVED,   FAB_MAIL_ARRIVED,
	MAX_FAB_STAT,   MAX_FAB_STAT,         MAX_FAB_STAT,      FAB_MAIL_DEPARTED,
};
// unused in Exteded: FAB_PAX_GENERATED, FAB_MAIL_GENERATED, FAB_PAX_DEPARTED

static const uint8 prod_cell_label[] =
{
	MAX_PROD_LABEL, MAX_PROD_LABEL, 5, MAX_PROD_LABEL,
	0, MAX_PROD_LABEL, MAX_PROD_LABEL, MAX_PROD_LABEL,
	1, MAX_PROD_LABEL, MAX_PROD_LABEL, MAX_PROD_LABEL,
	2, MAX_PROD_LABEL, MAX_PROD_LABEL, MAX_PROD_LABEL,
	3, MAX_PROD_LABEL, MAX_PROD_LABEL, MAX_PROD_LABEL,
	4, MAX_PROD_LABEL, MAX_PROD_LABEL, MAX_PROD_LABEL,
};

static const uint8 prod_cell_ref[] =
{
	MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE,
	MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE,
	MAX_FAB_REF_LINE, FAB_REF_MAX_BOOST_ELECTRIC, FAB_REF_MAX_BOOST_PAX, FAB_REF_MAX_BOOST_MAIL,
	MAX_FAB_REF_LINE, FAB_REF_DEMAND_ELECTRIC, FAB_REF_DEMAND_PAX, FAB_REF_DEMAND_MAIL,
	MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE,
	MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE, MAX_FAB_REF_LINE,
};

factory_chart_t::factory_chart_t(const fabrik_t *_factory)
{
	set_factory(_factory);
}

void factory_chart_t::set_factory(const fabrik_t *_factory)
{
	factory = _factory;
	if (factory == NULL) {
		return;
	}

	remove_all();
	goods_cont.remove_all();
	prod_cont.remove_all();
	button_to_chart.clear();

	set_table_layout(1,0);
	add_component( &tab_panel );

	const uint32 input_count = factory->get_input().get_count();
	const uint32 output_count = factory->get_output().get_count();
	if(  input_count>0  ||  output_count>0  ) {
		// only add tab if there is something to display
		tab_panel.add_tab(&goods_cont, translator::translate("Goods"));
		goods_cont.set_table_layout(1, 0);
		goods_cont.add_component(&goods_chart);

		// GUI components for goods input/output statistics
		goods_chart.set_min_size(scr_size(D_DEFAULT_WIDTH - D_MARGIN_LEFT - D_MARGIN_RIGHT, CHART_HEIGHT));
		goods_chart.set_dimension(12, 10000);
		goods_chart.set_background(SYSCOL_CHART_BACKGROUND);
		goods_chart.set_ltr(env_t::left_to_right_graphs);
		const uint32 input_count = factory->get_input().get_count();
		const uint32 output_count = factory->get_output().get_count();

		uint32 count = 0;

		// first tab: charts for goods production/consumption

		if (input_count > 0) {
			goods_cont.add_table(3,1);
			{
				goods_cont.new_component<gui_image_t>()->set_image(skinverwaltung_t::input_output ? skinverwaltung_t::input_output->get_image_id(0) : IMG_EMPTY, true);
				goods_cont.new_component<gui_label_t>("Verbrauch");
				goods_cont.new_component<gui_fill_t>();
			}
			goods_cont.end_table();

			// create table of buttons, insert curves to chart
			const array_tpl<ware_production_t>& input = factory->get_input();
			goods_cont.add_table(4,0)->set_force_equal_columns(true);
			for (uint32 g = 0; g < input_count; ++g) {
				const bool is_available = world()->get_goods_list().is_contained(input[g].get_typ());
				goods_cont.add_table(2,1);
				{
					goods_cont.new_component<gui_image_t>(is_available ? input[g].get_typ()->get_catg_symbol() : IMG_EMPTY, 0, ALIGN_NONE, true);
					goods_cont.new_component<gui_label_t>(input[g].get_typ()->get_name(), is_available ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
				}
				goods_cont.end_table();
				for (int s = 0; s < MAX_FAB_GOODS_STAT; ++s) {
					PIXVAL chart_col = color_idx_to_rgb(input[g].get_typ()->get_color_index());
					if (is_dark_color(chart_col)) {
						if (s) {
							chart_col = display_blend_colors(chart_col, color_idx_to_rgb(COL_WHITE), s*20);
						}
					}
					else if (s < 3) {
						chart_col = display_blend_colors(chart_col, color_idx_to_rgb(COL_BLACK), 54-(18*s));
					}
					uint16 curve = goods_chart.add_curve(chart_col, input[g].get_stats(), MAX_FAB_GOODS_STAT, s, MAX_MONTH, false, false, true, 0, goods_convert[s]);

					button_t* b = goods_cont.new_component<button_t>();
					b->init(button_t::box_state_automatic | button_t::flexible, input_type[s]);
					b->background_color = chart_col;
					b->pressed = false;
					b->enable(is_available);
					button_to_chart.append(b, &goods_chart, curve);

					if ((s % 2) == 1) {
						// skip last cell in current row, first cell in next row
						goods_cont.new_component<gui_empty_t>();
						if (s + 1 < MAX_FAB_GOODS_STAT) {
							goods_cont.new_component<gui_empty_t>();
						}
					}
				}
				count++;
			}
			goods_cont.end_table();
		}
		if (output_count > 0) {
			goods_cont.add_table(3, 1);
			{
				goods_cont.new_component<gui_image_t>()->set_image(skinverwaltung_t::input_output ? skinverwaltung_t::input_output->get_image_id(1) : IMG_EMPTY, true);
				goods_cont.new_component<gui_label_t>("Produktion");
				goods_cont.new_component<gui_fill_t>();
			}
			goods_cont.end_table();

			const array_tpl<ware_production_t>& output = factory->get_output();
			goods_cont.add_table(4,0)->set_force_equal_columns(true);
			for (uint32 g = 0; g < output_count; ++g) {
				const bool is_available = world()->get_goods_list().is_contained(output[g].get_typ());
				goods_cont.add_table(2,1);
				{
					goods_cont.new_component<gui_image_t>(is_available ? output[g].get_typ()->get_catg_symbol() : IMG_EMPTY, 0, ALIGN_NONE, true);
					goods_cont.new_component<gui_label_t>(output[g].get_typ()->get_name(), is_available ? SYSCOL_TEXT : SYSCOL_TEXT_WEAK);
				}
				goods_cont.end_table();
				for (int s = 0; s < 3; ++s) {
					PIXVAL chart_col = color_idx_to_rgb(output[g].get_typ()->get_color_index());
					if (is_dark_color(chart_col)) {
						if (s) {
							chart_col = display_blend_colors(chart_col, color_idx_to_rgb(COL_WHITE), s * 24);
						}
					}
					else if (s < 2) {
						chart_col = display_blend_colors(chart_col, color_idx_to_rgb(COL_BLACK), 40-(20*s));
					}
					uint16 curve = goods_chart.add_curve(chart_col, output[g].get_stats(), MAX_FAB_GOODS_STAT, s, MAX_MONTH, false, false, true, 0, goods_convert[s]);

					button_t* b = goods_cont.new_component<button_t>();
					b->init(button_t::box_state_automatic | button_t::flexible, output_type[s]);
					b->background_color = chart_col;
					b->pressed = false;
					b->enable(is_available);
					button_to_chart.append(b, &goods_chart, curve);
				}
				count++;
			}
			goods_cont.end_table();
		}
		goods_cont.new_component<gui_empty_t>();
	}

	tab_panel.add_tab( &prod_cont, translator::translate("Production/Boost") );
	prod_cont.set_table_layout(1,0);
	prod_cont.add_component( &prod_chart );

	prod_cont.add_table(4, 0)->set_force_equal_columns(true);
	// GUI components for other production-related statistics
	prod_chart.set_min_size( scr_size( D_DEFAULT_WIDTH-D_MARGIN_LEFT-D_MARGIN_RIGHT, CHART_HEIGHT ) );
	prod_chart.set_dimension(12, 10000);
	prod_chart.set_background(SYSCOL_CHART_BACKGROUND);
	prod_chart.set_ltr(env_t::left_to_right_graphs);

	for(  int row = 0, cell = 0; row<6; row++) {
		for(  int col = 0; col<4; col++, cell++) {
			// labels
			if (prod_cell_label[cell] < MAX_PROD_LABEL) {
				if (row==0 && factory->get_desc()->is_electricity_producer()) {
					prod_cont.new_component<gui_label_t>(label_text[MAX_PROD_LABEL]); // switch KW to MW
				}
				else{
					prod_cont.new_component<gui_label_t>(label_text[ prod_cell_label[cell] ]);
				}
			}
			// chart, buttons for production
			else if (prod_cell_button[cell] < MAX_FAB_STAT) {
				uint8 s = prod_cell_button[cell];
				// add curve
				uint16 curve = prod_chart.add_curve( color_idx_to_rgb(prod_color[s]), factory->get_stats(), MAX_FAB_STAT, s, MAX_MONTH, chart_type[s], false, true, (s==0) ? 2 : 0, prod_convert[s] );
				// only show buttons, if the is something to do ...
				if(
					(s==FAB_BOOST_ELECTRIC  &&  (factory->get_desc()->is_electricity_producer()  ||  factory->get_desc()->get_electric_boost()==0))  ||
					(s==FAB_BOOST_PAX  &&  factory->get_desc()->get_pax_boost()==0)  ||
					(s==FAB_BOOST_MAIL  &&  factory->get_desc()->get_mail_boost()==0) ||
					(s == FAB_CONSUMER_ARRIVED && factory->get_sector() != fabrik_t::end_consumer) ||
					s == FAB_PAX_GENERATED || s == FAB_PAX_DEPARTED || s == FAB_MAIL_GENERATED
				) {
					prod_cont.new_component<gui_empty_t>();
					continue;
				}
				// add button
				button_t *b = prod_cont.new_component<button_t>();
				if (s == 1 && factory->get_desc()->is_electricity_producer()) {
					// if power plant, switch label to output
					b->init(button_t::box_state_automatic | button_t::flexible, prod_type[MAX_FAB_STAT]);
				}
				else {
					b->init(button_t::box_state_automatic | button_t::flexible, prod_type[s]);
				}
				b->background_color = color_idx_to_rgb(prod_color[s]);
				b->pressed = false;
				button_to_chart.append(b, &prod_chart, curve);
			}
			// chart, buttons for reference lines
			else if (prod_cell_ref[cell] < MAX_FAB_REF_LINE) {
				uint8 r = prod_cell_ref[cell];
				// add curve
				uint16 curve = prod_chart.add_curve( color_idx_to_rgb(ref_color[r]), prod_ref_line_data + r, 0, 0, MAX_MONTH, ref_chart_type[r], false, true, 0, ref_convert[r], marker_type[r] );
				if(
					(r==FAB_REF_MAX_BOOST_ELECTRIC  &&  (factory->get_desc()->is_electricity_producer()  ||  factory->get_desc()->get_electric_boost()==0))  ||
					(r==FAB_REF_MAX_BOOST_PAX  &&  factory->get_desc()->get_pax_boost()==0)  ||
					(r==FAB_REF_MAX_BOOST_MAIL  &&  factory->get_desc()->get_mail_boost()==0)  ||
					(r==FAB_REF_DEMAND_ELECTRIC  &&  (factory->get_desc()->is_electricity_producer()  ||  factory->get_desc()->get_electric_amount()==0))  ||
					(r==FAB_REF_DEMAND_PAX  &&  factory->get_desc()->get_pax_demand()==0)  ||
					(r==FAB_REF_DEMAND_MAIL  &&  factory->get_desc()->get_mail_demand()==0)
				) {
					prod_cont.new_component<gui_empty_t>();
					continue;
				}
				// add button
				button_t *b = prod_cont.new_component<button_t>();
				b->init(button_t::box_state_automatic | button_t::flexible, ref_type[r]);
				b->background_color = color_idx_to_rgb(ref_color[r]);
				b->pressed = false;
				button_to_chart.append(b, &prod_chart, curve);
			}
			else {
				prod_cont.new_component<gui_empty_t>();
			}
		}
	}
	prod_cont.end_table();
	prod_cont.new_component<gui_empty_t>();

	set_size(get_min_size());
	// initialize reference lines' data (these do not change over time)
	prod_ref_line_data[FAB_REF_MAX_BOOST_ELECTRIC] = factory->get_desc()->get_electric_boost();
	prod_ref_line_data[FAB_REF_MAX_BOOST_PAX] = factory->get_desc()->get_pax_boost();
	prod_ref_line_data[FAB_REF_MAX_BOOST_MAIL] = factory->get_desc()->get_mail_boost();
}


factory_chart_t::~factory_chart_t()
{
	button_to_chart.clear();
}


void factory_chart_t::update()
{
	// update reference lines' data (these might change over time)
	prod_ref_line_data[FAB_REF_DEMAND_ELECTRIC] = ( factory->get_desc()->is_electricity_producer() ? 0 : factory->get_scaled_electric_demand()*1000 );
	prod_ref_line_data[FAB_REF_DEMAND_PAX] = factory->get_scaled_pax_demand();
	prod_ref_line_data[FAB_REF_DEMAND_MAIL] = factory->get_scaled_mail_demand();
}


void factory_chart_t::rdwr( loadsave_t *file )
{
	// button-to-chart array
	button_to_chart.rdwr(file);
}
