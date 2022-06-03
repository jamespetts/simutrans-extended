/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef TPL_PTRHASHTABLE_TPL_H
#define TPL_PTRHASHTABLE_TPL_H


#include "hashtable_tpl.h"
#include "../simtypes.h"


/*
 * Define the key characteristics for hashing pointers. For hashing the
 * direct value is used.
 */
template<class key_t>
class ptrhash_tpl {
public:
// define NO_INTPTR_T if it does not compile
#ifndef NO_INTPTR_T
	typedef intptr_t cast_ptr_to_t;
	typedef intptr_t diff_type;
#else
	typedef ptrdiff_t diff_type;
	typedef const char* cast_ptr_to_t;
#endif

	static uint32 hash(const key_t key)
	{
		return (uint32)(size_t)key;
	}

	static diff_type comp(key_t key1, key_t key2)
	{
		return (cast_ptr_to_t)key1 - (cast_ptr_to_t)key2;
	}
};


/**
 * A template class which implements a hashtable with pointer keys
 */
template<class key_t, class value_t, size_t n_bags>
class ptrhashtable_tpl : public hashtable_tpl<key_t, value_t, ptrhash_tpl<key_t>, n_bags>
{
public:
	ptrhashtable_tpl() : hashtable_tpl<key_t, value_t, ptrhash_tpl<key_t>, n_bags>() {}
private:
	ptrhashtable_tpl(const ptrhashtable_tpl&);
	ptrhashtable_tpl& operator=( ptrhashtable_tpl const&);
};

#endif
