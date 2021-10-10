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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MDB_IP_COMMON_H
#define	_MDB_IP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ks.h>

#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <netinet/sctp.h>

#include <inet/sctp/sctp_impl.h>
#include <inet/sctp/sctp_addr.h>

#ifdef _BIG_ENDIAN
#define	ip_ntoh_32(x)	((x) & 0xffffffff)
#define	ip_ntoh_16(x)	((x) & 0xffff)
#else
#define	ip_ntoh_32(x)	(((uint32_t)(x) << 24) | \
			(((uint32_t)(x) << 8) & 0xff0000) | \
			(((uint32_t)(x) >> 8) & 0xff00) | \
			((uint32_t)(x)  >> 24))
#define	ip_ntoh_16(x)	((((uint16_t)(x) << 8) & 0xff00) | \
			((uint16_t)(x) >> 8))
#endif

/* Definitions provided in ip.c. */
extern int ns_walk_step(mdb_walk_state_t *, int);

/*
 * TCP related definitions.
 */
extern int tcphdr(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int tcphdr_print(struct tcphdr *, uintptr_t);
extern int tcp_stacks_walk_step(mdb_walk_state_t *);
extern int tcps_sc_walk_init(mdb_walk_state_t *);
extern int tcps_sc_walk_step(mdb_walk_state_t *);

/*
 * UDP related definitions.
 */
extern int udphdr(uintptr_t, uint_t, int, const mdb_arg_t *);
extern void udphdr_print(struct udphdr *);
extern int udp_stacks_walk_step(mdb_walk_state_t *);

/*
 * SCTP related definitions.
 */
typedef struct sctp_fanout_walk_data {
	int index;
	int size;
	uintptr_t sctp;
	sctp_tf_t *fanout;
	uintptr_t (*getnext)(sctp_t *);
} sctp_fanout_walk_data_t;

typedef struct sctp_fanout_init {
	const char *nested_walker_name;
	size_t offset;	/* for what used to be a symbol */
	int (*getsize)(sctp_stack_t *);
	uintptr_t (*getnext)(sctp_t *);
} sctp_fanout_init_t;

extern const sctp_fanout_init_t sctp_listen_fanout_init;
extern const sctp_fanout_init_t sctp_conn_fanout_init;
extern const sctp_fanout_init_t sctp_bind_fanout_init;

extern void sctp_fanout_stack_walk_fini(mdb_walk_state_t *);
extern int sctp_fanout_stack_walk_init(mdb_walk_state_t *);
extern int sctp_fanout_stack_walk_step(mdb_walk_state_t *);
extern int sctp_fanout_walk_init(mdb_walk_state_t *);
extern int sctp_fanout_walk_step(mdb_walk_state_t *);

extern int sctp(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctp_chunk_print(uintptr_t, uintptr_t);

extern int sctphdr(uintptr_t, uint_t, int, const mdb_arg_t *);
void sctphdr_print(sctp_hdr_t *);
extern void sctp_help(void);

extern int sctp_ill_walk_init(mdb_walk_state_t *);
extern int sctp_ill_walk_step(mdb_walk_state_t *);
extern int sctp_instr(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int sctp_ipif_walk_init(mdb_walk_state_t *);
extern int sctp_ipif_walk_step(mdb_walk_state_t *);
extern int sctp_istr_msgs(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctp_mdata_chunk(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctp_reass_list(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctp_set(uintptr_t, uint_t, int, const mdb_arg_t *);
extern int sctp_stack_ill_walk_init(mdb_walk_state_t *);
extern int sctp_stack_ill_walk_step(mdb_walk_state_t *);
extern int sctp_stack_ipif_walk_init(mdb_walk_state_t *);
extern int sctp_stack_ipif_walk_step(mdb_walk_state_t *);

extern int sctp_uo_reass_list(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctp_walk_faddr_init(mdb_walk_state_t *);
extern int sctp_walk_faddr_step(mdb_walk_state_t *);
extern void sctp_walk_saddr_fini(mdb_walk_state_t *);
extern int sctp_walk_saddr_init(mdb_walk_state_t *);
extern int sctp_walk_saddr_step(mdb_walk_state_t *);

extern int sctp_xmit_list(uintptr_t, uint_t, int, const mdb_arg_t *);

extern int sctps_sc_walk_init(mdb_walk_state_t *);
extern int sctps_sc_walk_step(mdb_walk_state_t *);
extern int sctp_stacks_walk_step(mdb_walk_state_t *);
extern int sctps_walk_init(mdb_walk_state_t *);
extern int sctps_walk_step(mdb_walk_state_t *);

/*
 * ILB related definitions.
 */
extern int ilb_stacks_walk_step(mdb_walk_state_t *);
extern int ilb_rules_walk_init(mdb_walk_state_t *);
extern int ilb_rules_walk_step(mdb_walk_state_t *);
extern int ilb_servers_walk_init(mdb_walk_state_t *);
extern int ilb_servers_walk_step(mdb_walk_state_t *);
extern int ilb_nat_src_walk_init(mdb_walk_state_t *);
extern int ilb_nat_src_walk_step(mdb_walk_state_t *);
extern int ilb_conn_walk_init(mdb_walk_state_t *);
extern int ilb_conn_walk_step(mdb_walk_state_t *);
extern int ilb_sticky_walk_init(mdb_walk_state_t *);
extern int ilb_sticky_walk_step(mdb_walk_state_t *);
extern void ilb_common_walk_fini(mdb_walk_state_t *);

#ifdef __cplusplus
}
#endif

#endif /* _MDB_IP_COMMON_H */
