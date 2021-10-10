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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * xdr_vn.c, XDR implementation where the XDR-ed data gets written out to file
 * via the vnode interface. The implementation takes care of details related
 * to allocating the buffer where the XDR-ed data  gets stored, pushing it to
 * the file, and destroying/reusing the buffer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/sdt.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <sys/cmn_err.h>

/* 128K buffer size for storing the XDR-ed data. */
#define	BLKSIZE 	(131072)
#define	LAST_RECORD	(1)
#define	NOT_LAST_RECORD	(0)

static bool_t xdrvn_getint32(XDR *, int32_t *);
static bool_t xdrvn_putint32(XDR *, int32_t *);
static bool_t xdrvn_getbytes(XDR *, caddr_t, int);
static bool_t xdrvn_putbytes(XDR *, caddr_t, int);
static uint_t xdrvn_getpos(XDR *);
static bool_t xdrvn_setpos(XDR *, uint_t);
static rpc_inline_t *xdrvn_inline(XDR *, int);
static bool_t   xdrvn_control(XDR *, int, void *);
static int xdrvn_reset(XDR *, int, int);
static int xdrvn_write(XDR *, int);
static int xdrvn_read(XDR *);
void xdrvn_destroy(XDR *);

typedef struct {
	caddr_t	xp_buffer;	/* pointer to the buffer */
	caddr_t	xp_off; 	/* offset within the buffer */
	vnode_t	*vp;		/* vnode pointer for the file */
	offset_t foffset;	/* current offset in the file */
} xdrvn_private_t;

struct xdr_ops xdrvn_ops = {
	xdrvn_getbytes,
	xdrvn_putbytes,
	xdrvn_getpos,
	xdrvn_setpos,
	xdrvn_inline,
	xdrvn_destroy,
	xdrvn_control,
	xdrvn_getint32,
	xdrvn_putint32
};

void
xdrvn_create(XDR *xdrs, vnode_t *vp, enum xdr_op op)
{
	xdrvn_private_t *xdrp = NULL;

	ASSERT(xdrs != NULL);
	xdrs->x_op = op;
	xdrs->x_ops = &xdrvn_ops;
	xdrs->x_public = NULL;
	if (op == XDR_ENCODE) {
		xdrs->x_handy = BLKSIZE;
	} else if (op == XDR_DECODE) {
		xdrs->x_handy = 0;
	}


	xdrp = (xdrvn_private_t *)kmem_zalloc(sizeof (xdrvn_private_t),
	    KM_NOSLEEP);
	xdrs->x_private = (caddr_t)xdrp;

	xdrp->vp = vp;
	xdrp->xp_buffer = (caddr_t)kmem_zalloc(BLKSIZE, KM_NOSLEEP);
	xdrp->xp_off = xdrp->xp_buffer;
	xdrp->foffset = 0;
}

void
xdrvn_destroy(XDR *xdrs)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;

	if (xdrp == NULL)
		return;

	/* Write any remaining buffer data to the file */
	if (xdrs->x_op == XDR_ENCODE) {
		if (((uintptr_t)xdrp->xp_off -
		    (uintptr_t)xdrp->xp_buffer) != 0) {
			if (xdrvn_reset(xdrs, 0, LAST_RECORD) != 0) {
				cmn_err(CE_WARN,
				    "Write of last buffer failed for file %s\n",
				    xdrp->vp->v_path);
			}
		}
	}
	if (xdrp && xdrp->xp_buffer) {
		kmem_free(xdrp->xp_buffer, BLKSIZE);
		xdrp->xp_buffer = NULL;
	}
	if (xdrp) {
		kmem_free(xdrp, sizeof (xdrvn_private_t));
		xdrp = NULL;
	}
}

static bool_t
xdrvn_getint32(XDR *xdrs, int32_t *int32p)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;

	if ((xdrs->x_handy -= (int)sizeof (int32_t)) < 0) {
		if (xdrvn_reset(xdrs, (int)sizeof (int32_t),
		    NOT_LAST_RECORD) != 0) {
			return (FALSE);
		}
	}
	ASSERT((xdrp->xp_off + sizeof (int32_t)) <=
	    (xdrp->xp_buffer + BLKSIZE));
	/* LINTED pointer alignment */
	*int32p = (int32_t)ntohl((uint32_t)(*((int32_t *)(xdrp->xp_off))));

	DTRACE_PROBE1(genunix__i__xdrvn_getint32, int32_t, *int32p);

	xdrp->xp_off += sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrvn_putint32(XDR *xdrs, int32_t *int32p)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;

	if ((xdrs->x_handy -= (int)sizeof (int32_t)) < 0) {
		if (xdrvn_reset(xdrs, (int)sizeof (int32_t),
		    NOT_LAST_RECORD) != 0) {
			return (FALSE);
		}
	}
	ASSERT((xdrp->xp_off + sizeof (int32_t))
	    <= (xdrp->xp_buffer + BLKSIZE));
	/* LINTED pointer alignment */
	*(int32_t *)xdrp->xp_off = (int32_t)htonl((uint32_t)(*int32p));
	xdrp->xp_off += sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrvn_getbytes(XDR *xdrs, caddr_t addr, int len)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;

	if (len > BLKSIZE)
		return (FALSE);

	if ((xdrs->x_handy -= len) < 0) {
		if (xdrvn_reset(xdrs, len, NOT_LAST_RECORD) != 0) {
			return (FALSE);
		}
	}
	ASSERT((xdrp->xp_off + len) <= (xdrp->xp_buffer + BLKSIZE));
	bcopy(xdrp->xp_off, addr, len);

	DTRACE_PROBE1(genunix__i__xdrvn_getbytes, caddr_t, addr);

	xdrp->xp_off += len;
	return (TRUE);
}

static bool_t
xdrvn_putbytes(XDR *xdrs, caddr_t addr, int len)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;

	if (len > BLKSIZE)
		return (FALSE);

	if ((xdrs->x_handy -= len) < 0) {
		if (xdrvn_reset(xdrs, len, NOT_LAST_RECORD) != 0) {
			return (FALSE);
		}
	}
	ASSERT((xdrp->xp_off + len) <= (xdrp->xp_buffer + BLKSIZE));
	bcopy(addr, xdrp->xp_off, len);
	xdrp->xp_off += len;
	return (TRUE);
}

static uint_t
xdrvn_getpos(XDR *xdrs)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	return ((uint_t)((uintptr_t)xdrp->xp_off - (uintptr_t)xdrp->xp_buffer));
}

static bool_t
xdrvn_setpos(XDR *xdrs, uint_t pos)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	caddr_t newaddr = xdrp->xp_buffer + pos;
	caddr_t lastaddr = xdrp->xp_off + xdrs->x_handy;
	ptrdiff_t diff;

	if (newaddr > lastaddr)
		return (FALSE);
	xdrp->xp_off = newaddr;
	diff = lastaddr - newaddr;
	xdrs->x_handy = (int)diff;
	return (TRUE);
}

/*
 * xdrvn_reset is an interface to:
 *
 * (a) re-initialize the buffer to read or write more XDR-ed data.
 * (b) read case: read the contents of the file into the buffer.
 * (c) write case: write the contents of the buffer out to the file.
 *
 * The len and last_rec serve special purpose:
 *
 * len: used to reset the remaining bytes in the  buffer, x_handy. The
 * xdrvn_get* or xdrvn_put* functions  have already subtracted the size of the
 * data unit being XDR encoded from x_handy before calling this function.
 * Resetting x_handy here: (a) provides a single place to reset the buffer and
 * all the associated data; and (b) prevents a potential bug, where the caller
 * forgets to subtract these bytes a second time after calling xdrvn_reset.
 *
 * last_rec: used to indicate if the record to be written out to the file is
 * the last record. If it is, then only the relevant bytes in the buffer are
 * written out. In all other cases, except for the last buffer, the entire
 * BLKSIZE MUST be written out to enable the file containing the XDR-ed data
 * to be read correctly.
 */
static int
xdrvn_reset(XDR *xdrs, int len, int last_rec)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	int error = 0;

	if (xdrs->x_op == XDR_ENCODE) {
		error = xdrvn_write(xdrs, last_rec);
		bzero(xdrp->xp_buffer, BLKSIZE);
	}

	if (xdrs->x_op == XDR_DECODE) {
		error = xdrvn_read(xdrs);
	}

	/* If no error, then reinitialize the buffer */
	if (error == 0) {
		xdrp->xp_off = xdrp->xp_buffer;
		xdrs->x_handy = BLKSIZE - len;
	}
	return (error);

}

static int
xdrvn_write(XDR *xdrs, int last_rec)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	offset_t 	offset = xdrp->foffset;
	vnode_t 	*vp = xdrp->vp;
	caddr_t		buf = xdrp->xp_buffer;
	ssize_t		len, resid;
	int		error = 0;

	if (last_rec == LAST_RECORD)
		resid = len = xdrp->xp_off - xdrp->xp_buffer;
	else
		resid = len = BLKSIZE;

	/*
	 * XXXps: Remove FSYNC when doing end-to-end migration since it will
	 * make the harvest slow. zfs send of the final will ensure that all
	 * unsynced data is written out. We need the FSYNC here for
	 * debugging/testing purposes.
	 */
	while (len != 0) {
		if ((error = vn_rdwr(UIO_WRITE, vp, buf, len, offset,
		    UIO_SYSSPACE, 0, (rlim64_t)MAXOFFSET_T,
		    CRED(), &resid)) != 0)
			goto out;

		if (resid >= len)
			return (ENOSPC);

		buf = buf + len - resid;
		offset += len - resid;
		len = resid;
	}
out:
	xdrp->foffset = offset;
	return (error);
}

static int
xdrvn_read(XDR *xdrs)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	offset_t 	offset = xdrp->foffset;
	vnode_t 	*vp = xdrp->vp;
	caddr_t		buf = xdrp->xp_buffer;
	ssize_t 	len, resid;
	int		error = 0;
	vattr_t		va;

	va.va_mask = AT_SIZE;
	if ((error = VOP_GETATTR(vp, &va, 0, CRED(), NULL)) != 0)
		return (error);

	if ((va.va_size - offset) > BLKSIZE)
		resid = len = BLKSIZE;
	else
		resid = len = (va.va_size - offset);

	while (len != 0) {
		if ((error = vn_rdwr(UIO_READ, vp, (caddr_t)buf, len, offset,
		    UIO_SYSSPACE, FREAD, (rlim64_t)0,
		    CRED(), &resid)) != 0)
			goto out;

		buf = buf + len - resid;
		offset += len - resid;
		len = resid;
	}
out:
	xdrp->foffset = offset;
	return (error);
}

static rpc_inline_t *
xdrvn_inline(XDR * xdrs, int len)
{
	xdrvn_private_t *xdrp = (xdrvn_private_t *)xdrs->x_private;
	rpc_inline_t *buf = NULL;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		/* LINTED pointer alignment */
		buf = (rpc_inline_t *)xdrp->xp_off;
		xdrp->xp_off += len;
	}
	return (buf);
}

/*ARGSUSED*/
static bool_t
xdrvn_control(XDR *xdrs, int request, void *info)
{
	return (FALSE);
}
