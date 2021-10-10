/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley. The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "mpd_defs.h"
#include "mpd_tables.h"
#include <inet/xprobe.h>
/*
 * Probe types for icmp_probe()
 */
#define	PROBE_UNI	0x1234		/* Unicast probe packet */
#define	PROBE_MULTI	0x5678		/* Multicast probe packet */
#define	PROBE_RTT	0x9abc		/* RTT only probe packet */

#define	MSEC_PERMIN	(60 * MILLISEC)	/* Number of milliseconds in a minute */
/*
 * Format of probe / probe response packets. This is an ICMP Echo request
 * or ICMP Echo reply. Packet format is same for both IPv4 and IPv6
 */
struct pr_icmp
{
	uint8_t  pr_icmp_type;		/* type field */
	uint8_t  pr_icmp_code;		/* code field */
	uint16_t pr_icmp_cksum;		/* checksum field */
	uint16_t pr_icmp_id;		/* Identification */
	uint16_t pr_icmp_seq;		/* sequence number */
	uint64_t pr_icmp_timestamp;	/* Time stamp (in ns) */
	uint32_t pr_icmp_mtype;		/* Message type */
};

static struct in6_addr all_nodes_mcast_v6 = { { 0xff, 0x2, 0x0, 0x0,
				    0x0, 0x0, 0x0, 0x0,
				    0x0, 0x0, 0x0, 0x0,
				    0x0, 0x0, 0x0, 0x1 } };

static struct in_addr all_nodes_mcast_v4 = { { { 0xe0, 0x0, 0x0, 0x1 } } };

static hrtime_t	last_fdt_bumpup_time;	/* When FDT was bumped up last */

static void		*find_ancillary(struct msghdr *msg, int cmsg_level,
    int cmsg_type);
static void		pi_set_crtt(struct target *tg, int64_t m,
    boolean_t is_probe_uni);
static void		incoming_echo_reply(struct phyint_instance *pii,
    struct pr_icmp *reply, struct in6_addr fromaddr, struct timeval *recv_tvp);
static void		incoming_rtt_reply(struct phyint_instance *pii,
    struct pr_icmp *reply, struct in6_addr fromaddr);
static void		incoming_mcast_reply(struct phyint_instance *pii,
    struct pr_icmp *reply, struct in6_addr fromaddr);

static boolean_t	check_pg_crtt_improved(struct phyint_group *pg);
static boolean_t	check_pii_crtt_improved(struct phyint_instance *pii);
static boolean_t	check_exception_target(struct phyint_instance *pii,
    struct target *target);
static void		probe_fail_info(struct phyint_instance *pii,
    struct target *cur_tg, struct probe_fail_count *pfinfo);
static void		icmp_probe_success_info(struct phyint_instance *pii,
    struct target *cur_tg, struct probe_success_count *psinfo);
static void		local_probe_success_info(struct phyint *pii,
    struct target *cur_tg, struct probe_success_count *psinfo, uint_t *total);
static boolean_t	phyint_repaired(struct phyint *pi);

static boolean_t	highest_ack_tg(uint16_t seq, struct target *tg);
static int 		in_cksum(ushort_t *addr, int len);
static void		reset_snxt_basetimes(void);
static int		ns2ms(int64_t ns);
static int64_t		tv2ns(struct timeval *);
static void		group_recovered(struct phyint *);
static boolean_t	transitive_probe_success(struct phyint *,
    struct phyint *);
static int		local_probe_target_count(struct phyint *);
static boolean_t	local_dpi_rr(struct phyint *);
static void		phyint_icmp_prober_failed(struct phyint *);
static void		phyint_inst_fail(struct phyint_instance *);
static boolean_t	phyint_inst_mcast_timeout(struct phyint_instance *);

/*
 * CRTT - Conservative Round Trip Time Estimate
 * Probe success - A matching probe reply received before CRTT ms has elapsed
 *	after sending the probe.
 * Probe failure - No probe reply received and more than CRTT ms has elapsed
 *	after sending the probe.
 *
 * TLS - Time last success. Most recent probe ack received at this time.
 * TFF - Time first fail. The time of the earliest probe failure in
 *	a consecutive series of probe failures.
 * NUM_PROBE_REPAIRS  - Number of consecutive successful probes required
 * 	before declaring phyint repair.
 * NUM_PROBE_FAILS - Number of consecutive probe failures required to
 *	declare a phyint failure.
 *
 * 			Phyint state diagram
 *
 * The state of a phyint that is capable of being probed, is completely
 * specified by the 3-tuple <pi_state, pg_state, (I, P)>.
 *
 * A phyint is defined as an IP prober in one of the two conditions:
 * (i) it has been configured with a test address (the common case), or,
 * (ii) it is active. An active phyint is one that has an IP address
 *      (`ipif_bound_ill' in the kernel) bound to it and can receive inbound
 *      IP packets for that address that are sent to its link-layer address.
 *
 * A phyint that is not an IP prober MAY be sending Layer-2 transtive probes.
 * Transitive probes will only be sent from phyints that are in IPMP
 * groups which have no IP test addresses configured on any of the
 * participating interfaces. In this configuration, a phyint that is
 * not bound to a data-address determines its health indirectly by probing
 * another phyint in the group that is sending/receiving ICMP probes.
 *
 * A phyint starts in either PI_RUNNING or PI_OFFLINE, depending on whether
 * IFF_OFFLINE is set.  If the phyint is an IP prober, and unicast probe
 * targets are available, then a phyint must also successfully
 * be able to send and receive probes in order to remain in the PI_RUNNING
 * state (otherwise, it transitions to PI_FAILED).
 *
 * Further, if a PI_RUNNING phyint is an IP prober, but is unable to find
 * any probe targets, it will transition to the PI_NOTARGETS state, which
 * indicates that the link is apparently functional but that in.mpathd is
 * unable to send probes to verify functionality (in this case, in.mpathd
 * makes the optimistic assumption that the interface is working
 * correctly and thus does not mark the interface FAILED, but reports it as
 * IPMP_IF_UNKNOWN through the async events and query interfaces).
 *
 * When all interfaces in the group have failed, a phyint from the group is
 * nominated as the "designated prober". The designated prober will have
 * a state of  PI_FAILED, with the pi_flags indicating that IFF_PROBER has
 * been set in the kernel. The designated prober will be used to send out
 * ICMP probes using a data-address assigned to the group until one of
 * the following events occur:
 * (a) NUM_PROBE_REPAIRS successful probes are sent and received to a probe
 *     target, or,
 * (b) a unicast probe target has been configured, and NUM_PROBE_FAILS probes
 *     to that target fail, or,
 * (c) probe targets have not been configured, and the designated prober is
 *     unable to reach a probe target within a 15 s (approximate time for
 *     sending 3 * NUM_PROBE_FAILS probes, 1s apart) interval.
 * In the case of (b) or (c), another phyint in the group will be nominated
 * as the desginated prober using round-robin selection.
 *
 * At any point, a phyint may be administratively marked offline via if_mpadm.
 * In this case, the interface always transitions to PI_OFFLINE, regardless
 * of its previous state.  When the interface is later brought back online,
 * in.mpathd acts as if the interface is new (and thus it transitions to
 * PI_RUNNING or PI_FAILED based on the status of the link and the result of
 * its probes, if probes are sent).
 *
 * pi_state -  PI_RUNNING or PI_FAILED
 *	PI_RUNNING: The failure detection logic says the phyint is good.
 *	PI_FAILED: The failure detection logic says the phyint has failed.
 *
 * pg_state  - PG_OK, PG_DEGRADED, or PG_FAILED.
 *	PG_OK: All interfaces in the group are OK.
 *	PG_DEGRADED: Some interfaces in the group are unusable.
 *	PG_FAILED: All interfaces in the group are unusable.
 *
 *	In the case of router targets, we assume that the current list of
 *	targets obtained from the routing table, is still valid, so the
 *	phyint stat is PI_FAILED. In the case of host targets, we delete the
 *	list of targets, and multicast to the all hosts, to reconstruct the
 *	target list. So the phyints are in the PI_NOTARGETS state.
 *
 * I -	value of (pi_flags & IFF_INACTIVE)
 *	IFF_INACTIVE: This phyint will not send or receive packets.
 *	Usually, inactive is tied to standby interfaces that are not yet
 *	needed (e.g., no non-standby interfaces in the group have failed).
 *	When failback has been disabled (FAILBACK=no configured), phyint can
 *	also be a non-STANDBY. In this case IFF_INACTIVE is set when phyint
 *	subsequently recovers after a failure.
 *
 * P - value of (pi_flags & IFF_PROBER)
 *     IFF_PROBER: This phyint is the designated prober of its IPMP group when
 *     the group itself has failed.
 *
 * Not all 9 possible combinations of the above 3-tuple are possible.
 *
 * I is tracked by IP. pi_state is tracked by mpathd.
 *
 *			pi_state state machine
 *
 * Note: P must be zero in all states listed below where it has not been
 * explicitly specified
 * ---------------------------------------------------------------------------
 *	Event			State			New State
 *				Action:
 * ---------------------------------------------------------------------------
 *	IP interface failure	(PI_RUNNING, I == 0) -> (PI_FAILED, I == 0)
 *	detection		: set IFF_FAILED on this phyint
 *
 *	IP interface failure	(PI_RUNNING, I == 1) -> (PI_FAILED, I == 0)
 *	detection		: set IFF_FAILED on this phyint
 *
 *	IP interface repair 	(PI_FAILED, I == 0, FAILBACK=yes, P == any)
 *	detection				     -> (PI_RUNNING, I == 0)
 *				: clear IFF_FAILED, IFF_PROBER on this phyint
 *
 *	IP interface repair 	(PI_FAILED, I == 0, FAILBACK=no, P == any)
 *	detection				     ->	(PI_RUNNING, I == 1)
 *				: clear IFF_FAILED, IFF_PROBER on this phyint
 *				: if failback is disabled set I == 1
 *
 *	Group failure		(perform on all phyints in the group)
 *	detection 		PI_RUNNING		PI_FAILED
 *	(Router targets)	: set IFF_FAILED, select a phyint as
 *				: designated prober by setting its IFF_PROBER
 *
 *	Group failure		(perform on all phyints in the group)
 *	detection 		PI_RUNNING		PI_NOTARGETS
 *	(Host targets)		: set IFF_FAILED
 *				: delete the target list on all phyints. select
 *				: a designated prober by setting its IFF_PROBER
 *
 * ---------------------------------------------------------------------------
 */

struct probes_missed probes_missed;

/*
 * Compose and transmit an ICMP ECHO REQUEST packet.  The IP header
 * will be added on by the kernel.  The id field identifies this phyint.
 * and the sequence number is an increasing (modulo 2^^16) integer. The data
 * portion holds the time value when the packet is sent. On echo this is
 * extracted to compute the round-trip time. Three different types of
 * probe packets are used.
 *
 * PROBE_UNI: This type is used to do failure detection / failure recovery
 *	and RTT calculation. PROBE_UNI probes are spaced apart in time,
 *	not less than the current CRTT. pii_probes[] stores data
 *	about these probes. These packets consume sequence number space.
 *
 * PROBE_RTT: This type is used to make only rtt measurements. Normally these
 * 	are not used. Under heavy network load, the rtt may go up very high,
 *	due to a spike, or may appear to go high, due to extreme scheduling
 * 	delays. Once the network stress is removed, mpathd takes long time to
 *	recover, because the probe_interval is already high, and it takes
 *	a long time to send out sufficient number of probes to bring down the
 *	rtt. To avoid this problem, PROBE_RTT probes are sent out every
 *	user_probe_interval ms. and will cause only rtt updates. These packets
 *	do not consume sequence number space nor is information about these
 *	packets stored in the pii_probes[]
 *
 * PROBE_MULTI: This type is only used to construct a list of targets, when
 *	no targets are known. The packet is multicast to the all hosts addr.
 */
static void
icmp_probe(struct phyint_instance *pii, uint_t probe_type,
    hrtime_t start_hrtime)
{
	hrtime_t sent_hrtime;
	struct timeval sent_tv;
	struct pr_icmp probe_pkt;	/* Probe packet */
	struct sockaddr_storage targ;	/* target address */
	uint_t	targaddrlen;		/* targed address length */
	int	pr_ndx;			/* probe index in pii->pii_probes[] */
	boolean_t sent = _B_FALSE;
	int	rval;

	if (debug & D_TARGET) {
		logdebug("icmp_probe(%s %s %d %lld)\n", AF_STR(pii->pii_af),
		    pii->pii_name, probe_type, start_hrtime);
	}

	assert(pii->pii_probe_sock != -1);
	assert(probe_type == PROBE_UNI || probe_type == PROBE_MULTI ||
	    probe_type == PROBE_RTT);

	probe_pkt.pr_icmp_type = (pii->pii_af == AF_INET) ?
	    ICMP_ECHO_REQUEST : ICMP6_ECHO_REQUEST;
	probe_pkt.pr_icmp_code = 0;
	probe_pkt.pr_icmp_cksum = 0;
	probe_pkt.pr_icmp_seq = htons(pii->pii_snxt);

	/*
	 * Since there is no need to do arithmetic on the icmpid,
	 * (only equality check is done) pii_icmpid is stored in
	 * network byte order at initialization itself.
	 */
	probe_pkt.pr_icmp_id = pii->pii_icmpid;
	probe_pkt.pr_icmp_timestamp = htonll(start_hrtime);
	probe_pkt.pr_icmp_mtype = htonl(probe_type);

	/*
	 * If probe_type is PROBE_MULTI, this packet will be multicast to
	 * the all hosts address. Otherwise it is unicast to the next target.
	 */
	assert(probe_type == PROBE_MULTI || ((pii->pii_target_next != NULL) &&
	    pii->pii_rtt_target_next != NULL));

	bzero(&targ, sizeof (targ));
	targ.ss_family = pii->pii_af;

	if (pii->pii_af == AF_INET6) {
		struct in6_addr *addr6;

		addr6 = &((struct sockaddr_in6 *)&targ)->sin6_addr;
		targaddrlen = sizeof (struct sockaddr_in6);
		if (probe_type == PROBE_MULTI) {
			*addr6 = all_nodes_mcast_v6;
		} else if (probe_type == PROBE_UNI) {
			*addr6 = pii->pii_target_next->tg_address;
		} else { /* type is PROBE_RTT */
			*addr6 = pii->pii_rtt_target_next->tg_address;
		}
	} else {
		struct in_addr *addr4;

		addr4 = &((struct sockaddr_in *)&targ)->sin_addr;
		targaddrlen = sizeof (struct sockaddr_in);
		if (probe_type == PROBE_MULTI) {
			*addr4 = all_nodes_mcast_v4;
		} else if (probe_type == PROBE_UNI) {
			IN6_V4MAPPED_TO_INADDR(
			    &pii->pii_target_next->tg_address, addr4);
		} else { /* type is PROBE_RTT */
			IN6_V4MAPPED_TO_INADDR(
			    &pii->pii_rtt_target_next->tg_address, addr4);
		}

		/*
		 * Compute the IPv4 icmp checksum. Does not cover the IP header.
		 */
		probe_pkt.pr_icmp_cksum =
		    in_cksum((ushort_t *)&probe_pkt, (int)sizeof (probe_pkt));
	}

	/*
	 * Use the current time as the time we sent.  Not atomic, but the best
	 * we can do from here.
	 */
	sent_hrtime = gethrtime();
	(void) gettimeofday(&sent_tv, NULL);
	rval = sendto(pii->pii_probe_sock, &probe_pkt, sizeof (probe_pkt), 0,
	    (struct sockaddr *)&targ, targaddrlen);
	/*
	 * If the send would block, this may either be transient or a hang in a
	 * lower layer. We pretend the probe was actually sent, the daemon will
	 * not see a reply to the probe and will fail the interface if normal
	 * failure detection criteria are met.
	 */
	if (rval == sizeof (probe_pkt) ||
	    (rval == -1 && errno == EWOULDBLOCK)) {
		sent = _B_TRUE;
	} else {
		logperror_pii(pii, "probe: probe sendto");
	}

	/*
	 * If this is a PROBE_UNI probe packet being unicast to a target, then
	 * update our tables. We will need this info in processing the probe
	 * response. PROBE_MULTI and PROBE_RTT packets are not used for
	 * the purpose of failure or recovery detection. PROBE_MULTI packets
	 * are only used to construct a list of targets. PROBE_RTT packets are
	 * used only for updating the rtt and not for failure detection.
	 */
	if (probe_type == PROBE_UNI && sent) {
		pr_ndx = pii->pii_probe_next;
		assert(pr_ndx >= 0 && pr_ndx < PROBE_STATS_COUNT);

		/* Collect statistics, before we reuse the last slot. */
		if (pii->pii_probes[pr_ndx].pr_status == PR_LOST)
			pii->pii_cum_stats.lost++;
		else if (pii->pii_probes[pr_ndx].pr_status == PR_ACKED)
			pii->pii_cum_stats.acked++;
		pii->pii_cum_stats.sent++;

		pii->pii_probes[pr_ndx].pr_id = pii->pii_snxt;
		pii->pii_probes[pr_ndx].pr_tv_sent = sent_tv;
		pii->pii_probes[pr_ndx].pr_hrtime_sent = sent_hrtime;
		pii->pii_probes[pr_ndx].pr_hrtime_start = start_hrtime;
		pii->pii_probes[pr_ndx].pr_target = pii->pii_target_next;
		probe_chstate(&pii->pii_probes[pr_ndx], pii->pii_name,
		    pii->pii_af, PR_UNACKED);

		pii->pii_probe_next = PROBE_INDEX_NEXT(pii->pii_probe_next);
		pii->pii_target_next = icmp_target_next(pii->pii_target_next);
		assert(pii->pii_target_next != NULL);
		/*
		 * If we have a single variable to denote the next target to
		 * probe for both rtt probes and failure detection probes, we
		 * could end up with a situation where the failure detection
		 * probe targets become disjoint from the rtt probe targets.
		 * Eg. if 2 targets and the actual fdt is double the user
		 * specified fdt. So we have 2 variables. In this scheme
		 * we also reset pii_rtt_target_next for every fdt probe,
		 * though that may not be necessary.
		 */
		pii->pii_rtt_target_next = pii->pii_target_next;
		pii->pii_snxt++;
	} else if (probe_type == PROBE_RTT) {
		pii->pii_rtt_target_next =
		    icmp_target_next(pii->pii_rtt_target_next);
		assert(pii->pii_rtt_target_next != NULL);
	}
}

/*
 * Incoming IPv4 data from wire, is received here. Called from main.
 */
void
in_data(struct phyint_instance *pii)
{
	struct	sockaddr_in 	from;
	struct	in6_addr	fromaddr;
	static uint64_t in_packet[(IP_MAXPACKET + 1)/8];
	static uint64_t ancillary_data[(IP_MAXPACKET + 1)/8];
	struct ip *ip;
	int 	iphlen;
	int 	len;
	char 	abuf[INET_ADDRSTRLEN];
	struct msghdr msg;
	struct iovec iov;
	struct pr_icmp *reply;
	struct timeval *recv_tvp;

	if (debug & D_PROBE) {
		logdebug("in_data(%s %s)\n",
		    AF_STR(pii->pii_af), pii->pii_name);
	}

	iov.iov_base = (char *)in_packet;
	iov.iov_len = sizeof (in_packet);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = (struct sockaddr *)&from;
	msg.msg_namelen = sizeof (from);
	msg.msg_control = ancillary_data;
	msg.msg_controllen = sizeof (ancillary_data);

	/*
	 * Poll has already told us that a message is waiting,
	 * on this socket. Read it now. We should not block.
	 */
	if ((len = recvmsg(pii->pii_probe_sock, &msg, 0)) < 0) {
		logperror_pii(pii, "in_data: recvmsg");
		return;
	}

	/*
	 * If the datalink has indicated the link is down, don't go
	 * any further.
	 */
	if (LINK_DOWN(pii->pii_phyint))
		return;

	/* Get the printable address for error reporting */
	(void) inet_ntop(AF_INET, &from.sin_addr, abuf, sizeof (abuf));

	/* Ignore packets > 64k or control buffers that don't fit */
	if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
		if (debug & D_PKTBAD) {
			logdebug("Truncated message: msg_flags 0x%x from %s\n",
			    msg.msg_flags, abuf);
		}
		return;
	}

	/* Make sure packet contains at least minimum ICMP header */
	ip = (struct ip *)in_packet;
	iphlen = ip->ip_hl << 2;
	if (len < iphlen + ICMP_MINLEN) {
		if (debug & D_PKTBAD) {
			logdebug("in_data: packet too short (%d bytes)"
			    " from %s\n", len, abuf);
		}
		return;
	}

	/*
	 * Subtract the IP hdr length, 'len' will be length of the probe
	 * reply, starting from the icmp hdr.
	 */
	len -= iphlen;
	/* LINTED */
	reply = (struct pr_icmp *)((char *)in_packet + iphlen);

	/* Probe replies are icmp echo replies. Ignore anything else */
	if (reply->pr_icmp_type != ICMP_ECHO_REPLY)
		return;

	/*
	 * The icmp id should match what we sent, which is stored
	 * in pi_icmpid. The icmp code for reply must be 0.
	 * The reply content must be a struct pr_icmp
	 */
	if (reply->pr_icmp_id != pii->pii_icmpid) {
		/* Not in response to our probe */
		return;
	}

	if (reply->pr_icmp_code != 0) {
		logtrace("probe reply code %d from %s on %s\n",
		    reply->pr_icmp_code, abuf, pii->pii_name);
		return;
	}

	if (len < sizeof (struct pr_icmp)) {
		logtrace("probe reply too short: %d bytes from %s on %s\n",
		    len, abuf, pii->pii_name);
		return;
	}

	recv_tvp = find_ancillary(&msg, SOL_SOCKET, SCM_TIMESTAMP);
	if (recv_tvp == NULL) {
		logtrace("message without timestamp from %s on %s\n",
		    abuf, pii->pii_name);
		return;
	}

	IN6_INADDR_TO_V4MAPPED(&from.sin_addr, &fromaddr);
	if (reply->pr_icmp_mtype == htonl(PROBE_UNI))
		/* Unicast probe reply */
		incoming_echo_reply(pii, reply, fromaddr, recv_tvp);
	else if (reply->pr_icmp_mtype == htonl(PROBE_MULTI)) {
		/* Multicast reply */
		incoming_mcast_reply(pii, reply, fromaddr);
	} else if (reply->pr_icmp_mtype == htonl(PROBE_RTT)) {
		incoming_rtt_reply(pii, reply, fromaddr);
	} else {
		/* Probably not in response to our probe */
		logtrace("probe reply type: %d from %s on %s\n",
		    reply->pr_icmp_mtype, abuf, pii->pii_name);
		return;
	}
}

/*
 * Incoming IPv6 data from wire is received here. Called from main.
 */
void
in6_data(struct phyint_instance *pii)
{
	struct sockaddr_in6 from;
	static uint64_t in_packet[(IP_MAXPACKET + 1)/8];
	static uint64_t ancillary_data[(IP_MAXPACKET + 1)/8];
	int len;
	char abuf[INET6_ADDRSTRLEN];
	struct msghdr msg;
	struct iovec iov;
	void	*opt;
	struct	pr_icmp *reply;
	struct	timeval *recv_tvp;

	if (debug & D_PROBE) {
		logdebug("in6_data(%s %s)\n",
		    AF_STR(pii->pii_af), pii->pii_name);
	}

	iov.iov_base = (char *)in_packet;
	iov.iov_len = sizeof (in_packet);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = (struct sockaddr *)&from;
	msg.msg_namelen = sizeof (from);
	msg.msg_control = ancillary_data;
	msg.msg_controllen = sizeof (ancillary_data);

	if ((len = recvmsg(pii->pii_probe_sock, &msg, 0)) < 0) {
		logperror_pii(pii, "in6_data: recvmsg");
		return;
	}

	/*
	 * If the datalink has indicated that the link is down, don't go
	 * any further.
	 */
	if (LINK_DOWN(pii->pii_phyint))
		return;

	/* Get the printable address for error reporting */
	(void) inet_ntop(AF_INET6, &from.sin6_addr, abuf, sizeof (abuf));
	if (len < ICMP_MINLEN) {
		if (debug & D_PKTBAD) {
			logdebug("Truncated message: msg_flags 0x%x from %s\n",
			    msg.msg_flags, abuf);
		}
		return;
	}
	/* Ignore packets > 64k or control buffers that don't fit */
	if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
		if (debug & D_PKTBAD) {
			logdebug("Truncated message: msg_flags 0x%x from %s\n",
			    msg.msg_flags, abuf);
		}
		return;
	}

	reply = (struct pr_icmp *)in_packet;
	if (reply->pr_icmp_type != ICMP6_ECHO_REPLY)
		return;

	if (reply->pr_icmp_id != pii->pii_icmpid) {
		/* Not in response to our probe */
		return;
	}

	/*
	 * The kernel has already verified the the ICMP checksum.
	 */
	if (!IN6_IS_ADDR_LINKLOCAL(&from.sin6_addr)) {
		logtrace("ICMPv6 echo reply source address not linklocal from "
		    "%s on %s\n", abuf, pii->pii_name);
		return;
	}
	opt = find_ancillary(&msg, IPPROTO_IPV6, IPV6_RTHDR);
	if (opt != NULL) {
		/* Can't allow routing headers in probe replies  */
		logtrace("message with routing header from %s on %s\n",
		    abuf, pii->pii_name);
		return;
	}

	if (reply->pr_icmp_code != 0) {
		logtrace("probe reply code: %d from %s on %s\n",
		    reply->pr_icmp_code, abuf, pii->pii_name);
		return;
	}
	if (len < (sizeof (struct pr_icmp))) {
		logtrace("probe reply too short: %d bytes from %s on %s\n",
		    len, abuf, pii->pii_name);
		return;
	}

	recv_tvp = find_ancillary(&msg, SOL_SOCKET, SCM_TIMESTAMP);
	if (recv_tvp == NULL) {
		logtrace("message without timestamp from %s on %s\n",
		    abuf, pii->pii_name);
		return;
	}

	if (reply->pr_icmp_mtype == htonl(PROBE_UNI)) {
		incoming_echo_reply(pii, reply, from.sin6_addr, recv_tvp);
	} else if (reply->pr_icmp_mtype == htonl(PROBE_MULTI)) {
		incoming_mcast_reply(pii, reply, from.sin6_addr);
	} else if (reply->pr_icmp_mtype == htonl(PROBE_RTT)) {
		incoming_rtt_reply(pii, reply, from.sin6_addr);
	} else  {
		/* Probably not in response to our probe */
		logtrace("probe reply type: %d from %s on %s\n",
		    reply->pr_icmp_mtype, abuf, pii->pii_name);
	}
}

/*
 * Process the incoming rtt reply, in response to our rtt probe.
 * Common for both IPv4 and IPv6. Unlike incoming_echo_reply() we don't
 * have any stored information about the probe we sent. So we don't log
 * any errors if we receive bad replies.
 */
static void
incoming_rtt_reply(struct phyint_instance *pii, struct pr_icmp *reply,
    struct in6_addr fromaddr)
{
	int64_t	m;		/* rtt measurement in ns */
	char	abuf[INET6_ADDRSTRLEN];
	struct	target	*target;
	struct 	phyint_group *pg;

	/* Get the printable address for error reporting */
	(void) pr_addr(pii->pii_af, fromaddr, abuf, sizeof (abuf));

	if (debug & D_PROBE) {
		logdebug("incoming_rtt_reply: %s %s %s\n",
		    AF_STR(pii->pii_af), pii->pii_name, abuf);
	}

	/* Do we know this target ? */
	target = icmp_target_lookup(pii, fromaddr);
	if (target == NULL)
		return;

	m = (int64_t)(gethrtime() - ntohll(reply->pr_icmp_timestamp));
	/* Invalid rtt. It has wrapped around */
	if (m < 0)
		return;

	/*
	 * Don't update rtt until we see NUM_PROBE_REPAIRS probe responses
	 * The initial few responses after the interface is repaired may
	 * contain high rtt's because they could have been queued up waiting
	 * for ARP/NDP resolution on a failed interface.
	 */
	pg = pii->pii_phyint->pi_group;
	if ((pii->pii_state != PI_RUNNING) || GROUP_FAILED(pg))
		return;

	/*
	 * Update rtt only if the new rtt is lower than the current rtt.
	 * (specified by the 3rd parameter to pi_set_crtt).
	 * If a spike has caused the current probe_interval to be >
	 * user_probe_interval, then this mechanism is used to bring down
	 * the rtt rapidly once the network stress is removed.
	 * If the new rtt is higher than the current rtt, we don't want to
	 * update the rtt. We are having more than 1 outstanding probe and
	 * the increase in rtt we are seeing is being unnecessarily weighted
	 * many times. The regular rtt update will be handled by
	 * incoming_echo_reply() and will take care of any rtt increase.
	 */
	pi_set_crtt(target, m, _B_FALSE);
	if ((target->tg_crtt < (pg->pg_probeint / LOWER_FDT_TRIGGER)) &&
	    (user_failure_detection_time < pg->pg_fdt) &&
	    (last_fdt_bumpup_time + MIN_SETTLING_TIME < gethrtime())) {
		/*
		 * If the crtt has now dropped by a factor of LOWER_FT_TRIGGER,
		 * investigate if we can improve the failure detection time to
		 * meet whatever the user specified.
		 */
		if (check_pg_crtt_improved(pg)) {
			pg->pg_fdt = MAX(pg->pg_fdt / NEXT_FDT_MULTIPLE,
			    user_failure_detection_time);
			pg->pg_probeint = pg->pg_fdt / (NUM_PROBE_FAILS + 2);
			if (pii->pii_phyint->pi_group != phyint_anongroup) {
				logerr("Improved failure detection time %d ms "
				    "on (%s %s) for group \"%s\"\n",
				    pg->pg_fdt, AF_STR(pii->pii_af),
				    pii->pii_name,
				    pii->pii_phyint->pi_group->pg_name);
			}
			if (user_failure_detection_time == pg->pg_fdt) {
				/* Avoid any truncation or rounding errors */
				pg->pg_probeint = user_probe_interval;
				/*
				 * No more rtt probes will be sent. The actual
				 * fdt has dropped to the user specified value.
				 * pii_fd_snxt_basetime and pii_snxt_basetime
				 * will be in sync henceforth.
				 */
				reset_snxt_basetimes();
			}
		}
	}
}

/*
 * Process the incoming echo reply, in response to our unicast probe.
 * Common for both IPv4 and IPv6
 */
static void
incoming_echo_reply(struct phyint_instance *pii, struct pr_icmp *reply,
    struct in6_addr fromaddr, struct timeval *recv_tvp)
{
	int64_t	m;		/* rtt measurement in ns */
	hrtime_t cur_hrtime;	/* in ns from some arbitrary point */
	char	abuf[INET6_ADDRSTRLEN];
	int	pr_ndx;
	struct	target	*target;
	boolean_t exception;
	uint64_t pr_icmp_timestamp;
	uint16_t pr_icmp_seq;
	struct	probe_stats *pr_statp;
	struct 	phyint_group *pg = pii->pii_phyint->pi_group;

	/* Get the printable address for error reporting */
	(void) pr_addr(pii->pii_af, fromaddr, abuf, sizeof (abuf));

	if (debug & D_PROBE) {
		logdebug("incoming_echo_reply: %s %s %s seq %u recv_tvp %lld\n",
		    AF_STR(pii->pii_af), pii->pii_name, abuf,
		    ntohs(reply->pr_icmp_seq), tv2ns(recv_tvp));
	}

	pr_icmp_timestamp = ntohll(reply->pr_icmp_timestamp);
	pr_icmp_seq = ntohs(reply->pr_icmp_seq);

	/* Reject out of window probe replies */
	if (SEQ_GE(pr_icmp_seq, pii->pii_snxt) ||
	    SEQ_LT(pr_icmp_seq, pii->pii_snxt - PROBE_STATS_COUNT)) {
		logtrace("out of window probe seq %u snxt %u on %s from %s\n",
		    pr_icmp_seq, pii->pii_snxt, pii->pii_name, abuf);
		pii->pii_cum_stats.unknown++;
		return;
	}

	cur_hrtime = gethrtime();
	m = (int64_t)(cur_hrtime - pr_icmp_timestamp);
	if (m < 0) {
		/*
		 * This is a ridiculously high value of rtt. rtt has wrapped
		 * around. Log a message, and ignore the rtt.
		 */
		logerr("incoming_echo_reply: rtt wraparound cur_hrtime %lld "
		    "reply timestamp %lld\n", cur_hrtime, pr_icmp_timestamp);
	}

	/*
	 * Get the probe index pr_ndx corresponding to the received icmp seq.
	 * number in our pii->pii_probes[] array. The icmp sequence number
	 * pii_snxt corresponds to the probe index pii->pii_probe_next
	 */
	pr_ndx = MOD_SUB(pii->pii_probe_next,
	    (uint16_t)(pii->pii_snxt - pr_icmp_seq), PROBE_STATS_COUNT);

	assert(PR_STATUS_VALID(pii->pii_probes[pr_ndx].pr_status));

	target = pii->pii_probes[pr_ndx].pr_target;

	/*
	 * Perform sanity checks, whether this probe reply that we
	 * have received is genuine
	 */
	if (target != NULL) {
		/*
		 * Compare the src. addr of the received ICMP or ICMPv6
		 * probe reply with the target address in our tables.
		 */
		if (!IN6_ARE_ADDR_EQUAL(&target->tg_address, &fromaddr)) {
			/*
			 * We don't have any record of having sent a probe to
			 * this target. This is a fake probe reply. Log an error
			 */
			logtrace("probe status %d Fake probe reply seq %u "
			    "snxt %u on %s from %s\n",
			    pii->pii_probes[pr_ndx].pr_status,
			    pr_icmp_seq, pii->pii_snxt, pii->pii_name, abuf);
			pii->pii_cum_stats.unknown++;
			return;
		} else if (pii->pii_probes[pr_ndx].pr_status == PR_ACKED) {
			/*
			 * The address matches, but our tables indicate that
			 * this probe reply has been acked already. So this
			 * is a duplicate probe reply. Log an error
			 */
			logtrace("probe status %d Duplicate probe reply seq %u "
			    "snxt %u on %s from %s\n",
			    pii->pii_probes[pr_ndx].pr_status,
			    pr_icmp_seq, pii->pii_snxt, pii->pii_name, abuf);
			pii->pii_cum_stats.unknown++;
			return;
		}
	} else {
		/*
		 * Target must not be NULL in the PR_UNACKED state
		 */
		assert(pii->pii_probes[pr_ndx].pr_status != PR_UNACKED);
		if (pii->pii_probes[pr_ndx].pr_status == PR_UNUSED) {
			/*
			 * The probe stats slot is unused. So we didn't
			 * send out any probe to this target. This is a fake.
			 * Log an error.
			 */
			logtrace("probe status %d Fake probe reply seq %u "
			    "snxt %u on %s from %s\n",
			    pii->pii_probes[pr_ndx].pr_status,
			    pr_icmp_seq, pii->pii_snxt, pii->pii_name, abuf);
		}
		pii->pii_cum_stats.unknown++;
		return;
	}

	/*
	 * If the rtt does not appear to be right, don't update the
	 * rtt stats. This can happen if the system dropped into the
	 * debugger, or the system was hung or too busy for a
	 * substantial time that we didn't get a chance to run.
	 */
	if ((m < 0) || (ns2ms(m) > PROBE_STATS_COUNT * pg->pg_probeint)) {
		/*
		 * If the probe corresponding to this received response
		 * was truly sent 'm' ns. ago, then this response must
		 * have been rejected by the sequence number checks. The
		 * fact that it has passed the sequence number checks
		 * means that the measured rtt is wrong. We were probably
		 * scheduled long after the packet was received.
		 */
		goto out;
	}

	/*
	 * Don't update rtt until we see NUM_PROBE_REPAIRS probe responses
	 * The initial few responses after the interface is repaired may
	 * contain high rtt's because they could have been queued up waiting
	 * for ARP/NDP resolution on a failed interface.
	 */
	if ((pii->pii_state != PI_RUNNING) || GROUP_FAILED(pg))
		goto out;

	/*
	 * Don't update the Conservative Round Trip Time estimate for this
	 * (phint, target) pair if this is the not the highest ack seq seen
	 * thus far on this target.
	 */
	if (!highest_ack_tg(pr_icmp_seq, target))
		goto out;

	/*
	 * Always update the rtt. This is a failure detection probe
	 * and we want to measure both increase / decrease in rtt.
	 */
	pi_set_crtt(target, m, _B_TRUE);

	/*
	 * If the crtt exceeds the average time between probes,
	 * investigate if this slow target is an exception. If so we
	 * can avoid this target and still meet the failure detection
	 * time. Otherwise we can't meet the failure detection time.
	 */
	if (target->tg_crtt > pg->pg_probeint) {
		exception = check_exception_target(pii, target);
		if (exception) {
			/*
			 * This target is exceptionally slow. Don't use it
			 * for future probes. check_exception_target() has
			 * made sure that we have at least MIN_PROBE_TARGETS
			 * other active targets
			 */
			if (pii->pii_targets_are_routers) {
				/*
				 * This is a slow router, mark it as slow
				 * and don't use it for further probes. We
				 * don't delete it, since it will be populated
				 * again when we do a router scan. Hence we
				 * need to maintain extra state (unlike the
				 * host case below).  Mark it as TG_SLOW.
				 */
				if (target->tg_status == TG_ACTIVE)
					pii->pii_ntargets--;
				target->tg_status = TG_SLOW;
				target->tg_latime = gethrtime();
				target->tg_rtt_sa = -1;
				target->tg_crtt = 0;
				target->tg_rtt_sd = 0;
				if (pii->pii_target_next == target) {
					pii->pii_target_next =
					    icmp_target_next(target);
				}
			} else {
				/*
				 * the slow target is not a router, we can
				 * just delete it. Send an icmp multicast and
				 * pick the fastest responder that is not
				 * already an active target.
				 * icmp_target_delete() adjusts
				 * pii->pii_target_next
				 */
				icmp_target_delete(target);
				icmp_probe(pii, PROBE_MULTI, cur_hrtime);
			}
		} else {
			/*
			 * We can't meet the failure detection time.
			 * Log a message, and update the detection time to
			 * whatever we can achieve.
			 */
			pg->pg_probeint = target->tg_crtt * NEXT_FDT_MULTIPLE;
			pg->pg_fdt = pg->pg_probeint * (NUM_PROBE_FAILS + 2);
			last_fdt_bumpup_time = gethrtime();
			if (pg != phyint_anongroup) {
				logtrace("Cannot meet requested failure"
				    " detection time of %d ms on (%s %s) new"
				    " failure detection time for group \"%s\""
				    " is %d ms\n", user_failure_detection_time,
				    AF_STR(pii->pii_af), pii->pii_name,
				    pg->pg_name, pg->pg_fdt);
			}
		}
	} else if ((target->tg_crtt < (pg->pg_probeint / LOWER_FDT_TRIGGER)) &&
	    (user_failure_detection_time < pg->pg_fdt) &&
	    (last_fdt_bumpup_time + MIN_SETTLING_TIME < gethrtime())) {
		/*
		 * If the crtt has now dropped by a factor of LOWER_FDT_TRIGGER
		 * investigate if we can improve the failure detection time to
		 * meet whatever the user specified.
		 */
		if (check_pg_crtt_improved(pg)) {
			pg->pg_fdt = MAX(pg->pg_fdt / NEXT_FDT_MULTIPLE,
			    user_failure_detection_time);
			pg->pg_probeint = pg->pg_fdt / (NUM_PROBE_FAILS + 2);
			if (pg != phyint_anongroup) {
				logtrace("Improved failure detection time %d ms"
				    " on (%s %s) for group \"%s\"\n",
				    pg->pg_fdt, AF_STR(pii->pii_af),
				    pii->pii_name, pg->pg_name);
			}
			if (user_failure_detection_time == pg->pg_fdt) {
				/* Avoid any truncation or rounding errors */
				pg->pg_probeint = user_probe_interval;
				/*
				 * No more rtt probes will be sent. The actual
				 * fdt has dropped to the user specified value.
				 * pii_fd_snxt_basetime and pii_snxt_basetime
				 * will be in sync henceforth.
				 */
				reset_snxt_basetimes();
			}
		}
	}
out:
	pr_statp = &pii->pii_probes[pr_ndx];
	pr_statp->pr_hrtime_ackproc = cur_hrtime;
	pr_statp->pr_hrtime_ackrecv = pr_statp->pr_hrtime_sent +
	    (tv2ns(recv_tvp) - tv2ns(&pr_statp->pr_tv_sent));

	probe_chstate(pr_statp, pii->pii_name, pii->pii_af, PR_ACKED);

	/*
	 * Update pii->pii_rack, i.e. the sequence number of the last received
	 * probe response, based on the echo reply we have received now, if
	 * either of the following conditions are satisfied.
	 * a. pii_rack is outside the current receive window of
	 *    [pii->pii_snxt - PROBE_STATS_COUNT, pii->pii_snxt).
	 *    This means we have not received probe responses for a
	 *    long time, and the sequence number has wrapped around.
	 * b. pii_rack is within the current receive window and this echo
	 *    reply corresponds to the highest sequence number we have seen
	 *    so far.
	 */
	if (SEQ_GE(pii->pii_rack, pii->pii_snxt) ||
	    SEQ_LT(pii->pii_rack, pii->pii_snxt - PROBE_STATS_COUNT) ||
	    SEQ_GT(pr_icmp_seq, pii->pii_rack)) {
		pii->pii_rack = pr_icmp_seq;
	}
}

/*
 * Returns true if seq is the highest unacknowledged seq for target tg
 * else returns false
 */
static boolean_t
highest_ack_tg(uint16_t seq, struct target *tg)
{
	struct phyint_instance *pii;
	int	 pr_ndx;
	uint16_t pr_seq;

	pii = tg->tg_phyint_inst;

	/*
	 * Get the seq number of the most recent probe sent so far,
	 * and also get the corresponding probe index in the probe stats
	 * array.
	 */
	pr_ndx = PROBE_INDEX_PREV(pii->pii_probe_next);
	pr_seq = pii->pii_snxt;
	pr_seq--;

	/*
	 * Start from the most recent probe and walk back, trying to find
	 * an acked probe corresponding to target tg.
	 */
	for (; pr_ndx != pii->pii_probe_next;
	    pr_ndx = PROBE_INDEX_PREV(pr_ndx), pr_seq--) {
		if (pii->pii_probes[pr_ndx].pr_target == tg &&
		    pii->pii_probes[pr_ndx].pr_status == PR_ACKED) {
			if (SEQ_GT(pr_seq, seq))
				return (_B_FALSE);
		}
	}
	return (_B_TRUE);
}

/*
 * Check whether the crtt for the group has improved by a factor of
 * LOWER_FDT_TRIGGER.  Small crtt improvements are ignored to avoid failure
 * detection time flapping in the face of small crtt changes.
 */
static boolean_t
check_pg_crtt_improved(struct phyint_group *pg)
{
	struct	phyint *pi;

	if (debug & D_PROBE)
		logdebug("check_pg_crtt_improved()\n");

	/*
	 * The crtt for the group is only improved if each phyint_instance
	 * for both ipv4 and ipv6 is improved.
	 */
	for (pi = pg->pg_phyint; pi != NULL; pi = pi->pi_pgnext) {
		if (!check_pii_crtt_improved(pi->pi_v4) ||
		    !check_pii_crtt_improved(pi->pi_v6))
			return (_B_FALSE);
	}

	return (_B_TRUE);
}

/*
 * Check whether the crtt has improved substantially on this phyint_instance.
 * Returns _B_TRUE if there's no crtt information available, because pii
 * is NULL or the phyint_instance is not capable of probing.
 */
boolean_t
check_pii_crtt_improved(struct phyint_instance *pii) {
	struct 	target *tg;

	if (pii == NULL)
		return (_B_TRUE);

	if (!PROBE_CAPABLE(pii) ||
	    pii->pii_phyint->pi_state == PI_FAILED)
		return (_B_TRUE);

	for (tg = pii->pii_targets; tg != NULL; tg = tg->tg_next) {
		if (tg->tg_status != TG_ACTIVE)
			continue;
		if (tg->tg_crtt > (pii->pii_phyint->pi_group->pg_probeint /
		    LOWER_FDT_TRIGGER)) {
			return (_B_FALSE);
		}
	}

	return (_B_TRUE);
}

/*
 * This target responds very slowly to probes. The target's crtt exceeds
 * the probe interval of its group. Compare against other targets
 * and determine if this target is an exception, if so return true, else false
 */
static boolean_t
check_exception_target(struct phyint_instance *pii, struct target *target)
{
	struct	target *tg;
	char abuf[INET6_ADDRSTRLEN];

	if (debug & D_PROBE) {
		logdebug("check_exception_target(%s %s target %s)\n",
		    AF_STR(pii->pii_af), pii->pii_name,
		    pr_addr(pii->pii_af, target->tg_address,
		    abuf, sizeof (abuf)));
	}

	/*
	 * We should have at least MIN_PROBE_TARGETS + 1 good targets now,
	 * to make a good judgement. Otherwise don't drop this target.
	 */
	if (pii->pii_ntargets <  MIN_PROBE_TARGETS + 1)
		return (_B_FALSE);

	/*
	 * Determine whether only this particular target is slow.
	 * We know that this target's crtt exceeds the group's probe interval.
	 * If all other active targets have a
	 * crtt < (this group's probe interval) / EXCEPTION_FACTOR,
	 * then this target is considered slow.
	 */
	for (tg = pii->pii_targets; tg != NULL; tg = tg->tg_next) {
		if (tg != target && tg->tg_status == TG_ACTIVE) {
			if (tg->tg_crtt >
			    pii->pii_phyint->pi_group->pg_probeint /
			    EXCEPTION_FACTOR) {
				return (_B_FALSE);
			}
		}
	}

	return (_B_TRUE);
}

/*
 * Update the target list. The icmp all hosts multicast has given us
 * some host to which we can send probes. If we already have sufficient
 * targets, discard it.
 */
static void
incoming_mcast_reply(struct phyint_instance *pii, struct pr_icmp *reply,
    struct in6_addr fromaddr)
/* ARGSUSED */
{
	int af;
	char abuf[INET6_ADDRSTRLEN];
	struct phyint *pi;

	if (debug & D_PROBE) {
		logdebug("incoming_mcast_reply(%s %s %s)\n",
		    AF_STR(pii->pii_af), pii->pii_name,
		    pr_addr(pii->pii_af, fromaddr, abuf, sizeof (abuf)));
	}

	/*
	 * Using host targets is a fallback mechanism. If we have
	 * found a router, don't add this host target. If we already
	 * know MAX_PROBE_TARGETS, don't add another target.
	 */
	assert(pii->pii_ntargets <= MAX_PROBE_TARGETS);
	if (pii->pii_targets != NULL) {
		if (pii->pii_targets_are_routers ||
		    (pii->pii_ntargets == MAX_PROBE_TARGETS)) {
			return;
		}
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&fromaddr) ||
	    IN6_IS_ADDR_V4MAPPED_ANY(&fromaddr)) {
		/*
		 * Guard against response from 0.0.0.0
		 * and ::. Log a trace message
		 */
		logtrace("probe response from %s on %s\n",
		    pr_addr(pii->pii_af, fromaddr, abuf, sizeof (abuf)),
		    pii->pii_name);
		return;
	}

	/*
	 * This address is one of our own, so reject this address as a
	 * valid probe target.
	 */
	af = pii->pii_af;
	if (own_address(fromaddr))
		return;

	/*
	 * If the phyint is part a named group, then add the address to all
	 * members of the group.  Otherwise, add the address only to the
	 * phyint itself, since other phyints in the anongroup may not be on
	 * the same subnet.
	 */
	pi = pii->pii_phyint;
	if (pi->pi_group == phyint_anongroup) {
		icmp_target_add(pii, fromaddr, _B_FALSE);
	} else {
		pi = pi->pi_group->pg_phyint;
		for (; pi != NULL; pi = pi->pi_pgnext) {
			if (IS_IPMP(pi))
				continue;
			icmp_target_add(PHYINT_INSTANCE(pi, af), fromaddr,
			    _B_FALSE);
		}
	}
}

/*
 * Compute CRTT given an existing scaled average, scaled deviation estimate
 * and a new rtt time.  The formula is from Jacobson and Karels'
 * "Congestion Avoidance and Control" in SIGCOMM '88.  The variable names
 * are the same as those in Appendix A.2 of that paper.
 *
 * m = new measurement
 * sa = scaled RTT average (8 * average estimates)
 * sv = scaled mean deviation (mdev) of RTT (4 * deviation estimates).
 * crtt = Conservative round trip time. Used to determine whether probe
 * has timed out.
 *
 * New scaled average and deviation are passed back via sap and svp
 */
static int64_t
compute_crtt(int64_t *sap, int64_t *svp, int64_t m)
{
	int64_t sa = *sap;
	int64_t sv = *svp;
	int64_t crtt;
	int64_t saved_m = m;

	assert(*sap >= -1);
	assert(*svp >= 0);

	if (sa != -1) {
		/*
		 * Update average estimator:
		 *	new rtt = old rtt + 1/8 Error
		 *	    where Error = m - old rtt
		 *	i.e. 8 * new rtt = 8 * old rtt + Error
		 *	i.e. new sa =  old sa + Error
		 */
		m -= sa >> 3;		/* m is now Error in estimate. */
		if ((sa += m) < 0) {
			/* Don't allow the smoothed average to be negative. */
			sa = 0;
		}

		/*
		 * Update deviation estimator:
		 *	new mdev =  old mdev + 1/4 (abs(Error) - old mdev)
		 *	i.e. 4 * new mdev = 4 * old mdev +
		 *		(abs(Error) - old mdev)
		 * 	i.e. new sv = old sv + (abs(Error) - old mdev)
		 */
		if (m < 0)
			m = -m;
		m -= sv >> 2;
		sv += m;
	} else {
		/* Initialization. This is the first response received. */
		sa = (m << 3);
		sv = (m << 1);
	}

	crtt = (sa >> 3) + sv;

	if (debug & D_PROBE) {
		logerr("compute_crtt: m = %lld sa = %lld, sv = %lld -> "
		    "crtt = %lld\n", saved_m, sa, sv, crtt);
	}

	*sap = sa;
	*svp = sv;

	/*
	 * CRTT = average estimates  + 4 * deviation estimates
	 *	= sa / 8 + sv
	 */
	return (crtt);
}

void
set_crtt(struct target *tg, int64_t m, boolean_t is_probe_uni, char *pi_name,
    int probe_interval)
{
	int64_t sa = tg->tg_rtt_sa;
	int64_t sv = tg->tg_rtt_sd;
	int new_crtt;
	int i;

	/* store the round trip time, in case we need to defer computation */
	tg->tg_deferred[tg->tg_num_deferred] = m;

	new_crtt = ns2ms(compute_crtt(&sa, &sv, m));

	/*
	 * If this probe's round trip time would singlehandedly cause an
	 * increase in the group's probe interval consider it suspect.
	 */
	if ((new_crtt > probe_interval) && is_probe_uni) {
		if (debug & D_PROBE) {
			logdebug("Received a suspect probe on %s, new_crtt ="
			    " %d, probe_interval = %d, num_deferred = %d\n",
			    pi_name, new_crtt,
			    probe_interval, tg->tg_num_deferred);
		}

		/*
		 * If we've deferred as many rtts as we plan on deferring, then
		 * assume the link really did slow down and process all queued
		 * rtts
		 */
		if (tg->tg_num_deferred == MAXDEFERREDRTT) {
			if (debug & D_PROBE) {
				logdebug("Received MAXDEFERREDRTT probes which "
				    "would cause an increased probe_interval.  "
				    "Integrating queued rtt data points.\n");
			}

			for (i = 0; i <= tg->tg_num_deferred; i++) {
				tg->tg_crtt = ns2ms(compute_crtt(&tg->tg_rtt_sa,
				    &tg->tg_rtt_sd, tg->tg_deferred[i]));
			}

			tg->tg_num_deferred = 0;
		} else {
			tg->tg_num_deferred++;
		}
		return;
	}

	/*
	 * If this is a normal probe, or an RTT probe that would lead to a
	 * reduced CRTT, then update our CRTT data.  Further, if this was
	 * a normal probe, pitch any deferred probes since our probes are
	 * again being answered within our CRTT estimates.
	 */
	if (is_probe_uni || new_crtt < tg->tg_crtt) {
		tg->tg_rtt_sa = sa;
		tg->tg_rtt_sd = sv;
		tg->tg_crtt = new_crtt;
		if (is_probe_uni)
			tg->tg_num_deferred = 0;
	}
}

static void
pi_set_crtt(struct target *tg, int64_t m, boolean_t is_probe_uni)
{
	struct phyint_instance *pii = tg->tg_phyint_inst;
	int probe_interval = pii->pii_phyint->pi_group->pg_probeint;

	if (debug & D_PROBE)
		logdebug("pi_set_crtt: target -  m %lld\n", m);
	set_crtt(tg, m, is_probe_uni, pii->pii_probe_logint->li_name,
	    probe_interval);
}


/*
 * Return a pointer to the specified option buffer.
 * If not found return NULL.
 */
static void *
find_ancillary(struct msghdr *msg, int cmsg_level, int cmsg_type)
{
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == cmsg_level &&
		    cmsg->cmsg_type == cmsg_type) {
			return (CMSG_DATA(cmsg));
		}
	}
	return (NULL);
}

/*
 * Try to activate another INACTIVE interface in the same group as `pi'.
 * Prefer STANDBY INACTIVE to just INACTIVE. Among the inactives, prefer the
 * one that can indirectly probe the largest set of other phyints.
 */
void
phyint_activate_another(struct phyint *pi)
{
	struct phyint *pi2;
	struct phyint *inactive_pi = NULL;
	int inactive_targ = 0, n;

	if (pi->pi_group == phyint_anongroup)
		return;

	for (pi2 = pi->pi_group->pg_phyint; pi2 != NULL; pi2 = pi2->pi_pgnext) {
		if (IS_IPMP(pi) || pi == pi2 || !phyint_is_functioning(pi2) ||
		    !(pi2->pi_flags & IFF_INACTIVE))
			continue;

		if (xprobe_interval > 0) {
			n = local_probe_target_count(pi2);
			if (n > inactive_targ || inactive_targ == 0) {
				inactive_pi = pi2;
				inactive_targ = n;
			}
		} else {
			inactive_pi = pi2;
		}
		if (pi2->pi_flags & IFF_STANDBY)
			break;
	}

	if (inactive_pi != NULL)
		(void) change_pif_flags(inactive_pi, 0, IFF_INACTIVE);
}

/*
 * Transition a phyint to PI_RUNNING.  The caller must ensure that the
 * transition is appropriate.  Clears IFF_OFFLINE or IFF_FAILED if
 * appropriate.  Also sets IFF_INACTIVE on this or other interfaces as
 * appropriate (see comment below).
 */
static void
phyint_transition_to_running_state(struct phyint *pi)
{
	struct phyint *pi2;
	struct phyint *actstandbypi = NULL;
	uint_t nactive = 0, nnonstandby = 0;
	boolean_t onlining = (pi->pi_state == PI_OFFLINE);
	boolean_t initial = (pi->pi_state == PI_INIT);
	uint64_t set, clear;

	/*
	 * The interface is running again, but should it or another interface
	 * in the group end up INACTIVE?  There are three cases:
	 *
	 * 1. If it's a STANDBY interface, it should be end up INACTIVE if
	 *    the group is operating at capacity (i.e., there are at least as
	 *    many active interfaces as non-STANDBY interfaces in the group).
	 *    No other interfaces should be changed.
	 *
	 * 2. If it's a non-STANDBY interface and we're onlining it or
	 *    FAILBACK is enabled, then it should *not* end up INACTIVE.
	 *    Further, if the group is above capacity as a result of this
	 *    interface, then an active STANDBY interface in the group should
	 *    end up INACTIVE.
	 *
	 * 3. If it's a non-STANDBY interface, we're repairing it, and
	 *    FAILBACK is disabled, then it should end up INACTIVE *unless*
	 *    the group was failed (in which case we have no choice but to
	 *    use it).  No other interfaces should be changed.
	 */
	if (pi->pi_group != phyint_anongroup) {
		pi2 = pi->pi_group->pg_phyint;
		for (; pi2 != NULL; pi2 = pi2->pi_pgnext) {
			if (IS_IPMP(pi2))
				continue;
			if (!(pi2->pi_flags & IFF_STANDBY))
				nnonstandby++;

			if (phyint_is_functioning(pi2) &&
			    !(pi2->pi_flags & IFF_INACTIVE)) {
				nactive++;
				if (pi2->pi_flags & IFF_STANDBY)
					actstandbypi = pi2;
			}
		}
	}

	set = 0;
	clear = (onlining ? IFF_OFFLINE : IFF_FAILED);

	if (pi->pi_flags & IFF_STANDBY) {			/* case 1 */
		if (nactive >= nnonstandby)
			set |= IFF_INACTIVE;
		else
			clear |= IFF_INACTIVE;
	} else if (onlining || failback_enabled) {		/* case 2 */
		if (nactive >= nnonstandby && actstandbypi != NULL) {
			(void) change_pif_flags(actstandbypi, IFF_INACTIVE, 0);
			actstandbypi->pi_fd_hrtime = gethrtime();
		}
	} else if (!initial && !GROUP_FAILED(pi->pi_group)) {	/* case 3 */
		set |= IFF_INACTIVE;
	}

	(void) change_pif_flags(pi, set, clear);
	phyint_chstate(pi, PI_RUNNING);
}

static void
phyint_transition_to_prober(struct phyint *pi)
{
	uint64_t set, clear;

	set = (IFF_PROBER | IFF_FAILED);
	clear =  IFF_INACTIVE;
	pi->pi_last_dpi = getcurrenttime();
	pi->pi_group->pg_has_dpi = 1;
	(void) change_pif_flags(pi, set, clear);
	phyint_chstate(pi, PI_FAILED);
	/*
	 * We've  just transitioned to PROBER, so we reset the state info,
	 * optimistically hoping that this will lead us to RUNNING.
	 */
	phyint_state_info(pi, PI_PROBER);
	phyint_group_refresh_state(pi->pi_group);
}

/*
 * Transition the in.mpathd and kernel state of the phyint to indicate that the
 * phyint is now running. Also updates the phyint's group * state to
 * account for the change.
 */
void
phyint_transition_to_running(struct phyint *pi)
{

	phyint_transition_to_running_state(pi);
	/*
	 * Update the group state to account for the change.
	 */
	phyint_group_refresh_state(pi->pi_group);
}

/*
 * Adjust IFF_INACTIVE on the provided `pi' to trend the group configuration
 * to have at least one active interface and as many active interfaces as
 * non-standby interfaces.
 */
void
phyint_standby_refresh_inactive(struct phyint *pi)
{
	struct phyint *pi2;
	uint_t nactive = 0, nnonstandby = 0;

	/*
	 * All phyints in the anonymous group are effectively in their own
	 * group and thus active regardless of whether they're marked standby.
	 */
	if (pi->pi_group == phyint_anongroup) {
		(void) change_pif_flags(pi, 0, IFF_INACTIVE);
		return;
	}

	/*
	 * If the phyint isn't functioning we can't consider it.
	 */
	if (!phyint_is_functioning(pi))
		return;

	for (pi2 = pi->pi_group->pg_phyint; pi2 != NULL; pi2 = pi2->pi_pgnext) {
		if (IS_IPMP(pi2))
			continue;

		if (!(pi2->pi_flags & IFF_STANDBY))
			nnonstandby++;

		if (phyint_is_functioning(pi2) &&
		    !(pi2->pi_flags & IFF_INACTIVE))
			nactive++;
	}

	if (nactive == 0 || nactive < nnonstandby)
		(void) change_pif_flags(pi, 0, IFF_INACTIVE);
	else if (nactive > nnonstandby)
		(void) change_pif_flags(pi, IFF_INACTIVE, 0);
}

/*
 * See if a previously failed interface has started working again.
 */
void
phyint_check_for_repair(struct phyint *pi)
{
	struct phyint *pi2;
	boolean_t group_failed = GROUP_FAILED(pi->pi_group);

	if (!phyint_repaired(pi))
		return;

	if (pi->pi_group == phyint_anongroup) {
		logerr("IP interface repair detected on %s\n", pi->pi_name);
	} else if (PROBE_ENABLED(pi->pi_v4) || PROBE_ENABLED(pi->pi_v6)) {
		logerr("IP interface repair detected on %s of group %s\n",
		    pi->pi_name, pi->pi_group->pg_name);
		if (group_failed) {
			group_failed = _B_FALSE;
			/*
			 * Clear the IFF_PROBER flag, which will deactivate
			 * the interface in preparation for its reactivation
			 * and restoration of the IPMP meta-interface
			 * status in phyint_transition_to_running()
			 */
			if (xprobe_interval > 0)
				(void) change_pif_flags(pi, 0, IFF_PROBER);
		}
	}
	/*
	 * If the interface is PI_OFFLINE, it can't be made PI_RUNNING yet.
	 * So just clear IFF_OFFLINE and defer phyint_transition_to_running()
	 * until it is brought back online.
	 */
	if (pi->pi_state == PI_OFFLINE) {
		(void) change_pif_flags(pi, 0, IFF_FAILED);
		return;
	}

	if (!group_failed || xprobe_interval == 0)
		phyint_transition_to_running(pi); /* calls phyint_chstate() */
	else
		phyint_transition_to_prober(pi); /* calls phyint_chstate() */

	if (xprobe_interval <= 0)
		return;

	/*
	 * At least one interface is now capable of probing. Adjust the state
	 * of all inactive interfaces based on the probe-capable interface. An
	 * interface that's been declared 'ok' based on link-state and/or
	 * transitive probing capability alone will now be declared failed if
	 * it cannot xprobe an active interface.
	 * phyint_transition_to_running() would have updated group state
	 */
	if (!pi->pi_group->pg_has_testaddrs && xprobe_interval > 0 &&
	    !group_failed) {
		/*
		 * first mark all the interfaces in my island as live.
		 */
		group_recovered(pi);
		/*
		 * now mark anything that cannot reach me as dead.
		 */
		for (pi2 = pi->pi_group->pg_phyint; pi2 != NULL;
		    pi2 = pi2->pi_pgnext) {
			if (pi2 == pi || PROBE_CAPABLE(pi2->pi_v4) ||
			    PROBE_CAPABLE(pi2->pi_v6) || IS_IPMP(pi2))
				continue;
			if (!transitive_probe_success(pi2, NULL)) {
				(void) change_pif_flags(pi2, IFF_FAILED,
				    IFF_INACTIVE);
				if (pi2->pi_state == PI_OFFLINE)
					continue;
				reset_crtt_all(pi2);
				if (pi2->pi_state == PI_RUNNING)
					phyint_chstate(pi2, PI_FAILED);
			}
		}
	}
}

/*
 * Do the necessary kernel/mpathd state adjustments for the failed phyint `pii'
 */
void
phyint_inst_fail(struct phyint_instance *pii)
{
	struct phyint *pi = pii->pii_phyint;
	boolean_t was_active = ((pi->pi_flags & IFF_INACTIVE) == 0);
	boolean_t prober = IS_PROBER(pi);

	(void) change_pif_flags(pi, IFF_FAILED, IFF_INACTIVE);
	if (!prober) {
		if (pi->pi_group == phyint_anongroup) {
			logerr("IP interface failure detected on %s\n",
			    pii->pii_name);
		} else {
			logerr("IP interface failure detected on "
			    "%s of group %s\n", pii->pii_name,
			    pi->pi_group->pg_name);
		}
	}

	/*
	 * If the failed interface was active, activate another
	 * INACTIVE interface in the group if possible.
	 */
	if (was_active)
		phyint_activate_another(pi);

	/*
	 * If the interface is offline, the state change will be
	 * noted when it comes back online.
	 */
	if (pi->pi_state != PI_OFFLINE) {
		phyint_chstate(pi, PI_FAILED);
		reset_crtt_all(pi);
		if (xprobe_interval > 0) {
			/*
			 * The designated prober failed. need to stop probing
			 * and reset any prober related state, including the
			 * IFF_PROBER flag.
			 */
			if (prober)
				phyint_reset_prober(pi);
			pii->pii_probe_logint = NULL;
			close_probe_socket(pii, _B_TRUE);
			pii->pii_phyint->pi_snxt_time = 0;
		}
	}
}

/*
 * See if an interface has failed, or if the whole group of interfaces has
 * failed.
 */
static void
phyint_inst_check_for_failure(struct phyint_instance *pii)
{
	struct phyint	*pi = pii->pii_phyint;
	struct phyint	*pi2;

	switch (failure_state(pii)) {
	case PHYINT_FAILURE:
		phyint_inst_fail(pii);
		break;

	case GROUP_FAILURE:
		pi2 = pi->pi_group->pg_phyint;
		for (; pi2 != NULL; pi2 = pi2->pi_pgnext) {
			(void) change_pif_flags(pi2, IFF_FAILED, IFF_INACTIVE);
			if (pi2->pi_state == PI_OFFLINE) /* see comment above */
				continue;

			reset_crtt_all(pi2);
			/*
			 * In the case of host targets, we would have flushed
			 * the targets, and gone to PI_NOTARGETS state.
			 */
			if (pi2->pi_state == PI_RUNNING)
				phyint_chstate(pi2, PI_FAILED);
			else if (IS_PROBER(pi2)) {
				phyint_reset_prober(pi2);
				phyint_state_info(pi2, PI_ICMP_PROBE_FAILED);
			}
		}
		break;

	default:
		break;
	}
}

/*
 * Determines if any timeout event has occurred and returns the number of
 * milliseconds until the next timeout event for the phyint. Returns
 * TIMER_INFINITY for "never".
 */
uint_t
phyint_inst_timer(struct phyint_instance *pii)
{
	int 	pr_ndx;
	uint_t	timeout;
	struct	target	*cur_tg;
	struct	probe_stats *pr_statp;
	struct	phyint_instance *pii_other;
	struct	phyint *pi;
	int	valid_unack_count;
	int	i;
	int	interval;
	uint_t	check_time;
	uint_t	cur_time;
	hrtime_t cur_hrtime;
	int	probe_interval = pii->pii_phyint->pi_group->pg_probeint;

	cur_hrtime = gethrtime();
	cur_time = ns2ms(cur_hrtime);

	if (debug & D_TIMER) {
		logdebug("phyint_inst_timer(%s %s)\n",
		    AF_STR(pii->pii_af), pii->pii_name);
	}

	pii_other = phyint_inst_other(pii);
	if (!PROBE_ENABLED(pii) && !PROBE_ENABLED(pii_other)) {
		/*
		 * Check to see if we're here due to link up/down flapping; If
		 * enough time has passed, then try to bring the interface
		 * back up; otherwise, schedule a timer to bring it back up
		 * when enough time *has* elapsed.
		 */
		pi = pii->pii_phyint;
		if (pi->pi_state == PI_FAILED && LINK_UP(pi)) {
			check_time = pi->pi_whenup[pi->pi_whendx] + MSEC_PERMIN;
			if (check_time > cur_time)
				return (check_time - cur_time);

			phyint_check_for_repair(pi);
		}
	}

	/*
	 * If probing is not enabled on this phyint instance, don't proceed.
	 * But ensure that the indirect probing status does not indicate
	 * interface failure.
	 */
	if (!PROBE_ENABLED(pii))
		return (TIMER_INFINITY);

	/*
	 * If the timer has fired too soon, probably triggered
	 * by some other phyint instance, return the remaining
	 * time
	 */
	if (TIME_LT(cur_time, pii->pii_snxt_time))
		return (pii->pii_snxt_time - cur_time);

	/*
	 * If the link is down, don't send any probes for now.
	 */
	if (LINK_DOWN(pii->pii_phyint))
		return (TIMER_INFINITY);

	/*
	 * Randomize the next probe time, between MIN_RANDOM_FACTOR
	 * and MAX_RANDOM_FACTOR with respect to the base probe time.
	 * Base probe time is strictly periodic.
	 */
	interval = GET_RANDOM(
	    (int)(MIN_RANDOM_FACTOR * user_probe_interval),
	    (int)(MAX_RANDOM_FACTOR * user_probe_interval));
	pii->pii_snxt_time = pii->pii_snxt_basetime + interval;

	/*
	 * Check if the current time > next time to probe. If so, we missed
	 * sending 1 or more probes, probably due to heavy system load. At least
	 * 'MIN_RANDOM_FACTOR * user_probe_interval' ms has elapsed since we
	 * were scheduled. Make adjustments to the times, in multiples of
	 * user_probe_interval.
	 */
	if (TIME_GT(cur_time, pii->pii_snxt_time)) {
		int n;

		n = (cur_time - pii->pii_snxt_time) / user_probe_interval;
		pii->pii_snxt_time 	+= (n + 1) * user_probe_interval;
		pii->pii_snxt_basetime 	+= (n + 1) * user_probe_interval;
		logtrace("missed sending %d probes cur_time %u snxt_time %u"
		    " snxt_basetime %u\n", n + 1, cur_time, pii->pii_snxt_time,
		    pii->pii_snxt_basetime);

		/* Collect statistics about missed probes */
		probes_missed.pm_nprobes += n + 1;
		probes_missed.pm_ntimes++;
	}
	pii->pii_snxt_basetime += user_probe_interval;
	interval = pii->pii_snxt_time - cur_time;
	if (debug & D_TARGET) {
		logdebug("cur_time %u snxt_time %u snxt_basetime %u"
		    " interval %u\n", cur_time, pii->pii_snxt_time,
		    pii->pii_snxt_basetime, interval);
	}

	/*
	 * If no targets are known, we need to send an ICMP multicast. The
	 * probe type is PROBE_MULTI.  We'll check back in 'interval' msec
	 * to see if we found a target.
	 */
	if (pii->pii_target_next == NULL) {
		assert(pii->pii_ntargets == 0);
		if (IS_PROBER(pii->pii_phyint) &&
		    phyint_inst_mcast_timeout(pii)) {
			phyint_inst_fail(pii);
			return (TIMER_INFINITY);
		} else {
			pii->pii_fd_snxt_basetime = pii->pii_snxt_basetime;
			icmp_probe(pii, PROBE_MULTI, cur_time);
			return (interval);
		}
	}

	if ((user_probe_interval != probe_interval) &&
	    TIME_LT(pii->pii_snxt_time, pii->pii_fd_snxt_basetime)) {
		/*
		 * the failure detection (fd) probe timer has not yet fired.
		 * Need to send only an rtt probe. The probe type is PROBE_RTT.
		 */
		icmp_probe(pii, PROBE_RTT, cur_hrtime);
		return (interval);
	}
	/*
	 * the fd probe timer has fired. Need to do all failure
	 * detection / recovery calculations, and then send an fd probe
	 * of type PROBE_UNI.
	 */
	if (user_probe_interval == probe_interval) {
		/*
		 * We could have missed some probes, and then adjusted
		 * pii_snxt_basetime above. Otherwise we could have
		 * blindly added probe_interval to pii_fd_snxt_basetime.
		 */
		pii->pii_fd_snxt_basetime = pii->pii_snxt_basetime;
	} else {
		pii->pii_fd_snxt_basetime += probe_interval;
		if (TIME_GT(cur_time, pii->pii_fd_snxt_basetime)) {
			int n;

			n = (cur_time - pii->pii_fd_snxt_basetime) /
			    probe_interval;
			pii->pii_fd_snxt_basetime += (n + 1) * probe_interval;
		}
	}

	/*
	 * We can have at most, the latest 2 probes that we sent, in
	 * the PR_UNACKED state. All previous probes sent, are either
	 * PR_LOST or PR_ACKED. An unacknowledged probe is considered
	 * timed out if the probe's time_start + the CRTT < currenttime.
	 * For each of the last 2 probes, examine whether it has timed
	 * out. If so, mark it PR_LOST. The probe stats is a circular array.
	 */
	pr_ndx = PROBE_INDEX_PREV(pii->pii_probe_next);
	valid_unack_count = 0;

	for (i = 0; i < 2; i++) {
		pr_statp = &pii->pii_probes[pr_ndx];
		cur_tg = pii->pii_probes[pr_ndx].pr_target;
		switch (pr_statp->pr_status) {
		case PR_ACKED:
			/*
			 * We received back an ACK, so the switch clearly
			 * is not dropping our traffic, and thus we can
			 * enable failure detection immediately.
			 */
			if (pii->pii_fd_hrtime > gethrtime()) {
				pii->pii_fd_hrtime = gethrtime();
				/* End quiet period for xprobe as well */
				pii->pii_phyint->pi_fd_hrtime = gethrtime();
				if (debug & D_PROBE) {
					logdebug("successful probe on %s; "
					    "ending quiet period at %x\n",
					    pii->pii_phyint->pi_name,
					    getcurrenttime());
				}
			}
			break;

		case PR_UNACKED:
			assert(cur_tg != NULL);
			/*
			 * The crtt could be zero for some reason,
			 * Eg. the phyint could be failed. If the crtt is
			 * not available use group's probe interval,
			 * which is a worst case estimate.
			 */
			timeout = ns2ms(pr_statp->pr_hrtime_start);
			if (cur_tg->tg_crtt != 0) {
				timeout += cur_tg->tg_crtt;
			} else {
				timeout += probe_interval;
			}
			if (TIME_LT(timeout, cur_time)) {
				pr_statp->pr_time_lost = timeout;
				probe_chstate(pr_statp, pii->pii_name,
				    pii->pii_af, PR_LOST);
			} else if (i == 1) {
				/*
				 * We are forced to consider this probe
				 * lost, as we can have at most 2 unack.
				 * probes any time, and we will be sending a
				 * probe at the end of this function.
				 * Normally, we should not be here, but
				 * this can happen if an incoming response
				 * that was considered lost has increased
				 * the crtt for this target, and also bumped
				 * up the FDT. Note that we never cancel or
				 * increase the current pii_time_left, so
				 * when the timer fires, we find 2 valid
				 * unacked probes, and they are yet to timeout
				 */
				pr_statp->pr_time_lost = cur_time;
				probe_chstate(pr_statp, pii->pii_name,
				    pii->pii_af, PR_LOST);
			} else {
				/*
				 * Only the most recent probe can enter
				 * this 'else' arm. The second most recent
				 * probe must take either of the above arms,
				 * if it is unacked.
				 */
				valid_unack_count++;
			}
			break;
		}
		pr_ndx = PROBE_INDEX_PREV(pr_ndx);
	}

	/*
	 * We send out 1 probe randomly in the interval between one half
	 * and one probe interval for the group. Given that the CRTT is always
	 * less than the group's probe interval, we can have at most 1
	 * unacknowledged probe now.  All previous probes are either lost or
	 * acked.
	 */
	assert(valid_unack_count == 0 || valid_unack_count == 1);

	/*
	 * The timer has fired. Take appropriate action depending
	 * on the current state of the phyint.
	 *
	 * PI_RUNNING state 	- Failure detection
	 * PI_FAILED state 	- Repair detection
	 */
	switch (pii->pii_phyint->pi_state) {
	case PI_FAILED:
		/*
		 * If the most recent probe (excluding unacked probes that
		 * are yet to time out) has been acked, check whether the
		 * phyint is now repaired.
		 */
		if (pii->pii_rack + valid_unack_count + 1 == pii->pii_snxt) {
			phyint_check_for_repair(pii->pii_phyint);
		}
		if (!IS_PROBER(pii->pii_phyint))
			break;
		/*
		 * For the PROBER, we need to eventually timeout so that we can
		 * round-robin over to another PROBER.
		 */
		/* FALLTHRU */
	case PI_RUNNING:
		/*
		 * It's possible our probes have been lost because of a
		 * spanning-tree mandated quiet period on the switch.  If so,
		 * ignore the lost probes.
		 */
		if (pii->pii_fd_hrtime - cur_hrtime > 0)
			break;

		if (pii->pii_rack + valid_unack_count + 1 != pii->pii_snxt) {
			/*
			 * We have 1 or more failed probes (excluding unacked
			 * probes that are yet to time out). Determine if the
			 * phyint has failed.
			 */
			phyint_inst_check_for_failure(pii);
		}
		break;

	default:
		logerr("phyint_inst_timer: invalid state %d\n",
		    pii->pii_phyint->pi_state);
		abort();
	}

	/*
	 * If a failure was detected, phyint_inst_check_for_failure() would
	 * have stopped probing on pii
	 */
	if (!PROBE_ENABLED(pii)) {
		assert(xprobe_interval > 0);
		return (TIMER_INFINITY);
	}
	/*
	 * Start the next probe. icmp_probe() will also set
	 * pii->pii_probe_time_left to the group's probe interval. If
	 * phyint_failed -> target_flush_hosts
	 * was called, the target list may be empty.
	 */
	if (pii->pii_target_next != NULL) {
		icmp_probe(pii, PROBE_UNI, cur_hrtime);
		/*
		 * If we have just the one probe target, and we're not using
		 * router targets, try to find another as we presently have
		 * no resilience.
		 */
		if (!pii->pii_targets_are_routers && pii->pii_ntargets == 1)
			icmp_probe(pii, PROBE_MULTI, cur_hrtime);
	} else {
		icmp_probe(pii, PROBE_MULTI, cur_hrtime);
	}
	return (interval);
}

/*
 * Start the probe timer for an interface instance.
 */
void
start_timer(struct phyint_instance *pii)
{
	uint32_t interval;

	/*
	 * Spread the base probe times (pi_snxt_basetime) across phyints
	 * uniformly over the (curtime..curtime + the group's probe_interval).
	 * pi_snxt_basetime is strictly periodic with a frequency of
	 * the group's probe interval. The actual probe time pi_snxt_time
	 * adds some randomness to pi_snxt_basetime and happens in icmp_probe().
	 * For the 1st probe on each phyint after the timer is started,
	 * pi_snxt_time and pi_snxt_basetime are the same.
	 */
	interval = GET_RANDOM(0,
	    (int)pii->pii_phyint->pi_group->pg_probeint);

	pii->pii_snxt_basetime = getcurrenttime() + interval;
	pii->pii_fd_snxt_basetime = pii->pii_snxt_basetime;
	pii->pii_snxt_time = pii->pii_snxt_basetime;
	timer_schedule(interval);
}

/*
 * Restart the probe timer on an interface instance.
 */
static void
restart_timer(struct phyint_instance *pii)
{
	/*
	 * We don't need to restart the timer if it was never started in
	 * the first place (pii->pii_basetime_inited not set), as the timer
	 * won't have gone off yet.
	 */
	if (pii->pii_basetime_inited != 0) {

		if (debug & D_LINKNOTE)
			logdebug("restart timer: restarting timer on %s, "
			    "address family %s\n", pii->pii_phyint->pi_name,
			    AF_STR(pii->pii_af));

		start_timer(pii);
	}
}

static void
process_link_state_down(struct phyint *pi)
{
	logerr("The link has gone down on %s\n", pi->pi_name);

	/*
	 * Clear the probe statistics arrays, we don't want the repair
	 * detection logic relying on probes that were successful prior
	 * to the link going down.
	 */
	if (PROBE_CAPABLE(pi->pi_v4))
		clear_pii_probe_stats(pi->pi_v4);
	if (PROBE_CAPABLE(pi->pi_v6))
		clear_pii_probe_stats(pi->pi_v6);
	phyint_state_info(pi, PI_LINK_DOWN);

	/*
	 * Check for interface failure.  Although we know the interface
	 * has failed, we don't know if all the other interfaces in the
	 * group have failed as well.
	 */
	if (pi->pi_state == PI_RUNNING || IS_PROBER(pi) ||
	    (pi->pi_state != PI_FAILED && !GROUP_FAILED(pi->pi_group))) {
		if (debug & D_LINKNOTE) {
			logdebug("process_link_state_down:"
			    " checking for failure on %s\n", pi->pi_name);
		}

		if (pi->pi_v4 != NULL)
			phyint_inst_check_for_failure(pi->pi_v4);
		else if (pi->pi_v6 != NULL)
			phyint_inst_check_for_failure(pi->pi_v6);
	}
}

static void
process_link_state_up(struct phyint *pi)
{
	logerr("The link has come up on %s\n", pi->pi_name);

	/*
	 * We stopped any running timers on each instance when the link
	 * went down, so restart them.
	 */
	if (pi->pi_v4)
		restart_timer(pi->pi_v4);
	if (pi->pi_v6)
		restart_timer(pi->pi_v6);

	phyint_check_for_repair(pi);

	pi->pi_whenup[pi->pi_whendx++] = getcurrenttime();
	if (pi->pi_whendx == LINK_UP_PERMIN)
		pi->pi_whendx = 0;
}

/*
 * Process any changes in link state passed up from the interfaces.
 */
void
process_link_state_changes(void)
{
	struct phyint *pi;

	/* Look for interfaces where the link state has just changed */

	for (pi = phyints; pi != NULL; pi = pi->pi_next) {
		boolean_t old_link_state_up = LINK_UP(pi);

		if IS_IPMP(pi)
			continue;
		/*
		 * Except when the "phyint" structure is created, this is
		 * the only place the link state is updated.  This allows
		 * this routine to detect changes in link state, rather
		 * than just the current state.
		 */
		UPDATE_LINK_STATE(pi);

		if (LINK_DOWN(pi)) {
			/*
			 * Has link just gone down?
			 */
			if (old_link_state_up)
				process_link_state_down(pi);
		} else {
			/*
			 * Has link just gone back up?
			 */
			if (!old_link_state_up)
				process_link_state_up(pi);
		}
	}
}

void
reset_crtt_all(struct phyint *pi)
{
	struct phyint_instance *pii;
	struct target *tg;

	if (debug & D_XPROBE)
		logdebug("resetting crtt of  %s\n", pi->pi_name);
	pii = pi->pi_v4;
	if (pii != NULL) {
		for (tg = pii->pii_targets; tg != NULL; tg = tg->tg_next) {
			tg->tg_crtt = 0;
			tg->tg_rtt_sa = -1;
			tg->tg_rtt_sd = 0;
		}
	}

	pii = pi->pi_v6;
	if (pii != NULL) {
		for (tg = pii->pii_targets; tg != NULL; tg = tg->tg_next) {
			tg->tg_crtt = 0;
			tg->tg_rtt_sa = -1;
			tg->tg_rtt_sd = 0;
		}
	}
}

/*
 * Check if the phyint has failed the last NUM_PROBE_FAILS consecutive
 * probes on both instances IPv4 and IPv6.
 * If the interface has failed, return the time of the first probe failure
 * in "tff".
 */
static int
phyint_inst_probe_failure_state(struct phyint_instance *pii, uint_t *tff)
{
	uint_t	pi_tff;
	struct	target *cur_tg;
	struct	probe_fail_count pfinfo;
	struct	phyint_instance *pii_other;
	int	pr_ndx;

	/*
	 * Get the number of consecutive failed probes on
	 * this phyint across all targets. Also get the number
	 * of consecutive failed probes on this target only
	 */
	pr_ndx = PROBE_INDEX_PREV(pii->pii_probe_next);
	cur_tg = pii->pii_probes[pr_ndx].pr_target;
	probe_fail_info(pii, cur_tg, &pfinfo);

	/* Get the time of first failure, for later use */
	pi_tff = pfinfo.pf_tff;

	/*
	 * If the current target has not responded to the
	 * last NUM_PROBE_FAILS probes, and other targets are
	 * responding delete this target. Dead gateway detection
	 * will eventually remove this target (if router) from the
	 * routing tables. If that does not occur, we may end
	 * up adding this to our list again.
	 */
	if (pfinfo.pf_nfail < NUM_PROBE_FAILS &&
	    pfinfo.pf_nfail_tg >= NUM_PROBE_FAILS) {
		if (pii->pii_targets_are_routers) {
			if (cur_tg->tg_status == TG_ACTIVE)
				pii->pii_ntargets--;
			cur_tg->tg_status = TG_DEAD;
			cur_tg->tg_crtt = 0;
			cur_tg->tg_rtt_sa = -1;
			cur_tg->tg_rtt_sd = 0;
			if (pii->pii_target_next == cur_tg)
				pii->pii_target_next = icmp_target_next(cur_tg);
		} else {
			icmp_target_delete(cur_tg);
			icmp_probe(pii, PROBE_MULTI, gethrtime());
		}
		return (PHYINT_OK);
	}

	/*
	 * If the phyint has lost NUM_PROBE_FAILS or more
	 * consecutive probes, on both IPv4 and IPv6 protocol
	 * instances of the phyint, then trigger failure
	 * detection, else return false
	 */
	if (pfinfo.pf_nfail < NUM_PROBE_FAILS)
		return (PHYINT_OK);

	pii_other = phyint_inst_other(pii);
	if (PROBE_CAPABLE(pii_other)) {
		probe_fail_info(pii_other, NULL, &pfinfo);
		if (pfinfo.pf_nfail >= NUM_PROBE_FAILS) {
			/*
			 * We have NUM_PROBE_FAILS or more failures
			 * on both IPv4 and IPv6. Get the earliest
			 * time when failure was detected on this
			 * phyint across IPv4 and IPv6.
			 */
			if (TIME_LT(pfinfo.pf_tff, pi_tff))
				pi_tff = pfinfo.pf_tff;
		} else {
			/*
			 * This instance has < NUM_PROBE_FAILS failure.
			 * So return false
			 */
			return (PHYINT_OK);
		}
	}
	*tff = pi_tff;
	return (PHYINT_FAILURE);
}

/*
 * Check if the link has gone down on this phyint, or it has failed the
 * last NUM_PROBE_FAILS consecutive probes on both instances IPv4 and IPv6.
 * Also look at other phyints of this group, for group failures.
 */
int
failure_state(struct phyint_instance *pii)
{
	struct	probe_success_count psinfo;
	uint_t	pi2_tls;		/* time last success */
	uint_t	pi_tff;			/* time first fail */
	struct	phyint *pi2;
	struct	phyint *pi;
	struct	phyint_instance *pii2;
	struct  phyint_group *pg;
	int	retval;

	if (debug & D_FAILREP)
		logdebug("phyint_failed(%s)\n", pii->pii_name);

	pi = pii->pii_phyint;
	pg = pi->pi_group;

	if (LINK_UP(pi)) {
		if (phyint_inst_probe_failure_state(pii, &pi_tff) == PHYINT_OK)
			return (PHYINT_OK);
		else
			phyint_icmp_prober_failed(pi);
	}

	/*
	 * At this point, the link is down, or the phyint is suspect, as it
	 * has lost NUM_PROBE_FAILS or more probes. If the phyint does not
	 * belong to any group, this is a PHYINT_FAILURE.  Otherwise, continue
	 * on to determine whether this should be considered a PHYINT_FAILURE
	 * or GROUP_FAILURE.
	 */
	if (pg == phyint_anongroup)
		return (PHYINT_FAILURE);

	/*
	 * Need to compare against other phyints of the same group
	 * to exclude group failures. If the failure was detected via
	 * probing, then if the time of last success (tls) of any
	 * phyint is more recent than the time of first fail (tff) of the
	 * phyint in question, and the link is up on the phyint,
	 * then it is a phyint failure. Otherwise it is a group failure.
	 * If failure was detected via a link down notification sent from
	 * the driver to IP, we see if any phyints in the group are still
	 * running and haven't received a link down notification.  We
	 * will usually be processing the link down notification shortly
	 * after it was received, so there is no point looking at the tls
	 * of other phyints.
	 */
	retval = GROUP_FAILURE;
	for (pi2 = pg->pg_phyint; pi2 != NULL; pi2 = pi2->pi_pgnext) {

		if (IS_PROBER(pi)) {
			assert(xprobe_interval > 0);
			/*
			 * Group has failed only when there are no more dpi's
			 */
			retval = PHYINT_FAILURE;
			break;
		}

		/* Exclude ourself from comparison */
		if (pi2 == pi)
			continue;

		if (LINK_DOWN(pi)) {
			/*
			 * We use FLAGS_TO_LINK_STATE() to test the flags
			 * directly, rather then LINK_UP() or LINK_DOWN(), as
			 * we may not have got round to processing the link
			 * state for the other phyints in the group yet.
			 *
			 * The check for PI_RUNNING and group failure handles
			 * the case when the group begins to recover.
			 * PI_RUNNING will be set, and group failure cleared
			 * only after receipt of NUM_PROBE_REPAIRS, by which
			 * time the other phyints should have received at
			 * least 1 packet, and so will not have NUM_PROBE_FAILS.
			 */
			if ((pi2->pi_state == PI_RUNNING || IS_PROBER(pi2)) ||
			    !GROUP_FAILED(pg) && FLAGS_TO_LINK_STATE(pi2)) {
				retval = PHYINT_FAILURE;
				break;
			}
			continue;
		}

		if (LINK_DOWN(pi2))
			continue;

		/*
		 * If there's no probe-based failure detection on this
		 * interface, and its link is still up, and indirect probing
		 * has not been enabled, then it's still working and thus
		 * the group has not failed.
		 */
		if (!PROBE_ENABLED(pi2->pi_v4) && !PROBE_ENABLED(pi2->pi_v6)) {
			if (xprobe_interval <= 0) {
				retval = PHYINT_FAILURE;
				break;
			}
			/*
			 * Indirect probing is enabled. Infer interface status
			 * by transitivity to distinguish between PHYINT and
			 * GROUP failure.
			 */
			if (!IS_IPMP(pi2) &&
			    transitive_probe_success(pi2, pi)) {
				retval = PHYINT_FAILURE;
				if (debug & D_XPROBE)
					logerr("%s alive, so PHYINT_FAILURE",
					    pi2->pi_name);
				break;
			} else {
				continue;
			}
		}

		/*
		 * Need to compare against both IPv4 and IPv6 instances.
		 */
		pii2 = pi2->pi_v4;
		if (pii2 != NULL) {
			icmp_probe_success_info(pii2, NULL, &psinfo);
			if (psinfo.ps_tls_valid) {
				pi2_tls = psinfo.ps_tls;
				/*
				 * See comment above regarding check
				 * for PI_RUNNING and group failure.
				 */
				if (TIME_GT(pi2_tls, pi_tff) &&
				    (pi2->pi_state == PI_RUNNING ||
				    IS_PROBER(pi2)) && !GROUP_FAILED(pg) &&
				    FLAGS_TO_LINK_STATE(pi2)) {
					retval = PHYINT_FAILURE;
					break;
				}
			}
		}

		pii2 = pi2->pi_v6;
		if (pii2 != NULL) {
			icmp_probe_success_info(pii2, NULL, &psinfo);
			if (psinfo.ps_tls_valid) {
				pi2_tls = psinfo.ps_tls;
				/*
				 * See comment above regarding check
				 * for PI_RUNNING and group failure.
				 */
				if (TIME_GT(pi2_tls, pi_tff) &&
				    (pi2->pi_state == PI_RUNNING ||
				    IS_PROBER(pi2)) && !GROUP_FAILED(pg) &&
				    FLAGS_TO_LINK_STATE(pi2)) {
					retval = PHYINT_FAILURE;
					break;
				}
			}
		}
	}

	/*
	 * Update the group state to account for the changes.
	 */
	phyint_group_refresh_state(pg);
	return (retval);
}

/*
 * Return the information associated with consecutive probe successes
 * starting with the most recent probe. At most the last 2 probes can be
 * in the unacknowledged state. All previous probes have either failed
 * or succeeded.
 */
static void
icmp_probe_success_info(struct phyint_instance *pii, struct target *cur_tg,
    struct probe_success_count *psinfo)
{
	uint_t	i;
	struct probe_stats *pr_statp;
	uint_t most_recent;
	uint_t second_most_recent;
	boolean_t pi_found_failure = _B_FALSE;
	boolean_t tg_found_failure = _B_FALSE;
	uint_t now;
	uint_t timeout;
	struct target *tg;

	if (debug & D_FAILREP)
		logdebug("probe_success_info(%s)\n", pii->pii_name);

	bzero(psinfo, sizeof (*psinfo));
	now = getcurrenttime();

	/*
	 * Start with the most recent probe, and count the number
	 * of consecutive probe successes. Latch the number of successes
	 * on hitting a failure.
	 */
	most_recent = PROBE_INDEX_PREV(pii->pii_probe_next);
	second_most_recent = PROBE_INDEX_PREV(most_recent);

	for (i = most_recent; i != pii->pii_probe_next;
	    i = PROBE_INDEX_PREV(i)) {
		pr_statp = &pii->pii_probes[i];

		switch (pr_statp->pr_status) {
		case PR_UNACKED:
			/*
			 * Only the most recent 2 probes can be unacknowledged
			 */
			assert(i == most_recent || i == second_most_recent);

			tg = pr_statp->pr_target;
			assert(tg != NULL);
			/*
			 * The crtt could be zero for some reason,
			 * Eg. the phyint could be failed. If the crtt is
			 * not available use the value of the group's probe
			 * interval which is a worst case estimate.
			 */
			timeout = ns2ms(pr_statp->pr_hrtime_start);
			if (tg->tg_crtt != 0) {
				timeout += tg->tg_crtt;
			} else {
				timeout +=
				    pii->pii_phyint->pi_group->pg_probeint;
			}

			if (TIME_LT(timeout, now)) {
				/*
				 * We hit a failure. Latch the total number of
				 * recent consecutive successes.
				 */
				pr_statp->pr_time_lost = timeout;
				probe_chstate(pr_statp, pii->pii_name,
				    pii->pii_af, PR_LOST);
				pi_found_failure = _B_TRUE;
				if (cur_tg != NULL && tg == cur_tg) {
					/*
					 * We hit a failure for the desired
					 * target. Latch the number of recent
					 * consecutive successes for this target
					 */
					tg_found_failure = _B_TRUE;
				}
			}
			break;

		case PR_ACKED:
			/*
			 * Bump up the count of probe successes, if we
			 * have not seen any failure so far.
			 */
			if (!pi_found_failure)
				psinfo->ps_nsucc++;

			if (cur_tg != NULL && pr_statp->pr_target == cur_tg &&
			    !tg_found_failure) {
				psinfo->ps_nsucc_tg++;
			}

			/*
			 * Record the time of last success, if this is
			 * the most recent probe success.
			 */
			if (!psinfo->ps_tls_valid) {
				psinfo->ps_tls =
				    ns2ms(pr_statp->pr_hrtime_ackproc);
				psinfo->ps_tls_valid = _B_TRUE;
			}
			break;

		case PR_LOST:
			/*
			 * We hit a failure. Latch the total number of
			 * recent consecutive successes.
			 */
			pi_found_failure = _B_TRUE;
			if (cur_tg != NULL && pr_statp->pr_target == cur_tg) {
				/*
				 * We hit a failure for the desired target.
				 * Latch the number of recent consecutive
				 * successes for this target
				 */
				tg_found_failure = _B_TRUE;
			}
			break;

		default:
			return;

		}
	}
}

/*
 * Return the information associated with consecutive probe successes
 * starting with the most recent probe. At most the last 2 probes can be
 * in the unacknowledged state. All previous probes have either failed
 * or succeeded.
 */
static void
local_probe_success_info(struct phyint *pi, struct target *cur_tg,
    struct probe_success_count *psinfo, uint_t *total)
{
	uint_t	i;
	struct probe_stats *pr_statp;
	uint_t most_recent;
	uint_t second_most_recent;
	boolean_t tg_found_failure = _B_FALSE;
	uint_t now;
	uint_t timeout;
	uint_t nsamples = 0;

	if (debug & D_FAILREP)
		logdebug("probe_success_info(%s)\n", pi->pi_name);

	bzero(psinfo, sizeof (*psinfo));
	now = getcurrenttime();

	/*
	 * Start with the most recent probe, and count the number
	 * of consecutive probe successes. Latch the number of successes
	 * on hitting a failure.
	 */
	most_recent = LPROBE_INDEX_PREV(cur_tg->tg_lprobe_next);
	second_most_recent = LPROBE_INDEX_PREV(most_recent);

	for (i = most_recent; i != cur_tg->tg_lprobe_next;
	    i = LPROBE_INDEX_PREV(i)) {
		pr_statp = &cur_tg->tg_lprobes[i];

		switch (pr_statp->pr_status) {
		case PR_UNACKED:
			/*
			 * Only the most recent 2 probes can be unacknowledged
			 */
			assert(i == most_recent || i == second_most_recent);

			/*
			 * The crtt could be zero for some reason,
			 * Eg. the phyint could be failed. If the crtt is
			 * not available use the value of the group's probe
			 * interval which is a worst case estimate.
			 */
			timeout = ns2ms(pr_statp->pr_hrtime_sent);
			nsamples++;

			if (cur_tg->tg_crtt != 0) {
				timeout += cur_tg->tg_crtt;
			} else {
				timeout += MAX(pi->pi_group->pg_probeint,
				    xprobe_interval);
			}

			if (TIME_LT(timeout, now)) {
				/*
				 * We hit a failure. Latch the total number of
				 * recent consecutive successes.
				 */
				pr_statp->pr_time_lost = timeout;
				if (debug & D_XPROBE)
					logdebug("local_probe_success_info("
					    "%s tg %s) marking %d as lost "
					    "at %llx \n", pi->pi_name,
					    cur_tg->tg_local->pi_name, i,
					    gethrtime());
				probe_chstate(pr_statp, pi->pi_name,
				    AF_LINK, PR_LOST);
				/*
				 * We hit a failure for the desired
				 * target. Latch the number of recent
				 * consecutive successes for this target
				 */
				tg_found_failure = _B_TRUE;
			}
			break;

		case PR_ACKED:

			nsamples++;
			if (!tg_found_failure)
				psinfo->ps_nsucc_tg++;

			/*
			 * Record the time of last success, if this is
			 * the most recent probe success.
			 */
			if (!psinfo->ps_tls_valid) {
				psinfo->ps_tls =
				    ns2ms(pr_statp->pr_hrtime_ackproc);
				psinfo->ps_tls_valid = _B_TRUE;
			}
			break;

		case PR_LOST:
			/*
			 * We hit a failure for the desired target.
			 * Latch the number of recent consecutive
			 * successes for this target
			 */
			tg_found_failure = _B_TRUE;
			if (ns2ms(pi->pi_fd_hrtime) < pr_statp->pr_time_lost)
				nsamples++;
			break;

		default:
			*total = nsamples;
			return;

		}
	}
	*total = nsamples;
}

/*
 * Return the information associated with consecutive probe failures
 * starting with the most recent probe. Only the last 2 probes can be in the
 * unacknowledged state. All previous probes have either failed or succeeded.
 */
static void
probe_fail_info(struct phyint_instance *pii, struct target *cur_tg,
    struct probe_fail_count *pfinfo)
{
	int	i;
	struct probe_stats *pr_statp;
	boolean_t	tg_found_success = _B_FALSE;
	boolean_t	pi_found_success = _B_FALSE;
	int	most_recent;
	int	second_most_recent;
	uint_t	now;
	uint_t	timeout;
	struct	target *tg;

	if (debug & D_FAILREP)
		logdebug("probe_fail_info(%s)\n", pii->pii_name);

	bzero(pfinfo, sizeof (*pfinfo));
	now = getcurrenttime();

	/*
	 * Start with the most recent probe, and count the number
	 * of consecutive probe failures. Latch the number of failures
	 * on hitting a probe success.
	 */
	most_recent = PROBE_INDEX_PREV(pii->pii_probe_next);
	second_most_recent = PROBE_INDEX_PREV(most_recent);

	for (i = most_recent; i != pii->pii_probe_next;
	    i = PROBE_INDEX_PREV(i)) {
		pr_statp = &pii->pii_probes[i];

		assert(PR_STATUS_VALID(pr_statp->pr_status));

		switch (pr_statp->pr_status) {
		case PR_UNACKED:
			/*
			 * Only the most recent 2 probes can be unacknowledged
			 */
			assert(i == most_recent || i == second_most_recent);

			tg = pr_statp->pr_target;
			/*
			 * Target is guaranteed to exist in the unack. state
			 */
			assert(tg != NULL);
			/*
			 * The crtt could be zero for some reason,
			 * Eg. the phyint could be failed. If the crtt is
			 * not available use the group's probe interval,
			 * which is a worst case estimate.
			 */
			timeout = ns2ms(pr_statp->pr_hrtime_start);
			if (tg->tg_crtt != 0) {
				timeout += tg->tg_crtt;
			} else {
				timeout +=
				    pii->pii_phyint->pi_group->pg_probeint;
			}

			if (TIME_GT(timeout, now))
				break;

			pr_statp->pr_time_lost = timeout;
			probe_chstate(pr_statp, pii->pii_name, pii->pii_af,
			    PR_LOST);
			/* FALLTHRU */

		case PR_LOST:
			if (!pi_found_success) {
				pfinfo->pf_nfail++;
				pfinfo->pf_tff = pr_statp->pr_time_lost;
			}
			if (cur_tg != NULL && pr_statp->pr_target == cur_tg &&
			    !tg_found_success)  {
				pfinfo->pf_nfail_tg++;
			}
			break;

		default:
			/*
			 * We hit a success or unused slot. Latch the
			 * total number of recent consecutive failures.
			 */
			pi_found_success = _B_TRUE;
			if (cur_tg != NULL && pr_statp->pr_target == cur_tg) {
				/*
				 * We hit a success for the desired target.
				 * Latch the number of recent consecutive
				 * failures for this target
				 */
				tg_found_success = _B_TRUE;
			}
		}
	}
}

/*
 * Change the state of probe `pr' on phyint_instance `pii' to state `state'.
 * probe_chstate() is called with a non-null pii for TG_LOCAL probes only when
 * collecting rtt statistics.
 */
void
probe_chstate(struct probe_stats *pr, const char *pi_name, int af, int state)
{
	struct target *tg = pr->pr_target;

	if (pr->pr_status == state)
		return;

	pr->pr_status = state;
	(void) probe_state_event(pr, pi_name, af,
	    (tg->tg_type == TG_ICMP ? NULL : tg->tg_local->pi_name));
}

/*
 * Check if the phyint has been repaired.  If no test address has been
 * configured, then consider the interface repaired if the link is up (unless
 * the link is flapping; see below).  Otherwise, look for proof of probes
 * being sent and received. If last NUM_PROBE_REPAIRS probes are fine on
 * either IPv4 or IPv6 instance, the phyint can be considered repaired.
 */
static boolean_t
phyint_repaired(struct phyint *pi)
{
	struct	probe_success_count psinfo;
	struct	phyint_instance *pii;
	struct	target *cur_tg;
	int	pr_ndx;
	uint_t	cur_time;

	if (debug & D_FAILREP)
		logdebug("phyint_repaired(%s)\n", pi->pi_name);

	if (LINK_DOWN(pi))
		return (_B_FALSE);

	/*
	 * If we don't have any test addresses and the link is up, then
	 * consider the interface repaired, unless we've received more than
	 * LINK_UP_PERMIN link up notifications in the last minute, in
	 * which case we keep the link down until we drop back below
	 * the threshold.
	 */
	if (!PROBE_ENABLED(pi->pi_v4) && !PROBE_ENABLED(pi->pi_v6)) {
		cur_time = getcurrenttime();
		if ((pi->pi_whenup[pi->pi_whendx] == 0 ||
		    (cur_time - pi->pi_whenup[pi->pi_whendx]) > MSEC_PERMIN)) {
			pi->pi_lfmsg_printed = 0;
			return (xprobe_interval > 0 ?
			    transitive_probe_success(pi, NULL) : _B_TRUE);
		}
		if (!pi->pi_lfmsg_printed) {
			logerr("The link has come up on %s more than %d times "
			    "in the last minute; disabling repair until it "
			    "stabilizes\n", pi->pi_name, LINK_UP_PERMIN);
			pi->pi_lfmsg_printed = 1;
		}

		return (_B_FALSE);
	}

	pii = pi->pi_v4;
	if (PROBE_CAPABLE(pii)) {
		pr_ndx = PROBE_INDEX_PREV(pii->pii_probe_next);
		cur_tg = pii->pii_probes[pr_ndx].pr_target;
		icmp_probe_success_info(pii, cur_tg, &psinfo);
		if (psinfo.ps_nsucc >= NUM_PROBE_REPAIRS ||
		    psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS) {
			return (_B_TRUE);
		}
	}

	pii = pi->pi_v6;
	if (PROBE_CAPABLE(pii)) {
		pr_ndx = PROBE_INDEX_PREV(pii->pii_probe_next);
		cur_tg = pii->pii_probes[pr_ndx].pr_target;
		icmp_probe_success_info(pii, cur_tg, &psinfo);
		if (psinfo.ps_nsucc >= NUM_PROBE_REPAIRS ||
		    psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS) {
			return (_B_TRUE);
		}
	}

	return (_B_FALSE);
}

static void
reset_local_probes(struct phyint *pi)
{
	struct target *tg;
	int i;

	for (tg = pi->pi_loc_all; tg != NULL; tg = tg->tg_next) {
		for (i = 0; i < LPROBE_STATS_COUNT; i++)
			tg->tg_lprobes[i].pr_status = PR_UNUSED;
	}
}

/*
 * Used to set/clear phyint flags, by making a SIOCSLIFFLAGS call.
 */
boolean_t
change_pif_flags(struct phyint *pi, uint64_t set, uint64_t clear)
{
	int ifsock;
	struct lifreq lifr;
	uint64_t old_flags;

	if (debug & D_FAILREP) {
		logdebug("change_pif_flags(%s): set %llx clear %llx\n",
		    pi->pi_name, set, clear);
	}

	/*
	 * If the interface is being activated, reset transitive probe stats.
	 */
	if (clear & IFF_INACTIVE)
		reset_local_probes(pi);

	/*
	 * if an active interface fails, start transitive probing. Active
	 * interfaces would have a pi_snxt_time of TIMER_INFINITY since they
	 * do not send out transitive probes.
	 */
	if ((set & IFF_FAILED) != 0 && pi->pi_snxt_time == TIMER_INFINITY)
		pi->pi_snxt_time = 0;
	if (pi->pi_v4 != NULL)
		ifsock = ifsock_v4;
	else
		ifsock = ifsock_v6;

	/*
	 * Get the current flags from the kernel, and set/clear the
	 * desired phyint flags. Since we set only phyint flags, we can
	 * do it on either IPv4 or IPv6 instance.
	 */
	(void) strlcpy(lifr.lifr_name, pi->pi_name, sizeof (lifr.lifr_name));

	if (ioctl(ifsock, SIOCGLIFFLAGS, (char *)&lifr) < 0) {
		if (errno != ENXIO)
			logperror("change_pif_flags: ioctl (get flags)");
		return (_B_FALSE);
	}

	old_flags = lifr.lifr_flags;
	lifr.lifr_flags |= set;
	lifr.lifr_flags &= ~clear;

	if (old_flags == lifr.lifr_flags) {
		/* No change in the flags. No need to send ioctl */
		return (_B_TRUE);
	}

	lifr.lifr_cflags.lcr_modflags = set;
	lifr.lifr_cflags.lcr_modmask = set | clear;
	lifr.lifr_cflags.lcr_origflags = 0;
	lifr.lifr_cflags.lcr_origmask = 0;
	if (ioctl(ifsock, SIOCCLIFFLAGS, (char *)&lifr) < 0) {
		if (errno != ENXIO)
			logperror("change_pif_flags: ioctl (change flags)");
		return (_B_FALSE);
	}

	/*
	 * Keep pi_flags in synch. with actual flags. Assumes flags are
	 * phyint flags.
	 */
	pi->pi_flags |= set;
	pi->pi_flags &= ~clear;

	if (pi->pi_v4 != NULL)
		pi->pi_v4->pii_flags = pi->pi_flags;

	if (pi->pi_v6 != NULL)
		pi->pi_v6->pii_flags = pi->pi_flags;

	return (_B_TRUE);
}

/*
 * icmp cksum computation for IPv4.
 */
static int
in_cksum(ushort_t *addr, int len)
{
	register int nleft = len;
	register ushort_t *w = addr;
	register ushort_t answer;
	ushort_t odd_byte = 0;
	register int sum = 0;

	/*
	 *  Our algorithm is simple, using a 32 bit accumulator (sum),
	 *  we add sequential 16 bit words to it, and at the end, fold
	 *  back all the carry bits from the top 16 bits into the lower
	 *  16 bits.
	 */
	while (nleft > 1)  {
		sum += *w++;
		nleft -= 2;
	}

	/* mop up an odd byte, if necessary */
	if (nleft == 1) {
		*(uchar_t *)(&odd_byte) = *(uchar_t *)w;
		sum += odd_byte;
	}

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return (answer);
}

static void
reset_snxt_basetimes(void)
{
	struct phyint_instance *pii;

	for (pii = phyint_instances; pii != NULL; pii = pii->pii_next) {
		pii->pii_fd_snxt_basetime = pii->pii_snxt_basetime;
	}
}

/*
 * Is the address one of our own addresses? Unfortunately,
 * we cannot check our phyint tables to determine if the address
 * is our own. This is because, we don't track interfaces that
 * are not part of any group. We have to either use a 'bind' or
 * get the complete list of all interfaces using SIOCGLIFCONF,
 * to do this check. We could also use SIOCTMYADDR.
 * Bind fails for the local zone address, so we might include local zone
 * address as target address. If local zone address is a target address
 * and it is up, it is not possible to detect the interface failure.
 * SIOCTMYADDR also doesn't consider local zone address as own address.
 * So, we choose to use SIOCGLIFCONF to collect the local addresses, and they
 * are stored in `localaddrs'
 */
boolean_t
own_address(struct in6_addr addr)
{
	addrlist_t *addrp;
	struct sockaddr_storage ss;
	int af = IN6_IS_ADDR_V4MAPPED(&addr) ? AF_INET : AF_INET6;

	addr2storage(af, &addr, &ss);
	for (addrp = localaddrs; addrp != NULL; addrp = addrp->al_next) {
		if (sockaddrcmp(&ss, &addrp->al_addr))
			return (_B_TRUE);
	}
	return (_B_FALSE);
}

static int
ns2ms(int64_t ns)
{
	return (ns / (NANOSEC / MILLISEC));
}

static int64_t
tv2ns(struct timeval *tvp)
{
	return (tvp->tv_sec * NANOSEC + tvp->tv_usec * 1000);
}

/*
 * Send out a transitive probe and return time of next send in ms
 */
uint_t
local_probe(struct phyint *pi, struct target *tg, hrtime_t start_hrtime)
{
	uint8_t *daddr;
	size_t pktsize;
	xprobe_data_t *pkt;
	struct probe_stats *prstatp;
	int i, pr_ndx;
	struct probe_stats *pr_statp;
	uint_t now = getcurrenttime();
	uint_t cur_time;
	hrtime_t cur_hrtime;
	int interval;

	if (LINK_DOWN(pi) || PROBE_ENABLED(pi->pi_v4) ||
	    PROBE_ENABLED(pi->pi_v6))
		return (TIMER_INFINITY);

	cur_hrtime = gethrtime();
	cur_time = ns2ms(cur_hrtime);
	/*
	 * We would like to have at most two unack'ed probes at any instant
	 * so each time we send a probe out we have to check the last 2 probes
	 * and declare any unack'ed probes as lost.
	 */
	pr_ndx = LPROBE_INDEX_PREV(tg->tg_lprobe_next);

	/* if timer fired too early for tg, wait for the scheduled send time */
	if (tg->tg_snxt_time > now)
		return (tg->tg_snxt_time);

	/*
	 * Randomize the next probe time, between MIN_RANDOM_FACTOR
	 * and MAX_RANDOM_FACTOR with respect to the base probe time.
	 * Base probe time is strictly periodic.
	 */
	interval = GET_RANDOM(
	    (int)(MIN_RANDOM_FACTOR * xprobe_interval),
	    (int)(MAX_RANDOM_FACTOR * xprobe_interval));
	tg->tg_snxt_time = xprobe_interval + interval;

	for (i = 0; i < 2; i++) {
		pr_statp = &tg->tg_lprobes[pr_ndx];
		switch (pr_statp->pr_status) {
		case PR_ACKED:
			if (pi->pi_fd_hrtime > cur_hrtime) {
				if (debug & D_XPROBE) {
					logdebug("successful xprobe on %s; "
					    "ending quiet period at %x \n",
					    pi->pi_name, getcurrenttime());
				}
				pi->pi_fd_hrtime = gethrtime();
			}
			break;
		case PR_UNACKED:
			if (i == 1) {
				if (pi->pi_fd_hrtime < cur_hrtime) {
					/*
					 * Probes lost during the
					 * spanning-tree mandated quiet period
					 * are ignored. Mark the pr_statp as
					 * unused, since we can have at most 2
					 * outstanding unacknowledged probes.
					 */
					pr_statp->pr_status = PR_UNUSED;
				}

				/*
				 * We are forced to consider this probe lost,
				 * as we can have at most 2 unacknowledged
				 * probes any time, and we will be sending a
				 * probe at the end of this function.
				 */
				if (debug & D_XPROBE)
					logdebug("local_probe marking oldest "
					    "%d as lost at %x \n", pr_ndx,
					    cur_time);
				pr_statp->pr_time_lost = cur_time;
				probe_chstate(pr_statp, pi->pi_name, AF_LINK,
				    PR_LOST);
			}
			break;
		}
		pr_ndx = LPROBE_INDEX_PREV(pr_ndx);
	}
	pktsize = sizeof (xprobe_data_t) + 2 * pi->pi_hwaddrlen + 2;
	pkt = (xprobe_data_t *)malloc(pktsize);
	(void) memset(pkt, 0, pktsize);
	daddr = (uint8_t *)&pkt[1];
	*(uint8_t *)daddr = pi->pi_hwaddrlen;
	daddr++;
	(void) memcpy(daddr, tg->tg_local->pi_hwaddr,
	    tg->tg_local->pi_hwaddrlen);
	/*
	 * some links, e.g., IB, don't provide the src hwaddr len as part
	 * of the DL_UNITDATA_IND, so we encode our address as well.
	 */
	daddr += pi->pi_hwaddrlen;
	*(uint8_t *)daddr = pi->pi_hwaddrlen;
	daddr++;
	(void) memcpy(daddr, pi->pi_hwaddr, pi->pi_hwaddrlen);

	pkt->xp_proto = ORCL_XPROBE_PROTO;
	pkt->xp_ts = cur_hrtime;
	pkt->xp_type = XP_REQUEST;
	pkt->xp_seq = tg->tg_snxt;
	if (write(dlpi_fd(pi->pi_ldh), pkt, pktsize) != pktsize) {
		logerr("write: %s", strerror(errno));
		free(pkt);
		return (TIMER_INFINITY);
	}
	prstatp = &tg->tg_lprobes[tg->tg_lprobe_next];
	prstatp->pr_id = tg->tg_snxt++;
	(void) gettimeofday(&prstatp->pr_tv_sent, NULL);
	prstatp->pr_hrtime_sent = pkt->xp_ts;
	prstatp->pr_hrtime_start = start_hrtime;
	prstatp->pr_target = tg;
	probe_chstate(prstatp, pi->pi_name, AF_LINK, PR_UNACKED);
	tg->tg_lprobe_next =  MOD_INCR(tg->tg_lprobe_next, LPROBE_STATS_COUNT);
	free(pkt);

	return (tg->tg_snxt_time);
}

/*
 * pi just inferred rechability to an icmp target. So we should update
 * the status of any alternate interfaces that were probing pi succesfully.
 */
static void
group_recovered(struct phyint *pi)
{
	struct phyint *xpi;
	struct probe_success_count psinfo;
	boolean_t repaired;
	struct target *tg;
	uint_t nsamples;

	if (debug & D_XPROBE) {
		logdebug("group_recovered because %s recovered\n",
		    pi->pi_name);
	}
	pi->pi_group->pg_has_dpi = 0;
	for (xpi = pi->pi_group->pg_phyint; xpi != NULL; xpi = xpi->pi_pgnext) {
		(void) change_pif_flags(pi, 0, IFF_PROBER);
		pi->pi_snxt_time = 0;
		if (xpi == pi || IS_IPMP(xpi) || xpi->pi_state == PI_RUNNING)
			continue;
		repaired = _B_FALSE;
		for (tg = xpi->pi_loc_all; tg != NULL; tg = tg->tg_next) {
			if (tg->tg_local == pi) {
				local_probe_success_info(pi, tg, &psinfo,
				    &nsamples);
				/*
				 * psinfo.ps_nsucc is not used for local probes
				 */
				if (psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS) {
					phyint_transition_to_running(xpi);
					repaired = _B_TRUE;
				}
			}
			if (repaired)
				break; /* next phyint */
		}
	}
}

/*
 * Return _B_TRUE if pi has succesfully completed ICMP based probing
 * within the last xprobe_interval seconds
 */
static boolean_t
target_is_icmp_probing(struct phyint *pi)
{
	struct probe_success_count psinfo;
	uint_t now = getcurrenttime();
	uint_t pi_when_ms;

	if (!PROBE_ENABLED(pi->pi_v4) && !PROBE_ENABLED(pi->pi_v6) ||
	    pi->pi_state_info.pi_why != PI_SUCCESS)
		return (_B_FALSE);

	/*
	 * If `pi' has just recovered, allow ourselves enough time to
	 * send NUM_PROBE_REPAIRS to it
	 */
	pi_when_ms = ns2ms(pi->pi_state_info.pi_when);
	if (pi_when_ms > 0 && (getcurrenttime() - pi_when_ms <
	    xprobe_interval * (NUM_PROBE_REPAIRS + 1))) {
		if (debug & D_XPROBE)
			logdebug("%s hasn't been ok for long enough\n",
			    pi->pi_name);
		return (_B_FALSE);
	}

	/*
	 * Return TRUE if pi is sending ICMP probes, and has had a probe
	 * success within the last two probe intervals.
	 */
	if (PROBE_ENABLED(pi->pi_v4)) {
		icmp_probe_success_info(pi->pi_v4, NULL, &psinfo);
		if (psinfo.ps_tls_valid &&
		    (now - psinfo.ps_tls) < 2 * pi->pi_group->pg_probeint)
			return (_B_TRUE);
	}
	if (PROBE_ENABLED(pi->pi_v6)) {
		icmp_probe_success_info(pi->pi_v6, NULL, &psinfo);
		if (psinfo.ps_tls_valid &&
		    (now - psinfo.ps_tls) < 2 * pi->pi_group->pg_probeint)
			return (_B_TRUE);
	}
	return (_B_FALSE);
}

/*
 * Determine the liveness status of alternate interafaces.
 * As long as we are not doing any ICMP based failure detection, we will
 * return status quo (i.e., true, if the interface is currently not failed,
 * false otherwise). If there is at least one PROBE_ENABLED interface, then
 * the retval is determined by our ability to probe that PROBE_ENABLED
 * interface. In the group failure situation, return TRUE for the first
 * interface in an island (gets nominated as the designated prober) and FALSE
 * for other interfaces in the island.
 */
static boolean_t
transitive_probe_success(struct phyint *pi, struct phyint *exclude)
{
	struct probe_success_count psinfo;
	boolean_t retval = (pi->pi_state != PI_FAILED);
	struct target *tg;
	uint_t nsamples;

	if (IS_PROBER(pi))
		return (_B_FALSE);

	if (debug & D_XPROBE)
		logdebug("%s default retval %s\n", pi->pi_name,
		    (retval ? "true" : "false"));
	/*
	 * The default return value is to retain status quo, except in the
	 * group failure case where we want to allow a failed interface to
	 * become the designated probing interface.
	 */
	if (GROUP_FAILED(pi->pi_group)) {
		if (debug & D_XPROBE)
			logdebug("%s group failed state %s\n", pi->pi_name,
			    (retval ? "true" : "false"));
		retval = _B_TRUE;
	}

	for (tg = pi->pi_loc_all; tg != NULL; tg = tg->tg_next) {
		/*
		 * 'exclude' is non-null when some interface has failed, and
		 * we need to determine whether this is PHYINT_FAILURE or
		 * GROUP_FAILURE by examining each interface in the group.
		 *
		 * Anything that is still successfully sending indirect probes
		 * to  a non-null 'exclude' has also FAILED.
		 */
		if (tg->tg_local == exclude) {
			assert(exclude != NULL);
			local_probe_success_info(pi, tg, &psinfo, &nsamples);
			if (nsamples >= NUM_PROBE_REPAIRS &&
			    psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS) {
				if (debug & D_XPROBE) {
					logdebug("%s is probing the failed %s "
					    "so it has also failed\n",
					    pi->pi_name, exclude->pi_name);
				}
				return (_B_FALSE);
			}
			continue;
		}
		if (exclude == NULL) {
			/*
			 * `exclude' is null when we are checking if the phyint
			 * has repaired. In this case,  IS_PROBER(tg->tg_local)
			 * indicates that the target has been
			 * nominated as the designated prober after group
			 * failure was detected. If pi can successfully send
			 * indirect probes to the target, then we are in the
			 * same island as target, and we back off.
			 */
			if (IS_PROBER(tg->tg_local)) {
				local_probe_success_info(pi, tg, &psinfo,
				    &nsamples);
				if (nsamples >= NUM_PROBE_REPAIRS &&
				    psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS) {
					if (debug & D_XPROBE)
						logdebug("%s on same island as "
						    "%s\n", pi->pi_name,
						    tg->tg_local->pi_name);
					return (_B_FALSE);
				}
			}
		}
		if (!target_is_icmp_probing(tg->tg_local))
			continue;
		if (debug & D_XPROBE) {
			logdebug("target %s is probe enabled\n",
			    tg->tg_local->pi_name);
		}

		local_probe_success_info(pi, tg, &psinfo, &nsamples);
		/*
		 * psinfo.ps_nsucc is not used for local probes
		 */
		if (nsamples >= NUM_PROBE_REPAIRS &&
		    psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS &&
		    tg->tg_local->pi_state == PI_RUNNING) {
			/*
			 * Local probes to some target other than exclude are
			 * succeeding.
			 */
			if (debug & D_XPROBE) {
				logdebug("probing tg %s succesfully \n",
				    tg->tg_local->pi_name);
			}
			retval = _B_TRUE;
			break;
		} else if (nsamples >= NUM_PROBE_REPAIRS) {
			if (debug & D_XPROBE)
				logdebug("%s -> %s psinfo %p "
				    "nsamples %d ps_nsucc_tg %d\n",
				    pi->pi_name, tg->tg_local->pi_name,
				    (void *)&psinfo, nsamples,
				    psinfo.ps_nsucc_tg);
			retval = _B_FALSE;
		}
	}
	if (GROUP_FAILED(pi->pi_group) && !IS_PROBER(pi)) {
		/*
		 * In the GROUP_FAILED mode, activate exactly one interface.
		 */
		if (pi->pi_group->pg_has_dpi != 0) {
			if (debug & D_XPROBE)
				logdebug("only 1 dpi allowed\n");
			retval = _B_FALSE;
		}

		/* Round-robin between islands  */
		if (!local_dpi_rr(pi)) {
			if (debug & D_XPROBE)
				logdebug("!local_dpi_rr\n");
			retval = _B_FALSE;
		}
	}
	if (debug & D_XPROBE) {
		logdebug("transitive_probe_success (%s): %s\n",
		    pi->pi_name, (retval? "true" : "false"));
	}
	return (retval);
}

/*
 * Return the number of local targets that 'pi' can successfully probe using
 * indirect probing.
 */
static int
local_probe_target_count(struct phyint *pi)
{
	struct probe_success_count psinfo;
	struct target *tg;
	int count = 0;
	uint_t nsamples;

	for (tg = pi->pi_loc_all; tg != NULL; tg = tg->tg_next) {

		local_probe_success_info(pi, tg, &psinfo, &nsamples);

		if (psinfo.ps_nsucc_tg >= NUM_PROBE_REPAIRS &&
		    (tg->tg_local->pi_state == PI_RUNNING ||
		    IS_PROBER(tg->tg_local))) {
			count++;
		}
	}
	return (count);
}

/*
 * Return TRUE if it is pi's turn to be the designated prober.
 */
static boolean_t
local_dpi_rr(struct phyint *pi)
{
	struct phyint *p, *maybe_prober, *next_prober = NULL;
	uint_t pi_last_dpi, min_dpi = UINT_MAX;
	struct target *tg;
	struct probe_success_count psinfo;
	uint_t nsamples;

	for (p = pi->pi_group->pg_phyint; p != NULL; p = p->pi_pgnext) {

		if (IS_IPMP(p) || (p->pi_flags & IFF_UP) == 0 || !LINK_UP(p))
			continue;

		pi_last_dpi = p->pi_last_dpi;
		maybe_prober = p;
		for (tg = p->pi_loc_all; tg != NULL; tg = tg->tg_next) {

			if (!LINK_UP(tg->tg_local))
				continue;

			local_probe_success_info(p, tg, &psinfo, &nsamples);
			/*
			 * We should have successfully probed in the last 2
			 * xprobe_interval periods.
			 */

			if (!psinfo.ps_tls_valid ||
			    psinfo.ps_tls > (2 * xprobe_interval))
				continue;
			/*
			 * tg is in our island. Update pi_last_dpi so that it is
			 * the smallest non-zero dpi for the island.
			 */
			if (TIME_GT(tg->tg_local->pi_last_dpi, pi_last_dpi)) {
				pi_last_dpi = tg->tg_local->pi_last_dpi;
				maybe_prober = tg->tg_local;
			}
		}
		if (debug & D_XPROBE)
			logdebug("local_dpi_rr: %s dpi_last %x\n",
			    p->pi_name, pi_last_dpi);
		if (pi_last_dpi < min_dpi) {
			min_dpi = pi_last_dpi;
			next_prober = maybe_prober;
			if (debug & D_XPROBE)
				logdebug("next_prober now %p dpi_last %x\n",
				    (void *)next_prober, pi_last_dpi);
		}
	}
	if (debug & D_XPROBE)
		logdebug("local_dpi_rr(%s) next_prober %s dpi_last %x\n",
		    pi->pi_name, next_prober->pi_name, pi_last_dpi);

	if (pi == next_prober || (min_dpi == 0 && pi->pi_last_dpi == min_dpi))
		return (_B_TRUE);
	else
		return (_B_FALSE);
}

/*
 * Check if an alternate interface `pi' has failed.
 */
static void
phyint_check_for_failure(struct phyint *pi)
{
	int orig_state = pi->pi_state;

	if (PROBE_ENABLED(pi->pi_v4) || PROBE_ENABLED(pi->pi_v6) ||
	    IS_IPMP(pi) || GROUP_FAILED(pi->pi_group) ||
	    xprobe_interval == 0)
		return;
	/*
	 * If pi cannot successfully probe an actively probing interface, it
	 * has failed.
	 */
	if (!transitive_probe_success(pi, NULL)) {
		if (orig_state != PI_FAILED) {
			logerr("Interface failure detected on %s\n",
			    pi->pi_name);
			(void) change_pif_flags(pi, IFF_FAILED, IFF_INACTIVE);
			if (pi->pi_state == PI_OFFLINE)
				return;
			reset_crtt_all(pi);
			assert(!IS_PROBER(pi));
			if (pi->pi_state == PI_RUNNING) {
				phyint_chstate(pi, PI_FAILED);
				phyint_group_refresh_state(pi->pi_group);
			}
		}
	}
}

/*
 * Timer to handle periodic sends of indirect probes. Determine if any
 * indirect probe timeout event has occurred and return the number of
 * milliseconds until the next indirect probe timeout event.
 */
uint_t
phyint_timer()
{
	struct phyint *pi;
	hrtime_t start_hrtime;
	uint_t tnext = 0, start_mstime;
	uint_t tmptime, qtime = 0, next = TIMER_INFINITY;
	struct target *tg;

	if (xprobe_interval == 0)
		return (next);

	start_hrtime = gethrtime();
	start_mstime = getcurrenttime();
	for (pi = phyints; pi != NULL; pi = pi->pi_next) {

		if (IS_IPMP(pi) || pi->pi_group->pg_has_testaddrs)
			continue;
		if (TIME_LT(start_mstime, pi->pi_snxt_time)) {
			next = MIN(next, pi->pi_snxt_time - start_mstime);
			continue;
		}
		/*
		 * allow for spanning tree mandated quiet time, plus at least
		 * NUM_PROBE_REPAIRS + 1 indirect probes.
		 */
		qtime = ns2ms(pi->pi_fd_hrtime);
		if (qtime < start_mstime)
			phyint_check_for_failure(pi);

		for (tg = pi->pi_loc_all; tg != NULL; tg = tg->tg_next) {
			tmptime = local_probe(pi, tg, start_hrtime);
			if (tnext == 0 || tmptime < tnext)
				tnext = tmptime;
		}
		pi->pi_snxt_time = tnext;
		next = MIN(next, pi->pi_snxt_time - start_mstime);
	}
	return (next);
}

/*
 * Avoid flapping when an ICMP prober fails: if an alternate interface
 * declares failure based on its inability to send transitive probes to the
 * current ICMP prober "just before" (and due to the same reason as) the last
 * ICMP prober itself declares failure, we optimistically mark the alternate
 * interface as alive. Here the definition of "just before" is based on
 * XPROBE_SETTLING_TIME
 *
 * Note that transitive_probe_success() only marks an alternate interface
 * as failed if it cannot probe *any* ICMP prober, and
 * phyint_icmp_prober_failed() will only do the optimistic reset when the
 * last ICMP prober fails.
 */
static void
phyint_icmp_prober_failed(struct phyint *pi)
{
	struct phyint_group *pg;
	struct phyint *p;

	pg = pi->pi_group;
	for (p = pg->pg_phyint; p != NULL; p = p->pi_pgnext) {
		if (IS_IPMP(p) || p == pi)
			continue;
		if (PROBE_ENABLED(p->pi_v4) || PROBE_ENABLED(p->pi_v6))
			return; /* not the last ICMP prober */
	}
	for (p = pg->pg_phyint; p != NULL; p = p->pi_pgnext) {
		if (IS_IPMP(p) || p == pi)
			continue;
		if (p->pi_state_info.pi_why == PI_XPROBE_FAILED &&
		    p->pi_state_info.pi_when +
		    XPROBE_SETTLING_TIME > gethrtime())
			phyint_transition_to_running_state(p);
	}
}

/*
 * In the PROBE_MULTI probing mode, we make sure each dpi is allowed to send
 * trial mcast probes for a time-slice of 15s (i.e., approximately
 * 3 * NUM_PROBE_FAILS mcast probes). If the phyint does not recover in
 * that time-slice, we timeout the phyint and move on to another island
 * in the group.
 */
boolean_t
phyint_inst_mcast_timeout(struct phyint_instance *pii)
{
	hrtime_t	now = gethrtime(), slice;

	slice = 3 * NUM_PROBE_FAILS * 1000000000LL;
	return (now - pii->pii_phyint->pi_state_info.pi_when > slice);
}

/*
 * Reset the PROBER related state for `pi'
 */
void
phyint_reset_prober(struct phyint *pi)
{
	pi->pi_snxt_time = 0;
	pi->pi_group->pg_has_dpi = 0;
	(void) change_pif_flags(pi, 0, IFF_PROBER);
	if (debug & D_XPROBE)
		logdebug("done phyint_reset_prober(%s) flags now %llx\n",
		    pi->pi_name, pi->pi_flags);
}
