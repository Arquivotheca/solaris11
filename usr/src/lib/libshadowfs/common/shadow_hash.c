/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <assert.h>
#include <shadow_impl.h>
#include <string.h>
#include <strings.h>

/*
 * The (prime) number 137 happens to have the nice property that -- when
 * multiplied by two and added to 33 -- one gets a pretty long series of
 * primes:
 *
 *   307, 647, 1327, 2687, 5407, 10847, 21727, 43487
 *
 * And beyond 43487, the numbers in the series have few factors or are prime.
 * That is, one can have a prime number and roughly double it to get another
 * prime number -- but the series starts at 137.  A size of 137 buckets doesn't
 * particularly accommodate small hash tables, but we note that 13 also yields
 * a reasonable sequence when doubling it and adding 5:
 *
 *   13, 31, 67, 139, 283, 571
 *
 * So we start with this second sequence, crossing over to the first when
 * the size is greater than 137.  (And when reducing the size of the hash
 * table, we cross back when the size gets below 67.)
 */
#define	SHADOW_HASHCROSSOVER	137
#define	SHADOW_HASHCROSSUNDER	67
#define	SHADOW_HASHMINSIZE		13

static ulong_t
shadow_hash_double(ulong_t size)
{
	ulong_t nsize;

	if (size < SHADOW_HASHCROSSOVER) {
		nsize = (size * 2) + 5;
		return (nsize < SHADOW_HASHCROSSOVER ? nsize :
		    SHADOW_HASHCROSSOVER);
	}

	return ((size * 2) + 33);
}

static ulong_t
shadow_hash_half(ulong_t size)
{
	ulong_t nsize;

	if (size > SHADOW_HASHCROSSUNDER) {
		nsize = (size - 33) / 2;
		return (nsize > SHADOW_HASHCROSSUNDER ? nsize :
		    SHADOW_HASHCROSSUNDER);
	}

	nsize = (size - 5) / 2;

	return (nsize > SHADOW_HASHMINSIZE ? nsize : SHADOW_HASHMINSIZE);
}

shadow_hash_t *
shadow_hash_create(size_t linkoffs,
    const void *(*convert)(const void *elem),
    ulong_t (*compute)(const void *key),
    int (*compare)(const void *lkey, const void *rkey))
{
	shadow_hash_t *shp;

	if ((shp = shadow_zalloc(sizeof (shadow_hash_t))) == NULL)
		return (NULL);

	shp->sh_nbuckets = SHADOW_HASHMINSIZE;
	shp->sh_linkoffs = linkoffs;
	shp->sh_convert = convert;
	shp->sh_compute = compute;
	shp->sh_compare = compare;

	if ((shp->sh_buckets = shadow_zalloc(
	    shp->sh_nbuckets * sizeof (void *))) == NULL) {
		free(shp);
		return (NULL);
	}

	return (shp);
}

void
shadow_hash_destroy(shadow_hash_t *shp)
{
	if (shp != NULL) {
		free(shp->sh_buckets);
		free(shp);
	}
}


ulong_t
shadow_hash_strhash(const void *key)
{
	ulong_t g, h = 0;
	const char *p;

	for (p = key; *p != '\0'; p++) {
		h = (h << 4) + *p;

		if ((g = (h & 0xf0000000)) != 0) {
			h ^= (g >> 24);
			h ^= g;
		}
	}

	return (h);
}

int
shadow_hash_strcmp(const void *lhs, const void *rhs)
{
	return (strcmp(lhs, rhs));
}

static ulong_t
shadow_hash_compute(shadow_hash_t *shp, const void *elem)
{
	return (shp->sh_compute(shp->sh_convert(elem)) % shp->sh_nbuckets);
}

static void
shadow_hash_resize(shadow_hash_t *shp, ulong_t nsize)
{
	size_t osize = shp->sh_nbuckets;
	shadow_hash_link_t *link, **nbuckets;
	ulong_t idx, nidx;

	assert(nsize >= SHADOW_HASHMINSIZE);

	if (nsize == osize)
		return;

	if ((nbuckets = shadow_zalloc(nsize * sizeof (void *))) == NULL) {
		/*
		 * This routine can't fail, so we just eat the failure here.
		 * The consequences of this failing are only for performance;
		 * correctness is not affected by our inability to resize
		 * the hash table.
		 */
		return;
	}

	shp->sh_nbuckets = nsize;

	for (idx = 0; idx < osize; idx++) {
		while ((link = shp->sh_buckets[idx]) != NULL) {
			void *elem;

			/*
			 * For every hash element, we need to remove it from
			 * this bucket, and rehash it given the new bucket
			 * size.
			 */
			shp->sh_buckets[idx] = link->shl_next;
			elem = (void *)((uintptr_t)link - shp->sh_linkoffs);
			nidx = shadow_hash_compute(shp, elem);

			link->shl_next = nbuckets[nidx];
			nbuckets[nidx] = link;
		}
	}

	free(shp->sh_buckets);
	shp->sh_buckets = nbuckets;
}

void *
shadow_hash_lookup(shadow_hash_t *shp, const void *search)
{
	ulong_t idx = shp->sh_compute(search) % shp->sh_nbuckets;
	shadow_hash_link_t *hl;

	for (hl = shp->sh_buckets[idx]; hl != NULL; hl = hl->shl_next) {
		void *elem = (void *)((uintptr_t)hl - shp->sh_linkoffs);

		if (shp->sh_compare(shp->sh_convert(elem), search) == 0)
			return (elem);
	}

	return (NULL);
}

void *
shadow_hash_first(shadow_hash_t *shp)
{
	void *link = shadow_list_next(&(shp)->sh_list);

	if (link == NULL)
		return (NULL);

	return ((void *)((uintptr_t)link - shp->sh_linkoffs));
}

void *
shadow_hash_next(shadow_hash_t *shp, void *elem)
{
	void *link = shadow_list_next((uintptr_t)elem + shp->sh_linkoffs);

	if (link == NULL)
		return (NULL);

	return ((void *)((uintptr_t)link - shp->sh_linkoffs));
}

void
shadow_hash_insert(shadow_hash_t *shp, void *elem)
{
	shadow_hash_link_t *link = (void *)((uintptr_t)elem + shp->sh_linkoffs);
	ulong_t idx = shadow_hash_compute(shp, elem);

	assert(shadow_hash_lookup(shp, shp->sh_convert(elem)) == NULL);

	link->shl_next = shp->sh_buckets[idx];
	shp->sh_buckets[idx] = link;

	shadow_list_append(&shp->sh_list, link);

	if (++shp->sh_nelements > shp->sh_nbuckets / 2)
		shadow_hash_resize(shp, shadow_hash_double(shp->sh_nbuckets));
}

void
shadow_hash_remove(shadow_hash_t *shp, void *elem)
{
	ulong_t idx = shadow_hash_compute(shp, elem);
	shadow_hash_link_t *link = (void *)((uintptr_t)elem + shp->sh_linkoffs);
	shadow_hash_link_t **hlp = &shp->sh_buckets[idx];

	for (; *hlp != NULL; hlp = &(*hlp)->shl_next) {
		if (*hlp == link)
			break;
	}

	assert(*hlp != NULL);
	*hlp = (*hlp)->shl_next;

	shadow_list_delete(&shp->sh_list, link);

	assert(shp->sh_nelements > 0);

	if (--shp->sh_nelements < shp->sh_nbuckets / 4)
		shadow_hash_resize(shp, shadow_hash_half(shp->sh_nbuckets));
}

size_t
shadow_hash_count(shadow_hash_t *shp)
{
	return (shp->sh_nelements);
}
