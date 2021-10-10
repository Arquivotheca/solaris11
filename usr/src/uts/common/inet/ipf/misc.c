/*
 * Copyright (C) 2003 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#ifndef __hpux
#pragma ident "@(#)$Id: misc.c,v 1.12 2003/11/29 07:11:03 darrenr Exp $"
#else
struct uio;
#endif

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/systm.h>
#include <sys/types.h>
#include <sys/stream.h>
#include <sys/ddi.h>

#ifdef	__hpux
# define	BCOPY(a,b,c)	bcopy((caddr_t)a, (caddr_t)b, c)
#endif
#ifdef	sun
# define	BCOPY(a,b,c)	bcopy((char *)a, (char *)b, c)
#endif

void mb_copydata(min, off, len, buf)
mblk_t *min;
size_t off, len;
char *buf;
{
	u_char *s, *bp = (u_char *)buf;
	size_t mlen, olen, clen;
	mblk_t *m;

	for (m = min; (m != NULL) && (len > 0); m = m->b_cont) {
		if (m->b_datap->db_type != M_DATA)
			continue;
		s = m->b_rptr;
		mlen = m->b_wptr - s;
		olen = min(off, mlen);
		if ((olen == mlen) || (olen < off)) {
			off -= olen;
			continue;
		} else if (olen) {
			off -= olen;
			s += olen;
			mlen -= olen;
		}
		clen = min(mlen, len);
		BCOPY(s, bp, clen);
		len -= clen;
		bp += clen;
	}
}


void mb_copyback(min, off, len, buf)
mblk_t *min;
size_t off, len;
char *buf;
{
	u_char *s, *bp = (u_char *)buf;
	size_t mlen, olen, clen;
	mblk_t *m, *mp;

	for (m = min, mp = NULL; (m != NULL) && (len > 0); m = m->b_cont) {
		mp = m;
		if (m->b_datap->db_type != M_DATA)
			continue;

		s = m->b_rptr;
		mlen = m->b_wptr - s;
		olen = min(off, mlen);
		if ((olen == mlen) || (olen < off)) {
			off -= olen;
			continue;
		} else if (olen) {
			off -= olen;
			s += olen;
			mlen -= olen;
		}
		clen = min(mlen, len);
		BCOPY(bp, s, clen);
		len -= clen;
		bp += clen;
	}

	if ((m == NULL) && (mp != NULL)) {
		if (len > 0) {
			mlen = mp->b_datap->db_lim - mp->b_wptr;
			if (mlen > 0) {
				if (mlen > len)
					mlen = len;
				BCOPY(bp, mp->b_wptr, mlen);
				bp += mlen;
				len -= mlen;
				mp->b_wptr += mlen;
#ifdef  STRUIO_IP
# if SOLARIS2 < 10
				mp->b_datap->db_struiolim = mp->b_wptr;
# endif
				mp->b_datap->db_struioflag &= ~STRUIO_IP;
#endif
			}
		}

		if (len > 0) {
			m = allocb(len, BPRI_MED);
			if (m != NULL) {
				BCOPY(bp, m->b_wptr, len);
				m->b_band = mp->b_band;
				m->b_wptr += len;
				linkb(mp, m);
			}
		}
	}
}
