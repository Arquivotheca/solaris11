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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SXGE_XDC_H
#define	_SYS_SXGE_XDC_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/debug.h>
#include <sys/stream.h>
#include <sys/atomic.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/dditypes.h>

#include <sxge_tdc.h>
#include <sxge_rdc.h>

#define	swap16(value) \
	((((value) & 0xff) << 8) | ((value) >> 8))

#define	swap32(value) \
	(((uint32_t)swap16((uint16_t)((value) & 0xffff)) << 16) | \
	(uint32_t)swap16((uint16_t)((value) >> 16)))

#define	swap64(value) \
	(((uint64_t)swap32((uint32_t)((value) & 0xffffffff)) << 32) | \
	(uint64_t)swap32((uint32_t)((value) >> 32)))

#if !defined(_BIG_ENDIAN)
#define	SXGE_SWAP32(value)	(value)
#define	SXGE_SWAP64(value)	(value)
#else
#define	SXGE_SWAP32(value)	swap32(value)
#define	SXGE_SWAP64(value)	swap64(value)
#endif

#define	SXGE_MPUTNN		SXGE_MPUTN
#define	SXGE_MGETNN		SXGE_MGETN

/* #define	SXGE_UDELAY(sxge, usec, busy)	   sxge_delay(usec) */

#define	TDC_NR_DESC		(512)
#define	TDC_NR_DESC_GAP		(16)
#define	TDC_BUF_SIZE		(4096)
#define	TDC_DESC_MAXLN		(4096)
#define	TDC_ALIGN_64B		(64)
#define	TDC_ALIGN_4KB		(4096)
#define	TDC_RESET_LOOP		(5)
#define	TDC_HDR_SIZE_DEFAULT	(16)
#define	TDC_TINY_SIZE		(128)

#define	RDC_NR_DESC		(512)
#define	RDC_NR_RCDESC		(512)
#define	RDC_ALIGN_64B		(64)
#define	RDC_ALIGN_4KB		(4096)
#define	RDC_BUF_SIZE		(4096)
#define	RDC_TINY_SIZE		(128)
#define	RDC_PAD_SIZE		(0x2)
#define	RDC_HDR_SIZE		(0x0) /* RSS 4 */

#define	SXGE_RX_CKERR		(0x1)
#define	SXGE_RX_FRAG		(0x2)
#define	SXGE_TX_CKENB		(0x10)

#define	SXGE_TDC_MAXGTHR	(TDC_MAX_GATHER_POINTERS - 2)
#define	SXGE_TX_BUFSIZE		(256)
#define	SXGE_TX_CTL_OFFSET	(0)
#define	SXGE_TX_MIN_LEN		(60)

#define	VNIN			(global_vnin)
#define	RDCN			(global_rdcn)
#define	TDCN			(global_tdcn)

#if defined(PBASE)
#undef	PBASE
#define	PBASE			(pbase)
#endif

#define	TDC_PBASE(t)		(TDC_BASE(t->vnin, t->tdcn))
#define	RDC_PBASE(r)		(RDC_BASE(r->vnin, r->rdcn))

#define	ROUNDUP(a, n)		(((a) + ((n) - 1)) & ~((n) - 1))

#define	NEXTTMD(s, p)		(((p)+1) == (s)->tmdlp ? (s)->tmdp : ((p)+1))
#define	NEXTRMD(s, p)		(((p)+1) == (s)->rmdlp ? (s)->rmdp : ((p)+1))
#define	NEXTRCMD(s, p)		(((p)+1) == (s)->rcmdlp ? (s)->rcmdp : ((p)+1))

#if 0
#define	SXGE_ALLOCB		sxge_allocb
#define	SXGE_FREEB		sxge_freeb

#define	SXGE_SUCCESS		(0)
#define	SXGE_FAILURE		(-1)
#endif

#define	SXGE_DUPB		sxge_dupb
#define	SXGE_FREEMSG		sxge_freemsg
#define	SXGE_ALLOCD		sxge_desballoc
#define	SXGE_MSIZE		sxge_msgsize
#define	SXGE_DBG_MP(mp)		sxge_dbg_mp(mp)

#define	SXGE_ALLOCM		sxge_allocm
#define	SXGE_FREEM		sxge_freem
#define	SXGE_DUPM		sxge_dupm


#define	LLC_SNAP_SAP		(0xAA)	/* SNAP SAP */
#define	VLAN_ETHERTYPE		(0x8100)

typedef struct ether_header	ether_header_t;
typedef struct ether_vlan_header ether_vlan_header_t;

typedef struct _sxge_msg_t {
	void			*sxgep;
	kmutex_t		lock;
	boolean_t		in_use;
	uint_t			in_bytes;
	boolean_t		in_last;
	uint_t			in_bufsz;
	struct _sxge_msg_t	*in_prev[4];
	uint32_t		ref_cnt;
	frtn_t			freeb;
	uint64_t		dma_ioaddr; /* ddi_dma_cookie_t */
	boolean_t		pre_buf;
	uchar_t			*buffer;
	uchar_t			*buffer_a;
	ddi_dma_handle_t	buffer_h;
	ddi_acc_handle_t 	buffer_mh;
	void			*buffer_vp;
	ddi_dma_cookie_t 	buffer_pp;
	uint_t			buffer_cc;
	size_t			size;
	size_t			asize;
	mblk_t			*mp;
	mblk_t			*nmp;
	uint_t			pri;
} sxge_msg_t, *sxge_msg_pt;

/*
 * tdc_state_t
 */

typedef struct tdc_state {
	void			*sxgep;
	uint_t			enable;
	uint_t			vnin;
	uint_t			tdcn;
	uint64_t		pbase;
	sxge_pio_handle_t	phdl;
	void			*rhdl;
	uint_t			size;
	uint_t			tdc_prsr_en;
	uint_t			intr_msk;

	tdc_desc_t		*tmdp;
	tdc_desc_t		*tmdlp;
	tdc_desc_t		*tnextp;
	uint32_t		wrap;
	uint64_t		*txbp;
	void			**txmb;
	void			**txmb_s;
	uint8_t			*txb;
	uint8_t			*txb_a;
	uint64_t		*ring;

	uint32_t		tdc_idx;
	tdc_cs_t		tdc_cs;
	tdc_rng_hdl_t		tdc_hdl;
	tdc_rng_kick_t		tdc_kick;

	uint32_t		tmhp;
	tdc_mbx_desc_t		*mbxp;
	tdc_mbx_desc_t		*mbxp_a;

	ddi_dma_handle_t	desc_h;   /* DMA handle */
	ddi_acc_handle_t 	desc_mh;  /* DMA Memory Handle */
	void			*desc_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	desc_pp;  /* Physical Address Pointer */

	ddi_dma_handle_t	txb_h;   /* DMA handle */
	ddi_acc_handle_t 	txb_mh;  /* DMA Memory Handle */
	void			*txb_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	txb_pp;  /* Physical Address Pointer */

	ddi_dma_handle_t	mbx_h;   /* DMA handle */
	ddi_acc_handle_t 	mbx_mh;  /* DMA Memory Handle */
	void			*mbx_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	mbx_pp;  /* Physical Address Pointer */

	uint32_t		obytes;
	uint32_t		opackets;
	uint32_t		oerrors;
	uint32_t		oerrors_rst;
	uint32_t		ofulls;
} tdc_state_t, *tdc_state_pt;

/*
 * rdc_state_t
 */

typedef struct rdc_state {
	void			*sxgep;
	uint_t			enable;
	uint_t			vnin;
	uint_t			rdcn;
	uint_t			rxpad;
	uint_t			rxhdr;
	uint64_t		pbase;
	sxge_pio_handle_t	phdl;
	void			*rhdl;
	uint_t			size;
	uint_t			rcr_size;
	uint_t			intr_msk;

	rdc_rbr_desc_t		*rmdp;
	rdc_rbr_desc_t		*rmdlp;
	rdc_rbr_desc_t		*rnextp;
	rdc_rcr_desc_t		*rcmdp;
	rdc_rcr_desc_t		*rcmdlp;
	rdc_rcr_desc_t		*rcnextp;
	rdc_rcr_desc_t		*rcmtp;

	uint32_t		rcr_idx;
	uint32_t		rbr_idx;
	rdc_ctl_stat_t		rdc_cs;
	rdc_kick_t		rdc_kick;

	uint64_t		*rxbp;
	void			**rxmb;
	void			**rxmb_s;
	uint8_t			*rxb;
	uint8_t			*rxb_a;
	uint64_t		*ring;
	uint64_t		*rcr_ring;
	uint32_t		wrap;
	uint32_t		rcwrap;
	uint32_t		qlen;
	rdc_mbx_desc_t		*mbxp;
	rdc_mbx_desc_t		*mbxp_a;
	rdc_ctl_stat_t		mbx_cs;

	ddi_dma_handle_t	desc_h;   /* DMA handle */
	ddi_acc_handle_t 	desc_mh;  /* DMA Memory Handle */
	void			*desc_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	desc_pp;  /* Physical Address Pointer */

	ddi_dma_handle_t	cdesc_h;   /* DMA handle */
	ddi_acc_handle_t 	cdesc_mh;  /* DMA Memory Handle */
	void			*cdesc_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	cdesc_pp;  /* Physical Address Pointer */

	ddi_dma_handle_t	rxb_h;   /* DMA handle */
	ddi_acc_handle_t 	rxb_mh;  /* DMA Memory Handle */
	void			*rxb_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	rxb_pp;  /* Physical Address Pointer */

	ddi_dma_handle_t	mbx_h;   /* DMA handle */
	ddi_acc_handle_t 	mbx_mh;  /* DMA Memory Handle */
	void			*mbx_vp; /* Virtual Address Pointer */
	ddi_dma_cookie_t 	mbx_pp;  /* Physical Address Pointer */

	uint32_t		ibytes;
	uint32_t		ipackets;
	uint32_t		ierrors;
	uint32_t		ierrors_alloc;
	uint32_t		ierrors_rst;
	uint32_t		idrops;
} rdc_state_t, *rdc_state_pt;

#define	BUF_SET_IFLAGS(buf, val) ((buf)->b_datap->b_ick_flag = val)
#define	BUF_GET_IFLAGS(buf)	((buf)->b_datap->b_ick_flag)
#define	BUF_SET_ISTART(buf, val) ((buf)->b_datap->b_ick_start = val)
#define	BUF_GET_ISTART(buf)	((buf)->b_datap->b_ick_start)
#define	BUF_SET_ISTUFF(buf, val) ((buf)->b_datap->b_ick_stuff = val)
#define	BUF_GET_ISTUFF(buf)	((buf)->b_datap->b_ick_stuff)
#define	BUF_SET_IEND(buf, val)	((buf)->b_datap->b_ick_end = val)
#define	BUF_GET_IEND(buf)	((buf)->b_datap->b_ick_end)
#define	BUF_SET_IVALUE(buf, val) ((buf)->b_datap->b_ick_value = val)
#define	BUF_GET_IVALUE(buf)	((buf)->b_datap->b_ick_value)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SXGE_XDC_H */
