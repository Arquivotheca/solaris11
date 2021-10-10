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

#ifndef	__PCIEV_IMPL_H__
#define	__PCIEV_IMPL_H__

#include <sys/pciev_channel.h>
#include <sys/sunddi.h>
#include <sys/pcie.h>
#include <sys/pciev.h>

void pciv_proxy_init(void);
void pciv_proxy_fini(void);

/*
 * These flags are kept in io_flag.
 */
#define	PCIV_INIT	0x0001	/* is initialized */
#define	PCIV_BUSY	0x0002	/* is using in transaction */
#define	PCIV_DONE	0x0004	/* transaction finished */
#define	PCIV_ERROR	0x0008	/* transaction aborted */

typedef struct pciv_assigned_dev {
	char *devpath;
	pcie_fn_type_t type;
	dom_id_t domain_id;
	boolean_t marked;	/* If false, device has not yet been marked */
				/* as assigned */
	dev_info_t *dip;
	struct pciv_assigned_dev *next;
} pciv_assigned_dev_t;

extern pciv_assigned_dev_t *pciv_dev_cache;

/* PCIV packet implementation definitions */
typedef struct pciv_pkt {
	struct pciv_pkt	*next;		/* next packet on the chain */
	pciv_pkt_hdr_t	hdr;		/* packets header */
	uint64_t	src_domain;	/* source domain */
	uint64_t	dst_domain;	/* dest domain */
	void*		buf;		/* payload of the buf */
	uint16_t	io_flag;	/* io flags see define above */
	int		io_err;		/* expanded error field */
	kcondvar_t	io_cv;		/* condvar for io wait */
	kmutex_t	io_lock;
	void		(*io_cb)();	/* the callback after the io done */
	void		(*buf_cb)();	/* the callback for nonblocking IO */
	caddr_t		cb_arg;		/* callback arg */
	void*		priv_data;
} pciv_pkt_t;

typedef void (*pciv_io_cb_t)(caddr_t arg, pciv_pkt_t *pkt);

pciv_pkt_t *pciv_pkt_alloc(caddr_t buf, size_t nbyte, int flag);
void pciv_pkt_free(pciv_pkt_t *pkt);

/* Opaque handle types */
typedef struct __pciv_handle *pciv_handle_t;

/* Entry points for proxy driver */
typedef int (*pciv_proxy_check_t)(void *state);
typedef void (*pciv_proxy_tx_t)(void *state, pciv_pkt_t *pkt);
void pciv_proxy_rx(pciv_handle_t h, pciv_pkt_t *pkt_chain);

/* Proxy driver register definitions  */
typedef struct pciv_proxy_reg {
	dev_info_t		*dip;	/* the dip of driver */
	void			*state;	/* the driver state */
	dom_id_t		domid;	/* Domain id */
	pciv_proxy_check_t	check;	/* status check entry point */
	pciv_proxy_tx_t		tx;	/* driver TX entry point */
} pciv_proxy_reg_t;

int pciv_proxy_register(pciv_proxy_reg_t *regp, pciv_handle_t *h);
void pciv_proxy_unregister(pciv_handle_t h);

/* PCIv framework version binding to low-layer version */
typedef struct pciv_version {
	uint16_t	proxy_major;
	uint16_t	proxy_minor;
	uint16_t	pciv_major;
	uint16_t	pciv_minor;
} pciv_version_t;

/* Virtual proxy drivers instances maintained by common layer */
typedef struct pciv_proxy {
	dev_info_t		*dip;	/* the dip of driver */
	void			*state;	/* the driver state */
	dom_id_t		domid;	/* the domain id */
	pciv_proxy_check_t	check;	/* status check entry point */
	pciv_proxy_tx_t		tx;	/* driver TX entry point */
	ddi_taskq_t		*rx_taskq[PCIV_PKT_TYPE_MAX]; /* rx taskq */
	pciv_version_t		*version;
} pciv_proxy_t;

int pciv_set_version(pciv_handle_t h, uint16_t proxy_major,
    uint16_t proxy_minor);
void pciv_unset_version(pciv_handle_t h);

/* Interfaces used by other driver or modules */
int pciv_register_notify(dev_info_t *dip);
int pciv_unregister_notify(dev_info_t *dip);
int pciv_domain_send(dev_info_t *dip, dom_id_t dst_domain,
    pciv_pkt_type_t pkt_type, caddr_t buf, size_t nbyte,
    buf_cb_t buf_cb, caddr_t cb_arg);

#define	MAXTASKQNAME		256
#define	PCIV_HIGH_PIL_QSIZE	64
#define	PCIV_MAX_BUF_SIZE	8192	/* Must be multiple times of PAGESIZE */

/* Per Root Complex tx queue */
typedef struct pciv_hpil_queue {
	pciv_pkt_t	*pkt[PCIV_HIGH_PIL_QSIZE];
	uint16_t	qused;
	uint16_t	qhead;		/* Point to head pkt */
	uint16_t	qtail;		/* Point to tail pkt */
	uint_t		qfailed;	/* # of overflows */
} pciv_hpil_queue_t;

/* Per Root Complex taskqs and sofint */
typedef struct pciv_tx_taskq {
	ddi_taskq_t		*lb_taskq;		/* loop back taskq */
	ddi_taskq_t		*hpil_taskq;		/* intr taskq */
	boolean_t		hpil_taskq_running;
	boolean_t		hpil_taskq_exit;
	kmutex_t		hpil_taskq_lock;
	kcondvar_t		hpil_taskq_cv;
	ddi_softint_handle_t	hpil_softint_hdl;
	pciv_hpil_queue_t	hpil_queue;
	ddi_taskq_t		*tx_taskq[PCIV_PKT_TYPE_MAX];	/* tx taskq */
} pciv_tx_taskq_t;

int pciv_tx_taskq_create(dev_info_t *dip, uint32_t task_id,
    boolean_t loopback_mode);
void pciv_tx_taskq_destroy(dev_info_t *dip);

uint32_t pciv_get_rc_addr(dev_info_t *dip);
dev_info_t *pciv_get_vf_dip(dev_info_t *pf_dip, int vf_idx);
int pciv_get_vf_idx(dev_info_t *vf_dip);

#define	PCIV_SEND(dip, dst_dip, domain_id, req, remote) \
	    (remote ? pciv_pfvf_remote_send(dip, dst_dip, domain_id, req) : \
	    pciv_pfvf_local_send(dip, dst_dip, req))

#endif	/* __PCIEV_IMPL_H__ */
