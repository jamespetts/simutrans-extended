/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef UTILS_SIMRANDOM_H
#define UTILS_SIMRANDOM_H


#include "../simtypes.h"

#include <cstddef>


class loadsave_t;


uint32 get_random_seed();

uint32 setsimrand(uint32 seed, uint32 noise_seed);

/* generates a random number on [0,max-1]-interval
 * without affecting the game state
 * Use this for UI etc.
 */
uint32 sim_async_rand(const uint32 max);

/* generates a random number on [0,max-1]-interval */
#ifdef DEBUG_SIMRAND_CALLS
uint32 simrand(const uint32 max, const char* caller = "");
#else
uint32 simrand(const uint32 max, const char* = "");
#endif

/* Generates a random number on [0,max-1] interval with a normal distribution*/
#ifdef DEBUG_SIMRAND_CALLS
uint32 simrand_normal(const uint32 max, uint32 exponent, const char* caller);
#else
uint32 simrand_normal(const uint32 max, uint32 exponent, const char*);
#endif

/* generates a random number on [0,0xFFFFFFFFu]-interval */
uint32 simrand_plain();

/// reads/writes the sate of the random number generator
void simrand_rdwr(loadsave_t *file);

double perlin_noise_2D(const double x, const double y, const double persistence, const sint32 map_size = 512);

// for network debugging, i.e. finding hidden simrands in wrong places
enum {
	INTERACTIVE_RANDOM = 1 << 0,
	STEP_RANDOM        = 1 << 1,
	SYNC_STEP_RANDOM   = 1 << 2,
	LOAD_RANDOM        = 1 << 3,
	MAP_CREATE_RANDOM  = 1 << 4,
	MODAL_RANDOM       = 1 << 5
};

void set_random_mode( uint16 );
void clear_random_mode( uint16 );
uint16 get_random_mode();

// just more speed with those (generate a precalculated map, which needs only smoothing)
void init_perlin_map( sint32 w, sint32 h );
void exit_perlin_map();

/* Randomly select an entry from the given array. */
template<typename T, size_t N> T const& pick_any(T const (&array)[N])
{
	return array[simrand(N, "template<typename T, size_t N> T const& pick_any(T const (&array)[N])")];
}

/* Randomly select an entry from the given container. */
template<typename T, template<typename> class U> T const& pick_any(U<T> const& container)
{
	return container[simrand(container.get_count(), "template<typename T, template<typename> class U> T const& pick_any(U<T> const& container)")];
}

/* Randomly select an entry from the given weighted container. */
template<typename T, template<typename> class U> T const& pick_any_weighted(U<T> const& container)
{
	return container.at_weight(simrand(container.get_sum_weight(), "template<typename T, template<typename> class U> T const& pick_any_weighted(U<T> const& container)"));
}


// compute integer log10
uint32 log10( uint32 v );

uint32 log2( uint32 i );


// compute integer sqrt
uint32 sqrt_i32(uint32 num);
uint64 sqrt_i64(uint64 num);

// Fixed-point scale of the value returned by sigmoid(): it is always in the
// range [0, SIGMOID_SCALE]. Callers map a target range [min, max] by
//     min + (max - min) * sigmoid(value, upper_bound) / SIGMOID_SCALE.
constexpr uint64 SIGMOID_SCALE = 100000;

// Compute an integer sigmoid. Returns a fixed-point value in [0, SIGMOID_SCALE]
// for value in [0, upper_bound] (0 when upper_bound == 0). This is the cubic
// smoothstep f(t) = t^2 * (3 - 2t) with t = value / upper_bound: monotonic and
// saturating. It uses only integer arithmetic (integer multiply and divide
// are exact and identical on every platform), so the result is deterministic
// between network peers.
constexpr uint64 sigmoid(uint64 value, uint64 upper_bound)
{
	if (upper_bound == 0 || value == 0)
	{
		return 0;
	}
	if (value >= upper_bound)
	{
		return SIGMOID_SCALE;
	}
	// t is in [0, SIGMOID_SCALE). t * t <= 1e10 and (3 * SIGMOID_SCALE - 2 * t)
	// <= 3e5, so the product is at most ~3e15 and cannot overflow a uint64.
	const uint64 t = (value * SIGMOID_SCALE) / upper_bound;
	return (t * t * (3 * SIGMOID_SCALE - 2 * t)) / (SIGMOID_SCALE * SIGMOID_SCALE);
}

#endif
