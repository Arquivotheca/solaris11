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
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * This header file has been maintained to preserve source compatibility
 * for the two notable definitions which were herein: SC_WILDC and IDLEN.
 */


#ifndef _SAC_H
#define	_SAC_H

#include <sys/types.h>
#include <utmpx.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	IDLEN		_UTMP_ID_LEN	/* length in bytes of a utmp id */
#define	SC_WILDC	_UTMP_ID_WILDCARD /* wild character for utmp ids */

#ifdef	__cplusplus
}
#endif

#endif	/* _SAC_H */
