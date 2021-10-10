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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/sunddi.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/sdt.h>
#include <net/pfkeyv2.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/ipsec_impl.h>
#include <inet/ipdrop.h>
#include <inet/kstatcom.h>

/*
 * Packet drop facility.
 */

static struct ip_dropstats ip_dropstats_template = {
	/* TCP IPsec drop statistics. */
	{ "tcp_clear",			KSTAT_DATA_UINT64, 0 },
	{ "tcp_secure",			KSTAT_DATA_UINT64, 0 },
	{ "tcp_mismatch",		KSTAT_DATA_UINT64, 0 },
	{ "tcp_ipsec_alloc",		KSTAT_DATA_UINT64, 0 },
	/* SADB-specific drop statistics. */
	{ "sadb_inlarval_timeout",	KSTAT_DATA_UINT64, 0 },
	{ "sadb_inlarval_replace",	KSTAT_DATA_UINT64, 0 },
	{ "sadb_inidle_timeout",	KSTAT_DATA_UINT64, 0 },
	{ "sadb_inidle_overflow",	KSTAT_DATA_UINT64, 0 },
	{ "sadb_acquire_nomem",		KSTAT_DATA_UINT64, 0 },
	{ "sadb_acquire_toofull",	KSTAT_DATA_UINT64, 0 },
	{ "sadb_acquire_timeout",	KSTAT_DATA_UINT64, 0 },
	/* SPD drop statistics. */
	{ "spd_ahesp_diffid",		KSTAT_DATA_UINT64, 0 },
	{ "spd_loopback_mismatch",	KSTAT_DATA_UINT64, 0 },
	{ "spd_explicit",		KSTAT_DATA_UINT64, 0 },
	{ "spd_got_secure",		KSTAT_DATA_UINT64, 0 },
	{ "spd_got_clear",		KSTAT_DATA_UINT64, 0 },
	{ "spd_bad_ahalg",		KSTAT_DATA_UINT64, 0 },
	{ "spd_got_ah",			KSTAT_DATA_UINT64, 0 },
	{ "spd_bad_espalg",		KSTAT_DATA_UINT64, 0 },
	{ "spd_bad_espaalg",		KSTAT_DATA_UINT64, 0 },
	{ "spd_got_esp",		KSTAT_DATA_UINT64, 0 },
	{ "spd_got_selfencap",		KSTAT_DATA_UINT64, 0 },
	{ "spd_bad_selfencap",		KSTAT_DATA_UINT64, 0 },
	{ "spd_nomem",			KSTAT_DATA_UINT64, 0 },
	{ "spd_ah_badid",		KSTAT_DATA_UINT64, 0 },
	{ "spd_esp_badid",		KSTAT_DATA_UINT64, 0 },
	{ "spd_ah_innermismatch",	KSTAT_DATA_UINT64, 0 },
	{ "spd_esp_innermismatch",	KSTAT_DATA_UINT64, 0 },
	{ "spd_no_policy",		KSTAT_DATA_UINT64, 0 },
	{ "spd_malformed_packet",	KSTAT_DATA_UINT64, 0 },
	{ "spd_malformed_frag",		KSTAT_DATA_UINT64, 0 },
	{ "spd_overlap_frag",		KSTAT_DATA_UINT64, 0 },
	{ "spd_evil_frag",		KSTAT_DATA_UINT64, 0 },
	{ "spd_max_frags",		KSTAT_DATA_UINT64, 0 },
	{ "spd_expired_frags",		KSTAT_DATA_UINT64, 0 },
	/* ESP-specific drop statistics. */
	{ "esp_nomem",			KSTAT_DATA_UINT64, 0 },
	{ "esp_no_sa",			KSTAT_DATA_UINT64, 0 },
	{ "esp_early_replay",		KSTAT_DATA_UINT64, 0 },
	{ "esp_replay",			KSTAT_DATA_UINT64, 0 },
	{ "esp_bytes_expire",		KSTAT_DATA_UINT64, 0 },
	{ "esp_bad_padlen",		KSTAT_DATA_UINT64, 0 },
	{ "esp_bad_padding",		KSTAT_DATA_UINT64, 0 },
	{ "esp_bad_auth",		KSTAT_DATA_UINT64, 0 },
	{ "esp_crypto_failed",		KSTAT_DATA_UINT64, 0 },
	{ "esp_icmp",			KSTAT_DATA_UINT64, 0 },
	{ "esp_nat_t_ipsec",		KSTAT_DATA_UINT64, 0 },
	{ "esp_nat_t_ka",		KSTAT_DATA_UINT64, 0 },
	{ "esp_iv_wrap",		KSTAT_DATA_UINT64, 0 },
	/* AH-specific drop statistics. */
	{ "ah_nomem",			KSTAT_DATA_UINT64, 0 },
	{ "ah_bad_v6_hdrs",		KSTAT_DATA_UINT64, 0 },
	{ "ah_bad_v4_opts",		KSTAT_DATA_UINT64, 0 },
	{ "ah_no_sa",			KSTAT_DATA_UINT64, 0 },
	{ "ah_bad_length",		KSTAT_DATA_UINT64, 0 },
	{ "ah_bad_auth",		KSTAT_DATA_UINT64, 0 },
	{ "ah_crypto_failed",		KSTAT_DATA_UINT64, 0 },
	{ "ah_early_replay",		KSTAT_DATA_UINT64, 0 },
	{ "ah_replay",			KSTAT_DATA_UINT64, 0 },
	{ "ah_bytes_expire",		KSTAT_DATA_UINT64, 0 },
	/* IP-specific drop statistics. */
	{ "ip_ipsec_not_loaded",	KSTAT_DATA_UINT64, 0 },
};

/*
 * Initialize drop facility kstats.
 */
void
ip_drop_init(ipsec_stack_t *ipss)
{
	ipss->ipsec_ip_drop_kstat = kstat_create_netstack("ip", 0, "ipdrop",
	    "net", KSTAT_TYPE_NAMED, NUM_OF_FIELDS(struct ip_dropstats),
	    0, ipss->ipsec_netstack->netstack_stackid);

	if (ipss->ipsec_ip_drop_kstat == NULL ||
	    ipss->ipsec_ip_drop_kstat->ks_data == NULL)
		return;

	/*
	 * Note: here ipss->ipsec_ip_drop_types is initialized, however,
	 * if the previous kstat_create_netstack failed, it will remain
	 * NULL. Note this is done for all stack instances, so it *could*
	 * be NULL. Hence a non-NULL checking is added where
	 * ipss->ipsec_ip_drop_types is used. This checking is hidden in
	 * the DROPPER macro.
	 */
	ipss->ipsec_ip_drop_types = ipss->ipsec_ip_drop_kstat->ks_data;
	*ipss->ipsec_ip_drop_types = ip_dropstats_template;

	kstat_install(ipss->ipsec_ip_drop_kstat);
}

void
ip_drop_destroy(ipsec_stack_t *ipss)
{
	kstat_delete_netstack(ipss->ipsec_ip_drop_kstat,
	    ipss->ipsec_netstack->netstack_stackid);
	ipss->ipsec_ip_drop_kstat = NULL;
	ipss->ipsec_ip_drop_types = NULL;
}

/*
 * Register a packet dropper.
 */
void
ip_drop_register(ipdropper_t *ipd, char *name)
{
	if (ipd->ipd_name != NULL) {
		cmn_err(CE_WARN,
		    "ip_drop_register: ipdropper %s already registered with %s",
		    name, ipd->ipd_name);
		return;
	}

	/* Assume that name is reasonable in length.  This isn't user-land. */
	ipd->ipd_name = kmem_alloc(strlen(name) + 1, KM_SLEEP);
	(void) strcpy(ipd->ipd_name, name);
}

/*
 * Un-register a packet dropper.
 */
void
ip_drop_unregister(ipdropper_t *ipd)
{
	if (ipd->ipd_name == NULL) {
		cmn_err(CE_WARN,
		    "ip_drop_unregister: not registered (%p)\n",
		    (void *)ipd);
		return;
	}
	kmem_free(ipd->ipd_name, strlen(ipd->ipd_name) + 1);

	ipd->ipd_name = NULL;
}

/*
 * Actually drop a packet.  Many things could happen here, but at the least,
 * the packet will be freemsg()ed.
 */
void
ip_drop_packet(mblk_t *mp, boolean_t inbound, ill_t *ill,
    struct kstat_named *counter, ipdropper_t *who_called)
{
	char *str;

	if (mp == NULL) {
		/*
		 * Return immediately - NULL packets should not affect any
		 * statistics.
		 */
		return;
	}

	ASSERT(mp->b_datap->db_type == M_DATA);

	/* Increment the bean counter, if available. */
	if (counter != NULL) {
		switch (counter->data_type) {
		case KSTAT_DATA_INT32:
			counter->value.i32++;
			break;
		case KSTAT_DATA_UINT32:
			counter->value.ui32++;
			break;
		case KSTAT_DATA_INT64:
			counter->value.i64++;
			break;
		case KSTAT_DATA_UINT64:
			counter->value.ui64++;
			break;
		/* Other types we can't handle for now. */
		}
	}

	if (counter != NULL)
		str = counter->name;
	else if (who_called != NULL)
		str = who_called->ipd_name;
	else
		str = "Unspecified IPsec drop";

	if (inbound)
		ip_drop_input(str, mp, ill);
	else
		ip_drop_output(str, mp, ill);

	/* TODO: queue the packet onto a snoop-friendly queue. */

	/*
	 * ASSERT this isn't a b_next linked mblk chain where a
	 * chained dropper should be used instead
	 */
	ASSERT(mp->b_prev == NULL && mp->b_next == NULL);
	freemsg(mp);
}

/*
 * This is just a convinient place for dtrace to see dropped packets
 */
/*ARGSUSED*/
void
ip_drop_input(char *str, mblk_t *mp, ill_t *ill)
{
	if (mp == NULL)
		return;

	if (IPH_HDR_VERSION(mp->b_rptr) == IPV4_VERSION) {
		ipha_t *ipha = (ipha_t *)mp->b_rptr;

		DTRACE_IP7(drop__in, mblk_t *, mp, conn_t *, NULL, void_ip_t *,
		    ipha, __dtrace_ipsr_ill_t *, ill, ipha_t *, ipha,
		    ip6_t *, NULL, int, 0);
	} else {
		ip6_t *ip6h = (ip6_t *)mp->b_rptr;

		DTRACE_IP7(drop__in, mblk_t *, mp, conn_t *, NULL, void_ip_t *,
		    ip6h, __dtrace_ipsr_ill_t *, ill, ipha_t *, NULL,
		    ip6_t *, ip6h, int, 0);
	}
}

/*ARGSUSED*/
void
ip_drop_output(char *str, mblk_t *mp, ill_t *ill)
{
	if (mp == NULL)
		return;

	if (IPH_HDR_VERSION(mp->b_rptr) == IPV4_VERSION) {
		ipha_t *ipha = (ipha_t *)mp->b_rptr;

		DTRACE_IP7(drop__out, mblk_t *, mp, conn_t *, NULL, void_ip_t *,
		    ipha, __dtrace_ipsr_ill_t *, ill, ipha_t *, ipha,
		    ip6_t *, NULL, int, 0);
	} else {
		ip6_t *ip6h = (ip6_t *)mp->b_rptr;

		DTRACE_IP7(drop__out, mblk_t *, mp, conn_t *, NULL, void_ip_t *,
		    ip6h, __dtrace_ipsr_ill_t *, ill, ipha_t *, NULL,
		    ip6_t *, ip6h, int, 0);
	}
}
