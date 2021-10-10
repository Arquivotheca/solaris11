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

/*
 * Graphic bit-mapped frame buffer support.
 */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/fbinfo.h>
#include <sys/kd.h>
#include <sys/atomic.h>
#include <sys/pnglib.h>
/* For console types. */
#include <sys/bootinfo.h>
#include <sys/boot_console.h>

#include "gfx_private.h"
#include "gfxp_fb.h"
#include "gfxp_bitmap.h"

extern void splash_stop();
extern int splash_shutdown_start();
extern void splash_shutdown_update();
extern void splash_shutdown_last();
extern uint8_t force_screen_output;
extern boolean_t check_reset_console;

/* Frame buffer functions prototypes */
static int gfxp_bm_devinit(struct gfxp_softc *, struct vis_devinit *);
static void gfxp_bm_cons_copy(struct gfxp_softc *, struct vis_conscopy *);
static void gfxp_bm_cons_display(struct gfxp_softc *,
	struct vis_consdisplay *);
static void gfxp_bm_cons_cursor(struct gfxp_softc *,
	struct vis_conscursor *);
static int gfxp_bm_cons_clear(struct gfxp_softc *, struct vis_consclear *);
static void gfxp_bm_polled_display(struct vis_polledio_arg *,
	struct vis_consdisplay *);
static void gfxp_bm_polled_cursor(struct vis_polledio_arg *,
	struct vis_conscursor *);
static void gfxp_bm_polled_copy(struct vis_polledio_arg *,
	struct vis_conscopy *);
static int gfxp_bm_kdsetmode(struct gfxp_softc *, int);
static int gfxp_bm_kdshutdown(struct gfxp_softc *, int);
static void gfxp_bm_kdsettext(struct gfxp_softc *);
static int gfxp_bm_suspend(struct gfxp_softc *);
static void gfxp_bm_resume(struct gfxp_softc *);
static int gfxp_bm_devmap(struct gfxp_softc *, dev_t, devmap_cookie_t,
	offset_t, size_t, size_t *, uint_t);
static void gfxp_bm_shutdown_end(struct gfxp_softc *);

/* Structure for FBIOGATTR ioctl. fbtype will be filled at DEVINIT time. */
static struct fbgattr gfxp_bm_attr =  {
/*	real_type	owner */
	FBTYPE_SUNFAST_COLOR, 0,
/* fbtype: type		h  w  depth cms  size */
	{ FBTYPE_SUNFAST_COLOR, 0, 0, 0, 0, 0 },
/* fbsattr: flags emu_type	dev_specific */
	{ 0, FBTYPE_SUN4COLOR, { 0 } },
/*	emu_types */
	{ -1 }
};

/*
 * Handy macros. Note that they expect a 'struct gfxp_softc *softc'
 * declaration.
 */
#define	FBCONS		(softc->console.fb)
#define	SHADOW_FB	(FBCONS.shadow)

static struct gfxp_ops	fb_ops = {
	gfxp_bm_kdsetmode,	/* kdsetmode 	*/
	gfxp_bm_devinit,	/* devinit 	*/
	gfxp_bm_cons_copy,	/* conscopy 	*/
	gfxp_bm_cons_display,	/* consdisplay 	*/
	gfxp_bm_cons_cursor,	/* conscursor 	*/
	gfxp_bm_cons_clear,	/* consclear	*/
	gfxp_bm_suspend,	/* suspend	*/
	gfxp_bm_resume,		/* resume	*/
	gfxp_bm_devmap		/* devmap	*/
};

/*
 * Generic drawing functions.
 * These functions emulate in software the blit/copy of a square/rectangle on
 * the screen. They are used by default and, right now, in the vast majority (if
 * not the totality) of cases. If a graphic driver supports hardware blitting
 * it can register a function callbacks via gfxp_bm_register_fbops().
 * NOTE that these functions are also used in polled I/O mode, and are so
 * subject to its limitations.
 */
static void gfxp_bm_generic_blt(struct gfxp_softc *softc,
    struct vis_consdisplay *);
static void gfxp_bm_generic_copy(struct gfxp_softc *softc,
    struct vis_conscopy *);

/*
 * Shadow buffer update functions.
 * These functions update the shadow buffer content, keeping it in sync after
 * a copy or a blt operation, regardless if it's software emulated or
 * hardware driven.
 */
static void gfxp_bm_blt_updtshadow(struct gfxp_softc *softc,
    struct vis_consdisplay *da);
static void gfxp_bm_copy_updtshadow(struct gfxp_softc *softc,
    struct vis_conscopy *ma);

static void
gfxp_bm_paint_memory(struct vis_consdisplay *da, uint8_t *map,
    uint32_t scanline, uint8_t bpp)
{
	uint32_t	offset;
	uint8_t		*data = da->data;
	uint8_t		*where;
	int		i;

	offset = (da->col * bpp) + da->row * scanline;
	where = (uint8_t *)(map + offset);

	for (i = 0; i < da->height; i++) {
		uint8_t	*dest = where + (i * scanline);
		uint8_t	*src = data + (i * (da->width * bpp));

		(void) memcpy(dest, src, (da->width * bpp));
	}
}

static void
gfxp_bm_generic_blt(struct gfxp_softc *softc, struct vis_consdisplay *da)
{
	gfxp_bm_paint_memory(da, FBCONS.fb_map, FBCONS.scanline, FBCONS.bpp);
}

static void
gfxp_bm_blt_updtshadow(struct gfxp_softc *softc, struct vis_consdisplay *da)
{
	gfxp_bm_paint_memory(da, SHADOW_FB.map, FBCONS.scanline, FBCONS.bpp);
}

/*
 * Copy types.
 * DO_MEMCPY: memcpy from the src pointer to the dest pointer (relative offsets
 *            specified by the members of vis_conscopy).
 * DO_MEMMOVE: use memmove instead of memcpy.
 * The reason for that is that we want to access the graphic framebuffer as
 * little as possible (for performance reasons) and so we copy from the shadow
 * buffer instead of memmoving memory on the graphics fb.
 */
#define	DO_MEMCPY	(0)
#define	DO_MEMMOVE	(1)

static void
gfxp_bm_copy_memory(struct vis_conscopy *ma, uint8_t *srcmap, uint8_t *destmap,
    uint32_t scanline, uint8_t bpp, uint8_t type)
{
	uint32_t	s_offset, t_offset;
	uint8_t		*where_dest, *where_src;
	uint32_t	width, height;
	int		i = 0;

	if (type != DO_MEMCPY && type != DO_MEMMOVE)
		return;

	/* Copy from */
	s_offset = (ma->s_col * bpp) + ma->s_row * scanline;
	where_src = (uint8_t *)(srcmap + s_offset);

	/* Copy to */
	t_offset = (ma->t_col * bpp) + ma->t_row * scanline;
	where_dest = (uint8_t *)(destmap + t_offset);

	/* How much */
	width = (ma->e_col - ma->s_col + 1) * bpp;
	height = (ma->e_row - ma->s_row + 1);

	/* Iterate scanline over scanline. */
	for (i = 0; i < height; i++) {
		uint32_t incr;
		uint8_t	*dest, *src;

		if (t_offset < s_offset)
			incr = i * scanline;
		else
			incr = (height - i - 1) * scanline;

		dest = where_dest + incr;
		src = where_src + incr;

		if (type == DO_MEMCPY)
			(void) memcpy(dest, src, width);
		if (type == DO_MEMMOVE)
			(void) memmove(dest, src, width);
	}
}

static void
gfxp_bm_generic_copy(struct gfxp_softc *softc, struct vis_conscopy *ma)
{
	gfxp_bm_copy_memory(ma, SHADOW_FB.map, FBCONS.fb_map, FBCONS.scanline,
	    FBCONS.bpp, DO_MEMCPY);
}

static void
gfxp_bm_copy_updtshadow(struct gfxp_softc *softc, struct vis_conscopy *ma)
{
	gfxp_bm_copy_memory(ma, SHADOW_FB.map, SHADOW_FB.map, FBCONS.scanline,
	    FBCONS.bpp, DO_MEMMOVE);
}

/*
 * Public functions to register and unregister driver routines with the
 * generic gfx driver. NOTE: FBCONS macro expects the vgatext_softc structure
 * pointer to be called 'softc'.
 */
void
gfxp_bm_register_fbops(gfxp_vgatext_softc_ptr_t ptr, struct gfxp_blt_ops *fbops)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;
	FBCONS.fbops = fbops;
}

void
gfxp_bm_unregister_fbops(gfxp_vgatext_softc_ptr_t ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;
	FBCONS.fbops = NULL;
}


/*
 * Not much to do here, just setup function pointers for polled I/O (used f.e.
 * by kmdb) and for gfxp ioctl handling. Initialize splash shutdown
 * synchronization primitives here too.
 */
void
gfxp_bm_attach(struct gfxp_softc *softc)
{
	softc->polledio.display = gfxp_bm_polled_display;
	softc->polledio.copy = gfxp_bm_polled_copy;
	softc->polledio.cursor = gfxp_bm_polled_cursor;
	softc->ops = &fb_ops;

	mutex_init(&FBCONS.splash_bg_mu, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&FBCONS.splash_step_mu, NULL, MUTEX_DRIVER, NULL);
	cv_init(&FBCONS.splash_bg_cv, NULL, CV_DRIVER, NULL);
	cv_init(&FBCONS.splash_step_cv, NULL, CV_DRIVER, NULL);

	FBCONS.splash_bg_loading = B_FALSE;
	FBCONS.splash_bg_loaded = B_FALSE;
	FBCONS.splash_steps_done = B_FALSE;
	FBCONS.do_splash_step = 0;
}

void
gfxp_bm_detach(struct gfxp_softc *softc)
{
	if (SHADOW_FB.map != NULL)
		kmem_free(SHADOW_FB.map, SHADOW_FB.size);
	if (FBCONS.fb_map != NULL)
		gfxp_unmap_kernel_space((gfxp_kva_t)FBCONS.fb_map,
		    FBCONS.memsize);

	if (FBCONS.splash_bg_loaded && !FBCONS.splash_steps_done)
		gfxp_bm_shutdown_end(softc);

	mutex_destroy(&FBCONS.splash_bg_mu);
	mutex_destroy(&FBCONS.splash_step_mu);
	cv_destroy(&FBCONS.splash_bg_cv);
	cv_destroy(&FBCONS.splash_step_cv);
}

static void
gfxp_bm_export_fbinfo(struct gfxp_softc *softc)
{
	dev_info_t 	*devi = softc->devi;
	int		unit = ddi_get_instance(devi);
	dev_t		devnum = makedevice(DDI_MAJOR_T_UNKNOWN, unit);

	/* Export framebuffer information as properties. */
	(void) ddi_prop_update_int64(devnum, devi, "fb-phys-address",
	    FBCONS.fb_phys_addr);
	(void) ddi_prop_update_int(devnum, devi, "fb-width", FBCONS.xres);
	(void) ddi_prop_update_int(devnum, devi, "fb-height", FBCONS.yres);
	(void) ddi_prop_update_int(devnum, devi, "fb-depth", FBCONS.depth);
	(void) ddi_prop_update_int(devnum, devi, "fb-bpp", FBCONS.bpp);
	(void) ddi_prop_update_int(devnum, devi, "fb-red-size",
	    FBCONS.rgb.red.size);
	(void) ddi_prop_update_int(devnum, devi, "fb-red-shift",
	    FBCONS.rgb.red.shift);
	(void) ddi_prop_update_int(devnum, devi, "fb-green-size",
	    FBCONS.rgb.green.size);
	(void) ddi_prop_update_int(devnum, devi, "fb-green-shift",
	    FBCONS.rgb.green.shift);
	(void) ddi_prop_update_int(devnum, devi, "fb-blue-size",
	    FBCONS.rgb.blue.size);
	(void) ddi_prop_update_int(devnum, devi, "fb-blue-shift",
	    FBCONS.rgb.blue.shift);
}

/*
 * Setup the frame buffer console based on the information gathered at boot time
 * and saved inside the fb_info structure.
 */
static int
gfxp_setup_fbcons(struct gfxp_softc *softc)
{
	uint32_t		fb_size;
	/* Boot time gathered information. */
	extern uint16_t		saved_vbe_mode;

	FBCONS.xres = fb_info.screen_pix.x;
	FBCONS.yres = fb_info.screen_pix.y;
	FBCONS.depth = fb_info.depth;
	FBCONS.bpp = fb_info.bpp;
	FBCONS.rgb = fb_info.rgb;

	/*
	 * Make the frame buffer big enough to hold the entire screen.
	 * Since we do not use any panning approach, there is no need to use
	 * more memory than that.
	 */
	fb_size = FBCONS.bpp * FBCONS.xres * FBCONS.yres;

	FBCONS.scanline = FBCONS.xres * FBCONS.bpp;
	FBCONS.memsize = ptob(btopr(fb_size));
	FBCONS.fb_phys_addr = fb_info.phys_addr;
	FBCONS.going_down = 0;
	SHADOW_FB.map = NULL;

	/* Map linear frame buffer */
	FBCONS.fb_map = (uint8_t *)gfxp_map_kernel_space(FBCONS.fb_phys_addr,
	    FBCONS.memsize, GFXP_MEMORY_WRITECOMBINED);

	if (FBCONS.fb_map == NULL)
		return (1);

	/* Alloc space for the cursor. */
	FBCONS.cursor.cursize = fb_info.font_width * fb_info.font_height *
	    FBCONS.bpp;
	FBCONS.cursor.curbuf = kmem_zalloc(FBCONS.cursor.cursize, KM_SLEEP);
	FBCONS.cursor.curdst = kmem_zalloc(FBCONS.cursor.cursize, KM_SLEEP);

	/* Map shadow fb -- XXX should be KM_NOSLEEP? */
	SHADOW_FB.map = kmem_zalloc(fb_size, KM_SLEEP);

	SHADOW_FB.size = FBCONS.memsize;

	/* XXX Move output to /var/adm/messages. */
	cmn_err(CE_CONT, "!"MYNAME ": %dx%d - %d bit framebuffer mapped at %p",
	    FBCONS.xres, FBCONS.yres, FBCONS.depth, (void *)FBCONS.fb_map);
	cmn_err(CE_CONT, "!"MYNAME ": shadow framebuffer mapped at %p",
	    (void *)SHADOW_FB.map);
	cmn_err(CE_CONT, "!"MYNAME ": using %dx%d fonts", fb_info.font_width,
	    fb_info.font_height);

	/* By default use software emulated blt/copy/clear routines. */
	FBCONS.fbops = NULL;

	/* Fill the fbgattr structure to correctly support terminal I/O. */
	gfxp_bm_attr.fbtype.fb_width = FBCONS.xres;
	gfxp_bm_attr.fbtype.fb_height = FBCONS.yres;
	gfxp_bm_attr.fbtype.fb_depth = FBCONS.depth;
	gfxp_bm_attr.fbtype.fb_cmsize = 0;
	gfxp_bm_attr.fbtype.fb_size = fb_size;

	/* Fill the gfx extra information. */
	FBCONS.type = GFXP_VESA;
	/*
	 * We saved the vbe mode for fastreboot. Keep track of it here to
	 * execute a SETMODE vbios call each time X exits or the user
	 * switches from X to the console.
	 */
	FBCONS.gfx_exinfo.vesa.mode = saved_vbe_mode;

	if (console == CONS_SCREEN_GRAPHICS) {
		/* Push output only to the shadow buffer. */
		FBCONS.active = 0;
	} else {
		/* Sync the shadow buffer with what is on the screen. */
		(void) memcpy(SHADOW_FB.map, FBCONS.fb_map, fb_size);
		/* Activate output to the screen. */
		FBCONS.active = 1;
	}

	/*
	 * Export framebuffer information as properties. This both allows to
	 * quickly check/debug the framebuffer setup from user land and
	 * allows programs that wish to paint on a mmaped frame buffer to know
	 * its configuration.
	 */
	gfxp_bm_export_fbinfo(softc);
	return (0);
}

static uint32_t
gfxp_4to8(uint8_t color)
{
	/*
	 * For DirectColor 8-bit framebuffers (this really should be
	 * dynamically created based on the number of R/G/B bits).  This
	 * assumes a 3/3/2 split of R/G/B (MSB to LSB)
	 */
	static uint8_t cmap4_to_8[16] = {
/* BEGIN CSTYLED */
	/* 0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15   
	  Wh+  Bk   Bl   Gr   Cy   Rd   Mg   Br   Wh   Bk+  Bl+  Gr+  Cy+  Rd+  Mg+  Yw */
	  0xff,0x00,0x01,0x80,0x09,0x40,0x41,0x48,0x49,0x25,0x03,0x1c,0x1f,0xe0,0xe3,0xfc,
/* END CSTYLED */
	};

	if (color < (sizeof (cmap4_to_8) / sizeof (cmap4_to_8[0])))
		return (cmap4_to_8[color]);
	else
		return ((uint32_t)~0);
}

/*
 * VIS_DEVINIT ioctl handler.
 */
static int
gfxp_bm_devinit(struct gfxp_softc *softc, struct vis_devinit *data)
{
	if (gfxp_setup_fbcons(softc) != 0)
		return (1);

	/* Fill vis_devinit structure. */
	data->version = VIS_CONS_REV;
	data->width = FBCONS.xres;
	data->height = FBCONS.yres;
	data->depth = FBCONS.depth;
	data->color_map = (data->depth == 8) ? gfxp_4to8 : NULL;
	data->linebytes = FBCONS.scanline;
	data->mode = VIS_PIXEL;
	data->polledio = &softc->polledio;

	softc->fbgattr = &gfxp_bm_attr;

	return (0);
}

/*
 * VIS_CONSDISPLAY(da) ioctl handler.
 * Paint the buffer contained in da->data at the position specified by
 * da->col and da->row. 'col' and 'row' contain pixel values.
 */
static void
gfxp_bm_cons_display(struct gfxp_softc *softc, struct vis_consdisplay *da)
{
	/* Sanitize input. */
	if (da->col < 0 || da->col >= FBCONS.xres ||
	    da->row < 0 || da->row >= FBCONS.yres ||
	    da->col + da->width > FBCONS.xres ||
	    da->row + da->height > FBCONS.yres) {
		return;
	}

	/*
	 * If we are in "console mode", update the screen.
	 * Use an hardware provided blt function, if possible, otherwise
	 * fallback to gfxp_bm_generic_blt().
	 */
	if (FBCONS.active == 1) {
		if (FBCONS.fbops == NULL || FBCONS.fbops->blt == NULL ||
		    FBCONS.fbops->blt(da) != GFXP_SUCCESS)
			gfxp_bm_generic_blt(softc, da);

		/* Flush to memory. */
		membar_producer();
	}
	/* Update the shadow buffer. */
	gfxp_bm_blt_updtshadow(softc, da);
}

static void
gfxp_bm_polled_display(
	struct vis_polledio_arg *arg,
	struct vis_consdisplay *da)
{
	struct gfxp_softc	*softc = (struct gfxp_softc *)arg;
	uint8_t			prev_state;

	if (force_screen_output) {
		prev_state = FBCONS.active;
		FBCONS.active = 1;
	}

	gfxp_bm_cons_display((struct gfxp_softc *)arg, da);

	if (force_screen_output)
		FBCONS.active = prev_state;
}

/*
 * VIS_CONSCOPY(ma) ioctl handler.
 * Copy the area defined by ma->s_col/s_row ma->e_col/e_row at the position
 * specified by ma->t_row/t_col. All values are in pixels.
 */
static void
gfxp_bm_cons_copy(struct gfxp_softc *softc, struct vis_conscopy *ma)
{
	uint32_t	width, height;

	/* Sanity checks */
	if (ma->s_col < 0 || ma->s_col >= FBCONS.xres ||
	    ma->s_row < 0 || ma->s_row >= FBCONS.yres ||
	    ma->e_col < 0 || ma->e_col >= FBCONS.xres ||
	    ma->e_row < 0 || ma->e_row >= FBCONS.yres ||
	    ma->t_col < 0 || ma->t_col >= FBCONS.xres ||
	    ma->t_row < 0 || ma->t_row >= FBCONS.yres ||
	    ma->s_col > ma->e_col ||
	    ma->s_row > ma->e_row ||
	    (ma->s_row == ma->t_row && ma->t_col == ma->s_col)) {
		return;
	}

	/*
	 * Compute how much we're going to move here too, to catch malicious
	 * or broken attempts.
	 */
	width = (ma->e_col - ma->s_col + 1);
	height = (ma->e_row - ma->s_row + 1);

	if (ma->t_row + height > FBCONS.yres ||
	    ma->t_col + width > FBCONS.xres)
		return;

	if (FBCONS.active == 1) {
		if (FBCONS.fbops == NULL || FBCONS.fbops->copy == NULL ||
		    FBCONS.fbops->copy(ma) != GFXP_SUCCESS)
			gfxp_bm_generic_copy(softc, ma);

		/* Flush to memory. */
		membar_producer();
	}
	/* Update the shadow buffer. */
	gfxp_bm_copy_updtshadow(softc, ma);
}

static void
gfxp_bm_polled_copy(
	struct vis_polledio_arg *arg,
	struct vis_conscopy *ma)
{
	struct gfxp_softc	*softc = (struct gfxp_softc *)arg;
	uint8_t			prev_state;

	if (force_screen_output) {
		prev_state = FBCONS.active;
		FBCONS.active = 1;
	}

	gfxp_bm_cons_copy(softc, ma);

	if (force_screen_output)
		FBCONS.active = prev_state;
}

/*
 * Cursor handling routines.
 */
static void
gfxp_bm_hide_cursor(struct gfxp_softc *softc, struct vis_conscursor *ca)
{
	struct vis_consdisplay 	cursor;

	/* sanity checks */
	if (ca->col < 0 || ca->col >= FBCONS.xres ||
	    ca->row < 0 || ca->row >= FBCONS.yres ||
	    ca->col + ca->width > FBCONS.xres ||
	    ca->row + ca->height > FBCONS.yres) {
		return;
	}

	cursor.row = ca->row;
	cursor.col = ca->col;
	cursor.width = ca->width;
	cursor.height = ca->height;
	cursor.data = FBCONS.cursor.curbuf;

	gfxp_bm_cons_display(softc, &cursor);
}

/*
 * Depending on the bit-depth of the frame buffer we interprete differently
 * ca->[fg|bg].color. This is necessary to create the effect of 'highlighting'
 * the character under the cursor.
 */
static void
gfxp_bm_prepare_cursor_8bit(struct gfxp_softc *softc,
    struct vis_conscursor *ca)
{
	int		i = 0;

	for (i = 0; i < FBCONS.cursor.cursize; i++) {
		if (FBCONS.cursor.curbuf[i] == gfxp_4to8(ca->fg_color.eight))
			FBCONS.cursor.curdst[i] = gfxp_4to8(ca->bg_color.eight);
		else
			FBCONS.cursor.curdst[i] = gfxp_4to8(ca->fg_color.eight);
	}
}

static void
gfxp_bm_prepare_cursor_16bit(struct gfxp_softc *softc,
    struct vis_conscursor *ca)
{
	uint16_t	*src = (uint16_t *)FBCONS.cursor.curbuf;
	uint16_t	*dst = (uint16_t *)FBCONS.cursor.curdst;
	uint16_t	fg_color_16 = 0;
	uint16_t	bg_color_16 = 0;
	int		i = 0;

	fg_color_16 = ca->fg_color.sixteen[0] |
	    ca->fg_color.sixteen[1] << 8;

	bg_color_16 = ca->bg_color.sixteen[0] |
	    ca->bg_color.sixteen[1] << 8;

	for (i = 0; i < FBCONS.cursor.cursize; i += 2) {
		if (*src++ == fg_color_16)
			*dst++ = bg_color_16;
		else
			*dst++ = fg_color_16;
	}
}

static void
gfxp_bm_prepare_cursor_32bit(struct gfxp_softc *softc,
    struct vis_conscursor *ca)
{
	uint32_t	*src = (uint32_t *)FBCONS.cursor.curbuf;
	uint32_t	*dst = (uint32_t *)FBCONS.cursor.curdst;
	uint32_t	fg_color_32 = 0;
	uint32_t	bg_color_32 = 0;
	int		i = 0;

	fg_color_32 = ca->fg_color.twentyfour[0] |
	    ca->fg_color.twentyfour[1] << 8 |
	    ca->fg_color.twentyfour[2] << 16;

	bg_color_32 = ca->bg_color.twentyfour[0] |
	    ca->bg_color.twentyfour[1] << 8 |
	    ca->bg_color.twentyfour[2] << 16;

	for (i = 0; i < FBCONS.cursor.cursize; i += 4) {
		if (*src++ == fg_color_32)
			*dst++ = bg_color_32;
		else
			*dst++ = fg_color_32;
	}
}

/*
 * Display the cursor. We first prepare the cursor buffer as per the functions
 * above and then we invoke the VIS_CONSDISPLAY handler directly to paint it
 * on the screen.
 */
static void
gfxp_bm_show_cursor(struct gfxp_softc *softc, struct vis_conscursor *ca)
{
	uint32_t	offset;
	uint8_t		*where;
	uint8_t		bpp = FBCONS.bpp;
	struct vis_consdisplay cursor;
	int		i;

	/* sanity checks */
	if (ca->col < 0 || ca->col >= FBCONS.xres ||
	    ca->row < 0 || ca->row >= FBCONS.yres ||
	    ca->col + ca->width > FBCONS.xres ||
	    ca->row + ca->height > FBCONS.yres) {
		return;
	}

	offset = (ca->col * bpp) + ca->row * FBCONS.scanline;
	where = (uint8_t *)(SHADOW_FB.map + offset);

	/* Save data under the cursor */
	for (i = 0; i < ca->height; i++) {
		uint8_t	*src = where + (i * FBCONS.scanline);
		uint8_t	*dest = FBCONS.cursor.curbuf + (i *
		    (ca->width * bpp));

		(void) memcpy(dest, src, (ca->width * bpp));
	}

	/* Prepare the cursor paint to display on the screen */
	if (FBCONS.bpp == 1)
		gfxp_bm_prepare_cursor_8bit(softc, ca);
	else if (FBCONS.bpp == 2)
		gfxp_bm_prepare_cursor_16bit(softc, ca);
	else if	(FBCONS.bpp == 4)
		gfxp_bm_prepare_cursor_32bit(softc, ca);

	cursor.row = ca->row;
	cursor.col = ca->col;
	cursor.width = ca->width;
	cursor.height = ca->height;
	cursor.data = FBCONS.cursor.curdst;

	/* Call VIS_CONSDISPLAY handler. */
	gfxp_bm_cons_display(softc, &cursor);
}

/*
 * VIS_CONCURSOR(ca) ioctl handler.
 * ca->action specifies what to do with respect to the cursor:
 * VIS_DISPLAY_CURSOR: show the cursor somewhere on the screen. Save the content
 *                     of the screen under the cursor.
 * VIS_HIDE_CURSOR: restore the content of the screen where the cursor was
 *                  previously placed.
 * VIS_GET_CURSOR: get x,y cursor position on the screen.
 */
static void
gfxp_bm_cons_cursor(struct gfxp_softc *softc, struct vis_conscursor *ca)
{
	switch (ca->action) {
	case VIS_HIDE_CURSOR:
		FBCONS.cursor.visible = B_FALSE;
		gfxp_bm_hide_cursor(softc, ca);
		break;
	case VIS_DISPLAY_CURSOR:
		FBCONS.cursor.visible = B_TRUE;
		FBCONS.cursor.col = ca->col;
		FBCONS.cursor.row = ca->row;
		gfxp_bm_show_cursor(softc, ca);
		break;
	case VIS_GET_CURSOR:
		ca->col = FBCONS.cursor.col;
		ca->row = FBCONS.cursor.row;
		break;
	}
}


static void
gfxp_bm_polled_cursor(
	struct vis_polledio_arg *arg,
	struct vis_conscursor *ca)
{
	struct gfxp_softc	*softc = (struct gfxp_softc *)arg;
	uint8_t			prev_state;

	if (force_screen_output) {
		prev_state = FBCONS.active;
		FBCONS.active = 1;
	}

	gfxp_bm_cons_cursor(softc, ca);

	if (force_screen_output)
		FBCONS.active = prev_state;
}

/*
 * VIS_CONSCLEAR handler for non-polled I/O.
 * Clears the content of the screen.
 */
static int
gfxp_bm_cons_clear(struct gfxp_softc *softc, struct vis_consclear *cls)
{
	/*
	 * Clear the shadow buffer. We ignore the background color value since,
	 * at the moment, is statically declared as black.
	 */
	(void) memset(SHADOW_FB.map, '\0', SHADOW_FB.size);

	/* Writing to the frame buffer is disabled, exit here. */
	if (FBCONS.active == 0)
		return (0);

	/*
	 * Check if the driver provided an accelerated way to clear the
	 * screen. Fallback to bcopy if not (or if it failed).
	 */
	if (FBCONS.fbops == NULL || FBCONS.fbops->clear == NULL ||
	    FBCONS.fbops->clear(cls) != GFXP_SUCCESS) {
		/*
		 * Clear the gfx frame buffer. The shadow buffer is assumed
		 * equal to or bigger than the frame buffer.
		 */
		bcopy(SHADOW_FB.map, FBCONS.fb_map, FBCONS.memsize);
	}
	/* Flush to memory. */
	membar_producer();
	return (0);
}

/*
 * Splash shutdown thread.
 */
static void
gfxp_bm_splash_shutdown(struct gfxp_softc *softc)
{
	if (splash_shutdown_start() < 0) {
		FBCONS.going_down = 0;
		gfxp_bm_kdsettext(softc);
	} else {
		/* The image is up, prevent writing on the screen. */
		FBCONS.active = 0;
		FBCONS.splash_bg_loaded = B_TRUE;
	}

	/*
	 * At this point either the image is correctly displayed on the
	 * screen or we encountered some fatal error. In any case, signal to
	 * the ioctl parsing code to go on (if it's still waiting on us).
	 */
	mutex_enter(&FBCONS.splash_bg_mu);
	FBCONS.splash_bg_loading = B_FALSE;
	cv_signal(&FBCONS.splash_bg_cv);
	mutex_exit(&FBCONS.splash_bg_mu);

	/* Error case: nothing else to do */
	if (FBCONS.active == 1)
		thread_exit();

	while (!FBCONS.splash_steps_done) {
		mutex_enter(&FBCONS.splash_step_mu);

		while (!FBCONS.do_splash_step)
			cv_wait(&FBCONS.splash_step_cv, &FBCONS.splash_step_mu);

		FBCONS.do_splash_step = 0;
		mutex_exit(&FBCONS.splash_step_mu);

		splash_shutdown_update();
	}

	splash_shutdown_last();

	splash_stop();
	thread_exit();
}

static void
gfxp_bm_shutdown_end(struct gfxp_softc *softc)
{
	mutex_enter(&FBCONS.splash_step_mu);
	FBCONS.splash_steps_done = B_TRUE;
	FBCONS.do_splash_step = 1;
	cv_signal(&FBCONS.splash_step_cv);
	mutex_exit(&FBCONS.splash_step_mu);

	if (FBCONS.splash_tid->t_tid != NULL)
		thread_join(FBCONS.splash_tid->t_tid);
}

/*
 * Load the shutdown image.
 */
static void
gfxp_bm_load_sd_image(struct gfxp_softc *softc)
{
	clock_t			cur_ticks, to_wait;
	extern pri_t 		minclsyspri;

	FBCONS.splash_bg_loading = B_TRUE;

	/* Kick-off the image loading thread. */
	FBCONS.splash_tid = thread_create(NULL, 0, gfxp_bm_splash_shutdown,
	    softc, 0, &p0, TS_RUN, minclsyspri);

	mutex_enter(&FBCONS.splash_bg_mu);
	if (FBCONS.splash_bg_loading) {
		cur_ticks = ddi_get_lbolt();
		/* Give the thread one second. */
		to_wait = cur_ticks + drv_usectohz(1000000);
		(void) cv_timedwait(&FBCONS.splash_bg_cv, &FBCONS.splash_bg_mu,
		    to_wait);
	}
	mutex_exit(&FBCONS.splash_bg_mu);
}

/*
 * gfxp_bm_vbios_setmode.
 *
 * Sends a request to the vbios component to set a specific mode. And returns
 * 0 on success and -1 on error. If err != NULL, it stores there the reason
 * of the error. Positive values corresponds to errno values returned by
 * the vbios component, negative values indicate 'special' errors:
 * -1 generic error that didn't lead to the execution of the vbios call
 * -2 the vbios request timeouted (note that the underlying BIOS call may or
 *    may not have been executed even if it is likely that it didn't).
 */
static int
gfxp_bm_vbios_setmode(struct gfxp_softc *softc, int *err)
{
	uint8_t			type = FBCONS.type;
	vbios_cmd_reply_t	*reply, saved_reply;
	int			ret = -1;

	if (type == GFXP_VESA) {
		vbios_mode_t	mode;
		vbios_cmd_req_t	v_cmd;

		mode.vesa.val = FBCONS.gfx_exinfo.vesa.mode;
		mode.vesa.flags = VBIOS_VESASET_LINFB;
		v_cmd.cmd = VBIOS_CMD_SETMODE;
		v_cmd.type = VBIOS_VESA_CALL;
		v_cmd.args.mode = mode;

		reply = vbios_exec_cmd(&v_cmd);

		if (reply == NULL)
			goto out_err;

		saved_reply = *reply;
		/*
		 * We do not expect any output back so we can clear the
		 * reply for everybody.
		 */
		vbios_free_reply(reply);

		if (saved_reply.call_ret == VBIOS_SUCCESS) {
			/* We don't expect any output back. */
			return (0);
		} else if (saved_reply.call_ret == VBIOS_STUCK) {
			ret = -2;
			goto out_err;
		} else {
			ret = saved_reply.call_errtype;
			goto out_err;
		}
	}
out_err:
	if (err != NULL)
		*err = ret;
	return (-1);
}

/*
 * KDSETMODE(KD_TEXT) ioctl handler.
 * This ioctl is performed by the X Server to inform the coherent console that
 * it is relinquishing the control over the screen. If this happens as a
 * consequence of the shutdown process, then load the shutdown image.
 */
static void
gfxp_bm_kdsettext(struct gfxp_softc *softc)
{

	if (FBCONS.going_down) {
		/* Push the image on the screen. */
		gfxp_bm_load_sd_image(softc);
		/*
		 * If we loaded the image correctly, then stop the writing to
		 * the screen, otherwise, just fall back to resetting the
		 * previous contents normally. The idea here is that displaying
		 * the saved text on the screen is a lot better than leaving
		 * blank screen.
		 */
		if (FBCONS.splash_bg_loaded) {
			/* Image loaded successfully. */
			FBCONS.active = 0;
			return;
		}
	}
	/* Restore the screen with the contents of the shadow buffer. */
	(void) memcpy(FBCONS.fb_map, SHADOW_FB.map, FBCONS.memsize);
	FBCONS.active = 1;
}

/*
 * KDSETMODE(KD_GRAPHICS) ioctl handler.
 * This ioctl is performed by the X Server to inform the coherent console that
 * it is taking control over the screen. We have little to do here: just shut
 * down the output to the frame buffer. First, though, we check if any boot
 * time animation thread is up and stop it.
 */
static void
gfxp_bm_kdsetgraphics(struct gfxp_softc *softc)
{
	splash_stop();
	/* Make blt/copy only update the shadow buffer. */
	FBCONS.active = 0;
}

/*
 * KDSETMODE ioctl generic entry point.
 */
static int
gfxp_bm_kdsetmode(struct gfxp_softc *softc, int mode)
{
	if (mode == softc->mode)
		return (0);

	switch (mode) {
	case KD_RESUME:
		/*
		 * This is a special state that indicates to the gfx driver that
		 * the userland processes have resumed. Since the current gfx
		 * drivers leave the card in VGA TEXT mode upon resume we
		 * need to reset the correct VESA mode. We do so only if the
		 * X server is not running, in which case we behave like a
		 * 'reset' of KD_TEXT.
		 */
		if (softc->mode == KD_GRAPHICS)
			return (0);

		mode = KD_TEXT;
	/*FALLTHRU*/
	case KD_TEXT:
		/*
		 * Give the driver a chance to record or handle the event.
		 * If the driver does not implement a setmode operation or if
		 * it fails, fallback into emulating a BIOS call.
		 * Drivers *should not* avoid restoring the mode even if already
		 * in KD_TEXT: we have already caught this case before and if
		 * we get there is really only because of a KD_RESUME.
		 */
		if (FBCONS.fbops == NULL || FBCONS.fbops->setmode == NULL ||
		    FBCONS.fbops->setmode(mode) != GFXP_SUCCESS) {
			int	err = 0;
			int	i = 0;

			/* Retry only if interrupted. */
			for (i = 0; i < 5; i++) {
				if (gfxp_bm_vbios_setmode(softc, &err) != 0 &&
				    err == EINTR) {
					/* Wait a little. */
					delay(10);
				} else {
					/* Get out. */
					break;
				}
			}
		}

		gfxp_bm_kdsettext(softc);
		break;

	case KD_GRAPHICS:
		/* Give the driver a chance to record/handle the event. */
		if (FBCONS.fbops != NULL && FBCONS.fbops->setmode != NULL)
			(void) FBCONS.fbops->setmode(mode);

		/*
		 * The reason why we call the driver specific routine, but we
		 * do not call any vbios one is that Xorg will manipulate the
		 * card and change the state, so we just need to acknowledge
		 * the fact here.
		 * The driver, though, needs to know that the transition
		 * happened to successfully restore the state back on a
		 * future setmode() call.
		 */
		gfxp_bm_kdsetgraphics(softc);
		break;

	case KD_RESETTEXT:
		/*
		 * In order to avoid racing with a starting X server,
		 * this needs to be a test and set that is performed in
		 * a single (softc->lock protected) ioctl into this driver.
		 */
		if (softc->mode == KD_TEXT && FBCONS.active == 0) {
			splash_stop();
			FBCONS.splash_bg_loaded = B_FALSE;
			FBCONS.going_down = 0;
			gfxp_bm_kdsettext(softc);
			mode = KD_TEXT;
		}
		return (0);

	case KD_IGNORE_EARLYRESET:
		/*
		 * To achive a smoother transition from the boot animation
		 * to the X environment, ignore any cnread()/cnpoll() until
		 * GDM starts and only depend on an explicit KD_RESETTEXT
		 * to reset the console to text mode.
		 */
		check_reset_console = B_FALSE;
		return (0);

	case KD_SHUTDOWN_START:
	case KD_SHUTDOWN_STEP:
	case KD_SHUTDOWN_LAST:
		/*
		 * Those should really live inside a separate ioctl command,
		 * but until the nvidia driver learns about it, we need to
		 * make them slip through KDSETMODE.
		 */
		return (gfxp_bm_kdshutdown(softc, mode));
	default:
		return (EINVAL);
	}

	softc->mode = mode;
	return (0);
}

/*
 * KDSHUTDOWN ioctl entry point.
 */
static int
gfxp_bm_kdshutdown(struct gfxp_softc *softc, int mode)
{
	if (mode > KDS_MAX || mode < 0)
		return (EINVAL);

	switch (mode) {
	case KDS_START:
		/*
		 * Splash shutdown kick-off. If we booted with graphics on
		 * and if we are coming out off the graphical environment,
		 * then start the thread responsible of the shutdown
		 * animation.
		 */
		if (FBCONS.going_down != 0)
			return (0);

		if (console != CONS_SCREEN_GRAPHICS)
			return (1);

		if (softc->mode != KD_GRAPHICS)
			return (2);

		/*
		 * Prevent further trips down here and notify
		 * gfxp_bm_kdsettext() that there is work to do.
		 */
		FBCONS.going_down = 1;
		break;
	case KDS_STEP:
		/*
		 * Update the shutdown bar each time userland reaches a given
		 * point in the process (really, each times userland tells us).
		 */
		if (!FBCONS.splash_bg_loaded)
			return (0);

		/* Tell the shutdown splash thread to update the bar. */
		mutex_enter(&FBCONS.splash_step_mu);
		FBCONS.do_splash_step = 1;
		cv_signal(&FBCONS.splash_step_cv);
		mutex_exit(&FBCONS.splash_step_mu);

		break;
	case KDS_LAST:
		/*
		 * Display the last entry in the shutdown animation.
		 */
		if (!FBCONS.splash_bg_loaded || FBCONS.splash_steps_done)
			return (0);

		gfxp_bm_shutdown_end(softc);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/* Suspend and resume. */
int
gfxp_bm_suspend(struct gfxp_softc *softc)
{
	switch (softc->mode) {
	/*
	 * The shadow buffer is always in sync with the contents on the
	 * screen, so we do not need to save them (as instead we have to
	 * do in VGA TEXT mode).
	 */
	case KD_TEXT:
	case KD_GRAPHICS:
		/*
		 * This is terrible.
		 *
		 * We clear the first 128K of the linear framebuffer. This
		 * should clear the 'vga memory range' of the card once it is
		 * set back to TEXT mode upon resume and give a cleaner 'output'
		 * for drivers that fail to clear the screen during their
		 * resume process.
		 */
		(void) memset(FBCONS.fb_map, (int)0x0, 1 << 17);
		break;

	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

void
gfxp_bm_resume(struct gfxp_softc *softc)
{
	switch (softc->mode) {
	case KD_TEXT:
	case KD_GRAPHICS:
	default:

		break;
	}
}

/*ARGSUSED*/
static int
gfxp_bm_devmap(struct gfxp_softc *softc, dev_t dev, devmap_cookie_t dhp,
	offset_t off, size_t len, size_t *maplen, uint_t model)
{
	size_t 	length;

	if (off < 0 || off >= FBCONS.memsize) {
		cmn_err(CE_WARN, "%s: Can't map offset 0x%llx", MYNAME, off);
		return (ENXIO);
	}

	if (off + len > FBCONS.memsize)
		length = FBCONS.memsize - off;
	else
		length = len;

	gfxp_map_devmem(dhp, FBCONS.fb_phys_addr, length, &dev_attr);

	*maplen = length;
	return (0);
}
