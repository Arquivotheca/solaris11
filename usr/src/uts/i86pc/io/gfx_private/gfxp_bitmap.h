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

#ifndef	_GFXP_BM_H
#define	_GFXP_BM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/pnglib.h>
#include <sys/rgb.h>
#include "gfx_private.h"

struct gfxp_shadow_fb {
	uint32_t	size;
	uint8_t		*map;
};

#define	GFXP_VESA	(1)

struct gfxp_vesa_exi {
	uint16_t	mode;
};

struct gfxp_bm_console {
	uint16_t		xres;
	uint16_t		yres;
	uint32_t		memsize;
	uint32_t		scanline;
	uintptr_t		fb_phys_addr;
	uint8_t			bpp;
	uint8_t			depth;
	uint8_t			active;
	uint8_t			going_down;
	uint8_t			*fb_map;
	rgb_t			rgb;
	/* shadow buffer */
	struct gfxp_shadow_fb	shadow;
	/* splash background loading syncronization */
	kmutex_t 		splash_bg_mu;
	kcondvar_t		splash_bg_cv;
	boolean_t		splash_bg_loading;
	boolean_t		splash_bg_loaded;
	/* shutdown progressbar update. */
	kmutex_t		splash_step_mu;
	kcondvar_t		splash_step_cv;
	boolean_t		splash_steps_done;
	uint8_t			do_splash_step;
	kthread_t		*splash_tid;
	/* cursor handling */
	struct {
		boolean_t visible;
		uint16_t row;
		uint16_t col;
		uint8_t *curbuf;
		uint8_t	*curdst;
		uint32_t cursize;
	} 			cursor;
	uint8_t			type;
	/* We support only VESA at the moment, but leave space for extension. */
	union gfxp_gfx_exinfo {
		struct gfxp_vesa_exi	vesa;
	} gfx_exinfo;
	struct gfxp_blt_ops	*fbops;
};

struct gfxp_img_thr {
	struct gfxp_softc 	*softc;
	char			*imgname;
	png_screen_t		s_image;
};

#ifdef __cplusplus
}
#endif

#endif /* _GFXP_BM_H */
