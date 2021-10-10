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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright (c) 1990 Mentat Inc. */

#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/udp_impl.h>
#include <sys/sunddi.h>

/* default maximum size of UDP socket buffer */
#define	UDP_DEF_MAX_SOCKBUF	(2 * 1024 * 1024)

/*
 * All of these are alterable, within the min/max values given, at run time.
 *
 * Note: All those tunables which do not start with "_" are Committed and
 * therefore are public. See PSARC 2010/080.
 */
mod_prop_info_t udp_propinfo_tbl[] = {
	/* tunable - 0 */
	{ "_wroff_extra", MOD_PROTO_UDP,
	    mod_set_uint32, mod_get_uint32,
	    {0, 256, 32}, {32} },

	{ "_ipv4_ttl", MOD_PROTO_UDP,
	    mod_set_uint32, mod_get_uint32,
	    {1, 255, 255}, {255} },

	{ "_ipv6_hoplimit", MOD_PROTO_UDP,
	    mod_set_uint32, mod_get_uint32,
	    {0, IPV6_MAX_HOPS, IPV6_DEFAULT_HOPS}, {IPV6_DEFAULT_HOPS} },

	{ "smallest_nonpriv_port", MOD_PROTO_UDP,
	    mod_set_uint32, mod_get_uint32,
	    {1024, (32 * 1024), 1024}, {1024} },

	{ "_do_checksum", MOD_PROTO_UDP,
	    mod_set_boolean, mod_get_boolean,
	    {B_TRUE}, {B_TRUE} },

	{ MOD_PROPNAME_SMALL_ANONPORT, MOD_PROTO_UDP,
	    mod_set_anon, mod_get_anon,
	    {1024, ULP_DEF_LARGE_ANONPORT, ULP_DEF_SMALL_ANONPORT},
	    {ULP_DEF_SMALL_ANONPORT} },

	{ MOD_PROPNAME_LARGE_ANONPORT, MOD_PROTO_UDP,
	    mod_set_anon, mod_get_anon,
	    {ULP_DEF_SMALL_ANONPORT, ULP_MAX_PORT, ULP_DEF_LARGE_ANONPORT},
	    {ULP_DEF_LARGE_ANONPORT} },

	{ MOD_PROPNAME_SEND_BUF, MOD_PROTO_UDP,
	    mod_set_buf, mod_get_buf,
	    {UDP_XMIT_LOWATER, UDP_DEF_MAX_SOCKBUF, UDP_XMIT_HIWATER},
	    {UDP_XMIT_HIWATER} },

	{ "_xmit_lowat", MOD_PROTO_UDP,
	    mod_set_uint32, mod_get_uint32,
	    {0, (1<<30), UDP_XMIT_LOWATER},
	    {UDP_XMIT_LOWATER} },

	{ MOD_PROPNAME_RECV_BUF, MOD_PROTO_UDP,
	    mod_set_buf, mod_get_buf,
	    {UDP_RECV_LOWATER, UDP_DEF_MAX_SOCKBUF, UDP_RECV_HIWATER},
	    {UDP_RECV_HIWATER} },

	/* tunable - 10 */
	{ MOD_PROPNAME_MAX_BUF, MOD_PROTO_UDP,
	    mod_set_max_buf, mod_get_max_buf,
	    {65536, (1<<30), UDP_DEF_MAX_SOCKBUF}, {UDP_DEF_MAX_SOCKBUF} },

	{ "_pmtu_discovery", MOD_PROTO_UDP,
	    mod_set_boolean, mod_get_boolean,
	    {B_FALSE}, {B_FALSE} },

	{ "_sendto_ignerr", MOD_PROTO_UDP,
	    mod_set_boolean, mod_get_boolean,
	    {B_FALSE}, {B_FALSE} },

	{ "extra_priv_ports", MOD_PROTO_UDP,
	    mod_set_extra_privports, mod_get_extra_privports,
	    {1, ULP_MAX_PORT, 0}, {0} },

	{ "?", MOD_PROTO_UDP, NULL, mod_get_allprop, {0}, {0} },

	{ NULL, 0, NULL, NULL, {0}, {0} }
};

int udp_propinfo_count = A_CNT(udp_propinfo_tbl);
