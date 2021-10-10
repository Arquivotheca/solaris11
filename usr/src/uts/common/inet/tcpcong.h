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
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Pluggable TCP-style congestion control module interfaces
 */

#ifndef _INET_TCPCONG_H
#define	_INET_TCPCONG_H

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/t_lock.h>
#include <sys/cred.h>
#include <sys/param.h>
#include <sys/zone.h>
#include <sys/sdt.h>
#include <sys/modctl.h>
#include <sys/atomic.h>
#include <sys/kstat.h>
#include <sys/list.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	TCPCONG_ALG_BUILTIN	"newreno"

#ifdef _KERNEL

#define	TCPCONG_VERSION	1

/* module/ops flags */
#define	TCPCONG_PROTO_TCP	0x01		/* TCP supported */
#define	TCPCONG_PROTO_SCTP	0x02		/* SCTP supported */

typedef struct tcpcong_args_s {
	void		*ca_state;		/* private state */
	int		ca_flags;		/* flags */
	struct mod_prop_info_s *ca_propinfo;	/* property table */
	uint32_t	*ca_ssthresh;		/* slow start threshold */
	uint32_t	*ca_cwnd;		/* congestion window */
	int32_t		*ca_cwnd_cnt;		/* cwnd in cong avoidance */
	const uint32_t	*ca_cwnd_max;		/* max cwnd */
	const uint32_t	*ca_maxburst;		/* Max.Burst per RFC 4960 */
	const uint32_t	*ca_mss;		/* max segment size */
	const int32_t	*ca_bytes_acked;	/* bytes acknowledged */
	const uint32_t	*ca_flight_size;	/* bytes unacknowledged */
	const uint16_t	*ca_dupack_cnt;		/* consecutive duplicate acks */
	const clock_t	*ca_srtt;		/* smoothed RTT */
	const clock_t	*ca_rttdev;		/* RTT deviation */
	const clock_t	*ca_idle_time;		/* conn's idle time */
} tcpcong_args_t;

typedef struct tcpcong_ops_s {
	int	co_version;			/* interface version */
	char	*co_name;			/* module/algorithm name */
	int	co_flags;			/* module flags */

	/* property management (optional) */
	struct mod_prop_info_s	*(*co_prop_info_alloc)(uint_t);
	void	(*co_prop_info_free)(struct mod_prop_info_s *, uint_t);

	/* per peer, private state */
	size_t	(*co_state_size)(int);
	void	(*co_state_init)(tcpcong_args_t *);
	void	(*co_state_fini)(tcpcong_args_t *);

	/* events */
	void	(*co_ack)(tcpcong_args_t *);	/* ack recvd, increase cwnd */
	void	(*co_loss)(tcpcong_args_t *);	/* packet loss */
	void	(*co_rto)(tcpcong_args_t *);	/* retransmit timeout */
	void	(*co_congestion)(tcpcong_args_t *); /* explicit congestion */
	void	(*co_after_idle)(tcpcong_args_t *); /* after no activity */
} tcpcong_ops_t;

typedef struct tcpcong_mod_s {
	list_node_t	cm_list;		/* linked list of modules */
	uint_t		cm_refcnt;		/* ref counter */
	tcpcong_ops_t	*cm_ops;		/* ops */
} tcpcong_mod_t;

typedef struct tcpcong_mod_s *tcpcong_handle_t;

/*
 * Define it here for convenience, so we don't have to duplicate
 * essentially the same struct in tcp_stack.h, sctp_stack.h, etc.
 */
typedef struct tcpcong_list_ent {
	tcpcong_handle_t	tce_hdl;
	tcpcong_ops_t		*tce_ops;
	char			*tce_name;
	struct mod_prop_info_s	*tce_propinfo;
	list_node_t		tce_node;
} tcpcong_list_ent_t;

#define	TCPCONG_PROP_INFO_ALLOC(tce, proto)				\
	if ((tce)->tce_ops->co_prop_info_alloc != NULL &&		\
	    (tce)->tce_ops->co_prop_info_free != NULL) {		\
		(tce)->tce_propinfo = (tce)->tce_ops->			\
		    co_prop_info_alloc(proto);				\
	}

#define	TCPCONG_PROP_INFO_FREE(tce, proto)				\
	if ((tce)->tce_propinfo != NULL) {				\
		ASSERT((tce)->tce_ops->co_prop_info_free != NULL);	\
		(tce)->tce_ops->co_prop_info_free(			\
		    (tce)->tce_propinfo, proto);			\
		(tce)->tce_propinfo = NULL;				\
	}

/*
 * Max private state size, in 64-bit words, across all algorithms
 * implemented by ON consolidation. Protocols use it as a hint to
 * optimize memory allocation.
 */
#define	TCPCONG_MAX_STATE_SIZE		16

/* For use with D probes */
#define	TCPCONG_D_CWND			0
#define	TCPCONG_D_CWND_REXMIT		1
#define	TCPCONG_D_CWND_LIM_REXMIT	2
#define	TCPCONG_D_CWND_SACK_REXMIT	3
#define	TCPCONG_D_CWND_FR_ENTER		4
#define	TCPCONG_D_CWND_FR_EXIT		5
#define	TCPCONG_D_CWND_HOE		6
#define	TCPCONG_D_CWND_AFTER_IDLE	7

extern int		tcpcong_mod_register(tcpcong_ops_t *);
extern int		tcpcong_mod_unregister(tcpcong_ops_t *);
extern tcpcong_handle_t	tcpcong_lookup(const char *, tcpcong_ops_t **);
extern void		tcpcong_unref(tcpcong_handle_t);
extern void		tcpcong_ddi_g_init();
extern void		tcpcong_ddi_g_destroy();
extern void		newreno_ddi_g_init();
extern void		newreno_ddi_g_destroy();

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_TCPCONG_H */
