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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ast.h"
#include <sys/types.h>
#include <sys/ddi.h>

#if VIS_CONS_REV > 2

static void ast_termemu_display(struct ast_softc *, struct ast_vis_draw_data *);
extern void ast_termemu_copy(struct ast_softc *, struct vis_conscopy *);
static void ast_termemu_cursor(struct ast_softc *, struct vis_conscursor *);
static int  ast_invalidate_userctx(struct ast_softc *);
static int  ast_chk_disp_params(struct ast_softc *, struct vis_consdisplay *);
static int  ast_chk_copy_params(struct ast_softc *, struct vis_conscopy *);
static int  ast_chk_cursor_params(struct ast_softc *, struct vis_conscursor *);
static void ast_polled_check_power(struct ast_softc *);
static void ast_polled_consdisplay(struct vis_polledio_arg *,
    struct vis_consdisplay *);
static void ast_polled_conscopy(struct vis_polledio_arg *,
    struct vis_conscopy *);
static void ast_polled_conscursor(struct vis_polledio_arg *,
    struct vis_conscursor *);

static void ast_polledio_enter(void);
static void ast_polledio_exit(void);

static void ast_restore_cmap(struct ast_softc *);
extern void ast_getsize(struct ast_softc *);


#define	DFB32ADR(softc, _row, _col) \
	(uint32_t *)(((uint32_t *)(softc->consinfo.dfb)) +	\
    softc->consinfo.pitch * (_row) + (_col))

#define	DFB8ADR(softc, _row, _col) \
	(uint8_t *)(((uint8_t *)(softc->consinfo.dfb)) +	\
    softc->consinfo.pitch * (_row) + (_col))

#ifdef AST_DEBUG

void
ast_vis_debug(struct ast_softc *softc, int cmd, int row, int col,
    int width, int height)
{
	struct ast_vis_cmd_buf *cmd_buf;

	if ((softc->consinfo.vis_cmd_buf_idx > 190) ||
	    (softc->consinfo.vis_cmd_buf_idx < 0)) {
		softc->consinfo.vis_cmd_buf_idx = 0;
	}

	cmd_buf =
	    &(softc->consinfo.vis_cmd_buf[softc->consinfo.vis_cmd_buf_idx]);
	cmd_buf->cmd = cmd;
	cmd_buf->row = row;
	cmd_buf->col = col;
	cmd_buf->width = width;
	cmd_buf->height = height;
	cmd_buf->word1 = (unsigned long)(softc->consinfo.dfb);

	cmd_buf++;
	cmd_buf->cmd = -1;
	softc->consinfo.vis_cmd_buf_idx++;

}

/*
 * ast_vis_debug_test
 * -- a debug test function to display a green box on the screen
 */
int
ast_vis_debug_test(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	int i, j;
	int *base;
	int r = 100;
	int c = 100;

	if (softc->consinfo.dfb == NULL) {
		ast_vis_map_dfb(softc);
	}

	for (j = 0; j < 10; j++) {
		base = ((int *)(softc->consinfo.dfb)) +
		    softc->consinfo.pitch * (r+j) + c;

		for (i = 0; i < 20; i++, base++) {
			*base = 0x0000ff00;
		}
	}

	return (0);
}


/*
 * ast_vis_debug_get_buf
 * -- a debug test function to get vis commands from the buffer
 */
int
ast_vis_debug_get_buf(struct ast_softc *softc, dev_t dev, intptr_t arg,
    int mode)
{
	int idx = softc->consinfo.vis_cmd_buf_idx;

	softc->consinfo.vis_cmd_buf[idx].cmd = AST_VIS_DEBUG_END;
	softc->consinfo.vis_cmd_buf[idx].width = softc->displayWidth;
	softc->consinfo.vis_cmd_buf[idx].height = softc->displayHeight;
	softc->consinfo.vis_cmd_buf[idx].row = softc->consinfo.pitch;

	if (ddi_copyout(&(softc->consinfo.vis_cmd_buf[0]), arg,
	    (softc->consinfo.vis_cmd_buf_idx + 1) *
	    sizeof (struct ast_vis_cmd_buf), mode))
		return (EFAULT);

	softc->consinfo.vis_cmd_buf_idx = 0;
	softc->consinfo.vis_cmd_buf[0].cmd = AST_VIS_DEBUG_END;

	return (DDI_SUCCESS);
}


/*
 * ast_vis_debug_get_image
 * -- a debug test function to get vis image
 */
int
ast_vis_debug_get_image(struct ast_softc *softc, dev_t dev, intptr_t arg,
    int mode)
{
	if (ddi_copyout(&(softc->consinfo.bufp[0]), arg,
	    softc->consinfo.bufsize, mode))
		return (EFAULT);

	return (DDI_SUCCESS);
}
#endif /* AST_DEBUG */



extern void
ast_getsize(struct ast_softc *softc)
{
	int width;

	/*
	 * Open Key
	 */
	SetIndexReg(CRTC_PORT, 0x80, 0xA8);

	/*
	 * get the VDE
	 */
	GetIndexRegMask(CRTC_PORT, 0x01, 0xff, width);
	softc->displayWidth = ((int)(width + 1)) << 3;

	switch (softc->displayWidth) {
	case 1920:
		softc->displayHeight = 1200;
		break;

	case 1600:
		softc->displayHeight = 1200;
		break;

	case 1280:
		softc->displayHeight = 1024;
		break;

	case 1024:
		softc->displayHeight = 768;
		break;

	case 800:
		softc->displayHeight = 600;
		break;

	case 640:
	default:
		softc->displayHeight = 480;
		break;
	}


#ifdef AST_DEBUG
	printf("w=0x%x width=%d h=0x%x height=%d\n",
	    width, softc->displayWidth, height, softc->displayHeight);
#endif /* AST_DEBUG */

	softc->consinfo.pitch  = softc->displayWidth;
	softc->consinfo.rshift = 16;
	softc->consinfo.gshift = 8;
	softc->consinfo.bshift = 0;
}


int
ast_vis_map_dfb(struct ast_softc *softc)
{
	softc->consinfo.dfb = softc->fbbase;

	if (softc->consinfo.dfb == NULL)
		return (DDI_FAILURE);

	return (DDI_SUCCESS);
}

int
ast_vis_devinit(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct vis_devinit devinit;

	if (!(mode & FKIOCTL)) {
		return (EPERM);
	}

	/*
	 * Read the terminal emulator's change mode callback
	 * address out of the incoming structure
	 */
	if (ddi_copyin((void *)arg, &devinit, sizeof (struct vis_devinit),
	    mode)) {
		return (EFAULT);
	}

	mutex_enter(&softc->lock);

	/*
	 * Allocate memory needed by the kernel console
	 */

	if (softc->consinfo.bufp == NULL) {
		softc->consinfo.bufsize = DEFCHAR_SIZE * AST_MAX_PIXBYTES;
		softc->consinfo.bufp =
		    kmem_zalloc(softc->consinfo.bufsize, KM_SLEEP);
	}

	if (softc->consinfo.polledio == NULL) {
		softc->consinfo.polledio =
		    kmem_zalloc(sizeof (struct vis_polledio), KM_SLEEP);
		softc->consinfo.polledio->arg =
		    (struct vis_polledio_arg *)softc;
	}

	if (softc->consinfo.csrp == NULL) {
		softc->consinfo.csrp =
		    kmem_alloc(CSRMAX * sizeof (uint32_t), KM_SLEEP);
	}
	softc->consinfo.csr_x = -1;
	softc->consinfo.csr_y = -1;

	/*
	 * By default, console messages are black on white in 24 bits
	 * Set this bit to specify white on black message
	 */
	softc->consinfo.flags = AST_VIS_COLOR_INVERSE;

	/*
	 * Extract the terminal emulator's video mode change notification
	 * callback information from the incoming struct
	 */
	softc->consinfo.te_modechg_cb = devinit.modechg_cb;
	softc->consinfo.te_ctx = devinit.modechg_arg;

	ast_getsize(softc);

	/*
	 * Describe this driver's configuration for the caller
	 */
	devinit.version		= VIS_CONS_REV;
	devinit.mode		= VIS_PIXEL;
	devinit.polledio	= softc->consinfo.polledio;
	devinit.width		= softc->displayWidth;
	devinit.height		= softc->displayHeight;
	devinit.depth		= softc->displayDepth;
	devinit.linebytes	= devinit.width * devinit.depth / 8;

	/*
	 * Setup the standalone access (polled mode) entry points
	 * which are also passed back to the terminal emulator
	 */
	softc->consinfo.polledio->display = ast_polled_consdisplay;
	softc->consinfo.polledio->copy	  = ast_polled_conscopy;
	softc->consinfo.polledio->cursor  = ast_polled_conscursor;

	/*
	 * Get our view of the console as scribbling pad of memory.
	 * (dumb framebuffer)
	 */
	if (ast_vis_map_dfb(softc) != DDI_SUCCESS) {
		mutex_exit(&softc->lock);
		return (ENOMEM);
	}

#ifdef AST_DEBUG
	/*
	 * put the ok command in the vis command debug buffer to mark
	 * coherent console is good to go
	 */
	softc->consinfo.vis_cmd_buf[0].cmd	= AST_VIS_DEBUG_INIT;
	softc->consinfo.vis_cmd_buf[0].width	= softc->displayWidth;
	softc->consinfo.vis_cmd_buf[0].height	= softc->displayHeight;
	softc->consinfo.vis_cmd_buf[0].row	= softc->consinfo.pitch;
	softc->consinfo.vis_cmd_buf[0].word1	= (unsigned long)
	    softc->consinfo.te_modechg_cb;
	softc->consinfo.vis_cmd_buf_idx		= 1;
#endif /* AST_DEBUG */

	mutex_exit(&softc->lock);

	/*
	 * Send framebuffer kernel console rendering parameters back to the
	 * terminal emulator
	 */
	if (ddi_copyout(&devinit, (void *) arg, sizeof (struct vis_devinit),
	    mode))
		return (EFAULT);

	return (DDI_SUCCESS);
}

int
ast_vis_devfini(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	if (!(mode & FKIOCTL))
		return (EPERM);

	mutex_enter(&softc->lock);

	softc->consinfo.polledio->display = NULL;
	softc->consinfo.polledio->copy	  = NULL;
	softc->consinfo.polledio->cursor  = NULL;
	softc->consinfo.te_modechg_cb	  = NULL;
	softc->consinfo.te_ctx		  = NULL;

	if (softc->consinfo.polledio != NULL) {
		kmem_free(softc->consinfo.polledio,
		    sizeof (struct vis_polledio));
		softc->consinfo.polledio = NULL;
	}

	if (softc->consinfo.bufp != NULL) {
		kmem_free(softc->consinfo.bufp, softc->consinfo.bufsize);
		softc->consinfo.bufsize = 0;
		softc->consinfo.bufp	= NULL;
	}

	if (softc->consinfo.csrp != NULL) {
		kmem_free(softc->consinfo.csrp, CSRMAX);
		softc->consinfo.csrp	= NULL;
	}

	mutex_exit(&softc->lock);

	return (DDI_SUCCESS);
}

extern void
ast_vis_termemu_callback(struct ast_softc *softc)
{
	struct vis_devinit devinit;

	mutex_enter(&softc->lock);

	ast_getsize(softc);

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_TERMCALLBACK, softc->consinfo.pitch,
	    0, softc->displayWidth, softc->displayHeight);
#endif /* AST_DEBUG */

	mutex_exit(&softc->lock);

	if (softc->consinfo.te_modechg_cb != NULL) {

		devinit.version		= VIS_CONS_REV;
		devinit.mode		= VIS_PIXEL;
		devinit.polledio	= softc->consinfo.polledio;
		devinit.width		= softc->displayWidth;
		devinit.height		= softc->displayHeight;
		devinit.depth		= softc->displayDepth;
		devinit.linebytes	= devinit.width * devinit.depth / 8;

		softc->consinfo.te_modechg_cb(softc->consinfo.te_ctx, &devinit);
	}
}

int
ast_vis_consdisplay(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct vis_consdisplay		consdis;
	struct ast_vis_draw_data	ast_draw;
	int				image_size;

	if (!(mode & FKIOCTL))
		return (EPERM);

	if (ddi_copyin((void *) arg, &consdis, sizeof (struct vis_consdisplay),
	    mode))
		return (EFAULT);

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_CONSDISPLAY,
	    consdis.row, consdis.col, consdis.width, consdis.height);
#endif /* AST_DEBUG */


	image_size = consdis.width * consdis.height * AST_MAX_PIXBYTES;

	mutex_enter(&softc->lock);

	if (image_size > softc->consinfo.bufsize) {
		void *tmp = softc->consinfo.bufp;
		if (!(softc->consinfo.bufp =
		    kmem_alloc(image_size, KM_SLEEP))) {
			softc->consinfo.bufp = tmp;
			mutex_exit(&softc->lock);
			return (ENOMEM);
		}
		if (tmp)
			kmem_free(tmp, softc->consinfo.bufsize);
		softc->consinfo.bufsize = image_size;
	}

	mutex_exit(&softc->lock);

	if (ddi_copyin(consdis.data, softc->consinfo.bufp, image_size, mode))
		return (EFAULT);

	mutex_enter(&softc->lock);

	if (ast_chk_disp_params(softc, &consdis) != DDI_SUCCESS)  {
		mutex_exit(&softc->lock);
		return (EINVAL);
	}

	ast_draw.image_row	= consdis.row;
	ast_draw.image_col	= consdis.col;
	ast_draw.image_width	= consdis.width;
	ast_draw.image_height	= consdis.height;
	ast_draw.image		= softc->consinfo.bufp;

	if (ast_invalidate_userctx(softc) == DDI_SUCCESS) {
		ast_termemu_display(softc, &ast_draw);
	}

	mutex_exit(&softc->lock);

	return (DDI_SUCCESS);
}

int
ast_vis_conscursor(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct vis_conscursor data;

	if (!(mode & FKIOCTL))
		return (EPERM);

	if (ddi_copyin((void *)arg, &data, sizeof (struct vis_conscursor),
	    mode))
		return (EFAULT);

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_CONSCURSOR,
	    data.row, data.col, data.width, data.height);
#endif /* AST_DEBUG */

	mutex_enter(&softc->lock);

	if (ast_chk_cursor_params(softc, &data) != DDI_SUCCESS) {
		mutex_exit(&softc->lock);
		return (EINVAL);
	}

	if (ast_invalidate_userctx(softc) == DDI_SUCCESS)
		ast_termemu_cursor(softc, &data);

	mutex_exit(&softc->lock);

	return (DDI_SUCCESS);
}

int
ast_vis_conscopy(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct vis_conscopy data;

	if (!(mode & FKIOCTL))
		return (EPERM);

	if (ddi_copyin((void *)arg, &data, sizeof (struct vis_conscopy), mode))
		return (EFAULT);

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_CONSCOPY,
	    data.s_row, data.s_col, data.e_row, data.e_col);
#endif /* AST_DEBUG */

	mutex_enter(&softc->lock);

	if (ast_chk_copy_params(softc, &data) != DDI_SUCCESS) {
		mutex_exit(&softc->lock);
		return (EINVAL);
	}

	if (ast_invalidate_userctx(softc) == DDI_SUCCESS)
		ast_termemu_copy(softc, &data);

	mutex_exit(&softc->lock);

	return (DDI_SUCCESS);
}

int
ast_vis_putcmap(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct vis_cmap *cmap = (struct vis_cmap *)arg;
	int i, index;

	if (!(mode & FKIOCTL))
		return (EPERM);

	index = cmap->index;
	for (i = 0; i < cmap->count; i++, index++) {
		softc->consinfo.kcmap[0][index] =
		    cmap->red[i] << 2 | cmap->red[i] >> 6;

		softc->consinfo.kcmap[1][index] =
		    cmap->green[i] << 2 | cmap->green[i] >> 6;

		softc->consinfo.kcmap[2][index] =
		    cmap->blue[i] << 2 | cmap->blue[i] >> 6;

		softc->consinfo.kcmap[3][index] = 1;

		if (index > softc->consinfo.kcmap_max)
			softc->consinfo.kcmap_max = (uint8_t)index;
	}

	return (DDI_SUCCESS);
}


/*
 * Polled I/O Entry Points. -----------------------------------------
 *
 * The tactics in these routines are based on the fact that we are
 * -only- called in standalone mode.  Therefore time is frozen
 * for us.  We sneak in restore the kernel's colormap, establish
 * the kernel's draw engine context, render, and then replace the
 * previous context -- no one the wiser for it.
 *
 * In polled I/O mode (also called standalone mode), the kernel isn't
 * running, Only one CPU is enabled, system services are not running,
 * and all access is single-threaded.  The limitations of standalone
 * mode are:  (1) The driver cannot wait for interrupts, (2) The driver
 * cannot use mutexes, (3) The driver cannot allocate memory.
 *
 * The advantage of polled I/O mode is, that because we don't have to
 * worry about concurrent access to device state, we don't need to
 * unload mappings and can perform a lighter form of graphics context
 * switching, which doesn't require the use of mutexes.
 *
 */


/*
 * Setup for DFB rectangle BLIT on a "quiesced" system
 */
static void
ast_polled_consdisplay(struct vis_polledio_arg *arg,
    struct vis_consdisplay *data)
{
	struct ast_softc *softc = (struct ast_softc *)arg;
	struct ast_vis_draw_data ast_draw;

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_POLLDISPLAY,
	    data->row, data->col, data->width, data->height);
#endif /* AST_DEBUG */

	if (ast_chk_disp_params(softc, data) != DDI_SUCCESS)  {
		return;
	}

	ast_draw.image_row	= data->row;
	ast_draw.image_col	= data->col;
	ast_draw.image_width	= data->width;
	ast_draw.image_height	= data->height;
	ast_draw.image		= data->data;

	ast_polledio_enter();
	ast_polled_check_power(softc);
	ast_termemu_display(softc, &ast_draw);
	ast_polledio_exit();
}

/*
 * Set up for DFB rectangle copy (vertical scroll) on a "quiesced" system
 */
static void
ast_polled_conscopy(struct vis_polledio_arg *arg, struct vis_conscopy *data)
{
	struct ast_softc *softc = (struct ast_softc *)arg;

#ifdef AST_DEBUG
	ast_vis_debug(softc, AST_VIS_DEBUG_POLLCOPY,
	    data->s_row, data->s_col, data->e_row, data->e_col);
#endif /* AST_DEBUG */

	if (ast_chk_copy_params(softc, data) != DDI_SUCCESS) {
		return;
	}

	ast_polledio_enter();
	ast_polled_check_power(softc);
	ast_termemu_copy(softc, data);
	ast_polledio_exit();
}


/*
 * Set up for DFB cursor blit on a "quiesced" system
 */
static void
ast_polled_conscursor(struct vis_polledio_arg *arg, struct vis_conscursor *data)
{
	struct ast_softc *softc = (struct ast_softc *)arg;

	ast_polledio_enter();
	ast_polled_check_power(softc);
	ast_termemu_cursor(softc, data);
	ast_polledio_exit();
}

/*
 * Copy to DFB a rectangular image whose size, coordinates and pixels are
 * defined in the draw struct, The proper pixel conversions are made based on
 * the video mode depth. This operation is implemented in terms of memory copies
 */
static void
ast_termemu_display(struct ast_softc *softc, struct ast_vis_draw_data *draw)
{
	uint8_t		*disp, red, grn, blu;
	int		r, c, y, x, h, w;
	int		rshift, gshift, bshift;

	r = draw->image_row;
	c = draw->image_col;
	h = draw->image_height;
	w = draw->image_width;

#ifdef AST_DEBUG
	ast_vis_debug(softc, 73, r, c, w, h);
#endif /* AST_DEBUG */

	/*
	 * Enable 2D
	 */
	SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x01);

	ast_restore_cmap(softc);

	disp = draw->image;

	if (softc->displayDepth == 32) {

		uint32_t	*ppix;

		/*
		 * only support 32 bits depth
		 */
		rshift = softc->consinfo.rshift;
		gshift = softc->consinfo.gshift;
		bshift = softc->consinfo.bshift;

		if (softc->consinfo.flags & AST_VIS_COLOR_INVERSE) {

			for (y = 0; y < h; y++) {
				ppix = DFB32ADR(softc, r + y, c);
				for (x = 0; x < w; x++, ppix++) {
					/* disp is 00rrggbb */
					disp++;
					red = *disp++;
					grn = *disp++;
					blu = *disp++;
					/* Transform into DFB byte order */
					*ppix = ~((red << rshift) |
					    (grn << gshift) | (blu << bshift));
				}
			}
		} else {
			for (y = 0; y < h; y++) {
				ppix = DFB32ADR(softc, r + y, c);
				for (x = 0; x < w; x++, ppix++) {
					/* disp is 00rrggbb */
					disp++;
					red = *disp++;
					grn = *disp++;
					blu = *disp++;
					/* Transform into DFB byte order */
					*ppix = (red << rshift) |
					    (grn << gshift) | (blu << bshift);
				}
			}
		}
	} else {

		uint8_t		*ppix;

		for (y = 0; y < h; y++) {

			ppix = DFB8ADR(softc, r + y, c);
			for (x = 0; x < w; x++, ppix++) {
				*ppix = *disp++;
			}
		}
	}
}


static void
ast_termemu_cursor(struct ast_softc *softc, struct vis_conscursor *data)
{
	int		x, y;
	int		r, c, w, h;
	uint32_t	idx;

	r = data->row;
	c = data->col;
	w = data->width;
	h = data->height;


#ifdef AST_DEBUG
	ast_vis_debug(softc, 72, r, c, w, h);
#endif /* AST_DEBUG */

	/*
	 * Enable 2D
	 */
	SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x01);

	if ((w * h) > CSRMAX) {
		h = CSRMAX / w;
	}

	if (softc->displayDepth == 32) {

		uint32_t	*pcur;
		uint32_t	*ppix;
		uint32_t	fg, bg;

		pcur = (uint32_t *)(softc->consinfo.csrp);

		/*
		 * Convert fg/bg into DFB order for direct comparability
		 */
		fg = (data->fg_color.twentyfour[0] << softc->consinfo.rshift) |
		    (data->fg_color.twentyfour[1] << softc->consinfo.gshift) |
		    (data->fg_color.twentyfour[2] << softc->consinfo.bshift);

		bg = (data->bg_color.twentyfour[0] << softc->consinfo.rshift) |
		    (data->bg_color.twentyfour[1] << softc->consinfo.gshift) |
		    (data->bg_color.twentyfour[2] << softc->consinfo.bshift);

		if (softc->consinfo.flags & AST_VIS_COLOR_INVERSE) {
			fg = ~fg;
			bg = ~bg;
		}

		if (data->action == SHOW_CURSOR) {
			if (!((softc->consinfo.csr_x == c) &&
			    (softc->consinfo.csr_y == r))) {

				for (y = 0; y < h; y++) {
					ppix = DFB32ADR(softc, r + y, c);
					idx = y * w;
					for (x = 0; x < w; x++, ppix++, idx++) {
						pcur[idx] = *ppix;
						*ppix =
						    ((*ppix == fg) ? bg : fg);
					}
				}
			}
			softc->consinfo.csr_x = c;
			softc->consinfo.csr_y = r;
		} else {
			for (y = 0; y < h; y++) {
				ppix = DFB32ADR(softc, r + y, c);
				idx = y * w;
				for (x = 0; x < w; x++, ppix++, idx++) {
					*ppix = pcur[idx];
				}
			}
			softc->consinfo.csr_x = -1;
			softc->consinfo.csr_y = -1;
		}
	} else {

		uint8_t	*pcur;
		uint8_t	*ppix;
		uint8_t	fg, bg;

		pcur = (uint8_t *)(softc->consinfo.csrp);

		fg = data->fg_color.eight;
		bg = data->bg_color.eight;

		if (data->action == SHOW_CURSOR) {
			if (!((softc->consinfo.csr_x == c) &&
			    (softc->consinfo.csr_y == r))) {

				for (y = 0; y < h; y++) {
					ppix = DFB8ADR(softc, r + y, c);
					idx = y * w;
					for (x = 0; x < w; x++, ppix++, idx++) {
						pcur[idx] = *ppix;
						*ppix =
						    ((*ppix == fg) ? bg : fg);
					}
				}
			}
			softc->consinfo.csr_x = c;
			softc->consinfo.csr_y = r;
		} else {
			for (y = 0; y < h; y++) {
				ppix = DFB8ADR(softc, r + y, c);
				idx = y * w;
				for (x = 0; x < w; x++, ppix++, idx++) {
					*ppix = pcur[idx];
				}
			}
			softc->consinfo.csr_x = -1;
			softc->consinfo.csr_y = -1;
		}
	}
}

void
ast_setup_cmd(struct ast_softc *softc, unsigned int offset, unsigned int val)
{
	unsigned int tmp;
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	unsigned int *addr = (unsigned int *)(softc->regbase + offset);

	do {
		*addr = val;
		tmp = *addr;
	} while (tmp != val);
}

void
ast_wait_eng_idle(struct ast_softc *softc, int MMIOCheck)
{
	uint32_t	ulEngState, ulEngState2;
	uint32_t	ulEngCheckSetting;
	unsigned char	jReg;
#ifdef AST_DEBUG
	unsigned int	*addr;
#endif

	/* MMIO check */
	if (MMIOCheck) {
		ulEngCheckSetting = 0x10000000;
	} else {
		ulEngCheckSetting = 0x80000000;
	}

	/* 2D is disabled if 0xA4 D[0] = 1 */
	GetIndexRegMask(CRTC_PORT, 0xA4, 0x01, jReg);
	if (!jReg)
		return;

	/* 2D is not working if in standard mode */
	GetIndexRegMask(CRTC_PORT, 0xA3, 0x0F, jReg);
	if (!jReg)
		return;

#ifdef AST_DEBUG
	addr = (unsigned int *)(softc->regbase + 0x804C);
#endif

	do {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState  = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState2 = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState2 = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState2 = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState2 = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulEngState2 = (*(volatile unsigned int *)
		    (softc->regbase + 0x804C)) & 0xFFFC0000;
	} while ((ulEngState & ulEngCheckSetting) ||
	    (ulEngState != ulEngState2));
}

void
ast_termemu_copy(struct ast_softc *softc, struct vis_conscopy *data)
{
	uint16_t	srcx	= data->s_col;
	uint16_t	srcy	= data->s_row;
	uint16_t	dstx	= data->t_col;
	uint16_t	dsty	= data->t_row;
	uint16_t	height	= data->e_row - data->s_row + 1;
	uint16_t	width	= data->e_col - data->s_col + 1;
	uint32_t	cmdreg;
	uint32_t	val;
	uint32_t	pitch;
	struct ast_context	ctx;


#ifdef AST_DEBUG
	ast_vis_debug(softc, 74, srcx, srcy, dstx, dsty);
	ast_vis_debug(softc, 75, width, height, 0, 0);
#endif /* AST_DEBUG */

	/*
	 * Save the current setting
	 */
	ast_ctx_save(softc, &ctx);

	/*
	 * Enable 2D
	 */
	SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x01);

	/*
	 * Switch to MMIO
	 */
	softc->write32(softc, CMD_SETTING, 0xF2000000);

	/*
	 * start the bitblt command
	 */

	cmdreg = CMD_BITBLT;

	if (softc->displayDepth == 8) {
		cmdreg |= CMD_COLOR_8;
	} else {
		cmdreg |= CMD_COLOR_32;
	}

	cmdreg |= (ROP_S << 8);

	if (srcy < dsty) {
		dsty += height - 1;
		srcy += height - 1;
		cmdreg |= CMD_Y_DEC;
	}

	if (srcx < dstx) {
		dstx += width - 1;
		srcx += width - 1;
		cmdreg |= CMD_X_DEC;
	}

	/*
	 * set up source pitch
	 */

	pitch = softc->consinfo.pitch * softc->displayDepth / 8;
	val = (pitch << 16);
	softc->write32(softc, SRC_PITCH, val);

	/*
	 * set up destination pitch and height
	 */
	val = (pitch << 16) | (-1 & MASK_DST_HEIGHT);
	softc->write32(softc, DST_PITCH, val);

	/*
	 * set up source base
	 */
	softc->write32(softc, SRC_BASE, 0);

	/*
	 * set up destination base
	 */
	softc->write32(softc, DST_BASE, 0);

	/*
	 * set up source x & y
	 */
	val = ((srcx & MASK_SRC_X) << 16) | (srcy & MASK_SRC_Y);
	softc->write32(softc, SRC_XY, val);

	/*
	 * set up destination x & y
	 */
	val = ((dstx & MASK_DST_X) << 16) | (dsty & MASK_DST_Y);
	softc->write32(softc, DST_XY, val);

	/*
	 * set up rect x & y
	 */
	val = ((width & MASK_RECT_WIDTH) << 16) | (height & MASK_RECT_WIDTH);
	softc->write32(softc, RECT_XY, val);

	/*
	 * set up cmd reg
	 */
	softc->write32(softc, CMD_REG, cmdreg);

	/*
	 * wait for eng go idle
	 */
	ast_wait_eng_idle(softc, TRUE);
	ast_wait_eng_idle(softc, TRUE);

	drv_usecwait(10000);

	ast_ctx_restore(softc, &ctx);
}


static int
ast_invalidate_userctx(struct ast_softc *softc)
{

	if ((softc->flags & AST_HW_INITIALIZED) == 0) {
		return (DDI_FAILURE);
	}

	if (softc->cur_ctx != NULL) {
		ast_wait_eng_idle(softc, FALSE);
		(void) ast_ctx_make_current(softc, NULL);
	}

	return (DDI_SUCCESS);
}

/*
 * Validate the parameters for the data to be displayed on the console.
 */
static int
ast_chk_disp_params(struct ast_softc *softc, struct vis_consdisplay *data)
{
	int d;

	if ((data->row > softc->displayHeight) ||
	    (data->col > softc->displayWidth))
		return (EINVAL);

	if ((data->row + data->height) >= softc->displayHeight) {
		d = data->row + data->height - softc->displayHeight + 1;
		if (d < data->height)
			data->height -= d;
		else
			return (EINVAL);
	}

	if ((data->col + data->width) >= softc->displayWidth) {
		d = data->col + data->width - softc->displayWidth + 1;
		if (d < data->width)
			data->width -= d;
		else
			return (EINVAL);
	}

	return (DDI_SUCCESS);
}

/*
 * Validate the parameters for the data to be displayed on the console.
 *
 * . Verify beginning (X,Y) coords are in the display are of fb
 * . Verify that the character doesn't extend beyond display area
 */
static int
ast_chk_cursor_params(struct ast_softc *softc, struct vis_conscursor *data)
{
	if ((data->row + data->height) >= softc->displayHeight)
		return (EINVAL);

	if ((data->col + data->width) >= softc->displayWidth)
		return (EINVAL);

	return (DDI_SUCCESS);
}


static int
ast_chk_copy_params(struct ast_softc *softc, struct vis_conscopy *data)
{
	int	width, height;

	width = data->e_col - data->s_col;
	height = data->e_row - data->s_row;

	if ((width < 0) || (width > softc->displayWidth) ||
	    (height < 0) || (height > softc->displayHeight))
		return (EINVAL);

	if ((data->t_row + height) >= softc->displayHeight)
		return (EINVAL);

	return (DDI_SUCCESS);
}


static void
ast_polled_check_power(struct ast_softc *softc)
{
}

static void
ast_polledio_enter(void)
{
}

static void
ast_polledio_exit(void)
{
}

static void
ast_restore_cmap(struct ast_softc *softc)
{
#if 0
/*
 * commented out any cmap write here because cmap is already initialized
 * in fcode and di coherent code is not sending down the right values
 */
	ast_cmap_write(softc);
#endif
}

#endif /* VIS_CONS_REV */
