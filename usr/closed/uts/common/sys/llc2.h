/*
 * Copyright (c) 1998, 1999, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 1996,1997 NCR Corporation, Dayton, Ohio USA
 *
 */

#ifndef	_SYS_LLC2_H
#define	_SYS_LLC2_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	ETHER_ADDR_SIZE	8
#define	IEEE_ADDR_SIZE	7
#define	TOKEN_ADDR_SIZE	7

/*
 * XDLPI DLSAP definition
 */
typedef union dlsap {
	struct {
		uchar_t dl_nodeaddr[6];
		ushort_t dl_type;
	} ether;
	struct {
		uchar_t dl_nodeaddr[6];
		uchar_t dl_sap;
	} llc;
} dlsap_t;

typedef struct ether_hdr {
	uchar_t dl_nodeaddr[6];
	ushort_t dl_type;
} ether_hdr_t;


#define	DL_MAXSTATE DL_SUBS_BIND_PND

typedef struct {
    uint_t mactype;
    uint_t ppa;
} init_t;

typedef struct {
    uint_t mactype;
    uint_t ppa;
} uninit_t;

#define	LLC2_MAX_SAPS	128	/* maximum number of SAPs per station */
#define	LLC2_MAX_CONS	256	/* maximum number of connections per SAP */

/*
 * LLC2_INIT data structure for the ILD_LLC2 ioctl command
 */
typedef struct llc2Init {
	uint_t ppa;		/* Physical Point of Attachment number */
	uint_t cmd;		/* LLC2_INIT */
	ushort_t ackTimerInt;	/* ack timer duration (1/2 seconds) */
	ushort_t pollTimerInt;	/* P-timer duration (1/2 seconds) */
	ushort_t rejTimerInt;	/* reject timer duration (1/2 seconds) */
	ushort_t remBusyTimerInt; /* remote busy timer duration (1/2 seconds) */
	ushort_t inactTimerInt;	/* inactivity timer duration (1/2 seconds) */
	ushort_t maxRetry;	/* maximum retry value */
	ushort_t xmitWindowSz;	/* transmit window size */
	ushort_t rcvWindowSz;	/* receive window size */
	/* muoe963102 buzz 12/05/96 */
	ushort_t timeinterval;	/* timer tick count multiplier */
	/* buzz muoe972185 10/02/97  */
	ushort_t rspTimerInt;	/* rsp timer duration (1/2 seconds) */
	ushort_t loopback;	/* loopback in SAM this node/bcast/mcast pkts */
} llc2Init_t;

/*
 * LLC2_UNINIT data structure for the ILD_LLC2 ioctl command
 */
typedef struct llc2Uninit {
	uint_t ppa;		/* Physical Point of Attachment number */
	uint_t cmd;		/* LLC2_UNINIT */
} llc2Uninit_t;

/*
 * Initialization structure for Ethernet/802.3/Starlan
 */
typedef struct {
	uchar_t macaddr[6];	/* Physical address */
	uchar_t multicast1[6];	/* Multicast address */
	uchar_t multicast2[6];	/* Multicast address */
	uchar_t multicast3[6];	/* Multicast address */
	uchar_t multicast4[6];	/* Multicast address */
	unsigned int novell;	/* use length or type for Novell */
} ether_init_t;

typedef struct {
	uchar_t nothing;
} ether_uninit_t;

/*
 * LLC2_GET_STA_STATS data structure for the ILD_LLC2 ioctl command
 */

typedef struct llc2GetStaStats {
	uint_t ppa;		/* Physical Point of Attachment number */
	uint_t cmd;		/* LLC2_GET_STA_STATS */
	uchar_t clearFlag;	/* clear counters flag (1 = yes, 0 = no) */
	uchar_t state;		/* station component state */
	ushort_t numSaps;	/* # of active SAPs in the saps array */
	uchar_t saps[LLC2_MAX_SAPS]; /* array of active SAP values */
	uint_t nullSapXidCmdRcvd; /* # of NULL SAP XID commands received */
	uint_t nullSapXidRspSent; /* # of NULL SAP XID responses sent */
	uint_t nullSapTestCmdRcvd; /* # of NULL SAP TEST commands received */
	uint_t nullSapTestRspSent; /* # of NULL SAP TEST responses sent */
	uint_t outOfState;	/* # of events rcvd in invalid state */
	uint_t allocFail;	/* # of buffer allocation failures */
	uint_t protocolError;	/* # of protocol errors */
} llc2GetStaStats_t;

/*
 * LLC2_GET_SAP_STATS data structure for the ILD_LLC2 ioctl command
 */
typedef struct llc2GetSapStats {
	uint_t ppa;		/* Physical Point of Attachment number */
	uint_t cmd;		/* LLC2_GET_SAP_STATS */
	uchar_t sap;		/* SAP value */
	uchar_t clearFlag;	/* clear counters flag (1 = yes, 0 = no) */
	uchar_t state;		/* SAP component state */
	uint_t numCons;	/* # of active connections in the cons array */
	ushort_t cons[LLC2_MAX_CONS]; /* array of active connection indexes */
	uint_t xidCmdSent;	/* # of XID commands sent */
	uint_t xidCmdRcvd;	/* # of XID commands received */
	uint_t xidRspSent;	/* # of XID responses sent */
	uint_t xidRspRcvd;	/* # of XID responses received */
	uint_t testCmdSent;	/* # of TEST commands sent */
	uint_t testCmdRcvd;	/* # of TEST commands received */
	uint_t testRspSent;	/* # of TEST responses sent */
	uint_t testRspRcvd;	/* # of TEST responses received */
	uint_t uiSent;		/* # of UI frames sent */
	uint_t uiRcvd;		/* # of UI frames received */
	uint_t outOfState;	/* # of events rcvd in invalid state */
	uint_t allocFail;	/* # of buffer allocation failures */
	uint_t protocolError;	/* # of protocol errors */
} llc2GetSapStats_t;

/*
 * LLC2_GET_CON_STATS data structure for the ILD_LLC2 ioctl command
 */
typedef struct llc2GetConStats {
	uint_t ppa;		/* Physical Point of Attachment number */
	uint_t cmd;		/* LLC2_GET_CON_STATS */
	uchar_t sap;		/* SAP value */
	ushort_t con;		/* connection index */
	uchar_t clearFlag;	/* clear counters flag (1 = yes, 0 = no) */
	uchar_t stateOldest;	/* connection component state trace */
	uchar_t stateOlder;
	uchar_t stateOld;
	uchar_t state;
	ushort_t sid;		/* SAP value and connection index */
	dlsap_t rem;		/* remote MAC address and SAP pair */
	ushort_t flag;		/* connection component processing flag */
	uchar_t dataFlag;	/* DATA_FLAG */
	uchar_t k;		/* transmit window size */
	uchar_t vs;		/* V(S) */
	uchar_t vr;		/* V(R) */
	uchar_t nrRcvd;		/* N(R)_RECEIVED */
	ushort_t retryCount;	/* RETRY_COUNT */
	uint_t numToBeAcked;	/* # of outbound I-frames to be acknowledged */
	uint_t numToResend;	/* # of outbound I-frames to be re-sent */
	uint_t macOutSave;	/* # of outbound I-frames held by the MAC  */
				/*   driver to be saved on return to LLC2  */
	uint_t macOutDump;	/* # of outbound I-frames held by the MAC  */
				/*   driver to be dumped on return to LLC2 */
	uchar_t timerOn;	/* timer activity flag */
	uint_t iSent;		/* # of I-frames sent */
	uint_t iRcvd;		/* # of I-frames received */
	uint_t frmrSent;	/* # of frame rejects sent */
	uint_t frmrRcvd;	/* # of frame rejects received */
	uint_t rrSent;		/* # of RRs sent */
	uint_t rrRcvd;		/* # of RRs received */
	uint_t rnrSent;	/* # of RNRs sent */
	uint_t rnrRcvd;	/* # of RNRs received */
	uint_t rejSent;	/* # of rejects sent */
	uint_t rejRcvd;	/* # of rejects received */
	uint_t sabmeSent;	/* # of SABMEs sent */
	uint_t sabmeRcvd;	/* # of SABMEs received */
	uint_t uaSent;		/* # of UAs sent */
	uint_t uaRcvd;		/* # of UAs received */
	uint_t discSent;	/* # of DISCs sent */
	uint_t outOfState;	/* # of events rcvd in invalid state */
	uint_t allocFail;	/* # of buffer allocation failures */
	uint_t protocolError;	/* # of protocol errors */
	uint_t localBusy;	/* # of times in a local busy state */
	uint_t remoteBusy;	/* # of times in a remote busy state */
	uint_t maxRetryFail;	/* # of failures due to reaching maxRetry */
	uint_t ackTimerExp;	/* # of ack timer expirations */
	uint_t pollTimerExp;	/* # of P-timer expirations */
	uint_t rejTimerExp;	/* # of reject timer expirations */
	uint_t remBusyTimerExp; /* # of remote busy timer expirations */
	uint_t inactTimerExp;	/* # of inactivity timer expirations */
	uint_t sendAckTimerExp; /* # of send ack timer expirations */
} llc2GetConStats_t;

/*
 * Parameter structure used with:
 * 1. LLC_SET_CON_PARMS_REQ - change parameters for a connection.  Can be
 *				issued anytime after DL_CONNECT_REQ (send SABME)
 *				or DL_CONNECT_RSP (SABME rcvd and accepted by
 *				the user).  The station parameters set during
 *				LLC2_INIT are used for the connection
 *				if LLC_SET_CON_PARMS_REQ is not issued.
 * 2. LLC_GET_CON_PARMS_ACK - return parameters in use by a connection in
 *				response to LLC_GET_CON_PARMS_REQ.
 *
 * Timer values are in units of one tenth of a second.
 */
typedef struct {
	int  llc_ack_timer;	/* 802.2 acknowledgement (T1) timer */
	int  llc_poll_timer;	/* 802.2 poll (T1) timer */
	int  llc_rej_timer;	/* 802.2 reject timer */
	int  llc_busy_timer;	/* 802.2 remote busy timer */
	int  llc_inac_timer;	/* 802.2 inactivity (TI) timer */
	int  llc_ackdelay_timer; /* 802.2 ack delay (T2) timer */
	int  llc_l2_timer;	/* level 2 retries (poll+ack) timer */
	int  llc_rnr_limit_timer; /* SNA remote busy (RNRLIMT) timer */
	int  llc_ackdelay_max;	/* ack delay outstanding ack limit */
	int  llc_maxretry;	/* 802.2 (level 1) retry limit */
	int  llc_l2_maxretry;	/* level 2 retry limit */
	int  llc_xmitwindow;	/* 802.2 transmit window size */
	int  llc_flag;		/* flags */
} llc_con_parms_t;

typedef struct {
	uint_t   ppa;		/* Physical Point of Attachment # */
	uint_t   cmd;		/* LLC_SET_CON_PARMS or LLC_GET_CON_PARMS  */
	uchar_t  sap;		/* SAP value */
	ushort_t  con;		/* connection index */
	llc_con_parms_t	parms;
} llc2ConParms_t;

/*
 * timerOn bit settings
 */
#define	T_ACK_ON	0x80
#define	T_P_ON		0x40
#define	T_REJ_ON	0x20
#define	T_REM_BUSY_ON	0x10
#define	T_INACT_ON	0x08
#define	T_SEND_ACK_ON	0x04

/*
 * Adapter states
 */
#define	MAC_INST	0x0001 /* adapter installed */
#define	MAC_INIT	0x0004 /* adapter initialized and opened */

/*
 * ioctl commands
 */
#define	ILD_INIT		0x0001  /* initialize adapter */
#define	ILD_UNINIT		0x0002  /* uninitialize adapter */
#define	ILD_MAC			0x0003  /* MAC specific ioctl */
#define	ILD_TCAPSTART		0x0007  /* start trace capture */
#define	ILD_TCAPSTOP		0x0008  /* stop trace capture */
#define	ILD_GCONFIG		0x000a  /* Read adapter config. (new way) */
#define	ILD_PPA_INFO		0x000d  /* Get current/max PPA info */
#define	ILD_PPA_CONFIG   	0x000f  /* Change PPA Configuration */


#define	ILD_LLC2	(('I'<<24)|8) /* LLC2 specific ioctl */

#define	NOVELL_LENGTH	0x04	/* use length for Novell */
#define	NOVELL_TYPE	0x03	/* use type   for Novell */

/*
 * LLC2 specific commands for ILD_LLC2
 */
#define	LLC2_INIT		(('L'<<24)|1)  /* initialize LLC2  station */
#define	LLC2_UNINIT		(('L'<<24)|2)  /* uninitialize LLC2  station */
#define	LLC2_GET_STA_STATS	(('L'<<24)|3)  /* get station statistics */
#define	LLC2_GET_SAP_STATS	(('L'<<24)|4)  /* get SAP statistics */
#define	LLC2_GET_CON_STATS	(('L'<<24)|5)  /* get connection statistics */

#define	LLC_GET_CON_PARMS_REQ	(('L'<<24)|8) /* get connection parameters */
#define	LLC_SET_CON_PARMS_REQ	(('L'<<24)|9) /* set connection parameters */
#define	LLC_GETPPA		(('L'<<24)|10) /* get PPA for lower MAC */
#define	LLC_SETPPA		(('L'<<24)|11) /* set up PPA for lower MAC */

/*
 * MAC (non-Token Ring) specific commands for ILD_MAC
 */
#define	ConfigMCast	0x01	/* configure an ethernet multicast address */
#define	ReconfigMCast	0x02	/* reconfigure an ethernet multicast address */

#define	CFGMULTICAST	4	/* MCast_t struct number of addresses */


/*
 * MAC (Token Ring) specific commands for ILD_MAC
 */
#define	SetFunctionalAddr	0x03
#define	ClearFunctionalAddr	0x04
#define	SetGroupAddr		0x05
#define	ClearGroupAddr		0x06


/*
 * ioc_rval values for ioctl NAK responses
 */
#define	MCErr_Already	0xf1	/* Already configured */
#define	MCErr_Invalid	0xf2	/* Not a valid multicast address */
#define	MCErr_Overflow	0xf3	/* Too many multicast addresses already */
#define	MCErr_None	0xf4	/* Not already configured */
#define	MCErr_Command	0xf5	/* Bad command */

#define	RVAL_CMD_UNKNOWN	-2  /* unknown command */
#define	RVAL_PARM_INVALID	-3  /* invalid parameter in the command */
#define	RVAL_SYS_ERR		-4  /* system error (e.g. allocation failure) */
#define	RVAL_STA_INVALID	-5  /* invalid LLC2 station number */
#define	RVAL_STA_OUTSTATE	-6  /* LLC2 station not in correct state */
#define	RVAL_SAP_INVALID	-7  /* invalid LLC2 SAP number */
#define	RVAL_CON_INVALID	-8  /* invalid LLC2 connection number */
#define	RVAL_MAC_INVALID	-9  /* invalid MAC number */

/*
 * llc2_ioctl_t is a union for a bunch of ILD_LLC2 and ILD_MAC
 * type ioctls. At the minimum the mblk passed should contain ppa
 * and cmd as defined in ild_header_t.
 */
typedef struct {
	uint_t ppa;
	uint_t cmd;
} ild_header_t;

typedef struct {
	uint_t ppa;
	uint_t cmd;
	uint_t data1;
	uint_t data2;
	ushort_t buf[256];
} llc2_ioctl_t;

typedef struct {
	uint_t ppa;
	uint_t cmd;
	uint_t options;
} lk_options_t;

typedef struct ppa_info {
	uint_t ppa;	/* Not USED */
	uint_t cmd;	/* Command ILD_PPA_INFO */
	uint_t curppa;	/* Highest numbered Active PPA */
	uint_t maxppa;	/* Maximum number configured in the system */
			/* This value reflects the kernel tunable ILDMAXPPA */
} ppa_info_t;

typedef struct ppa_config {
	/* PPA we want assigned to the MAC with the same index */
	uint_t ppa;
	uint_t cmd;		/* Command L_SETPPA/L_GETPPA */
	uint_t index;		/* Index of Lower Q - (muxid from I_LINK) */
	uint_t instance;	/* Instance number of Lower MAC  */
} ppa_config_t;

/* muoe940783 3/9/94 buzz submit 3com AOM to 2g and need support for XPOS */
typedef struct {
	uchar_t  ppa;
	uchar_t  state;
	ushort_t  adapterid;
	uchar_t  bia[6];
} adapter_t;

typedef struct {
	uint_t ppa;
	uint_t cmd;
	uchar_t multicast[CFGMULTICAST][6];
} MCast_t;

/*
 * llc field definitions in the adapter_t structure
 */
#define	LLC_RESIDENT		1 /* LLC1/LLC2 is resident on the adapter */
/* LLC (or something) is resident on adapter */
#define	ADAPTER_RESIDENT	1

#define	DOWNLOAD_REQUIRED	2 /* Adapter Download required */
#define	SRB_SUPPORTED		4 /* Source Route Bridging support required */
#define	HOST_LLC_IN_USE		8 /* MAC is using the HOST-BASED LLC */

/*
 * define tracing stuff
 */
typedef struct ildTraceEntry {
	uint_t time;
	uint_t cpu_mod_line;
	uint_t parm1;
	uint_t parm2;
} ildTraceEntry_t;

#define	ILDTCAPSIZE 64

typedef struct tCapBuf {
	uint_t seqNum;
	uint_t lastFlag;
	ildTraceEntry_t t[ILDTCAPSIZE];
} tCapBuf_t;

#define	ILDTRCTABSIZ 512   /* number of trace table entries, must be **2 */


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_LLC2_H */
