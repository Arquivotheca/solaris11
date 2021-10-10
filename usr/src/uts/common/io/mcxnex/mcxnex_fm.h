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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MCXNEX_FM_H
#define	_MCXNEX_FM_H

/*
 * mcxnex_fm.h
 */
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HCA FMA compile note.
 *
 * FMA_TEST is used for HCA function tests, and
 * the macro can be on by changing Makefile.
 *
 * in case of DEBUG
 * 	FMA_TEST is on
 *
 * in case of non-DEBUG (DEBUG is off)
 * 	FMA_TEST is off
 */

/*
 * HCA FM common data structure
 */

/*
 * HCA FM Structure
 * This structure is used to catch HCA HW errors.
 */
struct i_hca_fm {
	uint32_t ref_cnt;	/* the number of instances referring to this */
	kmutex_t lock;		/* protection for last_err & polling thread */
	struct i_hca_acc_handle *hdl;	/* HCA FM acc handle structure */
	struct kmem_cache *fm_acc_cache; /* HCA acc handle cache */

};

/*
 * HCA FM acc handle structure
 * This structure is holding ddi_acc_handle_t and other members
 * to deal with HCA PIO FM.
 */
struct i_hca_acc_handle {
	struct i_hca_acc_handle *next;	/* next structure */
	ddi_acc_handle_t save_hdl;	/* acc handle */
	kmutex_t lock;			/* mutex lock for thread count */
	uint32_t thread_cnt;		/* number of threads issuing PIOs */
};
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", i_hca_acc_handle::save_hdl))
#define	fm_acc_hdl(hdl)	(((struct i_hca_acc_handle *)(hdl))->save_hdl)
#define	FM_POLL_INTERVAL (10000000)	/* 10ms (nano) */

/*
 * HCA FM function test structure
 * This structure can be used to test the basic fm function test for HCA.
 * The test code is included if the FMA_TEST macro is defined.
 */
struct i_hca_fm_test {
	int num;		/* serial numner */
	int type;		/* PIO or Mcxnex specific errors */
#define	HCA_TEST_PIO	0x1
#define	HCA_TEST_IBA	0x2
	int trigger;		/* how to trigger a HW error */
#define	HCA_TEST_TRANSIENT		0x0001
#define	HCA_TEST_PERSISTENT		0x0002
#define	HCA_TEST_ATTACH			0x0010
#define	HCA_TEST_START			0x0100
#define	HCA_TEST_END			0x0200
	void (*pio_injection)(struct i_hca_fm_test *, ddi_fm_error_t *);
	int errcnt;		/* how many transient error occurs */
	int line_num;		/* line number in the source code */
	char *file_name;	/* source filename */
	char *hash_key;		/* hash table for test items */
	void *private;		/* private data */
};

/*
 * Mcxnex FM data structure
 */
typedef struct i_hca_fm mcxnex_hca_fm_t;
typedef struct i_hca_acc_handle mcxnex_acc_handle_t;
typedef struct i_hca_fm_test mcxnex_test_t;

/*
 * The following defines are to supplement device error reporting.  At
 * each place where the planned FMA error matrix specifies that an
 * ereport will be generated, for now there is a MCXNEX_FMANOTE() call
 * generating an appropriate message string.
 */

#define	MCXNEX_FMANOTE(state, string)					\
	cmn_err(CE_NOTE, "mcxnex%d: Device Error: %s",			\
		(state)->hs_instance, string)

/* CQE Syndrome errors - see mcxnex_cq.c */

#define	MCXNEX_FMA_LOCLEN 	"CQE local length error"
#define	MCXNEX_FMA_LOCQPOP	"CQE local qp operation error"
#define	MCXNEX_FMA_LOCPROT	"CQE local protection error"
#define	MCXNEX_FMA_WQFLUSH	"CQE wqe flushed in error"
#define	MCXNEX_FMA_MWBIND	"CQE memory window bind error"
#define	MCXNEX_FMA_RESP		"CQE bad response"
#define	MCXNEX_FMA_LOCACC	"CQE local access error"
#define	MCXNEX_FMA_REMREQ	"CQE remote invalid request error"
#define	MCXNEX_FMA_REMACC	"CQE remote access error"
#define	MCXNEX_FMA_REMOP	"CQE remote operation error"
#define	MCXNEX_FMA_XPORTCNT	"CQE transport retry counter exceeded"
#define	MCXNEX_FMA_RNRCNT	"CQE RNR retry counter exceeded"
#define	MCXNEX_FMA_REMABRT	"CQE remote aborted error"
#define	MCXNEX_FMA_UNKN		"CQE unknown/reserved error returned"

/* event errors - see mcxnex_event.c */
#define	MCXNEX_FMA_OVERRUN	"EQE cq overrun or protection error"
#define	MCXNEX_FMA_LOCCAT	"EQE local work queue catastrophic error"
#define	MCXNEX_FMA_QPCAT	"EQE local queue pair catastrophic error"
#define	MCXNEX_FMA_PATHMIG	"EQE path migration failed"
#define	MCXNEX_FMA_LOCINV	"EQE invalid request - local work queue"
#define	MCXNEX_FMA_LOCACEQ	"EQE local access violation"
#define	MCXNEX_FMA_SRQCAT	"EQE shared received queue catastrophic"
#define	MCXNEX_FMA_INTERNAL	"EQE hca internal error"

/* HCR device failure returns - see mcxnex_cmd.c */
#define	MCXNEX_FMA_HCRINT	"HCR internal error processing command"
#define	MCXNEX_FMA_NVMEM	"HCR NVRAM checksum/CRC failure"
#define	MCXNEX_FMA_TOTOG	"HCR Timeout waiting for command toggle"
#define	MCXNEX_FMA_GOBIT	"HCR Timeout waiting for command go bit"
#define	MCXNEX_FMA_RSRC		"HCR Command insufficient resources"
#define	MCXNEX_FMA_CMDINV	"HCR Command invalid status returned"

/* HCA initialization errors - see mcxnex.c */
#define	MCXNEX_FMA_FWVER	"HCA firmware not at minimum version"
#define	MCXNEX_FMA_PCIID	"HCA PCIe devid not supported"
#define	MCXNEX_FMA_MAINT	"HCA device set to memory controller mode"
#define	MCXNEX_FMA_BADNVMEM	"HCR bad NVMEM error"

/*
 * HCA FM constants
 */

/* HCA FM state */
#define	HCA_NO_FM		0x0000	/* HCA FM is not supported */
/* HCA FM state flags */
#define	HCA_PIO_FM		0x0001	/* PIO is fma-protected */
#define	HCA_DMA_FM		0x0002	/* DMA is fma-protected */
#define	HCA_EREPORT_FM		0x0004	/* FMA ereport is available */
#define	HCA_ERRCB_FM		0x0010	/* FMA error callback is supported */

#define	HCA_ATTCH_FM		0x0100	/* HCA FM attach mode */
#define	HCA_RUNTM_FM		0x0200	/* HCA FM runtime mode */

/* HCA ererport type */
#define	HCA_SYS_ERR		0x001	/* HW error reported by Solaris FMA */
#define	HCA_IBA_ERR		0x002	/* IB specific HW error */

/* HCA ereport detail */
#define	HCA_ERR_TRANSIENT	0x010	/* HCA temporary error */
#define	HCA_ERR_NON_FATAL	0x020	/* HCA persistent error */
#define	HCA_ERR_SRV_LOST	0x040	/* HCA attach failure */
#define	HCA_ERR_DEGRADED	0x080	/* HCA maintenance mode */
#define	HCA_ERR_FATAL		0x100	/* HCA critical situation */
#define	HCA_ERR_IOCTL		0x200	/* EIO */

/* Ignore HCA HW error check */
#define	HCA_SKIP_HW_CHK		(-1)

/* HCA FM pio retry operation state */
#define	HCA_PIO_OK		(0)	/* No HW errors */
#define	HCA_PIO_TRANSIENT	(1)	/* transient error */
#define	HCA_PIO_PERSISTENT	(2)	/* persistent error */
#define	HCA_PIO_RETRY_CNT	(3)

/* HCA firmware faults */
#define	HCA_FW_MISC		0x1	/* firmware misc faults */
#define	HCA_FW_CORRUPT		0x2	/* firmware corruption */
#define	HCA_FW_MISMATCH		0x3	/* firmware version mismatch */

/*
 * Mcxnex FM macros
 */

#ifdef FMA_TEST
#define	TEST_DECLARE(tst)		mcxnex_test_t *tst;
#define	REGISTER_PIO_TEST(st, tst)					\
    tst = mcxnex_test_register(st, __FILE__, __LINE__, HCA_TEST_PIO)
#define	PIO_START(st, hdl, tst)		mcxnex_PIO_start(st, hdl, tst)
#define	PIO_END(st, hdl, cnt, tst)	mcxnex_PIO_end(st, hdl, &cnt, tst)
#else
#define	TEST_DECLARE(tst)
#define	REGISTER_PIO_TEST(st, tst)
#define	PIO_START(st, hdl, tst)		mcxnex_PIO_start(st, hdl, NULL)
#define	PIO_END(st, hdl, cnt, tst)	mcxnex_PIO_end(st, hdl, &cnt, NULL)
#endif /* FMA_TEST */

/*
 * mcxnex_pio_init() is a macro initializing variables.
 */
#define	mcxnex_pio_init(cnt, status, tst)				\
	TEST_DECLARE(tst)						\
	int	status = HCA_PIO_OK;					\
	int	cnt = HCA_PIO_RETRY_CNT

/*
 * mcxnex_pio_start() is one of a pair of macros checking HW errors
 * at PIO requests, which should be called before the requests are issued.
 */
#define	mcxnex_pio_start(st, hdl, label, cnt, status, tst)		\
	if (st->hs_fm_state & HCA_PIO_FM) {				\
		if (st->hs_fm_async_fatal) {				\
			mcxnex_fm_ereport(st, HCA_SYS_ERR,		\
			    HCA_ERR_NON_FATAL);				\
			goto label;					\
		} else {						\
			REGISTER_PIO_TEST(st, tst);			\
			cnt = HCA_PIO_RETRY_CNT;			\
			if (PIO_START(st, hdl, tst) ==			\
			    HCA_PIO_PERSISTENT) {			\
				goto label;				\
			}						\
		}							\
	} else {							\
		status = HCA_SKIP_HW_CHK;				\
	}								\
	do {

/*
 * mcxnex_pio_end() is the other of a pair of macros checking HW errors
 * at PIO requests, which should be called after the requests end.
 * If a HW error is detected and can be isolated well, these macros
 * retry the operation to determine if the error is persistent or not.
 */
#define	mcxnex_pio_end(st, hdl, label, cnt, status, tst)		\
	if (status != HCA_SKIP_HW_CHK) {				\
		if (st->hs_fm_async_fatal) {				\
			mcxnex_fm_ereport(st, HCA_SYS_ERR,		\
			    HCA_ERR_NON_FATAL);				\
			goto label;					\
		}							\
		if ((status = PIO_END(st, hdl, cnt, tst)) ==		\
		    HCA_PIO_PERSISTENT) {				\
			goto label;					\
		} else if (status == HCA_PIO_TRANSIENT) {		\
			mcxnex_fm_ereport(st, HCA_SYS_ERR,		\
			    HCA_ERR_TRANSIENT);				\
		}							\
	}								\
	} while (status == HCA_PIO_TRANSIENT)

void mcxnex_fm_init(mcxnex_state_t *);
void mcxnex_fm_fini(mcxnex_state_t *);
extern int mcxnex_fm_ereport_init(mcxnex_state_t *);
extern void mcxnex_fm_ereport_fini(mcxnex_state_t *);
extern int mcxnex_get_state(mcxnex_state_t *);
extern boolean_t mcxnex_init_failure(mcxnex_state_t *);
extern boolean_t mcxnex_cmd_retry_ok(mcxnex_cmd_post_t *, int);
extern void mcxnex_fm_ereport(mcxnex_state_t *, int, int);
extern int mcxnex_regs_map_setup(mcxnex_state_t *, uint_t, caddr_t *, offset_t,
    offset_t, ddi_device_acc_attr_t *, ddi_acc_handle_t *);
extern void mcxnex_regs_map_free(mcxnex_state_t *, ddi_acc_handle_t *);
extern int mcxnex_pci_config_setup(mcxnex_state_t *, ddi_acc_handle_t *);
extern void mcxnex_pci_config_teardown(mcxnex_state_t *, ddi_acc_handle_t *);
extern ushort_t mcxnex_devacc_attr_version(mcxnex_state_t *);
extern uchar_t mcxnex_devacc_attr_access(mcxnex_state_t *);
extern int mcxnex_PIO_start(mcxnex_state_t *, ddi_acc_handle_t,
    mcxnex_test_t *);
extern int mcxnex_PIO_end(mcxnex_state_t *, ddi_acc_handle_t, int *,
    mcxnex_test_t *);
extern ddi_acc_handle_t mcxnex_rsrc_alloc_uarhdl(mcxnex_state_t *);
extern ddi_acc_handle_t mcxnex_get_uarhdl(mcxnex_state_t *);
extern ddi_acc_handle_t mcxnex_get_cmdhdl(mcxnex_state_t *);
extern ddi_acc_handle_t mcxnex_get_msix_tblhdl(mcxnex_state_t *);
extern ddi_acc_handle_t mcxnex_get_msix_pbahdl(mcxnex_state_t *);
extern ddi_acc_handle_t mcxnex_get_pcihdl(mcxnex_state_t *);
extern void mcxnex_clr_state_nolock(mcxnex_state_t *, int);
extern void mcxnex_inter_err_chk(void *);

#ifdef FMA_TEST
extern mcxnex_test_t *mcxnex_test_register(mcxnex_state_t *, char *, int, int);
extern void mcxnex_test_deregister(void);
extern int mcxnex_test_num;
#endif /* FMA_TEST */

#ifdef __cplusplus
}
#endif

#endif	/* _MCXNEX_FM_H */
