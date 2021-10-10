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
 * Copyright (c) 1990, 2006, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_STRREDIR_H
#define	_SYS_STRREDIR_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * strredir.h:	Declarations for the redirection driver and its matching
 *		STREAMS module.
 */

/*
 * The module's module id.
 *
 * XXX:	Since there's no authority responsible for administering this name
 *	space, there's no guarantee that this value is unique.  That wouldn't
 *	be so bad except that the DKI now suggests that ioctl cookie values
 *	should be based on module id to make them unique...
 */
#define	STRREDIR_MODID	7326

/*
 * Redirection ioctls:
 */
#define	SRIOCSREDIR	((STRREDIR_MODID<<16) | 1)	/* set redir target */
#define	SRIOCISREDIR	((STRREDIR_MODID<<16) | 2)	/* is redir target? */


/*
 * Everything from here on is of interest only to the kernel.
 */
#ifdef	_KERNEL

/* name of the module used to detect closes on redirected streams */
#define	STRREDIR_MOD	"redirmod"

extern void srpop(vnode_t *, boolean_t);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_STRREDIR_H */
