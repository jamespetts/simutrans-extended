/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "path_explorer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tpl/slist_tpl.h"
#include "dataobj/translator.h"
#include "bauer/goods_manager.h"
#include "descriptor/goods_desc.h"
#include "sys/simsys.h"
#include "display/simgraph.h"
#include "player/simplay.h"
#include "dataobj/environment.h"
#include "dataobj/schedule.h"
#include "simconvoi.h"
#include "simloadingscreen.h"


// #define DEBUG_EXPLORER_SPEED
// #define DEBUG_COMPARTMENT_STEP


///////////////////////////////////////////////

// path_explorer_t

karte_t *path_explorer_t::world = NULL;
uint8 path_explorer_t::max_categories = 0;
uint8 path_explorer_t::max_classes = 0;
uint8 path_explorer_t::category_empty = 255;
path_explorer_t::compartment_t **path_explorer_t::goods_compartment = NULL;
uint8 path_explorer_t::current_compartment_category = 0;
uint8 path_explorer_t::current_compartment_class = 0;
bool path_explorer_t::processing = false;
uint32 path_explorer_t::compartment_t::time_midpoint;
uint32 path_explorer_t::compartment_t::time_lower_limit;
uint32 path_explorer_t::compartment_t::time_upper_limit;
uint32 path_explorer_t::compartment_t::time_threshold;
bool path_explorer_t::must_refresh_on_loading;

#ifdef MULTI_THREAD
bool thread_local path_explorer_t::allow_path_explorer_on_this_thread = false;

pthread_mutex_t path_explorer_t::compartment_t::pool_mutex;
pthread_mutex_t path_explorer_t::compartment_t::pool_work_mutex;
pthread_cond_t path_explorer_t::compartment_t::pool_start_cond;
pthread_cond_t path_explorer_t::compartment_t::pool_done_cond;
path_explorer_t::compartment_t::pool_batch_t path_explorer_t::compartment_t::pool_batch;
uint32 path_explorer_t::compartment_t::pool_thread_count = 1;
uint32 path_explorer_t::compartment_t::pool_generation = 0;
uint32 path_explorer_t::compartment_t::pool_outstanding = 0;
uint32 path_explorer_t::compartment_t::pool_next_origin = 0;
uint32 path_explorer_t::compartment_t::pool_end_origin = 0;
bool path_explorer_t::compartment_t::pool_terminating = false;
bool path_explorer_t::compartment_t::pool_initialised = false;
#endif

void path_explorer_t::initialise(karte_t *welt)
{
	if (welt)
	{
		world = welt;
	}
	compartment_t::set_absolute_limits();
	max_categories = goods_manager_t::get_max_catg_index();
	max_classes = max(goods_manager_t::passengers->get_number_of_classes(), goods_manager_t::mail->get_number_of_classes());
	category_empty = goods_manager_t::none->get_catg_index();
	// See here for an explanation of the below: http://stackoverflow.com/questions/29375797/copy-2d-array-using-memcpy/29375830#29375830
	// This is not a true 2d array as the number of categories is not fixed.
	goods_compartment = new compartment_t*[max_categories];
	goods_compartment[0] = new compartment_t[max_categories * max_classes];
	for (uint8 i = 1; i < max_categories; i++)
	{
		goods_compartment[i] = goods_compartment[i - 1] + max_classes;
	}

	for (uint8 i = 0; i < max_categories; ++i)
	{
		for (uint8 j = 0; j < max_classes; j++)
		{
			goods_compartment[i][j].set_category(i);
			goods_compartment[i][j].set_class(j);
		}
	}

	current_compartment_category = 0;
	current_compartment_class = 0;
	processing = false;

	compartment_t::initialise();
}


void path_explorer_t::finalise()
{
	// See here for an explanation of the below: http://stackoverflow.com/questions/29375797/copy-2d-array-using-memcpy/29375830#29375830
	// This is not a true 2d array as the number of categories is not fixed.
	delete[] goods_compartment[0];
	delete[] goods_compartment;
	goods_compartment = NULL;
	current_compartment_category = 0;
	current_compartment_class = 0;
	category_empty = 255;
	max_categories = 0;
	max_classes = 0;
	processing = false;

	compartment_t::finalise();
}

void path_explorer_t::rdwr(loadsave_t* file)
{
	if (file->get_extended_version() < 14 || (file->get_extended_version() == 14 && file->get_extended_revision() < 10))
	{
		// Iterate through the compartments and load/save these
		for (uint8 ca = 0; ca < max_categories; ++ca)
		{
			for (uint8 cl = 0; cl < max_classes; ++cl)
			{
				if (ca != category_empty)
				{
					goods_compartment[ca][cl].rdwr(file);
				}
			}
		}
	}
	else
	{
		uint8 file_max_categories = max_categories;
		file->rdwr_byte(file_max_categories);

		uint8 file_passenger_classes = goods_manager_t::passengers->get_number_of_classes();
		file->rdwr_byte(file_passenger_classes);
		for (uint8 cl = 0; cl < file_passenger_classes; cl++)
		{
			if (cl < max_classes)
			{
				goods_compartment[goods_manager_t::INDEX_PAS][cl].rdwr(file);
			}
			else
			{
				compartment_t* dummy_goods_compartment = new compartment_t;
				dummy_goods_compartment->rdwr(file);
				delete dummy_goods_compartment;
			}
		}

		uint8 file_mail_classes = goods_manager_t::mail->get_number_of_classes();
		file->rdwr_byte(file_mail_classes);
		for (uint8 cl = 0; cl < file_mail_classes; cl++)
		{
			if (cl < max_classes)
			{
				goods_compartment[goods_manager_t::INDEX_MAIL][cl].rdwr(file);
			}
			else
			{
				compartment_t* dummy_goods_compartment = new compartment_t;
				dummy_goods_compartment->rdwr(file);
				delete dummy_goods_compartment;
			}
		}

		for (uint8 ca = 0; ca < file_max_categories; ca++)
		{
			if (ca != category_empty && ca != goods_manager_t::INDEX_PAS && ca != goods_manager_t::INDEX_MAIL)
			{
				if (ca < max_categories)
				{
					goods_compartment[ca][0].rdwr(file);
				}
				else
				{
					compartment_t* dummy_goods_compartment = new compartment_t;
					dummy_goods_compartment->rdwr(file);
					delete dummy_goods_compartment;
				}
			}
		}

		file->rdwr_byte(current_compartment_category);
		file->rdwr_byte(current_compartment_class);
	}

	// Load/save the connexion_list, which is static
	uint32 connexion_list_size = 65536;
	if (file->get_extended_version() < 14 || (file->get_extended_version() == 14 && file->get_extended_revision() < 11))
	{
		// Wrong number was used in original code
		connexion_list_size = 63336;
	}

	for (uint32 i = 0; i < connexion_list_size; ++i)
	{
		file->rdwr_byte(compartment_t::connexion_list[i].serving_transport);

		if (file->is_saving())
		{
			uint16 tmp_idx;

			uint32 tmp_journey_time;
			uint32 tmp_waiting_time;
			uint32 tmp_transfer_time;
			uint16 tmp_best_line_idx;
			uint16 tmp_best_convoy_idx;
			uint16 tmp_alternative_seats;
			// TODO: Consider whether to add comfort.
			// Probably the best thing to do with comfort is have it modify the journey, waiting and transfer times:
			// these would then be weights instead of times and be a composite value if comfort influenced routing
			// were enabled.

			uint32 connexion_table_count = compartment_t::connexion_list[i].connexion_table->get_count();
			file->rdwr_long(connexion_table_count);

			for (const haltestelle_t::connexion &cnx : *compartment_t::connexion_list[i].connexion_table)
			{
				tmp_idx = cnx.halt_id;
				file->rdwr_short(tmp_idx);

				tmp_journey_time = cnx.journey_time;
				tmp_waiting_time = cnx.waiting_time;
				tmp_transfer_time = cnx.transfer_time;
				tmp_best_line_idx = cnx.best_line.get_id();
				tmp_best_convoy_idx = cnx.best_convoy.get_id();
				tmp_alternative_seats = cnx.alternative_seats;

				file->rdwr_long(tmp_journey_time);
				file->rdwr_long(tmp_waiting_time);
				file->rdwr_long(tmp_transfer_time);
				file->rdwr_short(tmp_best_line_idx);
				file->rdwr_short(tmp_best_convoy_idx);
				file->rdwr_short(tmp_alternative_seats);
			}
		}

		if (file->is_loading())
		{
			uint32 connexion_table_count;
			file->rdwr_long(connexion_table_count);

			halthandle_t tmp_halt;

			for (uint32 j = 0; j < connexion_table_count; j++)
			{
				uint16 tmp_idx;
				file->rdwr_short(tmp_idx);
				tmp_halt.set_id(tmp_idx);

				haltestelle_t::connexion tmp_cnx;

				uint32 tmp_journey_time;
				uint32 tmp_waiting_time;
				uint32 tmp_transfer_time;
				uint16 tmp_best_line_idx;
				uint16 tmp_best_convoy_idx;
				uint16 tmp_alternative_seats;
				// TODO: Consider whether to add comfort

				file->rdwr_long(tmp_journey_time);
				file->rdwr_long(tmp_waiting_time);
				file->rdwr_long(tmp_transfer_time);
				file->rdwr_short(tmp_best_line_idx);
				file->rdwr_short(tmp_best_convoy_idx);
				file->rdwr_short(tmp_alternative_seats);

				tmp_cnx.journey_time = tmp_journey_time;
				tmp_cnx.waiting_time = tmp_waiting_time;
				tmp_cnx.transfer_time = tmp_transfer_time;
				tmp_cnx.best_line.set_id(tmp_best_line_idx);
				tmp_cnx.best_convoy.set_id(tmp_best_convoy_idx);
				tmp_cnx.alternative_seats = tmp_alternative_seats;

				compartment_t::connexion_list[i].connexion_table->put(tmp_halt, tmp_cnx);
			}
		}
	}
}

void path_explorer_t::step()
{
#ifdef MULTI_THREAD
	if (!allow_path_explorer_on_this_thread)
	{
		return;
	}
#endif
	// at most check all goods categories once
	const uint8 max_runs = (max_categories - 2) + goods_manager_t::passengers->get_number_of_classes() + goods_manager_t::mail->get_number_of_classes();
	for (uint8 i = 0; i < max_runs; ++i)
	{
		if ( current_compartment_category != category_empty
			 && (!goods_compartment[current_compartment_category][current_compartment_class].is_refresh_completed()
			     || goods_compartment[current_compartment_category][current_compartment_class].is_refresh_requested() ) )
		{
			processing = true;	// this step performs something
			// perform step
			goods_compartment[current_compartment_category][current_compartment_class].step();

			// if refresh is completed, move on to the next category or class as appropriate
			if ( goods_compartment[current_compartment_category][current_compartment_class].is_refresh_completed() )
			{
				next_compartment();
			}
			// each step process at most 1 goods category
			return;
		}

		// advance to the next category or class only if compartment.step() is not invoked
		next_compartment();
	}

	processing = false;	// this step performs nothing
}

void path_explorer_t::next_compartment()
{
	if (current_compartment_class < goods_manager_t::get_classes_catg_index(current_compartment_category) - 1)
	{
		current_compartment_class += 1;
	}
	else
	{
		current_compartment_class = 0;
		current_compartment_category = (current_compartment_category + 1) % max_categories;
	}
}


void path_explorer_t::full_instant_refresh()
{
#ifdef MULTI_THREAD
	path_explorer_t::allow_path_explorer_on_this_thread = true;
#endif
	uint16 curr_step = 0;
	// exclude empty goods (none)
	uint16 total_steps = (max_categories - 1) * max_classes * 6;

	processing = true;

	// disable the iteration limits
	compartment_t::enable_limits(false);

	// clear all connexion hash tables and reset serving transport counters
	compartment_t::reset_connexion_list();

#ifdef DEBUG_EXPLORER_SPEED
	unsigned long start, diff;
	start = dr_time();
#endif

	// initialize progress bar
	loadingscreen_t ls(translator::translate("Calculating paths ..."), total_steps, true, true);
	ls.set_progress(curr_step);

	for (uint8 ca = 0; ca < max_categories; ++ca)
	{
		for(uint8 cl = 0; cl < goods_manager_t::get_classes_catg_index(ca); ++cl)
		{
			if (ca != category_empty)
			{
				// clear any previous leftovers
				goods_compartment[ca][cl].reset(true);

#ifndef DEBUG_EXPLORER_SPEED
				// Run the six refresh steps.
				for (uint8 p = 0; p < 6; ++p)
				{
					// perform step
					goods_compartment[ca][cl].step();
					++curr_step;
					ls.set_progress(curr_step);
				}
#else
				// One step runs the refresh from the first step through the
				// routing search.
				goods_compartment[c].step();
				curr_step += 6;
				ls.set_progress(curr_step);
#endif
			}
		}
	}

#ifdef DEBUG_EXPLORER_SPEED
	diff = dr_time() - start;
	printf("\n\nTotal time taken :  %lu ms \n", diff);
#endif

	// enable iteration limits again
	compartment_t::enable_limits(true);

	// reset current category pointer
	current_compartment_category = 0;
	current_compartment_class = 0;

	processing = false;
#ifdef MULTI_THREAD
	path_explorer_t::allow_path_explorer_on_this_thread = false;
#endif
}




void path_explorer_t::refresh_all_categories(const bool reset_working_set)
{
	if (reset_working_set)
	{
		for (uint8 ca = 0; ca < max_categories; ++ca)
		{
			for (uint8 cl = 0; cl < max_classes; ++cl)
			{
				// do not remove the finished matrix and halt index map
				goods_compartment[ca][cl].reset(false);
			}
		}

		// clear all connexion hash tables and reset serving transport counters
		compartment_t::reset_connexion_list();

		// reset current category pointer : refresh will start from passengers
		current_compartment_category = 0;
		current_compartment_class = 0;
	}
	else
	{
		for (uint8 ca = 0; ca < max_categories; ++ca)
		{
			for (uint8 cl = 0; cl < max_classes; ++cl)
			{
				// only set flag
				goods_compartment[ca][cl].set_refresh();
			}
		}
	}
}

void path_explorer_t::refresh_category(uint8 category)
{
#ifdef MULTI_THREAD
	world->await_path_explorer();
#endif

	uint8 number_of_classes = goods_manager_t::get_classes_catg_index(category);
	for (uint8 i = 0; i < number_of_classes; i++)
	{
		goods_compartment[category][i].set_refresh();
	}
}

void path_explorer_t::refresh_class_category(uint8 category, uint8 g_class)
{
	goods_compartment[category][g_class].set_refresh();
}

///////////////////////////////////////////////

// compartment_t

const char *const path_explorer_t::compartment_t::phase_name[] =
{
	"flag",
	"prepare",
	"rebuild",
	"filter",
	"matrix",
	"explore",
	"reroute"
};

path_explorer_t::compartment_t::connexion_list_entry_t path_explorer_t::compartment_t::connexion_list[65536];

bool path_explorer_t::compartment_t::use_limits = true;

uint32 path_explorer_t::compartment_t::limit_rebuild_connexions = default_rebuild_connexions;
uint32 path_explorer_t::compartment_t::limit_filter_eligible = default_filter_eligible;
uint32 path_explorer_t::compartment_t::limit_fill_matrix = default_fill_matrix;
uint64 path_explorer_t::compartment_t::limit_explore_paths = default_explore_paths;
uint32 path_explorer_t::compartment_t::limit_reroute_goods = default_reroute_goods;

uint32 path_explorer_t::compartment_t::local_rebuild_connexions = default_rebuild_connexions;
uint32 path_explorer_t::compartment_t::local_filter_eligible = default_filter_eligible;
uint32 path_explorer_t::compartment_t::local_fill_matrix = default_fill_matrix;
uint64 path_explorer_t::compartment_t::local_explore_paths = default_explore_paths;
uint32 path_explorer_t::compartment_t::local_reroute_goods = default_reroute_goods;

bool path_explorer_t::compartment_t::local_limits_changed = false;

uint16 path_explorer_t::compartment_t::representative_halt_count = 0;
uint8 path_explorer_t::compartment_t::representative_category = 0;

// The path matrices are stored as one flat block per field. A cell is addressed
// as row * count + column; the product is computed in size_t so that it cannot
// overflow a 32-bit int on large maps.
static inline size_t path_matrix_index(const uint16 row, const uint16 column, const uint16 count)
{
	return static_cast<size_t>(row) * static_cast<size_t>(count) + column;
}

static uint32 *new_time_matrix(const uint16 count)
{
	const size_t cells = static_cast<size_t>(count) * static_cast<size_t>(count);
	uint32 *times = new uint32[cells];
	memset(times, 0xFF, cells * sizeof(uint32));
	return times;
}

static uint16 *new_short_matrix(const uint16 count)
{
	const size_t cells = static_cast<size_t>(count) * static_cast<size_t>(count);
	return new uint16[cells]();
}

void path_explorer_t::compartment_t::run_origin_slice(const uint32 thread_index, const uint32 thread_count,
	const uint16 origin_begin, const uint32 origin_count)
{
	if ( thread_count == 0 || origin_count == 0 )
	{
		return;
	}

	const uint32 slice_begin = origin_begin + (uint32)( ( (uint64)thread_index * origin_count ) / thread_count );
	const uint32 slice_end = origin_begin + (uint32)( ( (uint64)( thread_index + 1 ) * origin_count ) / thread_count );

	for ( uint32 origin = slice_begin; origin < slice_end; ++origin )
	{
		run_origin( (uint16)origin, thread_index );
	}
}

#ifdef MULTI_THREAD
void path_explorer_t::compartment_t::run_origin_batch(const uint32 thread_index)
{
	for ( ;; )
	{
		pthread_mutex_lock(&pool_work_mutex);
		const bool available = pool_next_origin < pool_end_origin;
		const uint32 origin = available ? pool_next_origin++ : 0;
		pthread_mutex_unlock(&pool_work_mutex);
		if ( !available )
		{
			return;
		}
		run_origin( (uint16)origin, thread_index );
	}
}
#endif

void path_explorer_t::compartment_t::pool_run(compartment_t *compartment, const uint16 origin_begin, const uint32 origin_count)
{
#ifdef MULTI_THREAD
	if ( pool_initialised && pool_thread_count > 1 )
	{
		pool_batch_t batch;
		batch.compartment = compartment;
		batch.origin_count = origin_count;

		pthread_mutex_lock(&pool_mutex);
		pool_batch = batch;
		pool_outstanding = pool_thread_count - 1;
		++pool_generation;
		pthread_mutex_lock(&pool_work_mutex);
		pool_next_origin = origin_begin;
		pool_end_origin = (uint32)origin_begin + origin_count;
		pthread_mutex_unlock(&pool_work_mutex);
		pthread_cond_broadcast(&pool_start_cond);
		pthread_mutex_unlock(&pool_mutex);

		// The lead and the helpers claim origins from the batch until it is
		// exhausted.
		compartment->run_origin_batch(0);

		pthread_mutex_lock(&pool_mutex);
		while ( pool_outstanding != 0 )
		{
			pthread_cond_wait(&pool_done_cond, &pool_mutex);
		}
		// A helper that starts late must not process this batch again; clear the
		// stale pointers before releasing the lock.
		pool_batch.compartment = NULL;
		pthread_mutex_unlock(&pool_mutex);
		return;
	}
#endif
	compartment->run_origin_slice(0, 1, origin_begin, origin_count);
}

#ifdef MULTI_THREAD
void path_explorer_t::compartment_t::pool_initialise(const uint32 thread_count)
{
	if ( pool_initialised )
	{
		return;
	}

	pthread_mutex_init(&pool_mutex, NULL);
	pthread_mutex_init(&pool_work_mutex, NULL);
	pthread_cond_init(&pool_start_cond, NULL);
	pthread_cond_init(&pool_done_cond, NULL);

	pool_thread_count = thread_count == 0 ? 1 : thread_count;
	pool_generation = 0;
	pool_outstanding = 0;
	pool_next_origin = 0;
	pool_end_origin = 0;
	pool_terminating = false;
	pool_batch.compartment = NULL;
	pool_batch.origin_count = 0;
	pool_initialised = true;
}

void path_explorer_t::compartment_t::pool_shutdown()
{
	if ( !pool_initialised )
	{
		return;
	}

	pthread_mutex_lock(&pool_mutex);
	pool_terminating = true;
	pthread_cond_broadcast(&pool_start_cond);
	pthread_mutex_unlock(&pool_mutex);
}

void path_explorer_t::compartment_t::pool_destroy()
{
	if ( !pool_initialised )
	{
		return;
	}

	pthread_mutex_destroy(&pool_mutex);
	pthread_mutex_destroy(&pool_work_mutex);
	pthread_cond_destroy(&pool_start_cond);
	pthread_cond_destroy(&pool_done_cond);
	pool_initialised = false;
}

void path_explorer_t::compartment_t::pool_worker(const uint32 thread_index)
{
	uint32 helper_generation = 0;

	pthread_mutex_lock(&pool_mutex);
	while ( !pool_terminating )
	{
		while ( !pool_terminating && pool_generation == helper_generation )
		{
			pthread_cond_wait(&pool_start_cond, &pool_mutex);
		}
		if ( pool_terminating )
		{
			break;
		}

		helper_generation = pool_generation;
		const pool_batch_t batch = pool_batch;
		const bool worked = batch.compartment != NULL && batch.origin_count > 0;
		pthread_mutex_unlock(&pool_mutex);

		if ( worked )
		{
			batch.compartment->run_origin_batch(thread_index);
		}

		pthread_mutex_lock(&pool_mutex);
		// A helper that starts after a batch was completed sees the cleared batch
		// and must not consume the outstanding count of a later batch.
		if ( worked && pool_outstanding > 0 )
		{
			--pool_outstanding;
		}
		if ( pool_outstanding == 0 )
		{
			pthread_cond_broadcast(&pool_done_cond);
		}
	}
	pthread_mutex_unlock(&pool_mutex);
}

void path_explorer_t::initialise_thread_pool(const uint32 thread_count)
{
	compartment_t::pool_initialise(thread_count);
}

void path_explorer_t::shutdown_thread_pool()
{
	compartment_t::pool_shutdown();
}

void path_explorer_t::destroy_thread_pool()
{
	compartment_t::pool_destroy();
}

void path_explorer_t::worker_thread(const uint32 thread_index)
{
	allow_path_explorer_on_this_thread = true;
	compartment_t::pool_worker(thread_index);
}
#endif

path_explorer_t::compartment_t::compartment_t()
{
	refresh_start_time = 0;

	finished_times = NULL;
	finished_next_transfers = NULL;
	finished_halt_index_map = NULL;
	finished_halt_count = 0;

	working_times = NULL;
	working_next_transfers = NULL;
	transport_index_map = NULL;
	working_halt_index_map = NULL;
	working_halt_list = NULL;
	working_halt_count = 0;

	full_edges = NULL;
	full_edge_count = 0;
	out_offset = NULL;
	out_edge = NULL;
	out_edges = NULL;
	state_offset = NULL;
	state_halt = NULL;
	state_key = NULL;
	state_count = 0;
	thread_dist = NULL;
	thread_first_hop = NULL;
	heaps = NULL;
	thread_scratch_count = 0;

	all_halts_list = NULL;
	all_halts_count = 0;

	linkages = NULL;

	transfer_list = NULL;
	transfer_count = 0;

	catg = 255;
	g_class = 255;
	catg_name = NULL;
	class_name = NULL;
	step_count = 0;

	paths_available = false;
	refresh_completed = true;
	refresh_requested = true;

	current_phase = phase_check_flag;

	phase_counter = 0;
	iterations = 0;
	total_iterations = 0;

	origin_index = 0;

	statistic_duration = 0;
	statistic_iteration = 0;
}


path_explorer_t::compartment_t::~compartment_t()
{
	delete[] finished_times;
	delete[] finished_next_transfers;
	if (finished_halt_index_map)
	{
		delete[] finished_halt_index_map;
	}


	delete[] working_times;
	delete[] working_next_transfers;
	if (transport_index_map)
	{
		delete[] transport_index_map;
	}
	if (working_halt_index_map)
	{
		delete[] working_halt_index_map;
	}
	if (working_halt_list)
	{
		delete[] working_halt_list;
	}

	clear_search_graph();
	free_thread_scratch();


	if (all_halts_list)
	{
		delete[] all_halts_list;
	}

	if (linkages)
	{
		delete linkages;
	}

	if (transfer_list)
	{
		delete[] transfer_list;
	}

	if (class_name)
	{
		delete [] class_name;
	}
}


void path_explorer_t::compartment_t::reset(const bool reset_finished_set)
{
	refresh_start_time = 0;

	if (reset_finished_set)
	{
		delete[] finished_times;
		finished_times = NULL;
		delete[] finished_next_transfers;
		finished_next_transfers = NULL;
		if (finished_halt_index_map)
		{
			delete[] finished_halt_index_map;
			finished_halt_index_map = NULL;
		}
		finished_halt_count = 0;
	}


	delete[] working_times;
	working_times = NULL;
	delete[] working_next_transfers;
	working_next_transfers = NULL;
	full_connexions.clear();
	clear_search_graph();
	free_thread_scratch();
	if (transport_index_map)
	{
		delete[] transport_index_map;
		transport_index_map = NULL;
	}
	if (working_halt_index_map)
	{
		delete[] working_halt_index_map;
		working_halt_index_map = NULL;
	}
	if (working_halt_list)
	{
		delete[] working_halt_list;
		working_halt_list = NULL;
	}
	working_halt_count = 0;


	if (all_halts_list)
	{
		delete[] all_halts_list;
		all_halts_list = NULL;
	}
	all_halts_count = 0;


	if (linkages)
	{
		delete linkages;
		linkages = NULL;
	}


	if (transfer_list)
	{
		delete[] transfer_list;
		transfer_list = NULL;
	}
	transfer_count = 0;

#ifdef DEBUG_COMPARTMENT_STEP
	step_count = 0;
#endif

	if (reset_finished_set)
	{
		paths_available = false;
	}
	refresh_completed = true;
	refresh_requested = true;

	current_phase = phase_check_flag;

	phase_counter = 0;
	iterations = 0;
	total_iterations = 0;

	origin_index = 0;

	statistic_duration = 0;
	statistic_iteration = 0;
}


void path_explorer_t::compartment_t::initialise()
{
	initialise_connexion_list();
}


void path_explorer_t::compartment_t::finalise()
{
	finalise_connexion_list();
}

void path_explorer_t::compartment_t::set_absolute_limits()
{
	time_midpoint = get_world()->get_settings().get_path_explorer_time_midpoint();
	time_lower_limit = time_midpoint - time_deviation;
	time_upper_limit = time_midpoint + time_deviation;
	time_threshold = time_midpoint / 2;
}

// ---------------------------------------------------------------------------
// Through connexion generation across schedule couplings.
//
// These helpers run while rebuilding connexions, once per linkage, after the ordinary
// within-schedule connexions have been generated. They add direct connexions
// from stops upstream of a coupling entry on the source schedule to stops
// downstream of it on the destination schedule. The record is owned by the
// source linkage (the one the passenger or goods board at the origin); the
// record's time is the actual pair time recorded by the destination linkage
// when one exists, otherwise the sum of the consecutive leg values.
// ---------------------------------------------------------------------------

bool path_explorer_t::compartment_t::linkage_carries(const linkage_t &linkage) const
{
	if (linkage.line.is_bound())
	{
		return linkage.line->carries_this_category(catg) && (catg >= 2 || linkage.line->carries_this_or_lower_class(catg, g_class));
	}
	if (linkage.convoy.is_bound())
	{
		return linkage.convoy->carries_this_category(catg) && (catg >= 2 || linkage.convoy->carries_this_or_lower_class(catg, g_class));
	}
	return false;
}


uint32 path_explorer_t::compartment_t::get_linkage_average_speed(const linkage_t &linkage) const
{
	if (linkage.line.is_bound())
	{
		if (linkage.line->get_finance_history(1, LINE_AVERAGE_SPEED) > 0)
		{
			return (uint32)linkage.line->get_finance_history(1, LINE_AVERAGE_SPEED);
		}
		if (linkage.line->count_convoys() > 0)
		{
			return speed_to_kmh(linkage.line->get_convoy(0)->get_min_top_speed()) >> 1;
		}
		return 1;
	}
	if (linkage.convoy.is_bound())
	{
		if (linkage.convoy->get_finance_history(1, convoi_t::CONVOI_AVERAGE_SPEED) > 0)
		{
			return (uint32)linkage.convoy->get_finance_history(1, convoi_t::CONVOI_AVERAGE_SPEED);
		}
		return speed_to_kmh(linkage.convoy->get_min_top_speed()) >> 1;
	}
	return 1;
}


uint32 path_explorer_t::compartment_t::get_recorded_pair_time(const linkage_t &linkage, const halthandle_t from, const halthandle_t to) const
{
	if (!from.is_bound() || !to.is_bound())
	{
		return 0;
	}
	const id_pair pair(from.get_id(), to.get_id());
	average_tpl<uint32> *average = NULL;
	if (linkage.line.is_bound())
	{
		journey_times_map &times = linkage.line->get_average_journey_times();
		if (times.is_contained(pair))
		{
			average = times.access(pair);
		}
	}
	else if (linkage.convoy.is_bound())
	{
		journey_times_map &times = linkage.convoy->get_average_journey_times();
		if (times.is_contained(pair))
		{
			average = times.access(pair);
		}
	}
	return average != NULL && average->count > 0 ? average->reduce() : 0;
}


uint32 path_explorer_t::compartment_t::get_leg_time(const linkage_t &linkage, const schedule_entry_t &from, const schedule_entry_t &to, uint32 average_speed) const
{
	player_t *const owner = linkage.line.is_bound() ? linkage.line->get_owner() : (linkage.convoy.is_bound() ? linkage.convoy->get_owner() : NULL);
	if (owner == NULL)
	{
		return 0;
	}
	const halthandle_t from_halt = haltestelle_t::get_halt(from.pos, owner);
	const halthandle_t to_halt = haltestelle_t::get_halt(to.pos, owner);
	if (!from_halt.is_bound() || !to_halt.is_bound())
	{
		return 0;
	}
	uint32 journey_time = get_recorded_pair_time(linkage, from_halt, to_halt);
	if (journey_time == 0)
	{
		const uint32 distance = shortest_distance(from_halt->get_basis_pos(), to_halt->get_basis_pos());
		journey_time = world->travel_time_tenths_from_distance(distance, average_speed);
	}
	if (from.is_flag_set(schedule_entry_t::lay_over) ||
		(from.is_flag_set(schedule_entry_t::discharge_payload) && !from.is_flag_set(schedule_entry_t::set_down_only)))
	{
		journey_time += catg != goods_manager_t::passengers->get_catg_index() ? from_halt->get_transshipment_time() : from_halt->get_transfer_time();
	}
	return journey_time;
}


bool path_explorer_t::compartment_t::entry_carries(const schedule_t *schedule, const schedule_entry_t &entry, bool to, uint8 category, uint8 g_class) const
{
	if (schedule == NULL || !schedule->has_consist_orders())
	{
		return true;
	}
	const inthashtable_tpl<uint16, vector_tpl<uint8>, N_BAGS_SMALL> &catg_table = to ? schedule->get_catg_carried_to() : schedule->get_catg_carried_from();
	if (catg_table.empty() || !catg_table.is_contained(entry.unique_entry_id))
	{
		return true;
	}
	if (!catg_table.get(entry.unique_entry_id).is_contained(category))
	{
		return false;
	}
	if (category == goods_manager_t::passengers->get_catg_index())
	{
		const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> &min_class = to ? schedule->get_passenger_min_class_carried_to() : schedule->get_passenger_min_class_carried_from();
		return !min_class.is_contained(entry.unique_entry_id) || min_class.get(entry.unique_entry_id) <= g_class;
	}
	if (category == goods_manager_t::mail->get_catg_index())
	{
		const inthashtable_tpl<uint16, uint8, N_BAGS_SMALL> &min_class = to ? schedule->get_mail_min_class_carried_to() : schedule->get_mail_min_class_carried_from();
		return !min_class.is_contained(entry.unique_entry_id) || min_class.get(entry.unique_entry_id) <= g_class;
	}
	return true;
}


bool path_explorer_t::compartment_t::through_predicate_allows(const schedule_t *source_schedule, const schedule_entry_t &source_origin, const schedule_entry_t &source_coupling,
	const schedule_t *destination_schedule, const schedule_entry_t &destination_coupling, const schedule_entry_t &destination_entry) const
{
	if (source_origin.is_flag_set(schedule_entry_t::set_down_only) || destination_entry.is_flag_set(schedule_entry_t::pick_up_only))
	{
		return false;
	}
	if (source_coupling.is_flag_set(schedule_entry_t::lay_over) || source_coupling.is_flag_set(schedule_entry_t::discharge_payload) || source_coupling.is_flag_set(schedule_entry_t::force_range_stop))
	{
		return false;
	}
	if (destination_coupling.is_flag_set(schedule_entry_t::lay_over) || destination_coupling.is_flag_set(schedule_entry_t::discharge_payload) || destination_coupling.is_flag_set(schedule_entry_t::force_range_stop))
	{
		return false;
	}
	return entry_carries(source_schedule, source_origin, true, catg, g_class) &&
		entry_carries(source_schedule, source_coupling, true, catg, g_class) &&
		entry_carries(destination_schedule, destination_coupling, false, catg, g_class) &&
		entry_carries(destination_schedule, destination_entry, false, catg, g_class);
}


void path_explorer_t::compartment_t::collect_upstream_stops(const linkage_t &linkage, const schedule_t *schedule, uint8 entry_index, bool reverse, vector_tpl<through_stop_t> &out) const
{
	const uint32 average_speed = get_linkage_average_speed(linkage);
	const bool effective_reverse = reverse && schedule->is_mirrored();
	uint32 time = 0;
	if (!effective_reverse)
	{
		for (sint32 i = (sint32)entry_index - 1; i >= 0; --i)
		{
			time += get_leg_time(linkage, schedule->entries[(uint8)i], schedule->entries[(uint8)i + 1], average_speed);
			const halthandle_t halt = haltestelle_t::get_halt(schedule->entries[(uint8)i].pos, linkage.line.is_bound() ? linkage.line->get_owner() : linkage.convoy->get_owner());
			if (halt.is_bound())
			{
				through_stop_t stop;
				stop.halt = halt;
				stop.time = time;
				stop.linkage = linkage;
				stop.schedule = schedule;
				stop.entry_index = (uint8)i;
				stop.coupling_entry = NULL;
				out.append(stop);
			}
		}
	}
	else
	{
		const uint32 count = schedule->get_count();
		for (uint32 i = (uint32)entry_index + 1; i < count; ++i)
		{
			time += get_leg_time(linkage, schedule->entries[(uint8)i], schedule->entries[(uint8)i - 1], average_speed);
			const halthandle_t halt = haltestelle_t::get_halt(schedule->entries[(uint8)i].pos, linkage.line.is_bound() ? linkage.line->get_owner() : linkage.convoy->get_owner());
			if (halt.is_bound())
			{
				through_stop_t stop;
				stop.halt = halt;
				stop.time = time;
				stop.linkage = linkage;
				stop.schedule = schedule;
				stop.entry_index = (uint8)i;
				stop.coupling_entry = NULL;
				out.append(stop);
			}
		}
	}
}


void path_explorer_t::compartment_t::collect_downstream_stops(const linkage_t &linkage, const schedule_t *schedule, uint8 entry_index, bool reverse, uint32 base_time, uint8 depth, const schedule_entry_t *coupling_entry, vector_tpl<through_stop_t> &out) const
{
	const uint32 average_speed = get_linkage_average_speed(linkage);
	const sint32 max_depth = world->get_settings().get_max_through_coupling_depth();
	player_t *const owner = linkage.line.is_bound() ? linkage.line->get_owner() : (linkage.convoy.is_bound() ? linkage.convoy->get_owner() : NULL);
	if (owner == NULL)
	{
		return;
	}
	uint8 index = entry_index;
	bool rev = reverse && schedule->is_mirrored();
	uint32 time = base_time;
	for (uint32 step = 0; step < schedule->get_count(); ++step)
	{
		uint8 next = index;
		bool next_reverse = rev;
		schedule->increment_index(&next, &next_reverse);
		if (next_reverse != rev)
		{
			// Reached the turning point of a mirrored schedule.
			break;
		}
		if (rev ? next >= index : next <= index)
		{
			// Wrapped around the end of the schedule.
			break;
		}
		const schedule_entry_t &next_entry = schedule->entries[next];
		// A portion that forces discharge cannot continue as a through movement.
		if (next_entry.is_flag_set(schedule_entry_t::lay_over) || next_entry.is_flag_set(schedule_entry_t::discharge_payload) || next_entry.is_flag_set(schedule_entry_t::force_range_stop))
		{
			break;
		}
		time += get_leg_time(linkage, schedule->entries[index], next_entry, average_speed);
		const halthandle_t halt = haltestelle_t::get_halt(next_entry.pos, owner);
		if (halt.is_bound())
		{
			bool updated = false;
			for (uint32 i = 0; i < out.get_count(); ++i)
			{
				if (out[i].halt == halt)
				{
					if (time < out[i].time)
					{
						out[i].time = time;
						out[i].linkage = linkage;
						out[i].schedule = schedule;
						out[i].entry_index = next;
						out[i].coupling_entry = coupling_entry;
					}
					updated = true;
					break;
				}
			}
			if (!updated)
			{
				through_stop_t stop;
				stop.halt = halt;
				stop.time = time;
				stop.linkage = linkage;
				stop.schedule = schedule;
				stop.entry_index = next;
				stop.coupling_entry = coupling_entry;
				out.append(stop);
			}
		}
		if (next_entry.is_flag_set(schedule_entry_t::uncouple) && depth < (uint8)max_depth)
		{
			linehandle_t target_line;
			convoihandle_t target_convoy;
			if (next_entry.is_flag_set(schedule_entry_t::uncouple_target_is_line_or_cnv))
			{
				target_convoy.set_id(next_entry.target_id_uncouple);
				if (target_convoy.is_bound() && target_convoy->get_line().is_bound())
				{
					target_line = target_convoy->get_line();
				}
			}
			else
			{
				target_line.set_id(next_entry.target_id_uncouple);
			}
			schedule_t *const target_schedule = target_line.is_bound() ? target_line->get_schedule() : (target_convoy.is_bound() ? target_convoy->get_schedule() : NULL);
			if (target_schedule != NULL)
			{
				const uint8 target_entry = target_schedule->get_entry_from_unique_id(next_entry.target_unique_entry_uncouple);
				if (target_entry != 255)
				{
					linkage_t target_linkage;
					target_linkage.line = target_line;
					target_linkage.convoy = target_convoy;
					if (linkage_carries(target_linkage))
					{
						collect_downstream_stops(target_linkage, target_schedule, target_entry,
							next_entry.is_flag_set(schedule_entry_t::uncouple_target_sch_is_reversed), time, depth + 1,
							&target_schedule->entries[target_entry], out);
					}
				}
			}
		}
		index = next;
		rev = next_reverse;
	}
}


void path_explorer_t::compartment_t::add_through_connexions(const linkage_t &current_linkage, const schedule_t *schedule)
{
	if (world->get_settings().get_max_through_coupling_depth() <= 0 || schedule == NULL)
	{
		return;
	}
	bool has_coupling = false;
	for (uint8 i = 0; i < schedule->get_count() && !has_coupling; ++i)
	{
		has_coupling = schedule->entries[i].is_flag_set(schedule_entry_t::uncouple) || schedule->entries[i].is_flag_set(schedule_entry_t::couple);
	}
	if (!has_coupling)
	{
		return;
	}
	player_t *const current_owner = current_linkage.line.is_bound() ? current_linkage.line->get_owner() : (current_linkage.convoy.is_bound() ? current_linkage.convoy->get_owner() : NULL);
	if (current_owner == NULL)
	{
		return;
	}
	const uint32 count = schedule->get_count();
	const bool mirrored = schedule->is_mirrored();
	for (uint8 pass = 0; pass < (mirrored ? 2 : 1); ++pass)
	{
		const bool current_reverse = pass == 1;
		for (uint32 step = 0; step < count; ++step)
		{
			const uint8 entry_index = current_reverse ? (uint8)(count - 1 - step) : (uint8)step;
			const schedule_entry_t &entry = schedule->entries[entry_index];

			linkage_t source_linkage;
			const schedule_t *source_schedule = NULL;
			uint8 source_entry_index = 255;
			bool source_reverse = false;
			linkage_t destination_linkage;
			const schedule_t *destination_schedule = NULL;
			uint8 destination_entry_index = 255;
			bool destination_reverse = false;

			if (entry.is_flag_set(schedule_entry_t::uncouple))
			{
				if (entry.is_flag_set(schedule_entry_t::uncouple_target_is_line_or_cnv))
				{
					destination_linkage.convoy.set_id(entry.target_id_uncouple);
					if (destination_linkage.convoy.is_bound() && destination_linkage.convoy->get_line().is_bound())
					{
						destination_linkage.line = destination_linkage.convoy->get_line();
					}
				}
				else
				{
					destination_linkage.line.set_id(entry.target_id_uncouple);
				}
				destination_schedule = destination_linkage.line.is_bound() ? destination_linkage.line->get_schedule() : (destination_linkage.convoy.is_bound() ? destination_linkage.convoy->get_schedule() : NULL);
				if (destination_schedule == NULL)
				{
					continue;
				}
				destination_entry_index = destination_schedule->get_entry_from_unique_id(entry.target_unique_entry_uncouple);
				if (destination_entry_index == 255)
				{
					continue;
				}
				if (!linkage_carries(destination_linkage) ||
					(destination_linkage.line.is_bound() ? destination_linkage.line->count_convoys() == 0 : !destination_linkage.convoy.is_bound()))
				{
					continue;
				}
				player_t *const destination_owner = destination_linkage.line.is_bound() ? destination_linkage.line->get_owner() : destination_linkage.convoy->get_owner();
				const halthandle_t source_halt = haltestelle_t::get_halt(entry.pos, current_owner);
				const halthandle_t destination_halt = haltestelle_t::get_halt(destination_schedule->entries[destination_entry_index].pos, destination_owner);
				if (!source_halt.is_bound() || source_halt != destination_halt)
				{
					continue;
				}
				source_linkage = current_linkage;
				source_schedule = schedule;
				source_entry_index = entry_index;
				source_reverse = current_reverse;
				destination_reverse = entry.is_flag_set(schedule_entry_t::uncouple_target_sch_is_reversed);
			}
			else if (entry.is_flag_set(schedule_entry_t::couple))
			{
				if (entry.is_flag_set(schedule_entry_t::couple_target_is_line_or_cnv))
				{
					source_linkage.convoy.set_id(entry.target_id_couple);
					if (source_linkage.convoy.is_bound() && source_linkage.convoy->get_line().is_bound())
					{
						source_linkage.line = source_linkage.convoy->get_line();
					}
				}
				else
				{
					source_linkage.line.set_id(entry.target_id_couple);
				}
				source_schedule = source_linkage.line.is_bound() ? source_linkage.line->get_schedule() : (source_linkage.convoy.is_bound() ? source_linkage.convoy->get_schedule() : NULL);
				if (source_schedule == NULL)
				{
					continue;
				}
				// The source linkage must be in this compartment's linkage list:
				// it is the transport the through record is attributed to.
				const uint16 source_transport = source_linkage.line.is_bound() ? transport_index_map[source_linkage.line.get_id()] : transport_index_map[65536u + source_linkage.convoy.get_id()];
				if (source_transport == 0)
				{
					continue;
				}
				// Determine the source coupling entry. The explicit field wins;
				// otherwise use a paired uncouple entry; otherwise the single
				// visit of the stop in the source schedule.
				if (entry.target_unique_entry_couple != 0 && entry.target_unique_entry_couple != 65535)
				{
					source_entry_index = source_schedule->get_entry_from_unique_id(entry.target_unique_entry_couple);
				}
				bool paired = false;
				const uint16 current_unique_id = schedule->get_unique_id_from_entry(entry_index);
				const uint16 current_line_id = current_linkage.line.is_bound() ? current_linkage.line.get_id() : 0;
				const uint16 current_convoy_id = current_linkage.convoy.is_bound() ? current_linkage.convoy.get_id() : 0;
				for (uint8 i = 0; i < source_schedule->get_count() && !paired; ++i)
				{
					const schedule_entry_t &candidate = source_schedule->entries[i];
					if (candidate.pos != entry.pos || !candidate.is_flag_set(schedule_entry_t::uncouple))
					{
						continue;
					}
					bool target_matches = false;
					if (!candidate.is_flag_set(schedule_entry_t::uncouple_target_is_line_or_cnv))
					{
						target_matches = current_line_id != 0 && candidate.target_id_uncouple == current_line_id;
					}
					else
					{
						convoihandle_t candidate_convoy;
						candidate_convoy.set_id(candidate.target_id_uncouple);
						target_matches = candidate_convoy.is_bound() &&
							((current_convoy_id != 0 && candidate_convoy.get_id() == current_convoy_id) ||
								(current_line_id != 0 && candidate_convoy->get_line().is_bound() && candidate_convoy->get_line().get_id() == current_line_id));
					}
					if (target_matches &&
						(candidate.target_unique_entry_uncouple == 0 || candidate.target_unique_entry_uncouple == 65535 || candidate.target_unique_entry_uncouple == current_unique_id))
					{
						paired = true;
						if (source_entry_index == 255)
						{
							source_entry_index = i;
						}
					}
				}
				if (paired)
				{
					// The paired uncouple entry generates the same pair; do not
					// generate it again from the receiving side.
					continue;
				}
				if (source_entry_index == 255)
				{
					// Fall back to a unique visit of the stop in the expanded
					// source schedule. Count the visits in both directions.
					uint8 visit = 255;
					uint8 visits = 0;
					for (uint8 pass2 = 0; pass2 < (source_schedule->is_mirrored() ? 2 : 1); ++pass2)
					{
						const bool rev2 = pass2 == 1;
						for (uint8 i = 0; i < source_schedule->get_count(); ++i)
						{
							const uint8 index2 = rev2 ? (uint8)(source_schedule->get_count() - 1 - i) : i;
							if (source_schedule->entries[index2].pos == entry.pos)
							{
								visit = index2;
								++visits;
							}
						}
					}
					if (visits != 1)
					{
						continue;
					}
					source_entry_index = visit;
				}
				if (source_entry_index == 255)
				{
					continue;
				}
				player_t *const source_owner = source_linkage.line.is_bound() ? source_linkage.line->get_owner() : source_linkage.convoy->get_owner();
				const halthandle_t source_halt = haltestelle_t::get_halt(source_schedule->entries[source_entry_index].pos, source_owner);
				const halthandle_t destination_halt = haltestelle_t::get_halt(entry.pos, current_owner);
				if (!source_halt.is_bound() || source_halt != destination_halt)
				{
					continue;
				}
				source_reverse = entry.is_flag_set(schedule_entry_t::couple_target_sch_is_reversed);
				destination_linkage = current_linkage;
				destination_schedule = schedule;
				destination_entry_index = entry_index;
				destination_reverse = current_reverse;
			}
			else
			{
				continue;
			}

			vector_tpl<through_stop_t> upstream(8);
			vector_tpl<through_stop_t> downstream(8);
			collect_upstream_stops(source_linkage, source_schedule, source_entry_index, source_reverse, upstream);
			collect_downstream_stops(destination_linkage, destination_schedule, destination_entry_index, destination_reverse, 0, 1,
				&destination_schedule->entries[destination_entry_index], downstream);
			if (upstream.get_count() == 0 || downstream.get_count() == 0)
			{
				continue;
			}

			const schedule_entry_t &source_coupling = source_schedule->entries[source_entry_index];
			for (uint32 u = 0; u < upstream.get_count(); ++u)
			{
				const through_stop_t &origin = upstream[u];
				haltestelle_t::connexions_map *const table = connexion_list[origin.halt.get_id()].connexion_table;
				if (table == NULL)
				{
					continue;
				}
				for (uint32 d = 0; d < downstream.get_count(); ++d)
				{
					const through_stop_t &destination = downstream[d];
					if (origin.halt == destination.halt)
					{
						continue;
					}
					if (!through_predicate_allows(origin.schedule, origin.schedule->entries[origin.entry_index], source_coupling,
						destination.schedule, *destination.coupling_entry, destination.schedule->entries[destination.entry_index]))
					{
						continue;
					}
					haltestelle_t::connexion new_connexion;
					new_connexion.waiting_time = origin.halt->get_average_waiting_time(destination.halt, catg, g_class);
					new_connexion.transfer_time = catg != goods_manager_t::passengers->get_catg_index() ? origin.halt->get_transshipment_time() : origin.halt->get_transfer_time();
					new_connexion.journey_time = get_recorded_pair_time(destination.linkage, origin.halt, destination.halt);
					if (new_connexion.journey_time == 0)
					{
						new_connexion.journey_time = origin.time + destination.time;
					}
					new_connexion.best_line = source_linkage.line;
					new_connexion.best_convoy = source_linkage.convoy;
					new_connexion.alternative_seats = 0;
					add_full_connexion(origin.halt, destination.halt, new_connexion);
					haltestelle_t::connexion *existing_connexion = table->get(destination.halt);
					if (existing_connexion == NULL)
					{
						table->put(destination.halt, new_connexion);
						origin.halt->prepare_goods_list(catg);
					}
					else if (get_connexion_weight(*existing_connexion) > get_connexion_weight(new_connexion))
					{
						new_connexion.alternative_seats = existing_connexion->alternative_seats;
						table->set(destination.halt, new_connexion);
					}
				}
			}
		}
	}
}


uint32 path_explorer_t::compartment_t::get_connexion_weight(const haltestelle_t::connexion &cnx)
{
	const uint64 weight = static_cast<uint64>(cnx.waiting_time)
		+ static_cast<uint64>(cnx.journey_time) + static_cast<uint64>(cnx.transfer_time);
	return weight > static_cast<uint64>(UINT32_MAX_VALUE) ? UINT32_MAX_VALUE : static_cast<uint32>(weight);
}


// Append one stop-to-stop candidate to the full edge list used by the
// routing-search graph.
void path_explorer_t::compartment_t::add_full_connexion(const halthandle_t origin, const halthandle_t target,
	const haltestelle_t::connexion &cnx)
{
	if ( ! origin.is_bound() || ! target.is_bound() )
	{
		return;
	}

	full_connexion_t fc;
	fc.origin_halt_id = origin.get_id();
	fc.target_halt_id = target.get_id();
	if ( cnx.best_line.is_bound() && transport_index_map != NULL )
	{
		fc.transport_idx = transport_index_map[ cnx.best_line.get_id() ];
	}
	else if ( cnx.best_convoy.is_bound() && transport_index_map != NULL )
	{
		fc.transport_idx = transport_index_map[ 65536u + cnx.best_convoy.get_id() ];
	}
	else
	{
		fc.transport_idx = 0;
	}

	fc.weight = get_connexion_weight(cnx);
	full_connexions.append(fc);
}


void path_explorer_t::compartment_t::clear_search_graph()
{
	delete[] full_edges;
	full_edges = NULL;
	delete[] out_offset;
	out_offset = NULL;
	delete[] out_edge;
	out_edge = NULL;
	delete[] out_edges;
	out_edges = NULL;
	delete[] state_offset;
	state_offset = NULL;
	delete[] state_halt;
	state_halt = NULL;
	delete[] state_key;
	state_key = NULL;
	full_edge_count = 0;
	state_count = 0;
}


void path_explorer_t::compartment_t::free_thread_scratch()
{
	delete[] thread_dist;
	thread_dist = NULL;
	delete[] thread_first_hop;
	thread_first_hop = NULL;
	delete[] heaps;
	heaps = NULL;
	thread_scratch_count = 0;
}


// Build the compact routing-search graph and the expanded state space from the
// full stop-to-stop candidates. Called before the search.
void path_explorer_t::compartment_t::build_search_graph()
{
	clear_search_graph();

	const uint32 H = working_halt_count;
	if ( H == 0 )
	{
		return;
	}

	delete[] working_times;
	working_times = NULL;
	delete[] working_next_transfers;
	working_next_transfers = NULL;
	working_times = new_time_matrix(working_halt_count);
	working_next_transfers = new_short_matrix(working_halt_count);
	for ( uint32 i = 0; i < H; ++i )
	{
		working_times[ path_matrix_index(i, i, H) ] = 0;
	}

	const uint32 fc_count = full_connexions.get_count();

	// Count the candidates whose endpoints are both in the eligible set.
	uint32 edge_count = 0;
	for ( uint32 i = 0; i < fc_count; ++i )
	{
		const full_connexion_t &fc = full_connexions[i];
		const uint16 origin = working_halt_index_map[ fc.origin_halt_id ];
		const uint16 target = working_halt_index_map[ fc.target_halt_id ];
		if ( origin == 0xFFFFu || target == 0xFFFFu || origin == target )
		{
			continue;
		}
		++edge_count;
	}

	full_edge_count = edge_count;
	full_edges = new full_edge_t[ edge_count > 0 ? edge_count : 1 ];
	out_offset = new uint32[ H + 1 ];
	out_edge = new uint32[ edge_count > 0 ? edge_count : 1 ];
	for ( uint32 i = 0; i <= H; ++i )
	{
		out_offset[i] = 0;
	}
	for ( uint32 i = 0; i < fc_count; ++i )
	{
		const full_connexion_t &fc = full_connexions[i];
		const uint16 origin = working_halt_index_map[ fc.origin_halt_id ];
		const uint16 target = working_halt_index_map[ fc.target_halt_id ];
		if ( origin == 0xFFFFu || target == 0xFFFFu || origin == target )
		{
			continue;
		}
		++out_offset[ origin + 1 ];
	}
	for ( uint32 h = 0; h < H; ++h )
	{
		out_offset[ h + 1 ] += out_offset[h];
	}

	uint32 *cursor = new uint32[H];
	for ( uint32 h = 0; h < H; ++h )
	{
		cursor[h] = out_offset[h];
	}
	uint32 e = 0;
	for ( uint32 i = 0; i < fc_count; ++i )
	{
		const full_connexion_t &fc = full_connexions[i];
		const uint16 origin = working_halt_index_map[ fc.origin_halt_id ];
		const uint16 target = working_halt_index_map[ fc.target_halt_id ];
		if ( origin == 0xFFFFu || target == 0xFFFFu || origin == target )
		{
			continue;
		}
		full_edges[e].target = target;
		full_edges[e].transport = fc.transport_idx;
		full_edges[e].weight = fc.weight;
		full_edges[e].target_state = 0xFFFFFFFFu;
		out_edge[ cursor[origin]++ ] = e;
		++e;
	}
	delete[] cursor;

	// Distinct incoming transport keys per target halt.
	uint32 *key_offset = new uint32[ H + 1 ]();
	for ( uint32 i = 0; i < edge_count; ++i )
	{
		++key_offset[ full_edges[i].target + 1 ];
	}
	for ( uint32 h = 0; h < H; ++h )
	{
		key_offset[ h + 1 ] += key_offset[h];
	}
	uint32 *keys = new uint32[ edge_count > 0 ? edge_count : 1 ];
	uint32 *key_cursor = new uint32[H];
	for ( uint32 h = 0; h < H; ++h )
	{
		key_cursor[h] = key_offset[h];
	}
	for ( uint32 i = 0; i < edge_count; ++i )
	{
		keys[ key_cursor[ full_edges[i].target ]++ ] = full_edges[i].transport;
	}
	delete[] key_cursor;

	uint32 *unique_keys = new uint32[ edge_count > 0 ? edge_count : 1 ];
	uint32 *unique_offset = new uint32[ H + 1 ];
	uint32 unique_count = 0;
	for ( uint32 h = 0; h < H; ++h )
	{
		unique_offset[h] = unique_count;
		const uint32 begin = key_offset[h];
		const uint32 end = key_offset[ h + 1 ];
		for ( uint32 i = begin + 1; i < end; ++i )
		{
			const uint32 value = keys[i];
			uint32 j = i;
			while ( j > begin && keys[ j - 1 ] > value )
			{
				keys[j] = keys[ j - 1 ];
				--j;
			}
			keys[j] = value;
		}
		for ( uint32 i = begin; i < end; ++i )
		{
			if ( i == begin || keys[i] != keys[ i - 1 ] )
			{
				unique_keys[ unique_count++ ] = keys[i];
			}
		}
	}
	unique_offset[H] = unique_count;
	delete[] key_offset;
	delete[] keys;

	state_count = H + unique_count;
	state_offset = new uint32[ H + 1 ];
	state_offset[0] = 0;
	for ( uint32 h = 0; h < H; ++h )
	{
		state_offset[ h + 1 ] = state_offset[h] + 1 + ( unique_offset[ h + 1 ] - unique_offset[h] );
	}
	state_halt = new uint16[ state_count > 0 ? state_count : 1 ];
	state_key = new uint32[ state_count > 0 ? state_count : 1 ];
	for ( uint32 h = 0; h < H; ++h )
	{
		uint32 s = state_offset[h];
		state_halt[s] = static_cast<uint16>(h);
		state_key[s] = 0xFFFFFFFFu;
		++s;
		for ( uint32 k = unique_offset[h]; k < unique_offset[ h + 1 ]; ++k )
		{
			state_halt[s] = static_cast<uint16>(h);
			state_key[s] = unique_keys[k];
			++s;
		}
	}

	// Resolve every edge's target state.
	for ( uint32 i = 0; i < edge_count; ++i )
	{
		const uint16 target = full_edges[i].target;
		const uint32 begin = unique_offset[target];
		const uint32 end = unique_offset[ target + 1 ];
		uint32 found = 0xFFFFFFFFu;
		for ( uint32 k = begin; k < end; ++k )
		{
			if ( unique_keys[k] == full_edges[i].transport )
			{
				found = k;
				break;
			}
		}
		if ( found != 0xFFFFFFFFu )
		{
			full_edges[i].target_state = state_offset[target] + 1 + ( found - begin );
		}
	}
	delete[] unique_keys;
	delete[] unique_offset;

	// Group the resolved edges by origin so that the search reads one
	// contiguous edge stream instead of an index array plus full_edges.
	out_edges = new full_edge_t[ edge_count > 0 ? edge_count : 1 ];
	for ( uint32 i = 0; i < edge_count; ++i )
	{
		out_edges[i] = full_edges[ out_edge[i] ];
	}
	delete[] full_edges;
	full_edges = NULL;
	delete[] out_edge;
	out_edge = NULL;

	// Transfer list, retained for the diagnostic counters and the checklist.
	if ( transfer_list == NULL )
	{
		transfer_list = new uint16[H];
	}
	transfer_count = 0;
	for ( uint32 i = 0; i < H; ++i )
	{
		if ( working_halt_list[i]->get_schedule_count(catg, g_class, max_classes) > 1 )
		{
			transfer_list[ transfer_count++ ] = static_cast<uint16>(i);
		}
	}

	// Per-thread Dijkstra scratch.
	free_thread_scratch();
	thread_scratch_count = 1;
#ifdef MULTI_THREAD
	if ( pool_initialised && pool_thread_count > 1 )
	{
		thread_scratch_count = pool_thread_count;
	}
#endif
	const uint32 state_size = state_count > 0 ? state_count : 1;
	thread_dist = new uint32[ static_cast<size_t>(thread_scratch_count) * state_size ];
	thread_first_hop = new uint16[ static_cast<size_t>(thread_scratch_count) * state_size ];
	heaps = new binary_heap_tpl<heap_item_t>[thread_scratch_count];
}


// One per-origin Dijkstra over the expanded state graph, writing one matrix
// row.
void path_explorer_t::compartment_t::run_origin(const uint16 origin, const uint32 thread_index)
{
	if ( thread_index >= thread_scratch_count || state_count == 0 )
	{
		return;
	}

	const uint32 S = state_count;
	uint32 *const dist = thread_dist + static_cast<size_t>(thread_index) * S;
	uint16 *const first_hop = thread_first_hop + static_cast<size_t>(thread_index) * S;
	binary_heap_tpl<heap_item_t> &heap = heaps[thread_index];

	heap.node_count = 0;
	for ( uint32 i = 0; i < S; ++i )
	{
		dist[i] = UINT32_MAX_VALUE;
		first_hop[i] = 0xFFFFu;
	}

	const uint32 origin_state = state_offset[origin];
	dist[origin_state] = 0;
	heap_item_t origin_item;
	origin_item.dist = 0;
	origin_item.state = origin_state;
	heap.insert(origin_item);

	while ( heap.node_count > 0 )
	{
		const heap_item_t item = heap.pop();
		const uint32 d = item.dist;
		const uint32 s = item.state;
		if ( d != dist[s] )
		{
			continue;
		}
		const uint32 h = state_halt[s];
		const uint32 arriving = state_key[s];
		for ( uint32 i = out_offset[h]; i < out_offset[ h + 1 ]; ++i )
		{
			const full_edge_t &edge = out_edges[i];
			if ( edge.transport == arriving && edge.transport != 0 )
			{
				continue;
			}
			if ( edge.target_state == 0xFFFFFFFFu || d > UINT32_MAX_VALUE - edge.weight )
			{
				continue;
			}
			const uint32 candidate = d + edge.weight;
			if ( candidate < dist[ edge.target_state ] )
			{
				dist[ edge.target_state ] = candidate;
				first_hop[ edge.target_state ] = ( s == origin_state ) ? edge.target : first_hop[s];
				heap_item_t next_item;
				next_item.dist = candidate;
				next_item.state = edge.target_state;
				heap.insert(next_item);
			}
		}
	}

	const size_t row = static_cast<size_t>(origin) * working_halt_count;
	for ( uint32 target = 0; target < working_halt_count; ++target )
	{
		if ( target == origin )
		{
			working_times[ row + target ] = 0;
			working_next_transfers[ row + target ] = 0;
			continue;
		}
		uint32 best = UINT32_MAX_VALUE;
		uint16 best_hop = 0xFFFFu;
		for ( uint32 s = state_offset[target]; s < state_offset[ target + 1 ]; ++s )
		{
			if ( dist[s] < best || ( dist[s] == best && first_hop[s] < best_hop ) )
			{
				best = dist[s];
				best_hop = first_hop[s];
			}
		}
		working_times[ row + target ] = best;
		working_next_transfers[ row + target ] = best_hop != 0xFFFFu
			? working_halt_list[ best_hop ].get_id() : 0;
	}
}


void path_explorer_t::compartment_t::step()
{
#ifdef MULTI_THREAD
	if (!allow_path_explorer_on_this_thread)
	{
		return;
	}
#endif

#ifdef DEBUG_COMPARTMENT_STEP
	printf("\n\nCategory :  %s; Class : %s \n", translator::translate(catg_name), translator::translate(class_name));
#endif

	// For timing use
	uint32 start, diff;

	switch (current_phase)
	{
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Determine whether a new refresh should start; prepare the flags.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_check_flag :
		{
			if (refresh_requested)
			{
				refresh_requested = false;	// immediately reset it so that we can take new requests
				refresh_completed = false;	// indicate that processing is at work
				//refresh_start_time = dr_time();
				refresh_start_time = world->get_ticks(); // Possibly more network safe than the original (commented out above)
				current_phase = phase_init_prepare;	// proceed to next phase
				// Fall through to the next refresh step.
			}
			else
			{
#ifdef DEBUG_COMPARTMENT_STEP
				printf("\t\t\tRefresh has not been requested\n");
#endif

				return;
			}
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Halt-list preparation: build the halt list, halt index map and linkage
		// list for the connexion rebuild.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_init_prepare :
		{
#ifdef DEBUG_COMPARTMENT_STEP
			++step_count;

			printf("\t\tCurrent Step : %lu \n", step_count);

			start = dr_time();	// start timing
#endif
			if ( phase_counter == 0 )
			{
				// A new pass: release everything built by the previous pass.
				full_connexions.clear();
				clear_search_graph();
				free_thread_scratch();
				delete[] working_times;
				working_times = NULL;
				delete[] working_next_transfers;
				working_next_transfers = NULL;
				if ( all_halts_list != NULL )
				{
					delete[] all_halts_list;
					all_halts_list = NULL;
				}
				if ( working_halt_list != NULL )
				{
					delete[] working_halt_list;
					working_halt_list = NULL;
				}
				if ( working_halt_index_map != NULL )
				{
					delete[] working_halt_index_map;
					working_halt_index_map = NULL;
				}
				if ( transport_index_map != NULL )
				{
					delete[] transport_index_map;
					transport_index_map = NULL;
				}
				if ( linkages != NULL )
				{
					delete linkages;
					linkages = NULL;
				}
				if ( transfer_list != NULL )
				{
					delete[] transfer_list;
					transfer_list = NULL;
				}
				transfer_count = 0;
				working_halt_count = 0;
			}

			vector_tpl<halthandle_t>::const_iterator halt_iter = haltestelle_t::get_alle_haltestellen().begin();
			all_halts_count = (uint16) haltestelle_t::get_alle_haltestellen().get_count();

			// create all halts list
			if (all_halts_count > 0)
			{
				all_halts_list = new halthandle_t[all_halts_count];
			}

			const bool no_walking_connexions = !world->get_settings().get_allow_routing_on_foot() || catg != goods_manager_t::passengers->get_catg_index();

			// Save the halt list in an array first to prevent the list from being modified across steps, causing bugs
			for (uint16 i = 0; i < all_halts_count; ++i)
			{
				all_halts_list[i] = *halt_iter;
				++halt_iter;

				// Connect halts within walking distance of each other (for passengers only)
				// @author: jamespetts, July 2011

				if ( no_walking_connexions || !all_halts_list[i]->is_enabled(goods_manager_t::passengers) )
				{
					continue;
				}

				const uint32 halts_within_walking_distance = all_halts_list[i]->get_number_of_halts_within_walking_distance();

				halthandle_t walking_distance_halt;

				for ( uint32 x = 0; x < halts_within_walking_distance; ++x )
				{
					walking_distance_halt = all_halts_list[i]->get_halt_within_walking_distance(x);

					if(!walking_distance_halt.is_bound() || !walking_distance_halt->is_enabled(goods_manager_t::passengers))
					{
						continue;
					}

					const uint32 walking_journey_distance = shortest_distance(
						all_halts_list[i]->get_next_pos(walking_distance_halt->get_basis_pos()),
						walking_distance_halt->get_next_pos(all_halts_list[i]->get_basis_pos())
						);

					const uint32 journey_time = world->walking_time_tenths_from_distance(walking_journey_distance);

					// Check the journey times to the connexion
					haltestelle_t::connexion new_connexion;
					new_connexion.waiting_time = 0; // People do not need to wait to walk.
					new_connexion.transfer_time = walking_distance_halt->get_transfer_time();
					new_connexion.best_convoy = convoihandle_t();
					new_connexion.best_line = linehandle_t();
					new_connexion.journey_time = journey_time;
					new_connexion.alternative_seats = 0;
					add_full_connexion(all_halts_list[i], walking_distance_halt, new_connexion);

					// These are walking connexions only. There will not be multiple possible connexions, so no need
					// to check for existing connexions here.
					connexion_list[ all_halts_list[i].get_id() ].connexion_table->put(walking_distance_halt, new_connexion);
					connexion_list[ all_halts_list[i].get_id() ].serving_transport = 1u;	// will become an interchange if served by additional transport(s)
					all_halts_list[i]->prepare_goods_list(catg);
				}
			}

			// create and initlialize a halthandle-entry to matrix-index map (halt index map)
			working_halt_index_map = new uint16[65536];
			for (uint32 i = 0; i < 65536; ++i)
			{
				// For quickstone handle, there can at most be 65535 valid entries, plus entry 0 which is reserved for null handle
				// Thus, the range of quickstone entries [1, 65535] is mapped to the range of matrix index [0, 65534]
				// Matrix index 65535 either means null handle or the halt has no connexion of the relevant goods category
				// This is always created regardless
				working_halt_index_map[i] = 65535;
			}

			transport_index_map = new uint16[131072]();		// initialise all elements to zero

			// create a list of schedules of lines and lineless convoys
			linkages = new vector_tpl<linkage_t>(1024);
			convoihandle_t current_convoy;
			linehandle_t current_line;
			linkage_t temp_linkage;


			// loop through all convoys
			for (vector_tpl<convoihandle_t>::const_iterator i = world->convoys().begin(), end = world->convoys().end(); i != end; i++)
			{
				current_convoy = *i;
				// only consider lineless convoys which support this compartment's goods catetory which are not in the depot
				if (!current_convoy->in_depot() && !current_convoy->get_line().is_bound() && (catg >= 2 || current_convoy->carries_this_or_lower_class(catg, g_class)) && current_convoy->carries_this_category(catg) )
				{
					temp_linkage.convoy = current_convoy;
					linkages->append(temp_linkage);
					transport_index_map[ 65536u + current_convoy.get_id() ] = linkages->get_count();
				}
			}

			temp_linkage.convoy = convoihandle_t();	// reset the convoy handle component

			// loop through all lines of all players
			for (int i = 0; i < MAX_PLAYER_COUNT; ++i)
			{
				player_t *current_player = world->get_player(i);

				if(  current_player == NULL  )
				{
					continue;
				}

				for (vector_tpl<linehandle_t>::const_iterator j = current_player->simlinemgmt.get_all_lines().begin(),
					 end = current_player->simlinemgmt.get_all_lines().end(); j != end; j++)
				{
					current_line = *j;
					// only consider lines which support this compartment's goods category and, where applicable, class
					if ( current_line->carries_this_category(catg) && (catg >= 2 || current_line->carries_this_or_lower_class(catg, g_class)) && current_line->count_convoys() > 0)
					{
						temp_linkage.line = current_line;
						linkages->append(temp_linkage);
						transport_index_map[ current_line.get_id() ] = linkages->get_count();
					}
				}
			}

			// can have at most 65535 different lines and lineless convoys; passing this limit should be extremely unlikely
			assert( linkages->get_count() <= 65535u );

#ifdef DEBUG_COMPARTMENT_STEP
			diff = dr_time() - start;	// stop timing

			printf("\tTotal Halt Count :  %lu \n", all_halts_count);
			printf("\tTotal Lines/Lineless Convoys Count :  %ul \n", linkages->get_count());
			printf("\t\t\tInitial prepration takes :  %lu ms \n", diff);
#endif

			current_phase = phase_rebuild_connexions;	// proceed to the next phase

#ifndef DEBUG_EXPLORER_SPEED
			return;
#endif
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Connexion rebuild for this compartment's goods category.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_rebuild_connexions :
		{
#ifdef DEBUG_COMPARTMENT_STEP
			++step_count;

			printf("\t\tCurrent Step : %lu \n", step_count);
#endif
			const goods_desc_t *const ware_type = goods_manager_t::get_info_catg_index(catg);

			linkage_t current_linkage;
			schedule_t *current_schedule;
			player_t *current_owner;
			uint32 current_average_speed;

			uint8 entry_count;
			halthandle_t current_halt;

			minivec_tpl<halthandle_t> halt_list(64);
			minivec_tpl<uint32> journey_time_list(64);
			// A vector indicating whether certain halts have been processed already as well as other flags
			minivec_tpl<uint8> flag_list(64);

			uint32 accumulated_journey_time;
			haltestelle_t::connexions_map *catg_connexions;

			start = dr_time();	// start timing

			// for each schedule of line / lineless convoy
			while (phase_counter < linkages->get_count())
			{
				current_linkage = (*linkages)[phase_counter];

				// determine schedule, owner and average speed
				if ( current_linkage.line.is_bound() && current_linkage.line->get_schedule() && current_linkage.line->count_convoys() )
				{
					// Case : a line
					current_schedule = current_linkage.line->get_schedule();
					current_owner = current_linkage.line->get_owner();
					current_average_speed = (uint32) ( current_linkage.line->get_finance_history(1, LINE_AVERAGE_SPEED) > 0 ?
													   current_linkage.line->get_finance_history(1, LINE_AVERAGE_SPEED) :
													   ( speed_to_kmh(current_linkage.line->get_convoy(0)->get_min_top_speed()) >> 1 ) );
				}
				else if ( current_linkage.convoy.is_bound() && current_linkage.convoy->get_schedule() )
				{
					// Case : a lineless convoy
					current_schedule = current_linkage.convoy->get_schedule();
					current_owner = current_linkage.convoy->get_owner();
					current_average_speed = (uint32) ( current_linkage.convoy->get_finance_history(1, convoi_t::CONVOI_AVERAGE_SPEED) > 0 ?
													   current_linkage.convoy->get_finance_history(1, convoi_t::CONVOI_AVERAGE_SPEED) :
													   ( speed_to_kmh(current_linkage.convoy->get_min_top_speed()) >> 1 ) );
				}
				else
				{
					// Case : nothing is bound -> just ignore
					++phase_counter;
					continue;
				}

				// create a list of reachable halts
				bool reverse = false;
				halt_list.clear();
				flag_list.clear();

				uint8 index = 0;
				uint8 flag = 0;

				const bool this_compartment_affected_by_consist_orders = current_schedule->has_consist_orders() &&
					(!current_schedule->get_catg_carried_to().empty() ||
						(ware_type == goods_manager_t::passengers && !current_schedule->get_passenger_min_class_carried_to().empty()) ||
						(ware_type == goods_manager_t::mail && !current_schedule->get_mail_min_class_carried_to().empty()));

				bool previous_is_carried_from_here = true;
				bool previous_discharge_payload_and_set_down_only = false;
				bool fragmented_schedule = false;

				// If the schedule has consist orders that change what category/class that this schedule carries, check whether this category and class can be carried from the point of the first entry.
				if(this_compartment_affected_by_consist_orders)
				{
					if (current_schedule->orders.is_contained(current_schedule->get_unique_id_from_entry(index)))
					{
						if ((current_schedule->get_catg_carried_from().get(current_schedule->get_unique_id_from_entry(index)).is_contained(ware_type->get_catg_index())) &&
							(ware_type == goods_manager_t::passengers && current_schedule->get_passenger_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) <= g_class) ||
							(ware_type == goods_manager_t::mail && current_schedule->get_mail_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) <= g_class))
						{
							set_flag(flag, does_not_carry_to);
						}
						else
						{
							// Incompatible consist order at the first entry
							set_flag(flag, does_not_carry_from);
							previous_is_carried_from_here = false;
						}
					}
				}

				for (uint32 k = 0; k < (this_compartment_affected_by_consist_orders ? 2 : 1); k++)
				{
					index = 0;
					reverse = false;
					entry_count = current_schedule->is_mirrored() ? (current_schedule->get_count() * 2) - 2 : current_schedule->get_count();

					while (entry_count-- && index < current_schedule->get_count())
					{
						flag = 0;
						current_halt = haltestelle_t::get_halt(current_schedule->entries[index].pos, current_owner);

						// Make sure that the halt found was built before refresh started and that it supports current goods category
						if (current_halt.is_bound() && current_halt->get_inauguration_time() < refresh_start_time && current_halt->is_enabled(ware_type))
						{
							if (this_compartment_affected_by_consist_orders)
							{
								if (!previous_is_carried_from_here)
								{
									if (!reverse)
									{
										if (current_schedule->get_catg_carried_from().is_contained(current_schedule->entries[index].unique_entry_id) &&
											current_schedule->get_catg_carried_from().get(current_schedule->entries[index].unique_entry_id).is_contained(ware_type->get_catg_index()))
										{
											if (ware_type == goods_manager_t::passengers)
											{
												if (current_schedule->get_passenger_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) <= g_class)
												{
													set_flag(flag, does_not_carry_to);
												}
											}
											else if (ware_type == goods_manager_t::mail)
											{
												if (current_schedule->get_mail_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) <= g_class)
												{
													set_flag(flag, does_not_carry_to);
												}
											}
											else
											{
												set_flag(flag, does_not_carry_to);
											}
										}
									}
									else
									{
										// Swap from and to for reverse.
										if (current_schedule->get_catg_carried_to().is_contained(current_schedule->entries[index].unique_entry_id) &&
											current_schedule->get_catg_carried_to().get(current_schedule->entries[index].unique_entry_id).is_contained(ware_type->get_catg_index()))
										{
											if (ware_type == goods_manager_t::passengers)
											{
												if (current_schedule->get_passenger_min_class_carried_to().get(current_schedule->get_unique_id_from_entry(index)) <= g_class)
												{
													set_flag(flag, does_not_carry_to);
												}
											}
											else if (ware_type == goods_manager_t::mail)
											{
												if (current_schedule->get_mail_min_class_carried_to().get(current_schedule->get_unique_id_from_entry(index)) <= g_class)
												{
													set_flag(flag, does_not_carry_to);
												}
											}
											else
											{
												set_flag(flag, does_not_carry_to);
											}
										}
									}
								}
								else
								{
									if (!reverse)
									{
										if (current_schedule->get_catg_carried_to().is_contained(current_schedule->entries[index].unique_entry_id) &&
											current_schedule->get_catg_carried_to().get(current_schedule->entries[index].unique_entry_id).is_contained(ware_type->get_catg_index()))
										{
											// This cannot be an origin halt for this category
											set_flag(flag, does_not_carry_from);
										}
										else if (ware_type == goods_manager_t::passengers && current_schedule->get_passenger_min_class_carried_to().get(current_schedule->get_unique_id_from_entry(index)) > g_class)
										{
											// This cannot be an origin halt for this class
											set_flag(flag, does_not_carry_from);
										}
										else if (ware_type == goods_manager_t::mail && current_schedule->get_mail_min_class_carried_to().get(current_schedule->get_unique_id_from_entry(index)) > g_class)
										{
											set_flag(flag, does_not_carry_from);
										}
									}
									else
									{
										// Swap from and to for reverse.
										if (current_schedule->get_catg_carried_from().is_contained(current_schedule->entries[index].unique_entry_id) &&
											current_schedule->get_catg_carried_from().get(current_schedule->entries[index].unique_entry_id).is_contained(ware_type->get_catg_index()))
										{
											// This cannot be an origin halt for this category
											set_flag(flag, does_not_carry_from);
										}
										else if (ware_type == goods_manager_t::passengers && current_schedule->get_passenger_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) > g_class)
										{
											// This cannot be an origin halt for this class
											set_flag(flag, does_not_carry_from);
										}
										else if (ware_type == goods_manager_t::mail && current_schedule->get_mail_min_class_carried_from().get(current_schedule->get_unique_id_from_entry(index)) > g_class)
										{
											set_flag(flag, does_not_carry_from);
										}
									}
								}
							}

							if (is_flag_set(flag, does_not_carry_from))
							{
								previous_is_carried_from_here = false;
							}
							else if (!previous_is_carried_from_here)
							{
								if (is_flag_set(flag, does_not_carry_to))
								{
									previous_is_carried_from_here = true;
								}
							}

							if (k == 0)
							{
								if (current_schedule->entries[index].is_flag_set(schedule_entry_t::set_down_only))
								{
									set_flag(flag, set_down_only);
								}
								if (current_schedule->entries[index].is_flag_set(schedule_entry_t::pick_up_only))
								{
									set_flag(flag, pick_up_only);
								}
								if (current_schedule->entries[index].is_flag_set(schedule_entry_t::lay_over))
								{
									set_flag(flag, layover);
								}
								if (current_schedule->entries[index].is_flag_set(schedule_entry_t::discharge_payload))
								{
									set_flag(flag, discharge_payload);
									if (is_flag_set(flag, set_down_only) && !is_flag_set(flag, does_not_carry_from))
									{
										// If both discharge payload and set down only are engaged, we cannot route past this point.
										set_flag(flag, does_not_carry_from);
										previous_discharge_payload_and_set_down_only = true;
										fragmented_schedule = true;
									}
									else if (!is_flag_set(flag, set_down_only))
									{
										if (previous_discharge_payload_and_set_down_only && !is_flag_set(flag, does_not_carry_from))
										{
											set_flag(flag, does_not_carry_to);
										}
										previous_discharge_payload_and_set_down_only = false;
									}
								}
								else if(!is_flag_set(flag, set_down_only))
								{
									if (previous_discharge_payload_and_set_down_only && !is_flag_set(flag, does_not_carry_from))
									{
										set_flag(flag, does_not_carry_to);
									}
									previous_discharge_payload_and_set_down_only = false;
								}

								// Assign to halt list only if current halt supports this compartment's goods category
								halt_list.append(current_halt, 64);

								// Initialise the corresponding flag list entry without the recurrence flag set
								flag_list.append(flag, 64);
							}
							else
							{
								// Second run - only update flags
								if (flag_list.get_count() > index)
								{
									flag_list[index] |= flag;
								}
							}
						}

						current_schedule->increment_index(&index, &reverse);
					}
				}

				fragmented_schedule |= this_compartment_affected_by_consist_orders;

				// precalculate journey times between consecutive halts
				// This is now only a fallback in case the point to point journey time data are not available.
				entry_count = halt_list.get_count();
				uint32 journey_time = 0;
				journey_time_list.clear();
				journey_time_list.append(0);	// reserve the first entry for the last journey time from last halt to first halt

				for (uint8 i = 0; i < entry_count; ++i)
				{
					journey_time = 0;
					const id_pair pair(halt_list[i].get_id(), halt_list[(i + 1) % entry_count].get_id());

					if (current_linkage.line.is_bound() && current_linkage.line->get_average_journey_times().is_contained(pair))
					{
						if (!halt_list[i].is_bound() || !halt_list[(i + 1) % entry_count].is_bound())
						{
							current_linkage.line->get_average_journey_times().remove(pair);
							continue;
						}
						else
						{
							journey_time = current_linkage.line->get_average_journey_times().access(pair)->reduce();
						}
					}
					else if (current_linkage.convoy.is_bound() && current_linkage.convoy->get_average_journey_times().is_contained(pair))
					{
						if (!halt_list[i].is_bound() || !halt_list[(i + 1) % entry_count].is_bound())
						{
							current_linkage.convoy->get_average_journey_times().remove(pair);
							continue;
						}
						else
						{
							journey_time = current_linkage.convoy->get_average_journey_times().access(pair)->reduce();
						}
					}

					if (journey_time == 0)
					{
						// Zero here means that there are no journey time data even if the hashtable entry exists.
						// Fallback to convoy's general average speed if a point-to-point average is not available.
						const uint32 distance = shortest_distance(halt_list[i]->get_basis_pos(), halt_list[(i + 1) % entry_count]->get_basis_pos());
						journey_time = world->travel_time_tenths_from_distance(distance, current_average_speed);
					}

					if (is_flag_set(flag_list[i], layover) || (is_flag_set(flag_list[i], discharge_payload) && !is_flag_set(flag_list[i], set_down_only)))
					{
						// If the previous stop is a layover, assume that we will always need to board the consist here even
						// if we were already onboard. Thus, add the transfer time. Should we also add the waiting time?
						journey_time += catg != goods_manager_t::passengers->get_catg_index() ? halt_list[i]->get_transshipment_time() : halt_list[i]->get_transfer_time();
					}

					// journey time from halt 0 to halt 1 is stored in journey_time_list[1]
					journey_time_list.append(journey_time, 64);

				}

				journey_time_list[0] = journey_time_list[entry_count];	// copy the last entry to the first entry
				journey_time_list.remove_at(entry_count);	// remove the last entry


				// rebuild connexions for all halts in halt list
				// for each origin halt
				uint8 last_consist_order_stop_point_h = 255;
				uint8 last_consist_order_start_point_h = 255;

				if (fragmented_schedule)
				{
					// Deal with wrapping around the ends of schedules.
					for (uint8 h = 0; h < entry_count; ++h)
					{
						if (is_flag_set(flag_list[h], does_not_carry_from))
						{
							last_consist_order_stop_point_h = h;
						}
						else if (is_flag_set(flag_list[h], does_not_carry_to))
						{
							last_consist_order_start_point_h = h;
						}
					}

					if (last_consist_order_stop_point_h > last_consist_order_start_point_h)
					{
						last_consist_order_stop_point_h = 0;
					}
				}

				for (uint8 h = 0; h < entry_count; ++h)
				{
					if (this_compartment_affected_by_consist_orders)
					{
						if (is_flag_set(flag_list[h], does_not_carry_from))
						{
							last_consist_order_stop_point_h = h;
						}
						else if (is_flag_set(flag_list[h], does_not_carry_to))
						{
							last_consist_order_start_point_h = h;
						}
					}

					if (is_flag_set(flag_list[h], recurrence))
					{
						// skip this halt if it has already been processed
						continue;
					}

					// Is the origin in the invalid zone?
					if (fragmented_schedule)
					{
						if (h >= last_consist_order_stop_point_h && h < last_consist_order_start_point_h)
						{
							continue;
						}
					}

					accumulated_journey_time = 0;

					// use hash tables in connexion list, but not hash tables stored in the halt
					catg_connexions = connexion_list[ halt_list[h].get_id() ].connexion_table;
					// any serving line/lineless convoy increments serving transport count
					++connexion_list[ halt_list[h].get_id() ].serving_transport;

					uint8 last_consist_order_stop_point_t = last_consist_order_stop_point_h;
					uint8 last_consist_order_start_point_t = last_consist_order_start_point_h;

					// for each target halt (origin halt is excluded)
					for (uint8 i = 1, t = (h + 1) % entry_count;
						i < entry_count;
						++i, t = (t + 1) % entry_count)
					{
						if (fragmented_schedule)
						{
							if (is_flag_set(flag_list[t], does_not_carry_from))
							{
								last_consist_order_stop_point_t = t;
							}
							else if (is_flag_set(flag_list[t], does_not_carry_to))
							{
								last_consist_order_start_point_t = t;
							}
						}

						// Case: origin halt is encountered again
						if (halt_list[t] == halt_list[h])
						{
							// reset and process the next
							accumulated_journey_time = 0;
							// mark this halt in the recurrence list to avoid duplicated processing
							set_flag(flag_list[t], recurrence);
							continue;
						}

						// Case: this destination halt is unreachable from this origin halt for this category/class
						if (fragmented_schedule)
						{
							if (last_consist_order_stop_point_h < 255)
							{
								if ((t > last_consist_order_stop_point_t && (t < last_consist_order_start_point_t || last_consist_order_start_point_t < last_consist_order_stop_point_t)) ||
									(last_consist_order_start_point_t < last_consist_order_stop_point_t && t < last_consist_order_start_point_t)) // Wrap around
								{
									// The destination is in the invalid zone
									continue;
								}
								else if ((h < last_consist_order_start_point_t && t > h && t > last_consist_order_stop_point_t) ||
									(t < h && (last_consist_order_start_point_t < last_consist_order_stop_point_t || h < last_consist_order_start_point_h))) // Wrap around
								{
									// The start point is in a different validity zone to the end point.
									continue;
								}
							}
						}

						// Case: suitable halt
						accumulated_journey_time += journey_time_list[t];

						// Check the journey times to the connexion
						id_pair halt_pair(halt_list[h].get_id(), halt_list[t].get_id());
						if (is_flag_set(flag_list[h], set_down_only) || is_flag_set(flag_list[t], pick_up_only))
						{
							// Skip this connexion
							dbg->message("void path_explorer_t::compartment_t::step()", "Skipping step owing to schedule settings");
						}
						else
						{
							haltestelle_t::connexion new_connexion;
							new_connexion.waiting_time = halt_list[h]->get_average_waiting_time(halt_list[t], catg, g_class);
							new_connexion.transfer_time = catg != goods_manager_t::passengers->get_catg_index() ? halt_list[h]->get_transshipment_time() : halt_list[h]->get_transfer_time();
							if (current_linkage.line.is_bound())
							{
								average_tpl<uint32>* ave = current_linkage.line->get_average_journey_times().access(halt_pair);
								if (ave && ave->count > 0)
								{
									new_connexion.journey_time = ave->reduce();
								}
								else
								{
									// Fallback - use the old method. This will be an estimate, and a somewhat generous one at that.
									new_connexion.journey_time = accumulated_journey_time;
								}
							}
							else if (current_linkage.convoy.is_bound())
							{
								average_tpl<uint32>* ave = current_linkage.convoy->get_average_journey_times().access(halt_pair);
								if (ave && ave->count > 0)
								{
									new_connexion.journey_time = ave->reduce();
								}
								else
								{
									// Fallback - use the old method. This will be an estimate, and a somewhat generous one at that.
									new_connexion.journey_time = accumulated_journey_time;
								}
							}
							new_connexion.best_convoy = current_linkage.convoy;
							new_connexion.best_line = current_linkage.line;
							new_connexion.alternative_seats = 0;
							add_full_connexion(halt_list[h], halt_list[t], new_connexion);

							// Check whether this is the best connexion so far, and, if so, add it.
							haltestelle_t::connexion *existing_connexion = catg_connexions->get(halt_list[t]);
							if (existing_connexion == NULL)
							{
								catg_connexions->put(halt_list[t], new_connexion);
								halt_list[h]->prepare_goods_list(catg);
							}
							else if (get_connexion_weight(*existing_connexion) > get_connexion_weight(new_connexion))
							{
								// The new connexion is better - replace it.
								new_connexion.alternative_seats = existing_connexion->alternative_seats;
								catg_connexions->set(halt_list[t], new_connexion);
							}
						}
					}
				}

				// Add cross-schedule through connexions for any coupling entries
				// on this linkage's schedule.
				add_through_connexions(current_linkage, current_schedule);

				++phase_counter;

				// iteration control
				++total_iterations;
				if ( use_limits && iterations == limit_rebuild_connexions)
				{
					break;
				}
			}

			diff = dr_time() - start;	// stop timing

			// iteration statistics collection
			if ( catg == representative_category )
			{
				statistic_duration += ( diff ? diff : 1 );
				statistic_iteration += iterations;
			}

#ifdef DEBUG_COMPARTMENT_STEP
			printf("\t\t\tRebuilding connexions takes :  %lu ms \n", diff);
#endif

			// Check whether this refresh step is finished.
			if (phase_counter == linkages->get_count())
			{
				// iteration limit adjustment
				if ( catg == representative_category )
				{
					const uint32 projected_iterations = statistic_iteration * time_midpoint / statistic_duration;
					if ( projected_iterations > 0 )
					{
						if ( env_t::networkmode )
						{
							const uint32 percentage = projected_iterations * 100 / local_rebuild_connexions;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								local_rebuild_connexions = projected_iterations;
								local_limits_changed = true;
							}
						}
						else
						{
							const uint32 percentage = projected_iterations * 100 / limit_rebuild_connexions;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								limit_rebuild_connexions = projected_iterations;
							}
						}
					}
				}

				// reset statistic variables
				statistic_duration = 0;
				statistic_iteration = 0;

				// delete immediately after use
				if (linkages)
				{
					delete linkages;
					linkages = NULL;
				}

				current_phase = phase_filter_eligible;	// proceed to the next phase
				phase_counter = 0;	// reset counter

			}

			iterations = 0;	// reset iteration counter

#ifndef DEBUG_EXPLORER_SPEED
			return;
#endif
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Eligible-halt selection: build the list of halts that support the
		// current goods type and update the halt index map.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_filter_eligible :
		{
#ifdef DEBUG_COMPARTMENT_STEP
			++step_count;

			printf("\t\tCurrent Step : %lu \n", step_count);
#endif
			// create working halt list only if we are not resuming and there is at least one halt
			if ( phase_counter == 0 && all_halts_count > 0 )
			{
				working_halt_list = new halthandle_t[all_halts_count];
			}

			start = dr_time();	// start timing

			// add halt to working halt list only if it has connexions that support this compartment's goods category
			while (phase_counter < all_halts_count)
			{
				const halthandle_t &current_halt = all_halts_list[phase_counter];

				// halts may be removed during the process of refresh
				if ( ! current_halt.is_bound() )
				{
					reset_connexion_entry( current_halt.get_id() );
					++phase_counter;
					continue;
				}

				if (!connexion_list[ current_halt.get_id() ].connexion_table->empty())
				{
					// valid connexion(s) found -> add to working halt list and update halt index map
					working_halt_list[working_halt_count] = current_halt;
					working_halt_index_map[ current_halt.get_id() ] = working_halt_count;
					++working_halt_count;
				}

				// swap the old connexion hash table with a new one
				current_halt->swap_connexions(catg, g_class, connexion_list[current_halt.get_id()].connexion_table);

				// transfer the value of the serving transport counter
				current_halt->set_schedule_count( catg, g_class, max_classes, connexion_list[ current_halt.get_id() ].serving_transport );

				// Delete the contents of the old connexion_table; we then reuse the empty table for the next catg/class
				reset_connexion_entry( current_halt.get_id() );

				++phase_counter;

				// iteration control
				++iterations;
				++total_iterations;
				if ( use_limits && iterations == limit_filter_eligible )
				{
					break;
				}
			}

			diff = dr_time() - start;	// stop timing

			// iteration statistics collection
			if ( catg == representative_category )
			{
				statistic_duration += ( diff ? diff : 1 );
				statistic_iteration += iterations;
			}

#ifdef DEBUG_COMPARTMENT_STEP
			printf("\t\t\tConstructing eligible halt list takes :  %lu ms \n", diff);
#endif

			// Check whether this refresh step is finished.
			if (phase_counter == all_halts_count)
			{
				// iteration limit adjustment
				if ( catg == representative_category )
				{
					const uint32 projected_iterations = statistic_iteration * time_midpoint / statistic_duration;
					if ( projected_iterations > 0 )
					{
						if ( env_t::networkmode )
						{
							const uint32 percentage = projected_iterations * 100 / local_filter_eligible;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								local_filter_eligible = projected_iterations;
								local_limits_changed = true;
							}
						}
						else
						{
							const uint32 percentage = projected_iterations * 100 / limit_filter_eligible;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								limit_filter_eligible = projected_iterations;
							}
						}
					}
				}

				// reset statistic variables
				statistic_duration = 0;
				statistic_iteration = 0;

				// update representative category and halt count where necessary
				if ( working_halt_count > representative_halt_count )
				{
					representative_category = catg;
					representative_halt_count = working_halt_count;
				}

				current_phase = phase_fill_matrix;	// proceed to the next phase
				phase_counter = 0;	// reset counter

#ifdef DEBUG_COMPARTMENT_STEP
				printf("\tEligible Halt Count :  %lu \n", working_halt_count);
#endif
			}

			iterations = 0;	// reset iteration counter

#ifndef DEBUG_EXPLORER_SPEED
			return;
#endif
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Graph build: build the routing-search graph and the working matrices.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_fill_matrix :
		{
			if ( phase_counter == 0 )
			{
				build_search_graph();
				phase_counter = working_halt_count;
				iterations = working_halt_count;
				total_iterations += working_halt_count;
				current_phase = phase_explore_paths;
				phase_counter = 0;
				origin_index = 0;
			}
			iterations = 0;
			return;
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Routing search: one Dijkstra per origin over the expanded
		// (halt, arriving transport) state graph.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		case phase_explore_paths :
		{
			uint64 iterations_processed = 0;
			start = dr_time();	// start timing

			while ( origin_index < working_halt_count )
			{
				uint32 origins_to_process = working_halt_count - origin_index;
				if ( use_limits )
				{
					const uint64 remaining = iterations_processed < limit_explore_paths
						? limit_explore_paths - iterations_processed : 0;
					if ( remaining < origins_to_process )
					{
						origins_to_process = static_cast<uint32>(remaining);
					}
					if ( origins_to_process == 0 && origin_index < working_halt_count )
					{
						origins_to_process = 1;
					}
				}
				if ( origins_to_process == 0 )
				{
					break;
				}

				pool_run(this, origin_index, origins_to_process);

				origin_index += static_cast<uint16>(origins_to_process);
				iterations_processed += origins_to_process;
				total_iterations += origins_to_process;
				if ( use_limits && iterations_processed >= limit_explore_paths )
				{
					break;
				}
			}

			diff = dr_time() - start;	// stop timing

			// iterations statistics collection
			if ( catg == representative_category )
			{
				++statistic_duration;	// step count
				statistic_iteration += static_cast<uint32>( iterations_processed / ( diff ? diff : 1 ) );	// sum of iterations per ms
			}

			if ( origin_index == working_halt_count )
			{
				// iteration limit adjustment
				if ( catg == representative_category )
				{
					const uint64 projected_iterations = static_cast<uint64>( statistic_iteration / statistic_duration ) * static_cast<uint64>( time_midpoint );
					if ( projected_iterations > 0 )
					{
						if ( env_t::networkmode )
						{
							const uint32 percentage = static_cast<uint32>( projected_iterations * 100 / local_explore_paths );
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								local_explore_paths = projected_iterations;
								local_limits_changed = true;
							}
						}
						else
						{
							const uint32 percentage = static_cast<uint32>( projected_iterations * 100 / limit_explore_paths );
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								limit_explore_paths = projected_iterations;
							}
						}
					}
				}

				// reset statistic variables
				statistic_duration = 0;
				statistic_iteration = 0;

				// path search completed -> delete old path info
				delete[] finished_times;
				finished_times = NULL;
				delete[] finished_next_transfers;
				finished_next_transfers = NULL;
				if ( finished_halt_index_map )
				{
					delete[] finished_halt_index_map;
					finished_halt_index_map = NULL;
				}

				// transfer working to finished
				finished_times = working_times;
				working_times = NULL;
				finished_next_transfers = working_next_transfers;
				working_next_transfers = NULL;
				finished_halt_index_map = working_halt_index_map;
				working_halt_index_map = NULL;
				finished_halt_count = working_halt_count;

				// path search completed -> release the graph and the candidate list
				clear_search_graph();
				free_thread_scratch();
				full_connexions.clear();
				working_halt_count = 0;
				if ( transfer_list )
				{
					delete[] transfer_list;
					transfer_list = NULL;
				}
				transfer_count = 0;

				current_phase = phase_reroute_goods;	// proceed to the next phase

				origin_index = 0;
				paths_available = true;
			}

			iterations = 0;	// reset iteration counter // desync debug

			return;
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Waiting-packet reroute: re-route goods already waiting at the halts.
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		case phase_reroute_goods :
		{
#ifdef DEBUG_COMPARTMENT_STEP
			++step_count;

			printf("\t\tCurrent Step : %lu \n", step_count);
#endif
			start = dr_time();	// start timing

			while (phase_counter < all_halts_count)
			{
				// halts may be removed during the process of refresh
				if ( ! all_halts_list[phase_counter].is_bound() )
				{
					++phase_counter;
					continue;
				}

				all_halts_list[phase_counter]->set_reroute_goods_next_step(catg);
				++iterations;
				++total_iterations;

				// The below is not compatible with multi-threading.
				/*if ( all_halts_list[phase_counter]->reroute_goods(catg) )
				{
					// only halts with relevant goods packets are counted
					++iterations;
					++total_iterations;
				}*/

				++phase_counter;

				// iteration control
				if ( use_limits && iterations == limit_reroute_goods )
				{
					break;
				}
			}

			diff = dr_time() - start;	// stop timing

			// iteration statistics collection
			if ( catg == representative_category )
			{
				statistic_duration += ( diff ? diff : 1 );
				statistic_iteration += iterations;
			}

#ifdef DEBUG_COMPARTMENT_STEP
			printf("\t\t\tRe-routing goods takes :  %lu ms \n", diff);
#endif

			if (phase_counter == all_halts_count)
			{
				// iteration limit adjustment
				if ( catg == representative_category )
				{
					const uint32 projected_iterations = statistic_iteration * time_midpoint / statistic_duration;
					if ( projected_iterations > 0 )
					{
						if ( env_t::networkmode )
						{
							const uint32 percentage = projected_iterations * 100 / local_reroute_goods;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								local_reroute_goods = projected_iterations;
								local_limits_changed = true;
							}
						}
						else
						{
							const uint32 percentage = projected_iterations * 100 / limit_reroute_goods;
							if ( percentage < percent_lower_limit || percentage > percent_upper_limit )
							{
								limit_reroute_goods = projected_iterations;
							}
						}
					}
				}

				// reset statistic variables
				statistic_duration = 0;
				statistic_iteration = 0;

				current_phase = phase_check_flag;	// reset to the 1st phase
				phase_counter = 0;	// reset counter

				// delete immediately after use
				if (all_halts_list)
				{
					delete[] all_halts_list;
					all_halts_list = NULL;
				}
				all_halts_count = 0;

#ifdef DEBUG_COMPARTMENT_STEP
				printf("\tFinished in : %lu Steps \n\n", step_count);
				step_count = 0;
#endif

				refresh_start_time = 0;
				refresh_completed = true;
			}

			iterations = 0;	// reset iteration counter

			return;
		}

		default :
		{
			// Should not reach here
			dbg->error("compartment_t::step()", "Invalid step index : %u \n", current_phase);
			return;
		}
	}
}


bool path_explorer_t::compartment_t::get_path_between(const halthandle_t origin_halt, const halthandle_t target_halt,
													  uint32 &aggregate_time, halthandle_t &next_transfer)
{
	uint16 origin_index, target_index;

	// check if origin and target halts are both present in matrix; if yes, check the validity of the next transfer
	if ( paths_available /*&& origin_halt.is_bound() && target_halt.is_bound()*/
			&& ( origin_index = finished_halt_index_map[ origin_halt.get_id() ] ) != 65535
			&& ( target_index = finished_halt_index_map[ target_halt.get_id() ] ) != 65535 )
	{
		const size_t matrix_index = path_matrix_index(origin_index, target_index, finished_halt_count);
		halthandle_t stored_next_transfer;
		stored_next_transfer.set_id(finished_next_transfers[matrix_index]);
		if (stored_next_transfer.is_bound())
		{
			aggregate_time = finished_times[matrix_index];
			next_transfer = stored_next_transfer;
			return true;
		}
	}

	// requested path not found
	aggregate_time = UINT32_MAX_VALUE;
	next_transfer = halthandle_t();
	return false;
}


void path_explorer_t::compartment_t::set_category(uint8 category)
{
	catg = category;
	const goods_desc_t *ware_type = goods_manager_t::get_info_catg_index(catg);
	catg_name = ware_type->get_catg_name();
}

void path_explorer_t::compartment_t::set_class(uint8 value)
{
	g_class = value;
	delete[] class_name;
	class_name = new char[96];

	if (catg == goods_manager_t::INDEX_PAS)
	{
		sprintf(class_name, "%s_%u", translator::translate("p_class"), g_class);
	}
	else if (catg == goods_manager_t::INDEX_MAIL)
	{
		sprintf(class_name, "%s_%u", translator::translate("m_class"), g_class);
	}
	else
	{
		class_name[0] = '\0';
	}
}


void path_explorer_t::compartment_t::initialise_connexion_list()
{
	for (uint32 i = 0; i < 65536; ++i)
	{
		// Note that the connexion_tables created here will be
		// swapped with connexion_tables created/stored in halts.
		connexion_list[i].connexion_table = new haltestelle_t::connexions_map ();
		connexion_list[i].serving_transport = 0;
	}
}


void path_explorer_t::compartment_t::reset_connexion_entry(const uint16 halt_id)
{
	connexion_list[halt_id].connexion_table->clear();
	connexion_list[halt_id].serving_transport = 0;
}


void path_explorer_t::compartment_t::reset_connexion_list()
{
	for (uint32 i = 0; i < 65536; ++i)
	{
		if ( connexion_list[i].connexion_table )
		{
			reset_connexion_entry(i);
		}
	}
}


void path_explorer_t::compartment_t::finalise_connexion_list()
{
	for (uint32 i = 0; i < 65536; ++i)
	{
		reset_connexion_entry(i);
		delete connexion_list[i].connexion_table;
		connexion_list[i].connexion_table = NULL;
	}
}

void path_explorer_t::compartment_t::rdwr(loadsave_t* file)
{
	const bool save_revision_9 = file->is_version_ex_atleast(15, 9);

	file->rdwr_longlong(refresh_start_time);

	// Finished matrix (the layout is the same in every supported savegame
	// revision).
	file->rdwr_short(finished_halt_count);

	bool finished_halt_index_map_live = finished_halt_index_map != NULL;
	file->rdwr_bool(finished_halt_index_map_live);

	if (finished_halt_index_map_live)
	{
		if (file->is_loading())
		{
			finished_halt_index_map = new uint16[65536];
		}
		for (uint32 i = 0; i < 65536; ++i)
		{
			file->rdwr_short(finished_halt_index_map[i]);
		}
	}

	bool finished_matrix_live = finished_times != NULL;
	file->rdwr_bool(finished_matrix_live);

	if (finished_matrix_live)
	{
		if (file->is_saving())
		{
			uint16 tmp_idx;
			for (uint16 i = 0; i < finished_halt_count; i++)
			{
				for (uint16 j = 0; j < finished_halt_count; j++)
				{
					const size_t matrix_index = path_matrix_index(i, j, finished_halt_count);
					file->rdwr_long(finished_times[matrix_index]);
					tmp_idx = finished_next_transfers[matrix_index];
					file->rdwr_short(tmp_idx);
				}
			}
		}
		else
		{
			if (finished_halt_count > 0)
			{
				uint16 tmp_idx;
				finished_times = new_time_matrix(finished_halt_count);
				finished_next_transfers = new_short_matrix(finished_halt_count);
				for (uint16 i = 0; i < finished_halt_count; i++)
				{
					for (uint16 j = 0; j < finished_halt_count; j++)
					{
						const size_t matrix_index = path_matrix_index(i, j, finished_halt_count);
						file->rdwr_long(finished_times[matrix_index]);
						file->rdwr_short(tmp_idx);
						finished_next_transfers[matrix_index] = tmp_idx;
					}
				}
			}
		}
	}

	if (!save_revision_9 && file->is_loading())
	{
		// Saves older than Extended savegame revision 9 (EX_SAVE_MINOR in
		// simversion.h): read the old routing-refresh working state so that
		// the stream stays aligned, then discard it. The compartment is
		// restarted below.
		file->rdwr_short(working_halt_count);

		bool working_matrix_live = working_times != NULL;
		file->rdwr_bool(working_matrix_live);
		if (working_matrix_live)
		{
			if (working_halt_count > 0)
			{
				uint16 tmp_idx;
				uint16 tmp_first;
				uint16 tmp_last;
				working_times = new_time_matrix(working_halt_count);
				working_next_transfers = new_short_matrix(working_halt_count);
				for (uint16 i = 0; i < working_halt_count; i++)
				{
					for (uint16 j = 0; j < working_halt_count; j++)
					{
						const size_t matrix_index = path_matrix_index(i, j, working_halt_count);
						file->rdwr_long(working_times[matrix_index]);
						file->rdwr_short(tmp_idx);
						working_next_transfers[matrix_index] = tmp_idx;
						file->rdwr_short(tmp_first);
						file->rdwr_short(tmp_last);
					}
				}
			}
		}

		bool working_halt_index_map_live = working_halt_index_map != NULL;
		file->rdwr_bool(working_halt_index_map_live);
		if (working_halt_index_map_live)
		{
			working_halt_index_map = new uint16[65536];
			for (uint32 i = 0; i < 65536; ++i)
			{
				working_halt_index_map[i] = 65535;
			}
			for (uint32 i = 0; i < 65536; ++i)
			{
				file->rdwr_short(working_halt_index_map[i]);
			}
		}

		bool transport_index_map_live = transport_index_map != NULL;
		file->rdwr_bool(transport_index_map_live);
		if (transport_index_map_live)
		{
			transport_index_map = new uint16[131072]();
			for (uint32 i = 0; i < 131072; i++)
			{
				file->rdwr_short(transport_index_map[i]);
			}
		}

		file->rdwr_short(all_halts_count);
		bool all_halts_list_live = all_halts_list != NULL;
		file->rdwr_bool(all_halts_list_live);
		if (all_halts_list_live)
		{
			all_halts_list = new halthandle_t[all_halts_count];
			for (uint32 i = 0; i < all_halts_count; i++)
			{
				uint16 id;
				file->rdwr_short(id);
				all_halts_list[i].set_id(id);
			}
		}

		bool working_halt_list_live = working_halt_list != NULL;
		file->rdwr_bool(working_halt_list_live);
		if (working_halt_list_live)
		{
			working_halt_list = new halthandle_t[all_halts_count];
			for (uint32 i = 0; i < all_halts_count; i++)
			{
				uint16 id;
				file->rdwr_short(id);
				working_halt_list[i].set_id(id);
			}
		}

		bool linkages_live = linkages != NULL;
		file->rdwr_bool(linkages_live);
		if (linkages_live)
		{
			uint32 linkages_count;
			file->rdwr_long(linkages_count);
			linkages = new vector_tpl<linkage_t>(linkages_count);
			uint16 cnv_id;
			uint16 line_id;
			for (uint32 i = 0; i < linkages_count; i++)
			{
				file->rdwr_short(cnv_id);
				file->rdwr_short(line_id);
				linkage_t tmp;
				tmp.convoy.set_id(cnv_id);
				tmp.line.set_id(line_id);
				linkages->append(tmp);
			}
		}

		file->rdwr_short(transfer_count);
		transfer_list = new uint16[working_halt_count > 0 ? working_halt_count : 1];
		for (uint16 i = 0; i < transfer_count; i++)
		{
			file->rdwr_short(transfer_list[i]);
		}
	}
	else
	{
		// Revision 9 saves carry the finished matrix only; write an empty
		// working state.
		uint16 empty_working_halt_count = 0;
		file->rdwr_short(empty_working_halt_count);
		bool working_matrix_live = false;
		file->rdwr_bool(working_matrix_live);
		bool working_halt_index_map_live = false;
		file->rdwr_bool(working_halt_index_map_live);
		bool transport_index_map_live = false;
		file->rdwr_bool(transport_index_map_live);
		uint16 empty_all_halts_count = 0;
		file->rdwr_short(empty_all_halts_count);
		bool all_halts_list_live = false;
		file->rdwr_bool(all_halts_list_live);
		bool working_halt_list_live = false;
		file->rdwr_bool(working_halt_list_live);
		bool linkages_live = false;
		file->rdwr_bool(linkages_live);
		uint16 empty_transfer_count = 0;
		file->rdwr_short(empty_transfer_count);
		if (file->is_loading())
		{
			working_halt_count = 0;
			all_halts_count = 0;
			transfer_count = 0;
		}
	}

	file->rdwr_byte(catg);
	if (file->is_loading())
	{
		set_category(catg);
	}
	file->rdwr_byte(g_class);
	if (file->is_loading())
	{
		set_class(g_class);
	}

	const bool save_refresh_completed = save_revision_9 ? refresh_completed : paths_available;
	const bool save_refresh_requested = save_revision_9 ? refresh_requested : !paths_available;
	const uint8 save_current_phase = save_revision_9 ? current_phase : phase_check_flag;
	bool written_refresh_completed = save_refresh_completed;
	bool written_refresh_requested = save_refresh_requested;
	uint8 written_current_phase = save_current_phase;

	file->rdwr_short(step_count);
	file->rdwr_bool(paths_available);
	file->rdwr_bool(written_refresh_completed);
	file->rdwr_bool(written_refresh_requested);
	file->rdwr_byte(written_current_phase);
	file->rdwr_short(phase_counter);
	file->rdwr_long(iterations);
	if (file->is_version_ex_atleast(15, 6))
	{
		sint64 total_iterations_64 = static_cast<sint64>(total_iterations);
		file->rdwr_longlong(total_iterations_64);
		if (file->is_loading())
		{
			total_iterations = static_cast<uint64>(total_iterations_64);
		}
	}
	else
	{
		uint32 total_iterations_32 = total_iterations > UINT32_MAX_VALUE ? UINT32_MAX_VALUE : static_cast<uint32>(total_iterations);
		file->rdwr_long(total_iterations_32);
		if (file->is_loading())
		{
			total_iterations = total_iterations_32;
		}
	}
	if (file->is_loading())
	{
		refresh_completed = written_refresh_completed;
		refresh_requested = written_refresh_requested;
		current_phase = written_current_phase;
	}

	if (save_revision_9)
	{
		file->rdwr_short(origin_index);
	}
	else
	{
		uint16 old_via_index;
		uint32 old_origin_cluster_index;
		uint32 old_target_cluster_index;
		uint32 old_origin_member_index;
		file->rdwr_short(old_via_index);
		file->rdwr_long(old_origin_cluster_index);
		file->rdwr_long(old_target_cluster_index);
		file->rdwr_long(old_origin_member_index);

		bool inbound_connections_live;
		file->rdwr_bool(inbound_connections_live);
		if (inbound_connections_live)
		{
			connection_t tmp(64u, working_halt_count);
			tmp.rdwr(file);
		}
		bool outbound_connections_live;
		file->rdwr_bool(outbound_connections_live);
		if (outbound_connections_live)
		{
			connection_t tmp(64u, working_halt_count);
			tmp.rdwr(file);
		}
		bool old_process_next_transfer;
		file->rdwr_bool(old_process_next_transfer);
	}

	file->rdwr_long(statistic_duration);
	file->rdwr_long(statistic_iteration);

	if (file->is_loading() && !refresh_completed)
	{
		// The save carries only the finished matrix. Restart the compartment
		// at the halt-list preparation (phase_init_prepare); the finished
		// matrix from the previous pass remains available.
		reset(false);
	}
}

void path_explorer_t::compartment_t::connection_t::rdwr(loadsave_t* file)
{
	uint32 connection_cluster_count;
	if(file->is_saving())
	{
		connection_cluster_count = connection_clusters.get_count();
	}

	file->rdwr_long(connection_cluster_count);

	for(uint32 i = 0; i < connection_cluster_count; i ++)
	{
		if(file->is_saving())
		{
			connection_clusters[i]->rdwr(file);
		}
		else
		{
			connection_cluster_t* tmp_cnx = new connection_cluster_t(file);
			connection_clusters.append(tmp_cnx);
		}
	}

	file->rdwr_long(usage_level);
	file->rdwr_long(halt_vector_size);

	if (file->is_saving())
	{
		uint32 cluster_map_count = cluster_map.get_count();
		file->rdwr_long(cluster_map_count);
		for(auto iter : cluster_map)
		{
			file->rdwr_short(iter.key);
			iter.value->rdwr(file);

			cluster_map_count++;
		}
	}
	else if (file->is_loading())
	{
		uint32 cluster_map_count;
		file->rdwr_long(cluster_map_count);
		uint16 key;
		for(uint32 i = 0; i < cluster_map_count; i ++)
		{
			file->rdwr_short(key);
			connection_cluster_t* value = new connection_cluster_t(file);
			cluster_map.put(key, value);
		}
	}
}

void path_explorer_t::compartment_t::connection_t::connection_cluster_t::rdwr(loadsave_t* file)
{
	assert(file->is_saving());
	file->rdwr_short(transport);
	uint32 connected_halts_count = connected_halts.get_count();
	file->rdwr_long(connected_halts_count);
	for(uint32 i = 0; i < connected_halts_count; i ++)
	{
		file->rdwr_short(connected_halts[i]);
	}
}

path_explorer_t::compartment_t::connection_t::connection_cluster_t::connection_cluster_t(loadsave_t* file)
{
	assert(file->is_loading());
	file->rdwr_short(transport);
	uint32 connected_halts_count;
	file->rdwr_long(connected_halts_count);
	connected_halts.resize(connected_halts_count);

	for(uint32 i = 0; i < connected_halts_count; i ++)
	{
		uint16 tmp;
		file->rdwr_short(tmp);
		connected_halts.append(tmp);
	}
}
