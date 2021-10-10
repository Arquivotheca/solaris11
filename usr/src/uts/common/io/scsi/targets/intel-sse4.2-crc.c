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
 *
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <hd_crc.h>
#include <sys/scsi/targets/stdef.h>

static const char *
crc32c_name(void)
{
#if defined(__i386) || defined(__amd_64) || defined(__x86_64)
	if (is_x86_feature(x86_featureset, X86FSET_SSE4_2))
		return ("crc32c-hardware");
#endif
#if defined(lint)
	/* Function call only to suppress warning */
	(void) hd_crc32_avail(NULL);
#endif

	return ("");

}

/* ARGSUSED */
static uint32_t
crc32c_len(dp_setable const *set, uint32_t data_len)
{
	return (set->dp_len);
}

static void
crc32c_crcadd(dp_setable const *set, caddr_t datap, uint32_t blk_len)
{
	uint32_t crc = set->dp_seed;
	uint32_t *crcpnt = (uint32_t *)(void *)&datap[blk_len];

	crc = HW_CRC32((uint8_t *)datap, blk_len, crc);
	*crcpnt = BE_32(crc);
}

/*
 * Since this is called from the read function we'll assume that
 * blk_len is what the read returned and includes the CRC.
 */
static boolean_t
crc32c_crcchk(dp_setable const *set, caddr_t datap, uint32_t blk_len)
{
	uint32_t crc = set->dp_seed;
	uint32_t *crcpnt = (uint32_t *)(void *)&datap[blk_len - set->dp_len];

	crc = HW_CRC32((uint8_t *)datap, blk_len - set->dp_len, crc);
	if (BE_32(*crcpnt) == crc)
		return (B_TRUE);
	return (B_FALSE);
}

const dp_funcs crc32c_xhardware = {
	crc32c_name,
	crc32c_len,
	crc32c_crcadd,
	crc32c_crcchk,
	no_crc
};
