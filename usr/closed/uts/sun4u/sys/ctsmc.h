/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CTSMC_H
#define	_SYS_CTSMC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/smc_if.h>
#include <sys/ctsmc_queue.h>
#include <sys/ctsmc_seq.h>
#include <sys/ctsmc_hw.h>
#include <sys/ctsmc_ipmseq.h>
#include <sys/ctsmc_i2c.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	SMC_SUCCESS		0
#define	SMC_FAILURE		1
#define	SMC_HW_FAILURE	2
#define	SMC_INVALID_ARG	3
#define	SMC_TIMEOUT		4
#define	SMC_NOMEM		5
#define	SMC_OP_ABORTED	6

#define	SMC_DOWN	0x0000
#define	SMC_UP		0x0001
#define	SMC_OPEN	0x0002
#define	SMC_EXCL	0x0004
#define	SMC_LOCKED	0x0008
#define	SMC_FROZEN	0x0010
#define	SMC_ECHO	0x0020
#define	SMC_HS_ACTIVE	0x0040

#define	SMC_INCR_IDX(idx, lmt)	(((idx) >= (lmt) - 1) ? idx = 0 : (idx)++)

#define	NEW(n, type)	\
	(type *) kmem_zalloc((n) * sizeof (type), KM_SLEEP)

#define	ANEW(n, el_size)	\
		kmem_zalloc((n) * (el_size), KM_SLEEP)

#define	FREE(ref, n, type)	\
	kmem_free(ref, (n) * sizeof (type))

#define	AFREE(ref, n, el_size)	\
	kmem_free(ref, (n) * (el_size))

#define	NEXT(X)		((X)->next)

#define	SMC_BLOCK_FULL		(0xFFFF)
#define	NUM_TO_BLOCK(X)		((X)/SMC_BLOCK_SZ)
#define	NUM_OFFSET(X)		((X) % SMC_BLOCK_SZ)
#define	MAKE_NUM(chunk, off)	((chunk) * SMC_BLOCK_SZ + (off))
#define	BITTEST(num, pos)	((num) & (1 << (pos)))
#define	SETBIT(var, pos)	((var) |= (1 << (pos)))
#define	CLRBIT(var, pos)	((var) &= ~(1 << (pos)))

#define	LOCK_DATA(dataptr)		mutex_enter(&(dataptr)->lock)
#define	UNLOCK_DATA(dataptr)	mutex_exit(&(dataptr)->lock)

#define	SMC_ASYNC_REQ	0
#define	SMC_SYNC_REQ	1
/*
 * Data structures for handling asynchronous messages from SMC.
 * An application interested in receiving async messages may
 * register a set of commands with driver. These, when received,
 * are forwarded to this stream. Currently we have only few async
 * messages, e.g. ENUM# notiification, Local event, Healthy
 * change, IPMI response notification.
 */
#define	SMC_NUM_CMDS	0x100
typedef struct ctsmc_cmdspec_ent ctsmc_cmdspec_ent_t;
struct ctsmc_cmdspec_ent {
	uint8_t	attribute;		/* exclusive or shared */
	uint8_t	minor;			/* Cached minor# for exclusive */
	uint16_t minor_mask[NUM_BLOCKS];
};

typedef struct {
	kmutex_t	lock;
	ctsmc_cmdspec_ent_t ctsmc_cmd_ent[SMC_NUM_CMDS];
} ctsmc_cmdspec_list_t;

#define	MUXGET(q)	(ctsmc_minor_t *)(q)->q_ptr
#define	MUXPUT(q, mptr)	(q)->q_ptr = (void *)(mptr)

/*
 * During a synchronous request, requestor asks for a request
 * buffer, where the reply message is copied when a response
 * is received. Buffer 0 is reserved, only 1...255 are available
 * for allocation
 */
#define	SMC_NUM_BUFS	0x100
typedef struct ctsmc_state_data ctsmc_state_t;
extern ctsmc_state_t *ctsmc_get_soft_state(int unit);
typedef struct {
	kmutex_t lock;
	uint8_t		last_buf;	/* Index of last alloc'd buffer */
	ctsmc_queue_t	queue;		/* Queue of buffer allocation */
	uint16_t mask[SMC_NUM_BUFS/SMC_BLOCK_SZ];	/* Mask of allocation */
	uint8_t minor[SMC_NUM_BUFS];	/* Mapping of buf# ==> minor */
	sc_rspmsg_t *rspbuf[SMC_NUM_BUFS];
} ctsmc_buflist_t;

#define	FIND_BUF(SMC, idX)	((ctsmc)->ctsmc_buf_list->rspbuf[idX])
extern void	ctsmc_initBufList(ctsmc_state_t *ctsmc);
extern void	ctsmc_freeBufList(ctsmc_state_t *ctsmc);
extern uint8_t ctsmc_allocBuf(ctsmc_state_t *, uint8_t, uint8_t *);
extern void ctsmc_freeBuf(ctsmc_state_t *ctsmc, uint8_t index);

/*
 * Offsets of different fields in an IPMB message in payload
 */
#define	IPMB_OFF_CHAN	0	/* 00 => IPMB */
#define	IPMB_OFF_DADDR	1	/* Address of intended destination */
#define	IPMB_OFF_OADDR	4	/* Address of message origin */
#define	IPMB_OFF_NETFN	2	/* NetFn/remoteLUN */
#define	IPMB_OFF_SEQ	5	/* Seq/localLUN */
#define	IPMB_OFF_CMD	6	/* Command */

#define	IPMB_GET_NETFN(X)	((X) >> 2)	/* Extracts NetFn from byte 1 */
#define	IPMB_GET_SEQ(X)		((X) >> 2)	/* Extracts Seq from byte 4 */
#define	IPMB_GET_LUN(X)		((X) & 0x3)	/* Extracts LUN */

#define	IS_IPMB_REQ(NetFn)	(((NetFn)&1) == 0)	/* Whether IPMB req */
#define	IS_IPMB_RSP(NetFn)	((NetFn)&1)		/* Whether IPMB rsp */

/*
 * ================================================================
 * Streams queue structures
 * SMC driver is a clonable driver. For every open of the device, a
 * separate minor number and queue pair is allocated.
 * There will be a private stucture for each stream from SMC to describe
 * the purpose of the stream in more detail. One ctsmc_minor_t is allocated
 * for each of possible 256 minor numbers.
 *
 * Private data for each minor node allocated. This is stored as
 * queue private data when SMC driver is opened.
 */
typedef struct {
	kmutex_t	lock;
	uint8_t		minor;		/* minor num for the queue */
	queue_t		*ctsmc_rq;	/* read queue for this minor node */
	ctsmc_state_t	*ctsmc_state; /* pointer to SMC device state */
	uint64_t	req_events;	/* Events requested */
	uint_t		ctsmc_flags;	/* various flags */
} ctsmc_minor_t;

typedef struct {
	kmutex_t	lock;		/* Protect this list */
	ctsmc_queue_t	queue;	/* queue to track minor allocation */
	ctsmc_minor_t *minor_list[MAX_SMC_MINORS];
} ctsmc_minor_list_t;

/*
 * Macros to encode device instance number (unit) and
 * allocated device open instance into minor number
 */
#define	SMC_NO_OF_BOARDS	1
#define	SMC_CTRL_SHIFT	8
#define	SMCDEV_MAX	(1 << SMC_CTRL_SHIFT)
#define	SMCDEV_MASK	((SMCDEV_MAX) - 1)
#define	SMC_DEVICE(minor)	((minor) & SMCDEV_MASK)
#define	SMC_UNIT(minor)		((minor) >> SMC_CTRL_SHIFT)
#define	SMC_MAKEMINOR(unit, mdev)	(((unit) >> SMC_CTRL_SHIFT) | \
							SMC_DEVICE(mdev))

#define	SMC_CLONE_DEV	"ctsmc"

/*
 * typedef to facilitate casting the function being
 * passed to ddi_add_intr().
 */
typedef	uint_t (*intr_handler_t)(caddr_t);

typedef struct {
	intr_handler_t 	enum_handler;
	void			*intr_arg;
} enum_intr_desc_t;

#define	SMC_IS_ATTACHED			0x0001
#define	SMC_IS_DETACHING		0x0002
#define	SMC_IN_INTR				0x0004
#define	SMC_INTR_ADDED			0x0008
#define	SMC_REGS_MAPPED			0x0010
#define	SMC_STATE_INIT			0x0020
#define	SMC_ATTACH_DONE			0x0040
#define	SMC_KSTST				0x0080

/*
 * SMC runtime operation state, e.g. busy etc.
 */
#define	SMC_IS_BUSY				0x01
#define	SMC_SEQ_TIMEOUT_RUNNING	0x02

#define	SMC_KSTAT_NAME			"ctsmc_cmd_stat"
#define	SMC_I2C_PCF8591_NAME	"adc-dac"
#define	SMC_I2C_KSTAT_CPUTEMP	"adc_temp"
/*
 * SMC driver soft state structure
 */
struct ctsmc_state_data {
	uint32_t ctsmc_instance;	/* instance # */
	kmutex_t lock;			/* Per instance data lock */
	kcondvar_t ctsmc_cv;		/* Condition var for SMC data struct */
	kcondvar_t exit_cv;		/* To assist in SMC detach process */
	uint32_t ctsmc_init;		/* Driver init/operational state */
	uint32_t ctsmc_opens;		/* number of opens */
	uint8_t	 ctsmc_mode;		/* mode of operation */
	dev_info_t	*ctsmc_dip;	/* Pointer to devinfo struct */
	ctsmc_hwinfo_t	*ctsmc_hw; /* SMC hardware & mapping data */
	ctsmc_rsppkt_t	*ctsmc_rsppkt;	/* Last received packet */
	timeout_id_t ctsmc_tid;
	uint32_t ctsmc_flag;		/* Operational flag (BUSY ?) */
	ctsmc_minor_list_t	*ctsmc_minor_list; /* minor allocation */
	ctsmc_seqarr_t	*ctsmc_seq_list; /* Maintains list of available seq # */
	ctsmc_cmdspec_list_t	*ctsmc_cmdspec_list; /* 1 each for cmd & evt */
	ctsmc_buflist_t		*ctsmc_buf_list; /* List of reply buffers */
	ctsmc_dest_seq_list_t *ctsmc_ipmb_seq;	/* Alloc seq# for IPMB comm */
	ctsmc_i2c_state_t *ctsmc_i2c;	/* i2c_mstr state */
	kstat_t			*smcksp;	/* kstat */
	kstat_t			*smctempksp;	/* temperature kstat */
	void			**msglog[4]; /* logs of requests/responses */
};

/*
 * Given a minor number, return a pointer to minor node
 */
#define	GETMINOR(ctsmc, n)	\
	((ctsmc)->ctsmc_minor_list->minor_list[n])

typedef void (*FnPtr)(void *);
typedef int (*intFnPtr)(uint8_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CTSMC_H */
