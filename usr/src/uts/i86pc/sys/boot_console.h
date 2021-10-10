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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file is shared between dboot and the kernel.
 */

#ifndef _BOOT_CONSOLE_H
#define	_BOOT_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/bootinfo.h>

#define	CONS_INVALID		-1
#define	CONS_SCREEN_VGATEXT	0
#define	CONS_TTYA		1
#define	CONS_TTYB		2
#define	CONS_TTYC		3
#define	CONS_TTYD		4
#define	CONS_USBSER		5
#define	CONS_HYPERVISOR		6
#define	CONS_SCREEN_GRAPHICS	7
#define	CONS_SCREEN_FB		8
#define	CONS_ASYDEV		9

#define	CONS_MIN		CONS_SCREEN_VGATEXT
#define	CONS_MAX		CONS_ASYDEV

#define	CONS_COLOR		15

#define	BOOT_FB_NONE		0	/* Text mode (no framebuffer) */
#define	BOOT_FB_VESA		1	/* VESA-specific info provided */
#define	BOOT_FB_GENERIC		2	/* Generic framebuffer info provided */

extern int boot_fb_type;

extern void kb_init(void);
extern int kb_getchar(void);
extern int kb_ischar(void);

extern void bcons_init(struct xboot_info *);
extern void bcons_putchar(int);
extern int bcons_getchar(void);
extern int bcons_ischar(void);
extern int bcons_gets(char *, int);

#if !defined(_BOOT)
extern void bcons_init2(char *, char *, char *);
#endif /* !_BOOT */

extern int console;

#ifdef __cplusplus
}
#endif

#endif /* _BOOT_CONSOLE_H */
