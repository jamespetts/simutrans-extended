/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef UTILS_CBUFFER_T_H
#define UTILS_CBUFFER_T_H


#include <stdarg.h>
#include <stdlib.h>

#include "../simtypes.h"
/**
 * A character buffer. Main operation is 'append'
 */
class cbuffer_t
{
private:

	size_t capacity;

	/**
	* Number of characters without(!) trailing '\0'
	*/
	size_t size;

	char *buf;

	/**
	 * Implementation for copy constructor and copy assignment operator
	 */
	void copy( const cbuffer_t& cbx );

	/**
	 * Implementation for destructor and copy assignment operator
	 */
	void free();

public:
	/**
	 * Number of characters without(!) trailing '\0'
	 */
	int len() const { return size; }

	/**
	 * Creates a new cbuffer with capacity cap
	 */
	cbuffer_t();
	~cbuffer_t();

	/**
	 * Copy constructor
	 */
	cbuffer_t(const cbuffer_t& cbx);

	cbuffer_t(const char*);

	/**
	 * Copy assignment operator
	 */
	cbuffer_t& operator= (const cbuffer_t& cbx);

	/**
	 * Clears the buffer
	 */
	void clear();


	/**
	 * Appends text. Buffer will be extended if it does not have enough capacity.
	 */
	void append(const char * text);

	/**
	 * Appends text, at most n characters worth. Buffer will be extended if needed.
	 * maxchars should NOT include the null at the end of the string!
	 *  (e.g. it should be equivalent to the output of strlen())
	 */
	void append(const char* text, size_t maxchars);

	/**
	 * Return contents of buffer
	 */
	const char* get_str() const;

	/**
	 * Appends a signed 32 bit integer. Buffer will be extended if it does not have enough capacity.
	 */
	void append(long n);

	/**
 * Appends an unsigned 32 bit integer. Buffer will be extended if it does not have enough capacity.
 */
	void append_u(uint32 n);

	/**
	* Append a number represented by a fixed number of
	* characters sufficient for 8-bit precision (3)
	*/
	void append_fixed(uint8 n);

	/**
	* Append a number represented by a fixed number of
	* characters sufficient for 16-bit precision (5)
	*/
	void append_fixed(uint16 n);

	/**
	* Append a number represented by a fixed number of
	* characters sufficient for 32-bit precision (10)
	*/
	void append_fixed(uint32 n);

	/**
	* Appends a 0 if false or 1 if true.
	*/
	void append_bool(bool value);

	/**
	 * Appends a number. Buffer will be extended if it does not have enough capacity.
	 */
	void append(double n, int precision);

	/**
	 * appends formatted money string
	 */
	void append_money(double money);

	/* Append formatted text to the buffer */
	void printf(const char *fmt, ...);

	void vprintf(const char *fmt,  va_list args );

	/* enlarge the buffer if needed (i.e. size+by_amount larger than capacity) */
	void extend(unsigned int by_amount);

	// removes trailing whitespaces
	void trim();

	/**
	 * Automagic conversion to a const char* for backwards compatibility
	 */
	operator const char *() const {return buf;}

	/// checks whether format specifiers in @p translated match those in @p master
	static bool check_and_repair_format_strings(const char* master, const char* translated, char **repaired = NULL);

	static uint8 decode_uint8(const char* &p);
	static uint16 decode_uint16(const char* &p);
	static uint32 decode_uint32(const char* &p);
	static bool decode_bool(const char* &p);
};

#endif
