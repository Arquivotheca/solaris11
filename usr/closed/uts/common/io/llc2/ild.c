/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio, USA
 */
#define	ILD_C

/*
 * The PSARC case number PSARC 1998/360 contains additional material
 * including the design document for this driver.
 */

#define	RELEASE 4
#define	VERSION 0
#define	PATCH	".00"

char copyright[] =
"Class II Logical Link Control Driver\nCopyright (c) 1991-1998 NCR Corp.\n";

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/mkdev.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/log.h>
#include <sys/stropts.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/cpuvar.h>
#include <sys/policy.h>

#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/llc2.h>
#include "ild.h"
#include "llc2k.h"
#include "ildlock.h"
#include <sys/dlpi.h>

#define	BITSPERWORD	(sizeof (unsigned int) * 8) /* bytes in long * 8 bits */

/* ioctls for compatibility with old Grenoble LLC2 */
#define	L_SETTUNE	('L'<<8 | 3)	/* Set tuning parameters */
#define	L_GETTUNE	('L'<<8 | 4)	/* Get tuning parameters */
#define	X25_TRACEON	('L'<<8 | 7)	/* Set message tracing on */
#define	X25_TRACEOFF	('L'<<8 | 8)	/* Set message tracing off */
#define	LI_LLC2TUNE	0x23

#ifdef OLD_X25
#define	L_SETPPA	('L'<<8 | 1)
#endif

/*
 * We need to distinguish between the first open and remaining opens. Also,
 * if the first open does not links any mac interfaces underneath, the state
 * is changed back to FIRST_OPEN_NOT_DONE so that someone else can open us
 * again and do the linking.
 */
#define	FIRST_OPEN_NOT_DONE		0
#define	FIRST_OPEN_DONE			1
#define	MAC_INTERFACE_LINKED		2

/*
 * Globals
 */
int	dlpi_lnk_cnt = 4096;
int	ilddebug = 0;
int	ild_max_ppa = MAXPPA;
int	max_macs = 16;
int	ild_execution_options = (ALWAYS_Q | SHUTDOWN_DLPI);

unsigned int ild_minors[4096/BITSPERWORD];  /* up to 4096 bits */
llc2FreeList_t llc2FreeList;

#define	STA_DISABLE_REQ		1

extern dLnkLst_t lnkFreeHead;

extern int SAMsod();
extern int llc2XonReq(mac_t *, ushort_t);
extern int SAMinit_req(mac_t *, queue_t *, mblk_t *, void *);
extern int SAMuninit_req(mac_t *, queue_t *, mblk_t *);
extern int SAMioctl(mac_t *, queue_t *, mblk_t *);
extern int llc2AllocFreeList();
extern void llc2DelFreeList();
extern mblk_t *llc2_allocb(size_t, uint_t);


/*
 *  The MAC Structures and pointers
 */
mac_t	*ild_macp[MAXPPA+1+HEADROOM];

/*
 * The MAC EXTENSION Structures and pointers
 */
macx_t	*ild_macxp[MAXPPA+1+HEADROOM];

macs_t  *macs[16] = {
	SAMsod, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

dLnkLst_t	mac_lnk[MAXPPA+1];
listenQ_t	*listenQueue[MAXPPA+1];

/*
 * llc2 Stuff
 */
llc2Sta_t	*llc2StaArray[MAXPPA+1+HEADROOM];
llc2Timer_t	*llc2TimerArray[MAXPPA+1+HEADROOM];

int llc2_first_open = FIRST_OPEN_NOT_DONE;
queue_t *llc2_first_open_queue = NULL;
queue_t *llc2TimerQ = NULL;

/*
 * Global Link Lock Definitions for each PPA
 */
ild_lock_t	ild_glnk_lck[MAXPPA+1];

/* muoe970020 buzz 06/02/97 */
extern	int		llc2GetqEnableStatus(queue_t *);
extern	timeout_id_t	llc2TimerId;

/*
 * Globals
 */
ildTraceEntry_t ildTraceTable[ILDTRCTABSIZ];
int	ildTraceIndex	= 0;
int	ildTraceEnabled = 0;
queue_t *ildTraceQ	= NULL;	/* READ side queue of tcap stream */
int	ild_hwm_hit	= 0;	/* muoe962970 */
int	ild_maccnt	= 0;

/* X25 Trace Queue */
queue_t	*x25TraceQ	= NULL;
int	x25TraceEnabled	= 0;
int	x25_hwm_hit	= 0;

dev_info_t *ild_dev_info = NULL;


int  ild_open(queue_t *, dev_t *, int, int, cred_t *);
int  ild_close(queue_t *, int,	cred_t *);
void ild_ioctl(queue_t *, mblk_t *);
int  ild_uwput(queue_t *, mblk_t *);
int  ild_uwsrv(queue_t *);
int  ild_lwsrv(queue_t *);
int  ild_ursrv(queue_t *);
void ild_dealloc();
void ildFlshTrace(int);

extern int  llc2BindReq(link_t *, uint_t, uint_t);
extern int  llc2ReadSrv(queue_t *);
extern int  llc2TimerSrv();
extern int  SAMrput(queue_t *, mblk_t *);
extern void SAMcleanup(mac_t *, macx_t *);


#define	THIS_MOD MID_ILD
/*
 * Locals
 */
/* STREAMS configuration structures */
#define	HIWAT		64 * 1024	/* 64K Hi water Mark */
#define	LOWAT		HIWAT * .3	/* 30% of the HIWATER mark */

uint_t	ild_q_hiwat	= 256 * 1024;	/* initially 256K */
uint_t	ild_q_lowat	= 78 * 1024;	/* 30% of 256K => 78K */

struct module_info ildinfo = {0, "llc2", 0, INFPSZ, HIWAT, LOWAT};

/* Upper Streams (i.e. netbios/sna users) */

struct qinit urinit = { NULL, ild_ursrv, ild_open, ild_close, NULL,
				&ildinfo, NULL };

struct qinit uwinit = { ild_uwput, ild_uwsrv, NULL, NULL, NULL,
				&ildinfo, NULL };

/* Lower Streams (i.e. Solaris MAC Drivers) */

static struct qinit lrinit = { SAMrput, llc2ReadSrv, NULL, NULL, NULL,
				&ildinfo, NULL };

static struct qinit lwinit = { NULL, ild_lwsrv, NULL, NULL, NULL,
				&ildinfo, NULL };


struct streamtab ild_muxinfo = { &urinit, &uwinit, &lrinit, &lwinit };

ILD_DECL_LOCK(ild_trace_lock);
ILD_DECL_LOCK(global_mac_lock);

/*
 * Lock to protect the minor number, llc2_first_open and
 * llc2_first_open_queue.
 */
ILD_DECL_LOCK(ild_open_lock);

/* External Lock declarations */
ILD_EDECL_LOCK(ild_llc2timerq_lock);
ILD_EDECL_LOCK(ild_lnk_lock); 			/* global link lock */
ILD_EDECL_LOCK(ild_listenq_lock);
ILD_EDECL_LOCK(Con_Avail_Lock);
ILD_EDECL_LOCK(Sap_Avail_Lock);


/*
 * Function prototypes.
 */
int	llc2_attach(dev_info_t *, ddi_attach_cmd_t);
int	llc2_detach(dev_info_t *, ddi_detach_cmd_t);
int	llc2_info(dev_info_t *, ddi_info_cmd_t, void *, void **);

/*
 * This is the loadable module wrapper.  It is a generic declaration.  The
 * only things which change are:
 *   The description of this module in struct modldrv
 *   The dev_ops ptr in struct modldrv
 */
DDI_DEFINE_STREAM_OPS(llc2, nulldev, nulldev, llc2_attach, llc2_detach, nodev,
    llc2_info, D_MP, &ild_muxinfo, ddi_quiesce_not_supported);

/*
 * Module linkage information for the kernel
 */

static struct modldrv modldrv = {
	&mod_driverops,				/* drv_modops */
	"SUN LLC2 Class II Streams Driver", 	/* drv_linkinfo */
	&llc2,					/* drv_dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,			/* ml_rev, has to be MODREV_1 */
	&modldrv,		/* linkage structures */
	NULL
};

int
_init(void)
{
	int	error, ild_init();

	error = ild_init();
	if (!error) {
		error = mod_install(&modlinkage);
		if (error) {
			ild_dealloc();
			return (error);
		}
	}
	return (error);
}

int
_fini(void)
{
	int	error;

	if (ild_maccnt != 0)
		return (EBUSY);

	error = mod_remove(&modlinkage);
	if (!error) {
		ild_dealloc();
	}
	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * Functions of the driver
 */

int
llc2_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	/* Get properties (ie configuration variable values) */

	ild_max_ppa = ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "maxppa", MINPPA);
	if (ild_max_ppa > MAXPPA) {
		cmn_err(CE_NOTE,
		    "!LLC2: attach: maxppa cannot be > %d, reduced to %d",
		    MAXPPA, MAXPPA);
		ild_max_ppa = MAXPPA;
	}
	if (ild_max_ppa < MINPPA) {
		cmn_err(CE_NOTE,
		    "!LLC2: attach: maxppa cannot be < %d, set to %d",
		    MINPPA, MINPPA);
		ild_max_ppa = MINPPA;
	}

	ilddebug = ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "debug", 0);

	if (ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "shutdown_dlpi", 1) == 0)
		ild_execution_options &= ~SHUTDOWN_DLPI;

	/*
	 * Create the filesystem device node.
	 */
	if (ddi_create_minor_node(dip, "llc2", S_IFCHR, ddi_get_instance(dip),
	    DDI_PSEUDO, CLONE_DEV) == DDI_FAILURE) {
		cmn_err(CE_NOTE,
		    "!LLC2: attach: failed to create node - llc2 ");
		ddi_remove_minor_node(dip, NULL);
		return (DDI_FAILURE);
	}

	ild_dev_info = dip;
	return (DDI_SUCCESS);
}


int
llc2_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
int
llc2_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int	error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (ild_dev_info == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) ild_dev_info;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/*
 * Links (ie structures representing open applications) are queued
 * to the PPA that they have DL_ATTACHED to. This functions cycles through
 * this list and for each link structures, it will
 *	- remove that link from the list
 *	- free any pending connect or disconnect indications
 *	- free the pending outbound connection structure
 *	- deallocate the condition variable
 * 	- deallocate the lock
 *	- and, finally, free the link_t itself.
 * Note that before unlocking the link_t, the state is set to UNATTACHED,
 * the mac pointer and upper write queue pointer are cleared.
 *
 * Locks:	ild_lnk_lock (Global Lock) set on entry/exit
 *		ild_glnk_lck (per ppa lnk lock) set on entry/exit
 *		lnk->lk_lock (acquired/released)
 *
 */

void
ild_free_links(dLnkLst_t *headlnk)
{
	link_t *lnk, *nlnk;
	mblk_t *mp;
	queue_t *q;

	ild_execution_options |= LLC2_SHUTTING_DOWN;

	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);
		nlnk = (link_t *)lnk->chain.flink;
		if (lnk->lk_close & LKCL_WAKE) {
			/*
			 * This one's sleeping, change state, note failure
			 * and wake him up.
			 */
			lnk->lk_state = DL_UNATTACHED;
			lnk->lk_close |= LKCL_FAIL;
			ILD_WAKEUP(lnk->sleep_var);
			ILD_RWUNLOCK(lnk->lk_lock);
			lnk = nlnk;
			continue;
		}
		/*
		 * Remove the link from the queue so it won't be located
		 * during searches
		 */
		RMV_DLNKLST(&lnk->chain);

		if (lnk->lkOutIndPtr)
			kmem_free((void *)lnk->lkOutIndPtr, sizeof (outInd_t));

		/* Free any pending Connect/Disconnect Indications */
		/*
		 * The link is now (or soon will be) UNATTACHED, so dump any
		 * remaining pending connect indications.
		 */
		mp = lnk->lkPendHead;
		while (mp != NULL) {
			lnk->lkPendHead = lnk->lkPendHead->b_next;
			mp->b_next = NULL;
			freemsg(mp);
			mp = lnk->lkPendHead;
		}
		lnk->lkPendTail = NULL;

		/* Free any pending Disconnect Indications */
		/*
		 * simply discard any disconnect indications that are pending.
		 */
		mp = lnk->lkDisIndHead;
		while (mp != NULL) {
			lnk->lkDisIndHead = lnk->lkDisIndHead->b_next;
			mp->b_next = NULL;
			freemsg(mp);
			mp = lnk->lkDisIndHead;
		}
		lnk->lkDisIndTail = NULL;

		q = lnk->lk_wrq;
		if (q != NULL && WR(q) != NULL && WR(q)->q_ptr != NULL) {
			WR(q)->q_ptr = NULL;
		}

		lnk->lk_wrq = NULL;
		lnk->lk_mac = NULL;
		lnk->lk_state = DL_UNATTACHED;
		lnk->lk_rnr = FALSE;
		lnk->lk_close = 0;

		ILD_SLEEP_DEALLOC(lnk->sleep_var);
		ILD_RWUNLOCK(lnk->lk_lock);
		ILD_RWDEALLOC_LOCK(&lnk->lk_lock);

		kmem_free((void *)lnk, sizeof (link_t));

		lnk = nlnk;
	}
}

/*
 * This functions is called from not only the ild_dealloc() function, but
 * the LLC2 module's write put routine when processing the I_UNLINK M_IOCTL
 * command. When the unlink occurs, EVERYTHING associated with that
 * particular lower STREAM and PPA must be shut down and released.
 *
 * ild_dealloc() is invoked from both the _fini and _init functions.
 *
 * Locks held on entry/exit:	global_mac_lock should be held.
 */
void
ild_closedown_ppa(int ppa)
{
	dLnkLst_t	*headlnk;
	mac_t		*mac;
	macx_t		*macx;
	listenQ_t	*ptr, *tmp;
	size_t		sz;

	int upDisableReq(llc2Sta_t *, uchar_t, mac_t *, mblk_t *,
	    dlsap_t *, dlsap_t *, uintptr_t, uintptr_t);

	if ((mac = ild_macp[ppa]) == NULL) {
		return;
	}
	macx = ild_macxp[ppa];

	ILD_WRLOCK(mac->mac_lock);
	mac->execution_options |= LLC2_SHUTTING_DOWN;
	ILD_RWUNLOCK(mac->mac_lock);

	/*
	 * For each Station, knock it out and all it's subsidiary
	 * structures (Saps, Cons, etc)
	 */

	(void) upDisableReq(llc2StaArray[ppa], STA_DISABLE_REQ,
	    ild_macp[ppa], NULL, NULL, NULL, 0, 0);

	/* Now call dlpi_shutdown to (possibly) notify DLS Users */
	dlpi_shutdown(mac, 1);

	/*
	 * Grab the very global lock to prevent the race
	 * between application closing down and ild_free_links
	 * freeing the link_t list (pointed by q_ptr).
	 * lk_status helps prevent the race between dlpi_close
	 * and dlpi_shutdown/dlpi_linkdown.
	 */
	ILD_WRLOCK(ild_lnk_lock);
	ILD_WRLOCK(ild_listenq_lock);
	ILD_WRLOCK(ild_glnk_lck[ppa]);
	headlnk = &mac_lnk[ppa];

	ild_execution_options |= LLC2_SHUTTING_DOWN;

	ild_free_links(headlnk);

	ptr = listenQueue[ppa];
	while (ptr != NULL) {
		tmp = ptr;
		ptr = ptr->next;
		tmp->next = NULL;
		tmp->lnk = NULL;
		kmem_free((void *)tmp, sizeof (listenQ_t));
	}
	listenQueue[ppa] = NULL;

	ILD_RWUNLOCK(ild_listenq_lock);
	ILD_RWUNLOCK(ild_glnk_lck[ppa]);
	ILD_RWUNLOCK(ild_lnk_lock);

	/* Free up the lower LLC2 stuff associated with this PPA. */

	ILD_RWDEALLOC_LOCK(&llc2StaArray[ppa]->sta_lock);
	ILD_RWDEALLOC_LOCK(&llc2TimerArray[ppa]->llc2_timer_lock);
	ild_macp[ppa] = NULL;
	ild_macxp[ppa] = NULL;
	llc2StaArray[ppa] = NULL;
	llc2TimerArray[ppa] = NULL;

	/*
	 * SAM cleanup will free it's private struct. and
	 * deallocate the MAC lock
	 */
	SAMcleanup(mac, macx);

	ILD_RWDEALLOC_LOCK(&(mac->mac_lock));

	sz = sizeof (mac_t) + sizeof (macx_t) + sizeof (llc2Sta_t)
	    + sizeof (llc2Timer_t);

	kmem_free((void *)mac, sz);
}

void ild_dealloc() {
	int indx;

	/* Free dlpi_link list structures */
	for (indx = 0; indx < ild_max_ppa; indx += 1) {
		ILD_RDLOCK(global_mac_lock);
		ild_closedown_ppa(indx);
		ILD_RWUNLOCK(global_mac_lock);
		ILD_RWDEALLOC_LOCK(&ild_glnk_lck[indx]);
	}
	/* now get rid of any on the global link free list */

	ILD_WRLOCK(ild_lnk_lock);
	ild_free_links(&lnkFreeHead);
	ILD_RWUNLOCK(ild_lnk_lock);

	/* Free global lock structures */

	ILD_RWDEALLOC_LOCK(&ild_lnk_lock);
	ILD_RWDEALLOC_LOCK(&ild_listenq_lock);

	ILD_RWDEALLOC_LOCK(&ild_trace_lock);
	ILD_RWDEALLOC_LOCK(&ild_llc2timerq_lock);

	llc2DelFreeList();
	ILD_RWDEALLOC_LOCK(&llc2FreeList.lock);

	ILD_RWDEALLOC_LOCK(&Con_Avail_Lock);
	ILD_RWDEALLOC_LOCK(&Sap_Avail_Lock);
	ILD_RWDEALLOC_LOCK(&global_mac_lock);
	ILD_RWDEALLOC_LOCK(&ild_open_lock);
}


/*
 *  The decision to move from an array oriented fixed number of
 *  links was dictated by the need to speed up searching through
 *  the links as part of the performance enhancements. By keeping
 *  everything in double-linked lists, the dlpi_link is now represented
 *  by pointers and there is no need not to have all the links statically
 *  allocated. However, not having the array now means that determining
 *  the minor number is a little harder. Formally, the array index
 *  WAS the minor number. Scheme arrived at is a 4096 entry bitmap.
 *  Bit position defines the minor number, bit on implies an open device.
 *
 *  How it works:
 *	When an open occurs, ild_get_nxt_minor is called to return the
 *	value of the next available minor number. A dlpi_link is then
 *	kmem_alloced and linked to the MASTER_DLPI_LINK_LIST,
 *	mac_lnk[ild_max_ppa+1]. When whoever opened the device performs
 *	a DL_ATTACH command specifying a PPA, and therefore an adapter,
 *	the dlpi_link is moved from the master_list to the mac_lnk[ppa],
 *	where it remains until a DL_DETACH and/or ild_close is called.
 *	A successful DL_DETACH will cause the dlpi_link to move back
 *	to the master_list. The completion of the close will
 *	kmem_free the link and turn off the appropriate bit in
 *	the minor bitmap array.
 *
 */

int
ild_get_nxt_minor(unsigned int bits[])
{
	int indx;			/* Needs to be int */
	unsigned int i, start;		/* Needs to be uint */
	int j, k;			/* Needs to be int */

	start = 1;
	start = start << (BITSPERWORD-1);

	for (k = 0, indx = 0; indx < dlpi_lnk_cnt/BITSPERWORD; indx++) {
		for (i = start, j = (BITSPERWORD - 1);
		    j >= 0;
		    j--, i = (1<<j), k++) {
			if ((bits[indx] & i) == 0) {
				bits[indx] |= i;
				return (k);
			}
		}
	}
	return (-1);
}

void
ild_set_bit(int minor, unsigned int bits[])
{
	if ((minor >= 0) && (minor < dlpi_lnk_cnt))
		bits[minor/BITSPERWORD] |=
		    (1 << ((BITSPERWORD-1) - (minor%BITSPERWORD)));
}
void
ild_clear_bit(int minor, unsigned int bits[])
{
	if ((minor >= 0) && (minor < dlpi_lnk_cnt))
		bits[minor/BITSPERWORD] &=
		    ~(1 << ((BITSPERWORD-1) - (minor%BITSPERWORD)));
}

int
ild_init(void) {

	/*
	 * Zero out all array pointers in preparation for later
	 * initialization.
	 */
	bzero((void *)&ild_minors[0], sizeof (ild_minors));
	bzero((void *)&ild_macp[0], sizeof (ild_macp));
	bzero((void *)&ild_macxp[0], sizeof (ild_macxp));
	bzero((void *)&mac_lnk[0], sizeof (mac_lnk));
	bzero((void *)&listenQueue[0], sizeof (listenQueue));
	bzero((void *)&llc2StaArray[0], sizeof (llc2StaArray));

	/* muoe971496 6/12/96 buzz */
	/* This initializes the bare minimum of stuff */
	ILD_RWALLOC_LOCK(&ild_open_lock, ILD_LCK1, NULL);
	ILD_RWALLOC_LOCK(&ild_trace_lock, ILD_LCK9, NULL);
	ILD_RWALLOC_LOCK(&global_mac_lock, ILD_LCK1, NULL);
	ILD_RWALLOC_LOCK(&ild_llc2timerq_lock, ILD_LCK1, NULL);

	/* the following will allocate the global and per PPA link Locks */
	dlpi_init_lnks();
	ild_execution_options &= ~LLC2_SHUTTING_DOWN;

	llc2FreeList.available = 0;
	llc2FreeList.head = NULL;
	ILD_RWALLOC_LOCK(&llc2FreeList.lock, ILD_LCK1, NULL);
	if (llc2AllocFreeList() != 0)
		return (1);

	return (0);
}

/*
 * ild_open
 *
 * Locks:	No locks set on entry/exit.
 *		lnk is WR locked/unlocked.
 */
/*ARGSUSED*/
int
ild_open(queue_t *q, dev_t *dev, int flag, int sflag, cred_t *credp)
{
	link_t *lnk;
	int minornum;

	ild_trace(MID_ILD, __LINE__, 0, 0, 0);

	/* If system is running low on memory, don't accept new connections */
	if (llc2FreeList.available < LLC2_FREELIST_MIN) {
		if (llc2AllocFreeList() != 0)
			return (ENOMEM);
	}

	ILD_WRLOCK(ild_open_lock);
	if ((llc2_first_open == FIRST_OPEN_NOT_DONE &&
	    llc2_first_open_queue == NULL) || ild_maccnt == 0) {
		/* first open */
		if ((minornum = ild_get_nxt_minor(&ild_minors[0])) != 0) {
			ILD_RWUNLOCK(ild_open_lock);
			return (ENXIO);
		}
		/* Save the queue pointer */
		llc2_first_open_queue = RD(q);
		llc2_first_open = FIRST_OPEN_DONE;
		ILD_RWUNLOCK(ild_open_lock);
		q->q_ptr = (caddr_t)(uintptr_t)minornum;
	} else {

		/*
		 * Clone open. If plumbing is not done or no adapters in
		 * the system, then reject all clone opens until the llc2init
		 * does its job.
		 */
		ILD_WRLOCK(ild_llc2timerq_lock);
		if (llc2_first_open != MAC_INTERFACE_LINKED ||
		    llc2TimerQ == NULL) {
			ILD_RWUNLOCK(ild_llc2timerq_lock);
			ILD_RWUNLOCK(ild_open_lock);
			ild_trace(MID_ILD, __LINE__, 0, 0, 0);
			return (ENXIO);
		}
		ILD_RWUNLOCK(ild_llc2timerq_lock);

		if ((minornum = dlpi_getfreelnk(&lnk)) == -1) {
			ILD_RWUNLOCK(ild_open_lock);
			cmn_err(CE_WARN, "!LLC2: no free links\n");
			ild_trace(MID_ILD, __LINE__, 0, 0, 0);
			ILD_LOG(0, 0, "ild_open()", ILD_NO_LINK, E_SOFTWARE,
			    E_WARNING, __FILE__, __LINE__,
			    "No free dlpi_link[]'s", 0, 0, 0, 0);
			return (EAGAIN);
		}
		ILD_RWUNLOCK(ild_open_lock);
		lnk->lk_wrq = WR(q);
		lnk->lk_minor = minornum;
		WR(q)->q_ptr = (caddr_t)lnk;
		q->q_ptr = NULL;
		lnk->lk_state = DL_UNATTACHED;
		/* Was locked by dlpi_getfreelnk. Unlock it now. */
		ILD_RWUNLOCK(lnk->lk_lock);
	}

	qprocson(q);
	*dev = makedevice(getmajor(*dev), minornum);
	return (0);
}

/*
 * valid_mac - Check passed mac_t address against those in the
 *		ild_macp pointer array.
 *
 *	Returns: 1 if mac arg is in the array.
 *	0 no match
 */
int
valid_mac(mac_t *mac)
{
	int i;
	for (i = 0; i < ild_max_ppa; i++) {
		if (mac == ild_macp[i])
			return (1);
	}
	return (0);
}

/*
 * ild_uwput
 *
 * this is really a void function but the UNIX supplied header files don't
 * recognize this fact!
 */
int
ild_uwput(queue_t *q, mblk_t *mp)
{
	mac_t *mac = NULL;
	int *mctlp;
	uint_t prim;

	/*
	 * switch on message type
	 */
	switch (mp->b_datap->db_type) {
	case M_FLUSH:
		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		if (*mp->b_rptr & FLUSHW) {
			flushq(q, FLUSHALL);
			/*
			 * since the write Q has been flushed, if it is
			 * currently not enabled, allow it to be scheduled
			 * for service
			 */
			enableok(q);
		}
		if (*mp->b_rptr & FLUSHR) {
			flushq(RD(q), FLUSHALL);
			*mp->b_rptr &= ~FLUSHW;
			qreply(q, mp);
		} else {
			freemsg(mp);
		}
		break;

	case M_IOCTL:
		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		(void) putq(q, mp);
		break;

	case M_DATA:
		ild_trace(MID_ILD, __LINE__, (uintptr_t)(q), 0, 0);
		(void) putq(q, mp);
		break;

	case M_PROTO:
	case M_PCPROTO:
		prim = *((uint_t *)mp->b_rptr);
		/*
		 * Allow a DL_INFO_REQ to pass through even if there are
		 * no interfaces plumbed underneath us.
		 */
		if (ild_maccnt > 0 || prim == DL_INFO_REQ) {
			ild_trace(MID_ILD, __LINE__, 0, 0, 0);
			if (ild_execution_options & ALWAYS_Q) {
				(void) putq(q, mp);
			} else {
				if ((q->q_first == 0) &&
				    !(q->q_flag & QNOENB)) {
					dlpi_state(q, mp);
				} else {
					/*
					 * Defer processing to the service
					 * routine.
					 */
					ild_trace(MID_ILD, __LINE__, 0, 0, 0);
					(void) putq(q, mp);
				}
			}
		} else {
			mblk_t	*tmp;
			dl_error_ack_t *dl;
			union DL_primitives *d;
			/*
			 * We should not be here without a valid ppa
			 * underneath us. Send a NAK back.
			 */
			if ((tmp = allocb(DL_ERROR_ACK_SIZE, BPRI_HI))
			    == NULL) {
				cmn_err(CE_WARN, "LLC2: allocb failed\n");
				ild_trace(MID_DLPI, __LINE__, 0, 0, 0);
				freemsg(mp);
				break;
			}
			tmp->b_datap->db_type = M_PCPROTO;
			tmp->b_wptr += DL_ERROR_ACK_SIZE;

			dl = (dl_error_ack_t *)tmp->b_rptr;
			dl->dl_primitive = DL_ERROR_ACK;
			d = (union DL_primitives *)(void *)mp->b_rptr;
			dl->dl_error_primitive = d->dl_primitive;
			dl->dl_errno = DL_BADPPA;
			dl->dl_unix_errno = 0;
			freemsg(mp);
			qreply(q, tmp);
		}
		break;

	case M_CTL:
		/*
		 * The M_CTL is used to tell the driver how to handle
		 * the type field for Novell Netware users. We can
		 * either handle it the "standard" Novell RYO (Roll
		 * Your Own) way and send the length in the type field,
		 * or handle it the "econfig" way and send  the Novell
		 * SAP in the type field.
		 *
		 * The first byte of the data (r_rptr) field is the
		 * command. The second byte of the data field is the
		 * ppa.
		 */
		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		mctlp = (int *)mp->b_rptr;
		if (*mctlp == NOVELL_TYPE) {
			ILD_WRLOCK(global_mac_lock);
			mac = ild_macp[*++mctlp];
			if (mac != NULL) {
				ILD_WRLOCK(mac->mac_lock);
				ILD_RWUNLOCK(global_mac_lock);
				mac->mac_novell = NOVELL_TYPE;
				ILD_RWUNLOCK(mac->mac_lock);
			} else {
				ILD_RWUNLOCK(global_mac_lock);
			}
		} else if (*mctlp == NOVELL_LENGTH) {
			ILD_WRLOCK(global_mac_lock);
			mac = ild_macp[*++mctlp];
			if (mac != NULL) {
				ILD_WRLOCK(mac->mac_lock);
				ILD_RWUNLOCK(global_mac_lock);
				mac->mac_novell = NOVELL_LENGTH;
				ILD_RWUNLOCK(mac->mac_lock);
			} else {
				ILD_RWUNLOCK(global_mac_lock);
			}
		}
		break;

	default:
		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		freemsg(mp);
		break;
	}
	return (0);
}


/*
 *	ild_ursrv
 *
 *
 * ILD Read Service Procedure
 *
 * description:
 *	this procedure is the key part of inbound flow control, it really
 *	really belongs in dlpi.c but is put here for consistency with its
 *	partner ild_uwsrv, this procedure simply takes a message off the
 *	queue, does a canput to see if there is room above, if there is
 *	does a putnext() else does a putbq(), if the queue becomes empty
 *	an L_xon call is made to the LLC layer to indicate local busy is
 *	clear
 *
 *	this is really a void function but the UNIX supplied header files
 *	don't recognize this fact!
 *
 * execution state:
 *	service level
 *
 * parameters
 *	q			pointer to read queue
 *
 * returns:
 *	nothing
 */

int
ild_ursrv(queue_t *q)
{
	mblk_t *mp;
	link_t *lnk;
	mac_t *mac;
	ushort_t sid;


	ild_trace(MID_ILD, __LINE__, 0, 1, 0);

	while ((mp = getq(q)) != NULL) {
		if (canputnext(q)) {
			putnext(q, mp);
		} else {
			(void) putbq(q, mp);
			return (0);
		}
	}

	if (ildTraceQ == q)
		return (0);

	if ((lnk = (link_t *)WR(q)->q_ptr) != NULL) {
		ILD_WRLOCK(lnk->lk_lock);
		if ((lnk->lk_mac != NULL) && (lnk->lk_state == DL_DATAXFER)) {
			mac = lnk->lk_mac;
			sid = lnk->lk_sid;
			ILD_RWUNLOCK(lnk->lk_lock);
			(void) llc2XonReq(mac, sid);
			ILD_WRLOCK(lnk->lk_lock);
			/* muoe962592 buzz 8/16/96 */
			lnk->lk_rnr &= ~LKRNR_FC;
			ILD_RWUNLOCK(lnk->lk_lock);
			/* muoe962592 buzz 8/16/96 */
		} else {
			ILD_RWUNLOCK(lnk->lk_lock);
		}
	}
	return (0);
}


/*
 * ild_uwsrv
 *
 * this is really a void function but the UNIX supplied header files don't
 * recognize this fact!
 */

int
ild_uwsrv(queue_t *q)
{
	mblk_t *mp;

	ild_trace(MID_ILD, __LINE__, (uintptr_t)(q), 0, 0);


	for (;;) {
		if ((mp = getq(q)) == NULL) {
			break;
		}
		switch (mp->b_datap->db_type) {
		case M_DATA:
			/*
			 * dlpi_data will return non-zero when the queue is
			 * busy. Should bail now, we'll be back-enabled later.
			 */
			if (dlpi_data(q, mp, 1) != 0) {
				/* muoe970020 buzz 06/02/97 */
				if (llc2GetqEnableStatus(q))
					qenable(q);
				return (0);
			}
			break;

		case M_IOCTL:
			ild_trace(MID_ILD, __LINE__, 0, 0, 0);
			ild_ioctl(q, mp);
			break;

		case M_PROTO:
		case M_PCPROTO:
			dlpi_state(q, mp);
			break;

		default:
			ild_trace(MID_ILD, __LINE__, 0, 0, 0);
			freemsg(mp);
		}
	}
	return (0);
}

/*
 * ild_lwsrv
 *
 * this is really a void function but the UNIX supplied header files don't
 * recognize this fact!
 *
 * Lower Write Service Routine.
 *
 * This functions will be run by the STREAMS when the MAC driver drains
 * it's queues and backenables us.
 *
 * This function is to re-enable any upper queues associated with this MAC
 * when backenabled.
 *
 * Each link_t structure associated with this MAC (via an explicit
 * DL_ATTACH_REQ) will be in a linked list addressed by mac_lnk[mac->mac_ppa].
 *
 * The MAC address is in the q_ptr field.
 */

int
ild_lwsrv(queue_t *q)
{
	link_t		*lnk, *nlnk;
	dLnkLst_t	*headlnk;
	mac_t		*mac = (mac_t *)q->q_ptr;

	ild_trace(MID_ILD, __LINE__, (uintptr_t)(q), 0, 0);

	ILD_WRLOCK(ild_glnk_lck[mac->mac_ppa]);
	headlnk = &mac_lnk[mac->mac_ppa];
	lnk = (link_t *)headlnk->flink;
	while (lnk != (link_t *)headlnk) {
		ILD_WRLOCK(lnk->lk_lock);
		if (lnk->lk_wrq)
			qenable(lnk->lk_wrq);
		nlnk = (link_t *)lnk->chain.flink;
		ILD_RWUNLOCK(lnk->lk_lock);
		lnk = nlnk;
	}
	ILD_RWUNLOCK(ild_glnk_lck[mac->mac_ppa]);

	return (0);
}


/*
 * ild_close
 */
/*ARGSUSED*/
int
ild_close(queue_t *q, int flag, cred_t *credp)
{
	timeout_id_t llc2TimerId_save = 0;
	link_t *lnk;

	ild_trace(MID_ILD, __LINE__, 0, 0, 1);

	qprocsoff(q);

	ILD_WRLOCK(global_mac_lock);
	if (ild_maccnt == 0 && llc2TimerId != 0) {
		/*
		 * If the timeout callback is already running
		 * the DDI dictates it will complete before
		 * untimeout() returns.
		 */
		llc2TimerId_save = llc2TimerId;
		(void) untimeout(llc2TimerId_save);

		/*
		 * The callback was already running and  it
		 * has rerun timeout(), that will not recur
		 * since llc2TimerQ was zero'ed when ild_maccnt
		 * went to zero.
		 */
		if (llc2TimerId != llc2TimerId_save)
			(void) untimeout(llc2TimerId);
		llc2TimerId = 0;
	}
	ILD_RWUNLOCK(global_mac_lock);

	ILD_WRLOCK(ild_open_lock);
	if (llc2_first_open == FIRST_OPEN_DONE &&
	    llc2_first_open_queue == RD(q)) {
		int minornum;


		minornum = (uintptr_t)q->q_ptr;
		q->q_ptr = NULL;
		OTHERQ(q)->q_ptr = NULL;

		ild_clear_bit(minornum, &ild_minors[0]);
		/*
		 * If the first open actually linked some mac interface
		 * underneath, then we are ready to accept more opens else
		 * we are still looking for someone to open us and link
		 * some mac interfaces underneath.
		 */
		if (ild_maccnt)
			llc2_first_open = MAC_INTERFACE_LINKED;
		else
			llc2_first_open = FIRST_OPEN_NOT_DONE;
		llc2_first_open_queue = NULL;
	} else {
		/*
		 * if appl is doing trace capture, turn it off, don't send
		 * up data
		 */
		if (ildTraceQ == q) {
			ILD_WRLOCK(ild_trace_lock);
			ildTraceEnabled = 0;
			ildFlshTrace(0);
			ildTraceQ = NULL;
			ILD_RWUNLOCK(ild_trace_lock);
		}
		flushq(q, FLUSHALL);
		flushq(WR(q), FLUSHALL);

		if (x25TraceQ == q) {
			ILD_WRLOCK(ild_trace_lock);
			x25TraceEnabled = 0;
			x25TraceQ = NULL;
			ILD_RWUNLOCK(ild_trace_lock);
		}
		flushq(q, FLUSHALL);
		flushq(WR(q), FLUSHALL);

		/*
		 * Grab the very global lock to prevent the race
		 * between application closing down and ild_free_links
		 * freeing the link_t list (pointed by q_ptr).
		 * lk_status helps prevent the race between dlpi_close
		 * and dlpi_shutdown/dlpi_linkdown.
		 */
		ILD_WRLOCK(ild_lnk_lock);
		if (WR(q)->q_ptr != NULL) {
			lnk = (link_t *)WR(q)->q_ptr;
			ILD_WRLOCK(lnk->lk_lock);
			lnk->lk_status = LKCL_CLOSED;
			ILD_RWUNLOCK(lnk->lk_lock);
			if (!(ild_execution_options & LLC2_SHUTTING_DOWN)) {
				dlpi_close(WR(q)->q_ptr);
			}
			WR(q)->q_ptr = NULL;
			lnk->lk_wrq = NULL;
		}
		ILD_RWUNLOCK(ild_lnk_lock);
	}

	if (ild_maccnt == 0) {
		/* Reset the minor number generation */
		bzero((void *)&ild_minors[0], sizeof (ild_minors));
		/* Someone might want to plumb us again */
		llc2_first_open = FIRST_OPEN_NOT_DONE;

		ild_execution_options &= ~LLC2_SHUTTING_DOWN;
	}

	ILD_RWUNLOCK(ild_open_lock);
	return (0);
}

/*
 * ild_ioctl
 */
void
ild_ioctl(queue_t *q, mblk_t *mp)
{
	mac_t		*mac = NULL;
	macx_t		*macx = NULL;
	struct iocblk 	*iocp = (struct iocblk *)mp->b_rptr;
	link_t		*lnk = (link_t *)q->q_ptr;
	int		ppa = 0;
	mblk_t		*opt;
	int		error;
	struct stroptions	*stropt;

	if (ild_maccnt == 0 && iocp->ioc_cmd != I_PLINK) {
		/*
		 * If there is no mac interface underneath and this ioctl
		 * is not about to create one, there is no point to go down
		 * further.
		 */
		miocnak(q, mp, 0, EINVAL);
		return;
	}

	switch (iocp->ioc_cmd) {
	case ILD_LLC2:
	case ILD_GCONFIG:
	case ILD_PPA_INFO:
		break;
	default:
		/* Every other ioctl needs to have sufficient privilege */
		if (secpolicy_net_config(iocp->ioc_cr, B_FALSE) != 0) {
			miocnak(q, mp, 0, EACCES);
			return;
		}
	}

	switch (iocp->ioc_cmd) {

	case ILD_MAC: {
		ild_header_t *llc2iocp;

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		iocp->ioc_error = 0;

		/*
		 * Here miocpullup() verifies at least ild_header_t's
		 * worth of data is present. The rest of the payload will
		 * be verified in SAMioctl().
		 */
		error = miocpullup(mp, sizeof (ild_header_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		llc2iocp = (ild_header_t *)mp->b_cont->b_rptr;

		if (llc2iocp->ppa >= ild_max_ppa) {
			iocp->ioc_error = ENXIO;
		} else {
			ILD_RDLOCK(global_mac_lock);
			mac = ild_macp[llc2iocp->ppa];
			ILD_RWUNLOCK(global_mac_lock);
			if (mac) {
				/* qreply will be handled by SAMioctl */
				(void) SAMioctl(mac, q, mp);
				return;
			} else {
				iocp->ioc_error = ENOENT;
			}
		}
	}
	break;

	case ILD_LLC2: {
		ild_header_t *llc2iocp;

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		iocp->ioc_error = 0;

		/*
		 * Here miocpullup() verifies at least ild_header_t's
		 * worth of data is present. The rest of the payload will
		 * be verified in llc2Ioctl().
		 */
		error = miocpullup(mp, sizeof (ild_header_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		llc2iocp = (ild_header_t *)mp->b_cont->b_rptr;

		if (llc2iocp->ppa >= ild_max_ppa) {
			iocp->ioc_error = ENXIO;
		} else {
			ILD_RDLOCK(global_mac_lock);
			mac = ild_macp[llc2iocp->ppa];
			macx = ild_macxp[llc2iocp->ppa];
			if (mac) {
				llc2Ioctl(mac, q, mp);
				ILD_RWUNLOCK(global_mac_lock);
				return;
			} else {
				iocp->ioc_error = ENOENT;
			}
			ILD_RWUNLOCK(global_mac_lock);
		}
	}
	break;

	case ILD_INIT: {
		init_t *initp;

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);

		error = miocpullup(mp, sizeof (init_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		initp = (init_t *)mp->b_cont->b_rptr;

		/*
		 * don't allow ILD_INIT for attached queue
		 */
		ILD_RDLOCK(global_mac_lock);
		if ((mac = ild_macp[initp->ppa]) == NULL) {
			iocp->ioc_error = ENXIO;
		} else {
			ILD_WRLOCK(mac->mac_lock);
			if (lnk->lk_state != DL_UNATTACHED ||
			    initp->ppa >= ild_max_ppa) {
				iocp->ioc_error = ENXIO;
			} else if (mac->mac_state & MAC_INIT) {
				/*
				 * see that adapter is in correct state before
				 * initializing
				 */
				iocp->ioc_error = ENXIO;
			} else {
				/*
				 * initialize MAC
				 */
				iocp->ioc_error = EIO; /* preset bad status */
				/*
				 * SAMinit_req calls ild_init_con() which does
				 * the qreply. SAMinit_req() takes care of
				 * dropping the mac_lock.
				 */
				ILD_RWUNLOCK(global_mac_lock);
				if (SAMinit_req(mac, q, mp, NULL) == 0) {
					return;
				}
				break;
			}
			ILD_RWUNLOCK(mac->mac_lock);
		}
		ILD_RWUNLOCK(global_mac_lock);
	}
	break;

	case ILD_UNINIT: {
		macx_t *macx;
		uninit_t *uninitp;

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);

		error = miocpullup(mp, sizeof (uninit_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		uninitp = (uninit_t *)mp->b_cont->b_rptr;

		/*
		 * don't allow ILD_UNINIT for attached queue
		 */
		ILD_RDLOCK(global_mac_lock);
		if ((mac = ild_macp[uninitp->ppa]) == NULL) {
			iocp->ioc_error = ENXIO;
		} else {
			ILD_WRLOCK(mac->mac_lock);

			if (lnk->lk_state != DL_UNATTACHED ||
			    uninitp->ppa >= ild_max_ppa) {
				iocp->ioc_error = ENXIO;
			} else if ((mac->mac_state & MAC_INIT) == 0) {
				iocp->ioc_error = ENXIO;
			} else {
				if (ild_execution_options & SHUTDOWN_DLPI)
					dlpi_shutdown(mac, 0);
				/*
				 * uninitialize MAC
				 */
				iocp->ioc_error = EIO;
				macx = ild_macxp[mac->mac_ppa];
				macx->ppa_status_old = macx->ppa_status;
				/* PPA is not set any longer but still linked */
				macx->ppa_status = PPA_LINKED;
				ILD_RWUNLOCK(mac->mac_lock);
				(void) SAMuninit_req(mac, q, mp);
				/*
				 * SAMuninit_req calls
				 * ild_uninit_con() that does
				 * the qreply.
				 */
				ILD_RWUNLOCK(global_mac_lock);
				return;
			}
			ILD_RWUNLOCK(mac->mac_lock);
		}
		ILD_RWUNLOCK(global_mac_lock);
	}
	break;

	case ILD_GCONFIG: {
		adapter_t *brd;

		error = miocpullup(mp, sizeof (adapter_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		brd = (adapter_t *)mp->b_cont->b_rptr;

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);
		/*
		 * don't allow ILD_GCONFIG for attached queue
		 */
		if (lnk->lk_state != DL_UNATTACHED ||
		    (ppa = brd->ppa) >= ild_max_ppa) {
			iocp->ioc_error = ENXIO;
			break;
		}

		ILD_RDLOCK(global_mac_lock);
		mac = ild_macp[ppa];
		if (mac == NULL) {
			ILD_RWUNLOCK(global_mac_lock);
			iocp->ioc_error = ENOENT;
			break;
		}

		macx = ild_macxp[ppa];
		ILD_WRLOCK(mac->mac_lock);
		if (mac != NULL) {
			brd->state = (uchar_t)mac->mac_state;
			brd->ppa = (uchar_t)mac->mac_ppa;
			brd->adapterid = ((mac->mac_pos[1] << 8)
			    | mac->mac_pos[0]);
			CPY_MAC(mac->mac_bia, brd->bia);
		} else {
			iocp->ioc_error = ENOENT;
		}
		ILD_RWUNLOCK(mac->mac_lock);
		ILD_RWUNLOCK(global_mac_lock);
	}
	break;

	case ILD_TCAPSTART: {

		/*
		 * don't allow ILD_TCAPSTART for attached queue
		 */
		iocp->ioc_error = 0;
		ILD_WRLOCK(lnk->lk_lock);
		if (lnk->lk_state != DL_UNATTACHED) {
			iocp->ioc_error = ENXIO;
		}
		ILD_RWUNLOCK(lnk->lk_lock);
		if (ildTraceQ != NULL) {
			iocp->ioc_error = EAGAIN;
		}
		if (iocp->ioc_error == 0) {
			/*
			 * muoe962970 : brundage - 11/14/96
			 * Increase high water mark of ildTraceQ to 2 meg.
			 */
			(void) strqset(RD(q), QHIWAT, 0, 2097152);
			ild_hwm_hit = 0;
			/*
			 * save the READ side queue, then enable trace
			 */
			ILD_WRLOCK(ild_trace_lock);
			ildTraceQ = RD(q);
			ildTraceEnabled = 1;
			ILD_RWUNLOCK(ild_trace_lock);
		}
	}
	break;

	case ILD_TCAPSTOP: {
		iocp->ioc_error = 0;
		ILD_WRLOCK(ild_trace_lock);
		if (ildTraceQ != RD(q)) {
			iocp->ioc_error = ENXIO;
		}
		/*
		 * disable trace, flush any remaining trace information, then
		 * clear queue pointer
		 */
		ildTraceEnabled = 0;
		ildFlshTrace(1);
		ildTraceQ = NULL;
		ILD_RWUNLOCK(ild_trace_lock);
	}
	break;

	case X25_TRACEON: {

		/*
		 * allow X25 Trace for only unattached queue so
		 * that llc2 doesn't have to worry about this
		 * stream.
		 */
		iocp->ioc_error = 0;
		ILD_WRLOCK(lnk->lk_lock);
		if (lnk->lk_state != DL_UNATTACHED) {
			iocp->ioc_error = ENXIO;
			break;
		}
		ILD_RWUNLOCK(lnk->lk_lock);

		if (x25TraceQ != NULL) {
			iocp->ioc_error = EAGAIN;
			break;
		}

		/*
		 * muoe962970 : brundage - 11/14/96
		 * Increase high water mark of ildTraceQ to 2 meg.
		 */
		(void) strqset(RD(q), QHIWAT, 0, 2097152);
		x25_hwm_hit = 0;

		/*
		 * save the READ side queue, then enable trace
		 */
		ILD_WRLOCK(ild_trace_lock);
		x25TraceQ = RD(q);
		x25TraceEnabled = 1;
		ILD_RWUNLOCK(ild_trace_lock);
	}
	break;

	case X25_TRACEOFF: {
		iocp->ioc_error = 0;
		ILD_WRLOCK(ild_trace_lock);
		if (x25TraceQ != RD(q)) {
			iocp->ioc_error = ENXIO;
		}

		/* disable trace, clear queue pointer */
		x25TraceEnabled = 0;
		x25TraceQ = NULL;
		ILD_RWUNLOCK(ild_trace_lock);
	}
	break;

	case M_SETOPTS: {
		iocp->ioc_error = 0;
		if ((opt = allocb(sizeof (struct stroptions), BPRI_MED))
		    != NULL) {
			stropt = (struct stroptions *)opt->b_rptr;
			opt->b_datap->db_type = M_SETOPTS;
			opt->b_wptr += sizeof (struct stroptions);
			bcopy(mp->b_cont->b_rptr, stropt,
			    sizeof (struct stroptions));
			putnext(OTHERQ(q), opt);
		} else {
			iocp->ioc_error = ENOSR;
		}
	}
	break;

	case L_GETTUNE:
	case L_SETTUNE: {
		struct llc2_tnioc *pp;
		int llc2SetTuneParmsReq();

		ild_trace(MID_ILD, __LINE__, 0, 0, 0);

		error = miocpullup(mp, sizeof (struct llc2_tnioc));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		pp = (struct llc2_tnioc *)mp->b_cont->b_rptr;

		if (pp->lli_type != LI_LLC2TUNE) {
			iocp->ioc_error = EINVAL;
			break;
		}

		if (pp->lli_ppa >= ild_max_ppa) {
			iocp->ioc_error = ENXIO;
			break;
		}

		ILD_RDLOCK(global_mac_lock);
		mac = ild_macp[pp->lli_ppa];
		macx = ild_macxp[pp->lli_ppa];
		if (!mac) {
			iocp->ioc_error = ENOENT;
			ILD_RWUNLOCK(global_mac_lock);
			break;
		}

		iocp->ioc_error = llc2SetTuneParmsReq(mac, q, mp);
		ILD_RWUNLOCK(global_mac_lock);
	}
	break;

#ifdef OLD_X25

	case I_LINK:
	case I_UNLINK:
		cmn_err(CE_CONT, "ild_ioctl: Got I_LINK/I_UNLINK\n");
		iocp->ioc_error = 0;
		break;

	case L_SETPPA:
		cmn_err(CE_CONT, "ild_ioctl: Got L_SETPPA/L_GETPPA\n");
		iocp->ioc_error = 0;
		break;

#endif /* OLD_X25 */

	case I_PLINK:
	case I_PUNLINK: {
		struct	linkblk *linkp = (struct linkblk *)mp->b_cont->b_rptr;
		queue_t *downq = linkp->l_qbot;
		/*
		 * XX64	This doesn't look at all right ...
		 */
		int	index = (uintptr_t)q->q_ptr;
		size_t	sz;
		int	n;

		if (iocp->ioc_cmd == I_PLINK) {
			iocp->ioc_error = EAGAIN;
			for (n = MAXPPA + HEADROOM; n > ild_max_ppa; n--) {
				/* make sure slot is empty */
				ILD_WRLOCK(global_mac_lock);
				if (ild_macp[n] == NULL) {
					void *bigptr;
					sz = sizeof (mac_t) + sizeof (macx_t)
					    + sizeof (llc2Sta_t)
					    + sizeof (llc2Timer_t);
					bigptr = kmem_zalloc(sz, KM_NOSLEEP);
					if (bigptr == NULL) {
						/*
						 * break out of the loop.
						 */
						cmn_err(CE_WARN,
						    "!LLC2: No memory\n");
						ild_trace(THIS_MOD, __LINE__,
						    0, 0, 0);
						ILD_LOG(0, 0, "ild_ioctl()",
						    ILD_NO_LINK, E_SOFTWARE,
						    E_WARNING, __FILE__,
						    __LINE__,
						    "LLC2: No memory\n",
						    0, 0, 0, 0);
						ILD_WRLOCK(global_mac_lock);
						break;
					}
					mac = (mac_t *)bigptr;
					ILD_RWALLOC_LOCK(&mac->mac_lock,
					    ILD_LCK3, NULL);
					ild_macp[n] = mac;
					macx = ild_macxp[n] =
					    (macx_t *)((uintptr_t)mac +
					    sizeof (mac_t));
					llc2StaArray[n] =
					    (llc2Sta_t *)((uintptr_t)macx +
					    sizeof (macx_t));
					llc2TimerArray[n] =
					    (llc2Timer_t *)((uintptr_t)
					    llc2StaArray[n] +
					    sizeof (llc2Sta_t));
					ild_maccnt++;
					ILD_WRLOCK(ild_llc2timerq_lock);
					if (llc2TimerQ == NULL)
						llc2TimerQ = RD(downq);
					ILD_RWUNLOCK(ild_llc2timerq_lock);
					ILD_WRLOCK(mac->mac_lock);

					/* Connect up with lower STREAM */
					macx->lowerQ = downq;
					/* UpperQ  minor # */
					macx->upper_index = index;
					macx->lower_index = linkp->l_index;
					macx->ppa_status = PPA_LINKED;
					macx->ppa_status_old = PPA_LINKED;
					/* Not currently assigned */
					mac->mac_ppa = MAXPPA;
					mac->execution_options &=
					    ~LLC2_SHUTTING_DOWN;
					downq->q_ptr = RD(downq)->q_ptr =
					    (caddr_t)mac;
					noenable(RD(downq));
					iocp->ioc_error = 0;
					ILD_RWUNLOCK(mac->mac_lock);
					ILD_RWUNLOCK(global_mac_lock);
					break;
				} else {
					ILD_RWUNLOCK(global_mac_lock);
				}
			}
		} else { /* I_PUNLINK */
			int n;
			int tppa;
			macx_t *tmacx = NULL;

			iocp->ioc_error = EINVAL;
			/*
			 * first, figure out which PPA is being unlinked!
			 */
			for (n = MAXPPA + HEADROOM; n > 0; n--) {
				ILD_RDLOCK(global_mac_lock);
				macx = ild_macxp[n];
				if ((macx != NULL) &&
				    macx->lower_index == linkp->l_index) {

					ILD_WRLOCK(ild_llc2timerq_lock);
					if (llc2TimerQ == RD(macx->lowerQ) &&
					    ild_maccnt > 1) {
					/*
					 * We are shutting down the
					 * ppa on which timerQ was
					 * running and this is not the
					 * last ppa to be shut down.
					 * We will still need the
					 * timerQ.
					 */
					for (tppa = 0; tppa < ild_max_ppa;
					    tppa++) {
					if (tppa == n)
						continue;
					tmacx = ild_macxp[tppa];
					if (tmacx == NULL)
						continue;
					/* found another valid ppa */
					llc2TimerQ = RD(tmacx->lowerQ);
					break;
					}
					}
					ILD_RWUNLOCK(ild_llc2timerq_lock);

					if (macx->ppa_status == PPA_LINKED &&
					    macx->ppa_status_old ==
					    PPA_SET) {
						/*
						 * now shut it down, trashing
						 * all upper streams in the
						 * process.
						 */
						ild_closedown_ppa(n);
					} else {
						mac_t *mac;
						sz = sizeof (mac_t)
						    + sizeof (macx_t)
						    + sizeof (llc2Sta_t)
						    + sizeof (llc2Timer_t);
						mac = ild_macp[n];
						ild_macp[n] = NULL;
						ild_macxp[n] = NULL;
						llc2StaArray[n] = NULL;
						llc2TimerArray[n] = NULL;

						kmem_free((void *)mac, sz);
					}
					if (--ild_maccnt == 0) {
						ILD_WRLOCK(
						    ild_llc2timerq_lock);
						llc2TimerQ = NULL;
						ILD_RWUNLOCK(
						    ild_llc2timerq_lock);
					}
					iocp->ioc_error = 0;
					ILD_RWUNLOCK(global_mac_lock);
					break;
				}

				ILD_RWUNLOCK(global_mac_lock);

			}
		}
	}
	break;

	case LLC_SETPPA: {
		llc2Sta_t *llc2Sta;
		macx_t *macx;
		int n;
		ppa_config_t *ppc;
		uint_t newppa;
		uint_t newindex;
		int newinstance;

		error = miocpullup(mp, sizeof (ppa_config_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		ppc = (ppa_config_t *)mp->b_cont->b_rptr;

		if ((ppc->index == 0) || (ppc->ppa >= ild_max_ppa)) {
			iocp->ioc_error = EINVAL;
			break;
		} else {
			newppa = ppc->ppa;
			newindex = ppc->index;
			newinstance = ppc->instance;
		}

		if (ild_macp[newppa]) {
			iocp->ioc_error = EBUSY;
			break;
		}

		iocp->ioc_error = ENODEV;
		for (n = MAXPPA + HEADROOM; n > ild_max_ppa; n--) {
			ILD_WRLOCK(global_mac_lock);
			if ((mac = ild_macp[n]) != NULL) {
				macx = ild_macxp[n];
				llc2Sta = llc2StaArray[n];
				ILD_WRLOCK(mac->mac_lock);
				if ((macx != NULL) &&
				    (macx->lower_index == newindex)) {

					mac->mac_ppa = newppa;
					/*
					 * Each MAC driver associates it's own
					 * numbers with each of the adapters it
					 * controls. It uses these instance num
					 * as its PPAs. The mac_ppa field is
					 * the logical mapping imposed by the
					 * llc2 module across all the adapters
					 * it controls.
					 */
					if (newinstance >= 0)
						macx->lower_instance =
						    newinstance;
					llc2Sta->mac = mac;
					ILD_RWUNLOCK(mac->mac_lock);

					ild_macp[newppa] = mac;
					ild_macp[n] = NULL;

					ild_macxp[newppa] = macx;
					macx->ppa_status_old = macx->ppa_status;
					macx->ppa_status = PPA_SET;
					ild_macxp[n] = NULL;

					llc2StaArray[newppa] = llc2Sta;
					llc2StaArray[n] = NULL;

					llc2TimerArray[newppa] =
					    llc2TimerArray[n];
					llc2TimerArray[n] = NULL;
					ILD_WRLOCK(mac->mac_lock);

					(void) strqset(macx->lowerQ, QHIWAT,
					    0, ild_q_hiwat);
					(void) strqset(macx->lowerQ, QLOWAT,
					    0, ild_q_lowat);

					/*
					 * Everything's setup call
					 * SAM SOD function
					 */
					if (macs[0](mac, newppa,
					    newppa) > 0) {
						/*
						 * Establish/Init.
						 * llc2 func. ptrs
						 */
						llc2_init(mac);
					}

					enableok(RD(macx->lowerQ));
					iocp->ioc_error = 0;
					ILD_RWUNLOCK(mac->mac_lock);
					ILD_RWUNLOCK(global_mac_lock);
					break;
				}
				ILD_RWUNLOCK(mac->mac_lock);
			}
			ILD_RWUNLOCK(global_mac_lock);
		}

	}
	break;


	case LLC_GETPPA: {
		ppa_config_t *ppc;

		error = miocpullup(mp, sizeof (ppa_config_t));
		if (error != 0) {
			miocnak(q, mp, 0, error);
			return;
		}
		ppc = (ppa_config_t *)mp->b_cont->b_rptr;

		ILD_RDLOCK(global_mac_lock);
		if (ild_macp[ppc->ppa] == NULL)
			iocp->ioc_error = ENODEV;
		else {
			macx = ild_macxp[ppc->ppa];
			ppc->index = macx->lower_index;
			ppc->instance =  macx->lower_instance;
			iocp->ioc_error = 0;
		}
		ILD_RWUNLOCK(global_mac_lock);
	}
	break;

	default:
		iocp->ioc_error = ENXIO;
		break;
	}

	if (iocp->ioc_error != 0) {
		ild_trace(MID_ILD, __LINE__, iocp->ioc_cmd, iocp->ioc_error, 0);
		mp->b_datap->db_type = M_IOCNAK;
	} else {
		mp->b_datap->db_type = M_IOCACK;
	}
	qreply(q, mp);

}


/*
 * ild_init_con
 *
 * Locks:	NO locks set on entry /exit
 *
 */

/*ARGSUSED*/
void
ild_init_con(mac_t *mac, queue_t *q, mblk_t *mp, ushort_t rc)
{

	struct iocblk 	*iocp = (struct iocblk *)mp->b_rptr;

	ild_trace(MID_ILD, __LINE__, rc, (uintptr_t)q->q_ptr, 0);
	if (rc) {
		mp->b_datap->db_type = M_IOCNAK;
	} else {
		iocp->ioc_error = 0;
		mp->b_datap->db_type = M_IOCACK;
	}
	if (q->q_ptr) {
		qreply(q, mp);
	} else {
		freemsg(mp);
	}

}

/*ARGSUSED*/
void
ild_uninit_con(mac_t *mac, queue_t *q, mblk_t *mp, ushort_t rc)
{
	ild_trace(MID_ILD, __LINE__, rc, 0, 0);
	if (rc) {
		mp->b_datap->db_type = M_IOCNAK;
	} else {
		mp->b_datap->db_type = M_IOCACK;
	}
	qreply(q, mp);

}

/*
 * ild_trace
 *
 * returns nothing
 *
 * Locks:	No locks set on entry/exit.
 *		ild_trace_lock is WR locked/unlocked.
 */
uint_t ildTrcSeqNum = 0;

/*ARGSUSED*/
void
ildTrace(uint_t mod, uint_t line, uintptr_t parm1, uintptr_t parm2,
		uint_t level) {
	ildTraceEntry_t *tptr;
	mblk_t *mp;
	tCapBuf_t *tcap;
	uint_t ild_lbolt;
	processorid_t cpuid;

	if (ildTraceQ == NULL)
		return;

	ild_lbolt = ddi_get_lbolt();
	ILD_WRLOCK(ild_trace_lock);
	tptr = &ildTraceTable[ildTraceIndex];
	ildTraceIndex = (ildTraceIndex + 1) & (ILDTRCTABSIZ - 1);
	tptr->time = ild_lbolt;
	/* 05/12/98 WARNING!!! Following line is not DDI/DKI */
	cpuid = CPU->cpu_id;
	tptr->cpu_mod_line = ((cpuid << 24) & 0xff000000) +
			((mod << 16)  & 0xff0000) + (line & 0xffff);
	tptr->parm1 = parm1;
	tptr->parm2 = parm2;
	if ((ildTraceIndex & (ILDTCAPSIZE - 1)) == 0) {
		/*
		 * muoe962970 : brundage - 11/14/96 Verify that
		 * ildTraceQ is  not in flow control before
		 * allocating new message.
		 * If ildTraceQ is full don't bother allocating
		 * the message.
		 */
		if (!canput(ildTraceQ)) {
			if (!ild_hwm_hit) {
				cmn_err(CE_NOTE,
					"!LLC2: tcap high water mark reached.");
				cmn_err(CE_NOTE,
					"Data may be missing from log");
				ild_hwm_hit = 1;
			}
		} else if ((mp = allocb(sizeof (tCapBuf_t), BPRI_LO)) != NULL) {
			tcap = (tCapBuf_t *)mp->b_rptr;
			mp->b_wptr = mp->b_rptr + sizeof (tCapBuf_t);
			tcap->seqNum = ildTrcSeqNum;
			tcap->lastFlag = 0;
			if (ildTraceIndex == 0) {
				bcopy((caddr_t)&ildTraceTable[ILDTRCTABSIZ
								- ILDTCAPSIZE],
				(caddr_t)tcap->t,
				ILDTCAPSIZE * sizeof (ildTraceEntry_t));
			} else {
				bcopy((caddr_t)&ildTraceTable[ildTraceIndex
								- ILDTCAPSIZE],
				(caddr_t)tcap->t,
				ILDTCAPSIZE * sizeof (ildTraceEntry_t));
			}
			(void) putq(ildTraceQ, mp);
		}
		ildTrcSeqNum += 1;
	}
	ILD_RWUNLOCK(ild_trace_lock);
}


/*
 *	ildFlshTrace
 *
 *
 * execution state:
 *	must be called at splstr level
 *
 * Locks:	No locks set on entry/exit.
 *		ild_trace_lock is WR locked/unlocked.
 *
 * returns:
 *	nothing
 */
void
ildFlshTrace(int fflag)
{
	int tsize;
	mblk_t *mp;
	tCapBuf_t *tcap;

	if (fflag && ((tsize = ildTraceIndex & (ILDTCAPSIZE - 1)) != 0)) {
		if ((mp = allocb(sizeof (tCapBuf_t), BPRI_LO)) != NULL) {
			tcap = (tCapBuf_t *)mp->b_rptr;
			mp->b_wptr = ((uchar_t *)&tcap->t) +
			    (tsize * sizeof (ildTraceEntry_t));
			tcap->seqNum = ildTrcSeqNum;
			tcap->lastFlag = 1;
			ildTraceIndex -= tsize;
			bcopy((caddr_t)&ildTraceTable[ildTraceIndex],
			    (caddr_t)tcap->t,
			    tsize * sizeof (ildTraceEntry_t));
			mp->b_next = NULL;
			(void) putq(ildTraceQ, mp);
		}
	}
	ildTrcSeqNum = 0;
	ildTraceIndex = 0;

}

#define	TR_LLC2_DAT	101
#define	LLC_STID	202

void
x25_trace(mac_t *mac, mblk_t *mp, int received) {
	struct trc_ctl {
		uint8_t		trc_prim;	/* Trace msg identifier */
		uint8_t		trc_mid;	/* Id of protocol module */
		uint16_t	trc_spare;	/* for alignment */
		uint32_t	trc_linkid;	/* Link Id */
		uint8_t		trc_rcv;	/* Message tx or rx */
		uint8_t		trc_spare2[3];	/* for alignment */
		uint32_t	trc_time;	/* Time stamp */
		uint16_t	trc_seq;	/* Message seq number */
	};

	struct trc_llc2_dat {
		uint8_t		trc_prim;	/* Trace msg identifier */
		uint8_t		trc_mid;	/* Id of protocol module */
		uint16_t	trc_spare;	/* for alignment */
		uint32_t	trc_linkid;	/* Link Id */
		uint8_t		trc_rcv;	/* Message tx or rx */
		uint8_t		trc_spare2[3];	/* for alignment */
		uint32_t	trc_time;	/* Time stamp */
		uint16_t	trc_seq;	/* Message seq number */
		uint8_t		trc_src[6];	/* Source address */
		uint8_t		trc_dst[6];	/* Destination address */
	};

	union trc_types {
		uint8_t			trc_prim; /* Trace msg identifier */
		struct trc_ctl		trc_hdr; /* Basic trace data */
		struct trc_llc2_dat	trc_llc2dat; /* LLC2 trace data */
	};

	union DL_primitives *pp = (union DL_primitives *)mp->b_rptr;
	union trc_types *trc_msg;
	static uint16_t trace_seq = 0;
	mblk_t *cmp;
	mblk_t *dmp;

	ILD_WRLOCK(ild_trace_lock);

	ASSERT(x25TraceQ != NULL);

	if (!canputnext(x25TraceQ)) {
		ILD_RWUNLOCK(ild_trace_lock);
		if (!x25_hwm_hit) {
			cmn_err(CE_NOTE,
				"Data may be missing from x25Trace log");
			x25_hwm_hit = 1;
		}
		return;
	}

	if (mp->b_cont != NULL && ((dmp = copymsg(mp->b_cont)) != NULL)) {

		if ((cmp = allocb(sizeof (union trc_types), BPRI_LO)) == NULL) {
			ILD_RWUNLOCK(ild_trace_lock);
			freemsg(dmp);
			return;
		}

		cmp->b_datap->db_type = M_PROTO;
		cmp->b_wptr += sizeof (union trc_types);

		trc_msg = (union trc_types *)cmp->b_rptr;

		trc_msg->trc_prim = TR_LLC2_DAT;
		trc_msg->trc_llc2dat.trc_seq = trace_seq;
		trace_seq++;
		trc_msg->trc_llc2dat.trc_linkid = mac->mac_ppa;
		trc_msg->trc_llc2dat.trc_time = ddi_get_lbolt();
		trc_msg->trc_llc2dat.trc_rcv = (uint8_t)received;
		trc_msg->trc_llc2dat.trc_mid = LLC_STID;

		/*
		 * Return both source and destination
		 * if we have a unitdata_ind, or 0 and
		 * destination for unitdata_req (since
		 * the req has no source field)
		 */
		if (pp->dl_primitive == DL_UNITDATA_IND) {
			bcopy((char *)(mp->b_rptr +
				pp->unitdata_ind.dl_src_addr_offset),
				(char *)trc_msg->trc_llc2dat.trc_src, 6);
			bcopy((char *)(mp->b_rptr +
				pp->unitdata_ind.dl_dest_addr_offset),
				(char *)trc_msg->trc_llc2dat.trc_dst, 6);
		} else {
			bcopy((char *)mac->mac_bia,
				(char *)trc_msg->trc_llc2dat.trc_src, 6);
			bcopy((char *)(mp->b_rptr +
				pp->unitdata_req.dl_dest_addr_offset),
				(char *)trc_msg->trc_llc2dat.trc_dst, 6);
		}

		cmp->b_cont = dmp;
		(void) putnext(x25TraceQ, cmp);
	}

	ILD_RWUNLOCK(ild_trace_lock);
}
