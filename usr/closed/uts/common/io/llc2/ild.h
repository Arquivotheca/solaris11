/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 1990-1998 NCR Corporation, Dayton, Ohio USA
 */

#ifndef	_LLC2_ILD_H
#define	_LLC2_ILD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/stream.h>
#ifdef _KERNEL
#include "ildlock.h"
#include <sys/ksynch.h>
#endif

#ifndef TRUE
#define	TRUE 1
#define	FALSE 0
#endif

#define	HEADROOM	1

/*
 * multicast limits
 */
#define	MAXMULTICAST	16	/* how many multicast addresses we support */
#define	MAXPPA		64
#define	MINPPA		16

typedef struct mac_addr {
    uchar_t mac_addr[6];
} mac_addr_t;

/*
 * generic MAC statistics structure definition
 */
typedef struct {
	uint_t inOctets;	/* # rcvd octets */
	uint_t inUcastPkts;	/* # rcvd unicast frames */
	uint_t inNUcastPkts;	/* # rcvd non-unicast frames */
	uint_t inDiscards;	/* # rcvd frames discarded */
	uint_t inErrors;	/* # rcvd frames with error(s) */
	uint_t inUnknownProtos; /* # rcvd frames with unknown protocol */
	uint_t outOctets;	/* # xmit octets */
	uint_t outUcastPkts;	/* # xmit unicast frames */
	uint_t outNUcastPkts;	/* # xmit non-unicast frames */
	uint_t outDiscards;	/* # xmit frames discarded */
	uint_t outErrors;	/* # xmit frames with error(s) */
	uint_t outQLen;		/* # xmit frames waiting on queue */
} mGenStat_t;

/*
 * bind structure - embedded in link structure
 * contains all info specified on the bind
 */
typedef struct llc2_bind {
	dlsap_t	bd_sap;		/* local sap */
	int	bd_max_conind;	/* max connects on sap (link stations) */
	int	bd_conind;
	ushort_t	bd_service_mode; /* connection(less) */
	ushort_t	bd_conn_mgmt; /* connection management queue */
} llc2_bind_t;

/*
 * define macros to extract the SAP and Connection ID portions of a SID
 */
#define	GETSAPID(sid) ((ushort_t)(sid) & 0xFFC0)

/*
 * outstanding DL_CONNECT_IND information structure
 * contains information necessary to issue L_disconnect_req at close time
 */
typedef struct outInd {
	struct outInd *next;
	ushort_t sid;
	dlsap_t remAddr;
} outInd_t;

/*
 * structure for a doubly-linked circular queue
 */
typedef struct dLnkLst {
	struct dLnkLst *flink;  /* forward link to next element in chain */
	struct dLnkLst *blink;  /* backward link to previous element in chain */
} dLnkLst_t;

#define	LLC2_MAX_SAPS	128	/* maximum number of SAPs per station */
#define	LLC2_MAX_CONS	256	/* maximum number of connections per SAP */

#define	ILD_NO_LINK	1200	/* No free dlpi_link[]'s */
#define	LANSubsystemID	6
#define	E_ENVIRONMENT	'E'	/* Error caused by environment  */
#define	E_HARDWARE	'H'	/* Error caused by hardware	*/
#define	E_MEDIA		'M'	/* Error caused by media	*/
#define	E_NOERROR	'N'	/* Non-error event		*/
#define	E_OPERATOR	'O'	/* Error caused by operator	*/
#define	E_SOFTWARE	'S'	/* Error caused by software	*/
#define	E_UNDETERMINED	'U'	/* Cause of error undetermined	*/
#define	E_WARNING	'W'	/* Operation succeeded; operator */
				/* warned about some condition	*/

#ifdef _KERNEL

/*
 * LLC2 resource limits
 */

/*
 * if off, M_PROTO msgs are put directly to
 * the DLPI state machine. M_DATA is
 * always queued
 */
#define	ALWAYS_Q	0x01
/*
 * checked in unitdata_ind, if off, data is put
 * up stream regardless of q Hiwater marks
 */
#define	DO_CANPUT	0x02
/*
 * Checked in ild_uninit. If not set, DLS
 * users will _NOT_ be notified that the
 * ILD is shutting down.
 */
#define	SHUTDOWN_DLPI	0x04
#define	LOOPBACK_BCAST	0x08	/* Loopback broadcast/multicast for ether */
/*
 * Set when DLPI is shutdown/links freed, etc
 * helps avoid panics when shutting down while
 * lots of stuff is running (ie closes haven't
 * completed yet, but structures are being
 * freed)
 */
#define	LLC2_SHUTTING_DOWN    0x10

/*
 * link structure - tied to the queue via q_ptr, contains all internal states
 *		for the station.
 */
typedef struct link {
	dLnkLst_t chain; 	/* doubly linked list of dlpi_link structures */
	queue_t *lk_wrq;
	struct mac *lk_mac;
	short lk_state; 	/* dlpi state */
	short lk_status;	/* link is up or close in progress */
	ushort_t lk_sid;	/* conn/sap id (connection ff00, sap 00ff) */
	ushort_t lk_rnr;	/* Flow control variables */
	ushort_t lk_close;	/* closing state */
	int lk_minor;		/* Assigned minor number */
	llc2_bind_t lk_bind;	/* binding state */
	dlsap_t lk_dsap; 	/* remote dlsap on connects */
	short lk_enet;		/* 802.2 or Ethernet II */
	short lk_opt;		/* link Options */
	void  *lkOutIndPtr;	/* ptr to kmem_alloc'd block--use during free */
	outInd_t *lkOutIndHead;
	outInd_t *lkOutIndTail;
	mblk_t *lkPendHead;
	mblk_t *lkPendTail;
	mblk_t *lkDisIndHead;
	mblk_t *lkDisIndTail;

	/* SNAP stuff */
	ushort_t lk_subs_bind_type;
	uchar_t lk_subs_bind_vendor1;
	uchar_t lk_subs_bind_vendor2;
	uchar_t lk_subs_bind_vendor3;
	uchar_t lk_local_reset;

	int lk_max_sdu;
	int lk_min_sdu;
	int lk_max_idu;

	/* Medium-specific address length */
	short lk_addr_length;
	ILD_DECL_LOCK(lk_lock);
	/* multicast addresses for port */
	mac_addr_t lk_multicast[MAXMULTICAST];
	int lk_multiuse;	/* multicast usage count */
	int lk_timeoutid;	/* ID from itimeout call */
	ILD_DECL_SV(sleep_var);	/* ptr to sync. variable structure */
	uchar_t lk_xidtest_flg;	/* Auto XID/TEST response flags */
} link_t;


/*
 * listening stream queue structure definition
 */
typedef struct listenQ {
	struct listenQ *next;
	link_t *lnk;
	ILD_DECL_LOCK(lq_lock);
} listenQ_t;


/*
 * define bit mapped flags for lk_close element
 */
#define	LKCL_PEND	1	/* close in progress */
#define	LKCL_WAKE	2	/* waiting for wakeup */
#define	LKCL_FAIL	4	/* disconnect_req failure */
#define	LKCL_OKAY	8	/* disconnect_req okay */


/*
 * muoe951976 Prasad 5/23/95
 * Upported yatin 8/04/95
 * To prevent from race conditions between dlpi_close() other
 * routines
 */
#define	LKCL_INUSE 0x10

#define	LKCL_CLOSED 0x12
#define	LKCL_CLOSE_IN_PROGRESS	0x14

/*
 * define values for lk_rnr element
 */
#define	LKRNR_OK	0	/* local busy clear */
#define	LKRNR_FC	1	/* local busy by FLOW.CONTROL */
#define	LKRNR_RC	2	/* local busy by RECV.CANCEL */
#define	LKRNR_IE	4	/* RECV.CANCEL interrupt expected */

/*
 * define values for lk_opt element
 */
/*
 * This field and definition is an RFC requested by SD to allow them to
 * specify a different remote "SAP" (actually Ethernet II 16 bit type) on
 * a send. The ILD typically fills in the remote field with the sap/type
 * that this stream was originally bound to. This options will allow
 * them to bind to one address and send to another.
 */
/*
 * As more options are added this field should have additional options
 * OR'd in to be used to create the mask for accepting _ONLY_ valid
 * options for setting/clearing
 */
#define	ILD_VALID_LK_OPTS   (LKOPT_REM_ADDR_OK)

#define	LKOPT_REM_ADDR_OK   1	/* Remote Address type (dl_type) is OK */


/*
 * Structures for ild_ioctl and llc2SetTuneParmsReq
 */
typedef struct llc2tune {
	uint16_t  N2; /* Maximum number of retriesW */
	uint16_t  T1; /* Acknowledgement time (unit 0.1 sec) */
	uint16_t  Tpf; /* P/F cycle retry time (unit 0.1 sec) */
	uint16_t  Trej; /* Reject retry time (unit 0.1 sec) */
	uint16_t  Tbusy; /* Rem busy check time (unit 0.1 sec) */
	uint16_t  Tidle; /* Idle P/F cycle time (unit 0.1 sec) */
	uint16_t  ack_delay; /* RR delay time (unit 0.1 sec) */
	uint16_t  notack_max; /* Max rcv window */
	uint16_t  tx_window; /* Transmit window */
	uint16_t  tx_probe; /* P-bit pos before end of Tx win */
	uint16_t  max_I_len; /* Maximum I-frame length */
	uint16_t  xid_window; /* receive window */
	uint16_t  xid_Ndup; /* Duplicate MAC XID count */
	uint16_t  xid_Tdup; /* Duplicate MAC XID time */
} llc2tune_t;

struct llc2_tnioc {
	uint8_t	lli_type;
	uint8_t	lli_spare[3];
	uint32_t lli_ppa;
	llc2tune_t llc2_tune;
};

/*
 * define the snaphdr_t structure here so the mac_t function prototypes
 * work
 */
typedef struct snap_hdr {
	uchar_t oui0;
	uchar_t oui1;
	uchar_t oui2;
	uchar_t type_h;
	uchar_t type_l;
} snaphdr_t;

#define	SNAP_HDR_SIZE	5
#define	SNAPSAP		0xAA /* sap value for SNAP */

/*
 * MAC Structure
 */
typedef struct mac {
	ushort_t mac_state;
	uint_t mac_type;	/* DL_ETHER, DL_TPR, etc. */
	uint_t mac_ppa;		/* adapter number */
	ushort_t mac_service_modes;
	ushort_t mac_hdr_sz;
	link_t *mac_conn_mgmt;
	uchar_t mac_bia[6];
	uchar_t mac_pos[8];
	unsigned int mac_novell; /* NOVELL_LENGTH or NOVELL_TYPE */
	int mac_max_sdu;
	int mac_min_sdu;
	int mac_max_idu;
	int execution_options;
	mGenStat_t mGenStat;
	ILD_DECL_LOCK(mac_lock); /* Lock structure *buzz* */
} mac_t;


/*
 * MAC Structure EXTENSIONS
 */
typedef struct macx {
	/*
	 * DLPI Version 2 Local/Essential Management Primitive Support
	 * Because these are Extensions to the MAC, little effect should
	 * be noticed by add on drivers.
	 */
	uchar_t factory_addr[6];

	void	*UserDataPtr;
	/* MAC B/C adrs (initialized ff.ff.ff.ff.ff.ff */
	mac_addr_t broadcast_addr;
	queue_t	*lowerQ;	/* ptr to Lower Write Q assigned at I_LINK */
	int lower_index;	/* index for lower stream. */
	int lower_instance;	/* instance # for lower MAC driver. */
	int upper_index;	/* index for upper LLC stream. */
	/* m/c adrs pending ack from Solaris MAC */
	mac_addr_t multicast_pending;
	mblk_t *multicast_mblk; /* original ioctl to IOCACK/IOCNAK */
	queue_t *multicast_queue; /* upper Q for returning multicast ACK/NAK */
	queue_t *statistics_queue; /* upper Q for returning statistic ACK/NAK */
	int mac_options;	/* per MAC options */
	uchar_t ppa_status; /* current ppa status. linked or ppa_set. */
	uchar_t ppa_status_old; /* previous ppa status */
} macx_t;

/* macx_t ppa_status options */
#define	PPA_UNLINKED	0
#define	PPA_LINKED	1
#define	PPA_SET		2

/*
 * Token Ring Source Routing Broadcast bit definitions
 */
#define	NON_BROADCAST	0x00 /* Non Broadcast */
#define	ALLRTE_SGLRET	0x80 /* All Routes Broadcast w/ Single route Return */
#define	SGLRTE_SGLRET	0xc0 /* Single Route Bcast w/Single Route Return */
#define	SGLRTE_ALLRET	0xe0 /* Single Route Bcast w/All Routes Return */
#define	RTE_DIRECTION	0x80 /* Direction indicator */

/*
 * define return codes for dlpi_data_ind()
 */
#define	DLPI_DI_OKAY	0
#define	DLPI_DI_BUSY	1
#define	DLPI_DI_OVFL	2
#define	DLPI_DI_STATE	3

/*
 * define ild.c public function prototypes
 */
void ild_init_con(mac_t *, queue_t *, mblk_t *, ushort_t);
void ild_uninit_con(mac_t *, queue_t *, mblk_t *, ushort_t);

#ifdef ILDTRACE_NO_LINE_NUM
#define	ILD_LOG(link_no, slot_no, sw_module, tag_err, cause, \
	severity, file, lineno, \
	msg, arg1, arg2, arg3, arg4)	\
	ild_log(link_no, slot_no, sw_module, tag_err, cause, \
	    severity, file, 0, \
	    msg, arg1, arg2, arg3, arg4)
#else
#define	ILD_LOG(link_no, slot_no, sw_module, tag_err, cause, \
	severity, file, lineno, \
	msg, arg1, arg2, arg3, arg4)	\
	ild_log(link_no, slot_no, sw_module, tag_err, cause, \
	    severity, file, lineno, \
	    msg, arg1, arg2, arg3, arg4)
#endif /* ILDTRACE_NO_LINE_NUM */
void ild_log(uint_t, uint_t, char *, uint_t, uchar_t, uchar_t, char *,
    uint_t, char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

void ildDrvrInitReg(int ());
void ildDrvrInitDeReg(int ());

/*
 * define dlpi.c public function prototypes
 */
void dlpi_shutdown(mac_t *, int);
void dlpi_XON(mac_t *, link_t *);
void dlpi_bind_con(mac_t *, dlsap_t *, ushort_t, int);
void dlpi_connect_con(mac_t *, link_t *, ushort_t, ushort_t);
void dlpi_disconnect_con(mac_t *, ushort_t, ushort_t);
void dlpi_unbind_con(mac_t *, uint_t);
void dlpi_connect_ind(mac_t *, short, dlsap_t *, ushort_t);
void dlpi_disconnect_ind(mac_t *, ushort_t, uint_t);
void dlpi_unitdata_ind(mac_t *, dlsap_t *, dlsap_t *, mblk_t *, int, int);
int dlpi_data_ind(mac_t *, link_t *, ushort_t, mblk_t *);
int dlpi_canput(mac_t *, link_t *, ushort_t, mblk_t *);
int dlpi_putnext(mac_t *, link_t *, ushort_t, mblk_t *, int);
void dlpi_xid_ind(mac_t *, dlsap_t *, dlsap_t *, mblk_t *, ushort_t,
    ushort_t, int);
void dlpi_test_ind(mac_t *, dlsap_t *, dlsap_t *, mblk_t *, int, int, int);
void dlpi_close(link_t *);
void dlpi_init_lnks(void);
int dlpi_getfreelnk(link_t **);
int dlpi_data(queue_t *, mblk_t *, int);
void dlpi_state(queue_t *, mblk_t *);
void dlpi_reset_ind(mac_t *, ushort_t, uint_t, uint_t);
void dlpi_reset_con(mac_t *, ushort_t, ushort_t);
void dlpi_get_statistics_ack(queue_t *, void *, int);
void dlpi_multicast_ack(queue_t *, mblk_t *);
void dlpi_ack(queue_t *, mblk_t *);
void dlpi_linkup(mac_t *);
void dlpi_linkdown(mac_t *);
void dlpi_flushall(void);

/*
 * define llc2.c public function prototypes
 */
void llc2_init(mac_t *);
void llc2Ioctl(mac_t *, queue_t *, mblk_t *);
void llc2RcvIntLvl(mac_t *, mblk_t *);
void llc2RcvSrvLvl(mac_t *, dlsap_t *, dlsap_t *, mblk_t *);

typedef int macs_t(mac_t *mac, uint_t slot, uint_t ppa);
#endif


extern int ildTraceEnabled;
void ildTrace(uint_t mod, uint_t line, uintptr_t parm1, uintptr_t parm2,
    uint_t level);
void ildFlshTrace(int fflag);
#ifdef ILDTRACE_NO_LINE_NUM
#define	ild_trace(m, l, p1, p2, i)
#else
#define	ild_trace(m, l, p1, p2, i) \
	if (ildTraceEnabled) ildTrace(m, l, p1, p2, i)
#endif

/*
 * define module ID value for tracing, must be no larger than 16 bits
 */
#define	MID_ILD			0x1
#define	MID_DLPI		0x2
#define	MID_LLC2		0x3
#define	MID_ILD_WD		0x4
#define	MID_NCRTR		0x5
#define	MID_ILD_WAVELAN		0x6
#define	MID_21040		0x7 /* DEC21040 Adapters */
#define	MID_SAM			0x20

/*
 * define some useful macro's for dealing with 6 byte MAC addresses
 */
/*
 * CMP_MAC(a, b)
 *
 * compare two 6 byte MAC addresses for equality
 *
 * NOTE: 'a' and 'b' must be pointer arguments
 *
 * returns 0 if MAC addresses NOT equal
 * returns 1 if MAC addresses equal
 */

#define	CMP_MAC(a, b) 	(bcmp((caddr_t)a, (caddr_t)b, 6) == 0)

/*
 * CPY_MAC(s, d)
 *
 * copy 6 byte MAC address from 's' to 'd'
 *
 * NOTE: 's' and 'd' must be pointer arguments
 * NOTE: since this macro consists of two statements the user is responsible
 *		 for adding '{' and '}' to surround it if necessary
 */

#define	CPY_MAC(s, d) bcopy((caddr_t)(s), (caddr_t)(d), 6)

/*
 * define some useful macro's for processing a doubly-linked circular queue
 */

/*
 * ADD_DLNKLST(currentElementPtr, previousElementPtr)
 *
 * add an element to a chain after the given previous element
 */
#define	ADD_DLNKLST(currentElementPtr, previousElementPtr) \
	(currentElementPtr)->blink = (previousElementPtr); \
	(currentElementPtr)->flink = (previousElementPtr)->flink; \
	(previousElementPtr)->flink = (currentElementPtr); \
	(currentElementPtr)->flink->blink = (currentElementPtr)

/*
 * RMV_DLNKLST(currentElementPtr)
 *
 * remove an element from a chain
 *
 * WARNING:
 *   Do not code currentElementPtr as "xxx->flink" or "xxx->blink", since
 *   this macro will change their value during execution. Instead, declare
 *   a variable, assign it the flink or blink value, and code the variable
 *   as the currentElementPtr.
 */
#define	RMV_DLNKLST(currentElementPtr) \
	(currentElementPtr)->blink->flink = (currentElementPtr)->flink; \
	(currentElementPtr)->flink->blink = (currentElementPtr)->blink; \
	(currentElementPtr)->flink = (dLnkLst_t *)0; \
	(currentElementPtr)->blink = (dLnkLst_t *)0


#ifdef	__cplusplus
}
#endif

#endif /* _LLC2_ILD_H */
