/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * Copyright (c) 1996,1997 NCR Corporation, Dayton, Ohio USA
 */

#ifndef	_LLC2_LLC2K_H
#define	_LLC2_LLC2K_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *		LLC2 ELEMENTS OF PROCEDURE
 */

/*
 * defines for LLC2 protocol elements
 */
#define	UI		0x03
#define	TEST		0xe3
#define	XID		0xaf
#define	RR		0x01
#define	RNR		0x05
#define	REJ		0x09
#define	DM		0x0f
#define	DISC		0x43
#define	UA		0x63
#define	SABME		0x6f
#define	FRMR		0x87

#define	P_F		0x10	/* Poll/Final bit in U control field */
#define	SI_P_F		0x01	/* Poll/Final bit in S and I control fields */
#define	LLC_RESPONSE	0x01	/* Command/Response bit in the SSAP */

/*
 * generic LLC2 header
 */
typedef struct llc2Hdr {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t control;
} llc2Hdr_t;

#define	LLC_HDR_SIZE	3		/* DSAP, SSAP, CONTROL */

/*
 * LLC2 header for an I-format PDU
 */
typedef struct llc2HdrI {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t ns;
	uchar_t nr;
} llc2HdrI_t;

/*
 * LLC2 header for an S-format PDU
 */
typedef struct llc2HdrS {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t control;
	uchar_t nr;
} llc2HdrS_t;

/*
 * LLC2 header for a U-format PDU
 */
typedef struct llc2HdrU {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t control;
} llc2HdrU_t;

/*
 * FRMR information field format
 */
typedef struct llc2FrmrInfo {
	uchar_t c1;	/* rejected PDU control field byte 1 */
	uchar_t c2;	/* rejected PDU control field byte 2 */
	uchar_t vs;	/* V(S) */
	uchar_t vr;	/* V(R) */
	uchar_t flag;	/* error flag bit field */
} llc2FrmrInfo_t;

/*
 * flag
 */
#define	FRMR_V	0x10	/* FRMR V bit */
#define	FRMR_Z	0x08	/* FRMR Z bit */
#define	FRMR_Y	0x04	/* FRMR Y bit */
#define	FRMR_X	0x02	/* FRMR X bit */
#define	FRMR_W	0x01	/* FRMR W bit */

/*
 * LLC2 header for an FRMR PDU
 */
typedef struct llc2HdrFrmr {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t control;
	llc2FrmrInfo_t s;
} llc2HdrFrmr_t;

/*
 * LLC2 header for an XID PDU
 */
typedef struct llc2HdrXid {
	uchar_t dsap;
	uchar_t ssap;
	uchar_t control;
	uchar_t fmtId;
	uchar_t llc;
	uchar_t rw;
} llc2HdrXid_t;

/*
 * fmtId
 */
#define	LLC_XID_FMTID		0x81	/* IEEE basic format */

/*
 * llc
 */
#define	LLC_SERVICES_1	0x01	/* Class 1 LLC */
#define	LLC_SERVICES_2	0x03	/* Class 2 LLC */

#define	LLC_TYPE_1		0x01	/* Type 1 LLC */
#define	LLC_TYPE_2		0x02	/* Type 2 LLC */
#define	LLC_TYPE_1_2		0x03	/* Type 1 and Type 2 LLC */

#ifdef _KERNEL

/*
 *		LLC2 PROCESSING STRUCTURES AND DEFINES
 */
/*
 * LLC2 resource limits and implementation constants
 */

#define	LLC2_NUM_BUCKETS 8 /* number of timer buckets (must be power 2) */
#define	LLC2_BUCKET_MASK (LLC2_NUM_BUCKETS - 1) /* timer bucket hash mask */

#define	TR_RCF_MAX_LEN	30		/* TknRng Routing field maximum len */
#define	TR_RCF_LEN_MASK	0x1f		/* TknRng Routing field length mask */
/* Connection ID portion of the SID SAP is 0xff00 */
#define	CON_CID_MASK	0x000000ff
#define	CON_SAP_MASK	0x0000ff00	/* SAP portion of the SID */
/* when applied to a 6-byte MAC address, This is least significant 3 bytes */
#define	MAC_UNIQUE_MASK	0xffffff00
#define	MAXSAPVALUE	0xff		/* Maximum SAP value */
#define	NOVELLSAP	0x8137

/*
 * timerOn bit settings (used for statistics)
 */
#define	T_ACK_ON		0x80
#define	T_P_ON			0x40
#define	T_REJ_ON		0x20
#define	T_REM_BUSY_ON		0x10
#define	T_INACT_ON		0x08
#define	T_SEND_ACK_ON		0x04
#define	T_L2_ON			0x02
#define	T_RNR_ON		0x01

/*
 * type
 */
#define	LLC2_RCV_INT_LVL		1
#define	LLC2_SND_CONF_INT_LVL		2
#define	LLC2_SOLARIS_RCV_INT_LVL	3

/*
 * LLC2 state/event function error return values
 */
#define	LLC2_GOOD_STATUS	0 /* Everything's OK Good Return Value */
#define	LLC2_ERR_OUTSTATE	-1 /* event occurring in an improper state */
#define	LLC2_ERR_SYSERR		-2 /* system resource error */
#define	LLC2_ERR_MAXIFRAMES	-3 /* cannot accept more outbound I-frames */
#define	LLC2_ERR_BADPARM	-4 /* invalid parameter as part of the event */
#define	LLC2_ERR_BADPDU		-5 /* invalid PDU passed as part of the event */
#define	LLC2_ERR_DUPLICATE	-6 /* duplicate node in binary tree */
#define	LLC2_ERR_BADSTATE	-7 /* saved recover state invalid */
#define	LLC2_ERR_BADSTATION	-8 /* station not initialized */
#define	LLC2_ERR_ECHO_RCVD	-9 /* (frame relay) echo received */


/*
 * LLC2 connection component window handling algorithms (modulo 128)
 * These algorithms handle wrapping (e.g. send number < receive number).
 */
#define	MOD_128_DIFF(to, from) (((to) - (from)) & 0x7f)
#define	MOD_128_INCR(val) (((val) + 1) & 0x7f)
#define	MOD_128_DECR(val) (((val) - 1) & 0x7f)

/*
 * LLC2 connection component state change and tracking macro
 */
/* muoe970020 buzz 06/02/97 */
#define	CON_NEXT_STATE(ptr, val) \
	if ((ptr)->flag & NEED_ENABLEQ) { \
		qenable((ptr)->link->lk_wrq); \
		(ptr)->flag &= ~NEED_ENABLEQ; \
	} \
	(ptr)->stateOldest = (ptr)->stateOlder; \
	(ptr)->stateOlder = (ptr)->stateOld; \
	(ptr)->stateOld = (ptr)->state; \
	(ptr)->state = (val)

/*
 * LLC2 connection component source id (sid) validation macro
 */
#define	VALID_SID(sid)						\
	(((sid) != 0) &&					\
	(((llc2Con_t *)(sid))->signature == (uint_t)LLC2_CON_SIGNATURE))

/*
 * LLC2 timer bucket pointer increment macro
 */
#define	ADVANCE_BUCKET(bucketPtr, increment, wrapPtr)			\
	bucketPtr += increment;						\
	if (bucketPtr >= wrapPtr) {					\
		bucketPtr -= LLC2_NUM_BUCKETS;				\
	}

/*
 * LLC2 station component timer control structure
 */
typedef struct llc2TimerHead {
	volatile uchar_t flags;			/* timer control flags */
	uchar_t nu1;
	ushort_t nu2;
	volatile dLnkLst_t *curBucket;		/* current bucket ptr */
	volatile dLnkLst_t *expBucket;		/* oldest expired bucket ptr */
	dLnkLst_t bucket[LLC2_NUM_BUCKETS];	/* list of timer buckets */
} llc2TimerHead_t;

/* timer control flags definitions */
#define	BUCKET_WRAP		0x01	/* bucket pointers have wrapped */

/*
 * LLC2 connection component timer queue entry structure
 */
typedef struct llc2TimerEntry {
	dLnkLst_t chain;	/* chain to timerHead[i] in llc2Sta_t */
	uint_t expTime;	/* expiration time in tenth-seconds */
	ushort_t timerInt;	/* timer interval (in tenth-seconds) */
	struct llc2Con *conptr;	/* pointer to beginning of connection cb */
	mac_t  *mac;		/* Mac pointer */
	ushort_t  sid;		/* Station ID */
} llc2TimerEntry_t;

/* set llc2 timer granularity to 1/10 sec */
#define	LLC2_TIME_GRANULARITY    (int)(drv_usectohz(100000))
#define	LLC2_SHORT_TIMER	1	/* 1/10 (100 ms) sec timer */
#define	LLC2_CONVERT_TIME	5	/* 1/2 (500 ms) sec interval */
/* Default llc2 station parameters not set by GMI_SET_LLC2_PARMS */
#define	LLC2_SEND_ACK_TIMER_INT	2	/* send ack delay 2/10 sec */
#define	LLC2_LEVEL_2_TIMER_INT	0	/* level 2 delay 0/10 sec */
#define	LLC2_SEND_ACK_ALLOW	1	/* send ack without delay */
#define	LLC2_LEVEL_2_MAXRETRY	0	/* no level 2 retries */
#define	LLC2_RNR_TIMER_INT	65535	/* rnr limit 109 minutes */

/*
 * Timer type indices.  Used as indices into the station component's
 * timer head and timer interval arrays, and the connection component's
 * timer entry array.
 */
#define	T_ACK_INDEX		0	/* ack timer */
#define	T_P_INDEX		1	/* poll timer */
#define	T_REJ_INDEX		2	/* reject timer */
#define	T_REM_BUSY_INDEX	3	/* remote busy timer (allocb retry) */
#define	T_INACT_INDEX		4	/* inactivity timer */
#define	T_SEND_ACK_INDEX	5	/* send ack timer */
#define	T_L2_INDEX		6	/* level 2 retry timer (ntri enhance) */
#define	T_RNR_INDEX		7	/* RNR limit timer (ntri enhancement) */

#define	T_NUM_TIMERS		8	/* number of timer types supported */

/*
 * startTimer() and stopTimer() function call type parameter.  For start,
 * 'type' consists of at most one timer type OR'd with an optional modifier
 * flag. For stop, multiple timer types may be OR'd together.
 */
#define	T_ACK		(1 << T_ACK_INDEX)		/* 0x01 */
#define	T_P		(1 << T_P_INDEX)		/* 0x02 */
#define	T_REJ		(1 << T_REJ_INDEX)		/* 0x04 */
#define	T_REM_BUSY	(1 << T_REM_BUSY_INDEX)		/* 0x08 */
#define	T_INACT		(1 << T_INACT_INDEX)		/* 0x10 */
#define	T_SEND_ACK	(1 << T_SEND_ACK_INDEX)		/* 0x20 */
#define	T_L2		(1 << T_L2_INDEX)		/* 0x40 */
#define	T_RNR		(1 << T_RNR_INDEX)		/* 0x80 */

#define	T_OTHER		(T_P | T_REJ | T_REM_BUSY | T_ACK | T_L2)
#define	T_ALL		(T_P | T_REJ | T_REM_BUSY | T_ACK | T_L2	\
				| T_RNR | T_INACT)

/* timer type modifier flags */
#define	T_IF_NOT_RUNNING		0x8000	/* start timer IF NOT RUNNING */
/*
 * LLC2 connection component q queue entry structure
 *	used for: inbound suspend/resume(busy) queue in the llc2Sap
 *	outbound suspend/resume(xon/xoff) queue in the mac
 */
typedef struct llc2QEntry {
	dLnkLst_t chain;	/* chain to QHead in llc2Sta_t */
	struct llc2Con *conptr;	/* pointer to beginning of connection cb */
} llc2QEntry_t;

/* llc2XonReq() busy reasons */
#define	XON_NO_BUFFERS		1 /* testb failed for station input buffers */
#define	XON_SAP_SUSPENDED	2 /* sap suspended by dlpi user */
#define	XON_CON_SUSPENDED	3 /* connection suspended by dlpi user */
#define	XON_DLPI_BUSY		4 /* dlpi canput failed for connection */

typedef struct llc2Timer {
	uchar_t state; /* 802.2 station component state */
	ushort_t flags; /* Station Flags */
	ushort_t timerInt[T_NUM_TIMERS]; /* timer intervals */
	llc2TimerHead_t timerHead[T_NUM_TIMERS]; /* timer head structures */
	uint_t curTime; /* current time, in 1/10 sec (for timers) */
#ifdef _KERNEL
	ILD_DECL_LOCK(llc2_timer_lock);
#endif
} llc2Timer_t;

/*
 * LLC2 Free List structures
 */

typedef struct llc2FreeBuf {
	struct llc2FreeBuf *next;
	mblk_t *buf;
} llc2FreeBuf_t;

typedef struct llc2FreeList {
	struct llc2FreeBuf *head;
	uint_t available;
	ILD_DECL_LOCK(lock);
} llc2FreeList_t;

/*
 * At the most, there can only be 128 open saps. Since llc2StateMachine
 * changes the state before finally sending the M_PROTO upstream, we
 * need to ensure that there is atleast one mblk available per sap to
 * atleast be able to send a disconnect_ind. The data blocks are not
 * important since we can just drop them and other side will retransmit
 * them (the state is not changing for data blocks).
 */
#define	LLC2_FREELIST_MAX	128
#define	LLC2_FREELIST_MIN	56
#define	LLC2_FREEBUF_SIZE	sizeof (union DL_primitives)

/*
 * LLC2 station component control block structure
 */
typedef struct llc2Sta {
	/*
	 * processing fields
	 */
	mac_t *mac; /* mac structure ptr (for timer dispatching) */
	dLnkLst_t activeSapHead; /* doubly linked list of active saps */

	uchar_t state; /* 802.2 station component state */

	int maxRetry; /* N2 for all connections on this station */
	int maxRetryL2; /* number of level 2 retries */
	int sendAckAllow; /* number of frames rcvd before sending ack */

	int xmitWindowSz; /* k for all connections on this station */
	int rcvWindowSz; /* RW for all connections on this station */
	/* array of pointers to associated active SAPS */
	struct llc2Sap *sapTbl[LLC2_MAX_SAPS];

	/*
	 * PDU counters
	 */
	uint_t nullSapXidCmdRcvd;
	uint_t nullSapXidRspSent;
	uint_t nullSapTestCmdRcvd;
	uint_t nullSapTestRspSent;
	/*
	 * processing error counters
	 */
	uint_t outOfState;
	uint_t allocFail;
	uint_t protocolError;

#ifdef _KERNEL
	ILD_DECL_LOCK(sta_lock);
#endif
} llc2Sta_t;

/* station flags */
#define	NULL_SAP_OPEN		0x01	/* respond to null dsap xid and test */
#define	TIMERS_SUSPENDED	0x02	/* timers suspended by the i/o driver */

/*
 * LLC2 SAP component control block structure
 */
typedef struct llc2Sap {
	dLnkLst_t chain;	/* chain of llc2Sap_t on Avail list */
	/*
	 * processing fields
	 */
	uchar_t state;
	uchar_t sap;		/* the real SAP value, NOT the index */
	uchar_t flag;		/* flags */
	uchar_t nu1;		/* not used  */
	/* index to associated active connections */
	struct llc2Con *conTbl[LLC2_MAX_CONS];
	/* head of chain of connections prioritized by activity */
	dLnkLst_t conHead;

#ifdef _KERNEL
	ILD_DECL_LOCK(sap_lock);	/* lock structure for connections */
#endif
	/*
	 * PDU counters
	 */
	uint_t xidCmdSent;
	uint_t xidCmdRcvd;
	uint_t xidRspSent;
	uint_t xidRspRcvd;
	uint_t testCmdSent;
	uint_t testCmdRcvd;
	uint_t testRspSent;
	uint_t testRspRcvd;
	uint_t uiSent;
	uint_t uiRcvd;
	/*
	 * processing error counters
	 */
	uint_t outOfState;
	uint_t allocFail;
	uint_t protocolError;

	dLnkLst_t busyHead;	/* local busy connection chain head */

	llc2Sta_t *station;
} llc2Sap_t;

/*
 * LLC2 connection component control block structure
 */
typedef struct llc2Con {
	dLnkLst_t chain; /* chain of llc2Con_t on Avail list */
	uint_t signature; /* llc2Con signature (used to validate sid) */
	/*
	 * processing fields
	 */

	uchar_t stateOldest; /* connection component state history */
	uchar_t stateOlder;
	uchar_t stateOld;
	uchar_t state;
	ushort_t sid; /* SAP value and connection index */
	uint_t ppa; /* The PPA for this connection */
	link_t *link; /* DLPI link structure ptr for the connection */

	/* THIS NEEDS TO BE LONG */
	uint_t key; /* SAP value and connection index */
	llc2Sap_t *llc2Sap; /* sap structure ptr */
	dlsap_t rem; /* MAC addr and SAP value for the remote node */
	uchar_t flag; /* connection component processing flags */
	uchar_t dataFlag; /* DATA_FLAG */
	uchar_t rsp; /* C/R bit setting for next outbound I-PDU */
			/* or S-PDU during information transfer */
	uchar_t pf; /* P/F bit setting for next outbound I-PDU */
			/* or S-PDU during information transfer */
	int kAck; /* Frames acked since k changed last */
	int k; /* k - send window size */
	int kMax; /* Maximum k - for dynamic windowing */

	int vs; /* V(S) - next to send sequence number */
	int vsMax; /* largest V(S) - to validate a received N(R) */
	int vr; /* V(R) */
	int nrRcvd; /* N(R)_RECEIVED */
	int recoverState; /* state to return to if Recover Request rcvd */
	int retryCount; /* RETRY_COUNT */
	int frmrRcvdFlags; /* flags from last FRMR received */
	int allow; /* number of I-frames rcvd before sending ack */
	int allowCount; /* remaining number of inbound I-frames */
			/* allowed to be unacknowledged */
	int N2; /* N2 - number of level 1 retries */
	int pollRetryCount; /* POLL_RETRY_COUNT */
	int ackRetryCount; /* ACK_RETRY_COUNT */

	int N2L2; /* N2(level 2) - number of level 2 retries */
	int pollRetryCountL2; /* POLL_RETRY_COUNT (level 2) */
	int ackRetryCountL2; /* ACK_RETRY_COUNT (level 2) */

	dLnkLst_t unackHead; /* chain head of unacknowledged outbound I PDUs */
	dLnkLst_t resendHead; /* chain head of I PDUs to be re-transmitted */
	/* outbound suspend/resume data (xoff) queuing */
	llc2QEntry_t xoffEntry;
	llc2QEntry_t busyEntry; /* inbound suspend/resume data (busy) queuing */

	int macOutSave; /* # of outbound I PDUs held by the MAC driver */
			/* to be saved by LLC2 on return */
	int macOutDump; /* # of outbound I PDUs held by the MAC driver */
			/* to be discarded by LLC2 on return */
	llc2TimerEntry_t timerEntry[T_NUM_TIMERS];

#ifdef _KERNEL
	ILD_DECL_LOCK(con_lock);	/* lock structure for connections */
#endif
	/*
	 * OSS/SNA statistic counters
	 */
	ushort_t	framesSent;
	ushort_t	framesRcvd;
	ushort_t	framesSentError;
	ushort_t	framesRcvdError;
	ushort_t	t1TimerExp;
	uchar_t	cmdLastSent;
	uchar_t	cmdLastRcvd;

	/*
	 * PDU counters
	 */
	uint_t iSent;
	uint_t iRcvd;
	uint_t frmrSent;
	uint_t frmrRcvd;
	uint_t rrSent;
	uint_t rrRcvd;
	uint_t rnrSent;
	uint_t rnrRcvd;
	uint_t rejSent;
	uint_t rejRcvd;
	uint_t sabmeSent;
	uint_t sabmeRcvd;
	uint_t uaSent;
	uint_t uaRcvd;
	uint_t discSent;
	/*
	 * processing error counters
	 */
	uint_t outOfState;
	uint_t allocFail;
	uint_t protocolError;
	uint_t localBusy;
	uint_t remoteBusy;
	uint_t maxRetryFail;
	/*
	 * timer expiration counters
	 */
	uint_t ackTimerExp;
	uint_t pollTimerExp;
	uint_t rejTimerExp;
	uint_t remBusyTimerExp;
	uint_t inactTimerExp;
	uint_t sendAckTimerExp;
	uint_t level2TimerExp;

	llc2FrmrInfo_t frmrInfo; /* FRMR info saved for a RE-SEND_FRMR_RSP */
} llc2Con_t;

/* llc2Con signature */
#define	LLC2_CON_SIGNATURE 0x87654321U
/*
 * UPDATE_COUNTER(conPtr, counter, mac)
 *
 * increment a 16-bit statistic counter and, if threshold reached,
 * send LLC_STATS_OVERFLOW
 */
#define	UPDATE_COUNTER(conPtr, counter, mac)	(conPtr)->counter++;
#define	SRB_LENGTH		18	/* Length of MAX Source Routing field */
/*
 * flag
 */
#define	P_FLAG		0x80
#define	F_FLAG		0x40
#define	S_FLAG		0x20
#define	REMOTE_BUSY	0x10
#define	RESEND_PENDING	0x08

/* NTRI enhancements flags */
#define	CON_SECONDARY	0x0008	/* SABME received caused normal state */
#define	R_FLAG	0x0004	/* re-route recovery in progress */
/* muoe970020 buzz 06/02/97 */
#define	NEED_ENABLEQ	0x0002
#define	CON_SUSPENDED	0x0001	/* user requested suspend connection */

#define	SNA_INN	0x8000	/* SNA INN connection */
#define	T_FLAG	0x4000	/* null dsap TEST rcvd while connected */

/* llc2Con_t dataFlag field definition */
#define	DATA_ACCEPTED	0	/* Data PDUs NOT discarded during Local Busy */
#define	DATA_DISCARDED	1	/* Data PDUs WERE discarded during Local Busy */
#define	BUSY_REJECT	2	/* Busy State enter while REJECT outstanding */
#endif /* _KERNEL */
#if defined(LLC2_C)
/*
 *	LLC2 STATES AND EVENTS	**
 */

/*
 * WARNING :
 * Any state and/or event modifications below must be reflected in the LLC2
 * component state machine tables in llc2.c
 */

/*
 * Station component states
 */
#define	STA_DOWN		0
#define	STA_UP			1
#define	STA_STATES_NUM		2

/*
 * SAP component states
 */
#define	SAP_INACTIVE		0
#define	SAP_ACTIVE		1
#define	SAP_STATES_NUM		2

/*
 * Connection component states
 */
#define	CON_ADM			0
#define	CON_CONN		1
#define	CON_RESET_WAIT		2
#define	CON_RESET_CHECK		3
#define	CON_SETUP		4
#define	CON_RESET		5
#define	CON_D_CONN		6
#define	CON_ERROR		7
#define	CON_NORMAL		8
#define	CON_BUSY		9
#define	CON_REJECT		10
#define	CON_AWAIT		11
#define	CON_AWAIT_BUSY		12
#define	CON_AWAIT_REJECT	13
#define	CON_STATES_NUM		14

/*
 * Station component events
 */
#define	STA_ENABLE_REQ		0
#define	STA_DISABLE_REQ		1
#define	STA_RCV_NULL_DSAP_XID_CMD	2
#define	STA_RCV_NULL_DSAP_TEST_CMD	3
#define	STA_RCV_BAD_PDU		4
#define	STA_OTHER_EVENT		5	/* must match the first SAP event */

/*
 * SAP component events
 */
#define	SAP_ACTIVATION_REQ	5
#define	SAP_UNITDATA_REQ	6
#define	SAP_XID_REQ		7
#define	SAP_TEST_REQ		8
#define	SAP_DEACTIVATION_REQ	9
#define	SAP_RCV_UI		10
#define	SAP_RCV_XID_CMD		11
#define	SAP_RCV_XID_RSP		12
#define	SAP_RCV_TEST_CMD	13
#define	SAP_RCV_TEST_RSP	14
#define	SAP_RCV_BAD_PDU		15
#define	SAP_OTHER_EVENT		16	/* must match the first CON event */

/*
 * Connection component events
 */
#define	CON_CONNECT_REQ		16
#define	CON_CONNECT_RES		17
#define	CON_DATA_REQ		18
#define	CON_DISCONNECT_REQ	19
#define	CON_RESET_REQ		20
#define	CON_RESET_RES		21
#define	CON_LOCAL_BUSY_CLR	22
#define	CON_RESEND_PENDING_CLR	23
/*
 * only code CON_RCV_xxx_yyy between CON_RCV_BEGIN and CON_RCV_END
 */
#define	CON_RCV_BEGIN		24	/* must match the first RCV event */
#define	CON_RCV_BAD_PDU		24
#define	CON_RCV_DISC_CMD	25
#define	CON_RCV_DM_RSP		26
#define	CON_RCV_FRMR_RSP	27
#define	CON_RCV_I_CMD		28
#define	CON_RCV_I_CMD_UNEXP_NS	29
#define	CON_RCV_I_CMD_INVALID_NS	30
#define	CON_RCV_I_RSP		31
#define	CON_RCV_I_RSP_UNEXP_NS	32
#define	CON_RCV_I_RSP_INVALID_NS	33
#define	CON_RCV_REJ_CMD		34
#define	CON_RCV_REJ_RSP		35
#define	CON_RCV_RNR_CMD		36
#define	CON_RCV_RNR_RSP		37
#define	CON_RCV_RR_CMD		38
#define	CON_RCV_RR_RSP		39
#define	CON_RCV_SABME_CMD	40
#define	CON_RCV_UA_RSP		41
#define	CON_RCV_ZZZ_CMD_INVALID_NR	42
#define	CON_RCV_ZZZ_RSP_INVALID_NR	43
#define	CON_RCV_END		43	/* must match the last RCV event */

#define	CON_POLL_TIMER_EXP	44
#define	CON_ACK_TIMER_EXP	45
#define	CON_REJ_TIMER_EXP	46
#define	CON_REM_BUSY_TIMER_EXP	47
#define	CON_INITIATE_P_F_CYCLE	48
#define	CON_SEND_ACK_TIMER_EXP	49
#define	CON_LEVEL2_TIMER_EXP	50
#define	CON_RNR_TIMER_EXP	51
#define	CON_MAC_XON_IND		52
#define	UNK_OTHER_EVENT		53


/*
 *		LLC2 FUNCTIONS
 */

int llc2Open(queue_t *, dev_t *, int, int, cred_t *);
int llc2Close(queue_t *, int, cred_t *);
int llc2WritePut(queue_t *, mblk_t *);
int llc2StaInit(mac_t *, llc2Init_t *);
int llc2StaUninit(mac_t *, llc2Uninit_t *);
int llc2GetStaStats(llc2GetStaStats_t *);
int llc2GetSapStats(llc2GetSapStats_t *);
int llc2GetConStats(llc2GetConStats_t *);
int llc2GetConParmsReq(llc2ConParms_t *);
int llc2SetConParmsReq(ushort_t, ushort_t, llc2ConParms_t *);
int llc2SetTuneParmsReq(mac_t *, queue_t *, mblk_t *);
void dlpiCheckSleepingLinks(int);

int llc2BindReq(link_t *, uint_t, uint_t);
int llc2UnbindReq(mac_t *, uint_t);
int llc2UnitdataReq(mac_t *, dlsap_t *, dlsap_t *, mblk_t *,
			snaphdr_t *, int, uint_t);
int llc2TestReq(mac_t *, dlsap_t *, dlsap_t *, mblk_t *, uint_t);
int llc2XidReq(mac_t *, dlsap_t *, dlsap_t *, uint_t, uint_t,
			mblk_t *, uint_t);
int llc2ConnectReq(mac_t *, link_t *, dlsap_t *, dlsap_t *);
int llc2ConnectRes(mac_t *, link_t *, dlsap_t *, dlsap_t *, ushort_t);
int llc2ResetReq(mac_t *, ushort_t);
int llc2ResetRes(mac_t *, ushort_t);
int llc2DisconnectReq(mac_t *, ushort_t);
int llc2DataReq(mac_t *, uint_t, mblk_t *, dlsap_t *);
int llc2XonReq(mac_t *, ushort_t);

	int llc2ReadSrv(queue_t *);
void llc2TimerIsr(void *);
#ifdef JUNK
	int llc2TimerSrv(queue_t *);
#endif
int llc2TimerSrv();
int llc2StateMachine(uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *,
	uint_t, uintptr_t, uintptr_t);
	int llc2GetqEnableStatus(queue_t *);

/*
 * State/event functions are ordered as in the ISO 8802-2 state
 * machine descriptions.
 */

/*
 * Station
 */
int downInvalidEvt(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int downEnableReq(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int upInvalidEvt(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int upDisableReq(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int upRcvNullDsapXidCmd(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int upRcvNullDsapTestCmd(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int upRcvBadPdu(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

/*
 * SAP
 */
int inactiveInvalidEvt(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int inactiveActivationReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int activeActivationReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvUi(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeUnitdataReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeXidReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvXidCmd(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvXidRsp(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeTestReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvTestCmd(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvTestRsp(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeDeactivationReq(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int activeRcvBadPdu(llc2Sap_t *, llc2Sta_t *, uchar_t,
	mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

/*
 * Connection
 */
int admInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int admConnectReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int admRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int admRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int admRcvXxxCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int admRcvXxxRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int connInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connConnectRes(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connDisconnectReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int connRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int resetwaitInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitResetReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitLocalBusyClr(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRecoverReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int resetwaitDisconnectReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetwaitSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int resetcheckInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckResetRes(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckDisconnectReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetcheckSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int setupInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvUaRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int setupRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int resetInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvUaRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int resetSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int dconnInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvUaRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvXxxYyy(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int dconnSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int errorInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvFrmrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvXxxCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvXxxRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int errorSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int xferDisconnectReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferResetReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvSabmeCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvDiscCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvFrmrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvDmRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvZzzCmdInvalidNr(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvZzzRspInvalidNr(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvICmdInvalidNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvIRspInvalidNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvBadPdu(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvUaRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferRcvXxxRspBadPF(llc2Con_t *, llc2Sta_t *,
	mac_t *, mblk_t *);
int xferRnrTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int xferMacXonInd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
void xferMaxRetry(llc2Con_t *, llc2Sta_t *, mac_t *, uint_t, uchar_t);

int normalInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalDataReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalRemBusyTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int normalSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int busyInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyDataReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyLocalBusyClr(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRemBusyTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busyRejTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int busySendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int rejectInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectDataReq(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRemBusyTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectRejTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int rejectSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

int awaitInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitL2TimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);


int awaitbusyInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyLocalBusyClr(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusySendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyL2TimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitbusyInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);


int awaitrejectInvalidEvt(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvICmdUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvIRspUnexpNs(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvICmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvIRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRnrCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRnrRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRejCmd(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectRcvRejRsp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectPollTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectSendAckTimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectL2TimerExp(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);
int awaitrejectInitiatePFCycle(llc2Con_t *, llc2Sap_t *, llc2Sta_t *,
	uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

void disableSap(llc2Sap_t *, llc2Sta_t *, mac_t *);
int enableCon(llc2Con_t **, llc2Sap_t *, llc2Sta_t *, mac_t *,
	dlsap_t *);
void resetCon(llc2Con_t *, llc2Sta_t *);
void disableCon(llc2Con_t *, llc2Sap_t *, llc2Sta_t *, mac_t *);
void setRemBusy(llc2Con_t *, llc2Sta_t *);
void clrRemBusy(llc2Con_t *, llc2Sta_t *);
void updateNrRcvd(llc2Con_t *, llc2Sta_t *, mac_t *, uchar_t, int);
void sendAck(llc2Con_t *, llc2Sta_t *, mac_t *, int, int, mblk_t *);
void sendSabme(llc2Con_t *, llc2Sta_t *, mac_t *, int, mblk_t *);
int sendUa(llc2Con_t *, llc2Sta_t *, mac_t *, int, mblk_t *);
void sendSup(uchar_t, llc2Con_t *, llc2Sta_t *, mac_t *, int, int,
	mblk_t *);
void sendFrmr(llc2Con_t *, llc2Sta_t *, mac_t *, int, llc2FrmrInfo_t *,
	mblk_t *);
void sendDisc(llc2Con_t *, llc2Sta_t *, mac_t *, int, mblk_t *);
void sendDm(mac_t *, llc2Con_t *, dlsap_t *, uchar_t, int, mblk_t *);
int sendI(llc2Con_t *, llc2Sta_t *, mac_t *, int, int, mblk_t *);
void setResendI(llc2Con_t *, uchar_t, uchar_t);
int tryOutI(llc2Con_t *, llc2Sta_t *, mac_t *, int, int);
void startTimer(ushort_t, llc2Con_t *, llc2Sta_t *);
void stopTimer(ushort_t, llc2Con_t *, llc2Sta_t *);
mblk_t *check_if_reusable(mblk_t *, int);
#ifdef LOG_ALLOC_FAIL
void allocFail(uchar_t, mac_t *, uint_t, uchar_t);
#endif

#endif /* LLC2_C */

#ifdef	__cplusplus
}
#endif

#endif /* _LLC2_LLC2K_H */
