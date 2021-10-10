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
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 * All rights reserved.
 */

#ifndef	_CXGE_CXGE_H
#define	_CXGE_CXGE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#define	ETHERJUMBO_MTU	9000

/* cxge flags */
#define	CXGE_PLUMBED	0x1
#define	CXGE_TICKING	0x2

typedef struct cxge_s {
	dev_info_t *dip;
	int instance;
	int mac_version;
	kmutex_t lock;

	kstat_t *config_ksp;

	struct port_info *pi;

	/* Useful knobs */
	int mtu;
	int hw_csum;
	int lso;

	timeout_id_t timer;

	uint32_t flags;
	hrtime_t last_stats_update;

	void *handle;
	void *rx_gh;
} cxge_t, *p_cxge_t;

/*
 * GLD functions.  These should be moved to a private header.
 */
int cxge_register_gld(p_cxge_t);
int cxge_unregister_gld(p_cxge_t);
void cxge_gld_init_ops(struct dev_ops *);
void cxge_gld_fini_ops(struct dev_ops *);
void cxge_gld_link_changed(struct port_info *, int, int, int, int, int);
int cxge_gld_rx(struct sge_qset *, mblk_t *);
int cxge_gld_tx_update(struct sge_qset *);

/*
 * MAC functions.  These should be moved to a private header.
 */
int cxge_mac_available(void);
int cxge_register_mac(p_cxge_t);
int cxge_unregister_mac(p_cxge_t);
void cxge_mac_init_ops(struct dev_ops *);
void cxge_mac_fini_ops(struct dev_ops *);
void cxge_mac_link_changed(struct port_info *, int, int, int, int, int);
int cxge_mac_rx(struct sge_qset *, mblk_t *);
int cxge_mac_tx_update(struct sge_qset *);

/*
 * Miscellaneous.
 */
void cxge_ioctl(p_cxge_t, queue_t *, mblk_t *);
int cxge_init(p_cxge_t);
void cxge_uninit(p_cxge_t);
int cxge_add_multicast(p_cxge_t, const uint8_t *);
int cxge_del_multicast(p_cxge_t, const uint8_t *);
int cxge_tx_lb_queue(p_cxge_t, mblk_t *);
void cxge_rx_mode(struct port_info *);
int cxge_set_coalesce(p_cxge_t, int);
int cxge_set_desc_budget(p_cxge_t, int);
int cxge_set_frame_budget(p_cxge_t, int);

#define	DIP_TO_CXGE(dip) (ddi_get_soft_state(cxge_list, ddi_get_instance(dip)))

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _CXGE_CXGE_H */
