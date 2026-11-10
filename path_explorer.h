/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef PATH_EXPLORER_H
#define PATH_EXPLORER_H


#include "network/memory_rw.h"
#include "simline.h"
#include "simhalt.h"
#include "simworld.h"
#include "halthandle_t.h"
#include "convoihandle_t.h"
#include "linehandle_t.h"
#include "simtypes.h"
#include "simdebug.h"

#include "tpl/vector_tpl.h"
#include "tpl/quickstone_hashtable_tpl.h"
#include "tpl/binary_heap_tpl.h"


/*
 * A centralised, steppable path searching system. It maintains the routing
 * database used by passenger, mail and goods packets: for every pair of
 * stops it stores an estimated journey time and the next stop to head for.
 * A refresh rebuilds the stop-to-stop connexions from the schedules and
 * searches from each origin while tracking the line or lineless convoy used
 * to arrive, so a route cannot re-board the same one at a transfer. The work
 * is spread over simulation steps.
 */
class path_explorer_t
{
public:
	struct limit_set_t
	{
		uint32 rebuild_connexions;
		uint32 filter_eligible;
		uint32 fill_matrix;
		uint64 explore_paths;
		uint32 reroute_goods;

		explicit limit_set_t() :
			rebuild_connexions(0),
			filter_eligible(0),
			fill_matrix(0),
			explore_paths(0),
			reroute_goods(0)
		{}

		explicit limit_set_t(bool) :
			rebuild_connexions(UINT32_MAX_VALUE),
			filter_eligible(UINT32_MAX_VALUE),
			fill_matrix(UINT32_MAX_VALUE),
			explore_paths(UINT64_MAX_VALUE),
			reroute_goods(UINT32_MAX_VALUE)
		{}

		limit_set_t(uint32 c, uint32 e, uint32 m, uint64 p, uint32 g) :
			rebuild_connexions(c),
			filter_eligible(e),
			fill_matrix(m),
			explore_paths(p),
			reroute_goods(g)
		{}

		void find_min_with(const limit_set_t &other)
		{
			if( other.rebuild_connexions < rebuild_connexions ) { rebuild_connexions = other.rebuild_connexions; }
			if( other.filter_eligible < filter_eligible ) { filter_eligible = other.filter_eligible; }
			if( other.fill_matrix < fill_matrix ) { fill_matrix = other.fill_matrix; }
			if( other.explore_paths < explore_paths ) { explore_paths = other.explore_paths; }
			if( other.reroute_goods < reroute_goods ) { reroute_goods = other.reroute_goods; }
		}

		bool operator == (const limit_set_t &other) const
		{
			if( rebuild_connexions == other.rebuild_connexions	&&
				filter_eligible == other.filter_eligible		&&
				fill_matrix == other.fill_matrix				&&
				explore_paths == other.explore_paths			&&
				reroute_goods == other.reroute_goods				)
			{
				return true;
			}
			return false;
		}

		bool operator != (const limit_set_t &other) const { return !( *this == other ); }

		void rdwr(memory_rw_t *buffer)
		{
			buffer->rdwr_long( rebuild_connexions );
			buffer->rdwr_long( filter_eligible );
			buffer->rdwr_long( fill_matrix );
			uint32 explore_paths_quotient;
			uint32 explore_paths_remainder;
			if( buffer->is_saving() )
			{
				explore_paths_quotient = (uint32)(explore_paths >> 32);
				explore_paths_remainder = (uint32)(explore_paths & 0xFFFFFFFFull);
			}
			buffer->rdwr_long( explore_paths_quotient );
			buffer->rdwr_long( explore_paths_remainder );
			if( buffer->is_loading() )
			{
				explore_paths = ((uint64)explore_paths_quotient << 32) | (uint64)explore_paths_remainder;
			}
			buffer->rdwr_long( reroute_goods );
		}
	};

	static bool must_refresh_on_loading;

	static void rdwr(loadsave_t* file);

private:

	class compartment_t
	{
		friend class path_explorer_t;

	protected:
		// structure for storing connexion hashtable and serving transport counter
		struct connexion_list_entry_t
		{
			haltestelle_t::connexions_map *connexion_table = NULL;
			uint8 serving_transport = 0;
		};

	private:

		// Legacy routing-refresh cluster state from older savegames. The class
		// and its reader exist only to load those saves and discard the
		// obsolete state. The savegame version gate is in compartment_t::rdwr.
		class connection_t
		{

		public:

			// element used for storing indices of halts connected to a transfer, together with their common transport
			struct connection_cluster_t
			{
				uint16 transport;
				vector_tpl<uint16> connected_halts;

				connection_cluster_t(loadsave_t* file);

				void rdwr(loadsave_t* file);
			};

		private:

			vector_tpl<connection_cluster_t*> connection_clusters;
			uint32 usage_level;			// number of connection clusters used
			uint32 halt_vector_size;	// size of connected halt vector in connection cluster object
			typedef  inthashtable_tpl<uint16, connection_cluster_t*, N_BAGS_LARGE> cluster_map_type;
			cluster_map_type cluster_map;

		public:

			connection_t(const uint32 cluster_count, const uint32 working_halt_count) :
				connection_clusters(cluster_count),
				usage_level(0),
				halt_vector_size(working_halt_count)
			{}

			~connection_t()
			{
				// deallocate memory for dynamically allocated connection clusters
				for ( uint32 i = 0; i < connection_clusters.get_count(); ++i )
				{
					delete connection_clusters[i];
				}
			}

			void rdwr(loadsave_t* file);
		};

		// data structure for temporarily storing lines and lineless conovys
		struct linkage_t
		{
			linehandle_t line;
			convoihandle_t convoy;
		};

		// A stop reachable from a coupling point, with the time from that
		// point and the linkage operating the final segment. Used while
		// generating through connexions across schedule couplings during the
		// connexion rebuild.
		struct through_stop_t
		{
			halthandle_t halt;
			uint32 time;
			linkage_t linkage;
			const schedule_t *schedule;
			uint8 entry_index;
			const schedule_entry_t *coupling_entry;
		};

		bool linkage_carries(const linkage_t &linkage) const;
		uint32 get_linkage_average_speed(const linkage_t &linkage) const;
		uint32 get_leg_time(const linkage_t &linkage, const schedule_entry_t &from, const schedule_entry_t &to, uint32 average_speed) const;
		uint32 get_recorded_pair_time(const linkage_t &linkage, const halthandle_t from, const halthandle_t to) const;
		bool entry_carries(const schedule_t *schedule, const schedule_entry_t &entry, bool to, uint8 category, uint8 g_class) const;
		bool through_predicate_allows(const schedule_t *source_schedule, const schedule_entry_t &source_origin, const schedule_entry_t &source_coupling, const schedule_t *destination_schedule, const schedule_entry_t &destination_coupling, const schedule_entry_t &destination_entry) const;
		void collect_downstream_stops(const linkage_t &linkage, const schedule_t *schedule, uint8 entry_index, bool reverse, uint32 base_time, uint8 depth, const schedule_entry_t *coupling_entry, vector_tpl<through_stop_t> &out) const;
		void collect_upstream_stops(const linkage_t &linkage, const schedule_t *schedule, uint8 entry_index, bool reverse, vector_tpl<through_stop_t> &out) const;
		void add_through_connexions(const linkage_t &current_linkage, const schedule_t *schedule);

		// store the start time of refresh
		sint64 refresh_start_time;

		// set of variables for finished path data
		uint32 *finished_times;
		uint16 *finished_next_transfers;
		uint16 *finished_halt_index_map;
		uint16 finished_halt_count;

		// set of variables for working path data
		uint32 *working_times;
		uint16 *working_next_transfers;
		uint16 *transport_index_map;
		uint16 *working_halt_index_map;
		halthandle_t *working_halt_list;
		uint16 working_halt_count;

		// Full stop-to-stop connexion candidates: every walking candidate,
		// every direct ride, and every through candidate generated across a
		// coupling (a journey that stays aboard when portions join or split),
		// before cross-linkage deduplication. The routing search graph is
		// built from this list.
		struct full_connexion_t
		{
			uint16 origin_halt_id;
			uint16 target_halt_id;
			uint16 transport_idx;
			uint16 comfort;
			uint32 weight;
			uint32 journey_time;	// actual ride time, used by the comfort-allowance reset check
		};
		vector_tpl<full_connexion_t> full_connexions;

		// Compact routing-search graph: per-origin outgoing edges and the
		// expanded (halt, arriving transport) state space. Rebuilt from
		// full_connexions before the search.
		struct full_edge_t
		{
			uint16 target;
			uint16 transport;
			uint16 comfort;
			uint32 weight;
			uint32 journey_time;	// actual ride time, used by the comfort-allowance reset check
			uint32 target_state;
		};
		full_edge_t *full_edges;
		uint32 full_edge_count;
		uint32 *out_offset;		// one per working halt plus a terminator
		uint32 *out_edge;		// build-time edge indices grouped by origin
		full_edge_t *out_edges;	// search-time edges grouped by origin
		uint32 *state_offset;	// one per working halt plus a terminator
		uint16 *state_halt;
		uint32 *state_key;		// transport index; 0xFFFFFFFF for the origin state
		uint16 *transport_comfort;	// maximum comfortable journey time per transport index
		uint8 *state_arriving_continues;	// quick check: the arriving transport has an outgoing edge at this halt
		uint32 *state_arriving_edge_begin;	// start of the arriving transport's outgoing edges in out_edges
		uint32 *state_arriving_edge_end;	// end of the arriving transport's outgoing edges in out_edges

		// Per-state lookup used by the rule that prevents changing transport
		// only to get a fresh comfortable-time allowance: maps each destination
		// served by the arriving transport to the actual ride time. A small hash
		// table with linear probing, sized to twice the number of served
		// destinations so it stays small enough to remain in cache.
		struct arriving_target_t
		{
			uint32 target;
			uint32 journey_time;
		};
		arriving_target_t *arriving_targets;
		uint32 *state_arriving_table_offset;
		uint32 *state_arriving_table_mask;
		uint32 state_count;

		// Per-thread Dijkstra scratch. The heap stores heap_item_t values
		// directly; one heap per pool thread.
		struct heap_item_t
		{
			uint32 dist;
			uint32 state;
			// Dereferencing hook required by binary_heap_tpl's comparisons.
			const heap_item_t &operator*() const { return *this; }
			bool operator<=(const heap_item_t &other) const
			{
				return dist < other.dist || (dist == other.dist && state <= other.state);
			}
		};
		uint32 *thread_dist;
		uint16 *thread_first_hop;
		binary_heap_tpl<heap_item_t> *heaps;
		uint32 thread_scratch_count;

		// set of variables for full halt list
		halthandle_t *all_halts_list;
		uint16 all_halts_count;

		// a vector for storing lines and lineless convoys
		vector_tpl<linkage_t> *linkages;

		// set of variables for transfer list
		uint16 *transfer_list;
		uint16 transfer_count;

		uint8 catg;				// category managed by this compartment
		uint8 g_class;			// Class managed by this compartment
		const char *catg_name;	// Name of the category
		char *class_name;		// Name of the class
		uint16 step_count;		// number of steps done so far for a path refresh request

		// coordination flags
		bool paths_available;
		bool refresh_completed;
		bool refresh_requested;

		// Current refresh step.
		uint8 current_phase;

		// Per-step counters.
		uint16 phase_counter;
		uint32 iterations;
		uint64 total_iterations; // for desync debug only

		// Routing-search resume position: the next origin (working index) to
		// search.
		uint16 origin_index;

		// statistics for determining limits
		uint32 statistic_duration;
		uint32 statistic_iteration;

#ifdef MULTI_THREAD
		// Worker-pool state for the routing-search origin scan. The pool has
		// pool_thread_count threads: the caller of pool_run() (thread index 0,
		// the lead) and pool_thread_count - 1 parked helpers.
		struct pool_batch_t
		{
			compartment_t *compartment;
			uint32 origin_count;
		};

		static pthread_mutex_t pool_mutex;
		static pthread_mutex_t pool_work_mutex;
		static pthread_cond_t pool_start_cond;
		static pthread_cond_t pool_done_cond;
		static pool_batch_t pool_batch;
		static uint32 pool_thread_count;
		static uint32 pool_generation;
		static uint32 pool_outstanding;
		static uint32 pool_next_origin;
		static uint32 pool_end_origin;
		static bool pool_terminating;
		static bool pool_initialised;
#endif

		// Run one batch of origins: the caller and the helpers claim origins
		// from the batch until it is exhausted, then the caller waits for the
		// helpers; the batch is the exact set the single-threaded loop would
		// process in this step.
		static void pool_run(compartment_t *compartment, const uint16 origin_begin, const uint32 origin_count);

		// Process this thread's slice of one origin batch (serial path).
		void run_origin_slice(const uint32 thread_index, const uint32 thread_count,
			const uint16 origin_begin, const uint32 origin_count);

		// Claim and process origins from the current pool batch until exhausted.
#ifdef MULTI_THREAD
		void run_origin_batch(const uint32 thread_index);
#endif

		// One per-origin Dijkstra over the expanded state graph, writing one
		// matrix row.
		void run_origin(const uint16 origin, const uint32 thread_index);

		// Whether an equal- or lower-comfort change to `target` must be blocked
		// because the arriving transport also serves that target and staying on
		// it is no slower than the candidate's full perceived cost.
		bool reboarding_blocked(uint32 state, uint32 target, uint32 candidate_weight) const;

		// Build and release the routing-search graph and thread scratch.
		void build_search_graph();
		void clear_search_graph();
		void free_thread_scratch();

		// Waiting + journey + transfer, saturating at UINT32_MAX_VALUE. The
		// runtime-table deduplication compares this full weight so that a
		// walking connexion, which has no waiting time, is not replaced by a
		// slower line.
		static uint32 get_connexion_weight(const haltestelle_t::connexion &cnx);

		// Append one full stop-to-stop candidate before deduplication.
		void add_full_connexion(const halthandle_t origin, const halthandle_t target,
			const haltestelle_t::connexion &cnx);

		// Names of the refresh steps.
		static const char *const phase_name[];

protected:
		// an array for keeping a list of connexion hash table
		static connexion_list_entry_t connexion_list[65536];

private:

		// iteration representative
		static uint16 representative_halt_count;
		static uint8 representative_category;

		// Whether per-step iteration limits are used. Turned off for the
		// initial full instant search.
		static bool use_limits;

		// iteration limitslinkages
		static uint32 limit_rebuild_connexions;
		static uint32 limit_filter_eligible;
		static uint32 limit_fill_matrix;
		static uint64 limit_explore_paths;
		static uint32 limit_reroute_goods;

		// local iteration limits
		static uint32 local_rebuild_connexions;
		static uint32 local_filter_eligible;
		static uint32 local_fill_matrix;
		static uint64 local_explore_paths;
		static uint32 local_reroute_goods;

		// indicate whether local limits has changed
		static bool local_limits_changed;

		// default iteration limits
		static const uint32 default_rebuild_connexions  = 0x0400;
		static const uint32 default_filter_eligible = 0x00018000;
		static const uint32 default_fill_matrix     = 0x00010000;
		// Routing-refresh search budget in origins per step.
		static const uint64 default_explore_paths   = 16;
		static const uint32 default_reroute_goods       = 0x0100;

		// Refresh step indices.
		static const uint8 phase_check_flag = 0;
		static const uint8 phase_init_prepare = 1;
		static const uint8 phase_rebuild_connexions = 2;
		static const uint8 phase_filter_eligible = 3;
		static const uint8 phase_fill_matrix = 4;
		static const uint8 phase_explore_paths = 5;
		static const uint8 phase_reroute_goods = 6;

		// absolute time limits
		// The higher this number, the more processing will be done per step and the more quickly that a refresh will complete, but the more computationally intensive that it will be.
		// Knightly's original setting was 24. The revised setting was 64.
		// Now set by simuconf.tab.
		static uint32 time_midpoint;
		static const uint32 time_deviation = 2;
		static uint32 time_lower_limit;
		static uint32 time_upper_limit;
		static uint32 time_threshold;

		// percentage time limits
		static const uint32 percent_deviation = 5;
		static const uint32 percent_lower_limit = 100 - percent_deviation;
		static const uint32 percent_upper_limit = 100 + percent_deviation;

		enum schedule_flags
		{
			recurrence = (1u << 0),
			pick_up_only = (1u << 1),
			set_down_only = (1u << 2),
			layover = (1u << 3),
			discharge_payload = (1u << 4),
			does_not_carry_from = (1u << 5),
			does_not_carry_to = (1u << 6)
		};

		bool is_flag_set(uint8& data, schedule_flags flag) const { return data & flag; }

		void set_flag(uint8& data, schedule_flags flag) { data |= flag; }

		void clear_flag(uint8& data, schedule_flags flag) { data &= ~flag; }

	public:

#ifdef MULTI_THREAD
		static void pool_initialise(const uint32 thread_count);
		static void pool_shutdown();
		static void pool_destroy();
		static void pool_worker(const uint32 thread_index);
#endif

		compartment_t();
		~compartment_t();

		static void initialise();
		static void finalise();

		void rdwr(loadsave_t* file);

		static void set_absolute_limits();

		void step();
		void reset(const bool reset_finished_set);

		bool are_paths_available() const { return paths_available; }
		bool is_refresh_completed() const { return refresh_completed; }
		bool is_refresh_requested() const { return refresh_requested; }

		// Note that these are only used for the client/server synchronisation checklist for diagnostic purposes.
		uint8 get_current_phase() const { return current_phase; }
		uint16 get_phase_counter() const { return phase_counter; }
		uint16 get_working_halt_count() const { return working_halt_count; }
		uint16 get_all_halt_count() const { return all_halts_count; }
		uint16 get_transfer_count() const { return transfer_count; }
		uint64 get_total_iterations() { const uint64 ti = total_iterations; total_iterations = 0; return ti; }

		void set_category(uint8 category);
		void set_class(uint8 value);
		void set_refresh() { refresh_requested = true; }

		bool get_path_between(const halthandle_t origin_halt, const halthandle_t target_halt,
							  uint32 &aggregate_time, halthandle_t &next_transfer);

		const char *get_category_name() const { return ( catg_name ? catg_name : "" ); }
		const char *get_class_name() const { return ( class_name ? class_name : "" );  }
		const char *get_current_phase_name() const { return phase_name[current_phase]; }

		static void initialise_connexion_list();

		static void reset_connexion_entry(const uint16 halt_id);

		static void reset_connexion_list();

		static void finalise_connexion_list();

		static void enable_limits(const bool yesno)
		{
			use_limits = yesno;
		}

		static limit_set_t get_local_limits()
		{
			return limit_set_t( local_rebuild_connexions, local_filter_eligible, local_fill_matrix, local_explore_paths, local_reroute_goods );
		}

		static limit_set_t get_active_limits()
		{
			return limit_set_t( limit_rebuild_connexions, limit_filter_eligible, limit_fill_matrix, limit_explore_paths, limit_reroute_goods );
		}

		// This is only used in network mode.
		static void set_limits(const limit_set_t &limit_set)
		{
			limit_rebuild_connexions = limit_set.rebuild_connexions;
			limit_filter_eligible = limit_set.filter_eligible;
			limit_fill_matrix = limit_set.fill_matrix;
			limit_explore_paths = limit_set.explore_paths;
			limit_reroute_goods = limit_set.reroute_goods;
		}

		static void set_default_limits()
		{
			limit_rebuild_connexions = default_rebuild_connexions;
			limit_filter_eligible = default_filter_eligible;
			limit_fill_matrix = default_fill_matrix;
			limit_explore_paths = default_explore_paths;
			limit_reroute_goods = default_reroute_goods;
		}

		static bool are_local_limits_changed() { return local_limits_changed; }
		static void reset_local_limits_state() { local_limits_changed = false; }
		static uint32 get_limit_rebuild_connexions() { return limit_rebuild_connexions; }
		static uint32 get_limit_filter_eligible() { return limit_filter_eligible; }
		static uint32 get_limit_fill_matrix() { return limit_fill_matrix; }
		static uint64 get_limit_explore_paths() { return limit_explore_paths; }
		static uint32 get_limit_reroute_goods() { return limit_reroute_goods; }

	};

	static karte_t *world;
	static uint8 max_categories;
	static uint8 max_classes;
	static uint8 category_empty;
public:
	static compartment_t **goods_compartment;
private:
	static uint8 current_compartment_category;
	static uint8 current_compartment_class;
	static bool processing;

public:
#ifdef MULTI_THREAD
	static thread_local bool allow_path_explorer_on_this_thread;
	friend void *path_explorer_threaded(void* args);

	// Dedicated routing-search worker pool. The existing path explorer thread is the lead;
	// the helpers are parked on the pool condition variable and are released per
	// origin batch by the lead.
	static void initialise_thread_pool(const uint32 thread_count);
	static void shutdown_thread_pool();
	static void destroy_thread_pool();
	static void worker_thread(const uint32 thread_index);
#endif
	static void initialise(karte_t *welt);
	static void finalise();
	static void step();
	static void next_compartment();

	static void full_instant_refresh();
	static void refresh_all_categories(const bool reset_working_set);
	static void refresh_category(const uint8 category);
	static void refresh_class_category(const uint8 category, const uint8 g_class);
	static bool get_catg_path_between(const uint8 category, const halthandle_t origin_halt, const halthandle_t target_halt,
									  uint32 &aggregate_time, halthandle_t &next_transfer, uint8 g_class = 0)
	{
		return goods_compartment[category][g_class].get_path_between(origin_halt, target_halt, aggregate_time, next_transfer);
	}

	// Mean comfort times over a line's convoys, as used by the route table for
	// this class. A convoy without a line uses its own values.
	static uint16 get_linkage_maximum_comfortable_journey(const linehandle_t &line, const convoihandle_t &convoy, uint8 g_class);
	static uint16 get_linkage_maximum_tolerable_journey(const linehandle_t &line, const convoihandle_t &convoy, uint8 g_class);

	static karte_t *get_world() { return world; }
	static bool are_local_limits_changed() { return compartment_t::are_local_limits_changed(); }
	static void reset_local_limits_state() { compartment_t::reset_local_limits_state(); }
	static limit_set_t get_local_limits() { return compartment_t::get_local_limits(); }
	static limit_set_t get_active_limits() { return compartment_t::get_active_limits(); }
	static void set_limits(const limit_set_t &limit_set) { compartment_t::set_limits(limit_set); }
	static uint32 get_limit_rebuild_connexions() { return compartment_t::get_limit_rebuild_connexions(); }
	static uint32 get_limit_filter_eligible() { return compartment_t::get_limit_filter_eligible(); }
	static uint32 get_limit_fill_matrix() { return compartment_t::get_limit_fill_matrix(); }
	static uint64 get_limit_explore_paths() { return compartment_t::get_limit_explore_paths(); }
	static uint32 get_limit_reroute_goods() { return compartment_t::get_limit_reroute_goods(); }
	static bool is_processing() { return processing; }
	static const char *get_current_category_name() { return goods_compartment[current_compartment_category][current_compartment_class].get_category_name(); }
	static const char *get_current_class_name() { return  goods_compartment[current_compartment_category][current_compartment_class].get_class_name(); }
	static const char *get_current_phase_name() { return goods_compartment[current_compartment_category][current_compartment_class].get_current_phase_name(); }

	// Note that these are only used for the client/server synchronisation checklist for diagnostic purposes.
	static uint8 get_current_compartment_category() { return current_compartment_category; }
	static uint8 get_current_compartment_class() { return current_compartment_class;  }
	static uint8 get_max_categories() { return max_categories; }
	static bool get_paths_available(uint8 catg, uint8 g_class = 0) { return goods_compartment[catg][g_class].are_paths_available(); }
	static bool get_refresh_completed(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].is_refresh_completed(); }
	static bool get_refresh_requested(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].is_refresh_requested(); }
	static uint8 get_current_phase(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_current_phase(); }
	static uint16 get_phase_counter(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_phase_counter(); }
	static uint16 get_working_halt_count(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_working_halt_count(); }
	static uint16 get_all_halt_count(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_all_halt_count(); }
	static uint16 get_transfer_count(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_transfer_count(); }
	static uint64 get_total_iterations(uint8 catg, uint8 g_class) { return goods_compartment[catg][g_class].get_total_iterations(); }

	inline static void set_absolute_limits_external() { compartment_t::set_absolute_limits();  }

	inline static bool get_must_refresh_on_loading() { return must_refresh_on_loading; }
	inline static void set_must_refresh_on_loading() { must_refresh_on_loading = true; }
	inline static void reset_must_refresh_on_loading() { must_refresh_on_loading = false; }
};

#endif

