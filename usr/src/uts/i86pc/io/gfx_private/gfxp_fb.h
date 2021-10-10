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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_GFXP_FB_H
#define	_GFXP_FB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/visual_io.h>
#include <sys/fbio.h>
#include <sys/vbios.h>
#include "gfx_private.h"
#include "gfxp_vga.h"
#include "gfxp_bitmap.h"

#define	MYNAME	"gfxp_fb"

#define	GFXP_FLAG_CONSOLE 	0x00000001
#define	GFXP_IS_CONSOLE(softc) 	((softc)->flags & GFXP_FLAG_CONSOLE)
#define	STREQ(a, b)		(strcmp((a), (b)) == 0)

struct gfxp_ops;

struct gfxp_softc {
	dev_info_t		*devi;
	/* KD_TEXT or KD_GRAPHICS */
	int			mode;
	/* BITMAP or VGATEXT */
	uint8_t			fb_type;
	struct vis_polledio	polledio;
	unsigned int 		flags;
	kmutex_t 		lock;
	char			silent;
	/* Handle for the VBIOS helper. */
	struct fbgattr 		*fbgattr;
	struct gfxp_ops		*ops;
	union {
		struct gfxp_bm_console		fb;
		struct gfxp_vga_console		vga;
	} 			console;
};

struct gfxp_ops {
	int (*kdsetmode) (struct gfxp_softc *, int);
	int (*devinit) (struct gfxp_softc *, struct vis_devinit *);
	void (*conscopy) (struct gfxp_softc *, struct vis_conscopy *);
	void (*consdisplay) (struct gfxp_softc *, struct vis_consdisplay *);
	void (*conscursor) (struct gfxp_softc *, struct vis_conscursor *);
	int (*consclear) (struct gfxp_softc *, struct vis_consclear *);
	int (*suspend) (struct gfxp_softc *);
	void (*resume) (struct gfxp_softc *);
	int (*devmap) (struct gfxp_softc *, dev_t, devmap_cookie_t, offset_t,
		size_t, size_t *, uint_t);
};

/* Both gfxp_vga.c and gfxp_bitmap.c use the same attributes. */
static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

#ifdef __cplusplus
}
#endif

#endif /* _GFXP_FB_H */
