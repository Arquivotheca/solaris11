/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "ixgb.h"

/*
 * Atomically decrement a counter, but only if it will remain
 * strictly positive (greater than zero) afterwards.  We return
 * the decremented value if so, otherwise zero (in which case
 * the counter is unchanged).
 *
 * This is used for keeping track of available resources such
 * as transmit ring slots ...
 */
uint32_t
ixgb_atomic_reserve(uint32_t *count_p, uint32_t n)
{
	uint32_t oldval;
	uint32_t newval;

	/* ATOMICALLY */
	do {
		oldval = *count_p;
		newval = oldval - n;
		if (oldval <= n)
			return (0);	/* no resources left	*/
	} while (cas32(count_p, oldval, newval) != oldval);

	return (newval);
}

/*
 * Atomically increment a counter
 */
void
ixgb_atomic_renounce(uint32_t *count_p, uint32_t n)
{
	uint32_t oldval;
	uint32_t newval;

	/* ATOMICALLY */
	do {
		oldval = *count_p;
		newval = oldval + n;
	} while (cas32(count_p, oldval, newval) != oldval);
}

/*
 * Atomically clear bits in a 64-bit word, returning
 * the value it had *before* the bits were cleared.
 */
uint64_t
ixgb_atomic_clr64(uint64_t *sp, uint64_t bits)
{
	uint64_t oldval;
	uint64_t newval;

	/* ATOMICALLY */
	do {
		oldval = *sp;
		newval = oldval & ~bits;
	} while (cas64(sp, oldval, newval) != oldval);

	return (oldval);
}

/*
 * Atomically shift a 32-bit word left, returning
 * the value it had *before* the shift was applied
 */
uint32_t
ixgb_atomic_shl32(uint32_t *sp, uint_t count)
{
	uint32_t oldval;
	uint32_t newval;

	/* ATOMICALLY */
	do {
		oldval = *sp;
		newval = oldval << count;
	} while (cas32(sp, oldval, newval) != oldval);

	return (oldval);
}

uint32_t
ixgb_atomic_next(uint32_t *sp, uint32_t limit)
{
	uint32_t oldval;
	uint32_t newval;

	do {
		oldval = *sp;
		newval = NEXT(oldval, limit);
	} while (cas32(sp, oldval, newval) != oldval);

	return (oldval);
}
