/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 1992-1998 NCR Corporation, Dayton, Ohio USA
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#define	LLC2_C

/*
 * The PSARC case number PSARC 1998/360 contains additional material
 * including the design document for this driver.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/mkdev.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/log.h>
#include <sys/stropts.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/systm.h>
#include <sys/policy.h>

#include <sys/llc2.h>
#include "ild.h"
#include "llc2k.h"
#define	THIS_MOD MID_LLC2
#include "ildlock.h"
#include <sys/dlpi.h>

/*
 *
 * Some Helpful Acronyms and Definitions
 * =====================================
 * Acronyms
 * --------
 * ABM		Asynchronous Balanced Mode
 * ACK		ACKnowledge
 * ADM		Asynchronous Disconnected Mode
 * C		Command(Also CMD)
 * CCITT	International Telegraph and Telephone Consultative Committee
 * C/R		Command/Response
 * DA		Destination Address
 * DCE		Data Circuit-Terminating Equipment
 * DISC		DISConnect
 * DM		Disconnected Mode
 * DSAP		Destination Service Access Point
 * DTE		Data Terminal Equipment
 * F		Final
 * FCS		Frame Check Sequence
 * FRMR		FRaMe Reject
 * HDLC		High-level Data Link control
 * I		Information or Information Transfer Format
 * LLC		Logical Link Control
 * LSAP		Link layer Service Access Point
 * LSB		Least Significant Bit
 * MAC		Medium Access Control
 * N(R)		Receive Sequence Number
 * N(S)		Send Sequence Number
 * P		Poll
 * PDU		Protocol Data Unit
 * P/F		Poll/Final
 * R		Response
 * REJ		REJect
 * RNR		Receive Not Ready
 * RR		Receive Ready
 * S		Supervisory(also Sup)
 * SA		Source Address
 * SABME	Set Asynchronous Balanced Mode Extended
 * SAP		Service Access Point
 * SSAP		Source Service Access Point
 * TEST		TEST frame
 * U		Unnumbered format
 * UA		Unnumbered Acknowledgment
 * UI		Unnumbered Information
 * V(R)		Receive state Variable
 * V(S)		Send state Variable
 * XID		eXchange IDentification
 *
 * Definitions
 * -----------
 * address fields(DSAP and SSAP). The ordered pair of service access point
 * addresses at the beginning of an LLC PDU which identifies the LLC(s)
 * designated to receive the PDU and the LLC sending the PDU. Each
 * address field is one octet in length.
 *
 * command. In data communications, an instruction represented in the control
 * field of a PDU and transmitted by an LLC. It causes the addressed LLC(s)
 * to execute a specific data link control function.
 *
 * command PDU. All PDUs transmitted by an LLC in which the C/R bit is = 0.
 *
 * control field(C). The field immediately following the DSAP and SSAP address
 * fields of a PDU. The content of the control field is interpreted by the
 * receiving destination LLC(s) designated by the DSAP address field:
 * (a) As a command, from the source LLC designated by the SSAP address
 * field, instructing the performance of some specific function; or
 * (b) As a response, from the source LLC designated by the SSAP address
 * field.
 *
 * information field(I). The sequence of octets occurring between the control
 * field and the end of the LLC PDU. The information field contents of I,
 * TEST, and UI PDUs are not interpreted at the LLC sublayer.
 *
 * LLC. That part of a data station that supports the logical link control
 * functions of one or more logical link.s The LLC generates command PDUs
 * and response PDUs for transmission and interprets received command PDUs
 * and response PDUs. Specific responsibilities assigned to an LLC include
 * a) Initiation of control signal interchange,
 * b) Organization of data flow,
 * c) Interpretation of received command PDUs and generation of appropriate
 * response PDUs, and
 * d) Actions regarding error control and error recovery functions in the
 * LLC sublayer.
 *
 * MAC. That part of a data station that supports the medium access control
 * functions that reside just below the LLC sublayer. The MAC procedures
 * include framing/deframing data units, performing error checking, and
 * acquiring the right to use the underlying physical medium.
 *
 * protocol data unit(PDU). The sequence of contiguous octets delivered as
 * a unit from or to the MAC sublayer. A valid LLC PDU is at least 3 octets
 * in length, and contains two address fields(DSAP/SSAP) and a control
 * field. A PDU may or may not include an information field.
 *
 * response. In data communications, a reply represented in the control field
 * of a response PDU. It advises the addressed destination LLC of the action
 * taken by the source LLC to one or more command PDUs.
 *
 * response PDU. All PDUs send by an LLC in which the C/R bit is equal to "1".
 */

extern int SAMsend(mac_t *, dlsap_t *, int, mblk_t *, int, void *);

/*
 * LOCAL FUNCTION PROTOTYPES
 */
/* LLC2 clock (1/10 second ticks) */
int llc2DontTakeFromAvailList = 0;
int llc2FreeAvailListFlag = 0;

/* array of LLC2 station component control blocks */
extern llc2Sta_t *llc2StaArray[];
extern llc2Timer_t *llc2TimerArray[];

/* Q head for connection component structures in ADM state to be reused */
dLnkLst_t llc2ConAvailHead = {&llc2ConAvailHead, &llc2ConAvailHead};
/* Q head for SAP component structures to be reused */
dLnkLst_t llc2SapAvailHead = {&llc2SapAvailHead, &llc2SapAvailHead};

int llc2_first_ppa = 0;
timeout_id_t llc2TimerId = 0;
int llc2_first_time = 1;
int llc2_run_timer = 0;

/*
 * The MAC/MAC EXTENSION Structures and pointers
 */
extern mac_t *ild_macp[];	/* MAC structure array */
extern macx_t *ild_macxp[];	/* MAC Extension structure array */
extern macs_t *macs[];
extern llc2FreeList_t llc2FreeList;

extern int	ild_max_ppa;
extern uint_t	ild_q_hiwat;
extern uint_t	ild_q_lowat;
extern queue_t	*llc2TimerQ;

/*
 * Lock declarations
 */

/*
 * muoe930??? 01/29/93 buzz
 * must lock the timerQ to avoid race condition when shutting down.
 */
#ifdef _LP64
ILD_DECL_LOCK(Con_Avail_Lock) = {0, 0, 0, 0, 0};
ILD_DECL_LOCK(Sap_Avail_Lock) = {0, 0, 0, 0, 0};
ILD_DECL_LOCK(ild_llc2timerq_lock) = {0, 0, 0, 0, 0};
#else
ILD_DECL_LOCK(Con_Avail_Lock) = {{0, 0}, 0, 0, 0, 0};
ILD_DECL_LOCK(Sap_Avail_Lock) = {{0, 0}, 0, 0, 0, 0};
ILD_DECL_LOCK(ild_llc2timerq_lock) = {{0, 0}, 0, 0, 0, 0};
#endif

#define	ADDR_ALIGN(x)	(((uintptr_t)(x) + sizeof (uintptr_t) - 1) &	\
				~(sizeof (uintptr_t) - 1))

/*
 *
 * LLC2 START OF DAY PROCESSING
 *
 */

/*
 *	llc2_init
 *
 *
 * LLC2 Initialization Processing for a MAC
 *
 * description:
 *	Set fields in the MAC structure which pertain to LLC2 processing.
 *	Initialize to the down state the station component which corresponds
 *	to the adapter being initialized.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac	MAC structure ptr
 *
 * returns:
 *	nothing
 */
void
llc2_init(mac_t *mac)
{

	mac->mac_service_modes |= (DL_CLDLS | DL_CODLS);
	mac->mac_conn_mgmt = (link_t *)0;
	llc2StaArray[mac->mac_ppa]->state = STA_DOWN;
	llc2TimerArray[mac->mac_ppa]->state = STA_DOWN;
	ILD_RWALLOC_LOCK(&(llc2StaArray[mac->mac_ppa]->sta_lock),
				ILD_LCK4, NULL);
	ILD_RWALLOC_LOCK(&(llc2TimerArray[mac->mac_ppa]->llc2_timer_lock),
				ILD_LCK4, NULL);
	if (llc2_first_time) {
		llc2_first_time = 0;
		llc2_first_ppa = mac->mac_ppa;
		ILD_RWALLOC_LOCK(&Con_Avail_Lock, ILD_LCK5, NULL);
		ILD_RWALLOC_LOCK(&Sap_Avail_Lock, ILD_LCK5, NULL);
	}
}

/*
 *
 * LLC2 CONFIGURATION MANAGEMENT AND STATISTICS REPORTING
 *
 */

/*
 *	llc2Ioctl
 *
 *
 * LLC2 IOCTL Handler
 *
 * description:
 *	All ioctl requests for LLC2 pass through this function.
 *	Based on the command type in the continuation parameter block,
 *	the appropriate function is called and passed a pointer to
 *	the parameter block. That function returns an ioc_rval. If the
 *	value is zero, the ioctl message is passed upstream as a positive
 *	acknowledgement. If non-zero, a negative acknowledgement is
 *	passed upstream containing the non-zero ioc_rval and an ioc_error
 *	value of EINVAL.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	q		STREAMS write Q ptr
 *	mp		ioctl message ptr(the appropriate LLC2
 *			structure is in the continuation message
 *			block)
 *
 * Locks:	No locks set on entry/exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
llc2Ioctl(mac_t *mac, queue_t *q, mblk_t *mp)
{
	struct iocblk *ctl = (struct iocblk *)mp->b_rptr;
	llc2_ioctl_t *ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;
	int ioc_rval = RVAL_CMD_UNKNOWN;
	int error;

	mp->b_datap->db_type = M_IOCACK;
	ctl->ioc_error = 0;
	ctl->ioc_rval = 0;

	if (ic->cmd == LLC2_INIT || ic->cmd == LLC2_UNINIT ||
		ic->cmd == LLC_SET_CON_PARMS_REQ) {
		if (secpolicy_net_config(ctl->ioc_cr, B_FALSE) != 0) {
			ctl->ioc_error = EACCES;
			mp->b_datap->db_type = M_IOCNAK;
			if (q)
				qreply(q, mp);
			else
				freemsg(mp);
			return;
		}
	}

	/*
	 * if the following test fails, then we're not supposed to be
	 * in this module, just no-op the IOCTL and return an error.
	 */
	if (!llc2_first_time) {
		switch (ic->cmd) {
		case LLC2_INIT:
			error = miocpullup(mp, sizeof (llc2Init_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;

			if (llc2StaArray[mac->mac_ppa]->sta_lock.lock_init
				!= 0) {
				ioc_rval = llc2StaInit(mac, (llc2Init_t *)ic);
				/* muoe972527 buzz 12/10/97 */
				dlpi_linkup(mac);
			} else {
				ioc_rval = RVAL_MAC_INVALID;
			}
			break;

		case LLC2_UNINIT:
			error = miocpullup(mp, sizeof (llc2Uninit_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;

			if (llc2StaArray[mac->mac_ppa]->sta_lock.lock_init
				!= 0) {
				ioc_rval =
					llc2StaUninit(mac, (llc2Uninit_t *)ic);
				/* muoe972527 buzz 12/10/97 */
				dlpi_linkdown(mac);
			} else {
				ioc_rval = RVAL_MAC_INVALID;
			}
			break;
		/*
		 * the following functions test the lock before attempting to
		 * acquire it
		 */
		case LLC2_GET_STA_STATS:
			error = miocpullup(mp, sizeof (llc2GetStaStats_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;
			ioc_rval = llc2GetStaStats((llc2GetStaStats_t *)ic);
			break;

		case LLC2_GET_SAP_STATS:
			error = miocpullup(mp, sizeof (llc2GetSapStats_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;
			ioc_rval = llc2GetSapStats((llc2GetSapStats_t *)ic);
			break;

		case LLC2_GET_CON_STATS:
			error = miocpullup(mp, sizeof (llc2GetConStats_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;
			ioc_rval = llc2GetConStats((llc2GetConStats_t *)ic);
			break;

		case LLC_GET_CON_PARMS_REQ:
		case LLC_SET_CON_PARMS_REQ:
			error = miocpullup(mp, sizeof (llc2ConParms_t));
			if (error != 0) {
				miocnak(q, mp, 0, error);
				return;
			}
			ic = (llc2_ioctl_t *)mp->b_cont->b_rptr;
			if (ic->cmd == LLC_GET_CON_PARMS_REQ) {
				ioc_rval =
				    llc2GetConParmsReq((llc2ConParms_t *)ic);
			} else {
				link_t *lnk = (link_t *)q->q_ptr;

				if (lnk) {
				ioc_rval = llc2SetConParmsReq(lnk->lk_state,
						    lnk->lk_sid,
						    (llc2ConParms_t *)ic);
				} else {
					ioc_rval = RVAL_CON_INVALID;
				}
			}
		default:
			ioc_rval = RVAL_CMD_UNKNOWN;
			break;
		}
	}
	if (ioc_rval != 0) {
		ctl->ioc_error = EINVAL;
		ctl->ioc_rval = ioc_rval;
		mp->b_datap->db_type = M_IOCNAK;
	}
	if (q)
		qreply(q, mp);
	else
		freemsg(mp);
}

int
llc2AllocFreeList()
{
	llc2FreeList_t  *llc2Free;
	llc2FreeBuf_t *llc2FreeBuf;
	mblk_t *mp;

	llc2Free = &llc2FreeList;
	ILD_WRLOCK(llc2Free->lock);

	while (llc2Free->available < LLC2_FREELIST_MAX) {
		if ((llc2FreeBuf = kmem_zalloc(sizeof (llc2FreeBuf_t),
						KM_NOSLEEP)) != NULL) {
			if ((mp = allocb(LLC2_FREEBUF_SIZE, BPRI_HI)) == NULL) {
				kmem_free(llc2FreeBuf, sizeof (llc2FreeBuf_t));
				ILD_RWUNLOCK(llc2Free->lock);
				return (1);
			}
			llc2FreeBuf->next = llc2Free->head;
			llc2FreeBuf->buf = mp;
			llc2Free->head = llc2FreeBuf;
			llc2Free->available++;
		} else {
			ILD_RWUNLOCK(llc2Free->lock);
			return (1);
		}
	}

	ILD_RWUNLOCK(llc2Free->lock);
	return (0);
}

void
llc2DelFreeList()
{
	llc2FreeList_t  *llc2Free;
	llc2FreeBuf_t *llc2FreeBuf;

	llc2Free = &llc2FreeList;
	ILD_WRLOCK(llc2Free->lock);

	while (llc2Free->available > 0) {

		ASSERT(llc2Free->head != NULL);

		llc2FreeBuf = llc2Free->head;
		llc2Free->head = llc2FreeBuf->next;
		llc2FreeBuf->next = NULL;
		llc2Free->available--;
		freeb(llc2FreeBuf->buf);
		kmem_free(llc2FreeBuf, sizeof (llc2FreeBuf_t));
	}

	ILD_RWUNLOCK(llc2Free->lock);
}

mblk_t *
llc2_allocb(size_t sz, uint_t pri)
{
	llc2FreeList_t  *llc2Free;
	llc2FreeBuf_t *llc2FreeBuf;
	mblk_t *mp = NULL;

	if ((mp = allocb(sz, pri)) != NULL) {
		return (mp);
	}

	if (sz > LLC2_FREEBUF_SIZE) {
		return (mp);
	}

	llc2Free = &llc2FreeList;
	ILD_WRLOCK(llc2Free->lock);

	if (llc2Free->available > 0) {

		ASSERT(llc2Free->head != NULL);

		llc2FreeBuf = llc2Free->head;
		llc2Free->head = llc2FreeBuf->next;
		llc2FreeBuf->next = NULL;
		llc2Free->available--;
		mp = llc2FreeBuf->buf;
		llc2FreeBuf->buf = NULL;
		kmem_free(llc2FreeBuf, sizeof (llc2FreeBuf_t));

	}

	ILD_RWUNLOCK(llc2Free->lock);
	return (mp);
}


/*
 *	llc2StaInit
 *
 *
 * LLC2 Station Component Initialization Handler
 *
 * description:
 *	This function calls the LLC2 state machine to enable a station
 *	component and passes the LLC2 station component configuration
 *	parameters in parm1. If the return value indicates success, the
 *	LLC2 clock is started if this is the first station component
 *	initialized. The clock is only run if station(s) are active.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	ic		ptr to LLC2 station component
 *			configuration parameters
 *
 * locks:		sta_lock is locked/unlocked
 *
 * returns:
 *	0			success
 *	RVAL_STA_OUTSTATE	attempting to enable a station that is up
 *	RVAL_SYS_ERR		a system error has occurred
 *	RVAL_PARM_INVALID	an invalid configuration parameter was passed
 */
int
llc2StaInit(mac_t *mac, llc2Init_t *ic)
{
	int retVal;
	int ioc_rval;
	int activeStaCount;
	int i;

	retVal = llc2StateMachine(STA_ENABLE_REQ, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					0, (uintptr_t)ic, 0);
	switch (retVal) {
	case 0:
		/*
		 * start the LLC2 timer if this is the first station active
		 */
		activeStaCount = 0;

		for (i = 0; i < ild_max_ppa; i++) {
			if (llc2TimerArray[i]) {

			if (llc2TimerArray[i]->llc2_timer_lock.lock_init) {

			ILD_WRLOCK(llc2TimerArray[i]->llc2_timer_lock);
			if (llc2TimerArray[i]->state != STA_DOWN) {
				activeStaCount++;
			}
			ILD_RWUNLOCK(llc2TimerArray[i]->llc2_timer_lock);

			}
			}
		}
		if (activeStaCount == 1) {
			llc2TimerId = timeout(llc2TimerIsr, (void *)NULL,
						LLC2_TIME_GRANULARITY);
		}

		ioc_rval = 0;
		break;
	case LLC2_ERR_OUTSTATE:
		ioc_rval = RVAL_STA_OUTSTATE;
		break;
	case LLC2_ERR_SYSERR:
		ioc_rval = RVAL_SYS_ERR;
		break;
	case LLC2_ERR_BADPARM:
		ioc_rval = RVAL_PARM_INVALID;
		break;
	default:
		ioc_rval = RVAL_SYS_ERR;
		break;
	}

	return (ioc_rval);
}


/*
 *	llc2StaUninit
 *
 *
 * LLC2 Station Component Uninitialization Handler
 *
 * description:
 *	This function calls the LLC2 state machine to disable a station
 *	component and passes parameters(currently unused) in parm1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	ic		ptr to parameters(currently unused)
 *
 * locks:		No locks set on entry/exit
 *
 * returns:
 *	0			success
 *	RVAL_STA_OUTSTATE	attempting to disable a station that is down
 *	RVAL_SYS_ERR		a system error has occurred
 *	RVAL_PARM_INVALID	an invalid parameter was passed
 */
int
llc2StaUninit(mac_t *mac, llc2Uninit_t *ic)
{
	int retVal;
	int ioc_rval;

	retVal = llc2StateMachine(STA_DISABLE_REQ, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					0, (uintptr_t)ic, 0);
	switch (retVal) {
	case 0:
		ioc_rval = 0;
		break;
	case LLC2_ERR_OUTSTATE:
		ioc_rval = RVAL_STA_OUTSTATE;
		break;
	case LLC2_ERR_SYSERR:
		ioc_rval = RVAL_SYS_ERR;
		break;
	case LLC2_ERR_BADPARM:
		ioc_rval = RVAL_PARM_INVALID;
		break;
	default:
		ioc_rval = RVAL_SYS_ERR;
		break;
	}

	return (ioc_rval);
}


/*
 *	llc2GetStaStats
 *
 *
 * Get LLC2 Station Component Statistics
 *
 * description:
 *	If the specified station component is enabled, gather statistics
 *	to be reported. If the clear flag is set, reset station component
 *	counters to zero.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	ic		ptr to the structure to hold the gathered
 *			statistics
 *
 * locks:		sta_lock is locked/unlocked
 *
 * returns:
 *	0			success
 *	RVAL_STA_OUTSTATE	attempting to gather statistics for a
 *				disabled station component
 */
int
llc2GetStaStats(llc2GetStaStats_t *ic)
{
	llc2Sta_t *llc2Sta = llc2StaArray[ic->ppa];
	uint_t	sapIndx;
	uint_t	numSaps;
	int	ioc_rval = 0;

	if (llc2Sta == NULL)
		return (RVAL_STA_OUTSTATE);

	if (llc2Sta->sta_lock.lock_init) {
		ILD_WRLOCK(llc2Sta->sta_lock);
	} else {
		return (RVAL_STA_OUTSTATE);
	}

	if (llc2Sta->state == STA_UP) {
		ic->state = llc2Sta->state;

		/*
		 * report the number of active SAP components, and list the
		 * SAP values
		 */
		for (sapIndx = 0, numSaps = 0; sapIndx < LLC2_MAX_SAPS;
			sapIndx++) {
			if (llc2Sta->sapTbl[sapIndx] != (llc2Sap_t *)0) {
				ic->saps[numSaps] = sapIndx<<1;
				numSaps++;
			}
		}
		ic->numSaps = (ushort_t)numSaps;

		ic->nullSapXidCmdRcvd = llc2Sta->nullSapXidCmdRcvd;
		ic->nullSapXidRspSent = llc2Sta->nullSapXidRspSent;
		ic->nullSapTestCmdRcvd = llc2Sta->nullSapTestCmdRcvd;
		ic->nullSapTestRspSent = llc2Sta->nullSapTestRspSent;
		ic->outOfState = llc2Sta->outOfState;
		ic->allocFail = llc2Sta->allocFail;
		ic->protocolError = llc2Sta->protocolError;

		/*
		 * if the clear flag is non-zero, clear the counters
		 */
		if (ic->clearFlag != 0) {
			llc2Sta->nullSapXidCmdRcvd = 0;
			llc2Sta->nullSapXidRspSent = 0;
			llc2Sta->nullSapTestCmdRcvd = 0;
			llc2Sta->nullSapTestRspSent = 0;
			llc2Sta->outOfState = 0;
			llc2Sta->allocFail = 0;
			llc2Sta->protocolError = 0;
		}
	} else {
		ioc_rval = RVAL_STA_OUTSTATE;
	}

	ILD_RWUNLOCK(llc2Sta->sta_lock);

	return (ioc_rval);
}


/*
 *	llc2GetSapStats
 *
 *
 * Get LLC2 SAP Component Statistics
 *
 * description:
 *	If the specified SAP component is enabled, gather statistics
 *	to be reported. If the clear flag is set, reset SAP component
 *	counters to zero.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	ic		ptr to the structure to hold the gathered
 *			statistics
 *
 * locks:		sta_lock is locked/unlocked
 *
 * returns:
 *	0			success
 *	RVAL_SAP_INVALID	attempting to gather statistics for an
 *				invalid SAP component
 *	RVAL_STA_OUTSTATE	attempting to gather statistics on a
 *				disabled station component
 */
int
llc2GetSapStats(llc2GetSapStats_t *ic)
{
	llc2Sta_t *llc2Sta = llc2StaArray[ic->ppa];
	llc2Sap_t *llc2Sap;
	uint_t		conIndx;
	uint_t		numCons = 0; /* num of connection MAC/SAPs reported */
	int		sap;
	int ioc_rval = 0;

	if (llc2Sta == NULL)
		return (RVAL_STA_OUTSTATE);

	if (llc2Sta->sta_lock.lock_init) {
		ILD_WRLOCK(llc2Sta->sta_lock);
	} else {
		return (RVAL_STA_OUTSTATE);
	}

	if (llc2Sta->state == STA_UP) {
		/*
		 * the low-order bit of the SAP value must be zero
		 */
		sap = ic->sap;
		if (!(sap & 0x01) && ((llc2Sap = llc2Sta->sapTbl[(sap)>>1])
					!= (llc2Sap_t *)0)) {
			ic->state = llc2Sap->state;
			/*
			 * report the number of active connection components,
			 * and list their index values.
			 */
			for (conIndx = 0, numCons = 0; conIndx < LLC2_MAX_CONS;
				conIndx++) {
				if (llc2Sap->conTbl[conIndx]
					!= (llc2Con_t *)0) {
					ic->cons[numCons] = (ushort_t)conIndx;
					numCons++;
				}
			}

			ic->numCons = numCons;

			ic->xidCmdSent = llc2Sap->xidCmdSent;
			ic->xidCmdRcvd = llc2Sap->xidCmdRcvd;
			ic->xidRspSent = llc2Sap->xidRspSent;
			ic->xidRspRcvd = llc2Sap->xidRspRcvd;
			ic->testCmdSent = llc2Sap->testCmdSent;
			ic->testCmdRcvd = llc2Sap->testCmdRcvd;
			ic->testRspSent = llc2Sap->testRspSent;
			ic->testRspRcvd = llc2Sap->testRspRcvd;
			ic->uiSent = llc2Sap->uiSent;
			ic->uiRcvd = llc2Sap->uiRcvd;
			ic->outOfState = llc2Sap->outOfState;
			ic->allocFail = llc2Sap->allocFail;
			ic->protocolError = llc2Sap->protocolError;

			/*
			 * if the clear flag is non-zero, clear the counters
			 */
			if (ic->clearFlag != 0) {
				llc2Sap->xidCmdSent = 0;
				llc2Sap->xidCmdRcvd = 0;
				llc2Sap->xidRspSent = 0;
				llc2Sap->xidRspRcvd = 0;
				llc2Sap->testCmdSent = 0;
				llc2Sap->testCmdRcvd = 0;
				llc2Sap->testRspSent = 0;
				llc2Sap->testRspRcvd = 0;
				llc2Sap->uiSent = 0;
				llc2Sap->uiRcvd = 0;
				llc2Sap->outOfState = 0;
				llc2Sap->allocFail = 0;
				llc2Sap->protocolError = 0;
			}
		} else {
			ioc_rval = RVAL_SAP_INVALID;
		}
	} else {
		ioc_rval = RVAL_STA_OUTSTATE;
	}

	ILD_RWUNLOCK(llc2Sta->sta_lock);

	return (ioc_rval);
}


/*
 *	llc2GetConStats
 *
 *
 * Get LLC2 Connection Component Statistics
 *
 * description:
 *	If the specified connection component is enabled, gather statistics
 *	to be reported. If the clear flag is set, reset connection component
 *	counters to zero.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	ic		ptr to the structure to hold the gathered
 *			statistics
 *
 * locks:		sta_lock is locked/unlocked
 *			con_lock is locked/unlocked
 *
 * returns:
 *	0			success
 *	RVAL_CON_INVALID	attempting to gather statistics for an
 *				invalid connection component
 *	RVAL_SAP_INVALID	attempting to gather statistics on an
 *				invalid SAP component
 *	RVAL_STA_OUTSTATE	attempting to gather statistics on a
 *				disabled station component
 */
/*ARGSUSED*/
int
llc2GetConStats(llc2GetConStats_t *ic)
{
	llc2Sta_t *llc2Sta = llc2StaArray[ic->ppa];
	llc2Sap_t *llc2Sap;
	llc2Con_t *llc2Con;
	dLnkLst_t *elm;
	int i;
	int sap = (int)ic->sap;

	int ioc_rval = 0;
	int Sta_state = 0;

	/*
	 * connection statistics timer on flag table, indexed by timer index.
	 */
	static uchar_t timerOn[T_NUM_TIMERS] = {
		T_ACK_ON,
		T_P_ON,
		T_REJ_ON,
		T_REM_BUSY_ON,
		T_INACT_ON,
		T_SEND_ACK_ON,
		T_L2_ON,
		T_RNR_ON
	};

	if (llc2Sta == NULL)
		return (RVAL_STA_OUTSTATE);

	if (llc2Sta->sta_lock.lock_init) {
		ILD_WRLOCK(llc2Sta->sta_lock);
	} else {
		return (RVAL_STA_OUTSTATE);
	}

	if ((llc2Sap = llc2Sta->sapTbl[(sap)>>1]) != NULL) {
		ILD_WRLOCK(llc2Sap->sap_lock);
		llc2Con = llc2Sap->conTbl[ic->con];
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}

	Sta_state = llc2Sta->state;
	ILD_RWUNLOCK(llc2Sta->sta_lock);

	if (Sta_state == STA_UP) {
		/*
		 * the low-order bit of the SAP value must be zero
		 */
		if (!(sap & 0x01) && (llc2Sap != NULL)) {

			if ((ic->con < LLC2_MAX_CONS) && (llc2Con != NULL)) {

				ILD_WRLOCK(llc2Con->con_lock);

				ic->stateOldest = llc2Con->stateOldest;
				ic->stateOlder = llc2Con->stateOlder;
				ic->stateOld = llc2Con->stateOld;
				ic->state = llc2Con->state;
				ic->sid = llc2Con->sid;
				/*
				 * buzz 03/29/96 Don't zap the Routing
				 * information
				 */
				bcopy((caddr_t)&llc2Con->rem,
					(caddr_t)&ic->rem,
					sizeof (dlsap_t));
				ic->flag = llc2Con->flag;
				ic->dataFlag = llc2Con->dataFlag;
				ic->k = (uchar_t)llc2Con->k;
				ic->vs = (uchar_t)llc2Con->vs;
				ic->vr = (uchar_t)llc2Con->vr;
				ic->nrRcvd = (uchar_t)llc2Con->nrRcvd;
				/*
				 * Report POLL count
				 * leave this one basically alone, don't want
				 * to add the extra field to the stats struct
				 * and then have to change the llc2stats
				 * applications as well
				 */
				ic->retryCount = (ushort_t)
					llc2Con->pollRetryCount;

				/*
				 * report the number of messages on the
				 * unacknowledged outbound message Q
				 */
				for (i = 0, elm = llc2Con->unackHead.flink;
					elm != &llc2Con->unackHead;
					i++, elm = elm->flink) {
					;
				}
				ic->numToBeAcked = i;

				/*
				 * report the number of messages on the resend
				 * message Q
				 */
				for (i = 0, elm = llc2Con->resendHead.flink;
					elm != &llc2Con->resendHead;
					i++, elm = elm->flink) {
					;
				}
				ic->numToResend = i;

				ic->macOutSave = llc2Con->macOutSave;
				ic->macOutDump = llc2Con->macOutDump;

				/*
				 * report which timers are started
				 */
				ic->timerOn = 0;
				for (i = 0; i < T_NUM_TIMERS; i++) {
					if (llc2Con->timerEntry[i].chain.flink
						!= (dLnkLst_t *)0) {
						ic->timerOn |= timerOn[i];
					}
				}

				ic->iSent = llc2Con->iSent;
				ic->iRcvd = llc2Con->iRcvd;
				ic->frmrSent = llc2Con->frmrSent;
				ic->frmrRcvd = llc2Con->frmrRcvd;
				ic->rrSent = llc2Con->rrSent;
				ic->rrRcvd = llc2Con->rrRcvd;
				ic->rnrSent = llc2Con->rnrSent;
				ic->rnrRcvd = llc2Con->rnrRcvd;
				ic->rejSent = llc2Con->rejSent;
				ic->rejRcvd = llc2Con->rejRcvd;
				ic->sabmeSent = llc2Con->sabmeSent;
				ic->sabmeRcvd = llc2Con->sabmeRcvd;
				ic->uaSent = llc2Con->uaSent;
				ic->uaRcvd = llc2Con->uaRcvd;
				ic->discSent = llc2Con->discSent;
				ic->outOfState = llc2Con->outOfState;
				ic->allocFail = llc2Con->allocFail;
				ic->protocolError = llc2Con->protocolError;
				ic->localBusy = llc2Con->localBusy;
				ic->remoteBusy = llc2Con->remoteBusy;
				ic->maxRetryFail = llc2Con->maxRetryFail;
				ic->ackTimerExp = llc2Con->ackTimerExp;
				ic->pollTimerExp = llc2Con->pollTimerExp;
				ic->rejTimerExp = llc2Con->rejTimerExp;
				ic->remBusyTimerExp = llc2Con->remBusyTimerExp;
				ic->inactTimerExp = llc2Con->inactTimerExp;
				ic->sendAckTimerExp = llc2Con->sendAckTimerExp;

				/*
				 * if the clear flag is non-zero, clear the
				 * counters.
				 */
				if (ic->clearFlag != 0) {
					llc2Con->iSent = 0;
					llc2Con->iRcvd = 0;
					llc2Con->frmrSent = 0;
					llc2Con->frmrRcvd = 0;
					llc2Con->rrSent = 0;
					llc2Con->rrRcvd = 0;
					llc2Con->rnrSent = 0;
					llc2Con->rnrRcvd = 0;
					llc2Con->rejSent = 0;
					llc2Con->rejRcvd = 0;
					llc2Con->sabmeSent = 0;
					llc2Con->sabmeRcvd = 0;
					llc2Con->uaSent = 0;
					llc2Con->uaRcvd = 0;
					llc2Con->discSent = 0;
					llc2Con->outOfState = 0;
					llc2Con->allocFail = 0;
					llc2Con->protocolError = 0;
					llc2Con->localBusy = 0;
					llc2Con->remoteBusy = 0;
					llc2Con->maxRetryFail = 0;
					llc2Con->ackTimerExp = 0;
					llc2Con->pollTimerExp = 0;
					llc2Con->rejTimerExp = 0;
					llc2Con->remBusyTimerExp = 0;
					llc2Con->inactTimerExp = 0;
					llc2Con->sendAckTimerExp = 0;
				}
				ILD_RWUNLOCK(llc2Con->con_lock);
			} else {
				ioc_rval = RVAL_CON_INVALID;
			}
		} else {
			ioc_rval = RVAL_SAP_INVALID;
		}
	} else {
		ioc_rval = RVAL_STA_OUTSTATE;
	}

	return (ioc_rval);
}

/*
 *	llc2GetConParmsReq
 *
 *	LLC2 Get Connection Parameters Request Handler
 *
 * description:
 *	using passed buffer containing sap/station info, retrieve
 *	the connection parameters for this sap/station combination.
 *
 * parameters:
 *	ic			llc2 Connection Parameters IOCTL structure
 * returns:
 *	0			success
 *	non-zero value		no connection or invalid SAP or Station
 */
int
llc2GetConParmsReq(llc2ConParms_t *ic)
{
	llc2Sta_t		*llc2Sta = llc2StaArray[ic->ppa];
	llc2Sap_t		*llc2Sap = NULL;
	llc2Con_t		*llc2Con = NULL;
	llc2TimerEntry_t	*entry;
	int			sap = (int)ic->sap;
	int			ioc_rval = 0;
	llc_con_parms_t	*ack = (llc_con_parms_t *)&ic->parms;


	if (llc2Sta == NULL)
		return (RVAL_STA_OUTSTATE);

	if (llc2Sta->sta_lock.lock_init) {
		ILD_WRLOCK(llc2Sta->sta_lock);
	} else {
		return (RVAL_STA_OUTSTATE);
	}

	if (llc2Sta->state == STA_UP) {
		/*
		 * the low-order bit of the SAP value must be zero
		 */
		if (!(sap & 0x01) && ((llc2Sap = llc2Sta->sapTbl[(sap)>>1])
					!= (llc2Sap_t *)0)) {

			if ((ic->con < LLC2_MAX_CONS) &&
				((llc2Con = llc2Sap->conTbl[ic->con]) !=
				(llc2Con_t *)0)) {

				ILD_WRLOCK(llc2Con->con_lock);

				/* insert connection parameters */

				entry = llc2Con->timerEntry;
				ack->llc_ack_timer =
					entry[T_ACK_INDEX].timerInt;
				ack->llc_poll_timer =
					entry[T_P_INDEX].timerInt;
				ack->llc_rej_timer =
					entry[T_REJ_INDEX].timerInt;
				ack->llc_busy_timer =
					entry[T_REM_BUSY_INDEX].timerInt;
				ack->llc_inac_timer =
					entry[T_INACT_INDEX].timerInt;
				ack->llc_ackdelay_timer =
					entry[T_SEND_ACK_INDEX].timerInt;
				ack->llc_l2_timer =
					entry[T_L2_INDEX].timerInt;
				ack->llc_rnr_limit_timer =
					entry[T_RNR_INDEX].timerInt;

				ack->llc_ackdelay_max	= llc2Con->allow;
				ack->llc_maxretry	= llc2Con->N2;
				ack->llc_l2_maxretry	= llc2Con->N2L2;
				ack->llc_xmitwindow	= llc2Con->kMax;
				ack->llc_flag		= llc2Con->flag;

			} else {
				ioc_rval = RVAL_CON_INVALID;
			}
		} else {
			ioc_rval = RVAL_SAP_INVALID;
		}
	} else {
		ioc_rval = RVAL_STA_OUTSTATE;
	}

	ILD_RWUNLOCK(llc2Sta->sta_lock);

	return (ioc_rval);
}

/*
 *	llc2SetConParmsReq
 *
 *	LLC2 Set Connection Parameters Request Handler
 *
 * description:
 *	Set the connection parameters(timers and retry counts) for this
 *	link_sid using the passed llc2ConParms_t parameters.
 *
 *	parameters:
 *	link_state		DLPI link state
 *	link_sid		SAP/Connection ID pair
 *	ic			Connection parameters structure ptr
 *
 * returns:
 *	0			success
 *	non-zero value		no connection or incorrect state
 *				or invalid SAP or Station
 */
int
llc2SetConParmsReq(ushort_t link_state, ushort_t link_sid, llc2ConParms_t *ic)
{
	llc2Sta_t		*llc2Sta = llc2StaArray[ic->ppa];
	llc2Sap_t		*llc2Sap = NULL;
	llc2Con_t		*llc2Con = NULL;
	llc_con_parms_t	*parms = (llc_con_parms_t *)&ic->parms;
	llc2TimerEntry_t	*entry;
	int			ioc_rval = 0;
	uchar_t			sap;
	uchar_t			conid;


	if (llc2Sta == NULL)
		return (RVAL_STA_OUTSTATE);

	if ((link_state == DL_OUTCON_PENDING) ||
		(link_state == DL_DATAXFER) ||
		(link_state == DL_USER_RESET_PENDING) ||
		(link_state == DL_PROV_RESET_PENDING)) {

		if (llc2Sta->sta_lock.lock_init) {
			ILD_WRLOCK(llc2Sta->sta_lock);
		} else {
			return (RVAL_STA_OUTSTATE);
		}

		if (llc2Sta->state == STA_UP) {
			/*
			 * the low-order bit of the SAP value must be zero
			 */
			sap = (uchar_t)(link_sid >> 8);
			conid = link_sid & CON_CID_MASK;
			if (!(sap & 0x01) &&
				((llc2Sap = llc2Sta->sapTbl[(sap)>>1]) !=
				(llc2Sap_t *)NULL)) {

				if ((llc2Con = llc2Sap->conTbl[conid]) !=
				    NULL) {

					ILD_WRLOCK(llc2Con->con_lock);

					/* insert connection parameters */
					entry = llc2Con->timerEntry;
					entry[T_ACK_INDEX].timerInt =
						(ushort_t)parms->llc_ack_timer;
					entry[T_P_INDEX].timerInt =
						(ushort_t)parms->llc_poll_timer;
					entry[T_REJ_INDEX].timerInt =
						(ushort_t)parms->llc_rej_timer;
					entry[T_REM_BUSY_INDEX].timerInt =
						(ushort_t)parms->llc_busy_timer;
					entry[T_INACT_INDEX].timerInt =
						(ushort_t)parms->llc_inac_timer;
					entry[T_SEND_ACK_INDEX].timerInt =
					(ushort_t)parms->llc_ackdelay_timer;
					entry[T_L2_INDEX].timerInt =
						(ushort_t)parms->llc_l2_timer;
					entry[T_RNR_INDEX].timerInt =
					(ushort_t)parms->llc_rnr_limit_timer;

					llc2Con->allow =
						parms->llc_ackdelay_max;
					llc2Con->N2 = parms->llc_maxretry;
					llc2Con->N2L2 = parms->llc_l2_maxretry;
					llc2Con->k = parms->llc_xmitwindow;
					llc2Con->kMax = parms->llc_xmitwindow;
					ILD_RWUNLOCK(llc2Con->con_lock);
				} else {
					ioc_rval = RVAL_CON_INVALID;
				}
			} else {
				ioc_rval = RVAL_SAP_INVALID;
			}
		} else {
			ioc_rval = RVAL_STA_OUTSTATE;
		}
		ILD_RWUNLOCK(llc2Sta->sta_lock);
	} else {
		ioc_rval = RVAL_STA_OUTSTATE;
	}
	return (ioc_rval);
}

#define	L_SETTUNE	('L'<<8 | 3)
#define	L_GETTUNE	('L'<<8 | 4)
/*
 *	llc2SetTuneParmsReq
 *
 *	LLC2 Set Connection Parameters Request Handler for SUN X.25
 *
 * description:
 *	To maintain backward compatibility with older LLC2 (shipped with X.25)
 *	we need to implement L_SETTUNE/L_GETTUNE ioctl..
 *
 *	parameters:
 *	mac			the mac structure
 *	q			queue
 *	mp			message block
 *
 * returns:
 *	0			success
 *	non-zero value		failure
 */
/*ARGSUSED*/
int
llc2SetTuneParmsReq(mac_t *mac, queue_t *q, mblk_t *mp)
{
	struct llc2_tnioc	*pp;
	llc2Sta_t		*llc2Sta = llc2StaArray[mac->mac_ppa];
	int			ioc_rval = 0;
	llc2tune_t		*llc2tune = NULL;
	llc2Init_t		llc2Init;
	struct iocblk		*iocb = (struct iocblk *)mp->b_rptr;
	llc2Timer_t		*llc2Timer;

	pp = (struct llc2_tnioc *)mp->b_cont->b_rptr;
	llc2tune = &pp->llc2_tune;

	if (iocb->ioc_cmd == L_SETTUNE) {
		llc2Init.ppa = pp->lli_ppa;
		llc2Init.cmd = LLC2_INIT;
		llc2Init.ackTimerInt = llc2tune->T1;
		llc2Init.pollTimerInt = llc2tune->Tpf;
		llc2Init.rejTimerInt = llc2tune->Trej;
		llc2Init.remBusyTimerInt = llc2tune->Tbusy;
		llc2Init.inactTimerInt = llc2tune->Tidle;
		llc2Init.maxRetry = llc2tune->N2;
		llc2Init.xmitWindowSz = llc2tune->tx_window;
		llc2Init.rcvWindowSz = llc2tune->xid_window;
		llc2Init.timeinterval = 0;
		llc2Init.rspTimerInt = llc2tune->ack_delay;
		llc2Init.loopback = LOOPBACK_BCAST;

		ioc_rval = downEnableReq(llc2Sta, 0, mac, mp, 0, 0,
					(uintptr_t)&llc2Init, 0);
	} else {
		if (llc2Sta == NULL)
			return (EINVAL);

		bzero((void *)llc2tune, sizeof (llc2tune_t));

		ILD_WRLOCK(llc2Sta->sta_lock);
		llc2Timer = llc2TimerArray[llc2Sta->mac->mac_ppa];
		ILD_WRLOCK(llc2Timer->llc2_timer_lock);

		llc2tune->T1 = llc2Timer->timerInt[T_ACK_INDEX];
		llc2tune->Tpf = llc2Timer->timerInt[T_P_INDEX];
		llc2tune->Trej = llc2Timer->timerInt[T_REJ_INDEX];
		llc2tune->Tbusy = llc2Timer->timerInt[T_REM_BUSY_INDEX];
		llc2tune->Tidle = llc2Timer->timerInt[T_INACT_INDEX];
		llc2tune->ack_delay = llc2Timer->timerInt[T_SEND_ACK_INDEX];
		llc2tune->N2 = llc2Sta->maxRetry;
		llc2tune->tx_window = llc2Sta->xmitWindowSz;
		llc2tune->xid_window = llc2Sta->rcvWindowSz;
	}
	return (ioc_rval);
}




/*
 *
 * LLC2 UPPER INTERFACE FUNCTIONS
 *
 */

/*
 *	llc2BindReq
 *
 *
 * DLPI Bind Request Handler
 *
 * description:
 *	If the bind request is for a DIX Ethernet SAP, return a bind
 *	confirm immediately. Otherwise, call the LLC2 state machine to
 *	enable the LLC2 SAP. Issue a bind confirm on success and report
 *	the error on failure. For now, a bind on a group SAP will be
 *	rejected. The maximum and minimum frame sizes for the link are
 *	set if the bind is successful.
 *
 *	The mode parameter will not be saved for now, since DLPI does not
 *	notify LLC2 of a mode change caused by an unbind. Mode decisions
 *	will be left to DLPI. Thus, IEEE XID requests responded to by LLC2
 *	will always include an LLC type indicator of Type 1 and Type 2 (see
 *	IEEE 802.2 section 5.4.1.2.1).
 *
 * execution state:
 *	service level
 *
 * locks:	link->lk_lock is locked on entry/exit
 *
 * parameters:
 *	link		link structure ptr
 *	sap		LLC2 SAP value or DIX Ethernet type
 *	mode		bind mode (DL_CLDLS, DL_CL_ETHER, DL_CODLS)
 *
 * Locks:		link->lk_lock is locked on entry/exit
 *
 * returns:
 *	0			success
 *	non-zero value		failure to enable the LLC2 SAP
 *
 */
int
llc2BindReq(link_t *link, uint_t sap, uint_t mode)
{
	mac_t *mac = link->lk_mac;
	dlsap_t loc;
	int retVal = LLC2_GOOD_STATUS;

	bzero((caddr_t)&loc, sizeof (dlsap_t));
	if (sap > MAXSAPVALUE) {
		link->lk_min_sdu = 1;
		link->lk_max_sdu = mac->mac_max_sdu;
		link->lk_max_idu = mac->mac_max_idu;
		CPY_MAC(mac->mac_bia, loc.ether.dl_nodeaddr);
		loc.ether.dl_type = (ushort_t)sap;
		ILD_RWUNLOCK(link->lk_lock);
		/* unlock for call for BIND confirm */
		dlpi_bind_con(mac, &loc, 0, 1);
		ILD_WRLOCK(link->lk_lock);
	} else {
		/*
		 * do not allow a group SAP to be specified
		 */
		if ((sap & 0x01) == 0) {
			CPY_MAC(mac->mac_bia, loc.llc.dl_nodeaddr);
			loc.llc.dl_sap = (uchar_t)sap;

			ILD_RWUNLOCK(link->lk_lock);
			retVal = llc2StateMachine(SAP_ACTIVATION_REQ, mac,
						(mblk_t *)0,
						&loc, (dlsap_t *)0,
						((uint_t)sap)<<8, 0, 0);
			ILD_WRLOCK(link->lk_lock);

			if (retVal == 0) {
				link->lk_min_sdu = 1;

				if (sap == SNAPSAP) {
					/* MR muoe930320 buzz 02/03/93 */
					link->lk_max_sdu = (mac->mac_max_sdu -
								(LLC_HDR_SIZE +
								SNAP_HDR_SIZE));
					link->lk_max_idu = (mac->mac_max_idu -
								(LLC_HDR_SIZE +
								SNAP_HDR_SIZE));
				} else {
					/* just a regular 802 SAP */

					/*
					 * if a bind which includes a
					 * specification for connection-
					 * oriented traffic is being done,
					 * set the maximum data size to one
					 * less than normal since the LLC2
					 * header for an I-frame is one byte
					 * longer than for a UI frame.
					 */
					if (mode & DL_CODLS) {
						link->lk_max_sdu =
							(mac->mac_max_sdu -
							(LLC_HDR_SIZE+1));
						link->lk_max_idu =
							(mac->mac_max_idu -
							(LLC_HDR_SIZE+1));
					} else {
						link->lk_max_sdu =
							(mac->mac_max_sdu -
							LLC_HDR_SIZE);
						link->lk_max_idu =
							(mac->mac_max_idu -
							LLC_HDR_SIZE);
					}
				}

				ILD_RWUNLOCK(link->lk_lock);
				dlpi_bind_con(mac, &loc, 0, 0);
				ILD_WRLOCK(link->lk_lock);
			}
		} else {
			retVal = DL_BADADDR;
			ild_trace(MID_LLC2, __LINE__, 0, sap, 0);
		}
	}

	return (retVal);
}


/*
 *	llc2UnbindReq
 *
 *
 * DLPI Unbind Request Handler
 *
 * description:
 *	If the unbind request is for a DIX Ethernet SAP, return an unbind
 *	confirm immediately. Otherwise, call the LLC2 state machine to
 *	disable the LLC2 SAP. Issue an unbind confirm on success and report
 *	the error on failure.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	sap		LLC2 SAP value or DIX Ethernet type
 *
 *
 * locks:		no locks on entry/exit
 *
 * returns:
 *	0		success
 *	non-zero value	failure to disable the LLC2 SAP
 */
int
llc2UnbindReq(mac_t *mac, uint_t sap)
{
	int retVal = LLC2_GOOD_STATUS;

	if (sap > MAXSAPVALUE) {
		dlpi_unbind_con(mac, sap);
	} else {
		retVal = llc2StateMachine(SAP_DEACTIVATION_REQ, mac,
						(mblk_t *)0,
						(dlsap_t *)0, (dlsap_t *)0,
						((uint_t)sap)<<8, 0, 0);
		if (retVal == 0) {
			dlpi_unbind_con(mac, sap);
		}
	}

	return (retVal);
}


/*
 *	llc2UnitdataReq
 *
 *
 * DLPI Connectionless Unitdata Request Handler
 *
 * description:
 *	DIX Ethernet requests are sent to the MAC layer immediately after
 *	appending the unitdata message to a header message used by the
 *	MAC layer.
 *
 *	For LLC2 requests, the header message additionally includes
 *	the LLC2 information for a UI, and SNAP information if the
 *	unitdata is destined for the SNAP SAP. The LLC2 state machine
 *	is called to handle an LLC2 request.
 *
 *	If the header message cannot be allocated, the unitdata message
 *	is freed and the error is traced.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	link		DLPI link structure ptr
 *	rem		remote node address structure ptr
 *	loc		local node address structure ptr
 *	mp		unitdata message ptr (DLPI has ensured this
 * is not NULL)
 *	snap			SNAP header structure ptr
 *	opt_enet		LINK OPTIONS/DIX Ethernet indicator
 *	dlLength		remote node address length (Possible,
 *				routing information included)
 *
 * locks:		link is locked on entry/exit
 *			mac locked/unlocked
 *
 *
 * returns:
 *	0		always returns 0 since DLPI does nothing
 *			if the message cannot be processed
 */
/*ARGSUSED*/
int
llc2UnitdataReq(mac_t *mac, dlsap_t *rem, dlsap_t *loc, mblk_t *mp,
    snaphdr_t *snap, int opt_enet, uint_t dlLength)
{
	llc2HdrU_t *llc2HdrU;
	snaphdr_t *snaphdr;
	mblk_t *hdrmp;
	int len;
	int retVal = LLC2_GOOD_STATUS;
	short		link_opt;
	short		enet;

	enet = opt_enet & 0xffff;
	link_opt = ((opt_enet >> 16) & 0xffff);

	if (enet) {
		if (((uintptr_t)mp->b_rptr - (uintptr_t)mp->b_datap->db_base) >
			mac->mac_hdr_sz) {
			hdrmp = mp;
		} else if ((hdrmp = allocb(mac->mac_hdr_sz, BPRI_MED)) !=
				(mblk_t *)0) {
			hdrmp->b_cont = mp;
			hdrmp->b_rptr += mac->mac_hdr_sz;
			hdrmp->b_wptr = hdrmp->b_rptr;

		} else {
			freemsg(mp);
#ifdef LOG_ALLOC_FAIL
			allocFail(SAP_UNITDATA_REQ, mac, 0,
					((uint_t)loc->llc.dl_sap));
#endif
			ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
			return (0);
		}
		len = -1;
		if (loc->ether.dl_type == NOVELLSAP) {
			if (mac->mac_novell == NOVELL_TYPE) {
				rem->ether.dl_type = NOVELLSAP;
			} else {
				/*
				 * use length provided by novell
				 */
				/* MR muoe930245 buzz 01/26/93 */
				len = rem->ether.dl_type;
			}
		} else {
			/* muoe960282 buzz 02/05/96 SD RFC */
			ild_trace(MID_LLC2, __LINE__,
					(uint_t)enet,
					(uint_t)rem->ether.dl_type, 0);
			/*
			 * if option _NOT_ on, use dl_type field that
			 * was bound via DL_BIND
			 */
			if ((link_opt & LKOPT_REM_ADDR_OK) == 0)
				rem->ether.dl_type = loc->ether.dl_type;
		}
		/*
		 * This function will lock the mac, etc as necessary.
		 */
		(void) SAMsend(mac, rem, len, hdrmp, 0, (void *)0);
	} else {
		int mac_hdr_len;	/* buzz 03/29/96 */

		/* Initial header length */

		mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t) +
			SNAP_HDR_SIZE;


		if (((uintptr_t)mp->b_rptr - (uintptr_t)mp->b_datap->db_base) >
			mac_hdr_len) {
			hdrmp = mp;
			if (rem->llc.dl_sap == SNAPSAP) {
				hdrmp->b_rptr -= SNAP_HDR_SIZE;
				snaphdr = (snaphdr_t *)hdrmp->b_rptr;
				/*
				 * the fast way to transfer the data
				 */
				*((uint_t *)snaphdr) = *((uint_t *)snap);
				snaphdr->type_l = snap->type_l;
			}
			hdrmp->b_rptr -= sizeof (llc2HdrU_t);
			llc2HdrU = (llc2HdrU_t *)hdrmp->b_rptr;
			llc2HdrU->dsap = rem->llc.dl_sap;
			llc2HdrU->ssap = loc->llc.dl_sap;
			llc2HdrU->control = UI;
		} else if ((hdrmp = allocb(mac_hdr_len, BPRI_MED)) !=
				(mblk_t *)0) {

			hdrmp->b_cont = mp;
			hdrmp->b_rptr += mac->mac_hdr_sz;

			/*
			 * build LLC header
			 */
			llc2HdrU = (llc2HdrU_t *)hdrmp->b_rptr;
			hdrmp->b_wptr = hdrmp->b_rptr + sizeof (llc2HdrU_t);
			llc2HdrU->dsap = rem->llc.dl_sap;
			llc2HdrU->ssap = loc->llc.dl_sap;
			llc2HdrU->control = UI;
			if (llc2HdrU->dsap == SNAPSAP) {
				snaphdr = (snaphdr_t *)hdrmp->b_wptr;
				hdrmp->b_wptr += SNAP_HDR_SIZE;
				/*
				 * the slow, lint way to transfer the data
				 *
				 * snaphdr->oui0 = snap->oui0;
				 * snaphdr->oui1 = snap->oui1;
				 * snaphdr->oui2 = snap->oui2;
				 * snaphdr->type_h = snap->type_h;
				 * snaphdr->type_l = snap->type_l;
				 *
				 * the fast way to transfer the data
				 */
				*((uint_t *)snaphdr) = *((uint_t *)snap);
				snaphdr->type_l = snap->type_l;
			}
		} else {
			freemsg(mp);
			/*
			 * increment the allocFail counter in the appropriate
			 * SAP component control block
			 */
#ifdef LOG_ALLOC_FAIL
			allocFail(SAP_UNITDATA_REQ, mac, 0,
					((uint_t)loc->llc.dl_sap));
#endif
			ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
			return (0);
		}

		retVal = llc2StateMachine(SAP_UNITDATA_REQ, mac, hdrmp, loc,
					rem, ((uint_t)loc->llc.dl_sap)<<8,
					0, 0);
		/*
		 * if the return value is non-zero, the LLC2 state machine
		 * couldn't handle the unitdata message, and it must be freed
		 * here
		 */
		if (retVal != 0) {
			freemsg(hdrmp);
		}
	}

	return (0);
}


/*
 *	llc2TestReq
 *
 *
 * DLPI Test Request Handler
 *
 * description:
 *	A message containing the LLC header information for a test request
 *	is built, and the test data message is appended if provided.
 *	The LLC2 state machine is then called to send the request.
 *
 *	If the header message cannot be allocated, the test data message
 *	is freed and the error is traced.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	lnk		DLPI link structure ptr
 *	rem		remote node address structure ptr
 *	loc		local node address structure ptr
 *	mp		test data message ptr (may be NULL)
 *	dlLength	remote node address length (Possible,
 *			routing information included)
 *
 * locks:		link is locked on entry/exit
 *
 * returns:
 *	0		always returns 0 since DLPI does nothing
 *			if the message cannot be processed
 */
/*ARGSUSED*/
int
llc2TestReq(mac_t *mac, dlsap_t *rem, dlsap_t *loc, mblk_t *mp, uint_t dlLength)
{
	llc2HdrU_t *llc2HdrU;
	mblk_t *hdrmp;
	int retVal = LLC2_GOOD_STATUS;
	int mac_hdr_len;	/* buzz 03/29/96 */


	/* Initial header length */

	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);


	if ((hdrmp = allocb(mac_hdr_len, BPRI_MED)) != (mblk_t *)0) {
		hdrmp->b_cont = mp;
		hdrmp->b_rptr += mac->mac_hdr_sz;

		llc2HdrU = (llc2HdrU_t *)hdrmp->b_rptr;
		hdrmp->b_wptr = hdrmp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU->dsap = rem->llc.dl_sap;
		llc2HdrU->ssap = loc->llc.dl_sap;
		/*
		 * always send a test request with the poll bit set
		 */
		llc2HdrU->control = TEST | P_F;

		retVal =
			llc2StateMachine(SAP_TEST_REQ, mac, hdrmp,
						loc, rem,
						((uint_t)loc->llc.dl_sap)<<8,
						0, 0);
		/*
		 * if the return value is non-zero, the LLC2 state machine
		 * couldn't handle the test request message, and it must be
		 * freed here
		 */
		if (retVal != 0) {
			freemsg(hdrmp);
		}
	} else {
		if (mp != (mblk_t *)0) {
			freemsg(mp);
		}
		/*
		 * increment the allocFail counter in the appropriate SAP
		 * component control block
		 */
#ifdef LOG_ALLOC_FAIL
		allocFail(SAP_TEST_REQ, mac, 0, ((uint_t)loc->llc.dl_sap));
#endif
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}

	return (0);
}


/*
 *	llc2XidReq
 *
 *
 * DLPI XID Request/Response Handler
 *
 * description:
 *	A message containing the LLC header information for an XID
 *	request/response is built, and the XID data message is appended
 *	if provided. The LLC2 state machine is then called to send the
 *	request/response.
 *
 *	If the header message cannot be allocated, the XID data message
 *	is freed and the error is traced.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	lnk		DLPI link structure ptr
 *	rem		remote node address structure ptr
 *	loc		local node address structure ptr
 *	rsp		response/request indicator
 *	pf		poll/final indicator
 *	mp		XID data message ptr (may be NULL)
 *	dlLength	remote node address length (Possible,
 *			routing information included)
 *
 * locks:		lnk locked on entry/exit
 *
 * returns:
 *	0		always returns 0 since DLPI does nothing
 *			if the message cannot be processed
 */
/*ARGSUSED*/
int
llc2XidReq(mac_t *mac, dlsap_t *rem, dlsap_t *loc, uint_t rsp, uint_t pf,
    mblk_t *mp, uint_t dlLength)
{
	llc2HdrU_t *llc2HdrU;
	mblk_t *hdrmp;
	int retVal = LLC2_GOOD_STATUS;
	int mac_hdr_len;	/* buzz 03/29/96 */


	/* Initial header length */

	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);

	if ((hdrmp = allocb(mac_hdr_len, BPRI_MED)) != (mblk_t *)0) {
		hdrmp->b_cont = mp;
		hdrmp->b_rptr += mac->mac_hdr_sz;
		llc2HdrU = (llc2HdrU_t *)hdrmp->b_rptr;
		hdrmp->b_wptr = hdrmp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU->dsap = rem->llc.dl_sap;
		llc2HdrU->ssap = loc->llc.dl_sap;
		if (rsp != 0) {
			llc2HdrU->ssap |= LLC_RESPONSE;
		}
		llc2HdrU->control = (pf ? (XID | P_F) : XID);

		/*
		 * if this is an IEEE XID request, the LLC type and receive
		 * window size are filled in after accessing the component
		 * control blocks during LLC2 state machine processing
		 */
		retVal =
			llc2StateMachine(SAP_XID_REQ, mac, hdrmp, loc, rem,
					((uint_t)loc->llc.dl_sap)<<8, 0, 0);
		/*
		 * if the return value is non-zero, the LLC2 state machine
		 * couldn't handle the XID request/response message, and it must
		 * be freed here
		 */
		if (retVal != 0) {
			freemsg(hdrmp);
		}
	} else {
		if (mp != (mblk_t *)0) {
			freemsg(mp);
		}
		/*
		 * increment the allocFail counter in the appropriate SAP
		 * component control block
		 */
#ifdef LOG_ALLOC_FAIL
		allocFail(SAP_XID_REQ, mac, 0, ((uint_t)loc->llc.dl_sap));
#endif
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}

	return (0);
}


/*
 *	llc2ConnectReq
 *
 *
 * DLPI Connect Request Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the connect request,
 *	and a pointer to the link structure is passed in parm1.
 *	However, the connect request is rejected immediately by this
 *	function if the SAP given for the remote node has the group bit
 *	set.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	link		link structure ptr
 *	rem		remote node address structure ptr
 *	loc		local node address structure ptr
 *
 * returns:
 *	0		success
 *	non-zero value	failure to set-up a connection
 */
/*ARGSUSED*/
int
llc2ConnectReq(mac_t *mac, link_t *link, dlsap_t *rem, dlsap_t *loc)
{
	int retVal = LLC2_GOOD_STATUS;

	/*
	 * do not allow a group SAP to be specified as the remote SAP
	 */
	if ((rem->llc.dl_sap & 0x01) == 0) {
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
		ILD_RWUNLOCK(link->lk_lock);
		retVal = llc2StateMachine(CON_CONNECT_REQ, mac, (mblk_t *)0,
						loc, rem,
						((uint_t)loc->llc.dl_sap) << 8,
						(uintptr_t)link, 0);
		ILD_WRLOCK(link->lk_lock);
	} else {
		retVal = DL_BADADDR;
		ild_trace(MID_LLC2, __LINE__, 0, loc->llc.dl_sap, 0);
	}

	return (retVal);
}


/*
 *	llc2ConnectRes
 *
 *
 * DLPI Connect Response Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the connect response,
 *	and a pointer to the link structure is passed in parm1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	link		link structure ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	sid		SAP value and connection index
 *
 *
 * locks:		No locks set on entry/exit
 *
 * returns:
 *	0		success
 *	non-zero value	failure to complete connection processing
 */
/*ARGSUSED*/
int
llc2ConnectRes(mac_t *mac, link_t *link, dlsap_t *loc, dlsap_t *rem,
    ushort_t sid)
{
	int retVal;

	retVal = llc2StateMachine(CON_CONNECT_RES, mac, (mblk_t *)0,
					loc, rem, (uint_t)sid,
					(uintptr_t)link, 0);
	return (retVal);
}


/*
 *	llc2ResetReq
 *
 *
 * DLPI Reset Request Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the reset request.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	sid		SAP value and connection index
 *
 *
 * locks:		no locks set on entry/exit
 *
 * returns:
 *	0		success
 *	non-zero value	failure to initiate reset processing
 */
/*ARGSUSED*/
int
llc2ResetReq(mac_t *mac, ushort_t sid)
{
	int retVal;

	retVal = llc2StateMachine(CON_RESET_REQ, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					(uint_t)sid, 0, 0);
	return (retVal);
}


/*
 *	llc2ResetRes
 *
 *
 * DLPI Reset Response Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the reset response.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	sid		SAP value and connection index
 *
 *
 * locks:		no locks set on entry/exit
 *
 * returns:
 *	0		success
 *	non-zero value	failure to complete reset processing
 */
/*ARGSUSED*/
int
llc2ResetRes(mac_t *mac, ushort_t sid)
{
	int retVal;

	retVal = llc2StateMachine(CON_RESET_RES, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					(uint_t)sid, 0, 0);
	return (retVal);
}


/*
 *	llc2DisconnectReq
 *
 *
 * DLPI Disconnect Request Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the disconnect request.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	sid		SAP value and connection index
 *
 *
 * locks:		no locks set on entry/exit
 *
 * returns:
 *	0		success
 *	non-zero value	failure to initiate disconnect processing
 */
/*ARGSUSED*/
int
llc2DisconnectReq(mac_t *mac, ushort_t sid)
{
	int retVal;

	retVal = llc2StateMachine(CON_DISCONNECT_REQ, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					(uint_t)sid, 0, 0);
	return (retVal);
}


/*
 *	llc2DataReq
 *
 *
 * DLPI Connection-Oriented Data Request Handler
 *
 * description:
 *	Allocate the LLC2 I-frame header and append the data message,
 *	then call the LLC2 state machine to transmit the message over
 *	a connection.
 *
 *	If the message cannot be transmitted for any reason, the data
 *	message originally passed by DLPI is left intact and a non-zero
 *	return code is given to DLPI. DLPI will then re-queue the data
 *	message on its write Q and disable the Q until LLC2 indicates
 *	that the request may be retried.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	lnk		DLPI link structure ptr (for unlocking)
 *	mac		MAC structure ptr
 *	sid		SAP value and connection index
 *	mp		data message ptr
 *	rem		remote node address structure ptr
 *
 *
 * Locks:		no locks set on entry/exit
 *
 *
 * returns:
 *	0		success
 *	non-zero value	data could not be transmitted
 */
/*ARGSUSED*/
int
llc2DataReq(mac_t *mac, uint_t sid, mblk_t *mp, dlsap_t *rem)
{
	llc2HdrI_t *llc2HdrI;
	mblk_t *hdrmp;
	int retVal = LLC2_GOOD_STATUS;
	int mac_hdr_len;	/* buzz 03/29/96 */
	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrI_t);

	if ((hdrmp = allocb(mac_hdr_len, BPRI_MED)) != (mblk_t *)0) {
		hdrmp->b_cont = mp;
		/*
		 * Skip over header and any (possible) Source Routing
		 * Information
		 */
		hdrmp->b_rptr += mac->mac_hdr_sz;

		llc2HdrI = (llc2HdrI_t *)hdrmp->b_rptr;
		hdrmp->b_wptr = hdrmp->b_rptr + sizeof (llc2HdrI_t);
		/*
		 * the command/response bit, poll/final bit, N (S) value, and
		 * N (R) value are set during LLC2 state machine processing
		 * of this data request before transmission
		 */
		llc2HdrI->dsap = rem->llc.dl_sap;
		llc2HdrI->ssap = (sid>>8) & 0xfe;
		llc2HdrI->ns = 0;
		llc2HdrI->nr = 0;

		retVal = llc2StateMachine(CON_DATA_REQ, mac, hdrmp,
						(dlsap_t *)0, rem,
						(uint_t)sid, 0, 0);
		/*
		 * if the LLC2 state machine could not transmit the data
		 * request, strip the LLC2 header message but leave the data
		 * message intact so that DLPI can re-queue it for subsequent
		 * processing.
		 */
		if (retVal != 0) {
			hdrmp->b_cont = (mblk_t *)0;
			freeb(hdrmp);
		}
	} else {
		/*
		 * If the LLC2 header couldn't be allocated, set a non-zero
		 * return value so DLPI will re-queue the data message for
		 * subsequent processing. Increment the allocFail counter
		 * in the appropriate connection component control block.
		 */
#ifdef LOG_ALLOC_FAIL
		allocFail(CON_DATA_REQ, mac, (uint_t)sid, rem->llc.dl_sap);
#endif
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
		retVal = LLC2_ERR_SYSERR;
	}

	return (retVal);
}


/*
 *	llc2XonReq
 *
 *
 * Local Busy Clear Handler
 *
 * description:
 *	The LLC2 state machine is called to handle the local busy clear
 *	indication from DLPI for inbound data flow on a connection.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	sid		SAP value and connection index
 *
 * locks:		no locks set on entry/exit
 *
 *
 * returns:
 *	0		success
 *	non-zero value	failure to process the local busy clear
 *			indication
 */
/*ARGSUSED*/
int
llc2XonReq(mac_t *mac, ushort_t sid)
{
	int retVal;

	retVal = llc2StateMachine(CON_LOCAL_BUSY_CLR, mac, (mblk_t *)0,
					(dlsap_t *)0, (dlsap_t *)0,
					(uint_t)sid, 0, 0);
	return (retVal);
}


/*
 *
 * LLC2 LOWER INTERFACE FUNCTIONS
 *
 */

/*
 *	llc2RcvIntLvl
 *
 *
 * Interrupt Level LLC2 Frame Receiver
 *
 * description:
 *	If the LLC2 read Q is enabled, store the passed parameters, and
 *	an indicator that the message is a received frame, in the area
 *	reserved in the message buffer. Place the message on the
 *	read Q so that subsequent frame processing is done at service
 *	level. Since routing information is not handled, it is not
 *	copied.
 *
 *	If the LLC2 read Q is not enabled, the message is freed and a
 *	trace entry is made.
 *
 * execution state:
 *	interrupt level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	mp		frame message ptr
 *	pri		priority indicator
 *	enet		DIX Ethernet indicator
 *
 * Locks:	mac is locked on entry/exit
 *		w_ptr is locked on entry/exit
 * returns:
 *		nothing
 */
/*ARGSUSED*/
void
llc2RcvIntLvl(mac_t *mac, mblk_t *mp)
{
	macx_t		*macx;
	queue_t 	*llc2ReadQ;

	macx = ild_macxp[mac->mac_ppa];
	llc2ReadQ = RD(macx->lowerQ);

	if (llc2ReadQ == (queue_t *)0) {
		freemsg(mp);
		ild_trace(MID_LLC2, __LINE__, 0, 0, 1);
		return;
	}

	/*
	 * We should be holding the llc2FreeList.lock before we
	 * make this check. But that would create contention for
	 * this lock and the worst that can happen here is we
	 * end up calling llc2AllocFreeList() when there was no
	 * need. Lets optimize for the best case when system is
	 * not running low on memory and check llc2FreeList.available
	 * without grabbing any lock.
	 */
	if (llc2FreeList.available < LLC2_FREELIST_MAX) {
		/*
		 * module has used up memory from emergency list.
		 * try to get it back. If we have dipped below
		 * MIN_FREE, start dropping frames. The remote side
		 * will retransmit them.
		 */
		if (llc2AllocFreeList() != 0) {
			ILD_WRLOCK(llc2FreeList.lock);
			if (llc2FreeList.available < LLC2_FREELIST_MIN) {
				ILD_RWUNLOCK(llc2FreeList.lock);
				freemsg(mp);
				return;
			} else {
				ILD_RWUNLOCK(llc2FreeList.lock);
			}
		}
	}

	if (canput(llc2ReadQ)) {
		if (mp->b_cont != NULL) {
			(void) putq(llc2ReadQ, mp);
		} else {
			freemsg(mp);
			mac->mGenStat.inUnknownProtos++;
		}
	} else {
		freemsg(mp);
		mac->mGenStat.inDiscards++;
	}
}


/*
 *	llc2ReadSrv
 *
 *
 * LLC2 STREAMS Device Read Service Routine
 *
 * description:
 *	Each queued message is extracted and a determination is made
 *	whether the message is an inbound frame or a returned outbound
 *	frame. Based on this determination, the associated parameters
 *	are extracted from the message and the appropriate service level
 *	routine is called.
 *
 *	If the message type cannot be identified, the message is freed
 *	and a trace entry is made.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	q		LLC2 STREAMS device read Q pointer
 *
 * returns:
 *	0		always
 */
int
llc2ReadSrv(queue_t *q)
{
	mblk_t		*mp;
	mblk_t		*dp;
	mac_t		*mac;
	dlsap_t		*loc, *rem;
	dlsap_t		saveLoc, saveRem;
	dl_unitdata_ind_t *dl;

	if (llc2_run_timer) {
		(void) llc2TimerSrv();
	}

	mac = (mac_t *)q->q_ptr;

	while ((dp = getq(q)) != (mblk_t *)0) {
		if (dp->b_cont != NULL) {
			mp = dp->b_cont;
			dl = (dl_unitdata_ind_t *)dp->b_rptr;
			loc = (dlsap_t *)((uintptr_t)(dp->b_rptr) +
						dl->dl_dest_addr_offset);
			rem = (dlsap_t *)((uintptr_t)(dp->b_rptr) +
						dl->dl_src_addr_offset);
			bcopy((caddr_t)loc, (caddr_t)&saveLoc,
				sizeof (dlsap_t));
			bcopy((caddr_t)rem, (caddr_t)&saveRem,
				sizeof (dlsap_t));
			dp->b_cont = NULL;
			freemsg(dp);
			llc2RcvSrvLvl(mac, &saveLoc, &saveRem, mp);
		} else {
			freemsg(dp);
			ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
		}
	}

	return (0);
}

/*
 *	llc2RcvSrvLvl
 *
 *
 * Service Level LLC2 Frame Receiver
 *
 * description:
 *	This function determines the type of the received frame and passes
 *	it on to the appropriate handler.
 *
 *	DIX Ethernet and NOVELL frames are passed directly to DLPI as
 *	unitdata indications.
 *
 *	LLC2 frames are analyzed for LLC type, command/response setting,
 *	and poll/final setting. Having made these determinations, the LLC2
 *	state machine is called to handle the received frame event.
 *
 *	WARNINGS:
 *	LLC2 processing assumes that the MAC layer has included all
 *	necessary LLC2 header information in the first message block.
 *	The LLC2 header structures are in llc2.h.
 *
 *	For LLC2 frame processing, the value of the poll/final setting
 *	is not always determined if the incoming frame generates an
 *	xxx_RCV_BAD_PDU event.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	mp		frame message ptr
 *	pri		priority indicator
 *	enet		DIX Ethernet indicator
 *
 * locks:	No locks set - dlpi_unitdata will lock the correct link.
 *		llc2StateMachine will lock whatever it needs to.
 *
 * returns:
 *		nothing
 */
/*ARGSUSED*/
void
llc2RcvSrvLvl(mac_t *mac, dlsap_t *loc, dlsap_t *rem, mblk_t *mp)
{
	dlsap_t saveLoc;
	dlsap_t saveRem;
	llc2Hdr_t *llc2Hdr;
	int mpSize;
	uint_t sid;
	uint_t pf;
	uchar_t event;
	int retVal = LLC2_GOOD_STATUS;
	/* muoe971422 buzz 6/11/97 */
	int	max_sdu = mac->mac_max_sdu;

	/*
	 * copies of the local and remote node structures are made to eliminate
	 * the possibility of overwriting an existing structure or using a
	 * structure contained within a STREAMS message that is released before
	 * processing which requires the structure is completed
	 */
	CPY_MAC(loc->ether.dl_nodeaddr, saveLoc.ether.dl_nodeaddr);
	saveLoc.ether.dl_type = loc->ether.dl_type;
	CPY_MAC(rem->ether.dl_nodeaddr, saveRem.ether.dl_nodeaddr);
	saveRem.ether.dl_type = rem->ether.dl_type;

	/*
	 * ensure the message block contains sufficient data to make
	 * the following checks
	 */
	mpSize = mp->b_wptr - mp->b_rptr;
	if (mpSize < 2) {

		/*
		 * There is not enough header information to determine what
		 * type of frame is being processed. Therefore, the LLC2
		 * station component will act as the garbage collector.
		 */
		event = STA_RCV_BAD_PDU;
		retVal = llc2StateMachine(event, mac, mp,
						&saveLoc, &saveRem,
						0, FALSE, 0);
		/*
		 * A non-zero return value indicates that LLC2 state
		 * machine processing could not handle the message.
		 * Therefore, the message must be freed here.
		 */
		if (retVal != 0) {
			freemsg(mp);
		}

		return;
	}

	/* mpSize >= 2. Sufficient data here */

	llc2Hdr = (llc2Hdr_t *)mp->b_rptr;

	if ((llc2Hdr->ssap == 0xff) && (llc2Hdr->dsap == 0xff)) {
		/* NOVELL */
		if (mac->mac_novell == NOVELL_TYPE) {
			/*
			 * supposed to use type and did not
			 */
			freemsg(mp);
			ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
		} else {
			/*
			 * supposed to use length and did
			 */
			saveLoc.ether.dl_type = NOVELLSAP;
			dlpi_unitdata_ind(mac, &saveLoc,
						&saveRem,
						mp, 1, 0);
		}
		return;
	}

	if ((llc2Hdr->dsap & 0x01) != 0) {
		/*
		 * A group SAP has been specified as the destination SAP.
		 * This is currently not supported.
		 */
		event = STA_RCV_BAD_PDU;
		retVal = llc2StateMachine(event, mac, mp,
						&saveLoc, &saveRem,
						0, FALSE, 0);
		/*
		 * A non-zero return value indicates that LLC2 state
		 * machine processing could not handle the message.
		 * Therefore, the message must be freed here.
		 */
		if (retVal != 0) {
			freemsg(mp);
		}

		return;
	}


	/*
	 * PROCESS AN LLC2 FRAME
	 *
	 * set the local and remote SAP values in the
	 * respective node address structures and
	 * indicate that no routing information is
	 * being passed.
	 */

	/*
	 * Copy the _whole_ thing, addresses, saps
	 * and routing
	 */
	bcopy((caddr_t)loc, (caddr_t)&saveLoc,
		sizeof (dlsap_t));
	bcopy((caddr_t)rem, (caddr_t)&saveRem,
		sizeof (dlsap_t));
	saveLoc.llc.dl_sap = llc2Hdr->dsap;
	saveRem.llc.dl_sap = (llc2Hdr->ssap) &
		~LLC_RESPONSE;

	/*
	 * Only the SAP portion of the sid can be
	 * determined at this time. LLC2 state
	 * machine processing will determine the
	 * connection index, if appropriate, after
	 * accessing the LLC2 component control
	 * block structures.
	 */
	sid = ((uint_t)llc2Hdr->dsap)<<8;

	pf = FALSE;
	/*
	 * ensure the message block contains
	 * sufficient data
	 * to make the following LLC2 header checks
	 */
	if (mpSize >= sizeof (llc2Hdr_t)) {
		/* I-FORMAT PDU CHECK */
		if ((llc2Hdr->control & 0x01) == 0) {
			if ((mpSize >= sizeof (llc2HdrI_t)) &&
			    /* muoe971422 buzz 6/11/97 */
				(mpSize <= max_sdu)) {
				event = (((llc2Hdr->ssap)
						& LLC_RESPONSE) ?
						CON_RCV_I_RSP :
						CON_RCV_I_CMD);
				pf = ((((llc2HdrI_t *)llc2Hdr)->nr & SI_P_F)
					? TRUE : FALSE);
			} else {
				event = CON_RCV_BAD_PDU;
			}
		}
		/* S-FORMAT PDU CHECK */
		else if ((llc2Hdr->control & 0x03) == 0x01) {
			if (mpSize >= sizeof (llc2HdrS_t)) {
				if (llc2Hdr->control == RR) {
					/* RR */
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE)?
							CON_RCV_RR_RSP :
							CON_RCV_RR_CMD);
					pf = ((((llc2HdrS_t *)llc2Hdr)->nr
						& SI_P_F) ? TRUE : FALSE);
				} else if (llc2Hdr->control == RNR) {
					/* RNR */
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE)?
							CON_RCV_RNR_RSP :
							CON_RCV_RNR_CMD);
					pf = ((((llc2HdrS_t *)llc2Hdr)->nr
						& SI_P_F) ? TRUE : FALSE);
				} else if (llc2Hdr->control == REJ) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE)?
							CON_RCV_REJ_RSP :
							CON_RCV_REJ_CMD);
					pf = ((((llc2HdrS_t *)llc2Hdr)->nr
						& SI_P_F) ?
						TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else {
				event = CON_RCV_BAD_PDU;
			}
		}
		/* U-FORMAT PDU CHECK */
		else if ((llc2Hdr->control & 0x03) == 0x03) {
			if ((llc2Hdr->control & ~P_F) == UI) {
				/* UI */
				pf = ((((llc2HdrU_t *)llc2Hdr)->control
					& P_F) ? TRUE : FALSE);
				if (((mpSize > sizeof (llc2HdrU_t)) &&
					/* muoe971422 buzz 6/11/97 */
					(mpSize <= max_sdu)) &&
					(pf == FALSE)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE)?
							SAP_RCV_BAD_PDU :
							SAP_RCV_UI);
				} else {
					event = SAP_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) ==
					SABME) {
				/* SABME */
				if (mpSize >= sizeof (llc2HdrU_t)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE) ?
							CON_RCV_BAD_PDU :
							CON_RCV_SABME_CMD);
					pf = ((((llc2HdrU_t *)llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == UA) {
				/* UA */
				if (mpSize >= sizeof (llc2HdrU_t)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE) ?
							CON_RCV_UA_RSP :
							CON_RCV_BAD_PDU);
					pf = ((((llc2HdrU_t *)llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == DISC) {
				/* DISC */
				if (mpSize >= sizeof (llc2HdrU_t)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE) ?
							CON_RCV_BAD_PDU :
							CON_RCV_DISC_CMD);
					pf = ((((llc2HdrU_t *)llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == DM) {
				/* DM */
				if (mpSize >= sizeof (llc2HdrU_t)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE) ?
							CON_RCV_DM_RSP :
							CON_RCV_BAD_PDU);
					pf = ((((llc2HdrU_t *)llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == FRMR) {
				/* FRMR */
				if (mpSize >= sizeof (llc2HdrFrmr_t)) {
					event = (((llc2Hdr->ssap)
							& LLC_RESPONSE) ?
							CON_RCV_FRMR_RSP :
							CON_RCV_BAD_PDU);
					pf = ((((llc2HdrU_t *)llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = CON_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == XID) {
				/* XID */
				if ((llc2Hdr->dsap == 0) &&
					!(llc2Hdr->ssap & LLC_RESPONSE)) {
					/* NULL DSAP */
					if (mpSize >= sizeof (llc2HdrU_t)) {
						event =
						STA_RCV_NULL_DSAP_XID_CMD;
						pf = ((((llc2HdrU_t *)
							llc2Hdr)->control
							& P_F) ? TRUE : FALSE);
					} else {
						event = STA_RCV_BAD_PDU;
					}
				} else if (mpSize >= sizeof (llc2HdrU_t)) {
					/* NON-NULL DSAP */
					event = (((llc2Hdr->ssap) &
							LLC_RESPONSE) ?
							SAP_RCV_XID_RSP :
							SAP_RCV_XID_CMD);
					pf = ((((llc2HdrU_t *)
						llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = SAP_RCV_BAD_PDU;
				}
			} else if ((llc2Hdr->control & ~P_F) == TEST) {
				/* TEST */
				if ((llc2Hdr->dsap == 0) &&
					!(llc2Hdr->ssap & LLC_RESPONSE)) {
				/* NULL DSAP */
					if (mpSize >= sizeof (llc2HdrU_t)) {
						event =
						STA_RCV_NULL_DSAP_TEST_CMD;
						pf = ((((llc2HdrU_t *)
							llc2Hdr)->control
							& P_F) ? TRUE : FALSE);
					} else {
						event = STA_RCV_BAD_PDU;
					}
				} else if (mpSize >= sizeof (llc2HdrU_t)) {
				/* NON-NULL DSAP */
					event = (((llc2Hdr->ssap) &
							LLC_RESPONSE) ?
							SAP_RCV_TEST_RSP :
							SAP_RCV_TEST_CMD);
					pf = ((((llc2HdrU_t *)
						llc2Hdr)->control
						& P_F) ? TRUE : FALSE);
				} else {
					event = SAP_RCV_BAD_PDU;
				}
			} else {
				event = SAP_RCV_BAD_PDU;
			}
		} else {
			event = SAP_RCV_BAD_PDU;
		}
	} else {
		event = SAP_RCV_BAD_PDU;
	}

	retVal = llc2StateMachine(event, mac, mp,
					&saveLoc, &saveRem,
					sid, pf, 0);
	/*
	 * A non-zero return value indicates that LLC2 state
	 * machine processing could not handle the message.
	 * Therefore, the message must be freed here.
	 */
	if (retVal != 0) {
		freemsg(mp);
	}
}


/*
 *
 * LLC2 TIMER INTERFACE FUNCTIONS
 *
 */
/*
 *	llc2TimerIsr
 *
 *
 * LLC2 Timer Interrupt Handler
 *
 * description:
 *	This function is called by the kernel when the 1/10 second llc2
 *	master timer expires. The master timer is re-started, via a call
 *	to UNIX 'timeout', if at least one station component is still UP.
 *
 *	For each UP station component, if station timers are not suspended,
 *	this function increments the station's 1/10 second current time
 *	clock and, for each of the station's timers, advances the current
 *	bucket pointer. If no entries are found in the newly expired
 *	bucket and there are no old, unprocessed expired buckets, the
 *	expired bucket pointer is also advanced.
 *
 *	If any station timer is found to have a newly expired bucket entry
 *	and has no old, unprocessed expired buckets, this function schedules
 *	llc2TimerSrv () to process the entries at service level.
 *
 * execution state:
 *	interrupt level
 *
 * parameters:
 *	none
 *
 * locks sta_lock locked/unlocked
 *	ild_llc2timerq_lock		locked on entry/exit
 *
 * returns:
 *		nothing
 */

/*ARGSUSED*/
void
llc2TimerIsr(void *arg)
{
	llc2Timer_t *llc2Timer = NULL;	/* station component structure ptr */
	llc2TimerHead_t *thead = NULL;	/* timer head structure ptr */
	int i;	/* station loop index */
	int j;	/* timer loop index */
	int ScheduleTask = FALSE; /* schedule timer service task indicator */
	int RestartTimer = FALSE; /* re-start master timer indicator */

	ILD_WRLOCK(ild_llc2timerq_lock);

	if (llc2TimerId == 0 || llc2TimerQ == NULL) {
		ILD_RWUNLOCK(ild_llc2timerq_lock);
		return;
	}

	/*
	 * loop through all stations; for each station that is UP with timers
	 * not suspended, update the station clock and timer control pointers.
	 */
	for (i = 0; i < ild_max_ppa; i++) {
		llc2Timer = llc2TimerArray[i];

		if ((llc2Timer != NULL) && (llc2Timer->state == STA_UP)) {

			ILD_WRLOCK(llc2Timer->llc2_timer_lock);

			RestartTimer = TRUE;

			if (!(llc2Timer->flags & TIMERS_SUSPENDED)) {
				llc2Timer->curTime++;
				/*
				 * loop through all timers on this station; for
				 * each timer, advance bucket pointers and
				 * schedule the timer task if there is any entry
				 * in the newly expired timer bucket.
				 */
				for (j = 0; j < T_NUM_TIMERS; j++) {
					thead = &llc2Timer->timerHead[j];
					/*
					 * If old expired buckets remain to
					 * be processed, llc2TimerSrv () is
					 * already scheduled or was
					 * interrupted before processing this
					 * timer on this station.
					 */
					if ((thead->curBucket !=
						thead->expBucket) ||
						(thead->flags & BUCKET_WRAP)) {

					ADVANCE_BUCKET(thead->curBucket,
					1, &thead->bucket[LLC2_NUM_BUCKETS]);

						if (thead->curBucket ==
							thead->expBucket) {
							/*
							 * bucket wrap has
							 * occurred
							 */
							thead->flags |=
								BUCKET_WRAP;
						}
					} else {
						/*
						 * Advance the expired bucket
						 * pointer if the current
						 * bucket is empty,
						 * otherwise indicate that the
						 * timer
						 * task needs to be scheduled.
						 */
						if (thead->curBucket->flink ==
							thead->curBucket) {
							/*
							 * current bucket is
							 * empty
							 */
							ADVANCE_BUCKET(
							thead->expBucket, 1,
							&thead->bucket[
							LLC2_NUM_BUCKETS]);
						} else {
							/*
							 * current bucket has
							 * something in it
							 */
							ScheduleTask = TRUE;
						}
						ADVANCE_BUCKET(
							thead->curBucket,
							1, &thead->bucket[
							LLC2_NUM_BUCKETS]);
					}
				}
			}
			ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);

		}
	}
	/*
	 * Re-start the timer
	 */
	if (RestartTimer) {
		llc2TimerId = timeout(llc2TimerIsr, (void *)NULL,
					LLC2_TIME_GRANULARITY);
	}

	if (ScheduleTask) {
		llc2_run_timer = 1;
		qenable(llc2TimerQ);
	}

	ILD_RWUNLOCK(ild_llc2timerq_lock);
}


/*
 *	llc2TimerSrv
 *
 *
 * LLC2 Timer Q Handler
 *
 * description:
 *	This function dispatches all expired timer entries for all station
 *	timer types. For each, a timer expiration event is generated and
 *	passed to the LLC2 state machine.
 *
 *	Since the timer interrupt service routine, llc2TimerIsr (), tests
 *	and modifies station timer controls, this function must lock out
 *	interrupts when accessing or modifying them to maintain their
 *	integrity.
 *
 *	An efficiency note. Except for the inactivity timer, timer
 *	expiration is not the norm. Timers are usually cancelled before
 *	they expire, and the inactivity timer is usually a 'long' timer,
 *	so no attempt has been made to streamline timer dispatching.
 *
 *	WARNING: This routine processes all timers of a given type before
 *		going on to process timers of the next type.  Expired
 *		timers will be dispatched out of sequence when this
 *		routine gets behind. This routine gets behind when the
 *		system is so busy that one or more timer interrupts
 *		occur after this routine has been scheduled, but before
 *		this routine has completed processing all timers.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	q		LLC2 STREAMS device timer Q pointer
 *
 * locks		sta_lock locked/unlocked as necessary
 *
 * returns:
 *	0		always
 */
/*ARGSUSED*/
int
llc2TimerSrv()
{
	llc2Timer_t *llc2Timer = NULL; /* station component structure ptr */
	llc2TimerHead_t *thead = NULL; /* timer head structure ptr */
	int i; /* station loop index */
	int j; /* timer loop index */
	uint_t curTime = 0; /* station current time */
	volatile dLnkLst_t *curBucket = NULL; /* current timer bucket ptr */
	volatile dLnkLst_t *expBucket = NULL; /* expired timer bucket ptr */
	dLnkLst_t *entry = NULL; /* timer bucket entry ptr */
	uchar_t flags = 0; /* timer flags */

	/*
	 * 802.2 connection component fsm timer event code table,
	 * indexed by timer type index.
	 */
	static int event[T_NUM_TIMERS] = {
		CON_ACK_TIMER_EXP, /* T_ACK */
		CON_POLL_TIMER_EXP, /* T_P */
		CON_REJ_TIMER_EXP, /* T_REJ */
		CON_REM_BUSY_TIMER_EXP, /* T_REM_BUSY */
		CON_INITIATE_P_F_CYCLE, /* T_INACT */
		CON_SEND_ACK_TIMER_EXP, /* T_SEND_ACK */
		CON_LEVEL2_TIMER_EXP, /* T_L2 */
		CON_RNR_TIMER_EXP /* T_RNR */
	};

	llc2DontTakeFromAvailList = 1;

	ILD_WRLOCK(ild_llc2timerq_lock);
	llc2_run_timer = 0;
	ILD_RWUNLOCK(ild_llc2timerq_lock);

	/*
	 * loop through all stations, processing those that are UP.
	 */
	for (i = 0; i < ild_max_ppa; i++) {
		llc2Timer = llc2TimerArray[i];

		if (llc2Timer == NULL)
			continue;

		ILD_WRLOCK(llc2Timer->llc2_timer_lock);

		if (llc2Timer->state != STA_UP) {
			ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
			continue;
		}

		/*
		 * loop through all timers on this station.
		 */
		for (j = 0; j < T_NUM_TIMERS; j++) {
			thead = &llc2Timer->timerHead[j];
			/*
			 * With interrupts locked out, save station's
			 * current time, make copies of this timer's
			 * controls, then update timer
			 * controls to indicate all expired buckets are
			 * processed.
			 *
			 * This must be done prior to processing them
			 * so that the timer interrupt service routine,
			 * llc2TimerIsr(), will re-schedule this
			 * service routine if new timers
			 * expire before we are done.
			 */

			curTime = llc2Timer->curTime;

			curBucket = thead->curBucket;
			expBucket = thead->expBucket;
			flags = thead->flags;

			thead->expBucket = thead->curBucket;
			thead->flags &= ~BUCKET_WRAP;

			/*
			 * Advance through the expired buckets
			 * until the current
			 * time bucket is reached. Repeat if
			 * bucket wrap indicated.
			 */
			while ((expBucket != curBucket) ||
				(flags & BUCKET_WRAP)) {
				/*
				 * Make sure we do not wrap more
				 * than once
				 */
				if (expBucket == curBucket) {
					flags &= ~BUCKET_WRAP;
				}
				/*
				 * Advance through the entries in
				 * this bucket,
				 * dispatching expired entries.
				 */
				entry = expBucket->flink;
				while (entry && (entry != expBucket)) {

					if (curTime >= ((llc2TimerEntry_t *)
							entry)->expTime) {
					/*
					 * expired entry found;
					 * dispatch it
					 */
					RMV_DLNKLST(entry);
					/*
					 * Need to drop the lock since state
					 * machine will grab the locks in
					 * correct sequence.
					 */
					ILD_RWUNLOCK(
						llc2Timer->llc2_timer_lock);

					(void) llc2StateMachine(event[j],
					((llc2TimerEntry_t *)entry)->mac,
					(mblk_t *)0, (dlsap_t *)0, (dlsap_t *)0,
					((llc2TimerEntry_t *)entry)->sid, 0, 0);

					/* muoe970956 buzz 06/03/97 */
					ILD_WRLOCK(llc2Timer->llc2_timer_lock);
					entry = expBucket->flink;
					} else {
						entry = entry->flink;
					}
				}

				ADVANCE_BUCKET(expBucket, 1,
					&thead->bucket[LLC2_NUM_BUCKETS]);
			}
		}

		ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
	}

	llc2DontTakeFromAvailList = 0;
	return (0);
}




/*
 *
 * LLC2 STATION, SAP, AND CONNECTION STATE MACHINES
 *
 */

int (*llc2StaFunc[STA_STATES_NUM][STA_OTHER_EVENT])
	(llc2Sta_t *, uchar_t, mac_t *, mblk_t *, dlsap_t *, dlsap_t *,
	uintptr_t, uintptr_t) = {
	{		/* STA_DOWN */
		downEnableReq,		/* STA_ENABLE_REQ */
		downInvalidEvt,		/* STA_DISABLE_REQ */
		downInvalidEvt,		/* STA_RCV_NULL_DSAP_XID_CMD */
		downInvalidEvt,		/* STA_RCV_NULL_DSAP_TEST_CMD */
		downInvalidEvt		/* STA_RCV_BAD_PDU */
	},
	{		/* STA_UP */
		upInvalidEvt,		/* STA_ENABLE_REQ */
		upDisableReq,		/* STA_DISABLE_REQ */
		upRcvNullDsapXidCmd,	/* STA_RCV_NULL_DSAP_XID_CMD */
		upRcvNullDsapTestCmd,	/* STA_RCV_NULL_DSAP_TEST_CMD */
		upRcvBadPdu		/* STA_RCV_BAD_PDU */
	}

	};

int (*llc2SapFunc[SAP_STATES_NUM][SAP_OTHER_EVENT - STA_OTHER_EVENT])
	(llc2Sap_t *, llc2Sta_t *, uchar_t, mac_t *, mblk_t *, dlsap_t *,
	dlsap_t *, uintptr_t, uintptr_t) = {
	{		/* SAP_INACTIVE */
		inactiveActivationReq,	/* SAP_ACTIVATION_REQ */
		inactiveInvalidEvt,	/* SAP_UNITDATA_REQ */
		inactiveInvalidEvt,	/* SAP_XID_REQ */
		inactiveInvalidEvt,	/* SAP_TEST_REQ */
		inactiveInvalidEvt,	/* SAP_DEACTIVATION_REQ */
		inactiveInvalidEvt,	/* SAP_RCV_UI */
		inactiveInvalidEvt,	/* SAP_RCV_XID_CMD */
		inactiveInvalidEvt,	/* SAP_RCV_XID_RSP */
		inactiveInvalidEvt,	/* SAP_RCV_TEST_CMD */
		inactiveInvalidEvt,	/* SAP_RCV_TEST_RSP */
		inactiveInvalidEvt	/* SAP_RCV_BAD_PDU */
	},
	{		/* SAP_ACTIVE */
		activeActivationReq,	/* SAP_ACTIVATION_REQ */
		activeUnitdataReq,	/* SAP_UNITDATA_REQ */
		activeXidReq,		/* SAP_XID_REQ */
		activeTestReq,		/* SAP_TEST_REQ */
		activeDeactivationReq,	/* SAP_DEACTIVATION_REQ */
		activeRcvUi,		/* SAP_RCV_UI */
		activeRcvXidCmd,	/* SAP_RCV_XID_CMD */
		activeRcvXidRsp,	/* SAP_RCV_XID_RSP */
		activeRcvTestCmd,	/* SAP_RCV_TEST_CMD */
		activeRcvTestRsp,	/* SAP_RCV_TEST_RSP */
		activeRcvBadPdu		/* SAP_RCV_BAD_PDU */
	}

	};

int (*llc2ConFunc[CON_STATES_NUM][UNK_OTHER_EVENT - SAP_OTHER_EVENT])
	(llc2Con_t *, llc2Sap_t *, llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	dlsap_t *, dlsap_t *, uintptr_t, uintptr_t) = {
	{		/* CON_ADM */
		admConnectReq,		/* CON_CONNECT_REQ */
		admInvalidEvt,		/* CON_CONNECT_RES */
		admInvalidEvt,		/* CON_DATA_REQ */
		admInvalidEvt,		/* CON_DISCONNECT_REQ */
		admInvalidEvt,		/* CON_RESET_REQ */
		admInvalidEvt,		/* CON_RESET_RES */
		admInvalidEvt,		/* CON_LOCAL_BUSY_CLR */
		admInvalidEvt,		/* CON_RECOVER_REQ */
		admInvalidEvt,		/* CON_RCV_BAD_PDU */
		admRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		admRcvXxxRsp,		/* CON_RCV_DM_RSP */
		admRcvXxxRsp,		/* CON_RCV_FRMR_RSP */
		admRcvXxxCmd,		/* CON_RCV_I_CMD */
		admRcvXxxCmd,		/* CON_RCV_I_CMD_UNEXP_NS */
		admRcvXxxCmd,		/* CON_RCV_I_CMD_INVALID_NS */
		admRcvXxxRsp,		/* CON_RCV_I_RSP */
		admRcvXxxRsp,		/* CON_RCV_I_RSP_UNEXP_NS */
		admRcvXxxRsp,		/* CON_RCV_I_RSP_INVALID_NS */
		admRcvXxxCmd,		/* CON_RCV_REJ_CMD */
		admRcvXxxRsp,		/* CON_RCV_REJ_RSP */
		admRcvXxxCmd,		/* CON_RCV_RNR_CMD */
		admRcvXxxRsp,		/* CON_RCV_RNR_RSP */
		admRcvXxxCmd,		/* CON_RCV_RR_CMD */
		admRcvXxxRsp,		/* CON_RCV_RR_RSP */
		admRcvSabmeCmd,		/* CON_RCV_SABME_CMD */
		admRcvXxxRsp,		/* CON_RCV_UA_RSP */
		admRcvXxxCmd,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		admRcvXxxRsp,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		admInvalidEvt,		/* CON_POLL_TIMER_EXP */
		admInvalidEvt,		/* CON_ACK_TIMER_EXP */
		admInvalidEvt,		/* CON_REJ_TIMER_EXP */
		admInvalidEvt,		/* CON_REM_BUSY_TIMER_EXP */
		admInvalidEvt,		/* CON_INITIATE_P_F_CYCLE */
		admInvalidEvt,		/* CON_SEND_ACK_TIMER_EXP */
		admInvalidEvt,		/* CON_LEVEL2_TIMER_EXP */
		admInvalidEvt,		/* CON_RNR_TIMER_EXP */
		admInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_CONN */
		connInvalidEvt,		/* CON_CONNECT_REQ */
		connConnectRes,		/* CON_CONNECT_RES */
		connInvalidEvt,		/* CON_DATA_REQ */
		connDisconnectReq,	/* CON_DISCONNECT_REQ */
		connInvalidEvt,		/* CON_RESET_REQ */
		connInvalidEvt,		/* CON_RESET_RES */
		connInvalidEvt,		/* CON_LOCAL_BUSY_CLR */
		connInvalidEvt,		/* CON_RECOVER_REQ */
		connRcvBadPdu,		/* CON_RCV_BAD_PDU */
		connRcvXxxYyy,		/* CON_RCV_DISC_CMD */
		connRcvDmRsp,		/* CON_RCV_DM_RSP */
		connRcvXxxYyy,		/* CON_RCV_FRMR_RSP */
		connRcvXxxYyy,		/* CON_RCV_I_CMD */
		connRcvXxxYyy,		/* CON_RCV_I_CMD_UNEXP_NS */
		connRcvXxxYyy,		/* CON_RCV_I_CMD_INVALID_NS */
		connRcvXxxYyy,		/* CON_RCV_I_RSP */
		connRcvXxxYyy,		/* CON_RCV_I_RSP_UNEXP_NS */
		connRcvXxxYyy,		/* CON_RCV_I_RSP_INVALID_NS */
		connRcvXxxYyy,		/* CON_RCV_REJ_CMD */
		connRcvXxxYyy,		/* CON_RCV_REJ_RSP */
		connRcvXxxYyy,		/* CON_RCV_RNR_CMD */
		connRcvXxxYyy,		/* CON_RCV_RNR_RSP */
		connRcvXxxYyy,		/* CON_RCV_RR_CMD */
		connRcvXxxYyy,		/* CON_RCV_RR_RSP */
		connRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		connRcvXxxYyy,		/* CON_RCV_UA_RSP */
		connRcvXxxYyy,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		connRcvXxxYyy,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		connInvalidEvt,		/* CON_POLL_TIMER_EXP */
		connInvalidEvt,		/* CON_ACK_TIMER_EXP */
		connInvalidEvt,		/* CON_REJ_TIMER_EXP */
		connInvalidEvt,		/* CON_REM_BUSY_TIMER_EXP */
		connInvalidEvt,		/* CON_INITIATE_P_F_CYCLE */
		connInvalidEvt,		/* CON_SEND_ACK_TIMER_EXP */
		connInvalidEvt,		/* CON_LEVEL2_TIMER_EXP */
		connInvalidEvt,		/* CON_RNR_TIMER_EXP */
		connInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_RESET_WAIT */
		resetwaitInvalidEvt,	/* CON_CONNECT_REQ */
		resetwaitInvalidEvt,	/* CON_CONNECT_RES */
		resetwaitInvalidEvt,	/* CON_DATA_REQ */
		resetwaitDisconnectReq,	/* CON_DISCONNECT_REQ */
		resetwaitResetReq,	/* CON_RESET_REQ */
		resetwaitInvalidEvt,	/* CON_RESET_RES */
		resetwaitLocalBusyClr,	/* CON_LOCAL_BUSY_CLR */
		resetwaitRecoverReq,	/* CON_RECOVER_REQ */
		resetwaitRcvBadPdu,	/* CON_RCV_BAD_PDU */
		resetwaitRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		resetwaitRcvDmRsp,	/* CON_RCV_DM_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_FRMR_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_CMD */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_CMD_UNEXP_NS */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_CMD_INVALID_NS */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_RSP_UNEXP_NS */
		resetwaitRcvXxxYyy,	/* CON_RCV_I_RSP_INVALID_NS */
		resetwaitRcvXxxYyy,	/* CON_RCV_REJ_CMD */
		resetwaitRcvXxxYyy,	/* CON_RCV_REJ_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_RNR_CMD */
		resetwaitRcvXxxYyy,	/* CON_RCV_RNR_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_RR_CMD */
		resetwaitRcvXxxYyy,	/* CON_RCV_RR_RSP */
		resetwaitRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		resetwaitRcvXxxYyy,	/* CON_RCV_UA_RSP */
		resetwaitRcvXxxYyy,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		resetwaitRcvXxxYyy,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		resetwaitInvalidEvt,	/* CON_POLL_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_ACK_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_REJ_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		resetwaitSendAckTimerExp, /* CON_SEND_ACK_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		resetwaitInvalidEvt,	/* CON_RNR_TIMER_EXP */
		resetwaitInvalidEvt	/* CON_MAC_XON_IND */
	},
	{		/* CON_RESET_CHECK */
		resetcheckInvalidEvt,	/* CON_CONNECT_REQ */
		resetcheckInvalidEvt,	/* CON_CONNECT_RES */
		resetcheckInvalidEvt,	/* CON_DATA_REQ */
		resetcheckDisconnectReq, /* CON_DISCONNECT_REQ */
		resetcheckInvalidEvt,	/* CON_RESET_REQ */
		resetcheckResetRes,	/* CON_RESET_RES */
		resetcheckInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		resetcheckInvalidEvt,	/* CON_RECOVER_REQ */
		resetcheckRcvBadPdu,	/* CON_RCV_BAD_PDU */
		resetcheckRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		resetcheckRcvDmRsp,	/* CON_RCV_DM_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_FRMR_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_CMD */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_CMD_UNEXP_NS */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_CMD_INVALID_NS */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_RSP_UNEXP_NS */
		resetcheckRcvXxxYyy,	/* CON_RCV_I_RSP_INVALID_NS */
		resetcheckRcvXxxYyy,	/* CON_RCV_REJ_CMD */
		resetcheckRcvXxxYyy,	/* CON_RCV_REJ_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_RNR_CMD */
		resetcheckRcvXxxYyy,	/* CON_RCV_RNR_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_RR_CMD */
		resetcheckRcvXxxYyy,	/* CON_RCV_RR_RSP */
		resetcheckRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		resetcheckRcvXxxYyy,	/* CON_RCV_UA_RSP */
		resetcheckRcvXxxYyy,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		resetcheckRcvXxxYyy,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		resetcheckInvalidEvt,	/* CON_POLL_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_ACK_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_REJ_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		resetcheckSendAckTimerExp, /* CON_SEND_ACK_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		resetcheckInvalidEvt,	/* CON_RNR_TIMER_EXP */
		resetcheckInvalidEvt	/* CON_MAC_XON_IND */
	},
	{ 			/* CON_SETUP */
		setupInvalidEvt,	/* CON_CONNECT_REQ */
		setupInvalidEvt,	/* CON_CONNECT_RES */
		setupInvalidEvt,	/* CON_DATA_REQ */
		connDisconnectReq,	/* CON_DISCONNECT_REQ */
		setupInvalidEvt,	/* CON_RESET_REQ */
		setupInvalidEvt,	/* CON_RESET_RES */
		setupInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		setupInvalidEvt,	/* CON_RECOVER_REQ */
		setupRcvBadPdu,		/* CON_RCV_BAD_PDU */
		setupRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		setupRcvDmRsp,		/* CON_RCV_DM_RSP */
		setupRcvXxxYyy,		/* CON_RCV_FRMR_RSP */
		setupRcvXxxYyy,		/* CON_RCV_I_CMD */
		setupRcvXxxYyy,		/* CON_RCV_I_CMD_UNEXP_NS */
		setupRcvXxxYyy,		/* CON_RCV_I_CMD_INVALID_NS */
		setupRcvXxxYyy,		/* CON_RCV_I_RSP */
		setupRcvXxxYyy,		/* CON_RCV_I_RSP_UNEXP_NS */
		setupRcvXxxYyy,		/* CON_RCV_I_RSP_INVALID_NS */
		setupRcvXxxYyy,		/* CON_RCV_REJ_CMD */
		setupRcvXxxYyy,		/* CON_RCV_REJ_RSP */
		setupRcvXxxYyy,		/* CON_RCV_RNR_CMD */
		setupRcvXxxYyy,		/* CON_RCV_RNR_RSP */
		setupRcvXxxYyy,		/* CON_RCV_RR_CMD */
		setupRcvXxxYyy,		/* CON_RCV_RR_RSP */
		setupRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		setupRcvUaRsp,		/* CON_RCV_UA_RSP */
		setupRcvXxxYyy,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		setupRcvXxxYyy,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		setupInvalidEvt,	/* CON_POLL_TIMER_EXP */
		setupAckTimerExp,	/* CON_ACK_TIMER_EXP */
		setupInvalidEvt,	/* CON_REJ_TIMER_EXP */
		setupInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		setupInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		setupInvalidEvt,	/* CON_SEND_ACK_TIMER_EXP */
		setupInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		setupInvalidEvt,	/* CON_RNR_TIMER_EXP */
		setupInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_RESET */
		resetInvalidEvt,	/* CON_CONNECT_REQ */
		resetInvalidEvt,	/* CON_CONNECT_RES */
		resetInvalidEvt,	/* CON_DATA_REQ */
		resetInvalidEvt,	/* CON_DISCONNECT_REQ */
		resetInvalidEvt,	/* CON_RESET_REQ */
		resetInvalidEvt,	/* CON_RESET_RES */
		resetInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		resetInvalidEvt,	/* CON_RECOVER_REQ */
		resetRcvBadPdu,		/* CON_RCV_BAD_PDU */
		resetRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		resetRcvDmRsp,		/* CON_RCV_DM_RSP */
		resetRcvXxxYyy,		/* CON_RCV_FRMR_RSP */
		resetRcvXxxYyy,		/* CON_RCV_I_CMD */
		resetRcvXxxYyy,		/* CON_RCV_I_CMD_UNEXP_NS */
		resetRcvXxxYyy,		/* CON_RCV_I_CMD_INVALID_NS */
		resetRcvXxxYyy,		/* CON_RCV_I_RSP */
		resetRcvXxxYyy,		/* CON_RCV_I_RSP_UNEXP_NS */
		resetRcvXxxYyy,		/* CON_RCV_I_RSP_INVALID_NS */
		resetRcvXxxYyy,		/* CON_RCV_REJ_CMD */
		resetRcvXxxYyy,		/* CON_RCV_REJ_RSP */
		resetRcvXxxYyy,		/* CON_RCV_RNR_CMD */
		resetRcvXxxYyy,		/* CON_RCV_RNR_RSP */
		resetRcvXxxYyy,		/* CON_RCV_RR_CMD */
		resetRcvXxxYyy,		/* CON_RCV_RR_RSP */
		resetRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		resetRcvUaRsp,		/* CON_RCV_UA_RSP */
		resetRcvXxxYyy,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		resetRcvXxxYyy,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		resetInvalidEvt,	/* CON_POLL_TIMER_EXP */
		resetAckTimerExp,	/* CON_ACK_TIMER_EXP */
		resetInvalidEvt,	/* CON_REJ_TIMER_EXP */
		resetInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		resetInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		resetSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		resetInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		resetInvalidEvt,	/* CON_RNR_TIMER_EXP */
		resetInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_D_CONN */
		dconnInvalidEvt,	/* CON_CONNECT_REQ */
		dconnInvalidEvt,	/* CON_CONNECT_RES */
		dconnInvalidEvt,	/* CON_DATA_REQ */
		dconnInvalidEvt,	/* CON_DISCONNECT_REQ */
		dconnInvalidEvt,	/* CON_RESET_REQ */
		dconnInvalidEvt,	/* CON_RESET_RES */
		dconnInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		dconnInvalidEvt,	/* CON_RECOVER_REQ */
		dconnRcvBadPdu,		/* CON_RCV_BAD_PDU */
		dconnRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		dconnRcvDmRsp,		/* CON_RCV_DM_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_FRMR_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_I_CMD */
		dconnRcvXxxYyy,		/* CON_RCV_I_CMD_UNEXP_NS */
		dconnRcvXxxYyy,		/* CON_RCV_I_CMD_INVALID_NS */
		dconnRcvXxxYyy,		/* CON_RCV_I_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_I_RSP_UNEXP_NS */
		dconnRcvXxxYyy,		/* CON_RCV_I_RSP_INVALID_NS */
		dconnRcvXxxYyy,		/* CON_RCV_REJ_CMD */
		dconnRcvXxxYyy,		/* CON_RCV_REJ_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_RNR_CMD */
		dconnRcvXxxYyy,		/* CON_RCV_RNR_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_RR_CMD */
		dconnRcvXxxYyy,		/* CON_RCV_RR_RSP */
		dconnRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		dconnRcvUaRsp,		/* CON_RCV_UA_RSP */
		dconnRcvXxxYyy,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		dconnRcvXxxYyy,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		dconnInvalidEvt,	/* CON_POLL_TIMER_EXP */
		dconnAckTimerExp,	/* CON_ACK_TIMER_EXP */
		dconnInvalidEvt,	/* CON_REJ_TIMER_EXP */
		dconnInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		dconnInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		dconnSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		dconnInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		dconnInvalidEvt,	/* CON_RNR_TIMER_EXP */
		dconnInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_ERROR */
		errorInvalidEvt,	/* CON_CONNECT_REQ */
		errorInvalidEvt,	/* CON_CONNECT_RES */
		errorInvalidEvt,	/* CON_DATA_REQ */
		errorInvalidEvt,	/* CON_DISCONNECT_REQ */
		errorInvalidEvt,	/* CON_RESET_REQ */
		errorInvalidEvt,	/* CON_RESET_RES */
		errorInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		errorInvalidEvt,	/* CON_RECOVER_REQ */
		errorRcvBadPdu,		/* CON_RCV_BAD_PDU */
		errorRcvDiscCmd,	/* CON_RCV_DISC_CMD */
		errorRcvDmRsp,		/* CON_RCV_DM_RSP */
		errorRcvFrmrRsp,	/* CON_RCV_FRMR_RSP */
		errorRcvXxxCmd,		/* CON_RCV_I_CMD */
		errorRcvXxxCmd,		/* CON_RCV_I_CMD_UNEXP_NS */
		errorRcvXxxCmd,		/* CON_RCV_I_CMD_INVALID_NS */
		errorRcvXxxRsp,		/* CON_RCV_I_RSP */
		errorRcvXxxRsp,		/* CON_RCV_I_RSP_UNEXP_NS */
		errorRcvXxxRsp,		/* CON_RCV_I_RSP_INVALID_NS */
		errorRcvXxxCmd,		/* CON_RCV_REJ_CMD */
		errorRcvXxxRsp,		/* CON_RCV_REJ_RSP */
		errorRcvXxxCmd,		/* CON_RCV_RNR_CMD */
		errorRcvXxxRsp,		/* CON_RCV_RNR_RSP */
		errorRcvXxxCmd,		/* CON_RCV_RR_CMD */
		errorRcvXxxRsp,		/* CON_RCV_RR_RSP */
		errorRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		errorRcvXxxRsp,		/* CON_RCV_UA_RSP */
		errorRcvXxxCmd,		/* CON_RCV_ZZZ_CMD_INVALID_NR */
		errorRcvXxxRsp,		/* CON_RCV_ZZZ_RSP_INVALID_NR */
		errorInvalidEvt,	/* CON_POLL_TIMER_EXP */
		errorAckTimerExp,	/* CON_ACK_TIMER_EXP */
		errorInvalidEvt,	/* CON_REJ_TIMER_EXP */
		errorInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		errorInvalidEvt,	/* CON_INITIATE_P_F_CYCLE */
		errorSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		errorInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		errorInvalidEvt,	/* CON_RNR_TIMER_EXP */
		errorInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_NORMAL */
		normalInvalidEvt,	/* CON_CONNECT_REQ */
		normalInvalidEvt,	/* CON_CONNECT_RES */
		normalDataReq,		/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		normalInvalidEvt,	/* CON_RESET_RES */
		normalInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		normalInvalidEvt,	/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		normalRcvICmd,		/* CON_RCV_I_CMD */
		normalRcvICmdUnexpNs,	/* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		normalRcvIRsp,		/* CON_RCV_I_RSP */
		normalRcvIRspUnexpNs,	/* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		normalRcvRejCmd,	/* CON_RCV_REJ_CMD */
		normalRcvRejRsp,	/* CON_RCV_REJ_RSP */
		normalRcvRnrCmd,	/* CON_RCV_RNR_CMD */
		normalRcvRnrRsp,	/* CON_RCV_RNR_RSP */
		normalRcvRrCmd,		/* CON_RCV_RR_CMD */
		normalRcvRrRsp,		/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		normalPollTimerExp,	/* CON_POLL_TIMER_EXP */
		normalAckTimerExp,	/* CON_ACK_TIMER_EXP */
		normalInvalidEvt,	/* CON_REJ_TIMER_EXP */
		normalRemBusyTimerExp,	/* CON_REM_BUSY_TIMER_EXP */
		normalInitiatePFCycle,	/* CON_INITIATE_P_F_CYCLE */
		normalSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		normalInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		xferMacXonInd		/* CON_MAC_XON_IND */
	},
	{		/* CON_BUSY */
		busyInvalidEvt,		/* CON_CONNECT_REQ */
		busyInvalidEvt,		/* CON_CONNECT_RES */
		busyDataReq,		/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		busyInvalidEvt,		/* CON_RESET_RES */
		busyLocalBusyClr,	/* CON_LOCAL_BUSY_CLR */
		busyInvalidEvt,		/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		busyRcvICmd,		/* CON_RCV_I_CMD */
		busyRcvICmdUnexpNs,	/* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		busyRcvIRsp,		/* CON_RCV_I_RSP */
		busyRcvIRspUnexpNs,	/* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		busyRcvRejCmd,		/* CON_RCV_REJ_CMD */
		busyRcvRejRsp,		/* CON_RCV_REJ_RSP */
		busyRcvRnrCmd,		/* CON_RCV_RNR_CMD */
		busyRcvRnrRsp,		/* CON_RCV_RNR_RSP */
		busyRcvRrCmd,		/* CON_RCV_RR_CMD */
		busyRcvRrRsp,		/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		busyPollTimerExp,	/* CON_POLL_TIMER_EXP */
		busyAckTimerExp,	/* CON_ACK_TIMER_EXP */
		busyRejTimerExp,	/* CON_REJ_TIMER_EXP */
		busyRemBusyTimerExp,	/* CON_REM_BUSY_TIMER_EXP */
		busyInitiatePFCycle,	/* CON_INITIATE_P_F_CYCLE */
		busySendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		busyInvalidEvt,		/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		xferMacXonInd		/* CON_MAC_XON_IND */
	},
	{		/* CON_REJECT */
		rejectInvalidEvt,	/* CON_CONNECT_REQ */
		rejectInvalidEvt,	/* CON_CONNECT_RES */
		rejectDataReq,		/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		rejectInvalidEvt,	/* CON_RESET_RES */
		rejectInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		rejectInvalidEvt,	/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		rejectRcvICmd,		/* CON_RCV_I_CMD */
		rejectRcvICmdUnexpNs,	/* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		rejectRcvIRsp,		/* CON_RCV_I_RSP */
		rejectRcvIRspUnexpNs,	/* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		rejectRcvRejCmd,	/* CON_RCV_REJ_CMD */
		rejectRcvRejRsp,	/* CON_RCV_REJ_RSP */
		rejectRcvRnrCmd,	/* CON_RCV_RNR_CMD */
		rejectRcvRnrRsp,	/* CON_RCV_RNR_RSP */
		rejectRcvRrCmd,		/* CON_RCV_RR_CMD */
		rejectRcvRrRsp,		/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		rejectPollTimerExp,	/* CON_POLL_TIMER_EXP */
		rejectAckTimerExp,	/* CON_ACK_TIMER_EXP */
		rejectRejTimerExp,	/* CON_REJ_TIMER_EXP */
		rejectRemBusyTimerExp,	/* CON_REM_BUSY_TIMER_EXP */
		rejectInitiatePFCycle,	/* CON_INITIATE_P_F_CYCLE */
		rejectSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		rejectInvalidEvt,	/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		xferMacXonInd		/* CON_MAC_XON_IND */
	},
	{		/* CON_AWAIT */
		awaitInvalidEvt,	/* CON_CONNECT_REQ */
		awaitInvalidEvt,	/* CON_CONNECT_RES */
		awaitInvalidEvt,	/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		awaitInvalidEvt,	/* CON_RESET_RES */
		awaitInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		awaitInvalidEvt,	/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		awaitRcvICmd,		/* CON_RCV_I_CMD */
		awaitRcvICmdUnexpNs,	/* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		awaitRcvIRsp,		/* CON_RCV_I_RSP */
		awaitRcvIRspUnexpNs,	/* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		awaitRcvRejCmd,		/* CON_RCV_REJ_CMD */
		awaitRcvRejRsp,		/* CON_RCV_REJ_RSP */
		awaitRcvRnrCmd,		/* CON_RCV_RNR_CMD */
		awaitRcvRnrRsp,		/* CON_RCV_RNR_RSP */
		awaitRcvRrCmd,		/* CON_RCV_RR_CMD */
		awaitRcvRrRsp,		/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		awaitPollTimerExp,	/* CON_POLL_TIMER_EXP */
		awaitInvalidEvt,	/* CON_ACK_TIMER_EXP */
		awaitInvalidEvt,	/* CON_REJ_TIMER_EXP */
		awaitInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		awaitInitiatePFCycle,	/* CON_INITIATE_P_F_CYCLE */
		awaitSendAckTimerExp,	/* CON_SEND_ACK_TIMER_EXP */
		awaitL2TimerExp,	/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		awaitInvalidEvt		/* CON_MAC_XON_IND */
	},
	{		/* CON_AWAIT_BUSY */
		awaitbusyInvalidEvt,	/* CON_CONNECT_REQ */
		awaitbusyInvalidEvt,	/* CON_CONNECT_RES */
		awaitbusyInvalidEvt,	/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		awaitbusyInvalidEvt,	/* CON_RESET_RES */
		awaitbusyLocalBusyClr,	/* CON_LOCAL_BUSY_CLR */
		awaitbusyInvalidEvt,	/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		awaitbusyRcvICmd,	/* CON_RCV_I_CMD */
		awaitbusyRcvICmdUnexpNs, /* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		awaitbusyRcvIRsp,	/* CON_RCV_I_RSP */
		awaitbusyRcvIRspUnexpNs, /* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		awaitbusyRcvRejCmd,	/* CON_RCV_REJ_CMD */
		awaitbusyRcvRejRsp,	/* CON_RCV_REJ_RSP */
		awaitbusyRcvRnrCmd,	/* CON_RCV_RNR_CMD */
		awaitbusyRcvRnrRsp,	/* CON_RCV_RNR_RSP */
		awaitbusyRcvRrCmd,	/* CON_RCV_RR_CMD */
		awaitbusyRcvRrRsp,	/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		awaitbusyPollTimerExp,	/* CON_POLL_TIMER_EXP */
		awaitbusyInvalidEvt,	/* CON_ACK_TIMER_EXP */
		awaitbusyInvalidEvt,	/* CON_REJ_TIMER_EXP */
		awaitbusyInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		awaitbusyInitiatePFCycle, /* CON_INITIATE_P_F_CYCLE */
		awaitbusySendAckTimerExp, /* CON_SEND_ACK_TIMER_EXP */
		awaitbusyL2TimerExp,	/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		awaitbusyInvalidEvt	/* CON_MAC_XON_IND */
	},
	{		/* CON_AWAIT_REJECT */
		awaitrejectInvalidEvt,	/* CON_CONNECT_REQ */
		awaitrejectInvalidEvt,	/* CON_CONNECT_RES */
		awaitrejectInvalidEvt,	/* CON_DATA_REQ */
		xferDisconnectReq,	/* CON_DISCONNECT_REQ */
		xferResetReq,		/* CON_RESET_REQ */
		awaitrejectInvalidEvt,	/* CON_RESET_RES */
		awaitrejectInvalidEvt,	/* CON_LOCAL_BUSY_CLR */
		awaitrejectInvalidEvt,	/* CON_RECOVER_REQ */
		xferRcvBadPdu,		/* CON_RCV_BAD_PDU */
		xferRcvDiscCmd,		/* CON_RCV_DISC_CMD */
		xferRcvDmRsp,		/* CON_RCV_DM_RSP */
		xferRcvFrmrRsp,		/* CON_RCV_FRMR_RSP */
		awaitrejectRcvICmd,	/* CON_RCV_I_CMD */
		awaitrejectRcvICmdUnexpNs, /* CON_RCV_I_CMD_UNEXP_NS */
		xferRcvICmdInvalidNs,	/* CON_RCV_I_CMD_INVALID_NS */
		awaitrejectRcvIRsp,	/* CON_RCV_I_RSP */
		awaitrejectRcvIRspUnexpNs, /* CON_RCV_I_RSP_UNEXP_NS */
		xferRcvIRspInvalidNs,	/* CON_RCV_I_RSP_INVALID_NS */
		awaitrejectRcvRejCmd,	/* CON_RCV_REJ_CMD */
		awaitrejectRcvRejRsp,	/* CON_RCV_REJ_RSP */
		awaitrejectRcvRnrCmd,	/* CON_RCV_RNR_CMD */
		awaitrejectRcvRnrRsp,	/* CON_RCV_RNR_RSP */
		awaitrejectRcvRrCmd,	/* CON_RCV_RR_CMD */
		awaitrejectRcvRrRsp,	/* CON_RCV_RR_RSP */
		xferRcvSabmeCmd,	/* CON_RCV_SABME_CMD */
		xferRcvUaRsp,		/* CON_RCV_UA_RSP */
		xferRcvZzzCmdInvalidNr,	/* CON_RCV_ZZZ_CMD_INVALID_NR */
		xferRcvZzzRspInvalidNr,	/* CON_RCV_ZZZ_RSP_INVALID_NR */
		awaitrejectPollTimerExp, /* CON_POLL_TIMER_EXP */
		awaitrejectInvalidEvt,	/* CON_ACK_TIMER_EXP */
		awaitrejectInvalidEvt,	/* CON_REJ_TIMER_EXP */
		awaitrejectInvalidEvt,	/* CON_REM_BUSY_TIMER_EXP */
		awaitrejectInitiatePFCycle, /* CON_INITIATE_P_F_CYCLE */
		awaitrejectSendAckTimerExp, /* CON_SEND_ACK_TIMER_EXP */
		awaitrejectL2TimerExp,	/* CON_LEVEL2_TIMER_EXP */
		xferRnrTimerExp,	/* CON_RNR_TIMER_EXP */
		awaitrejectInvalidEvt	/* CON_MAC_XON_IND */
	}

};

/*
 *	The Sequence Numbering
 *	======================
 *
 *
 *	<----------valid range---------->
 *	|----------------|-------|------|
 *	N (R)_RECEIVED    V (S)    N (R)   V (S)_MAX
 *	oldest unacked 	current		largest sent
 *	seq num		seq num		seq num
 *
 * Sequence numbers that are are queued to be
 * resent are valid. See function updateNrRcvd.
 *
 *
 * Assuming "the last sent N(R)" = V(R)
 * (see ISO 8802-2, Section 5.4.2.3.5):
 *
 * N(S) - V(R) > = RW	== > invalid N(S)
 *	otherwise	== > unexpected N(S)
 *
 * HOWEVER, we do not want to take down the
 * connection when an 'invalid N(S)' is rcvd,
 * since it usually is an indication that the
 * other end has multiple re-try sequences
 * going due to RR Final responses from us
 * getting out of sequence. This is a normal
 * occurrence when the network contains
 * devices that introduce transmission delays
 * that are longer than the other end's Poll
 * Timer Interval can handle. We treat this
 * as an 'unexpected N(S)', instead.
 */

/*
 *	llc2StateMachine
 *
 *
 * LLC2 Station, SAP, and Connection Component State Machine Entry
 *
 * description:
 *	Based on the type of event passed in, this function determines
 *	which LLC2 component (station, SAP, or connection) state/event
 *	function should be called. llc2StateMachine () may change the event
 *	type if it determines that another event type better describes
 *	the situation. In any case, it passes the original event value to
 *	the state/event function for tracing purposes.
 *
 *	The component state machines are hierarchical in that the station
 *	component must be enabled before the subsidiary SAP event is valid,
 *	and the station and SAP components must be enabled before the
 *	subsidiary connection component event is valid.
 *
 * execution state:
 *	service level - no event may enter this function at interrupt level
 *
 * locks:	No locks set on entry/exit
 *		sta_lock	locked/unlocked
 *
 * parameters:
 *	origEvent	event indicator
 *	mac		MAC structure ptr
 *	mp		message ptr (may be NULL for some events)
 *	loc		local node address structure ptr (may
 *			contain no useful information for some events)
 *	rem		remote node address structure ptr (may
 *			contain no useful information for some events)
 *	sid		resource identifier:
 *				1) no useful information for a station
 *					component event
 *				2) SAP value or'ed with 0 for a SAP
 *					component event
 *				3) SAP value or'ed with 0 or the connection
 *					index for a connection component event
 *	parm1			multi-use parameter 1:
 *				1) station component initialization parameters
 *					structure ptr
 *				2) DLPI link structure ptr on connect request
 *					and connect response events
 *				3) poll/final indicator for received PDU events
 *	parm2			multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0			success (if a message was passed in, it has
 *				been handled and does not have to be freed)
 *	non-zero value		the event was not completely handled
 *				(if a message was passed in, it has NOT
 *				been freed)
 */

/*ARGSUSED*/
int
llc2StateMachine(uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uint_t sid, uintptr_t parm1, uintptr_t parm2)
{
	/* preserve origEvent for tracing purposes */
	uchar_t event = origEvent;

	/*
	 * the PPA value in the MAC structure is used to determine which
	 * station component is to be used
	 */
	llc2Sta_t *llc2Sta = llc2StaArray[mac->mac_ppa];
	llc2Sap_t *llc2Sap;
	llc2Con_t *llc2Con;
	uchar_t state;
	llc2HdrI_t *llc2HdrI;
	llc2HdrS_t *llc2HdrS;
	int vsMax;
	int updateNrRcvd_lock_held = 0;
	int vr;
	int nrRcvd;
	int ns;
	int nr;
	uint_t key;
	uint_t	tmp_key;
	int retVal = LLC2_GOOD_STATUS;

	ild_trace(MID_LLC2, __LINE__, (uint_t)origEvent, (uintptr_t)llc2Sta, 0);

	if (llc2Sta == NULL)
		return (LLC2_ERR_OUTSTATE);

	ILD_WRLOCK(llc2Sta->sta_lock);

	if (event > UNK_OTHER_EVENT) {
		/* UNRECOGNIZED EVENT */
		if (llc2Sta->state == STA_UP) {
			retVal = upInvalidEvt(llc2Sta, origEvent, mac, mp,
						loc, rem, parm1, parm2);
			ILD_RWUNLOCK(llc2Sta->sta_lock);
		} else {
			retVal = downInvalidEvt(llc2Sta, origEvent, mac, mp,
						loc, rem, parm1, parm2);
			ILD_RWUNLOCK(llc2Sta->sta_lock);
		}

		return (retVal);
	}

	if (event < STA_OTHER_EVENT) {
		/* STATION COMPONENT EVENT */
		state = llc2Sta->state;

		/* call the station component state/event function */
		ILD_RWUNLOCK(llc2Sta->sta_lock);
		retVal = (*llc2StaFunc[state][event])
			(llc2Sta, origEvent, mac, mp,
				loc, rem, parm1, parm2);

		return (retVal);
	}

	if (event < SAP_OTHER_EVENT) {
		/* SAP COMPONENT EVENT */
		if (llc2Sta->state == STA_UP) {
			llc2Sap = llc2Sta->sapTbl[(sid>>9) & 0x0000007f];
			if (llc2Sap != (llc2Sap_t *)0) {
				ILD_WRLOCK(llc2Sap->sap_lock);
				state = llc2Sap->state;
				ILD_RWUNLOCK(llc2Sap->sap_lock);
			} else {
				state = SAP_INACTIVE;
			}

			ILD_RWUNLOCK(llc2Sta->sta_lock);
			/* call the SAP component state/event funct. */
			retVal = (*llc2SapFunc[state][event - STA_OTHER_EVENT])
				(llc2Sap, llc2Sta, origEvent, mac, mp,
					loc, rem, parm1, parm2);
		} else {
			/*
			 * the station component is disabled
			 */
			retVal = downInvalidEvt(llc2Sta, origEvent, mac, mp,
						loc, rem, parm1, parm2);
			ILD_RWUNLOCK(llc2Sta->sta_lock);
		}

		return (retVal);
	}

	/* event < UNK_OTHER_EVENT */

	if (llc2Sta->state != STA_UP) {
		/*
		 * the station component is disabled
		 */
		retVal = downInvalidEvt(llc2Sta, origEvent, mac, mp,
					loc, rem, parm1, parm2);
		ILD_RWUNLOCK(llc2Sta->sta_lock);
		return (retVal);
	}

	/* CONNECTION COMPONENT EVENT */

	llc2Sap = llc2Sta->sapTbl[(sid>>9) & 0x0000007f];

	if ((llc2Sap == (llc2Sap_t *)0) || (llc2Sap->state != SAP_ACTIVE)) {
		/*
		 * the SAP component is disabled
		 */
		retVal = upInvalidEvt(llc2Sta, origEvent, mac, mp,
					loc, rem, parm1, parm2);
		ILD_RWUNLOCK(llc2Sta->sta_lock);
		return (retVal);
	}

	ILD_WRLOCK(llc2Sap->sap_lock);
	ILD_RWUNLOCK(llc2Sta->sta_lock);

	if ((event >= CON_RCV_BEGIN) && (event <= CON_RCV_END)) {
		/* RECEIVED PDU EVENT */

		/*
		 * We need to identify the connection component for this
		 * SAP which has a remote node equivalent to that for the
		 * received PDU.
		 *
		 * The "key" contains lower 3 bytes of remote MAC address +
		 * 1 byte of remote sap. Once we match the key, we still
		 * need to check the upper 3 bytes of remote MAC address with
		 * the stored address to be sure we found the correct
		 * llc2Con_t.
		 */
		bcopy((char *)&rem->llc.dl_nodeaddr[3],
			(char *)&tmp_key, sizeof (uint_t));
		key = (((uint_t)rem->llc.dl_sap) |
			(tmp_key & MAC_UNIQUE_MASK));

		llc2Con = (llc2Con_t *)llc2Sap->conHead.flink;
		while (llc2Con !=
			(llc2Con_t *)&llc2Sap->conHead) {
			/*
			 * The key already contains the lower 3 bytes of
			 * the remote MAC address. Just match the top 3
			 * to be sure.
			 */
			if ((key == llc2Con->key) &&
				(bcmp((char *)&rem->llc.dl_nodeaddr[0],
				(char *)&llc2Con->rem.llc.dl_nodeaddr[0], 3)
					== 0)) {
				break;
			}
			llc2Con = (llc2Con_t *)llc2Con->chain.flink;
		}
		if (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
			/*
			 * CONNECTION COMPONENT FOUND FOR
			 * RECEIVED PDU EVENT
			 */

			/*
			 * Hold the con_lock from Nr validity checks
			 * through to updating nrRcvd, to ensure state
			 * machine functions consistently in the case
			 * where our threads are preempted.
			 */
			ILD_WRLOCK(llc2Con->con_lock);

			/*
			 * Having obtained the component
			 * control blocks, get the necessary
			 * values to check N (S) and N (R)
			 * if the received PDU is an I-frame,
			 * and N (R) if it is an S-frame.
			 * Give the I-frame checks processing
			 * priority over the S-frame checks.
			 */
			state = llc2Con->state;
			vsMax = llc2Con->vsMax;
			vr = llc2Con->vr;
			nrRcvd = llc2Con->nrRcvd;
			llc2Con->cmdLastRcvd =
				((llc2Hdr_t *)(mp->b_rptr))->control;

			if (event == CON_RCV_I_CMD) {
				/*
				 * COMMAND I-FRAME N (R) AND N (S) CHECK
				 */
				llc2HdrI = (llc2HdrI_t *)
					mp->b_rptr;
				nr = (llc2HdrI->nr)>>1;
				ns = (llc2HdrI->ns)>>1;
				/*
				 * See comment on top of the function for
				 * numbering sequence Sequence numbers that
				 * are queued to be resent are valid. See
				 * function updateNrRcvd.
				 */


				/*
				 * N (R) < N (R)_RECEIVED
				 *	or		==> invalid N (R)
				 * N (R) > V (S)_MAX
				 */

				if (MOD_128_DIFF(nr, nrRcvd) >
					MOD_128_DIFF(vsMax, nrRcvd)) {
					event = CON_RCV_ZZZ_CMD_INVALID_NR;
				} else if (ns != vr) {
					/*
					 *			invalid N(S)
					 * N(S) != V(R) ==>		or
					 *			unexpected N(S)
					 */

					/*
					 * See comment on top of the function
					 * for numbering sequence
					 */
					event = CON_RCV_I_CMD_UNEXP_NS;
					updateNrRcvd_lock_held = 1;
				} else
					updateNrRcvd_lock_held = 1;
			} else if (event == CON_RCV_I_RSP) {
				/*
				 * RESPONSE I-FRAME N(R)
				 * AND N(S) CHECK
				 */
				llc2HdrI = (llc2HdrI_t *)
					mp->b_rptr;
				nr = (llc2HdrI->nr)>>1;
				ns = (llc2HdrI->ns)>>1;

				/*
				 * N(R) < N(R)_RECEIVED
				 *	OR		==> invalid N(R)
				 * N(R) > V(S)_MAX
				 */
				if (MOD_128_DIFF(nr, nrRcvd) >
					MOD_128_DIFF(vsMax, nrRcvd)) {
					event = CON_RCV_ZZZ_RSP_INVALID_NR;
				} else if (ns != vr) {
					/*
					 *			invalid N(S)
					 * N(S) != V(R) ==>		or
					 *			unexpected N(S)
					 */

					/*
					 * See comment on top of the function
					 * for numbering sequence
					 */
					event = CON_RCV_I_RSP_UNEXP_NS;
					updateNrRcvd_lock_held = 1;
				} else
					updateNrRcvd_lock_held = 1;
			} else if ((event == CON_RCV_RR_CMD) ||
					(event == CON_RCV_RNR_CMD) ||
					(event == CON_RCV_REJ_CMD)) {
				/* COMMAND S-FRAME N(R) CHECK */
				llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
				nr = (llc2HdrS->nr) >> 1;

				/*
				 * N(R) < N(R)_RECEIVED
				 *	or		==> invalid N(R)
				 * N(R) > V(S)_MAX
				 */
				if (MOD_128_DIFF(nr, nrRcvd) >
					MOD_128_DIFF(vsMax, nrRcvd)) {
					event = CON_RCV_ZZZ_CMD_INVALID_NR;
				} else
					updateNrRcvd_lock_held = 1;
			} else if ((event == CON_RCV_RR_RSP) ||
					(event == CON_RCV_RNR_RSP) ||
					(event == CON_RCV_REJ_RSP)) {
				/* RESPONSE S-FRAME N(R) CHECK */
				llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
				nr = (llc2HdrS->nr)>>1;

				/*
				 * N(R) < N(R)_RECEIVED
				 *	or		==> invalid N(R)
				 * N(R) > V(S)_MAX
				 */
				if (MOD_128_DIFF(nr, nrRcvd) >
					MOD_128_DIFF(vsMax, nrRcvd)) {
					event = CON_RCV_ZZZ_RSP_INVALID_NR;
				} else
					updateNrRcvd_lock_held = 1;
			}

			/*
			 * Routines which update nrRcvd based upon Nr of the
			 * incoming Iframe, must be called before dropping the
			 * con_lock. All such routines are associated by the
			 * llc2ConFunc[] array with states >= CON_NORMAL.
			 */
			if ((updateNrRcvd_lock_held == 1) &&
			    (state >= CON_NORMAL)) {
				/*
				 * restart T_INACT timer when we receive a
				 * PDU from the remote link station.
				 */
				startTimer(T_INACT, llc2Con, llc2Sta);

				/*
				 * call the connection component state/event
				 * function
				 */
				retVal = (*llc2ConFunc[state]
				    [event - SAP_OTHER_EVENT])
				    (llc2Con, llc2Sap, llc2Sta, origEvent,
				    mac, mp, loc, rem, parm1, parm2);
				ILD_RWUNLOCK(llc2Con->con_lock);
				ILD_RWUNLOCK(llc2Sap->sap_lock);
				return (retVal);
			}
			ILD_RWUNLOCK(llc2Con->con_lock);
		} else {
			/* NO CONNECTION COMPONENT FOR RECEIVED PDU EVENT */
			llc2Con = (llc2Con_t *)0;
			state = CON_ADM;
		}
	} else {
		/*
		 * CONNECTION COMPONENT EVENT OTHER THAN
		 * RECEIVED PDU
		 */
		/*
		 * For all connection component events other
		 * than received PDU events and the connect
		 * request event, the connection component is
		 * passed as the sid.
		 *
		 * For a connect request, the state is
		 * specified as ADM and the admConnectReq()
		 * state/event function will receive control
		 * to allocate a connection component
		 * structure.
		 */
		if (event == CON_CONNECT_REQ) {
			llc2Con = (llc2Con_t *)0;
			state = CON_ADM;
		} else {
			llc2Con =
				llc2Sap->conTbl[sid & CON_CID_MASK];
			if (llc2Con != (llc2Con_t *)0) {
				state = llc2Con->state;
			} else {
				llc2Con = (llc2Con_t *)0;
				state = CON_ADM;
			}
		}
	}

	ILD_RWUNLOCK(llc2Sap->sap_lock);

	/*
	 * sturblen muoe970955 05/01/97
	 * if the connection component exists, restart
	 * T_INACT timer when we receive a PDU from the
	 * remote link station.
	 */
	if ((llc2Con != (llc2Con_t *)0) && ((event >= CON_RCV_BEGIN) &&
						(event <= CON_RCV_END))) {
		ILD_WRLOCK(llc2Con->con_lock);
		startTimer(T_INACT, llc2Con, llc2Sta);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	/*
	 * call the connection component state/event function
	 */
	retVal = (*llc2ConFunc[state][event - SAP_OTHER_EVENT])
		(llc2Con, llc2Sap, llc2Sta, origEvent, mac, mp,
			loc, rem, parm1, parm2);

	return (retVal);

}


/*
 *
 * LLC2 STATION COMPONENT STATE/EVENT FUNCTIONS
 *
 */

/*
 *	DOWN_STATE
 *
 *
 * LLC2 Station Component Event Functions for DOWN_STATE
 *
 * description:
 *	Only the ENABLE_WITHOUT_DUPLICATE_ADDRESS_CHECK event is successful
 *	in this state. This event occurs along with initialization
 *	processing for a MAC.
 *
 *	Statistics are not collected while the station component control
 *	block is in the disabled state.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) station component initialization parameters
 *				structure ptr for an enable request
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0			success
 *	non-zero value		the event was not completely handled
 *				(if a message was passed in, it has NOT
 *				been freed)
 */

/*
 *
 * downInvalidEvt
 *
 *
 * locks			sta_lock set on entry/exit
 */
/*ARGSUSED*/
int
downInvalidEvt(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp,
    dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	llc2Sta->outOfState++;
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * downEnableReq
 *
 *
 * locks		sta_lock locked/unlocked on entry/exit
 */
/*ARGSUSED*/
int
downEnableReq(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp,
    dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	llc2Init_t *llc2Init = (llc2Init_t *)parm1;
	llc2Timer_t *llc2Timer;
	int i, j;
	int retVal = LLC2_GOOD_STATUS;
	ushort_t timeinterval = 0;
	macx_t *macx;

	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);

	ASSERT(llc2Sta != NULL);

	llc2Timer = llc2TimerArray[llc2Sta->mac->mac_ppa];
	ASSERT(llc2Timer != NULL);

	if ((llc2Init->xmitWindowSz <= 127) &&
		(llc2Init->rcvWindowSz <= 127)) {
		ILD_WRLOCK(llc2Sta->sta_lock);
		ILD_WRLOCK(llc2Timer->llc2_timer_lock);
		for (i = 0; i < T_NUM_TIMERS; i++) {
			llc2TimerHead_t *thead = &llc2Timer->timerHead[i];
			thead->flags = 0;
			thead->nu1 = 0;
			thead->nu2 = 0;
			thead->curBucket = &thead->bucket[0];
			thead->expBucket = &thead->bucket[0];

			for (j = 0; j < LLC2_NUM_BUCKETS; j++) {
				thead->bucket[j].flink = &thead->bucket[j];
				thead->bucket[j].blink = &thead->bucket[j];
			}
		}

		/*
		 * muoe963102 buzz 12/02/96
		 * Check Init structure for timer multiplier
		 * if 0 use multiplier of 1 for token ring to get timer
		 * values of 100 milliseconds in /etc/ildcf file.
		 * otherwise use 5 (500 ms).
		 */
		timeinterval = llc2Init->timeinterval;
		if ((timeinterval == 0) || (timeinterval > 10)) {
			if (mac->mac_type == DL_TPR) {
				/* 100 ms multiplier */
				timeinterval = LLC2_SHORT_TIMER;
			} else {
				/*
				 * This multiplier will yield 500 ms
				 * (1/2 second) timer ticks.
				 * Actually there is no reason that it all
				 * could not use the 100
				 * ms. value, but the llc2 code has been in
				 * operation for 4 years with 500 ms intervals,
				 * so best to leave well enough alone.
				 */
				timeinterval = LLC2_CONVERT_TIME;
			}
		}

		llc2Timer->timerInt[T_ACK_INDEX] =
			llc2Init->ackTimerInt * timeinterval;
		/* sturblen muoe972185 09/23/97 */
		llc2Timer->timerInt[T_SEND_ACK_INDEX] =
			llc2Init->rspTimerInt * timeinterval;
		llc2Timer->timerInt[T_P_INDEX] =
			llc2Init->pollTimerInt * timeinterval;
		llc2Timer->timerInt[T_REJ_INDEX] =
			llc2Init->rejTimerInt * timeinterval;
		llc2Timer->timerInt[T_REM_BUSY_INDEX] =
			llc2Init->remBusyTimerInt * timeinterval;
		llc2Timer->timerInt[T_INACT_INDEX] =
			llc2Init->inactTimerInt * timeinterval;

		/* END muoe963102 buzz 12/04/96 */

		llc2Timer->timerInt[T_L2_INDEX] = LLC2_LEVEL_2_TIMER_INT;
		llc2Timer->timerInt[T_RNR_INDEX] = LLC2_RNR_TIMER_INT;
		llc2Timer->state = STA_UP;

		llc2Sta->maxRetry = llc2Init->maxRetry;
		llc2Sta->maxRetryL2 = LLC2_LEVEL_2_MAXRETRY;
		llc2Sta->xmitWindowSz = llc2Init->xmitWindowSz;
		llc2Sta->rcvWindowSz = llc2Init->rcvWindowSz;
		llc2Sta->sendAckAllow = (uchar_t)
			((llc2Sta->rcvWindowSz * 3) / 4);
		for (i = 0; i < LLC2_MAX_SAPS; i++) {
			llc2Sta->sapTbl[i] = (llc2Sap_t *)0;
		}
		llc2Sta->nullSapXidCmdRcvd = 0;
		llc2Sta->nullSapXidRspSent = 0;
		llc2Sta->nullSapTestCmdRcvd = 0;
		llc2Sta->nullSapTestRspSent = 0;
		llc2Sta->outOfState = 0;
		llc2Sta->allocFail = 0;
		llc2Sta->protocolError = 0;
		llc2Sta->mac = mac;
		llc2Sta->state = STA_UP;
		ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
		ILD_RWUNLOCK(llc2Sta->sta_lock);
		ILD_WRLOCK(mac->mac_lock);
		macx = ild_macxp[mac->mac_ppa];
		if (llc2Init->loopback)
			macx->mac_options |= LOOPBACK_BCAST;
		else
			macx->mac_options &= ~LOOPBACK_BCAST;

		ILD_RWUNLOCK(mac->mac_lock);
	} else {
		retVal = LLC2_ERR_BADPARM;
		ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	}

	return (retVal);
}


/*
 *	UP_STATE
 *
 *
 * LLC2 Station Component Event Functions for UP_STATE
 *
 * description:
 *	Responses to NULL DSAP XID and NULL DSAP TEST requests are responded
 *	to in this state, as is the station component disable event.
 *
 *	Received PDUs which cannot be parsed successfully and cannot be
 *	associated with the SAP or connection component are recorded as
 *	station events when the station is active.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Sta		station component structure ptr
 *	origEvent	event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * upInvalidEvt
 *
 *
 * locks		sta_lock locked on entry/exit
 */
/*ARGSUSED*/
int
upInvalidEvt(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp,
    dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{

	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	llc2Sta->outOfState++;

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * upDisableReq
 *
 *
 * locks sta_lock locked/unlocked on entry/exit
 */
/*ARGSUSED*/
int
upDisableReq(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp,
    dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	int i;
	llc2Sta_t *testSta;
	llc2Con_t *llc2Con;
	llc2Sap_t *llc2Sap;
	llc2Timer_t *llc2Timer;
	uchar_t sap;

	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);

	ASSERT(llc2Sta != NULL);

	if (llc2Sta->sta_lock.lock_init) {
		ILD_WRLOCK(llc2Sta->sta_lock);
	}

	llc2Timer = llc2TimerArray[llc2Sta->mac->mac_ppa];
	ASSERT(llc2Timer != NULL);

	if (llc2Timer->llc2_timer_lock.lock_init) {
		ILD_WRLOCK(llc2Timer->llc2_timer_lock);
	}

	llc2Sta->state = STA_DOWN;
	llc2Timer->state = STA_DOWN;

	if (llc2Timer->llc2_timer_lock.lock_init) {
		ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
	}

	if (llc2Sta->sta_lock.lock_init) {
		ILD_RWUNLOCK(llc2Sta->sta_lock);
	}

	/*
	 * disable all subsidiary SAP and connection components
	 */
	for (i = 0; i < LLC2_MAX_SAPS; i++) {
		if (llc2Sta->sta_lock.lock_init) {
			ILD_WRLOCK(llc2Sta->sta_lock);
		}
		llc2Sap = (llc2Sap_t *)(llc2Sta->sapTbl[i]);
		if (llc2Sap != (llc2Sap_t *)0) {
			sap = llc2Sap->sap;
			llc2Sta->sapTbl[(sap>>1)] = (llc2Sap_t *)0;
			if (llc2Sta->sta_lock.lock_init) {
				ILD_RWUNLOCK(llc2Sta->sta_lock);
			}
			ILD_WRLOCK(llc2Sap->sap_lock);
			disableSap(llc2Sap, llc2Sta, mac);
			ILD_RWUNLOCK(llc2Sap->sap_lock);
		} else {
			if (llc2Sta->sta_lock.lock_init) {
				ILD_RWUNLOCK(llc2Sta->sta_lock);
			}

		}
	}

	for (i = 0; i < ild_max_ppa; i++) {
		testSta = llc2StaArray[i];
		if ((testSta != NULL) && (testSta->state != STA_DOWN)) {
			break;
		}
	}
	/* Don't take these things out until the LAST station is down */
	if (i == ild_max_ppa) {
		ILD_WRLOCK(Con_Avail_Lock);
		/*
		 * Free all Connection Component structures in the
		 * Available List
		 */
		while (llc2ConAvailHead.flink != &llc2ConAvailHead) {
			llc2Con = (llc2Con_t *)llc2ConAvailHead.flink;
			RMV_DLNKLST(&llc2Con->chain);
			ILD_RWDEALLOC_LOCK(&llc2Con->con_lock);
			kmem_free((void *)llc2Con, sizeof (llc2Con_t));
		}
		ILD_RWUNLOCK(Con_Avail_Lock);

		ILD_WRLOCK(Sap_Avail_Lock);
		while (llc2SapAvailHead.flink != &llc2SapAvailHead) {
			llc2Sap = (llc2Sap_t *)(llc2SapAvailHead.flink);
			RMV_DLNKLST(&llc2Sap->chain);
			ILD_RWDEALLOC_LOCK(&llc2Sap->sap_lock);
			kmem_free((void *)llc2Sap, sizeof (llc2Sap_t));
		}
		ILD_RWUNLOCK(Sap_Avail_Lock);

		if (llc2FreeAvailListFlag == 0)
			llc2FreeAvailListFlag = 1;
	}

	return (0);
}


/*
 *
 * upRcvNullDsapXidCmd
 *
 *
 * locks	sta_lock locked/unlocked
 */
/*ARGSUSED*/
int
upRcvNullDsapXidCmd(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac,
    mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	llc2HdrXid_t *llc2HdrXid;
	uchar_t saveSsap;
	int len;
	ILD_WRLOCK(llc2Sta->sta_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	llc2Sta->nullSapXidCmdRcvd++;
	llc2HdrXid = (llc2HdrXid_t *)mp->b_rptr;



	saveSsap = llc2HdrXid->ssap;
	llc2HdrXid->ssap = llc2HdrXid->dsap | LLC_RESPONSE;
	llc2HdrXid->dsap = saveSsap;

	len = msgdsize(mp);
	if ((len == sizeof (llc2HdrXid_t)) &&
		(llc2HdrXid->fmtId == LLC_XID_FMTID)) {
		llc2HdrXid->llc = LLC_SERVICES_2;
		llc2HdrXid->rw = (llc2Sta->rcvWindowSz)<<1;
	}

	ILD_RWUNLOCK(llc2Sta->sta_lock);

	if (SAMsend(mac, rem, len, mp, 0, (void *)0) == 0) {
		ILD_WRLOCK(llc2Sta->sta_lock);
		llc2Sta->nullSapXidRspSent++;
		ILD_RWUNLOCK(llc2Sta->sta_lock);
	}

	return (0);
}


/*
 *
 * upRcvNullDsapTestCmd
 *
 *
 * locks	sta_lock locked/unlocked
 */
/*ARGSUSED*/
int
upRcvNullDsapTestCmd(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac,
    mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	llc2HdrU_t *llc2HdrU;
	uchar_t saveSsap;	/* used to swap sap fields */



	ILD_WRLOCK(llc2Sta->sta_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	llc2Sta->nullSapTestCmdRcvd++;

	llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
	saveSsap = llc2HdrU->ssap;
	llc2HdrU->ssap = llc2HdrU->dsap | LLC_RESPONSE;
	llc2HdrU->dsap = saveSsap;

	ILD_RWUNLOCK(llc2Sta->sta_lock);

	if (SAMsend(mac, rem, msgdsize(mp), mp, 0, (void *)0) == 0) {
		ILD_WRLOCK(llc2Sta->sta_lock);
		llc2Sta->nullSapTestRspSent++;
		ILD_RWUNLOCK(llc2Sta->sta_lock);
	}

	return (0);
}


/*
 *
 * upRcvBadPdu
 *
 */
/*ARGSUSED*/
int
upRcvBadPdu(llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp,
    dlsap_t *loc, dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);
	ILD_WRLOCK(llc2Sta->sta_lock);
	llc2Sta->protocolError++;
	ILD_RWUNLOCK(llc2Sta->sta_lock);
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * LLC2 SAP COMPONENT STATE/EVENT FUNCTIONS
 *
 */

/*
 *	INACTIVE_STATE
 *
 *
 * LLC2 SAP Component Event Functions for INACTIVE_STATE
 *
 * description:
 *	Only the SAP_ACTIVATION_REQUEST event is successful in this state.
 *	This event occurs as a result of a bind request.
 *
 *	The SAP component control block does not exist in the disabled state.
 *	Therefore, error statistics are recorded in the station component
 *	control block.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0			success
 *	non-zero value		the event was not completely handled
 *				(if a message was passed in, it has NOT
 *				been freed)
 */

/*
 *
 * inactiveInvalidEvt
 *
 *
 * locks:			sta_lock locked/unlocked
 */
/*ARGSUSED*/
int
inactiveInvalidEvt(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, mac->mac_ppa, 0);

	llc2Sta->outOfState++;

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * inactiveActivationReq
 *
 */
/*ARGSUSED*/
int
inactiveActivationReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	uchar_t sap = loc->llc.dl_sap;
	int i;
	int retVal = LLC2_GOOD_STATUS;

	ild_trace(MID_LLC2, __LINE__, origEvent, sap, 0);

	ILD_WRLOCK(Sap_Avail_Lock);
	if (llc2SapAvailHead.flink != &llc2SapAvailHead) {
		llc2Sap = (llc2Sap_t *)llc2SapAvailHead.flink;
		RMV_DLNKLST(&llc2Sap->chain);
	} else if ((llc2Sap = kmem_zalloc(sizeof (llc2Sap_t), KM_NOSLEEP)) !=
			(llc2Sap_t *)0) {
		ILD_RWALLOC_LOCK(&llc2Sap->sap_lock, ILD_LCK4, NULL);
	} else {
		retVal = LLC2_ERR_SYSERR;
		ild_trace(MID_LLC2, __LINE__, origEvent, sap, 0);
		llc2Sta->allocFail++;
		ILD_RWUNLOCK(Sap_Avail_Lock);
		return (retVal);
	}

	ILD_WRLOCK(llc2Sap->sap_lock);
	ILD_RWUNLOCK(Sap_Avail_Lock);

	llc2Sap->state = SAP_ACTIVE;
	llc2Sap->sap = sap;
	llc2Sap->flag = 0;
	llc2Sap->nu1 = 0;

	llc2Sap->xidCmdSent = 0;
	llc2Sap->xidCmdRcvd = 0;
	llc2Sap->xidRspSent = 0;
	llc2Sap->xidRspRcvd = 0;
	llc2Sap->testCmdSent = 0;
	llc2Sap->testCmdRcvd = 0;
	llc2Sap->testRspSent = 0;
	llc2Sap->testRspRcvd = 0;
	llc2Sap->uiSent = 0;
	llc2Sap->uiRcvd = 0;

	llc2Sap->outOfState = 0;
	llc2Sap->allocFail = 0;
	llc2Sap->protocolError = 0;

	llc2Sap->busyHead.flink = &llc2Sap->busyHead;
	llc2Sap->busyHead.blink = &llc2Sap->busyHead;
	for (i = 0; i < LLC2_MAX_CONS; i++) {
		llc2Sap->conTbl[i] = (llc2Con_t *)0;
	}
	llc2Sap->conHead.flink = &llc2Sap->conHead;
	llc2Sap->conHead.blink = &llc2Sap->conHead;
	/*
	 * add back pointer to llc2Sta for locking so I can find it
	 * when I need to
	 */
	llc2Sap->station = llc2Sta;

	llc2Sta->sapTbl[(sap>>1)] = llc2Sap;
	ILD_RWUNLOCK(llc2Sap->sap_lock);

	return (retVal);
}


/*
 *	ACTIVE_STATE
 *
 *
 * LLC2 SAP Component Event Functions for ACTIVE_STATE
 *
 * description:
 *	In the active state, the SAP component handles unitdata requests,
 *	received UI frames, XID requests/responses, TEST requests/responses,
 *	and the SAP deactivation request.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the SAP component are logged as errors.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * activeActivationReq
 *
 */
/*ARGSUSED*/
int
activeActivationReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	/*
	 * the SAP is already active, so just return 0
	 */
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	return (0);
}


/*
 *
 * activeRcvUi
 *
 */
/*ARGSUSED*/
int
activeRcvUi(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	/* muoe971984 buzz 8/27/97 */
	ILD_WRLOCK(llc2Sap->sap_lock);
	llc2Sap->uiRcvd += 1;
	ILD_RWUNLOCK(llc2Sap->sap_lock);

	/*
	 * strip the LLC header off
	 */
	mp->b_rptr += sizeof (llc2HdrU_t);
	dlpi_unitdata_ind(mac, loc, rem, mp, 0, 0);

	return (0);
}


/*
 *
 * activeUnitdataReq
 *
 * locks		no locks set on entry/exit
 */
/*ARGSUSED*/
int
activeUnitdataReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	if (SAMsend(mac, rem, msgdsize(mp), mp, 0, (void *)0) == 0) {
		/* muoe971984 buzz 8/27/97 */
		ILD_WRLOCK(llc2Sap->sap_lock);
		llc2Sap->uiSent += 1;
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}
	return (0);
}


/*
 *
 * activeXidReq
 *
 */
/*ARGSUSED*/
int
activeXidReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	mblk_t *mpCont;
	uchar_t *cp;
	llc2HdrU_t *llc2HdrU;
	int rsp;


	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/*
	 * The 3 byte LLC header is in the first message block. Any additional
	 * XID information is in the continuation message block.
	 */
	if ((mpCont = mp->b_cont) != (mblk_t *)0) {
		if ((mpCont->b_wptr - mpCont->b_rptr) ==
			(sizeof (llc2HdrXid_t) - sizeof (llc2HdrU_t))) {
			cp = mpCont->b_rptr;
			if (*cp == LLC_XID_FMTID) {
				*(cp+1) = LLC_TYPE_1_2;
				*(cp+2) = (llc2Sta->rcvWindowSz)<<1;
			}
		}
	}

	llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
	rsp = (((llc2HdrU->ssap) & LLC_RESPONSE) ? TRUE : FALSE);

	if (SAMsend(mac, rem, msgdsize(mp), mp, 0, (void *)0) == 0) {
		ILD_WRLOCK(llc2Sap->sap_lock);
		if (rsp == TRUE) {
			/* muoe971984 buzz 8/27/97 */
			llc2Sap->xidRspSent += 1;
		} else {
			/* muoe971984 buzz 8/27/97 */
			llc2Sap->xidCmdSent += 1;
		}
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}

	return (0);
}


/*
 *
 * activeRcvXidCmd
 *
 */
/*ARGSUSED*/
int
activeRcvXidCmd(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t pf,
    uintptr_t parm2)
{
	llc2HdrXid_t *llc2HdrXid;
	int	remRW;
	llc2Con_t	*llc2Con;
	uchar_t	saveSsap;
	int		len;
	uint_t	key;
	uint_t	tmp_key;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	ILD_WRLOCK(llc2Sap->sap_lock);
	llc2Sap->xidCmdRcvd += 1;
	ILD_RWUNLOCK(llc2Sap->sap_lock);

	llc2HdrXid = (llc2HdrXid_t *)mp->b_rptr;
	len = msgdsize(mp);
	if ((len == sizeof (llc2HdrXid_t)) &&
		(llc2HdrXid->fmtId == LLC_XID_FMTID)) {
		/*
		 * If the XID sender's receive window size (RW) is not
		 * equal to zero, search for a connection component on the
		 * local SAP with the same MAC address and SAP as the XID.
		 * If found, set the connection's
		 * transmit window size (k) equal to the smaller of the receive
		 * window size from the XID and the configured transmit
		 * window size.
		 *
		 * NOTE: This code is probably wasted effort. SNA llc2
		 * stations may send an IEEE XID, but only before sending
		 * or receiving SABME, when no connection exists yet.
		 */
		remRW = (int)((llc2HdrXid->rw) >> 1);
		if (remRW != 0) {
			ILD_WRLOCK(llc2Sap->sap_lock);
			bcopy((char *)&rem->llc.dl_nodeaddr[3],
				(char *)&tmp_key,
				sizeof (uint_t));
			key = (((uint_t)rem->llc.dl_sap) |
				(tmp_key & MAC_UNIQUE_MASK));

			llc2Con = (llc2Con_t *)llc2Sap->conHead.flink;
			while (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
				/*
				 * The key already contains the lower 3 bytes of
				 * the remote MAC address. Just match the top 3
				 * to be sure.
				 */
				if ((key == llc2Con->key) &&
				(bcmp((char *)&rem->llc.dl_nodeaddr[0],
				(char *)&llc2Con->rem.llc.dl_nodeaddr[0], 3)
					== 0)) {
					break;
				}
				llc2Con = (llc2Con_t *)llc2Con->chain.flink;
			}
			if (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
				if (remRW <= llc2Con->kMax) {
					llc2Con->k = remRW;
					llc2Con->kMax = remRW;
				}
			}
			ILD_RWUNLOCK(llc2Sap->sap_lock);
		}
		saveSsap = llc2HdrXid->ssap;
		llc2HdrXid->ssap = llc2HdrXid->dsap | LLC_RESPONSE;
		llc2HdrXid->dsap = saveSsap;
		llc2HdrXid->llc = LLC_TYPE_1_2;
		llc2HdrXid->rw = (llc2Sta->rcvWindowSz)<<1;

		if (SAMsend(mac, rem, len, mp, 0, (void *)0) == 0) {
			/* muoe971984 buzz 8/27/97 */
			ILD_WRLOCK(llc2Sap->sap_lock);
			llc2Sap->xidRspSent += 1;
			ILD_RWUNLOCK(llc2Sap->sap_lock);
		}
	} else {
		/*
		 * strip the LLC header off
		 */
		mp->b_rptr += sizeof (llc2HdrU_t);

		dlpi_xid_ind(mac, loc, rem, mp, FALSE, pf, 0);
	}

	return (0);
}


/*
 *
 * activeRcvXidRsp
 *
 */
/*ARGSUSED*/
int
activeRcvXidRsp(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t pf,
    uintptr_t parm2)
{
	llc2HdrXid_t *llc2HdrXid;
	int remRW;
	llc2Con_t *llc2Con;
	int len;
	uint_t key;
	uint_t tmp_key;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	ILD_WRLOCK(llc2Sap->sap_lock);
	llc2Sap->xidRspRcvd += 1;
	ILD_RWUNLOCK(llc2Sap->sap_lock);

	llc2HdrXid = (llc2HdrXid_t *)mp->b_rptr;
	len = msgdsize(mp);
	if ((len == sizeof (llc2HdrXid_t)) &&
		(llc2HdrXid->fmtId == LLC_XID_FMTID)) {
		/*
		 * If the XID sender's receive window size (RW) is not equal
		 * to zero, search for a connection component on the local
		 * SAP with the same MAC address and SAP as the XID. If
		 * found, set the connection's transmit window size (k)
		 * equal to the smaller of the receive
		 * window size from the XID and the configured transmit
		 * window size.
		 *
		 * NOTE: This code is probably wasted effort. SNA llc2
		 * stations may send an IEEE XID, but only before sending
		 * or receiving SABME, when no connection exists yet.
		 */
		remRW = ((llc2HdrXid->rw) >> 1);
		if (remRW != 0) {
			ILD_WRLOCK(llc2Sap->sap_lock);
			bcopy((char *)&rem->llc.dl_nodeaddr[3],
				(char *)&tmp_key, sizeof (uint_t));
			key = (((uint_t)rem->llc.dl_sap) |
				(tmp_key & MAC_UNIQUE_MASK));

			llc2Con = (llc2Con_t *)llc2Sap->conHead.flink;
			while (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
				/*
				 * The key already contains the lower 3 bytes of
				 * the remote MAC address. Just match the top 3
				 * to be sure.
				 */
				if ((key == llc2Con->key) &&
				(bcmp((char *)&rem->llc.dl_nodeaddr[0],
				(char *)&llc2Con->rem.llc.dl_nodeaddr[0], 3)
					== 0)) {
					break;
				}
				llc2Con = (llc2Con_t *)llc2Con->chain.flink;
			}
			if (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
				if (remRW <= llc2Con->kMax) {
					llc2Con->k = remRW;
					llc2Con->kMax = remRW;
				}
			}
			ILD_RWUNLOCK(llc2Sap->sap_lock);
		}
	}

	/*
	 * strip the LLC header off
	 */
	mp->b_rptr += sizeof (llc2HdrU_t);

	dlpi_xid_ind(mac, loc, rem, mp, TRUE, pf, 0);

	return (0);
}


/*
 *
 * activeTestReq
 *
 */
/*ARGSUSED*/
int
activeTestReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	if (SAMsend(mac, rem, msgdsize(mp), mp, 0, (void *)0) == 0) {
		/* muoe971984 buzz 8/27/97 */
		ILD_WRLOCK(llc2Sap->sap_lock);
		llc2Sap->testCmdSent += 1;
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}

	return (0);
}


/*
 *
 * activeRcvTestCmd
 *
 */
/*ARGSUSED*/
int
activeRcvTestCmd(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	llc2HdrU_t *llc2HdrU;
	uchar_t saveSsap;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	ILD_WRLOCK(llc2Sap->sap_lock);
	llc2Sap->testCmdRcvd += 1;
	ILD_RWUNLOCK(llc2Sap->sap_lock);

	llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
	saveSsap = llc2HdrU->ssap;
	llc2HdrU->ssap = llc2HdrU->dsap | LLC_RESPONSE;
	llc2HdrU->dsap = saveSsap;

	if (SAMsend(mac, rem, msgdsize(mp), mp, 0, (void *)0) == 0) {
		/* muoe971984 buzz 8/27/97 */
		ILD_WRLOCK(llc2Sap->sap_lock);
		llc2Sap->testRspSent += 1;
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}

	return (0);
}


/*
 *
 * activeRcvTestRsp
 *
 *
 */
/*ARGSUSED*/
int
activeRcvTestRsp(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t pf,
    uintptr_t parm2)
{

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	ILD_WRLOCK(llc2Sap->sap_lock);
	llc2Sap->testRspRcvd += 1;
	ILD_RWUNLOCK(llc2Sap->sap_lock);


	/*
	 * strip the LLC header off
	 */
	mp->b_rptr += sizeof (llc2HdrU_t);

	dlpi_test_ind(mac, loc, rem, mp, TRUE, pf, 0);

	return (0);
}


/*
 *
 * activeDeactivationReq
 *
 */
/*ARGSUSED*/
int
activeDeactivationReq(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	uchar_t sap;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	/*
	 * disable all subsidiary connection components and the SAP component
	 */
	if (llc2Sap) {
		ILD_WRLOCK(llc2Sap->sap_lock);
		sap = llc2Sap->sap;
		llc2Sta->sapTbl[(sap>>1)] = (llc2Sap_t *)0;
		disableSap(llc2Sap, llc2Sta, mac);
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}
	return (0);
}


/*
 *
 * activeRcvBadPdu
 *
 *
 */
/*ARGSUSED*/
int
activeRcvBadPdu(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, uchar_t origEvent,
    mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem, uintptr_t parm1,
    uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	if (llc2Sap) {
		ILD_WRLOCK(llc2Sap->sap_lock);
		llc2Sap->protocolError += 1;
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	}
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * LLC2 CONNECTION COMPONENT STATE/EVENT FUNCTIONS
 *
 */

/*
 *	ADM
 *
 *
 * LLC2 Connection Component Event Functions for ADM State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	The connection component control block does not exist in this
 *	disabled state. Therefore, error statistics are recorded in the
 *	SAP component control block.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) DLPI link structure ptr on a connect
 *				request event
 *				2) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *			non-zero value the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * admInvalidEvt
 *
 *
 * locks		sta_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
admInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);
	/* muoe971984 buzz 8/27/97 */
	llc2Sap->outOfState += 1;
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * admConnectReq
 *
 * locks		no locks set on entry/exit
 *			con_lock locked/unlocked
 */
/*ARGSUSED*/
int
admConnectReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t link, uintptr_t parm2)
{
	llc2Con_t *newCon = (llc2Con_t *)0;
	int retVal = LLC2_GOOD_STATUS;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	if ((retVal = enableCon(&newCon, llc2Sap, llc2Sta, mac, rem)) == 0) {
		/* enableCon returns con_lock locked */
		sendSabme(newCon, llc2Sta, mac, TRUE, (mblk_t *)0);

		newCon->flag |= P_FLAG;
		startTimer(T_ACK, newCon, llc2Sta);
		/* muoe972183 */
		newCon->ackRetryCount = 0;
		newCon->flag &= ~S_FLAG;
		newCon->link = (link_t *)link;
		newCon->link->lk_sid = newCon->sid;
		CON_NEXT_STATE(newCon, CON_SETUP);


		ILD_RWUNLOCK(newCon->con_lock);
	} else {
		ild_trace(MID_LLC2, __LINE__, retVal, llc2Sap->sap, 0);
	}

	return (retVal);
}


/*
 *
 * admRcvSabmeCmd
 *
 *
 * locks	 con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
admRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2Con_t *newCon = (llc2Con_t *)0;
	dlsap_t newrem;
	int retVal = LLC2_GOOD_STATUS;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	freemsg(mp);
	newrem = *rem;
	if ((retVal = enableCon(&newCon, llc2Sap, llc2Sta, mac, &newrem))
		== 0) {
		/* enableCon return con_lock locked */
		newCon->sabmeRcvd++;

		newCon->flag = ((pf == TRUE) ? newCon->flag | F_FLAG :
				newCon->flag & ~F_FLAG);
		CON_NEXT_STATE(newCon, CON_CONN);

		ILD_RWUNLOCK(newCon->con_lock);
		dlpi_connect_ind(mac, llc2Sap->sap, rem, newCon->sid);
	} else {
		ild_trace(MID_LLC2, __LINE__, retVal, llc2Sap->sap, 0);
	}



	return (0);
}


/*
 *
 * admRcvDiscCmd
 *
 *
 * locks		no locks set on entry/exit
 *
 */
/*ARGSUSED*/
int
admRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	sendDm(mac, (llc2Con_t *)(NULL), rem, llc2Sap->sap, pf, mp);

	return (0);
}


/*
 *
 * admRcvXxxCmd
 *
 *
 * locks		no locks set on entry/exit
 *
 */
/*ARGSUSED*/
int
admRcvXxxCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	/*
	 * 802.2 calls for sending a DM Final response to an S- or I-format
	 * Poll command in Connection Component state ADM. However, the 802.2
	 * spec is ambiguous about when an LLC2 node must consider itself to be
	 * in Asynchronous Disconnected Mode (ADM).
	 *
	 * IBM 3745 testing has shown that the IBM NCP LLC2 does not send DM,
	 * and the duplicate MAC feature of ACF/NCP6.1 would not work were LLC2
	 * to send DM. For these reasons, the sending of DM has been removed.
	 */
	freemsg(mp);

	return (0);
}


/*
 *
 * admRcvXxxRsp
 *
 *
 * locks		no locks set on entry/exit
 *
 */
/*ARGSUSED*/
int
admRcvXxxRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Sap->sap, 0);

	freemsg(mp);

	return (0);
}


/*
 *	CONN
 *
 *
 * LLC2 Connection Component Event Functions for CONN State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) DLPI link structure ptr on a connect
 *					response event
 *				2) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * connInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
connInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;
	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * connConnectRes
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
connConnectRes(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t link, uintptr_t parm2)
{
	int pf;
	int retVal;
	link_t *lnk;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

	if ((retVal = sendUa(llc2Con, llc2Sta, mac, pf, (mblk_t *)0)) == 0) {
		llc2Con->vs = 0;
		llc2Con->vr = 0;
		llc2Con->nrRcvd = 0;

		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		llc2Con->flag &= ~REMOTE_BUSY;
		llc2Con->link = (link_t *)link;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		lnk = llc2Con->link;
		sid = llc2Con->sid;
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_connect_con(mac, lnk, sid, 0);
	} else {
		ild_trace(MID_LLC2, __LINE__, retVal, llc2Con->sid, 0);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	/*
	 * since there is no retry mechanism (timer) if the MAC layer cannot
	 * send the UA response, the user will be notified of the failure
	 * when a non-zero value is returned here
	 */

	return (retVal);
}


/*
 *
 * connDisconnectReq
 *
 *
 * lock			no locks set on entry/exit
 *			con_lock locked/unlocked
 */
/*ARGSUSED*/
int
connDisconnectReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int pf;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

	sendDm(mac, llc2Con, &llc2Con->rem, llc2Sap->sap, pf, (mblk_t *)0);

	/* disableCon will unlock, and possible free, the con_lock. */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_con(mac, sid, 0);

	return (0);
}


/*
 *
 * connRcvSabmeCmd
 *
 *
 * locks	con_lock is locked/unlocked
 */
/*ARGSUSED*/
int
connRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	freemsg(mp);

	llc2Con->flag = ((pf == TRUE) ? llc2Con->flag | F_FLAG :
				llc2Con->flag & ~F_FLAG);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * connRcvDmRsp
 *
 * locks	con_lock is locked/unlocked
 */
/*ARGSUSED*/
int
connRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	freemsg(mp);

	/* Disable Con will unlock the con_lock. */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 * connRcvXxxYyy
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
connRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * connRcvBadPdu
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
connRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_BADPDU);
}


/*
 *	RESET_WAIT
 *
 *
 * LLC2 Connection Component Event Functions for RESET_WAIT State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 *	If the send acknowledgement timer, which was started in an
 *	information transfer state, expires in this connection reset state,
 *	clear the send acknowledgement parameters and do not send an
 *	acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * resetwaitInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetwaitInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * resetwaitResetReq
 *
 *
 * locks	con_lock locked/unlocked.
 */
/*ARGSUSED*/
int
resetwaitResetReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int pf;
	int retVal = LLC2_GOOD_STATUS;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if ((llc2Con->flag & S_FLAG) == 0) {
		sendSabme(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
		llc2Con->flag |= P_FLAG;
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount = 0;	/* muoe972183 */
		CON_NEXT_STATE(llc2Con, CON_RESET);
		/* MR muoe941595 buzz 06/01/94 - added missing unlock */
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

		/*
		 * since there is no retry mechanism(timer) if the MAC
		 * layer cannot send the UA response, the user will be
		 * notified of the failure when a non-zero value is returned
		 * below
		 */
		if ((retVal = sendUa(llc2Con, llc2Sta, mac, pf, (mblk_t *)0))
			== 0) {
			resetCon(llc2Con, llc2Sta);
			llc2Con->vs = 0;
			llc2Con->vr = 0;
			llc2Con->nrRcvd = 0;

			/* muoe972183 */
			llc2Con->pollRetryCount = 0;
			llc2Con->ackRetryCount = 0;
			llc2Con->pollRetryCountL2 = 0;
			llc2Con->ackRetryCountL2 = 0;

			llc2Con->flag &= ~(P_FLAG | T_FLAG);
			llc2Con->flag &= ~REMOTE_BUSY;
			CON_NEXT_STATE(llc2Con, CON_NORMAL);

			sid = llc2Con->sid;
			ILD_RWUNLOCK(llc2Con->con_lock);
			dlpi_reset_con(mac, sid, 0);
		} else {
			ild_trace(MID_LLC2, __LINE__, retVal, llc2Con->sid, 0);
			ILD_RWUNLOCK(llc2Con->con_lock);
		}
	}

	return (retVal);
}
/*
 *
 * resetwaitLocalBusyClr
 *
 */
/*ARGSUSED*/
int
resetwaitLocalBusyClr(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;

	/*
	 * Local Busy Clear is valid in state RESET WAIT if the saved
	 * recover state is AWAIT BUSY.
	 */
	if (llc2Con->recoverState == CON_AWAIT_BUSY) {
		if (llc2Con->dataFlag == BUSY_REJECT) {
			llc2Con->recoverState = CON_AWAIT_REJECT;
		} else {
			llc2Con->recoverState = CON_AWAIT;
		}
	} else {
		retVal = LLC2_ERR_BADSTATE;
	}

	return (retVal);
}

/*
 *
 * resetwaitRecoverReq
 *
 */
/*ARGSUSED*/
int
resetwaitRecoverReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;

	/*
	 * Set the P-bit flag to zero, so startTimer() will reset retry counts
	 * to zero. Set the Recovery flag to one, so that xxxPollTimerExp will
	 * not do second level retries if first level retries fail.
	 */
	switch (llc2Con->recoverState) {
	case CON_AWAIT :
		llc2Con->flag |= R_FLAG;
		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		/* save all of dlsap of remote station */
		llc2Con->rem = *rem;
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
		break;

	case CON_AWAIT_BUSY :
		llc2Con->flag |= R_FLAG;
		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		/* save all of dlsap of remote station */
		llc2Con->rem = *rem;
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		break;

	case CON_AWAIT_REJECT :
		llc2Con->flag |= R_FLAG;
		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		/* save all of dlsap of remote station */
		llc2Con->rem = *rem;
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
		break;

	default:
		retVal = LLC2_ERR_BADSTATE;
		break;
	}

	return (retVal);
}




/*
 *
 * resetwaitDisconnectReq
 *
 *
 * locks	con_lock locked/unlocked
 */
/*ARGSUSED*/
int
resetwaitDisconnectReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	int pf;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if ((llc2Con->flag & S_FLAG) == 0) {
		sendDisc(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
		llc2Con->flag |= P_FLAG;
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount = 0;	/* muoe972183 */
		CON_NEXT_STATE(llc2Con, CON_D_CONN);
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

		sendDm(mac, llc2Con, &llc2Con->rem, llc2Sap->sap, pf,
			(mblk_t *)0);
		/* Disable will unlock the con_lock */
		disableCon(llc2Con, llc2Sap, llc2Sta, mac);

		dlpi_disconnect_con(mac, sid, 0);
	}

	return (0);
}


/*
 *
 * resetwaitRcvDmRsp
 *
 *
 * locks	no locks locked/unlocked on entry/exit
 */
/*ARGSUSED*/
int
resetwaitRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	/* This is locked here because disableCon EXPECTS it to be locked */
	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	freemsg(mp);
	/* Disable will unlock, and possible free, the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetwaitRcvSabmeCmd
 *
 *
 * locks		con_lock locked/unlocked
 */
/*ARGSUSED*/
int
resetwaitRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	freemsg(mp);

	llc2Con->flag |= S_FLAG;
	llc2Con->flag = ((pf == TRUE) ? llc2Con->flag | F_FLAG :
				llc2Con->flag & ~F_FLAG);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * resetwaitRcvDiscCmd
 *
 *
 * lock			con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetwaitRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	sendDm(mac, llc2Con, rem, llc2Sap->sap, pf, mp);
	/* disableCon will unlock, and possibly free, the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetwaitRcvXxxYyy
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetwaitRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;
	freemsg(mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * resetwaitRcvBadPdu
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetwaitRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * resetwaitSendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetwaitSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *	RESET_CHECK
 *
 *
 * LLC2 Connection Component Event Functions for RESET_CHECK State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 *	If the send acknowledgement timer, which was started in an
 *	information transfer state, expires in this connection reset state,
 *	clear the send acknowledgement parameters and do not send an
 *	acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * resetcheckInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * resetcheckResetRes
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckResetRes(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int pf;
	int retVal = LLC2_GOOD_STATUS;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

	if ((retVal = sendUa(llc2Con, llc2Sta, mac, pf, (mblk_t *)0)) == 0) {
		resetCon(llc2Con, llc2Sta);
		llc2Con->vs = 0;
		llc2Con->vr = 0;
		llc2Con->nrRcvd = 0;

		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		llc2Con->flag &= ~REMOTE_BUSY;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		sid = llc2Con->sid;
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_reset_con(mac, sid, 0);
	} else {
		ild_trace(MID_LLC2, __LINE__, retVal, llc2Con->sid, 0);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	/*
	 * since there is no retry mechanism (timer) if the MAC layer cannot
	 * send the UA response, the user will be notified of the failure
	 * when a non-zero value is returned here
	 */
	return (retVal);
}


/*
 *
 * resetcheckDisconnectReq
 *
 *
 * locks	con_lock is locked/unlocked
 */
/*ARGSUSED*/
int
resetcheckDisconnectReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	int pf;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	pf = ((llc2Con->flag & F_FLAG) ? TRUE : FALSE);

	sendDm(mac, llc2Con, &llc2Con->rem, llc2Sap->sap, pf, (mblk_t *)0);

	/* disableCon will unlock the con_lock */

	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_con(mac, sid, 0);

	return (0);
}


/*
 *
 * resetcheckRcvDmRsp
 *
 *
 * locks		con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	/* disableCon will unlock the con_lock */

	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetcheckRcvSabmeCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	freemsg(mp);

	llc2Con->flag = ((pf == TRUE) ? llc2Con->flag | F_FLAG :
				llc2Con->flag & ~F_FLAG);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * resetcheckRcvDiscCmd
 *
 *
 * locks		con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	sendDm(mac, llc2Con, rem, llc2Sap->sap, pf, mp);

	/* disbleCon will unlock the con_lock */

	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetcheckRcvXxxYyy
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * resetcheckRcvBadPdu
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * resetcheckSendAckTimerExp
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetcheckSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *	SETUP
 *
 *
 * LLC2 Connection Component Event Functions for SETUP State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator (for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority (not currently used)
 *
 * returns:
 *	0		success (if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * setupInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * setupRcvSabmeCmd
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	llc2Con->vs = 0;
	llc2Con->vr = 0;
	llc2Con->nrRcvd = 0;
	llc2Con->ackRetryCount = 0;	/* muoe972183 */
	(void) sendUa(llc2Con, llc2Sta, mac, pf, mp);
	llc2Con->flag |= S_FLAG;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * setupRcvUaRsp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvUaRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	link_t *link;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	if (((pf == TRUE) && (llc2Con->flag & P_FLAG)) ||
		((pf == FALSE) && !(llc2Con->flag & P_FLAG))) {
		llc2Con->uaRcvd++;

		stopTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->vs = 0;
		llc2Con->vr = 0;
		llc2Con->nrRcvd = 0;
		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		if (pf == TRUE) {
			stopTimer(T_P, llc2Con, llc2Sta);
		}
		llc2Con->flag &= ~REMOTE_BUSY;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		/*
		 * To compensate for a deficiency in the Microsoft LLC2/NetBeui
		 * implementation on DOS clients, an RR command PDU with the
		 * poll bit set is sent after the establishment of the
		 * connection. This places the DOS client in the correct
		 * state to appropriately respond to a NetBios session init
		 * request from the server with a session confirm I-frame.
		 * Without this RR polling sequence, the client
		 * responds incorrectly with a session init I-frame.
		 */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, mp);
		startTimer(T_P, llc2Con, llc2Sta);

		link = llc2Con->link;
		sid = llc2Con->sid;
		dlpi_XON(mac, link);
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_connect_con(mac, link, sid, 0);
	} else {
		freemsg(mp);
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
		llc2Con->protocolError++;
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	return (0);
}


/*
 *
 * setupAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	link_t *link;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	link = llc2Con->link;
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->ackTimerExp++;

	if (llc2Con->flag & S_FLAG) {

		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		llc2Con->flag &= ~(P_FLAG|T_FLAG);
		llc2Con->flag &= ~REMOTE_BUSY;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		dlpi_XON(mac, link);
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_connect_con(mac, link, sid, 0);
	} else if (llc2Con->ackRetryCount < llc2Con->N2) {

		sendSabme(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
		llc2Con->flag |= P_FLAG;
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount++;
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* DisableCon will unlock con_lock */
		disableCon(llc2Con, llc2Sap, llc2Sta, mac);

		dlpi_disconnect_ind(mac, sid,
					DL_CONREJ_DEST_UNREACH_PERMANENT);
	}

	return (0);
}


/*
 *
 * setupRcvDiscCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	sendDm(mac, llc2Con, rem, llc2Sap->sap, pf, mp);
	stopTimer(T_ACK, llc2Con, llc2Sta);
	/* disableCon will unlock con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * setupRcvDmRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;
	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);
	/* disableCon will unlock con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * setupRcvXxxYyy
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * setupRcvBadPdu
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
setupRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_BADPDU);
}


/*
 *	RESET
 *
 *
 * LLC2 Connection Component Event Functions for RESET State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 *	If the send acknowledgement timer, which was started in an
 *	information transfer state, expires in this connection reset state,
 *	clear the send acknowledgement parameters and do not send an
 *	acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * resetInvalidEvt
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * resetRcvSabmeCmd
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	resetCon(llc2Con, llc2Sta);
	llc2Con->vs = 0;
	llc2Con->vr = 0;
	llc2Con->nrRcvd = 0;
	llc2Con->ackRetryCount = 0;	/* muoe972183 */
	llc2Con->flag |= S_FLAG;
	(void) sendUa(llc2Con, llc2Sta, mac, pf, mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * resetRcvUaRsp
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvUaRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	link_t *link;
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	if (((pf == TRUE) && (llc2Con->flag & P_FLAG)) ||
		((pf == FALSE) && !(llc2Con->flag & P_FLAG))) {
		llc2Con->uaRcvd++;

		stopTimer(T_ACK, llc2Con, llc2Sta);
		resetCon(llc2Con, llc2Sta);
		llc2Con->vs = 0;
		llc2Con->vr = 0;
		llc2Con->nrRcvd = 0;
		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		if (pf == TRUE) {
			stopTimer(T_P, llc2Con, llc2Sta);
		}
		llc2Con->flag &= ~REMOTE_BUSY;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		link = llc2Con->link;
		sid = llc2Con->sid;
		dlpi_XON(mac, link);
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_reset_con(mac, sid, 0);
	} else {
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
		llc2Con->protocolError++;
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	return (0);
}


/*
 *
 * resetAckTimerExp
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;
	link_t *link;

	ILD_WRLOCK(llc2Con->con_lock);

	link = llc2Con->link;
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->ackTimerExp++;

	if (llc2Con->flag & S_FLAG) {
		/* muoe972183 */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;
		llc2Con->pollRetryCountL2 = 0;
		llc2Con->ackRetryCountL2 = 0;

		llc2Con->flag &= ~(P_FLAG | T_FLAG);
		llc2Con->flag &= ~REMOTE_BUSY;
		CON_NEXT_STATE(llc2Con, CON_NORMAL);

		dlpi_XON(mac, link);
		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_reset_con(mac, sid, 0);
	} else if (llc2Con->ackRetryCount < llc2Con->N2) {
		sendSabme(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
		llc2Con->flag |= P_FLAG;
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount++;	/* muoe972183 */
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* disableCon will unlock the con_lock */
		disableCon(llc2Con, llc2Sap, llc2Sta, mac);

		dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);
	}

	return (0);
}


/*
 *
 * resetRcvDiscCmd
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	sendDm(mac, llc2Con, rem, llc2Sap->sap, pf, mp);
	stopTimer(T_ACK, llc2Con, llc2Sta);
	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetRcvDmRsp
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;
	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);

	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * resetRcvXxxYyy
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * resetRcvBadPdu
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_BADPDU);
}


/*
 *
 * resetSendAckTimerExp
 *
 *
 * locks	con_lock is locked/unlocked
 *
 */
/*ARGSUSED*/
int
resetSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *	D_CONN
 *
 *
 * LLC2 Connection Component Event Functions for D_CONN State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 *	If the send acknowledgement timer, which was started in an
 *	information transfer state, expires in this disconnection pending
 *	state, clear the send acknowledgement parameters and do not send an
 *	acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * dconnInvalidEvt
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * dconnRcvSabmeCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	sendDm(mac, llc2Con, rem, llc2Sap->sap, pf, mp);
	stopTimer(T_ACK, llc2Con, llc2Sta);

	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_con(mac, sid, 0);

	return (0);
}


/*
 *
 * dconnRcvUaRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvUaRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	if (((pf == TRUE) && (llc2Con->flag & P_FLAG)) ||
		((pf == FALSE) && !(llc2Con->flag & P_FLAG))) {
		stopTimer(T_ACK, llc2Con, llc2Sta);
		/* disableCon unlock the con_lock */
		disableCon(llc2Con, llc2Sap, llc2Sta, mac);

		dlpi_disconnect_con(mac, sid, 0);
	} else {
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
		llc2Con->protocolError++;
		ILD_RWUNLOCK(llc2Con->con_lock);
	}

	return (0);
}


/*
 *
 * dconnRcvDiscCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	(void) sendUa(llc2Con, llc2Sta, mac, pf, mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * dconnRcvDmRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);
	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_con(mac, sid, 0);

	return (0);
}


/*
 *
 * dconnRcvXxxYyy
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvXxxYyy(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * dconnRcvBadPdu
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * dconnAckTimerExp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->ackTimerExp++;

	if (llc2Con->ackRetryCount < llc2Con->N2) {
		sendDisc(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
		llc2Con->flag |= P_FLAG;
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount++;	/* muoe972183 */
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* disableCon will unlock con_lock */
		disableCon(llc2Con, llc2Sap, llc2Sta, mac);

		dlpi_disconnect_con(mac, sid, 0);
	}

	return (0);
}

/*
 *
 * dconnSendAckTimerExp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
dconnSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *	ERROR
 *
 *
 * LLC2 Connection Component Event Functions for ERROR State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 *	Received PDUs which cannot be parsed successfully and are
 *	associated with the connection component are logged as errors.
 *
 *	If the send acknowledgement timer, which was started in an
 *	information transfer state, expires in this connection error state,
 *	clear the send acknowledgement parameters and do not send an
 *	acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * errorInvalidEvt
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * errorRcvSabmeCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sabmeRcvd++;

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);
	llc2Con->flag = ((pf == TRUE) ? llc2Con->flag | F_FLAG :
				llc2Con->flag & ~F_FLAG);
	CON_NEXT_STATE(llc2Con, CON_RESET_CHECK);

	sid = llc2Con->sid;
	ILD_RWUNLOCK(llc2Con->con_lock);

	dlpi_reset_ind(mac, sid, DL_USER, DL_RESET_RESYNCH);

	return (0);
}


/*
 *
 * errorRcvDiscCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;
	uchar_t lastcmd;

	ILD_WRLOCK(llc2Con->con_lock);
	/*
	 * muoe961411 4/16/96 buzz
	 * Restore cmdLastRcvd to the command that caused us to send FRMR,
	 * and restore cmdLastSent to FRMR after sending UA.
	 *
	 * We got into CON_ERROR state because of a perceived protocol
	 * violation by the other end, and sent FRMR to tell the other
	 * end about it. We still need to tell user at our end about it.
	 */
	llc2Con->cmdLastRcvd = llc2Con->frmrInfo.c1;

	lastcmd = llc2Con->cmdLastSent;

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	(void) sendUa(llc2Con, llc2Sta, mac, pf, mp);

	llc2Con->cmdLastSent = lastcmd;

	stopTimer(T_ACK, llc2Con, llc2Sta);

	/*
	 * disableCon will unlock the con_lock
	 */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * errorRcvDmRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);
	/*
	 * disableCon will unlock the con_lock
	 */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * errorRcvFrmrRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvFrmrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);
	/*
	 * Restore cmdLastRcvd to the command that caused us to send FRMR.
	 */
	llc2Con->cmdLastRcvd = llc2Con->frmrInfo.c1;

	llc2Con->frmrRcvd++;

	freemsg(mp);

	stopTimer(T_ACK, llc2Con, llc2Sta);
	llc2Con->flag &= ~S_FLAG;
	llc2Con->recoverState = CON_ADM;
	CON_NEXT_STATE(llc2Con, CON_RESET_WAIT);

	ILD_RWUNLOCK(llc2Con->con_lock);
	dlpi_reset_ind(mac, sid, DL_PROVIDER, DL_RESET_RESYNCH);

	return (0);
}


/*
 *
 * errorRcvXxxCmd
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvXxxCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	sendFrmr(llc2Con, llc2Sta, mac, pf, &llc2Con->frmrInfo, mp);
	startTimer(T_ACK, llc2Con, llc2Sta);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * errorRcvXxxRsp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvXxxRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	/*
	 * Restore cmdLastRcvd to the command that caused us to send FRMR.
	 */
	llc2Con->cmdLastRcvd = llc2Con->frmrInfo.c1;

	llc2Con->protocolError++;

	freemsg(mp);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * errorRcvBadPdu
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_BADPDU);
}


/*
 *
 * errorAckTimerExp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);
	llc2Con->ackTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	/* muoe961411 buzz 4/16/96 */
	if (llc2Con->ackRetryCount < llc2Con->N2) {
		sendFrmr(llc2Con, llc2Sta, mac, FALSE, &llc2Con->frmrInfo, mp);
		startTimer(T_ACK, llc2Con, llc2Sta);
		llc2Con->ackRetryCount++;
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		llc2Con->flag &= ~S_FLAG;
		CON_NEXT_STATE(llc2Con, CON_RESET_WAIT);

		ILD_RWUNLOCK(llc2Con->con_lock);
		dlpi_reset_ind(mac, sid, DL_PROVIDER, DL_RESET_RESYNCH);
	}

	return (0);
}


/*
 *
 * errorSendAckTimerExp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
errorSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *	INFORMATION TRANSFER COMMON FUNCTIONS
 *
 *
 * Common LLC2 Connection Component Event Functions for the Information
 * Transfer States(NORMAL, BUSY, REJECT, AWAIT, AWAIT_BUSY, AWAIT_REJECT)
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters(except xferRcvXxxRspBadPF and xferMaxRetry:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * parameters(xferRcvXxxRspBadPF() - called by other event handling functions):
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	mp		message ptr
 *
 * parameters xferMaxRetry - called by other event handling functions):
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	reason		DL_ERROR code passed to dlpi_reset_ind
 *	recoverState	llc2 STATE saved in llc2Con for recovery
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * xferDisconnectReq
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferDisconnectReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	/*
	 * muoe961411 buzz 4/16/96
	 *
	 * NOTE: We deviate from 802.2 by processing CON_DISCONNECT_REQ in
	 * state CON_ERROR immediately, instead of waiting for FRMR
	 * retries to be exhausted as specified by 802.2.
	 *
	 * We cannot delay event CON_DISCONNECT_REQ in state CON_ERROR
	 * (as called for by 802.2), since that could leave Streams CLose
	 * processing sleeping for an unacceptably long time if the
	 * disconnect were due to Streams Close processing...
	 *
	 * We cannot treat CON_DISCONNECT_REQ as an invalid event
	 * in state CON_ERROR, as that would leave this connection hung
	 * forever in state CON_RESET_WAIT after FRMR retries exhausted
	 * if dlpi_close were to free up the LINK during Streams Close
	 * processing while we were sitting in CON_ERROR. Subsequent
	 * connection attempts by/for/to/from this poor remote station
	 * would fail due to duplicate MAC/SAP found by llc2BtreeFind
	 * called from enableCon.
	 */

	sendDisc(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);
	llc2Con->flag |= P_FLAG;

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_D_CONN);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * xferResetReq
 *
 *
 * locks con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferResetReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	sendSabme(llc2Con, llc2Sta, mac, TRUE, (mblk_t *)0);
	/*
	 * muoe961411 buzz 4/16/96 change order of execution
	 */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);
	llc2Con->flag |= P_FLAG;

	llc2Con->ackRetryCount = 0;
	llc2Con->flag &= ~S_FLAG;
	CON_NEXT_STATE(llc2Con, CON_RESET);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * xferRcvSabmeCmd
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvSabmeCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);
	llc2Con->sabmeRcvd++;

	freemsg(mp);

	llc2Con->flag = ((pf == TRUE) ? llc2Con->flag | F_FLAG :
				llc2Con->flag & ~F_FLAG);
	stopTimer(T_ALL, llc2Con, llc2Sta);
	CON_NEXT_STATE(llc2Con, CON_RESET_CHECK);

	ILD_RWUNLOCK(llc2Con->con_lock);
	dlpi_reset_ind(mac, sid, DL_USER, DL_RESET_RESYNCH);

	return (0);
}


/*
 *
 * xferRcvDiscCmd
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvDiscCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	(void) sendUa(llc2Con, llc2Sta, mac, pf, mp);
	stopTimer(T_ALL, llc2Con, llc2Sta);
	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * xferRcvFrmrRsp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvFrmrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	/*
	 * muoe961411 buzz 4/16/96
	 * Save Sflag from FRMR and set recover State
	 */
	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);
	llc2Con->frmrRcvd++;
	llc2Con->frmrRcvdFlags = ((llc2HdrFrmr_t *)mp->b_rptr)->s.flag;
	freemsg(mp);

	stopTimer(T_ALL, llc2Con, llc2Sta);

	llc2Con->flag &= ~S_FLAG;
	llc2Con->recoverState = CON_ADM;
	CON_NEXT_STATE(llc2Con, CON_RESET_WAIT);

	ILD_RWUNLOCK(llc2Con->con_lock);
	dlpi_reset_ind(mac, sid, DL_PROVIDER, DL_RESET_RESYNCH);

	return (0);
}


/*
 *
 * xferRcvDmRsp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvDmRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	uint_t sid;

	ILD_WRLOCK(llc2Con->con_lock);

	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	freemsg(mp);

	stopTimer(T_ALL, llc2Con, llc2Sta);
	/* disableCon will unlock the con_lock */
	disableCon(llc2Con, llc2Sap, llc2Sta, mac);

	dlpi_disconnect_ind(mac, sid, DL_DISC_UNSPECIFIED);

	return (0);
}


/*
 *
 * xferRcvZzzCmdInvalidNr
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvZzzCmdInvalidNr(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2HdrS->control;
	llc2Con->frmrInfo.c2 = llc2HdrS->nr;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	llc2Con->frmrInfo.vr = (llc2Con->vr)<<1;
	llc2Con->frmrInfo.flag = FRMR_Z;

	sendFrmr(llc2Con, llc2Sta, mac, pf, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * xferRcvZzzRspInvalidNr
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvZzzRspInvalidNr(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2HdrS->control;
	llc2Con->frmrInfo.c2 = llc2HdrS->nr;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	llc2Con->frmrInfo.vr = ((llc2Con->vr)<<1) | 0x01;
	llc2Con->frmrInfo.flag = FRMR_Z;

	sendFrmr(llc2Con, llc2Sta, mac, FALSE, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * xferRcvICmdInvalidNs
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvICmdInvalidNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2HdrI->ns;
	llc2Con->frmrInfo.c2 = llc2HdrI->nr;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	llc2Con->frmrInfo.vr = (llc2Con->vr)<<1;
	llc2Con->frmrInfo.flag = (FRMR_W | FRMR_V);

	sendFrmr(llc2Con, llc2Sta, mac, pf, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * xferRcvIRspInvalidNs
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvIRspInvalidNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2HdrI->ns;
	llc2Con->frmrInfo.c2 = llc2HdrI->nr;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	llc2Con->frmrInfo.vr = ((llc2Con->vr)<<1) | 0x01;
	llc2Con->frmrInfo.flag = (FRMR_W | FRMR_V);

	sendFrmr(llc2Con, llc2Sta, mac, FALSE, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * xferRcvBadPdu
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvBadPdu(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	llc2Hdr_t *llc2Hdr;
	int mpSize;
	/* muoe971422 buzz 6/11/97 */
	int	max_sdu = mac->mac_max_sdu;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	mpSize = msgdsize(mp);
	llc2Hdr = (llc2Hdr_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2Hdr->control;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	if (llc2Hdr->ssap & LLC_RESPONSE) {
		llc2Con->frmrInfo.vr = ((llc2Con->vr)<<1) | 0x01;
	} else {
		llc2Con->frmrInfo.vr = (llc2Con->vr)<<1;
	}

	if ((llc2Hdr->control & 0x01) == 0) {
		llc2Con->frmrInfo.c2 = ((llc2HdrI_t *)llc2Hdr)->nr;
		/* muoe971422 buzz 6/11/97 */
		if (mpSize > max_sdu)
			/* if it was TOO big */
			llc2Con->frmrInfo.flag = FRMR_Y;
		else
			llc2Con->frmrInfo.flag = FRMR_W;
	} else if ((llc2Hdr->control & 0x03) == 0x01) {
		llc2Con->frmrInfo.c2 = ((llc2HdrS_t *)llc2Hdr)->nr;

		if (mpSize <= sizeof (llc2HdrS_t)) {
			llc2Con->frmrInfo.flag = FRMR_W;
		} else {
			llc2Con->frmrInfo.flag = (FRMR_W | FRMR_X);
		}
	} else {
		llc2Con->frmrInfo.c2 = 0;

		if ((llc2Hdr->control & ~P_F) == FRMR) {
			if (mpSize <= sizeof (llc2HdrFrmr_t)) {
				llc2Con->frmrInfo.flag = FRMR_W;
			} else {
				llc2Con->frmrInfo.flag = FRMR_Y;
			}
		} else {
			/* supposed to be info but none */
			if (mpSize <= sizeof (llc2HdrU_t)) {
				llc2Con->frmrInfo.flag = FRMR_W;
			}
			/* muoe971422 buzz 6/11/97 */
			else if (mpSize > max_sdu) {
				/* Too much Info */
				llc2Con->frmrInfo.flag = FRMR_Y;
			} else {
				llc2Con->frmrInfo.flag = (FRMR_W | FRMR_X);
			}
		}
	}

	sendFrmr(llc2Con, llc2Sta, mac, FALSE, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * xferRcvUaRsp
 *
 *
 * locks		con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
xferRcvUaRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	llc2HdrU_t *llc2HdrU;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
	llc2Con->frmrInfo.c1 = llc2HdrU->control;
	llc2Con->frmrInfo.c2 = 0;
	llc2Con->frmrInfo.vs = (llc2Con->vs)<<1;
	llc2Con->frmrInfo.vr = ((llc2Con->vr)<<1) | 0x01;
	llc2Con->frmrInfo.flag = FRMR_W;

	sendFrmr(llc2Con, llc2Sta, mac, FALSE, &llc2Con->frmrInfo, mp);

	/* stop all timers and clear the P_FLAG, then start the T_ACK timer */
	stopTimer(T_ALL, llc2Con, llc2Sta);
	startTimer(T_ACK, llc2Con, llc2Sta);

	llc2Con->ackRetryCount = 0;
	CON_NEXT_STATE(llc2Con, CON_ERROR);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * xferRcvXxxRspBadPF
 *
 *
 * locks	con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
xferRcvXxxRspBadPF(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac,
    mblk_t *mp)
{
	/*
	 * muoe961411 buzz 4/16/96
	 * this function basically becomes a NOP
	 */
	ild_trace(MID_LLC2, __LINE__, 0, llc2Con->sid, 0);
	llc2Con->protocolError++;

	freemsg(mp);

	return (0);
}

/*
 *
 * xferRnrTimerExp
 *
 */
/*ARGSUSED*/
int
xferRnrTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	xferMaxRetry(llc2Con, llc2Sta, mac,
			DL_DISC_TRANSIENT_CONDITION, CON_ADM);

	return (0);
}


/*
 *
 * xferMacXonInd
 *
 */
/*ARGSUSED*/
int
xferMacXonInd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

	return (0);
}



/*
 *
 * xferMaxRetry
 *
 *
 * locks	con_lock locked on entry, unlocked on exit
 *
 */
/*ARGSUSED*/
void
xferMaxRetry(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, uint_t reason,
    uchar_t recoverState)
{
	uint_t sid;

	ild_trace(MID_LLC2, __LINE__, 0, llc2Con->sid, 0);

	sid = llc2Con->sid;

	llc2Con->maxRetryFail++;

	stopTimer(T_ALL, llc2Con, llc2Sta);
	llc2Con->flag &= ~S_FLAG;
	llc2Con->recoverState = recoverState;

	CON_NEXT_STATE(llc2Con, CON_RESET_WAIT);

	ILD_RWUNLOCK(llc2Con->con_lock);
	dlpi_reset_ind(mac, sid, DL_PROVIDER, reason);
}


/*
 *	NORMAL
 *
 *
 * LLC2 Connection Component Event Functions for NORMAL State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * normalInvalidEvt
 *
 *
 * locks	con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * normalDataReq
 *
 *
 * locks	con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalDataReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;

	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if (!(llc2Con->flag & REMOTE_BUSY)) {
		retVal = sendI(llc2Con, llc2Sta, mac, FALSE, FALSE, mp);

		if (retVal == 0) {
			/*
			 * muoe961411 buzz 4/16/96
			 * Start-if-not-running is the MSB(0x8000)
			 * of the TIMER type
			 */
			startTimer(T_ACK | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		}
	} else {
		retVal = LLC2_ERR_MAXIFRAMES;
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	}
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (retVal);
}


/*
 *
 * normalRcvICmdUnexpNs
 *
 *
 * locks	con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
	} else {
		sendSup(REJ, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}
	startTimer(T_REJ, llc2Con, llc2Sta);
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	CON_NEXT_STATE(llc2Con, CON_REJECT);

	return (0);
}


/*
 *
 * normalRcvIRspUnexpNs
 *
 *
 * locks	con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
		startTimer(T_REJ, llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		CON_NEXT_STATE(llc2Con, CON_REJECT);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;

			stopTimer(T_P, llc2Con, llc2Sta);
			sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			startTimer(T_REJ, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * normalRcvICmd
 *
 *
 * locks	con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	int retVal = LLC2_GOOD_STATUS;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	sid = llc2Con->sid;
	link = llc2Con->link;

	ild_trace(MID_LLC2, __LINE__, origEvent, sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;


	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

	mp->b_rptr += sizeof (llc2HdrI_t);

	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_NORMAL) {
				sendAck(llc2Con, llc2Sta, mac, FALSE, FALSE,
					(mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * busy state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * busy state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_NORMAL) {
				sendAck(llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	}

	return (0);
}


/*
 *
 * normalRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	int retVal = LLC2_GOOD_STATUS;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	link = llc2Con->link;
	sid = llc2Con->sid;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

		mp->b_rptr += sizeof (llc2HdrI_t);
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_NORMAL) {
				sendAck(llc2Con, llc2Sta, mac, FALSE, FALSE,
					(mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * busy state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			/*
			 * If we were to send RNR with P=1 and remain in busy
			 * state(an 802.2 option), that could result in two
			 * F/P checkpoint retry sequences at once, which can
			 * result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

			mp->b_rptr += sizeof (llc2HdrI_t);
			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				if (llc2Con->state == CON_NORMAL) {
					sendAck(llc2Con, llc2Sta, mac, FALSE,
						FALSE, (mblk_t *)0);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				llc2Con->iRcvd++;
				llc2Con->localBusy++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, (mblk_t *)0);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 0;
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;
				llc2Con->localBusy++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, mp);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 1;	/* data lost */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * normalRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendAck(llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * normalRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * normalRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	}

	return (0);
}


/*
 *
 * normalRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * Add start timer call for the Rcv Not Ready timer
			 */
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * normalRcvRejCmd
 *
 *
 * locks con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	freemsg(mp);
	llc2Con->vs = nr;
	clrRemBusy(llc2Con, llc2Sta);
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	/*
	 * muoe961411 buzz 4/16/96
	 * remove if... from tryOutI call is now unconditional
	 */
	setResendI(llc2Con, origVs, nr);
	if (pf == FALSE) {
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	} else {
		(void) tryOutI(llc2Con, llc2Sta, mac, TRUE, TRUE);
	}

	return (0);
}


/*
 *
 * normalRcvRejRsp
 *
 *
 * locks	con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
normalRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		llc2Con->vs = nr;
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * remove if... from setResendI call is now unconditional
		 */
		setResendI(llc2Con, origVs, nr);
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * remove if... from setResendI call is now
			 * unconditional
			 */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * normalInitiatePFCycle
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->inactTimerExp++;

	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		/*
		 * muoe961411 buzz 4/16/96
		 * We deviate from 802.2 here in going to await state. If
		 * we were to send RR with P=1 and remain in normal state,
		 * as specified by 802.2, that could result in two F/P
		 * checkpoint
		 * retry sequences at once, which can result in Invalid N(R).
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
	}

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * normalPollTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->pollTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);


	if (llc2Con->pollRetryCount < llc2Con->N2) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}
	/*
	 * check if second level retries are exhausted.
	 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
	 */
	else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT);
	}

	return (0);
}


/*
 *
 * normalAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->ackTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	/*
	 * muoe961411 buzz 4/16/96
	 * Added the second level of retries and change the
	 * retrycount to ackretrycount and pollretry count
	 */
	if (llc2Con->ackRetryCount < llc2Con->N2) {
		/*
		 * do first level retry(send a poll, unless one is
		 * outstanding)
		 */
		llc2Con->ackRetryCount++;
		if (!(llc2Con->flag & P_FLAG)) {
			sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE,
				(mblk_t *)0);
			startTimer(T_P, llc2Con, llc2Sta);
			CON_NEXT_STATE(llc2Con, CON_AWAIT);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else if (llc2Con->ackRetryCountL2 < llc2Con->N2L2) {

		/*
		 * do second level retry delay(unless a poll is outstanding)
		 *
		 * If a poll is outstanding, we cannot begin an ack L2 delay
		 * because the L2 timer is canceled when a final comes in. Some
		 * future ack timer expiration will find no poll outstanding,
		 * and we will begin the L2 delay then.
		 */
		if (!(llc2Con->flag & P_FLAG)) {
			llc2Con->pollRetryCount = 0;
			llc2Con->ackRetryCount = 0;

			/*
			 * stop all timers except T_RNR and T_INACT,
			 * then start T_L2 Tmr
			 */
			stopTimer(T_OTHER, llc2Con, llc2Sta);
			startTimer(T_L2, llc2Con, llc2Sta);

			llc2Con->ackRetryCountL2++;
			CON_NEXT_STATE(llc2Con, CON_AWAIT);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_RESET_RESYNCH, CON_AWAIT);
	}

	return (0);
}


/*
 *
 * normalRemBusyTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalRemBusyTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2Con->remBusyTimerExp++;

	/*
	 * muoe961411 buzz 4/16/96
	 * Get rid of outer if... and don't increment retryCount
	 */
	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		/*
		 * 802.2 calls for incrementing retryCount here, but since
		 * we set P_FLAG=0 when RNR w/Final is received in await state,
		 * the next time we poll, startTimer(T_P) will set
		 * retryCount=0 again. Incrementing retryCount on busy timer
		 * expiration don't do nuttin'.
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
	}
	ILD_RWUNLOCK(llc2Con->con_lock);


	return (0);
}


/*
 *
 * normalSendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
normalSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2Con->sendAckTimerExp++;
	/*
	 * muoe961411 buzz 4/16/96
	 * Added comment block
	 * and change allow -> allowCount
	 */
	/*
	 * The combination(rsp, pf) = (FALSE, TRUE) is not possible, since
	 * callers of sendAck() use either(TRUE, TRUE) or(FALSE, FALSE).
	 *
	 * If we were to send RR with P=1 and remain in normal state
	 * (an 802.2 option), that could result in two F/P checkpoint
	 * retry sequences at once, which can result in Invalid N(R).
	 */
	sendSup(RR, llc2Con, llc2Sta, mac, llc2Con->rsp, llc2Con->pf,
		(mblk_t *)0);
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *	BUSY
 *
 *
 * LLC2 Connection Component Event Functions for BUSY State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * busyInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * busyDataReq
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyDataReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;

	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if (!(llc2Con->flag & REMOTE_BUSY)) {
		retVal = sendI(llc2Con, llc2Sta, mac, FALSE, FALSE, mp);

		if (retVal == 0) {
			/*
			 * muoe961411 buzz 4/16/96
			 * Start-if-not-running is the MSB(0x8000) of the
			 * TIMER type
			 */
			startTimer(T_ACK | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		}
	} else {
		retVal = LLC2_ERR_MAXIFRAMES;
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	}

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (retVal);
}


/*
 *
 * busyLocalBusyClr
 *
 *
 * locks con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyLocalBusyClr(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if (llc2Con->dataFlag == 1) {
		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		startTimer(T_REJ, llc2Con, llc2Sta);
		CON_NEXT_STATE(llc2Con, CON_REJECT);
	} else if (llc2Con->dataFlag == 0) {
		/*
		 * If we were to send RR with P=1 and remain in a normal state
		 * (an 802.2 option), that could result in two F/P checkpoint
		 * retry sequences at once, which can result in Invalid N(R).
		 */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		CON_NEXT_STATE(llc2Con, CON_NORMAL);
	} else if (llc2Con->dataFlag == BUSY_REJECT) {
		/*
		 * If we were to send RR with P=1 and remain in a normal state
		 * (an 802.2 option), that could result in two F/P checkpoint
		 * retry sequences at once, which can result in Invalid N(R).
		 */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		CON_NEXT_STATE(llc2Con, CON_REJECT);
	} else {
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	}
	/*
	 * Turn off the Flow Control flag. This will let dlpi start
	 * checking to see if it "canput"
	 */
	if (llc2Con->link) {
		(llc2Con->link)->lk_rnr &= ~LKRNR_FC;
	}

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}



/*
 *
 * busyRcvICmdUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
	} else {
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	if (llc2Con->dataFlag == 0) {
		llc2Con->dataFlag = 1;
	}

	return (0);
}


/*
 *
 * busyRcvIRspUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		if (llc2Con->dataFlag == 0) {
			llc2Con->dataFlag = 1;
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;

			stopTimer(T_P, llc2Con, llc2Sta);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
			if (llc2Con->dataFlag == 0) {
				llc2Con->dataFlag = 1;
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * busyRcvICmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	int retVal = LLC2_GOOD_STATUS;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	link = llc2Con->link;
	sid = llc2Con->sid;

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	if (llc2Con->dataFlag == BUSY_REJECT) {
		stopTimer(T_REJ, llc2Con, llc2Sta);
	}

	mp->b_rptr += sizeof (llc2HdrI_t);

	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			llc2Con->dataFlag = 0;
			if (llc2Con->state == CON_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			llc2Con->dataFlag = 0;
			if (llc2Con->state == CON_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		}
	}

	return (0);
}


/*
 *
 * busyRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;
	link = llc2Con->link;
	sid = llc2Con->sid;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		if (llc2Con->dataFlag == BUSY_REJECT) {
			stopTimer(T_REJ, llc2Con, llc2Sta);
		}

		mp->b_rptr += sizeof (llc2HdrI_t);
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			llc2Con->dataFlag = 0;
			if (llc2Con->state == CON_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			if (llc2Con->dataFlag == BUSY_REJECT) {
				stopTimer(T_REJ, llc2Con, llc2Sta);
			}

			mp->b_rptr += sizeof (llc2HdrI_t);
			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				llc2Con->dataFlag = 0;
				if (llc2Con->state == CON_BUSY) {
					sendSup(RNR, llc2Con, llc2Sta, mac,
						FALSE, FALSE, (mblk_t *)0);
					(void) tryOutI(llc2Con, llc2Sta, mac,
							FALSE, FALSE);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				llc2Con->dataFlag = 0;
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, mp);
				llc2Con->dataFlag = 1;	/* data lost */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * busyRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	}

	return (0);
}


/*
 *
 * busyRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * busyRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	}

	return (0);
}


/*
 *
 * busyRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Add start timer call for the Rcv Not Ready timer
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * Add start timer call for the Rcv Not Ready timer
			 */
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * busyRcvRejCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int nr;
	llc2HdrS_t *llc2HdrS;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	llc2Con->vs = nr;
	clrRemBusy(llc2Con, llc2Sta);
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

	if (pf == TRUE) {
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	} else {
		freemsg(mp);
	}

	/*
	 * muoe961411 buzz 4/16/96
	 * remove if... from setResendI. TryOutI call is now unconditional
	 */
	setResendI(llc2Con, origVs, nr);
	(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

	return (0);
}


/*
 *
 * busyRcvRejRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
busyRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int nr;
	llc2HdrS_t *llc2HdrS;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		llc2Con->vs = nr;
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * remove if... from setResendI. tryOutI call is now
		 * unconditional
		 */
		setResendI(llc2Con, origVs, nr);
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * remove if... from setResendI. tryOutI call is now
			 * unconditional
			 */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * busyInitiatePFCycle
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->inactTimerExp++;

	/*
	 * muoe961411 buzz 4/16/96
	 * added state change to CON_AWAIT_BUSY after Start T_P timer.
	 * Also added else(P_FLAG is set) to start the Inactivity timer.
	 */
	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		/*
		 * We deviate from 802.2 here in going to await-busy state. If
		 * we were to send RR with P=1 and remain in busy state,
		 * as specified by 802.2, that could result in two F/P
		 * checkpoint retry sequences at once, which can result in
		 * Invalid N(R).
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
	} else {
		startTimer(T_INACT, llc2Con, llc2Sta);
	}

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * busyPollTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);


	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->pollTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	if (llc2Con->pollRetryCount < llc2Con->N2) {

		/* do first level retry */
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}
	/*
	 * check if second level retries are exhausted.
	 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
	 */
	else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_BUSY);
	}

	return (0);
}


/*
 *
 * busyAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->ackTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	if (llc2Con->ackRetryCount < llc2Con->N2) {
		/*
		 * do first level retry(send a poll, unless one is
		 * outstanding)
		 */
		llc2Con->ackRetryCount++;
		if (!(llc2Con->flag & P_FLAG)) {
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE,
				(mblk_t *)0);
			startTimer(T_P, llc2Con, llc2Sta);
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else if (llc2Con->ackRetryCountL2 < llc2Con->N2L2) {

		/*
		 * do second level retry delay(unless a poll is outstanding)
		 *
		 * If a poll is outstanding, we cannot begin an ack L2 delay
		 * because the L2 timer is canceled when a final comes in. Some
		 * future ack timer expiration will find no poll outstanding,
		 * and we will begin the L2 delay then.
		 */
		if (!(llc2Con->flag & P_FLAG)) {
			llc2Con->pollRetryCount = 0;
			llc2Con->ackRetryCount = 0;

			/*
			 * stop all timers except T_RNR and T_INACT, then
			 * start T_L2 Tmr
			 */
			stopTimer(T_OTHER, llc2Con, llc2Sta);
			startTimer(T_L2, llc2Con, llc2Sta);

			llc2Con->ackRetryCountL2++;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_RESET_RESYNCH, CON_AWAIT_BUSY);
	}
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);


	return (0);
}


/*
 *
 * busyRemBusyTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyRemBusyTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{

	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->remBusyTimerExp++;

	/* muoe972183 */
	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		/*
		 * 802.2 calls for incrementing retryCount here, but since
		 * we set P_FLAG=0 when RNR w/Final is received in await_busy
		 * state, the next time we poll, startTimer(T_P) will set
		 * retryCount=0 again.
		 * Incrementing retryCount on busy timer expiration don't
		 * do nuttin'.
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
	}
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * busyRejTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busyRejTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejTimerExp++;

	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->dataFlag = 1;
		/*
		 * 802.2 calls for incrementing retryCount here, but since we
		 * set P_FLAG=0 when Final is received in await_busy state,
		 * the next time we poll, startTimer(T_P) will set retryCount=0
		 * again. Incrementing retryCount on reject timer expiration
		 * don't do nuttin'.
		 */

		CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
	} else {
		llc2Con->dataFlag = 1;
	}
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * busySendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
busySendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	/*
	 * The timer should only expire in this state if the MAC layer was
	 * unable to accept and send an RNR. Had the RNR been sent, the
	 * send acknowledgement parameters and timer would have been cleared.
	 * Do not send an acknowledgement here, since the other node would take
	 * it as an indication that the busy condition has been cleared.
	 */
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	/*
	 * muoe961411 buzz 4/16/96
	 * allow -> allowCount
	 */
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *	REJECT
 *
 *
 * LLC2 Connection Component Event Functions for REJECT State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * rejectInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * rejectDataReq
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectDataReq(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;

	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if (!(llc2Con->flag & REMOTE_BUSY)) {
		retVal = sendI(llc2Con, llc2Sta, mac, FALSE, FALSE, mp);

		if (retVal == 0) {
			/*
			 * muoe961411 buzz 4/16/96
			 * Start if not running is not the MSB of the
			 * TIMER(0x8000)
			 */
			startTimer(T_ACK | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		}
	} else {
		retVal = LLC2_ERR_MAXIFRAMES;
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	}

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (retVal);
}


/*
 *
 * rejectRcvICmdUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
	} else {
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);

	return (0);
}


/*
 *
 * rejectRcvIRspUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * rejectRcvICmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	stopTimer(T_REJ, llc2Con, llc2Sta);

	mp->b_rptr += sizeof (llc2HdrI_t);

	link = llc2Con->link;
	sid = llc2Con->sid;

	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_REJECT) {
				sendAck(llc2Con, llc2Sta, mac, FALSE, FALSE,
						(mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_NORMAL);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * reject state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */

			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * reject state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_REJECT) {
				sendAck(llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_NORMAL);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	}

	return (0);
}


/*
 *
 * rejectRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	link = llc2Con->link;
	sid = llc2Con->sid;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		stopTimer(T_REJ, llc2Con, llc2Sta);

		mp->b_rptr += sizeof (llc2HdrI_t);
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_REJECT) {
				sendAck(llc2Con, llc2Sta, mac, FALSE, FALSE,
					(mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_NORMAL);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			/*
			 * If we were to send RNR with P=1 and remain in reject
			 * state(an 802.2 option), that could result in two
			 * F/P checkpoint retry sequences at once, which can
			 * result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			/*
			 * If we were to send RNR with P=1 and remain in
			 * reject state(an 802.2 option), that could result
			 * in two F/P checkpoint retry sequences at once,
			 * which can result in Invalid N(R).
			 */
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
			CON_NEXT_STATE(llc2Con, CON_BUSY);
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			stopTimer(T_REJ, llc2Con, llc2Sta);

			mp->b_rptr += sizeof (llc2HdrI_t);

			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				if (llc2Con->state == CON_REJECT) {
					sendAck(llc2Con, llc2Sta, mac, FALSE,
						FALSE, (mblk_t *)0);
					CON_NEXT_STATE(llc2Con, CON_NORMAL);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				llc2Con->iRcvd++;
				llc2Con->localBusy++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, (mblk_t *)0);
				startTimer(T_P, llc2Con, llc2Sta);
				/*
				 * set dataFlag to 0 instead of 2 since the
				 * reject condition has been cleared by the
				 * successful receipt of this I-frame
				 */
				llc2Con->dataFlag = 0;
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;
				llc2Con->localBusy++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, mp);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 1;	/* data lost */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * rejectRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendAck(llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * rejectRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, TRUE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * rejectRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Receiver Not Ready timer if not running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Receiver Not Ready timer if not running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	}

	return (0);
}


/*
 *
 * rejectRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Receiver Not Ready timer if not running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * Start Receiver Not Ready timer if not running
			 */
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * rejectRcvRejCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	freemsg(mp);
	llc2Con->vs = nr;
	clrRemBusy(llc2Con, llc2Sta);
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	/*
	 * muoe961411 buzz 4/16/96
	 * remove if... from setResendI. tryOutI call is now unconditional
	 */
	setResendI(llc2Con, origVs, nr);
	if (pf == FALSE) {
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	} else {
		(void) tryOutI(llc2Con, llc2Sta, mac, TRUE, TRUE);
	}

	return (0);
}


/*
 *
 * rejectRcvRejRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
rejectRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		llc2Con->vs = nr;
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		/*
		 * muoe961411 buzz 4/16/96
		 * remove if... from setResendI. tryOutI call is now
		 * unconditional.
		 */
		setResendI(llc2Con, origVs, nr);
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			stopTimer(T_P, llc2Con, llc2Sta);
			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * remove if... from setResendI. tryOutI call is now
			 * unconditional.
			 */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * rejectInitiatePFCycle
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->inactTimerExp++;

	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		/*
		 * muoe961411 buzz 4/16/96
		 * We deviate from 802.2 here in going to await-reject state.
		 * If we were to send RR with P=1 and remain in normal state,
		 * as specified by 802.2, that could result in two F/P
		 * checkpoint
		 * retry sequences at once, which can result in Invalid N(R).
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
	} else {
		startTimer(T_INACT, llc2Con, llc2Sta);
	}
	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * rejectPollTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2Con->pollTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	/*
	 * muoe961411 buzz 4/16/96
	 * Add 2nd level of retries and pass extra params to xferMaxRetry
	 */
	if (llc2Con->pollRetryCount < llc2Con->N2) {
		/* do first level retry */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		startTimer(T_REJ, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
		ILD_RWUNLOCK(llc2Con->con_lock);
	}
	/*
	 * check if second level retries are exhausted.
	 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
	 */
	else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_REJECT);
	}

	return (0);
}


/*
 *
 * rejectAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2Con->ackTimerExp++;
	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	if (llc2Con->ackRetryCount < llc2Con->N2) {

		/*
		 * do first level retry(send a poll, unless one is
		 * outstanding)
		 */
		llc2Con->ackRetryCount++;

		if (!(llc2Con->flag & P_FLAG)) {
			sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE,
				(mblk_t *)0);
			startTimer(T_P, llc2Con, llc2Sta);
			startTimer(T_REJ, llc2Con, llc2Sta);
			CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else if (llc2Con->ackRetryCountL2 < llc2Con->N2L2) {

		/*
		 * do second level retry delay(unless a poll is outstanding)
		 *
		 * If a poll is outstanding, we cannot begin an ack L2 delay
		 * because the L2 timer is canceled when a final comes in. Some
		 * future ack timer expiration will find no poll outstanding,
		 * and we will begin the L2 delay then.
		 */
		if (!(llc2Con->flag & P_FLAG)) {
			llc2Con->pollRetryCount = 0;
			llc2Con->ackRetryCount = 0;

			/*
			 * stop all timers except T_RNR and T_INACT, then
			 * start T_L2 Tmr
			 */
			stopTimer(T_OTHER, llc2Con, llc2Sta);
			startTimer(T_L2, llc2Con, llc2Sta);

			llc2Con->ackRetryCountL2++;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
		}
		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {
		/* xferMaxRetry unlocks con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_RESET_RESYNCH, CON_AWAIT_REJECT);
	}

	return (0);
}


/*
 *
 * rejectRemBusyTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectRemBusyTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->remBusyTimerExp++;

	/*
	 * muoe961411 buzz 4/16/96
	 * Get rid of outer most IF... check for retry exceeded
	 * don't increment retry count.
	 */

	if (!(llc2Con->flag & P_FLAG)) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		startTimer(T_REJ, llc2Con, llc2Sta);
		/*
		 * 802.2 calls for incrementing retryCount here, but since we
		 * set P_FLAG=0 when RNR w/Final is received in await_reject
		 * state, the next time we poll, startTimer(T_P) will set
		 * retryCount=0 again.
		 * Incrementing retryCount on busy timer expiration
		 * don't do nuttin'.
		 */
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
	}
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * rejectRejTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectRejTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejTimerExp++;
	/*
	 * muoe961411 buzz 4/16/96
	 * Get rid of out most IF... check for retry exceeded
	 * don't increment retry count.
	 */
	if (!(llc2Con->flag & P_FLAG)) {
		/*
		 * muoe961411 buzz 4/16/96
		 * WARNING! If we ever get here, we are in danger of losing the
		 * connection. Sending a second REJ without proof that the
		 * first one was received could cause multiple retry
		 * sequences, which can cause Invalid N(R).
		 *
		 * The 802.2 LLC2 reject timer should not be used. Reject timer
		 * value should be 'a very large value' (essentially infinite).
		 */
		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		startTimer(T_REJ, llc2Con, llc2Sta);
		/*
		 * 802.2 calls for incrementing retryCount here, but since we
		 * set P_FLAG=0 when Final is received in reject state, the next
		 * time we poll, startTimer(T_P) will set retryCount=0 again.
		 * In-crementing retryCount on reject timer expiration
		 * don't do nuttin'.
		 */
	}
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * rejectSendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
rejectSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	/*
	 * The timer should only expire in this state if the MAC layer was
	 * unable to accept and send an REJ. Had the REJ been sent, the
	 * send acknowledgement parameters and timer would have been cleared.
	 * Do not send an acknowledgement here.
	 */
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	/*
	 * muoe961411 buzz 4/16/96
	 * allow -> allowCount
	 */
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *	AWAIT
 *
 *
 * LLC2 Connection Component Event Functions for AWAIT State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * awaitInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * awaitRcvICmdUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
	} else {
		sendSup(REJ, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}
	startTimer(T_REJ, llc2Con, llc2Sta);
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);

	return (0);
}


/*
 *
 * awaitRcvIRspUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
		startTimer(T_REJ, llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;

			stopTimer(T_P, llc2Con, llc2Sta);
			sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			startTimer(T_REJ, llc2Con, llc2Sta);
			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * remove IF.. ie unconditionally execute tryOutI
			 */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitRcvICmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	link = llc2Con->link;
	sid = llc2Con->sid;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

	mp->b_rptr += sizeof (llc2HdrI_t);

	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT) {
				sendSup(RR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT) {
				sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	}


	return (0);
}


/*
 *
 * awaitRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	int origVs;
	int nr;
	int numOut = 0;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	link = llc2Con->link;
	sid = llc2Con->sid;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

		mp->b_rptr += sizeof (llc2HdrI_t);
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT) {
				sendSup(RR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			clrRemBusy(llc2Con, llc2Sta);
			llc2Con->vs = nr;
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);

			mp->b_rptr += sizeof (llc2HdrI_t);

			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				if (llc2Con->state == CON_AWAIT) {
					numOut = tryOutI(llc2Con, llc2Sta, mac,
								FALSE, FALSE);

					if (numOut <= 0) {
						sendSup(RR, llc2Con, llc2Sta,
							mac, FALSE, FALSE,
							(mblk_t *)0);
					}

					CON_NEXT_STATE(llc2Con, CON_NORMAL);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				/*
				 * The next two cases are deviating from
				 * ISO 8802-2, since what is really happening
				 * is that NORMAL state is being returned to
				 * while at the same time local busy is
				 * detected. Therefore, the resulting state is
				 * BUSY instead of AWAIT_BUSY.
				 * Note that a resend operation is required
				 * also.
				 */
				llc2Con->iRcvd++;
				llc2Con->vr = MOD_128_INCR(llc2Con->vr);

				llc2Con->localBusy++;

				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, (mblk_t *)0);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 0;
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);

				CON_NEXT_STATE(llc2Con, CON_BUSY);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;
				llc2Con->localBusy++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, mp);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 1;	/* data lost */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);

				CON_NEXT_STATE(llc2Con, CON_BUSY);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {

			llc2Con->rrRcvd++;

			freemsg(mp);

			/*
			 * muoe961411 buzz 4 16/96
			 * stop the T_P and T_L2 timers and clear the
			 * P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_NORMAL);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			/*
			 * muoe961411 buzz 4/16/96
			 * Start Recv Not Ready timer if not already running
			 */
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * since V(S) is changed, setResendI is done, but
			 * nothing can be sent since the remote is busy
			 */
			setResendI(llc2Con, origVs, nr);

			CON_NEXT_STATE(llc2Con, CON_NORMAL);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitRcvRejCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uchar_t nr;
	llc2HdrS_t *llc2HdrS;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitRcvRejRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int nr;
	llc2HdrS_t *llc2HdrS;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * muoe961411 buzz 4/16/96
			 * remove IF.. ie unconditionally execute tryOutI
			 */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_NORMAL);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitPollTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->pollTimerExp++;
	/*
	 * muoe961411 buzz 4/16/96
	 * This was lifted out of the "new" version.
	 * Most will not be executed since we will not have the R_flag
	 * (recovery) or the SNA_INN(Frame Relay) on. But it's here if we
	 * ever do
	 */

	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	if ((llc2Con->pollRetryCount > 0) &&
		(llc2Con->flag & T_FLAG) &&
		!(llc2Con->flag & R_FLAG) &&
		(llc2Con->flag & SNA_INN)) {

		/*
		 * This is an SNA INN connection, a null dsap TEST command
		 * was received since the last response to a poll was received,
		 * we are not currently doing INN Re-Route, and at least one
		 * ack or poll retry has failed. Send RESET_IND to the user,
		 * who should start INN Re-Route.
		 */
		/* xferMaxRetry will unlock con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT);

	} else if (llc2Con->pollRetryCount < llc2Con->N2) {

		/* do first level retry */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		ILD_RWUNLOCK(llc2Con->con_lock);
		/*
		 * check if second level retries are exhausted.
		 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
		 */
	} else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;
		ILD_RWUNLOCK(llc2Con->con_lock);

	} else {

		/*
		 * First level retries have been exhausted and either this is a
		 * re-route recovery sequence, or second level retries have also
		 * been exhausted.
		 */
		/* xferMaxRetry will unlock con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT);
	}

	return (0);
}

/*
 *
 * awaitInitiatePFCycle
 *
 */
/*ARGSUSED*/
int
awaitInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->inactTimerExp++;
	startTimer(T_INACT, llc2Con, llc2Sta);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * awaitSendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	sendSup(RR, llc2Con, llc2Sta, mac, llc2Con->rsp, llc2Con->pf,
		(mblk_t *)0);
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}

/*
 *
 * awaitL2TimerExp
 *
 */
/*ARGSUSED*/
int
awaitL2TimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->level2TimerExp++;

	/* begin next set of first level retries */
	sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
	startTimer(T_P, llc2Con, llc2Sta);
	llc2Con->pollRetryCount++;
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *	AWAIT_BUSY
 *
 *
 * LLC2 Connection Component Event Functions for AWAIT_BUSY State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * awaitbusyInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitbusyInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;
	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * awaitbusyLocalBusyClr
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitbusyLocalBusyClr(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	if (llc2Con->dataFlag == 1) {
		sendSup(REJ, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		startTimer(T_REJ, llc2Con, llc2Sta);
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
	} else if (llc2Con->dataFlag == 0) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		CON_NEXT_STATE(llc2Con, CON_AWAIT);
	} else if (llc2Con->dataFlag == BUSY_REJECT) {
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, FALSE, (mblk_t *)0);
		CON_NEXT_STATE(llc2Con, CON_AWAIT_REJECT);
	} else {
		ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	}
	/*
	 * Turn off the Flow Control flag. This will let dlpi start
	 * checking to see if it "canput"
	 */
	if (llc2Con->link) {
		(llc2Con->link)->lk_rnr &= ~LKRNR_FC;
	}

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * awaitbusyRcvICmdUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	uchar_t nr;
	llc2HdrI_t *llc2HdrI;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
	} else {
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}
	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	llc2Con->dataFlag = 1;	/* data lost */

	return (0);
}


/*
 *
 * awaitbusyRcvIRspUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	int nr;
	llc2HdrI_t *llc2HdrI;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		llc2Con->dataFlag = 1;	/* data lost */
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */

			stopTimer(T_P | T_L2, llc2Con, llc2Sta);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);

			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			llc2Con->dataFlag = 1;	/* data lost */
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_BUSY);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitbusyRcvICmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;
	link_t	*link;
	uint_t	sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

	mp->b_rptr += sizeof (llc2HdrI_t);
	link = llc2Con->link;
	sid = llc2Con->sid;

	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
			}
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
			}
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
		}
	}

	return (0);
}


/*
 *
 * awaitbusyRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int retVal = LLC2_GOOD_STATUS;
	llc2HdrI_t *llc2HdrI;
	int origVs;
	int nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

		mp->b_rptr += sizeof (llc2HdrI_t);
		link = llc2Con->link;	/* pull these out before unlocking */
		sid = llc2Con->sid;
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_BUSY) {
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
			}
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			llc2Con->dataFlag = 0;
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->vs = nr;
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);

			mp->b_rptr += sizeof (llc2HdrI_t);
			/* save these before unlocking */
			link = llc2Con->link;
			sid = llc2Con->sid;
			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				llc2Con->dataFlag = 0;
				if (llc2Con->state == CON_AWAIT_BUSY) {
					sendSup(RNR, llc2Con, llc2Sta, mac,
						FALSE, FALSE, (mblk_t *)0);
					(void) tryOutI(llc2Con, llc2Sta, mac,
							FALSE, FALSE);
					CON_NEXT_STATE(llc2Con, CON_BUSY);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				llc2Con->iRcvd++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				llc2Con->dataFlag = 0;
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, mp);
				llc2Con->dataFlag = 1;	/* data lost */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);
				CON_NEXT_STATE(llc2Con, CON_BUSY);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	uchar_t nr;
	llc2HdrS_t *llc2HdrS;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	int nr;
	llc2HdrS_t *llc2HdrS;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rrRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);

			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_BUSY);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the Recv Not Ready timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * since V(S) is changed, setResendI is done, but
			 * nothing can be sent since the remote is busy
			 */
			setResendI(llc2Con, origVs, nr);

			CON_NEXT_STATE(llc2Con, CON_BUSY);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRejCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitbusyRcvRejRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitbusyRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_BUSY);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitbusyPollTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitbusyPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->pollTimerExp++;

	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);

	if ((llc2Con->pollRetryCount > 0) &&
		(llc2Con->flag & T_FLAG) &&
		!(llc2Con->flag & R_FLAG) &&
		(llc2Con->flag & SNA_INN)) {

		/*
		 * This is an SNA INN connection, a null dsap TEST command
		 * was received since the last response to a poll was received,
		 * we are not currently doing INN Re-Route, and at least one
		 * ack or poll retry has failed. Send RESET_IND to the user,
		 * who should start INN Re-Route.
		 */
		/* xferMaxRetry will unlock the con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_BUSY);

	} else if (llc2Con->pollRetryCount < llc2Con->N2) {

		/* do first level retry */
		sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		ILD_RWUNLOCK(llc2Con->con_lock);
		/*
		 * check if second level retries are exhausted.
		 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
		 */
	} else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;

		ILD_RWUNLOCK(llc2Con->con_lock);
	} else {

		/*
		 * First level retries have been exhausted and either this is a
		 * re-route recovery sequence, or second level retries have also
		 * been exhausted.
		 */
		/* xferMaxRetry will unlock the con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_BUSY);
	}
	return (0);
}
/*
 *
 * awaitbusyInitiatePFCycle
 *
 */
/*ARGSUSED*/
int
awaitbusyInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	/*
	 * muoe961411 buzz 4/16/96
	 * New Function
	 */
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->inactTimerExp++;
	startTimer(T_INACT, llc2Con, llc2Sta);
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}


/*
 *
 * awaitbusySendAckTimerExp
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitbusySendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	/*
	 * The timer should only expire in this state if the MAC layer was
	 * unable to accept and send an RNR. Had the RNR been sent, the
	 * send acknowledgement parameters and timer would have been cleared.
	 * Do not send an acknowledgement here, since the other node would take
	 * it as an indication that the busy condition has been cleared.
	 */
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	/*
	 * muoe961411 buzz 4/16/96
	 * allow -> allowCount
	 */
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}

/*
 *
 * awaitbusyL2TimerExp
 *
 */
/*ARGSUSED*/
int
awaitbusyL2TimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t parm1, uintptr_t parm2)
{
	/*
	 * muoe961411 buzz 4/16/96
	 * New Function for 2nd level retries
	 */
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->level2TimerExp++;

	/* begin next set of first level retries */
	sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
	startTimer(T_P, llc2Con, llc2Sta);
	llc2Con->pollRetryCount++;
	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}



/*
 *	AWAIT_REJECT
 *
 *
 * LLC2 Connection Component Event Functions for AWAIT_REJECT State
 *
 * description:
 *	See ISO 8802-2, Section 7.9.2.1.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	origEvent		event indicator(for tracing purposes)
 *	mac		MAC structure ptr
 *	mp		message ptr
 *	loc		local node address structure ptr
 *	rem		remote node address structure ptr
 *	parm1		multi-use parameter 1:
 *				1) poll/final indicator for received PDU events
 *	parm2		multi-use parameter 2:
 *				1) message priority(not currently used)
 *
 * returns:
 *	0		success(if a message was passed in, it has
 *			been handled and does not have to be freed)
 *	non-zero value	the event was not completely handled
 *			(if a message was passed in, it has NOT
 *			been freed)
 */

/*
 *
 * awaitrejectInvalidEvt
 *
 *
 * locks		con_lock locked/unlocked on entry/exit
 *
 */
/*ARGSUSED*/
int
awaitrejectInvalidEvt(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->outOfState++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (LLC2_ERR_OUTSTATE);
}


/*
 *
 * awaitrejectRcvICmdUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvICmdUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->protocolError++;

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
	} else {
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);

	return (0);
}


/*
 *
 * awaitrejectRcvIRspUnexpNs
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvIRspUnexpNs(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	int origVs;
	int nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	if (pf == FALSE) {
		llc2Con->protocolError++;

		freemsg(mp);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->protocolError++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 * and unconditionally call setResendI
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitrejectRcvICmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvICmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	int retVal = LLC2_GOOD_STATUS;
	uchar_t nr;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;

	updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	stopTimer(T_REJ, llc2Con, llc2Sta);

	mp->b_rptr += sizeof (llc2HdrI_t);
	link = llc2Con->link;	/* assign these to temps before unlocking */
	sid = llc2Con->sid;
	retVal = dlpi_canput(mac, link, sid, mp);

	if (pf == FALSE) {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_REJECT) {
				sendSup(RR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_AWAIT);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	} else {
		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_REJECT) {
				sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE,
					(mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_AWAIT);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE,
				(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	}

	return (0);
}


/*
 *
 * awaitrejectRcvIRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvIRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrI_t *llc2HdrI;
	int retVal = LLC2_GOOD_STATUS;
	int origVs;
	int nr;
	int numOut = 0;
	link_t *link;
	uint_t sid;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
	nr = (llc2HdrI->nr)>>1;
	link = llc2Con->link;
	sid = llc2Con->sid;

	if (pf == FALSE) {
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		stopTimer(T_REJ, llc2Con, llc2Sta);

		mp->b_rptr += sizeof (llc2HdrI_t);
		retVal = dlpi_canput(mac, link, sid, mp);

		if (retVal == DLPI_DI_OKAY) {
			llc2Con->iRcvd++;
			UPDATE_COUNTER(llc2Con, framesRcvd, mac);

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			if (llc2Con->state == CON_AWAIT_REJECT) {
				sendSup(RR, llc2Con, llc2Sta, mac, FALSE,
					FALSE, (mblk_t *)0);
				CON_NEXT_STATE(llc2Con, CON_AWAIT);
			}
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else if (retVal == DLPI_DI_BUSY) {
			llc2Con->iRcvd++;
			UPDATE_COUNTER(llc2Con, framesRcvd, mac);
			llc2Con->localBusy++;

			llc2Con->vr = MOD_128_INCR(llc2Con->vr);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE,
				(mblk_t *)0);
			/*
			 * set dataFlag to 0 instead of 2 since the reject
			 * condition has been cleared by the successful
			 * receipt of this I-frame
			 */
			llc2Con->dataFlag = 0;
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
			retVal = dlpi_putnext(mac, link, sid, mp, retVal);
		} else {
			llc2Con->protocolError++;
			llc2Con->localBusy++;

			mp->b_rptr -= sizeof (llc2HdrI_t);
			sendSup(RNR, llc2Con, llc2Sta, mac, FALSE, FALSE, mp);
			llc2Con->dataFlag = 1;	/* data lost */
			CON_NEXT_STATE(llc2Con, CON_AWAIT_BUSY);
		}
	} else {
		if (llc2Con->flag & P_FLAG) {
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P, T_REJ and T_L2 timers and clear the
			 * P_FLAG
			 */
			stopTimer(T_P | T_L2 | T_REJ, llc2Con, llc2Sta);

			clrRemBusy(llc2Con, llc2Sta);
			llc2Con->vs = nr;
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);

			mp->b_rptr += sizeof (llc2HdrI_t);

			retVal = dlpi_canput(mac, link, sid, mp);

			if (retVal == DLPI_DI_OKAY) {
				llc2Con->iRcvd++;
				UPDATE_COUNTER(llc2Con, framesRcvd, mac);

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				if (llc2Con->state == CON_AWAIT_REJECT) {
					/*
					 * muoe961411 buzz 4/16/96
					 * Unconditionally execute tryOutI
					 */
					numOut = tryOutI(llc2Con, llc2Sta, mac,
								FALSE, FALSE);

					if (numOut <= 0) {
						sendSup(RR, llc2Con, llc2Sta,
							mac, FALSE, FALSE,
							(mblk_t *)0);
					}

					CON_NEXT_STATE(llc2Con, CON_NORMAL);
				}
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else if (retVal == DLPI_DI_BUSY) {
				/*
				 * The next two cases are deviating from
				 * ISO 8802-2, since what is really happening
				 * is that NORMAL state is being returned to
				 * while at the same time local busy is
				 * detected.
				 * Therefore, the resulting state is BUSY
				 * instead of AWAIT_BUSY.
				 * Note that a resend operation is required
				 * also.
				 */
				llc2Con->iRcvd++;
				UPDATE_COUNTER(llc2Con, framesRcvd, mac);
				llc2Con->localBusy++;

				llc2Con->vr = MOD_128_INCR(llc2Con->vr);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, (mblk_t *)0);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 0;
				/*
				 * muoe961411 buzz 4/16/96
				 * Unconditionally execute tryOutI
				 */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);

				CON_NEXT_STATE(llc2Con, CON_BUSY);
				retVal = dlpi_putnext(mac, link, sid, mp,
							retVal);
			} else {
				llc2Con->protocolError++;
				llc2Con->localBusy++;

				mp->b_rptr -= sizeof (llc2HdrI_t);
				sendSup(RNR, llc2Con, llc2Sta, mac, FALSE,
					TRUE, mp);
				startTimer(T_P, llc2Con, llc2Sta);
				llc2Con->dataFlag = 1;	/* data lost */
				/*
				 * muoe961411 buzz 4/16/96
				 * Unconditionally execute tryOutI
				 */
				(void) tryOutI(llc2Con, llc2Sta, mac, FALSE,
						FALSE);

				CON_NEXT_STATE(llc2Con, CON_BUSY);
			}
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int nr;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rrRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rrRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 * unconditionally execute tryOutI
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRnrCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRnrCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rnrRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the RcvNotReady timer if not running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the RcvNotReady timer if not running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRnrRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRnrRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int nr;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rnrRcvd++;

		freemsg(mp);
		/*
		 * muoe961411 buzz 4/16/96
		 * Start the RcvNotReady Timer if not already running
		 */
		startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		setRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rnrRcvd++;

			freemsg(mp);

			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			startTimer(T_RNR | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
			setRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			/*
			 * since V(S) is changed, setResendI() is done,
			 * but nothing
			 * can be sent since the remote is busy
			 */
			setResendI(llc2Con, origVs, nr);

			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRejCmd
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRejCmd(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	uchar_t nr;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->rejRcvd++;

	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
		sendSup(RR, llc2Con, llc2Sta, mac, TRUE, TRUE, mp);
	}

	return (0);
}


/*
 *
 * awaitrejectRcvRejRsp
 *
 *
 * locks		con_lock to be held on entry
 *
 */
/*ARGSUSED*/
int
awaitrejectRcvRejRsp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc, dlsap_t *rem,
    uintptr_t pf, uintptr_t parm2)
{
	llc2HdrS_t *llc2HdrS;
	int nr;
	int origVs;

	MP_ASSERT(ILD_LOCKMINE(llc2Con->con_lock));

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);

	origVs = llc2Con->vs;
	llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
	nr = (llc2HdrS->nr)>>1;

	if (pf == FALSE) {
		llc2Con->rejRcvd++;

		freemsg(mp);
		clrRemBusy(llc2Con, llc2Sta);
		updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
	} else {
		if (llc2Con->flag & P_FLAG) {
			llc2Con->rejRcvd++;

			freemsg(mp);
			/*
			 * muoe961411 buzz 4/16/96
			 * stop the T_P and T_L2 timers and clear the P_FLAG
			 * unconditionally execute tryoutI
			 */
			stopTimer(T_P | T_L2, llc2Con, llc2Sta);

			llc2Con->vs = nr;
			clrRemBusy(llc2Con, llc2Sta);
			updateNrRcvd(llc2Con, llc2Sta, mac, nr, FALSE);
			setResendI(llc2Con, origVs, nr);
			(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);

			CON_NEXT_STATE(llc2Con, CON_REJECT);
		} else {
			(void) xferRcvXxxRspBadPF(llc2Con, llc2Sta, mac, mp);
		}
	}
	return (0);
}


/*
 *
 * awaitrejectPollTimerExp
 *
 *
 * locks			no locks set on entry/exit
 *				con_lock locked/unlocked
 */
/*ARGSUSED*/
int
awaitrejectPollTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);
	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->pollTimerExp++;

	UPDATE_COUNTER(llc2Con, t1TimerExp, mac);


	if ((llc2Con->pollRetryCount > 0) &&
		(llc2Con->flag & T_FLAG) &&
		!(llc2Con->flag & R_FLAG) &&
		(llc2Con->flag & SNA_INN)) {

		/*
		 * This is an SNA INN connection, a null dsap TEST command
		 * was received since the last response to a poll was received,
		 * we are not currently doing INN Re-Route, and at least one
		 * ack or poll retry has failed. Send RESET_IND to the user,
		 * who should start INN Re-Route.
		 */
		/* xferMaxRetry will unlock con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_REJECT);

	} else if (llc2Con->pollRetryCount < llc2Con->N2) {

		/*
		 * do first level retry
		 *
		 * We deviate from 802.2 by sending RR instead of REJ here.
		 * Sending a second REJ without proof that the first one was
		 * received could cause multiple retry sequences, which can
		 * cause Invalid N(R).
		 */
		sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
		startTimer(T_P, llc2Con, llc2Sta);
		llc2Con->pollRetryCount++;
		ILD_RWUNLOCK(llc2Con->con_lock);
		/*
		 * check if second level retries are exhausted.
		 * DON'T initiate if R_FLAG(SNA re-route recovery flag) is set.
		 */
	} else if ((llc2Con->pollRetryCountL2 < llc2Con->N2L2) &&
			!(llc2Con->flag & R_FLAG)) {

		/* do second level retry delay */
		llc2Con->pollRetryCount = 0;
		llc2Con->ackRetryCount = 0;

		/*
		 * stop all timers except T_RNR and T_INACT, then start the T_L2
		 * timer and set the P_FLAG
		 */
		stopTimer(T_OTHER, llc2Con, llc2Sta);
		startTimer(T_L2, llc2Con, llc2Sta);
		llc2Con->flag |= P_FLAG;

		llc2Con->pollRetryCountL2++;
		ILD_RWUNLOCK(llc2Con->con_lock);

	} else {

		/*
		 * First level retries have been exhausted and either this is a
		 * re-route recovery sequence, or second level retries have also
		 * been exhausted.
		 */
		/* xferMaxRetry will unlock con_lock */
		xferMaxRetry(llc2Con, llc2Sta, mac,
				DL_DISC_TRANSIENT_CONDITION, CON_AWAIT_REJECT);
	}

	return (0);
}

/*
 *
 * awaitrejectInitiatePFCycle
 *
 */
/*ARGSUSED*/
int
awaitrejectInitiatePFCycle(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	/*
	 * muoe961411 buzz 4 16/96
	 * new function
	 */
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->inactTimerExp++;
	startTimer(T_INACT, llc2Con, llc2Sta);

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}


/*
 *
 * awaitrejectSendAckTimerExp
 *
 *
 * locks	con_lock locked/unlocked
 *
 */
/*ARGSUSED*/
int
awaitrejectSendAckTimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	ILD_WRLOCK(llc2Con->con_lock);

	ild_trace(MID_LLC2, __LINE__, origEvent, llc2Con->sid, 0);
	llc2Con->sendAckTimerExp++;

	/*
	 * The timer should only expire in this state if the MAC layer was
	 * unable to accept and send an REJ. Had the REJ been sent, the
	 * send acknowledgement parameters and timer would have been cleared.
	 * Do not send an acknowledgement here.
	 */
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	/*
	 * muoe961411 buzz 4/16/96
	 * change allow => allowCount
	 */
	llc2Con->allowCount = 0;

	ILD_RWUNLOCK(llc2Con->con_lock);

	return (0);
}
/*
 *
 * awaitrejectL2TimerExp
 *
 */
/*ARGSUSED*/
int
awaitrejectL2TimerExp(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap,
    llc2Sta_t *llc2Sta, uchar_t origEvent, mac_t *mac, mblk_t *mp, dlsap_t *loc,
    dlsap_t *rem, uintptr_t parm1, uintptr_t parm2)
{
	/*
	 * muoe961411 buzz 4/16/96
	 * new function
	 */
	ILD_WRLOCK(llc2Con->con_lock);
	llc2Con->level2TimerExp++;

	/* begin next set of first level retries */
	sendSup(RR, llc2Con, llc2Sta, mac, FALSE, TRUE, (mblk_t *)0);
	startTimer(T_P, llc2Con, llc2Sta);
	llc2Con->pollRetryCount++;

	ILD_RWUNLOCK(llc2Con->con_lock);
	return (0);
}

/*
 *
 * LLC2 SUBROUTINES
 *
 */

/*
 *	disableSap
 *
 *
 * SAP Component and Subsidiary Connection Components Disabler
 *
 * description:
 *	For the given SAP component, send a DM response on all subsidiary
 *	connections and then disable each. This is therefore an abrupt
 *	shutdown of all connections on the SAP.
 *
 *	However, DLPI normally imposes an orderly shutdown of all connections
 *	on a SAP first, such that all connections are disabled before the SAP
 *	is disabled.
 *
 *	Unlink the SAP component from the SAP table contained in the station
 *	component, and free the SAP component.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *
 * locks:		sap_lock locked/unlocked and FREED on exit
 *			con_lock locked/unlocked
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
disableSap(llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta, mac_t *mac)
{
	llc2Con_t *llc2Con;
	dlsap_t *rem;
	uchar_t locSap;
	int i;

	for (i = 0; i < LLC2_MAX_CONS; i++) {
		llc2Con = llc2Sap->conTbl[i];
		if (llc2Con != (llc2Con_t *)0) {
			llc2Sap->conTbl[llc2Con->sid & CON_CID_MASK] =
				(llc2Con_t *)0;
			ILD_WRLOCK(llc2Con->con_lock);
			ILD_RWUNLOCK(llc2Sap->sap_lock);
			rem = &llc2Con->rem;
			locSap = llc2Sap->sap;
			sendDm(mac, llc2Con, rem, locSap, FALSE, (mblk_t *)0);
			/*
			 * moved this from disableCon to here so we could keep
			 * the locking straight
			 */
			/*
			 * disableCon unlocks(and possible frees) the
			 * con_lock
			 */
			disableCon(llc2Con, llc2Sap, llc2Sta, mac);
			ILD_WRLOCK(llc2Sap->sap_lock);
		}
	}
	llc2Sap->state = SAP_INACTIVE;	/* for trace purposes */

	ILD_WRLOCK(Sap_Avail_Lock);
	ADD_DLNKLST(&llc2Sap->chain, &llc2SapAvailHead);
	ILD_RWUNLOCK(Sap_Avail_Lock);
}


/*
 *	enableCon
 *
 *
 * Connection Component Enabler
 *
 * description:
 *	Allocate and initialize a connection component if possible. Add
 *	it to the connection component table in the SAP component and put
 *	it at the front of the connection component search Q.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		ptr to the connection component structure ptr
 *			(so that the connection component can be
 *			made available to the calling routine)
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	rem		remote node address structure ptr
 *
 * locks:	sta_lock locked/unlocked by stopTimer function
 *		sap_lock is locked/unlocked
 *		con_lock is allocated and locked on exit if allocate successful
 *
 * returns:
 *	0			success
 *	LLC2_ERR_SYSERR		failure to allocate a connection component
 */
/*ARGSUSED*/
int
enableCon(llc2Con_t **llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    mac_t *mac, dlsap_t *rem)
{
	int i, j, newconid;
	llc2Con_t *newCon;
	ushort_t sid = 0;
	uint_t tmp_key;
	int retVal = LLC2_GOOD_STATUS;
	llc2Timer_t *llc2Timer;

	ASSERT(mac != NULL);

	/*
	 * muoe971984 buzz 8/27/97
	 * lock the SAP while we look for an empty slot for this new connection
	 */
	ILD_WRLOCK(llc2Sap->sap_lock);

	for (i = 0; i < LLC2_MAX_CONS; i++) {
		if (llc2Sap->conTbl[i] == (llc2Con_t *)0) {
			sid = ((((uint_t)llc2Sap->sap) << 8) | i);
			newconid = i;
			break;
		}
	}

	if (i < LLC2_MAX_CONS) {

		ILD_WRLOCK(Con_Avail_Lock);
		if ((llc2ConAvailHead.flink != &llc2ConAvailHead) &&
			(llc2DontTakeFromAvailList == 0)) {
			newCon = (llc2Con_t *)llc2ConAvailHead.flink;
			RMV_DLNKLST(&newCon->chain);
			ILD_RWUNLOCK(Con_Avail_Lock);
			stopTimer(T_ALL, newCon, llc2Sta);
		} else {
			ILD_RWUNLOCK(Con_Avail_Lock);
			if ((newCon = kmem_zalloc(sizeof (llc2Con_t),
							KM_NOSLEEP)) !=
				(llc2Con_t *)0) {
				ILD_RWALLOC_LOCK(&newCon->con_lock, ILD_LCK4,
							NULL);
			} else {
				llc2Sap->allocFail++;
				ILD_RWUNLOCK(llc2Sap->sap_lock);
				return (LLC2_ERR_SYSERR);
			}
		}

		/*
		 * The connection component search key is composed of the
		 * remote SAP value OR'ed with the last three bytes of the
		 * remote MAC address, which is usually the most unique part
		 * of the MAC address.
		 */
		bcopy((char *)&rem->llc.dl_nodeaddr[3], (char *)&tmp_key,
			sizeof (uint_t));

		newCon->key = (((uint_t)rem->llc.dl_sap) |
				(tmp_key & MAC_UNIQUE_MASK));

		newCon->signature = (uint_t)LLC2_CON_SIGNATURE;

		newCon->stateOldest = CON_ADM;
		newCon->stateOlder = CON_ADM;
		newCon->stateOld = CON_ADM;
		newCon->state = CON_ADM;
		newCon->ppa = mac->mac_ppa;
		newCon->sid = sid;
		newCon->link = (link_t *)0;
		newCon->llc2Sap = llc2Sap;
		/*
		 * muoe961411 buzz 4/16/96
		 * copy _all_ the remote address information, including the
		 * routing stuff, but ZAP the Broadcast bits in the Route
		 * From this point on, we _DO NOT_ want to use any form of
		 * Broadcast
		 */

		/* save all of dlsap of remote station */
		newCon->rem = *rem;

		newCon->flag = 0;
		newCon->dataFlag = 0;
		newCon->rsp = FALSE;

		newCon->pf = FALSE;
		newCon->kAck = 0;
		newCon->k = (uchar_t)llc2Sta->xmitWindowSz;
		newCon->kMax = (uchar_t)llc2Sta->xmitWindowSz;

		newCon->vs = 0;
		newCon->vsMax = 0;
		newCon->vr = 0;
		newCon->nrRcvd = 0;

		newCon->recoverState = CON_ADM;
		newCon->frmrRcvdFlags = 0;
		newCon->allow = llc2Sta->sendAckAllow;
		newCon->allowCount = 0;

		newCon->N2 = llc2Sta->maxRetry;
		newCon->pollRetryCount = 0;
		newCon->ackRetryCount = 0;

		newCon->N2L2 = llc2Sta->maxRetryL2;
		newCon->pollRetryCountL2 = 0;
		newCon->ackRetryCountL2 = 0;

		newCon->unackHead.flink = &newCon->unackHead;
		newCon->unackHead.blink = &newCon->unackHead;

		newCon->resendHead.flink = &newCon->resendHead;
		newCon->resendHead.blink = &newCon->resendHead;

		newCon->xoffEntry.chain.flink = (dLnkLst_t *)0;
		newCon->xoffEntry.chain.blink = (dLnkLst_t *)0;
		newCon->xoffEntry.conptr = newCon;

		newCon->busyEntry.chain.flink = (dLnkLst_t *)0;
		newCon->busyEntry.chain.blink = (dLnkLst_t *)0;
		newCon->busyEntry.conptr = newCon;


		newCon->macOutSave = 0;
		newCon->macOutDump = 0;

		newCon->frmrInfo.c1 = 0;
		newCon->frmrInfo.c2 = 0;
		newCon->frmrInfo.vs = 0;
		newCon->frmrInfo.vr = 0;
		newCon->frmrInfo.flag = 0;
		newCon->framesSent = 0;
		newCon->framesRcvd = 0;
		newCon->framesSentError = 0;
		newCon->framesRcvdError = 0;
		newCon->t1TimerExp = 0;
		newCon->cmdLastSent = 0;
		newCon->cmdLastRcvd = 0;

		llc2Timer = llc2TimerArray[llc2Sta->mac->mac_ppa];
		ASSERT(llc2Timer != NULL);

		/*
		 * muoe961411 buzz 4/16/96
		 * New timer scheme requires different initialization
		 */
		for (i = 0; i < T_NUM_TIMERS; i++) {
			newCon->timerEntry[i].chain.flink = (dLnkLst_t *)0;
			newCon->timerEntry[i].chain.blink = (dLnkLst_t *)0;
			newCon->timerEntry[i].expTime = 0;
			newCon->timerEntry[i].timerInt = llc2Timer->timerInt[i];
			newCon->timerEntry[i].conptr = newCon;
			newCon->timerEntry[i].mac = mac;
			newCon->timerEntry[i].sid = sid;
		}

		newCon->iSent = 0;
		newCon->iRcvd = 0;
		newCon->frmrSent = 0;
		newCon->frmrRcvd = 0;
		newCon->rrSent = 0;
		newCon->rrRcvd = 0;
		newCon->rnrSent = 0;
		newCon->rnrRcvd = 0;
		newCon->rejSent = 0;
		newCon->rejRcvd = 0;
		newCon->sabmeSent = 0;
		newCon->sabmeRcvd = 0;
		newCon->uaSent = 0;
		newCon->uaRcvd = 0;
		newCon->discSent = 0;

		newCon->outOfState = 0;
		newCon->allocFail = 0;
		newCon->protocolError = 0;
		newCon->localBusy = 0;
		newCon->remoteBusy = 0;
		newCon->maxRetryFail = 0;

		newCon->ackTimerExp = 0;
		newCon->pollTimerExp = 0;
		newCon->rejTimerExp = 0;
		newCon->remBusyTimerExp = 0;
		newCon->inactTimerExp = 0;
		newCon->sendAckTimerExp = 0;

		/*
		 * Clean up any previous vestiges of former connections in the
		 * table. There really shouldn't be any, but just in case...
		 */
		for (j = 0; j < LLC2_MAX_CONS; j++) {
			if (llc2Sap->conTbl[j] == newCon) {
				llc2Sap->conTbl[j] = NULL;
			}
		}
		llc2Sap->conTbl[newconid] = newCon;
		ADD_DLNKLST(&newCon->chain, &llc2Sap->conHead);
		*llc2Con = newCon;

		/* Return the new con to the user locked */
		ILD_WRLOCK(newCon->con_lock);
		ILD_RWUNLOCK(llc2Sap->sap_lock);
	} else {
		/* muoe971984 buzz 8/27/97 */
		llc2Sap->allocFail += 1;
		ILD_WRLOCK(llc2Sap->sap_lock);
		retVal = LLC2_ERR_SYSERR;
	}


	return (retVal);
}


/*
 *	resetCon
 *
 *
 * Connection Component Reset Handler
 *
 * description:
 *	Clear the unacknowledged message and resend message Qs.
 *	Terminate a resend pending condition and a send acknowledgement
 *	pending condition. Cause all outbound messages currently held
 *	by the MAC layer to be dumped when returned to LLC2.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *
 * locks		con_lock set on entry/exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
resetCon(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta)
{
	mblk_t *mp;


	while (llc2Con->unackHead.flink != &llc2Con->unackHead) {
		mp = (mblk_t *)llc2Con->unackHead.flink;
		RMV_DLNKLST((dLnkLst_t *)mp);
		freemsg(mp);
	}

	while (llc2Con->resendHead.flink != &llc2Con->resendHead) {
		mp = (mblk_t *)llc2Con->resendHead.flink;
		RMV_DLNKLST((dLnkLst_t *)mp);
		freemsg(mp);
	}

	stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
	llc2Con->rsp = FALSE;
	llc2Con->pf = FALSE;
	llc2Con->allowCount = 0;
}


/*
 *	disableCon
 *
 *
 * Connection Component Disabler
 *
 * description:
 *	Stop any running timers for the connection. Unlink the connection
 *	component from the SAP(search Q and connection table).
 *	Clear the unacknowledged message and resend message Qs.
 *	Cause all outbound messages currently held by the MAC layer to
 *	be dumped when returned to LLC2. If there are no messages held by
 *	the MAC layer, or the LLC2 STREAMS device is no longer open, the
 *	connection component is freed. Otherwise, its state is set to ADM
 *	and it is placed on the llc2ConAvailHead Q to be freed when all
 *	messages are returned from the MAC layer. While on this Q, the
 *	connection component is NOT available to LLC2 state machine processing.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sap		SAP component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *
 * locks:		con_lock locked on entry
 *			unlocked(and possibly freed) on exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
disableCon(llc2Con_t *llc2Con, llc2Sap_t *llc2Sap, llc2Sta_t *llc2Sta,
    mac_t *mac)
{
	mblk_t *mp;
	int newipl;

	stopTimer(T_ACK | T_P | T_REJ | T_REM_BUSY | T_INACT | T_SEND_ACK |
			T_L2 | T_RNR, llc2Con, llc2Sta);


	/*
	 * muoe971984 buzz 8/27/97
	 * Go ahead and change the state here in case we have to unlock
	 * the Con to keep the locking order straight before removing
	 * the structures from the Sap's chain.
	 * The "normal" order for acquiring these locks are Sap then Con, but
	 * we enter this routine with the Con already locked. So, first
	 * TRY for the Sap lock. If we get it, fine...go ahead clean up
	 * and be done with it. If we can't get it, go ahead and release
	 * the CON(we've already changed the state, so nobody should be mucking
	 * with it) and then acquire the Sap and Con in the "normal" order
	 */
	CON_NEXT_STATE(llc2Con, CON_ADM);
	llc2Con->signature = 0;	/* for sid validation purposes */

	ILD_TRYWRLOCK(llc2Sap->sap_lock, newipl);
	if (newipl == 0) {
		/*
		 * Couldn't get the SAP lock, so let's back ourselves out
		 * and acquire them in the "correct" order.
		 * After dequeing, finish the cleanup operations.
		 */
		ILD_RWUNLOCK(llc2Con->con_lock);
		ILD_WRLOCK(llc2Sap->sap_lock);
		ILD_WRLOCK(llc2Con->con_lock);
	}

	llc2Sap->conTbl[llc2Con->sid & CON_CID_MASK] = (llc2Con_t *)0;
	if (llc2Con->chain.flink != (dLnkLst_t *)0) {
		RMV_DLNKLST(&llc2Con->chain);
	}
	ILD_RWUNLOCK(llc2Sap->sap_lock);
	if (llc2Con->busyEntry.chain.flink != (dLnkLst_t *)0) {
		RMV_DLNKLST((dLnkLst_t *)&llc2Con->busyEntry);
	}

	if (llc2Con->xoffEntry.chain.flink != (dLnkLst_t *)0) {
		RMV_DLNKLST((dLnkLst_t *)&llc2Con->xoffEntry);
	}

	while (llc2Con->unackHead.flink != &llc2Con->unackHead) {
		mp = (mblk_t *)llc2Con->unackHead.flink;
		RMV_DLNKLST((dLnkLst_t *)mp);
		freemsg(mp);
	}

	while (llc2Con->resendHead.flink != &llc2Con->resendHead) {
		mp = (mblk_t *)llc2Con->resendHead.flink;
		RMV_DLNKLST((dLnkLst_t *)mp);
		freemsg(mp);
	}

	ILD_WRLOCK(Con_Avail_Lock);
	/*
	 * sturblen muoe980786  09/28/98 (upport by buzz)
	 * if REMOTE_BUSY is set we must be sure to
	 * enable the ild queue when removing llc2Con
	 */
	if ((llc2Con->flag & REMOTE_BUSY) && llc2Con->link &&
		llc2Con->link->lk_mac) {
		ild_trace(MID_LLC2, __LINE__, (uintptr_t)llc2Con,
				(uintptr_t)llc2Con->link, 0);
		dlpi_XON(llc2Con->link->lk_mac, llc2Con->link);
	}
	ADD_DLNKLST(&llc2Con->chain, &llc2ConAvailHead);
	ILD_RWUNLOCK(Con_Avail_Lock);
	ILD_RWUNLOCK(llc2Con->con_lock);
}


/*
 *	setRemBusy
 *
 *
 * SET_REMOTE_BUSY Connection Component Action
 *
 * description:
 *	If the REMOTE_BUSY flag is not set, set it, start the remote
 *	BUSY_TIMER, and log a statistic. Otherwise, start the
 *	remote BUSY_TIMER if it is not running.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *
 * locks		con_lock set on entry/exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
setRemBusy(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta)
{
	if (!(llc2Con->flag & REMOTE_BUSY)) {
		llc2Con->flag |= REMOTE_BUSY;
		startTimer(T_REM_BUSY, llc2Con, llc2Sta);
		llc2Con->remoteBusy++;
	} else {
		/*
		 * muoe961411 buzz 4/16/96
		 * Change the way NOT RUNNING is specified(ie now OR in the
		 * bit)
		 */
		startTimer(T_REM_BUSY | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
	}

}


/*
 *	clrRemBusy
 *
 *
 * CLEAR_REMOTE_BUSY Connection Component Action
 *
 * description:
 *	If the REMOTE_BUSY flag is set, clear it and stop the remote
 *	BUSY_TIMER.
 *
 *	updateNrRcvd() is always called when clrRemBusy() is, and will
 *	be used to start the(re)sending of I-frames.
 *
 *	NOTE:
 *	Always do CLEAR_REMOTE_BUSY and UPDATE_N(R)_RECEIVED, if
 *	specified, before a(re)send operation so that the REMOTE_BUSY
 *	flag does not block the send and the send window is as large
 *	as possible.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *
 * locks		con_lock set on entry/exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
clrRemBusy(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta)
{
	if (llc2Con->flag & REMOTE_BUSY) {
		llc2Con->flag &= ~REMOTE_BUSY;
		/*
		 * muoe961411 buzz 4/16/96
		 * stop BUSY and RNR timers
		 */
		stopTimer(T_REM_BUSY | T_RNR, llc2Con, llc2Sta);
		/*
		 * buzz muoe971965 08/08/97
		 * need to ensure ild write queue is always
		 * enabled when the REMOTE_BUSY flag is turned off
		 */
		if (llc2Con->link && llc2Con->link->lk_mac) {
			ild_trace(MID_LLC2, __LINE__, (uintptr_t)llc2Con,
					(uintptr_t)llc2Con->link, 0);
			dlpi_XON(llc2Con->link->lk_mac, llc2Con->link);
		}
	}

}


/*
 *	updateNrRcvd
 *
 *
 * UPDATE_N(R)_RECEIVED Connection Component Action
 *
 * description:
 *	The N(R) (shifted right once) passed to this function has already
 *	been validated by llc2StateMachine(). It is compared to the local
 *	record of N(R)_RECEIVED to determine if any previously unacknowledged
 *	I-frames are being acknowledged. If so, the local record of
 *	N(R)_RECEIVED is updated and RETRY_COUNT is set to zero. If there
 *	are still unacknowledged I-frames the acknowledgement timer is
 *	re-started. Otherwise, it is stopped. Messages that have been
 *	acknowledged are removed from the unacknowledged messages Q or the
 *	resend messaged Q, and freed.
 *
 *	If the caller indicates that a send I-frame operation should be
 *	done, it is then attempted.
 *
 *	It is possible that some(or all) of the acknowledged messages are
 *	on the resend messages Q. This can happen if our poll timer value
 *	is shorter than the worst case transmission delay, e.g., over a
 *	Frame Relay network or with Token Ring bridges and routers. We
 *	timed out after sending a poll command, retried the poll, then
 *	received the final response from the first poll. We then sent
 *	one(or more) I-frames, sent a third poll, and received the final
 *	response from the second poll. We treated that n(r) as a nack,
 *	and moved all of the I-frames from the unacknowledged Q to the
 *	resend Q. However, those frames were received OK by our connection
 *	partner who has now sent us this n(r) acknowledging some(or all)
 *	of the I-frames.
 *
 *	NOTES:
 *	Always do CLEAR_REMOTE_BUSY and UPDATE_N(R)_RECEIVED, if
 *	specified, before a(re)send operation so that the REMOTE_BUSY
 *	flag does not block the send and the send window is as large
 *	as possible.
 *
 *	If the V(S):=N(R) reset action must be done in a connection
 *	component state/event function, make sure it is done before
 *	UPDATE_N(R)_RECEIVED so that updateNrRcvd() handles starting
 *	and stopping the acknowledgement timer properly.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	nr		N(R) >> 1(SHIFT OUT THE P/F BIT BEFORE
 *			CALLING updateNrRcvd())
 *	send		TRUE ==> (re)send I-frames
 *			FALSE ==> do not(re)send I-frames
 *
 * locks		con_lock locked on entry/exit
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
updateNrRcvd(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, uchar_t nr,
    int send)
{
	dLnkLst_t *msg;

	ild_trace(MID_LLC2, __LINE__, nr, llc2Con->sid, 0);

	if (nr != llc2Con->nrRcvd) {
		llc2Con->nrRcvd = nr;
		llc2Con->ackRetryCount = 0;
		llc2Con->ackRetryCountL2 = 0;
		if (nr != llc2Con->vs) {
			startTimer(T_ACK, llc2Con, llc2Sta);
		} else {
			/*
			 * N(R) = V(S) means either all outstanding frames
			 * are acked, or caller has set V(S) = N(R) in
			 * preparation for a resend  operation; in either case,
			 * stop the acknowledgement timer.
			 *
			 * NOTE that we get here for this second case only if
			 * this is a partial ack(at least one frame is acked);
			 * the case of all outstanding frames being nacked is
			 * NOT covered, so the ack timer would be left running.
			 */
			stopTimer(T_ACK, llc2Con, llc2Sta);
		}
		/*
		 * Remove messages. Start with the unacknowledged message Q and
		 * continue with the resend Q(if necessary), until the
		 * message with send sequence number one less than nr received
		 * has been removed.
		 */
		msg = llc2Con->unackHead.flink;
		while (msg != &llc2Con->unackHead) {
			llc2HdrI_t *llc2HdrI;
			llc2HdrI = (llc2HdrI_t *)(((mblk_t *)msg)->b_rptr);
			if (nr == ((llc2HdrI->ns)>>1)) {
				break;
			}
			RMV_DLNKLST(msg);
			freemsg((mblk_t *)msg);
			llc2Con->kAck++;

			msg = llc2Con->unackHead.flink;
		}

		/*
		 * continue with the resend Q, if last message acked not found
		 * yet
		 */
		if (msg == &llc2Con->unackHead) {
			msg = llc2Con->resendHead.flink;
			while (msg != &llc2Con->resendHead) {
				llc2HdrI_t *llc2HdrI;
				llc2HdrI = (llc2HdrI_t *)(((mblk_t *)
								msg)->b_rptr);
				if (nr == ((llc2HdrI->ns)>>1)) {
					break;
				}
				RMV_DLNKLST(msg);
				freemsg((mblk_t *)msg);
				llc2Con->kAck++;

				msg = llc2Con->resendHead.flink;
			}
		}

	}
	/*
	 * muoe961411 buzz 4/16/96
	 * Add dynamic windowing algorithm. This allows better congestion
	 * control with fewer re-transmitted frames
	 */
	/*
	 * Dynamic Window Algorithm: Increment the window size by one if
	 * k < kMax, and k frames have been acked since k was last changed.
	 * k frames acked is an indication that congestion no longer exists.
	 */
	if ((llc2Con->k < llc2Con->kMax) && (llc2Con->kAck >= llc2Con->k)) {
		llc2Con->k++;
		llc2Con->kAck = 0;
	}

	if (send == TRUE) {
		(void) tryOutI(llc2Con, llc2Sta, mac, FALSE, FALSE);
	}

}


/*
 *	sendAck
 *
 *
 * SEND_ACKNOWLEDGE_XXX Connection Component Action
 * SEND_ACKNOWLEDGE_RSP(F=1) Connection Component Action
 *
 * description:
 *	The SEND_ACKNOWLEDGE_XXX and SEND_ACKNOWLEDGE_RSP(F=1) connection
 *	component actions are both handled by this function. They differ
 *	in that the second action produces an immediate acknowledgement
 *	while the first action is not required to produce an immediate
 *	acknowledgement. The SEND_ACKNOWLEDGE_CMD(P=1) is not implemented,
 *	since this version of LLC2 never does an acknowledgement using a
 *	command PDU with the poll bit set.
 *
 *	SEND_ACKNOWLEDGE_XXX:
 *
 *	The SEND_ACKNOWLEDGE_XXX action is used whenever an immediate
 *	acknowledgement of a received I-frame is not required of the local
 *	node. The decision as to when to send an acknowledgement to the
 *	remote node when an immediate acknowledgement is not required is
 *	controlled by sendAck().
 *
 *	When the first unacknowledged I-frame is received, the send
 *	acknowledgement timer is started and the connection component
 *	"allowCount" field is set equal to a proportion of the receive
 *	window size. Based on the parameters passed in, the "rsp" and "pf"
 *	fields are also set. Subsequently received I-frames cause the
 *	"allowCount" field to be decremented. When either the "allowCount"
 *	field is equal to 1 during this function's processing, or the timer
 *	expires, an S-frame acknowledgement is sent to the remote node with
 *	the command/response and poll/final bits set according to the "rsp"
 *	and "pf" indicators.
 *
 *	The send acknowledgement timer interval is set to be shorter than
 *	the time interval set by the remote node in which it expects to
 *	receive an acknowledgement.
 *
 *	Any command or response sent by the local node acts as an
 *	acknowledgement. Therefore, all the send functions clear the
 *	"allowCount", "rsp", and "pf" fields and stop the send acknowledgement
 *	timer when they successfully send a PDU.
 *
 *	SEND_ACKNOWLEDGE_RSP(F=1):
 *
 *	If the "rsp" and "pf" parameters passed to sendAck() indicate an
 *	acknowledgement response with the final bit set is required, an
 *	RR response is sent immediately if an I-frame is not available to be
 *	sent. If a re-usable message is passed to sendAck(), it is used here
 *	to generate the RR PDU.
 *
 *	SEND_ACKNOWLEDGE_CMD(P=1):
 *
 *	WARNING!: This combination(rsp=FALSE, pf=TRUE) should NEVER be used.
 *	If we were to send RR with P=1 and remain in normal state(an 802.2
 *	option), that could result in two F/P checkpoint retry sequences at
 *	once, which can result in Invalid N(R).
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	rsp		response/request indicator
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr(used to generate an
 *			immediate RR response, if necessary)
 *
 *	locks		con_lock locked on entry/exit
 *
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
sendAck(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int rsp, int pf,
    mblk_t *mpReuse)
{
	/*
	 * If the send ACK timer is not started, it is started regardless of
	 * the type of acknowledgement requested. Then, if the MAC layer
	 * cannot successfully send an immediate acknowledgement for the
	 * SEND_ACKNOWLEDGEMENT_RSP(F=1) action, there is still a chance it
	 * can be sent when the timer expires.
	 *
	 * For a SEND_ACKNOWLEDGE_XXX action, the "allowCount" parameter needs
	 * to be set when the timer needs to be started, since it indicates that
	 * this is the first unacknowledged message. The "allowCount" parameter
	 * is set equal to 3/4 of the receive window size. The remote node send
	 * window size is probably equal to the local node receive window size
	 * as a result of the exchange of XIDs. The "allowCount" field is set
	 * at a proportion of this size to start an acknowledgement on the way
	 * back to the remote node before the remote node reaches its send
	 * window limit.
	 */

	if (llc2Con->timerEntry[T_SEND_ACK_INDEX].chain.flink ==
		(dLnkLst_t *)0) {
		startTimer(T_SEND_ACK, llc2Con, llc2Sta);
		llc2Con->allowCount = llc2Con->allow;
	}

	/*
	 * Multiple sendAck() calls may be made before an acknowledgement is
	 * issued. The newest rsp and pf values will be used unless a response
	 * with the final bit set was previously requested and hasn't been
	 * successfully sent. Those settings will take priority over any new
	 * settings.
	 */
	if (!((llc2Con->rsp == TRUE) && (llc2Con->pf == TRUE))) {
		llc2Con->rsp = (uchar_t)rsp;
		llc2Con->pf = (uchar_t)pf;
	}
	/*
	 * Test if an acknowledgement should be sent now.
	 *
	 * If allowCount = 1, don't decrement it so that if there is an SAMsend
	 * error, allowCount is still positive to indicate that the rsp and pf
	 * values are still relevant.
	 */
	if ((llc2Con->allowCount <= 1) ||
		((llc2Con->rsp == TRUE) && (llc2Con->pf == TRUE))) {
		/*
		 * If an acknowledgement must be sent, try to send an I-frame.
		 * If this is successful, free the re-usable message if one was
		 * passed in.
		 *
		 * Otherwise, generate an RR using the re-usable message if one
		 * was passed in.
		 */
		if (tryOutI(llc2Con, llc2Sta, mac, llc2Con->rsp, llc2Con->pf) >
			0) {
			if (mpReuse != (mblk_t *)0) {
				freemsg(mpReuse);
			}
		} else {
			/* See WARNING in the routine description, above */
			sendSup(RR, llc2Con, llc2Sta, mac,
				llc2Con->rsp, llc2Con->pf, mpReuse);
		}
	} else {
		/*
		 * If an acknowledgement is not needed at this time, free the
		 * re-usable message if it was passed in and decrement the
		 * allowCount. Try to send an I-frame.
		 */
		if (mpReuse != (mblk_t *)0) {
			freemsg(mpReuse);
		}

		--llc2Con->allowCount;

		(void) tryOutI(llc2Con, llc2Sta, mac, llc2Con->rsp,
				llc2Con->pf);
	}

}


/*
 *	sendSabme
 *
 *
 * Send SABME Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	If the SAMsend is successful, clear the send acknowledgement
 *	parameters and timer since this PDU serves as an acknowledgement.
 *
 *	Start the inactivity timer since a command PDU has been sent.
 *
 *	WARNING:
 *	The connection component passed in must have the "rem" and "sid"
 *	fields set up.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr
 *
 * locks:		con_lock locked on entry/exit
 *
 *
 * returns:
 *	nothing		success
 */
/*ARGSUSED*/
void
sendSabme(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int pf,
    mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrU_t *llc2HdrU;
	int mac_hdr_len;	/* buzz 03/29/96 */

	/* Initial header length */

	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {

		/*
		 * muoe961411 buzz 4/16/96
		 * Skip over the MAC header and any source route bridging
		 * (SRB) information
		 */
		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;

		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
		llc2HdrU->dsap = llc2Con->rem.llc.dl_sap;
		llc2HdrU->ssap = (llc2Con->sid)>>8;
		llc2HdrU->ssap &= ~LLC_RESPONSE;
		llc2HdrU->control = SABME;
		if (pf == TRUE) {
			llc2HdrU->control |= P_F;
		}
		llc2Con->cmdLastSent = llc2HdrU->control;

		if (SAMsend(mac, &llc2Con->rem, sizeof (llc2HdrU_t),
					mp, 0, (void *)0) == 0) {
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			startTimer(T_INACT, llc2Con, llc2Sta);

			llc2Con->sabmeSent++;
		}
	} else {
		llc2Con->allocFail++;
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}

}


/*
 *	sendUa
 *
 *
 * Send UA Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	If the SAMsend is successful, clear the send acknowledgement
 *	parameters and timer since this PDU serves as an acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr
 *
 * returns:
 *	0			success
 *	LLC2_ERR_SYSERR		failure to allocate or send a UA response
 *
 * locks:		con_lock locked on entry/exit
 *
 */
/*ARGSUSED*/
int
sendUa(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int pf,
    mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrU_t *llc2HdrU;
	int retVal = LLC2_GOOD_STATUS;
	int mac_hdr_len;	/* buzz 03/29/96 */

	/* Initial header length */

	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {

		/*
		 * muoe961411 buzz 4/16/96
		 * Add in any source route bridging(SRB) information
		 */
		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;

		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
		llc2HdrU->dsap = llc2Con->rem.llc.dl_sap;
		llc2HdrU->ssap = (llc2Con->sid)>>8;
		llc2HdrU->ssap |= LLC_RESPONSE;
		llc2HdrU->control = UA;
		if (pf == TRUE) {
			llc2HdrU->control |= P_F;
		}
		llc2Con->cmdLastSent = llc2HdrU->control;

		if (SAMsend(mac, &llc2Con->rem, sizeof (llc2HdrU_t),
					mp, 0, (void *)0)) {
			retVal = LLC2_ERR_SYSERR;
		} else {
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			llc2Con->uaSent++;
		}
	} else {
		llc2Con->allocFail++;
		retVal = LLC2_ERR_SYSERR;
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}
	return (retVal);
}


/*
 *	sendSup
 *
 *
 * Send RR, RNR, and REJ Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	If the SAMsend is successful, clear the send acknowledgement
 *	parameters and timer since this PDU serves as an acknowledgement.
 *
 *	Start the inactivity timer if a command PDU has been sent.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	type		RR, RNR, or REJ
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	rsp		response/request indicator
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr
 *
 *
 * locks:		con_lock locked on entry/exit
 *
 * returns:		nothing.
 */
/*ARGSUSED*/
void
sendSup(uchar_t type, llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac,
    int rsp, int pf, mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrS_t *llc2HdrS;
	int mac_hdr_len;	/* buzz 03/29/96 */


	/* Initial header length */
	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrS_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {
		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;

		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrS_t);
		llc2HdrS = (llc2HdrS_t *)mp->b_rptr;
		llc2HdrS->dsap = llc2Con->rem.llc.dl_sap;
		llc2HdrS->ssap = ((llc2Con->sid)>>8) & 0xfe;
		if (rsp == TRUE) {
			llc2HdrS->ssap |= LLC_RESPONSE;
		}
		llc2HdrS->control = type;
		llc2HdrS->nr = (llc2Con->vr)<<1;
		if (pf == TRUE) {
			llc2HdrS->nr |= SI_P_F;
		}
		/*
		 * muoe961411 buzz 4/16/96
		 * Save last command sent
		 */
		llc2Con->cmdLastSent = llc2HdrS->control;

		if (SAMsend(mac, &llc2Con->rem, sizeof (llc2HdrS_t),
					mp, 0, (void *)0) == 0) {
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			if (rsp == FALSE) {
				startTimer(T_INACT, llc2Con, llc2Sta);
			}

			if (type == RR) {
				llc2Con->rrSent++;
			} else if (type == RNR) {
				llc2Con->rnrSent++;
			} else if (type == REJ) {
				llc2Con->rejSent++;
			}
		}

	} else {
		llc2Con->allocFail++;
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}
}


/*
 *	sendFrmr
 *
 *
 * Send FRMR Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	If the SAMsend is successful, clear the send acknowledgement
 *	parameters and timer since this PDU serves as an acknowledgement.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	pf		poll/final indicator
 *	info		FRMR information structure ptr
 *	mpReuse		re-usable message ptr
 *
 * locks:		con_lock locked on entry/exit
 *			unlocked for SAMsend call
 *
 * returns:
 *	nothing
 */
/*ARGSUSED*/
void
sendFrmr(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int pf,
    llc2FrmrInfo_t *info, mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrFrmr_t *llc2HdrFrmr;
	int mac_hdr_len;	/* buzz 03/29/96 */

	/* Initial header length */
	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrFrmr_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {

		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;

		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrFrmr_t);
		llc2HdrFrmr = (llc2HdrFrmr_t *)mp->b_rptr;
		llc2HdrFrmr->dsap = llc2Con->rem.llc.dl_sap;
		llc2HdrFrmr->ssap = (llc2Con->sid)>>8;
		llc2HdrFrmr->ssap |= LLC_RESPONSE;
		llc2HdrFrmr->control = FRMR;
		if (pf == TRUE) {
			llc2HdrFrmr->control |= P_F;
		}
		llc2HdrFrmr->s.c1 = info->c1;
		llc2HdrFrmr->s.c2 = info->c2;
		llc2HdrFrmr->s.vs = info->vs;
		llc2HdrFrmr->s.vr = info->vr;
		llc2HdrFrmr->s.flag = info->flag;

		llc2Con->cmdLastSent = llc2HdrFrmr->control;

		if (SAMsend(mac, &llc2Con->rem, sizeof (llc2HdrFrmr_t), mp,
					0, (void *)0) == 0) {
			/* Good Send */
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			llc2Con->frmrSent++;
		}

	} else {
		llc2Con->allocFail++;
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}

}


/*
 *	sendDisc
 *
 *
 * Send DISC Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	If the SAMsend is successful, clear the send acknowledgement
 *	parameters and timer since this PDU serves as an acknowledgement.
 *
 *	Start the inactivity timer since a command PDU has been sent.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr
 *
 * locks:		con_lock locked on entry/exit
 *
 * returns:
 *	0			success
 *	LLC2_ERR_SYSERR		failure to allocate or send a DISC command
 */
/*ARGSUSED*/
void
sendDisc(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int pf,
    mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrU_t *llc2HdrU;
	int mac_hdr_len;	/* buzz 03/29/96 */

	/* Initial header length */
	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {

		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;
		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
		llc2HdrU->dsap = llc2Con->rem.llc.dl_sap;
		llc2HdrU->ssap = (llc2Con->sid)>>8;
		llc2HdrU->ssap &= ~LLC_RESPONSE;
		llc2HdrU->control = DISC;
		if (pf == TRUE) {
			llc2HdrU->control |= P_F;
		}
		llc2Con->cmdLastSent = llc2HdrU->control;

		if (SAMsend(mac, &llc2Con->rem, sizeof (llc2HdrU_t),
					mp, 0, (void *)0) == 0) {
			/* Good Send */
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			startTimer(T_INACT, llc2Con, llc2Sta);

			llc2Con->discSent++;
		}

	} else {
		llc2Con->allocFail++;
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}
}


/*
 *	sendDm
 *
 *
 * Send DM Handler
 *
 * description:
 *	If a re-usable message is passed in, adjust it to be used to send
 *	the PDU if possible. Otherwise, allocate a new message for
 *	the purpose.
 *
 *	The connection component structure pointer is not passed in since
 *	a DM response may be issued in ADM state, when no connection
 *	component is allocated. Therefore, the remote node address
 *	structure and local SAP value must be passed in.
 *
 *	The send acknowledgement parameters and timer do not need to be
 *	cleared, nor are statistics recorded, since the connection
 *	component will not be available after the DM response is issued.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	mac		MAC structure ptr
 *	llc2Con		Connection Structure ptr(for unlocking only)
 *	rem		remote node address structure ptr
 *	locSap		local SAP value
 *	pf		poll/final indicator
 *	mpReuse		re-usable message ptr
 *
 *
 * locks:	con_lock locked on entry/exit(Most of the time)
 *		There is only one case from admRcvDiscCmd
 *		that this routine will be called with
 *		NO LOCKS set (llc2Con will be NULL).
 *
 * returns:
 *	0			success
 *	LLC2_ERR_SYSERR		failure to allocate or send a DM response
 */
/*ARGSUSED*/
void
sendDm(mac_t *mac, llc2Con_t *llc2Con,
	/* this may occaisionally be NULL */ dlsap_t *rem, uchar_t locSap,
	int pf, mblk_t *mpReuse)
{
	mblk_t *mp = (mblk_t *)0;
	llc2HdrU_t *llc2HdrU;
	int mac_hdr_len;	/* buzz 03/29/96 */

	/* Initial header length */
	mac_hdr_len = mac->mac_hdr_sz + sizeof (llc2HdrU_t);

	if ((mp = check_if_reusable(mpReuse, mac_hdr_len)) != NULL) {
		mp->b_rptr = mp->b_datap->db_base + mac->mac_hdr_sz;

		mp->b_wptr = mp->b_rptr + sizeof (llc2HdrU_t);
		llc2HdrU = (llc2HdrU_t *)mp->b_rptr;
		llc2HdrU->dsap = rem->llc.dl_sap;
		llc2HdrU->ssap = locSap;
		llc2HdrU->ssap |= LLC_RESPONSE;
		llc2HdrU->control = DM;
		if (pf == TRUE) {
			llc2HdrU->control |= P_F;
		}
		/*
		 * muoe961411 buzz 4/16/96
		 * Log last command only if llc2Con is not NULL.
		 */
		if (llc2Con != (llc2Con_t *)0) {
			llc2Con->cmdLastSent = llc2HdrU->control;
			/* We don't want to hold any locks across putnext. */
			ILD_RWUNLOCK(llc2Con->con_lock);
		}

		(void) SAMsend(mac, rem, sizeof (llc2HdrU_t),
						mp, 0, (void *)0);
		if (llc2Con != (llc2Con_t *)0) {
			ILD_WRLOCK(llc2Con->con_lock);
		}

	} else {
		ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
	}
}


/*
 *	sendI
 *
 *
 * Send I-Frame Handler
 *
 * description:
 *	Check that the resend Q is empty,
 *	and the send window is open. If so, send an I-frame as a command or
 *	response, with or without the poll/final bit set, based on a
 *	comparison of the parameters passed in against the settings of the
 *	send acknowledgement parameters.
 *
 *	If the SAMsend is successful, increment V(S). Clear the send
 *	acknowledgement parameters and timer since this PDU serves as an
 *	acknowledgement. Start the inactivity timer if a command PDU has
 *	been sent.
 *
 *	If the SAMsend is unsuccessful, execute the SET_REMOTE_BUSY action to
 *	handle this error as if the remote node could not accept the I-frame.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	rsp		response/request indicator
 *	pf		poll/final indicator
 *	mp		I-frame message ptr
 *
 *
 *	locks:		con_lock locked on entry/exit
 *
 * returns:
 *	0			success
 *	LLC2_ERR_SYSERR		failure to send the I frame
 *	LLC2_ERR_MAXIFRAMES	LLC2 cannot accept an I-frame from DLPI
 */
/*ARGSUSED*/
int
sendI(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int rsp, int pf,
    mblk_t *mp)
{
	llc2HdrI_t *llc2HdrI;
	mblk_t *dmp;
	int retVal = LLC2_GOOD_STATUS;

	if ((llc2Con->resendHead.flink == &llc2Con->resendHead) &&
		(MOD_128_DIFF(llc2Con->vs, llc2Con->nrRcvd) < llc2Con->k)) {
		/*
		 * rsp and pf precedence:
		 * 1) if the pf value passed in is TRUE, use the rsp and pf
		 * values passed in
		 * 2) else if the send acknowledgement parameters are set,
		 * use them
		 * 3) else use the rsp and pf values passed in, whatever they
		 * are
		 */
		if (pf != TRUE) {
			if (llc2Con->allowCount > 0) {
				rsp = llc2Con->rsp;
				pf = llc2Con->pf;
			}
		}

		llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
		if (rsp == TRUE) {
			llc2HdrI->ssap |= LLC_RESPONSE;
		} else {
			llc2HdrI->ssap &= ~LLC_RESPONSE;
		}
		llc2HdrI->ns = (llc2Con->vs)<<1;
		llc2HdrI->nr = (llc2Con->vr)<<1;
		if (pf == TRUE) {
			llc2HdrI->nr |= SI_P_F;
		}

		if ((dmp = dupmsg(mp)) == NULL) {
			/*
			 * if message cannot be duped then return system
			 * error
			 */
			/*
			 * muoe961411 buzz 4/16/96
			 * This section is a radical change. In the past all we
			 * did was count the _number_ of outstanding frames.
			 * now we actually track which frames are outstanding.
			 * This allows  much more freedom in determining exactly
			 * which frames have been acknowledged.
			 */
			setRemBusy(llc2Con, llc2Sta);
			retVal = LLC2_ERR_SYSERR;
		}
		/*
		 * muoe961411 buzz 4/16/96
		 * due to the "new way" of handling acknowledgements,
		 * the MAC does _NOT_ need to call back when the
		 * frame has been transmitted, since we keep the original,
		 * leave final parameter NULL, signifying that the MAC
		 * may release the passed Message Block when the
		 * send is complete.
		 */
		else if (SAMsend(mac, &llc2Con->rem,
						msgdsize(dmp),
						dmp, 0, (void *)0)) {
			/*
			 * Send rejected by the mac layer. MAC has freed the
			 * duplicate(because the kptr was NULL) and returned
			 * a non-zero value.
			 * The original msgb will be re-queued to the dlpi
			 * stream write queue by caller. When XON is received
			 * from the mac layer, tryOutI will be called to enable
			 * the queue, and the message will get sent through
			 * this routine again.
			 */
			retVal = LLC2_ERR_SYSERR;
		} else {
			/* send accepted by MAC layer */

			llc2Con->cmdLastSent = llc2HdrI->ns;

			/* add message to the unacknowledged I frame que */
			ADD_DLNKLST((dLnkLst_t *)mp, llc2Con->unackHead.blink);
			/*
			 * increment V(S) and, if V(S) was the largest V(S)
			 * sent, update largest V(S) sent, also.
			 */
			if (llc2Con->vs == llc2Con->vsMax) {
				llc2Con->vs = MOD_128_INCR(llc2Con->vs);
				llc2Con->vsMax = llc2Con->vs;
			} else {
				llc2Con->vs = MOD_128_INCR(llc2Con->vs);
			}

			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			if (rsp == FALSE) {
				startTimer(T_INACT, llc2Con, llc2Sta);
			}

			llc2Con->iSent++;
			UPDATE_COUNTER(llc2Con, framesSent, mac);
		}
	} else {
		retVal = LLC2_ERR_MAXIFRAMES;
		if (llc2Con->resendHead.flink != &llc2Con->resendHead) {
			ild_trace(MID_LLC2, __LINE__, llc2Con->sid, 1, 0);
		} else {
			ild_trace(MID_LLC2, __LINE__, llc2Con->sid, 2, 0);
		}
	}

	return (retVal);
}


/*
 *	setResendI
 *
 *
 * Resend Set-up Processing
 *
 * description:
 *	Determine if messages have to be moved from the unacknowledged
 *	messages Q to the resend messages Q by comparing the received N(R)
 *	(shifted right once) with the V(S) value that existed before the
 *	start of this resend operation. If they differ, move all of the
 *	messages from the unacknowledged Q to the resend Q.
 *
 *	WARNING1: llc2Con->vs, the the current send sequence number V(S),
 *	is not updated by this routine. Caller is responsible
 *	for setting llc2Con->vs equal to the received N(R).
 *
 *	WARNING2: updateNrRcvd must be called before calling this routine.
 *	This routine assumes acknowledged messages have already
 *	been removed from the Qs.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	origVs		V(S) before it was found necessary to do
 *			a resend operation
 *	nr		N(R) >> 1(SHIFT OUT THE P/F BIT BEFORE
 *			CALLING setResendI
 *
 * locks		con_lock locked on entry/exit
 *
 */
/*ARGSUSED*/
void
setResendI(llc2Con_t *llc2Con, uchar_t origVs, uchar_t nr)
{
	dLnkLst_t *msg;
	llc2HdrI_t *llc2HdrI;
	/*
	 * If the received nr is a nack and there are frames on the unacked Q,
	 * move all of the I-frames from the unacked Q to the resend Q.
	 */
	if ((origVs != nr) &&
		((msg = llc2Con->unackHead.flink) != &llc2Con->unackHead)) {
		/*
		 * There is an I-frame on the unacked Q. Sanity check:
		 * The first
		 * frame should have ns equal to the received nr.
		 */
		llc2HdrI = (llc2HdrI_t *)(((mblk_t *)msg)->b_rptr);
		if (nr == ((llc2HdrI->ns)>>1)) {
			/*
			 * move unacknowledged messages
			 *
			 * for example:
			 *
			 * VS = 4
			 *
			 * UNACK Q: 4-5-6
			 * RESEND Q: 7-8-9
			 *	|
			 *	v
			 * UNACK Q:
			 * RESEND Q: 4-5-6-7-8-9
			 */
			msg = llc2Con->unackHead.blink;
			while (msg != &llc2Con->unackHead) {
				dLnkLst_t *msgHold;
				msgHold = msg;
				msg = msg->blink;
				RMV_DLNKLST(msgHold);
				ADD_DLNKLST(msgHold, &llc2Con->resendHead);
			}
			/*
			 * Dynamic Windowing Algorithm. Assume that the loss
			 * of one or more I-frames is due to congestion
			 * (i.e., that bit errors are rare, and the network
			 * throws away frames when it is congested). Shut the
			 * send window size down to 1.
			 */
			llc2Con->k = 1;
			llc2Con->kAck = 0;

		} else {
			/*
			 * N(s) in the first I-frame on the unacked Q is not
			 * correct.
			 */
			ild_trace(MID_LLC2, __LINE__, (uintptr_t)llc2Con,
					(uint_t)((origVs<<8)|nr), 0);
		}
	}
}


/*
 *	tryOutI
 *
 *
 * Outbound I-Frame Prompter
 *
 * description:
 *	If there is not a remote busy condition and a resend operation is not
 *	pending, beat the bushes for an I-frame to send. Attempt to send
 *	any messages on the resend messages Q first, while the send window
 *	is open. If all the queued messages can be resent, or there were
 *	none to begin with, tickle the DLPI write queue in case it was
 *	disabled.
 *
 *	This function will send the first message as a command or response
 *	with or without the P/F bit set, based on a comparison of the
 *	parameters passed in against the settings of the send acknowledgement
 *	parameters. Subsequent messages will be sent as commands with the
 *	P/F bit not set. The number of messages output is returned to the
 *	caller.
 *
 *	On each successful SAMsend, increment V(S).
 *
 *	Clear the send acknowledgement parameters and timer if at least one
 *	I-frame is sent since it serves as an acknowledgement. Also,
 *	start the receive acknowledgement timer if it is not running if at
 *	least one I-frame is sent. Start the inactivity timer if at
 *	least one command PDU is sent.
 *
 *	If any SAMsend is unsuccessful, execute the SET_REMOTE_BUSY action to
 *	handle this error as if the remote node could not accept further
 *	I-frames. Place the I-frame back on the resend messages Q.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *	mac		MAC structure ptr
 *	rsp		response/request indicator
 *	pf		poll/final indicator
 *
 *	locks		con_lock locked on entry/exit
 *
 * returns:
 *	>= 0 number of I-frames output
 */
/*ARGSUSED*/
int
tryOutI(llc2Con_t *llc2Con, llc2Sta_t *llc2Sta, mac_t *mac, int rsp, int pf)
{
	mblk_t *mp;
	mblk_t *dmp;	/* duplicate message pointer for unacked messages */
	llc2HdrI_t *llc2HdrI;
	int cmdSent = 0;
	int numOut = 0;

	if (!(llc2Con->flag & REMOTE_BUSY)) {
		/*
		 * rsp and pf precedence:
		 * 1) if the pf value passed in is TRUE, use the rsp and pf
		 * values passed in
		 * 2) else if the send acknowledgement parameters are set,
		 * use them
		 * 3) else use the rsp and pf values passed in, whatever they
		 * are
		 */
		if (pf != TRUE) {
			if (llc2Con->allowCount > 0) {
				rsp = llc2Con->rsp;
				pf = llc2Con->pf;
			}
		}

		while ((llc2Con->resendHead.flink != &llc2Con->resendHead) &&
			(MOD_128_DIFF(llc2Con->vs, llc2Con->nrRcvd) <
			llc2Con->k)) {
			mp = (mblk_t *)llc2Con->resendHead.flink;
			RMV_DLNKLST((dLnkLst_t *)mp);
			llc2HdrI = (llc2HdrI_t *)mp->b_rptr;
			if (rsp == TRUE) {
				llc2HdrI->ssap |= LLC_RESPONSE;
				/*
				 * only allow the response bit to be set on
				 * the first message sent
				 */
				rsp = FALSE;
			} else {
				llc2HdrI->ssap &= ~LLC_RESPONSE;
				cmdSent++;
			}
			/*
			 * N(S) is not set since it was set on the initial
			 * transmission and will be the same on re-transmissions
			 */
			llc2HdrI->nr = (llc2Con->vr)<<1;


			if (pf == TRUE) {
				llc2HdrI->nr |= SI_P_F;
				/*
				 * only allow the P/F bit to be set on the first
				 * message sent
				 */
				pf = FALSE;
			}
			/*
			 * the SAMsend function may be NULL if the MAC driver
			 * unloaded without shutting down cleanly
			 */
			/* SOLVP980146 buzz 7/20/98 */
			if ((dmp = dupmsg(mp)) == NULL) {
				ADD_DLNKLST((dLnkLst_t *)mp,
						&llc2Con->resendHead);
				setRemBusy(llc2Con, llc2Sta);
				/*
				 * Since all I-frames except(possibly) the
				 * first one are sent as a command, cmdSent > 0
				 * at this point means that a command could not
				 * be sent. Decrement cmdSent, which
				 * is used to determine whether the inactivity
				 * timer should be started.
				 */
				if (cmdSent > 0) {
					--cmdSent;
				}
				break;
			} else {

				/*
				 * muoe961411 buzz 4/16/96
				 * due to the "new way" of handling
				 * acknowledgements, the MAC does _NOT_ need
				 * to call back when the frame has been
				 * transmitted. Since we keep a dup.
				 * leave final parameter NULL, signifying
				 * that the MAC may release then passed
				 * Message Block when the send is complete.
				 */
				if (SAMsend(mac, &llc2Con->rem,
						msgdsize(dmp),
						dmp, 0, (void *)0)) {
					/*
					 * Send rejected by the mac layer.
					 * MAC has freed the
					 * duplicate so we put the original
					 * msgb back on the
					 * resend queue. When XON is received
					 * from mac layer,
					 * tryOutI will be called again.
					 */
					ADD_DLNKLST((dLnkLst_t *)mp,
							&llc2Con->resendHead);
					/*
					 * Since all I-frames except(possibly)
					 * the first one are
					 * sent as a command, cmdSent > 0 at
					 * this point means that
					 * a command could not be sent.
					 * Decrement cmdSent, which
					 * is used to determine whether the
					 * inactivity timer
					 * should be started.
					 */
					if (cmdSent > 0) {
						--cmdSent;
					}
					break;
				}
				/* send accepted by the mac layer */
				else {
					llc2Con->cmdLastSent = llc2HdrI->ns;

					/* que message on unacknowledged que */
					ADD_DLNKLST((dLnkLst_t *)mp,
						llc2Con->unackHead.blink);

					/*
					 * increment V(S) and, if V(S) was the
					 * largest V(S) sent,
					 * update largest V(S) sent, also.
					 */
					if (llc2Con->vs == llc2Con->vsMax) {
						llc2Con->vs = MOD_128_INCR(
							llc2Con->vs);
						llc2Con->vsMax = llc2Con->vs;
					} else {
						llc2Con->vs = MOD_128_INCR(
							llc2Con->vs);
					}
					numOut++;

					llc2Con->iSent++;
					UPDATE_COUNTER(llc2Con, framesSent,
							mac);
					UPDATE_COUNTER(llc2Con,
							framesSentError,
							mac);
				}
			}
		}
		if (numOut > 0) {
			stopTimer(T_SEND_ACK, llc2Con, llc2Sta);
			llc2Con->rsp = FALSE;
			llc2Con->pf = FALSE;
			llc2Con->allowCount = 0;

			startTimer(T_ACK | T_IF_NOT_RUNNING, llc2Con, llc2Sta);
		}

		if (cmdSent > 0) {
			startTimer(T_INACT, llc2Con, llc2Sta);
		}

		/*
		 * if the re-send operation(if done) has completed without
		 * causing a remote busy condition, and the send window is still
		 * open, tickle the DLPI write queue to allow any held I-frames
		 * to be delivered to LLC2
		 */
		if (!(llc2Con->flag & REMOTE_BUSY) &&
			(llc2Con->resendHead.flink == &llc2Con->resendHead) &&
			(MOD_128_DIFF(llc2Con->vs,
				llc2Con->nrRcvd) < llc2Con->k)) {
			dlpi_XON(mac, llc2Con->link);
		}
	}

	return (numOut);
}

/*
 *	startTimer
 *
 *
 * START_XXX_TIMER Connection Component Action
 *
 * description:
 *	Based on the timer type passed in, execute the appropriate
 *	START_XXX_TIMER action. If the timer is already running and
 *	the timer type is not modified by 'IF_NOT_RUNNING', the timer is
 *	re-started by first de-queueing, then re-queueing the timer entry.
 *
 *	If the timer to be started is the T_P(poll) timer, then if P_FLAG
 *	is not set, this routine clears poll retry counters and sets P_FLAG.
 *
 *	The timer type parameter consists of two parts:
 *	Least significant byte = timer type
 *	Most significant byte = modifier flags
 *
 *	Exactly one timer type must be specified. Starting multiple
 *	timers is NOT supported by this routine.
 *
 *	Modifier flags supported:
 *	T_IF_NOT_RUNNING - don't re-start timer if already running
 *
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	type		timer type and modifier flags
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *
 *	locks		con_lock is set on entry/exit
 *			sta_lock is locked/unlocked
 *
 * returns:
 *	nothing
 */
void
startTimer(ushort_t type, llc2Con_t *llc2Con, llc2Sta_t *llc2Sta)
{
	llc2TimerEntry_t *entry;	/* timer entry structure pointer */
	llc2TimerHead_t *thead;	/* timer head structure pointer */
	volatile dLnkLst_t *bucketPtr;	/* bucket pointer */
	llc2Timer_t *llc2Timer;

	/*
	 * Get connection timer entry pointer and station timer head pointer
	 * for timer type.
	 *
	 * Implementation note: If timer index were passed in place of the
	 * bit-wise timer type parameter, then entry and head pointers could
	 * be set simply by using timer index. However, indexing means
	 * multiplication, which is expensive on most machines.
	 *
	 * The following if/then/elseif implementation is more efficient than
	 * multiplication on the 486 when the timer to be set is one of the
	 * first three timer types tested. 'Most likely' timer types are tested
	 * for first.
	 */
	ild_trace(MID_LLC2, __LINE__, (uint_t)type, (uintptr_t)llc2Con, 0);

	ASSERT(llc2Sta != NULL);

	llc2Timer = llc2TimerArray[llc2Con->ppa];
	ASSERT(llc2Timer != NULL);

	ILD_WRLOCK(llc2Timer->llc2_timer_lock);

	if (type & T_SEND_ACK) {
		entry = &llc2Con->timerEntry[T_SEND_ACK_INDEX];
		thead = &llc2Timer->timerHead[T_SEND_ACK_INDEX];
	} else if (type & T_ACK) {
		entry = &llc2Con->timerEntry[T_ACK_INDEX];
		thead = &llc2Timer->timerHead[T_ACK_INDEX];
	} else if (type & T_P) {
		entry = &llc2Con->timerEntry[T_P_INDEX];
		thead = &llc2Timer->timerHead[T_P_INDEX];

		if (!(llc2Con->flag & P_FLAG)) {
			/* muoe972183 */
			llc2Con->pollRetryCount = 0;
			llc2Con->pollRetryCountL2 = 0;

			llc2Con->flag |= P_FLAG;
		}
	} else if (type & T_REJ) {
		entry = &llc2Con->timerEntry[T_REJ_INDEX];
		thead = &llc2Timer->timerHead[T_REJ_INDEX];
	} else if (type & T_REM_BUSY) {
		entry = &llc2Con->timerEntry[T_REM_BUSY_INDEX];
		thead = &llc2Timer->timerHead[T_REM_BUSY_INDEX];
	} else if (type & T_INACT) {
		entry = &llc2Con->timerEntry[T_INACT_INDEX];
		thead = &llc2Timer->timerHead[T_INACT_INDEX];
	} else if (type & T_L2) {
		entry = &llc2Con->timerEntry[T_L2_INDEX];
		thead = &llc2Timer->timerHead[T_L2_INDEX];
	} else if (type & T_RNR) {
		entry = &llc2Con->timerEntry[T_RNR_INDEX];
		thead = &llc2Timer->timerHead[T_RNR_INDEX];
	} else {
		/* this should not happen */
		ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
		return;
	}
	/*
	 * If caller does not care whether the timer is already running,
	 * or if the timer is not already running, then start(or stop,
	 * then re-start) the timer.
	 */
	if (!(type & T_IF_NOT_RUNNING) ||
		(entry->chain.flink == (dLnkLst_t *)0)) {
		if (entry->chain.flink != (dLnkLst_t *)0) {
			RMV_DLNKLST(&entry->chain);
		}
		entry->expTime = llc2Timer->curTime + entry->timerInt;

		bucketPtr = thead->curBucket;
		ADVANCE_BUCKET(bucketPtr, entry->timerInt & LLC2_BUCKET_MASK,
				&thead->bucket[LLC2_NUM_BUCKETS]);
		ADD_DLNKLST(&entry->chain, bucketPtr->blink);
	}
	ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);
}


/*
 *	stopTimer
 *
 *
 * STOP_XXX_TIMER Connection Component Action
 *
 * description:
 *	Based on the timer type(s) passed in, execute the appropriate
 *	STOP_XXX_TIMER(S) actions.
 *
 *	If one of the timers to be stopped is the T_P(poll) timer, this
 *	routine clears the P_FLAG(poll outstanding flag) and the R_FLAG
 *	(SNA Re-route Recovery flag).
 *
 *	This implementation differs from the 802.2 STOP_XXX_TIMER(S) actions
 *	in the following ways:
 *		1. The 802.2 STOP_OTHER_TIMERS is not implemented; it was
 *		STOP_ALL_TIMERS other than T_REJ.  Instead of doing
 *		802.2 STOP_OTHER_TIMERS after starting T_REJ, callers of
 *		this routine STOP_ALL_TIMERS before starting T_REJ.
 *		2. P_FLAG is cleared for STOP_ALL_TIMERS. For all but two
 *		callers, P_FLAG is a don't care. The two callers that
 *		set P_FLAG do so after STOP_ALL_TIMERS, rather than before.
 *		3. A new action STOP_OTHER_TIMERS is defined as STOP_ALL_TIMERS
 *		other than T_RNR and T_INACT. STOP_OTHER_TIMERS is used
 *		when beginning an SNA Level 2 Retries Delay. Since T_P is
 *		included in this action, the P_FLAG is cleared. Callers need
 *		to set P_FLAG after doing STOP_OTHER_TIMERS if the P_FLAG
 *		must remain set.
 *
 * execution state:
 *		service level
 *
 * parameters:
 *	type		timer type(may contain bit settings for
 *			more than one timer)
 *	llc2Con		connection component structure ptr
 *	llc2Sta		station component structure ptr
 *
 *	locks		con_lock is locked on entry/exit
 *			sta_lock is locked/unlocked
 * returns:
 *	nothing
 */
void
stopTimer(ushort_t type, llc2Con_t *llc2Con, llc2Sta_t *llc2Sta)
{

	llc2Timer_t *llc2Timer;

	ild_trace(MID_LLC2, __LINE__, (uintptr_t)type, (uintptr_t)llc2Con, 0);

	ASSERT(llc2Sta != NULL);
	ASSERT(llc2Con != NULL);

	llc2Timer = llc2TimerArray[llc2Con->ppa];
	ASSERT(llc2Timer != NULL);

	ILD_WRLOCK(llc2Timer->llc2_timer_lock);

	if (type & T_ACK) {
		if (llc2Con->timerEntry[T_ACK_INDEX].chain.flink !=
			(dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_ACK_INDEX].chain);
		}
	}

	if (type & T_P) {
		if (llc2Con->timerEntry[T_P_INDEX].chain.flink !=
			(dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_P_INDEX].chain);
		}
		/* clear the P_FLAG */
		llc2Con->flag &= ~(P_FLAG | R_FLAG | T_FLAG);
	}

	if (type & T_REJ) {
		if (llc2Con->timerEntry[T_REJ_INDEX].chain.flink !=
			(dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_REJ_INDEX].chain);
		}
	}

	if (type & T_REM_BUSY) {
		if (llc2Con->timerEntry[T_REM_BUSY_INDEX].chain.flink
			!= (dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[
				T_REM_BUSY_INDEX].chain);
		}
	}

	if (type & T_INACT) {
		if (llc2Con->timerEntry[T_INACT_INDEX].chain.flink
			!= (dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_INACT_INDEX].chain);
		}
	}

	if (type & T_SEND_ACK) {
		if (llc2Con->timerEntry[T_SEND_ACK_INDEX].chain.flink
			!= (dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[
				T_SEND_ACK_INDEX].chain);
		}
	}

	if (type & T_L2) {
		if (llc2Con->timerEntry[T_L2_INDEX].chain.flink !=
			(dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_L2_INDEX].chain);
		}
	}

	if (type & T_RNR) {
		if (llc2Con->timerEntry[T_RNR_INDEX].chain.flink !=
			(dLnkLst_t *)0) {
			RMV_DLNKLST(&llc2Con->timerEntry[T_RNR_INDEX].chain);
		}
	}

	ILD_RWUNLOCK(llc2Timer->llc2_timer_lock);

}

#ifdef LOG_ALLOC_FAIL
/*
 *	allocFail
 *
 *
 * Allocation Failure Recorder
 *
 * description:
 *	This function is used to record an allocation failure which
 *	occurs outside llc2StateMachine() processing where the LLC2
 *	component control blocks are not known to the caller.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	event		event indicator
 *	mac		MAC structure ptr
 *	sid		resource identifier
 *	sap		local SAP address
 *
 * locks		no locks set on entry/exit??
 *			sta_lock		locked/unlocked
 *
 * returns:
 *	nothing
 */
void
allocFail(uchar_t event, mac_t *mac, uint_t sid, uchar_t sap)
{
	llc2Sta_t *llc2Sta = llc2StaArray[mac->mac_ppa];
	llc2Sap_t *llc2Sap;
	llc2Con_t *llc2Con;

	/* make sure the station has been initialized */
	if (llc2Sta) {
		ILD_WRLOCK(llc2Sta->sta_lock);
		if (llc2Sta->state == STA_UP) {

			if (event < STA_OTHER_EVENT) {
				llc2Sta->allocFail++;
			} else if (event < SAP_OTHER_EVENT) {
				if ((llc2Sap = llc2Sta->sapTbl[sap]) !=
					(llc2Sap_t *)0) {
					/* muoe971984 buzz 8/27/97 */
					llc2Sap->allocFail += 1;
				} else {
					llc2Sta->allocFail++;
				}
			} else if (event < UNK_OTHER_EVENT) {
				if ((llc2Sap = llc2Sta->sapTbl[sap]) !=
					(llc2Sap_t *)0) {
					ILD_WRLOCK(llc2Sap->sap_lock);
					if ((llc2Con =
					llc2Sap->conTbl[sid & CON_CID_MASK])
						!= (llc2Con_t *)0) {
						/* muoe971984 buzz 8/27/97 */
						llc2Con->allocFail += 1;
					} else {
						llc2Sap->allocFail++;
					}
					ILD_RWUNLOCK(llc2Sap->sap_lock);
				} else {
					llc2Sta->allocFail++;
				}
			} else {
				llc2Sta->allocFail++;
			}
		}
		ILD_RWUNLOCK(llc2Sta->sta_lock);
	}
}
#endif /* LOG_ALLOC_FAIL */

/*
 * check_if_reusable
 *
 * check to see if the passed Message block(mpReuse) has
 * a large enough data area to be reused for this send.
 * If not, free the passed message and allocate a new one
 * of the appropriate size(mac_hdr_len)
 *
 * Returns: pointer to original or newly allocated message block
 * NULL on allocb failure
 */
mblk_t *
check_if_reusable(mblk_t *mpReuse, int mac_hdr_len)
{
	mblk_t *mp = NULL;

	if (mpReuse != (mblk_t *)NULL) {
		if ((mpReuse->b_datap->db_lim - mpReuse->b_datap->db_base) >=
			mac_hdr_len && mpReuse->b_datap->db_ref == 1) {
			mp = mpReuse->b_cont;
			if (mp != (mblk_t *)NULL) {
				mpReuse->b_cont = (mblk_t *)NULL;
				freemsg(mp);
			}
			mp = mpReuse;
		} else {
			freemsg(mpReuse);
			ild_trace(MID_LLC2, __LINE__, 0, 0, 0);
		}
	}
	if (mp == (mblk_t *)NULL) {
		mp = allocb(mac_hdr_len, BPRI_MED);
	}
	return (mp);
}

/* muoe970020 buzz 06/02/97 */
/*
 *	llc2GetqEnableStatus
 *
 *
 *
 * description:
 * called from ild_wsrv() in the event an error status is returned from
 * dlpi_data(). If the llc layer is set up to handle enabling the q
 * llc2Con->flag is set to NEED_ENABLEQ. The next time CON_NEXT_STATE()
 * is called for this connection the queue will be enabled.
 * If the llc layer does not appear to be set up to handle enabling the q
 * this routine returns a 1 indicating ild_wsrv() should enable the q.
 *
 * execution state:
 *	service level
 *
 * parameters:
 *	q		STREAMS read Q pointer
 *
 * returns:
 *	0		do not enable queue, llc2 will enable it
 *	1		enable the queue
 *
 * Locks:		No Locks set on entry/exit
 */

int
llc2GetqEnableStatus(queue_t *q)
{
	link_t *lnk;
	mac_t *mac;
	ushort_t sid;
	dlsap_t *rem;
	uint_t key;
	llc2Con_t *llc2Con;
	llc2Sta_t *llc2Sta;
	llc2Sap_t *llc2Sap;
	int ret = 1;
	/* buzz muoe971910 08/07/97 */
	mblk_t *mp;
	ushort_t state;

	lnk = (link_t *)q->q_ptr;
	if (lnk == NULL)
		return (ret);

	ILD_WRLOCK(lnk->lk_lock);
	mac = lnk->lk_mac;
	sid = lnk->lk_sid;
	rem = &lnk->lk_dsap;
	/* buzz muoe971965 08/25/97 */
	state = lnk->lk_state;
	ILD_RWUNLOCK(lnk->lk_lock);

	/*
	 * buzz muoe971965 08/25/97
	 * if the state is not DL_DATAXFER dlpi_data() will have
	 * freed the message and the queue will have to be enabled
	 * so that M_PROTO messages can be processed
	 */
	if (state != DL_DATAXFER) {
		ild_trace(MID_LLC2, __LINE__, (uintptr_t)lnk,
				(uint_t)lnk->lk_state, 0);
		return (ret);
	}

	llc2Sta = llc2StaArray[mac->mac_ppa];

	if ((llc2Sta) && (llc2Sta->sta_lock.lock_init)) {
		uint_t tmp_key;

		ILD_WRLOCK(llc2Sta->sta_lock);
		llc2Sap = llc2Sta->sapTbl[(sid>>9) &
						0x0000007f];
		ILD_RWUNLOCK(llc2Sta->sta_lock);

		if (llc2Sap == NULL)
			return (ret);

		ILD_WRLOCK(llc2Sap->sap_lock);
		bcopy((char *)&rem->llc.dl_nodeaddr[3],
			(char *)&tmp_key,
			sizeof (uint_t));
		key = (((uint_t)rem->llc.dl_sap) |
			(tmp_key & 0xffffff00));
		llc2Con = (llc2Con_t *)
			llc2Sap->conHead.flink;

		if (llc2Con == NULL) {
			ild_trace(MID_LLC2, __LINE__, (uintptr_t)lnk,
					(uint_t)lnk->lk_state, 0);
			ILD_RWUNLOCK(llc2Sap->sap_lock);
			return (ret);
		}

		while (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
			/*
			 * The key already contains the lower 3 bytes of
			 * the remote MAC address. Just match the top 3
			 * to be sure.
			 */
			if ((key == llc2Con->key) &&
				(bcmp((char *)&rem->llc.dl_nodeaddr[0],
				(char *)&llc2Con->rem.llc.dl_nodeaddr[0], 3)
					== 0)) {
				break;
			}
			llc2Con = (llc2Con_t *)llc2Con->chain.flink;
		}
		if (llc2Con != (llc2Con_t *)&llc2Sap->conHead) {
			int newipl;
			ILD_TRYWRLOCK(llc2Con->con_lock, newipl);
				/* if we get the lock go ahead */
			if (newipl != 0) {
				/*
				 * if REMOTE_BUSY is set llc2 will enable q
				 * when appropriate
				 */
				if (!llc2Con->flag & REMOTE_BUSY) {
					/*
					 * queue will be enabled when
					 * CON_NEXT_STATE()
					 * is called for this connection
					 */
					llc2Con->flag |= NEED_ENABLEQ;
				}
				ILD_RWUNLOCK(llc2Con->con_lock);
				ILD_RWUNLOCK(llc2Sap->sap_lock);
				ret = 0;
			} else {
				/*
				 * we didn't get the lock so
				 * unlock the SAP and get the CON lock
				 * separately. This will avoid a deadlock
				 * situation where someone else may be
				 * grabbing con-then-sap while we're trying
				 * for sap-then-con.
				 */
				ild_trace(MID_LLC2, __LINE__,
						(uintptr_t)llc2Con,
						(uintptr_t)llc2Sap, 0);

				ILD_WRLOCK(llc2Con->con_lock);
				ILD_RWUNLOCK(llc2Sap->sap_lock);
				/*
				 * if REMOTE_BUSY is set llc2 will enable q
				 * when appropriate
				 */
				if (!llc2Con->flag & REMOTE_BUSY) {
					/*
					 * queue will be enabled when
					 * CON_NEXT_STATE()
					 * is called for this connection
					 */
					llc2Con->flag |= NEED_ENABLEQ;
				}
				ILD_RWUNLOCK(llc2Con->con_lock);
				ret = 0;
			}
		} else {
			/*
			 * buzz muoe971910 08/07/97
			 * queue no longer has an associated llc2Con
			 * structure - disableCon() has already been
			 * called, remove the data message dlpi_data()
			 * returned to the queue
			 */
			mp = getq(q);
			if (mp) {
				if (mp->b_datap->db_type == M_DATA) {
					ild_trace(MID_LLC2, __LINE__,
							(uintptr_t)lnk,
							(uint_t)lnk->lk_state,
							0);
					freemsg(mp);
				} else {
					ild_trace(MID_LLC2, __LINE__,
							(uintptr_t)lnk,
							(uint_t)lnk->lk_state,
							0);
					(void) putbq(q, mp);
				}
			}
			ild_trace(MID_LLC2, __LINE__, (uintptr_t)lnk,
					(uint_t)lnk->lk_state, 0);
			ILD_RWUNLOCK(llc2Sap->sap_lock);
		}

	}

	ild_trace(MID_LLC2, __LINE__, (uintptr_t)lnk, (uint_t)lnk->lk_state, 0);
	return (ret);
}
