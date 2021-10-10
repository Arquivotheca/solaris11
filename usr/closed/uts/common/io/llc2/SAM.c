/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio USA
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/stropts.h>
#include <sys/ddi.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/log.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/byteorder.h>

#include "ildlock.h"

#include <sys/dlpi.h>
#include <sys/llc2.h>
#include "ild.h"
#include "llc2k.h"
#include "SAM.h"

/* Local Prototypes */
int sam_send_inforeq(mac_t *, samdata_t *, queue_t *, mblk_t *);
void x25_trace(mac_t *, mblk_t *, int);

/* External definitions */
extern mac_t		*ild_macp[];
extern macx_t		*ild_macxp[];
extern int		ild_maccnt;
extern int		x25TraceEnabled;
ILD_EDECL_LOCK(global_mac_lock);

/*
 * Description
 * -----------
 *
 * The Solaris Add-on MAC (SAM) module is a component of LLC2 and provides
 * a DLPI/Streams interface to Solaris GLD and NCR CoLD MAC drivers.
 *
 *
 * Locking scheme
 *
 * The mac_lock is used to synchronize access to the fields in SAM's
 * private data structure.
 *
 */


/*
 * Jump Table for DLPI functions - index into table is current DLPI state
 */
void (*samDLPI[])(queue_t *, mblk_t *, mac_t *, samdata_t *) = {
	samDL_OUTofSTATE,	/* DL_UNBOUND, PPA attached */
	samDL_BIND_PENDING,	/* Waiting ack of DL_BIND_REQ */
	samDL_UNBIND_PENDING,	/* Waiting ack of DL_UNBIND_REQ */
	samDL_IDLE,		/* dlsap bound, awaiting use */
	samDL_OUTofSTATE,	/* DL_UNATTACHED, PPA not attached */
	samDL_ATTACH_PENDING,	/* Waiting ack of DL_ATTACH_REQ */
	samDL_DETACH_PENDING,	/* Waiting ack of DL_DETACH_REQ */
	samDL_OUTofSTATE,	/* Waiting ack of DL_UDQOS_REQ */
	samDL_OUTofSTATE,	/* outgoing connection, awaiting DL_CONN_CON */
	samDL_OUTofSTATE,	/* incoming connection, awaiting DL_CONN_RES */
	samDL_OUTofSTATE,	/* Waiting ack of DL_CONNECT_RES */
	samDL_OUTofSTATE,	/* connection-oriented data transfer */
	/* user initiated reset, awaiting DL_RESET_CON */
	samDL_OUTofSTATE,
	/* provider initiated reset, awaiting DL_RESET_RES */
	samDL_OUTofSTATE,
	samDL_OUTofSTATE,	/* Waiting ack of DL_RESET_RES */
	/* Waiting ack of DL_DISC_REQ when DL_OUTCON_PENDING */
	samDL_OUTofSTATE,
	/* Waiting ack of DL_DISC_REQ when DL_INCON_PENDING */
	samDL_OUTofSTATE,
	/* Waiting ack of DL_DISC_REQ when DL_DATAXFER */
	samDL_OUTofSTATE,
	/* Waiting ack of DL_DISC_REQ when DL_USER_RESET_PENDING */
	samDL_OUTofSTATE,
	/* Waiting ack of DL_DISC_REQ when DL_DL_PROV_RESET_PENDING */
	samDL_OUTofSTATE,
	samDL_OUTofSTATE	/* Waiting ack of DL_SUBS_BIND_REQ */
};



/*
 * SAMsod()
 * --------
 *
 * This routine allocates SAM's private data structure and saves its address
 * in the extended mac structure, macx.  It also initializes the mac structure
 * with SAM's functions and allocates the mac lock.
 *
 * Return values:	SAMSODPASS (1)  start-of-day successful
 *			SAMSODFAIL (0)  start-of-day failed
 *
 *
 * Locks: 		mac lock allocated, not locked
 */

int
SAMsod(mac_t *mac, uint_t slot, uint_t ppa)
{

	samdata_t *samp;

	ild_trace(THIS_MOD, __LINE__, ppa, slot, 0);

	samp = kmem_zalloc(SAMDATA_SZ, KM_NOSLEEP);
	if (samp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"SAMsod()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate private structure",
			0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, ppa, slot, 0);
		return (SAMSODFAIL);
	}

	samp->sam_state = DL_UNATTACHED;
	samp->sam_mac = mac;

	ild_macxp[ppa]->UserDataPtr = samp;
	mac->mac_state = MAC_INST;
	mac->mac_hdr_sz = MAC_HDR_SZ;

	return (SAMSODPASS);
}


/*
 * SAMinit_req()
 * -------------
 *
 * This routine is called when LLC2 processes an ILD_INIT ioctl.  The
 * primary function is to establish a DLPI connection with the MAC driver
 * over the Stream that would have just been I_LINK'd.  Connection
 * establishment is initiated by sending a DL_ATTACH_REQ to the MAC driver.
 * The response is handled by SAMrput().  The message block and queue passed
 * in to this routine are saved so they can be returned to LLC2 when the
 * connection is established.  The mac state is set to 'initialized' so
 * LLC2 can't be unloaded from this point on.
 *
 * SAM may be in the DL_UNATTACHED, DL_UNBOUND, or DL_IDLE state on
 * entry to this routine.  The expected state is DL_UNATTACHED, however, if
 * the 'un-initialization' procedure failed, SAM might end up in the
 * DL_UNBOUND state.  In that case, the MAC driver is already attached, so
 * we just try to bind.  If SAM is in the DL_IDLE state, a successful status
 * is returned immediately.
 *
 * Return values: SAMPASS successful, connection establishment in progress
 *			SAMFAIL	error
 *
 * Locks:	called holding the mac lock, unlocks the mac lock
 */

/*ARGSUSED*/
int
SAMinit_req(mac_t *mac, queue_t *q, mblk_t *mp, void *init)
{

	samdata_t *samp = ild_macxp[mac->mac_ppa]->UserDataPtr;
	int status;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);

	switch (samp->sam_state) {
	case DL_UNATTACHED:
		status = sam_send_attach(mac, samp, q, mp);
		break;
	case DL_UNBOUND:
		status = sam_send_bind(mac, samp, q, mp);
		break;
	case DL_IDLE:
		ILD_RWUNLOCK(mac->mac_lock);
		ild_init_con(mac, q, mp, SAMPASS);
		return (SAMPASS);
	default:
		ILD_LOG(mac->mac_ppa, 0,
			"SAMinit_req()", SAM_EBADSTATE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Invalid state for init_req, DLPI state 0x%x",
			samp->sam_state, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);
		status = SAMFAIL;
		break;
	}
	ILD_RWUNLOCK(mac->mac_lock);
	return (status);
}


/*
 * SAMuninit_req()
 * ---------------
 *
 * This routine is called when LLC2 processes an ILD_UNINIT ioctl.
 * It initiates the shutdown of the connection with the MAC driver
 * by sending a DL_UNBIND_REQ.  The response is handled by SAMrput()
 * where the next step of the shutdown is performed.  The message block
 * and queue passed in to this routine are saved so they can be returned
 * to LLC2 when the shutdown is complete.
 *
 * SAM may be in the DL_IDLE or DL_UNBOUND state.  The expected state is
 * DL_IDLE, in which case an unbind request is sent to the MAC driver.  If
 * SAM is in the DL_UNBOUND state, then the detach request is sent instead.
 *
 * Return values: SAMPASS successful, shutdown in progress
 *		SAMFAIL   error
 *
 * Locks: lock/unlock the mac lock
 */

int
SAMuninit_req(mac_t *mac, queue_t *q, mblk_t *mp)
{
	samdata_t *samp = ild_macxp[mac->mac_ppa]->UserDataPtr;
	int status;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);

	ILD_WRLOCK(mac->mac_lock);

	switch (samp->sam_state) {
	case DL_IDLE:
		status = sam_send_unbind(mac, samp, q, mp);
		break;
	case DL_UNBOUND:
		status = sam_send_detach(mac, samp, q, mp);
		break;
	case DL_UNATTACHED:
		ILD_RWUNLOCK(mac->mac_lock);
		ild_uninit_con(mac, q, mp, SAMPASS);
		return (SAMPASS);
	default:
		status = SAMFAIL;
		ILD_LOG(mac->mac_ppa, 0,
			"SAMuninit_req()", SAM_EBADSTATE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Invalid state for uninit_req, DLPI state 0x%x",
			samp->sam_state, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
				samp->sam_state, 0);
		break;
	}
	ILD_RWUNLOCK(mac->mac_lock);
	return (status);
}

/*
 * SAMioctl()
 * ----------
 *
 * This routine is called when NCRLLC2 processes an ILD_MAC ioctl.
 * Its only function is to accept a request to set or clear a
 * multicast, functional, or group address. Anything else gets a
 * response of EINVAL.
 *
 * The request is translated into the appropriate DLPI DL_ENABMULTI_REQ
 * or DL_DISABMULTI_REQ primitive and  then forwarded to the Solaris MAC
 * I-LINK'd below.
 *
 * The returned response is handled in dlpi.c (dl_multicast_ack())
 *
 * Locks: lock/unlock the mac lock
 *
 *
 */
int
SAMioctl(mac_t *mac, queue_t *q, mblk_t *mp)
{
	struct iocblk *ctl;
	MCast_t *mult;
	macx_t  *macx;
	queue_t *lq;
	union DL_primitives *dl;
	mblk_t  *bp;
	uint_t   primitive = DL_ENABMULTI_REQ;
	int error;


	error = miocpullup(mp, sizeof (MCast_t));
	if (error != 0) {
		miocnak(q, mp, 0, error);
		return (0);
	}

	mp->b_datap->db_type = M_IOCACK;
	ctl = (struct iocblk *)mp->b_rptr;
	mult =  (MCast_t *)mp->b_cont->b_rptr;

	ILD_WRLOCK(mac->mac_lock);
	macx = ild_macxp[mac->mac_ppa];

	switch (mult->cmd) {

	case ReconfigMCast:
	case ClearFunctionalAddr:
	case ClearGroupAddr:
	case ((DL_TPR<<8) | ClearFunctionalAddr):
	case ((DL_TPR<<8) | ClearGroupAddr):
	{
		primitive = DL_DISABMULTI_REQ;
	}
	/*FALLTHROUGH*/
	case ConfigMCast:
	case SetFunctionalAddr:
	case SetGroupAddr:
	case ((DL_TPR<<8) | SetFunctionalAddr):
	case ((DL_TPR<<8) | SetGroupAddr):
	{
		/* Generate the DL_ENAB/DISABMULTI_REQ mblk_t */
		if ((bp = allocb(DL_ENABMULTI_REQ_SIZE + 6, BPRI_HI)) != NULL) {

			dl = (union DL_primitives *)(void *)bp->b_rptr;
			bp->b_datap->db_type = M_PROTO;
			bp->b_wptr = bp->b_rptr + DL_ENABMULTI_REQ_SIZE+6;
			dl->dl_primitive = primitive;
			dl->enabmulti_req.dl_addr_length = 6;
			dl->enabmulti_req.dl_addr_offset =
				DL_ENABMULTI_REQ_SIZE;

			bcopy((caddr_t)&mult->multicast[0],
				(caddr_t)(bp->b_rptr + DL_ENABMULTI_REQ_SIZE),
				6);

			/* Save for ACK/NAK from MAC driver */
			macx->multicast_queue = q;
			macx->multicast_mblk = mp;
			lq = macx->lowerQ;
			ILD_RWUNLOCK(mac->mac_lock);
			ild_trace(THIS_MOD, __LINE__, (uintptr_t)mac,
					dl->dl_primitive, 0);
			putnext(lq, bp);
			return (0);
		}
	}
	/* No, it's not an error to fall through... */
	default:
		ILD_RWUNLOCK(mac->mac_lock);
		ctl->ioc_rval = RVAL_CMD_UNKNOWN;
		ctl->ioc_error = EINVAL;
		mp->b_datap->db_type = M_IOCNAK;
		ild_trace(THIS_MOD, __LINE__, (uintptr_t)mac, mult->cmd, 0);
		qreply(q, mp);
	}
	return (0);
}


/*
 * SAMcleanup()
 * -------------
 *
 * This routine is called by LLC2 during shutdown.  The private data
 * structure associated with the macx structure is freed and the mac lock
 * is deallocated.
 *
 * Return values: none
 *
 * Locking: none
 */

/*ARGSUSED*/
void
SAMcleanup(mac_t *mac, macx_t *macx)
{

	ild_trace(THIS_MOD, __LINE__, 0, 0, 0);

	if (macx->UserDataPtr) {
		kmem_free(macx->UserDataPtr, SAMDATA_SZ);
	}
}

/*ARGSUSED*/
int
SAMloopback(mac_t *mac, dlsap_t *dsap, int len, mblk_t *mp)
{
	mblk_t *bp, *newmp;
	dl_unitdata_ind_t *dludi;
	llc2Hdr_t *llchdr;
	ether_hdr_t ether, *eth;
	int offSet, ret = 0;

	ild_trace(THIS_MOD, __LINE__, 0, 0, 0);

	if ((bp = allocb(DL_UNITDATA_REQ_SIZE +
				(2 * sizeof (ether_hdr_t)), BPRI_HI)) != NULL) {
		/* Build DL_UNITDATA_IND message */
		dludi = (dl_unitdata_ind_t *)(void *)bp->b_wptr;
		dludi->dl_primitive = DL_UNITDATA_IND;
		dludi->dl_dest_addr_length = sizeof (ether_hdr_t);
		dludi->dl_dest_addr_offset = DL_UNITDATA_IND_SIZE;
		bp->b_datap->db_type = M_PROTO;

		bcopy((caddr_t)&dsap->ether, (caddr_t)&ether,
			sizeof (ether_hdr_t));
		/*
		 * Internally, llc2 stores the frame header in llc2 format
		 * where sap is of type uchar while driver expects sap
		 * as 2 bytes. We need to shift it in low order bytes before
		 * giving it to the driver.
		 */
		ether.dl_type = ether.dl_type >> 8;
		bcopy((caddr_t)&ether,
			(caddr_t)(bp->b_wptr + DL_UNITDATA_IND_SIZE),
			sizeof (ether_hdr_t));

		dludi->dl_src_addr_offset = (DL_UNITDATA_IND_SIZE +
						sizeof (ether_hdr_t));
		dludi->dl_src_addr_length = sizeof (ether_hdr_t);
		bp->b_wptr += (DL_UNITDATA_IND_SIZE + sizeof (ether_hdr_t));

		eth = (ether_hdr_t *)(bp->b_wptr);
		bcopy((caddr_t)mac->mac_bia,
			(caddr_t)(eth->dl_nodeaddr), 6);
		llchdr = (llc2Hdr_t *)(mp->b_rptr);
		eth->dl_type = llchdr->ssap;
		dludi->dl_group_address = (dsap->llc.dl_nodeaddr[0] & 0x01);

		bp->b_wptr += sizeof (ether_hdr_t);

		offSet = mp->b_rptr - mp->b_datap->db_base;
		mp->b_rptr = mp->b_datap->db_base;
		if ((newmp = (mblk_t *)copymsg(mp)) != NULL) {

			newmp->b_rptr = newmp->b_datap->db_base + offSet;

			bp->b_cont = newmp;
			llc2RcvIntLvl(mac, bp);
			ret = 1;
		}
		else
			freemsg(bp);
		mp->b_rptr = mp->b_datap->db_base + offSet;
	}
	return (ret);
}

/*
 * SAMsend()
 * ---------
 *
 * This routine builds a DL_UNITDATA_REQ message and passes it downstream
 * to the MAC driver.  If SAM is in the DL_IDLE state, a message block is
 * allocated to hold the dl_unitdata_req_t structure and the destination
 * address.  After the destination address is copied in, the data portion
 * of the message contained in mp is attached.  Then the message is passed
 * downstream to the MAC driver.
 *
 * Return values: SAMPASS (0) - successful, message sent to MAC driver
 *		SAMFAIL (1) - failed, message not sent to MAC driver
 *
 * Locking: mac_lock is locked/unlocked.
 *
 */

/*ARGSUSED*/
int
SAMsend(mac_t *mac, dlsap_t *dsap, int len, mblk_t *mp, int pri, void *kp)
{

	samdata_t *samp;
	queue_t *lowerq;
	dl_unitdata_req_t *dludr;
	mblk_t *bp;
	ether_hdr_t ether;
	int meToo = 0;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, (uintptr_t)mp, 0);

	ILD_WRLOCK(mac->mac_lock);

	samp = ild_macxp[mac->mac_ppa]->UserDataPtr;

	/* Must be in data transfer state */
	if (samp->sam_state != DL_IDLE) {
		ILD_RWUNLOCK(mac->mac_lock);
		ILD_LOG(mac->mac_ppa, 0,
			"SAMsend()", SAM_EBADSTATE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Invalid state for send request, DLPI state 0x%x",
			samp->sam_state, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);
		freemsg(mp);
		return (SAMFAIL);
	}

	lowerq = ild_macxp[mac->mac_ppa]->lowerQ;

	if ((ild_macxp[mac->mac_ppa]->mac_options & LOOPBACK_BCAST) &&
		(((mac->mac_type == DL_TPR) &&
		(dsap->llc.dl_nodeaddr[0] & 0x80)) ||
		(dsap->llc.dl_nodeaddr[0] & 0x01) ||
		(CMP_MAC(mac->mac_bia, dsap->llc.dl_nodeaddr)))) {

		meToo = 1;
	}


	ILD_RWUNLOCK(mac->mac_lock);

	/*
	 * Make sure MAC driver's queue is not full.  If it is, let
	 * LLC2 handle retry.
	 */
	if (!canputnext(lowerq)) {
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
				(uintptr_t)lowerq, 0);
		freemsg(mp);
		return (SAMFAIL);
	}

	/* Get message block for DL_UNITDATA_REQ */

	bp = allocb(DL_UNITDATA_REQ_SIZE + sizeof (ether_hdr_t), BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"SAMsend()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate unitdata request",
			0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		freemsg(mp);
		return (SAMFAIL);
	}

	/* Build DL_UNITDATA_REQ message */
	dludr = (dl_unitdata_req_t *)(void *)bp->b_wptr;
	dludr->dl_primitive = DL_UNITDATA_REQ;
	dludr->dl_priority.dl_min = 0;
	dludr->dl_priority.dl_max = 0;
	/*
	 * At the moment, GLD supports a DLSAP length of 8 bytes
	 * for CSMA/CD, TR, FDDI. This may need revisiting in the
	 * future.
	 */
	dludr->dl_dest_addr_length = sizeof (ether_hdr_t);
	dludr->dl_dest_addr_offset = DL_UNITDATA_REQ_SIZE;
	bp->b_datap->db_type = M_PROTO;

	if (meToo) {
		if (SAMloopback(mac, dsap, len, mp) == 0) {
			freemsg(mp);
			freemsg(bp);
			return (SAMFAIL);
		}
	}

	/* Copy destination address into DL_UNITDATA_REQ message */
	bcopy((caddr_t)&dsap->ether, (caddr_t)&ether,
		sizeof (ether_hdr_t));
	/*
	 * Internally, llc2 stores the frame header in llc2 format
	 * where sap is of type uchar while driver expects sap
	 * as 2 bytes. We need to shift it in low order bytes before
	 * giving it to the driver.
	 */
	ether.dl_type = ether.dl_type >> 8;
	bcopy((caddr_t)&ether,
		(caddr_t)(bp->b_wptr + DL_UNITDATA_REQ_SIZE),
		sizeof (ether_hdr_t));

#ifdef LVL0
	ll = (llc2Hdr_t *)mp->b_rptr;
	cmn_err(CE_CONT,
		"SAMsend: dsap: %x; ssap: %x; control: %x; etype: %d\n",
		ll->dsap, ll->ssap, ll->control, ether.dl_type);
#endif

	bp->b_wptr += (DL_UNITDATA_REQ_SIZE + sizeof (ether_hdr_t));

	/* Attach data portion to DL_UNITDATA_REQ */
	bp->b_cont = mp;

	if (x25TraceEnabled)
		x25_trace(mac, bp, meToo);

	putnext(lowerq, bp);

	return (SAMPASS);

}

/*
 * SAMrput()
 * ---------
 *
 * This routine processes input messages sent upstream by the MAC driver
 * by calling one of the state machine routines.
 *
 * Return values: none
 *
 * Locking: mac_lock locked/unlocked
 *
 */

int
SAMrput(queue_t *q, mblk_t *mp)
{

	mac_t *mac;
	samdata_t *samp;


	ILD_WRLOCK(global_mac_lock);
	mac = q->q_ptr;
	if (mac != NULL) {
		ILD_WRLOCK(mac->mac_lock);
		ILD_RWUNLOCK(global_mac_lock);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
		    mp->b_datap->db_type, 0);
	} else {
		ILD_RWUNLOCK(global_mac_lock);
		freemsg(mp);
		return (0);
	}

	samp = ild_macxp[mac->mac_ppa]->UserDataPtr;

	switch (mp->b_datap->db_type) {
	case M_PROTO:
	case M_PCPROTO:
		samDLPI[samp->sam_state](q, mp, mac, samp);
		break;

	case M_FLUSH:
		/* M_FLUSH generated by MAC driver */
		if (*mp->b_rptr & FLUSHW)
			flushq(WR(q), FLUSHALL);
		if (*mp->b_rptr & FLUSHR) {
			flushq(q, FLUSHALL);
			*mp->b_rptr &= ~FLUSHR;
			ILD_RWUNLOCK(mac->mac_lock);
			qreply(q, mp);
			return (0);
		} else
			freemsg(mp);
		break;

	default:
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
				mp->b_datap->db_type, 0);
		freemsg(mp);
		break;
	}

	ILD_RWUNLOCK(mac->mac_lock);
	return (0);
}

/*
 * samDL_IDLE()
 * ------------
 *
 * This routine is called by SAMrput() to handle messages received in
 * the DL_IDLE(data transfer) state.  DL_UNITDATA_IND and unexpected
 * messages are passed up to the llc2RcvIntLvl() routine.  Responses to
 * DL_ENABMULTI_REQ, DL_DISABMULTI_REQ, and DL_GET_STATISTICS_REQ are
 * passed up to LLC2 thru the DLPI_ACK() macro.
 *
 * Return values: none
 *
 * Locking: mac_lock is locked on entry/exit
 *
 */

void
samDL_IDLE(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{

	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);

	switch (d->dl_primitive) {
	case DL_UNITDATA_IND:
		if (x25TraceEnabled)
			x25_trace(mac, mp, 1);
		llc2RcvIntLvl(mac, mp);
		break;

	case DL_OK_ACK:
		DLPI_ACK(q, mp);
		break;

	case DL_INFO_ACK:  {
		dlsap_t 	*sap;
		timeout_id_t	id;

		id = samp->sam_timeid;
		if (id) {
			samp->sam_timeid = 0;
			ILD_RWUNLOCK(mac->mac_lock);
			(void) untimeout(id);
			ILD_WRLOCK(mac->mac_lock);
		}

		mac->mac_type = d->info_ack.dl_mac_type;
		mac->mac_max_sdu = d->info_ack.dl_max_sdu;
		mac->mac_min_sdu = d->info_ack.dl_min_sdu;
		sap = (dlsap_t *)(mp->b_rptr +
					d->info_ack.dl_brdcst_addr_offset);
		CPY_MAC(sap->llc.dl_nodeaddr,
			ild_macxp[mac->mac_ppa]->broadcast_addr.mac_addr);
		freemsg(mp);
	}
	break;

	case DL_ERROR_ACK:
		switch (d->error_ack.dl_error_primitive) {
		case DL_INFO_REQ:

			UNTIMEOUT(samp->sam_timeid);
			ILD_LOG(mac->mac_ppa, 0,
				"samDL_IDLE()", SAM_EBADRESPONSE,
				E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
				"DL_INFO_REQ failed: errno %d, unix errno %d",
				d->error_ack.dl_errno,
				d->error_ack.dl_unix_errno,
				0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->error_ack.dl_errno, 0);
			freemsg(mp);
			break;

		default:
			DLPI_ACK(q, mp);
			break;
		}
		break;

	default:
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);
		freemsg(mp);
		break;

	}


}


/*
 * samDL_ATTACH_PENDING()
 * ----------------------
 *
 * This routine is called by SAMrput() to process the response to the
 * DL_ATTACH_REQ sent by sam_send_attach().  The next step in
 * establishing the DLPI connection is performed here, that is, building
 * and sending the DL_BIND_REQ.
 *
 * Return values: none
 *
 * Locking: mac_lock is locked on entry/exit
 *
 */

/*ARGSUSED*/
void
samDL_ATTACH_PENDING(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{

	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;
	mblk_t *mp1;
	queue_t *q1;
	volatile samdata_t *vsamp = samp;

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);

	switch (d->dl_primitive) {
	case DL_OK_ACK:
		switch (d->ok_ack.dl_correct_primitive) {
		case DL_ATTACH_REQ:
			UNTIMEOUT(vsamp->sam_timeid);
			/* Check for timeout, state would have changed */
			if (samp->sam_state == DL_ATTACH_PENDING) {
				if (sam_send_bind(mac, samp, NULL, NULL) ==
					SAMFAIL)
					goto reporterr;
			} else {
				/*
				 * Timeout occurred, ignore response but
				 * put SAM in the same state as MAC driver.
				 */
				samp->sam_state = DL_UNBOUND;
			}
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_ATTACH_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_OK_ACK for primitive %d, state 0x%x",
			d->ok_ack.dl_correct_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->ok_ack.dl_correct_primitive, 0);
			break;
		}
		break;

	case DL_ERROR_ACK:
		switch (d->error_ack.dl_error_primitive) {
		case DL_ATTACH_REQ:
			UNTIMEOUT(samp->sam_timeid);
			/* Check for timeout, state would have changed */
			if (samp->sam_state == DL_ATTACH_PENDING) {
				/* No timeout, report error */
				goto reporterr;
			}
			/*
			 * Timed out, leave state where sam_timeout()
			 * set it (DL_UNATTACHED).
			 */
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_ATTACH_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_ERROR_ACK for primitive %d, state 0x%x",
			d->error_ack.dl_error_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->error_ack.dl_error_primitive, 0);
			break;
		}
		break;

	default:
		/* Ignore invalid response, wait for correct one */
		ILD_LOG(mac->mac_ppa, 0,
		"samDL_ATTACH_PENDING()", SAM_EBADRESPONSE,
		E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
		"Unexpected primitive from driver, primitive %d, state 0x%x",
		d->dl_primitive, samp->sam_state, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);
		break;
	}
	freemsg(mp);
	return;

reporterr:
	mp1 = samp->sam_mpsave;
	q1 = samp->sam_qsave;
	samp->sam_mpsave = NULL;
	samp->sam_qsave = NULL;
	samp->sam_state = DL_UNATTACHED;
	ILD_RWUNLOCK(mac->mac_lock);
	ild_init_con(mac, q1, mp1, SAMFAIL);
	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
	freemsg(mp);
	ILD_WRLOCK(mac->mac_lock);

}


/*
 * samDL_BIND_PENDING()
 * --------------------
 *
 * This routine is called by SAMrput() to process the response to the
 * DL_BIND_REQ sent by sam_send_bind().  A DL_BIND_ACK response
 * puts SAM into the data transfer state (DL_IDLE).  A DL_ERROR_ACK
 * changes the state to DL_UNBOUND.  In either case, the status is sent
 * up to LLC2 using the ild initialization confirmation routine.
 *
 * Return values: none
 *
 * Locking: mac_lock is locked on entry/exit
 */

void
samDL_BIND_PENDING(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{
	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;
	int status;
	mblk_t *mp1;
	queue_t *q1;
	char *paddr;
	ushort_t post_untimeout_state;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	switch (d->dl_primitive) {
	case DL_BIND_ACK:

		UNTIMEOUT(samp->sam_timeid);

		/*
		 * In case a timeout occurred before we could cancel it, save
		 * current state as an indication of the timeout so
		 * ild_init_con() won't be called below (it would have already
		 * been called by sam_timeout()).  Finish processing bind ack
		 * so we'll match the driver's idle state.
		 */
		post_untimeout_state = samp->sam_state;
		samp->sam_state = DL_IDLE;


		/* Make copy of mac addr */
		paddr = (char *)d;
		paddr += d->bind_ack.dl_addr_offset;
		CPY_MAC(paddr, ild_macxp[mac->mac_ppa]->factory_addr);
		CPY_MAC(paddr, mac->mac_bia);

		/* Send request to get more MAC driver information */
		status = sam_send_inforeq(mac, samp, q, mp);
		break;

	case DL_ERROR_ACK:
		switch (d->error_ack.dl_error_primitive) {
		case DL_BIND_REQ:
			UNTIMEOUT(samp->sam_timeid);
			post_untimeout_state = samp->sam_state;
			samp->sam_state = DL_UNBOUND;
			status = SAMFAIL;
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_BIND_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_ERROR_ACK for primitive %d, state 0x%x",
			d->error_ack.dl_error_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->error_ack.dl_error_primitive, 0);
			goto out;
		}
		break;

	default:
		ILD_LOG(mac->mac_ppa, 0,
		"samDL_BIND_PENDING()", SAM_EBADRESPONSE,
		E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
		"Unexpected primitive from driver, primitive %d, state 0x%x",
		d->dl_primitive, samp->sam_state, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);
		/* Ignore invalid response, wait for correct one */
		goto out;
	}

	if (post_untimeout_state == DL_BIND_PENDING) {
		/* No timeout occurred, report status */
		mp1 = samp->sam_mpsave;
		q1 = samp->sam_qsave;
		samp->sam_mpsave = NULL;
		samp->sam_qsave = NULL;
		ILD_RWUNLOCK(mac->mac_lock);
		ild_init_con(mac, q1, mp1, status);
		ILD_WRLOCK(mac->mac_lock);
	}
out:
	freemsg(mp);

}

/*
 * samDL_UNBIND_PENDING()
 * ----------------------
 *
 * This routine is called by SAMrput() to process the response to the
 * DL_UNBIND_REQ sent by sam_send_unbind().  Either a DL_OK_ACK or
 * a DL_ERROR_ACK response results in a DL_DETACH_REQ being sent
 * to the MAC driver as the next step in un-initialization.
 *
 * Return values: none
 *
 * Locking: mac_lock is locked on entry/exit
 */

/*ARGSUSED*/
void
samDL_UNBIND_PENDING(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{
	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;
	mblk_t *mp1;
	queue_t *q1;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	switch (d->dl_primitive) {
	case DL_OK_ACK:
		switch (d->ok_ack.dl_correct_primitive) {
		case DL_UNBIND_REQ:
			UNTIMEOUT(samp->sam_timeid);

			/* Check for timeout, state would have changed */
			if (samp->sam_state == DL_UNBIND_PENDING) {
				/* No timeout, send the detach */
				if (sam_send_detach(mac, samp, NULL, NULL) ==
					SAMFAIL)
					goto reporterr;
			} else {
				/*
				 * Timeout occurred, set state to
				 * match that of the MAC driver.
				 */
				samp->sam_state = DL_UNBOUND;
			}
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_UNBIND_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_OK_ACK for primitive %d, state 0x%x",
			d->ok_ack.dl_correct_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->ok_ack.dl_correct_primitive, 0);
			break;
		}
		break;

	case DL_ERROR_ACK:
		switch (d->error_ack.dl_error_primitive) {
		case DL_UNBIND_REQ:
			UNTIMEOUT(samp->sam_timeid);
			/* Check for timeout, state would have changed */
			if (samp->sam_state == DL_UNBIND_PENDING) {
				if (sam_send_detach(mac, samp, NULL, NULL) ==
					SAMFAIL)
					goto reporterr;
			} else {
				/* Timeout occurred, match MAC driver state */
				samp->sam_state = DL_IDLE;
			}
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_UNBIND_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_ERROR_ACK for primitive %d, state 0x%x",
			d->error_ack.dl_error_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->error_ack.dl_error_primitive, 0);
			break;
		}
		break;

	default:
		ILD_LOG(mac->mac_ppa, 0,
		"samDL_UNBIND_PENDING()", SAM_EBADRESPONSE,
		E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
		"Unexpected primitive from driver, primitive %d, state 0x%x",
		d->dl_primitive, samp->sam_state, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);
		/* Ignore invalid response, wait for valid one. */
		break;
	}
	freemsg(mp);
	return;

reporterr:
	/* mac_lock is locked when entering here */
	mp1 = samp->sam_mpsave;
	q1 = samp->sam_qsave;
	samp->sam_mpsave = NULL;
	samp->sam_qsave = NULL;
	samp->sam_state = DL_IDLE;
	ILD_RWUNLOCK(mac->mac_lock);
	ild_uninit_con(mac, q1, mp1, SAMFAIL);
	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
	freemsg(mp);
	ILD_WRLOCK(mac->mac_lock);

}


/*
 * samDL_DETACH_PENDING()
 * ----------------------
 *
 * This routine is called by SAMrput() to process the response to the
 * DL_DETACH_REQ .  Either a DL_OK_ACK or a DL_ERROR_ACK response
 * is expected and either results in the ild_uninit_con() routine being
 * called to complete the un-initialization procedure.
 *
 * Return values: none
 *
 * Locking: mac_lock is locked on entry/exit
 */

/*ARGSUSED*/
void
samDL_DETACH_PENDING(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{
	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;
	mblk_t *mp1;
	queue_t *q1;
	ushort_t new_sam_state;
	int status;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	switch (d->dl_primitive) {
	case DL_OK_ACK:
		switch (d->ok_ack.dl_correct_primitive) {
		case DL_DETACH_REQ:
			status = SAMPASS;
			new_sam_state = DL_UNATTACHED;
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_DETACH_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_OK_ACK for primitive %d, state 0x%x",
			d->ok_ack.dl_correct_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->ok_ack.dl_correct_primitive, 0);
			goto out;
		}
		break;

	case DL_ERROR_ACK:
		switch (d->error_ack.dl_error_primitive) {
		case DL_DETACH_REQ:
			status = SAMFAIL;
			new_sam_state = DL_UNBOUND;
			break;

		default:
			/* Ignore invalid response, wait for correct one */
			ILD_LOG(mac->mac_ppa, 0,
			"samDL_DETACH_PENDING()", SAM_EBADRESPONSE,
			E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
			"Unexpected DL_ERROR_ACK for primitive %d, state 0x%x",
			d->error_ack.dl_error_primitive, samp->sam_state,
			0, 0);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->error_ack.dl_error_primitive, 0);
			goto out;
		}
		break;

	default:
		ILD_LOG(mac->mac_ppa, 0,
		"samDL_DETACH_PENDING()", SAM_EBADRESPONSE,
		E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
		"Unexpected primitive from driver, primitive %d, state 0x%x",
		d->dl_primitive, samp->sam_state, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, d->dl_primitive, 0);
		/* Ignore invalid response, wait for valid one. */
		goto out;
	}

	UNTIMEOUT(samp->sam_timeid);

	/* Check for timeout, state would have changed */
	if (samp->sam_state == DL_DETACH_PENDING) {
		/* No timeout, un-initialization complete, report status. */
		mp1 = samp->sam_mpsave;
		q1 = samp->sam_qsave;
		samp->sam_mpsave = NULL;
		samp->sam_qsave = NULL;
		samp->sam_state = new_sam_state;
		mac->mac_state = MAC_INST;
		ILD_RWUNLOCK(mac->mac_lock);
		ild_uninit_con(mac, q1, mp1, status);
		ILD_WRLOCK(mac->mac_lock);
	} else {
		/*
		 * A timeout occurred before response, error already
		 * reported, so put SAM in the same state as the driver.
		 */
		samp->sam_state = new_sam_state;
	}
out:
	freemsg(mp);

}

/*
 * samDL_OUTofSTATE()
 * ------------------
 *
 * This routine is called when a response is received from the MAC driver
 * when SAM is in a DLPI state that is not expecting a response.  The only
 * such states in SAM are DL_UNATTACHED and DL_UNBOUND and these states are
 * entered only after an error.  The message block is freed and an error is
 * logged.
 *
 * Return values: mac_lock is locked on entry/exit
 *
 * Locking: none
 */

/*ARGSUSED*/
void
samDL_OUTofSTATE(queue_t *q, mblk_t *mp, mac_t *mac, samdata_t *samp)
{
	union DL_primitives *d = (union DL_primitives *)(void *)mp->b_rptr;

	ILD_LOG(mac->mac_ppa, 0,
	"samDL_OUTofSTATE()", SAM_EBADRESPONSE,
	E_SOFTWARE, E_WARNING, __FILE__, __LINE__,
	"Unexpected primitive from driver, primitive %d, state 0x%x",
	d->dl_primitive, samp->sam_state, 0, 0);
	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);
	ild_trace(THIS_MOD, __LINE__, d->dl_primitive, 0, 0);
	freemsg(mp);

}


/*
 * sam_send_attach()
 * -----------------
 *
 * This routine is called by SAMinit_req() to send an attach request
 * to the MAC driver.  A message block is allocated, the DL_ATTACH_REQ
 * is built, and the message is sent downstream.
 *
 * Return values:	SAMPASS successful, attach request sent
 *			SAMFAIL failed, attach request not sent
 *
 * Locking: mac_lock is locked on entry/exit
 */

int
sam_send_attach(mac_t *mac, samdata_t *samp, queue_t *q, mblk_t *mp)
{
	mblk_t *bp;
	dl_attach_req_t *dlar;
	queue_t *lowerq;
	macx_t *macx;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	/* Allocate msg block for attach request. */
	bp = allocb(DL_ATTACH_REQ_SIZE, BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"sam_send_attach()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate attach request", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return (SAMFAIL);
	}

	macx = ild_macxp[mac->mac_ppa];

	/* Build attach request, use instance associated with this mac. */
	dlar = (dl_attach_req_t *)(void *)bp->b_wptr;
	dlar->dl_primitive = DL_ATTACH_REQ;
	dlar->dl_ppa = macx->lower_instance;
	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr += DL_ATTACH_REQ_SIZE;

	/* Set mac state so that unload cannot occur from this point on. */
	mac->mac_state |= MAC_INIT;

	/*
	 * Save msg block and queue so they can be returned when connection
	 * establishment is complete.
	 */
	samp->sam_mpsave = mp;
	samp->sam_qsave = q;

	samp->sam_state = DL_ATTACH_PENDING;

	lowerq = macx->lowerQ;

	samp->sam_timeid = TIMEOUT(sam_timeout, mac, SAMTIMEOUT);

	ILD_RWUNLOCK(mac->mac_lock);

	putnext(lowerq, bp);

	ILD_WRLOCK(mac->mac_lock);

	return (SAMPASS);
}


/*
 * sam_send_bind()
 * ---------------
 *
 * This routine is called to send a bind request to the MAC driver.
 * A message block is allocated, the DL_BIND_REQ is built, and
 * the message is sent downstream.
 *
 * Return values:	SAMPASS successful, bind request sent
 *			SAMFAIL failed, bind request not sent
 *
 * Locking: mac_lock is locked on entry/exit
 */

int
sam_send_bind(mac_t *mac, samdata_t *samp, queue_t *q, mblk_t *mp)
{

	mblk_t *bp;
	dl_bind_req_t *dlbr;
	queue_t *lowerq;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	/* Allocate a msg block for the bind request. */
	bp = allocb(DL_BIND_REQ_SIZE, BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"sam_send_bind()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate bind request", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return (SAMFAIL);
	}

	/* Build bind request */
	dlbr = (dl_bind_req_t *)(void *)bp->b_wptr;
	dlbr->dl_primitive = DL_BIND_REQ;
	dlbr->dl_sap = 1;
	dlbr->dl_max_conind = 0;
	dlbr->dl_service_mode = DL_CLDLS;
	dlbr->dl_conn_mgmt = 0;
	dlbr->dl_xidtest_flg = 0;

	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr += DL_BIND_REQ_SIZE;

	/*
	 * If a q pointer was passed in, save it and the mp to pass
	 * back during init confirmation.
	 */
	if (q) {
		samp->sam_mpsave = mp;
		samp->sam_qsave = q;
	}

	samp->sam_state = DL_BIND_PENDING;
	samp->sam_timeid = TIMEOUT(sam_timeout, mac, SAMTIMEOUT);
	lowerq = ild_macxp[mac->mac_ppa]->lowerQ;
	ILD_RWUNLOCK(mac->mac_lock);

	putnext(lowerq, bp);

	ILD_WRLOCK(mac->mac_lock);

	return (SAMPASS);
}


/*
 * sam_send_unbind()
 * -----------------
 *
 * This routine is called to send an unbind request to the MAC driver.
 * A message block is allocated, the DL_UNBIND_REQ is built, and
 * the message is sent downstream.
 *
 * Return values:	SAMPASS successful, unbind request sent
 *			SAMFAIL failed, unbind request not sent
 *
 * Locking: mac_lock is locked/unlocked
 */

int
sam_send_unbind(mac_t *mac, samdata_t *samp, queue_t *q, mblk_t *mp)
{
	mblk_t *bp;
	queue_t *lowerq;
	dl_unbind_req_t *dlur;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);

	/* Allocate msg block for unbind request. */
	bp = allocb(DL_UNBIND_REQ_SIZE, BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"sam_send_unbind()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate unbind request", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return (SAMFAIL);
	}

	/* Build unbind request */
	dlur = (dl_unbind_req_t *)(void *)bp->b_wptr;
	dlur->dl_primitive = DL_UNBIND_REQ;
	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr += DL_UNBIND_REQ_SIZE;

	/*
	 * Save msg block and queue so they can be returned to LLC2
	 * when uninit_req is complete (when response to unattach request
	 * is received).
	 */
	samp->sam_mpsave = mp;
	samp->sam_qsave = q;

	samp->sam_state = DL_UNBIND_PENDING;

	lowerq = ild_macxp[mac->mac_ppa]->lowerQ;

	samp->sam_timeid = TIMEOUT(sam_timeout, mac, SAMTIMEOUT);

	ILD_RWUNLOCK(mac->mac_lock);

	putnext(lowerq, bp);

	ILD_WRLOCK(mac->mac_lock);

	return (SAMPASS);
}


/*
 * sam_send_detach()
 * -----------------
 *
 * This routine is called to send a detach request to the MAC driver.
 * A message block is allocated, the DL_DETACH_REQ is built, and
 * the message is sent downstream.
 *
 * Return values: 	SAMPASS successful, detach request sent
 *			SAMFAIL failed, detach request not sent
 *
 * Locking: mac_lock is locked on entry/exit
 */

int
sam_send_detach(mac_t *mac, samdata_t *samp, queue_t *q, mblk_t *mp)
{

	mblk_t *bp;
	dl_detach_req_t *dldr;
	queue_t *lowerq;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	/* Allocate msg block for unbind request. */
	bp = allocb(DL_DETACH_REQ_SIZE, BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"sam_send_detach()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate detach request", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return (SAMFAIL);
	}

	/* Build detach request */
	dldr = (dl_detach_req_t *)(void *)bp->b_wptr;
	dldr->dl_primitive = DL_DETACH_REQ;

	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr += DL_DETACH_REQ_SIZE;

	samp->sam_state = DL_DETACH_PENDING;
	if (q) {
		samp->sam_mpsave = mp;
		samp->sam_qsave = q;
	}
	lowerq = ild_macxp[mac->mac_ppa]->lowerQ;
	samp->sam_timeid = TIMEOUT(sam_timeout, mac, SAMTIMEOUT);
	ILD_RWUNLOCK(mac->mac_lock);

	putnext(lowerq, bp);

	ILD_WRLOCK(mac->mac_lock);

	return (SAMPASS);
}



/*
 * sam_send_inforeq()
 * -----------------
 *
 * This routine is called to send an info request to the MAC driver.
 * A message block is allocated, the DL_INFO_REQ is built, and
 * the message is sent downstream.
 *
 * Return values: 	SAMPASS successful, detach request sent
 *			SAMFAIL failed, detach request not sent
 *
 * Locking: mac_lock is locked on entry/exit
 */

/*ARGSUSED*/
int
sam_send_inforeq(mac_t *mac, samdata_t *samp, queue_t *q, mblk_t *mp)
{

	mblk_t *bp;
	dl_info_req_t *dlin;
	queue_t *lowerq;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);

	MP_ASSERT(ILD_LOCKMINE(mac->mac_lock));

	/* Allocate msg block for inforeq request. */
	bp = allocb(DL_INFO_REQ_SIZE, BPRI_HI);

	if (bp == NULL) {
		ILD_LOG(mac->mac_ppa, 0,
			"sam_send_inforeq()", SAM_ENOSPACE,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Failed to allocate info request", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return (SAMFAIL);
	}

	/* Build info request */
	dlin = (dl_info_req_t *)(void *)bp->b_wptr;
	dlin->dl_primitive = DL_INFO_REQ;

	bp->b_datap->db_type = M_PROTO;
	bp->b_wptr += DL_INFO_REQ_SIZE;

	lowerq = ild_macxp[mac->mac_ppa]->lowerQ;
	samp->sam_timeid = TIMEOUT(sam_timeout, mac, SAMTIMEOUT);
	ILD_RWUNLOCK(mac->mac_lock);

	putnext(lowerq, bp);

	ILD_WRLOCK(mac->mac_lock);

	return (SAMPASS);
}


/*
 * sam_timeout()
 * -------------
 *
 * This routine is called when SAM's protective timer expires while
 * waiting for a response from the MAC driver.  Depending on the current
 * state, an error will be reported to ild_init_con() or ild_uninit_con().
 *
 * Return values: none
 *
 * Locking: mac_lock is locked/unlocked
 */

void
sam_timeout(void *arg)
{
	mac_t *mac = (mac_t *)arg;
	samdata_t *samp;
	ushort_t new_sam_state;
	int change_mac_state;
	void (*ild_con_routine)(mac_t *, queue_t *, mblk_t *, ushort_t);
	mblk_t *mp1;
	queue_t *q1;


	ILD_WRLOCK(mac->mac_lock);
	samp = ild_macxp[mac->mac_ppa]->UserDataPtr;

	ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);

	switch (samp->sam_state) {
	case DL_ATTACH_PENDING:
		new_sam_state = DL_UNATTACHED;
		change_mac_state = FALSE;
		ild_con_routine = ild_init_con;
		break;
	case DL_BIND_PENDING:
		new_sam_state = DL_UNBOUND;
		change_mac_state = FALSE;
		ild_con_routine = ild_init_con;
		break;
	case DL_IDLE:
		/* Timeout occurred while waiting for DL_INFO_ACK */
		samp->sam_timeid = 0;
		ILD_RWUNLOCK(mac->mac_lock);
		ILD_LOG(mac->mac_ppa, 0,
			"sam_timeout()", SAM_ETIMEOUT,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Timeout: No DL_INFO_ACK received", 0, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);
		return;
	case DL_UNBIND_PENDING:
		new_sam_state = DL_IDLE;
		change_mac_state = FALSE;
		ild_con_routine = ild_uninit_con;
		break;
	case DL_DETACH_PENDING:
		new_sam_state = DL_UNBOUND;
		/*
		 * Turn off MAC_INIT if detach times out.  Allows LLC2 to
		 * unload, if necessary.
		 */
		change_mac_state = TRUE;
		ild_con_routine = ild_uninit_con;
		break;
	default:
		/*
		 * Should be impossible to get here, timer is only started
		 * in one of the above states.
		 */
		ILD_RWUNLOCK(mac->mac_lock);
		ILD_LOG(mac->mac_ppa, 0,
			"sam_timeout()", SAM_ETIMEOUT,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Invalid state for timeout: DLPI state 0x%x",
			samp->sam_state, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);
		return;

	}

	/* Check if timer has just been canceled */
	if (samp->sam_timeid) {
		ushort_t orig_sam_state;

		/* Valid timeout, report failure */
		samp->sam_timeid = 0;
		mp1 = samp->sam_mpsave;
		q1 = samp->sam_qsave;
		samp->sam_mpsave = NULL;
		samp->sam_qsave = NULL;
		orig_sam_state = samp->sam_state;
		samp->sam_state = new_sam_state;
		if (change_mac_state == TRUE)
			mac->mac_state = MAC_INST;
		ILD_RWUNLOCK(mac->mac_lock);
		(*ild_con_routine)(mac, q1, mp1, SAMFAIL);

		ILD_LOG(mac->mac_ppa, 0,
			"sam_timeout()", SAM_ETIMEOUT,
			E_ENVIRONMENT, E_WARNING, __FILE__, __LINE__,
			"Timeout waiting for response, DLPI state 0x%x",
			orig_sam_state, 0, 0, 0);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, samp->sam_state, 0);

	} else {
		ILD_RWUNLOCK(mac->mac_lock);
	}

}
