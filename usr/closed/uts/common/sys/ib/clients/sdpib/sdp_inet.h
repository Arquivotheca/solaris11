/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#ifndef	_SYS_IB_CLIENTS_SDP_INET_H
#define	_SYS_IB_CLIENTS_SDP_INET_H

#include <inet/mib2.h>

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Event dispatch table
 */
#define	SDP_MSG_EVENT_TABLE_SIZE 0x20

/*
 * The data in this struct represents socket opts.
 *
 */
struct sdp_inet_ops {
	unsigned int daddr;	/* foreign ipv4 addr */
	unsigned int localroute : 1;	/* route locally only */
	unsigned int priority;
	unsigned short sport;	/* source port */
	unsigned short num;	/* local port */
	int bound_dev_if;	/* bound device index if != 0a */

	unsigned int reuse : 1;	/* SO_REUSEADDR setting */
	int rcvbuf;		/* size of receive buffer in bytes */

	int sndbuf;	/* size of send buffer in bytes */
	struct sdp_inet_ops *prev;

	/*
	 * Not all are volatile, but some are, so we might as well say they all
	 * are.
	 */
	uint32_t	dead : 1,
			done : 1,
			urginline : 1,
			keepopen : 1,
			linger : 1,
			destroy : 1,
			no_check : 1,
			broadcast : 1,
			bsdism;
	unsigned int	debug : 1,
			ipv6_v6only : 1;
	unsigned char rcvtstamp;
	unsigned char use_write_queue;
	unsigned char userlocks;
	/*
	 * Hole of 3 bytes. try to pack.
	 */
	int route_caps;
	int proc;
	unsigned long lingertime;

	struct sdp_inet_ops *pair;

	int rcvlowat;
	long rcvtimeo;
	long sndtimeo;
	boolean_t useloopback;
	boolean_t dgram_errind;
	boolean_t snd_zcopy_aware;
	boolean_t tcp_nodelay;
	clock_t   first_timer_threshold;
	clock_t   second_timer_threshold;
	clock_t   first_ctimer_threshold;
	clock_t   second_ctimer_threshold;
	clock_t   recvdstaddr;
	boolean_t anon_priv_bind;
	boolean_t exclbind;
	boolean_t ka_enabled;
	unsigned int ip_tos;
};

#define	SDP_STAT_INC(sdps, x)		((sdps)->sdps_named_ks.x.value.ui64++)
#define	SDP_STAT_DEC(sdps, x)		((sdps)->sdps_named_ks.x.value.ui64--)
#define	SDP_STAT_UPDATE(sdps, x, y)	\
		((sdps)->sdps_named_ks.x.value.ui64 += (y))

/* Round up the value to the nearest mss. */
#define	SDP_MSS_ROUNDUP(value, mss)	((((value) - 1) / (mss) + 1) * (mss))

extern int sdp_g_num_epriv_ports;
extern uint16_t sdp_g_epriv_ports[];

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_INET_H */
