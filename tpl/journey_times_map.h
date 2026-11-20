/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef TPL_JOURNEY_TIMES_MAP_H
#define TPL_JOURNEY_TIMES_MAP_H


#include <algorithm>

#include "../simtypes.h"
#include "vector_tpl.h"


/**
 * Point-to-point average journey times, keyed by a pair of halt ids.
 *
 * A flat open-addressed table. All entries are held in one contiguous array,
 * and a separate byte per slot records whether that slot is empty, occupied or
 * a tombstone left by remove(). No halt pair therefore has to be reserved to
 * mark an unused slot. A key whose hash names an occupied slot is stored in
 * the next free slot of the array (linear probing). The array size is a power
 * of two, and the number of occupied slots plus tombstones is kept at or below
 * three quarters of it, so the search for an absent key always ends at an
 * empty slot. A table that would exceed that limit is rebuilt: doubled when
 * the occupied count is the reason, and otherwise rebuilt at the same size,
 * which drops the tombstones.
 *
 * A lookup returns the value stored for its key whatever the slot layout. The
 * layout depends only on the order of insertions and removals, so a given
 * history always yields the same iteration order. Iteration is in slot order;
 * each element exposes its key as id_pair and its value as
 * average_tpl<uint32>.
 *
 * This class is not thread-safe.
 */
class journey_times_map
{
public:
	struct entry_t
	{
		id_pair key;
		average_tpl<uint32> value;
	};

private:
	enum slot_state_t : uint8
	{
		SLOT_EMPTY = 0,
		SLOT_OCCUPIED = 1,
		SLOT_TOMBSTONE = 2
	};

	enum { MINIMUM_CAPACITY = 16 };

	entry_t *slots;
	uint8 *states;
	uint32 capacity;  // Always a power of two, or zero while nothing is stored.
	uint32 used;      // Occupied slots plus tombstones.
	uint32 count;     // Occupied slots.

	journey_times_map(const journey_times_map &);
	journey_times_map &operator =(const journey_times_map &);

	static uint32 get_hash(const id_pair key)
	{
		uint32 hash = ((uint32)key.x << 16) | key.y;
		hash ^= hash >> 16;
		hash *= 0x7feb352du;
		hash ^= hash >> 15;
		return hash;
	}

	/// Returns the slot holding key, or capacity if the table has no such entry.
	uint32 find(const id_pair key) const
	{
		uint32 index = capacity == 0 ? 0 : get_hash(key) & (capacity - 1);
		for (uint32 probe = 0; probe < capacity; probe++)
		{
			if (states[index] == SLOT_EMPTY)
			{
				return capacity;
			}
			if (states[index] == SLOT_OCCUPIED && slots[index].key == key)
			{
				return index;
			}
			index = (index + 1) & (capacity - 1);
		}
		return capacity;
	}

	/// Returns the slot into which key would be inserted: the first tombstone
	/// that the search passes, or the empty slot at which the search ends.
	uint32 insertion_slot(const id_pair key) const
	{
		uint32 index = get_hash(key) & (capacity - 1);
		uint32 first_tombstone = capacity;
		for (uint32 probe = 0; probe < capacity; probe++)
		{
			if (states[index] == SLOT_EMPTY)
			{
				return first_tombstone == capacity ? index : first_tombstone;
			}
			if (states[index] == SLOT_TOMBSTONE && first_tombstone == capacity)
			{
				first_tombstone = index;
			}
			index = (index + 1) & (capacity - 1);
		}
		return first_tombstone;
	}

	static bool entry_less(const entry_t &a, const entry_t &b)
	{
		if (a.key.x != b.key.x)
		{
			return a.key.x < b.key.x;
		}
		return a.key.y < b.key.y;
	}

	void rehash(const uint32 new_capacity)
	{
		entry_t *const old_slots = slots;
		uint8 *const old_states = states;
		const uint32 old_capacity = capacity;
		slots = new entry_t[new_capacity];
		states = new uint8[new_capacity];
		capacity = new_capacity;
		used = 0;
		for (uint32 i = 0; i < capacity; i++)
		{
			states[i] = SLOT_EMPTY;
		}
		for (uint32 i = 0; i < old_capacity; i++)
		{
			if (old_states[i] == SLOT_OCCUPIED)
			{
				const uint32 index = insertion_slot(old_slots[i].key);
				slots[index] = old_slots[i];
				states[index] = SLOT_OCCUPIED;
				used++;
			}
		}
		delete[] old_slots;
		delete[] old_states;
	}

public:
	journey_times_map() : slots(NULL), states(NULL), capacity(0), used(0), count(0) {}

	~journey_times_map()
	{
		delete[] slots;
		delete[] states;
	}

	uint32 get_count() const
	{
		return count;
	}

	const average_tpl<uint32> &get(const id_pair key) const
	{
		static average_tpl<uint32> nix;
		const uint32 index = find(key);
		return index == capacity ? nix : slots[index].value;
	}

	average_tpl<uint32> *access(const id_pair key)
	{
		const uint32 index = find(key);
		return index == capacity ? NULL : &slots[index].value;
	}

	bool is_contained(const id_pair key) const
	{
		return find(key) != capacity;
	}

	/// Appends every entry to out in ascending key order: x first, then y.
	/// Serialization uses this order so that the saved byte order depends only
	/// on the table's contents, not on its slot layout.
	void get_sorted_entries(vector_tpl<entry_t> &out) const
	{
		out.clear();
		out.resize(count);
		for (const_iterator iter = begin(); iter != end(); ++iter)
		{
			out.append(*iter);
		}
		std::sort(out.begin(), out.end(), entry_less);
	}

	/// Inserts a new value; fails and leaves an existing value unchanged if
	/// the key is already present.
	bool put(const id_pair key, const average_tpl<uint32> &value)
	{
		if (find(key) != capacity)
		{
			return false;
		}
		if (capacity == 0 || (used + 1) * 4 > capacity * 3)
		{
			// A full table grows. A table that is only crowded by tombstones
			// is rebuilt at the same size, which drops them.
			const bool grow = capacity == 0 || (count + 1) * 4 > capacity * 3;
			rehash(grow ? (capacity == 0 ? MINIMUM_CAPACITY : capacity * 2) : capacity);
		}
		const uint32 index = insertion_slot(key);
		if (states[index] != SLOT_TOMBSTONE)
		{
			used++;
		}
		slots[index].key = key;
		slots[index].value = value;
		states[index] = SLOT_OCCUPIED;
		count++;
		return true;
	}

	void remove(const id_pair key)
	{
		const uint32 index = find(key);
		if (index == capacity)
		{
			return;
		}
		states[index] = SLOT_TOMBSTONE;
		count--;
	}

	void clear()
	{
		if (capacity != 0)
		{
			for (uint32 i = 0; i < capacity; i++)
			{
				states[i] = SLOT_EMPTY;
			}
		}
		used = 0;
		count = 0;
	}

	class iterator
	{
	public:
		entry_t &operator *() const { return map->slots[index]; }
		entry_t *operator ->() const { return &map->slots[index]; }

		iterator &operator ++()
		{
			index++;
			skip();
			return *this;
		}

		bool operator ==(const iterator &other) const { return map == other.map && index == other.index; }
		bool operator !=(const iterator &other) const { return !(*this == other); }

	private:
		friend class journey_times_map;

		iterator(journey_times_map *const map, const uint32 index) : map(map), index(index) { skip(); }

		void skip()
		{
			while (index < map->capacity && map->states[index] != SLOT_OCCUPIED)
			{
				index++;
			}
		}

		journey_times_map *map;
		uint32 index;
	};

	class const_iterator
	{
	public:
		const entry_t &operator *() const { return map->slots[index]; }
		const entry_t *operator ->() const { return &map->slots[index]; }

		const_iterator &operator ++()
		{
			index++;
			skip();
			return *this;
		}

		bool operator ==(const const_iterator &other) const { return map == other.map && index == other.index; }
		bool operator !=(const const_iterator &other) const { return !(*this == other); }

	private:
		friend class journey_times_map;

		const_iterator(const journey_times_map *const map, const uint32 index) : map(map), index(index) { skip(); }

		void skip()
		{
			while (index < map->capacity && map->states[index] != SLOT_OCCUPIED)
			{
				index++;
			}
		}

		const journey_times_map *map;
		uint32 index;
	};

	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, capacity); }
	const_iterator begin() const { return const_iterator(this, 0); }
	const_iterator end() const { return const_iterator(this, capacity); }
};


#endif
