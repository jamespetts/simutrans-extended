/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef TPL_WEIGHTED_VECTOR_TPL_H
#define TPL_WEIGHTED_VECTOR_TPL_H


#include <cstddef>
#include <iterator>

#include <cstddef>
#include <iterator>

#include "../macros.h"
#include "../simdebug.h"


template<class T> class weighted_vector_tpl;
template<class T> void swap(weighted_vector_tpl<T>&, weighted_vector_tpl<T>&);


/** A template class for a simple vector type */
template<class T> class weighted_vector_tpl
{
private:
	struct nodestruct
	{
		T data;
		uint32 weight;
	};

public:
	class const_iterator;

	class iterator
	{
	public:
		typedef std::forward_iterator_tag iterator_category;
		typedef std::ptrdiff_t            difference_type;
		typedef T const*                  pointer;
		typedef T const&                  reference;
		typedef T                         value_type;

		T& operator *() const { return ptr->data; }

		iterator& operator ++() { ++ptr; return *this; }

		bool operator !=(const iterator& o) { return ptr != o.ptr; }

	private:
		explicit iterator(nodestruct* ptr_) : ptr(ptr_) {}

		nodestruct* ptr;

	private:
		friend class weighted_vector_tpl;
		friend class const_iterator;
	};

	class const_iterator
	{
	public:
		typedef std::forward_iterator_tag iterator_category;
		typedef std::ptrdiff_t            difference_type;
		typedef T const*                  pointer;
		typedef T const&                  reference;
		typedef T                         value_type;

		const_iterator(const iterator& o) : ptr(o.ptr) {}

		const T& operator *() const { return ptr->data; }

		const_iterator& operator ++() { ++ptr; return *this; }

		bool operator !=(const const_iterator& o) { return ptr != o.ptr; }

	private:
		explicit const_iterator(nodestruct* ptr_) : ptr(ptr_) {}

		const nodestruct* ptr;

	private:
		friend class weighted_vector_tpl;
	};

	weighted_vector_tpl() : nodes(NULL), size(0), count(0), total_weight(0), coarse(NULL), coarse_count(0), coarse_size(0) {}

	/** Construct a vector for size elements */
	explicit weighted_vector_tpl(uint32 size)
	{
		this->size  = size;
		nodes = (size > 0 ? new nodestruct[size] : NULL);
		count = 0;
		total_weight = 0;
		coarse = NULL;
		coarse_count = 0;
		coarse_size = 0;
	}

	~weighted_vector_tpl() { delete [] nodes; delete [] coarse; }

	/** sets the vector to empty */
	void clear()
	{
		count = 0;
		total_weight = 0;
		coarse_count = 0;
	}

	/**
	 * Resizes the maximum data that can be hold by this vector.
	 * Existing entries are preserved, new_size must be big enough
	 * to hold them.
	 */
	void resize(uint32 new_size)
	{
		if (new_size <= size) return; // do nothing

		nodestruct* new_nodes = new nodestruct[new_size];
		for (uint32 i = 0; i < count; i++) new_nodes[i] = nodes[i];
		delete [] nodes;
		size  = new_size;
		nodes = new_nodes;
	}

	/**
	 * Checks if element elem is contained in vector.
	 * Uses the == operator for comparison.
	 */
	bool is_contained(T elem) const
	{
		for (uint32 i = 0; i < count; i++) {
			if (nodes[i].data == elem) return true;
		}
		return false;
	}

	/**
	 * Checks if element elem is contained in vector.
	 * Uses the == operator for comparison.
	 */
	template<typename U> uint32 index_of(U elem) const
	{
		for (uint32 i = 0; i < count; i++) {
			if (nodes[i].data == elem) return i;
		}
		dbg->fatal("weighted_vector_tpl<T>::index_of()", "not contained" );
	}

	/**
	 * Appends the element at the end of the vector.
	 * Extend if necessary.
	 */
	bool append(T elem, uint32 weight)
	{
#ifdef IGNORE_ZERO_WEIGHT
		if (weight == 0) {
			// ignore unused entries ...
			return false;
		}
#endif
		if(  count == size  ) {
			resize(size == 0 ? 1 : size * 2);
		}
		nodes[count].data   = elem;
		nodes[count].weight = total_weight;
		count++;
		total_weight += weight;
		rebuild_coarse_from(count - 1);
		return true;
	}

	/**
	 * Checks if element is contained. Appends only new elements.
	 */
	bool append_unique(T elem, uint32 weight)
	{
		return is_contained(elem) || append(elem, weight);
	}

	/** inserts data at a certain pos */
	bool insert_at(uint32 pos, T elem, uint32 weight)
	{
#ifdef IGNORE_ZERO_WEIGHT
		if (weight == 0) {
			// ignore unused entries ...
			return false;
		}
#endif
		if (pos < count) {
			if(  count==size  ) {
				resize(size == 0 ? 1 : size * 2);
			}
			for (uint32 i = count; i > pos; i--) {
				nodes[i].data   = nodes[i - 1].data;
				nodes[i].weight = nodes[i - 1].weight + weight;
			}
			nodes[pos].data = elem;
			total_weight += weight;
			count++;
			rebuild_coarse_from(pos);
			return true;
		}
		else {
			return append(elem, weight);
		}
	}

	/**
	 * Insert `elem' with respect to ordering.
	 */
	template<class StrictWeakOrdering>
	void insert_ordered(const T& elem, uint32 weight, StrictWeakOrdering comp)
	{
		if(  count==size  ) {
			resize(size == 0 ? 1 : size * 2);
		}
		sint32 high = count, low = -1;
		while(  high-low>1  ) {
			const sint32 mid = ((uint32)(high + low)) >> 1;
			if(  comp(elem, nodes[mid].data)  ) {
				high = mid;
			}
			else {
				low = mid;
			}
		}
		insert_at(high, elem, weight);
	}

	/**
	 * Only insert `elem' if not already contained in this vector.
	 * Respects the ordering and assumes the vector is ordered.
	 * Returns NULL if insertion is successful;
	 * Otherwise return the address of the element in conflict.
	 */
	template<class StrictWeakOrdering>
	T* insert_unique_ordered(const T& elem, uint32 weight, StrictWeakOrdering comp)
	{
		if(  count==size  ) {
			resize(size == 0 ? 1 : size * 2);
		}
		sint32 high = count, low = -1;
		while (high - low > 1) {
			const sint32 mid = ((uint32)(high + low)) >> 1;
			T &mid_elem = nodes[mid].data;
			if (elem == mid_elem) {
				return &mid_elem;
			}
			else if (comp(elem, mid_elem)) {
				high = mid;
			}
			else {
				low = mid;
			}
		}
		insert_at(high, elem, weight);
		return NULL;
	}

	/**
		* Update the weight of the element, if contained
		* @author Knightly
		*/
	bool update(T elem, uint32 weight)
	{
		for(  uint32 i=0;  i<count;  ++i  ) {
			if(  nodes[i].data==elem  ) {
				return update_at(i, weight);
			}
		}
		return false;
	}

	/**
		* Update the weight of the element at the specified position
		* @author Knightly
		*/
	bool update_at(uint32 pos, uint32 weight)
	{
		if(  pos>=count  ) {
			return false;
		}
		const uint32 start = pos;
		const uint32 elem_weight = ( pos + 1 < count ? nodes[pos + 1].weight : total_weight ) - nodes[pos].weight;
		if(  weight>elem_weight  ) {
			const uint32 delta_weight = weight - elem_weight;
			while(  ++pos<count  ) {
				nodes[pos].weight += delta_weight;
			}
			total_weight += delta_weight;
		}
		else if(  weight<elem_weight  ) {
			const uint32 delta_weight = elem_weight - weight;
			while(  ++pos<count  ) {
				nodes[pos].weight -= delta_weight;
			}
			total_weight -= delta_weight;
		}
		rebuild_coarse_from(start);
		return true;
	}

	/**
		* Update the weights of all elements.  The new weight of each element is
		* retrieved from get_weight().
		*/
	template<typename U> void update_weights(U& get_weight)
	{
		uint32 sum = 0;
		for (nodestruct* i = nodes, * const end = i + count; i != end; ++i) {
			i->weight = sum;
			sum      += get_weight(i->data);
		}
		total_weight = sum;
		rebuild_coarse_from(0);
	}

	/** removes element, if contained */
	bool remove(T elem)
	{
		for (uint32 i = 0; i < count; i++)
		{
			if (nodes != NULL && nodes[i].data == elem) return remove_at(i);
		}
		return false;
	}

	/** removes all copies of element, if contained */
	bool remove_all(T elem)
	{
		bool any_to_remove = false;
		for (uint32 i = 0; i < count; i++)
		{
			if(nodes != NULL && nodes[i].data == elem)
			{
				any_to_remove = remove_at(i);
				i--;
			}
		}
		return any_to_remove;
	}

	/** removes element at position */
	bool remove_at(uint32 pos)
	{
		if (pos >= count) return false;
		// get the change in the weight; must check, if it isn't the last element
		const uint32 diff_weight = ( pos + 1 < count ? nodes[pos + 1].weight : total_weight ) - nodes[pos].weight;
		for (uint32 i = pos; i < count - 1; i++) {
			nodes[i].data   = nodes[i + 1].data;
			nodes[i].weight = nodes[i + 1].weight - diff_weight;
		}
		count--;
		total_weight -= diff_weight;
		rebuild_coarse_from(pos);
		return true;
	}

	T& pop_back()
	{
		assert(count>0);
		--count;
		total_weight = nodes[count].weight;
		coarse_count = (count + COARSE_BUCKET - 1) >> COARSE_BUCKET_SHIFT;
		return nodes[count].data;
	}

	T& operator [](uint32 i)
	{
		if (i >= count) dbg->fatal("weighted_vector_tpl<T>::get()", "index out of bounds: %i not in 0..%d", i, count - 1);
		return nodes[i].data;
	}

	const T& operator [](uint32 i) const
	{
		if (i >= count) dbg->fatal("weighted_vector_tpl<T>::get()", "index out of bounds: %i not in 0..%d", i, count - 1);
		return nodes[i].data;
	}

	T& front() { return nodes[0].data; }

	/** returns the weight at a position */
	uint32 weight_at(uint32 pos) const
	{
		return (pos < count) ? nodes[pos].weight : total_weight + 1;
	}

	/** Accesses the element at position i by weight */
	T& at_weight(const uint32 target_weight) const
	{
		if (target_weight > total_weight) {
			dbg->fatal("weighted_vector_tpl<T>::at_weight()", "weight out of bounds: %i not in 0..%d", target_weight, total_weight);
		}
		if (coarse_count > 1) {
			// Locate the last bucket whose start weight is <= the target, then
			// scan the (at most COARSE_BUCKET) nodes inside that bucket. This
			// returns the same node as the binary search below: both select
			// the last node whose cumulative weight is at most the target.
			uint32 lo = 0;
			uint32 hi = coarse_count - 1;
			while (lo < hi) {
				const uint32 mid = (lo + hi + 1) >> 1;
				if (coarse[mid] <= target_weight) {
					lo = mid;
				}
				else {
					hi = mid - 1;
				}
			}
			uint32 pos = lo << COARSE_BUCKET_SHIFT;
			const uint32 end = (pos + COARSE_BUCKET < count) ? pos + COARSE_BUCKET : count - 1;
			while (pos < end && nodes[pos + 1].weight <= target_weight) {
				pos++;
			}
			return nodes[pos].data;
		}
		return nodes[at_weight_binary(target_weight)].data;
	}

	/** Gets the number of elements in the vector */
	uint32 get_count() const { return count; }

	/** Gets the capacity */
	uint32 get_size() const { return size; }

	/** Gets the total weight */
	uint32 get_sum_weight() const { return total_weight; }

	bool empty() const { return count == 0; }

	iterator begin() { return iterator(nodes); }
	iterator end()   { return iterator(nodes + count); }

	const_iterator begin() const { return const_iterator(nodes); }
	const_iterator end()   const { return const_iterator(nodes + count); }

private:
	/// Number of nodes summarised by one coarse entry.
	static const uint32 COARSE_BUCKET_SHIFT = 4;
	static const uint32 COARSE_BUCKET = 1u << COARSE_BUCKET_SHIFT;

	nodestruct* nodes;
	uint32 size;                  ///< Capacity
	uint32 count;                 ///< Number of elements in vector
	uint32 total_weight; ///< Sum of all weights

	/**
	 * Start weight of every COARSE_BUCKET-th node: coarse[k] is
	 * nodes[k * COARSE_BUCKET].weight. at_weight() first locates the bucket
	 * containing the target, then scans at most COARSE_BUCKET nodes, instead
	 * of chasing log2(count) scattered nodes. The array is maintained eagerly
	 * by every mutation, so concurrent readers only read it.
	 */
	uint32* coarse;
	uint32 coarse_count;  ///< Number of valid entries in coarse
	uint32 coarse_size;   ///< Allocated capacity of coarse

	void ensure_coarse_capacity(uint32 needed)
	{
		if (needed <= coarse_size) {
			return;
		}
		uint32 new_size = coarse_size == 0 ? 4 : coarse_size;
		while (new_size < needed) {
			new_size <<= 1;
		}
		uint32* new_coarse = new uint32[new_size];
		for (uint32 i = 0; i < coarse_count; i++) {
			new_coarse[i] = coarse[i];
		}
		delete [] coarse;
		coarse = new_coarse;
		coarse_size = new_size;
	}

	/// The original binary search over the cumulative weights, used for
	/// vectors that are too small to be worth the coarse index.
	uint32 at_weight_binary(const uint32 target_weight) const
	{
		uint32 diff = 1;
		sint8 counter = 0;
		while (diff <= count) {
			diff <<= 1;
			counter++;
		}
		diff >>= 1;

		uint32 pos = diff;
		while (counter-- > 0) {
			diff = (diff + 1) >> 1;
			if (pos < count && weight_at(pos) <= target_weight) {
				if (weight_at(pos + 1) > target_weight) break;
				pos += diff;
			} else {
				pos -= diff;
			}
		}
		return pos;
	}

	/**
	 * Recomputes the bucket starts from the bucket that contains @p pos.
	 * Every mutation calls this for the first index whose weight (or whose
	 * position) it changed, so the whole array is never rebuilt by an append.
	 */
	void rebuild_coarse_from(uint32 pos)
	{
		const uint32 needed = (count + COARSE_BUCKET - 1) >> COARSE_BUCKET_SHIFT;
		if (needed == 0) {
			coarse_count = 0;
			return;
		}
		ensure_coarse_capacity(needed);
		for (uint32 k = pos >> COARSE_BUCKET_SHIFT; k < needed; k++) {
			coarse[k] = nodes[k << COARSE_BUCKET_SHIFT].weight;
		}
		coarse_count = needed;
	}

	weighted_vector_tpl(const weighted_vector_tpl& other);

	weighted_vector_tpl& operator=( weighted_vector_tpl const& other );

	friend void swap(weighted_vector_tpl<T>&a, weighted_vector_tpl<T>&b)
	{
		sim::swap(a.nodes, b.nodes);
		sim::swap(a.size, b.size);
		sim::swap(a.count, b.count);
		sim::swap(a.total_weight, b.total_weight);
		sim::swap(a.coarse, b.coarse);
		sim::swap(a.coarse_count, b.coarse_count);
		sim::swap(a.coarse_size, b.coarse_size);
	}
};

#endif
