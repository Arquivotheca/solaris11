/*
 * Copyright (c) 1998-1999 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * Copyright (c) 1998 NCR Corporation, Dayton, Ohio USA
 */

#ifndef	_LLC2_SAM_H
#define	_LLC2_SAM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif



/*
 * SAM private data structure
 */
typedef struct samdata {
	mac_t		*sam_mac;	/* mac pointer */
	ushort_t	sam_state;	/* SAM DLPI state */
	timeout_id_t	sam_timeid; 	/* itimeout id */
	mblk_t		*sam_mpsave;	/* msg block to be returned when */
					/* init_req done */
	queue_t		*sam_qsave;	/* queue to be returned when */
					/* init_req done */
} samdata_t;

/*
 * prototypes
 */
int SAMsod(mac_t *, uint_t, uint_t);
int SAMinit_req(mac_t *, queue_t *, mblk_t *, void *);
int SAMuninit_req(mac_t *, queue_t *, mblk_t *);
int SAMioctl(mac_t *, queue_t *, mblk_t *);
int SAMsend(mac_t *, dlsap_t *, int, mblk_t *, int, void *);
int SAMrput(queue_t *, mblk_t *);
void SAMcleanup(mac_t *mac, macx_t *macx);

void sam_timeout(void *);
void samDL_IDLE(queue_t *, mblk_t *, mac_t *, samdata_t *);
void samDL_ATTACH_PENDING(queue_t *, mblk_t *, mac_t *, samdata_t *);
void samDL_BIND_PENDING(queue_t *, mblk_t *, mac_t *, samdata_t *);
void samDL_UNBIND_PENDING(queue_t *, mblk_t *, mac_t *, samdata_t *);
void samDL_DETACH_PENDING(queue_t *, mblk_t *, mac_t *, samdata_t *);
void samDL_OUTofSTATE(queue_t *, mblk_t *, mac_t *, samdata_t *);
int sam_send_attach(mac_t *, samdata_t *, queue_t *, mblk_t *);
int sam_send_detach(mac_t *, samdata_t *, queue_t *, mblk_t *);
int sam_send_unbind(mac_t *, samdata_t *, queue_t *, mblk_t *);
int sam_send_bind(mac_t *, samdata_t *, queue_t *, mblk_t *);

extern void dlpi_ack(queue_t *, mblk_t *);

#define	SAMSODFAIL 0
#define	SAMSODPASS 1
#define	SAMFAIL 1
#define	SAMPASS 0
#define	SAMTIMEOUT (3*hz)  /* 3 second timeout */
#define	SAMDATA_SZ sizeof (samdata_t)
#define	MAC_HDR_SZ 64

#undef TIMEOUT
#undef UNTIMEOUT

#define	TIMEOUT(func, arg, timeval) timeout(func, (void *)arg, timeval)

/* Enter with mac_lock held, exit with mac_lock held */
#define	UNTIMEOUT(timeid) { \
	timeout_id_t id = timeid; \
	if (id) { \
		timeid = 0; \
		ILD_RWUNLOCK(mac->mac_lock); \
		(void) untimeout(id); \
		ILD_WRLOCK(mac->mac_lock); \
	} \
}

#define	DLPI_ACK(q, mp) { \
	ILD_RWUNLOCK(mac->mac_lock); \
	dlpi_ack(q, mp); \
	ILD_WRLOCK(mac->mac_lock); \
}

#define	THIS_MOD 		MID_SAM

/* Error log statuses */
#define	SAM_ENOSPACE		0x2001  /* memory allocation failure */
#define	SAM_ETIMEOUT		0x2002  /* timeout waiting for response */
#define	SAM_EBADSTATE		0x2003  /* event occurred in wrong DLPI state */
#define	SAM_EBADRESPONSE	0x2004  /* unexpected response from driver */

#ifdef	__cplusplus
}
#endif

#endif	/* _LLC2_SAM_H */
