/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "checklist.h"

#include "../dataobj/environment.h"
#include "../network/memory_rw.h"
#include "../utils/cbuffer_t.h"

#include <cstring>


checklist_t::checklist_t() :
	hash(0),
	random_seed(0),
	halt_entry(0),
	line_entry(0),
	convoy_entry(0),
	ss(0),
	st(0),
	nfc(0)
{
	for(  uint8 i = 0;  i < CHK_RANDS;  i++  ) {
		rand[i] = 0;
	}
	for(  uint8 i = 0;  i < CHK_DEBUG_SUMS;  i++  ) {
		debug_sum[i] = 0;
	}
}


checklist_t::checklist_t(const uint32 &hash) :
	hash(hash),
	random_seed(0),
	halt_entry(0),
	line_entry(0),
	convoy_entry(0),
	ss(0),
	st(0),
	nfc(0)
{
	for(  uint8 i = 0;  i < CHK_RANDS;  i++  ) {
		rand[i] = 0;
	}
	for(  uint8 i = 0;  i < CHK_DEBUG_SUMS;  i++  ) {
		debug_sum[i] = 0;
	}
}


// checklist_t::checklist_t(uint32 _ss, uint32 _st, uint8 _nfc, uint32 _random_seed, uint16 _halt_entry, uint16 _line_entry, uint16 _convoy_entry, uint32* _rands, uint32* _debug_sums)
checklist_t::checklist_t(uint32 _ss, uint32 _st, uint8 _nfc, uint32 _random_seed, uint16 _halt_entry, uint16 _line_entry, uint16 _convoy_entry, uint32 *_rands, uint32 *_debug_sums) :
	hash(0),
	random_seed(_random_seed),
	halt_entry(_halt_entry),
	line_entry(_line_entry),
	convoy_entry(_convoy_entry),
	ss(_ss),
	st(_st),
	nfc(_nfc)
{
	for(  uint8 i = 0;  i < CHK_RANDS;  i++  ) {
		rand[i] = _rands[i];
	}
	for(  uint8 i = 0;  i < CHK_DEBUG_SUMS;  i++  ) {
		debug_sum[i] = _debug_sums[i];
	}
}


bool checklist_t::operator==(const checklist_t& other) const
{
	if (hash != other.hash) {
		return false;
	}

	bool rands_equal = true;
	for(  uint8 i = 0;  i < CHK_RANDS  &&  rands_equal;  i++  ) {
		// If debug sums are too expensive, then this test below would allow them to be switched off independently at either end:
		// debugs_equal = debugs_equal  &&  (debug_sum[i] == 0  ||  other.debug_sum[i] == 0  ||  debug_sum[i] == other.debug_sum[i]);
		rands_equal = rands_equal  &&  rand[i] == other.rand[i];
	}

	bool debugs_equal = true;
	for(  uint8 i = 0;  i < CHK_DEBUG_SUMS  &&  debugs_equal;  i++  ) {
		// If debug sums are too expensive, then this test below would allow them to be switched off independently at either end:
		// debugs_equal = debugs_equal  &&  (debug_sum[i] == 0  ||  other.debug_sum[i] == 0  ||  debug_sum[i] == other.debug_sum[i]);
		debugs_equal = debugs_equal  &&  debug_sum[i] == other.debug_sum[i];
	}

	return ( rands_equal &&
		debugs_equal &&
		ss == other.ss &&
		st == other.st &&
		nfc == other.nfc &&
		random_seed == other.random_seed &&
		halt_entry == other.halt_entry &&
		line_entry == other.line_entry &&
		convoy_entry == other.convoy_entry
		);
}


void checklist_t::rdwr(memory_rw_t *buffer)
{
	buffer->rdwr_long(hash);
	buffer->rdwr_long(ss);
	buffer->rdwr_long(st);
	buffer->rdwr_byte(nfc);
	buffer->rdwr_long(random_seed);
	buffer->rdwr_short(halt_entry);
	buffer->rdwr_short(line_entry);
	buffer->rdwr_short(convoy_entry);
	// desync debug
	for(  uint8 i = 0;  i < CHK_RANDS;  i++  ) {
		buffer->rdwr_long(rand[i]);
	}
	// More desync debug - should catch desyncs earlier with little computational penalty
	for(  uint8 i = 0;  i < CHK_DEBUG_SUMS;  i++  ) {
		buffer->rdwr_long(debug_sum[i]);
	}
}


void checklist_t::print(cbuffer_t &buffer, const char *entity) const
{
	if (env_t::network_heavy_mode >=1) {
		buffer.printf("%s=[adler32=%08x]", entity, hash);
	}
	else {
		// This string is split over several lines of code to avoid bugs caused by missing format specifiers
		buffer.printf("%s=[ss=%u st=%u nfc=%u hash=%u rand=%u halt=%u line=%u cnvy=%u\n"
			"\tssr=%u,%u,%u,%u,%u,%u,%u,%u\n"
			"\tstr=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n"
			"\texr=%u,%u,%u,%u,%u,%u,%u,%u\n"
			"\tsums=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]\n",
			entity, ss, st, nfc, hash, random_seed, halt_entry, line_entry, convoy_entry,
			rand[0], rand[1], rand[2], rand[3], rand[4], rand[5], rand[6], rand[7],
			rand[8], rand[9], rand[10], rand[11], rand[12], rand[13], rand[14], rand[15], rand[16], rand[17], rand[18], rand[19], rand[20], rand[21], rand[22], rand[23],
			rand[24], rand[25], rand[26], rand[27], rand[28], rand[29], rand[30], rand[31],
			debug_sum[0], debug_sum[1], debug_sum[2], debug_sum[3], debug_sum[4], debug_sum[5], debug_sum[6], debug_sum[7], debug_sum[8], debug_sum[9]
		);
	}
}
