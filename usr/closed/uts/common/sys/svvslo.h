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
/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SVVSLO_H
#define	_SYS_SVVSLO_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Loopback driver ioctl requests
 */
#define	I_NOARG 20
#define	I_INTARG 21
#define	I_DDARG 22
#define	I_ERRNAK 23
#define	I_TIMOUT 24
#define	I_ERROR 25
#define	I_UDARG 26
#define	I_SLOW 28
#define	I_MODCMD 30
#define	I_SETBIGB 40
#define	I_CLRBIGB 41
#define	I_SETHANG 42
#define	I_SETERR 43
#define	I_SETOFAIL 44
#define	I_GRAB 50
#define	I_FREE 51
#define	I_SETWOFF 52
#define	I_CLRWOFF 53

#define	I_GRABLINK	245
#define	I_RELLINK	246
#define	I_GRABSEVENT	247
#define	I_RELSEVENT	248
#define	I_TIMEOUT	249
#define	I_STRTEST	250
#define	I_SLPTEST	251
#define	I_QPTR		252
#define	I_SETRANGE	253
#define	I_UNSETRANGE	254

struct lo {
	queue_t		*lo_rdq;
	int		lo_minval;
	t_uscalar_t	lo_fdinsertval;
};

/*
 * Misc parameters
 */
#define	IOCTLSZ		1024
#define	LONGTIME	4000000

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SVVSLO_H */
