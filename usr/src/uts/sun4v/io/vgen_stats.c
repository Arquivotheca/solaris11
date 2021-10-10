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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/stat.h>
#include <sys/vgen_stats.h>

/*
 * A set of common kstat statistics related functions that are
 * used by both vnet and vsw drivers to maintain the statistics specific
 * LDCs.
 */

/*
 * Setup kstats for the LDC statistics.
 * NOTE: the synchronization for the statistics is the
 * responsibility of the caller.
 */
kstat_t *
vgen_setup_kstats(char *ks_mod, int instance,
    char *ks_name, vgen_stats_t *statsp)
{
	kstat_t *ksp;
	vgen_kstats_t *ldckp;
	size_t size;

	size = sizeof (vgen_kstats_t) / sizeof (kstat_named_t);
	ksp = kstat_create(ks_mod, instance, ks_name, "net", KSTAT_TYPE_NAMED,
	    size, 0);
	if (ksp == NULL) {
		return (NULL);
	}

	ldckp = (vgen_kstats_t *)ksp->ks_data;
	kstat_named_init(&ldckp->ipackets,		"ipackets",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->ipackets64,		"ipackets64",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->ierrors,		"ierrors",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->opackets,		"opackets",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->opackets64,		"opackets64",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->oerrors,		"oerrors",
	    KSTAT_DATA_ULONG);


	/* MIB II kstat variables */
	kstat_named_init(&ldckp->rbytes,		"rbytes",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->rbytes64,		"rbytes64",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->obytes,		"obytes",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->obytes64,		"obytes64",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->multircv,		"multircv",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->multixmt,		"multixmt",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->brdcstrcv,		"brdcstrcv",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->brdcstxmt,		"brdcstxmt",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->norcvbuf,		"norcvbuf",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->noxmtbuf,		"noxmtbuf",
	    KSTAT_DATA_ULONG);

	/* Tx stats */
	kstat_named_init(&ldckp->tx_no_desc,		"tx_no_desc",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->tx_qfull,		"tx_qfull",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->tx_pri_fail,		"tx_pri_fail",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->tx_pri_packets,	"tx_pri_packets",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->tx_pri_bytes,		"tx_pri_bytes",
	    KSTAT_DATA_ULONGLONG);

	/* Rx stats */
	kstat_named_init(&ldckp->rx_allocb_fail,	"rx_allocb_fail",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->rx_vio_allocb_fail,	"rx_vio_allocb_fail",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->rx_lost_pkts,		"rx_lost_pkts",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->rx_pri_fail,		"rx_pri_fail",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->rx_pri_packets,	"rx_pri_packets",
	    KSTAT_DATA_ULONGLONG);
	kstat_named_init(&ldckp->rx_pri_bytes,		"rx_pri_bytes",
	    KSTAT_DATA_ULONGLONG);

	/* Interrupt stats */
	kstat_named_init(&ldckp->callbacks,		"callbacks",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_data_msgs_sent,	"dring_data_msgs_sent",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_data_acks_rcvd,	"dring_data_acks_rcvd",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_stopped_acks_rcvd,
	    "dring_stopped_acks_rcvd", KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_data_msgs_rcvd,	"dring_data_msgs_rcvd",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_data_acks_sent,	"dring_data_acks_sent",
	    KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_stopped_acks_sent,
	    "dring_stopped_acks_sent", KSTAT_DATA_ULONG);
	kstat_named_init(&ldckp->dring_mode, "dring_mode", KSTAT_DATA_ULONG);

	ksp->ks_update = vgen_kstat_update;
	ksp->ks_private = (void *)statsp;
	kstat_install(ksp);
	return (ksp);
}

/*
 * Destroy kstats.
 */
void
vgen_destroy_kstats(kstat_t *ksp)
{
	if (ksp != NULL)
		kstat_delete(ksp);
}

/*
 * Update the kstats.
 */
int
vgen_kstat_update(kstat_t *ksp, int rw)
{
	vgen_stats_t *statsp;
	vgen_kstats_t *ldckp;

	statsp = (vgen_stats_t *)ksp->ks_private;
	ldckp = (vgen_kstats_t *)ksp->ks_data;

	if (rw == KSTAT_READ) {
		ldckp->ipackets.value.ul	= (uint32_t)statsp->ipackets +
		    (uint32_t)statsp->rx_pri_packets;
		ldckp->ipackets64.value.ull	= statsp->ipackets +
		    statsp->rx_pri_packets;
		ldckp->ierrors.value.ul		= statsp->ierrors;
		ldckp->opackets.value.ul	= (uint32_t)statsp->opackets +
		    (uint32_t)statsp->tx_pri_packets;
		ldckp->opackets64.value.ull	= statsp->opackets +
		    statsp->tx_pri_packets;
		ldckp->oerrors.value.ul		= statsp->oerrors;

		/*
		 * MIB II kstat variables
		 */
		ldckp->rbytes.value.ul		= (uint32_t)statsp->rbytes +
		    (uint32_t)statsp->rx_pri_bytes;
		ldckp->rbytes64.value.ull	= statsp->rbytes +
		    statsp->rx_pri_bytes;
		ldckp->obytes.value.ul		= (uint32_t)statsp->obytes +
		    (uint32_t)statsp->tx_pri_bytes;
		ldckp->obytes64.value.ull	= statsp->obytes +
		    statsp->tx_pri_bytes;
		ldckp->multircv.value.ul	= statsp->multircv;
		ldckp->multixmt.value.ul	= statsp->multixmt;
		ldckp->brdcstrcv.value.ul	= statsp->brdcstrcv;
		ldckp->brdcstxmt.value.ul	= statsp->brdcstxmt;
		ldckp->norcvbuf.value.ul	= statsp->norcvbuf;
		ldckp->noxmtbuf.value.ul	= statsp->noxmtbuf;

		ldckp->tx_no_desc.value.ul	= statsp->tx_no_desc;
		ldckp->tx_qfull.value.ul	= statsp->tx_qfull;
		ldckp->tx_pri_fail.value.ul	= statsp->tx_pri_fail;
		ldckp->tx_pri_packets.value.ull	= statsp->tx_pri_packets;
		ldckp->tx_pri_bytes.value.ull	= statsp->tx_pri_bytes;

		ldckp->rx_allocb_fail.value.ul	= statsp->rx_allocb_fail;
		ldckp->rx_vio_allocb_fail.value.ul = statsp->rx_vio_allocb_fail;
		ldckp->rx_lost_pkts.value.ul	= statsp->rx_lost_pkts;
		ldckp->rx_pri_fail.value.ul	= statsp->rx_pri_fail;
		ldckp->rx_pri_packets.value.ull	= statsp->rx_pri_packets;
		ldckp->rx_pri_bytes.value.ull	= statsp->rx_pri_bytes;

		ldckp->callbacks.value.ul	= statsp->callbacks;

		ldckp->dring_data_msgs_sent.value.ul =
		    statsp->dring_data_msgs_sent;
		ldckp->dring_data_acks_rcvd.value.ul =
		    statsp->dring_data_acks_rcvd;
		ldckp->dring_stopped_acks_rcvd.value.ul =
		    statsp->dring_stopped_acks_rcvd;
		ldckp->dring_data_msgs_rcvd.value.ul =
		    statsp->dring_data_msgs_rcvd;
		ldckp->dring_data_acks_sent.value.ul =
		    statsp->dring_data_acks_sent;
		ldckp->dring_stopped_acks_sent.value.ul =
		    statsp->dring_stopped_acks_sent;
		ldckp->dring_mode.value.ul = statsp->dring_mode;
	} else {
		statsp->ipackets	= ldckp->ipackets64.value.ull;
		statsp->ierrors		= ldckp->ierrors.value.ul;
		statsp->opackets	= ldckp->opackets64.value.ull;
		statsp->oerrors		= ldckp->oerrors.value.ul;

		/*
		 * MIB II kstat variables
		 */
		statsp->rbytes		= ldckp->rbytes64.value.ull;
		statsp->obytes		= ldckp->obytes64.value.ull;
		statsp->multircv	= ldckp->multircv.value.ul;
		statsp->multixmt	= ldckp->multixmt.value.ul;
		statsp->brdcstrcv	= ldckp->brdcstrcv.value.ul;
		statsp->brdcstxmt	= ldckp->brdcstxmt.value.ul;
		statsp->norcvbuf	= ldckp->norcvbuf.value.ul;
		statsp->noxmtbuf	= ldckp->noxmtbuf.value.ul;

		statsp->tx_no_desc	= ldckp->tx_no_desc.value.ul;
		statsp->tx_qfull	= ldckp->tx_qfull.value.ul;
		statsp->tx_pri_fail	= ldckp->tx_pri_fail.value.ul;
		statsp->tx_pri_packets	= ldckp->tx_pri_packets.value.ull;
		statsp->tx_pri_bytes	= ldckp->tx_pri_bytes.value.ull;

		statsp->rx_allocb_fail	= ldckp->rx_allocb_fail.value.ul;
		statsp->rx_vio_allocb_fail = ldckp->rx_vio_allocb_fail.value.ul;
		statsp->rx_lost_pkts	= ldckp->rx_lost_pkts.value.ul;
		statsp->rx_pri_fail	= ldckp->rx_pri_fail.value.ul;
		statsp->rx_pri_packets	= ldckp->rx_pri_packets.value.ull;
		statsp->rx_pri_bytes	= ldckp->rx_pri_bytes.value.ull;

		statsp->callbacks	= ldckp->callbacks.value.ul;
		statsp->dring_data_msgs_sent =
		    ldckp->dring_data_msgs_sent.value.ul;
		statsp->dring_data_acks_rcvd =
		    ldckp->dring_data_acks_rcvd.value.ul;
		statsp->dring_stopped_acks_rcvd =
		    ldckp->dring_stopped_acks_rcvd.value.ul;
		statsp->dring_data_msgs_rcvd =
		    ldckp->dring_data_msgs_rcvd.value.ul;
		statsp->dring_data_acks_sent =
		    ldckp->dring_data_acks_sent.value.ul;
		statsp->dring_stopped_acks_sent =
		    ldckp->dring_stopped_acks_sent.value.ul;
		statsp->dring_mode = ldckp->dring_mode.value.ul;
	}

	return (0);
}
