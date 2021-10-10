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

#ifndef _AST_H
#define	_AST_H

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pci.h>
#include <sys/utsname.h>

#include <sys/visual_io.h>
#include <sys/gfx_common.h>
#include <sys/astio.h>

#ifndef VIS_GETPCICONFIG

/*
 * These definitions can be removed when they are included in the
 * visual_io.h
 */
#define	VIS_GETGFXIDENTIFIER	(VIOC | 11)
#define	VIS_GETVIDEOMODENAME	(VIOC | 12)
#define	VIS_STOREVIDEOMODENAME	(VIOC | 13)
#define	VIS_GETPCICONFIG	(VIOC | 14)
#define	VIS_GETEDIDLENGTH	(VIOC | 15)
#define	VIS_GETEDID		(VIOC | 16)
#define	VIS_SETIOREG		(VIOC | 17)
#define	VIS_GETIOREG		(VIOC | 18)

typedef struct vis_io_reg {
	uchar_t		offset;
	uchar_t		value;
} vis_io_reg_t;

#endif /* VIS_GETPCICONFIG */

#define	REGNUM_CONTROL_FB	1
#define	REGNUM_CONTROL_REG	2
#define	REGNUM_CONTROL_IO	3
#define	REGNUM_CONTROL_ROM	4

typedef struct ast_regspace {
	offset_t	global_offset;
	size_t		size;
	int		rnum;
	offset_t	register_offset;
	ddi_device_acc_attr_t *attrs;
} ast_regspace_t;

#if VIS_CONS_REV > 2

#define	AST_MAX_LINEBYTES	1920
#define	AST_MAX_PIXBYTES	4

#define	DEFCHAR_WIDTH		16
#define	DEFCHAR_HEIGHT		22
#define	DEFCHAR_SIZE		(DEFCHAR_WIDTH * DEFCHAR_HEIGHT)

#define	CSRMAX			(32 * 32)

#define	SHOW_CURSOR		1
#define	HIDE_CURSOR		0

/*
 * bits used in flags
 */
#define	AST_VIS_MODE_INITIALIZED	0x1
/* by default background is white, set this bit for black background: */
#define	AST_VIS_COLOR_INVERSE		0x2

struct ast_consinfo {
	uint32_t		bufsize;
	uchar_t			*bufp;
	void *			*csrp;
	int32_t			csr_x;
	int32_t			csr_y;
	uint32_t		csrb[60][40];
	uint16_t		kcmap[4][256];
	uint8_t			kcmap_flags[256];
	uint8_t			kcmap_max;
	uint32_t		pitch;
	uint32_t		bgcolor;
	uint32_t		offset;
	uint32_t		rshift;
	uint32_t		gshift;
	uint32_t		bshift;
	void 			*dfb;
	void			*dfb_handle;
	vis_modechg_cb_t	te_modechg_cb;
	struct vis_modechg_arg	*te_ctx;
	struct vis_polledio	*polledio;
	uint32_t		flags;

	struct ast_vis_cmd_buf	vis_cmd_buf[200];
	int			vis_cmd_buf_idx;
};

struct ast_vis_draw_data {
	uint32_t		image_row;
	uint32_t		image_col;
	uint16_t		image_width;
	uint16_t		image_height;
	uint8_t			*image;
};
#endif

typedef struct ast_softc	ast_softc_t;

typedef unsigned int (*PFNRead32)  (ast_softc_t *, unsigned int);
typedef void (*PFNWrite32) (ast_softc_t *, unsigned int, unsigned int);


#define	AST_HW_INITIALIZED	0x01

struct ast_softc {
	ddi_acc_handle_t	pci_handle;
	volatile caddr_t	regbase;
	ddi_acc_handle_t	regs_handle;
	volatile caddr_t	iobase;
	ddi_acc_handle_t	io_handle;
	volatile caddr_t	fbbase;
	ddi_acc_handle_t	fb_handle;
	ddi_iblock_cookie_t	iblock_cookie;
	dev_info_t		*devi;
	struct gfx_video_mode	videomode;
	ast_regspace_t		regspace[4];
	kmutex_t		lock;
	kmutex_t		ctx_lock;

	uint32_t		flags;

	int			displayWidth;
	int			displayHeight;
	int			displayDepth;
	int			pitch;

	struct ast_context	*contexts;
	struct ast_context	*shared_ctx;
	struct ast_context	*cur_ctx;

	PFNRead32		read32;
	PFNWrite32		write32;

#if VIS_CONS_REV > 2
	struct ast_consinfo	consinfo;
#endif
};


/*
 * bits used in context flags
 */
#define	AST_CTX_SHARED		0x01
#define	AST_CTX_SAVED		0x02

struct ast_ctx_map {
	struct ast_softc	*softc;
	devmap_cookie_t		*dhp;
	offset_t		offset;
	size_t			len;
	struct ast_context	*ctx;
	struct ast_ctx_map	*next;
	struct ast_ctx_map	*prev;
};

struct ast_context {
	struct ast_softc	*softc;
	dev_t			dev;
	struct ast_ctx_map	*mappings;
	struct ast_context	*next;
	unsigned int		flags;

	unsigned int		src_pitch;
	unsigned int		dst_pitch;
	unsigned int		src_base;
	unsigned int		dst_base;
	unsigned int		src_xy;
	unsigned int		dst_xy;
	unsigned int		rect_xy;
	unsigned int		cmd_setting;
};


#define	AR_PORT_WRITE		0x40
#define	MISC_PORT_WRITE		0x42
#define	SEQ_PORT		0x44
#define	DAC_INDEX_READ		0x47
#define	DAC_INDEX_WRITE		0x48
#define	DAC_DATA		0x49
#define	MISC_PORT_READ		0x4C
#define	GR_PORT			0x4E
#define	CRTC_PORT		0x54
#define	INPUT_STATUS1_READ	0x5A

#define	SRC_BASE		0x8000
#define	SRC_PITCH		0x8004
#define	DST_BASE		0x8008
#define	DST_PITCH		0x800C
#define	DST_XY			0x8010
#define	SRC_XY			0x8014
#define	RECT_XY			0x8018
#define	CMD_REG			0x803C
#define	CMD_SETTING		0x8044

#define	MASK_SRC_PITCH		0x1FFF
#define	MASK_DST_PITCH		0x1FFF
#define	MASK_DST_HEIGHT		0x7FF
#define	MASK_SRC_X		0xFFF
#define	MASK_SRC_Y		0xFFF
#define	MASK_DST_X		0xFFF
#define	MASK_DST_Y		0xFFF
#define	MASK_RECT_WIDTH		0x7FF
#define	MASK_RECT_HEIGHT	0x7FF

/* CMD Reg Definition */
#define	CMD_BITBLT		0x00000000

#define	CMD_COLOR_8		0x00000000
#define	CMD_COLOR_16		0x00000010
#define	CMD_COLOR_32		0x00000020

#define	CMD_X_INC		0x00000000
#define	CMD_X_DEC		0x00200000

#define	CMD_Y_INC		0x00000000
#define	CMD_Y_DEC		0x00100000

#define	ROP_S			0xCC

#define	SetIOReg(offset, value)			\
	*((uchar_t *)softc->iobase + offset) = value;

#define	GetIOReg(offset, value)			\
	value = *((uchar_t *)softc->iobase + offset);

#define	SetIndexReg(offset, index, value) {	\
	SetIOReg(offset, index);		\
	SetIOReg(offset + 1, value);		\
	}

#define	GetIndexReg(offset, index, value) {	\
	SetIOReg(offset, index);		\
	GetIOReg(offset + 1, value);		\
	}

#define	SetIndexRegMask(offset, index, and, value) {	\
	unsigned char tmp;			\
	SetIOReg(offset, index);		\
	GetIOReg(offset+1, tmp);		\
	tmp = (tmp & and) | value;		\
	SetIOReg(offset, index);		\
	SetIOReg(offset+1, tmp);		\
	}

#define	GetIndexRegMask(offset, index, and, value) {	\
	SetIOReg(offset, index);		\
	GetIOReg(offset+1, value);		\
	value = value & and;			\
	}

#define	SetReg(offset, value)			\
	SetIOReg(offset, value)

#define	GetReg(offset, value)			\
	GetIOReg(offset, value);


typedef int			LONG;
typedef unsigned char		UCHAR;
typedef unsigned short		USHORT;
typedef unsigned int		ULONG;
typedef unsigned int		Bool;

#define	FALSE			0
#define	TRUE			1

#if VIS_CONS_REV > 2
extern int ast_vis_devinit(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_devfini(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_consdisplay(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_conscursor(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_conscopy(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_putcmap(struct ast_softc *, dev_t, intptr_t, int);
extern void ast_vis_polled_consdisplay(struct vis_polledio_arg *,
    struct vis_consdisplay *);
extern void ast_vis_polled_conscursor(struct vis_polledio_arg *,
    struct vis_conscursor *);
extern void ast_vis_polled_conscopy(struct vis_polledio_arg *,
    struct vis_conscopy *);
extern void ast_vis_termemu_callback(struct ast_softc *);

#endif


/* Debugging code enabled if built with #define AST_DEBUG */

#define	AST_VIS_DEBUG_INIT		1
#define	AST_VIS_DEBUG_CONSDISPLAY	2
#define	AST_VIS_DEBUG_POLLDISPLAY	3
#define	AST_VIS_DEBUG_CONSCURSOR	4
#define	AST_VIS_DEBUG_POLLCURSOR	5
#define	AST_VIS_DEBUG_CONSCOPY		6
#define	AST_VIS_DEBUG_POLLCOPY		7
#define	AST_VIS_DEBUG_TERMCALLBACK	8
#define	AST_VIS_DEBUG_END		-1

extern int ast_vis_debug_test(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_debug_get_buf(struct ast_softc *, dev_t, intptr_t, int);
extern int ast_vis_debug_get_image(struct ast_softc *, dev_t, intptr_t, int);

extern int ast_debug_test(struct ast_softc *);

extern struct ast_softc *ast_get_softc(int);
extern int    ast_devmap(dev_t, devmap_cookie_t, offset_t, size_t, size_t *,
    uint_t);
extern int    ast_ctx_make_current(struct ast_softc *, struct ast_context *);
extern void   ast_ctx_save(struct ast_softc *, struct ast_context *);
extern void   ast_ctx_restore(struct ast_softc *, struct ast_context *);

extern int    ast_read_edid(struct ast_softc *, caddr_t, unsigned int *);


#endif /* _AST_H */
