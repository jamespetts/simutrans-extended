/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "citylist_stats_t.h"
#include "city_info.h"

#include "../simcity.h"
#include "../simevent.h"
#include "../simworld.h"
#include "../simfab.h"

#include "../display/viewport.h"
#include "../utils/cbuffer_t.h"
#include "components/gui_label.h"
#include "components/gui_colorbox.h"

bool citylist_stats_t::sortreverse = false;
bool citylist_stats_t::filter_own_network = false;
uint8 citylist_stats_t::sort_mode = citylist_stats_t::SORT_BY_NAME;
uint8 citylist_stats_t::region_filter = 0;
uint8 citylist_stats_t::display_mode = 0;

uint16 citylist_stats_t::name_width = CITY_NAME_LABEL_WIDTH;
uint32 citylist_stats_t::world_max_value = 200;

#define L_MAX_GRAPH_WIDTH 200
#define L_VALUE_CELL_WIDTH proportional_string_width("88,888,888")

gui_city_stats_t::gui_city_stats_t(stadt_t *c)
{
	city = c;
	set_table_layout(1,0);
	set_spacing(scr_size(1,1));
	set_margin( scr_size(1,1), scr_size(0,0) );

	update_table();
}

void gui_city_stats_t::update_table()
{
	remove_all();
	mode = citylist_stats_t::display_mode;
	switch (mode) {
		case citylist_stats_t::cl_general:
			add_table(6,1);
			{
				gui_image_t *img = new_component<gui_image_t>(skinverwaltung_t::passengers->get_image_id(0), 0, ALIGN_CENTER_V, true);
				if (!city->get_citygrowth()) {
					img->set_transparent(color_idx_to_rgb(COL_RED) | TRANSPARENT75_FLAG | OUTLINE_FLAG);
					img->set_tooltip(translator::translate("City growth is restrained"));
				}

				gui_label_buf_t *lb = new_component<gui_label_buf_t>(city->get_citygrowth() ? SYSCOL_TEXT : SYSCOL_OBSOLETE, gui_label_t::right);
				const uint32 population = city->get_finance_history_month(0, HIST_CITIZENS);
				lb->buf().append(population, 0);
				lb->set_fixed_width(L_VALUE_CELL_WIDTH);
				lb->set_tooltip(translator::translate("citicens"));
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%.2f %s", city->get_land_area(), translator::translate("sq. km."));
				lb->set_fixed_width(proportional_string_width("8888.88 ")+ proportional_string_width(translator::translate("sq. km.")));
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%5d/%s", city->get_population_density(), translator::translate("sq. km."));
				lb->set_tooltip(translator::translate("Population density"));
				lb->set_fixed_width(proportional_string_width(" 88,888/") + proportional_string_width(translator::translate("sq. km.")));
				lb->update();

				if (!world()->get_settings().regions.empty()) {
					lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::left);
					lb->buf().append( translator::translate(world()->get_region_name(city->get_pos()).c_str()) );
					lb->update();
				}
				else{
					new_component<gui_empty_t>();
				}

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered);
				lb->buf().printf("%s", city->get_pos().get_fullstr());
				lb->set_fixed_width(proportional_string_width("(8888,8888)"));
				lb->update();
			}
			end_table();
			break;
		case citylist_stats_t::cl_population:
			add_table(3,1);
			{
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(city->get_citygrowth() ? SYSCOL_TEXT:SYSCOL_OBSOLETE, gui_label_t::right);
				const uint32 population = city->get_finance_history_month(0, HIST_CITIZENS);
				lb->buf().append(population,0);
				lb->set_fixed_width( L_VALUE_CELL_WIDTH );
				if (!city->get_citygrowth()) {
					lb->set_tooltip(translator::translate("City growth is restrained"));
				}
				lb->update();

				new_component<gui_colorbox_t>()->init(color_idx_to_rgb(COL_DARK_GREEN+1), scr_size((population*L_MAX_GRAPH_WIDTH + citylist_stats_t::world_max_value-1)/citylist_stats_t::world_max_value, D_LABEL_HEIGHT*2/3), true, false);
				new_component<gui_label_updown_t>()->init(city->get_finance_history_month(0, HIST_GROWTH), scr_coord(0,0), SYSCOL_TEXT, gui_label_t::left, 0, false);
			}
			end_table();
			break;
		case citylist_stats_t::cl_jobs:
		case citylist_stats_t::cl_visitor_demand:
			add_table(2,1);
			{
				const uint8 hist_index = mode==citylist_stats_t::cl_jobs ? HIST_JOBS : HIST_VISITOR_DEMAND;
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				const uint32 value = city->get_finance_history_month(0, hist_index);
				lb->buf().append(value, 0);
				lb->set_fixed_width( L_VALUE_CELL_WIDTH );
				lb->update();

				new_component<gui_colorbox_t>()->init(mode==citylist_stats_t::cl_jobs ? color_idx_to_rgb(COL_COMMUTER): goods_manager_t::passengers->get_color(),
					scr_size( (value*L_MAX_GRAPH_WIDTH + citylist_stats_t::world_max_value-1) / citylist_stats_t::world_max_value, D_LABEL_HEIGHT*2/3), true, false);
			}
			end_table();
			break;
		case citylist_stats_t::pax_traffic:
			add_table(3,1);
			{
				const sint64 sum = city->get_cityhistory_last_quarter(HIST_PAS_GENERATED);

				num[0] = city->get_cityhistory_last_quarter(HIST_PAS_TRANSPORTED);
				num[1] = city->get_cityhistory_last_quarter(HIST_CITYCARS);
				num[2] = city->get_cityhistory_last_quarter(HIST_PAS_WALKED);
				num[3] = max(0, sum - num[0] - num[1] - num[2]); // Gave up the move

				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().append(num[0], 0);
				lb->set_fixed_width(L_VALUE_CELL_WIDTH);
				lb->set_tooltip(translator::translate("Passengers using public transportation in the last quarter"));
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%.1f%%", sum ? num[0]*100.0 / sum : 0.0);
				lb->set_tooltip(translator::translate("Percentage of users of public transportation"));
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->update();

				bandgraph.clear();
				// link with hist_type_color
				bandgraph.add_color_value(&num[0], color_idx_to_rgb(COL_DODGER_BLUE), true);
				bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_TRAFFIC), true);
				bandgraph.add_color_value(&num[2], color_idx_to_rgb(COL_WALKED), true); // walked
				bandgraph.add_color_value(&num[3], color_idx_to_rgb(COL_RED));
				add_component(&bandgraph);
				bandgraph.set_size(scr_size((sum*L_MAX_GRAPH_WIDTH/3 + citylist_stats_t::world_max_value-1) / citylist_stats_t::world_max_value, D_LABEL_HEIGHT*2/3));

			}
			end_table();
			break;
		case citylist_stats_t::mail_traffic:
			add_table(3,1);
			{
				const sint64 sum = city->get_cityhistory_last_quarter(HIST_MAIL_GENERATED);
				num[0] = city->get_cityhistory_last_quarter(HIST_MAIL_TRANSPORTED);
				num[1] = max(0, sum - num[0]); // mail no route

				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().append(num[0], 0);
				lb->set_fixed_width( L_VALUE_CELL_WIDTH );
				lb->set_tooltip(translator::translate("Number of mail sent in the last quarter"));
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%.1f%%", sum ? num[0]*100.0/ sum : 0.0);
				lb->set_tooltip(translator::translate("Mail delivery success in the last quarter"));
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->update();

				bandgraph.clear();
				bandgraph.add_color_value(&num[0], color_idx_to_rgb(COL_MAIL_DELIVERED), true);
				bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_MAIL_NOROUTE));
				add_component(&bandgraph);
				bandgraph.set_size(scr_size((sum*L_MAX_GRAPH_WIDTH/3 + citylist_stats_t::world_max_value-1) / citylist_stats_t::world_max_value, D_LABEL_HEIGHT*2/3));
			}
			end_table();
			break;
		case citylist_stats_t::goods_traffic:
			add_table(3, 1);
			{
				const sint64 sum = city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED);
				num[0] = city->get_cityhistory_last_quarter(HIST_GOODS_RECEIVED);
				num[1] = max(0, sum - num[0]);
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);

				lb->buf().append(num[0],0);
				lb->set_fixed_width(L_VALUE_CELL_WIDTH);
				lb->set_tooltip(translator::translate("Goods supplied"));
				lb->update();

				lb = new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%.1f%%", sum ? num[0]*100.0 / sum : 0.0);
				lb->set_tooltip(translator::translate("ratio_goods"));
				lb->set_fixed_width(proportional_string_width("188.8%"));
				lb->update();

				bandgraph.clear();
				bandgraph.add_color_value(&num[0], color_idx_to_rgb(COL_LIGHT_BROWN), true);
				bandgraph.add_color_value(&num[1], color_idx_to_rgb(COL_BROWN-1));
				add_component(&bandgraph);
				bandgraph.set_size(scr_size((sum*L_MAX_GRAPH_WIDTH/3 + citylist_stats_t::world_max_value-1) / citylist_stats_t::world_max_value, D_LABEL_HEIGHT*2/3));
			}
			end_table();
			break;
#ifdef DEBUG
		case citylist_stats_t::dbg_demands:
			add_table(2,1);
			{
				gui_label_buf_t *lb = new_component<gui_label_buf_t>(city->get_homeless()<0? MONEY_MINUS : SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%4i", city->get_homeless());
				lb->update();

				lb = new_component<gui_label_buf_t>(city->get_unemployed()<0 ? MONEY_MINUS : SYSCOL_TEXT, gui_label_t::right);
				lb->buf().printf("%4i", city->get_unemployed());
				lb->update();
			}
			end_table();
			break;
#endif // DEBUG
		case citylist_stats_t::goods_demand:
		case citylist_stats_t::goods_product:
			if (!city->get_city_factories().get_count()) {
				new_component<gui_empty_t>();
				break;
			}
			else {
				slist_tpl<const goods_desc_t*> city_goods;
				for(fabrik_t* city_fab : city->get_city_factories()) {
					for(ware_production_t const& goods : mode == citylist_stats_t::goods_demand ? city_fab->get_input() : city_fab->get_output()) {
						city_goods.append_unique(goods.get_typ());
					}
				}

				if (city_goods.get_count()) {
					add_table(city_goods.get_count()+1,1);
					{
						new_component<gui_image_t>(skinverwaltung_t::input_output ? skinverwaltung_t::input_output->get_image_id(mode == citylist_stats_t::goods_demand ? 0:1) : IMG_EMPTY, 0, ALIGN_CENTER_V, true);
						for (const goods_desc_t* &goods : city_goods) {
							add_table(3,1);
							{
								new_component<gui_colorbox_t>(goods->get_color())->set_size(GOODS_COLOR_BOX_SIZE);
								new_component<gui_label_t>(goods->get_name());
								new_component<gui_margin_t>(D_H_SPACE);
							}
							end_table();
						}
					}
					end_table();
				}
				else {
					new_component<gui_empty_t>();
				}

				city_goods.clear();
			}
			break;
		default:
			break;
	}

	set_size(get_size());
}

void gui_city_stats_t::draw(scr_coord offset)
{
	bool update_flag = false;
	if (mode != citylist_stats_t::display_mode) {
		update_flag = true;
	}

	// TODO: Describe the data update conditions for each mode

	if (update_flag) {
		update_seed = 0;
		update_table();
	}

	gui_aligned_container_t::draw(offset);
}


citylist_stats_t::citylist_stats_t(stadt_t *c)
{
	city = c;
	set_table_layout(4,1);

	button_t *b = new_component<button_t>();
	b->set_typ(button_t::posbutton_automatic);
	b->set_targetpos(city->get_center());

	electricity.set_image(skinverwaltung_t::electricity->get_image_id(0), true);
	electricity.set_rigid(true);
	add_component(&electricity);

	lb_name.set_fixed_width( name_width );
	add_component(&lb_name);

	update_label();

	swichable_info = new_component<gui_city_stats_t>(city);

}

void citylist_stats_t::update_label()
{
	electricity.set_visible(city->get_finance_history_month(0, HIST_POWER_RECEIVED));
	if ( (city->get_finance_history_month(0, HIST_POWER_RECEIVED) * 9) < (world()->get_finance_history_month(0, HIST_POWER_NEEDED) / 10) ) {
		electricity.set_transparent(TRANSPARENT25_FLAG);
	}

	const scr_coord_val temp_w = proportional_string_width( city->get_name() ) + D_H_SPACE;
	if (temp_w > name_width) {
		name_width = temp_w;
	}
	lb_name.buf().printf("%s ", city->get_name());
	lb_name.update();

	set_size(size);
}


void citylist_stats_t::set_size(scr_size size)
{
	gui_aligned_container_t::set_size(size);
}


void citylist_stats_t::draw( scr_coord pos)
{
	update_label();
	lb_name.set_fixed_width(name_width);
	if (win_get_magic((ptrdiff_t)city)) {
		display_blend_wh_rgb(pos.x + get_pos().x, pos.y+get_pos().y, get_size().w, get_size().h, SYSCOL_TEXT_HIGHLIGHT, 30);
	}
	gui_aligned_container_t::draw(pos);
}


bool citylist_stats_t::is_valid() const
{
	return world()->get_cities().is_contained(city);
}


bool citylist_stats_t::infowin_event(const event_t *ev)
{
	bool swallowed = gui_aligned_container_t::infowin_event(ev);

	if (!swallowed  &&  IS_LEFTRELEASE(ev)) {
		city->show_info();
		swallowed = true;
	}
	else if (IS_RIGHTRELEASE(ev)) {
		world()->get_viewport()->change_world_position(city->get_pos());
	}
	return swallowed;
}


bool citylist_stats_t::compare(const gui_component_t *aa, const gui_component_t *bb)
{
	const citylist_stats_t* a = dynamic_cast<const citylist_stats_t*>(aa);
	const citylist_stats_t* b = dynamic_cast<const citylist_stats_t*>(bb);
	// good luck with mixed lists
	assert(a != NULL && b != NULL);

	if (sortreverse) {
		std::swap(a,b);
	}

	if (  sort_mode != sort_mode_t::SORT_BY_NAME  ) {
		switch (  sort_mode  ) {
			case SORT_BY_NAME: // default
				break;
			case SORT_BY_SIZE:
				return a->city->get_city_population() < b->city->get_city_population();
			case SORT_BY_GROWTH:
				return a->city->get_wachstum() < b->city->get_wachstum();
			case SORT_BY_REGION:
				return world()->get_region(a->city->get_pos()) < world()->get_region(b->city->get_pos());
			case SORT_BY_JOBS:
				return a->city->get_city_jobs() < b->city->get_city_jobs();
			case SORT_BY_VISITOR_DEMANDS:
				return a->city->get_city_visitor_demand() < b->city->get_city_visitor_demand();
			case SORT_BY_TRANSPORTED:
				return a->city->get_cityhistory_last_quarter(HIST_PAS_TRANSPORTED) < b->city->get_cityhistory_last_quarter(HIST_PAS_TRANSPORTED);
			case SORT_BY_RATIO_PAX:
			{
				const uint64 a_temp = a->city->get_cityhistory_last_quarter(HIST_PAS_GENERATED) ? 100*a->city->get_cityhistory_last_quarter(HIST_PAS_TRANSPORTED)/a->city->get_cityhistory_last_quarter(HIST_PAS_GENERATED) : 0;
				const uint64 b_temp = b->city->get_cityhistory_last_quarter(HIST_PAS_GENERATED) ? 100*b->city->get_cityhistory_last_quarter(HIST_PAS_TRANSPORTED)/b->city->get_cityhistory_last_quarter(HIST_PAS_GENERATED) : 0;
				return a_temp < b_temp;
			}
			case SORT_BY_SENT:
				return a->city->get_cityhistory_last_quarter(HIST_MAIL_TRANSPORTED) < b->city->get_cityhistory_last_quarter(HIST_MAIL_TRANSPORTED);
			case SORT_BY_RATIO_MAIL:
			{
				const uint64 a_temp = a->city->get_cityhistory_last_quarter(HIST_MAIL_GENERATED) ? 100*a->city->get_cityhistory_last_quarter(HIST_MAIL_TRANSPORTED)/a->city->get_cityhistory_last_quarter(HIST_MAIL_GENERATED) : 0;
				const uint64 b_temp = b->city->get_cityhistory_last_quarter(HIST_MAIL_GENERATED) ? 100*b->city->get_cityhistory_last_quarter(HIST_MAIL_TRANSPORTED)/b->city->get_cityhistory_last_quarter(HIST_MAIL_GENERATED) : 0;
				return a_temp < b_temp;
			}
			case SORT_BY_GOODS_DEMAND:
				return a->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED) < b->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED);
			case SORT_BY_GOODS_RECEIVED:
				return a->city->get_cityhistory_last_quarter(HIST_GOODS_RECEIVED) < b->city->get_cityhistory_last_quarter(HIST_GOODS_RECEIVED);
			case SORT_BY_RATIO_GOODS:
			{
				const uint64 a_temp = a->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED) ? 100 * a->city->get_cityhistory_last_quarter(HIST_GOODS_RECEIVED) / a->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED) : 0;
				const uint64 b_temp = b->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED) ? 100 * b->city->get_cityhistory_last_quarter(HIST_GOODS_RECEIVED) / b->city->get_cityhistory_last_quarter(HIST_GOODS_NEEDED) : 0;
				return a_temp < b_temp;
			}
			case SORT_BY_LAND_AREA:
				return a->city->get_land_area() < b->city->get_land_area();
			case SORT_BY_POPULATION_DENSITY:
				return a->city->get_population_density() < b->city->get_population_density();

#ifdef DEBUG
			case SORT_BY_JOB_DEMAND:
				return a->city->get_unemployed() < b->city->get_unemployed();
			case SORT_BY_RES_DEMAND:
				return a->city->get_homeless() < b->city->get_homeless();
#endif // DEBUG

			default: break;
		}
		// default sorting ...
	}
	// first: try to sort by number
	const char *atxt = a->get_text();
	int aint = 0;
	// isdigit produces with UTF8 assertions ...
	if (atxt[0] >= '0'  &&  atxt[0] <= '9') {
		aint = atoi(atxt);
	}
	else if (atxt[0] == '('  &&  atxt[1] >= '0'  &&  atxt[1] <= '9') {
		aint = atoi(atxt + 1);
	}
	const char *btxt = b->get_text();
	int bint = 0;
	if (btxt[0] >= '0'  &&  btxt[0] <= '9') {
		bint = atoi(btxt);
	}
	else if (btxt[0] == '('  &&  btxt[1] >= '0'  &&  btxt[1] <= '9') {
		bint = atoi(btxt + 1);
	}
	if (aint != bint) {
		return (aint - bint) < 0;
	}
	// otherwise: sort by name
	return strcmp(atxt, btxt) < 0;
}

void citylist_stats_t::recalc_wold_max()
{
	if (display_mode == cl_general) return;
#ifdef DEBUG
	if (display_mode == dbg_demands) return;
#endif // DEBUG

	world_max_value = 0;
	for(stadt_t* const city : world()->get_cities()) {
		switch (display_mode)
		{
			case cl_population:
				world_max_value = max(world_max_value, city->get_city_population());
				break;
			case cl_jobs:
				world_max_value = max(world_max_value, city->get_city_jobs());
				break;
			case cl_visitor_demand:
				world_max_value = max(world_max_value, city->get_city_visitor_demand());
				break;
			case pax_traffic:
				world_max_value = max(world_max_value, city->get_finance_history_month(1, HIST_PAS_GENERATED));
				break;
			case mail_traffic:
				world_max_value = max(world_max_value, city->get_finance_history_month(1, HIST_MAIL_GENERATED));
				break;
			case goods_traffic:
				world_max_value = max(world_max_value, city->get_finance_history_month(1, HIST_GOODS_NEEDED));
				break;
			default:
				break;
		}
	}
	if (world_max_value<200) {
		world_max_value = L_MAX_GRAPH_WIDTH;
	}

	return;
}
