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

#ifndef	_MAC_STAT_H
#define	_MAC_STAT_H

#include <sys/mac_flow_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif
#ifdef	__cplusplus
}
#endif

typedef struct mac_rx_stats_s {
	uint64_t	mrs_ipackets;
	uint64_t	mrs_rbytes;
	uint64_t	mrs_pollcnt;
	uint64_t	mrs_pollbytes;
	uint64_t	mrs_intrcnt;
	uint64_t	mrs_intrbytes;
	uint64_t	mrs_idropcnt;
	uint64_t	mrs_idropbytes;
	uint64_t	mrs_ierrors;
} mac_rx_stats_t;

typedef struct mac_tx_stats_s {
	uint64_t	mts_obytes;
	uint64_t	mts_opackets;
	uint64_t	mts_oerrors;
	/*
	 * Number of times the driver blocks mac Tx due to lack of Tx
	 * descriptors is noted down. Corresponding wakeups from the driver
	 * are also recorded. They should match in a correctly working setup.
	 * An off by one error where the unblock count is 1 less than the
	 * block count will block the transmit for ever.
	 */
	uint64_t	mts_driver_blockcnt;	/* times blocked for Tx descs */
	uint64_t	mts_driver_unblockcnt;	/* unblock calls from driver */
	/*
	 * Number of times the mac blocks the client (typically IP) due to a
	 * queue full condition is noted down. Corresponding wakeups from the
	 * mac are also recorded. They should match in a correctly working
	 * setup. An off by one error where the unblock count is 1 less than
	 * the block count will block the transmit for ever.
	 */
	uint64_t	mts_client_blockcnt;	/* times blocked for Tx */
	uint64_t	mts_client_unblockcnt;	/* unblock calls from mac */
	uint64_t	mts_odropcnt;
	uint64_t	mts_odropbytes;
} mac_tx_stats_t;

typedef struct mac_misc_stats_s {
	uint64_t	mms_rxlocalcnt;
	uint64_t	mms_rxlocalbytes;
	uint64_t	mms_txlocalcnt;
	uint64_t	mms_txlocalbytes;
	uint64_t	mms_multircv;
	uint64_t	mms_brdcstrcv;
	uint64_t	mms_multixmt;
	uint64_t	mms_brdcstxmt;
	uint64_t	mms_multircvbytes;
	uint64_t	mms_brdcstrcvbytes;
	uint64_t	mms_multixmtbytes;
	uint64_t	mms_brdcstxmtbytes;
	uint64_t	mms_txerrors; 	/* vid_check, tag needed errors */

	/* link protection stats */
	uint64_t	mms_macspoofed;
	uint64_t	mms_ipspoofed;
	uint64_t	mms_dhcpspoofed;
	uint64_t	mms_restricted;
	uint64_t	mms_dhcpdropped;
} mac_misc_stats_t;

typedef struct mac_client_stats_s {
	mac_rx_stats_t		ms_rx;
	mac_tx_stats_t		ms_tx;
	mac_misc_stats_t	ms_misc;
} mac_client_stats_t;

/* XXX Reorg code to avoid forward declarations */
struct mac_ring_s;
struct mac_client_impl_s;
struct mac_impl_s;

#define	MR_RX_STAT(r, s)	((r)->mr_rx_stat.mrs_##s)
#define	MR_TX_STAT(r, s)	((r)->mr_tx_stat.mts_##s)

#define	MCIP_RX_STAT(m, s)	((m)->mci_stat.ms_rx.mrs_##s)
#define	MCIP_TX_STAT(m, s)	((m)->mci_stat.ms_tx.mts_##s)
#define	MCIP_MISC_STAT(m, s)	((m)->mci_stat.ms_misc.mms_##s)

#define	MCIP_STAT_UPDATE(m, s, c) {					\
	((mac_client_impl_t *)(m))->mci_stat.ms_misc.mms_##s		\
	+= ((uint64_t)(c));						\
}

extern void	mac_ring_stat_create(struct mac_ring_s *);
extern void	mac_ring_stat_delete(struct mac_ring_s *);

extern void	mac_hwlane_stat_create(struct mac_impl_s *,
		    struct mac_ring_s *);
extern void	mac_hwlane_stat_delete(struct mac_ring_s *);

extern void	mac_link_stat_create(struct mac_client_impl_s *);
extern void	mac_link_stat_delete(struct mac_client_impl_s *);

extern void	mac_link_stat_rename(struct mac_client_impl_s *);
extern void	mac_pseudo_ring_stat_rename(struct mac_impl_s *);

extern void	mac_driver_stat_create(struct mac_impl_s *);
extern void	mac_driver_stat_delete(struct mac_impl_s *);
extern uint64_t	mac_driver_stat_default(struct mac_impl_s *, uint_t);

extern uint64_t mac_rx_ring_stat_get(void *, uint_t);
extern uint64_t mac_tx_ring_stat_get(void *, uint_t);

#endif	/* _MAC_STAT_H */
