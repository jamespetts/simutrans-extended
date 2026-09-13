/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <new>

#include "../simtypes.h"
#include "../simmem.h"
#include "freelist.h"

// define USE_VALGRIND_MEMCHECK to make
// valgrind aware of the freelist memory pool
#ifdef USE_VALGRIND_MEMCHECK
#include <valgrind/memcheck.h>
#endif

struct nodelist_node_t
{
#ifdef DEBUG_FREELIST
	unsigned magic : 16;
	unsigned free : 1;
	unsigned size : 15;
#endif
	nodelist_node_t* next;
};

#ifdef MULTI_THREAD
#include "../utils/simthread.h"
#include "../tpl/vector_tpl.h"
#include <atomic>
static pthread_mutex_t freelist_mutex = PTHREAD_MUTEX_INITIALIZER;

// Reclamation quarantine (see freelist.h): while simulation worker windows are
// open, freed memory is parked here instead of being recycled, and flushed when
// the last window closes (all workers parked).
std::atomic<unsigned> freelist_t::map_reader_window_depth(0);
struct quarantined_node_t
{
	quarantined_node_t() : size( 0 ), p( NULL ) {}
	quarantined_node_t( size_t size_, void *p_ ) : size( size_ ), p( p_ ) {}
	size_t size;
	void *p;
};
static vector_tpl<quarantined_node_t> quarantined_nodes;
static vector_tpl<void *> quarantined_objects;

// Must only be called when the quarantine depth has just reached zero.
static void flush_quarantine()
{
	vector_tpl<quarantined_node_t> nodes;
	vector_tpl<void *> objects;
	int error = pthread_mutex_lock( &freelist_mutex );
	assert(error == 0);
	nodes = quarantined_nodes;
	objects = quarantined_objects;
	quarantined_nodes.clear();
	quarantined_objects.clear();
	error = pthread_mutex_unlock( &freelist_mutex );
	assert(error == 0);
	(void)error;
	for(  uint32 i = 0;  i < nodes.get_count();  i++  ) {
		// depth is zero, so this takes the immediate path
		freelist_t::putback_node( nodes[i].size, nodes[i].p );
	}
	for(  uint32 i = 0;  i < objects.get_count();  i++  ) {
		::operator delete( objects[i] );
	}
}
#endif

// list of all allocated memory
static nodelist_node_t *chunk_list = NULL;

/* this module keeps account of the free nodes of list and recycles them.
 * nodes of the same size will be kept in the same list
 * to be more efficient, all nodes with sizes smaller than 16 will be used at size 16 (one cacheline)
 */

// if additional fixed sizes are required, add them here
// (the few request for larger ones are satisfied with xmalloc otherwise)


// for 64 bit, set this to 128
#define MAX_LIST_INDEX (128)

// list for nodes size 8...64
#define NUM_LIST ((MAX_LIST_INDEX/4)+1)

static nodelist_node_t *all_lists[NUM_LIST] = {
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL
};


// to have this working, we need chunks at least the size of a pointer
const size_t min_size = sizeof(void *);


void *freelist_t::gimme_node(size_t size)
{
	nodelist_node_t ** list = NULL;
	if(  size == 0  ) {
		return NULL;
	}

	// all sizes should be dividable by the pointer size and at least as large as a pointer
#ifdef DEBUG_FREELIST
	size = max( min_size, size + min_size);
#else
	size = max( min_size, size );
#endif
	// Round up to a multiple of the pointer size (not just of 4): the nodes are
	// carved from a chunk by advancing a base pointer in strides of size, and
	// every carved node must stay aligned for nodelist_node_t (and for payloads
	// with pointer-sized alignment). With 4-byte strides, sizes that are 4 mod 8
	// put every second node on a 4-byte-aligned address on 64-bit systems.
	size = (size + (min_size - 1)) & ~(min_size - 1);

#ifdef MULTI_THREAD
	int error = pthread_mutex_lock( &freelist_mutex );
	assert(error == 0);
	(void)error;
#endif

	// hold return value
	nodelist_node_t *tmp;
	if(  size > MAX_LIST_INDEX  ) {
		// too large: just use malloc anyway
		tmp = (nodelist_node_t *)xmalloc(size);
#ifdef MULTI_THREAD
		error = pthread_mutex_unlock( &freelist_mutex );
		assert(error == 0);
#endif
#ifdef DEBUG_FREELIST
		tmp->magic = 0xAA;
		tmp->free = 0;
		tmp->size = size/4;
#endif
		return tmp;
	}


	list = &(all_lists[size/4]);
	// need new memory?
	if(  *list == NULL  ) {
		int num_elements = 32764/(int)size;
		char* p = (char*)xmalloc(num_elements * size + sizeof(p));

#ifdef USE_VALGRIND_MEMCHECK
		// tell valgrind that we still cannot access the pool p
		VALGRIND_MAKE_MEM_NOACCESS(p, num_elements * size + sizeof(p));
#endif

		// put the memory into the chunklist for free it
		nodelist_node_t *chunk = (nodelist_node_t *)p;

#ifdef USE_VALGRIND_MEMCHECK
		// tell valgrind that we reserved space for one nodelist_node_t
		VALGRIND_CREATE_MEMPOOL(chunk, 0, false);
		VALGRIND_MEMPOOL_ALLOC(chunk, chunk, sizeof(*chunk));
		VALGRIND_MAKE_MEM_UNDEFINED(chunk, sizeof(*chunk));
#endif

		chunk->next = chunk_list;
		chunk_list = chunk;
		p += sizeof(p);
		// then enter nodes into nodelist
		for(  int i=0;  i<num_elements;  i++  ) {
			nodelist_node_t *tmp = (nodelist_node_t *)(p+i*size);
#ifdef USE_VALGRIND_MEMCHECK
			// tell valgrind that we reserved space for one nodelist_node_t
			VALGRIND_CREATE_MEMPOOL(tmp, 0, false);
			VALGRIND_MEMPOOL_ALLOC(tmp, tmp, sizeof(*tmp));
			VALGRIND_MAKE_MEM_UNDEFINED(tmp, sizeof(*tmp));
#endif
			tmp->next = *list;
			*list = tmp;
		}
	}

	// return first node of list
	tmp = *list;
	*list = tmp->next;

#ifdef USE_VALGRIND_MEMCHECK
	// tell valgrind that we now have access to a chunk of size bytes
	VALGRIND_MEMPOOL_CHANGE(tmp, tmp, tmp, size);
	VALGRIND_MAKE_MEM_UNDEFINED(tmp, size);
#endif

#ifdef MULTI_THREAD
	error = pthread_mutex_unlock( &freelist_mutex );
	assert(error == 0);
#endif

#ifdef DEBUG_FREELIST
	tmp->magic = 0x5555;
	tmp->free = 0;
	tmp->size = size/4;
#endif
	return (void *)&(tmp->next);
}


void freelist_t::putback_node( size_t size, void *p )
{
	nodelist_node_t ** list = NULL;
	if(  size==0  ||  p==NULL  ) {
		return;
	}

#ifdef MULTI_THREAD
	if(  map_reader_window_depth.load( std::memory_order_acquire ) != 0  ) {
		// Simulation workers may still hold pointers into this memory:
		// quarantine it until the last map-reader window closes.
		// NOTE: this is before size normalisation on purpose — the entry stores
		// the caller's original size and flush_quarantine() re-enters this
		// function with it, which must normalise it exactly once.
		int qerror = pthread_mutex_lock( &freelist_mutex );
		assert(qerror == 0);
		(void)qerror;
		if(  map_reader_window_depth.load( std::memory_order_acquire ) != 0  ) {
			quarantined_nodes.append( quarantined_node_t( size, p ) );
			qerror = pthread_mutex_unlock( &freelist_mutex );
			assert(qerror == 0);
			return;
		}
		// The last window closed in between: fall through to the immediate path.
		qerror = pthread_mutex_unlock( &freelist_mutex );
		assert(qerror == 0);
	}

	int error = pthread_mutex_lock( &freelist_mutex );
	assert(error == 0);
	(void)error;
#endif

	// all sizes should be dividable by the pointer size (see gimme_node)
#ifdef DEBUG_FREELIST
	size = max( min_size, size + min_size );
#else
	size = max( min_size, size );
#endif
	size = (size + (min_size - 1)) & ~(min_size - 1);


	if(  size > MAX_LIST_INDEX  ) {
		free(p);
#ifdef MULTI_THREAD
		int error = pthread_mutex_unlock( &freelist_mutex );
		assert(error == 0);
		(void)error;
#endif
		return;
	}

	list = &(all_lists[size/4]);

#ifdef USE_VALGRIND_MEMCHECK
	// tell valgrind that we keep access to a nodelist_node_t within the memory chunk
	VALGRIND_MEMPOOL_CHANGE(p, p, p, sizeof(nodelist_node_t));
	VALGRIND_MAKE_MEM_NOACCESS(p, size);
	VALGRIND_MAKE_MEM_UNDEFINED(p, sizeof(nodelist_node_t));
#endif

	// putback to first node
	nodelist_node_t *tmp = (nodelist_node_t *)p;
#ifdef DEBUG_FREELIST
	tmp = (nodelist_node_t *)((char *)p - min_size);
	assert(  tmp->magic == 0x5555  &&  tmp->free == 0  &&  tmp->size == size/4  );
	tmp->free = 1;
#endif
	tmp->next = *list;
	*list = tmp;

#ifdef MULTI_THREAD
	error = pthread_mutex_unlock( &freelist_mutex );
	assert(error == 0);
#endif
}


void freelist_t::begin_map_reader_window()
{
#ifdef MULTI_THREAD
	map_reader_window_depth.fetch_add( 1, std::memory_order_acq_rel );
#endif
}


void freelist_t::end_map_reader_window()
{
#ifdef MULTI_THREAD
	// Main thread only (called from the karte_t start_/await_ helpers).
	if(  map_reader_window_depth.fetch_sub( 1, std::memory_order_acq_rel ) == 1  ) {
		// Last window closed: all simulation workers are parked at barriers,
		// so no worker can hold a pointer to quarantined memory any more.
		flush_quarantine();
	}
#endif
}


void freelist_t::deferred_delete( void *p )
{
	if(  p == NULL  ) {
		return;
	}
#ifdef MULTI_THREAD
	if(  map_reader_window_depth.load( std::memory_order_acquire ) != 0  ) {
		int error = pthread_mutex_lock( &freelist_mutex );
		assert(error == 0);
		(void)error;
		// Re-check under the lock: the last window may have closed meanwhile,
		// in which case the quarantine has just been flushed.
		if(  map_reader_window_depth.load( std::memory_order_acquire ) != 0  ) {
			quarantined_objects.append( p );
			error = pthread_mutex_unlock( &freelist_mutex );
			assert(error == 0);
			return;
		}
		error = pthread_mutex_unlock( &freelist_mutex );
		assert(error == 0);
	}
#endif
	::operator delete( p );
}


// clears all list memories
void freelist_t::free_all_nodes()
{
	printf("freelist_t::free_all_nodes(): frees all list memory\n" );
	while(chunk_list) {
		nodelist_node_t *p = chunk_list;
		printf("freelist_t::free_all_nodes(): free node %p (next %p)\n", (void *)p, (void *)chunk_list->next);
		chunk_list = chunk_list->next;

		// now release memory
#ifdef USE_VALGRIND_MEMCHECK
		VALGRIND_DESTROY_MEMPOOL( p );
#endif
		free( p );
	}
	printf("freelist_t::free_all_nodes(): zeroing\n");
	for( int i=0;  i<NUM_LIST;  i++  ) {
		all_lists[i] = NULL;
	}
	printf("freelist_t::free_all_nodes(): ok\n");
}
