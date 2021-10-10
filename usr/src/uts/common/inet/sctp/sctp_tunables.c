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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/sctp/sctp_stack.h>
#include <inet/sctp/sctp_impl.h>
#include <sys/sunddi.h>

/* default maximum size of SCTP socket buffer */
#define	SCTP_DEF_MAX_SOCKBUF	(1024 * 1024)

/* Max size IP datagram is 64k - 1 */
#define	SCTP_MSS_MAX_IPV4 (IP_MAXPACKET - (sizeof (ipha_t) + \
					sizeof (sctp_hdr_t)))
#define	SCTP_MSS_MAX_IPV6 (IP_MAXPACKET - (sizeof (ip6_t) + \
					sizeof (sctp_hdr_t)))
/* Max of the above */
#define	SCTP_MSS_MAX	SCTP_MSS_MAX_IPV4

static char sctp_cong_builtin_alg[] = TCPCONG_ALG_BUILTIN;

/*
 * returns the current list of listener limit configuration.
 */
/* ARGSUSED */
static int
sctp_listener_conf_get(void *cbarg, mod_prop_info_t *pinfo, const char *ifname,
    void *val, uint_t *psize, uint_t ptype)
{
	sctp_stack_t	*sctps = (sctp_stack_t *)cbarg;
	sctp_listener_t	*sl;
	char		*pval = val;
	size_t		nbytes = 0, tbytes = 0;
	uint_t		size;
	int		err = 0;

	bzero(pval, *psize);
	size = *psize;

	if (ptype != MOD_PROP_ACTIVE)
		return (EINVAL);

	mutex_enter(&sctps->sctps_listener_conf_lock);
	for (sl = list_head(&sctps->sctps_listener_conf); sl != NULL;
	    sl = list_next(&sctps->sctps_listener_conf, sl)) {
		if (*psize == size) {
			nbytes = snprintf(pval, size, "%d:%d",  sl->sl_port,
			    sl->sl_ratio);
		} else {
			nbytes = snprintf(pval, size, ",%d:%d",  sl->sl_port,
			    sl->sl_ratio);
		}
		tbytes += nbytes;
		if (tbytes >= *psize) {
			/*
			 * insufficient buffer space, lets determine
			 * how much buffer is actually needed
			 */
			pval = NULL;
			size = 0;
		} else {
			size -= nbytes;
			pval += nbytes;
		}
	}
	mutex_exit(&sctps->sctps_listener_conf_lock);
	*psize = tbytes;
	return (err);
}

/*
 * add a new listener limit configuration.
 */
/* ARGSUSED */
static int
sctp_listener_conf_add(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* pval, uint_t flags)
{
	sctp_listener_t	*new_sl;
	sctp_listener_t	*sl;
	long		lport;
	long		ratio;
	char		*colon;
	sctp_stack_t	*sctps = (sctp_stack_t *)cbarg;

	if (flags & MOD_PROP_DEFAULT)
		return (ENOTSUP);

	if (ddi_strtol(pval, &colon, 10, &lport) != 0 || lport <= 0 ||
	    lport > USHRT_MAX || *colon != ':') {
		return (EINVAL);
	}
	if (ddi_strtol(colon + 1, NULL, 10, &ratio) != 0 || ratio <= 0)
		return (EINVAL);

	mutex_enter(&sctps->sctps_listener_conf_lock);
	for (sl = list_head(&sctps->sctps_listener_conf); sl != NULL;
	    sl = list_next(&sctps->sctps_listener_conf, sl)) {
		/* There is an existing entry, so update its ratio value. */
		if (sl->sl_port == lport) {
			sl->sl_ratio = ratio;
			mutex_exit(&sctps->sctps_listener_conf_lock);
			return (0);
		}
	}

	if ((new_sl = kmem_alloc(sizeof (sctp_listener_t), KM_NOSLEEP)) ==
	    NULL) {
		mutex_exit(&sctps->sctps_listener_conf_lock);
		return (ENOMEM);
	}

	new_sl->sl_port = lport;
	new_sl->sl_ratio = ratio;
	list_insert_tail(&sctps->sctps_listener_conf, new_sl);
	mutex_exit(&sctps->sctps_listener_conf_lock);
	return (0);
}

/*
 * remove a listener limit configuration.
 */
/* ARGSUSED */
static int
sctp_listener_conf_del(void *cbarg, cred_t *cr, mod_prop_info_t *pinfo,
    const char *ifname, const void* pval, uint_t flags)
{
	sctp_listener_t	*sl;
	long		lport;
	sctp_stack_t	*sctps = (sctp_stack_t *)cbarg;

	if (flags & MOD_PROP_DEFAULT)
		return (ENOTSUP);

	if (ddi_strtol(pval, NULL, 10, &lport) != 0 || lport <= 0 ||
	    lport > USHRT_MAX) {
		return (EINVAL);
	}
	mutex_enter(&sctps->sctps_listener_conf_lock);
	for (sl = list_head(&sctps->sctps_listener_conf); sl != NULL;
	    sl = list_next(&sctps->sctps_listener_conf, sl)) {
		if (sl->sl_port == lport) {
			list_remove(&sctps->sctps_listener_conf, sl);
			mutex_exit(&sctps->sctps_listener_conf_lock);
			kmem_free(sl, sizeof (sctp_listener_t));
			return (0);
		}
	}
	mutex_exit(&sctps->sctps_listener_conf_lock);
	return (ESRCH);
}

/*
 * All of these are alterable, within the min/max values given, at run time.
 *
 * Note: All those tunables which do not start with "_" are Committed and
 * therefore are public. See PSARC 2010/080.
 */
mod_prop_info_t sctp_propinfo_tbl[] = {
	{ "_max_init_retr", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 128, 8}, {8} },

	{ "_pa_max_retr", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 128, 10}, {10} },

	{ "_pp_max_retr", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 128, 5}, {5} },

	{ "_cwnd_max", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {128, (1<<30), 1*MB}, {1*MB} },

	{ "smallest_nonpriv_port", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1024, (32*1024), 1024}, {1024} },

	{ "_ipv4_ttl", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 255, 64}, {64} },

	{ "_heartbeat_interval", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1*DAYS, 30*SECONDS}, {30*SECONDS} },

	{ "_initial_mtu", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {68, 65535, 1500}, {1500} },

	{ "_mtu_probe_interval", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1*DAYS, 10*MINUTES}, {10*MINUTES} },

	{ "_new_secret_interval", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 1*DAYS, 2*MINUTES}, {2*MINUTES} },

	/* tunable - 10 */
	{ "_deferred_ack_interval", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {10*MS, 1*MINUTES, 100*MS}, {100*MS} },

	{ "_snd_lowat_fraction", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 16, 0}, {0} },

	{ "_ignore_path_mtu", MOD_PROTO_SCTP,
	    mod_set_boolean, mod_get_boolean,
	    {B_FALSE}, {B_FALSE} },

	{ "_initial_ssthresh", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1024, UINT32_MAX, 1*MB}, {1*MB} },

	{ MOD_PROPNAME_SMALL_ANONPORT, MOD_PROTO_SCTP,
	    mod_set_anon, mod_get_anon,
	    {1024, ULP_DEF_LARGE_ANONPORT, ULP_DEF_SMALL_ANONPORT},
	    {ULP_DEF_SMALL_ANONPORT} },

	{ MOD_PROPNAME_LARGE_ANONPORT, MOD_PROTO_SCTP,
	    mod_set_anon, mod_get_anon,
	    {ULP_DEF_SMALL_ANONPORT, ULP_MAX_PORT, ULP_DEF_LARGE_ANONPORT},
	    {ULP_DEF_LARGE_ANONPORT} },

	{ MOD_PROPNAME_SEND_BUF, MOD_PROTO_SCTP,
	    mod_set_buf, mod_get_buf,
	    {SCTP_XMIT_LOWATER, SCTP_DEF_MAX_SOCKBUF, SCTP_XMIT_HIWATER},
	    {SCTP_XMIT_HIWATER} },

	{ "_xmit_lowat", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {SCTP_XMIT_LOWATER,  (1<<30),  SCTP_XMIT_LOWATER},
	    {SCTP_XMIT_LOWATER} },

	{ MOD_PROPNAME_RECV_BUF, MOD_PROTO_SCTP,
	    mod_set_buf, mod_get_buf,
	    {SCTP_RECV_LOWATER,  SCTP_DEF_MAX_SOCKBUF,  SCTP_RECV_HIWATER},
	    {SCTP_RECV_HIWATER} },

	{ MOD_PROPNAME_MAX_BUF, MOD_PROTO_SCTP,
	    mod_set_max_buf, mod_get_max_buf,
	    {8192, (1<<30), SCTP_DEF_MAX_SOCKBUF}, {SCTP_DEF_MAX_SOCKBUF} },

	/* tunable - 20 */
	{ "_rtt_updates", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 65536, 20}, {20} },

	{ "_ipv6_hoplimit", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, IPV6_MAX_HOPS, IPV6_DEFAULT_HOPS}, {IPV6_DEFAULT_HOPS} },

	{ "_rto_min", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {500*MS, 60*SECONDS, 1*SECONDS}, {1*SECONDS} },

	{ "_rto_max", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1*SECONDS, 60000*SECONDS, 60*SECONDS}, {60*SECONDS} },

	{ "_rto_initial", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1*SECONDS, 60000*SECONDS, 3*SECONDS}, {3*SECONDS} },

	{ "_cookie_life", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {10*MS, 60000*SECONDS, 60*SECONDS}, {60*SECONDS} },

	{ "_max_in_streams", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, UINT16_MAX, 32}, {32} },

	{ "_initial_out_streams", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, UINT16_MAX, 32}, {32} },

	{ "_shutack_wait_bound", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 300*SECONDS, 60*SECONDS}, {60*SECONDS} },

	{ "_maxburst", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {2, 8, 4}, {4} },

	/* tunable - 30 */
	{ "_addip_enabled", MOD_PROTO_SCTP,
	    mod_set_boolean, mod_get_boolean,
	    {B_FALSE}, {B_FALSE} },

	{ "_recv_hiwat_minmss", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 65536, 4}, {4} },

	{ "_slow_start_initial", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 16, 4}, {4} },

	{ "_slow_start_after_idle", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 16384, 4}, {4} },

	{ "_prsctp_enabled", MOD_PROTO_SCTP,
	    mod_set_boolean, mod_get_boolean,
	    {B_TRUE}, {B_TRUE} },

	{ "_fast_rxt_thresh", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 10000, 3}, {3} },

	{ "_deferred_acks_max", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    { 1, 16, 2}, {2} },

	/*
	 * sctp_wroff_xtra is the extra space in front of SCTP/IP header
	 * for link layer header.  It has to be a multiple of 8.
	 */
	{ "_wroff_xtra", MOD_PROTO_SCTP,
	    mod_set_aligned, mod_get_uint32,
	    {0, 256, 32}, {32} },

	{ "_naglim_def", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, (64*1024)-1, (4*1024)-1}, {(4*1024)-1} },

	{ "_faddr_policy", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {SCTP_FADDR_POLICY_ROTATE, SCTP_FADDR_POLICY_PREF_PRIMARY,
	    SCTP_FADDR_POLICY_ROTATE}, {SCTP_FADDR_POLICY_ROTATE} },

	/* tunable - 40 */
	{ "_faddr_sticky_prim_cnt", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, UINT16_MAX, 1}, {1} },

	{ "_faddr_pref_prim_cnt", MOD_PROTO_SCTP,
	    mod_set_uint32, mod_get_uint32,
	    {1, UINT16_MAX, 2}, {2} },

	{ "cong_default", MOD_PROTO_SCTP,
	    mod_set_cong_default, mod_get_string,
	    {.mpi_sval = sctp_cong_builtin_alg},
	    {.mpi_def_sval = sctp_cong_builtin_alg} },

	{ "cong_enabled", MOD_PROTO_SCTP,
	    mod_set_cong_enabled, mod_get_cong_enabled,
	    {.mpi_sval = sctp_cong_builtin_alg},
	    {.mpi_def_sval = sctp_cong_builtin_alg} },

	{ "extra_priv_ports", MOD_PROTO_SCTP,
	    mod_set_extra_privports, mod_get_extra_privports,
	    {1, ULP_MAX_PORT, 0}, {0} },

	{ "_listener_limit_conf", MOD_PROTO_SCTP,
	    NULL, sctp_listener_conf_get, {0}, {0} },

	{ "_listener_limit_conf_add", MOD_PROTO_SCTP,
	    sctp_listener_conf_add, NULL, {0}, {0} },

	{ "_listener_limit_conf_del", MOD_PROTO_SCTP,
	    sctp_listener_conf_del, NULL, {0}, {0} },

	{ "?", MOD_PROTO_SCTP, NULL, mod_get_allprop, {0}, {0} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

int sctp_propinfo_count = A_CNT(sctp_propinfo_tbl);
