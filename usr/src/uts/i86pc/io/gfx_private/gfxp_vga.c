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

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/vbios.h>

#include "gfx_private.h"
#include "gfxp_fb.h"
#include "gfxp_vga.h"

typedef enum pc_colors {
	pc_black	= 0,
	pc_blue		= 1,
	pc_green	= 2,
	pc_cyan		= 3,
	pc_red		= 4,
	pc_magenta	= 5,
	pc_brown	= 6,
	pc_white	= 7,
	pc_grey		= 8,
	pc_brt_blue	= 9,
	pc_brt_green	= 10,
	pc_brt_cyan	= 11,
	pc_brt_red	= 12,
	pc_brt_magenta	= 13,
	pc_yellow	= 14,
	pc_brt_white	= 15
} pc_colors_t;

static const unsigned char solaris_color_to_pc_color[16] = {
	pc_brt_white,		/*  0 - brt_white	*/
	pc_black,		/*  1 - black		*/
	pc_blue,		/*  2 - blue		*/
	pc_green,		/*  3 - green		*/
	pc_cyan,		/*  4 - cyan		*/
	pc_red,			/*  5 - red		*/
	pc_magenta,		/*  6 - magenta		*/
	pc_brown,		/*  7 - brown		*/
	pc_white,		/*  8 - white		*/
	pc_grey,		/*  9 - gery		*/
	pc_brt_blue,		/* 10 - brt_blue	*/
	pc_brt_green,		/* 11 - brt_green	*/
	pc_brt_cyan,		/* 12 - brt_cyan	*/
	pc_brt_red,		/* 13 - brt_red		*/
	pc_brt_magenta,		/* 14 - brt_magenta	*/
	pc_yellow		/* 15 - yellow		*/
};

/* default structure for FBIOGATTR ioctl in VGA TEXT mode. */
static struct fbgattr gfxp_vga_attr =  {
/*	real_type	owner */
	FBTYPE_SUNFAST_COLOR, 0,
/* fbtype: type		h  w  depth cms  size */
	{ FBTYPE_SUNFAST_COLOR, TEXT_ROWS, TEXT_COLS, 1,    256,  0 },
/* fbsattr: flags emu_type	dev_specific */
	{ 0, FBTYPE_SUN4COLOR, { 0 } },
/*	emu_types */
	{ -1 }
};

/* By default, use userland VBIOS emulation. */
int	gfxp_vga_emulate_vbios = 1;


static int gfxp_vga_devinit(struct gfxp_softc *, struct vis_devinit *data);
static void	gfxp_vga_cons_copy(struct gfxp_softc *,
			struct vis_conscopy *);
static void	gfxp_vga_cons_display(struct gfxp_softc *,
			struct vis_consdisplay *);
static void	gfxp_vga_cons_cursor(struct gfxp_softc *,
			struct vis_conscursor *);
static int	gfxp_vga_cons_clear(struct gfxp_softc *,
			struct vis_consclear *);
static void	gfxp_vga_polled_copy(struct vis_polledio_arg *,
			struct vis_conscopy *);
static void	gfxp_vga_polled_display(struct vis_polledio_arg *,
			struct vis_consdisplay *);
static void	gfxp_vga_polled_cursor(struct vis_polledio_arg *,
			struct vis_conscursor *);
static void	gfxp_vga_init(struct gfxp_softc *);
static void	gfxp_vga_set_text(struct gfxp_softc *);

static void	gfxp_vga_save_text(struct gfxp_softc *softc);
static void	gfxp_vga_restore_textmode(struct gfxp_softc *softc);

#if	defined(USE_BORDERS)
static void	gfxp_vga_init_graphics(struct gfxp_softc *);
#endif

static int gfxp_vga_kdsetmode(struct gfxp_softc *softc, int mode);
static void gfxp_vga_setfont(struct gfxp_softc *softc);
static void gfxp_vga_get_cursor(struct gfxp_softc *softc,
		screen_pos_t *row, screen_pos_t *col);
static void gfxp_vga_set_cursor(struct gfxp_softc *softc, int row, int col);
static void gfxp_vga_hide_cursor(struct gfxp_softc *softc);
static void gfxp_vga_save_colormap(struct gfxp_softc *softc);
static void gfxp_vga_restore_colormap(struct gfxp_softc *softc);
static int gfxp_vga_get_pci_reg_index(dev_info_t *const devi,
		unsigned long himask, unsigned long hival, unsigned long addr,
		off_t *offset);
static int gfxp_vga_get_isa_reg_index(dev_info_t *const devi,
		unsigned long hival, unsigned long addr, off_t *offset);
static int gfxp_vga_suspend(struct gfxp_softc *softc);
static void gfxp_vga_resume(struct gfxp_softc *softc);
static int gfxp_vga_devmap(struct gfxp_softc *, dev_t, devmap_cookie_t,
		offset_t, size_t, size_t *, uint_t);

static struct gfxp_ops vga_ops = {
	gfxp_vga_kdsetmode,	/* kdsetmode 	*/
	gfxp_vga_devinit,	/* devinit 	*/
	gfxp_vga_cons_copy,	/* conscopy 	*/
	gfxp_vga_cons_display,	/* consdisplay 	*/
	gfxp_vga_cons_cursor,	/* conscursor 	*/
	gfxp_vga_cons_clear,	/* consclear	*/
	gfxp_vga_suspend,	/* suspend 	*/
	gfxp_vga_resume,	/* resume	*/
	gfxp_vga_devmap		/* devmap	*/
};

#define	VGACONS		(softc->console.vga)

/*
 * This gfxp_vga function is used to return the fb, and reg pointers
 * and handles for peer graphics drivers.
 */
void
gfxp_vga_get_mapped_ioregs(struct gfxp_softc *softc, struct vgaregmap *regss)
{
	if (regss != NULL) {
		regss->addr	= VGACONS.regs.addr;
		regss->handle	= VGACONS.regs.handle;
		regss->mapped	= VGACONS.regs.mapped;
	}
}

int
gfxp_vga_map_reg(dev_info_t *devi, uint8_t type, int size,
    struct vgaregmap *reg, int *regnum)
{
	int		reg_rnumber = -1;
	off_t		reg_offset;
	int		ret;
	char		*parent_type = NULL;
	unsigned long	addr;
	unsigned long	hival = PCI_RELOCAT_B;

	if (type != VGA_REG_IO && type != VGA_REG_MEM) {
		cmn_err(CE_WARN, MYNAME ": wrong reg mapping requested");
		ret = DDI_FAILURE;
		goto fail;
	}

	ret = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_get_parent(devi),
	    DDI_PROP_DONTPASS, "device_type", &parent_type);
	if (ret != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, MYNAME ": can't determine parent type.");
		ret = DDI_FAILURE;
		goto fail;
	}

	if (type == VGA_REG_MEM) {
		addr = VGA_MEM_ADDR;
		hival |= PCI_ADDR_MEM32;
	} else {
		addr = VGA_REG_ADDR;
		hival |= PCI_ADDR_IO;
	}

	if (STREQ(parent_type, "isa") || STREQ(parent_type, "eisa")) {
		reg_rnumber = gfxp_vga_get_isa_reg_index(devi, type, addr,
		    &reg_offset);
	} else if (STREQ(parent_type, "pci") || STREQ(parent_type, "pciex")) {
		reg_rnumber = gfxp_vga_get_pci_reg_index(devi,
		    PCI_REG_ADDR_M|PCI_REG_REL_M, hival, addr, &reg_offset);
	}

	if (reg_rnumber < 0) {
		cmn_err(CE_WARN, MYNAME ": can't find reg entry for registers");
		ret = DDI_FAILURE;
		goto fail;
	}

	ret = ddi_regs_map_setup(devi, reg_rnumber, (caddr_t *)&reg->addr,
	    reg_offset, size, &dev_attr, &reg->handle);

	if (ret != DDI_SUCCESS)
		goto fail;

	reg->mapped = B_TRUE;
	if (regnum)
		*regnum = reg_rnumber;

	ret = DDI_SUCCESS;
fail:
	if (parent_type != NULL)
		ddi_prop_free(parent_type);
	return (ret);
}


int
gfxp_vga_attach(dev_info_t *devi, struct gfxp_softc *softc)
{
	int	error;

	/* setup polled I/O routines */
	softc->polledio.display = gfxp_vga_polled_display;
	softc->polledio.copy = gfxp_vga_polled_copy;
	softc->polledio.cursor = gfxp_vga_polled_cursor;

	softc->ops = &vga_ops;

	error = gfxp_vga_map_reg(devi, VGA_REG_IO, VGA_REG_SIZE, &VGACONS.regs,
	    NULL);
	if (error != DDI_SUCCESS)
		return (error);

	VGACONS.fb_size = VGA_MEM_SIZE;

	error = gfxp_vga_map_reg(devi, VGA_REG_MEM, VGACONS.fb_size,
	    &VGACONS.fb, &VGACONS.fb_regno);

	if (error != DDI_SUCCESS)
		return (error);

	if (ddi_get8(VGACONS.regs.handle,
	    VGACONS.regs.addr + VGA_MISC_R) & VGA_MISC_IOA_SEL)
		VGACONS.text_base = (caddr_t)VGACONS.fb.addr + VGA_COLOR_BASE;
	else
		VGACONS.text_base = (caddr_t)VGACONS.fb.addr + VGA_MONO_BASE;

	/* We now only boot in VGA TEXT mode. */
	VGACONS.current_base = VGACONS.text_base;

	if ((GFXP_IS_CONSOLE(softc))) {
		gfxp_vga_init(softc);
		gfxp_vga_save_colormap(softc);
	}

	return (DDI_SUCCESS);
}

void
gfxp_vga_detach(struct gfxp_softc *softc)
{
	if (VGACONS.fb.mapped)
		ddi_regs_map_free(&VGACONS.fb.handle);
	if (VGACONS.regs.mapped)
		ddi_regs_map_free(&VGACONS.regs.handle);
}


static int
gfxp_vga_devinit(struct gfxp_softc *softc, struct vis_devinit *data)
{
	/* Initialize console instance. */
	data->version = VIS_CONS_REV;
	data->width = TEXT_COLS;
	data->height = TEXT_ROWS;
	data->linebytes = TEXT_COLS;
	data->depth = 4;
	data->color_map = NULL;
	data->mode = VIS_TEXT;
	data->polledio = &softc->polledio;

	softc->fbgattr = &gfxp_vga_attr;

	return (0);
}

/*
 * display a string on the screen at (row, col)
 *	 assume it has been cropped to fit.
 */

static void
gfxp_vga_cons_display(struct gfxp_softc *softc, struct vis_consdisplay *da)
{
	unsigned char	*string;
	int	i;
	unsigned char	attr;
	struct cgatext {
		unsigned char ch;
		unsigned char attr;
	};
	struct cgatext *addr;

	/* Sanitize input. */
	if (da->col < 0 || da->col >= TEXT_COLS ||
	    da->row < 0 || da->row >= TEXT_ROWS ||
	    da->col + da->width > TEXT_COLS) {
		return;
	}

	/*
	 * To be fully general, we should copyin the data.  This is not
	 * really relevant for this text-only driver, but a graphical driver
	 * should support these ioctls from userland to enable simple
	 * system startup graphics.
	 */
	attr = (solaris_color_to_pc_color[da->bg_color & 0xf] << 4)
	    | solaris_color_to_pc_color[da->fg_color & 0xf];
	string = da->data;
	addr = (struct cgatext *)VGACONS.current_base
	    +  (da->row * TEXT_COLS + da->col);
	for (i = 0; i < da->width; i++) {
		addr->ch = string[i];
		addr->attr = attr;
		addr++;
	}
}

static void
gfxp_vga_polled_display(
	struct vis_polledio_arg *arg,
	struct vis_consdisplay *da)
{
	struct gfxp_softc	*softc = (struct gfxp_softc *)arg;

	gfxp_vga_cons_display(softc, da);
}

/*
 * screen-to-screen copy
 */

static void
gfxp_vga_cons_copy(struct gfxp_softc *softc, struct vis_conscopy *ma)
{
	unsigned short	*from;
	unsigned short	*to;
	int		cnt;
	screen_size_t chars_per_row;
	unsigned short	*to_row_start;
	unsigned short	*from_row_start;
	screen_size_t	rows_to_move;
	unsigned short	*base;

	/* Sanity checks */
	if (ma->s_col < 0 || ma->s_col >= TEXT_COLS ||
	    ma->s_row < 0 || ma->s_row >= TEXT_ROWS ||
	    ma->e_col < 0 || ma->e_col >= TEXT_COLS ||
	    ma->e_row < 0 || ma->e_row >= TEXT_ROWS ||
	    ma->t_col < 0 || ma->t_col >= TEXT_COLS ||
	    ma->t_row < 0 || ma->t_row >= TEXT_ROWS ||
	    ma->s_col > ma->e_col ||
	    ma->s_row > ma->e_row ||
	    (ma->s_row == ma->t_row && ma->t_col == ma->s_col)) {
		return;
	}

	/*
	 * Remember we're going to copy shorts because each
	 * character/attribute pair is 16 bits.
	 */
	chars_per_row = ma->e_col - ma->s_col + 1;
	rows_to_move = ma->e_row - ma->s_row + 1;

	/* More sanity checks. */
	if (ma->t_row + rows_to_move > TEXT_ROWS ||
	    ma->t_col + chars_per_row > TEXT_COLS)
		return;

	base = (unsigned short *)VGACONS.current_base;

	to_row_start = base + ((ma->t_row * TEXT_COLS) + ma->t_col);
	from_row_start = base + ((ma->s_row * TEXT_COLS) + ma->s_col);

	if (to_row_start < from_row_start) {
		while (rows_to_move-- > 0) {
			to = to_row_start;
			from = from_row_start;
			to_row_start += TEXT_COLS;
			from_row_start += TEXT_COLS;
			for (cnt = chars_per_row; cnt-- > 0; )
				*to++ = *from++;
		}
	} else {
		/*
		 * Offset to the end of the region and copy backwards.
		 */
		cnt = rows_to_move * TEXT_COLS + chars_per_row;
		to_row_start += cnt;
		from_row_start += cnt;

		while (rows_to_move-- > 0) {
			to_row_start -= TEXT_COLS;
			from_row_start -= TEXT_COLS;
			to = to_row_start;
			from = from_row_start;
			for (cnt = chars_per_row; cnt-- > 0; )
				*--to = *--from;
		}
	}
}

static void
gfxp_vga_polled_copy(
	struct vis_polledio_arg *arg,
	struct vis_conscopy *ma)
{
	struct gfxp_softc	*softc = (struct gfxp_softc *)arg;

	gfxp_vga_cons_copy(softc, ma);
}

/* ARGSUSED */
static int
gfxp_vga_cons_clear(struct gfxp_softc *softc, struct vis_consclear *cls)
{
	/*
	 * VIS_CONSCLEAR is only called on VIS_PIXEL mode framebuffers.
	 * In any case, let's just safely fail here.
	 */
	return (ENOTSUP);
}

static void
gfxp_vga_cons_cursor(struct gfxp_softc *softc, struct vis_conscursor *ca)
{
	if (softc->silent)
		return;

	switch (ca->action) {
	case VIS_HIDE_CURSOR:
		VGACONS.cursor.visible = B_FALSE;
		if (VGACONS.current_base == VGACONS.text_base)
			gfxp_vga_hide_cursor(softc);
		break;
	case VIS_DISPLAY_CURSOR:
		/*
		 * Sanity check.  This is a last-ditch effort to avoid
		 * damage from brokenness or maliciousness above.
		 */
		if (ca->col < 0 || ca->col >= TEXT_COLS ||
		    ca->row < 0 || ca->row >= TEXT_ROWS)
			return;

		VGACONS.cursor.visible = B_TRUE;
		VGACONS.cursor.col = ca->col;
		VGACONS.cursor.row = ca->row;
		if (VGACONS.current_base == VGACONS.text_base)
			gfxp_vga_set_cursor(softc, ca->row, ca->col);
		break;
	case VIS_GET_CURSOR:
		if (VGACONS.current_base == VGACONS.text_base) {
			gfxp_vga_get_cursor(softc, &ca->row, &ca->col);
		}
		break;
	}
}

static void
gfxp_vga_polled_cursor(
	struct vis_polledio_arg *arg,
	struct vis_conscursor *ca)
{
	gfxp_vga_cons_cursor((struct gfxp_softc *)arg, ca);
}

/*ARGSUSED*/
static void
gfxp_vga_hide_cursor(struct gfxp_softc *softc)
{
	/* Nothing at present */
}

static void
gfxp_vga_set_cursor(struct gfxp_softc *softc, int row, int col)
{
	short	addr;

	if (softc->silent)
		return;

	addr = row * TEXT_COLS + col;

	vga_set_crtc(&VGACONS.regs, VGA_CRTC_CLAH, addr >> 8);
	vga_set_crtc(&VGACONS.regs, VGA_CRTC_CLAL, addr & 0xff);
}

static int vga_row, vga_col;

static void
gfxp_vga_get_cursor(struct gfxp_softc *softc,
    screen_pos_t *row, screen_pos_t *col)
{
	short   addr;

	addr = (vga_get_crtc(&VGACONS.regs, VGA_CRTC_CLAH) << 8) +
	    vga_get_crtc(&VGACONS.regs, VGA_CRTC_CLAL);

	vga_row = *row = addr / TEXT_COLS;
	vga_col = *col = addr % TEXT_COLS;
}


/*
 * gfxp_vga_vbios_setmode()
 *
 * Send a request to the vbios component to set a specific mode. Currently
 * this pretty much defaults to setting VGA TEXT mode (0x3).
 *
 * Returns 0 on success and -1 on failure. If ret is != NULL, the reason
 * for the failure is placed in there. Positive values correspond to
 * errno values returned by vbios_exec_cmd(), while negative values
 * correspond respectively to a general failure (-1) and a 'STUCK' case
 * (-2 -- STUCK means that the emulator didn't complete before the timeout).
 */
static int
gfxp_vga_vbios_setmode(uint8_t mode, int *err)
{
	vbios_cmd_reply_t	*reply;
	vbios_cmd_req_t		v_cmd;
	int			ret = -1;

	v_cmd.cmd = VBIOS_CMD_SETMODE;
	v_cmd.type = VBIOS_VGA_CALL;
	v_cmd.args.mode.vga.val = mode;

	reply = vbios_exec_cmd(&v_cmd);

	if (reply == NULL)
		goto out_err;

	if (reply->call_ret == VBIOS_SUCCESS) {
		vbios_free_reply(reply);
		return (0);
	} else if (reply->call_ret == VBIOS_STUCK) {
		ret = -2;
	} else {
		ret = reply->call_errtype;
	}
	vbios_free_reply(reply);

out_err:
	if (err != NULL)
		*err = ret;
	return (-1);
}

/*
 * gfxp_vga_save_text
 * gfxp_vga_restore_textmode
 * gfxp_vga_suspend
 * gfxp_vga_resume
 *
 * 	Routines to save and restore contents of the VGA text area
 * Mostly, this is to support Suspend/Resume operation for graphics
 * device drivers.  Here in the gfxp_vga common code, we simply squirrel
 * away the contents of the hardware's text area during Suspend and then
 * put it back during Resume
 */
static void
gfxp_vga_save_text(struct gfxp_softc *softc)
{
	unsigned	i;

	for (i = 0; i < sizeof (VGACONS.shadow); i++)
		VGACONS.shadow[i] = VGACONS.current_base[i];
}

static void
gfxp_vga_restore_textmode(struct gfxp_softc *softc)
{
	unsigned	i;

	gfxp_vga_init(softc);
	for (i = 0; i < sizeof (VGACONS.shadow); i++) {
		VGACONS.text_base[i] = VGACONS.shadow[i];
	}
	if (VGACONS.cursor.visible) {
		gfxp_vga_set_cursor(softc,
		    VGACONS.cursor.row, VGACONS.cursor.col);
	}
	gfxp_vga_restore_colormap(softc);
}

int
gfxp_vga_suspend(struct gfxp_softc *softc)
{
	switch (softc->mode) {
	case KD_TEXT:
		gfxp_vga_save_text(softc);
		break;

	case KD_GRAPHICS:
		break;

	default:
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}

void
gfxp_vga_resume(struct gfxp_softc *softc)
{
	/*
	 * If we're relying on userland emulation to restore the video card
	 * state, then defer the resume procedure up to the KDRESUME ioctl
	 * to wait for userland to come back.
	 */
	if (gfxp_vga_emulate_vbios == 1)
		return;

	switch (softc->mode) {
	case KD_TEXT:
		gfxp_vga_restore_textmode(softc);
		break;

	case KD_GRAPHICS:

		/*
		 * Upon RESUME, the graphics device will always actually
		 * be in TEXT mode even though the Xorg server did not
		 * make that mode change itself (the suspend code did).
		 * We want first, therefore, to restore textmode
		 * operation fully, and then the Xorg server will
		 * do the rest to restore the device to its
		 * (hi resolution) graphics mode
		 */
		gfxp_vga_restore_textmode(softc);
#if	defined(USE_BORDERS)
		gfxp_vga_init_graphics(softc);
#endif
		break;
	default:
		/* should have been caught already by gfxp_fb_resume */
		break;
	}
}

static void
gfxp_vga_kdsettext(struct gfxp_softc *softc)
{
	int i;

	gfxp_vga_init(softc);
	for (i = 0; i < sizeof (VGACONS.shadow); i++) {
		VGACONS.text_base[i] = VGACONS.shadow[i];
	}
	VGACONS.current_base = VGACONS.text_base;
	if (VGACONS.cursor.visible) {
		gfxp_vga_set_cursor(softc,
		    VGACONS.cursor.row, VGACONS.cursor.col);
	}
	gfxp_vga_restore_colormap(softc);
}

static void
gfxp_vga_kdsetgraphics(struct gfxp_softc *softc)
{
	softc->silent = 0;
	gfxp_vga_save_text(softc);
	VGACONS.current_base = VGACONS.shadow;
#if	defined(USE_BORDERS)
	gfxp_vga_init_graphics(softc);
#endif
}

#define	VB_TEXT_MODE	(0x3)
#define	TRIES		(5)

static int
gfxp_vga_kdsetmode(struct gfxp_softc *softc, int mode)
{
	if ((mode == softc->mode) || (!GFXP_IS_CONSOLE(softc)))
		return (0);

	switch (mode) {
	case KD_RESUME:
		/*
		 * This is a special mode that indicates to the gfx driver
		 * that the userland processes have resumed and it is now
		 * possible to request a VBIOS emulated call.
		 * The call is skipped if the X server is running.
		 */
		if (softc->mode == KD_GRAPHICS)
			return (0);

		/*
		 * If we are not relying on userland emulation, VGA TEXT
		 * mode has been already restored at this point.
		 */
		if (gfxp_vga_emulate_vbios == 0)
			return (0);

		mode = KD_TEXT;
	/* FALLTHROUGH */
	case KD_TEXT:
	{
		int	err;
		int	i;

		if (gfxp_vga_emulate_vbios == 0) {
			gfxp_vga_kdsettext(softc);
			break;
		}

		for (i = 0; i < TRIES; i++) {
			if (gfxp_vga_vbios_setmode(VB_TEXT_MODE, &err) != 0) {
				if (err == EINTR) {
					/* Wait a little. */
					delay(10);
				} else {
					gfxp_vga_kdsettext(softc);
					goto out;
				}
			} else {
				break;
			}
		}

		/*
		 * If user land emulation succeeded, we need to manually restore
		 * framebuffer contents.
		 */
		for (i = 0; i < sizeof (VGACONS.shadow); i++) {
			VGACONS.text_base[i] = VGACONS.shadow[i];
		}
		VGACONS.current_base = VGACONS.text_base;
		break;
	}
	case KD_GRAPHICS:
		gfxp_vga_kdsetgraphics(softc);
		break;
	case KD_RESETTEXT:
		/*
		 * In order to avoid racing with a starting X server,
		 * this needs to be a test and set that is performed in
		 * a single (softc->lock protected) ioctl into this driver.
		 */
		if (softc->mode == KD_TEXT && softc->silent == 1) {
			softc->silent = 0;
			gfxp_vga_kdsettext(softc);
		}
		/*
		 * Avoid setting an invalid mode.
		 */
		return (0);
	default:
		return (EINVAL);
	}
out:
	softc->mode = mode;
	return (0);
}

/*
 * This code is experimental. It's only enabled if console is
 * set to graphics, a preliminary implementation of happyface boot.
 */
static void
gfxp_vga_set_text(struct gfxp_softc *softc)
{
	int i;

	if (VGACONS.happyface_boot == 0)
		return;

	/* we are in graphics mode, set to text 80X25 mode */

	/* set misc registers */
	vga_set_reg(&VGACONS.regs, VGA_MISC_W, VGA_MISC_TEXT);

	/* set sequencer registers */
	vga_set_seq(&VGACONS.regs, VGA_SEQ_RST_SYN,
	    (vga_get_seq(&VGACONS.regs, VGA_SEQ_RST_SYN) &
	    ~VGA_SEQ_RST_SYN_NO_SYNC_RESET));
	for (i = 1; i < NUM_SEQ_REG; i++) {
		vga_set_seq(&VGACONS.regs, i, VGA_SEQ_TEXT[i]);
	}
	vga_set_seq(&VGACONS.regs, VGA_SEQ_RST_SYN,
	    (vga_get_seq(&VGACONS.regs, VGA_SEQ_RST_SYN) |
	    VGA_SEQ_RST_SYN_NO_ASYNC_RESET |
	    VGA_SEQ_RST_SYN_NO_SYNC_RESET));

	/* set crt controller registers */
	vga_set_crtc(&VGACONS.regs, VGA_CRTC_VRE,
	    (vga_get_crtc(&VGACONS.regs, VGA_CRTC_VRE) &
	    ~VGA_CRTC_VRE_LOCK));
	for (i = 0; i < NUM_CRTC_REG; i++) {
		vga_set_crtc(&VGACONS.regs, i, VGA_CRTC_TEXT[i]);
	}

	/* set graphics controller registers */
	for (i = 0; i < NUM_GRC_REG; i++) {
		vga_set_grc(&VGACONS.regs, i, VGA_GRC_TEXT[i]);
	}

	/* set attribute registers */
	for (i = 0; i < NUM_ATR_REG; i++) {
		vga_set_atr(&VGACONS.regs, i, VGA_ATR_TEXT[i]);
	}

	/* set palette */
	for (i = 0; i < VGA_TEXT_CMAP_ENTRIES; i++) {
		vga_put_cmap(&VGACONS.regs, i, VGA_TEXT_PALETTES[i][0] << 2,
		    VGA_TEXT_PALETTES[i][1] << 2,
		    VGA_TEXT_PALETTES[i][2] << 2);
	}
	for (i = VGA_TEXT_CMAP_ENTRIES; i < VGA8_CMAP_ENTRIES; i++) {
		vga_put_cmap(&VGACONS.regs, i, 0, 0, 0);
	}

	gfxp_vga_save_colormap(softc);
}

static void
gfxp_vga_init(struct gfxp_softc *softc)
{
	unsigned char atr_mode;

	atr_mode = vga_get_atr(&VGACONS.regs, VGA_ATR_MODE);
	if (atr_mode & VGA_ATR_MODE_GRAPH)
		gfxp_vga_set_text(softc);
	atr_mode = vga_get_atr(&VGACONS.regs, VGA_ATR_MODE);
	atr_mode &= ~VGA_ATR_MODE_BLINK;
	atr_mode &= ~VGA_ATR_MODE_9WIDE;
	vga_set_atr(&VGACONS.regs, VGA_ATR_MODE, atr_mode);
#if	defined(USE_BORDERS)
	vga_set_atr(&VGACONS.regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&VGACONS.regs, VGA_BRIGHT_WHITE));
#else
	vga_set_atr(&VGACONS.regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&VGACONS.regs, VGA_BLACK));
#endif
	gfxp_vga_setfont(softc);	/* need selectable font? */
}

#if	defined(USE_BORDERS)
static void
gfxp_vga_init_graphics(struct gfxp_softc *softc)
{
	vga_set_atr(&VGACONS.regs, VGA_ATR_BDR_CLR,
	    vga_get_atr(&VGACONS.regs, VGA_BLACK));
}
#endif

static void
gfxp_vga_setfont(struct gfxp_softc *softc)
{
	extern unsigned char *ENCODINGS[];
	unsigned char *from;
	unsigned char *to;
	int	i;
	int	j;
	int	bpc;

	/*
	 * The newboot code to use font plane 2 breaks NVIDIA
	 * (and some ATI) behavior.  Revert back to the S10
	 * code.
	 */

	/*
	 * I'm embarassed to say that I don't know what these magic
	 * sequences do, other than at the high level of "set the
	 * memory window to allow font setup".  I stole them straight
	 * from "kd"...
	 */
	vga_set_seq(&VGACONS.regs, 0x02, 0x04);
	vga_set_seq(&VGACONS.regs, 0x04, 0x06);
	vga_set_grc(&VGACONS.regs, 0x05, 0x00);
	vga_set_grc(&VGACONS.regs, 0x06, 0x04);

	/*
	 * This assumes 8x16 characters, which yield the traditional 80x25
	 * screen.  It really should support other character heights.
	 */
	bpc = 16;
	for (i = 0; i < 256; i++) {
		from = ENCODINGS[i];
		to = (unsigned char *)VGACONS.fb.addr + i * 0x20;
		for (j = 0; j < bpc; j++)
			*to++ = *from++;
	}

	vga_set_seq(&VGACONS.regs, 0x02, 0x03);
	vga_set_seq(&VGACONS.regs, 0x04, 0x02);
	vga_set_grc(&VGACONS.regs, 0x04, 0x00);
	vga_set_grc(&VGACONS.regs, 0x05, 0x10);
	vga_set_grc(&VGACONS.regs, 0x06, 0x0e);

}

static void
gfxp_vga_save_colormap(struct gfxp_softc *softc)
{
	int i;

	for (i = 0; i < VGA_ATR_NUM_PLT; i++) {
		VGACONS.attrib_palette[i] = vga_get_atr(&VGACONS.regs, i);
	}
	for (i = 0; i < VGA8_CMAP_ENTRIES; i++) {
		vga_get_cmap(&VGACONS.regs, i,
		    &VGACONS.colormap[i].red,
		    &VGACONS.colormap[i].green,
		    &VGACONS.colormap[i].blue);
	}
}

static void
gfxp_vga_restore_colormap(struct gfxp_softc *softc)
{
	int i;

	for (i = 0; i < VGA_ATR_NUM_PLT; i++) {
		vga_set_atr(&VGACONS.regs, i, VGACONS.attrib_palette[i]);
	}
	for (i = 0; i < VGA8_CMAP_ENTRIES; i++) {
		vga_put_cmap(&VGACONS.regs, i,
		    VGACONS.colormap[i].red,
		    VGACONS.colormap[i].green,
		    VGACONS.colormap[i].blue);
	}
}

/*ARGSUSED*/
static int
gfxp_vga_devmap(struct gfxp_softc *softc, dev_t dev, devmap_cookie_t dhp,
		offset_t off, size_t len, size_t *maplen, uint_t model)
{
	int err;
	size_t length;

	if (!(off >= VGA_MMAP_FB_BASE &&
	    off < VGA_MMAP_FB_BASE + VGACONS.fb_size)) {
		cmn_err(CE_WARN, "%s: Can't map offset 0x%llx", MYNAME, off);
		return (ENXIO);
	}

	if (off + len > VGA_MMAP_FB_BASE + VGACONS.fb_size)
		length = VGA_MMAP_FB_BASE + VGACONS.fb_size - off;
	else
		length = len;

	if ((err = devmap_devmem_setup(dhp, softc->devi,
	    NULL, VGACONS.fb_regno, off - VGA_MMAP_FB_BASE,
	    length, PROT_ALL, 0, &dev_attr)) < 0) {
		return (err);
	}

	*maplen = length;
	return (0);
}

/*
 * search the entries of the "reg" property for one which has the desired
 * combination of phys_hi bits and contains the desired address.
 *
 * This version searches a PCI-style "reg" property.  It was prompted by
 * issues surrounding the presence or absence of an entry for the ROM:
 * (a) a transition problem with PowerPC Virtual Open Firmware
 * (b) uncertainty as to whether an entry will be included on a device
 *     with ROM support (and so an "active" ROM base address register),
 *     but no ROM actually installed.
 *
 * See the note below on gfxp_vga_get_isa_reg_index for the reasons for
 * returning the offset.
 *
 * Note that this routine may not be fully general; it is intended for the
 * specific purpose of finding a couple of particular VGA reg entries and
 * may not be suitable for all reg-searching purposes.
 */
static int
gfxp_vga_get_pci_reg_index(
	dev_info_t *const devi,
	unsigned long himask,
	unsigned long hival,
	unsigned long addr,
	off_t *offset)
{

	int			length, index;
	pci_regspec_t	*reg;

	if (ddi_getlongprop(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &length) != DDI_PROP_SUCCESS) {
		return (-1);
	}

	for (index = 0; index < length / sizeof (pci_regspec_t); index++) {
		if ((reg[index].pci_phys_hi & himask) != hival)
			continue;
		if (reg[index].pci_size_hi != 0)
			continue;
		if (reg[index].pci_phys_mid != 0)
			continue;
		if (reg[index].pci_phys_low > addr)
			continue;
		if (reg[index].pci_phys_low + reg[index].pci_size_low <= addr)
			continue;

		*offset = addr - reg[index].pci_phys_low;
		kmem_free(reg, (size_t)length);
		return (index);
	}
	kmem_free(reg, (size_t)length);

	return (-1);
}

/*
 * search the entries of the "reg" property for one which has the desired
 * combination of phys_hi bits and contains the desired address.
 *
 * This version searches a ISA-style "reg" property.  It was prompted by
 * issues surrounding 8514/A support.  By IEEE 1275 compatibility conventions,
 * 8514/A registers should have been added after all standard VGA registers.
 * Unfortunately, the Solaris/Intel device configuration framework
 * (a) lists the 8514/A registers before the video memory, and then
 * (b) also sorts the entries so that I/O entries come before memory
 *     entries.
 *
 * It returns the "reg" index and offset into that register set.
 * The offset is needed because there exist (broken?) BIOSes that
 * report larger ranges enclosing the standard ranges.  One reports
 * 0x3bf for 0x21 instead of 0x3c0 for 0x20, for instance.  Using the
 * offset adjusts for this difference in the base of the register set.
 *
 * Note that this routine may not be fully general; it is intended for the
 * specific purpose of finding a couple of particular VGA reg entries and
 * may not be suitable for all reg-searching purposes.
 */
static int
gfxp_vga_get_isa_reg_index(
	dev_info_t *const devi,
	unsigned long hival,
	unsigned long addr,
	off_t *offset)
{

	int		length, index;
	struct regspec	*reg;
	uint64_t	reg_addr, reg_size;

	if (ddi_getlongprop(DDI_DEV_T_ANY, devi, DDI_PROP_DONTPASS,
	    "reg", (caddr_t)&reg, &length) != DDI_PROP_SUCCESS) {
		return (-1);
	}

	for (index = 0; index < length / sizeof (struct regspec); index++) {
		if (reg[index].regspec_bustype != hival)
			continue;
		reg_addr = REGSPEC_ADDR64(&reg[index]);
		reg_size = REGSPEC_SIZE64(&reg[index]);
		if (reg_addr > addr)
			continue;
		if (reg_addr + reg_size <= addr)
			continue;

		*offset = addr - reg_addr;
		kmem_free(reg, (size_t)length);
		return (index);
	}
	kmem_free(reg, (size_t)length);

	return (-1);
}
