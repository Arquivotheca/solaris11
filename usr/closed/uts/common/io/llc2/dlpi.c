/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * dlpi.c
 *
 * Copyright (c) 1991-1998 NCR Corporation, Dayton, Ohio
 * Copyright (c) 1990 NCR Corporation, Dayton, Ohio
 */

/*
 * The PSARC case number PSARC 1998/360 contains additional material
 * including the design document for this driver.
 */

/*
 *	Locking Scheme:
 *
 *	 This follows the same scheme implemented by jpm in the
 *	 original multiprocessing version of the driver.
 *
 *	The following locks are defined for the DLPI part of the driver:
 *
 *	ild_lnk_lck link_t global lock
 *	ild_glnk_lck  link_t per PPA lock
 *	lk_lock	link_t individual structure lock
 *
 *	The global lock on held only when a link_t structure is moving
 *	from the master linked list of free link_t structures onto
 *	one of the per PPA lists.
 *
 *	The ild_glnk_lck is held whenever a list for a particular
 *	adapter is being searched for a specific link_t structure.
 *	By having a separate lock for each PPA, input from multiple
 *	links can be processed simultaneously by multiple service
 *	routines running on different processors.
 *	When the specific link_t structure is located, it is then
 *	locked and the ild_glnk_lck released.
 *
 *	The hierarchy order is the same order the locks are shown
 *	in the list above.
 *
 *	mac_lock	mac_t individual structure lock
 *
 *	The mac_lock covers the mac_t structure and the dlpi_flow
 *	structure and MCA accesses.
 *
 *	The locking macros, structures, etc. are defined in
 *	ildlock.h.
 *
 *	The ild lock structure (ild_lock_t) contains a pointer
 *	to the actual lock structure and the value of the interrupt
 *	priority level.  When a lock routine is called, the actual
 *	lock structure is passed in.  On return, the ipl return
 *	value is saved in the ild lock structure.  When unlocking
 *	the lock, the saved ipl value is passed to the unlock
 *	routine for restoration of the ipl.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#define	DLPI_C

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/mkdev.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/log.h>
#include <sys/stropts.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/ddi.h>
#include <sys/byteorder.h>


#include "ildlock.h"

#include <sys/dlpi.h>
#include <sys/llc2.h>
#include "ild.h"
#include "llc2k.h"

/*
 * Externals
 */
extern  int		ild_maccnt;
extern  int		ild_execution_options;
extern	mac_t		*ild_macp[];
extern	macx_t		*ild_macxp[];
extern	int 		ild_max_ppa;
extern  uint_t		ild_minors[];
extern  int		dlpi_lnk_cnt;
extern	dLnkLst_t  	mac_lnk[];
extern	listenQ_t 	*listenQueue[];

/*
 * Globals
 */

/* muoe933145 09/23/93 */
dLnkLst_t lnkFreeHead = { &lnkFreeHead, &lnkFreeHead };
extern ild_lock_t  ild_glnk_lck[];
static uchar_t nullmac[6] = {0, 0, 0, 0, 0, 0};
static uchar_t ildbrdcst[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 * Locals
 */
#define	THIS_MOD MID_DLPI

void fDL_UNBOUND(queue_t *, mblk_t *);
void fDL_BIND_PENDING(queue_t *, mblk_t *);
void fDL_UNBIND_PENDING(queue_t *, mblk_t *);
void fDL_IDLE(queue_t *, mblk_t *);
void fDL_UNATTACHED(queue_t *, mblk_t *);
void fDL_ATTACH_PENDING(queue_t *, mblk_t *);
void fDL_DETACH_PENDING(queue_t *, mblk_t *);
void fDL_UDQOS_PENDING(queue_t *, mblk_t *);
void fDL_OUTCON_PENDING(queue_t *, mblk_t *);
void fDL_INCON_PENDING(queue_t *, mblk_t *);
void fDL_CONN_RES_PENDING(queue_t *, mblk_t *);
void fDL_DATAXFER(queue_t *, mblk_t *);
void fDL_USER_RESET_PENDING(queue_t *, mblk_t *);
void fDL_PROV_RESET_PENDING(queue_t *, mblk_t *);
void fDL_RESET_RES_PENDING(queue_t *, mblk_t *);
void fDL_DISCON8_PENDING(queue_t *, mblk_t *);
void fDL_DISCON9_PENDING(queue_t *, mblk_t *);
void fDL_DISCON11_PENDING(queue_t *, mblk_t *);
void fDL_DISCON12_PENDING(queue_t *, mblk_t *);
void fDL_DISCON13_PENDING(queue_t *, mblk_t *);
void fDL_SUBS_BIND_PND(queue_t *, mblk_t *);
void
dlpi_bind_ack(queue_t *q, dlsap_t *ssap, uint_t max_conind,
				int enet);
void
dlpi_error_ack(queue_t *q, uint_t dl_error_primitive, int dl_errno,
				int dl_unix_errno);
void dlpi_ok_ack(queue_t *q, uint_t dl_correct_primitive);
outInd_t *dlpiAllocOutInd(void);
int addOutInd(link_t *lnk, ushort_t sid, dlsap_t *remAddr);
outInd_t *peekOutInd(link_t *lnk, ushort_t sid);
int delOutInd(link_t *lnk, ushort_t sid);
listenQ_t *dlpiAllocListenQ(void);
int dlpiAddListenQ(listenQ_t **head, link_t *lnk);
int dlpiDelListenQ(listenQ_t **head, link_t *lnk);
int dlpi_check_multicast(link_t *, uchar_t *);
void dlpi_flush_multicast(mac_t *, link_t *);
int dlpi_remove_multicast(link_t *, mac_addr_t *);
int dlpi_disconnect_req(link_t *, uint_t);
int dlpi_unbind_req(link_t *);
void dlpi_info_req(queue_t *);
void dlpi_info_req(queue_t *);

/* a few more prototypes */
int  ild_get_nxt_minor(uint_t *);
void ild_clear_bit(int, uint_t *);

/* some extern prototypes */
extern int llc2BindReq(link_t *, uint_t, uint_t);
extern int llc2UnbindReq(mac_t *, uint_t);
extern int
llc2UnitdataReq(mac_t *, dlsap_t *, dlsap_t *, mblk_t *,
				snaphdr_t *, int, uint_t);
extern int llc2TestReq(mac_t *, dlsap_t *, dlsap_t *, mblk_t *, uint_t);
extern int
llc2XidReq(mac_t *, dlsap_t *, dlsap_t *, uint_t, uint_t,
			mblk_t *, uint_t);
extern int llc2DataReq(mac_t *, uint_t, mblk_t *, dlsap_t *);
extern int llc2ConnectRes(mac_t *, link_t *, dlsap_t *, dlsap_t *, ushort_t);
extern int llc2ConnectReq(mac_t *, link_t *, dlsap_t *, dlsap_t *);
extern int llc2DisconnectReq(mac_t *, ushort_t);
extern int llc2ResetRes(mac_t *, ushort_t);
extern int llc2ResetReq(mac_t *, ushort_t);
extern mblk_t *llc2_allocb(size_t, uint_t);


/* Jump Table for DLPI functions - index into table is current DLPI state */
void (*fDLPI[])(queue_t *, mblk_t *) = {
	fDL_UNBOUND,		/* PPA attached */
	fDL_BIND_PENDING,	/* Waiting ack of DL_BIND_REQ */
	fDL_UNBIND_PENDING,	/* Waiting ack of DL_UNBIND_REQ */
	fDL_IDLE,		/* dlsap bound, awaiting use */
	fDL_UNATTACHED,		/* PPA not attached */
	fDL_ATTACH_PENDING,	/* Waiting ack of DL_ATTACH_REQ */
	fDL_DETACH_PENDING,	/* Waiting ack of DL_DETACH_REQ */
	fDL_UDQOS_PENDING,	/* Waiting ack of DL_UDQOS_REQ */
	fDL_OUTCON_PENDING,	/* outgoing connection, awaiting DL_CONN_CON */
	fDL_INCON_PENDING,	/* incoming connection, awaiting DL_CONN_RES */
	fDL_CONN_RES_PENDING,  	/* Waiting ack of DL_CONNECT_RES */
	fDL_DATAXFER,		/* connection-oriented data transfer */
	fDL_USER_RESET_PENDING,	/* user initiated reset, waiting DL_RESET_CON */
	fDL_PROV_RESET_PENDING,	/* provider init reset, awaiting DL_RESET_RES */
	fDL_RESET_RES_PENDING, 	/* Waiting ack of DL_RESET_RES */
	/* Waiting ack of DL_DISC_REQ when DL_OUTCON_PENDING */
	fDL_DISCON8_PENDING,
	/* Waiting ack of DL_DISC_REQ when DL_INCON_PENDING */
	fDL_DISCON9_PENDING,
	/* Waiting ack of DL_DISC_REQ when DL_DATAXFER */
	fDL_DISCON11_PENDING,
	/* Waiting ack of DL_DISC_REQ when DL_USER_RESET_PENDING */
	fDL_DISCON12_PENDING,
	/* Waiting ack of DL_DISC_REQ when DL_DL_PROV_RESET_PENDING */
	fDL_DISCON13_PENDING,
	fDL_SUBS_BIND_PND	/* Waiting ack of DL_SUBS_BIND_REQ */
};

/* Locks */
ILD_DECL_LOCK(ild_lnk_lock);
ILD_DECL_LOCK(ild_listenq_lock);
ILD_EDECL_LOCK(global_mac_lock);

/*
 * allocate an outInd_t queue element
 *
 * note: the 'next' element of the returned outInd_t element is the
 *	responsibility of the caller and not set to any particular value
 *	by this function
 */
outInd_t
*dlpiAllocOutInd(void)
{
	outInd_t *ptr = NULL;

	ptr = (outInd_t *)kmem_zalloc(sizeof (outInd_t), KM_NOSLEEP);
	return (ptr);
}

void
dlpiFreeOutInd(outInd_t *ptr)
{

	ptr->next = NULL;
	kmem_free((void *)ptr, sizeof (outInd_t));
}

int
addOutInd(link_t *lnk, ushort_t sid, dlsap_t *sap)
{
	outInd_t *tmp;

	if ((tmp = dlpiAllocOutInd()) != NULL) 	{
		tmp->sid = sid;
		tmp->remAddr = *sap;
		tmp->next = NULL;
		if (lnk->lkOutIndHead == NULL)  {
			lnk->lkOutIndHead = lnk->lkOutIndTail = tmp;
			return (0);
		}
		lnk->lkOutIndTail->next = tmp;
		lnk->lkOutIndTail = tmp;
		return (0);
	}
	return (1);
}

outInd_t
*peekOutInd(link_t *lnk, ushort_t sid)
{
	outInd_t *tmp = lnk->lkOutIndHead;

	while (tmp != NULL) 	{
		if (tmp->sid == sid)  {
			break;
		}
		tmp = tmp->next;
	}
	return (tmp);
}

int
delOutInd(link_t *lnk, ushort_t sid)
{
	outInd_t *tmp = lnk->lkOutIndHead;
	outInd_t *otmp = NULL;

	while (tmp != NULL) 	{
		if (tmp->sid == sid)  {
			if (otmp == NULL)  {
				lnk->lkOutIndHead = lnk->lkOutIndHead->next;
			} else if (tmp == lnk->lkOutIndTail)  {
				lnk->lkOutIndTail = otmp;
				lnk->lkOutIndTail->next = NULL;
			} else {
				otmp->next = tmp->next;
			}
			dlpiFreeOutInd(tmp);
			return (0);
		}
		otmp = tmp;
		tmp = tmp->next;
	}
	return (1);
}

/*
 * allocate an listenQ_t queue element
 *
 * note: the 'next' element of the returned outInd_t element is the
 *	responsibility of the caller and not set to any particular value
 *	by this function
 */
listenQ_t
*dlpiAllocListenQ(void)
{
	listenQ_t *ptr = NULL;

	ptr = (listenQ_t *)kmem_zalloc(sizeof (listenQ_t), KM_NOSLEEP);
	return (ptr);
}

int
dlpiAddListenQ(listenQ_t **head, link_t *lnk)
{
	listenQ_t *tmp;

	ILD_WRLOCK(ild_listenq_lock);
	if ((tmp = dlpiAllocListenQ()) != NULL) 	{
		tmp->lnk = lnk;
		if (*head == NULL)  {
			tmp->next = NULL;
			*head = tmp;
			ILD_RWUNLOCK(ild_listenq_lock);
			return (0);
		}
		tmp->next = *head;
		*head = tmp;
		ILD_RWUNLOCK(ild_listenq_lock);
		return (0);
	}
	ILD_RWUNLOCK(ild_listenq_lock);
	return (1);
}

int
dlpiDelListenQ(listenQ_t **head, link_t *lnk)
{
	listenQ_t *tmp = *head;
	listenQ_t *otmp = NULL;

	ILD_WRLOCK(ild_listenq_lock);
	while (tmp != NULL) 	{
		if (tmp->lnk == lnk)  {
			if (otmp == NULL)  {
				*head = tmp->next;
			} else {
				otmp->next = tmp->next;
			}
			tmp->next = NULL;
			tmp->lnk = NULL; /* muoe932415 buzz 8/23/93 */
			kmem_free((void *)tmp, sizeof (listenQ_t));
			ILD_RWUNLOCK(ild_listenq_lock);
			return (0);
		}
		otmp = tmp;
		tmp = tmp->next;
	}
	ILD_RWUNLOCK(ild_listenq_lock);
	return (0);
}

/* GLOBAL PROCEDURES (called by ild.c) */

/*
 * Function Label:	dlpi_init_lnks
 *
 *
 * Description:		Start-of-day initialization of link structures
 * Locks:		ild_lnk_lock and lnk->lk_lock are allocated.  No locks
 * 			required to be set here because only one cpu is running.
 */
void
dlpi_init_lnks(void)
{
	int indx;

	/* Allocate global lock structure */

	ILD_RWALLOC_LOCK(&ild_lnk_lock, ILD_LCK1, NULL);
	ILD_RWALLOC_LOCK(&ild_listenq_lock, ILD_LCK1, NULL);

	/* initialize dlpi_link list structures */
	for (indx = 0; indx < ild_max_ppa; indx += 1) 	{
		listenQueue[indx] = NULL;
		ILD_RWALLOC_LOCK(&ild_glnk_lck[indx], ILD_LCK1, NULL);
		mac_lnk[indx].flink = &mac_lnk[indx];
		mac_lnk[indx].blink = &mac_lnk[indx];
	}
	/*
	 * initialize the "Master" dlpi_link list
	 */
	mac_lnk[ild_max_ppa].flink = &mac_lnk[ild_max_ppa];
	mac_lnk[ild_max_ppa].blink = &mac_lnk[ild_max_ppa];


}

/*
 * Function Label:	dlpi_getfreelnk
 * Description:		find an unused link structure, return indx to caller
 * Locks:		No locks set on entry.
 * 			returned link structure is WR locked on exit.
 */
int
dlpi_getfreelnk(link_t **nlink)
{
	int indx;
	link_t *lnk = NULL;
	dLnkLst_t *headlnk; /* muoe933145 09/23/93 */

	ILD_WRLOCK(ild_lnk_lock);
	headlnk = &lnkFreeHead; /* muoe941184 buzz 04/25/94 */

	if ((indx = ild_get_nxt_minor(&ild_minors[0])) != -1) {
		/* Should be the least recently used */
		lnk = (link_t *)headlnk->blink;
		/* muoe933145 09/23/93 buzz */
		if (lnk == (link_t *)headlnk) {
			/* free list empty, allocate a new one */
			ILD_RWUNLOCK(ild_lnk_lock);
			if ((lnk = (link_t *)kmem_zalloc(sizeof (link_t),
							KM_NOSLEEP)) != NULL) {
				lnk->lk_wrq = NULL;
				lnk->lk_mac = NULL;
				lnk->lk_state = DL_UNATTACHED;
				lnk->lk_status = LKCL_OKAY;
				lnk->lk_rnr = FALSE;
				lnk->lk_close = 0;
				lnk->lkOutIndHead = NULL;
				lnk->lkOutIndTail = NULL;
				lnk->lkPendHead = NULL;
				lnk->lkPendTail = NULL;
				lnk->lkDisIndHead = NULL;
				lnk->lkDisIndTail = NULL;

				/* muoe962955 buzz 10/30/96 */
				ILD_SLEEP_ALLOC(lnk->sleep_var);
				ILD_RWALLOC_LOCK(&lnk->lk_lock, ILD_LCK2, NULL);
				ILD_WRLOCK(ild_lnk_lock);
				ADD_DLNKLST(&lnk->chain, &mac_lnk[ild_max_ppa]);
				ILD_WRLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_lnk_lock);
				*nlink = lnk;
			} else {
				/* "give back" the minor number */
				ild_clear_bit(indx, &ild_minors[0]);
				indx = -1;
			}
		} else {
			/* One available on the free list, reuse it */
			RMV_DLNKLST(&lnk->chain);
			lnk->lk_wrq = NULL;
			lnk->lk_mac = NULL;
			lnk->lk_state = DL_UNATTACHED;
			lnk->lk_status = LKCL_OKAY;
			lnk->lk_rnr = FALSE;
			lnk->lk_close = 0;
			lnk->lkOutIndHead = NULL;
			lnk->lkOutIndTail = NULL;
			lnk->lkPendHead = NULL;
			lnk->lkPendTail = NULL;
			lnk->lkDisIndHead = NULL;
			lnk->lkDisIndTail = NULL;
			ADD_DLNKLST(&lnk->chain, &mac_lnk[ild_max_ppa]);
			ILD_WRLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_lnk_lock);
			*nlink = lnk;
		}
	} else { /* muoe933154 09/23/93 buzz */
		ILD_RWUNLOCK(ild_lnk_lock);
	}
	ild_trace(MID_DLPI, __LINE__, indx, (uintptr_t)lnk, 0);
	return (indx);

}

/*
 * Function Label:	dlpi_state
 * Description:		jump to the servicing routine based on the current link
 * state.  		This routine is the entry point from ild.c.
 * Locks:		No locks set on entry/exit.
 * 			lnk->lk_lock is locked/unlocked.
 */
void
dlpi_state(queue_t *q, mblk_t *mp)
{
	link_t *lnk = (link_t *)q->q_ptr;
	uint_t prim;

	ILD_WRLOCK(ild_lnk_lock);
	if (lnk) {
		ILD_WRLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_lnk_lock);

		if (lnk->lk_state <= DL_MAXSTATE) {
			fDLPI[lnk->lk_state](q, mp); /* structures are locked */
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);

			prim = *((uint_t *)mp->b_rptr);

			/* Handle a DL_INFO_REQ even if there in no lnk */
			if (prim == DL_INFO_REQ)
				dlpi_info_req(q);

			freemsg(mp);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			cmn_err(CE_WARN, "!LLC2: invalid DLPI state 0x%x\n",
				lnk->lk_state);
			freemsg(mp);
		}
	} else {
		ILD_RWUNLOCK(ild_lnk_lock);
		prim = *((uint_t *)mp->b_rptr);

		/* Handle a DL_INFO_REQ even if there is no lnk */
		if (prim == DL_INFO_REQ)
			dlpi_info_req(q);

		freemsg(mp);
	}
}


/*
 * Function Label:	dlpi_data
 * Description:		called by ild.c when M_DATA is received
 * Locks:		No locks set on entry/exit.
 * 			lnk->lk_lock is locked/unlocked.
 */
int
dlpi_data(queue_t *q, mblk_t *mp, int inserv)
{
	int msize;
	link_t *lnk = (link_t *)q->q_ptr;
	mac_t *mac;
	ushort_t sid;
	dlsap_t *rem;
	int retVal = 0;

	if (lnk == NULL) {
		freemsg(mp);
		return (0);
	}

	ILD_WRLOCK(lnk->lk_lock);
	if (lnk->lk_state == DL_DATAXFER) 	{
		/*
		 * request the llc to transmit the frame, if the return code is
		 * non-zero, the llc can't transmit now, so put the frame back
		 * on the queue
		 */

		/*
		 * Verify proper amount of data in frame.  If incorrect then
		 * generate an M_ERROR message and have application close the
		 * stream.
		 */
		msize = msgdsize(mp);
		if ((msize > lnk->lk_max_sdu) || (msize < lnk->lk_min_sdu))  {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			if (mp->b_cont != NULL)  {
				freemsg(mp->b_cont);
				mp->b_cont = NULL;
			}
			mp->b_datap->db_type = M_ERROR;
			mp->b_rptr = mp->b_datap->db_base;
			mp->b_wptr = mp->b_rptr + 1;
			*mp->b_rptr = EPROTO;
			if (q)
				qreply(q, mp);
			else
				freemsg(mp);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			mac = lnk->lk_mac;
			sid = lnk->lk_sid;
			rem =  &lnk->lk_dsap;
			ILD_RWUNLOCK(lnk->lk_lock);

			if ((retVal = llc2DataReq(mac, sid, mp, rem)) != 0) {
				/* Oops! Bad return */
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)q,
							0, 0);
				/*
				 * sturblen muoe971965 08/07/97
				 * need to recheck lk_state, it may
				 * have changed underneath us after
				 * we released the lk_lock and before
				 * llc2DataReq was able to process the
				 * data message
				 */
				ILD_WRLOCK(lnk->lk_lock);
				if (lnk->lk_state == DL_DATAXFER) {
					/*
					 * if called from the service
					 * procedure must do putbq,
					 * else we were called
					 * from the put routine and
					 * must do a putq
					 */
					if (inserv)
						(void) putbq(q, mp);
					else {
						(void) putq(q, mp);
						ild_trace(MID_DLPI, __LINE__,
								0, 0, 0);
					}
					ILD_RWUNLOCK(lnk->lk_lock);
				} else {
					ild_trace(MID_DLPI, __LINE__,
							(uintptr_t)lnk,
							lnk->lk_state, 0);
					ILD_RWUNLOCK(lnk->lk_lock);
					freemsg(mp);
				}
			}

		}
	} else if ((lnk->lk_state == DL_IDLE) ||
			(lnk->lk_state == DL_PROV_RESET_PENDING)) 	{
		/*
		 * throw the frame away without generating an error
		 */

		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
	} else {
		/*
		 * if the link is in the wrong state, generate an M_ERROR
		 * message the application must then close the stream
		 */
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		if (mp->b_cont != NULL)  {
			freemsg(mp->b_cont);
			mp->b_cont = NULL;
		}
		mp->b_datap->db_type = M_ERROR;
		mp->b_rptr = mp->b_datap->db_base;
		mp->b_wptr = mp->b_rptr + 1;
		*mp->b_rptr = EPROTO;
		if (q)
			qreply(q, mp);
		else
			freemsg(mp);
		ILD_RWUNLOCK(lnk->lk_lock);
	}
	return (retVal);
}

/*
 * called by ild.c when driver is closed.  cleans up for the DLS user.
 */
/*
 * Function Label:	dlpi_close
 * Description:		comment
 * Locks:		ild_lnk_lock WR locked on entry/exit.
 * 			ild_glnk_lck[mac_ppa] is WR locked/unlocked.
 * 			lnk->lk_lock is WR locked/unlocked.
 */
void
dlpi_close(link_t *lnk)
{
	int tries, sleep_ret;
	ushort_t origState;
	mac_t *mac;

	ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 1);
	/*
	 * if this is a primary side connection stream or a stream on which a
	 * connection has been established issue a disconnect request
	 *
	 * it is critical that the link in question be in an expected pending
	 * state whenever sleep() is called, either through sleep() directly or
	 * delay() indirectly, if the dlpi_disconnect_req() or dlpi_unbind_req()
	 * is accepted (returns 0) then the appropriate state change has been
	 * taken care of, but around delay() calls the appropriate pending link
	 * state must before the call and the appropriate actual link state
	 * set on return from the call, this is necessary to prevent the
	 * possibility of a call from a lower layer succeeding when the link is
	 * sleeping in close processing
	 */
	ILD_WRLOCK(lnk->lk_lock);
	origState = lnk->lk_state;

	/* muoe952936 upport from 2d  24/10/95 ranga */
	lnk->lk_close |= LKCL_PEND;
	lnk->lk_close &= ~(LKCL_FAIL | LKCL_OKAY);
	/* end muoe952936	*/

	if ((lnk->lk_state == DL_DATAXFER) ||
		(lnk->lk_state == DL_OUTCON_PENDING) ||
		(lnk->lk_state == DL_USER_RESET_PENDING) ||
		(lnk->lk_state == DL_PROV_RESET_PENDING)) 	{
		tries = 10;
		ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
		while (tries > 0)  {
			if (dlpi_disconnect_req(lnk, 0) == 0)  {
				/*
				 * command accepted by lower layer, go to sleep
				 * waiting for confirmation to arrive
				 */
				while ((lnk->lk_close & (LKCL_FAIL |
							LKCL_OKAY)) == 0)  {
					lnk->lk_close |= LKCL_WAKE;
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_SLEEP(lnk->sleep_var, lnk->lk_lock,
							sleep_ret);
					lnk->lk_close &= ~LKCL_WAKE;
					/*
					 * zero return from ILD_SLEEP means we
					 * were hit with a signal vs. a normal
					 * wakeup
					 */
					if (sleep_ret == 0)
						lnk->lk_close |= LKCL_FAIL;
				}
				if (lnk->lk_close & LKCL_FAIL)  {
					ild_trace(MID_DLPI, __LINE__, 0, 0, 1);

				}
				/* muoe970517:	Shiv 03/20/97 */
				if (lnk->lk_state == DL_UNATTACHED)
					origState = DL_UNATTACHED;
				break;
			} else {
				/*
				 * possible resource error, sleep for a short
				 * while and then try again, must fake a pending
				 * state to prevent stuff from lower layers from
				 * being sent any further up
				 */
				lnk->lk_state = DL_DISCON11_PENDING;
				ILD_RWUNLOCK(lnk->lk_lock);
				delay(5);
				ILD_WRLOCK(lnk->lk_lock);
				lnk->lk_state = DL_DATAXFER;
			}
			tries -= 1;
		}
		lnk->lk_close &= ~(LKCL_FAIL | LKCL_OKAY);
	}
	/*
	 * if this is a listening stream take care of any pending disconnect
	 * indications as well as pending and outstanding connect indications
	 */
	if (lnk->lk_bind.bd_max_conind > 0) 	{

		/*
		 * simply discard any disconnect indications that are pending
		 */
		while (lnk->lkDisIndHead != NULL)  {
			mblk_t *mp = lnk->lkDisIndHead;
			lnk->lkDisIndHead = lnk->lkDisIndHead->b_next;
			mp->b_next = NULL;
			freemsg(mp);
		}
		lnk->lkDisIndTail = NULL;

		/*
		 * take care of any pending connect indications if lk_mac is
		 * valid by issuing disconnect request for all, in either case
		 * discard queue elements
		 */
		mac = lnk->lk_mac;
		if ((mac != NULL) && (lnk->lk_state != DL_UNATTACHED))  {
			while (lnk->lkPendHead != NULL)  {
				mblk_t *mp = lnk->lkPendHead;
				ushort_t sid =
					(ushort_t)(((dl_connect_ind_t *)
						mp->b_rptr)->dl_correlation);
				/* muoe941849 buzz 06/21/94 */

				/* DEL */
				/* lnk->lkPendHead = lnk->lkPendHead->b_next; */
				lnk->lk_state = DL_INCON_PENDING;
				tries = 10;
				while (tries > 0) {
					if (dlpi_disconnect_req(lnk, sid)
						== 0) {
						/*
						 * command accepted by lower
						 * layer, go to sleep waiting
						 * for  confirmation to arrive
						 */
						while ((lnk->lk_close &
							(LKCL_FAIL | LKCL_OKAY))
							== 0)  {
						lnk->lk_close |= LKCL_WAKE;
						/* muoe961994 buzz 6/17/96 */
						/* muoe962955 buzz 10/30/96 */
						ILD_SLEEP(lnk->sleep_var,
							lnk->lk_lock,
							sleep_ret);
						lnk->lk_close &= ~LKCL_WAKE;
						/*
						 * zero return from ILD_SLEEP
						 * means we were hit with
						 * a signal vs. a normal wakeup
						 */
						if (sleep_ret == 0)
						lnk->lk_close |= LKCL_FAIL;
						}
						if (lnk->lk_close & LKCL_FAIL) {
						ild_trace(MID_DLPI, __LINE__, 0,
							0, 1);
						}
						break;
					} else {
						/*
						 * possible resource error,
						 * sleep for a short while and
						 * then try again, must fake a
						 * pending state to prevent
						 * stuff from lower layers from
						 * being sent any further up
						 */
						lnk->lk_state =
							DL_DISCON9_PENDING;
						ILD_RWUNLOCK(lnk->lk_lock);
						delay(5);
						ILD_WRLOCK(lnk->lk_lock);
						lnk->lk_state =
							DL_INCON_PENDING;
					}
					tries -= 1;
				}
				lnk->lk_close &= ~(LKCL_FAIL | LKCL_OKAY);
				lnk->lkPendHead = lnk->lkPendHead->b_next;
				mp->b_next = NULL;
				freemsg(mp);
				/* muoe970517:	Shiv 03/20/97 */
				if (lnk->lk_state == DL_UNATTACHED) {
					origState = DL_UNATTACHED;
					/*
					 * if the link is now UNATTACHED,
					 * dump any remaining pending connect
					 * indications.
					 */
					mp = lnk->lkPendHead;
					while (mp != NULL) {
						lnk->lkPendHead =
							lnk->lkPendHead->b_next;
						mp->b_next = NULL;
						freemsg(mp);
						mp = lnk->lkPendHead;
					}
					break;
				}
			}
			lnk->lkPendTail = NULL;
		} else {
			mblk_t *mp = lnk->lkPendHead;
			while (mp != NULL) {
				lnk->lkPendHead = lnk->lkPendHead->b_next;
				mp->b_next = NULL;
				freemsg(mp);
				mp = lnk->lkPendHead;
			}
			lnk->lkPendTail = NULL;
		}


		/*
		 * if there are unanswered DL_CONNECT_INDs and lk_mac is valid
		 * need to issue disconnect request for each unanswered, always
		 * discard queue elements
		 */
		mac = lnk->lk_mac;
		if ((mac != NULL) && (lnk->lk_state != DL_UNATTACHED)) {
			while (lnk->lkOutIndHead != NULL) {
				outInd_t *temp = lnk->lkOutIndHead;
				ushort_t sid = temp->sid;
				lnk->lk_state = DL_INCON_PENDING;
				tries = 10;
				while (tries > 0) {
					if (dlpi_disconnect_req(lnk, sid)
						== 0) {
						/*
						 * command accepted by lower
						 * layer, go to sleep waiting
						 * for confirmation to arrive
						 */
						while ((lnk->lk_close &
							(LKCL_FAIL | LKCL_OKAY))
							== 0) {
						lnk->lk_close |= LKCL_WAKE;
						/* muoe961994 buzz 6/17/96 */
						/* muoe962955 buzz 10/30/96 */
						ILD_SLEEP(lnk->sleep_var,
							lnk->lk_lock,
							sleep_ret);
						lnk->lk_close &= ~LKCL_WAKE;
						/*
						 * zero return from ILD_SLEEP
						 * means we were hit with
						 * a signal vs. a normal wakeup
						 */
						if (sleep_ret == 0)
						lnk->lk_close |= LKCL_FAIL;
						}
						if (lnk->lk_close & LKCL_FAIL) {
						ild_trace(MID_DLPI, __LINE__, 0,
							0, 1);
						}
						break;
					} else {
						/*
						 * possible resource error,
						 * sleep for a short while and
						 * then try again, must fake a
						 * pending state to prevent
						 * stuff from lower layers from
						 * being sent any further up
						 */
						lnk->lk_state =
							DL_DISCON9_PENDING;
						ILD_RWUNLOCK(lnk->lk_lock);
						delay(5);
						ILD_WRLOCK(lnk->lk_lock);
						lnk->lk_state =
							DL_INCON_PENDING;
					}
					tries -= 1;
				}
				lnk->lk_close &= ~(LKCL_FAIL | LKCL_OKAY);
				lnk->lkOutIndHead = lnk->lkOutIndHead->next;
				dlpiFreeOutInd(temp);
				/* muoe970517:	Shiv 03/20/97 */
				if (lnk->lk_state == DL_UNATTACHED) {
					origState = DL_UNATTACHED;
					/*
					 * if the link is now UNATTACHED, dump
					 * any remaining pending connect
					 * indications.
					 */
					temp = lnk->lkOutIndHead;
					while (temp != NULL) {
						lnk->lkOutIndHead =
							lnk->lkOutIndHead->next;
						dlpiFreeOutInd(temp);
						temp = lnk->lkOutIndHead;
					}
					break;
				}
			}
			lnk->lkOutIndTail = NULL;
		} else {
			outInd_t *temp = lnk->lkOutIndHead;
			while (temp != NULL) {
				lnk->lkOutIndHead = lnk->lkOutIndHead->next;
				dlpiFreeOutInd(temp);
				temp = lnk->lkOutIndHead;
			}
			lnk->lkOutIndTail = NULL;
		}
		/*
		 * muoe980112 : lokeshkn : 98/04/08 : Up port : NDF Wipro
		 * Closing listening stream, remove its link from listenQueue.
		 */
		if (lnk->lk_mac != NULL) {
			ILD_RWUNLOCK(lnk->lk_lock);
			if (dlpiDelListenQ(&listenQueue[lnk->lk_mac->mac_ppa],
						lnk)) {
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
						0, 0);
			}
			ILD_WRLOCK(lnk->lk_lock);
		}
	}


	/*
	 * continue processing even though a disconnect failure may have
	 * occurred, it is the responsibility of the lower layer to take
	 * whatever actions are necessary to make sure the unbind_req
	 * succeeds if there are problems
	 */
	if ((origState != DL_UNATTACHED) && (origState != DL_UNBOUND)) {
		tries = 10;
		ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
		while (tries > 0) {
			if (lnk->lk_state == DL_UNBIND_PENDING) {
				while ((lnk->lk_close & LKCL_OKAY) == 0) {
					lnk->lk_close |= LKCL_WAKE;
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_SLEEP(lnk->sleep_var,
						lnk->lk_lock, sleep_ret);
					lnk->lk_close &= ~LKCL_WAKE;
					/*
					 * zero return from ILD_SLEEP means
					 * we were hit with a signal vs. a
					 * normal wakeup
					 */
					if (sleep_ret == 0)
						lnk->lk_close |= LKCL_FAIL;
				}
				break;
			} else if (dlpi_unbind_req(lnk) == 0) {
				/* unbind clears locks */
				/*
				 * command accepted by lower layer, go to
				 * sleep waiting for confirmation to arrive
				 */
				/* unbind clears locks */
				ILD_WRLOCK(lnk->lk_lock);
				while ((lnk->lk_close & LKCL_OKAY) == 0) {
					lnk->lk_close |= LKCL_WAKE;
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_SLEEP(lnk->sleep_var,
						lnk->lk_lock, sleep_ret);
					lnk->lk_close &= ~LKCL_WAKE;
					/*
					 * zero return from ILD_SLEEP means
					 * we were hit with a signal vs. a
					 * normal wakeup
					 */
					if (sleep_ret == 0)
						lnk->lk_close |= LKCL_FAIL;
				}
				break;
			} else {
				/*
				 * possible resource error, sleep for a short
				 * while and then try again
				 */
				if (lnk->lk_state == DL_UNATTACHED)
					return;
				/* Unbind has released locks. */
				lnk->lk_state = DL_UNBIND_PENDING;
				delay(5);
				ILD_WRLOCK(lnk->lk_lock);
				lnk->lk_state = DL_IDLE;
			}
			tries -= 1;
		}
	}

	/*
	 * muoe951976 5/23/95	Prasad
	 * Upported 8/04/95	yatin
	 * Wait till lk_close & LKCL_INUSE become zero. This indicates that
	 * the link structure is not in use anywhere else. Currently this
	 * flag is set/cleared in dlpi_disconnect_con() only.
	 * Can't do anything about ILD_SLEEP getting interrupted.
	 */
	lnk->lk_close &= ~LKCL_OKAY;
	while (lnk->lk_close & LKCL_INUSE) {
		while ((lnk->lk_close & LKCL_OKAY) == 0) {
			lnk->lk_close |= LKCL_WAKE;
			/* muoe962955 buzz 10/30/96 */
			ILD_SLEEP(lnk->sleep_var, lnk->lk_lock, sleep_ret);
			lnk->lk_close &= ~LKCL_WAKE;
			/*
			 * zero return from ILD_SLEEP means we were hit with
			 * a signal vs. a normal wakeup
			 */
			if (sleep_ret == 0)
				lnk->lk_close |= LKCL_FAIL;
		}
	}
	/* End muoe951976 */


	/* begin muoe951422 buzz 04/05/95 */
	/* make sure all this link manipulation crap is inside a lock */

	lnk->lk_wrq = NULL;
	lnk->lk_state = DL_UNATTACHED;
	lnk->lk_rnr = FALSE;
	lnk->lk_close = 0;
	lnk->lkOutIndHead = NULL;
	lnk->lkOutIndTail = NULL;
	lnk->lkPendHead = NULL;
	lnk->lkPendTail = NULL;
	lnk->lkDisIndHead = NULL;
	lnk->lkDisIndTail = NULL;

	lnk->lk_addr_length = sizeof (dlsap_t);

	mac = lnk->lk_mac;
	lnk->lk_mac = NULL;
	ild_clear_bit(lnk->lk_minor, &ild_minors[0]);

	ILD_RWUNLOCK(lnk->lk_lock);

	if (mac) /* if we're already unattached, this will be NULL */
		ILD_WRLOCK(ild_glnk_lck[mac->mac_ppa]);
	ILD_WRLOCK(lnk->lk_lock); /* muoe941184 buzz 04/25/94 */

	if (lnk->chain.flink) {
		RMV_DLNKLST(&lnk->chain);
	}

	ILD_RWUNLOCK(lnk->lk_lock); /* muoe941184 buzz 04/25/94 */
	if (mac) /* if we're already unattached, this will be NULL */
		ILD_RWUNLOCK(ild_glnk_lck[mac->mac_ppa]);

	/*
	 * this seems as safe as any place. The link has been removed from
	 * the PPA queue, but has not been added to the freelist yet. So it
	 * won't be searched if stuff comes in, nor will it be reused by another
	 * open.
	 */
	dlpi_flush_multicast(mac, lnk);

	/* muoe933145 09/23/93 */
	ADD_DLNKLST(&lnk->chain, &lnkFreeHead); /* and mv to Link Free List */

	/* end muoe951422 buzz 04/05/95 */

}


/* muoe951422 buzz 04/05/95 */
/* Pass mac pointer to this routine */
void
dlpi_flush_multicast(mac_t *mac, link_t *lnk)
{
	int	i;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	ASSERT(lnk != NULL);
	ILD_WRLOCK(lnk->lk_lock);
	if (lnk->lk_multiuse) {
		if (mac) {
			for (i = 0; i < MAXMULTICAST; i++) {
			if (CMP_MAC(lnk->lk_multicast[i].mac_addr, nullmac))
				continue;
			(void) dlpi_remove_multicast(lnk,
				(mac_addr_t *)lnk->lk_multicast[i].mac_addr);
			}
		}
	}
	lnk->lk_multiuse = 0;
	ILD_RWUNLOCK(lnk->lk_lock);
}

/* GLOBAL PROCEDURES (called by lower modules) */

/*
 * Function Label:	dlpi_shutdown
 * Description:	shutdown the specified adapter.  can be caused by
 * 		ILD_UNINIT ioctl or failure detected by ring
 *		status or adapter check.
 * Locks:	No locks set on entry/exit.
 * 		ild_glnk_lck[mac_ppa] is locked/unlocked.
 *		lnk->lk_lock is locked/unlocked.
 */
void
dlpi_shutdown(mac_t *mac, int abort_all)
{
	mblk_t *mp;
	link_t *lnk, *nlnk;
	ushort_t sid;
	uint_t mac_ppa;
	dLnkLst_t *headlnk;

	/*
	 * if already shutdown, just return
	 */
	if (mac->mac_state == MAC_INST) {
		return;
	}

	/*
	 * mark the adapter as dead
	 */
	ild_trace(MID_DLPI, __LINE__, 0, 0, 1);

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		if (lnk->lk_status == LKCL_CLOSED ||
			lnk->lk_status == LKCL_CLOSE_IN_PROGRESS) {
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = (link_t *)lnk->chain.flink;
			continue;
		}

		lnk->lk_status = LKCL_CLOSE_IN_PROGRESS;

		/*
		 * muoe970517:	shiv 03/20/97
		 * wake up dlpi_close
		 */
		/*
		 * If the link is sleeping, set the close status to FAIL
		 * and wake it up so it can finish.
		 */
		if ((lnk->lk_close & LKCL_WAKE) &&
			(lnk->lk_close & LKCL_PEND)) {
			lnk->lk_close |= LKCL_FAIL;
			ILD_WAKEUP(lnk->sleep_var);
		}
		/* muoe970517 shiv 03/20/97 */

		/* muoe972527 buzz 12/09/97 */
		/*
		 * When MACs are taken out of service (via ILD_UNINIT ioctl)
		 * make sure we notify connection oriented DLS users that
		 * stuff is going away. Originally, this code sent M_ERROR
		 * messages upstream, which, quite effectively, killed the
		 * streams and required a close/open for recovery. Any DLS user
		 * that is simply DL_ATTACHED or DL_IDLE (these are mainly users
		 * of connectionless services) will receive no notification. But
		 * since connection oriented users depend of LLC2 timers/retries
		 * for recovery and notification, they have to be knocked down
		 * here or be left hung out to dry.
		 */
		nlnk = (link_t *)lnk->chain.flink;
		if (abort_all) {
			/*
			 * abort_all = 1 signifies  that were're unlinking the
			 * lower MAC drivers and freeing up all the mac_t,
			 * macx_t, llc2Sta_t structures. Time for drastic
			 * measures--Kill all STREAMS associated with this PPA
			 */
			lnk->lk_state = DL_UNATTACHED;
			if ((mp = llc2_allocb(1, BPRI_HI)) != NULL) {
				mp->b_datap->db_type = M_ERROR;
				if (lnk->lk_wrq != NULL)
					qreply(lnk->lk_wrq, mp);
				else
					freemsg(mp);
			}
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			/*
			 * Non-abortive shutdown resulting from simply an
			 * ildinit -u <ppa>.
			 * simply send up disconnect indications or M_HANGUPs as
			 * appropriate
			 */
			if (lnk->lk_state >= DL_OUTCON_PENDING) {
				sid = lnk->lk_sid;
				ILD_RWUNLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
				dlpi_disconnect_ind(mac, sid,
						DL_DISC_TRANSIENT_CONDITION);
				ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			}
			/*
			 * for BOUND LLC SAPs, drastic measures are required...
			 * Have to resort to the old M_HANGUP method for them,
			 * since there's no other way to get their attention
			 */
			if ((lnk->lk_state == DL_IDLE) &&
				(lnk->lk_addr_length == IEEE_ADDR_SIZE) &&
				(lnk->lk_bind.bd_sap.llc.dl_sap < 255)) {
				lnk->lk_state = DL_UNBOUND;
				if ((mp = llc2_allocb(1, BPRI_HI)) != NULL) {
					mp->b_datap->db_type = M_HANGUP;
					if (lnk->lk_wrq != NULL)
						qreply(lnk->lk_wrq, mp);
					else
						freemsg(mp);
				}

			}
			ILD_RWUNLOCK(lnk->lk_lock);
		}
		lnk = nlnk;
		/* muoe972527 buzz 12/09/97 */
	}
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);


}

/*
 * Function Label:	dlpi_linkdown
 * Description:	Notify SNAP users that the link is down via M_STOP
 *		Send DISCON_IND to connection oriented users.
 *		Everybody else get M_HANGUP
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock is locked/unlocked.
 *		per/ppa link lock (ild_glnk_lck) is locked/unlocked
 */
void
dlpi_linkdown(mac_t *mac)
{
	mblk_t *mp;
	uint_t	mac_ppa;
	ushort_t sid;
	link_t *lnk, *nlnk;
	dLnkLst_t *headlnk;

	ild_trace(MID_DLPI, __LINE__, (uint_t)mac->mac_ppa, 0, 1);
	/*
	 * mark all links attached to this adapter as DL_UNBOUND
	 * and send an M_STOP to SNAP SAP users, an M_HANGUP to the rest
	 */
	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
				(uint_t)lnk->lk_state, 0);
		ILD_WRLOCK(lnk->lk_lock);

		if (lnk->lk_status == LKCL_CLOSED ||
			lnk->lk_status == LKCL_CLOSE_IN_PROGRESS) {
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = (link_t *)lnk->chain.flink;
			continue;
		}

		lnk->lk_status = LKCL_CLOSE_IN_PROGRESS;

		/*
		 * muoe970517:	shiv 03/20/97
		 * wake up dlpi_close
		 */
		/*
		 * If the link is sleeping, set the close status to FAIL
		 * and wake it up so it can finish.
		 */
		if ((lnk->lk_close & LKCL_WAKE) &&
			(lnk->lk_close & LKCL_PEND)) {
			lnk->lk_close |= LKCL_FAIL;
			ILD_WAKEUP(lnk->sleep_var);
		}
		/* muoe970517 :	shiv	03/20/97 */

		/* muoe972527 buzz 12/09/97 */
		/*
		 * When MACs are taken out of service (via ILD_UNINIT ioctl)
		 * make sure we notify connection oriented DLS users that
		 * stuff is going away. Originally, this code sent M_ERROR
		 * messages upstream, which, quite effectively, killed the
		 * streams and required a close/open for recovery. Any DLS
		 * user that is simply DL_UNBOUND or DL_IDLE (these are mainly
		 * users of connectionless services) will receive no
		 * notification. But since connection oriented users depend on
		 * LLC2 timers/retries for recovery and notification, they have
		 * to be knocked down here or be left hung out to dry.
		 */
		nlnk = (link_t *)lnk->chain.flink;
		if (lnk->lk_state >= DL_OUTCON_PENDING) {
			sid = lnk->lk_sid;
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			dlpi_disconnect_ind(mac, sid,
						DL_DISC_TRANSIENT_CONDITION);
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			ILD_WRLOCK(lnk->lk_lock);
		} else if ((lnk->lk_state == DL_IDLE) &&
				(lnk->lk_addr_length == IEEE_ADDR_SIZE) &&
				(lnk->lk_bind.bd_sap.llc.dl_sap < 255)) {
			lnk->lk_state = DL_UNBOUND;
			if ((mp = llc2_allocb(1, BPRI_HI)) != NULL) {
				if (lnk->lk_bind.bd_sap.llc.dl_sap == SNAPSAP) {
					mp->b_datap->db_type = M_STOP;
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, M_STOP, 0);
				} else {
					mp->b_datap->db_type = M_HANGUP;
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, M_HANGUP, 0);
				}
				*mp->b_wptr++ = 0;
				if (lnk->lk_wrq != NULL)
					qreply(lnk->lk_wrq, mp);
				else
					freemsg(mp);
			}
		}
		ILD_RWUNLOCK(lnk->lk_lock);
		lnk = nlnk;
	}
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

}

/*
 * Function Label:	dlpi_linkup
 * Description:	Notify SNAP users that the link is back up via M_START
 *		after re-BINDing the SAP.
 *		this keeps TCP/IP happy since they're unable to
 *		re-BIND their own SAPs wo/taking the whole subsystem
 *		and bringing it back up again.
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock is locked/unlocked.
 *		ild_glnk_lck is locked/unlocked.
 */
void
dlpi_linkup(mac_t *mac)
{
	mblk_t *mp;
	link_t *lnk, *nlnk;
	uint_t	mac_ppa;
	uchar_t  sap;
	ushort_t mode;
	int err;
	dLnkLst_t *headlnk;


	ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
	/*
	 * find all SNAP links attached to this adapter that are DL_IDLE
	 * and send an M_START msg to them. Others don't get anything,
	 * since they previously got an M_HANGUP.
	 */

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);
		nlnk = (link_t *)lnk->chain.flink;
		if ((lnk->lk_state == DL_UNBOUND) &&
			(lnk->lk_bind.bd_sap.llc.dl_sap == SNAPSAP)) {
			mac = lnk->lk_mac;
			/*
			 * if this was the SNAP sap (ie 0xAA) try to recover
			 * the sap
			 */
			lnk->lk_state = DL_BIND_PENDING;
			/*
			 * re-bind the LLC sap
			 */
			mp = llc2_allocb(1, BPRI_HI);
			if (mp) mp->b_datap->db_type = M_ERROR;
			sap = lnk->lk_bind.bd_sap.llc.dl_sap;
			mode = lnk->lk_bind.bd_service_mode;
			ILD_WRLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			err = 1;
			if ((err = llc2BindReq(lnk, sap, mode)) == 0) {
				ild_trace(MID_DLPI, __LINE__,
					(uintptr_t)lnk, (uint_t)sap, 0);
				if (mp)
					mp->b_datap->db_type = M_START;
			}
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			ILD_WRLOCK(lnk->lk_lock);

			if (err)
				lnk->lk_state = DL_UNBOUND;

			/*
			 * If we were successful at re-BINDing the SAP send
			 * up M_START, otherwise, KILL it via M_ERROR
			 */
			if (mp) {
				if (mp->b_datap->db_type == M_ERROR)
					*mp->b_wptr++ = EPROTO;
				else
					*mp->b_wptr++ = 0;

				if (lnk->lk_wrq != NULL)
					qreply(lnk->lk_wrq, mp);
				else
					freemsg(mp);
			}
		}
		ILD_RWUNLOCK(lnk->lk_lock);
		lnk = nlnk;
	}
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

}


/*
 * Function Label:   dlpi_bind_con
 * Description:	called by lower LLC I/F module when OPEN.SAP
 *		command completes
 * Locks:	No locks set on entry/exit.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 * 		lnk->lk_lock locked/unlocked.
 */
void
dlpi_bind_con(mac_t *mac, dlsap_t *ssap, ushort_t status, int enet)
{
	dLnkLst_t *headlnk;
	link_t *lnk;
	uint_t mac_ppa;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];

	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		/*
		 * search for the right lnk, i.e.  type, state
		 */
		if ((lnk->lk_wrq != NULL) &&
			(((enet != 0) && (lnk->lk_enet != 0) &&
			(lnk->lk_bind.bd_sap.ether.dl_type ==
			ssap->ether.dl_type)) ||
			((enet == 0) && (lnk->lk_enet == 0) &&
			(lnk->lk_bind.bd_sap.llc.dl_sap ==
			ssap->llc.dl_sap))) &&
			(lnk->lk_state == DL_BIND_PENDING)) {

			ILD_SWAPIPL(mac_ppa, lnk);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			if ((lnk->lk_close & LKCL_PEND) == 0) {
				if (status == 0) {
				lnk->lk_state = DL_IDLE;

				if (lnk->lk_bind.bd_max_conind > 0) {
				lnk->lk_sid = 0;
				ILD_RWUNLOCK(lnk->lk_lock);
				if (dlpiAddListenQ(&listenQueue[mac_ppa],
							lnk)) {
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, 0, 0);
				}
				ILD_WRLOCK(lnk->lk_lock);
				}

				dlpi_bind_ack(lnk->lk_wrq, ssap,
						lnk->lk_bind.bd_max_conind,
						enet);
				} else {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				lnk->lk_state = DL_UNBOUND;

				dlpi_error_ack(lnk->lk_wrq, DL_BIND_REQ,
						status, 0);
				}
				ILD_RWUNLOCK(lnk->lk_lock);
			} else {
				/*
				 * close in progress on this stream, do not
				 * want to muck things up by putting a message
				 * on the stream, simply note the fact
				 * we were here and leave
				 */
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
						0, 0);
				ILD_RWUNLOCK(lnk->lk_lock);
			}
			return;
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);


}

/*
 *  Locks: 	No locks set on entry/exit.
 *		ild_listenq_lock locked/unlocked.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 *		lnk->lk_lock locked/unlocked.
 */
void
dlpi_disconnect_ind(mac_t *mac, ushort_t sid, uint_t reason)
{
	link_t *lnk;
	dLnkLst_t *headlnk;
	listenQ_t *lqPtr;
	mblk_t *mp, *disc_mp;
	mblk_t *omp;
	dl_connect_ind_t *d;
	dl_disconnect_ind_t *dl;
	uint_t mac_ppa;
	dlsap_t dsap;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	ASSERT(mac != NULL);

	/*
	 * if there are listening stream(s) then determine whether this is
	 * a disconnect against a listening stream
	 */
	mac_ppa = mac->mac_ppa;
	headlnk = &mac_lnk[mac_ppa];
	lnk = NULL;
	ILD_WRLOCK(ild_listenq_lock);
	lqPtr = listenQueue[mac_ppa];
	while (lqPtr != NULL) {
		if (lqPtr->lnk)  /* muoe932415 buzz 08/02/93 */  {
			if (lqPtr->lnk->lk_sid == GETSAPID(sid)) {
				lnk = lqPtr->lnk;
				break;
			}
		}
		lqPtr = lqPtr->next;
	}
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	ILD_RWUNLOCK(ild_listenq_lock);

	if (lnk != NULL) {
		ILD_RDLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		/*
		 * have a listening stream for this SAP ID, check to make sure
		 * SID matches one either on the pending indication or
		 * outstanding indication queues, if not then a disconnect
		 * indication for an active connection
		 */
		mp = lnk->lkPendHead;
		while (mp != NULL) {
			d = (dl_connect_ind_t *)mp->b_rptr;
			if (d->dl_correlation == sid) {
				break;
			}
			mp = mp->b_next;
		}
		if ((mp == NULL) && (peekOutInd(lnk, sid) == NULL)) {
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = NULL;
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
		}
	} else {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	}

	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);

	if (lnk == NULL) {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		goto LINK_NULL;
	}

	ILD_WRLOCK(lnk->lk_lock);
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	if ((lnk->lk_state == DL_INCON_PENDING) ||
		(lnk->lk_state == DL_DISCON9_PENDING)) {

		lnk->lk_close |= LKCL_INUSE;
		/*
		 * this link no longer represents a pending indication
		 */
		if ((lnk->lk_close & LKCL_PEND) == 0) {
			/*
			 * must check any pending connect indications first,
			 * if disconnect for an indication that has not been
			 * delivered then simply discard the indication message
			 */
			omp = NULL;
			mp = lnk->lkPendHead;
			while (mp != NULL) {
				d = (dl_connect_ind_t *)mp->b_rptr;
				if (d->dl_correlation == sid) {
					if (omp == NULL) {
						lnk->lkPendHead =
							lnk->lkPendHead->b_next;
					} else if (lnk->lkPendTail == mp) {
						lnk->lkPendTail = omp;
						lnk->lkPendTail->b_next = NULL;
					} else {
						omp->b_next = mp->b_next;
					}
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk->lkPendHead,
						(uintptr_t)mp, 0);
					freemsg(mp);
					/*
					 * Check if we need to wakeup dlpi_close
					 * to continue processing
					 */
					lnk->lk_close &= ~LKCL_INUSE;
					lnk->lk_close |= LKCL_OKAY;
					if (lnk->lk_close & LKCL_WAKE) {
						ILD_WAKEUP(lnk->sleep_var);
					}
					ILD_RWUNLOCK(lnk->lk_lock);
					return;
				}
				omp = mp;
				mp = mp->b_next;
			}
			if ((lnk->lk_bind.bd_conind += 1) ==
				lnk->lk_bind.bd_max_conind) {
				if (lnk->lk_state == DL_INCON_PENDING) {
					/*
					 * can only change state if not
					 * DISCON9_PENDING
					 */
					lnk->lk_state = DL_IDLE;
				}
			}
			/* muoe961994 6/19/96 buzz */
			/*
			 * The muoe961994 changes below basically move the
			 * qreply for the Disconnect Indication until after
			 * all the state changes take place.
			 */
			if ((disc_mp = llc2_allocb(DL_DISCONNECT_IND_SIZE,
						BPRI_MED)) != NULL) {
				dl = (dl_disconnect_ind_t *)disc_mp->b_rptr;
				/* muoe961994 6/19/96 buzz */
				disc_mp->b_datap->db_type = M_PROTO;
				disc_mp->b_wptr += DL_DISCONNECT_IND_SIZE;
				dl->dl_primitive = DL_DISCONNECT_IND;
				dl->dl_originator = DL_PROVIDER;
				dl->dl_reason = reason;
				dl->dl_correlation = sid;

				/*
				 * remove the element for this sid from the
				 * outstanding connect indication queue
				 */
				if (delOutInd(lnk, sid)) {
					ild_trace(MID_DLPI, __LINE__,
							(uint_t)sid, 0, 0);
				}
				if ((lnk->lk_state == DL_INCON_PENDING) ||
					(lnk->lk_state == DL_IDLE)) {
					if (lnk->lkPendHead != NULL) {
					/*
					 * if pending connect indications exist
					 * now is the time
					 * to indicate the next one
					 */
					mp = lnk->lkPendHead;
					lnk->lkPendHead =
						lnk->lkPendHead->b_next;
					lnk->lk_state = DL_INCON_PENDING;
					lnk->lk_bind.bd_conind -= 1;
					d = (dl_connect_ind_t *)mp->b_rptr;

					/*
					 * add sid for this connect indication
					 * to the outstanding DL_CONNECT_IND
					 * list
					 */
					bcopy((caddr_t)(mp->b_rptr +
						d->dl_calling_addr_offset),
						(caddr_t)&dsap,
						sizeof (dlsap_t));
					if (addOutInd(lnk, d->dl_correlation,
						&dsap)) {
							ild_trace(MID_DLPI,
							__LINE__,
							(uintptr_t)lnk,
							d->dl_correlation, 0);
						}
						/* muoe961994 6/19/96 buzz */
						if (disc_mp) {
							if (lnk->lk_wrq
								!= NULL)
							qreply(lnk->lk_wrq,
								disc_mp);
							else
							freemsg(disc_mp);
						}
						if (lnk->lk_wrq != NULL)
							qreply(lnk->lk_wrq, mp);
						else
							freemsg(mp);
					} else {
						/* muoe961994 6/19/96 buzz */
						if (disc_mp) {
						if (lnk->lk_wrq != NULL)
							qreply(lnk->lk_wrq,
								disc_mp);
						else
							freemsg(disc_mp);
						}
					}
				} else {
					mp->b_next = NULL;
					if (lnk->lkDisIndHead == NULL) {
						lnk->lkDisIndHead =
							lnk->lkDisIndTail = mp;
					} else {
						lnk->lkDisIndTail->b_next = mp;
						lnk->lkDisIndTail = mp;
					}
				}
			} else {
			cmn_err(CE_WARN, "!LLC2: allocb DL_DISCONN_IND fail\n");
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			}
		} else {
			/*
			 * this listening stream is in close processing,
			 * we do not want to muck things up by sending a
			 * message up a stream being closed, nor do we dare
			 * muck with the pending queues, simply
			 * make note of the fact we were here and leave
			 */
			ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, sid, 0);
		}
		/*
		 * Check if we need to wakeup dlpi_close to continue processing
		 */
		lnk->lk_close &= ~LKCL_INUSE;
		lnk->lk_close |= LKCL_OKAY;
		if (lnk->lk_close & LKCL_WAKE) {
			/* muoe961994 buzz 6/17/96 */
			/* muoe962955 buzz 10/30/96 */
			ILD_WAKEUP(lnk->sleep_var);
		}

		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	} else {
		ILD_RWUNLOCK(lnk->lk_lock);
	}

LINK_NULL:

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];

	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_sid == sid) &&
			((lnk->lk_state == DL_DATAXFER) ||
			(lnk->lk_state == DL_OUTCON_PENDING) ||
			(lnk->lk_state == DL_CONN_RES_PENDING) ||
			(lnk->lk_state == DL_PROV_RESET_PENDING) ||
			((lnk->lk_state >= DL_DISCON8_PENDING) &&
			(lnk->lk_state <= DL_DISCON13_PENDING)) ||
			(lnk->lk_state == DL_USER_RESET_PENDING))) {
			ILD_SWAPIPL(mac_ppa, lnk);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			/*
			 * buzz 24-Mar-93
			 * This can happen when both sides send disconnect
			 * at once we send a DISC and go to sleep waiting
			 * on the UA rsp. DISC comes from other side. LLC
			 * send UA and destroys the Con structure. UA comes
			 * in but is discarded. Here we need to check to see
			 * if we've got a disconnect outstanding and call
			 * the disconnect confirm code to finish processing
			 * the application close.
			 */
			if ((lnk->lk_state >= DL_DISCON8_PENDING) &&
				(lnk->lk_state <= DL_DISCON13_PENDING)) {
				ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, sid, 0);
				ILD_RWUNLOCK(lnk->lk_lock);
				dlpi_disconnect_con(mac, sid, 0);
				return;
			}

			lnk->lk_close |= LKCL_INUSE;
			if ((lnk->lk_state == DL_DATAXFER) ||
				(lnk->lk_state == DL_PROV_RESET_PENDING) ||
				(lnk->lk_state == DL_USER_RESET_PENDING)) {
			if (lnk->lk_wrq != NULL)
				flushq(RD(lnk->lk_wrq), FLUSHALL);

			if (lnk->lk_wrq != NULL && (putnextctl1(RD(lnk->lk_wrq),
						M_FLUSH, FLUSHRW) != 1)) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			cmn_err(CE_WARN,
			"!LLC2: couldnot putctl1 FLUSHRW in dlpi_discon_ind\n");
			}
			}
			if ((mp = llc2_allocb(DL_DISCONNECT_IND_SIZE, BPRI_MED))
				!= NULL) {
				dl = (dl_disconnect_ind_t *)mp->b_rptr;
				mp->b_datap->db_type = M_PROTO;
				mp->b_wptr += DL_DISCONNECT_IND_SIZE;
				dl->dl_primitive = DL_DISCONNECT_IND;
				dl->dl_originator = DL_PROVIDER;
				dl->dl_reason = reason;
				dl->dl_correlation = 0;
				/*
				 * need some extra work here to keep user view
				 * of state correct, if this streams state is
				 * CONN_RES_PENDING then it is a stream on which
				 * a secondary connection is being
				 * taken, the partner has refused the connection
				 * but an DL_OK_ACK is necessary before the
				 * DL_DISCONNECT_IND can appear
				 */
				if (lnk->lk_state == DL_CONN_RES_PENDING) {
					lnk->lk_state = DL_IDLE;
					dlpi_ok_ack(lnk->lk_wrq,
							DL_CONNECT_RES);
					if (lnk->lk_wrq != NULL)
						qreply(lnk->lk_wrq, mp);
					else
						freemsg(mp);
				} else {
					lnk->lk_state = DL_IDLE;
					if (lnk->lk_wrq) {
						qreply(lnk->lk_wrq, mp);
					} else
						freemsg(mp);
				}
			} else {
			cmn_err(CE_WARN,
			"!LLC2: could not allocb DL_DISCONNECT_IND\n");
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			lnk->lk_state = DL_IDLE;
			}

			/*
			 * Check to see if we need to wakeup dlpi_close to
			 * continue processing
			 */
			lnk->lk_close &= ~LKCL_INUSE;
			lnk->lk_close |= LKCL_OKAY;
			if (lnk->lk_close & LKCL_WAKE) {
				ILD_WAKEUP(lnk->sleep_var);
			}
			ILD_RWUNLOCK(lnk->lk_lock);
			return;
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

}

/*
 * Function Label:	dlpi_disconnect_con
 * Description:		comment
 * Locks:		No locks set on entry/exit.
 *			ild_listenq_lock locked/unlocked.
 *			ild_glnk_lck[mac_ppa] locked/unlocked.
 *			lnk->lk_lock locked/unlocked.
 */
void
dlpi_disconnect_con(mac_t *mac, ushort_t sid, ushort_t rc)
{
	link_t *lnk;
	dLnkLst_t *headlnk;
	listenQ_t *lqPtr;
	uint_t mac_ppa;
	dlsap_t dsap;

	/*
	 * if this is a disc_con for a connect_ind in a pending state
	 */
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	ASSERT(mac != NULL);
	/*
	 * if there are listening stream(s) then determine whether this is
	 * a disconnect against a listening stream
	 */
	mac_ppa = mac->mac_ppa;
	headlnk = &mac_lnk[mac_ppa];
	lnk = NULL;
	ILD_WRLOCK(ild_listenq_lock);
	lqPtr = listenQueue[mac_ppa];
	while (lqPtr != NULL) {
		if (lqPtr->lnk)  /* muoe932415 buzz 08/02/93 */  {
			if (lqPtr->lnk->lk_sid == GETSAPID(sid)) {
				lnk = lqPtr->lnk;
				break;
			}
		}
		lqPtr = lqPtr->next;
	}
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	ILD_RWUNLOCK(ild_listenq_lock);

	if (lnk != NULL) {
		dl_connect_ind_t *d;
		mblk_t *mp;

		ILD_RDLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		mp = lnk->lkPendHead;
		/*
		 * have a listening stream for this SAP ID, check to make sure
		 * SID matches one either on the pending indication or
		 * outstanding indication queues, if not then a disconnect
		 * confirmation for an active connection
		 */
		while (mp != NULL) {
			d = (dl_connect_ind_t *)mp->b_rptr;
			if (d->dl_correlation == sid) {
				break;
			}
			mp = mp->b_next;
		}
		if ((mp == NULL) && (peekOutInd(lnk, sid) == NULL)) {
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = NULL;
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
		}
	} else {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	}

	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	if ((lnk != NULL) && (lnk->lk_state == DL_DISCON9_PENDING)) {
		ILD_WRLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		if (rc == 0) {
			if ((lnk->lk_close & LKCL_PEND) == 0) {
				mblk_t  *pend_mp = NULL;
				queue_t *pend_q = NULL;

				if (lnk->lk_bind.bd_max_conind ==
					(lnk->lk_bind.bd_conind += 1)) {
					lnk->lk_state = DL_IDLE;
				} else {
					lnk->lk_state = DL_INCON_PENDING;
				}

				/*
				 * remove sid for this connect indication
				 * from the outstanding DL_CONNECT_IND list
				 */
				if (delOutInd(lnk, sid)) {
					ild_trace(MID_DLPI, __LINE__,
							(uintptr_t)lnk, sid, 0);
				}

				/*
				 * now is the time to send up any pending
				 * disconnect indications
				 */
				/*
				 * Just pull off the head mp
				 * They should all be chained correctly.
				 * Then qreply's the whole chain
				 */
				if ((pend_mp = lnk->lkDisIndHead) != NULL) {
					lnk->lkDisIndHead = NULL;
					pend_q = lnk->lk_wrq;
				}

				/*
				 * now is the time to deliver the next
				 * pending connect indication (if there is one)
				 */
				if (lnk->lkPendHead != NULL) {
					dl_connect_ind_t *d;
					mblk_t *temp = lnk->lkPendHead;
					lnk->lkPendHead =
						lnk->lkPendHead->b_next;
					lnk->lk_bind.bd_conind -= 1;
					lnk->lk_state = DL_INCON_PENDING;
					d = (dl_connect_ind_t *)temp->b_rptr;

					/*
					 * add sid for this connect indication
					 * to the outstanding DL_CONNECT_IND
					 * list
					 */
					bcopy((caddr_t)(temp->b_rptr +
						d->dl_calling_addr_offset),
						&dsap, sizeof (dlsap_t));
					if (addOutInd(lnk, d->dl_correlation,
						&dsap)) {
						ild_trace(MID_DLPI, __LINE__,
							(uintptr_t)lnk,
							d->dl_correlation, 0);
					}

					/* muoe961994 6/19/96 buzz */
					dlpi_ok_ack(lnk->lk_wrq,
							DL_DISCONNECT_REQ);
					if ((pend_mp) && (pend_q)) {
						qreply(pend_q, pend_mp);
					} else {
						if (pend_mp)
							freemsg(pend_mp);
					}
					if (lnk->lk_wrq != NULL)
						qreply(lnk->lk_wrq, temp);
					else
						freemsg(temp);
				} else {
					/* muoe961994 6/19/96 buzz */
					dlpi_ok_ack(lnk->lk_wrq,
							DL_DISCONNECT_REQ);
					if ((pend_mp) && (pend_q)) {
						qreply(pend_q, pend_mp);
					} else {
						if (pend_mp)
							freemsg(pend_mp);
					}
				}
			} else {
				/*
				 * need to wakeup dlpi_close to continue
				 * processing only when not an immediate
				 * callback
				 */
				lnk->lk_close |= LKCL_OKAY;
				if (lnk->lk_close & LKCL_WAKE) {
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_WAKEUP(lnk->sleep_var);
				}
			}
		} else {
			if ((lnk->lk_close & LKCL_PEND) == 0) {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				lnk->lk_state = DL_INCON_PENDING;
				dlpi_error_ack(lnk->lk_wrq, DL_DISCONNECT_REQ,
						DL_SYSERR, rc);
			} else {
				/*
				 * need to inform dlpi_close() the disconnect
				 * request failed, also need to wakeup
				 * dlpi_close()
				 */
				lnk->lk_close |= LKCL_FAIL;
				if (lnk->lk_close & LKCL_WAKE) {
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_WAKEUP(lnk->sleep_var);
				}
			}
		}
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	} else {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	}

	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_sid == sid) &&
			((lnk->lk_state >= DL_DISCON8_PENDING) &&
			(lnk->lk_state <= DL_DISCON13_PENDING))) {
			ILD_SWAPIPL(mac_ppa, lnk);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			if (rc == 0) {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				if ((lnk->lk_close & LKCL_PEND) == 0) {
				if ((lnk->lk_state == DL_DISCON11_PENDING) ||
					(lnk->lk_state ==
					DL_DISCON12_PENDING) ||
					(lnk->lk_state ==
						DL_DISCON13_PENDING)) {
					if (lnk->lk_wrq != NULL)
						flushq(RD(lnk->lk_wrq),
							FLUSHALL);

					lnk->lk_close |= LKCL_INUSE;

					if (lnk->lk_wrq &&
						(putnextctl1(RD(lnk->lk_wrq),
							M_FLUSH, FLUSHRW)
							!= 1)) {
						ild_trace(MID_DLPI,
							__LINE__, 0, 0, 0);
						cmn_err(CE_WARN,
						"!LLC2: FLUSHRW failed\n");
					}
					}
					dlpi_ok_ack(lnk->lk_wrq,
							DL_DISCONNECT_REQ);
					/*
					 * Clear the LKCL_INUSE flag here
					 * and wake up dlpi_close()
					 */
					lnk->lk_state = DL_IDLE;
					lnk->lk_sid = 0;
					lnk->lk_close &= ~LKCL_INUSE;
					lnk->lk_close |= LKCL_OKAY;
					if (lnk->lk_close & LKCL_WAKE) {
						/* muoe961994 buzz 6/17/96 */
						/* muoe962955 buzz 10/30/96 */
						ILD_WAKEUP(lnk->sleep_var);
					}
					ILD_RWUNLOCK(lnk->lk_lock);
					/* End muoe951976 */
				} else {
					/*
					 * need to wakeup dlpi_close to continue
					 * processing
					 */
					lnk->lk_close |= LKCL_OKAY;
					if (lnk->lk_close & LKCL_WAKE) {
						/* muoe961994 buzz 6/17/96 */
						/* muoe962955 buzz 10/30/96 */
						ILD_WAKEUP(lnk->sleep_var);
					}

					ILD_RWUNLOCK(lnk->lk_lock);
				}
			} else {
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
						rc, 0);
				switch (lnk->lk_state) {
				case DL_DISCON8_PENDING:
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, (rc << 16) +
						sid, 0);
					lnk->lk_state = DL_OUTCON_PENDING;
					break;
				case DL_DISCON9_PENDING:
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, (rc << 16) +
						sid, 0);
					lnk->lk_state = DL_INCON_PENDING;
					break;
				case DL_DISCON11_PENDING:
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, (rc << 16) +
						sid, 0);
					lnk->lk_state = DL_DATAXFER;
					break;
				case DL_DISCON12_PENDING:
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, (rc << 16) +
						sid, 0);
					lnk->lk_state = DL_USER_RESET_PENDING;
					break;
				case DL_DISCON13_PENDING:
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, (rc << 16) +
						sid, 0);
					lnk->lk_state = DL_PROV_RESET_PENDING;
					break;
					/*
					 * no default case is provided since
					 * we can only get here when
					 * lnk->lk_state is one of the above
					 * four values
					 */
				}

				if ((lnk->lk_close & LKCL_PEND) == 0) {
					dlpi_error_ack(lnk->lk_wrq,
							DL_DISCONNECT_REQ,
							DL_SYSERR, rc);
					ILD_RWUNLOCK(lnk->lk_lock);
				} else {
					/*
					 * need to wakeup dlpi_close to
					 * continue processing
					 */
					lnk->lk_close |= LKCL_FAIL;
					if (lnk->lk_close & LKCL_WAKE) {
						/* muoe961994 buzz 6/17/96 */
						/* muoe962955 buzz 10/30/96 */
						ILD_WAKEUP(lnk->sleep_var);
					}
					ILD_RWUNLOCK(lnk->lk_lock);
				}
			}
			return;
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

}


/*
 * process incoming i-frame.
 */
/*
 * Function Label:	dlpi_data_ind
 * Description:	comment
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock locked/unlocked.
 */
int
dlpi_data_ind(mac_t *mac, link_t *lnk, ushort_t sid, mblk_t *mp)
{
	queue_t *q;

	ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, sid, 0);

	/*
	 * validate the stream which should receive the frame, send the frame up
	 */

	ILD_WRLOCK(lnk->lk_lock);

	if ((lnk->lk_mac == mac) &&
		(lnk->lk_state == DL_DATAXFER) &&
		(lnk->lk_sid == sid) &&
		lnk->lk_wrq != NULL) {
		q = OTHERQ(lnk->lk_wrq);
		/*
		 * if we're already in flow control, just queue the
		 * frame. When the rsrv routine cleans off the queue,
		 * the MAC's (or LLC2) L_xon routine will be called
		 * and the LKRNR_FC flag will clear.
		 */
		if (lnk->lk_rnr & LKRNR_FC) {
			(void) putq(q, mp);
			ILD_RWUNLOCK(lnk->lk_lock);
			return (DLPI_DI_BUSY);
		}

		if (canputnext(q)) {
			putnext(q, mp);
			ILD_RWUNLOCK(lnk->lk_lock);
			return (DLPI_DI_OKAY);
		}

		(void) putq(q, mp);
		ILD_RWUNLOCK(lnk->lk_lock);
		return (DLPI_DI_BUSY);
	}

	/*
	 * something wrong with this stream
	 */
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	/*
	 * caller retains ownership of buffer on this return
	 */

	ILD_RWUNLOCK(lnk->lk_lock);

	return (DLPI_DI_STATE);
}

/*
 * Put up an incoming i-frame. We've already called dlpi_canput earlier
 * now's the time to go ahead and do it.
 */
/*
 * Function Label:	dlpi_putnext
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock locked/unlocked.
 */
/*ARGSUSED*/
int
dlpi_putnext(mac_t *mac, link_t *lnk, ushort_t sid, mblk_t *mp, int dowhat)
{

	ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, sid, 0);

	/*
	 * validate the stream which should receive the frame, send the
	 * frame up The busy check has already been done (in dlpi_canput)
	 * and we're going to accept the frame, so just go ahead and put
	 * it on up until the pipe drains and the otherside shuts up.
	 */
	if (lnk && lnk->lk_wrq) {
		/* muoe962592 buzz 8/16/96 */
		(void) putq(OTHERQ(lnk->lk_wrq), mp);
		/* muoe962592 buzz 8/16/96 */
	} else {
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, (uintptr_t)mp, 0);
		freemsg(mp);
	}

	return (dowhat);

}

/*
 * Can we put up an incoming i-frame.
 */
/*
 * Function Label:	dlpi_canput
 * Locks:	Several locks set on entry/exit.
 *		lnk->lk_lock locked/unlocked.
 */
/*ARGSUSED*/
int
dlpi_canput(mac_t *mac, link_t *lnk, ushort_t sid, mblk_t *mp)
{
	queue_t *q;
	int retVal;

	ASSERT(lnk != NULL);
	ILD_WRLOCK(lnk->lk_lock);

	ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, sid, 0);
	/*
	 * validate the stream which should receive the frame, and
	 * decide whether it should go up or not
	 */
	if (lnk->lk_rnr & LKRNR_FC) { /* if we're already in flow control */
		retVal = DLPI_DI_BUSY;		/* don't change our mind */
	} else if ((lnk->lk_mac == mac) &&
			(lnk->lk_state == DL_DATAXFER) &&
			(lnk->lk_sid == sid) &&
			lnk->lk_wrq != NULL) {
		q = OTHERQ(lnk->lk_wrq);
		if (q == NULL) {
			retVal = DLPI_DI_STATE;
		} else {
			if (canputnext(q)) {
				retVal = DLPI_DI_OKAY;
			} else {
				ild_trace(MID_DLPI, __LINE__, 0xff,
						0, 0);
				lnk->lk_rnr |= LKRNR_FC;
				retVal = DLPI_DI_BUSY;
			}
		}
	} else {
		/*
		 * something wrong with this stream
		 */
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		/*
		 * caller retains ownership of buffer on this return
		 */
		retVal = DLPI_DI_STATE;
	}
	ILD_RWUNLOCK(lnk->lk_lock);
	return (retVal);
}


/*
 * process incoming ui-frames.
 */
/*
 * Function Label:	dlpi_unitdata_ind
 * Description:	comment
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock locked/unlocked.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 */
void
dlpi_unitdata_ind(mac_t *mac, dlsap_t *dsap, dlsap_t *ssap, mblk_t *mp,
    int enet, int ri_len)
{
	mblk_t	*mp_hdr;
	link_t	*lnk;
	dLnkLst_t *headlnk;
	dl_unitdata_ind_t  *dl;
	ushort_t	type = 0;
	uint_t	received_org_id = 0;
	uint_t	stored_id = 0;
	uint_t	mac_ppa;
	int	addrtype = 0; /* 1 = bcast/mcast 2-this node, 0-discard */

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((enet == 0) && (dsap->llc.dl_sap == SNAPSAP)) {
		type = *(ushort_t *)(mp->b_rptr + 3);
		type = (type >> 8) | (type << 8);
		received_org_id = (htonl(*(uint_t *)(mp->b_rptr))) >> 8;
		mp->b_rptr += 5;
	}

	/*
	 * Pre-allocate the mblk_t for passing up the rest of the
	 * information
	 */
	mp_hdr = llc2_allocb((DL_UNITDATA_IND_SIZE + (2 * sizeof (dlsap_t))),
				BPRI_HI);

	/*
	 * find the recv'ing link
	 */

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];

	if (enet) {
		lnk = (link_t *)headlnk->flink;
		while (lnk != (link_t *)headlnk) {
			ILD_WRLOCK(lnk->lk_lock);

			if ((lnk->lk_state == DL_IDLE) &&
				(lnk->lk_enet != 0) &&
				(lnk->lk_bind.bd_sap.ether.dl_type ==
				dsap->ether.dl_type)) {
				/*
				 * At this point, we'd normally assume that
				 * the application wants this packet. But
				 * here's where the new changes
				 * for the multicast stuff goes. One last
				 * check to see if the received address match
				 * one of the appl's enabled multicast addresses
				 */
				if ((addrtype = dlpi_check_multicast(lnk,
						dsap->ether.dl_nodeaddr))
					!= 0) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			}
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = (link_t *)lnk->chain.flink;
		}
	} else {
		lnk = (link_t *)headlnk->flink;
		while (lnk != (link_t *)headlnk) {
			ILD_WRLOCK(lnk->lk_lock);

			if ((lnk->lk_state == DL_IDLE) &&
				(lnk->lk_enet == 0) &&
				((lnk->lk_bind.bd_service_mode & DL_CODLS)
				== 0) &&
				(lnk->lk_bind.bd_sap.llc.dl_sap ==
				dsap->llc.dl_sap)) {
				stored_id = ((lnk->lk_subs_bind_vendor1 << 16) |
					(lnk->lk_subs_bind_vendor2 << 8) |
					lnk->lk_subs_bind_vendor3);
				if ((lnk->lk_bind.bd_sap.llc.dl_sap
					!= SNAPSAP) ||
					((lnk->lk_subs_bind_type == type) &&
					/*
					 * According to the internet RFCs, we
					 * should only accept those Vendor IDs
					 * or Organizationally Unique IDs in
					 * packets that match what came down in
					 * the SUBS_BIND_REQ
					 */
					(stored_id == received_org_id))) {
					/*
					 * At this point, we'd normally assume
					 * that the application
					 * wants this packet. But here's where
					 * the new changes for the multicast
					 * stuff goes. One last check to see
					 * if the received address match one of
					 * the appl's enabled multicast
					 * addresses
					 */

					if ((addrtype =
						dlpi_check_multicast(lnk,
						dsap->llc.dl_nodeaddr)) != 0) {
						ILD_SWAPIPL(mac_ppa, lnk);
						break;
					}
				}
			}

			ILD_RWUNLOCK(lnk->lk_lock);

			lnk = (link_t *)lnk->chain.flink;
		}
	}
	if (lnk == (link_t *)headlnk) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		freemsg(mp);
		if (mp_hdr)
			freemsg(mp_hdr);
		return;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	if (ild_execution_options & DO_CANPUT) {
		if (lnk->lk_wrq && (canputnext(OTHERQ(lnk->lk_wrq)) == 0)) {
			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			if (mp_hdr)
				freemsg(mp_hdr);
			return;
		}
	}

	/*
	 * link the dlpi header to the top of the frame.
	 */
	if (mp_hdr == NULL) {
		/* Try the allocation (again) here */
		if ((mp_hdr = llc2_allocb((DL_UNITDATA_IND_SIZE +
					(2 * sizeof (dlsap_t))),
					BPRI_HI)) == NULL) {
			ILD_RWUNLOCK(lnk->lk_lock);
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			return;
		}
	}

	mp_hdr->b_wptr += (DL_UNITDATA_IND_SIZE + (2 * sizeof (dlsap_t)));
	mp_hdr->b_cont = mp;
	mp = mp_hdr;
	mp->b_datap->db_type = M_PROTO;

	dl = (dl_unitdata_ind_t *)mp->b_rptr;
	dl->dl_primitive = DL_UNITDATA_IND;

	dl->dl_dest_addr_length = lnk->lk_addr_length;
	dl->dl_dest_addr_offset = DL_UNITDATA_IND_SIZE;
	bcopy((caddr_t)dsap, (caddr_t)mp->b_rptr + dl->dl_dest_addr_offset,
		dl->dl_dest_addr_length);

	/* do dlsap format for TWG */
	if ((enet == 0) && (dsap->llc.dl_sap == SNAPSAP)) {
		/*
		 * skip 6 byte mac address, 1 byte SAP, 3 byte vendor code
		 */
		uchar_t *snap = mp->b_rptr + dl->dl_dest_addr_offset + 10;
		*snap++ = type >> 8;
		*snap = (type & 0xff);
	}

	dl->dl_src_addr_length = lnk->lk_addr_length + ri_len;
	dl->dl_src_addr_offset = DL_UNITDATA_IND_SIZE + sizeof (dlsap_t);
	bcopy((caddr_t)ssap,
		(caddr_t)mp->b_rptr + dl->dl_src_addr_offset,
		dl->dl_src_addr_length);

	/*
	 * addrtype is set by dlpi_check_multicast() as follows:
	 * 2 = frame is addressed to this node (i.e., Unicast)
	 * 1 = frame is a broadcast/multicast address
	 */

	dl->dl_group_address = ((addrtype == 2) ? 0: 1);

	mp_hdr = mp->b_cont;

	lnk->lk_close |= LKCL_INUSE;	/* ranga upport from 2d muoe952936 */

	if (lnk->lk_wrq != NULL)
		qreply(lnk->lk_wrq, mp);
	else
		freemsg(mp);

	/* Clear in use flag and wakeup dlpi_close() */
	lnk->lk_close &= (~LKCL_INUSE);
	if (lnk->lk_close & LKCL_WAKE) {
		/* muoe962955 buzz 10/30/96 */
		ILD_WAKEUP(lnk->sleep_var);
	}
	ILD_RWUNLOCK(lnk->lk_lock);
	/* muoe952936	*/

}

/*
 * Function Label:	dlpi_xid_ind
 * Description:	comment
 * Locks:	No locks set on entry/exit.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 *		lnk->lk_lock locked/unlocked.
 */
void
dlpi_xid_ind(mac_t *mac, dlsap_t *dsap, dlsap_t *ssap, mblk_t *dp,
    ushort_t response, ushort_t final, int ri_len)
{
	mblk_t *mp;
	dLnkLst_t *headlnk;
	link_t *lnk;
	dl_xid_ind_t *dl;
	ushort_t type = 0;
	uint_t received_org_id = 0;
	uint_t stored_id = 0;
	uint_t mac_ppa;


	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if (dsap->llc.dl_sap == SNAPSAP) {
		type = *(ushort_t *)(dp->b_rptr + 3);
		type = (type >> 8) | (type << 8);
		received_org_id = (htonl(*(uint_t *)(dp->b_rptr))) >> 8;
		dp->b_rptr += 5;
	}

	/*
	 * find the recv'ing link
	 */

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		/* muoe962998 11/12/96 buzz */
		if (((lnk->lk_state == DL_IDLE) ||
			(lnk->lk_state == DL_DATAXFER)) &&
			(lnk->lk_enet == 0) &&
			(lnk->lk_bind.bd_sap.llc.dl_sap == dsap->llc.dl_sap)) {
			stored_id = ((lnk->lk_subs_bind_vendor1 << 16) |
					(lnk->lk_subs_bind_vendor2 << 8) |
					lnk->lk_subs_bind_vendor3);
			if ((lnk->lk_bind.bd_sap.llc.dl_sap != SNAPSAP) ||
				((lnk->lk_subs_bind_type == type) &&
				/*
				 * According to the internet RFCs, we
				 * should only accept those Vendor IDs or
				 * Organizationally Unique IDs in packets that
				 * match what came down in the SUBS_BIND_REQ
				 */
				(stored_id == received_org_id))) {
				/*
				 * At this point, we'd normally assume that
				 * the application wants this packet. But
				 * here's where the new changes
				 * for the multicast stuff goes. One last check
				 * to see if the received address match one of
				 * the appl's enabled multicast addresses
				 */
				if (dlpi_check_multicast(lnk,
						dsap->llc.dl_nodeaddr)) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			}
		}

		ILD_RWUNLOCK(lnk->lk_lock);
		lnk = (link_t *)lnk->chain.flink;
	}

	if (lnk == (link_t *)headlnk) {

		/*
		 * buzz 08-Mar-93 remove unlock,
		 * If we're here there's no lock set
		 * Not only that, but there's no link either!
		 */
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(dp);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		return;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	if (lnk->lk_wrq && (canputnext(OTHERQ(lnk->lk_wrq)) == 0)) {

		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(dp);
		return;
	}

	/*
	 * link the dlpi header to the top of the frame
	 */
	if ((mp = llc2_allocb(DL_XID_IND_SIZE + (2 * sizeof (dlsap_t)),
				BPRI_HI)) == NULL) {
		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_XID_IND\n");
		freemsg(dp);
		return;
	}

	if (msgdsize(dp) > 0) {
		mp->b_cont = dp;
	} else {
		freemsg(dp);
	}
	mp->b_wptr += DL_XID_IND_SIZE + (2 * sizeof (dlsap_t));
	mp->b_datap->db_type = M_PROTO;

	dl = (dl_xid_ind_t *)mp->b_rptr;
	dl->dl_primitive = DL_XID_IND;

	dl->dl_dest_addr_length = lnk->lk_addr_length;
	dl->dl_dest_addr_offset = DL_XID_IND_SIZE;
	bcopy((caddr_t)dsap,
		(caddr_t)mp->b_rptr + dl->dl_dest_addr_offset,
		dl->dl_dest_addr_length);

	dl->dl_src_addr_length = lnk->lk_addr_length + ri_len;
	dl->dl_src_addr_offset = DL_XID_IND_SIZE + sizeof (dlsap_t);
	bcopy((caddr_t)ssap,
		(caddr_t)mp->b_rptr + dl->dl_src_addr_offset,
		dl->dl_src_addr_length);

	if (response)
		dl->dl_primitive = DL_XID_CON;
	dl->dl_flag = final;

	if (lnk->lk_wrq != NULL)
		qreply(lnk->lk_wrq, mp);
	else
		freemsg(mp);

	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	dlpi_test_ind
 * Description:	comment
 */
void
dlpi_test_ind(mac_t *mac, dlsap_t *dsap, dlsap_t *ssap, mblk_t *bp,
    int response, int pf, int ri_len)
{
	mblk_t *mp;
	dLnkLst_t *headlnk;
	link_t *lnk;
	dl_test_ind_t *dl;
	ushort_t type = 0;
	uint_t received_org_id = 0;
	uint_t stored_id = 0;
	uint_t mac_ppa;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if (dsap->llc.dl_sap == SNAPSAP) {
		type = *(ushort_t *)(bp->b_rptr + 3);
		type = (type >> 8) | (type << 8);
		received_org_id = (htonl(*(uint_t *)(bp->b_rptr))) >> 8;
		bp->b_rptr += 5;
	}

	/*
	 * find the recv'ing link
	 */

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];

	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		/* muoe962998 11/12/96 buzz */
		if (((lnk->lk_state == DL_IDLE) ||
			(lnk->lk_state == DL_DATAXFER)) &&
			(lnk->lk_enet == 0) &&
			(lnk->lk_bind.bd_sap.llc.dl_sap == dsap->llc.dl_sap)) {
			stored_id = ((lnk->lk_subs_bind_vendor1 << 16) |
					(lnk->lk_subs_bind_vendor2 << 8) |
					lnk->lk_subs_bind_vendor3);
			if ((lnk->lk_bind.bd_sap.llc.dl_sap != SNAPSAP) ||
				((lnk->lk_subs_bind_type == type) &&
				/*
				 * According to the internet RFCs, we
				 * should only accept those Vendor IDs or
				 * Organizationally Unique IDs in packets that
				 * match what came down in the SUBS_BIND_REQ
				 */
				(stored_id == received_org_id))) {

				/*
				 * At this point, we'd normally assume that
				 * the application wants this packet. But
				 * here's where the new changes
				 * for the multicast stuff goes. One last check
				 * to see if the received address match one of
				 * the appl's enabled multicast addresses
				 */
				if (dlpi_check_multicast(lnk,
						dsap->llc.dl_nodeaddr)) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			}
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}


	if (lnk == (link_t *)headlnk) {
		/*
		 * buzz 08-Mar-93 removed the UNLOCK for the link.
		 * if we're here there ain't no link
		 */
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(bp);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		return;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	if (lnk->lk_wrq && (canputnext(OTHERQ(lnk->lk_wrq)) == 0)) {

		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(bp);
		return;
	}

	/*
	 * link the dlpi header to the top of the frame
	 */
	if ((mp = llc2_allocb(DL_TEST_IND_SIZE + (2 * sizeof (dlsap_t)),
				BPRI_HI)) == NULL) {

		ILD_RWUNLOCK(lnk->lk_lock);

		cmn_err(CE_WARN, "!LLC2: could not allocb DL_TESTCMD_IND\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(bp);
		return;
	}

	if (msgdsize(bp) != 0) {
		mp->b_cont = bp;
	} else {
		freemsg(bp);
	}
	mp->b_wptr += DL_TEST_IND_SIZE + (2 * sizeof (dlsap_t));
	mp->b_datap->db_type = M_PROTO;

	dl = (dl_test_ind_t *)mp->b_rptr;
	dl->dl_primitive = ((response) ? DL_TEST_CON : DL_TEST_IND);
	dl->dl_flag = ((pf) ? DL_POLL_FINAL : 0);

	dl->dl_dest_addr_length = lnk->lk_addr_length;
	dl->dl_dest_addr_offset = DL_TEST_IND_SIZE;
	bcopy((caddr_t)dsap,
		(caddr_t)mp->b_rptr + dl->dl_dest_addr_offset,
		dl->dl_dest_addr_length);

	dl->dl_src_addr_length = lnk->lk_addr_length + ri_len;
	dl->dl_src_addr_offset = DL_TEST_IND_SIZE + sizeof (dlsap_t);
	bcopy((caddr_t)ssap,
		(caddr_t)mp->b_rptr + dl->dl_src_addr_offset,
		dl->dl_src_addr_length);

	if (lnk->lk_wrq != NULL)
		qreply(lnk->lk_wrq, mp);
	else
		freemsg(mp);

	ILD_RWUNLOCK(lnk->lk_lock);
}


/*
 * Function Label:	dlpi_unbind_con
 * Locks:	No locks set on entry/exit.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 *		lnk->lk_lock locked/unlocked.
 */
void
dlpi_unbind_con(mac_t *mac, uint_t dlsap)
{
	dLnkLst_t *headlnk;
	link_t *lnk;
	uint_t mac_ppa;

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);

	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_wrq) &&
			(lnk->lk_state == DL_UNBIND_PENDING)) {
			if (dlsap > 0x00FF) {
				if ((lnk->lk_enet != 0) &&
					(lnk->lk_bind.bd_sap.ether.dl_type
					== dlsap)) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			} else {
				if ((lnk->lk_enet == 0) &&
					(lnk->lk_bind.bd_sap.llc.dl_sap
					== dlsap)) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			}
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	if (lnk != (link_t *)headlnk) {
		/*
		 * found the link_t element, determine whether an internal or
		 * external command and react appropriately
		 */
		if (lnk->lk_bind.bd_max_conind > 0) {
			ILD_RWUNLOCK(lnk->lk_lock);
			if (dlpiDelListenQ(&listenQueue[mac_ppa], lnk)) {
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
						0, 0);
			}
			ILD_WRLOCK(lnk->lk_lock);
		}

		bzero((caddr_t)&lnk->lk_bind.bd_sap, sizeof (dlsap_t));
		if ((lnk->lk_close & LKCL_PEND) == 0) {
			mblk_t *mp;
			dl_ok_ack_t *dl;
			queue_t *q;

			if ((mp = llc2_allocb(DL_OK_ACK_SIZE, BPRI_HI))
				== NULL) {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				ILD_RWUNLOCK(lnk->lk_lock);
				return;
			}

			mp->b_datap->db_type = M_PCPROTO;
			mp->b_wptr += DL_OK_ACK_SIZE;

			dl = (dl_ok_ack_t *)mp->b_rptr;
			dl->dl_primitive = DL_OK_ACK;
			dl->dl_correct_primitive = DL_UNBIND_REQ;

			/*
			 * Check again for Close Pending  to see
			 * if we need to do a wakeup
			 */
			lnk->lk_state = DL_UNBOUND;
			if (lnk->lk_close & LKCL_PEND) {
				/*
				 * issue wakeup call using lnk as the key
				 */
				lnk->lk_close |= LKCL_OKAY;
				if (lnk->lk_close & LKCL_WAKE) {
					/* muoe961994 buzz 6/17/96 */
					/* muoe962955 buzz 10/30/96 */
					ILD_WAKEUP(lnk->sleep_var);
				}
			}
			q = lnk->lk_wrq;
			/*
			 * muoe961994 buzz 6/17/96
			 * Moved qreply to end of processing to avoid unlocking
			 * in the middle of state changes and sleep stuff
			 */
			if (q != NULL)
				qreply(q, mp);
			else
				freemsg(mp);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			/*
			 * issue wakeup call using lnk as the key
			 */
			lnk->lk_close |= LKCL_OKAY;
			if (lnk->lk_close & LKCL_WAKE) {
				/* muoe961994 buzz 6/17/96 */
				/* muoe962955 buzz 10/30/96 */
				ILD_WAKEUP(lnk->sleep_var);
			}

			ILD_RWUNLOCK(lnk->lk_lock);

		}
	} else {
		/*
		 * we are in a world of hurt here is this was an internal
		 * command, the processing trying to close the stream is
		 * permanently asleep
		 */

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	}

}

/*
 * Function Label:	dlpi_connect_con
 * Description:	comment
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock for both listener and acceptor
 *		links may be locked/unlocked.
 *		ild_listenq_lock locked/unlocked.
 *		ild_glnk_lck[mac_ppa] locked/unlocked.
 *		ild_lnk_lock locked/unlocked.
 */
void
dlpi_connect_con(mac_t *mac, link_t *lnk, ushort_t sid, ushort_t rc)
{
	dl_connect_con_t *dl;
	mblk_t *mp;
	link_t *lLnk;
	listenQ_t *lqPtr;
	uint_t mac_ppa;
	dlsap_t dsap;
	queue_t *q;

	ASSERT(mac != NULL);

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);

	if (lnk == NULL) {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		return;
	}

	ILD_WRLOCK(lnk->lk_lock);
	q = lnk->lk_wrq;
	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	if (q == NULL) {
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	}
	if (lnk->lk_mac == mac) {
		if (lnk->lk_state == DL_CONN_RES_PENDING) {
			lLnk = NULL;
			ILD_WRLOCK(ild_listenq_lock);
			lqPtr = listenQueue[mac_ppa];
			while (lqPtr != NULL) {
				if (lqPtr->lnk) {

					if (lqPtr->lnk->lk_sid ==
						GETSAPID(sid)) {
						lLnk = lqPtr->lnk;
						break;
					}
				}
				lqPtr = lqPtr->next;
			}
			/*
			 * Need to drop link-lock momentarily to ensure
			 * correct locking order.
			 */
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_WRLOCK(ild_lnk_lock);
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			/* Make sure the link is still there */
			if (q->q_ptr != NULL) {
				ILD_WRLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_lnk_lock);
			} else {
				/* Link is gone. Can't even send error ack */
				ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
				ILD_RWUNLOCK(ild_lnk_lock);
				ILD_RWUNLOCK(ild_listenq_lock);
				return;
			}
			ILD_RWUNLOCK(ild_listenq_lock);
			if (lLnk == NULL) {
				/*
				 * this can not happen, but a check is
				 * being made anyway
				 */
				ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk,
						((uint_t)rc << 16) + sid, 0);
				lnk->lk_state = DL_IDLE;
				ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
				dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
						DL_SYSERR, rc);
				ILD_RWUNLOCK(lnk->lk_lock);
				return;
			}

			/*
			 * We could be accepting connection on the same lnk. In
			 * which case, lnk will be same as lLnk and will already
			 * be locked.
			 */
			if (lnk != lLnk)
				ILD_WRLOCK(lLnk->lk_lock);

			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			if ((lLnk->lk_close & LKCL_PEND) == 0) {
				/*
				 * user must have issued
				 * DL_CONNECT_RES to get here
				 */
				if (rc) {
					ild_trace(MID_DLPI, __LINE__,
							(uintptr_t)lnk,
							((uint_t)rc << 16) +
							sid, 0);
					if (delOutInd(lLnk, sid)) {
						ild_trace(MID_DLPI,
							__LINE__,
							(uintptr_t)lLnk,
							sid, 0);
					}
					lnk->lk_state = DL_IDLE;
					dlpi_error_ack(lnk->lk_wrq,
							DL_CONNECT_RES,
							DL_SYSERR, rc);
					ILD_RWUNLOCK(lnk->lk_lock);
				} else {
					ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, sid, 0);
					/*
					 * find and remove the element
					 * for this sid from the
					 * outstanding connect
					 * indication queue, the remote
					 * address has already been
					 * transferred during the
					 * connect response phase
					 */
					if (delOutInd(lLnk, sid)) {
						ild_trace(MID_DLPI,
							__LINE__,
							(uintptr_t)lLnk,
							sid, 0);
					}
					lnk->lk_state = DL_DATAXFER;
					if ((lLnk->lk_bind.bd_conind += 1) ==
						lLnk->lk_bind.bd_max_conind) {
						if (lLnk->lk_state ==
							DL_INCON_PENDING) {
							lLnk->lk_state =
								DL_IDLE;
						}
					}
					ILD_RWUNLOCK(lnk->lk_lock);
					/* For x25, it should be */
					dlpi_ok_ack(lLnk->lk_wrq,
							DL_CONNECT_RES);
					/* Original code from NCR */
					/*
					 * dlpi_ok_ack(lnk->lk_wrq,
					 * DL_CONNECT_RES);
					 */
				}
				if ((lLnk->lk_state == DL_INCON_PENDING) ||
					(lLnk->lk_state == DL_IDLE)) {
					mblk_t  *pend_mp;
					queue_t *pend_q = NULL;
					/*
					 * now is the time to send up
					 * any pending disconnect
					 * indications
					 */
					/*
					 * Just pull off the head mp
					 * They should all be chained
					 * correctly.
					 * Then qreply's the whole chain
					 */
					if ((pend_mp = lLnk->lkDisIndHead)
						!= NULL) {
						lLnk->lkDisIndHead = NULL;
						pend_q = lLnk->lk_wrq;
					}

					/*
					 * now is the time to send up
					 * the next pending connect
					 * indication (if there is one)
					 */
					if (lLnk->lkPendHead != NULL) {
						dl_connect_ind_t *d;
						mblk_t *mp = lLnk->lkPendHead;
						lLnk->lkPendHead =
						lLnk->lkPendHead->b_next;
						lLnk->lk_state =
							DL_INCON_PENDING;
						lLnk->lk_bind.bd_conind -= 1;
						d = (dl_connect_ind_t *)
							mp->b_rptr;
						/*
						 * add sid for this connect
						 * indication to the
						 * outstanding DL_CONNECT_IND
						 * list
						 */
						bcopy((caddr_t)(mp->b_rptr +
						d->dl_calling_addr_offset),
						(caddr_t)&dsap,
						sizeof (dlsap_t));
						if (addOutInd(lLnk,
						d->dl_correlation, &dsap)) {
							ild_trace(MID_DLPI,
							__LINE__,
							(uintptr_t)lLnk,
							d->dl_correlation, 0);
						}
						/*
						 * send up the (possible) chain
						 * of Disc Ind
						 */
						if ((pend_mp) && (pend_q)) {
							qreply(pend_q, pend_mp);
						} else {
							if (pend_mp)
							freemsg(pend_mp);
						}
						if (lLnk->lk_wrq != NULL)
							qreply(lLnk->lk_wrq,
								mp);
						else
							freemsg(mp);
					} else {
						if ((pend_mp) && (pend_q)) {
							qreply(pend_q, pend_mp);
						} else {
							if (pend_mp)
							freemsg(pend_mp);
						}
					}
				}

				if (lnk != lLnk)
					ILD_RWUNLOCK(lLnk->lk_lock);

			} else {
				/*
				 * close on a listening stream active,
				 * do not muck things up by sending up
				 * a message or by messing with the
				 * pending queues, close will take care
				 * of disconnecting the now potentially
				 * active connection, simply note the
				 * fact we were here
				 */
				ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk,
						((uint_t)rc << 16) + sid, 0);

				if (lnk != lLnk)
					ILD_RWUNLOCK(lLnk->lk_lock);
				ILD_RWUNLOCK(lnk->lk_lock);
			}
			return;
		}


		if (lnk->lk_state == DL_OUTCON_PENDING) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			if (rc == 0) {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				if ((mp = llc2_allocb(DL_CONNECT_CON_SIZE +
							IEEE_ADDR_SIZE,
							BPRI_HI)) == NULL) {

					ILD_RWUNLOCK(lnk->lk_lock);

					cmn_err(CE_WARN,
						"!LLC2: could not allocb\n");
					ild_trace(MID_DLPI, __LINE__,
							0, 0, 0);
					return;
				}
				mp->b_datap->db_type = M_PROTO;
				mp->b_wptr += DL_CONNECT_CON_SIZE +
					IEEE_ADDR_SIZE;

				dl = (dl_connect_con_t *)mp->b_rptr;
				dl->dl_primitive = DL_CONNECT_CON;
				dl->dl_resp_addr_length =
					IEEE_ADDR_SIZE;
				dl->dl_resp_addr_offset =
					DL_CONNECT_CON_SIZE;
				bcopy((caddr_t)&lnk->lk_dsap,
					(caddr_t)dl +
					dl->dl_resp_addr_offset,
					IEEE_ADDR_SIZE);
				dl->dl_qos_length = 0;
				dl->dl_qos_offset = 0;
				dl->dl_growth = 0;
				lnk->lk_state = DL_DATAXFER;

				if (lnk->lk_wrq != NULL)
					qreply(lnk->lk_wrq, mp);
				else
					freemsg(mp);
				ILD_RWUNLOCK(lnk->lk_lock);
			} else {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				lnk->lk_state = DL_IDLE;

				dlpi_error_ack(lnk->lk_wrq,
						DL_CONNECT_REQ,
						DL_SYSERR, rc);
				ILD_RWUNLOCK(lnk->lk_lock);
			}
			return;
		}
	}
	ILD_RWUNLOCK(lnk->lk_lock);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

}

/*
 * Function Label:	dlpi_connect_ind
 * Description:	processing incoming connect requests from the link
 * Locks:	No locks set on entry/exit.
 *		ild_listenq_lock locked/unlocked.
 *		lnk->lk_lock locked/unlocked.
 */
void
dlpi_connect_ind(mac_t *mac, short dsap, dlsap_t *ssap, ushort_t sid)
{
	dl_connect_ind_t *dl;
	link_t *lnk;
	mblk_t *mp;
	listenQ_t *lqPtr;
	uint_t mac_ppa;
	dlsap_t dd;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	ASSERT(mac != NULL);
	mac_ppa = mac->mac_ppa;

	lnk = NULL;
	ILD_WRLOCK(ild_listenq_lock);
	lqPtr = listenQueue[mac_ppa];
	while (lqPtr != NULL) {
		if (lqPtr->lnk)  /* muoe932415 buzz 08/02/93 */  {
			/*
			 * Ensure matching link is truly set up for listening.
			 */
			if ((lqPtr->lnk->lk_bind.bd_sap.llc.dl_sap == dsap) &&
				(lqPtr->lnk->lk_bind.bd_max_conind > 0)) {
				lnk = lqPtr->lnk;
				break;
			}
		}
		lqPtr = lqPtr->next;
	}
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	ILD_RWUNLOCK(ild_listenq_lock);
	/*
	 * changed below to not lock the lnk until we determine
	 * that it's not NULL! buzz 08-Mar-93
	 */
	if (lnk != NULL) {
		ILD_WRLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		if ((lnk->lk_state == DL_IDLE) ||
			(lnk->lk_state == DL_INCON_PENDING)) {
			if ((mp = llc2_allocb(DL_CONNECT_IND_SIZE +
						(IEEE_ADDR_SIZE * 2),
						BPRI_HI)) == NULL) {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				cmn_err(CE_WARN, "!LLC2: could not allocb\n");
				/*
				 * this function should return an error to
				 * LLC so that appropriate steps can be taken
				 * so that when the next SABME arrives it
				 * will again be indicated
				 */
				ILD_RWUNLOCK(lnk->lk_lock);
				return;
			}
			lnk->lk_state = DL_INCON_PENDING;
			lnk->lk_sid = GETSAPID(sid);

			mp->b_datap->db_type = M_PROTO;
			mp->b_wptr += DL_CONNECT_IND_SIZE +
				(IEEE_ADDR_SIZE * 2);

			dl = (dl_connect_ind_t *)mp->b_rptr;
			dl->dl_primitive = DL_CONNECT_IND;
			dl->dl_correlation = sid;
			dl->dl_called_addr_length = IEEE_ADDR_SIZE;
			dl->dl_called_addr_offset = DL_CONNECT_IND_SIZE;
			dl->dl_calling_addr_length = IEEE_ADDR_SIZE;
			dl->dl_calling_addr_offset = DL_CONNECT_IND_SIZE +
				IEEE_ADDR_SIZE;
			bcopy((caddr_t)ssap, (caddr_t)dl +
				dl->dl_calling_addr_offset, IEEE_ADDR_SIZE);
			bcopy((caddr_t)ssap, (caddr_t)&dd, sizeof (dlsap_t));
			bcopy((caddr_t)&lnk->lk_bind.bd_sap,
				(caddr_t)dl + dl->dl_called_addr_offset,
				IEEE_ADDR_SIZE);
			dl->dl_qos_length = 0;
			dl->dl_qos_offset = 0;
			dl->dl_growth = 0;
			if (lnk->lk_bind.bd_conind == 0) {
				/*
				 * already have maximum number of
				 * outstanding connect_ind, must hold on
				 * queue until an outstanding connect_ind is
				 * responded to
				 */
				if (lnk->lkPendHead == NULL) {
					lnk->lkPendHead = lnk->lkPendTail = mp;
				} else {
					lnk->lkPendTail->b_next = mp;
					lnk->lkPendTail = mp;
				}
				mp->b_next = NULL;
				ILD_RWUNLOCK(lnk->lk_lock);
			} else {
				/*
				 * decrement the conind counter, this link
				 * has accepted a  connect_ind
				 */
				lnk->lk_bind.bd_conind -= 1;
				/*
				 * add sid for this connect indication to the
				 * outstanding DL_CONNECT_IND list
				 */

				if (addOutInd(lnk, dl->dl_correlation,
						(dlsap_t *)&dd)) {
					ild_trace(MID_DLPI, __LINE__,
							(uintptr_t)lnk,
							dl->dl_correlation, 0);
				}

				if (lnk->lk_wrq != NULL)
					qreply(lnk->lk_wrq, mp);
				else
					freemsg(mp);
				ILD_RWUNLOCK(lnk->lk_lock);
			}

			return;
		} else
			ILD_RWUNLOCK(lnk->lk_lock);
	} else {
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	}

	/*
	 * at this point we should be looking for the conn_mgmt queue
	 */
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((lnk = mac->mac_conn_mgmt) != (link_t *)NULL) {
		/*
		 * this is a big HOLE in that a DL_CONNECT_IND is never
		 * generated here
		 */
		if (!lnk->lk_bind.bd_conind) {
			cmn_err(CE_WARN, "!LLC2: conn_mgmt can't accept con\n");
		}
	}

	(void) llc2DisconnectReq(mac, sid);

}

/*
 * Function Label:	dlpi_test_response_ind
 * Description:	comment
 * Locks:	No locks set on entry/exit.
 *		lnk->lk_lock locked/unlocked.
 */
void
dlpi_test_response_ind(mac_t *mac, dlsap_t *dsap, dlsap_t *ssap, mblk_t *mp)
{
	mblk_t *mp_hdr;
	dLnkLst_t *headlnk;
	link_t *lnk;
	dl_test_ind_t *dl;
	ushort_t type = 0;
	uint_t received_org_id = 0;
	uint_t stored_id = 0;
	uint_t mac_ppa;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	/*
	 * find the recv'ing link
	 */
	if (dsap->llc.dl_sap == SNAPSAP) {
		type = *(ushort_t *)(mp->b_rptr + 3);
		type = (type >> 8) | (type << 8);
		received_org_id = (htonl(*(uint_t *)(mp->b_rptr))) >> 8;
		mp->b_rptr += 5;
	}

	mac_ppa = mac->mac_ppa;

	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {

		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_state == DL_IDLE) &&
			(lnk->lk_enet == 0) &&
			(lnk->lk_bind.bd_sap.llc.dl_sap == dsap->llc.dl_sap)) {
			stored_id = ((lnk->lk_subs_bind_vendor1 << 16) |
					(lnk->lk_subs_bind_vendor2 << 8) |
					lnk->lk_subs_bind_vendor3);

			if ((lnk->lk_bind.bd_sap.llc.dl_sap != SNAPSAP) ||
				((lnk->lk_subs_bind_type == type) &&
				/*
				 * According to the internet RFCs, we
				 * should only accept those Vendor IDs or
				 * Organizationally Unique IDs in packets that
				 * match what came down in the SUBS_BIND_REQ
				 */
				(stored_id == received_org_id))) {
				/*
				 * At this point, we'd normally assume that
				 * the application wants this packet. But
				 * here's where the new changes
				 * for the multicast stuff goes. One last
				 * check to see if the received address match
				 * one of the appl's enabled
				 * multicast addresses
				 */
				if (dlpi_check_multicast(lnk,
						dsap->llc.dl_nodeaddr)) {
					ILD_SWAPIPL(mac_ppa, lnk);
					break;
				}
			}
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}


	if (lnk == (link_t *)headlnk) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		freemsg(mp);
		return;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	if (lnk->lk_wrq && (canputnext(OTHERQ(lnk->lk_wrq)) == 0)) {

		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		return;
	}

	/*
	 * link the dlpi header to the top of the frame
	 */
	if ((mp_hdr = llc2_allocb(DL_TEST_IND_SIZE + (2 * sizeof (dlsap_t)),
				BPRI_HI)) == NULL) {

		ILD_RWUNLOCK(lnk->lk_lock);

		cmn_err(CE_WARN, "!LLC2: could not allocb DL_TESTCMD_IND\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		return;
	}
	mp_hdr->b_wptr += DL_TEST_IND_SIZE + (2 * sizeof (dlsap_t));
	mp_hdr->b_cont = mp;
	mp = mp_hdr;
	mp->b_datap->db_type = M_PROTO;

	dl = (dl_test_ind_t *)mp->b_rptr;
	dl->dl_primitive = DL_TEST_IND;
	dl->dl_dest_addr_length = lnk->lk_addr_length;
	dl->dl_dest_addr_offset = DL_TEST_IND_SIZE;

	bcopy((caddr_t)dsap,
		(caddr_t)mp->b_rptr + dl->dl_dest_addr_offset,
		lnk->lk_addr_length);

	dl->dl_src_addr_length = lnk->lk_addr_length;
	dl->dl_src_addr_offset = DL_TEST_IND_SIZE + sizeof (dlsap_t);
	bcopy((caddr_t)ssap,
		(caddr_t)mp->b_rptr + dl->dl_src_addr_offset,
		lnk->lk_addr_length);

	if (lnk->lk_wrq != NULL)
		qreply(lnk->lk_wrq, mp);
	else
		freemsg(mp);
	ILD_RWUNLOCK(lnk->lk_lock);

}


/*
 * Function Label:	dlpi_XON
 * Description:	comment
 * Locks:
 */
void
dlpi_XON(mac_t *mac, link_t *lnk)
{
	if (lnk->lk_mac == mac && lnk->lk_wrq) {
		enableok(lnk->lk_wrq);
		qenable(lnk->lk_wrq);
	}

}

/* LOCAL PROCEDURES */

/*
 * Function Label:	dlpi_bind_ack
 * Description:	send DL_BIND_ACK to specified queue
 * Locks:	No locks set on entry/exit.
 *		No locks set internally.
 */
void
dlpi_bind_ack(queue_t *q, dlsap_t *ssap, uint_t max_conind, int enet)
{
	mblk_t *mp;
	dl_bind_ack_t *dl;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((mp = llc2_allocb(DL_BIND_ACK_SIZE + ETHER_ADDR_SIZE, BPRI_HI))
		== NULL) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_BIND_ACK\n");
		return;
	}
	mp->b_datap->db_type = M_PCPROTO;
	mp->b_wptr += DL_BIND_ACK_SIZE + ETHER_ADDR_SIZE;

	dl = (dl_bind_ack_t *)mp->b_rptr;
	dl->dl_primitive = DL_BIND_ACK;

	if (enet) {
		dl->dl_sap = ssap->ether.dl_type;
		dl->dl_addr_length = ETHER_ADDR_SIZE;
	} else {
		dl->dl_sap = ssap->llc.dl_sap;
		dl->dl_addr_length = IEEE_ADDR_SIZE;
	}

	dl->dl_addr_offset = DL_BIND_ACK_SIZE;
	dl->dl_max_conind = max_conind;
	dl->dl_xidtest_flg = 0;
	bcopy((caddr_t)ssap,
		(caddr_t)dl + dl->dl_addr_offset,
		dl->dl_addr_length);
	if (q)
		qreply(q, mp);
	else
		freemsg(mp);

}

/*
 * Function Label:	dlpi_error_ack
 * Description:	comment
 * Locks:	No locks allowed on entry.
 */
void
dlpi_error_ack(queue_t *q, uint_t dl_error_primitive, int dl_errno,
    int dl_unix_errno)
{
	mblk_t *mp;
	dl_error_ack_t *dl;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((mp = llc2_allocb(DL_ERROR_ACK_SIZE, BPRI_HI)) == NULL) {
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_ERROR_ACK\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		return;
	}
	mp->b_datap->db_type = M_PCPROTO;
	mp->b_wptr += DL_ERROR_ACK_SIZE;

	dl = (dl_error_ack_t *)mp->b_rptr;
	dl->dl_primitive = DL_ERROR_ACK;
	dl->dl_error_primitive = dl_error_primitive;
	dl->dl_errno = dl_errno;
	dl->dl_unix_errno = dl_unix_errno;
	if (q)
		qreply(q, mp);
	else
		freemsg(mp);

}


/*
 * Function Label:	dlpi_uderror_ind()
 * Description:	comment
 * Locks:	No locks allowed on entry.
 */
void
dlpi_uderror_ind(queue_t *q, mblk_t *mp, uint_t dl_errno)
{
	mblk_t *uderr_mp;
	dl_uderror_ind_t *dl;
	dl_unitdata_req_t *req_dl = (dl_unitdata_req_t *)mp->b_rptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((uderr_mp = llc2_allocb(DL_UDERROR_IND_SIZE +
				req_dl->dl_dest_addr_length,
				BPRI_HI)) == NULL) {
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_UDERROR_IND\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		return;
	}
	dl = (dl_uderror_ind_t *)uderr_mp->b_wptr;

	uderr_mp->b_datap->db_type = M_PROTO;
	uderr_mp->b_wptr += DL_UDERROR_IND_SIZE +
		req_dl->dl_dest_addr_length;

	dl->dl_primitive = DL_UDERROR_IND;
	dl->dl_dest_addr_length = req_dl->dl_dest_addr_length;
	dl->dl_dest_addr_offset = DL_UDERROR_IND_SIZE;
	dl->dl_unix_errno = 0;
	dl->dl_errno = dl_errno;
	bcopy((caddr_t)req_dl + req_dl->dl_dest_addr_offset,
		(caddr_t)dl + dl->dl_dest_addr_offset,
		req_dl->dl_dest_addr_length);
	if (q)
		qreply(q, uderr_mp);
	else
		freemsg(uderr_mp);

}


/*
 * Function Label:	dlpi_ok_ack
 * Description:	comment
 * Locks:	No locks allowed on entry.
 */
void
dlpi_ok_ack(queue_t *q, uint_t dl_correct_primitive)
{
	mblk_t *mp;
	dl_ok_ack_t *dl;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((mp = llc2_allocb(DL_OK_ACK_SIZE, BPRI_HI)) == NULL) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_OK_ACK\n");
		return;
	}

	mp->b_datap->db_type = M_PCPROTO;
	mp->b_wptr += DL_OK_ACK_SIZE;

	dl = (dl_ok_ack_t *)mp->b_rptr;
	dl->dl_primitive = DL_OK_ACK;
	dl->dl_correct_primitive = dl_correct_primitive;
	if (q)
		qreply(q, mp);
	else
		freemsg(mp);

}


/*
 * Function Label:   dlpi_info_req
 * Description:	process DL_INFO_REQ messages.
 * Locks:	lnk is WR locked on entry, except when called directly
 *		from dlpi_state (to handle the case of someone doing
 *		a DL_INFO_REQ even when LLC2 doesn't have any mac
 *		interfaces underneath). Unlocking the lnk is the
 *		responsibility of the calling function.
 */
void
dlpi_info_req(queue_t *q)
{
	link_t *lnk = (link_t *)q->q_ptr;
	mblk_t *mp;
	dl_info_ack_t *dl;
	dlsap_t *dsap;
	mac_t *mac;
	macx_t *macx;

	/*
	 * SOLVP980099 buzz 06/02/98
	 * Several changes from here on down related to returning a
	 * "proper" DLPI Version 2 DL_INFO_ACK
	 */
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	/* SOLVP980099 */
	if ((mp = allocb((DL_INFO_ACK_SIZE + 7 + 6), BPRI_HI)) == NULL) {
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_INFO_ACK\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		return;
	}
	mp->b_datap->db_type = M_PCPROTO;
	/* SOLVP980099 */
	/* 7 -> addr len, 6 -> bcast len */
	mp->b_wptr += (DL_INFO_ACK_SIZE + 7 + 6);

	dl = (dl_info_ack_t *)mp->b_rptr;

	dl->dl_primitive = DL_INFO_ACK;

	/*
	 * At this point state could be UNATTACHED, dl_mac_type should also
	 * show DL_TPR, but DL_XXX's not defined to be able to 'OR' them
	 * together. dl_max_sdu is also dependent on which dl_mac_type.
	 */

	dl->dl_mac_type = DL_CSMACD;
	dl->dl_max_sdu = 1500;
	dl->dl_min_sdu = 1;
	dl->dl_addr_length = 7; /* SOLVP980099 */
	if (lnk)
		dl->dl_current_state = lnk->lk_state;
	else
		dl->dl_current_state = DL_UNATTACHED;
	dl->dl_addr_offset = 0;
	dl->dl_service_mode = DL_CLDLS | DL_CODLS;
	dl->dl_version = DL_VERSION_2;
	dl->dl_sap_length = 0; /* SOLVP980099 */
	dl->dl_brdcst_addr_offset = 0; /* SOLVP980099 */
	dl->dl_brdcst_addr_length = 6; /* SOLVP980099 */
	dsap = (dlsap_t *)(mp->b_rptr + DL_INFO_ACK_SIZE);
	/*
	 *	dlpi_info_req is valid and supported in the following states:
	 *		DL_UNATTACHED, DL_UNBOUND, DL_IDLE, DL_DATAXFER.
	 *	dlpi_info_req is also valid in DL_OUTCON_PENDING state but is
	 *	currently unsupported.
	 */

	if (lnk != NULL && lnk->lk_mac != NULL) {
		/* mac ptr will be non-null ONLY if we're attached */
		mac = lnk->lk_mac;
		ILD_RDLOCK(mac->mac_lock);
		macx = ild_macxp[mac->mac_ppa];

		dl->dl_brdcst_addr_offset = DL_INFO_ACK_SIZE;
		dl->dl_mac_type = mac->mac_type;
		CPY_MAC(lnk->lk_bind.bd_sap.llc.dl_nodeaddr,
			dsap->llc.dl_nodeaddr);
		dsap->llc.dl_sap = lnk->lk_bind.bd_sap.llc.dl_sap;

		/*  copy the broadcast info  */ /* SOLVP980099 */
		CPY_MAC(macx->broadcast_addr.mac_addr, dsap->llc.dl_nodeaddr);
		/*
		 * now reset the dsap pointer to get read for any address
		 * information that may follow
		 */
		/* SOLVP980099 */
		dsap = (dlsap_t *)(mp->b_rptr + DL_INFO_ACK_SIZE +
					dl->dl_brdcst_addr_length);
		if (lnk->lk_state == DL_UNBOUND) {
			dl->dl_service_mode = mac->mac_service_modes;
			dl->dl_max_sdu = mac->mac_max_sdu;
			dl->dl_min_sdu = mac->mac_min_sdu;
		} else if ((lnk->lk_state == DL_IDLE) ||
				(lnk->lk_state >= DL_UDQOS_PENDING)) {
			CPY_MAC(lnk->lk_bind.bd_sap.llc.dl_nodeaddr,
				dsap->llc.dl_nodeaddr);
			dsap->llc.dl_sap = lnk->lk_bind.bd_sap.llc.dl_sap;
			dl->dl_sap_length = -1; /* SOLVP980099 */
			dl->dl_addr_offset  = DL_INFO_ACK_SIZE +
				dl->dl_brdcst_addr_length;

			dl->dl_service_mode = lnk->lk_bind.bd_service_mode;
			dl->dl_max_sdu = lnk->lk_max_sdu;
			dl->dl_min_sdu = lnk->lk_min_sdu;

		}
		ILD_RWUNLOCK(mac->mac_lock);
	}

	dl->dl_reserved = 0;
	dl->dl_qos_length = 0;
	dl->dl_qos_offset = 0;
	dl->dl_qos_range_length = 0;
	dl->dl_qos_range_offset = 0;
	dl->dl_provider_style = DL_STYLE2;
	dl->dl_growth = 0;

	if (q)
		qreply(q, mp);
	else
		freemsg(mp);
}


/*
 * Function Label:	dlpi_unbind_req
 * Description:	comment
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 *		lnk_p->lk_lock locked/unlocked.
 *		mac->mac_lock locked/unlocked.
 */
int
dlpi_unbind_req(link_t *lnk)
{
	uint_t dlsap;
	link_t *lnkp;
	dLnkLst_t *headlnk;
	uint_t mac_ppa;
	mac_t *mac;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac = lnk->lk_mac;
	mac_ppa = mac->mac_ppa;

	if (lnk->lk_enet) {
		/*
		 * if DIX ethernet then dl_type is always unique for a mac
		 */
		dlsap = lnk->lk_bind.bd_sap.ether.dl_type;
	} else {
		dlsap = lnk->lk_bind.bd_sap.llc.dl_sap;

		if (lnk == mac->mac_conn_mgmt) {
			ILD_WRLOCK(mac->mac_lock);
			mac->mac_conn_mgmt = NULL;
			ILD_RWUNLOCK(mac->mac_lock);
		}

		ILD_RWUNLOCK(lnk->lk_lock);
		ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
		headlnk = &mac_lnk[mac_ppa];
		ILD_WRLOCK(lnk->lk_lock);

		lnkp = (link_t *)headlnk->flink;
		while (lnkp != (link_t *)headlnk) {
			if (lnk != lnkp) {
				if ((lnkp->lk_wrq) &&
				(dlsap == lnkp->lk_bind.bd_sap.llc.dl_sap)) {
					lnk->lk_state = DL_UNBOUND;
					bzero((caddr_t)&lnk->lk_bind.bd_sap,
						sizeof (dlsap_t));
					if ((lnk->lk_close & LKCL_PEND) == 0) {
					/*
					 * if unbind_req from ILD user
					 * then send the ok_ack
					 */

					ILD_RWUNLOCK(lnk->lk_lock);
					ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

					dlpi_ok_ack(lnk->lk_wrq, DL_UNBIND_REQ);
					} else {
					/*
					 * unbind_req from dlpi_close,
					 * mark lk_close okay, do not
					 * need to issue wakeup since
					 * not sleeping yet
					 */
					lnk->lk_close |= LKCL_OKAY;

					ILD_RWUNLOCK(lnk->lk_lock);
					ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

					}
					return (0);
				}

			}

			lnkp = (link_t *)lnkp->chain.flink;
		}
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	}

	/*
	 * send the unbind to the lower layer ONLY if no other queues are
	 * bound to the sap
	 */
	lnk->lk_state = DL_UNBIND_PENDING;

	ILD_RWUNLOCK(lnk->lk_lock);

	if (llc2UnbindReq(mac, dlsap) == 0) {
		return (0);
	}

	/*
	 * unbind request failed at lower layer, leave state unchanged and
	 * return to caller with error indication, this will cause error_ack
	 */

	ILD_WRLOCK(lnk->lk_lock);

	lnk->lk_state = DL_IDLE;

	ILD_RWUNLOCK(lnk->lk_lock);

	return (1);
}


/*
 * assign a token identifier to the lnk for subsequent DL_CONNECT_RES calls.
 * uses the address of the link_t structure as the unique id.
 */
/*
 * Function Label:	dlpi_token_req
 * Description:	comment
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
dlpi_token_req(link_t *lnk)
{
	mblk_t		*mp;
	dl_token_ack_t	*dl;
	queue_t		*q = lnk->lk_wrq; /* Fetch q ptr while locked */


	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((mp = llc2_allocb(DL_TOKEN_ACK_SIZE, BPRI_HI)) == NULL) {
		cmn_err(CE_WARN, "!LLC2: could not allocb DL_TOKEN_ACK\n");
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	}
	mp->b_datap->db_type = M_PCPROTO;
	mp->b_wptr += DL_TOKEN_ACK_SIZE;

	dl = (dl_token_ack_t *)mp->b_rptr;
	dl->dl_primitive = DL_TOKEN_ACK;

#ifdef _LP64
	if (lnk->lk_minor == 0)
		dl->dl_token = dlpi_lnk_cnt + 1;
	else
		dl->dl_token = (t_uscalar_t)lnk->lk_minor;
#else
	dl->dl_token = (t_uscalar_t)lnk;
#endif

	if (q)
		qreply(q, mp);
	else
		freemsg(mp);

	ILD_RWUNLOCK(lnk->lk_lock);
}


/*
 * process DL_DISCONNECT_REQ messages
 */
/*
 * Function Label:	dlpi_disconnect_req
 * Description:	comment
 * Locks:	lnk is WR locked on entry/exit.
 */
int
dlpi_disconnect_req(link_t *lnk, uint_t corr)
{
	/* sturblen muoe971910  08/05/97  */
	ushort_t lk_state_save_incoming;
	ushort_t lk_state_save;
	ushort_t sid;
	mac_t *mac;
	int rc;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	/* sturblen muoe971910  08/05/97  */
	lk_state_save_incoming = lnk->lk_state;

	switch (lnk->lk_state) {
	case DL_OUTCON_PENDING:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON8_PENDING;
		break;
	case DL_INCON_PENDING:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON9_PENDING;
		break;
	case DL_DATAXFER:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON11_PENDING;
		break;
	case DL_USER_RESET_PENDING:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON12_PENDING;
		break;
	case DL_PROV_RESET_PENDING:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON13_PENDING;
		break;
	default:
		ild_trace(MID_DLPI, __LINE__, (uintptr_t)lnk, 0, 0);
		lnk->lk_state = DL_DISCON8_PENDING;
		break;
	}

	/* sturblen muoe971910  08/05/97  */
	lk_state_save = lnk->lk_state;

	/*
	 * get llc to close the link
	 */
	mac = lnk->lk_mac;
	sid = lnk->lk_sid;

	ILD_RWUNLOCK(lnk->lk_lock);

	if (corr) {
		/*
		 * if the lnk to be disc'ed is a refusal from an
		 * INCON_PEND
		 */
		rc = llc2DisconnectReq(mac, (ushort_t)corr);
	} else {
		rc = llc2DisconnectReq(mac, sid);
	}

	ILD_WRLOCK(lnk->lk_lock);
	/* sturblen muoe971910  08/05/97  */
	if ((rc != 0) && (lnk->lk_state == lk_state_save)) {
		lnk->lk_state = lk_state_save_incoming;
	}

	return (rc);
}


/*
 * Function Label:	dlpi_connect_res
 * Description:	comment
 * Locks:	lnk is WR locked on entry/exit.
 *		resp_token is locked/unlocked.
 */
int
#ifdef _LP64
dlpi_connect_res(link_t *lnk, uint_t corr, link_t *resp_token)
#else /* _LP64 */
dlpi_connect_res(link_t *lnk, uint_t corr, link_t *resp_token, queue_t *q)
#endif /* _LP64 */
{
	outInd_t *oiPtr;
	mac_t  *mac;
	dlsap_t *loc, *rem;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 1);

#if !defined(_LP64)
	if (resp_token) {
		dLnkLst_t *headlnk;
		link_t *link;
		int mac_ppa, found_link;

		/*
		 * Check if token is valid. For 64bit kernel,
		 * the token is link minor number and we
		 * wouldn't have gotten here if the token was
		 * bad.
		 */

		for (mac_ppa = 0; mac_ppa <= ild_max_ppa; mac_ppa++) {
			found_link = 0;

			/*
			 * Need to drop link-lock momentarily to reacquire
			 * locks in the right order.
			 */
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_WRLOCK(ild_lnk_lock);
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			/* Make sure the link is still there */
			if (q->q_ptr != NULL) {
				ILD_WRLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_lnk_lock);
			} else {
				/* Link is gone. Can't even send error ack */
				ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
				ILD_RWUNLOCK(ild_lnk_lock);
				return (0);
			}

			headlnk = &mac_lnk[mac_ppa];
			if (headlnk != NULL) {
				link = (link_t *)headlnk->flink;
				while (link != (link_t *)headlnk) {
					if (link == resp_token) {
						found_link = 1;
						break;
					}
					link = (link_t *)link->chain.flink;
				}
			}

			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			if (found_link)
				break;
		}

		if (!found_link) {
			dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
					DL_BADTOKEN, 0);
			return (0);
		}
	}
#endif  /* !defined(_LP64) */

	/*
	 * If token is present, it better be for a different stream.
	 */
	if (resp_token && resp_token == lnk) {
		dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES, DL_BADTOKEN, 0);
		return (0);
	}

	if (resp_token) {
		ILD_WRLOCK(resp_token->lk_lock);
		if ((resp_token->lk_mac == lnk->lk_mac) &&
			(resp_token->lk_enet == FALSE) &&
			(resp_token->lk_bind.bd_sap.llc.dl_sap ==
			lnk->lk_bind.bd_sap.llc.dl_sap) &&
			(resp_token->lk_bind.bd_service_mode & DL_CODLS) &&
			(resp_token->lk_bind.bd_max_conind == 0) &&
			(resp_token->lk_mac->mac_conn_mgmt != resp_token) &&
			(resp_token->lk_state == DL_IDLE)) {
			/*
			 * Assume the call to LLC will be successful and set
			 * up the fields accordingly beforehand in case LLC
			 * immediately calls dlpi_connect_con(), which in turn
			 * may cause a kernel resident DLPI user to immediately
			 * issue a command which requires that these fields be
			 * set up.  This may all happen BEFORE llc2ConnectRes()
			 * completes.
			 *
			 * If the llc2ConnectRes() operation fails, the fields
			 * are restored from saved values.
			 */
			if ((oiPtr = peekOutInd(lnk, corr)) != NULL) {
				bcopy((caddr_t)&oiPtr->remAddr,
					(caddr_t)&resp_token->lk_dsap,
					sizeof (dlsap_t));
			} else {
				ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, corr, 0);
				ILD_RWUNLOCK(resp_token->lk_lock);
				dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
						DL_BADCORR, 0);
				return (0);
			}

			resp_token->lk_state = DL_CONN_RES_PENDING;
			resp_token->lk_sid = (ushort_t)corr;
			mac = lnk->lk_mac;

			loc = &lnk->lk_bind.bd_sap;
			rem = &resp_token->lk_dsap;

			/*
			 * Ugly stuff here. If we've got an S/W llc2 to deal
			 * with all this stuff must be unlocked here, because
			 * the dlpi_connect_con will be called from this same
			 * thread. With a TR board. the UA resp signals the
			 * end of the thread and the Transmit interrupt will
			 * begin a new thread for the confirm processing.
			 */
			ILD_RWUNLOCK(resp_token->lk_lock);
			ILD_RWUNLOCK(lnk->lk_lock);

			if ((llc2ConnectRes(mac, resp_token, loc, rem, corr)
				== 0)) {
				ILD_WRLOCK(lnk->lk_lock);
				return (0);
			} else {
				ILD_WRLOCK(lnk->lk_lock);
				ILD_WRLOCK(resp_token->lk_lock);

				ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
				resp_token->lk_state = DL_IDLE;
				resp_token->lk_sid = 0;
				ILD_RWUNLOCK(resp_token->lk_lock);
				return (1);
			}
		} else {
			ILD_RWUNLOCK(resp_token->lk_lock);
			ILD_RWUNLOCK(lnk->lk_lock);
			ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
			dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
					DL_OUTSTATE, 0);
			ILD_WRLOCK(lnk->lk_lock);
			return (0);
		}

	} else {

		/*
		 * Accept the connection back on the same Stream.
		 */
		if ((lnk->lk_enet == FALSE) &&
			(lnk->lk_bind.bd_service_mode & DL_CODLS) &&
			(lnk->lk_mac->mac_conn_mgmt != lnk)) {

			/*
			 * Make sure that there is only one outstanding
			 * DL_CONNECT_IND.
			 */
			if (lnk->lk_bind.bd_max_conind -
			    lnk->lk_bind.bd_conind > 1) {
				ILD_RWUNLOCK(lnk->lk_lock);
				ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
				dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
				    DL_PENDING, 0);
				ILD_WRLOCK(lnk->lk_lock);
				return (0);
			}

			/*
			 * Assume the call to LLC will be successful and set
			 * up the fields accordingly beforehand in case LLC
			 * immediately calls dlpi_connect_con(), which in turn
			 * may cause a kernel resident DLPI user to immediately
			 * issue a command which requires that these fields be
			 * set up.  This may all happen BEFORE llc2ConnectRes()
			 * completes.
			 *
			 * If the llc2ConnectRes() operation fails, the fields
			 * are restored from saved values.
			 */
			if ((oiPtr = peekOutInd(lnk, corr)) != NULL) {
				bcopy((caddr_t)&oiPtr->remAddr,
					(caddr_t)&lnk->lk_dsap,
					sizeof (dlsap_t));
			} else {
				ild_trace(MID_DLPI, __LINE__,
						(uintptr_t)lnk, corr, 0);
				dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
						DL_BADCORR, 0);
				return (0);
			}

			lnk->lk_state = DL_CONN_RES_PENDING;
			lnk->lk_sid = (ushort_t)corr;
			mac = lnk->lk_mac;

			loc = &lnk->lk_bind.bd_sap;
			rem = &lnk->lk_dsap;

			/*
			 * Ugly stuff here. If we've got an S/W llc2 to deal
			 * with all this stuff must be unlocked here, because
			 * the dlpi_connect_con will be called from this same
			 * thread. With a TR board. the UA resp signals the
			 * end of the thread and the Transmit interrupt will
			 * begin a new thread for the confirm processing.
			 */
			ILD_RWUNLOCK(lnk->lk_lock);
			if ((llc2ConnectRes(mac, lnk, loc, rem, corr) == 0)) {
				ILD_WRLOCK(lnk->lk_lock);
				return (0);
			} else {
				ILD_WRLOCK(lnk->lk_lock);
				ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
				lnk->lk_state = DL_IDLE;
				lnk->lk_sid = 0;
				return (1);
			}
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			ild_trace(MID_DLPI, __LINE__, 0, 0, 1);
			dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
					DL_OUTSTATE, 0);
			ILD_WRLOCK(lnk->lk_lock);
			return (0);
		}
	}
}


/*
 *	dlpi_reset_ind
 *
 *
 *	dlpi_reset_indication
 *
 *	description:
 *	Informs the DLS user that either the remote DLS user is resynchronizing
 *	the data link connection, or the DLS provider is reporting loss of data
 *	from which it cannot recover.  The indication conveys the reason for
 *	the reset.
 *
 *	execution state:
 *	Service level
 *
 *	parameters:
 *	mac			Pointer to mac struct of connection.
 *	sid			Identifier of connection.
 *	orig			Originator of reset (DL_USER or DL_PROVIDER).
 *	reason			Reason for the reset =	DL_RESET_FLOW_CONTROL
 *							DL_RESET_LINK_ERROR
 *							DL_RESET_RESYNCH.
 *
 *	returns:		nothing
 *	Locks:			ild_glnk_lck[mac_ppa] locked/unlocked on exit
 *				lnk->lk_lock locked/unlocked on exit
 */

void
dlpi_reset_ind(mac_t *mac, ushort_t sid, uint_t orig, uint_t reason)
{
	dl_reset_ind_t *dl;
	dLnkLst_t *headlnk;
	link_t *lnk;
	mblk_t *mp;
	uint_t mac_ppa;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_sid == sid) &&
			(lnk->lk_state == DL_DATAXFER)) {
			ILD_SWAPIPL(mac_ppa, lnk);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			lnk->lk_state = DL_PROV_RESET_PENDING;
			if (lnk->lk_wrq != NULL)
				flushq(RD(lnk->lk_wrq), FLUSHALL);
			if ((mp = llc2_allocb(DL_RESET_IND_SIZE, BPRI_MED))
				!= NULL) {
				dl = (dl_reset_ind_t *)mp->b_rptr;
				mp->b_datap->db_type = M_PROTO;
				mp->b_wptr += DL_RESET_IND_SIZE;
				dl->dl_primitive = DL_RESET_IND;
				dl->dl_originator = orig;
				dl->dl_reason = reason;

				/*
				 * save an indication of the originator of the
				 * reset so that DLPI can appropriately send
				 * either a reset response or reset request
				 * command to LLC on receipt of the DL_RESET_RES
				 * primitive from the DLS user
				 */
				lnk->lk_local_reset = ((orig == DL_PROVIDER)
							? TRUE : FALSE);
			} else {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			cmn_err(CE_WARN,
				"!LLC2: could not allocb DL_RESET_IND\n");
			}
			if (lnk->lk_wrq && (putnextctl1(RD(lnk->lk_wrq),
							M_FLUSH, FLUSHRW)
							!= 1)) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			cmn_err(CE_WARN,
			"!LLC2: could not putctl1 FLUSHRW in dlpi_reset_ind\n");
			}
			if (mp && lnk->lk_wrq) {
				qreply(lnk->lk_wrq, mp);
			} else
				freemsg(mp);
			/* muoe970862  (w/muoe970020) buzz 06/02/97  */
			/*
			 * sturblen muoe971965  08/08/97
			 * removed orig == DL_PROVIDER check
			 * enable queue regardless of who initiated the reset
			 */
			if (lnk->lk_wrq != NULL)
				qenable(lnk->lk_wrq);
			ild_trace(MID_DLPI, __LINE__, (uintptr_t)(lnk->lk_wrq),
					0, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
			return;
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

}


/*
 * dlpi_reset_con
 *
 *
 * dlpi_reset_confirmation
 *
 * description:
 * Informs the reset-initiating DLS user that the reset has completed.
 *
 * If the DLS user issued a DL_RESET_RES primitive in response to a local
 * reset indication from LLC, a DL_OK_ACK has already been returned by DLPI.
 * In this case the DL_RESET_RES_PENDING state will have been left before
 * LLC calls this routine to confirm the reset operation.  Therefore, no
 * action is taken in this case.
 *
 * execution state:
 * Service level
 *
 * parameters:
 * mac		Pointer to mac struct of connection.
 * sid		Identifier of connection.
 * rc		Return code of unix_errno, 0 = success
 *		of reset processing, nonzero = failure.
 * Locks:	ild_glnk_lck[mac_ppa]	WR locked on entry/unlocked on exit
 *		lnk->lk_lock	WR locked on entry/unlocked on exit
 *
 * returns:	nothing
 */

void
dlpi_reset_con(mac_t *mac, ushort_t sid, ushort_t rc)
{
	dl_reset_con_t *dl;
	dLnkLst_t *headlnk;
	link_t *lnk;
	mblk_t *mp;
	uint_t mac_ppa;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac_ppa = mac->mac_ppa;
	ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
	headlnk = &mac_lnk[mac_ppa];
	lnk = (link_t *)headlnk->flink;

	while (lnk != (link_t *)headlnk) {

		ILD_WRLOCK(lnk->lk_lock);

		if ((lnk->lk_sid == sid) &&
			((lnk->lk_state == DL_USER_RESET_PENDING) ||
			(lnk->lk_state == DL_RESET_RES_PENDING))) {
			ILD_SWAPIPL(mac_ppa, lnk);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			if (lnk->lk_state == DL_USER_RESET_PENDING) {
				if (rc == 0) {
					lnk->lk_state = DL_DATAXFER;
					if (lnk->lk_wrq != NULL)
						flushq(RD(lnk->lk_wrq),
							FLUSHALL);
					if ((mp = llc2_allocb(DL_RESET_CON_SIZE,
							BPRI_MED)) != NULL) {
						dl = (dl_reset_con_t *)
							mp->b_rptr;
						mp->b_datap->db_type = M_PROTO;
						mp->b_wptr += DL_RESET_CON_SIZE;
						dl->dl_primitive = DL_RESET_CON;

					} else {
					ild_trace(MID_DLPI, __LINE__,
							0, 0, 0);
					cmn_err(CE_WARN,
					"!LLC2: allocb DL_RESET_CON fail\n");
					}

					if (lnk->lk_wrq &&
						(putnextctl1(RD(lnk->lk_wrq),
						M_FLUSH, FLUSHRW) != 1)) {
					ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
					cmn_err(CE_WARN,
					"!LLC2: FLUSHRW fail in reset_con\n");
					}
					if (lnk->lk_wrq && mp)
						qreply(lnk->lk_wrq, mp);
					else
						freemsg(mp);
					ILD_RWUNLOCK(lnk->lk_lock);
				} else {
					ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
					lnk->lk_state = DL_DATAXFER;

					dlpi_error_ack(lnk->lk_wrq,
							DL_RESET_REQ,
							DL_SYSERR, rc);
					ILD_RWUNLOCK(lnk->lk_lock);

				}
			}

			/*
			 * DL_RESET_RES_PENDING state
			 */
			else {
				if (rc == 0) {
					ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
					lnk->lk_state = DL_DATAXFER;

					dlpi_ok_ack(lnk->lk_wrq, DL_RESET_RES);
					ILD_RWUNLOCK(lnk->lk_lock);
				} else {
					ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
					lnk->lk_state = DL_PROV_RESET_PENDING;

					dlpi_error_ack(lnk->lk_wrq,
							DL_RESET_RES,
							DL_SYSERR, rc);
					ILD_RWUNLOCK(lnk->lk_lock);
				}
			}
			return;
		}

		ILD_RWUNLOCK(lnk->lk_lock);

		lnk = (link_t *)lnk->chain.flink;
	}

	ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

}

/*
 * DLPI Version 2 Extension functions for Local/Essential Management
 */

/*
 *  Check received address against configured multicast addresses
 *  If match found, send the packet up, otherwise drop it.
 *  qualify the return type as follows:
 *	0 = Address is not broadcast/multicast/group nor does it match the node
 *	1 = Address is a Broadcast/Multicast/Group Address
 *	2 = Address matches this nodes unique address (unicast)
 */
int
dlpi_check_multicast(link_t *lnk, uchar_t *mac)
{
	int 	i;
	mac_t	*macp;

	macp = lnk->lk_mac;
	/*
	 * otherwise check against the ones in use
	 */
	/*
	 * first, check my address
	 * before looking at the broadcast/multicast addresses
	 */
	if (CMP_MAC(macp->mac_bia, mac))
		return (2);

	/*
	 * At this point we know it's not the Unique Node Address. Therefore it
	 * has to be either a broadcast/multicast/group address or we'd have
	 * never received it in the first place.  Of course, there is an
	 * exception to this:
	 * If the interface is in Promiscuous mode, frames for non-broadcast
	 * addresses can be received. However, they _WILL NOT_ present
	 * themselves here--at least not until the rest of DLPI V2 is
	 * implemented allowing promiscuous mode to be set on a per STREAM
	 * basis.
	 */
	/*
	 * if lk_multiuse is 0, no per STREAM screening is being done,
	 * return 1
	 */
	if (lnk->lk_multiuse == 0)
		return (1);

	/* check for the all 1's broadcast address  */
	if (CMP_MAC(ildbrdcst, mac))
		return (1);

	/* check Token Ring Specific Addresses for bcast OR group bit */
	if (macp->mac_type == DL_TPR) {
		if (mac[0] & 0x80) { /* Group/Specific bit */
			if ((mac[0] & 0xc0) && !(bcmp((void *)&mac[2],
				(void *)ildbrdcst, 4))) {
				/* this picks up TR BCAST like: c000ffffffff */
				return (1);
			}
			if (mac[2] & 0x80) {
				/* Token Ring Group Address */
				/* this picks up TR Group like: c000b2000000 */
				return (1);
			}
		}
		/*
		 * Since multiuse is non-zero at this point,
		 * we don't screen here for Token Ring Functional Addresses
		 * but let that occur in the loop below
		 */
	}

	/*
	 * If lnk->lk_multiuse is non-zero, then one or more DL_ENABLMULTI_REQ
	 * commands have been received to set up multicast screening
	 * on a per STREAM basis. Otherwise, the addresses have been setup
	 * directly between the application and adapter and we'll have to
	 * use the algorithm to figure out what to accept or reject
	 */
	for (i = 0; i < MAXMULTICAST; i++) {
		if (CMP_MAC(lnk->lk_multicast[i].mac_addr, nullmac))
			continue;
		if (macp->mac_type != DL_TPR) {
			if (CMP_MAC(lnk->lk_multicast[i].mac_addr, mac)) {
				return (1);
			}
		} else {
		/*
		 * if this is a Token Ring link only look at the last
		 * 4 bytes of the functional address for comparison
		 */
		if (!(bcmp((void *)(&lnk->lk_multicast[i].mac_addr[2]),
				(void *)(&mac[2]), 4))) {
			return (1);
		}
		}
	}
	return (0);
}

/*
 *  Insert address in the multicast array of the link structure
 *  If no space, return DL_TOOMANY
 */
int
dlpi_insert_multicast(link_t *lnk, mac_addr_t *mac)
{
	int i;

	if (lnk->lk_multiuse < MAXMULTICAST) {
		for (i = 0; i < MAXMULTICAST; i++) {
			if (CMP_MAC(lnk->lk_multicast[i].mac_addr,
					mac->mac_addr))
				/* already have it in the array, so return */
				return (0);
			if (!CMP_MAC(lnk->lk_multicast[i].mac_addr, nullmac))
				continue;
			CPY_MAC(mac, lnk->lk_multicast[i].mac_addr);
			lnk->lk_multiuse++;
			return (0);
		}
	}
	return (DL_TOOMANY);
}

/*
 *  Remove address in the multicast array of the link structure
 *  If not found, return DL_NOTENAB
 */
int
dlpi_remove_multicast(link_t *lnk, mac_addr_t *mac)
{
	int i;

	for (i = 0; i < MAXMULTICAST; i++) {
		if (CMP_MAC(lnk->lk_multicast[i].mac_addr, mac->mac_addr)) {
			CPY_MAC(nullmac, lnk->lk_multicast[i].mac_addr);
			lnk->lk_multiuse--;
			return (0);
		}
	}
	return (DL_NOTENAB);
}

void
dlpi_ack(queue_t *lq, mblk_t *bp)
{
	union DL_primitives *d = (union DL_primitives *)bp->b_rptr;
	mac_t		*mac;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac = (mac_t *)lq->q_ptr;
	switch (d->dl_primitive) {

	case DL_OK_ACK: 	{
		switch (d->ok_ack.dl_correct_primitive) {

		case DL_ENABMULTI_REQ:
		case DL_DISABMULTI_REQ:
			dlpi_multicast_ack(lq, bp);
			break;
		case DL_GET_STATISTICS_REQ:
			dlpi_get_statistics_ack(lq, bp, 0);
			break;
		default:
			freemsg(bp);
			ild_trace(THIS_MOD, __LINE__, mac->mac_ppa,
					d->ok_ack.dl_correct_primitive, 0);
			break;
		}
	}
	break;
	case DL_ERROR_ACK: 	{
		switch (d->error_ack.dl_error_primitive) {

		case DL_ENABMULTI_REQ:
		case DL_DISABMULTI_REQ:
			dlpi_multicast_ack(lq, bp);
			break;
		case DL_GET_STATISTICS_REQ:
			dlpi_get_statistics_ack(lq, bp, 0);
			break;
		default:
			freemsg(bp);
			ild_trace(THIS_MOD, __LINE__,  mac->mac_ppa,
					d->error_ack.dl_error_primitive, 0);
			break;
		}
	}
	break;
	default:
		freemsg(bp);
		ild_trace(THIS_MOD,  __LINE__,  mac->mac_ppa,
				d->dl_primitive, 0);
		break;
	}
}

void
dlpi_multicast_ack(queue_t *lq, mblk_t *bp)
{
	union DL_primitives *d = (union DL_primitives *)bp->b_rptr;
	link_t		*lnk;
	mac_t		*mac;
	macx_t		*macx;
	queue_t		*uq;
	mblk_t		*ubp;
	struct iocblk	*ctl;

	ild_trace(THIS_MOD, __LINE__, 0, 0, 0);

	mac = (mac_t *)lq->q_ptr;
	ILD_WRLOCK(mac->mac_lock);
	macx = ild_macxp[mac->mac_ppa];
	if ((uq = macx->multicast_queue) == NULL) {
		freemsg(bp);
		if ((ubp = macx->multicast_mblk) != NULL) {
			freemsg(ubp);
			macx->multicast_mblk = NULL;
		}
		ILD_RWUNLOCK(mac->mac_lock);
		ild_trace(THIS_MOD, __LINE__, mac->mac_ppa, 0, 0);
		return;
	}
	ubp = macx->multicast_mblk;

	ILD_WRLOCK(ild_glnk_lck[mac->mac_ppa]);
	lnk = (link_t *)uq->q_ptr;
	if (lnk != NULL) {
		ILD_WRLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac->mac_ppa]);
	} else {
		ILD_RWUNLOCK(ild_glnk_lck[mac->mac_ppa]);
		freemsg(bp);
		return;
	}

	if (d->dl_primitive == DL_ERROR_ACK) {
		if (ubp) {
			ubp->b_datap->db_type = M_IOCNAK;
			ctl = (struct iocblk *)ubp->b_rptr;
			ctl->ioc_rval = d->error_ack.dl_errno;
			ctl->ioc_error = d->error_ack.dl_unix_errno;
			ild_trace(THIS_MOD, __LINE__, ctl->ioc_rval,
					ctl->ioc_error, 0);
		} else
			/*
			 * If unsuccessful, remove entry from link_t
			 * structure
			 */
			(void) dlpi_remove_multicast(lnk,
					&macx->multicast_pending);
	}
	CPY_MAC(nullmac, macx->multicast_pending.mac_addr);
	macx->multicast_mblk = NULL;
	macx->multicast_queue = NULL;
	if (ubp == NULL)
		ubp = bp;
	ILD_RWUNLOCK(mac->mac_lock);

	if (uq) {
		qreply(uq, ubp);
	} else {
		freemsg(bp);
		ild_trace(THIS_MOD, __LINE__, ctl->ioc_rval, ctl->ioc_error, 0);
	}
	ILD_RWUNLOCK(lnk->lk_lock);

}

void
dlpi_enabmulti_req(queue_t *q, mblk_t *bp)
{
	dl_enabmulti_req_t 	*dl_multi = (dl_enabmulti_req_t *)(bp->b_rptr);
	mac_addr_t		*macaddr;
	link_t			*lnk = (link_t *)q->q_ptr;
	int			 ret = 0;
	mac_t			*mac;
	macx_t			*macx;
	queue_t			*lq;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	macaddr = (mac_addr_t *)((uintptr_t)bp->b_rptr +
					dl_multi->dl_addr_offset);

	if (dl_multi->dl_addr_length != sizeof (mac_addr_t)) {
		ret = DL_BADADDR;
	} else {
		mac = lnk->lk_mac;
		ILD_WRLOCK(global_mac_lock);
		if (mac) {
			macx = ild_macxp[mac->mac_ppa];
			if ((ret = dlpi_insert_multicast(lnk, macaddr)) == 0) {
				ILD_WRLOCK(mac->mac_lock);
				ILD_RWUNLOCK(global_mac_lock);
				ILD_RWUNLOCK(lnk->lk_lock);
				/* Save queue for ACK */
				macx->multicast_queue = q;
				lq = macx->lowerQ;
				ILD_RWUNLOCK(mac->mac_lock);
				putnext(lq, bp);
				return;
			}
		} else {
			ret = DL_OUTSTATE;
		}
		ILD_RWUNLOCK(global_mac_lock);
	}
	freemsg(bp);
	if (ret)
		dlpi_error_ack(q, DL_ENABMULTI_REQ, ret, 0);
	else
		dlpi_ok_ack(q, DL_ENABMULTI_REQ);
	ILD_RWUNLOCK(lnk->lk_lock);
}


void
dlpi_disabmulti_req(queue_t *q, mblk_t *bp)
{
	dl_disabmulti_req_t	*dl_multi = (dl_disabmulti_req_t *)(bp->b_rptr);
	mac_addr_t		*macaddr;
	link_t			*lnk = (link_t *)q->q_ptr;
	int			 ret = 0;
	mac_t			*mac;
	macx_t			*macx;
	queue_t			*lq;


	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	macaddr = (mac_addr_t *)((uintptr_t)bp->b_rptr +
					dl_multi->dl_addr_offset);

	if (dl_multi->dl_addr_length != sizeof (mac_addr_t)) {
		ret = DL_BADADDR;
	} else {
		mac = lnk->lk_mac;
		ILD_WRLOCK(global_mac_lock);
		if (mac) {
			macx = ild_macxp[mac->mac_ppa];
			if ((ret = dlpi_remove_multicast(lnk, macaddr)) == 0) {
				ILD_WRLOCK(mac->mac_lock);
				ILD_RWUNLOCK(global_mac_lock);
				ILD_RWUNLOCK(lnk->lk_lock);
				lq = macx->lowerQ;
				/* Save queue for ACK */
				macx->multicast_queue = q;
				ILD_RWUNLOCK(mac->mac_lock);
				putnext(lq, bp);
				return;
			}
		} else {
			ret = DL_OUTSTATE;
		}
		ILD_RWUNLOCK(global_mac_lock);
	}
	freemsg(bp);
	if (ret)
		dlpi_error_ack(q, DL_DISABMULTI_REQ, ret, 0);
	else
		dlpi_ok_ack(q, DL_DISABMULTI_REQ);
	ILD_RWUNLOCK(lnk->lk_lock);
}
/*
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
dlpi_set_phys_addr_req(queue_t *q, mblk_t *bp)
{
	dl_set_phys_addr_req_t *dl_addr;
	link_t		*lnk = (link_t *)q->q_ptr;
	link_t		*lnkp;
	dLnkLst_t 	*headlnk;
	int		ret = 0;
	mac_t		*mac;
	macx_t		*macx;
	queue_t		*lq;
	uint_t		mac_ppa;

	ild_trace(THIS_MOD, __LINE__, 0, 0, 0);

	dl_addr = (dl_set_phys_addr_req_t *)(bp->b_rptr);

	if (dl_addr->dl_addr_length != sizeof (mac_addr_t)) {
		ret = DL_BADADDR;
		ILD_RWUNLOCK(lnk->lk_lock);
	} else {
		mac = lnk->lk_mac;
		/* locks will be acquired in right order */
		ILD_RWUNLOCK(lnk->lk_lock);
		ILD_WRLOCK(global_mac_lock);
		if (mac) {
			ILD_WRLOCK(mac->mac_lock);
			ILD_RWUNLOCK(global_mac_lock);
			mac_ppa = mac->mac_ppa;
			macx = ild_macxp[mac_ppa];
			headlnk = &mac_lnk[mac_ppa];
			lnkp = (link_t *)headlnk->flink;
			/* make sure NO users are BOUND */
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			ILD_WRLOCK(lnk->lk_lock);
			while (lnkp != (link_t *)headlnk) {
				if (lnkp->lk_state > DL_UNBOUND) {
					ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
					freemsg(bp);
					dlpi_error_ack(q, DL_SET_PHYS_ADDR_REQ,
							DL_BUSY, 0);
					ILD_RWUNLOCK(lnk->lk_lock);
					return;
				}
				lnkp = (link_t *)lnkp->chain.flink;
			}
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			/* Save queue for ACK */
			macx->statistics_queue = q;
			lq = macx->lowerQ;
			ILD_RWUNLOCK(mac->mac_lock);
			putnext(lq, bp);
			return;
		} else {
			ILD_RWUNLOCK(global_mac_lock);
			ret = DL_OUTSTATE;
		}
	}
	freemsg(bp);
	ILD_WRLOCK(lnk->lk_lock);
	if (ret)
		dlpi_error_ack(q, DL_SET_PHYS_ADDR_REQ, ret, 0);
	else
		dlpi_ok_ack(q, DL_SET_PHYS_ADDR_REQ);

	ILD_RWUNLOCK(lnk->lk_lock);
}

void
dlpi_phys_addr_req(queue_t *q, mblk_t *bp)
{
	dl_phys_addr_req_t 	*dl_addr = (dl_phys_addr_req_t *)(bp->b_rptr);
	dl_phys_addr_ack_t	*dl;
	mblk_t		*mp;
	mac_addr_t		*macaddr;
	link_t		*lnk = (link_t *)q->q_ptr;
	mac_t		*mac;

	mac = lnk->lk_mac;
	ILD_RWUNLOCK(lnk->lk_lock);
	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	if ((mp = llc2_allocb(DL_PHYS_ADDR_ACK_SIZE + sizeof (mac_addr_t),
				BPRI_HI)) == NULL) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(bp);
		return;
	}
	mp->b_datap->db_type = M_PCPROTO;
	mp->b_wptr += (DL_PHYS_ADDR_ACK_SIZE + sizeof (mac_addr_t));

	dl = (dl_phys_addr_ack_t *)mp->b_rptr;
	macaddr = (mac_addr_t *)((uintptr_t)mp->b_rptr + DL_PHYS_ADDR_ACK_SIZE);

	dl->dl_addr_offset = DL_PHYS_ADDR_ACK_SIZE;
	dl->dl_addr_length = sizeof (mac_addr_t);
	dl->dl_primitive = DL_PHYS_ADDR_ACK;

	ILD_WRLOCK(global_mac_lock);
	if (mac) {
		ILD_WRLOCK(mac->mac_lock);
		ILD_RWUNLOCK(global_mac_lock);
		if (dl_addr->dl_addr_type == DL_CURR_PHYS_ADDR) {
			CPY_MAC(mac->mac_bia, macaddr);
			ILD_RWUNLOCK(mac->mac_lock);
			if (q)
				qreply(q, mp);
			else
				freemsg(mp);
		} else {
			ILD_RWUNLOCK(mac->mac_lock);
			dlpi_error_ack(q, DL_PHYS_ADDR_REQ,
						DL_NOTSUPPORTED, 0);
			freemsg(mp);
		}
	} else {
		ILD_RWUNLOCK(global_mac_lock);
		dlpi_error_ack(q, DL_PHYS_ADDR_REQ, DL_NOTSUPPORTED, 0);
		freemsg(mp);
	}
	freemsg(bp);
}

void
dlpi_get_statistics_req(queue_t *q, mblk_t *bp)
{
	link_t	*lnk = (link_t *)q->q_ptr;
	mac_t	*mac;
	macx_t	*macx;
	int 	ret = 1;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	mac = lnk->lk_mac;
	ILD_RWUNLOCK(lnk->lk_lock);
	if (mac) {
		macx = ild_macxp[mac->mac_ppa];
		putnext(macx->lowerQ, bp);
		return;
	}
	freemsg(bp);
	if (ret)
		dlpi_error_ack(q, DL_GET_STATISTICS_REQ, DL_NOTSUPPORTED, 0);
}

void
dlpi_get_statistics_ack(queue_t *q, void *stat, int statlen)
{
	dl_get_statistics_ack_t	*dl;
	mblk_t		*mp;
	link_t		*lnk = (link_t *)q->q_ptr;
	uchar_t		*usrstat;
	queue_t		*uq;

	if (statlen == 0) {
		mac_t *mac = (mac_t *)q->q_ptr;
		macx_t *macx;

		ILD_WRLOCK(mac->mac_lock);
		macx = ild_macxp[mac->mac_ppa];
		uq = macx->statistics_queue;
		macx->statistics_queue = NULL;
		if (uq)
			qreply(uq, (mblk_t *)stat);
		else
			freemsg((mblk_t *)stat);
		ILD_RWUNLOCK(mac->mac_lock);
	} else {
		if ((mp = llc2_allocb(DL_GET_STATISTICS_ACK_SIZE + statlen,
					BPRI_HI)) == NULL) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			return;
		}
		if (lnk) {
			ILD_WRLOCK(lnk->lk_lock);
			mp->b_datap->db_type = M_PCPROTO;
			mp->b_wptr += (DL_GET_STATISTICS_ACK_SIZE + statlen);

			dl = (dl_get_statistics_ack_t *)mp->b_rptr;
			usrstat = (uchar_t *)((uintptr_t)mp->b_rptr +
						DL_GET_STATISTICS_ACK_SIZE);

			dl->dl_stat_offset = DL_GET_STATISTICS_ACK_SIZE;
			dl->dl_stat_length = statlen;
			dl->dl_primitive = DL_GET_STATISTICS_ACK;
			bcopy((caddr_t)stat, (caddr_t)usrstat, statlen);
			qreply(q, mp);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			freemsg(mp);
		}
	}
}


/*
 * Function Label:	fDL_UNATTACHED
 * Description:	servicing routine for DL_UNATTACHED state
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 *		mac is locked/unlocked.
 */
void
fDL_UNATTACHED(queue_t *q, mblk_t *mp)
{
	uchar_t	*dl = mp->b_rptr;
	uint_t	prim = *((uint_t *)mp->b_rptr);
	link_t	*lnk = (link_t *)q->q_ptr;
	int	reterr = DL_OUTSTATE;
	uint_t	ppa;
	mac_t	*mac;

	/*
	 * muoe941184 buzz 04/13/94
	 * alter the locking scheme to specifically leave the global link lock
	 * asserted during the DETACH processing to protect the mac_lnk
	 * linked list operations:  ppa list <==> master list <==> freelist
	 */

	switch (prim) {
	case DL_INFO_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		freemsg(mp);
		break;

	case DL_TOKEN_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_token_req(lnk);
		break;

	case DL_ATTACH_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

		/*
		 * if trying to attach to non-existing adapter, send error_ack
		 * First "if" checks for out of range ppa, 2nd one checks for
		 * a "missing" adapter in a sequence 1, 2, 4, 5,... ie "the
		 * adapter formally know as 3" is no longer in the box, but
		 * we've preserved the numbering for the existing adapters at
		 * the expense of leaving a gap.
		 */
		ppa = ((dl_attach_req_t *)dl)->dl_ppa;
		/* 05/28/98 SOLVP98xxxx buzz */
		if ((ppa < ild_max_ppa) && ((mac = ild_macp[ppa]) != NULL)) {
			/* Store MAC address pointer in link structure */
			lnk->lk_mac = mac;

			lnk->lk_addr_length = (mac->mac_type == DL_CSMACD) ?
				ETHER_ADDR_SIZE : TOKEN_ADDR_SIZE;

			/*
			 * acknowledge the attach
			 */
			ILD_RWUNLOCK(lnk->lk_lock);

			/* SOLVP980253 buzz 10/05/98 */
			ILD_WRLOCK(ild_lnk_lock);

			ILD_WRLOCK(ild_glnk_lck[ppa]);
			ILD_WRLOCK(lnk->lk_lock);

			lnk->lk_state = DL_UNBOUND;
			RMV_DLNKLST(&lnk->chain);
			ADD_DLNKLST(&lnk->chain, &mac_lnk[ppa]);

			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[ppa]);

			/* SOLVP980253 buzz 10/05/98 */
			ILD_RWUNLOCK(ild_lnk_lock);

			freemsg(mp);
			dlpi_ok_ack(q, prim);
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BADPPA, 0);
			break;
		}
		break;

	case DL_UNITDATA_REQ:

		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_uderror_ind(q, mp, DL_OUTSTATE);
		freemsg(mp);
		break;

	case DL_PROMISCON_REQ:
	case DL_PROMISCOFF_REQ:
		reterr = DL_NOTSUPPORTED;

	default:
		ILD_RWUNLOCK(lnk->lk_lock);

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_error_ack(q, prim, reterr, 0);
		break;
	}

}


/*
 * Function Label:	fDL_ATTACH_PENDING
 * Description:	servicing routine for DL_ATTACH_PENDING state
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_ATTACH_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);


	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(((link_t *)(q->q_ptr))->lk_lock);
	freemsg(mp);
}


/*
 * Function Label:	fDL_DETACH_PENDING
 * Description:	servicing routine for DL_DETACH_PENDING state
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 *		ild_lnk_lock is WR locked on entry, unlocked on exit.
 */
void
fDL_DETACH_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(((link_t *)(q->q_ptr))->lk_lock);
	freemsg(mp);
}


/*
 * Function Label:	fDL_UNBOUND
 * Description:	servicing routine for DL_UNBOUND state
 *		lnk_lock locked on entry
 *		ild_glnk_lck[ppa] is locked/unlocked
 *		mac_lock is locked/unlocked
 */
void
fDL_UNBOUND(queue_t *q, mblk_t *mp)
{
	union DL_primitives *d = (union DL_primitives *)mp->b_rptr;
	link_t 		*lnk = (link_t *)q->q_ptr;
	int 		reterr = DL_OUTSTATE;
	mac_t		*mac;
	uint_t 		prim, mac_ppa;

	prim = d->dl_primitive;

	/*
	 * muoe941184 buzz 04/25/94
	 * alter the locking scheme to specifically leave the global link lock
	 * asserted during the DETACH processing to protect the mac_lnk
	 * linked list operations:  ppa list <==> master list <==> freelist
	 */
	mac = lnk->lk_mac;
	ILD_WRLOCK(mac->mac_lock);
	ILD_RWUNLOCK(lnk->lk_lock);
	mac_ppa = mac->mac_ppa;

	/*
	 * Check for uninitialized MAC. Only allow a PHYSADDR primitive
	 * if the MAC is uninitialized.
	 */
	if ((mac->mac_state & MAC_INIT) == 0) {
		switch (prim) {
			/* Allow these three only if the MAC is not Init'd */
		case DL_PHYS_ADDR_REQ:
		case DL_SET_PHYS_ADDR_REQ:
		case DL_DETACH_REQ:
			break;
		default:
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			ILD_RWUNLOCK(mac->mac_lock);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_NOTINIT, 0);
			return;
		}
	}
	ILD_RWUNLOCK(mac->mac_lock);

	switch (prim) {
	case DL_BIND_REQ: 	{
		boolean_t eflag = B_FALSE;

		ild_trace(MID_DLPI, __LINE__, d->bind_req.dl_sap, 0, 0);
		ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
		ILD_WRLOCK(lnk->lk_lock);

		lnk->lk_bind.bd_max_conind = d->bind_req.dl_max_conind;

		/*
		 * set the conind counter to equal the maximum coninds allowed
		 * for this link
		 */

		lnk->lk_bind.bd_conind = lnk->lk_bind.bd_max_conind;

		/*
		 * verify the service mode
		 */


		if (d->bind_req.dl_service_mode != DL_CODLS &&
			d->bind_req.dl_service_mode != DL_CLDLS) {

			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_UNSUPPORTED, 0);
			break;
		}

		/*
		 * verify not LSAP 0x00
		 */
		if (d->bind_req.dl_sap == 0x00) {

			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_UNSUPPORTED, 0);
			break;
		}

		lnk->lk_bind.bd_service_mode = d->bind_req.dl_service_mode;

		/*
		 * verify only one connection management stream per PPA
		 */
		if ((d->bind_req.dl_conn_mgmt) && (mac->mac_conn_mgmt)) {

			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BOUND, 0);
			break;
		}

		if (d->bind_req.dl_conn_mgmt) {
			/*
			 * if this queue is to be the connection management
			 * stream for the PPA, set the conn_mgmt field in
			 * mac to point to this stream queue
			 */
			lnk->lk_bind.bd_conn_mgmt = TRUE;
			mac->mac_conn_mgmt = lnk;
		}


#define	SAME_SAP(l, d)							  \
((l)->lk_enet ?								  \
((l)->lk_bind.bd_sap.ether.dl_type == (ushort_t)((d)->bind_req.dl_sap)) : \
((l)->lk_bind.bd_sap.llc.dl_sap == (uchar_t)((d)->bind_req.dl_sap)))

		/*
		 * make sure that no more than one stream is bound to
		 * this ppa with the same SAP and max_conind > 0
		 */
		if ((lnk->lk_bind.bd_service_mode & DL_CODLS) &&
		    (d->bind_req.dl_max_conind > 0)) {

			dLnkLst_t *headlnk = &mac_lnk[mac_ppa];
			link_t *lnkp = (link_t *)headlnk->flink;

			while ((lnkp != (link_t *)headlnk) && !eflag) {

				if (lnk != lnkp) {
					ILD_RDLOCK(lnkp->lk_lock);

					if ((lnkp->lk_wrq) &&
					    (lnkp->lk_bind.bd_service_mode &
						DL_CODLS) &&
					    SAME_SAP(lnkp, d) &&
					    (lnkp->lk_bind.bd_max_conind >
						0)) {
						eflag = B_TRUE;
					}

					ILD_RWUNLOCK(lnkp->lk_lock);
				}

				lnkp = (link_t *)lnkp->chain.flink;
			}

		/*
		 * make sure that no more than one stream is bound to
		 * this ppa as a connectionless DLS user for the same
		 * SAP (unless SAP is SNAP SAP akb)
		 */
		} else if ((lnk->lk_bind.bd_service_mode & DL_CLDLS) &&
		    (d->bind_req.dl_sap != SNAPSAP)) {

			dLnkLst_t *headlnk = &mac_lnk[mac_ppa];
			link_t *lnkp = (link_t *)headlnk->flink;

			/*
			 * ild_glnk_lock[ppa], lnk->lk_lock
			 * are locked
			 */
			while ((lnkp != (link_t *)headlnk) && !eflag) {
				if (lnk != lnkp) {
					/* RD */
					ILD_RDLOCK(lnkp->lk_lock);

					if ((lnkp->lk_wrq) &&
					    (lnkp->lk_bind.bd_service_mode &
						DL_CLDLS) &&
					    SAME_SAP(lnkp, d)) {
						eflag = B_TRUE;
					}

					ILD_RWUNLOCK(lnkp->lk_lock);
				}

				lnkp = (link_t *)lnkp->chain.flink;
			}
		}

		if (eflag) {
			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			dlpi_error_ack(q, prim, DL_BOUND, 0);
			freemsg(mp);
			/* out of main switch statement */
			break;
		}

#undef SAME_SAP

		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

		/*
		 * copy the bind info to the lnk structure for future reference
		 */

		if (d->bind_req.dl_sap > MAXSAPVALUE) {
			lnk->lk_bind.bd_sap.ether.dl_type =
				(ushort_t)d->bind_req.dl_sap;
			lnk->lk_enet = TRUE;
			lnk->lk_addr_length = ETHER_ADDR_SIZE;
		} else {
			lnk->lk_bind.bd_sap.llc.dl_sap =
				(uchar_t)d->bind_req.dl_sap;
			lnk->lk_enet = FALSE;
			lnk->lk_addr_length = IEEE_ADDR_SIZE;
		}
		CPY_MAC(mac->mac_bia, lnk->lk_bind.bd_sap.ether.dl_nodeaddr);

		/*
		 * set the state to bind pending
		 */
		lnk->lk_state = DL_BIND_PENDING;

		/*
		 * bind the LLC sap
		 */
		if ((reterr = llc2BindReq(lnk, d->bind_req.dl_sap,
					d->bind_req.dl_service_mode)) == 0) {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		} else {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			lnk->lk_state = DL_UNBOUND;
			if (mac->mac_conn_mgmt == lnk)
				mac->mac_conn_mgmt = NULL;
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
			if (reterr == DL_BADADDR)
				dlpi_error_ack(q, DL_BIND_REQ, DL_BADADDR, 0);
			else
				dlpi_error_ack(q, DL_BIND_REQ, DL_SYSERR,
						ENOSR);
		}
		break;
	}

	case DL_DETACH_REQ: 	{
		mac_t *mac;
		uint_t mac_ppa;
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		mac = lnk->lk_mac;
		mac_ppa = mac->mac_ppa;

		/* SOLVP980253 buzz 10/05/98 */
		ILD_WRLOCK(ild_lnk_lock);

		ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
		ILD_WRLOCK(lnk->lk_lock);

		lnk->lk_state = DL_UNATTACHED;
		RMV_DLNKLST(&lnk->chain); /* Take it off where it is */
		lnk->lk_mac = NULL;
		lnk->lk_addr_length = sizeof (dlsap_t);
		/* and mv to Mstr.Lst */
		ADD_DLNKLST(&lnk->chain, &mac_lnk[ild_max_ppa]);

		ILD_RWUNLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);

		/* SOLVP980253 buzz 10/05/98 */
		ILD_RWUNLOCK(ild_lnk_lock);

		freemsg(mp);
		dlpi_ok_ack(q, prim);
		break;
	}
	case DL_DISABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_disabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_ENABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_enabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_SET_PHYS_ADDR_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_set_phys_addr_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_PHYS_ADDR_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_phys_addr_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_GET_STATISTICS_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_get_statistics_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_INFO_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;
	}
	case DL_TOKEN_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		ILD_WRLOCK(lnk->lk_lock);
		dlpi_token_req(lnk);		/* lnk is unlocked on return */
		break;

	}
	case DL_PROMISCON_REQ:
	case DL_PROMISCOFF_REQ:
		reterr = DL_NOTSUPPORTED;

	default:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_error_ack(q, prim, reterr, 0);
		break;
	}

}


/*
 * Function Label:	fDL_BIND_PENDING
 * Description:	Waiting ack of DL_BIND_REQ
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_BIND_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(((link_t *)(q->q_ptr))->lk_lock);
	freemsg(mp);
}

/*
 * Function Label:	fDL_UNBIND_PENDING
 * Description:	Waiting ack of DL_UNBIND_REQ
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_UNBIND_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(((link_t *)(q->q_ptr))->lk_lock);
	freemsg(mp);
}

/*
 * Function Label:	fDL_IDLE
 * Description:	dlsap bound, awaiting use
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_IDLE(queue_t *q, mblk_t *mp)
{
	snaphdr_t 	snaphdr;
	uchar_t 		*snap;
	dlsap_t 	*dsap, *loc, *rem;
	int 		msize;
	int 		enet;
	mblk_t 		*db;
	mac_t 		*mac;
	link_t 		*lnk1, *next_lnk;
	dLnkLst_t 	*headlnk;
	uint_t		primitive, mac_ppa;
	union DL_primitives *dl = (union DL_primitives *)mp->b_rptr;
	dl_test_req_t 	*dt = (dl_test_req_t *)dl;
	dl_xid_req_t 	*dx = (dl_xid_req_t *)dl;
	link_t 		*lnk = (link_t *)q->q_ptr;
	int 		reterr = DL_OUTSTATE;

	primitive = dl->dl_primitive;
	switch (primitive) {
	case DL_UNITDATA_REQ: 	{

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		/*
		 * verify service mode
		 */
		if ((lnk->lk_bind.bd_service_mode & DL_CLDLS) == 0) {

			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, primitive, DL_UNSUPPORTED, 0);
			break;
		}

		/*
		 * verify proper amount of data in frame
		 * Dlpi spec. says to return DL_UDERROR_IND on error,
		 * not dlpi_error_ack.
		 */
		msize = msgdsize(mp->b_cont);
		if ((msize > lnk->lk_max_sdu) || (msize < lnk->lk_min_sdu)) {

			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, primitive, DL_BADDATA, 0);
			break;
		}
		/* muoe962996 11/11/96 buzz */
		if (dl->unitdata_req.dl_dest_addr_length == 0) {
			ILD_RWUNLOCK(lnk->lk_lock);
			ild_trace(MID_DLPI, __LINE__, primitive, 0, 0);
			/*
			 * if we free the mblk_t here, we'll probably end up
			 * re-allocating it to send up the error_ack
			 */
			freemsg(mp);
			dlpi_error_ack(q, primitive, DL_BADADDR, 0);
			break;
		}
		/*
		 * if SNAP SAP, pass in ethernet type field
		 */
		if (lnk->lk_bind.bd_sap.llc.dl_sap == SNAPSAP) {
			/* vendor code */
			snaphdr.oui0  = lnk->lk_subs_bind_vendor1;
			snaphdr.oui1  = lnk->lk_subs_bind_vendor2;
			snaphdr.oui2  = lnk->lk_subs_bind_vendor3;
			/* type hi */
			snaphdr.type_h  = lnk->lk_subs_bind_type >> 8;
			/* type lo */
			snaphdr.type_l  = lnk->lk_subs_bind_type & 0xff;
		}

		/*
		 * call lower level to xmit frame, since UI may be dropped
		 * by lower levels
		 */
		mac = lnk->lk_mac;
		rem = (dlsap_t *)(((uintptr_t)dl) +
					dl->unitdata_req.dl_dest_addr_offset);
		loc = &lnk->lk_bind.bd_sap;
		/* muoe960282 02/05/96 buzz SD RFC */
		enet =  (int)((lnk->lk_opt << 16) | lnk->lk_enet);
		if (lnk->lk_opt) {
			ild_trace(MID_DLPI, __LINE__,
				(uint_t)enet, (uint_t)rem->ether.dl_type, 0);
		}
		db = mp->b_cont;
		mp->b_cont = NULL;
		ILD_RWUNLOCK(lnk->lk_lock);
		(void) llc2UnitdataReq(mac, rem, loc, db, &snaphdr, enet,
					dl->unitdata_req.dl_dest_addr_length);
		freeb(mp);
	}
	break;
	case DL_TEST_REQ:
	case DL_TEST_RES:

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

		/*
		 * The lk_max_sdu and lk_min_sdu should be checked before
		 * the request is sent.  Dlpi extensions do not describe what
		 * to do in case of failure.
		 */
		rem = (dlsap_t *)(((uintptr_t)dt) + dt->dl_dest_addr_offset);
		loc =  &lnk->lk_bind.bd_sap;
		mac = lnk->lk_mac;

		db = mp->b_cont;
		mp->b_cont = NULL;
		ILD_RWUNLOCK(lnk->lk_lock);
		(void) llc2TestReq(mac, rem, loc, db, dt->dl_dest_addr_length);
		freeb(mp);

		break;

	case DL_XID_REQ:
	case DL_XID_RES:


		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

		/*
		 * The lk_max_sdu and lk_min_sdu should be checked before
		 * the request is sent.  Dlpi extensions do not describe what
		 * to do in case of failure.
		 */
		rem = (dlsap_t *)(((uintptr_t)dx) + dx->dl_dest_addr_offset);
		loc = &lnk->lk_bind.bd_sap;
		mac = lnk->lk_mac;
		db = mp->b_cont;
		mp->b_cont = NULL;
		ILD_RWUNLOCK(lnk->lk_lock);

		(void) llc2XidReq(mac, rem, loc, (primitive == DL_XID_RES)
				? 1: 0, dx->dl_flag, db,
				dx->dl_dest_addr_length);
		freeb(mp);

		break;

	case DL_CONNECT_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		/*
		 * verify service mode
		 */
		if ((lnk->lk_bind.bd_service_mode & DL_CODLS) == 0) {

			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, primitive, DL_UNSUPPORTED, 0);
			break;
		}
		/*
		 * verify that mac will support connects
		 */
		mac = lnk->lk_mac;

		if ((mac->mac_service_modes & DL_CODLS) == 0) {

			ILD_RWUNLOCK(lnk->lk_lock);

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, primitive, DL_UNSUPPORTED, 0);
			break;
		}

		dsap = (dlsap_t *)(((uintptr_t)dl) +
					dl->connect_req.dl_dest_addr_offset);

		bcopy((caddr_t)dsap, (caddr_t)&lnk->lk_dsap, sizeof (dlsap_t));
		/*
		 * verify that no other queues are currently connected or trying
		 * to connect or disconnect to this remote station
		 */

		/*
		 * unlock lnk->lk_lock and re-acquire in correct
		 * order for search
		 */
		mac_ppa = lnk->lk_mac->mac_ppa;
		ILD_RWUNLOCK(lnk->lk_lock);

		ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
		ILD_WRLOCK(lnk->lk_lock);

		headlnk = &mac_lnk[mac_ppa];
		lnk1 = (link_t *)headlnk->flink;
		while (lnk1 != (link_t *)headlnk) {
			if (lnk1 != lnk) {
				ILD_RDLOCK(lnk1->lk_lock); /* RD */

				if (((lnk1->lk_state == DL_DATAXFER) ||
				(lnk1->lk_state == DL_INCON_PENDING) ||
				(lnk1->lk_state == DL_OUTCON_PENDING) ||
				(lnk1->lk_state == DL_CONN_RES_PENDING) ||
				(lnk1->lk_state == DL_USER_RESET_PENDING) ||
				(lnk1->lk_state == DL_PROV_RESET_PENDING) ||
				(lnk1->lk_state == DL_RESET_RES_PENDING) ||
				(lnk1->lk_state == DL_DISCON8_PENDING) ||
				(lnk1->lk_state == DL_DISCON9_PENDING) ||
				(lnk1->lk_state == DL_DISCON11_PENDING) ||
				(lnk1->lk_state == DL_DISCON12_PENDING) ||
				(lnk1->lk_state == DL_DISCON13_PENDING)) &&
				(CMP_MAC(lnk1->lk_dsap.llc.dl_nodeaddr,
					lnk->lk_dsap.llc.dl_nodeaddr)) &&
				(lnk1->lk_dsap.llc.dl_sap ==
				lnk->lk_dsap.llc.dl_sap) &&
				(lnk1->lk_bind.bd_sap.llc.dl_sap ==
				lnk->lk_bind.bd_sap.llc.dl_sap)) {
				ILD_RWUNLOCK(lnk1->lk_lock);
				ILD_RWUNLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_glnk_lck[
					(lnk->lk_mac)->mac_ppa]);

					ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
					freemsg(mp);
					dlpi_error_ack(q, primitive, DL_BADADDR,
							0);
					cmn_err(CE_WARN,
					"!LLC2: Attempting to connect twice\n");
					return; /* was a "break" buzz */
				} else {
					next_lnk = (link_t *)lnk1->chain.flink;
					ILD_RWUNLOCK(lnk1->lk_lock); /* RD */
				}
			} else {
				next_lnk = (link_t *)lnk1->chain.flink;
			}
			lnk1 = next_lnk;
		}

		ILD_RWUNLOCK(lnk->lk_lock);
		ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
		ILD_WRLOCK(lnk->lk_lock);

		if (lnk1 == (link_t *)headlnk) {
			lnk->lk_state = DL_OUTCON_PENDING;
			lnk->lk_sid = 0;
			mac = lnk->lk_mac;

			if ((llc2ConnectReq(mac, lnk, dsap,
					&lnk->lk_bind.bd_sap) == 0)) {
				ILD_RWUNLOCK(lnk->lk_lock);
				freemsg(mp);
			} else {
				lnk->lk_state = DL_IDLE;

				ILD_RWUNLOCK(lnk->lk_lock);
				freemsg(mp);
				dlpi_error_ack(q, DL_CONNECT_REQ, DL_SYSERR,
						ENOSR);
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			}
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		}
		break;

	case DL_SUBS_BIND_REQ:

		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		snap = (uchar_t *)(((uintptr_t)dl) +
					dl->subs_bind_req.dl_subs_sap_offset);

		lnk->lk_subs_bind_vendor1 = *snap++;
		lnk->lk_subs_bind_vendor2 = *snap++;
		lnk->lk_subs_bind_vendor3 = *snap++;
		lnk->lk_subs_bind_type = (((ushort_t)*snap) << 8)
			+ *(snap + 1);

		dl->dl_primitive = DL_SUBS_BIND_ACK;
		mp->b_datap->db_type = M_PCPROTO;
		if (q)
			qreply(q, mp);
		else
			freemsg(mp);

		ILD_RWUNLOCK(lnk->lk_lock);

		break;

	case DL_UNBIND_REQ:
		/* dlpi_unbind_req unlocks the lnk->lk_lock */
		if (dlpi_unbind_req(lnk)) {
			ILD_WRLOCK(lnk->lk_lock);
			dlpi_error_ack(q, DL_UNBIND_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			freemsg(mp);
		}
		break;

	case DL_DISABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_disabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_ENABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_enabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_PHYS_ADDR_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_phys_addr_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_GET_STATISTICS_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_get_statistics_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_SET_PHYS_ADDR_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_set_phys_addr_req(q, mp);	/* lnk is unlocked on return */
		break;
	}
	case DL_INFO_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;

	case DL_TOKEN_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_token_req(lnk); 		/* lnk in unlocked on return */
		break;

	case DL_PROMISCON_REQ:
	case DL_PROMISCOFF_REQ:
		reterr = DL_NOTSUPPORTED;

	default:

		ild_trace(MID_DLPI, __LINE__, primitive, 0, 0);
		freemsg(mp);
		dlpi_error_ack(q, primitive, reterr, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;
	}

}

/*
 * Function Label:	fDL_UDQOS_PENDING
 * Description:	Waiting ack of DL_UDQOS_REQ
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_UDQOS_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(((link_t *)(q->q_ptr))->lk_lock);
	freemsg(mp);

}


/*
 * Function Label:	fDL_OUTCON_PENDING
 * Description:	outgoing connection, awaiting DL_CONN_CON
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_OUTCON_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	if (prim == DL_INFO_REQ) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	}

	if (prim == DL_DISCONNECT_REQ) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		if (dlpi_disconnect_req(lnk, 0)) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			dlpi_error_ack(q, DL_DISCONNECT_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			freemsg(mp);
			ILD_RWUNLOCK(lnk->lk_lock);
		}
	} else {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		freemsg(mp);
	}

}


/*
 * Function Label:	fDL_INCON_PENDING
 * Description:	incoming connection, awaiting DL_CONN_RES
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_INCON_PENDING(queue_t *q, mblk_t *mp)
{
	link_t *lnk = (link_t *)q->q_ptr;
	union DL_primitives *dl = (union DL_primitives *)mp->b_rptr;
	uint_t primitive;


	primitive = dl->dl_primitive;

	if (primitive == DL_INFO_REQ) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	}

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

	if (primitive == DL_DISCONNECT_REQ) {
		if (dlpi_disconnect_req(lnk,
					dl->disconnect_req.dl_correlation)) {
			/* lnk is still locked on return */
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, DL_DISCONNECT_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		}
	} else if (dl->dl_primitive == DL_CONNECT_RES) {
#ifdef _LP64
		dLnkLst_t *headlnk;
		link_t *link;
		int mac_ppa, found_link;

		/* Check if original token was 0 */
		if (dl->connect_res.dl_resp_token == dlpi_lnk_cnt + 1) {
			dl->connect_res.dl_resp_token = 0;
		}

		/*
		 * The caller wants to accept the connection on
		 * the listening stream.
		 */
		if (dl->connect_res.dl_resp_token == 0) {
			link = NULL;
			goto zero_token;
		}

		for (mac_ppa = 0; mac_ppa <= ild_max_ppa; mac_ppa++) {
			found_link = 0;

			/*
			 * Need to drop link-lock momentarily to reacquire
			 * locks in the right order
			 */
			ILD_RWUNLOCK(lnk->lk_lock);
			ILD_WRLOCK(ild_lnk_lock);
			ILD_WRLOCK(ild_glnk_lck[mac_ppa]);
			/* Make sure the link is still there */
			if (q->q_ptr != NULL) {
				ILD_WRLOCK(lnk->lk_lock);
				ILD_RWUNLOCK(ild_lnk_lock);
			} else {
				/* Link is gone. Can't even send error ack */
				ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
				ILD_RWUNLOCK(ild_lnk_lock);
				freemsg(mp);
				return;
			}

			headlnk = &mac_lnk[mac_ppa];
			if (headlnk != NULL) {
				link = (link_t *)headlnk->flink;
				while (link != (link_t *)headlnk) {
					if (link != lnk)
						ILD_WRLOCK(link->lk_lock);
					if (link->lk_minor ==
						dl->connect_res.dl_resp_token) {
						if (link != lnk)
						ILD_RWUNLOCK(link->lk_lock);
						found_link = 1;
						break;
					}
					if (link != lnk)
						ILD_RWUNLOCK(link->lk_lock);
					link = (link_t *)link->chain.flink;
				}
			}

			ILD_RWUNLOCK(ild_glnk_lck[mac_ppa]);
			if (found_link)
				break;
		}

		if (!found_link) {
			dlpi_error_ack(lnk->lk_wrq, DL_CONNECT_RES,
					DL_BADTOKEN, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
			return;
		}

zero_token:
		if (dlpi_connect_res(lnk, dl->connect_res.dl_correlation,
				(link_t *)link)) {
#else /* _LP64 */
		if (dlpi_connect_res(lnk, dl->connect_res.dl_correlation,
				(link_t *)dl->connect_res.dl_resp_token, q)) {
#endif /* _LP64 */
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, DL_CONNECT_RES, DL_SYSERR, ENOSR);
			/* lnk still locked on return from dlpi_connect_res */
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			freemsg(mp);
			ILD_RWUNLOCK(lnk->lk_lock);
		}
	} else {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_error_ack(q, primitive, DL_OUTSTATE, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
	}

}


/*
 * Function Label:	fDL_CONN_RES_PENDING
 * Description:	Waiting ack of DL_CONNECT_RES
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_CONN_RES_PENDING(queue_t *q, mblk_t *mp)
{
	link_t *lnk = (link_t *)q->q_ptr;
	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}


/*
 * Function Label:	fDL_DATAXFER
 * Description:	connection-oriented data transfer
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DATAXFER(queue_t *q, mblk_t *mp)
{
	dlsap_t		*loc, *rem;
	uint_t		prim = *((uint_t *)mp->b_rptr);
	link_t		*lnk = (link_t *)q->q_ptr;
	mac_t 		*mac;
	mblk_t		*db;
	ushort_t	sid;
	dl_xid_req_t	*dx = (dl_xid_req_t *)(mp->b_rptr);
	dl_test_req_t	*dt = (dl_test_req_t *)(mp->b_rptr);
	int		msize, reterr = DL_OUTSTATE;

	switch (prim) {
	case DL_PHYS_ADDR_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_phys_addr_req(q, mp);	/* lnk is unlocked on return */
		break;

	}
	case DL_GET_STATISTICS_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_get_statistics_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_DISABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_disabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_ENABMULTI_REQ: 	{
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_enabmulti_req(q, mp);	/* lnk is unlocked on return */
		break;
	}

	case DL_INFO_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;

	case DL_TOKEN_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_token_req(lnk);		/* lnk is unlocked on return */
		break;

	case DL_DISCONNECT_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		if (dlpi_disconnect_req(lnk, 0)) {

			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, DL_DISCONNECT_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		}
		break;

	case DL_RESET_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		lnk->lk_state = DL_USER_RESET_PENDING;
		mac = lnk->lk_mac;
		sid = lnk->lk_sid;
		ILD_RWUNLOCK(lnk->lk_lock);
		freemsg(mp);
		if ((llc2ResetRes(mac, sid) != 0)) {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			ILD_WRLOCK(lnk->lk_lock);
			lnk->lk_state = DL_DATAXFER;
			dlpi_error_ack(q, DL_RESET_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		}
		break;

		/* muoe962998 11/12/96 buzz */
	case DL_TEST_RES:
	case DL_TEST_REQ:


		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

		msize = msgdsize(mp);
		if (dt->dl_dest_addr_length == 0) {
			/* Validate Address length */
			ild_trace(MID_DLPI, __LINE__, prim, 0, 0);
			/*
			 * if we free the mblk_t here, we'll probably end up
			 * re-allocating it to send up the error_ack
			 */
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BADADDR, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
		}
		/* Validate that datasize is within limits */
		else if ((msize > lnk->lk_max_sdu) ||
				(msize < lnk->lk_min_sdu)) {
			ild_trace(MID_DLPI, __LINE__, prim, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BADDATA, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			/*
			 * DLPI extensions don't describe what to do in case
			 * of failure.
			 */
			rem = (dlsap_t *)(((uintptr_t)dt) +
						dt->dl_dest_addr_offset);
			loc =  &lnk->lk_bind.bd_sap;
			db = mp->b_cont;
			mp->b_cont = NULL;
			mac = lnk->lk_mac;
			ILD_RWUNLOCK(lnk->lk_lock);
			(void) llc2TestReq(mac, rem, loc, db,
					dt->dl_dest_addr_length);
			freeb(mp);
		}
		break;
		/* END muoe962998 11/12/96 buzz */

		/* muoe962998 11/12/96 buzz */
	case DL_XID_REQ:
	case DL_XID_RES:


		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);

		msize = msgdsize(mp);
		if (dx->dl_dest_addr_length == 0) {
			/* Validate Address length */
			ild_trace(MID_DLPI, __LINE__, prim, 0, 0);
			/*
			 * if we free the mblk_t here, we'll probably end up
			 * re-allocating it to send up the error_ack
			 */
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BADADDR, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
		}
		/* Validate that datasize is within limits */
		else if ((msize > lnk->lk_max_sdu) ||
				(msize < lnk->lk_min_sdu)) {
			ild_trace(MID_DLPI, __LINE__, prim, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, prim, DL_BADDATA, 0);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			/*
			 * DLPI extensions don't describe what to do in case
			 * of failure.
			 */
			rem = (dlsap_t *)(((uintptr_t)dx) +
						dx->dl_dest_addr_offset);
			loc = &lnk->lk_bind.bd_sap;
			mac = lnk->lk_mac;
			db = mp->b_cont;
			mp->b_cont = NULL;
			ILD_RWUNLOCK(lnk->lk_lock);

			(void) llc2XidReq(mac, rem, loc, (prim == DL_XID_RES)
				? 1 : 0, dx->dl_flag, db,
				dx->dl_dest_addr_length);

			freeb(mp);
		}
		break;
		/* END muoe962998 11/12/96 buzz */

	case DL_PROMISCON_REQ:
	case DL_PROMISCOFF_REQ:
		reterr = DL_NOTSUPPORTED;

	default:
		ild_trace(MID_DLPI, __LINE__, prim, reterr, 0);
		freemsg(mp);
		dlpi_error_ack(q, prim, reterr, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;
	}

}

/*
 * Function Label:	fDL_USER_RESET_PENDING
 * Description:	user initiated reset, awaiting DL_RESET_CON
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_USER_RESET_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	if (prim == DL_INFO_REQ) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		return;
	}

	if (prim == DL_DISCONNECT_REQ) {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		if (dlpi_disconnect_req(lnk, 0)) {
			/* lnk is still locked on return */
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, DL_DISCONNECT_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		}
	} else {
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		freemsg(mp);
	}

}


/*
 * Function Label:	fDL_PROV_RESET_PENDING
 * Description:	provider initiated reset, awaiting DL_RESET_RES
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_PROV_RESET_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;
	mac_t *mac;
	ushort_t sid;


	switch (prim) {
	case DL_DISCONNECT_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		if (dlpi_disconnect_req(lnk, 0)) {
			/* lnk is still locked on return  */
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			freemsg(mp);
			dlpi_error_ack(q, DL_DISCONNECT_REQ, DL_SYSERR, ENOSR);
			ILD_RWUNLOCK(lnk->lk_lock);
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
			freemsg(mp);
		}
		break;

	case DL_RESET_RES:
		if (lnk->lk_local_reset == TRUE) {
		/*
		 * If LLC previously caused a local reset indication to be
		 * sent by DLPI to the DLS user, DLPI expects a DL_RESET_RES
		 * primitive from the DLS user while LLC expects a DL_RESET_REQ
		 * primitive.  The following code does the switch from the
		 * reset response operation to the reset request operation.
		 * If the reset request operation is successfully initiated, an
		 * immediate DL_OK_ACK is issued to the DLS user and the data
		 * transfer state is entered by DLPI.  However, since LLC
		 * still is in the process of finishing the reset operation
		 * with the remote node when this occurs, during this period
		 * it will reject subsequent disconnect and reset requests from
		 * the local DLS user and queue data requests for subsequent
		 * processing.
		 *
		 * Although issuing the DL_OK_ACK immediately has a side effect,
		 * it must be done to lessen the risk of a DISC (issued by the
		 * remote LLC in response to the SABME from the local LLC)
		 * causing the DL_OK_ACK to be flushed from the STREAM head
		 * during local DLPI disconnect indication processing.
		 */
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			mac = lnk->lk_mac;
			sid = lnk->lk_sid;
			ILD_RWUNLOCK(lnk->lk_lock);
			if (llc2ResetReq(mac, sid) == 0) {
				ILD_WRLOCK(lnk->lk_lock);
				lnk->lk_state = DL_DATAXFER;
				dlpi_ok_ack(q, DL_RESET_RES);
				ILD_RWUNLOCK(lnk->lk_lock);
			} else {
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				ILD_WRLOCK(lnk->lk_lock);
				dlpi_error_ack(q, DL_RESET_RES, DL_SYSERR,
						ENOSR);
				ILD_RWUNLOCK(lnk->lk_lock);
			}
		} else {
			ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
			lnk->lk_state = DL_RESET_RES_PENDING;
			mac = lnk->lk_mac;
			sid = lnk->lk_sid;
			ILD_RWUNLOCK(lnk->lk_lock);
			if (llc2ResetRes(mac, sid)) {
				/* Bad return status */
				ILD_WRLOCK(lnk->lk_lock);
				lnk->lk_state = DL_PROV_RESET_PENDING;
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				dlpi_error_ack(q, DL_RESET_RES, DL_SYSERR,
						ENOSR);
				ILD_RWUNLOCK(lnk->lk_lock);
			}
		}
		freemsg(mp);
		break;

	case DL_INFO_REQ:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_info_req(q);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;

	default:
		ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
		freemsg(mp);
		dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
		ILD_RWUNLOCK(lnk->lk_lock);
		break;
	}

}

/*
 * Function Label:	fDL_RESET_RES_PENDING
 * Description:		Waiting ack of DL_RESET_RES
 * Locks:		lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_RESET_RES_PENDING(queue_t *q, mblk_t *mp)
{
	link_t *lnk = (link_t *)q->q_ptr;

	uint_t prim = *((uint_t *)mp->b_rptr);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	fDL_DISCON8_PENDING
 * Description:	Waiting ack of DL_DISC_REQ when in DL_OUTCON_PENDING
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DISCON8_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	fDL_DISCON9_PENDING
 * Description:	Waiting ack of DL_DISC_REQ when in DL_INCON_PENDING
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DISCON9_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	fDL_DISCON11_PENDING
 * Description:	Waiting ack of DL_DISC_REQ when in DL_DATAXFER
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DISCON11_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}


/*
 * Function Label:	fDL_DISCON12_PENDING
 * Description:	Waiting ack of DL_DISC_REQ when in DL_USER_RESET_PENDING
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DISCON12_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ILD_RWUNLOCK(lnk->lk_lock);

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);

}

/*
 * Function Label:	fDL_DISCON13_PENDING
 * Description:	Waiting ack of DL_DISC_REQ when in DL_DL_PROV_RESET_PENDING
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_DISCON13_PENDING(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	fDL_SUBS_BIND_PND
 * Description:	Waiting ack of DL_SUBS_BIND_REQ
 * Locks:	lnk is WR locked on entry, unlocked on exit.
 */
void
fDL_SUBS_BIND_PND(queue_t *q, mblk_t *mp)
{
	uint_t prim = *((uint_t *)mp->b_rptr);
	link_t *lnk = (link_t *)q->q_ptr;

	ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
	freemsg(mp);
	dlpi_error_ack(q, prim, DL_OUTSTATE, 0);
	ILD_RWUNLOCK(lnk->lk_lock);
}

/*
 * Function Label:	dlpi_flushall
 * Description:	flush all queues associated with ILD driver.  Called during
 *		powerfail recovery.
 * Locks:	No locks set on entry.
 */
void
dlpi_flushall() {
	int indx;
	dLnkLst_t *headlnk;
	link_t *lnk;

	ILD_WRLOCK(ild_lnk_lock);
	for (indx = 0; indx < ild_max_ppa; indx++) {
		ILD_RDLOCK(ild_glnk_lck[indx]);
		headlnk = &mac_lnk[indx];
		lnk = (link_t *)headlnk->flink;
		while (lnk != (link_t *)headlnk) {
			if (lnk) {
				ILD_WRLOCK(lnk->lk_lock);
				if (lnk->lk_wrq) {
					flushq(lnk->lk_wrq, FLUSHALL);
					flushq(OTHERQ(lnk->lk_wrq), FLUSHALL);
				}
				ILD_RWUNLOCK(lnk->lk_lock);
				lnk = (link_t *)lnk->chain.flink;
			} else {
				break;
			}
		}
		ILD_RWUNLOCK(ild_glnk_lck[indx]);
	}
	ILD_RWUNLOCK(ild_lnk_lock);

}
