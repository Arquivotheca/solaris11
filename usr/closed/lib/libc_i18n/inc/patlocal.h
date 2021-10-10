/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */

/* @(#)$RCSfile: patlocal.h,v $ $Revision: 1.3.2.3 $ (OSF) $Date: 1992/02/20 */
/* 22:59:55 $ */

#ifndef	__H_PATLOCAL
#define	__H_PATLOCAL

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COMPONENT_NAME: (LIBCPAT) Internal Regular Expression
 *
 * FUNCTIONS:
 *
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 * Licensed Materials - Property of IBM
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/lib/c/pat/patlocal.h, , bos320, 9134320 8/13/91 14:48:33
 */

/*
 * WARNING:
 * The interfaces defined in this header file are for Sun private use only.
 * The contents of this file are subject to change without notice for the
 * future releases.
 */

/* ******************************************************************** */
/*									*/
/* Function prototypes							*/
/*									*/
/* ******************************************************************** */

extern	wchar_t	_mbucoll(_LC_collate_t *, char *, char **);

/* ******************************************************************** */
/*									*/
/* Macros to determine collation weights				*/
/*									*/
/* ******************************************************************** */

#define	MAX_PC		phdl->co_wc_max		/* max process code	*/
#define	MIN_PC		phdl->co_wc_min		/* min process code	*/
#define	MAX_UCOLL	phdl->co_col_max 	/* max unique weight	*/
#define	MIN_UCOLL	phdl->co_col_min 	/* min unique weight	*/

#define	CLASS_SIZE	32			/* [: :] max length	*/


/*
 * Macros to get primary and unique collation weights and also some
 * macros for easier native method invocations.
 */
#define	__wcprimcollwgt(wc)	/* primary weight for process code */	\
	(phdl->co_coltbl[0][(wc)])
#define	__wcuniqcollwgt(wc)	/* relative weight value */	\
	(phdl->co_coltbl[phdl->co_nord + phdl->co_r_order][(wc)])

#define	MBLEN_NATIVE(pwc, n)	\
	METHOD_NATIVE(cmapp, mblen)(cmapp, (pwc), (n))
#define	MBTOWC_NATIVE(pwc, s, n)	\
	METHOD_NATIVE(cmapp, mbtowc)(cmapp, (pwc), (s), (n))
#define	WCTOMB_NATIVE(s, pwc)	\
	METHOD_NATIVE(cmapp, wctomb)(cmapp, (s), (pwc))
#define	TOWLOWER_NATIVE(wc)	\
	METHOD_NATIVE(__lc_ctype, towlower)(__lc_ctype, (wc))
#define	TOWUPPER_NATIVE(wc)	\
	METHOD_NATIVE(__lc_ctype, towupper)(__lc_ctype, (wc))
#define	WCTYPE_NATIVE(clsstr)	\
	METHOD_NATIVE(__lc_ctype, wctype)(__lc_ctype, (clsstr))
#define	ISWCTYPE_NATIVE(wc, n)	\
	METHOD_NATIVE(__lc_ctype, iswctype)(__lc_ctype, (wc), (n))

/*
 * Macros to determine a word boundary
 */
#define	IS_WORDCHAR(c)		((c) == '_' || (isgraph(c) && !ispunct(c)))
#define	IS_WC_WORDCHAR(wc)	((wc) == '_' || \
				(ISWCTYPE_NATIVE(wc, _ISGRAPH) &&\
				!ISWCTYPE_NATIVE(wc, _ISPUNCT)))

#endif /* __H_PATLOCAL */
