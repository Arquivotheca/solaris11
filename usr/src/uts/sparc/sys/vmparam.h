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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _SYS_VMPARAM_H
#define	_SYS_VMPARAM_H

#if (defined(_KERNEL) || defined(_KMEMUSER)) && defined(_MACHDEP)
#include <sys/vm_machparam.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define	SSIZE		4096		/* initial stack size */
#define	SINCR		4096		/* increment of stack */

/*
 * USRSTACK is the top (end) of the user stack.
 */
#define	USRSTACK	USERLIMIT
#define	USRSTACK32	USERLIMIT32
#define	USRSTACK64_32	USERLIMIT32

/*
 * Implementation architecture independent sections of the kernel use
 * this section.
 */
#if (defined(_KERNEL) || defined(_KMEMUSER)) && !defined(_MACHDEP)

#if defined(_KERNEL) && !defined(_ASM)
extern const unsigned int	_diskrpm;
extern const unsigned long	_dsize_limit;
extern const unsigned long	_ssize_limit;
extern const unsigned long	_pgthresh;
extern const unsigned int	_maxslp;
extern const unsigned long	_maxhandspreadpages;
#endif	/* defined(_KERNEL) && !defined(_ASM) */

#define	DISKRPM		_diskrpm
#define	DSIZE_LIMIT	_dsize_limit
#define	SSIZE_LIMIT	_ssize_limit
#define	PGTHRESH	_pgthresh
#define	MAXSLP		_maxslp
#define	MAXHANDSPREADPAGES	_maxhandspreadpages

#endif	/* (defined(_KERNEL) || defined(_KMEMUSER)) && !defined(_MACHDEP) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VMPARAM_H */
