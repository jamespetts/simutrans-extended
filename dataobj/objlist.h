/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_OBJLIST_H
#define DATAOBJ_OBJLIST_H


#include <atomic>
#include <stdint.h>

#include "../simtypes.h"
#include "../obj/simobj.h"


/*
 * Concurrent-reader protocol (single writer, multiple readers).
 *
 * Convoy route-finding and private-car worker threads read tile object lists
 * while the main thread mutates them (vehicle hops and object deletion in
 * sync_step). The writer is always the main thread; readers are the workers
 * (and display threads, which never run concurrently with the writer).
 *
 * To keep this lock-free and race-free:
 *  - All accesses to the shared words (optr, top, array elements) go through
 *    the OLIST_ATOMIC_* accessors, so no data race can occur.
 *  - optr is a single word holding both the inline/array discriminator and the
 *    pointer, so a reader always sees a consistent pair. It is also the
 *    publication point for array contents: released on write, acquired on read
 *    (see olist_atomic_load_word/olist_atomic_store_word).
 *  - Arrays are self-describing: element 0 holds the array capacity, so the
 *    capacity a reader uses is always the one belonging to the array it read,
 *    and speculative in-flight reads can never index out of bounds.
 *  - mutation_version is a seqlock: the writer bumps it (odd) before and (even)
 *    after every structural mutation; readers retry if it changed or was odd.
 *    This gives each individual read a consistent point-in-time view.
 *  - Object destructors must never run inside a mutation_begin/end window:
 *    destructors execute arbitrary code that may read this same list (e.g.
 *    gebaeude_t::~gebaeude_t reads it via check_road_tiles()), and a
 *    re-entrant read while mutation_version is odd spins forever in the
 *    seqlock retry loop (self-deadlock). loesche_alle() therefore unlinks
 *    each object under the lock and deletes it after mutation_end().
 *  - Freed arrays and deleted objects are never recycled while a worker window
 *    is open (freelist_t quarantine), so a stale pointer always refers to valid
 *    memory for the duration of the window.
 */
#if defined(__GNUC__) || defined(__clang__)
#	define OLIST_ATOMIC_LOAD(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#	define OLIST_ATOMIC_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
/* objlist_t is GCC_PACKED, so the compiler tracks the address of its members
 * as alignment 1. For the 8-byte optr word that makes GCC/clang emit calls to
 * libatomic's lock-based fallbacks (__atomic_load_8/__atomic_store_8), which
 * both fail to link (libatomic is not linked) and would be slow. Routing the
 * optr accesses through these helpers restores natural alignment: the atomic
 * is emitted inside the function, where the parameter has its declared
 * (natural) alignment, producing a plain lock-free instruction. The alignment
 * is genuine: objlist_t is only ever instantiated as the first data member of
 * grund_t (offset 8 after the vptr) and grund_t allocations are always at
 * least pointer-aligned (freelist alignment), so optr is naturally aligned. */
/* Memory order: the optr word is the PUBLICATION point for the list contents
 * (fresh arrays are initialised with plain writes, including MEMZERON, before
 * store_array/store_single). Release-store/acquire-load on optr itself orders
 * those plain initialisation writes against the readers' content accesses.
 * This must live on the atomic access, not on standalone fences: TSan does not
 * reliably model fence-to-fence synchronisation around relaxed atomics and
 * reported the (correctly fenced) memset-vs-content-read pairs as races. */
static inline uintptr_t olist_atomic_load_word(const uintptr_t *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static inline void olist_atomic_store_word(uintptr_t *p, uintptr_t v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#else
	/* MSVC targets (x64/ARM64): naturally aligned word-sized and byte accesses
	 * are atomic; the atomics/fences of the seqlock protocol provide ordering. */
#	define OLIST_ATOMIC_LOAD(p) (*(p))
#	define OLIST_ATOMIC_STORE(p, v) ((void)(*(p) = (v)))
static inline uintptr_t olist_atomic_load_word(const uintptr_t *p) { return *p; }
static inline void olist_atomic_store_word(uintptr_t *p, uintptr_t v) { *p = v; }
#endif

/* Debug builds only: bound the seqlock read retry loops. A read that retries
 * this many times means the writer never closed its mutation window — for a
 * same-thread read that is a self-deadlock (a re-entrant read inside the
 * writer's own window, e.g. an object destructor reading the list during
 * loesche_alle()); across threads it means a stuck writer. Fail loudly with a
 * fatal instead of hanging the process silently. Release builds keep the
 * unbounded spin, which is correct for genuine cross-thread retries. */
#ifdef DEBUG
#	define OLIST_SPIN_LIMIT 0x10000000u
#	define OLIST_SPIN_CHECK(reader, counter) do { if(  ++(counter) >= OLIST_SPIN_LIMIT  ) { objlist_seqlock_spin_fatal(reader); } } while(0)
#else
#	define OLIST_SPIN_CHECK(reader, counter) ((void)(counter))
#endif

#ifdef DEBUG
/* Reports an exhausted seqlock retry bound (see OLIST_SPIN_CHECK); never returns. */
void objlist_seqlock_spin_fatal(const char *reader);
#endif

/* Taking the address of members of this packed struct (for the atomic
 * accessors) triggers -Waddress-of-packed-member on GCC/clang. The addresses
 * are safe: objlist_t is only ever instantiated as the first data member of
 * grund_t (offset 8 after the vptr) and grund_t allocations are always at
 * least 8-aligned (freelist alignment), so optr is always naturally aligned. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#endif


/**
 * All things including ways are stored in this structure.
 * The entries are packed, i.e. the first free entry is at the top.
 * To save memory, a single element (like in the case for houses or a single tree)
 * is stored directly (inline in optr). Otherwise optr points to an array whose
 * element 0 is the array capacity header and elements 1..capacity are the objects.
 * The objects are sorted according to their drawing order.
 * ways are always first.
 */
class objlist_t
{
private:
	/**
	 * Tagged pointer word holding the list contents; accessed atomically:
	 *  - bit 0 set: zero or one object, stored inline; the object pointer is
	 *    (optr & ~TAG_MASK), or empty when that is NULL.
	 *  - bit 0 clear: optr points to an obj_t* array (8-aligned); element 0 is
	 *    the capacity header (a small integer stored as a misaligned obj_t*),
	 *    elements 1..capacity are the objects.
	 */
	uintptr_t optr;

	enum { INLINE_TAG = 1, TAG_MASK = 7 };

	/**
	 * Number of items which can be stored without expanding
	 * zero indicates empty list.
	 * Only the (single-threaded) writer uses this; concurrent readers derive
	 * the capacity from the array header, which is always consistent with the
	 * array they are reading.
	 */
	uint8 capacity;

	/**
	 * 0-based index of the next free entry after the last element
	 * therefore also the count of number of items which are stored.
	 * Accessed atomically (concurrent readers).
	 */
	uint8 top;

	/**
	 * Seqlock version: bumped before (becomes odd) and after (becomes even)
	 * every structural mutation. Readers retry while it is odd or changed.
	 * Accessed atomically via the OLIST_ATOMIC_* macros (a plain uint8 rather
	 * than std::atomic because this struct is GCC_PACKED).
	 */
	uint8 mutation_version;

	// --- seqlock writer protocol (the writer is always the main thread) ---
	void mutation_begin()
	{
		OLIST_ATOMIC_STORE(&mutation_version, (uint8)(OLIST_ATOMIC_LOAD(&mutation_version) + 1));
		std::atomic_thread_fence(std::memory_order_release);
	}
	void mutation_end()
	{
		std::atomic_thread_fence(std::memory_order_release);
		OLIST_ATOMIC_STORE(&mutation_version, (uint8)(OLIST_ATOMIC_LOAD(&mutation_version) + 1));
	}

	// --- seqlock reader protocol ---
	uint8 read_version_begin() const
	{
		const uint8 v = OLIST_ATOMIC_LOAD(&mutation_version);
		std::atomic_thread_fence(std::memory_order_acquire);
		return v;
	}
	bool read_version_ok(uint8 v0) const
	{
		std::atomic_thread_fence(std::memory_order_acq_rel);
		return (v0 & 1) == 0  &&  OLIST_ATOMIC_LOAD(&mutation_version) == v0;
	}

	// --- contents accessors (safe for concurrent use) ---

	// The inline-stored object, or NULL when empty or when an array is used.
	obj_t *read_single() const
	{
		const uintptr_t w = olist_atomic_load_word(&optr);
		return (w & INLINE_TAG) ? (obj_t *)(w & ~(uintptr_t)TAG_MASK) : NULL;
	}

	// The object array (element 0 = capacity header), or NULL when inline/empty.
	obj_t *const *read_array() const
	{
		const uintptr_t w = olist_atomic_load_word(&optr);
		std::atomic_thread_fence(std::memory_order_acquire);
		return (w & INLINE_TAG) ? NULL : (obj_t *const *)w;
	}
	obj_t **write_array() const
	{
		// writer-only variant without the fence
		return (optr & INLINE_TAG) ? NULL : (obj_t **)optr;
	}

	static uint8 array_capacity(obj_t *const *a)
	{
		return (uint8)(uintptr_t)OLIST_ATOMIC_LOAD(&a[0]);
	}
	static obj_t *array_read(obj_t *const *a, uint8 index)
	{
		return OLIST_ATOMIC_LOAD(&a[1 + index]);
	}
	static void array_write(obj_t **a, uint8 index, obj_t *o)
	{
		OLIST_ATOMIC_STORE(&a[1 + index], o);
	}

	// --- writer-side state transitions (call within mutation_begin/end) ---
	void store_single(obj_t *o) // NULL = empty
	{
		std::atomic_thread_fence(std::memory_order_release);
		olist_atomic_store_word(&optr, ((uintptr_t)o) | INLINE_TAG);
	}
	void store_array(obj_t **a) // a != NULL
	{
		std::atomic_thread_fence(std::memory_order_release);
		olist_atomic_store_word(&optr, (uintptr_t)a);
	}

	void set_capacity(uint16 new_cap);

	bool grow_capacity();

	void shrink_capacity(uint8 last_index);

	inline void intern_insert_at(obj_t* new_obj, uint8 pri);

	// only used internal for loading. DO NOT USE OTHERWISE! Use add instead!
	bool append_intern(obj_t *obj);

	// this will automatically give the right order for citycars and the like ...
	bool intern_add_moving(obj_t* new_obj);

	bool add_intern(obj_t* new_obj);
	bool remove_intern(const obj_t* obj);
	obj_t *remove_last_intern();
	obj_t *unlink_last_intern(uint8 offset);
	void sort_trees_intern(uint8 index, uint8 count);

	objlist_t(objlist_t const&);
	objlist_t& operator=(objlist_t const&);

public:
	objlist_t();
	~objlist_t();

	void rdwr(loadsave_t *file,koord3d current_pos);

	obj_t * suche(obj_t::typ typ,uint8 start) const;

	// since this is often needed, it is defined here
	obj_t * get_leitung() const;
	obj_t * get_convoi_vehicle() const;

	/**
	* @param n thing index (unsigned value!)
	* @return thing at index n or NULL if n is out of bounds
	*/
	inline obj_t * bei(uint8 n) const
	{
		uint32 spin_count = 0;
		for(  ;;  ) {
			const uint8 v0 = read_version_begin();
			const uintptr_t w = olist_atomic_load_word(&optr);
			std::atomic_thread_fence(std::memory_order_acquire);
			const uint8 t = OLIST_ATOMIC_LOAD(&top);
			obj_t *result = NULL;
			if(  n < t  ) {
				if(  w & INLINE_TAG  ) {
					if(  n == 0  ) {
						result = (obj_t *)(w & ~(uintptr_t)TAG_MASK);
					}
				}
				else {
					obj_t *const *a = (obj_t *const *)w;
					// The capacity header always matches this array, so the
					// index is in bounds even if the snapshot is torn (the
					// seqlock check below then fails and we retry).
					const uint8 acap = array_capacity(a);
					if(  n < acap  ) {
						result = array_read(a, n);
					}
				}
			}
			if(  read_version_ok(v0)  ) {
				return result;
			}
			OLIST_SPIN_CHECK("objlist_t::bei", spin_count);
		}
	}

	// usually used only for copying by grund_t
	obj_t *remove_last()
	{
		mutation_begin();
		obj_t *r = remove_last_intern();
		mutation_end();
		return r;
	}

	/// This routine will automatically obey the correct order of things during insertion.
	bool add(obj_t *obj)
	{
		mutation_begin();
		const bool r = add_intern(obj);
		mutation_end();
		return r;
	}

	bool remove(const obj_t* obj)
	{
		mutation_begin();
		const bool r = remove_intern(obj);
		mutation_end();
		return r;
	}

	// Defined in objlist.cc: unlinks each object inside the seqlock write
	// window, but deletes it only after mutation_end() (see the design note
	// at the top of this file: object destructors must never run inside the
	// write window).
	bool loesche_alle(player_t *player, uint8 offset);

	// only used internal for loading. DO NOT USE OTHERWISE! Use add instead!
	bool append(obj_t *obj)
	{
		mutation_begin();
		const bool r = append_intern(obj);
		mutation_end();
		return r;
	}

	bool ist_da(const obj_t* obj) const;

	inline uint8 get_top() const {return OLIST_ATOMIC_LOAD(&top);}

	/**
	* sorts the trees according to their offsets
	*/
	void sort_trees(uint8 index, uint8 count)
	{
		mutation_begin();
		sort_trees_intern(index, count);
		mutation_end();
	}

	/**
	* @return NULL when OK, or message, why not?
	*/
	const char * kann_alle_entfernen(const player_t *,uint8 ) const;

	/** recalcs all objects on this tile
	*/
	void calc_image();

	/**
	 * Sets all objects dirty to prevent artifacts with smart hide cursor
	 */
	void set_all_dirty();

	/**
	 * Called whenever the season or snowline height changes
	 */
	void check_season(const bool calc_only_season_change);

	/** display all things, faster, but will lead to clipping errors
	 */
#ifdef MULTI_THREAD
	void display_obj_quick_and_dirty( const sint16 xpos, const sint16 ypos, const uint8 start_offset, const sint8 clip_num ) const;
#else
	void display_obj_quick_and_dirty( const sint16 xpos, const sint16 ypos, const uint8 start_offset, const bool is_global ) const;
#endif

	/**
	* display all things, called by the routines in grund_t
	*/
	uint8 display_obj_bg(const sint16 xpos, const sint16 ypos, const uint8 start_offset  CLIP_NUM_DEF) const;
	uint8 display_obj_vh(const sint16 xpos, const sint16 ypos, const uint8 start_offset, const ribi_t::ribi ribi, const bool ontile  CLIP_NUM_DEF) const;

#ifdef MULTI_THREAD
	void display_obj_fg(const sint16 xpos, const sint16 ypos, const uint8 start_offset, const sint8 clip_num ) const;

	void display_obj_overlay(const sint16 xpos, const sint16 ypos) const;
#else
	void display_obj_fg(const sint16 xpos, const sint16 ypos, const uint8 start_offset, const bool is_global ) const;
#endif
} GCC_PACKED;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif
