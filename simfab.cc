/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include "simdebug.h"
#include "display/simimg.h"
#include "simcolor.h"
#include "simskin.h"
#include "boden/grund.h"
#include "boden/boden.h"
#include "boden/wege/strasse.h"
#include "boden/fundament.h"
#include "simfab.h"
#include "simcity.h"
#include "simhalt.h"
#include "simware.h"
#include "simworld.h"
#include "descriptor/building_desc.h"
#include "descriptor/goods_desc.h"
#include "descriptor/sound_desc.h"
#include "player/simplay.h"

#include "simmesg.h"
#include "simintr.h"

#include "obj/wolke.h"
#include "obj/gebaeude.h"
#include "obj/field.h"
#include "obj/leitung2.h"

#include "dataobj/settings.h"
#include "dataobj/environment.h"
#include "dataobj/translator.h"
#include "dataobj/loadsave.h"

#include "descriptor/factory_desc.h"
#include "bauer/hausbauer.h"
#include "bauer/fabrikbauer.h"

#include "gui/fabrik_info.h"

#include "utils/simrandom.h"
#include "utils/cbuffer_t.h"

#include "gui/simwin.h"
#include "display/simgraph.h"
#include "utils/simstring.h"

#include "path_explorer.h"


// Fabrik_t

static const int FAB_MAX_INPUT = 15000;
// Half a display unit (0.5).
static const sint64 FAB_DISPLAY_UNIT_HALF = ((sint64)1 << (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS - 1));
// Half a production factor unit (0.5).
static const sint32 FAB_PRODFACT_UNIT_HALF = ((sint32)1 << (DEFAULT_PRODUCTION_FACTOR_BITS - 1));

karte_ptr_t fabrik_t::welt;

const vector_tpl<koord> fabrik_t::null_vector = vector_tpl<koord>();

/**
 * Convert internal values to displayed values
 */
sint64 convert_goods(sint64 value) { return ((value + FAB_DISPLAY_UNIT_HALF) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS)); }
sint64 convert_power(sint64 value) { return (value >> POWER_TO_MW); }
sint64 convert_boost(sint64 value) { return ((value * 100 + (DEFAULT_PRODUCTION_FACTOR >> 1)) >> DEFAULT_PRODUCTION_FACTOR_BITS); }


/**
 * Ordering based on relative distance to a fixed point `origin'.
 */
class RelativeDistanceOrdering
{
private:
	const koord m_origin;
public:
	RelativeDistanceOrdering(const koord& origin)
		: m_origin(origin)
	{ /* nothing */ }

	/**
	 * Returns true if `a' is closer to the origin than `b', otherwise false.
	 */
	bool operator()(const koord& a, const koord& b) const
	{
		return shortest_distance(m_origin, a) < shortest_distance(m_origin, b);
	}
};


void ware_production_t::init_stats()
{
	for(  int m=0;  m<MAX_MONTH;  ++m  ) {
		for(  int s=0;  s<MAX_FAB_GOODS_STAT;  ++s  ) {
			statistics[m][s] = 0;
		}
	}
	max_transit = 0;
}


void ware_production_t::roll_stats(uint32 factor)
{
	for(  int s=0;  s<MAX_FAB_GOODS_STAT;  ++s  ) {
		for(  int m=MAX_MONTH-1;  m>0;  --m  ) {
			statistics[m][s] = statistics[m-1][s];
		}
		if(  s==FAB_GOODS_TRANSIT  ) {
			// keep the current amount in transit
			statistics[0][s] = statistics[1][s];
		}
		else {
			statistics[0][s] = 0;
		}
	}

	// restore current storage level
	set_stat((sint64)menge * (sint64)factor, FAB_GOODS_STORAGE);
}


void ware_production_t::rdwr(loadsave_t *file, uint8)
{
	if(  file->is_loading()  ) {
		init_stats();
	}

	// we use a temporary variable to save/load old data correctly
	sint64 statistics_buf[MAX_MONTH][MAX_FAB_GOODS_STAT];
	memcpy( statistics_buf, statistics, sizeof(statistics_buf) );
	if(  file->is_saving()  && ( file->is_version_less(120, 1) || (file->get_extended_version() < 13 && file->get_extended_version() < 18))  ) {
		for(  int m=0;  m<MAX_MONTH;  ++m  ) {
			statistics_buf[m][0] = (statistics[m][FAB_GOODS_STORAGE] >> DEFAULT_PRODUCTION_FACTOR_BITS);
			statistics_buf[m][2] = (statistics[m][2] >> DEFAULT_PRODUCTION_FACTOR_BITS);
		}
	}

	sint64 dummy=0;
	if(  file->is_version_atleast(112, 1)  ) {
		for(  int s=0;  s<MAX_FAB_GOODS_STAT;  ++s  ) {
			for(  int m=0;  m<MAX_MONTH;  ++m  ) {
				file->rdwr_longlong( statistics_buf[m][s] );
			}
		}
		file->rdwr_longlong( dummy );
	}
	else if(  file->is_version_atleast(110, 5)  ) {
		// save/load statistics
		for(  int s=0;  s<3;  ++s  ) {
			for(  int m=0;  m<MAX_MONTH;  ++m  ) {
				file->rdwr_longlong( statistics_buf[m][s] );
			}
		}
		file->rdwr_longlong( dummy );
	}

	if (file->is_loading()) {
		memcpy(statistics, statistics_buf, sizeof(statistics_buf));

		// Apply correction for output production graphs which have had their precision changed for factory normalization.
		// Also apply a fix for corrupted in-transit values caused by a logical error.
		if(file->is_version_less(120, 1) || (file->get_extended_version() < 13 && file->get_extended_version() < 18)){
			for(  int m=0;  m<MAX_MONTH;  ++m  ) {
				statistics[m][0] = (statistics[m][FAB_GOODS_STORAGE] & 0xffffffff) << DEFAULT_PRODUCTION_FACTOR_BITS;
				statistics[m][2] = (statistics[m][2] & 0xffffffff) << DEFAULT_PRODUCTION_FACTOR_BITS;
			}
		}
	}

	if(  file->is_loading()  ) {
		// recalc transit always on load
		set_stat(0, FAB_GOODS_TRANSIT);
	}
}


void ware_production_t::book_weighted_sum_storage(uint32 factor)
{
	const sint64 amount = (sint64)menge * (sint64)factor;
	set_stat(amount, FAB_GOODS_STORAGE);
}

void ware_production_t::add_contracts(sint32 add, ware_production_t &ware_in, ware_production_t &ware_out, uint32 index, sint32 max_output){
	const sint32 min_cont=256;

	sint32 cont = ware_out.get_contract(index);
	sint32 total_in = ware_in.get_total_contracts();
	sint32 total_out = ware_out.get_total_contracts();

	//avoid adding contracts beyond current production
	if(add>0 && total_out + add > max_output){
		add = max_output - total_out;
	}

	//total contracts must remain above zero
	if(add<-total_in){
		add=-total_in;
	}
	if(add<-total_out){
		add=-total_out;
	}

	//contract must be zero or greater than min contract
	if(cont+add < min_cont/2){
		add = -cont;
	}else if(cont + add < min_cont){
		add = min_cont - cont;
	}

	//adjust the contract documents
	ware_in.add_total_contracts(add);
	ware_out.add_contract(index,add);
	ware_out.add_total_contracts(add);
}

void fabrik_t::arrival_statistics_t::init()
{
	for(  uint32 s=0;  s<SLOT_COUNT;  ++s  ) {
		slots[s] = 0;
	}
	current_slot = 0;
	active_slots = 0;
	aggregate_arrival = 0;
	scaled_demand = 0;
}


void fabrik_t::arrival_statistics_t::rdwr(loadsave_t *file)
{
	if(  file->is_version_atleast(110, 5)  ) {
		if(  file->is_loading()  ) {
			aggregate_arrival = 0;
			for(  uint32 s=0;  s<SLOT_COUNT;  ++s  ) {
				file->rdwr_short( slots[s] );
				aggregate_arrival += slots[s];
			}
			scaled_demand = 0;
		}
		else {
			for(  uint32 s=0;  s<SLOT_COUNT;  ++s  ) {
				file->rdwr_short( slots[s] );
			}
		}
		file->rdwr_short( current_slot );
		file->rdwr_short( active_slots );
	}
	else if(  file->is_loading()  ) {
		init();
	}
}


sint32 fabrik_t::arrival_statistics_t::advance_slot()
{
	sint32 result = 0;
	// advance to the next slot
	++current_slot;
	if(  current_slot>=SLOT_COUNT  ) {
		current_slot = 0;
	}
	// handle expiration of past arrivals and reset slot to 0
	if(  slots[current_slot]>0  ) {
		aggregate_arrival -= slots[current_slot];
		slots[current_slot] = 0;
		if(  aggregate_arrival==0  ) {
			// reset slot count to 0 as all previous arrivals have expired
			active_slots = 0;
		}
		result |= ARRIVALS_CHANGED;
	}
	// count the number of slots covered since aggregate arrival last increased from 0 to +ve
	if(  active_slots>0  &&  active_slots<SLOT_COUNT  ) {
		++active_slots;
		result |= ACTIVE_SLOTS_INCREASED;
	}
	return result;
}


void fabrik_t::arrival_statistics_t::book_arrival(const uint16 amount)
{
	if(  aggregate_arrival==0  ) {
		// new arrival after complete inactivity -> start counting slots
		active_slots = 1;
	}
	// increment current slot and aggregate arrival
	slots[current_slot] += amount;
	aggregate_arrival += amount;
}


void fabrik_t::update_transit( const ware_t& ware, bool add )
{
	if(  ware.index > goods_manager_t::INDEX_NONE  ) {
		// only for freights
		fabrik_t *fab = get_fab( ware.get_zielpos() );
		if(  fab  ) {
			fab->update_transit_intern( ware, add );
		}
	}
}


// just for simplicity ...
void fabrik_t::update_transit_intern( const ware_t& ware, bool add )
{
	for(ware_production_t &w : input ) {
		if(  w.get_typ()->get_index() == ware.index  ) {

			w.book_stat_no_negative(add ? (sint64)ware.menge : -(sint64)ware.menge, FAB_GOODS_TRANSIT );
			return;
		}
	}
}


void fabrik_t::init_stats()
{
	for(  int m=0;  m<MAX_MONTH;  ++m  ) {
		for(  int s=0;  s<MAX_FAB_STAT;  ++s  ) {
			statistics[m][s] = 0;
		}
	}
}


void fabrik_t::book_weighted_sums()
{
	// storage level of input/output stores
	for (uint32 in = 0; in < input.get_count(); in++) {
		input[in].book_weighted_sum_storage(welt->get_settings().using_fab_contracts() ? (DEFAULT_PRODUCTION_FACTOR) : desc->get_supplier(in)->get_consumption());
	}
	for (uint32 out = 0; out < output.get_count(); out++) {
		output[out].book_weighted_sum_storage(welt->get_settings().using_fab_contracts() ? (DEFAULT_PRODUCTION_FACTOR) : desc->get_product(out)->get_factor());
	}

	// production rate
	set_stat(calc_operation_rate(0), FAB_PRODUCTION);

	// electricity, pax and mail boosts
	set_stat(prodfactor_electric, FAB_BOOST_ELECTRIC);

	// power produced or consumed
	if (!desc->is_electricity_producer()) {
		set_stat(power*1000, FAB_POWER); // convert MW to KW
	}
	else {
		set_stat(power, FAB_POWER);
	}
}


void fabrik_t::update_scaled_electric_demand()
{
	if(desc->get_electric_amount() == 65535)
	{
		// demand not specified in pak, use old fixed demands and the Extended electricity proportion
		const uint16 electricity_proportion = get_desc()->is_electricity_producer() ? 400 : get_desc()->get_electricity_proportion();
		scaled_electric_demand = PRODUCTION_DELTA_T * (uint32)(prodbase * electricity_proportion) / 100;
		return;
	}

	// If the demand is specified in the pakset, do not use the Extended electricity proportion.
	const sint64 prod = std::max(1ll, (sint64)desc->get_productivity());
	scaled_electric_demand = (uint32)( (( (sint64)(desc->get_electric_amount()) * (sint64)prodbase + (prod >> 1ll) ) / prod) << (sint64)POWER_TO_MW );

	if(  scaled_electric_demand == 0  ) {
		prodfactor_electric = 0;
	}

	scaled_electric_demand = welt->scale_for_distance_only(scaled_electric_demand);
}


void fabrik_t::update_scaled_pax_demand(bool is_from_saved_game)
{
	if(!welt->is_destroying())
	{
		const uint16 employment_capacity = desc->get_building()->get_employment_capacity();
		const int passenger_level = get_passenger_level_jobs();

		const sint64 base_visitor_demand_raw = building ? building->get_visitor_demand() : desc->get_building()->get_population_and_visitor_demand_capacity();
		const sint64 base_visitor_demand = base_visitor_demand_raw == 65535u ? (sint64)get_passenger_level_visitors() : base_visitor_demand_raw;
		const sint64 base_worker_demand = employment_capacity == 65535u ? (sint64)passenger_level : (sint64)employment_capacity;

		// then, scaling based on month length
		scaled_pax_demand = std::max(welt->calc_adjusted_monthly_figure(base_worker_demand), 1ll);
		const uint32 scaled_visitor_demand = std::max(welt->calc_adjusted_monthly_figure(base_visitor_demand), 1ll);

		// pax demand for fixed period length
		// Intentionally not the scaled value.
		arrival_stats_pax.set_scaled_demand(base_worker_demand);

		if(building && (!is_from_saved_game || !building->get_loaded_passenger_and_mail_figres()))
		{
			const uint32 percentage = std::max(1u, ((uint32)get_desc()->get_productivity() * 100)) / std::max(1u, (uint32)(get_base_production()));
			if (percentage > 100u)
			{
				get_building()->set_adjusted_jobs((uint16)std::min(65535u, (scaled_pax_demand * percentage) / 100u));
				get_building()->set_adjusted_visitor_demand((uint16)std::min(65535u, (scaled_visitor_demand * percentage) / 100u));
			}
			else
			{
				building->set_adjusted_jobs((uint16)std::min(65535u, scaled_pax_demand));
				building->set_adjusted_visitor_demand((uint16)std::min(65535u, scaled_visitor_demand));
			}
			// Must update the world building list to take into account the new passenger demand (weighting)
			welt->update_weight_of_building_in_world_list(building);
		}
	}
}


void fabrik_t::update_scaled_mail_demand(bool is_from_saved_game)
{
	if(!welt->is_destroying())
	{
		// first, scaling based on current production base
		const sint64 prod = desc->get_productivity() > 0 ? desc->get_productivity() : 1ll;
		// Take into account fields
		sint64 prod_adjust = prod;
		const field_group_desc_t *fd = desc->get_field_group();
		if(fd)
		{
			for(uint32 i=0; i<fields.get_count(); i++)
			{
				const field_class_desc_t *fc = fd->get_field_class( fields[i].field_class_index );
				if (fc)
				{
					prod_adjust += fc->get_field_production();
				}
			}

			if (desc->get_field_output_divider() > 1)
			{
				prod_adjust = max(1, prod_adjust / desc->get_field_output_divider());
			}
		}

		// formula : desc_mail_demand * (current_production_base / desc_production_base); (prod >> 1) is for rounding
		const uint16 mail_capacity = desc->get_building()->get_mail_demand_and_production_capacity();
		const int mail_level = get_mail_level();

		const sint64 base_mail_demand =  mail_capacity == 65535u ? mail_level : mail_capacity;
		const uint32 mail_demand = (uint32)((base_mail_demand * (sint64)prodbase + (prod_adjust >> 1ll) ) / prod_adjust);
		// then, scaling based on month length
		scaled_mail_demand = std::max(welt->calc_adjusted_monthly_figure(mail_demand), 1u);

		// mail demand for fixed period length
		// Intentionally not the scaled value.
		arrival_stats_mail.set_scaled_demand(mail_demand);
		if (building && (!is_from_saved_game || !building->get_loaded_passenger_and_mail_figres()))
		{
			const uint32 percentage = std::max(1u, ((uint32)get_desc()->get_productivity() * 100)) / std::max(1u, (uint32)(get_base_production()));
			if (percentage > 100u)
			{
				get_building()->set_adjusted_mail_demand((uint16)std::min(65535u, (scaled_mail_demand * percentage) / 100));
			}
			else
			{
				building->set_adjusted_mail_demand((uint16)std::min(65535u, scaled_mail_demand));
			}

			// Must update the world building list to take into account the new passenger demand (weighting)
			welt->update_weight_of_building_in_world_list(building);
		}
	}
}

int fabrik_t::get_passenger_level_jobs() const
{
	// This figure will be 65355 unless this is an older pakset.
	const int base_passenger_level = desc->get_pax_level();
	if(base_passenger_level != 65535)
	{
		return base_passenger_level;
	}

	return (int)desc->get_building()->get_level() * (int)welt->get_settings().get_jobs_per_level();
}

int fabrik_t::get_passenger_level_visitors() const
{
	// This figure will be 65355 unless this is an older pakset.
	const int base_passenger_level = desc->get_pax_level();
	if(base_passenger_level != 65535)
	{
		return base_passenger_level;
	}

	return desc->get_building()->get_level() * welt->get_settings().get_visitor_demand_per_level();
}

int fabrik_t::get_mail_level() const
{
	// This figure will be 65355 unless this is an older pakset.
	const int base_mail_level = desc->get_pax_level();
	if(base_mail_level != 65535)
	{
		return base_mail_level;
	}

	return desc->get_building()->get_level() * welt->get_settings().get_mail_per_level();
}

void fabrik_t::update_prodfactor_pax()
{
	// calculate pax boost based on arrival data and demand of the fixed-length period
	const uint32 periods = welt->get_settings().get_factory_arrival_periods();
	const uint32 slots = arrival_stats_pax.get_active_slots();
	const uint32 pax_demand = ( periods==1 || slots*periods<=(uint32)SLOT_COUNT ?
									arrival_stats_pax.get_scaled_demand() :
									( slots==(uint32)SLOT_COUNT ?
										arrival_stats_pax.get_scaled_demand() * periods :
										(arrival_stats_pax.get_scaled_demand() * periods * slots) >> SLOT_BITS ) );
	const uint32 pax_arrived = arrival_stats_pax.get_aggregate_arrival();
	// This is necessary because passenger demand is potentially spread over a number of months so as to make the jobs figure consistent
	// with the population figure.
	// TODO:  Consider making this better integrated with the general system.
	const uint32 adjusted_pax_demand = (pax_demand * 100) / welt->get_settings().get_job_replenishment_per_hundredths_of_months();
	if(  pax_demand==0  ||  pax_arrived==0  ||  desc->get_pax_boost()==0  ) {
		prodfactor_pax = 0;
	}
	else if(  pax_arrived>=adjusted_pax_demand  ) {
		// maximum boost
		prodfactor_pax = desc->get_pax_boost();
	}
	else {
		// pro-rata boost : (pax_arrived / pax_demand) * desc_pax_boost; (pax_demand >> 1) is for rounding
		prodfactor_pax = (sint32)( ( (sint64)pax_arrived * (sint64)(desc->get_pax_boost()) + (sint64)(adjusted_pax_demand >> 1) ) / (sint64)adjusted_pax_demand );
	}
	set_stat(prodfactor_pax, FAB_BOOST_PAX);
}


void fabrik_t::update_prodfactor_mail()
{
	// calculate mail boost based on arrival data and demand of the fixed-length period
	const uint32 periods = welt->get_settings().get_factory_arrival_periods();
	const uint32 slots = arrival_stats_mail.get_active_slots();
	const uint32 mail_demand = ( periods==1 || slots*periods<=(uint32)SLOT_COUNT ?
									arrival_stats_mail.get_scaled_demand() :
									( slots==(uint32)SLOT_COUNT ?
										arrival_stats_mail.get_scaled_demand() * periods :
										(arrival_stats_mail.get_scaled_demand() * periods * slots) >> SLOT_BITS ) );
	const uint32 mail_arrived = arrival_stats_mail.get_aggregate_arrival();
	if(  mail_demand==0  ||  mail_arrived==0  ||  desc->get_mail_boost()==0  ) {
		prodfactor_mail = 0;
	}
	else if(  mail_arrived>=mail_demand  ) {
		// maximum boost
		prodfactor_mail = desc->get_mail_boost();
	}
	else {
		// pro-rata boost : (mail_arrived / mail_demand) * desc_mail_boost; (mail_demand >> 1) is for rounding
		prodfactor_mail = (sint32)( ( (sint64)mail_arrived * (sint64)(desc->get_mail_boost()) + (sint64)(mail_demand >> 1) ) / (sint64)mail_demand );
	}
	set_stat(prodfactor_mail, FAB_BOOST_MAIL);
}


void fabrik_t::recalc_storage_capacities()
{
	if (desc->get_field_group()) {
		// with fields -> calculate based on capacities contributed by fields
		const uint32 ware_types = input.get_count() + output.get_count();
		if (ware_types>0) {
			// calculate total storage capacity contributed by fields
			const field_group_desc_t *const field_group = desc->get_field_group();
			sint32 field_capacities = 0;
			FOR(vector_tpl<field_data_t>, const& f, fields) {
				field_capacities += field_group->get_field_class(f.field_class_index)->get_storage_capacity();
			}
			const sint32 share = (sint32)(((sint64)field_capacities << precision_bits) / (sint64)ware_types);
			// first, for input goods
			// This minimum storage capacity (set in the pak's simuconf.tab) allows for loading a whole (small) truck.
			uint32 min_in = welt->get_settings().get_minimum_industry_input_storage_raw();
			for(ware_production_t & g : input) {
				if(const factory_supplier_desc_t *const input = desc->get_supplier(g.get_typ())){
					// Inputs are now normalized to factory production.
					uint32 prod_factor = welt->get_settings().using_fab_contracts() ? 256 : input->get_consumption();
					g.max = (sint32)(welt->scale_for_distance_only((((sint64)((input->get_capacity() << precision_bits) + share) << DEFAULT_PRODUCTION_FACTOR_BITS) + (sint64)(prod_factor - 1)) / (sint64)prod_factor));
					if (g.get_capacity(prod_factor) < min_in) {
						g.max = (sint32)( ( (sint64)min_in << precision_bits << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor );
					}
				}
			}
			// then, for output goods
			// This minimum storage capacity (set in the pak's simuconf.tab) allows for loading a whole (small) truck.
			uint32 min_out = welt->get_settings().get_minimum_industry_output_storage_raw();
			for(ware_production_t & g : output) {
				if(const factory_product_desc_t *const output = desc->get_product(g.get_typ())){
					// Outputs are now normalized to factory production.
					uint32 prod_factor = welt->get_settings().using_fab_contracts() ? 256 : output->get_factor();
					g.max = (sint32)(welt->scale_for_distance_only((((sint64)((output->get_capacity() << precision_bits) + share) << DEFAULT_PRODUCTION_FACTOR_BITS) + (sint64)(prod_factor - 1)) / (sint64)prod_factor));
					if (g.get_capacity(prod_factor) < min_out) {
						g.max = (sint32)( ( (sint64)min_out << precision_bits << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor );
					}
				}
			}
		}
	}
	else {
		// without fields -> scaling based on prodbase
		// first, for input goods
		// This minimum storage capacity (set in the pak's simuconf.tab) allows for loading a whole (small) truck.
		uint32 min_in = welt->get_settings().get_minimum_industry_input_storage_raw();
		for(ware_production_t & g : input) {
			if(const factory_supplier_desc_t *const input = desc->get_supplier(g.get_typ())){
				// Inputs are now normalized to factory production.
				uint32 prod_factor = welt->get_settings().using_fab_contracts() ? 256 : input->get_consumption();
				g.max = (sint32)(welt->scale_for_distance_only(((((sint64)input->get_capacity() * (sint64)prodbase) << (precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS)) + (sint64)(prod_factor - 1)) / ((sint64)desc->get_productivity() * (sint64)prod_factor)));
					if (g.get_capacity(prod_factor) < min_in) {
						g.max = (sint32)( ( (sint64)min_in << precision_bits << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor );
					}
			}
		}
		// then, for output goods
		// This minimum storage capacity (set in the pak's simuconf.tab) allows for loading a whole (small) truck.
		uint32 min_out = welt->get_settings().get_minimum_industry_output_storage_raw();
		for(ware_production_t & g : output) {
			if(const factory_product_desc_t *const output = desc->get_product(g.get_typ())){
				// Outputs are now normalized to factory production.
				uint32 prod_factor = welt->get_settings().using_fab_contracts() ? 256 : output->get_factor();
				g.max = (sint32)(welt->scale_for_distance_only(((((sint64)output->get_capacity() * (sint64)prodbase) << (precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS)) + (sint64)(prod_factor - 1)) / ((sint64)desc->get_productivity() * (sint64)prod_factor)));
					if (g.get_capacity(prod_factor) < min_out) {
						g.max = (sint32)( ( (sint64)min_out << precision_bits << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor );
					}
			}
		}
	}
}

void fabrik_t::set_base_production(sint32 p, bool is_from_saved_game)
{
	prodbase = p > 0 ? p : 1;
	recalc_storage_capacities();
	update_scaled_electric_demand();
	update_scaled_pax_demand(is_from_saved_game);
	update_scaled_mail_demand(is_from_saved_game);
	update_prodfactor_pax();
	update_prodfactor_mail();
	calc_max_intransit_percentages();
}


fabrik_t *fabrik_t::get_fab(const koord &pos)
{
	const grund_t *gr = welt->lookup_kartenboden(pos);
	if(gr) {
		gebaeude_t *gb = gr->find<gebaeude_t>();
		if(gb) {
			return gb->get_fabrik();
		}
	}
	return NULL;
}


void fabrik_t::add_consumer(koord ziel, const goods_desc_t *desc, const sint64 contract)
{
	for( auto& ware : get_output()){
		if(ware.get_typ()==desc){
			if(fabrik_t *fab = get_fab( ziel )){
				if(auto dest_ware = fab->get_input(ware.get_typ())){
					ware.link_add(ziel, RelativeDistanceOrdering(pos.get_2d()),contract);
					ware.add_total_contracts(contract);
					dest_ware->add_total_contracts(contract);
					fab->add_supplier(get_pos().get_2d(),desc);
				}
			}
		}else if(!desc){
			if(fabrik_t *fab = get_fab( ziel )){
				if(auto dest_ware = fab->get_input(ware.get_typ())){
					ware.link_add(ziel, RelativeDistanceOrdering(pos.get_2d()),contract);
					ware.add_total_contracts(contract);
					dest_ware->add_total_contracts(contract);
					fab->add_supplier(get_pos().get_2d(),ware.get_typ());
				}
			}
		}
	}
}


void fabrik_t::remove_consumer(koord consumer_pos)
{
	for(auto &ware : get_output()){
		uint32 idx=ware.link_index(consumer_pos);
		if(idx!=UINT32_MAX_VALUE){
			if(welt->get_settings().using_fab_contracts()){
				sint32 contract=ware.get_contract(idx);
				ware.sub_total_contracts(contract);
			}
			ware.link_remove(consumer_pos);
		}
	}
}

bool fabrik_t::disconnect_consumer(koord consumer_pos) //Returns true if must be destroyed.
{
	if (consumer_pos != koord::invalid)
	{
		remove_consumer(consumer_pos);
	}

	vector_tpl<const goods_desc_t*> unfulfilled_requirements;
	// Check to ensure that all supply types are still connected

	for(auto &ware : output){
		if(!ware.link_count()){
			unfulfilled_requirements.append(ware.get_typ());
		}
	}

	if (!unfulfilled_requirements.empty())
	{
		// If there are no consumers left, industry is orphaned.
		// Reconnect or close.

		// Attempt to reconnect.

		for(sint16 i = welt->get_fab_list().get_count() - 1; i >= 0; i --)
		{
			fabrik_t* fab = welt->get_fab_list()[i];
			const factory_desc_t* fab_desc = fab->get_desc();

			for(uint32 k = 0; k < unfulfilled_requirements.get_count(); k++){
				auto unfilled_product = unfulfilled_requirements[k];
				for (uint32 j = 0; j < fab_desc->get_supplier_count(); j++)
				{
					if(unfilled_product == fab_desc->get_supplier(j)->get_input_type()) {
						add_customer(fab,unfilled_product);
						unfulfilled_requirements.remove(unfilled_product);
						k--;
					}
					if(unfulfilled_requirements.empty()){
						return false;
					}
				}
			}
		}
		// Destroy if missing all outputs
		return unfulfilled_requirements.get_count() == desc->get_product_count();
	}
	return false;
}

bool fabrik_t::disconnect_supplier(koord supplier_pos, fabrik_t* supplier) //Returns true if must be destroyed.
{
	if (supplier_pos != koord::invalid)
	{
		remove_supplier(supplier_pos, supplier);
	}

	//try to connect with a supplier capable of supplying the goods
	if(welt->get_settings().using_fab_contracts()){
		for(uint32 i = 0; i < input.get_count(); i++){
			auto &ware=input[i];
			if(!ware.link_count()){
				factory_builder_t::build_chain_link(this,this->get_desc(),i,welt->get_public_player(),true);
			}
		}
	}

	vector_tpl<const goods_desc_t*> unfulfilled_requirements;
	// Check to ensure that all supply types are still connected

	for(uint32 i = 0; i < input.get_count(); i++){
		auto &ware=input[i];
		if(!ware.link_count()){
			unfulfilled_requirements.append(ware.get_typ());
			if(welt->get_settings().using_fab_contracts()){

			}
		}
	}

	if(!unfulfilled_requirements.empty())
	{
		// If there are any missing supplies, the industry is orphaned.
		// Reconnect or close.

		// Attempt to reconnect.

		for(sint16 i = welt->get_fab_list().get_count() - 1; i >= 0; i --)
		{
			fabrik_t* fab = welt->get_fab_list()[i];
			const factory_desc_t* fab_desc = fab->get_desc();

			for(uint32 k = 0; k < unfulfilled_requirements.get_count(); k++){
				auto unfilled_product = unfulfilled_requirements[k];
				for (uint32 j = 0; j < fab_desc->get_product_count(); j++)
				{
					if(unfilled_product == fab_desc->get_product(j)->get_output_type()) {
						add_supplier(fab,unfilled_product);
						unfulfilled_requirements.remove(unfilled_product);
						k--;
					}
					if(unfulfilled_requirements.empty()){
						return false;
					}
				}
			}
		}
		// Destroy if missing all inputs for end-consumers, or if missing any inputs for other industries.
		return sector == end_consumer ? unfulfilled_requirements.get_count() == desc->get_supplier_count() : true;
	}
	return false;
}


fabrik_t::fabrik_t(loadsave_t* file) :
	sector(ftype::unknown)
{
	owner = NULL;
	power = 0;
	power_demand = 0;
	prodfactor_electric = 0;
	city = NULL;
	building = NULL;
	pos = koord3d::invalid;
	months_unproductive=0;
	months_missing_contracts=0;

	rdwr(file);

	delta_t_sum = 0;
	delta_amount = 0;
	delta_amount_remainder = 0;
	total_input = total_transit = total_output = 0;
	sector = unknown;
	status = nothing;
	currently_producing = false;
	transformer_connected = NULL;
	last_sound_ms = welt->get_ticks();

	if(  desc == NULL  ) {
		dbg->warning( "fabrik_t::fabrik_t()", "No pak-file for factory at (%s) - will not be built!", pos_origin.get_str() );
		return;
	}
	else if(  !welt->is_within_limits(pos_origin.get_2d())  ) {
		dbg->warning( "fabrik_t::fabrik_t()", "%s is not a valid position! (Will not be built!)", pos_origin.get_str() );
		desc = NULL; // to get rid of this broken factory later...
	}
	else
	{
		// This will create a new gebaeude_t object if our existing one has not already been saved and re-loaded.
		build(rotate, false, false, true);

		// now get rid of construction image
		for(  sint16 y=0;  y<desc->get_building()->get_y(rotate);  y++  ) {
			for(  sint16 x=0;  x<desc->get_building()->get_x(rotate);  x++  ) {
				gebaeude_t *gb = welt->lookup_kartenboden( pos_origin.get_2d()+koord(x,y) )->find<gebaeude_t>();
				if(  gb  ) {
					gb->add_alter(10000ll);
				}
			}
		}
	}
}


fabrik_t::fabrik_t(koord3d pos_, player_t* owner, const factory_desc_t* desc, sint32 initial_prod_base) :
	desc(desc),
	pos(pos_)
{
	pos.z = welt->max_hgt(pos.get_2d());
	pos_origin = pos;
	building = NULL;

	this->owner = owner;
	prodfactor_electric = 0;
	prodfactor_pax = 0;
	prodfactor_mail = 0;
	if (initial_prod_base < 0) {
		prodbase = desc->get_productivity() + simrand(desc->get_range(), "fabrik_t::fabrik_t() prodbase");
	}
	else {
		prodbase = initial_prod_base;
	}

	delta_t_sum = 0;
	delta_amount = 0;
	delta_amount_remainder = 0;
	activity_count = 0;
	currently_producing = false;
	transformer_connected = NULL;
	power = 0;
	power_demand = 0;
	total_input = total_transit = total_output = 0;
	sector = unknown;
	status = nothing;
	city = check_local_city();
	months_unproductive=0;
	months_missing_contracts=0;

	if(desc->get_placement() == 2 && city && desc->get_product_count() == 0 && !desc->is_electricity_producer())
	{
		// City consumer industries (other than power stations) set their consumption rates by the relative size of the city
		const weighted_vector_tpl<stadt_t*>& cities = welt->get_cities();

		sint64 biggest_city_population = 0;
		sint64 smallest_city_population = -1;

		for (weighted_vector_tpl<stadt_t*>::const_iterator i = cities.begin(), end = cities.end(); i != end; ++i)
		{
			stadt_t* const c = *i;
			const sint64 pop = c->get_finance_history_month(0,HIST_CITIZENS);
			if(pop > biggest_city_population)
			{
				biggest_city_population = pop;
			}
			else if(pop < smallest_city_population || smallest_city_population == -1)
			{
				smallest_city_population = pop;
			}
		}

		const sint64 this_city_population = city->get_finance_history_month(0,HIST_CITIZENS);
		sint32 production;

		if(this_city_population == biggest_city_population)
		{
			production = desc->get_range();
		}
		else if(this_city_population == smallest_city_population)
		{
			production = 0;
		}
		else
		{
			const sint64 percentage = (this_city_population - smallest_city_population) * 100ll / (biggest_city_population - smallest_city_population);
			production = (desc->get_range() * (sint32)percentage) / 100;
		}
		prodbase = desc->get_productivity() + production;
	}
	else if(desc->get_placement() == 2 && !city && desc->get_product_count() == 0 && !desc->is_electricity_producer())
	{
		prodbase = desc->get_productivity();
	}
	else
	{
		prodbase = desc->get_productivity() + simrand(desc->get_range(), "fabrik_t::fabrik_t");
	}

	prodbase = prodbase > 0 ? prodbase : 1;

	// create input information
	input.resize( desc->get_supplier_count() );
	for(  int g=0;  g<desc->get_supplier_count();  ++g  ) {
		const factory_supplier_desc_t *const input_fac = desc->get_supplier(g);
		input[g].set_typ(input_fac->get_input_type() );
	}

	// create output information
	output.resize( desc->get_product_count() );
	for(  uint g=0;  g<desc->get_product_count();  ++g  ) {
		const factory_product_desc_t *const product = desc->get_product(g);
		output[g].set_typ( product->get_output_type() );
	}

	reset_consumer_active();

	recalc_storage_capacities();
	if (input.empty()) {
		for(ware_production_t & g : output) {
			if (g.max > 0) {
				// if source then start with full storage, so that AI will build line(s) immediately
				g.menge = g.max - 1;
			}
		}
	}

	last_sound_ms = welt->get_ticks();
	init_stats();
	arrival_stats_pax.init();
	arrival_stats_mail.init();

	delta_slot = 0;
	times_expanded = 0;

	calc_max_intransit_percentages();

	update_scaled_electric_demand();
	update_scaled_pax_demand();
	update_scaled_mail_demand();

	// We can't do these here, because get_tile_list will fail
	// We have to wait until after ::build is called
	// It would be better to call ::build here, but that fails too
	// --neroden
	// mark_connected_roads(false);
	// recalc_nearby_halts();
}

void fabrik_t::mark_connected_roads(bool del)
{
	grund_t* gr;
	vector_tpl<koord> tile_list;
	get_tile_list(tile_list);
	FOR(vector_tpl<koord>, const k, tile_list)
	{
		for(uint8 i = 0; i < 8; i ++)
		{
			// Check for connected roads. Only roads in immediately neighbouring tiles
			// and only those on the same height will register a connexion.
			koord3d pos3d(k + k.neighbours[i], pos.z);
			gr = welt->lookup(pos3d);
			if(!gr)
			{
				continue;
			}
			strasse_t* str = (strasse_t*)gr->get_weg(road_wt);

			if(str)
			{
				if(del)
				{
					str->connected_buildings.remove(building);
				}
				else
				{
					str->connected_buildings.append_unique(building);
				}
			}
		}
	}
}

void fabrik_t::delete_all_fields(bool destructor)
{
	bool adjust=false;
	while(!fields.empty())
	{
		planquadrat_t *plan = welt->access( fields.back().location );
		// if destructor is called when world is destroyed, plan is already invalid
		if (plan) {
			grund_t *gr = plan->get_kartenboden();
			if (field_t* f = gr->find<field_t>()) {
				f->pre_delete();
				delete f; // implicitly removes the field from fields
				plan->boden_ersetzen( gr, new boden_t(gr->get_pos(), slope_t::flat ) );
				plan->get_kartenboden()->calc_image();
				adjust=true;
				continue;
			}
		}
		fields.pop_back();
	}
	if(!destructor && adjust){
		adjust_production_for_fields();
	}
	// destroy chart window, if present
	destroy_win((ptrdiff_t)this);
}

fabrik_t::~fabrik_t()
{
	if (!welt->is_destroying())
	{
		mark_connected_roads(true);
	}
	delete_all_fields(true);

	if(!welt->is_destroying())
	{
		welt->remove_building_from_world_list(get_building());

		if(city)
		{
			city->remove_city_factory(this);
		}

		if (desc != NULL)
		{
			//welt->decrease_actual_industry_density(100 / desc->get_distribution_weight());
			if (desc->is_consumer_only()) {
				welt->decrease_actual_industry_density(100 / desc->get_distribution_weight());
			}
		}

		// Disconnect this factory from all chains.
		// @author: jamespetts
		uint32 number_of_customers = get_consumers().get_count();
		uint32 number_of_suppliers = get_suppliers().get_count();
		const weighted_vector_tpl<stadt_t*>& staedte = welt->get_cities();
		for(weighted_vector_tpl<stadt_t*>::const_iterator j = staedte.begin(), end = staedte.end(); j != end; ++j)
		{
			(*j)->remove_connected_industry(this);
		}

		char buf[192];
		// Tell the players about this, including a city name if possible
		if(city) {
			sprintf(buf, translator::translate("Industry: %s in %s has closed, with the loss of %d jobs.\n%d upstream suppliers and %d downstream customers are affected."), translator::translate(get_name()), city->get_name(), get_base_pax_demand(), number_of_suppliers, number_of_customers);
		}
		else {
			const stadt_t* nearby_city = welt->find_nearest_city(pos.get_2d());
			if(nearby_city) {
				sprintf(buf, translator::translate("Industry: %s near to %s has closed, with the loss of %d jobs.\n%d upstream suppliers and %d downstream customers are affected."), translator::translate(get_name()), nearby_city->get_name(), get_base_pax_demand(), number_of_suppliers, number_of_customers);
			}
			else {
				sprintf(buf, translator::translate("Industry:\n%s\nhas closed,\nwith the loss\nof %d jobs.\n%d upstream\nsuppliers and\n%d downstream\ncustomers\nare affected."), translator::translate(get_name()), get_base_pax_demand(), number_of_suppliers, number_of_customers);
			}
		}
		welt->get_message()->add_message(buf, pos.get_2d(), message_t::industry, color_idx_to_rgb(COL_DARK_RED), skinverwaltung_t::neujahrsymbol->get_image_id(0));
		for(auto &ware : get_output()){
			for(sint32 i = ware.link_count() - 1; i >= 0; i --)
			{
				fabrik_t* tmp = get_fab(ware.link_from_index(i));
				if(tmp && tmp->disconnect_supplier(pos.get_2d(),this))
				{
					// Orphaned, must be deleted.
					gebaeude_t* gb = tmp->get_building();
					hausbauer_t::remove(welt->get_public_player(), gb, false);
				}
			}
		}
		for(auto &ware : get_input()){
			for(sint32 i = ware.link_count() - 1; i >= 0; i --)
			{
				fabrik_t* tmp = get_fab(ware.link_from_index(i));
				if(tmp && tmp->disconnect_consumer(pos.get_2d()))
				{
					// Orphaned, must be deleted.
					gebaeude_t* gb = tmp->get_building();
					hausbauer_t::remove(welt->get_public_player(), gb, false);
				}
			}
		}
		if(transformer_connected)
		{
			transformer_connected->clear_factory();
		}
	}
}


void fabrik_t::build(sint32 rotate, bool build_fields, bool, bool from_saved)
{
	this->rotate = rotate;
	pos_origin = welt->lookup_kartenboden(pos_origin.get_2d())->get_pos();
	if (building && building->get_pos() == koord3d::invalid)
	{
		const sint64 available_jobs_by_time = building->get_available_jobs_by_time();
		const uint16 passengers_succeeded_visiting = building->get_passengers_succeeded_visiting();
		const uint16 passengers_succeeded_visiting_last_year = building->get_passenger_success_percent_last_year_visiting();
		const uint16 passengers_succeeded_commuting = building->get_passengers_succeeded_commuting();
		const uint16 passengers_succeeded_commuting_last_year = building->get_passenger_success_percent_last_year_commuting();
		const uint16 adjusted_jobs = building->get_adjusted_jobs();
		const uint16 adjusted_visitor_demand = building->get_adjusted_visitor_demand();
		const uint16 adjusted_mail_demand = building->get_adjusted_mail_demand();
		const bool loaded_passenger_and_mail_figres = building->get_loaded_passenger_and_mail_figres();
		const uint16 mail_generate = building->get_mail_generated();
		const uint16 mail_delivery_succeeded_last_year = building->get_mail_delivery_succeeded_last_year();
		const uint16 mail_delivery_succeeded = building->get_mail_delivery_succeeded();
		const uint16 mail_delivery_success_percent_last_year = building->get_mail_delivery_success_percent_last_year();

		delete building;
		building = hausbauer_t::build(owner, pos_origin, rotate, desc->get_building(), this);

		building->set_available_jobs_by_time(available_jobs_by_time);
		building->add_passengers_succeeded_visiting(passengers_succeeded_visiting);
		building->add_passengers_succeeded_commuting(passengers_succeeded_commuting);
		building->set_passengers_visiting_last_year(passengers_succeeded_visiting_last_year);
		building->set_passengers_commuting_last_year(passengers_succeeded_commuting_last_year);
		building->set_adjusted_jobs(adjusted_jobs);
		building->set_adjusted_visitor_demand(adjusted_visitor_demand);
		building->set_adjusted_mail_demand(adjusted_mail_demand);
		building->set_loaded_passenger_and_mail_figres(loaded_passenger_and_mail_figres);
		building->add_mail_generated(mail_generate);
		building->add_mail_delivery_succeeded(mail_delivery_succeeded);
		building->set_mail_delivery_succeeded_last_year(mail_delivery_succeeded_last_year);
		building->set_mail_delivery_success_percent_last_year(mail_delivery_success_percent_last_year);
	}
	if(!building)
	{
		building = hausbauer_t::build(owner, pos_origin, rotate, desc->get_building(), this);
	}

	city = check_local_city();
	if (city)
	{
		city->add_city_factory(this);
	}
	set_sector();

	pos = building->get_pos();
	pos_origin.z = pos.z;

	// Must build roads before fields so that the road is not blocked by the fields.
	if (!from_saved && welt->get_settings().get_auto_connect_industries_and_attractions_by_road())
	{
		building->connect_by_road_to_nearest_city();
	}

	if(desc->get_field_group()) {
		// if there are fields
		if(  !fields.empty()  ) {
			for(  uint16 i=0;  i<fields.get_count();  i++   ) {
				const koord k = fields[i].location;
				grund_t *gr=welt->lookup_kartenboden(k);
				if(  gr->ist_natur()  ) {
					// first make foundation below
					grund_t *gr2 = new fundament_t(gr->get_pos(), gr->get_grund_hang());
					welt->access(k)->boden_ersetzen(gr, gr2);
					gr2->obj_add( new field_t(gr2->get_pos(), owner, desc->get_field_group()->get_field_class( fields[i].field_class_index ), this ) );
				}
				else {
					// there was already a building at this position => do not restore!
					fields.remove_at(i);
					i--;
				}
			}
		}
		else if(  build_fields  )
		{
			// make sure not to exceed initial prodbase too much
			// we will start with a minimum number and try to get closer to start_fields
			const uint16 spawn_fields = desc->get_field_group()->get_min_fields() + simrand(desc->get_field_group()->get_start_fields() - desc->get_field_group()->get_min_fields(), "fabrik_t::build");
			while(  fields.get_count() < spawn_fields  &&  add_random_field(10000u)  )
			{
				/*if (fields.get_count() > desc->get_field_group()->get_min_fields()  &&  prodbase >= 2*org_prodbase) {
					// too much productivity, no more fields needed
					break;
				}*/
			}
			adjust_production_for_fields();
		}
	}
	else
	{
		fields.clear();
	}
}


/* field generation code
 */
bool fabrik_t::add_random_field(uint16 probability)
{
	// has fields, and not yet too many?
	const field_group_desc_t *fd = desc->get_field_group();
	if(fd==NULL  ||  fd->get_max_fields() <= fields.get_count()) {
		return false;
	}
	// we are lucky and are allowed to generate a field
	if(simrand(10000, "bool fabrik_t::add_random_field")>=probability) {
		return false;
	}

	// we start closest to the factory, and check for valid tiles as we move out
	uint8 radius = 1;

	// pick a coordinate to use - create a list of valid locations and choose a random one
	slist_tpl<grund_t *> build_locations;
	do {
		for(sint32 xoff = -radius; xoff < radius + get_desc()->get_building()->get_size().x ; xoff++) {
			for(sint32 yoff =-radius ; yoff < radius + get_desc()->get_building()->get_size().y; yoff++) {
				// if we can build on this tile then add it to the list
				grund_t *gr = welt->lookup_kartenboden(pos.get_2d()+koord(xoff,yoff));
				// Check for runways
				const karte_t::runway_info ri = welt->check_nearby_runways(pos.get_2d());
				if (gr != NULL &&
						gr->get_typ()        == grund_t::boden &&
						(gr->get_hoehe()     == pos.z || gr->get_hoehe() == pos.z + 1 || gr->get_hoehe() == pos.z - 1) &&
						gr->get_grund_hang() == slope_t::flat &&
						gr->ist_natur() &&
						ri.pos == koord::invalid &&
						(gr->find<leitung_t>() || gr->kann_alle_obj_entfernen(NULL) == NULL)) {
					// only on same height => climate will match!
					build_locations.append(gr);
					assert(gr->find<field_t>() == NULL);
				}
				// skip inside of rectangle (already checked earlier)
				if(radius > 1 && yoff == -radius && (xoff > -radius && xoff < radius + get_desc()->get_building()->get_size().x - 1)) {
					yoff = radius + get_desc()->get_building()->get_size().y - 2;
				}
			}
		}
		if (build_locations.empty()) {
			radius++;
		}
	} while (radius <= 128 && build_locations.empty());
	// built on one of the positions
	if (!build_locations.empty()) {
		grund_t *gr = build_locations.at(simrand(build_locations.get_count(), "bool fabrik_t::add_random_field"));
		leitung_t* lt = gr->find<leitung_t>();
		if(lt) {
			gr->obj_remove(lt);
		}
		gr->obj_loesche_alle(NULL);
		// first make foundation below
		const koord k = gr->get_pos().get_2d();
		field_data_t new_field(k);
		assert(!fields.is_contained(new_field));
		// fetch a random field class desc based on spawn weights
		const weighted_vector_tpl<uint16> &field_class_indices = fd->get_field_class_indices();
		new_field.field_class_index = pick_any_weighted(field_class_indices);
		const field_class_desc_t *const field_class = fd->get_field_class( new_field.field_class_index );
		fields.append(new_field);
		grund_t *gr2 = new fundament_t(gr->get_pos(), gr->get_grund_hang());
		welt->access(k)->boden_ersetzen(gr, gr2);
		gr2->obj_add( new field_t(gr2->get_pos(), owner, field_class, this ) );
		// adjust production base and storage capacities
		if(lt) {
			gr2->obj_add( lt );
		}
		gr2->calc_image();
		return true;
	}
	return false;
}


void fabrik_t::remove_field_at(koord pos)
{
	field_data_t field(pos);
	assert(fields.is_contained( field ));
	field = fields[ fields.index_of(field) ];
	fields.remove(field);
	// revert the field's effect on production base and storage capacities
	adjust_production_for_fields();
}

// "Are there any?" (Google Translate)
vector_tpl<fabrik_t *> &fabrik_t::sind_da_welche(koord min_pos, koord max_pos)
{
	static vector_tpl <fabrik_t*> factory_list(16);
	factory_list.clear();

	for(int y=min_pos.y; y<=max_pos.y; y++) {
		for(int x=min_pos.x; x<=max_pos.x; x++) {
			fabrik_t *fab=get_fab(koord(x,y));
			if(fab) {
				if (factory_list.append_unique(fab)) {
//DBG_MESSAGE("fabrik_t::sind_da_welche()","appended factory %s at (%i,%i)",gr->first_obj()->get_fabrik()->get_desc()->get_name(),x,y);
				}
			}
		}
	}
	return factory_list;
}


/**
 * if name==NULL translate desc factory name in game language
 */
char const* fabrik_t::get_name() const
{
    return name ? name.c_str() : translator::translate(desc->get_name(), welt->get_settings().get_name_language_id());
}


void fabrik_t::set_name(const char *new_name)
{
	if(new_name==NULL  ||  strcmp(new_name, translator::translate(desc->get_name(), welt->get_settings().get_name_language_id()))==0) {
		// new name is equal to name given by desc/translation -> set name to NULL
		name = NULL;
	} else {
		name = new_name;
	}

	fabrik_info_t *win = dynamic_cast<fabrik_info_t*>(win_get_magic((ptrdiff_t)this));
	if (win) {
		win->update_info();
	}
}


void fabrik_t::rdwr(loadsave_t *file)
{
	xml_tag_t f( file, "fabrik_t" );
	sint32 i;
	sint32 owner_n;
	sint32 input_count;
	sint32 output_count;
	sint32 consumers_count;
	uint8 sub_version=1; //4 bit local version number
	uint8 contract_version=1;
	if(  file->is_saving()  ) {
		if(welt->get_settings().using_fab_contracts()){
			sub_version|=6;
		}

		input_count = input.get_count();
		output_count = output.get_count();
		consumers.clear();
		contracts.clear();
		for(auto &ware : output){
			for(uint32 i = 0; i < ware.link_count(); i++){
				consumers.append(ware.link_from_index(i));
				if(sub_version & 2){
					contracts.append(ware.get_contract(i));
				}
			}
		}

		consumers_count = consumers.get_count();
		const char *s = desc->get_name();
		file->rdwr_str(s);

	}
	else {
		char s[256];
		file->rdwr_str(s, lengthof(s));
DBG_DEBUG("fabrik_t::rdwr()","loading factory '%s'",s);
		desc = factory_builder_t::get_desc(s);
		if(  desc==NULL  ) {
			//  maybe it was only renamed?
			desc = factory_builder_t::get_desc(translator::compatibility_name(s));
		}
		if(  desc==NULL  ) {
			dbg->warning( "fabrik_t::rdwr()", "Pak-file for factory '%s' missing!", s );
			// we continue loading even if desc==NULL
			welt->add_missing_paks( s, karte_t::MISSING_FACTORY );
		}
	}
	pos_origin.rdwr(file);

	//hide version information in upper nibble of rotate
	rotate &= 0xF;
	rotate |= sub_version << 4;

	// pos will be assigned after call to hausbauer_t::build
	file->rdwr_byte(rotate);

	sub_version = rotate >> 4;
	rotate &= 0xF;

	// now rebuilt information for received goods
	file->rdwr_long(input_count);
	if(  file->is_loading()  ) {
		input.resize( input_count );
	}
	bool mismatch = false;
	for(  i=0;  i<input_count;  ++i  ) {
		ware_production_t &ware = input[i];
		const char *ware_name = NULL;
		sint32 menge = ware.menge;
		if(  file->is_saving()  ) {
			ware_name = ware.get_typ()->get_name();
		}
		file->rdwr_str(ware_name);
		file->rdwr_long(menge);
		if(  file->is_version_less(110, 5)  ) {
			// max storage is only loaded/saved for older versions
			file->rdwr_long(ware.max);
		}

		/*
		* The below is from Standard, but it is hard to implement in Extended because of the need
		* to detect whether a JIT2 setting, set elsewhere, was set or not when the game was saved.
		* Hence, Standard games with this set will not be loadable in Extended.
		*/
		/*
		//  JIT2 needs to store input demand buffer
		if(  welt->get_settings().get_just_in_time() >= 2  &&  file->is_version_atleast(120, 1)  ){
			file->rdwr_long(ware.demand_buffer);
		}
		*/

		ware.rdwr( file, sub_version );

		if(  file->is_loading()  ) {
			if (!ware_name) {
				dbg->fatal("fabrik_t::rdwr", "Invalid ware at input slot %d of factory at %s", i, pos.get_fullstr());
			}
			ware.set_typ( goods_manager_t::get_info(ware_name) );

			if(  !desc  ||  !desc->get_supplier(i)  ) {
			// Maximum in-transit is always 0 on load.
			if(  welt->get_settings().get_just_in_time() < 2  ) {
				ware.max_transit = 0;
			}

				if (desc) {
					dbg->warning( "fabrik_t::rdwr()", "Factory at %s requested producer for %s but has none!", pos_origin.get_fullstr(), ware_name);
				}
				ware.menge = 0;
			}

			else {
				// Inputs used to be with respect to actual units of production. They now are normalized with respect to factory production so require conversion.
				const uint32 prod_factor = desc ? desc->get_supplier(i)->get_consumption() : 1;
				if(  file->is_version_less(120, 1)  ) {
					ware.menge = (sint32)(((sint64)menge << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor);
				}
				else {
					ware.menge = menge;
				}

				// repair files that have 'insane' values
				const sint32 max = (sint32)((((sint64)FAB_MAX_INPUT << (precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS)) + (sint64)(prod_factor - 1)) / (sint64)prod_factor);
				if (ware.menge < 0) {
					ware.menge = 0;
				}
				if (ware.menge > max) {
					ware.menge = max;
				}

				if (ware.get_typ() != desc->get_supplier(i)->get_input_type()) {
					mismatch = true;
					dbg->warning("fabrik_t::rdwr", "Factory at %s: producer[%d] mismatch in savegame=%s/%s, in pak=%s",
						pos_origin.get_fullstr(), i, ware_name, ware.get_typ()->get_name(), desc->get_supplier(i)->get_input_type()->get_name());
				}
			}
			free(const_cast<char *>(ware_name));

			/*
			* It's very easy for in-transit information to get corrupted,
			* if an intermediate program version fails to compute it right.
			* So *always* compute it fresh.  Do NOT load it.
			* It will be recomputed by halts and vehicles.
			*
			* Note, for this to work factories must be loaded before halts and vehicles
			* (this is how it is currently done in simworld.cc)
			*/
		}
	}

	if (desc  &&  input_count != desc->get_supplier_count()) {
		dbg->warning("fabrik_t::rdwr", "Mismatch of input slot count for factory %s at %s: savegame = %d, pak = %d", get_name(), pos_origin.get_fullstr(), input_count, desc->get_supplier_count());
		// resize input to match the descriptor
		input.resize(desc->get_supplier_count());
		mismatch = true;
	}
	if (mismatch) {
		array_tpl<ware_production_t> dummy;
		dummy.resize(desc->get_supplier_count());
		for (uint16 i = 0; i<desc->get_supplier_count(); i++) {
			dummy[i] = input[i];
		}
		for (uint16 i = 0; i<desc->get_supplier_count(); i++) {
			// search for matching type
			bool missing = true;
			const goods_desc_t* goods = desc->get_supplier(i)->get_input_type();
			for (uint16 j = 0; j<desc->get_supplier_count() && missing; j++) {
				if (dummy[j].get_typ() == goods) {
					input[i] = dummy[j];
					dummy[j].set_typ(NULL);
					missing = false;
				}
			}
			if (missing) {
				input[i].set_typ(goods);
			}
		}
	}

	// now rebuilt information for produced goods
	file->rdwr_long(output_count);
	if(  file->is_loading()  ) {
		output.resize( output_count );
	}
	mismatch = false;
	for(  i=0;  i<output_count;  ++i  ) {
		ware_production_t &ware = output[i];
		const char *ware_name = NULL;
		sint32 menge = ware.menge;
		if(  file->is_saving()  ) {
			ware_name = ware.get_typ()->get_name();
			// correct scaling for older saves
			if(  file->is_version_less(120, 1)  ){
				menge = (sint32)(((sint64)ware.menge * desc->get_product(i)->get_factor() ) >> DEFAULT_PRODUCTION_FACTOR_BITS);
			}
		}
		file->rdwr_str(ware_name);
		file->rdwr_long(menge);
		if(  file->is_version_less(110, 5)  ) {
			// max storage is only loaded/saved for older versions
			file->rdwr_long(ware.max);
			// obsolete variables -> statistics already contain records on goods delivered
			sint32 abgabe_sum = (sint32)(ware.get_stat(0, FAB_GOODS_DELIVERED));
			sint32 abgabe_letzt = (sint32)(ware.get_stat(1, FAB_GOODS_DELIVERED));
			file->rdwr_long(abgabe_sum);
			file->rdwr_long(abgabe_letzt);
		}
		ware.rdwr( file, sub_version );
		if(  file->is_loading()  ) {
			if (ware_name && goods_manager_t::get_info(ware_name)) {
				ware.set_typ( goods_manager_t::get_info(ware_name));
			}
			else {
				dbg->fatal("fabrik_t::rdwr", "Invalid ware %s at output slot %d of factory at %s", ware_name ? ware_name : "", i, pos.get_fullstr());
			}

			if(  !desc  ||  !desc->get_product(i)  ) {
				if (desc) {
					dbg->warning( "fabrik_t::rdwr()", "Factory at %s requested consumer for %s but has none!", pos_origin.get_fullstr(), ware_name );
				}
				ware.menge = 0;
			}
			else {
				// Outputs used to be with respect to actual units of production. They now are normalized with respect to factory production so require conversion.
				if(  file->is_version_less(120, 1)  ){
					const uint32 prod_factor = desc ? desc->get_product(i)->get_factor() : 1;
					ware.menge = (sint32)(((sint64)menge << DEFAULT_PRODUCTION_FACTOR_BITS) / (sint64)prod_factor);
				}
				else {
					ware.menge = menge;
				}

				// repair files that have 'insane' values
				if(  ware.menge < 0  ) {
					ware.menge = 0;
				}

				if (ware.get_typ() != desc->get_product(i)->get_output_type()) {
					mismatch = true;
					dbg->warning("fabrik_t::rdwr", "Factory at %s: consumer[%d] mismatch in savegame=%s/%s, in pak=%s",
						pos_origin.get_fullstr(), i, ware_name, ware.get_typ()->get_name(), desc->get_product(i)->get_output_type()->get_name());
				}
			}
			free(const_cast<char *>(ware_name));
		}
	}
	if (desc  &&  output_count != desc->get_product_count()) {
		dbg->warning("fabrik_t::rdwr", "Mismatch of output slot count for factory %s at %s: savegame = %d, pak = %d", get_name(), pos_origin.get_fullstr(), output_count, desc->get_product_count());
		// resize output to match the descriptor
		output.resize(desc->get_product_count());
		mismatch = true;
	}
	if (mismatch) {
		array_tpl<ware_production_t> dummy;
		dummy.resize(desc->get_product_count());
		for (uint16 i = 0; i<desc->get_product_count(); i++) {
			dummy[i] = output[i];
		}
		for (uint16 i = 0; i<desc->get_product_count(); i++) {
			// search for matching type
			bool missing = true;
			const goods_desc_t* goods = desc->get_product(i)->get_output_type();
			for (uint16 j = 0; j<desc->get_product_count() && missing; j++) {
				if (dummy[j].get_typ() == goods) {
					output[i] = dummy[j];
					dummy[j].set_typ(NULL);
					missing = false;
				}
			}
			if (missing) {
				output[i].set_typ(goods);
			}
		}
	}

	// restore other information
	owner_n = welt->sp2num(owner);
	file->rdwr_long(owner_n);
	file->rdwr_long(prodbase);
	if(  file->is_version_less(110, 5)  ) {
		// prodfactor saving no longer required
		sint32 adjusted_value = (prodfactor_electric / 16) + 16;
		file->rdwr_long(adjusted_value);
	}

	if(  file->is_version_atleast(99, 17)  ) {
		file->rdwr_long(power);
	}

	// owner stuff
	if(  file->is_loading()  ) {
		// take care of old files
		if(  file->is_version_less(86, 1)  ) {
			koord k = desc ? desc->get_building()->get_size() : koord(1,1);
			DBG_DEBUG("fabrik_t::rdwr()","correction of production by %i",k.x*k.y);
			// since we step from 86.01 per factory, not per tile!
			prodbase *= k.x*k.y*2;
		}
		// restore factory owner
		// Due to a omission in Volkers changes, there might be savegames
		// in which factories were saved without an owner. In this case
		// set the owner to the default of player 1
		if(owner_n == -1) {
			// Use default
			owner = welt->get_public_player();
		}
		else {
			// Restore owner pointer
			owner = welt->get_player(owner_n);
		}
	}

	file->rdwr_long(consumers_count);

	// connect/save consumer
	for(int i=0; i < consumers_count; i++) {
		if(file->is_loading()) {
			consumers.append(koord::invalid);
			if(sub_version & 2){
				contracts.append(0);
			}
		}
		consumers[i].rdwr(file);
		if(sub_version & 2){
			file->rdwr_long(contracts[i]);
		}
	}

	if(sub_version>=1){
		uint32 consumers_active_this_month_count=consumers_active_this_month.get_count();
		file->rdwr_long(consumers_active_this_month_count);
		for(uint32 i = 0; i < consumers_active_this_month_count; i++){
		if(file->is_loading()){
			consumers_active_this_month.append(0);
		}
		file->rdwr_long(consumers_active_this_month[i]);
		}
	}else if(  file->is_version_atleast(112, 2)  ) {
		uint32 dummy=0; //old consumers_active_last_month
		file->rdwr_long(dummy);
	}

	// suppliers / consumers will be recalculated in finish_rd
	if (file->is_loading()  &&  welt->get_settings().is_crossconnect_factories()) {
		consumers.clear();
		consumers.resize(0);
	}

	if(file->is_saving()){
		consumers.clear();
		consumers.resize(0);
	}

	// information on fields ...
	if(  file->is_version_atleast(99, 10)  ) {
		if(  file->is_saving()  ) {
			uint16 nr=fields.get_count();
			file->rdwr_short(nr);
			if(  file->is_version_atleast(102, 3) && file->get_extended_version() != 7  ) {
				// each field stores location and a field class index
				for(  uint16 i=0  ;  i<nr  ;  ++i  ) {
					koord k = fields[i].location;
					k.rdwr(file);
					uint16 idx = fields[i].field_class_index;
					file->rdwr_short(idx);
				}
			}
			else {
				// each field only stores location
				for(  uint16 i=0  ;  i<nr  ;  ++i  ) {
					koord k = fields[i].location;
					k.rdwr(file);
				}
			}
		}
		else {
			uint16 nr=0;
			koord k;
			file->rdwr_short(nr);
			fields.resize(nr);
			if(  file->is_version_atleast(102, 3) && file->get_extended_version() != 7  ) {
				// each field stores location and a field class index
				for(  uint16 i=0  ;  i<nr  ;  ++i  ) {
					k.rdwr(file);
					uint16 idx;
					file->rdwr_short(idx);
					if (desc  &&  desc->get_field_group()) {
						// set class index to 0 if it is out of range, if there fields at all
						fields.append(field_data_t(k, idx >= desc->get_field_group()->get_field_class_count() ? 0 : idx));
					}
				}
			}
			else {
				// each field only stores location
				for (uint16 i = 0; i<nr; ++i) {
					k.rdwr(file);
					if (desc  &&  desc->get_field_group()) {
						// oald add fields if there are any defined
						fields.append(field_data_t(k, 0));
					}
				}
			}
		}
	}

	if(  file->is_version_atleast(99, 14) && file->get_extended_version() < 12  ) {
		// Was saving/loading of "target_cities".
		sint32 nr = 0;
		file->rdwr_long(nr);
		sint32 city_index = -1;
		for(int i=0; i < nr; i++)
		{
			file->rdwr_long(city_index);
		}
	}

	if(  file->is_version_less(110, 6) && file->get_extended_version() < 9  ) {
		// Necessary to ensure that the industry density is correct after re-loading a game.
		//welt->increase_actual_industry_density(100 / desc->get_distribution_weight());
		if (desc->is_consumer_only()) {
			welt->increase_actual_industry_density(100 / desc->get_distribution_weight());
		}
	}

	if(  file->is_version_atleast(110, 5)  ) {
		file->rdwr_short(times_expanded);
		// statistics
		//sint64 dummy64 = 0;
		for(  int s=0;  s<MAX_FAB_STAT;  ++s  ) {
			for(  int m=0;  m<MAX_MONTH;  ++m  ) {
				if (((file->get_extended_version() == 14 && file->get_extended_revision() <= 6) || file->get_extended_version() < 14) && s == 11)
				{
					statistics[m][s] = 0;
					continue;
				}
				file->rdwr_longlong(statistics[m][s]);
				if (s== FAB_PRODUCTION && (file->get_extended_version() < 14 || (file->get_extended_version() == 14 && file->get_extended_revision() < 29))) {
					// convert production to producivity
					if (prodbase && welt->calc_adjusted_monthly_figure(get_base_production())) {
						statistics[m][s] = calc_operation_rate(m);
					}
					else {
						statistics[m][s] = 0;
					}
				}
			}
		}
		sint64 dummy = 0;
		file->rdwr_longlong( dummy );
		file->rdwr_longlong( dummy );
		file->rdwr_longlong( dummy );
		file->rdwr_longlong( dummy );
		file->rdwr_longlong( dummy );
		file->rdwr_longlong( dummy );
		file->rdwr_long( delta_slot );
	}
	else if(  file->is_loading()  ) {
		times_expanded = 0;
		init_stats();
		delta_slot = 0;
	}
	arrival_stats_pax.rdwr( file );
	arrival_stats_mail.rdwr( file );

	if(  file->is_version_atleast(110, 7)  ) {
		file->rdwr_byte(activity_count);
	}
	else if(  file->is_loading()  ) {
		activity_count = 0;
	}

	// save name
	if(  file->is_version_atleast(110, 7)  ) {
		if(  file->is_saving() &&  !name  ) {
			char const* fullname = desc->get_name();
			file->rdwr_str(fullname);
		}
		else {
			file->rdwr_str(name);
			if(  file->is_loading()  &&  desc != NULL  &&  name == desc->get_name()  ) {
				// equal to desc name
				name = 0;
			}
		}
	}

	if (file->get_extended_version() >= 13 || file->get_extended_revision() >= 23)
	{
		if (file->is_loading())
		{
			building = new gebaeude_t(file, true);
		}
		else // Saving
		{
			building->rdwr(file);
		}
	}

	if(sub_version & 4){
		file->rdwr_byte(contract_version);
		if(contract_version>=1){
			file->rdwr_long(months_unproductive);
			file->rdwr_long(months_missing_contracts);
		}
	}

	if (file->is_loading())
	{
		has_calculated_intransit_percentages = false;
	}
	// Cannot calculate intransit percentages here,
	// as this can only be done when paths are available.
}


/**
 * let the chimney smoke, if there is something to produce
 */
void fabrik_t::smoke() const
{
	const smoke_desc_t *rada = desc->get_smoke();
	if(rada) {
		const koord size = desc->get_building()->get_size(0)-koord(1,1);
		const uint8 rot = (4-rotate)%desc->get_building()->get_all_layouts();
		koord ro = rada->get_pos_off(size,rot);
		koord smoke_pos = pos_origin.get_2d() + ro;
		if (!welt->is_within_grid_limits(smoke_pos))
		{
			smoke_pos = pos_origin.get_2d();
		}
		grund_t *gr = welt->lookup_kartenboden(smoke_pos);
		// to get same random order on different compilers
		const sint8 offsetx = ((rada->get_xy_off(rot).x) * OBJECT_OFFSET_STEPS) / 16;
		const sint8 offsety = ((rada->get_xy_off(rot).y) * OBJECT_OFFSET_STEPS) / 16;
		wolke_t* smoke = new wolke_t(gr->get_pos(), offsetx, offsety, rada->get_images());
		gr->obj_add(smoke);
		welt->sync_way_eyecandy.add(smoke);
	}
	// maybe sound?
	if (!world()->is_fast_forward() && desc->get_sound() != NO_SOUND && (welt->get_ticks() > (last_sound_ms + desc->get_sound_interval_ms()))) {
		welt->play_sound_area_clipped( get_pos().get_2d(), desc->get_sound(), FACTORY_SOUND, noise_barrier_wt );
		// The below does not work because this is const - but without this, the whole sound interval does not work.
		//last_sound_ms = welt->get_ticks();
	}
}


uint32 fabrik_t::scale_output_production(const uint32 product, uint32 menge) const
{
	// prorate production based upon amount of product in storage
	// but allow full production rate for storage amounts less than the normal minimum distribution amount (10)
	const uint32 maxi = output[product].max;
	const uint32 actu = output[product].menge;
	if(  actu<maxi  ) {
		if(  actu >= ((10+1)<<fabrik_t::precision_bits)-1  ) {
			if(  menge>(0x7FFFFFFFu/maxi)  ) {
				// avoid overflow
				menge = (((maxi-actu)>>5)*(menge>>5))/(maxi>>10);
			}
			else {
				// and that is the simple formula
				menge = (menge*(maxi-actu)) / maxi;
			}
		}
	}
	else {
		// overfull? No production
		menge = 0;
	}
	return menge;
}


sint32 fabrik_t::get_input_stock(const goods_desc_t *ware)
{
	sint32 menge = -1;

	for(ware_production_t const& i : input) {
		if (ware == i.get_typ()) {
			menge = i.menge >> precision_bits;
			break;
		}
	}

	return menge;
}


sint32 fabrik_t::get_output_stock(const goods_desc_t *ware)
{
	sint32 menge = -1;

	for(ware_production_t const& i : output) {
		if (ware == i.get_typ()) {
			menge = i.menge >> precision_bits;
			break;
		}
	}

	return menge;
}


sint32 fabrik_t::liefere_an(const goods_desc_t *typ, sint32 menge)
{
	if(  typ==goods_manager_t::passengers  ) {
		// book pax arrival and recalculate pax boost
		book_stat(menge, FAB_PAX_ARRIVED);
		if(!building)
		{
			building = welt->lookup(pos)->find<gebaeude_t>();
		}
		arrival_stats_pax.book_arrival(menge);
		update_prodfactor_pax();
		return menge;
	}
	else if(  typ==goods_manager_t::mail  ) {
		// book mail arrival and recalculate mail boost
		book_stat(menge, FAB_MAIL_ARRIVED);
		arrival_stats_mail.book_arrival(menge);
		update_prodfactor_mail();
		return menge;
	}
	else {
		// case : freight
		if(welt->get_settings().using_fab_contracts()){
			if(ware_production_t *ware = get_input(typ)){
				ware->menge+=menge<<fabrik_t::precision_bits;
				ware->book_stat_no_negative(-menge, FAB_GOODS_TRANSIT);

				//do not overflow
				if(ware->menge > (FAB_MAX_INPUT << fabrik_t::precision_bits)){
					menge-=FAB_MAX_INPUT - (ware->menge >> fabrik_t::precision_bits);
					ware->menge = (FAB_MAX_INPUT << fabrik_t::precision_bits);
				}

				ware->book_stat(menge, FAB_GOODS_RECEIVED);
				return menge;
			}
			return -1;
		}

		for (uint32 in = 0; in < input.get_count(); in++) {
			ware_production_t& ware = input[in];
			if(  ware.get_typ() == typ  ) {
				// Can't use update_transit for interface reasons; we don't take a ware argument.
				// We should, however.
				ware.book_stat_no_negative( -menge, FAB_GOODS_TRANSIT );

				// Resolve how much normalized production arrived, rounding up (since we rounded up when we removed it).
				const uint32 prod_factor = desc->get_supplier(in) ? desc->get_supplier(in)->get_consumption() : 1;
				const sint32 prod_delta = (sint32)((((sint64)menge << (DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits)) + (sint64)(prod_factor - 1)) / (sint64)prod_factor);

				ware.menge += prod_delta;

				// avoid overflow
				const sint32 max = (sint32)((((sint64)FAB_MAX_INPUT << (precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS)) + (sint64)(prod_factor - 1)) / (sint64)prod_factor);
				if (ware.menge >= max) {
					menge -= (sint32)(((sint64)menge * (sint64)(ware.menge - max) + (sint64)(prod_delta >> 1)) / (sint64)prod_delta);
					ware.menge = max - 1;
				}
				ware.book_stat(menge, FAB_GOODS_RECEIVED);
				return menge;
			}
		}
	}
	// ware "typ" wird hier nicht verbraucht
	return -1;
}

void fabrik_t::add_consuming_passengers(sint32 number_of_passengers)
{
	if (desc->is_electricity_producer())
	{
		// Visiting passengers do not alter power stations' consumption of resources.
		return;
	}

	const sint64 passenger_demand_ticks_per_month = welt->ticks_per_world_month / (sint64)building->get_adjusted_visitor_demand();
	const sint64 delta_t = passenger_demand_ticks_per_month * number_of_passengers;

	const sint32 boost = get_prodfactor();

	// calculate the production per delta_t; scaled to PRODUCTION_DELTA_T
	// Calculate actual production. A remainder is used for extra precision.
	const uint64 want_prod_long = welt->scale_for_distance_only((uint64)prodbase * (uint64)boost * (uint64)delta_t + (uint64)delta_amount_remainder);
	const sint32 prod = (uint32)(want_prod_long >> (PRODUCTION_DELTA_T_BITS + DEFAULT_PRODUCTION_FACTOR_BITS + DEFAULT_PRODUCTION_FACTOR_BITS - fabrik_t::precision_bits));
	delta_amount_remainder = (uint32)(want_prod_long & ((1 << (PRODUCTION_DELTA_T_BITS + DEFAULT_PRODUCTION_FACTOR_BITS + DEFAULT_PRODUCTION_FACTOR_BITS - fabrik_t::precision_bits)) - 1));

	// Consume stock in proportion to passengers' visits
	// We want to consume prod amount of each input normally. However, if
	// some inputs are empty, then passenger visits were (or will be)
	// scaled down, so we need to scale up consumption of those goods which are
	// present.


	sint64 total_consumption_factor = 0;
	for (uint32 index = 0; index < input.get_count(); index++)
	{
		total_consumption_factor +=  (sint64)desc->get_supplier(index)->get_consumption();
	}

	sint64 total_remaining = prod * total_consumption_factor;

	while (true)
	{
		// Each round we try to allocate demand evenly among the leftover supply
		sint64 remaining_consumption_factor = 0;
		for (uint32 index = 0; index < input.get_count(); index++)
		{
			if (input[index].menge > 0)
			{
				remaining_consumption_factor +=  (sint64)desc->get_supplier(index)->get_consumption();
			}
		}

		if (remaining_consumption_factor == 0)
		{
		// Nothing left to consume
			break;
		}

		const sint64 prod_this_round = total_remaining / remaining_consumption_factor;

		if (prod_this_round <= 0)
		{
		// No (or very small) remaining demand
			break;
		}

		for (uint32 index = 0; index < input.get_count(); index++)
		{
			sint64 prod_this_input = input[index].menge < prod_this_round ? input[index].menge : prod_this_round;

			if (prod_this_input <= 0)
			{
				continue;
			}

			input[index].menge -= prod_this_input;
			input[index].book_stat(prod_this_input * (sint64)desc->get_supplier(index)->get_consumption(), FAB_GOODS_CONSUMED);
			delta_amount += prod_this_input;

			// Update the demand left to allocate
			total_remaining -= prod_this_input * (sint64)desc->get_supplier(index)->get_consumption();
		}
	}
}

bool fabrik_t::out_of_stock_selective()
{
	if (welt->get_settings().get_factory_enforce_demand())
	{
		if ( !(city == NULL && welt->get_settings().get_rural_industries_no_staff_shortage()) )
		{
			const sint32 staffing_percentage = building->get_staffing_level_percentage();
			if (staffing_percentage < welt->get_settings().get_minimum_staffing_percentage_consumer_industry()) {
				// If the shop has insufficient staff, it cannot open and
				// therefore nobody can buy anything.
				return true;
			}
		}
	}

	if (input.get_count() == 1)
	{
		// Very simple if only one thing is sold: save CPU time
		return input[0].menge <= 0;
	}

	// Passengers want a particular good. If the good is unavailable, they
	// won't come.

	sint32 weight_of_out_of_stock_items = 0;
	sint32 weight_of_all_items = 0;
	bool all_out_of_stock = true;

	for(ware_production_t const& i : input)
	{
		if (i.menge <= 0)
		{
			weight_of_out_of_stock_items += i.max;
		}
		else
		{
			all_out_of_stock = false;
		}
		weight_of_all_items += i.max;
	}

	if (all_out_of_stock)
	{
		return true;
	}

	const uint32 random = simrand(weight_of_all_items, "bool fabrik_t::out_of_stock_selective()");
	return random < weight_of_out_of_stock_items;
}


sint32 fabrik_t::goods_needed(const goods_desc_t *typ) const
{
	for (uint32 in = 0; in < input.get_count(); in++)
	{
		const ware_production_t& ware = input[in];
		if (ware.get_typ() == typ)
		{
			// not needed (< 1) if overflowing or too much already sent

			const sint32 prod_factor = desc->get_supplier(in)->get_consumption();
			const sint32 transit_internal_units = (((ware.get_in_transit() << fabrik_t::precision_bits) << DEFAULT_PRODUCTION_FACTOR_BITS) + prod_factor - 1) / prod_factor;

			// Version that respects the new industry internal scale and properly deals with the just in time setting being disabled
			if(typ->get_catg() == 0 || !welt->get_settings().get_just_in_time())
			{
				// Just in time 0 (or no just in time) - the simple system
				return ware.max - ware.menge;
			}
			else if(welt->get_settings().get_just_in_time() == 1)
			{
				// Original just in time with industries always demanding enough goods to fill their storage but no more.
				return ware.max_transit - (transit_internal_units + ware.menge - ware.max);
			}
			else if(welt->get_settings().get_just_in_time() == 2 || welt->get_settings().get_just_in_time() == 3)
			{
				// Modified just in time, with industries not filling more of their storage than they are likely to need.
				sint32 adjusted_factory_value = ware.menge - ware.max_transit;
				adjusted_factory_value = max(adjusted_factory_value, 0);
				if (welt->get_settings().get_just_in_time() == 2)
				{
					const sint32 overall_maximum = ware.max + ware.max_transit;
					return min(overall_maximum, ware.max_transit - (transit_internal_units + adjusted_factory_value));
				}
				else if (welt->get_settings().get_just_in_time() == 3)
				{
					// In this state, the size of the consumer's storage is ignored.
					return ware.max_transit - (transit_internal_units + adjusted_factory_value);
				}
			}
			else //just_in_time >= 4
			{
				// With this just in time algorithm, industries put goods in transit in time to fill their storage.
				if (ware.max >= ware.max_transit)
				{
					if (ware.menge < ware.max_transit) // Use max_transit as a warehouse stock level at which more goods need to be ordered.
					{
						// Demand goods enough goods to fill the internal storage.
						return ware.max - transit_internal_units;
					}
					else
					{
						return 0;
					}
				}
				else
				{
					return ware.max_transit - ware.menge - transit_internal_units;
				}
			}
		}
	}
	return -1;  // not needed here
}


bool fabrik_t::is_consumer_active_at(koord consumer_pos ) const
{
	uint32 idx=get_consumers().index_of(consumer_pos);

	if(idx>>5 >= consumers_active_this_month.get_count()){
		return false;
	}

	return 0 < ( ( 1 << (idx & 0x1F ) ) & consumers_active_this_month[idx>>5] );
}

void fabrik_t::set_consumer_active_at(koord consumer_pos){
	uint32 idx=get_consumers().index_of(consumer_pos);
	if(idx>>5 >= consumers_active_this_month.get_count()){
		return;
	}
	consumers_active_this_month[idx>>5] |= 1 << (idx & 0x1F ) ;
}


void fabrik_t::reset_consumer_active(){
	consumers_active_this_month.resize((get_consumers().get_count()>>5) + 1);
	consumers_active_this_month.set_count((get_consumers().get_count()>>5) + 1);
	for(auto& i : consumers_active_this_month){
		i=0;
	}
}

void fabrik_t::init_contracts(){
	for(uint32 i = 0; i < get_output().get_count(); i++){
		output[i].set_using_contracts();
		output[i].reset_total_contracts();
		const uint32 prod_factor = desc->get_product(i)->get_factor();
		output[i].menge = (sint32)(((sint64)output[i].menge * (sint64)(prod_factor)) >> (sint64)(DEFAULT_PRODUCTION_FACTOR_BITS));
	}
	for(uint32 i = 0; i < get_input().get_count(); i++){
		input[i].reset_total_contracts();
		const uint32 prod_factor = desc->get_supplier(i)->get_consumption();
		input[i].menge = (sint32)(((sint64)input[i].menge * (sint64)(prod_factor)) >> (sint64)(DEFAULT_PRODUCTION_FACTOR_BITS));
	}
	recalc_nearby_halts();
	recalc_storage_capacities();
}

void fabrik_t::remove_contracts(){
	for(uint32 i = 0; i < get_output().get_count(); i++){
		output[i].reset_using_contracts();
		const uint32 prod_factor = desc->get_product(i)->get_factor();
		output[i].menge = (sint32)((((sint64)output[i].menge << (sint64)DEFAULT_PRODUCTION_FACTOR_BITS) + prod_factor-1) / prod_factor);
	}

	for(uint32 i = 0; i < get_input().get_count(); i++){
		const uint32 prod_factor = desc->get_supplier(i)->get_consumption();
		input[i].menge = (sint32)((((sint64)input[i].menge << (sint64)DEFAULT_PRODUCTION_FACTOR_BITS) + prod_factor-1) / prod_factor);
	}
	recalc_nearby_halts();
	recalc_storage_capacities();
	calc_max_intransit_percentages();
}

void fabrik_t::step(uint32 delta_t)
{
	if(!has_calculated_intransit_percentages)
	{
		// Can only do it here (once after loading) as paths
		// are not available when loading, even in finish_rd
		calc_max_intransit_percentages();
	}

	if(  delta_t==0  ) {
		return;
	}

	if(welt->get_settings().using_fab_contracts()){
		step_contracts(delta_t);
		return;
	}

	// produce nothing/consumes nothing ...
	if(  input.empty()  &&  output.empty()  ) {
		// power station? => produce power
		if(  desc->is_electricity_producer()  ) {
			currently_producing = true;
			power = (uint32)( ((sint64)scaled_electric_demand * (sint64)(DEFAULT_PRODUCTION_FACTOR + prodfactor_pax + prodfactor_mail)) >> DEFAULT_PRODUCTION_FACTOR_BITS );
		}

		// produced => trigger smoke
		delta_amount = 1 << fabrik_t::precision_bits;
	}
	else {
		// not a producer => then consume electricity ...
		if(  !desc->is_electricity_producer()  &&  scaled_electric_demand>0  ) {
			// TODO: Consider linking this to actual production only

			prodfactor_electric = (sint32)( ( (sint64)(desc->get_electric_boost()) * (sint64)power + (sint64)(scaled_electric_demand >> 1) ) / (sint64)scaled_electric_demand );

		}

		const sint32 boost = get_prodfactor();
		const bool staff_shortage = is_staff_shortage();

		// calculate the production per delta_t; scaled to PRODUCTION_DELTA_T
		// Calculate actual production. A remainder is used for extra precision.
		const uint64 want_prod_long = welt->scale_for_distance_only(((uint64)prodbase * (uint64)boost * (uint64)delta_t)) + (uint64)delta_amount_remainder;
		const uint32 prod = (uint32)(want_prod_long >> (PRODUCTION_DELTA_T_BITS + DEFAULT_PRODUCTION_FACTOR_BITS + DEFAULT_PRODUCTION_FACTOR_BITS - fabrik_t::precision_bits));
		delta_amount_remainder = (uint32)(want_prod_long & ((1 << (PRODUCTION_DELTA_T_BITS + DEFAULT_PRODUCTION_FACTOR_BITS + DEFAULT_PRODUCTION_FACTOR_BITS - fabrik_t::precision_bits)) - 1));

		// needed for electricity
		currently_producing = false;
		power_demand = 0;

		if(output.empty() && (desc->is_electricity_producer() || desc->get_building()->get_population_and_visitor_demand_capacity() == 0))
		{
			// This formerly provided for all consumption for end consumers.
			// However, consumption for end consumers that have a visitor demand
			// and are now power stations is handled by arriving passengers

			if (desc->is_electricity_producer()) {
				// power station => start with no production
				power = 0;
			}

			// Consume stock
			for (uint32 index = 0; index < input.get_count(); index++)
			{
				uint32 v = prod;

				if (staff_shortage)
				{
					// Do not reduce production unless the staff numbers
					// are below the shortage threshold.
					v *= building->get_staffing_level_percentage();
					v /= 100;
				}

				if ((uint32)input[index].menge > v + 1)
				{
					input[index].menge -= v;
					input[index].book_stat((sint64)v * (sint64)desc->get_supplier(index)->get_consumption(), FAB_GOODS_CONSUMED);
					currently_producing = true;

					if (desc->is_electricity_producer())
					{
						// power station => produce power
						power += (uint32)(((sint64)scaled_electric_demand * (sint64)(DEFAULT_PRODUCTION_FACTOR + prodfactor_pax + prodfactor_mail)) >> DEFAULT_PRODUCTION_FACTOR_BITS);
					}

					if (staff_shortage)
					{
						// Do not reduce production unless the staff numbers
						// are below the shortage threshold.
						power *= building->get_staffing_level_percentage();
						power /= 100;
					}

					// to find out if storage changed
					delta_amount += v;
				}
				else
				{
					if (desc->is_electricity_producer())
					{
						// power station => produce power
						power += (uint32)((((sint64)scaled_electric_demand * (sint64)(DEFAULT_PRODUCTION_FACTOR + prodfactor_pax + prodfactor_mail)) >> DEFAULT_PRODUCTION_FACTOR_BITS) * input[index].menge / (v + 1));
					}

					if (staff_shortage)
					{
						// Do not reduce production unless the staff numbers
						// are below the shortage threshold.
						power *= building->get_staffing_level_percentage();
						power /= 100;
					}

					delta_amount += input[index].menge;

					input[index].book_stat((sint64)input[index].menge * (desc->get_supplier(index) ? (sint64)desc->get_supplier(index)->get_consumption() : 1), FAB_GOODS_CONSUMED);
					input[index].menge = 0;
				}
			}
		}
		else if (output.empty())
		{
			// Calculate here whether the end consumer is operative.
			for (uint32 index = 0; index < input.get_count(); index++)
			{
				if (input[index].menge > (sint32)prod + 1)
				{
					currently_producing = true;
					break;
				}
			}
		}
		else
		{
			// ok, calulate maximum allowed consumption.
			sint32 min_menge = input.empty() ? 0x7FFFFFFF : input[0].menge;
			sint32 consumed_menge = 0;
			for (uint32 index = 1; index < input.get_count(); index++) {
				if (input[index].menge < min_menge) {
					min_menge = input[index].menge;
				}
			}

			bool some_production = false;
			// produces something
			for (uint32 product = 0; product < output.get_count(); product++)
			{
				// calculate production
				// sint32 p_menge = (sint32)scale_output_production(product, prod);
				sint32 p_menge = prod;

				if (staff_shortage)
				{
					// Do not reduce production unless the staff numbers
					// are below the shortage threshold.
					p_menge *= building->get_staffing_level_percentage();
					p_menge /= 100;
				}

				const sint32 menge_out = p_menge < min_menge ? p_menge : min_menge;  // production smaller than possible due to consumption
				if (menge_out > consumed_menge) {
					consumed_menge = menge_out;
				}

				if (menge_out > 0) {
					const sint32 p = menge_out;

					// produce
					if (output[product].menge < output[product].max) {
						// to find out, if storage changed
						delta_amount += p;
						output[product].menge += p;
						output[product].book_stat((sint64)p * (sint64)desc->get_product(product)->get_factor(), FAB_GOODS_PRODUCED);
						// if less than 3/4 filled we neary always consume power
						currently_producing |= (output[product].menge * 4 < output[product].max * 3);
						some_production = true;
					}
					else {
						output[product].book_stat((sint64)(output[product].max - output[product].menge) * (sint64)desc->get_product(product)->get_factor(), FAB_GOODS_PRODUCED);
						output[product].menge = output[product].max;
					}
				}
			}

			// and finally consume stock
			if (some_production)
			{
				for (uint32 index = 0; index < input.get_count(); index++) {
					const uint32 v = consumed_menge;

					if ((uint32)input[index].menge > v + 1) {
						input[index].menge -= v;
						input[index].book_stat((sint64)v * (sint64)desc->get_supplier(index)->get_consumption(), FAB_GOODS_CONSUMED);
					}
					else {
						input[index].book_stat((sint64)input[index].menge * (sint64)desc->get_supplier(index)->get_consumption(), FAB_GOODS_CONSUMED);
						input[index].menge = 0;
					}
				}
			}
		}

		if(  currently_producing || desc->get_product_count() == 0  ) {
			// Pure consumers (i.e., those that do not produce anything) should require full power at all times
			// requires full power even if runs out of raw material next cycle
			power_demand = scaled_electric_demand;
		}
	}

	// increment weighted sums for average statistics
	book_weighted_sums();

	// not a power station => then consume all electricity ...
	if(  !desc->is_electricity_producer()  ) {
		power = 0;
	}

	delta_t_sum += delta_t;
	if(  delta_t_sum > PRODUCTION_DELTA_T  ) {
		delta_t_sum = delta_t_sum % PRODUCTION_DELTA_T;

		// distribute, if there is more than 1 waiting ...
		// Changed from the original 10 by jamespetts, July 2017
		for(  uint32 product = 0;  product < output.get_count();  product++  )
		{
			const sint32 units = (sint32)(((sint64)output[product].menge * (sint64)(get_prodfactor())) >> ((sint64)DEFAULT_PRODUCTION_FACTOR_BITS + (sint64)precision_bits));
			//if(  output[product].menge > (1 << precision_bits)  ||  output[product].menge*2 > output[product].max  )
			if(units)
			{
				verteile_waren(product);
				INT_CHECK("simfab 636");
			}
		}

		recalc_factory_status();

		rescale_delta();
	}

	advance_slot(delta_t);
}


void fabrik_t::step_contracts(uint32 delta_t){
	// produce nothing/consumes nothing ...
	if(  input.empty()  &&  output.empty()  ) {
		// power station? => produce power
		if(  desc->is_electricity_producer()  ) {
			currently_producing = true;
			power = (uint32)( ((sint64)scaled_electric_demand * (sint64)(DEFAULT_PRODUCTION_FACTOR + prodfactor_pax + prodfactor_mail)) >> DEFAULT_PRODUCTION_FACTOR_BITS );
		}

		// produced => trigger smoke
		delta_amount = 1 << fabrik_t::precision_bits;
	}else{
		// not a producer => then consume electricity ...
		if(  !desc->is_electricity_producer()  &&  scaled_electric_demand>0  ) {
			// TODO: Consider linking this to actual production only
			prodfactor_electric = (sint32)( ( (sint64)(desc->get_electric_boost()) * (sint64)power + (sint64)(scaled_electric_demand >> 1) ) / (sint64)scaled_electric_demand );

		}

		uint64 monthly_production=get_monthly_production(); //kilos / month
		if(is_staff_shortage()){
			monthly_production = monthly_production * building->get_staffing_level_percentage() / 100;
		}
		const uint64 kilo_per_quarter=DEFAULT_PRODUCTION_FACTOR;
		uint32 step_production_max=(monthly_production * (uint64)delta_t + delta_amount_remainder) / (welt->ticks_per_world_month * kilo_per_quarter); //quarters this step;
		delta_amount_remainder=(monthly_production * (uint64)delta_t + delta_amount_remainder) % (welt->ticks_per_world_month * kilo_per_quarter);

		power_demand = 0;

		//consume and/or produce stock based on industry type
		if(output.empty() && (desc->is_electricity_producer() || desc->get_building()->get_population_and_visitor_demand_capacity() == 0)){
			//power plant or utility

			if(step_production_max){
				if (desc->is_electricity_producer()) {
					// power station => start with no production
					power = 0;
					currently_producing = false;
				}

				//consume each good type
				for(uint32 i = 0; i < input.get_count(); i++){
					uint32 this_consumption;
					uint32 step_production;
					uint32 pfactor=desc->get_supplier(i)->get_consumption();
					if((uint32)input[i].menge >= step_production_max * pfactor){
						step_production=step_production_max;
						currently_producing=true;
					}else{
						step_production=input[i].menge / pfactor;
						if(step_production){
							currently_producing=true;
						}
					}
					this_consumption=step_production * pfactor;

					input[i].menge-=this_consumption;
					input[i].book_stat(this_consumption << DEFAULT_PRODUCTION_FACTOR_BITS,FAB_GOODS_CONSUMED);
					delta_amount+=step_production;
				}
			}

			//produce power
			if (currently_producing && desc->is_electricity_producer())
			{
				power = (uint32)( ((sint64)scaled_electric_demand * (sint64)(DEFAULT_PRODUCTION_FACTOR + prodfactor_pax + prodfactor_mail)) >> DEFAULT_PRODUCTION_FACTOR_BITS );
			}
		}else if(output.empty()){
			//consumer only, consumption in received goods code
			//check if operative though
			currently_producing = false;
			for(uint32 i = 0; i < input.get_count(); i++){
				if((uint32)input[i].menge >= step_production_max * desc->get_supplier(i)->get_consumption()){
					currently_producing=true;
					break;
				}
			}
		}else{
			//producer or manufacturer
			currently_producing = false;

			//reduce max_production_step to corespond with input
			for(uint32 i = 0; i < input.get_count(); i++){
				const uint32 pfactor=desc->get_supplier(i)->get_consumption();
				const uint32 step_production=input[i].menge / pfactor;
				if(step_production < step_production_max){
					step_production_max=step_production;
				}
			}

			uint32 step_consumption=0;
			//produce good(s)
			for(uint32 i = 0; i < output.get_count(); i++){
				if(output[i].menge >= output[i].max){
					continue;
				}
				const uint32 pfactor=desc->get_product(i)->get_factor();
				uint32 step_production=(output[i].max - output[i].menge) / pfactor;
				if(step_production > step_production_max){
					step_production = step_production_max;
				}
				if(step_production>step_consumption){
					step_consumption=step_production;
				}
				delta_amount+=step_production;
				uint32 this_production=step_production * pfactor;
				output[i].book_stat(this_production << DEFAULT_PRODUCTION_FACTOR_BITS, FAB_GOODS_PRODUCED);
				output[i].menge+=this_production;
				// if less than 3/4 filled we neary always consume power
				currently_producing |= (output[i].menge * 4 < output[i].max * 3);
			}
			if(step_consumption){
				for(uint32 i = 0; i < input.get_count(); i++){
					const uint32 pfactor=desc->get_supplier(i)->get_consumption();
					uint32 this_consumption=step_consumption * pfactor;
					input[i].menge-=this_consumption;
					input[i].book_stat(this_consumption << DEFAULT_PRODUCTION_FACTOR_BITS,FAB_GOODS_CONSUMED);
				}
			}
		}
		if( ( currently_producing || desc->get_product_count() == 0 ) && !desc->is_electricity_producer() ) {
			// Pure consumers (i.e., those that do not produce anything) should require full power at all times
			// requires full power even if runs out of raw material next cycle
			power_demand = scaled_electric_demand;
		}
	}

	book_weighted_sums();

	// not a power station => then consume all electricity ...
	if(  !desc->is_electricity_producer()  ) {
		power = 0;
	}

	distribute_contracts(delta_t);

	delta_t_sum += delta_t;
	if(delta_t_sum > PRODUCTION_DELTA_T){
		delta_t_sum %= PRODUCTION_DELTA_T;
		recalc_factory_status();
		rescale_delta();
	}

	advance_slot(delta_t);
}

void fabrik_t::rescale_delta(){
	// rescale delta_amount here: all products should be produced at least once
	// (if consumer only: all supplements should be consumed once)
	const uint32 min_change = output.empty() ? input.get_count() : output.get_count();

	if(  (delta_amount>>fabrik_t::precision_bits)>min_change  ) {

		// we produced some real quantity => smoke
		smoke();

		// chance to expand every 256 rounds of activities, after which activity count will return to 0 (overflow behaviour)
		if(  (++activity_count)==0  ) {
			if(  desc->get_field_group()  ) {
				if(  fields.get_count()<desc->get_field_group()->get_max_fields()  ) {
					// spawn new field with given probability
					add_random_field(desc->get_field_group()->get_probability());
					adjust_production_for_fields();
				}
			}
			else {
				if(  times_expanded<desc->get_expand_times()  ) {
					if(  simrand(10000, "fabrik_t::step (expand 1)")<desc->get_expand_probability()  ) {
						set_base_production( prodbase + desc->get_expand_minumum() + simrand( desc->get_expand_range(), "fabrik_t::step (expand 2)" ) );
						++times_expanded;
					}
				}
			}
		}

		INT_CHECK("simfab 558");
		// reset for next cycle
		delta_amount = 0;
	}
}

void fabrik_t::advance_slot(uint32 delta_t){
	/// advance arrival slot at calculated interval and recalculate boost where necessary
	delta_slot += delta_t;
	const sint32 periods = welt->get_settings().get_factory_arrival_periods();
	const sint32 slot_interval = (1 << (PERIOD_BITS - SLOT_BITS)) * periods;
	while(  delta_slot>slot_interval  ) {
		delta_slot -= slot_interval;
		const sint32 pax_result = arrival_stats_pax.advance_slot();
		if(  pax_result&ARRIVALS_CHANGED  ||  (periods>1  &&  pax_result&ACTIVE_SLOTS_INCREASED  &&  arrival_stats_pax.get_active_slots()*periods>SLOT_COUNT  )  ) {
			update_prodfactor_pax();
		}
		const sint32 mail_result = arrival_stats_mail.advance_slot();
		if(  mail_result&ARRIVALS_CHANGED  ||  (periods>1  &&  mail_result&ACTIVE_SLOTS_INCREASED  &&  arrival_stats_mail.get_active_slots()*periods>SLOT_COUNT  )  ) {
			update_prodfactor_mail();
		}
	}
}

void fabrik_t::distribute_contracts(uint32 delta_t){
	uint64 current_ticks=welt->get_ticks();
	uint64 last_ticks=welt->get_ticks()-delta_t;

	for(uint32 j = 0; j < output.get_count(); j++){
		for(uint32 i = 0; i < output[j].link_count(); i++){
			uint64 monthly_contract=output[j].get_contract(i);
			uint64 current_tonnes=current_ticks * monthly_contract / (welt->ticks_per_world_month << fabrik_t::precision_bits);
			current_tonnes-=last_ticks * monthly_contract / (welt->ticks_per_world_month << fabrik_t::precision_bits);
			if(current_tonnes && output[j].menge >= (sint32)(current_tonnes << fabrik_t::precision_bits)){
				koord consumer_pos=output[j].link_from_index(i);
				if (shortest_distance(consumer_pos, pos.get_2d()) <= welt->get_settings().get_station_coverage_factories()
						&& get_fab(consumer_pos)
						&& get_desc()->get_placement() != factory_desc_t::Water)
				{
					//walk toods to destination
					fabrik_t* consumer=get_fab(consumer_pos);
					if(ware_production_t* input_ware=consumer->get_input(output[j].get_typ())){
						if(input_ware->menge > input_ware->max){
							//destination full
							continue;
						}
						ware_t ware(output[j].get_typ());
						ware.menge=current_tonnes;
						ware.set_zielpos(consumer_pos);

						world()->add_to_waiting_list(ware, get_pos().get_2d());
						fabrik_t::update_transit( ware, true );
						// add as active destination
						set_consumer_active_at(ware.get_zielpos());

						output[j].menge-=ware.menge << fabrik_t::precision_bits;
						output[j].book_stat(ware.menge, FAB_GOODS_DELIVERED);
					}
				}else{
					//send to station

					//find station with most free space that has path to destination
					sint32 max_freespace_ratio=-1;
					ware_t best_ware;
					halthandle_t best_halt;
					for(auto nearby_halt : nearby_freight_halts){
						ware_t ware(output[j].get_typ(),nearby_halt.halt);
						ware.menge=current_tonnes;
						ware.set_zielpos(output[j].link_from_index(i));

						if(nearby_halt.halt->find_route(ware) != UINT32_MAX_VALUE){
							const sint32 halt_capacity = nearby_halt.halt->get_capacity(2);
							const sint32 halt_left = halt_capacity - (sint32)nearby_halt.halt->get_ware_summe(ware.get_desc());
							sint32 halt_freespace_ratio = halt_capacity ? (halt_left << fabrik_t::precision_bits) / halt_capacity : 0;
							if(halt_freespace_ratio > max_freespace_ratio){
								max_freespace_ratio=halt_freespace_ratio;
								best_ware=ware;
								best_halt=nearby_halt.halt;
							}
						}
					}

					//if halt found with free space, send the goods
					if(max_freespace_ratio>=0){
						best_halt->starte_mit_route(best_ware, get_pos().get_2d());
						best_halt->recalc_status();
						fabrik_t::update_transit( best_ware, true );
						set_consumer_active_at(best_ware.get_zielpos());
						output[j].menge-=best_ware.menge << fabrik_t::precision_bits;
						output[j].book_stat(best_ware.menge, FAB_GOODS_DELIVERED);
					}
				}
			}
		}
	}
}

class distribute_ware_t
{
public:
	ware_t ware;				/// goods to be routed to consumer
	nearby_halt_t nearby_halt;  /// potential start halt
	sint32 space_left;			/// free space at halt
	sint32 amount_waiting;		/// waiting goods at halt for same destination as ware
private:
	sint32 ratio_free_space;	/// ratio of free space at halt (=0 for overflowing station)

public:
	distribute_ware_t( nearby_halt_t n, sint32 l, sint32 t, sint32 a, ware_t w )
	{
		nearby_halt = n;
		space_left = l;
		amount_waiting = a;
		ware = w;
		// Ensure that over-full stations compare equally allowing tie breaker clause (amount waiting)
		sint32 space_total = t > 0 ? t : 1;
		ratio_free_space = space_left > 0 ? ((sint64)space_left << fabrik_t::precision_bits) / space_total : 0;
	}
	distribute_ware_t() {}

	static bool compare(const distribute_ware_t &dw1, const distribute_ware_t &dw2)
	{
		return  (dw1.ratio_free_space > dw2.ratio_free_space)
				||  (dw1.ratio_free_space == dw2.ratio_free_space  &&  dw1.amount_waiting <= dw2.amount_waiting);
	}
};


/**
 * distribute stuff to all best destination
 */
void fabrik_t::verteile_waren(const uint32 product)
{
	// Check consumers
	if(  !get_output()[product].link_count()  )
	{
		return;
	}

	static vector_tpl<distribute_ware_t> dist_list(16);
	dist_list.clear();

	// to distribute to all target equally, we use this counter, for the source hald, and target factory, to try first
	output[product].index_offset++;

	/* distribute goods to factory
	 * that has not an overflowing input storage
	 * also prevent stops from overflowing, if possible
	 * Since we can called with menge>max/2 are at least 1 are there, we must first limit the amount we distribute
	 */
	// We already know the distribution amount. However it has to be converted from factory units into real units.
	const uint32 prod_factor = desc->get_product(product)->get_factor();
	sint32 menge = (sint32)(((sint64)output[product].menge * (sint64)(prod_factor)) >> (sint64)(DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits));

	// Check to see whether any consumers are within carting distance: there is no point in boarding transport
	// if the consumer industry is next door.

	bool can_cart_to_consumer = false;
	sint32 needed = 0;
	sint32 needed_base_units = 0;

	for (uint32 consumer_num = 0; consumer_num < get_output()[product].link_count(); consumer_num++)
	{
		// Check whether these can be carted to their destination.
		const koord consumer_pos = get_output()[product].link_from_index((consumer_num + output[product].index_offset) % get_output()[product].link_count());
		const uint32 distance_to_consumer = shortest_distance(consumer_pos, pos.get_2d());
		if (distance_to_consumer <= welt->get_settings().get_station_coverage_factories())
		{
			fabrik_t* consumer = get_fab(consumer_pos);

			if (consumer && !(get_desc()->get_placement() == factory_desc_t::Water)) // Goods cannot be carted over water.
			{
				needed = consumer->goods_needed(output[product].get_typ());
				needed_base_units = (sint32)(((sint64)needed * (sint64)(prod_factor)) >> (DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits));
				if (needed >= 0)
				{
					can_cart_to_consumer = true;

					ware_t ware(output[product].get_typ());
					ware.menge = menge;
					ware.set_zielpos(consumer_pos);

					uint32 input_num;
					// find the index in the target factory
					for (input_num = 0; input_num < consumer->get_input().get_count() && consumer->get_input()[input_num].get_typ() != ware.get_desc(); input_num++)
					{
						// empty
					}

					const ware_production_t &this_input = consumer->get_input()[input_num];
					const bool needs_max_amount = needed >= this_input.max;

					if (needs_max_amount && (needed_base_units == 0))
					{
						needed_base_units = 1;
					}

					// if only overflown factories found => deliver to first
					// else deliver to non-overflown factory
					nearby_halt_t nearby_halt;
					nearby_halt.distance = distance_to_consumer;
					nearby_halt.halt = halthandle_t();

					if (!welt->get_settings().get_just_in_time())
					{
						// without production stop when target overflowing, distribute to least overflown target
						const sint32 goods_remaining = this_input.max - this_input.menge;
						dist_list.insert_ordered(distribute_ware_t(nearby_halt, goods_remaining, this_input.max, 0, ware), distribute_ware_t::compare);
					}
					else if (needed > 0)
					{
						// Carted goods need care not about any particular stop, but still care about the consumer's storage/in-transit value
						dist_list.insert_ordered(distribute_ware_t(nearby_halt, needed_base_units, 0, 0, ware), distribute_ware_t::compare);
					}
				}
			}
		}
	}

	const uint32 nearby_halts_count = nearby_freight_halts.get_count();

	if (!can_cart_to_consumer && nearby_halts_count == 0)
	{
		return;
	}

	for(uint32 nearby_halt_num = 0; nearby_halt_num < nearby_halts_count; nearby_halt_num++)
	{
		nearby_halt_t nearby_halt = nearby_freight_halts[(nearby_halt_num + output[product].index_offset) % nearby_halts_count];

		// Iterate over all targets
		for(uint32 consumer_num=0; consumer_num < get_output()[product].link_count(); consumer_num++  ) {
			// this way, the halt, that is tried first, will change. As a result, if all destinations are empty, it will be spread evenly
			const koord consumer_pos = get_output()[product].link_from_index((consumer_num + output[product].index_offset) % get_output()[product].link_count());
			fabrik_t * this_consumer = get_fab(consumer_pos);

			if(this_consumer)
			{
				needed = this_consumer->goods_needed(output[product].get_typ());
				needed_base_units = (sint32)(((sint64)needed * (sint64)(prod_factor)) >> (DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits));
				if(needed >= 0)
				{
					ware_t ware(output[product].get_typ(), nearby_halt.halt);
					ware.menge = menge;
					ware.set_zielpos(consumer_pos);
					ware.arrival_time = welt->get_ticks();

					uint32 w;
					// find the index in the target factory
					for(w = 0; w < this_consumer->get_input().get_count() && this_consumer->get_input()[w].get_typ() != ware.get_desc(); w++)
					{
						// empty
					}

					const bool needs_max_amount = needed >= this_consumer->get_input()[w].max;
					const sint32 storage_base_units = (sint32)(((sint64)this_consumer->get_input()[w].menge * (sint64)(prod_factor)) >> (DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits));

					if (needed > 0 && this_consumer->get_input()[w].get_in_transit() == 0 && (needs_max_amount || storage_base_units <= 1) && needed_base_units == 0)
					{
						needed_base_units = 1;
					}

					// if only overflown factories found => deliver to first
					// else deliver to non-overflown factory
					if(!welt->get_settings().get_just_in_time())
					{
						// without production stop when target overflowing, distribute to least overflow target
						const sint32 fab_left = this_consumer->get_input()[w].max - this_consumer->get_input()[w].menge;
						dist_list.insert_ordered(distribute_ware_t(nearby_halt, fab_left, this_consumer->get_input()[w].max, (sint32)nearby_halt.halt->get_ware_fuer_zielpos(output[product].get_typ(), ware.get_zielpos()), ware), distribute_ware_t::compare);
					}
					else if(needed > 0)
					{
						// we are not overflowing: Station can only store up to a maximum amount of goods
						const sint32 halt_left = (sint32)nearby_halt.halt->get_capacity(2) - (sint32)nearby_halt.halt->get_ware_summe(ware.get_desc());
						dist_list.insert_ordered(distribute_ware_t(nearby_halt, min(halt_left, needed_base_units), nearby_halt.halt->get_capacity(2), (sint32)nearby_halt.halt->get_ware_fuer_zielpos(output[product].get_typ(),ware.get_zielpos()), ware), distribute_ware_t::compare);
					}
				}
			}
		}
	}

	// Auswertung der Ergebnisse
	// "Evaluation of the results" (Babelfish)
	if(  !dist_list.empty()  ) {
		distribute_ware_t *best = NULL;
		// Assume a fixed 1km/h transshipment time of goods to industries. This gives a minimum transfer time
		// of 15 minutes for each stop at 125m/tile.
		const uint32 transfer_journey_time_factor = ((uint32)welt->get_settings().get_meters_per_tile() * 6u) * 10u;
		uint32 current_journey_time;
		for(distribute_ware_t & i : dist_list) {
			if (!i.nearby_halt.halt.is_bound())
			{
				best = &i;
				break;
			}

			// now search route
			const uint32 transfer_time = (i.nearby_halt.distance * transfer_journey_time_factor) / 100;
			current_journey_time = i.nearby_halt.halt->find_route(i.ware);
			if(current_journey_time < UINT32_MAX_VALUE)
			{
				current_journey_time += transfer_time;
				best = &i;
				break;
			}
		}

		if(  best == NULL  ) {
			return; // no route for any destination
		}

		halthandle_t &best_halt = best->nearby_halt.halt;
		ware_t       &best_ware = best->ware;

		// now process found route
		const sint32 space_left = best_halt.is_bound() ? welt->get_settings().get_just_in_time() ? best->space_left : (sint32)best_halt->get_capacity(2) - (sint32)best_halt->get_ware_summe(best_ware.get_desc()) : needed_base_units;
		menge = min(menge, space_left);
		// ensure amount is not negative ...
		if(  menge<0  ) {
			menge = 0;
		}
		// since it is assigned here to an unsigned variable!
		best_ware.menge = menge;

		if(  space_left<0 && best_halt.is_bound()  ) {
			// find, what is most waiting here from us
			ware_t most_waiting(output[product].get_typ());
			most_waiting.menge = 0;
			for(auto const & consumer_pos : consumers) {
				uint32 const amount = best_halt->get_ware_fuer_zielpos(output[product].get_typ(), consumer_pos);
				if(  amount > most_waiting.menge  ) {
					most_waiting.set_zielpos(consumer_pos);
					most_waiting.menge = amount;
					most_waiting.arrival_time = welt->get_ticks();
				}
			}

			//  we will reroute some goods
			if(  best->amount_waiting==0  &&  most_waiting.menge>0  ) {
				// remove something from the most waiting goods
				if(  best_halt->recall_ware( most_waiting, min((sint32)(most_waiting.menge/2), 1 - space_left) )  ) {
					best_ware.menge += most_waiting.menge;
				}
				else {
					// overcrowded with other stuff (not from us)
					return;
				}
			}
			else {
				return;
			}
		}

		// Since menge might have been mutated, it must be converted back. This might introduce some error with some prod factors which is always rounded up.
		const sint32 prod_delta = ((((sint64)menge << (DEFAULT_PRODUCTION_FACTOR_BITS + precision_bits)) + (sint64)(prod_factor - 1)) / (sint64)prod_factor);

		output[product].menge -= prod_delta;

		if (best_halt.is_bound())
		{
			best_halt->starte_mit_route(best_ware, get_pos().get_2d());
			best_halt->recalc_status();
		}
		else
		{
			world()->add_to_waiting_list(best_ware, get_pos().get_2d());
		}
		fabrik_t::update_transit( best_ware, true );
		// add as active destination
		set_consumer_active_at(best_ware.get_zielpos());
		output[product].book_stat(best_ware.menge, FAB_GOODS_DELIVERED);
	}
}

stadt_t* fabrik_t::check_local_city()
{
	stadt_t* c = NULL;
	// We cannot use the tile list here since this is called in many cases before that list can be generated.
	koord pos_2d = pos.get_2d();
	koord size = desc->get_building()->get_size(this->get_rotate());
	koord test;
	koord k;
	// Which tiles belong to the fab?
	for (test.x = 0; test.x < size.x; test.x++)
	{
		for (test.y = 0; test.y < size.y; test.y++)
		{
			k = pos_2d + test;
			for (uint8 i = 0; i < 8; i++)
			{
				// We need to check neighbouring tiles, since city borders can be very tightly drawn.
				const koord city_pos(k + k.neighbours[i]);
				c = welt->get_city(city_pos);
				if (c)
				{
					goto out_of_loop;
				}
			}
		}
	}
out_of_loop:

	return c;
}

void fabrik_t::new_month()
{
	// update statistics for input and output goods
	for (uint32 in = 0; in < input.get_count(); in++) {
		input[in].roll_stats(welt->get_settings().using_fab_contracts() ? DEFAULT_PRODUCTION_FACTOR : desc->get_supplier(in)->get_consumption());
	}
	for (uint32 out = 0; out < output.get_count(); out++) {
		output[out].roll_stats(welt->get_settings().using_fab_contracts() ? DEFAULT_PRODUCTION_FACTOR : desc->get_product(out)->get_factor());
	}
	reset_consumer_active();

	// update statistics
	for(  int s=0;  s<MAX_FAB_STAT;  ++s  ) {
		for(  int m=MAX_MONTH-1;  m>0;  --m  ) {
			statistics[m][s] = statistics[m-1][s];
		}
		statistics[0][s] = 0;
	}

	// restore the current values
	set_stat(calc_operation_rate(0), FAB_PRODUCTION);
	set_stat( prodfactor_electric, FAB_BOOST_ELECTRIC );
	set_stat( prodfactor_pax, FAB_BOOST_PAX );
	set_stat( prodfactor_mail, FAB_BOOST_MAIL );
	set_stat( power, FAB_POWER );

	if(welt->get_settings().using_fab_contracts()){
		// Check whether this industry has been idle too long
		// TODO: This may need substantial reworking when the new town growth/industry code ends up being added.
		if(get_stat(1,FAB_PRODUCTION)==0){
			months_unproductive++;
		}else{
			months_unproductive=0;
		}
		if(output.get_count()){
			uint32 missing_contracts=0;
			bool reset_missing_contracts=false;
			//check if not outputing anything
			for(uint32 i = 0; i < output.get_count(); i++){
				if(output[i].get_total_contracts()==0){
					missing_contracts++;
				}
			}
			if(missing_contracts!=output.get_count()){
				reset_missing_contracts=true;
			}
			//or are missing a good type
			for(uint32 i = 0; i < input.get_count(); i++){
				if(input[i].get_total_contracts()==0){
					reset_missing_contracts=false;
					missing_contracts++;
				}
			}
			if(reset_missing_contracts){
				months_missing_contracts=0;
			}else{
				months_missing_contracts+=missing_contracts;
			}
		}else{
			months_missing_contracts=0;
		}

		//should close factories that were missing contracts for more than 12 months or unproductive for 60 months
		if(months_unproductive>12*5 || months_missing_contracts>12){
			welt->should_close_factories_this_month.append(this,months_unproductive+months_missing_contracts);
		}
	}

	// This needs to be re-checked regularly, as cities grow, occasionally shrink and can be deleted.
	stadt_t* c = check_local_city();

	if(c && !c->get_city_factories().is_contained(this))
	{
		c->add_city_factory(this);
	}

	if(c != city && city)
	{
		city->remove_city_factory(this);
	}

	if(!c)
	{
		// Factory no longer in city.
		transformer_connected = NULL;
	}

	city = c;

	mark_connected_roads(false);

	calc_max_intransit_percentages();

	// Check whether we have any missing links
	bool must_close = false;
	if (status == missing_connections || status == material_not_available)
	{
		must_close = disconnect_supplier(koord::invalid); // This does not remove anything, but checks for missing suppliers
	}
	if (status == missing_connections || status == missing_consumer)
	{
		must_close |= disconnect_consumer(koord::invalid); // This does not remove anything, but checks for missing consumers
	}

	// Check to see whether factory is obsolete.
	// If it is, give it a distribution_weight of being closed down.
	// @author: jamespetts

	const uint16 timeline_month = welt->get_timeline_year_month(); // This will be 0 if timeline is disabled.
	const uint16 retire_month = desc->get_building()->get_retire_year_month();
	const uint32 latest_retire_month = retire_month + (12 * welt->get_settings().get_factory_max_years_obsolete());
	if(must_close || (timeline_month > retire_month && (latest_retire_month <= timeline_month || simrand(latest_retire_month - timeline_month, "void fabrik_t::new_month()") == 0)))
	{
		char buf[256];

		const sint32 upgrades_count = desc->get_upgrades_count();
		if(upgrades_count > 0)
		{
			// This factory has some upgrades; consider upgrading.
			minivec_tpl<const factory_desc_t*> upgrade_list(upgrades_count);
			const uint32 max_density = welt->get_target_industry_density() == 0 ? SINT32_MAX_VALUE : (welt->get_target_industry_density() * 150u) / 100u;
			const uint32 adjusted_density = welt->get_actual_industry_density() - (100u / desc->get_distribution_weight());
			for(sint32 i = 0; i < upgrades_count; i ++)
			{
				// Check whether any upgrades are suitable.
				// Currently, they must be of identical size, as the
				// upgrade mechanism is quite simple. In future, it might
				// be possible to write more sophisticated upgrading code
				// to enable industries that are not identical in such a
				// way to be upgraded. (Previously, the industry also
				// had to have the same number of suppliers and consumers,
				// but this is no longer necessary given the industry re-linker).

				// Thus, non-suitable upgrades are allowed to be specified
				// in the .dat files for future compatibility.

				const factory_desc_t* new_fab = desc->get_upgrades(i);
				if(new_fab != NULL && new_fab->is_electricity_producer() == desc->is_electricity_producer() &&
				   new_fab->get_building()->get_x() == desc->get_building()->get_x() &&
				   new_fab->get_building()->get_y() == desc->get_building()->get_y() &&
				   new_fab->get_building()->get_size() == desc->get_building()->get_size() &&
				   new_fab->get_building()->get_intro_year_month() <= timeline_month &&
				   new_fab->get_building()->get_retire_year_month() >= timeline_month &&
					(adjusted_density < (max_density + (100u / new_fab->get_distribution_weight())) || !desc->is_consumer_only()))
				{
					upgrade_list.append_unique(new_fab);
				}
			}

			const uint8 list_count = upgrade_list.get_count();
			if(list_count > 0)
			{
				// Upgrade if this industry is well served, otherwise close.
				uint32 upgrade_chance_percent;

				// Get average and max. operation rate for the last 11 months
				// Note that we have already rolled the statistics, so we cannot count the current (blank) month.
				uint32 total_operation_rate = 0;
				uint32 max_operation_rate = 0;
				for (uint32 i = 1; i < 11; i++)
				{
					max_operation_rate = max(max_operation_rate, statistics[i][FAB_PRODUCTION]);
					total_operation_rate += statistics[i][FAB_PRODUCTION];
				}
				const uint32 average_operation_rate = total_operation_rate / 11u;

				upgrade_chance_percent = max(average_operation_rate, max_operation_rate / 2);

				const uint32 minimum_base_upgrade_chance_percent = 50;

				upgrade_chance_percent += (minimum_base_upgrade_chance_percent * 2);
				upgrade_chance_percent /= 2;

				const uint32 probability = simrand(101, "void fabrik_t::new_month()");

				if (upgrade_chance_percent > probability)
				{
					// Upgrade
					uint32 total_density = 0;
					FOR(minivec_tpl<const factory_desc_t*>, upgrade, upgrade_list)
					{
						total_density += (100u / upgrade->get_distribution_weight());
					}
					const uint32 average_density = total_density / list_count;
					const uint32 probability = 1 / ((100u - ((adjusted_density + average_density) / max_density)) * (uint32)list_count) / 100u;
					const uint32 distribution_weight = simrand(probability, "void fabrik_t::new_month()");

					const int old_distributionweight = desc->get_distribution_weight();
					const factory_desc_t* new_type = upgrade_list[distribution_weight];
					//welt->decrease_actual_industry_density(100 / old_distributionweight);
					if (desc->is_consumer_only()) {
						welt->decrease_actual_industry_density(100 /desc->get_distribution_weight());
					}
					uint32 percentage = new_type->get_field_group() ? (new_type->get_field_group()->get_max_fields() * 100) / desc->get_field_group()->get_max_fields() : 0;
					const uint16 adjusted_number_of_fields = percentage ? (fields.get_count() * percentage) / 100 : 0;
					delete_all_fields();
					const char* old_name = get_name();
					desc = new_type;
					const char* new_name = get_name();
					get_building()->calc_image();
					// Base production is randomised, so is an instance value. Must re-set from the type.
					prodbase = desc->get_productivity() + simrand(desc->get_range(), "void fabrik_t::new_month()");
					// Re-add the fields
					for (uint16 i = 0; i < adjusted_number_of_fields; i++)
					{
						add_random_field(10000u);
					}
					adjust_production_for_fields();
					// Re-set the expansion counter: an upgraded factory may expand further.
					times_expanded = 0;
					// Re-calculate production/consumption
					if (desc->get_placement() == 2 && city && desc->get_product_count() == 0)
					{
						// City consumer industries set their consumption rates by the relative size of the city
						const weighted_vector_tpl<stadt_t*>& cities = welt->get_cities();

						sint64 biggest_city_population = 0;
						sint64 smallest_city_population = -1;

						for (weighted_vector_tpl<stadt_t*>::const_iterator i = cities.begin(), end = cities.end(); i != end; ++i)
						{
							stadt_t* const c = *i;
							const sint64 pop = c->get_finance_history_month(0, HIST_CITIZENS);
							if (pop > biggest_city_population)
							{
								biggest_city_population = pop;
							}
							else if (pop < smallest_city_population || smallest_city_population == -1)
							{
								smallest_city_population = pop;
							}
						}

						const sint64 this_city_population = city->get_finance_history_month(0, HIST_CITIZENS);
						sint32 production;

						if (this_city_population == biggest_city_population)
						{
							production = desc->get_range();
						}
						else if (this_city_population == smallest_city_population)
						{
							production = 0;
						}
						else
						{
							const sint64 percentage = (this_city_population - smallest_city_population) * 100ll / (biggest_city_population - smallest_city_population);
							production = (desc->get_range() * (sint32)percentage) / 100;
						}
						prodbase = desc->get_productivity() + production;
					}
					else if (desc->get_placement() == 2 && !city && desc->get_product_count() == 0)
					{
						prodbase = desc->get_productivity();
					}
					else
					{
						prodbase = desc->get_productivity() + simrand(desc->get_range(), "fabrik_t::new_month");
					}

					prodbase = prodbase > 0 ? prodbase : 1;

					bool disconnect_supplier_checked = false;
					bool must_close = false;
					array_tpl<ware_production_t> new_array;
					new_array.resize(desc->get_supplier_count());

					vector_tpl<uint32> kept_ware_indexes;
					//find and unlink obsolete input ware types, backup existing types
					for(uint32 j = 0; j < input.get_count(); j++){
						auto &ware = input[j];
						bool keep_ware=false;

						for(uint16 i = 0; i < desc->get_supplier_count(); i++){
							if(ware.get_typ()==desc->get_supplier(i)->get_input_type()){
								keep_ware=true;
								new_array[i]=input[j];
								kept_ware_indexes.append(i);
								break;
							}
						}
						if(!keep_ware){
							for(uint32 i = ware.link_count()-1; i < ware.link_count(); i--){
								fabrik_t* affected_fab = get_fab(ware.link_from_index(i));
								if(affected_fab){
									affected_fab->disconnect_consumer(get_pos().get_2d());
								}
								must_close = disconnect_supplier(ware.link_from_index(i));
								disconnect_supplier_checked=true;
							}
						}
					}
					//find which input ware types are new
					vector_tpl<uint16> new_ware_indexes;
					for(uint16 i = 0; i < desc->get_supplier_count(); i++){
						bool is_new=true;
						for(uint32 j = 0; j < input.get_count(); j++){
							if(input[j].get_typ()==desc->get_supplier(i)->get_input_type()){
								is_new=false;
							}
						}
						if(is_new){
							new_ware_indexes.append(i);
						}
					}

					//initialize new items
					input.resize(desc->get_supplier_count());
					for(auto i : new_ware_indexes){
						input[i]=ware_production_t();
						input[i].set_typ(desc->get_supplier(i)->get_input_type());
					}

					//transfer old items
					for(auto i : kept_ware_indexes){
						input[i]=new_array[i];
					}

					if (!disconnect_supplier_checked)
					{
						must_close = disconnect_supplier(koord::invalid); // This does not remove anything, but checks for missing suppliers
					}
					must_close |= disconnect_consumer(koord::invalid); // This does not remove anything, but checks for missing consumers

					// Missing inputs are checked in increase_industry_density

					if (!must_close)
					{
						//find and unlink obsolete output ware types, backup existing types
						new_array.resize(desc->get_product_count());
						kept_ware_indexes.clear();
						for(uint32 j = 0; j < output.get_count(); j++){
							auto &ware = output[j];
							bool keep_ware=false;

							for(uint16 i = 0; i < desc->get_product_count(); i++){
								if(ware.get_typ()==desc->get_product(i)->get_output_type()){
									keep_ware=true;
									new_array[i]=output[j];
									kept_ware_indexes.append(i);
									break;
								}
							}
							if(!keep_ware){
								for(uint32 i = ware.link_count()-1; i < ware.link_count(); i--){
									if(fabrik_t* affected_fab = get_fab(ware.link_from_index(i))){
										affected_fab->disconnect_supplier(get_pos().get_2d(),this);
									}
									must_close = disconnect_consumer(ware.link_from_index(i));
									disconnect_supplier_checked=true;
								}
							}
						}
						new_ware_indexes.clear();
						//find which output ware types are new
						for(uint16 i = 0; i < desc->get_product_count(); i++){
							bool is_new=true;
							for(uint32 j = 0; j < output.get_count(); j++){
								if(output[j].get_typ()==desc->get_product(i)->get_output_type()){
									is_new=false;
								}
							}
							if(is_new){
								new_ware_indexes.append(i);
							}
						}

						//initialize new items
						output.resize(desc->get_product_count());
						for(auto i : new_ware_indexes){
							output[i]=ware_production_t();
							output[i].set_typ(desc->get_product(i)->get_output_type());
						}

						//transfer old items
						for(auto i : kept_ware_indexes){
							output[i]=new_array[i];
						}

						recalc_storage_capacities();
						adjust_production_for_fields();
						// Re-calculate electricity consumption, mail and passenger demand, etc.
						update_scaled_electric_demand();
						update_scaled_pax_demand();
						update_scaled_mail_demand();
						update_prodfactor_pax();
						update_prodfactor_mail();
						//welt->increase_actual_industry_density(100 / new_type->get_distribution_weight());
						if (new_type->is_consumer_only()) {
							welt->increase_actual_industry_density(100 / new_type->get_distribution_weight());
						}
						// Message to tell players about upgrade, preferably with city name
						if(city) {
							sprintf(buf, translator::translate("Industry: %s in %s has been upgraded to industry: %s."), translator::translate(old_name), city->get_name(), translator::translate(new_name));
						}
						else {
							const stadt_t* nearby_city = welt->find_nearest_city(pos.get_2d());
							if (nearby_city) {
								sprintf(buf, translator::translate("Industry: %s near to %s has been upgraded to industry: %s."), translator::translate(old_name), nearby_city->get_name(), translator::translate(new_name));
							}
							// Fallback if no cities exist
							else {
								sprintf(buf, translator::translate("Industry:\n%s\nhas been upgraded\nto industry:\n%s."), translator::translate(old_name), translator::translate(new_name));
							}
						}
						welt->get_message()->add_message(buf, pos.get_2d(), message_t::industry, CITY_KI, skinverwaltung_t::neujahrsymbol->get_image_id(0));
						return;
					}
				}
			}
		}

		welt->closed_factories_this_month.append(this);
	}else if(welt->get_settings().using_fab_contracts()){
		negotiate_contracts();
	}
	// NOTE: No code should come after this part, as the closing down code may cause this object to be deleted.
}

void fabrik_t::negotiate_contracts(){
	//first check output and reduce contracts that cannot be filled
	for(uint32 i = 0; i < output.get_count(); i++){
		const sint64 pfactor = (sint64)get_desc()->get_product(i)->get_factor();
		const sint32 monthly_prod = get_monthly_production(pfactor);
		const sint32 monthly_cont = output[i].get_total_contracts();
		//check for too many output contracts
		if(monthly_prod < monthly_cont){
			//reduce contracts starting with furthest factory
			for(uint32 j = output[i].link_count()-1; j < output[i].link_count(); j--){
				sint32 contract_diff=output[i].get_total_contracts() - monthly_prod;
				sint32 average_removal=(((sint32)j+1)/2 + contract_diff) / ((sint32)j+1);
				sint32 this_removal;
				if(output[i].get_contract(j) > average_removal){
					this_removal=average_removal;
				}else{
					this_removal=output[i].get_contract(j);
				}
				if(fabrik_t* affected_fab = get_fab(output[i].link_from_index(j))){
					if(auto affected_ware = affected_fab->get_input(output[i].get_typ())){
						const sint64 affected_pfactor=affected_fab->get_desc()->get_supplier(affected_ware->get_typ())->get_consumption();
						const sint32 affected_prod=affected_fab->get_monthly_production(affected_pfactor);
						const sint32 affected_cont=affected_ware->get_total_contracts();
						if(affected_cont - this_removal > affected_prod){
							this_removal = affected_cont - affected_prod;
						}
						ware_production_t::add_contracts(-this_removal,*affected_ware,output[i],j,monthly_prod);
					}
				}
			}
		}
	}
	//for manufacturers determine determine factor of used output for input to be adjusted acordingly
	sint32 manufacturing_factor=0;
	if(input.get_count() && output.get_count()){
		for(uint32 i = 0; i < output.get_count(); i++){
			const sint64 pfactor = (sint64)get_desc()->get_product(i)->get_factor();
			sint32 monthly_prod = get_monthly_production(pfactor);
			sint32 this_factor = (DEFAULT_PRODUCTION_FACTOR * output[i].get_total_contracts()) / monthly_prod;
			if(this_factor>manufacturing_factor){
				manufacturing_factor=this_factor;
			}
		}
	}
	//check input to increase or decrease contracts as needed
	for(uint32 i = 0; i < input.get_count(); i++){
		const sint64 pfactor = (sint64)get_desc()->get_supplier(i)->get_consumption();
		sint32 monthly_prod = get_monthly_production(pfactor);
		monthly_prod = max(adjust_consumption_by_passenger_level(monthly_prod),1 << fabrik_t::precision_bits); //scale consumption by customers
		//manufacturers only need enough input to supply for their output
		if(manufacturing_factor && manufacturing_factor<DEFAULT_PRODUCTION_FACTOR){
			monthly_prod=(monthly_prod * manufacturing_factor) / DEFAULT_PRODUCTION_FACTOR;
		}
		//scale down used monthly production figure when intransit levels are too high
		if(input[i].get_in_transit() + input[i].menge > input[i].max_transit){
			monthly_prod=(monthly_prod * input[i].max_transit) / (input[i].get_in_transit() + input[i].menge);
		}
		sint32 monthly_cont = input[i].get_total_contracts();
		if(monthly_prod * 8 < monthly_cont * 9 || monthly_prod * 7 > monthly_cont * 8){
			//too little or too much input
			//start by adding to active staffed supliers or removing from unstaffed or inactive supliers
			for(uint32 j = input[i].link_count()-1; j < input[i].link_count(); j--){
				sint32 contract_diff=monthly_prod - input[i].get_total_contracts();
				if(contract_diff==0){
					break;
				}
				sint32 average_addition=(contract_diff + ((sint32)j+1)/2) / ((sint32)j+1);
				sint32 this_addition=average_addition;
				//verify that contract exists in first place and add to or reduce from it
				if(fabrik_t* affected_fab = get_fab(input[i].link_from_index(j))){
					if((this_addition>0 ? 1 : 0) ^ ((affected_fab->get_status()==fabrik_t::inactive || affected_fab->is_staff_shortage()) ? 1 : 0)){
						if(auto affected_ware = affected_fab->get_output(input[i].get_typ())){
							for(uint32 k = 0; k < affected_ware->link_count(); k++){
								if(affected_ware->link_from_index(k) == get_pos().get_2d()){
									//check for maximum output
									const sint64 affected_pfactor=affected_fab->get_desc()->get_product(affected_ware->get_typ())->get_factor();
									const sint32 affected_prod=affected_fab->get_monthly_production(affected_pfactor);
									ware_production_t::add_contracts(this_addition,input[i],*affected_ware,k,affected_prod);
									break;
								}
							}
						}
					}
				}
			}
			monthly_cont = input[i].get_total_contracts();
			//then add or remove from all linked suppliers
			sint32 untapped_sources=0;
			for(uint32 j = input[i].link_count()-1; j < input[i].link_count(); j--){
				sint32 contract_diff=monthly_prod - input[i].get_total_contracts();
				if(contract_diff==0){
					break;
				}
				sint32 average_addition=(contract_diff + ((sint32)j+1)/2) / ((sint32)j+1);
				sint32 this_addition=average_addition;
				//verify that contract exists in first place and add to or reduce from it
				if(fabrik_t* affected_fab = get_fab(input[i].link_from_index(j))){
					if(auto affected_ware = affected_fab->get_output(input[i].get_typ())){
						for(uint32 k = 0; k < affected_ware->link_count(); k++){
							if(affected_ware->link_from_index(k) == get_pos().get_2d()){
								//check for maximum output
								const sint64 affected_pfactor=affected_fab->get_desc()->get_product(affected_ware->get_typ())->get_factor();
								const sint32 affected_prod=affected_fab->get_monthly_production(affected_pfactor);
								ware_production_t::add_contracts(this_addition,input[i],*affected_ware,k,affected_prod);
								untapped_sources+=affected_prod - affected_ware->get_total_contracts();
								break;
							}
						}
					}
				}
			}
			monthly_cont = input[i].get_total_contracts();
			if(monthly_prod * 7 > monthly_cont * 8 && untapped_sources){
				//Still too little, try to fill more agressively
				for(uint32 j = 0; j < input[i].link_count(); j++){
					sint32 contract_diff=monthly_prod - input[i].get_total_contracts();
					if(contract_diff<=0){
						break;
					}
					sint32 this_addition=contract_diff;
					if(fabrik_t* affected_fab = get_fab(input[i].link_from_index(j))){
						if(auto affected_ware = affected_fab->get_output(input[i].get_typ())){
							for(uint32 k = 0; k < affected_ware->link_count(); k++){
								if(affected_ware->link_from_index(k) == get_pos().get_2d()){
									//check for maximum output
									const sint64 affected_pfactor=affected_fab->get_desc()->get_product(affected_ware->get_typ())->get_factor();
									const sint32 affected_prod=affected_fab->get_monthly_production(affected_pfactor);
									ware_production_t::add_contracts(this_addition,input[i],*affected_ware,k,affected_prod);
									break;
								}
							}
						}
					}
				}
			}
			monthly_cont = input[i].get_total_contracts();
			if(monthly_prod * 7 > monthly_cont * 8){
				//still too little, but all linked supliers exausted
				//Call functions to try to find another suplier(s) to link to
				factory_builder_t::build_chain_link(this,this->get_desc(),i,welt->get_public_player(),true);
			}
		}
	}
}

// static !
uint8 fabrik_t::status_to_color[MAX_FAB_STATUS] = {
	COL_WHITE, COL_GREEN, COL_DODGER_BLUE, COL_LIGHT_TURQUOISE, COL_BLUE, COL_DARK_GREEN,
	COL_GREY3, COL_DARK_BROWN + 1, COL_YELLOW - 1, COL_YELLOW, COL_ORANGE, COL_ORANGE_RED, COL_RED, COL_BLACK, COL_DARK_BLUE, COL_DARK_RED+1 };

#define FL_WARE_NULL           1
#define FL_WARE_ALLENULL       2
#define FL_WARE_LIMIT          4
#define FL_WARE_ALLELIMIT      8
#define FL_WARE_UEBER75        16
#define FL_WARE_ALLEUEBER75    32
#define FL_WARE_FEHLT_WAS      64


/* returns the status of the current factory, as well as output
   also updates total_input/total_transit/total_output          */
void fabrik_t::recalc_factory_status()
{
	uint64 warenlager;
	char status_ein; // in
	char status_aus; // out

	int haltcount = nearby_freight_halts.get_count();

	// set bits for input
	warenlager = 0;
	total_transit = 0;
	status_ein = FL_WARE_ALLELIMIT;
	status = nothing;
	uint32 i = 0;
	if(const uint32 input_count = input.get_count()){
		uint32 active_input_count = 0;
		for(ware_production_t const& j : input) {
			if (j.menge >= j.max) {
				status_ein |= FL_WARE_LIMIT;
			}
			else {
				status_ein &= ~FL_WARE_ALLELIMIT;
			}
			warenlager += (uint64)j.menge * (uint64)(desc->get_supplier(i++)->get_consumption());
			total_transit += j.get_in_transit();
			if ((j.menge >> fabrik_t::precision_bits) == 0) {
				status_ein |= FL_WARE_FEHLT_WAS;
			}
			// Does each input goods have one or more suppliers? If not, this factory will not be operational.
			bool found = j.link_count() ? true : false;

			if (found || (!found && (j.get_in_transit() || j.menge))) {
				active_input_count++;
			}
		}
		warenlager >>= fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS;
		if (warenlager == 0) {
			status_ein |= FL_WARE_ALLENULL;
		}
		total_input = (uint32)warenlager;

		if( !output.empty() ) {
			// All materials must be available for manufacturing.
			if (active_input_count != input_count) {
				status = material_not_available;
			}
			else if (status_ein & FL_WARE_FEHLT_WAS && !output.empty() && haltcount > 0 && status != material_not_available) {
				// one ware missing, but producing
				status = material_shortage;
			}
		}
		else if (!active_input_count) {
			// No access to any materials or goods.
			status = material_not_available;
		}
	}

	// set bits for output
	warenlager = 0;
	status_aus = FL_WARE_ALLEUEBER75 | FL_WARE_ALLENULL;
	i = 0;
	FORX(array_tpl<ware_production_t>, const& j, output, i++) {
		if (j.menge > 0) {

			status_aus &= ~FL_WARE_ALLENULL;
			if (j.menge >= 0.75 * j.max) {
				status_aus |= FL_WARE_UEBER75;
			}
			else {
				status_aus &= ~FL_WARE_ALLEUEBER75;
			}
			warenlager += (uint64)(FAB_DISPLAY_UNIT_HALF + (uint64)j.menge * (uint64)(desc->get_product(i)->get_factor())) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS);
			status_aus &= ~FL_WARE_ALLENULL;
		}
		else {
			// menge = 0
			status_aus &= ~FL_WARE_ALLEUEBER75;
		}
	}
	total_output = (uint32)warenlager;

	// At least one must have a normal downstream industry
	if( !output.empty() ) {
		bool has_any_consumer = false;
		for(auto k : get_consumers()) {
			if (const fabrik_t* consumer = fabrik_t::get_fab(k)) {
				// Focus on incomplete connections between manufacturers rather than missing end consumer's connection
				if (consumer->get_sector()==end_consumer) {
					has_any_consumer = true;
					break;
				}
				const uint8 consumer_status = consumer->get_status();
				if (consumer_status!= missing_connections && consumer_status != missing_consumer && consumer_status != material_not_available) {
					has_any_consumer = true;
					break;
				}
			}
		}
		if (!has_any_consumer) {
			// no sane shipping partner
			if (status== material_not_available) {
				status = missing_connections;
			}
			else {
				status = missing_consumer;
			}
		}
	}
	if (sector==marine_resource && !get_consumers().get_count()) {
		status = missing_consumer;
		return;
	}

	// now calculate status bar for sane factory
	if (status != missing_consumer && status != material_not_available && status != missing_connections) {
		switch (sector) {
			case marine_resource:
				// since it has a station function, it discriminates only whether stock is full or not
				status = status_aus & FL_WARE_ALLEUEBER75 ? water_resource_full : water_resource;
				break;
			case resource:
			case resource_city:
				if (!haltcount) {
					status = inactive;
				}
				else if (status_aus&FL_WARE_ALLEUEBER75 || status_aus & FL_WARE_UEBER75) {
					if (status_aus&FL_WARE_ALLEUEBER75) {
						status = storage_full;  // connect => needs better service
					}
					else {
						status = medium;        // connect => needs better service for at least one product
					}
				}
				else {
					status = good;
				}
				break;
			case manufacturing:
				if (!haltcount) {
					status = inactive;
				}
				else if (status_ein&FL_WARE_ALLELIMIT && status_aus&FL_WARE_ALLELIMIT) {
					status = stuck; // all storages are full => Shipment and arrival are stagnant, and it can not produce anything
				}
				else if (status_ein&FL_WARE_ALLENULL) {
					status = no_material;
				}
				else if (status_ein&FL_WARE_NULL) {
					status = material_shortage;
				}
				else if (status_ein&FL_WARE_ALLELIMIT) {
					status = mat_overstocked; // all input storages are full => lack of production speed = receiving stop
				}
				else if (status_aus&FL_WARE_ALLELIMIT) {
					status = shipment_stuck; // all out storages are full => product demand is low or shipment pace is slow = shipping stop
				}
				else if (status_ein&FL_WARE_LIMIT || status_aus & FL_WARE_LIMIT) {
					status = medium; // some storages are full
				}
				else {
					status = good;
				}
				break;
			case end_consumer:
			case power_plant:
				if (sector == power_plant && !desc->get_supplier_count()) {
					// Power plants that do not require materials such as wind power generation
					if (currently_producing) {
						status = good;
					}
					else {
						status = bad;
					}
				}
				else if (!haltcount) {
					status = inactive;
				}
				else if (status_ein&FL_WARE_ALLELIMIT) {
					// Excess supply or Stagnation of shipment or Low productivity or Customer shortage => receiving stop
					status = mat_overstocked;
				}
				else if (status_ein&FL_WARE_LIMIT) {
					// served, but still one at limit => possibility of customer shortage and some delivery stops because its storage is full
					status = bad;
				}
				else if (status_ein&FL_WARE_ALLENULL) {
					// there is a halt => needs better service
					status = no_material;
				}
				else if (status_ein&FL_WARE_NULL) {
					// some items out of stock, but still active
					status = medium;
				}
				else {
					status = good;
				}
				break;
			default:
				if (!haltcount) {
					status = inactive;
				}
				else {
					status = nothing;
				}
				break;
		}
	}
}

bool fabrik_t::is_input_empty() const
{
	if (input.empty())
	{
		return false;
	}

	for (uint32 index = 0; index < input.get_count(); index++)
	{
		if (input[index].menge > 0)
		{
			return false;
		}
	}

	return true;
}


void fabrik_t::show_info()
{
	create_win(new fabrik_info_t(this, get_building()), w_info, (ptrdiff_t)this );
}


void fabrik_t::info_prod(cbuffer_t& buf) const
{
	buf.clear();
	if (get_base_production()) {
		buf.append(translator::translate("Productivity")); // Note: This term is used in width calculation in fabrik_info. And it is common with the chart button label.
		// get_current_productivity() does not include factory connections and staffing
		buf.printf(": %u%% ", get_actual_productivity());
		const uint32 max_productivity = (100 * (get_desc()->get_electric_boost() + get_desc()->get_pax_boost() + get_desc()->get_mail_boost())) >> DEFAULT_PRODUCTION_FACTOR_BITS;
		buf.printf(translator::translate("(Max. %d%%)"), max_productivity+100);
		buf.append("\n");

		buf.printf("%s: %.1f%% (%s: %.1f%%)", translator::translate("Operation rate"), statistics[0][FAB_PRODUCTION] / 100.0, translator::translate("Last Month"), statistics[1][FAB_PRODUCTION] / 100.0);
		buf.append("\n");
	}
	if(get_desc()->is_electricity_producer())
	{
		buf.append(translator::translate("Electrical output: "));
		buf.append(scaled_electric_demand >> POWER_TO_MW);
		buf.append(" MW");
	}
	else
	{
		buf.append(translator::translate("Electrical demand: "));
		buf.append((scaled_electric_demand * 1000) >> POWER_TO_MW);
		buf.append(" KW");
	}

	buf.append("\n");

	if(city != NULL)
	{
		buf.append("\n");
		buf.append(translator::translate("City"));
		buf.append(": ");
		buf.append(city->get_name());
	}
	if (building)
	{
		buf.append("\n");
		buf.printf("%s: %d\n", translator::translate("Visitor demand"), building->get_adjusted_visitor_demand());
#ifdef DEBUG
		buf.printf("%s (%s): %d (%d)\n", translator::translate("Jobs"), translator::translate("available"), building->get_adjusted_jobs(), building->check_remaining_available_jobs());
#else
		buf.printf("%s (%s): %d (%d)\n", translator::translate("Jobs"), translator::translate("available"), building->get_adjusted_jobs(), max(0, building->check_remaining_available_jobs()));
#endif
		buf.printf("%s: %d\n", translator::translate("Mail demand/output"), building->get_adjusted_mail_demand());

	}

	if(welt->get_settings().using_fab_contracts()){
		if(months_unproductive || months_missing_contracts){
			buf.append("\n");
		}
		if(months_unproductive){
			buf.printf(translator::translate("Unproductive: %d Mo."),months_unproductive);
		}
		buf.append("\n");
		if(months_missing_contracts){
			buf.printf(translator::translate("Contractless: %d Mo."),months_missing_contracts);
		}
		buf.append("\n\n");
	}
}

/**
 * Recalculate the nearby_freight_halts and nearby_passenger_halts lists.
 * This is a subroutine in order to avoid code duplication.
 * @author neroden
 */
void fabrik_t::recalc_nearby_halts()
{
	// Temporary list for accumulation of halts;
	// avoid duplicating work on freight and passengers
	vector_tpl<nearby_halt_t> nearby_halts;

	// Clear out the old lists.
	nearby_freight_halts.clear();
	nearby_passenger_halts.clear();
	nearby_mail_halts.clear();

	// Go through all the base tiles of the factory.
	vector_tpl<koord> tile_list;
	get_tile_list(tile_list);

#ifdef DEBUG
	bool any_distribution_target = false; // just for debugging
#endif

	FOR(vector_tpl<koord>, const k, tile_list)
	{
		const planquadrat_t* plan = welt->access(k);
		if(plan)
		{
#ifdef DEBUG
			any_distribution_target=true;
#endif
			const uint8 haltlist_count = plan->get_haltlist_count();
			if(haltlist_count)
			{
				const nearby_halt_t *haltlist = plan->get_haltlist();
				for(int i = 0; i < haltlist_count; i++)
				{
					// We've found a halt.
					const nearby_halt_t new_nearby_halt = haltlist[i];
					// However, it might be a duplicate.
					bool duplicate = false;
					bool duplicate_freight = false;
					for(uint32 j=0; j < nearby_halts.get_count(); j++)
					{
						if(new_nearby_halt.halt == nearby_halts[j].halt)
						{
							duplicate = true;
							// Same halt handle.
							// We always want the shorter of the two distances,
							// since goods/passengers can ship from any part of a factory.
							uint8 new_distance = min(nearby_halts[j].distance, new_nearby_halt.distance);
							if(nearby_halts[j].distance <= welt->get_settings().get_station_coverage_factories() + 2)
							{
								duplicate_freight = true;
							}
							nearby_halts[j].distance = new_distance;
						}
					}
					if(!duplicate)
					{
						nearby_halts.append(new_nearby_halt);
						if(new_nearby_halt.halt->get_pax_enabled())
						{
							nearby_passenger_halts.append(new_nearby_halt);
						}
						if (new_nearby_halt.halt.is_bound() && new_nearby_halt.halt->get_mail_enabled())
						{
							nearby_mail_halts.append(new_nearby_halt);
						}
					}
					if(!duplicate_freight)
					{
						if(new_nearby_halt.halt.is_bound() && new_nearby_halt.halt->get_ware_enabled() && new_nearby_halt.distance <= welt->get_settings().get_station_coverage_factories() + 2) // We add 2 here as we do not count the first or last tile in the distance
						{
							// Halt is within freight coverage distance (shorter than regular) and handles freight...
							if(get_desc()->get_placement() == factory_desc_t::Water && (new_nearby_halt.halt->get_station_type() & haltestelle_t::dock) == 0)
							{
								// But this is a water factory and it's not a dock.
								// So do nothing.
							}
							else
							{
								// Add to the list of freight halts.
								nearby_freight_halts.append(new_nearby_halt);
								new_nearby_halt.halt->add_factory(this);
							}
						}
					}
				}
			}
		}
	}
#ifdef DEBUG
	if(!any_distribution_target)
	{
		dbg->fatal("fabrik_t::recalc_nearby_halts", "%s has no location on the map!", get_name() );
	}
#endif // DEBUG
}


void fabrik_t::finish_rd()
{
	recalc_nearby_halts();

	if(welt->get_settings().using_fab_contracts()){
		for(auto &ware : output){
			ware.set_using_contracts();
		}
	}

	// now we have a valid storage limit
	if(  welt->get_settings().is_crossconnect_factories()  ) {
		for(fabrik_t* const fab : welt->get_fab_list() ) {
			fab->add_supplier(this);
		}
	}
	else {
		// add as supplier to target(s)
		for(uint32 i=0; i < consumers.get_count(); i++) {
			sint32 contract=0;
			if(welt->get_settings().using_fab_contracts() && i < contracts.get_count()){
				contract=contracts[i];
			}
			add_consumer(consumers[i],0,contract);
		}
	}
	consumers.clear();
	consumers.resize(0);
	contracts.clear();
	contracts.resize(0);
	if(consumers_active_this_month.empty()){
		reset_consumer_active();
	}

	// Set field production
	adjust_production_for_fields(true);

	city = check_local_city();
	if (city != NULL)
	{
		city->add_city_factory(this);
	}

	mark_connected_roads(false);
	add_to_world_list();


	//check that things loaded correctly
	for(auto &ware : output){
		ware.verify_contracts();
	}

	//check that the settings did not change prior to load
	if(welt->get_settings().using_fab_contracts() && output.get_count() && !output[0].get_using_contracts()){
		init_contracts();
	}else if(!welt->get_settings().using_fab_contracts() && output.get_count() && output[0].get_using_contracts()){
		//this will not correct for unit change but will potentially prevent a crash
		remove_contracts();
	}
}

void fabrik_t::adjust_production_for_fields(bool is_from_saved_game)
{
	const field_group_desc_t *fd = desc->get_field_group();
	uint32 field_production = 0;
	if (fd)
	{
		for (uint32 i = 0; i<fields.get_count(); i++)
		{
			const field_class_desc_t *fc = fd->get_field_class(fields[i].field_class_index);
			if (fc)
			{
				field_production += fc->get_field_production();
			}
		}
		if (desc->get_field_output_divider() > 1)
		{
			field_production /= desc->get_field_output_divider();
		}
	}

	// set production, update all production related numbers
	if (field_production > 0)
	{
		// This does not take into account the "range" of the base production;
		// but this is not stored other than in "prodbase", which is overwritten
		// by the fields value.
		set_base_production(desc->get_productivity() + field_production, is_from_saved_game);
	}
	else
	{
		set_base_production(prodbase, is_from_saved_game);
	}

}

void fabrik_t::rotate90( const sint16 y_size )
{
	rotate = (rotate+3)%desc->get_building()->get_all_layouts();
	pos_origin.rotate90( y_size );
	pos_origin.x -= desc->get_building()->get_x(rotate)-1;
	pos.rotate90( y_size );

	for(auto i : get_consumers()) {
		i.rotate90(y_size);
	}
	for(auto i : get_suppliers()) {
		i.rotate90(y_size);
	}
	for(field_data_t & i : fields) {
		i.location.rotate90(y_size);
	}

	recalc_nearby_halts();
}


void fabrik_t::add_supplier(koord ziel, const goods_desc_t *desc)
{
	for(auto &ware : get_input()){
		if(ware.get_typ()==desc){
			ware.link_add(ziel, RelativeDistanceOrdering(pos.get_2d()));
			calc_max_intransit_percentages();
		}else if(!desc){
			if(fabrik_t *fab = get_fab( ziel )){
				bool calc_needed=false;
				for(auto &source_ware : fab->get_output()){
					if(source_ware.get_typ() == ware.get_typ()){
						ware.link_add(ziel, RelativeDistanceOrdering(pos.get_2d()));
						calc_needed=true;
					}
				}
				if(calc_needed){
					calc_max_intransit_percentages();
				}
			}
		}
	}
}


void fabrik_t::remove_supplier(koord supplier_pos, fabrik_t* supplier)
{
	for(auto& ware : get_input()){
		uint32 idx=ware.link_index(supplier_pos);
		if(idx!=UINT32_MAX_VALUE){
			if(welt->get_settings().using_fab_contracts()){
				fabrik_t* affected_fab = supplier;
				if(!affected_fab){
					affected_fab = get_fab(supplier_pos);
				}
				if(affected_fab){
					if(auto affected_ware = affected_fab->get_output(ware.get_typ())){
						uint32 affected_idx=affected_ware->link_index(this->get_pos().get_2d());
						if(affected_idx!=UINT32_MAX_VALUE){
							sint32 contract=affected_ware->get_contract(affected_idx);
							ware.sub_total_contracts(contract);
						}
					}
				}
			}
			ware.link_remove(idx);
		}
	}

	if(  welt->get_settings().get_factory_maximum_intransit_percentage()  ) {
		// set to zero
		for(ware_production_t &w : input ) {
			w.max_transit = 0;
		}

		calc_max_intransit_percentages();
	}
}


/** crossconnect everything possible */
void fabrik_t::add_all_suppliers()
{
	for(int i=0; i < desc->get_supplier_count(); i++) {
		const factory_supplier_desc_t *supplier = desc->get_supplier(i);
		const goods_desc_t *ware = supplier->get_input_type();

		for(fabrik_t* const fab : welt->get_fab_list()) {
			// connect to an existing one, if this is an producer
			if(fab!=this  && fab->get_output_stock(ware) > -1) {
				// add us to this factory
				// will also add to our suppliers list
				fab->add_consumer(pos.get_2d());
			}
		}
	}
}


/* adds a new supplier to this factory
 * fails if no matching goods are there
 */
bool fabrik_t::add_supplier(fabrik_t* fab, const goods_desc_t* product)
{
	for(int i=0; i < desc->get_supplier_count(); i++) {
		const factory_supplier_desc_t *supplier = desc->get_supplier(i);
		const goods_desc_t *ware = supplier->get_input_type();

			// connect to an existing one, if this is an producer
			if(  fab!=this  && fab->get_output_stock(ware) > -1  ) {
				// add us to this factory
				fab->add_consumer(pos.get_2d(),product);
				cbuffer_t buf;
				if(product){
					// Message if the factory is in a city
					stadt_t* factorys_city = fab->get_city();
					if(factorys_city) {
						buf.printf(translator::translate("New shipping destination added to Factory %s in %s for %s."), translator::translate(fab->get_name()), factorys_city->get_name(), translator::translate(product->get_name()));
					}
					// Message if the factory is near a city
					else {
						 factorys_city = welt->find_nearest_city((fab->get_pos()).get_2d());
						if(factorys_city) {
							buf.printf(translator::translate("New shipping destination added to Factory %s (near to %s) for %s."), translator::translate(fab->get_name()), factorys_city->get_name(), translator::translate(product->get_name()));
						}
					// Fallback if there are no cities
					buf.printf(translator::translate("New shipping destination added to Factory %s for %s"), translator::translate(fab->get_name()), translator::translate(product->get_name()));
					}
				}else{
					// TODO: Duplicate city-specific messages if this message appears often
					buf.printf(translator::translate("New shipping destination added to Factory %s"), translator::translate(fab->get_name()));
				}
				welt->get_message()->add_message(buf, fab->get_pos().get_2d(), message_t::industry, CITY_KI, fab->get_desc()->get_building()->get_tile(0)->get_background(0, 0, 0));
				return true;
			}
	}
	return false;
}

/* adds a new customer to this factory
 * fails if no matching goods are accepted
 */

bool fabrik_t::add_customer(fabrik_t* fab, const goods_desc_t* product)
{
	if(fab==this){
		return false;
	}
	// connect to an existing one, if it is a consumer
	if(fab->get_desc()->get_supplier(product)) {
		// add this factory
		add_consumer(fab->pos.get_2d(),product);
		return true;
	}
	return false;
}

void fabrik_t::get_tile_list( vector_tpl<koord> &tile_list ) const
{
	tile_list.clear();

	koord pos_2d = pos.get_2d();
	const factory_desc_t* desc = get_desc();
	if(!desc)
	{
		return;
	}
	const building_desc_t* building_desc = desc->get_building();
	if(!building_desc)
	{
		return;
	}
	koord size = building_desc->get_size(this->get_rotate());
	koord test;
	// Which tiles belong to the fab?
	for( test.x = 0; test.x < size.x; test.x++ ) {
		for( test.y = 0; test.y < size.y; test.y++ ) {
			if( fabrik_t::get_fab( pos_2d+test ) == this ) {
				tile_list.append( pos_2d+test );
			}
		}
	}
}

// Returns a list of goods produced by this factory. The caller must delete
// the list when done
slist_tpl<const goods_desc_t*> *fabrik_t::get_produced_goods() const
{
	slist_tpl<const goods_desc_t*> *goods = new slist_tpl<const goods_desc_t*>();

	for(ware_production_t const& i : output ) {
		goods->append(i.get_typ());
	}

	return goods;
}


void fabrik_t::add_to_world_list()
{
	welt->add_building_to_world_list(get_building()->access_first_tile());
}

gebaeude_t* fabrik_t::get_building()
{
	if(building)
	{
		return building;
	}
	const grund_t* gr = welt->lookup(pos);
	if(gr)
	{
		building = gr->find<gebaeude_t>();
		return building;
	}
	return NULL;
}


void fabrik_t::calc_max_intransit_percentages()
{
	if(!path_explorer_t::get_paths_available(path_explorer_t::get_current_compartment_category()))
	{
		has_calculated_intransit_percentages = false;
		return;
	}

	has_calculated_intransit_percentages = true;


	const uint16 base_max_intransit_percentage = welt->get_settings().get_factory_maximum_intransit_percentage();

	if(base_max_intransit_percentage == 0)
	{
		// Zero is code for the feature being disabled, so do not attempt to modify this value.
		return;
	}

	uint32 index = 0;
	for(ware_production_t &w : input ) {
		const uint32 lead_time = get_lead_time(w.get_typ());
		if(lead_time == UINT32_MAX_VALUE)
		{
			// No factories connected; use the default intransit percentage for now.
			input[index].max_transit = max(1, ((sint64)base_max_intransit_percentage * input[index].max) / 100); // This puts max_transit in internal units
			index ++;
			continue;
		}
		if(welt->get_settings().using_fab_contracts()){
			sint64 max_transit;
			max_transit = base_max_intransit_percentage; //percentage
			max_transit *= lead_time; //tenths of a minute
			sint64 pfactor = desc->get_supplier(index)->get_consumption();
			max_transit *= get_monthly_production(pfactor); //goods * precision / month
			max_transit /= welt->ticks_to_tenths_of_minutes(welt->ticks_per_world_month);
			max_transit /= 100;
			input[index].max_transit=max(1,max_transit); //goods * precision
			index++;
			continue;
		}

		const factory_supplier_desc_t* flb = desc->get_supplier(index);
		const uint32 vb = flb ? flb->get_consumption() : 0;

		const sint32 base_production = get_current_production();
		uint64 consumed_per_month = max((uint64)base_production, 1);

		consumed_per_month = adjust_consumption_by_passenger_level(consumed_per_month);
		uint64 max_transit = max(consumed_per_month,256);
		max_transit *= base_max_intransit_percentage;
		max_transit *= lead_time;
		max_transit *= 16384*256;
		max_transit /= 3 * (uint64)welt->get_settings().meters_per_tile * welt->ticks_per_world_month;
		input[index].max_transit = max_transit;
		index ++;
	}
}

sint64 fabrik_t::adjust_consumption_by_passenger_level(sint64 consumed_per_month){
	if(desc->is_consumer_only())
	{
		// Consumer industries adjust their consumption according to the number of visitors. Adjust for this.
		// We cannot use actual consumption figures, as this could lead to deadlocks.

		// Do not use the current month, as this is not complete yet, and the number of visitors will therefore be low.
		sint64 average_consumers = 0;
		if(get_stat(3, FAB_CONSUMER_ARRIVED))
		{
			average_consumers = (get_stat(1, FAB_CONSUMER_ARRIVED) + get_stat(2, FAB_CONSUMER_ARRIVED) + get_stat(3, FAB_CONSUMER_ARRIVED)) / 3ll;
		}
		else if(get_stat(2, FAB_CONSUMER_ARRIVED))
		{
			average_consumers = (get_stat(1, FAB_CONSUMER_ARRIVED) + get_stat(2, FAB_CONSUMER_ARRIVED) / 2ll);
		}
		else
		{
			average_consumers = get_stat(1, FAB_CONSUMER_ARRIVED);
		}
		// Only make the adjustment if we have data.
		if (average_consumers)
		{
			const sint64 visitor_demand = (sint64)building->get_adjusted_visitor_demand();
			const sint64 percentage = std::max(100ll, (average_consumers * 100ll) / visitor_demand);
			consumed_per_month = (consumed_per_month * percentage) / 100;
		}
	}
	return consumed_per_month;
}

uint32 fabrik_t::get_total_input_capacity() const
{
	uint32 capacity_sum = 0;
	for(uint32 i=0; i < input.get_count(); i++) {
		ware_production_t const& goods = input[i];
		const sint64 pfactor =desc->get_supplier(i) ? (sint64)desc->get_supplier(i)->get_consumption() : 1ll;
		const uint32 storage_capacity = (uint32)((FAB_DISPLAY_UNIT_HALF + (sint64)goods.max * pfactor) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS));

		capacity_sum += storage_capacity;
	}
	return capacity_sum;
}

uint32 fabrik_t::get_total_output_capacity() const
{
	uint32 sum = 0;
	for(uint32 i=0; i < output.get_count(); i++) {
		ware_production_t const& goods = output[i];
		const sint64 pfactor = (sint64)desc->get_product(i)->get_factor();
		sum += (uint32)((FAB_DISPLAY_UNIT_HALF + (sint64)goods.max * pfactor) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS));
	}
	return sum;
}

/**
 * Draws some nice colored bars giving some status information
 */
void fabrik_t::display_status(sint16 xpos, sint16 ypos)
{
	const sint16 count = input.get_count()+output.get_count();

	ypos -= D_LABEL_HEIGHT / 2 + D_WAITINGBAR_WIDTH;
	xpos -= (count * D_WAITINGBAR_WIDTH - get_tile_raster_width()) / 2;
	if( sector==marine_resource ) {
		ypos -= D_LABEL_HEIGHT / 2 + D_WAITINGBAR_WIDTH;
		xpos -= get_tile_raster_width()/4;
	}
	//const int x = xpos;
	const bool active = (status != missing_connections && status != inactive && status != material_not_available && status != missing_consumer);

	// if pakset has symbol, display it
	if( input.get_count() ) {
		if( active ) {
			display_ddd_box_clip_rgb(xpos-2,  ypos-1, D_WAITINGBAR_WIDTH*input.get_count()+4, 6, color_idx_to_rgb(10), color_idx_to_rgb(12));
		}
		display_fillbox_wh_clip_rgb(xpos-1, ypos, D_WAITINGBAR_WIDTH*input.get_count()+2, 4, color_idx_to_rgb(COL_DODGER_BLUE-1), true);

		for(uint32 i=0; i < input.get_count(); i++) {
			ware_production_t const& goods = input[i];
			if (!desc->get_supplier(i)) {
				continue;
			}
			const sint64 pfactor = desc->get_supplier(i) ? (sint64)desc->get_supplier(i)->get_consumption() : 1ll;
			//const sint64 max_transit      = (uint32)((FAB_DISPLAY_UNIT_HALF + (sint64)goods.max_transit * pfactor) >> (fabrik_t::precision_bits + DEFAULT_PRODUCTION_FACTOR_BITS));
			const uint32 stock_quantity = (uint32)goods.get_storage();
			const uint32 storage_capacity = (uint32)goods.get_capacity(pfactor);
			const PIXVAL goods_color = goods.get_typ()->get_color();

			if (storage_capacity) {
				const uint16 v = min(25, (uint16)(25 * stock_quantity / storage_capacity))+2;

				if( active ) {
					display_fillbox_wh_clip_rgb(xpos, ypos - v - 1, 1, v, color_idx_to_rgb(COL_GREY4), true);
					display_fillbox_wh_clip_rgb(xpos + 1, ypos - v - 1, D_WAITINGBAR_WIDTH - 2, v, goods_color, true);
					display_fillbox_wh_clip_rgb(xpos + D_WAITINGBAR_WIDTH - 1, ypos - v - 1, 1, v, color_idx_to_rgb(COL_GREY1), true);
				}
				else{
					display_blend_wh_rgb(xpos + 1, ypos - v - 1, D_WAITINGBAR_WIDTH - 2, v, goods_color, 60);
				}
			}
			xpos += D_WAITINGBAR_WIDTH;
		}
		xpos += 4;
	}

	if( output.get_count() ) {
		if( active ) {
			display_ddd_box_clip_rgb(xpos-2,  ypos-1, D_WAITINGBAR_WIDTH*output.get_count()+4, 6, color_idx_to_rgb(10), color_idx_to_rgb(12));
		}
		display_fillbox_wh_clip_rgb(xpos-1, ypos, D_WAITINGBAR_WIDTH*output.get_count()+2, 4, color_idx_to_rgb(COL_ORANGE), true);

		for(uint32 i=0; i < output.get_count(); i++) {
			ware_production_t const& goods = output[i];
			const sint64 pfactor = (sint64)desc->get_product(i)->get_factor();
			const uint32 stock_quantity = (uint32)goods.get_storage();
			const uint32 storage_capacity = (uint32)goods.get_capacity(pfactor);
			const PIXVAL goods_color  = goods.get_typ()->get_color();

			const uint16 v = min(25, (uint16)(25 * stock_quantity / storage_capacity)) + 2;

			if( active ) {
				display_fillbox_wh_clip_rgb(xpos, ypos - v - 1, 1, v, color_idx_to_rgb(COL_GREY4), true);
				display_fillbox_wh_clip_rgb(xpos + 1, ypos - v - 1, D_WAITINGBAR_WIDTH - 2, v, goods_color, true);
				display_fillbox_wh_clip_rgb(xpos + D_WAITINGBAR_WIDTH - 1, ypos - v - 1, 1, v, color_idx_to_rgb(COL_GREY1), true);
			}
			else {
				display_blend_wh_rgb(xpos + 1, ypos - v - 1, D_WAITINGBAR_WIDTH - 2, v, goods_color, 60);
			}

			xpos += D_WAITINGBAR_WIDTH;
		}
	}
}

uint32 fabrik_t::get_lead_time(const goods_desc_t* wtype)
{
	// Tenths of minutes.
	uint32 longest_lead_time = UINT32_MAX_VALUE;

	for(auto &ware : input){
		if(ware.get_typ()==wtype){
			for(auto supplier : ware.get_links()){
				const fabrik_t *fab = get_fab(supplier);
				if(!fab)
				{
					continue;
				}
				for (uint i = 0; i < fab->get_desc()->get_product_count(); i++)
				{
					const factory_product_desc_t *product = fab->get_desc()->get_product(i);
					if(product->get_output_type() == wtype)
					{
						uint32 best_journey_time = UINT32_MAX_VALUE;
						const uint32 transfer_journey_time_factor = ((uint32)welt->get_settings().get_meters_per_tile() * 6) * 10;

						FOR(vector_tpl<nearby_halt_t>, const& nearby_halt, fab->nearby_freight_halts)
						{
							// now search route
							const uint32 origin_transfer_time = (((uint32)nearby_halt.distance * transfer_journey_time_factor) / 100) + nearby_halt.halt->get_transshipment_time();
							ware_t tmp;
							tmp.set_desc(wtype);
							tmp.set_zielpos(pos.get_2d());
							tmp.set_origin(nearby_halt.halt);
							uint32 current_journey_time = (uint32)nearby_halt.halt->find_route(tmp, best_journey_time);
							if (current_journey_time < UINT32_MAX_VALUE)
							{
								current_journey_time += origin_transfer_time;
								if (tmp.get_ziel().is_bound())
								{
									const uint32 destination_distance_to_stop = shortest_distance(tmp.get_zielpos(), tmp.get_ziel()->get_basis_pos());
									const uint32 destination_transfer_time = ((destination_distance_to_stop * transfer_journey_time_factor) / 100) + tmp.get_ziel()->get_transshipment_time();
									current_journey_time += destination_transfer_time;
								}

								if (current_journey_time < best_journey_time)
								{
									best_journey_time = current_journey_time;
								}
							}
						}

						if(best_journey_time < UINT32_MAX_VALUE && (best_journey_time > longest_lead_time || longest_lead_time == UINT32_MAX_VALUE))
						{
							longest_lead_time = best_journey_time;
						}
						break;
					}
				}
			}
			break;
		}
	}

	return longest_lead_time;
}

uint32 fabrik_t::get_monthly_pax_demand() const
{
	return (scaled_pax_demand * 100) / welt->get_settings().get_job_replenishment_per_hundredths_of_months();
}

void fabrik_t::set_sector()
{
	if (get_desc()->is_electricity_producer()) {
		sector = power_plant;
	}
	else if (get_desc()->get_placement() == factory_desc_t::Water) {
		sector = marine_resource;
	}
	else if (input.empty() && !output.empty()) {
		sector = city ? resource_city : resource;
	}
	else if (!input.empty() && !output.empty()) {
		sector = manufacturing;
	}
	else if (!input.empty() && output.empty()) {
		sector = end_consumer;
	}else{
		// both input and output enmpty, but not a power_plant
		sector = unknown;
	}
}

bool fabrik_t::is_staff_shortage() const
{
	if ( !(welt->get_settings().get_factory_enforce_demand()) ) {
		return false;
	}
	const sint32 staffing_level_percentage = building->get_staffing_level_percentage();
	switch (sector) {
		//TODO: when power_plant or unknown ?
		case marine_resource:
			return false;
		case resource:
			if (welt->get_settings().get_rural_industries_no_staff_shortage()) {
				return false;
			}
			else {
				if (staffing_level_percentage < welt->get_settings().get_minimum_staffing_percentage_full_production_producer_industry()) {
					return true;
				}
			}
			break;
		case resource_city:
		case manufacturing:
			if (staffing_level_percentage < welt->get_settings().get_minimum_staffing_percentage_full_production_producer_industry()) {
				return true;
			}
			break;
		case end_consumer:
			if (staffing_level_percentage < welt->get_settings().get_minimum_staffing_percentage_consumer_industry()) {
				return true;
			}
			break;

		default:
			break;
	}
	return false;
}

sint32 fabrik_t::get_staffing_level_percentage() const {
	gebaeude_t* gb = get_fab(pos.get_2d())->get_building();
	return gb->get_staffing_level_percentage();
}

bool fabrik_t::is_connected_to_network(player_t *player) const
{
	FOR(vector_tpl<nearby_halt_t>, const i, nearby_freight_halts)
	{
		if(i.halt->get_owner() == player){
			// In the case of owning station, it only needs to have freight facilities.
			// It may still be in preparation...
			return true;
		}
		else {
			for (uint8 catg_index = goods_manager_t::INDEX_NONE+1; catg_index < goods_manager_t::get_max_catg_index(); catg_index++)
			{
				if (i.halt->has_available_network(player, catg_index)) {
					return true;
				}
			}
		}
	}
	return false;
}

bool fabrik_t::has_goods_catg_demand(uint8 catg_index, uint8 check_option) const
{
	if (!output.empty() && check_option!=1) {
		for (uint32 index = 0; index < output.get_count(); index++) {
			if (output[index].get_typ()->get_catg_index() == catg_index) {
				return true;
			}
		}
	}

	if (!input.empty() && check_option != 2) {
		for (uint32 index = 0; index < input.get_count(); index++) {
			if (!desc->get_supplier(index))
			{
				continue;
			}
			if (input[index].get_typ()->get_catg_index() == catg_index) {
				return true;
			}
		}
	}
	return false;
}

// NOTE: not added the formula for this month yet
// TODO: (for this month calcuration) Multiply the production amount and the basic production amount by the ratio to the length of the month time
uint32 fabrik_t::calc_operation_rate(sint8 month) const
{
	if (month < 0 || month >= MAX_MONTH) {
		return 0;
	}
	sint64 base_monthly_prod = welt->calc_adjusted_monthly_figure(prodbase*10); // need to consider the decimal point for small units
	if (month == 0) {
		const uint32 ticks_this_month = welt->get_ticks() % welt->ticks_per_world_month;
		base_monthly_prod = base_monthly_prod * ticks_this_month / welt->ticks_per_world_month;
	}
	if (!base_monthly_prod) { return 0; }

	sint32 temp_sum = 0;
	// which sector?
	if ((sector == ftype::end_consumer || sector == ftype::power_plant) && !input.empty()) {
		// check the consuming rate
		for (uint32 i = 0; i < input.get_count(); i++) {
			const sint64 pfactor = desc->get_supplier(i) ? (sint64)desc->get_supplier(i)->get_consumption() : 1ll;
			const uint32 prod_rate = (uint32)((FAB_PRODFACT_UNIT_HALF + (sint32)pfactor * 100) >> DEFAULT_PRODUCTION_FACTOR_BITS);
			temp_sum += convert_goods(input[i].get_stat(month, FAB_GOODS_CONSUMED)) * 10000 / (sint32)prod_rate;
		}
		return (uint32)(temp_sum * 1000 / input.get_count() / base_monthly_prod);
	}
	else if(sector != ftype::unknown && !output.empty()){
		// check the producing rate of this factory
		for (uint32 i = 0; i < output.get_count(); i++) {
			const sint64 pfactor = (sint64)desc->get_product(i)->get_factor();
			const uint32 prod_rate = (uint32)((FAB_PRODFACT_UNIT_HALF + (sint32)pfactor * 100) >> DEFAULT_PRODUCTION_FACTOR_BITS);

			temp_sum += convert_goods(output[i].get_stat(month, FAB_GOODS_PRODUCED)) * 10000 / (sint32)prod_rate;
		}
		return (uint32)(temp_sum * 1000 / output.get_count() / base_monthly_prod);
	}

	return 0;
}
