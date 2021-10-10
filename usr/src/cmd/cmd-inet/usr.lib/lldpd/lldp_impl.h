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

#ifndef _LLDP_IMPL_H
#define	_LLDP_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <door.h>
#include <libnvpair.h>
#include <libdllink.h>
#include <libdlvlan.h>
#include <libdlaggr.h>
#include <libdlvnic.h>
#include <libdlpi.h>
#include <lldp.h>
#include <liblldp.h>
#include <liblldp_lldpd.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/mac.h>
#include <sys/sysmacros.h>
#include <sys/ethernet.h>
#include <sys/sysevent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/lldp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/list.h>
#include <zone.h>

#define	A_CNT(arr)	(sizeof (arr) / sizeof (arr[0]))

/* private nvpairs */
#define	LLDP_NVP_RXINFO_TIMER_ID	"__rxinfo_tid"
#define	LLDP_NVP_RXINFO_AGE_ABSTIME	"__rxinfoage_abstime"
#define	LLDP_NVP_REMIFINDEX		"__remifindex"
#define	LLDP_NVP_REMSYSDATA_UPDTIME	"__remsysdata_updtime"

#define	LLDP_NVP_PRIVATE(name)	(name[0] == '_' && name[1] == '_')

#define	LLDP_STATS_INCR(stat, counter)	(++((stat).counter))

/* forward declaration */
struct lldp_agent_s;
typedef struct lldp_agent_s lldp_agent_t;

/*
 * Data structures and callback functions related to TLV management
 */

/*
 * TLV init function. Used to initialize a TLV before it's configured
 * for transmission.
 */
typedef int	lldp_tlv_initf_t(lldp_agent_t *, nvlist_t *);

/*
 * TLV fini function. Used to cleanup any buffers created by the init
 * function.
 */
typedef void	lldp_tlv_finif_t(lldp_agent_t *);

/*
 * TLV parse function. Used to parse the incoming stream of bytes and
 * the parsed information into nvlist_t.
 */
typedef int	lldp_tlv_parsef_t(lldp_agent_t *, lldp_tlv_t *, nvlist_t *);

/*
 * TLV write function. Used to convert the structured TLV information into a
 * stream of bytes to be send out on wire.
 */
typedef	int	lldp_tlv_writef_t(void *, uint8_t *, size_t, size_t *);

/*
 * TLV compare function. Used to compare the current TLV in remote store with
 * the new TLV on the wire.
 */
typedef	int	lldp_tlv_cmpf_t(nvlist_t *, nvlist_t *, nvlist_t *, nvlist_t *,
		    nvlist_t *);

typedef struct lldp_tlv_info_s {
	char			*lti_name;	/* TLV name */
	char			*lti_nvpname;	/* TLV nvpair name */
	uint8_t			lti_type;	/* TLV type */
	uint32_t		lti_oui;	/* OUI for OrgSpec TLV */
	uint8_t			lti_stype;	/* subtype for OrgSpec TLV */
	lldp_tlv_initf_t	*lti_initf;
	lldp_tlv_finif_t	*lti_finif;
	lldp_tlv_parsef_t	*lti_parsef;
	lldp_tlv_writef_t	*lti_writef;
	lldp_tlv_cmpf_t		*lti_cmpf;
} lldp_tlv_info_t;

/* list of TLVs that are configured for transmission */
typedef struct lldp_write2pdu_s {
	list_node_t		ltp_node;
	lldp_tlv_info_t		*ltp_infop;
	lldp_tlv_writef_t	*ltp_writef;
	void			*ltp_cbarg;
} lldp_write2pdu_t;

/* forward declaration */
struct dcbx_feature_s;

/* LLDP agent */
struct lldp_agent_s {
	list_node_t		la_node;
	uint32_t		la_refcnt;
	datalink_id_t		la_linkid;
	datalink_id_t		la_aggr_linkid;
	uint16_t		la_pvid;
	uint16_t		la_maxfsz;
	char			la_linkname[MAXLINKNAMELEN];
	lldp_admin_status_t	la_adminStatus;

	boolean_t		la_localChanges;
	boolean_t		la_newNeighbor;
	boolean_t		la_tooManyNeighbors;
	boolean_t		la_portEnabled;

	uint8_t			la_physaddr[DLPI_PHYSADDR_MAX];
	size_t			la_physaddrlen;

	uint8_t			la_rmac[ETHERADDRL];
	size_t			la_rmaclen;

	char			la_msap[LLDP_MAX_MSAPLEN];

	/* port monitor thread */
	dlpi_handle_t		la_dh;
	dlpi_notifyid_t		la_notify_id;
	pthread_t		la_portmonitor;

	/* local system MIB */
	nvlist_t		*la_local_mib;
	pthread_rwlock_t	la_txmib_rwlock;

	/*
	 * remote systems MIB (it's nvlist of nvlist, with each
	 * nvlist representing information about an end point.
	 */
	nvlist_t		*la_remote_mib;
	uint32_t		la_remote_index;
	uint32_t		la_unrec_orgspec_index;
	pthread_rwlock_t	la_rxmib_rwlock;

	/* Rx variables */
	int			la_rx_state;
	boolean_t		la_rxrcvFrame;
	boolean_t		la_remoteChanges;
	boolean_t		la_rxChanges;
	boolean_t		la_rxInfoAge;
	uint32_t		la_rxTTL;
	uint8_t			*la_pdu;
	uint_t			la_pdulen;
	boolean_t		la_badFrame;
	pthread_t		la_rx_thr;
	int			la_rx_sockfd;

	/* Tx variables */
	int			la_tx_state;
	int			la_tx_timer_state;
	uint32_t		la_txTTR;
	uint32_t		la_txShutdownWhile;
	uint32_t		la_txCredit;
	uint32_t		la_txFast;
	boolean_t		la_txNow;
	boolean_t		la_txTick;
	uint16_t		la_txTTL;
	uint8_t			la_tx_pdu[LLDP_MAX_PDULEN];
	uint_t			la_tx_pdulen;
	int			la_tx_sockfd;

	/*
	 * List of call back routines that are used to construct LLDPDU.
	 * Synchronization to this list is via `la_txmib_rwlock'.
	 */
	list_t			la_write2pdu;

	/* Stats */
	lldp_stats_t		la_stats;

	/* Notification enabled from agent to listeners */
	boolean_t		la_notify;

	/* State machines */

	pthread_t		la_rx_state_machine;
	pthread_t		la_tx_state_machine;
	pthread_t		la_txtimer_state_machine;

	/* Probably la_rx_mutex and la_tx_mutex? */
	pthread_mutex_t		la_mutex;
	pthread_cond_t		la_cond_var;

	/* RX side condition variables & mutex */
	pthread_cond_t		la_rx_cv;
	pthread_mutex_t		la_rx_mutex;
	pthread_cond_t		la_nextpkt_cv;
	pthread_mutex_t		la_nextpkt_mutex;

	uint_t			la_timer_tid;

	/* DB mutex to serialize persistence */
	pthread_mutex_t		la_db_mutex;

	/* DCB features */
	pthread_rwlock_t	la_feature_rwlock;
	list_t			la_features;
};

/* Tx States */
#define	LLDP_TX_INITIALIZE		0x01
#define	LLDP_TX_IDLE			0x02
#define	LLDP_PORT_SHUTDOWN		0x03

/* Tx Timer States */
#define	LLDP_TX_TIMER_INITIALIZE	0x04
#define	LLDP_TX_TIMER_IDLE		0x05

/* Rx States */
#define	LLDP_PORT_DISABLED		0x06
#define	LLDP_RX_INITIALIZE		0x07
#define	LLDP_RX_WAIT_FOR_FRAME		0x08
#define	LLDP_RX_FRAME			0x09

/* SCF related structures */
typedef struct lldp_scf_state {
	scf_handle_t		*lss_handle;
	scf_instance_t		*lss_inst;
	scf_transaction_t	*lss_trans;
	scf_transaction_entry_t	*lss_tent;
	scf_propertygroup_t	*lss_pg;
	scf_property_t 		*lss_prop;
} lldp_scf_state_t;

typedef enum {
	LLDP_RWLOCK_READER,
	LLDP_RWLOCK_WRITER
} lldp_rwlock_type_t;

/* linked list that captures all the agents running on this system */
extern list_t	lldp_agents;

/* mutex to synchronize access to lldp_agents list */
extern pthread_rwlock_t lldp_agents_list_rwlock;

/* LLDP event channel for publishing and subscription */
extern evchan_t	*lldpd_evchp;

extern dladm_handle_t dld_handle;

extern nvlist_t	*lldp_sysinfo;
extern pthread_rwlock_t	lldp_sysinfo_rwlock;

extern uint32_t lldp_msgFastTx;
extern uint32_t lldp_msgTxInterval;
extern uint32_t lldp_reinitDelay;
extern uint32_t lldp_msgTxHold;
extern uint32_t lldp_txFastInit;
extern uint32_t lldp_txCreditMax;
extern uint32_t lldp_txNotifyInterval;

extern boolean_t snmp_enabled;

extern void		lldp_rw_lock(pthread_rwlock_t *, lldp_rwlock_type_t);
extern void		lldp_rw_unlock(pthread_rwlock_t *);

extern void		lldp_mutex_lock(pthread_mutex_t *);
extern void		lldp_mutex_unlock(pthread_mutex_t *);

/* lldp_main.c */
extern void		i_lldpd_handle_snmp_prop(boolean_t);

/* lldp_door.c */
extern void		lldpd_handler(void *, char *, size_t, door_desc_t *,
			    uint_t);
extern void		i_lldpd_set_mode(lldp_agent_t *, lldp_admin_status_t);
extern int		i_lldpd_set_tlv(lldp_agent_t *, lldp_proptype_t,
			    uint32_t, uint32_t *, uint32_t);
extern int		i_lldpd_set_capab(datalink_id_t, uint32_t);
extern lldp_write2pdu_t	*i_lldp_get_write2pdu(lldp_agent_t *, const char *);
extern lldp_write2pdu_t	*i_lldp_get_write2pdu_nolock(lldp_agent_t *,
			    const char *);

/* lldp_dlpi.c */
extern boolean_t	lldp_dlpi(lldp_agent_t *);

/* lldp_util.c */
extern lldp_agent_t	*lldp_agent_get(datalink_id_t, int *);
extern lldp_agent_t	*lldp_agent_create(datalink_id_t, int *);
extern int		lldp_agent_delete(lldp_agent_t *);
extern void		lldp_agent_refcnt_incr(lldp_agent_t *);
extern void		lldp_agent_refcnt_decr(lldp_agent_t *);
extern uint_t		lldp_bytearr2hexstr(uint8_t *, uint_t, char *, uint_t);
extern char		*lldp_state2str(int);
extern int		lldp_nvlist_nelem(nvlist_t *nvl);
extern void		lldp_get_chassisid(lldp_chassisid_t *);
extern void		lldp_get_sysname(char *, size_t);
extern void		lldp_get_sysdesc(char *, size_t);
extern boolean_t	lldp_nvl_similar(nvlist_t *, nvlist_t *);
extern boolean_t	lldpd_validate_link(dladm_handle_t, const char *,
			    datalink_id_t *, int *);

/* lldp_rx.c */
extern void		*lldpd_rx_state_machine(void *);
extern void		lldp_nvlist2msap(nvlist_t *, char *, size_t);

/* lldp_tx.c */
extern void		*lldpd_tx_state_machine(void *);
extern void		*lldpd_txtimer_state_machine(void *);
extern int		lldp_create_txFrame(lldp_agent_t *, boolean_t);

/* lldp_timer.c */
extern uint32_t		lldp_timeout(void *, void (*callback_func)(void *),
			    struct timeval *);
extern boolean_t	lldp_untimeout(uint32_t);
extern int		lldp_timeout_init(void);
extern int		lldp_timerid2time(uint32_t, uint16_t *);

/* lldp_tlv.c */
extern lldp_tlv_info_t	*lldp_get_tlvinfo(uint8_t, uint32_t, uint8_t);
extern lldp_tlv_info_t	*lldp_get_tlvinfo_from_tlvname(const char *);
extern int		lldp_write2pdu_add(lldp_agent_t *, lldp_tlv_info_t *,
			    lldp_tlv_writef_t, void *);
extern int		lldp_write2pdu_remove(lldp_agent_t *,
			    lldp_tlv_writef_t);

extern int		lldp_add_chassisid2nvlist(lldp_chassisid_t *,
			    nvlist_t *);
extern int		lldp_add_portid2nvlist(lldp_portid_t *, nvlist_t *);
extern int		lldp_add_portdescr2nvlist(char *, nvlist_t *);
extern int		lldp_add_ttl2nvlist(uint16_t, nvlist_t *);
extern int		lldp_add_sysname2nvlist(char *, nvlist_t *);
extern int		lldp_add_sysdescr2nvlist(char *, nvlist_t *);
extern int		lldp_add_syscapab2nvlist(lldp_syscapab_t *, nvlist_t *);
extern int		lldp_add_maxfsz2nvlist(uint16_t, nvlist_t *);
extern int		lldp_add_aggr2nvlist(lldp_aggr_t *, nvlist_t *);
extern int		lldp_add_pvid2nvlist(uint16_t, nvlist_t *);
extern int		lldp_add_vlan2nvlist(lldp_vlan_info_t *, nvlist_t *);
extern int		lldp_add_mgmtaddr2nvlist(lldp_mgmtaddr_t *, nvlist_t *);
extern int		lldp_add_pfc2nvlist(lldp_pfc_t *, nvlist_t *);
extern int		lldp_add_appln2nvlist(lldp_appln_t *, uint_t,
			    nvlist_t *);

extern int		lldp_nvlist2chassisid(nvlist_t *, lldp_chassisid_t *);
extern int		lldp_nvlist2portid(nvlist_t *, lldp_portid_t *);
extern int		lldp_nvlist2sysname(nvlist_t *, char **);
extern int		lldp_nvlist2sysdescr(nvlist_t *, char **);
extern int		lldp_nvlist2syscapab(nvlist_t *, lldp_syscapab_t *);
extern int		lldp_nvlist2portdescr(nvlist_t *, char **);
extern int		lldp_add_vnic_info(dladm_handle_t, datalink_id_t,
			    void *);
extern int		lldp_add_vlan_info(dladm_handle_t, datalink_id_t,
			    void *);
extern int		lldp_add_vnic2nvlist(lldp_vnic_info_t *,
			    nvlist_t *);

extern void		lldp_something_changed_local(lldp_agent_t *);
extern void		lldp_something_changed_remote(lldp_agent_t *,
			    nvlist_t *);
extern void		lldp_mode_changed(lldp_agent_t *, uint32_t);
extern void		dcbx_post_event(lldp_agent_t *, const char *,
			    const char *, nvlist_t *);
extern boolean_t	lldp_local_mac_islower(lldp_agent_t *, int *);
extern int		i_lldpd_handle_mgmtaddr_prop(char *);
extern int		lldp_add_peer_identity(nvlist_t *, nvlist_t *);

/* lldp_scf.c */
extern int		lldpd_persist_prop(lldp_propclass_t, lldp_proptype_t,
			    const char *, void *, data_type_t, uint32_t);
extern int		lldpd_walk_db(nvlist_t *, const char *);

#ifdef __cplusplus
}
#endif

#endif /* _LLDP_IMPL_H */
