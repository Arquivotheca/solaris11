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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _KERNEL
#include <stdlib.h>
#include <note.h>
#else
#include <sys/note.h>
#endif

#include <sys/strsun.h>
#include <sys/types.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

#ifdef DEBUG_DUMP
#ifdef _KERNEL
#include <sys/cmn_err.h>	/* for print, sprintf */
#else
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#endif /* _KERNEL */
#endif /* DEBUG_DUMP */


/*
 * Initialize by setting iov_or_mp to point to the current iovec or mp,
 * and by setting current_offset to an offset within the current iovec or mp.
 *
 * Input:
 * out			Information about data; described by an iovec or mblk
 *
 * Output:
 * iov_or_mp		Pointer to iovec or mblk (for CRYPTO_DATA_{UIO,MBLK}).
 *			Not set for CRYPTO_DATA_RAW.
 * current_offset	Pointer to starting offset of the data within
 *			a iovec or mblk.
 */
void
crypto_init_ptrs(crypto_data_t *out, void **iov_or_mp, offset_t *current_offset)
{
#ifdef _KERNEL
	offset_t offset;
#else
NOTE(ARGUNUSED(iov_or_mp,current_offset))
#endif /* !_KERNEL */

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW:
		*current_offset = out->cd_offset;
		break;

#ifdef _KERNEL
	case CRYPTO_DATA_UIO: {
		uio_t *uiop = out->cd_uio;
		uintptr_t vec_idx;

		offset = out->cd_offset;
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    offset >= uiop->uio_iov[vec_idx].iov_len;
		    offset -= uiop->uio_iov[vec_idx++].iov_len)
			;

		*current_offset = offset;
		*iov_or_mp = (void *)vec_idx;
		break;
	}

	case CRYPTO_DATA_MBLK: {
		mblk_t *mp;

		offset = out->cd_offset;
		for (mp = out->cd_mp; mp != NULL && offset >= MBLKL(mp);
		    offset -= MBLKL(mp), mp = mp->b_cont)
			;

		*current_offset = offset;
		*iov_or_mp = mp;
		break;
	}
#endif /* _KERNEL */
	} /* end switch */
}


/*
 * Get pointers for where in the output to copy a block of encrypted or
 * decrypted data.  The iov_or_mp argument stores a pointer to the current
 * iovec or mp, and offset stores an offset into the current iovec or mp.
 *
 * Input:
 * out			Information about data; described by an iovec or mblk
 * current_offset	Pointer to starting offset of the data within
 *			a iovec or mblk.
 * amt			Amount of data to copy, in bytes.
 *
 * Output:
 * iov_or_mp		Pointer to iovec or mblk after "amt"
 *			(for CRYPTO_DATA_{UIO,MBLK}).
 *			Not set for CRYPTO_DATA_RAW.
 * current_offset	Incremented by "amt" if contained within an
 *			iovec or mblk.  If amt spans 2 iovecs/mblks, return
 *			offset to start of data contained in 1st block.
 * data_1		Pointer to 1st block containing data
 * data_1_len		Length of data contained in 1st block
 * out_data_2		Points to 2nd block if amt spans 2 iovecs/mblks.
 *			NULL if all the data fits in 1st block
 */
void
crypto_get_ptrs(crypto_data_t *out, void **iov_or_mp, offset_t *current_offset,
    uint8_t **out_data_1, size_t *out_data_1_len, uint8_t **out_data_2,
    size_t amt)
{
#ifndef _KERNEL
NOTE(ARGUNUSED(iov_or_mp,current_offset))
#endif /* !_KERNEL */

	offset_t offset;

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW: {
		iovec_t *iov = &out->cd_raw;

		offset = *current_offset;
		if ((offset + amt) <= iov->iov_len) {
			/* one block fits */
			*out_data_1 = (uint8_t *)iov->iov_base + offset;
			*out_data_1_len = amt;
			*out_data_2 = NULL;
			*current_offset = offset + amt;
		}
		break;
	}

#ifdef _KERNEL
	case CRYPTO_DATA_UIO: {
		uio_t *uio = out->cd_uio;
		iovec_t *iov;
		offset_t offset;
		uintptr_t vec_idx;
		uint8_t *p;

		offset = *current_offset;
		vec_idx = (uintptr_t)(*iov_or_mp);
		iov = &uio->uio_iov[vec_idx];
		p = (uint8_t *)iov->iov_base + offset;
		*out_data_1 = p;

		if (offset + amt <= iov->iov_len) {
			/* can fit one block into this iov */
			*out_data_1_len = amt;
			*out_data_2 = NULL;
			*current_offset = offset + amt;
		} else {
			/* one block spans two iovecs */
			*out_data_1_len = iov->iov_len - offset;
			if (vec_idx == uio->uio_iovcnt)
				return;
			vec_idx++;
			iov = &uio->uio_iov[vec_idx];
			*out_data_2 = (uint8_t *)iov->iov_base;
			*current_offset = amt - *out_data_1_len;
		}
		*iov_or_mp = (void *)vec_idx;
		break;
	}

	case CRYPTO_DATA_MBLK: {
		mblk_t *mp = (mblk_t *)*iov_or_mp;
		uint8_t *p;

		offset = *current_offset;
		p = mp->b_rptr + offset;
		*out_data_1 = p;

		if ((p + amt) <= mp->b_wptr) {
			/* can fit one block into this mblk */
			*out_data_1_len = amt;
			*out_data_2 = NULL;
			*current_offset = offset + amt;
		} else {
			/* one block spans two mblks */
			*out_data_1_len = _PTRDIFF(mp->b_wptr, p);
			if ((mp = mp->b_cont) == NULL)
				return;
			*out_data_2 = mp->b_rptr;
			*current_offset = (amt - *out_data_1_len);
		}
		*iov_or_mp = mp;
		break;
	}
#endif /* _KERNEL */
	} /* end switch */
}

int
crypto_init_outbufs(crypto_data_t *out, uint_t *outbufnum, iovec_t **out_bufs,
    iovec_t *outvecs, int outvecs_size, offset_t *out_offset,
    boolean_t *out_bufs_allocated)
{
	offset_t offset = out->cd_offset;

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW: {
		iovec_t *iov = &out->cd_raw;

		*out_offset = offset;
		*outbufnum = 1;
		*out_bufs = iov;

		break;
	}

#ifdef _KERNEL
	case CRYPTO_DATA_UIO: {
		uio_t *uiop = out->cd_uio;
		uintptr_t vec_idx;

		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    offset >= uiop->uio_iov[vec_idx].iov_len;
		    offset -= uiop->uio_iov[vec_idx++].iov_len)
			;

		if (vec_idx >= uiop->uio_iovcnt) {
			return (CRYPTO_DATA_LEN_RANGE);
		}

		*out_offset = offset;
		*outbufnum = uiop->uio_iovcnt - vec_idx;
		*out_bufs = &uiop->uio_iov[vec_idx];
		break;
	}

	case CRYPTO_DATA_MBLK: {
		mblk_t  *mp;
		mblk_t  *p;
		int	i;

		for (mp = out->cd_mp; mp != NULL && offset >= MBLKL(mp);
		    offset -= MBLKL(mp), mp = mp->b_cont)
			;

		if (mp == NULL) {
			return (CRYPTO_DATA_LEN_RANGE);
		}

		for (i = 0, p = mp;  p != NULL;  p = p->b_cont) {
			i++;
		}

		if (i <= outvecs_size) {
			*out_bufs = outvecs;
		} else {
			*out_bufs = (iovec_t *)CRYPTO_ALLOC(
			    i * sizeof (iovec_t), KM_NOSLEEP);
			if (*out_bufs == NULL) {
				return (CRYPTO_HOST_MEMORY);
			}
			*out_bufs_allocated = B_TRUE;
		}
		*outbufnum = i;
		*out_offset = offset;

		for (i = 0; i < *outbufnum; i++) {
			(*out_bufs)[i].iov_base = (char *)(mp->b_rptr);
			(*out_bufs)[i].iov_len = MBLKL(mp);
			mp = mp->b_cont;
		}
		break;
	}
#endif /* _KERNEL */
	} /* end switch */

	return (CRYPTO_SUCCESS);
}


/*
 * Free mode context data.  That is, the mode-specific structure
 * (e.g., cbc_ctx).  This includes its component mode-common data
 * (struct common_ctx).
 *
 * For CCM, GCM, and GMAC modes, free the *_pt_buf buffer, if allocated.
 */
void
crypto_free_mode_ctx(void *ctx)
{
	common_ctx_t *common_ctx = (common_ctx_t *)ctx;

	if (ctx == NULL)
		return;

	switch (common_ctx->cc_flags & MODE_MASK) {
	case ECB_MODE:
		CRYPTO_FREE(ctx, sizeof (ecb_ctx_t));
		break;

	case CBC_MODE:
		CRYPTO_FREE(ctx, sizeof (cbc_ctx_t));
		break;

	case CTR_MODE:
		CRYPTO_FREE(ctx, sizeof (ctr_ctx_t));
		break;

	case CCM_MODE:
		if (((ccm_ctx_t *)ctx)->ccm_pt_buf != NULL)
			CRYPTO_FREE(((ccm_ctx_t *)ctx)->ccm_pt_buf,
			    ((ccm_ctx_t *)ctx)->ccm_data_len +
			    ((ccm_ctx_t *)ctx)->ccm_mac_len);

		CRYPTO_FREE(ctx, sizeof (ccm_ctx_t));
		break;

	case GCM_MODE:
	case GMAC_MODE:
		if (((gcm_ctx_t *)ctx)->gcm_pt_buf != NULL)
			CRYPTO_FREE(((gcm_ctx_t *)ctx)->gcm_pt_buf,
			    ((gcm_ctx_t *)ctx)->gcm_pt_buf_len);

		CRYPTO_FREE(ctx, sizeof (gcm_ctx_t));
		break;

	case CFB128_MODE:
		CRYPTO_FREE(ctx, sizeof (cfb_ctx_t));
		break;
	}
}

#ifdef DEBUG_DUMP
/*
 * Debug dump utility
 */
char *
getdump(void * data, int len)
{
	char	*dump;
	uchar_t *buff;
	int	i, j, k;
	int	dumplen;

	dumplen = (int)(len * 4) + (len / 16) * 10 + 80;
	dump = CRYPTO_ALLOC(dumplen, KM_NOSLEEP);
	if (dump == NULL) {
		return (NULL);
	}
	dump[0] = '\000';
	if (data == NULL || len == 0) {
		return (dump);
	}

	buff = (uchar_t *)data;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			if (i != 0) {
				sprintf(dump, "%s\n", dump);
			}
			sprintf(dump, "%s      ", dump);
		}
		sprintf(dump, "%s%02x ", dump, buff[i]);

		if (i % 16 == 15) {
			/* Space between HEX and PRINTABLE */
			sprintf(dump, "%s   ", dump);
			/* PRINTABLE string */
			for (j = 0, k = i - 15; j < 16; j++, k++) {
				/* poor man's isprint() */
				if ((buff[j] > 32) && (buff[j] < 127)) {
					sprintf(dump, "%s%c", dump, buff[k]);
				} else {
					sprintf(dump, "%s.", dump);
				}
			}
		}
	}
	if (len % 16) {
		for (j = 0; j < 17 - (len % 16); j++) {
			sprintf(dump, "%s   ", dump);
		}

		for (j = 0, k = i - (len % 16); j < (len % 16); j++, k++) {
			/* poor man's isprint() */
			if ((buff[j] > 32) && (buff[j] < 127)) {
				sprintf(dump, "%s%c", dump, buff[k]);
			} else {
				sprintf(dump, "%s.", dump);
			}
		}
	}
	return (dump);
}


void
printdump(char *head, void *ptr, int len)
{
	char *dump;

	dump = getdump(ptr, len);
	if (dump) {
		printf("%s\n%s\n", head, dump);
		CRYPTO_FREE(dump, len);
	}
}

#endif
