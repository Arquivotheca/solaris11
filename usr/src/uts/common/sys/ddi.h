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
 * Copyright (c) 1988, 2004, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_DDI_H
#define	_SYS_DDI_H

#include <sys/types.h>
#include <sys/map.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/stream.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * ddi.h -- the flag and function definitions needed by DDI-conforming
 * drivers.  This header file contains #undefs to undefine macros that
 * drivers would otherwise pick up in order that function definitions
 * may be used. Programmers should place the include of "sys/ddi.h"
 * after any header files that define the macros #undef'ed or the code
 * may compile incorrectly.
 */

/*
 * define min() and max() as macros so that drivers will not pick up the
 * min() and max() kernel functions since they do signed comparison only.
 */
#ifdef	min
#undef	min
#endif	/* min */
#define	min(a, b)	((a) < (b) ? (a) : (b))

#ifdef	max
#undef	max
#endif	/* max */
#define	max(a, b)	((a) < (b) ? (b) : (a))

#define	TIME	1
#define	UPROCP	2
#define	PPGRP	3
#define	LBOLT	4
#define	SYSRINT	5
#define	SYSXINT	6
#define	SYSMINT	7
#define	SYSRAWC	8
#define	SYSCANC	9
#define	SYSOUTC	10
#define	PPID	11
#define	PSID	12
#define	UCRED	13

#ifdef __STDC__
extern int drv_getparm(uint_t, void *);
extern int drv_setparm(uint_t, ulong_t);
extern void drv_usecwait(clock_t);
extern clock_t drv_hztousec(clock_t);
extern clock_t drv_usectohz(clock_t);
extern void delay(clock_t);
extern void time_to_wait(clock_t *, clock_t);

#else

extern int drv_getparm();
extern int drv_setparm();
extern void drv_usecwait();
extern clock_t drv_hztousec();
extern clock_t drv_usectohz();
extern void delay();
extern time_to_wait();
#endif	/* __STDC__ */

/* XXX -- should be changed to major_t */
/* convert external to internal major number */

#ifdef __STDC__
extern int etoimajor(major_t);
/* convert internal to extern major number */
extern int itoemajor(major_t, int);

#else

extern int etoimajor();
/* convert internal to extern major number */
extern int itoemajor();
#endif	/* __STDC__ */

#if defined(__STDC__)

extern int drv_priv(struct cred *);

#else

extern int drv_priv();

#endif

/*
 * The following declarations take the place of macros in
 * sysmacros.h The undefs are for any case where a driver includes
 * sysmacros.h, even though DDI conforming drivers must not.
 */
#undef getemajor
#undef geteminor
#undef getmajor
#undef getminor
#undef makedevice
#undef cmpdev
#undef expdev


#ifdef __STDC__
extern major_t getemajor(dev_t);
extern minor_t geteminor(dev_t);
extern major_t getmajor(dev_t);
extern minor_t getminor(dev_t);
extern dev_t makedevice(major_t, minor_t);
extern o_dev_t cmpdev(dev_t);
extern dev_t expdev(dev_t);
#else
extern major_t getemajor();
extern minor_t geteminor();
extern major_t getmajor();
extern minor_t getminor();
extern dev_t makedevice();
extern o_dev_t cmpdev();
extern dev_t expdev();
#endif	/* __STDC__ */

/*
 * The following macros from param.h are also being converted to
 * functions and #undefs must be done here as well since param.h
 * will be included by most if not every driver
 */

#undef btop
#undef btopr
#undef ptob


#ifdef __STDC__
extern unsigned long btop(unsigned long);
extern unsigned long btopr(unsigned long);
extern unsigned long ptob(unsigned long);
#else
extern unsigned long btop();
extern unsigned long btopr();
extern unsigned long ptob();
#endif	/* __STDC__ */


/* STREAMS drivers and modules must include stream.h to pick up the */
/* needed structure and flag definitions. As was the case with map.h, */
/* macros used by both the kernel and drivers in times past now have */
/* a macro definition for the kernel and a function definition for */
/* drivers. The following #undefs allow drivers to include stream.h */
/* but call the functions rather than macros. */

#undef OTHERQ
#undef RD
#undef WR
#undef SAMESTR
#undef datamsg


#ifdef __STDC__
extern struct queue *OTHERQ(queue_t *);	/* stream.h */
extern struct queue *RD(queue_t *);
extern struct queue *WR(queue_t *);
extern int SAMESTR(queue_t *);
extern int datamsg(unsigned char);

#else

extern struct queue *OTHERQ();	/* stream.h */
extern struct queue *RD();
extern struct queue *WR();
extern int SAMESTR();
extern int datamsg();
#endif	/* __STDC__ */

/* declarations of functions for allocating and deallocating the space */
/* for a buffer header (just a header, not the associated buffer) */

#ifdef __STDC__
extern struct buf *getrbuf(int);
extern void freerbuf(struct buf *);
#else
extern struct buf *getrbuf();
extern void freerbuf();
#endif	/* __STDC__ */

#ifdef	_KERNEL
/*
 * SVR4MP replacement for hat_getkpfnum()
 */
#define	NOPAGE	(-1)	/* value returned for invalid addresses */

typedef pfn_t	ppid_t;	/* a 'physical page identifier' - no math allowed! */

#ifdef	__STDC__
extern ppid_t kvtoppid(caddr_t);
#else	/* __STDC__ */
extern ppid_t kvtoppid();
#endif	/* __STDC__ */

extern int qassociate(queue_t *, int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDI_H */
