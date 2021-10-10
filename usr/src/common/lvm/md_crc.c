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
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/param.h>

static uint_t		*mddb_crctab = NULL;

#ifndef _KERNEL
#include <meta.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define	MD_ZALLOC(x)	Zalloc(x)
#define	MD_FREE(x, y)	Free(x)
#else	/* _KERNEL */
#define	MD_ZALLOC(x)	kmem_zalloc(x, KM_SLEEP)
#define	MD_FREE(x, y)	kmem_free(x, y)
#include <sys/thread.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#endif	/* ! _KERNEL */
#include <sys/lvm/md_crc.h>

#define	MDDB_CRCMAGIC 987654
#define	offsetof(s, m)  ((size_t)(&(((s *)0)->m)))


#include <sys/lvm/mdvar.h>
#include <sys/lvm/md_mddb.h>
#include <sys/lvm/md_names.h>
#include <sys/lvm/md_crc.h>
#include <sys/lvm/md_convert.h>
#include <sys/lvm/md_stripe.h>


static uint_t *
crcgentab(void)
{
	int	b, i;
	uint_t		v;
	uint_t		*crctab;
	uint_t		poly = 0x04c11db7;

	crctab = (uint_t *)MD_ZALLOC(256 * sizeof (int));
	for (b = 0; b < 256; b++) {
		for (v = b << (24), i = 0; i < 8; i++) {
			if (v & ((unsigned int)1 << 31)) {
				v = (v << 1) ^ poly;
			} else {
				v = v << 1;
			}
		}
		crctab[b] = v;
	}
	return (crctab);
}

/*
 * crc function that allows  a number of areas to be skipped (ignored)
 * during the crc computation.  The result area of the record is also ignored
 * during the crc computation.  Ignored areas are used for data that may
 * be changed after record has been crcgen'd, but before the data has been
 * written to disk or for when a multi-owner diskset may have multiple
 * nodes writing the same record data with the exception of the timestamp field.
 * The list of skip areas must be in ascending order of offset and if any
 * areas overlap, the list will be modified.
 */
uint_t
crcfunc(
	uint_t	check,
	uchar_t *record,	/* record to be check-summed */
	uint_t	*result,	/* put check-sum here(really u_long) */
	size_t	size,		/* size of record in bytes */
	crc_skip_t *skip	/* list of areas to skip */
)
{
	uint_t		newcrc;
	uint_t		*crctab;
	uchar_t		*recaddr;
	crc_skip_t	*s, *p;

	/*
	 * Check skip areas to see if they overlap (this should never happen,
	 * but is handled just in case something changes in the future).
	 * Also the skip list must be in ascending order of offset, assert
	 * error if this is not the case.
	 * If any 2 adjacent skip areas overlap, then the skip areas will
	 * be merged into 1 skip area and the other skip area is freed.
	 * If any 2 adjacent skip areas abut (border) each other, then skip
	 * areas are not merged, but are left as 2 independent skip areas.
	 * If the skip areas are identical, no change is made to either skip
	 * area since this is handled later.
	 */
	if (skip) {
		p = NULL;
		for (s = skip; s != NULL; s = s->skip_next) {
			if (p == NULL) {
				p = s;
				continue;
			}
#ifdef _KERNEL
			ASSERT(s->skip_offset > p->skip_offset);
#else
			assert(s->skip_offset > p->skip_offset);
#endif
			if ((p->skip_offset + p->skip_size) > s->skip_offset) {
				/*
				 * Current area overlaps previous, modify
				 * previous area and release current
				 */
				p->skip_size += s->skip_size - (p->skip_offset
				    + p->skip_size - s->skip_offset);
				p->skip_next = s->skip_next;
				MD_FREE(s, sizeof (crc_skip_t));
				s = p;
			}
			p = s;
		}
	}

	if (! mddb_crctab)
		mddb_crctab = crcgentab();

	crctab = mddb_crctab;
	newcrc = MDDB_CRCMAGIC;

	recaddr = record;
	s = skip;
	while (size--) {
		/* Skip the result pointer */
		if (record == (uchar_t *)result) {
			record += sizeof (uint_t);
			size -= (sizeof (uint_t) - 1);
			continue;
		}

		/*
		 * Skip over next skip area if non-null
		 */
		if ((s) && (record == (recaddr + (s->skip_offset)))) {
			record += s->skip_size;
			size -= (s->skip_size - 1);
			s = s->skip_next;
			continue;
		}

		newcrc = (newcrc << 8) ^ crctab[(newcrc >> 24) ^ *record++];
	}

	/* If we are checking, we either get a 0 - OK, or 1 - Not OK result */
	if (check) {
		if (*((uint_t *)result) == newcrc)
			return (0);
		return (1);
	}

	/*
	 * If we are generating, we stuff the result, if we have a result
	 * pointer, and return the value.
	 */
	if (result != NULL)
		*((uint_t *)result) = newcrc;
	return (newcrc);
}

void
crcfreetab(void)
{
	if (mddb_crctab) {
		MD_FREE((caddr_t)mddb_crctab, 256 * sizeof (int));
		mddb_crctab = NULL;
	}
}

/*
 * stripe_skip_ts
 *
 * Returns a list of fields to be skipped in the stripe record structure.
 * These fields are ms_timestamp in the component structure.
 * Used to skip these fields when calculating the checksum.
 */
crc_skip_t *
stripe_skip_ts(void *un, uint_t revision)
{
	struct ms_row32_od	*small_mdr;
	struct ms_row		*big_mdr;
	uint_t			row, comp, ncomps, compoff;
	crc_skip_t		*skip;
	crc_skip_t		*skip_prev;
	crc_skip_t		skip_start = {0, 0, 0};
	ms_unit_t		*big_un;
	ms_unit32_od_t		*small_un;
	uint_t			rb_off = offsetof(mddb_rb32_t, rb_data[0]);

	switch (revision) {
	case MDDB_REV_RB:
	case MDDB_REV_RBFN:
		small_un = (ms_unit32_od_t *)un;
		skip_prev = &skip_start;

		if (small_un->un_nrows == 0)
			return (NULL);
		/*
		 * walk through all rows to find the total number
		 * of components
		 */
		small_mdr = &small_un->un_row[0];
		ncomps = 0;
		for (row = 0; (row < small_un->un_nrows); row++) {
			ncomps += small_mdr[row].un_ncomp;
		}

		/* Now walk through the components */
		compoff = small_un->un_ocomp + rb_off;
		for (comp = 0; (comp < ncomps); ++comp) {
			uint_t  mdcp = compoff +
			    (comp * sizeof (ms_comp32_od_t));
#ifdef _KERNEL
			skip = (crc_skip_t *)kmem_zalloc(sizeof (crc_skip_t),
			    KM_SLEEP);
#else
			skip = (crc_skip_t *)Zalloc(sizeof (crc_skip_t));
#endif
			skip->skip_offset = mdcp +
			    offsetof(ms_comp32_od_t, un_mirror.ms_timestamp);
			skip->skip_size = sizeof (md_timeval32_t);
			skip_prev->skip_next = skip;
			skip_prev = skip;
		}
		break;
	case MDDB_REV_RB64:
	case MDDB_REV_RB64FN:
		big_un = (ms_unit_t *)un;
		skip_prev = &skip_start;

		if (big_un->un_nrows == 0)
			return (NULL);
		/*
		 * walk through all rows to find the total number
		 * of components
		 */
		big_mdr = &big_un->un_row[0];
		ncomps = 0;
		for (row = 0; (row < big_un->un_nrows); row++) {
			ncomps += big_mdr[row].un_ncomp;
		}

		/* Now walk through the components */
		compoff = big_un->un_ocomp + rb_off;
		for (comp = 0; (comp < ncomps); ++comp) {
			uint_t  mdcp = compoff +
			    (comp * sizeof (ms_comp_t));
#ifdef _KERNEL
			skip = (crc_skip_t *)kmem_zalloc(sizeof (crc_skip_t),
			    KM_SLEEP);
#else
			skip = (crc_skip_t *)Zalloc(sizeof (crc_skip_t));
#endif
			skip->skip_offset = mdcp +
			    offsetof(ms_comp_t, un_mirror.ms_timestamp);
			skip->skip_size = sizeof (md_timeval32_t);
			skip_prev->skip_next = skip;
			skip_prev = skip;
		}
		break;
	}
	/* Return the start of the list of fields to skip */
	return (skip_start.skip_next);
}

/*
 * mirror_skip_ts
 *
 * Returns a list of fields to be skipped in the mirror record structure.
 * This includes un_last_read and sm_timestamp for each submirror
 * Used to skip these fields when calculating the checksum.
 */
crc_skip_t *
mirror_skip_ts(uint_t revision)
{
	int		i;
	crc_skip_t	*skip;
	crc_skip_t	*skip_prev;
	crc_skip_t	skip_start = {0, 0, 0};
	uint_t		rb_off = offsetof(mddb_rb32_t, rb_data[0]);

	skip_prev = &skip_start;

#ifdef _KERNEL
	skip = (crc_skip_t *)kmem_zalloc(sizeof (crc_skip_t), KM_SLEEP);
#else
	skip = (crc_skip_t *)Zalloc(sizeof (crc_skip_t));
#endif
	switch (revision) {
	case MDDB_REV_RB:
	case MDDB_REV_RBFN:
		skip->skip_offset = offsetof(mm_unit32_od_t,
		    un_last_read) + rb_off;
		break;
	case MDDB_REV_RB64:
	case MDDB_REV_RB64FN:
		skip->skip_offset = offsetof(mm_unit_t,
		    un_last_read) + rb_off;
		break;
	}
	skip->skip_size = sizeof (int);
	skip_prev->skip_next = skip;
	skip_prev = skip;

	for (i = 0; i < NMIRROR; i++) {
#ifdef _KERNEL
		skip = (crc_skip_t *)kmem_zalloc(sizeof (crc_skip_t), KM_SLEEP);
#else
		skip = (crc_skip_t *)Zalloc(sizeof (crc_skip_t));
#endif
		switch (revision) {
		case MDDB_REV_RB:
		case MDDB_REV_RBFN:
			skip->skip_offset = offsetof(mm_unit32_od_t,
			    un_sm[i].sm_timestamp) + rb_off;
			break;
		case MDDB_REV_RB64:
		case MDDB_REV_RB64FN:
			skip->skip_offset = offsetof(mm_unit_t,
			    un_sm[i].sm_timestamp) + rb_off;
			break;
		}
		skip->skip_size = sizeof (md_timeval32_t);
		skip_prev->skip_next = skip;
		skip_prev = skip;
	}
	/* Return the start of the list of fields to skip */
	return (skip_start.skip_next);
}

/*
 * hotspare_skip_ts
 *
 * Returns a list of the timestamp fields in the hotspare record structure.
 * Used to skip these fields when calculating the checksum.
 */
crc_skip_t *
hotspare_skip_ts(uint_t revision)
{
	crc_skip_t	*skip;
	uint_t		rb_off = offsetof(mddb_rb32_t, rb_data[0]);

#ifdef _KERNEL
	skip = (crc_skip_t *)kmem_zalloc(sizeof (crc_skip_t), KM_SLEEP);
#else
	skip = (crc_skip_t *)Zalloc(sizeof (crc_skip_t));
#endif
	switch (revision) {
	case MDDB_REV_RB:
	case MDDB_REV_RBFN:
		skip->skip_offset = offsetof(hot_spare32_od_t, hs_timestamp) +
		    rb_off;
		break;
	case MDDB_REV_RB64:
	case MDDB_REV_RB64FN:
		skip->skip_offset = offsetof(hot_spare_t, hs_timestamp) +
		    rb_off;
		break;
	}
	skip->skip_size = sizeof (md_timeval32_t);
	return (skip);
}
