/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_FREELIST_H
#define DATAOBJ_FREELIST_H


#include <cstddef>

#ifdef MULTI_THREAD
#include <atomic>
#endif


/**
 * Helper class to organize small memory objects i.e. nodes for linked lists
 * and such.
 */
class freelist_t
{
public:
	static void *gimme_node( size_t size );
	static void putback_node( size_t size, void *p );

	// clears all list memories
	static void free_all_nodes();

	/*
	 * Reclamation quarantine for simulation worker windows (MULTI_THREAD only).
	 *
	 * Convoy route-finding and private-car workers read map data (tile object
	 * lists and the objects they point to) concurrently with the main thread,
	 * which mutates those structures during sync_step (vehicle hops, object
	 * deletion). A worker may therefore hold a pointer to memory that the main
	 * thread has just freed; immediate recycling of that memory (freelist node
	 * reuse, objlist array reallocation) lets a reader observe overwritten
	 * contents (e.g. a free-list next pointer in place of a vtable), which
	 * crashed intermittently (TSan-verified).
	 *
	 * While at least one map-reader window is open (depth > 0), putback_node()
	 * and deferred_delete() therefore do NOT recycle/free memory but quarantine
	 * it; the quarantine is flushed when the last window closes (a quiescent
	 * point: all simulation workers are parked at barriers). Main thread only
	 * for begin/end; putback/deferred_delete remain callable from any thread.
	 */
	static void begin_map_reader_window();
	static void end_map_reader_window();

	// Route an unsized operator delete through the quarantine (obj_t::operator delete).
	static void deferred_delete( void *p );

#ifdef MULTI_THREAD
	static bool is_quarantine_active() { return map_reader_window_depth.load( std::memory_order_acquire ) != 0; }
private:
	static std::atomic<unsigned> map_reader_window_depth;
#endif
};

#endif
