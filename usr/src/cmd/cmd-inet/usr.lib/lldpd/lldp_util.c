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
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netpacket/packet.h>
#include <errno.h>
#include <libdladm.h>
#include <libdllink.h>
#include <sys/dld_ioc.h>
#include <sys/dld.h>
#include <signal.h>
#include <sys/utsname.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"

static void
lldp_timer(void *args)
{
	lldp_agent_t		*la = (lldp_agent_t *)args;
	struct timeval		timeout_time;

	lldp_mutex_lock(&la->la_mutex);
	la->la_txTick = B_TRUE;
	if (la->la_txTTR > 0)
		la->la_txTTR--;
	if (la->la_tx_state == LLDP_PORT_SHUTDOWN && la->la_txShutdownWhile > 0)
		la->la_txShutdownWhile--;
	lldp_mutex_unlock(&la->la_mutex);
	(void) pthread_cond_broadcast(&la->la_cond_var);

	/* if we have the last reference, do not restart the timer */
	if (la->la_refcnt == 1) {
		lldp_agent_refcnt_decr(la);
		return;
	}

	/* Restart timer */
	bzero(&timeout_time, sizeof (struct timeval));
	timeout_time.tv_sec = 1;
	la->la_timer_tid = lldp_timeout((void *)la, lldp_timer, &timeout_time);
}

/* Open a PF_PACKET socket on the linkid specified */
static int
lldp_open_socket(datalink_id_t linkid, boolean_t isaggr)
{
	int			sockfd;
	struct packet_mreq	pmreq;
	uint8_t			mcast_addr[8] = LLDP_GROUP_ADDRESS;
	struct sockaddr_ll	sll;

	/*
	 * Set up the PF_PACKET socket. ETH_P_ALL not yet defined.
	 */
	if ((sockfd = socket(PF_PACKET, SOCK_RAW, ETHERTYPE_LLDP)) == -1) {
		syslog(LOG_ERR, "Couldn't create PF_PACKET socket");
		return (-1);
	}

	(void) memset(&sll, 0, sizeof (sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = (int32_t)linkid;
	sll.sll_protocol = ETHERTYPE_LLDP;
	if (bind(sockfd, (struct sockaddr *)&sll, sizeof (sll)) == -1) {
		syslog(LOG_ERR, "Couldn't bind to the socket");
		(void) close(sockfd);
		return (-1);
	}

	/*
	 * If this link is part of an aggr we have to be in promisc
	 * mode to get packets because the underlying link will be
	 * in exclusive mode.
	 */
	if (isaggr)
		return (sockfd);

	bzero(&pmreq, sizeof (pmreq));
	pmreq.mr_ifindex = linkid;
	pmreq.mr_type = PACKET_MR_MULTICAST;
	pmreq.mr_alen =  ETHERADDRL;
	bcopy(mcast_addr, pmreq.mr_address, ETHERADDRL);

	if (setsockopt(sockfd, SOL_PACKET,
	    PACKET_ADD_MEMBERSHIP, &pmreq, sizeof (pmreq)) != 0) {
		syslog(LOG_ERR, "Failed to perform setsockopt()");
		(void) close(sockfd);
		return (-1);
	}

	bzero(&pmreq, sizeof (pmreq));
	pmreq.mr_ifindex = linkid;
	pmreq.mr_type = PACKET_MR_PROMISC;
	pmreq.mr_alen =  ETHERADDRL;
	if (setsockopt(sockfd, SOL_PACKET, PACKET_DROP_MEMBERSHIP, &pmreq,
	    sizeof (pmreq)) != 0) {
		syslog(LOG_ERR, "Failed to perform setsockopt()");
		(void) close(sockfd);
		return (-1);
	}

	return (sockfd);
}

/*
 * Initialize the local mib with information about this agent.
 */
static int
lldp_init_local_mib(lldp_agent_t *lap)
{
	int			err = 0;
	nvlist_t		*tlv_nvl;
	char			pidstr[LLDP_MAX_PORTIDSTRLEN];
	char			cidstr[LLDP_MAX_CHASSISIDSTRLEN];
	lldp_chassisid_t	cid;
	lldp_portid_t		pid;

	if (nvlist_alloc(&tlv_nvl, NV_UNIQUE_NAME, 0) != 0)
		return (ENOMEM);

	lldp_get_chassisid(&cid);
	if ((err = lldp_add_chassisid2nvlist(&cid, tlv_nvl)) != 0)
		goto ret;

	pid.lp_subtype = LLDP_PORT_ID_MACADDRESS;
	pid.lp_pidlen = lap->la_physaddrlen;
	bcopy(lap->la_physaddr, pid.lp_pid, pid.lp_pidlen);
	if ((err = lldp_add_portid2nvlist(&pid, tlv_nvl)) != 0)
		goto ret;

	/* retrieve chassisID as a string */
	(void) lldp_chassisID2str(&cid, cidstr, sizeof (cidstr));
	/* retrieve portID as a string */
	(void) lldp_portID2str(&pid, pidstr, sizeof (pidstr));

	(void) snprintf(lap->la_msap, sizeof (lap->la_msap), "%s_%s",
	    cidstr, pidstr);
	(void) nvlist_add_nvlist(lap->la_local_mib, lap->la_msap,
	    tlv_nvl);
ret:
	nvlist_free(tlv_nvl);
	return (err);
}

typedef struct lldp_link_hold_s {
	datalink_id_t	llh_linkid;	/* linkid of the port being checked */
	datalink_id_t	llh_aggrid;	/* linkid of the aggregation */
	uint8_t		llh_mac[ETHERADDRL];
} lldp_link_hold_t;

/*
 * This callback function is called for every aggregation on the system. We
 * walk through the ports of each aggregation to see if the port on which LLDP
 * is going to be enabled is part of an aggregation.
 */
static int
lldp_link_aggr_hold(dladm_handle_t handle, datalink_id_t aggrid, void *args)
{
	lldp_link_hold_t	*hold_arg = args;
	dladm_aggr_grp_attr_t	ginfo;
	int			i;
	dladm_aggr_port_attr_t	*port;

	bzero(&ginfo, sizeof (dladm_aggr_grp_attr_t));
	if (dladm_aggr_info(handle, aggrid, &ginfo, DLADM_OPT_ACTIVE) !=
	    DLADM_STATUS_OK) {
		return (DLADM_WALK_CONTINUE);
	}

	for (i = 0; i < ginfo.lg_nports; i++) {
		port = ginfo.lg_ports + i;
		if (port->lp_linkid == hold_arg->llh_linkid) {
			hold_arg->llh_aggrid = aggrid;
			(void) memcpy(hold_arg->llh_mac, port->lp_mac,
			    ETHERADDRL);
			free(ginfo.lg_ports);
			return (DLADM_WALK_TERMINATE);
		}
	}
	free(ginfo.lg_ports);
	return (DLADM_WALK_CONTINUE);
}

/*
 * If the given link is part of an aggregation then we have to retrieve
 * the MAC address of that link from the aggregation group.
 */
static void
i_lldp_retrieve_macaddr(datalink_id_t linkid, datalink_id_t *aggrid,
    uint8_t *maddr)
{
	lldp_link_hold_t	arg;
	size_t			maddrsz = ETHERADDRL;

	*aggrid = DATALINK_INVALID_LINKID;
	bzero(maddr, maddrsz);

	/* Let's check if this port is part of any aggr */
	bzero(&arg, sizeof (lldp_link_hold_t));
	arg.llh_linkid = linkid;
	arg.llh_aggrid = DATALINK_INVALID_LINKID;
	(void) dladm_walk_datalink_id(lldp_link_aggr_hold, dld_handle,
	    &arg, DATALINK_CLASS_AGGR, DATALINK_ANY_MEDIATYPE,
	    DLADM_OPT_ACTIVE);

	/*
	 * If this link is part of an aggr, we need the aggr id and
	 * the port's actual MAC address.
	 */
	if (arg.llh_aggrid != DATALINK_INVALID_LINKID) {
		*aggrid = arg.llh_aggrid;
		bcopy(arg.llh_mac, maddr, maddrsz);
	}
}

/*
 * Initialize the lldp_agent_t structure.
 */
static int
lldp_init(datalink_id_t linkid, lldp_agent_t **lap)
{
	int		err;
	struct timeval	timeout_time;
	pthread_attr_t	attr;
	datalink_id_t	alinkid;
	uint8_t		amacaddr[ETHERADDRL];
	size_t		amacaddrlen = sizeof (amacaddr);

	if ((*lap = calloc(1, sizeof (lldp_agent_t))) == NULL)
		return (ENOMEM);

	i_lldp_retrieve_macaddr(linkid, &alinkid, amacaddr);

	(*lap)->la_refcnt = 1;
	(*lap)->la_linkid = linkid;
	(*lap)->la_aggr_linkid = alinkid;
	(*lap)->la_adminStatus = LLDP_MODE_UNKNOWN;
	(*lap)->la_portEnabled = B_FALSE;
	(*lap)->la_notify = B_FALSE;
	(*lap)->la_remote_index = 1;
	(*lap)->la_unrec_orgspec_index = 1;
	(*lap)->la_rx_state = LLDP_PORT_DISABLED;
	(*lap)->la_tx_state = LLDP_TX_INITIALIZE;
	(*lap)->la_tx_timer_state = LLDP_TX_TIMER_INITIALIZE;

	(*lap)->la_tx_sockfd = -1;
	if (((*lap)->la_rx_sockfd = lldp_open_socket(linkid,
	    alinkid != DATALINK_INVALID_LINKID)) == -1) {
		err = -1;
		goto fail;
	}

	if (dladm_datalink_id2info(dld_handle, linkid, NULL, NULL, NULL,
	    (*lap)->la_linkname, MAXLINKNAMELEN) != DLADM_STATUS_OK) {
		syslog(LOG_ERR, "failed to retrieve linkname");
		err = -1;
		goto fail;
	}
	if (alinkid != DATALINK_INVALID_LINKID) {
		(void) memcpy((*lap)->la_physaddr, amacaddr, amacaddrlen);
		(*lap)->la_physaddrlen = amacaddrlen;
		/*
		 * We need to use the aggr to send the packet out
		 */
		(*lap)->la_tx_sockfd =
		    lldp_open_socket((*lap)->la_aggr_linkid,
		    alinkid != DATALINK_INVALID_LINKID);
		if ((*lap)->la_tx_sockfd == -1) {
			syslog(LOG_ERR, "Failed to open a socket on "
			    "aggregation.");
			err = -1;
			goto fail;
		}
	} else {
		/*
		 * Ordinarily the rx and tx socket will be the same, however
		 * for a port that's aggregated, they will be different as
		 * we will use the RX socket on the individual port, but
		 * the TX socket on the aggregation itself.
		 */
		(*lap)->la_tx_sockfd = (*lap)->la_rx_sockfd;
	}

	err = nvlist_alloc(&((*lap)->la_local_mib), NV_UNIQUE_NAME, 0);
	if (err != 0) {
		(*lap)->la_local_mib = NULL;
		goto fail;
	}

	(void) pthread_rwlock_init(&((*lap)->la_txmib_rwlock), NULL);
	(void) pthread_rwlock_init(&((*lap)->la_rxmib_rwlock), NULL);
	(void) pthread_mutex_init(&((*lap)->la_mutex), NULL);
	(void) pthread_cond_init(&((*lap)->la_cond_var), NULL);
	(void) pthread_mutex_init(&((*lap)->la_rx_mutex), NULL);
	(void) pthread_cond_init(&((*lap)->la_rx_cv), NULL);
	(void) pthread_mutex_init(&((*lap)->la_nextpkt_mutex), NULL);
	(void) pthread_cond_init(&((*lap)->la_nextpkt_cv), NULL);
	(void) pthread_rwlock_init(&((*lap)->la_feature_rwlock), NULL);
	(void) pthread_mutex_init(&((*lap)->la_db_mutex), NULL);

	/* Initialize the DCB feature list */
	list_create(&((*lap)->la_features), sizeof (dcbx_feature_t),
	    offsetof(dcbx_feature_t, df_node));

	/* Initialize the lldp_write2pdu list */
	list_create(&((*lap)->la_write2pdu), sizeof (lldp_write2pdu_t),
	    offsetof(lldp_write2pdu_t, ltp_node));

	/* start the port monitor thread */
	if (!lldp_dlpi(*lap)) {
		syslog(LOG_ERR, "dlpi failure");
		err = -1;
		goto synch_destroy;
	}

	/* Initialize the local MIB */
	if ((err = lldp_init_local_mib(*lap)) != 0)
		goto synch_destroy;

	if ((err = pthread_attr_init(&attr)) != 0)
		goto synch_destroy;

	if ((err = pthread_attr_setdetachstate(&attr,
	    PTHREAD_CREATE_DETACHED)) != 0) {
		(void) pthread_attr_destroy(&attr);
		goto synch_destroy;
	}

	/* Start the state machines */
	/* increment the refcnt for rx thread */
	(*lap)->la_refcnt++;
	if ((err = pthread_create(&(*lap)->la_rx_state_machine, &attr,
	    lldpd_rx_state_machine, (void *)(*lap))) != 0) {
		(void) pthread_attr_destroy(&attr);
		goto synch_destroy;
	}

	/* increment the refcnt for tx thread */
	(*lap)->la_refcnt++;
	if ((err = pthread_create(&(*lap)->la_tx_state_machine, &attr,
	    lldpd_tx_state_machine, (void *)(*lap))) != 0) {
		(void) pthread_cancel((*lap)->la_rx_state_machine);
		(void) pthread_attr_destroy(&attr);
		goto synch_destroy;
	}

	/* increment the refcnt for tx timer thread */
	(*lap)->la_refcnt++;
	if ((err = pthread_create(&(*lap)->la_txtimer_state_machine, &attr,
	    lldpd_txtimer_state_machine, (void *)(*lap))) != 0) {
		(void) pthread_cancel((*lap)->la_rx_state_machine);
		(void) pthread_cancel((*lap)->la_tx_state_machine);
		(void) pthread_attr_destroy(&attr);
		goto synch_destroy;
	}
	(void) pthread_attr_destroy(&attr);

	/*
	 * Start the *system* timer tick (per second timer) for this agent
	 * Should be a global timer.
	 */
	bzero(&timeout_time, sizeof (struct timeval));
	timeout_time.tv_sec = 1;
	/* increment the refcnt for lldp_timer() */
	(*lap)->la_refcnt++;
	(*lap)->la_timer_tid = lldp_timeout((void *)(*lap), lldp_timer,
	    &timeout_time);

	return (0);

synch_destroy:
	(void) pthread_mutex_destroy(&((*lap)->la_mutex));
	(void) pthread_cond_destroy(&((*lap)->la_cond_var));
	(void) pthread_mutex_destroy(&((*lap)->la_rx_mutex));
	(void) pthread_cond_destroy(&((*lap)->la_rx_cv));
	(void) pthread_mutex_destroy(&((*lap)->la_nextpkt_mutex));
	(void) pthread_cond_destroy(&((*lap)->la_nextpkt_cv));

	(void) pthread_rwlock_destroy(&((*lap)->la_rxmib_rwlock));
	(void) pthread_rwlock_destroy(&((*lap)->la_txmib_rwlock));
	(void) pthread_rwlock_destroy(&((*lap)->la_feature_rwlock));
	(void) pthread_mutex_destroy(&((*lap)->la_db_mutex));
fail:
	nvlist_free((*lap)->la_local_mib);
	if ((*lap)->la_rx_sockfd != -1)
		(void) close((*lap)->la_rx_sockfd);
	if ((*lap)->la_tx_sockfd != -1 &&
	    (*lap)->la_rx_sockfd != (*lap)->la_tx_sockfd) {
		(void) close((*lap)->la_tx_sockfd);
	}
	if ((*lap)->la_dh != NULL)
		dlpi_close((*lap)->la_dh);
	free(*lap);
	return (err);
}

static void
lldp_fini(lldp_agent_t *lap)
{
	uint8_t			mcast_addr[8] = LLDP_GROUP_ADDRESS;
	struct packet_mreq	pmreq;

	if (lap->la_aggr_linkid != DATALINK_INVALID_LINKID) {
		bzero(&pmreq, sizeof (pmreq));
		pmreq.mr_ifindex = lap->la_linkid;
		pmreq.mr_alen =  ETHERADDRL;
		pmreq.mr_type = PACKET_MR_MULTICAST;
		bcopy(mcast_addr, pmreq.mr_address, ETHERADDRL);

		(void) setsockopt(lap->la_rx_sockfd, SOL_PACKET,
		    PACKET_DROP_MEMBERSHIP, &pmreq, sizeof (pmreq));
	}
	/*
	 * If the tx socket is different than the rx, we wouldn't
	 * have added the multicast membership, so we don't need
	 * to drop it.
	 */
	if (lap->la_tx_sockfd != lap->la_rx_sockfd)
		(void) close(lap->la_tx_sockfd);
	(void) close(lap->la_rx_sockfd);

	(void) pthread_mutex_destroy(&lap->la_mutex);
	(void) pthread_cond_destroy(&lap->la_cond_var);
	(void) pthread_mutex_destroy(&lap->la_rx_mutex);
	(void) pthread_cond_destroy(&lap->la_rx_cv);
	(void) pthread_mutex_destroy(&lap->la_nextpkt_mutex);
	(void) pthread_cond_destroy(&lap->la_nextpkt_cv);

	(void) pthread_rwlock_destroy(&lap->la_rxmib_rwlock);
	(void) pthread_rwlock_destroy(&lap->la_txmib_rwlock);
	(void) pthread_rwlock_destroy(&lap->la_feature_rwlock);
	(void) pthread_mutex_destroy(&lap->la_db_mutex);

	/* free remote mib */
	nvlist_free(lap->la_remote_mib);
	/* free local mib */
	nvlist_free(lap->la_local_mib);

	/* destroy DCBx feature list */
	list_destroy(&lap->la_features);

	free(lap);
}

void
lldp_agent_refcnt_incr(lldp_agent_t *lap)
{
	lldp_mutex_lock(&lap->la_mutex);
	lap->la_refcnt++;
	lldp_mutex_unlock(&lap->la_mutex);
}

void
lldp_agent_refcnt_decr(lldp_agent_t *lap)
{
	lldp_mutex_lock(&lap->la_mutex);
	assert(lap->la_refcnt > 0);
	lap->la_refcnt--;
	if (lap->la_refcnt == 0) {
		lldp_mutex_unlock(&lap->la_mutex);
		lldp_fini(lap);
	} else {
		lldp_mutex_unlock(&lap->la_mutex);
	}
}

lldp_agent_t *
lldp_agent_create(datalink_id_t linkid, int *errp)
{
	lldp_agent_t	*lap;
	zoneid_t	zoneid;
	int		err;

	/* First check if the link is in use by an non-global zone */
	zoneid = ALL_ZONES;
	if (getzoneid() == GLOBAL_ZONEID &&
	    zone_check_datalink(&zoneid, linkid) == 0) {
		if (errp != NULL)
			*errp = EBUSY;
		return (NULL);
	}

	for (lap = list_head(&lldp_agents); lap != NULL;
	    lap = list_next(&lldp_agents, lap)) {
		if (lap->la_linkid == linkid) {
			if (errp != NULL)
				*errp = EEXIST;
			return (NULL);
		}
	}
	if ((err = lldp_init(linkid, &lap)) == 0)
		list_insert_tail(&lldp_agents, lap);
	else
		lap = NULL;
	if (errp != NULL)
		*errp = err;
	return (lap);
}


lldp_agent_t *
lldp_agent_get(datalink_id_t linkid, int *errp)
{
	lldp_agent_t	*lap;
	int		err = 0;

	for (lap = list_head(&lldp_agents); lap != NULL;
	    lap = list_next(&lldp_agents, lap)) {
		if (lap->la_linkid == linkid) {
			lldp_agent_refcnt_incr(lap);
			goto ret;
		}
	}
	err = ESRCH;
ret:
	if (errp != NULL)
		*errp = err;
	return (lap);
}

int
lldp_agent_delete(lldp_agent_t *lap)
{
	lldp_agent_t	*cur;

	/* detach the agent from the list */
	for (cur = list_head(&lldp_agents); cur != NULL;
	    cur = list_next(&lldp_agents, cur)) {
		if (cur == lap) {
			list_remove(&lldp_agents, cur);
			lldp_agent_refcnt_decr(cur);
			return (0);
		}
	}
	return (ENOENT);
}

/* assumes buf, barr is non-NULL and `buf' is of sufficient size */
uint_t
lldp_bytearr2hexstr(uint8_t *barr, uint_t blen, char *buf, uint_t buflen)
{
	uint_t	nbytes = 0, i;

	for (i = 0; i < blen; i++) {
		nbytes += snprintf(buf + nbytes, buflen - nbytes, "%02x",
		    barr[i]);
	}

	return (nbytes);
}

char *
lldp_state2str(int state)
{
	switch (state) {
	case LLDP_PORT_DISABLED:
		return ("portDsbd");
	case LLDP_PORT_SHUTDOWN:
		return ("portShut");
	case LLDP_TX_INITIALIZE:
	case LLDP_RX_INITIALIZE:
		return ("init");
	case LLDP_TX_IDLE:
		return ("idle");
	case LLDP_RX_WAIT_FOR_FRAME:
		return ("wait4Frm");
	case LLDP_RX_FRAME:
		return ("rcvdFrame");
	}
	return ("unknown");
}

/*
 * Counts the number of nvpairs in an nvlist. It skips through any private
 * nvpairs in the nvlist when counting.
 */
int
lldp_nvlist_nelem(nvlist_t *nvl)
{
	nvpair_t	*nvp;
	int		count = 0;
	char		*name;

	if (nvl == NULL)
		return (count);

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		name = nvpair_name(nvp);
		if (LLDP_NVP_PRIVATE(name))
			continue;
		++count;
	}
	return (count);
}

/* Returns true id the local MAC is numerical lower than the peer's */
boolean_t
lldp_local_mac_islower(lldp_agent_t *lap, int *errp)
{
	int	diff;

	*errp = 0;
	if (lap->la_rmaclen == 0) {
		syslog(LOG_WARNING,
		    "%p: Peer doesn't have MAC addresses info.", lap);
		*errp = ENOENT;
		return (B_FALSE);
	}
	diff = memcmp(lap->la_physaddr, lap->la_rmac, ETHERADDRL);
	return (diff < 0);
}

/* Returns the chassis ID, which is the output of hostid */
void
lldp_get_chassisid(lldp_chassisid_t *cid)
{
	/* get the hostid */
	(void) snprintf((char *)&cid->lc_cid, sizeof (cid->lc_cid),
	    "%08lx", gethostid());
	cid->lc_cidlen = strlen((char *)&cid->lc_cid);
	cid->lc_subtype = LLDP_CHASSIS_ID_LOCAL;
}

/* Returns the system name, which is the output of `uname -n`. */
void
lldp_get_sysname(char *buf, size_t bufsize)
{
	struct utsname	name;

	bzero(&name, sizeof (struct utsname));
	(void) uname(&name);
	(void) strlcpy(buf, name.nodename, bufsize);
}

/*
 * Returns system description, which is the concatenation of the output of
 * `uname -s`, `uname -r`, `uname -v` and `uname -m`. That is, concatenation
 * of kernel name, kernel release, kernel version and machine name.
 */
void
lldp_get_sysdesc(char *buf, size_t bufsize)
{
	struct utsname	name;

	bzero(&name, sizeof (struct utsname));
	(void) uname(&name);
	(void) snprintf(buf, bufsize, "%s %s %s %s",
	    name.sysname, name.release, name.version, name.machine);
}

/*
 * Compares two nvlist_t to check to see if they are same. It assumes that the
 * two nvlists have same number of nvpairs. For now this function is being used
 * to compare VLAN, Management Address and Application TLVs.  We err on the
 * side of caution for lookup failures.
 *
 * NOTE: If this function is used for comparing future TLVs please make sure
 * that all the required data types are compared.
 */
boolean_t
lldp_nvl_similar(nvlist_t *cnvl, nvlist_t *nnvl)
{
	nvpair_t	*cnvp, *nnvp;
	uint8_t		cu8, nu8, *carr, *narr;
	uint16_t	cu16, nu16;
	uint32_t	cu32, nu32;
	uint_t		ccnt, ncnt;
	char		*cstr, *nstr;
	nvlist_t	*i_cnvl, *i_nnvl;
	boolean_t	differ;

	/* We err on the side of caution for lookup failures */
	for (cnvp = nvlist_next_nvpair(cnvl, NULL); cnvp != NULL;
	    cnvp = nvlist_next_nvpair(cnvl, cnvp)) {
		if (nvlist_lookup_nvpair(nnvl, nvpair_name(cnvp), &nnvp) != 0)
			return (B_FALSE);
		differ = B_FALSE;
		switch (nvpair_type(cnvp)) {
		case DATA_TYPE_UINT8:
			if (nvpair_value_uint8(cnvp, &cu8) != 0 ||
			    nvpair_value_uint8(nnvp, &nu8) != 0 ||
			    cu8 != nu8)
				differ = B_TRUE;
			break;
		case DATA_TYPE_UINT16:
			if (nvpair_value_uint16(cnvp, &cu16) != 0 ||
			    nvpair_value_uint16(nnvp, &nu16) != 0 ||
			    cu16 != nu16)
				differ = B_TRUE;
			break;
		case DATA_TYPE_UINT32:
			if (nvpair_value_uint32(cnvp, &cu32) != 0 ||
			    nvpair_value_uint32(nnvp, &nu32) != 0 ||
			    cu32 != nu32)
				differ = B_TRUE;
			break;
		case DATA_TYPE_STRING:
			if (nvpair_value_string(cnvp, &cstr) != 0 ||
			    nvpair_value_string(nnvp, &nstr) != 0 ||
			    strcmp(cstr, nstr) != 0)
				differ = B_TRUE;
			break;
		case DATA_TYPE_BYTE_ARRAY:
			if (nvpair_value_byte_array(cnvp, &carr, &ccnt) != 0 ||
			    nvpair_value_byte_array(nnvp, &narr, &ncnt) != 0 ||
			    ccnt != ncnt || bcmp(&carr, &narr, ncnt) != 0)
				differ = B_TRUE;
			break;
		case DATA_TYPE_NVLIST:
			if (nvpair_value_nvlist(cnvp, &i_cnvl) != 0 ||
			    nvpair_value_nvlist(nnvp, &i_nnvl) != 0 ||
			    !lldp_nvl_similar(i_cnvl, i_nnvl))
				differ = B_TRUE;
			break;
		}
		if (differ)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/* Wrapper functions for locks that aborts on failure */
void
lldp_rw_lock(pthread_rwlock_t *rwlock, lldp_rwlock_type_t type)
{
	if (type ==  LLDP_RWLOCK_READER) {
		if (pthread_rwlock_rdlock(rwlock) != 0)
			abort();
	} else {
		if (pthread_rwlock_wrlock(rwlock) != 0)
			abort();
	}
}

void
lldp_rw_unlock(pthread_rwlock_t *rwlock)
{
	if (pthread_rwlock_unlock(rwlock) != 0)
		abort();
}

void
lldp_mutex_lock(pthread_mutex_t *lock)
{
	if (pthread_mutex_lock(lock) != 0)
		abort();
}

void
lldp_mutex_unlock(pthread_mutex_t *lock)
{
	if (pthread_mutex_unlock(lock) != 0)
		abort();
}

/*
 * LLDP operations are only permitted on a physical Link and DL_ETHER media
 */
boolean_t
lldpd_validate_link(dladm_handle_t handle, const char *linkname,
    datalink_id_t *linkid, int *errp)
{
	datalink_class_t	class;
	uint32_t		media, flags;

	*errp = 0;
	if (dladm_name2info(handle, linkname, linkid, &flags, &class,
	    &media) != DLADM_STATUS_OK) {
		*errp = EINVAL;
		return (B_FALSE);
	}

	/* we do not allow operation on temporary link */
	if ((flags | DLADM_OPT_PERSIST) == DLADM_OPT_PERSIST) {
		*errp = ENOTSUP;
		return (B_FALSE);
	}

	/* LLDP is supported only on Ethernet */
	if (media != DL_ETHER) {
		*errp = ENOTSUP;
		return (B_FALSE);
	}

	/* VLANs, VNICs and Aggrs  not supported */
	if (class != DATALINK_CLASS_PHYS && class != DATALINK_CLASS_SIMNET) {
		*errp = ENOTSUP;
		return (B_FALSE);
	}

	return (B_TRUE);
}
