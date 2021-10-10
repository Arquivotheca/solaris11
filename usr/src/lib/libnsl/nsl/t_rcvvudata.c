/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * t_rcvudata.c and t_rcvvudata.c are very similar and contain common code.
 * Any changes to either of them should be reviewed to see whether they
 * are applicable to the other file.
 */
#include "mt.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stropts.h>
#include <sys/stream.h>
#define	_SUN_TPI_VERSION 2
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <xti.h>
#include <syslog.h>
#include <assert.h>
#include "tx.h"


int
_tx_rcvvudata(
	int fd,
	struct t_unitdata *unitdata,
	struct t_iovec *tiov,
	unsigned int tiovcount,
	int *flags,
	int api_semantics
)
{
	struct strbuf ctlbuf;
	struct strbuf databuf;
	char *dataptr;
	int retval;
	union T_primitives *pptr;
	struct _ti_user *tiptr;
	int sv_errno;
	int didalloc;
	int flg = 0;
	unsigned int nbytes;

	assert(api_semantics == TX_XTI_XNS5_API);

	if (tiovcount == 0 || tiovcount > T_IOV_MAX) {
		t_errno = TBADDATA;
		return (-1);
	}

	if ((tiptr = _t_checkfd(fd, 0, api_semantics)) == NULL)
		return (-1);
	sig_mutex_lock(&tiptr->ti_lock);

	if (tiptr->ti_servtype != T_CLTS) {
		t_errno = TNOTSUPPORT;
		sig_mutex_unlock(&tiptr->ti_lock);
		return (-1);
	}

	if (tiptr->ti_state != T_IDLE) {
		t_errno = TOUTSTATE;
		sig_mutex_unlock(&tiptr->ti_lock);
		return (-1);
	}

	/*
	 * check if there is something in look buffer
	 */
	if (tiptr->ti_lookcnt > 0) {
		sig_mutex_unlock(&tiptr->ti_lock);
		t_errno = TLOOK;
		return (-1);
	}

	/*
	 * Acquire ctlbuf for use in sending/receiving control part
	 * of the message.
	 */
	if (_t_acquire_ctlbuf(tiptr, &ctlbuf, &didalloc) < 0) {
		sv_errno = errno;
		sig_mutex_unlock(&tiptr->ti_lock);
		errno = sv_errno;
		return (-1);
	}

	*flags = 0;

	nbytes = _t_bytecount_upto_intmax(tiov, tiovcount);
	dataptr = NULL;
	if (nbytes != 0 && ((dataptr = malloc(nbytes)) == NULL)) {
		t_errno = TSYSERR;
		goto err_out;
	}

	databuf.maxlen = nbytes;
	databuf.len = 0;
	databuf.buf = dataptr;

	/*
	 * This is a call that may block indefinitely so we drop the
	 * lock and allow signals in MT case here and reacquire it.
	 * Error case should roll back state changes done above
	 * (happens to be no state change here)
	 */
	sig_mutex_unlock(&tiptr->ti_lock);
	if ((retval = getmsg(fd, &ctlbuf, &databuf, &flg)) < 0) {
		if (errno == EAGAIN)
			t_errno = TNODATA;
		else
			t_errno = TSYSERR;
		sv_errno = errno;
		sig_mutex_lock(&tiptr->ti_lock);
		errno = sv_errno;
		goto err_out;
	}
	sig_mutex_lock(&tiptr->ti_lock);

	/*
	 * is there control piece with data?
	 */
	if (ctlbuf.len > 0) {
		if (ctlbuf.len < (int)sizeof (t_scalar_t)) {
			t_errno = TSYSERR;
			errno = EPROTO;
			goto err_out;
		}

		/* LINTED pointer cast */
		pptr = (union T_primitives *)ctlbuf.buf;

		switch (pptr->type) {

		case T_UNITDATA_IND:
			if ((ctlbuf.len <
			    (int)sizeof (struct T_unitdata_ind)) ||
			    (pptr->unitdata_ind.OPT_length &&
			    (ctlbuf.len < (int)(pptr->unitdata_ind.OPT_length
			    + pptr->unitdata_ind.OPT_offset)))) {
				t_errno = TSYSERR;
				errno = EPROTO;
				goto err_out;
			}

			if (unitdata->addr.maxlen > 0) {
				if (TLEN_GT_NLEN(pptr->unitdata_ind.SRC_length,
				    unitdata->addr.maxlen)) {
					t_errno = TBUFOVFLW;
					goto err_out;
				}
				(void) memcpy(unitdata->addr.buf,
				    ctlbuf.buf + pptr->unitdata_ind.SRC_offset,
				    (size_t)pptr->unitdata_ind.SRC_length);
				unitdata->addr.len =
				    pptr->unitdata_ind.SRC_length;
			}
			if (unitdata->opt.maxlen > 0) {
				if (TLEN_GT_NLEN(pptr->unitdata_ind.OPT_length,
				    unitdata->opt.maxlen)) {
					t_errno = TBUFOVFLW;
					goto err_out;
				}
				(void) memcpy(unitdata->opt.buf, ctlbuf.buf +
				    pptr->unitdata_ind.OPT_offset,
				    (size_t)pptr->unitdata_ind.OPT_length);
				unitdata->opt.len =
					pptr->unitdata_ind.OPT_length;
			}
			if (retval & MOREDATA)
				*flags |= T_MORE;
			/*
			 * No state changes happens on T_RCVUDATA
			 * event (NOOP). We do it only to log errors.
			 */
			_T_TX_NEXTSTATE(T_RCVUDATA, tiptr,
			    "t_rcvvudata: invalid state event T_RCVUDATA");

			if (didalloc)
				free(ctlbuf.buf);
			else
				tiptr->ti_ctlbuf = ctlbuf.buf;
			_t_scatter(&databuf, tiov, tiovcount);
			if (dataptr != NULL)
				free(dataptr);
			sig_mutex_unlock(&tiptr->ti_lock);
			return (databuf.len);

		case T_UDERROR_IND:
			if (_t_register_lookevent(tiptr, 0, 0, ctlbuf.buf,
				ctlbuf.len) < 0) {
				t_errno = TSYSERR;
				errno = ENOMEM;
				goto err_out;
			}
			t_errno = TLOOK;
			goto err_out;

		default:
			break;
		}

		t_errno = TSYSERR;
		errno = EPROTO;
		goto err_out;

	} else {		/* else part of "if (ctlbuf.len > 0)" */
		unitdata->addr.len = 0;
		unitdata->opt.len = 0;
		/*
		 * only data in message no control piece
		 */
		if (retval & MOREDATA)
			*flags = T_MORE;
		/*
		 * No state transition occurs on
		 * event T_RCVUDATA. We do it only to
		 * log errors.
		 */
		_T_TX_NEXTSTATE(T_RCVUDATA, tiptr,
		    "t_rcvvudata: invalid state event T_RCVUDATA");
		if (didalloc)
			free(ctlbuf.buf);
		else
			tiptr->ti_ctlbuf = ctlbuf.buf;
		_t_scatter(&databuf, tiov, tiovcount);
		if (dataptr != NULL)
			free(dataptr);
		sig_mutex_unlock(&tiptr->ti_lock);
		return (databuf.len);
	}
	/* NOTREACHED */
err_out:
	sv_errno = errno;
	if (didalloc)
		free(ctlbuf.buf);
	else
		tiptr->ti_ctlbuf = ctlbuf.buf;
	if (dataptr != NULL)
		free(dataptr);
	sig_mutex_unlock(&tiptr->ti_lock);
	errno = sv_errno;
	return (-1);
}
