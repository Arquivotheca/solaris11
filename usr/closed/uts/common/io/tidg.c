/*	Copyright (c) 1989 AT&T		*/
/*		All Rights Reserved	*/

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Transport Interface Library connection oriented test driver - issue 1
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#define	_SUN_TPI_VERSION	2
#include <sys/tihdr.h>
#include <sys/tidg.h>
#include <sys/tiuser.h>
#include <sys/strlog.h>
#include <sys/debug.h>
#include <sys/signal.h>
#include <sys/pcb.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/stat.h>

#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

/*
 * macro to change state NEXTSTATE(event, current state)
 */
#define	NEXTSTATE(X, Y) ti_statetbl[X][Y]
#define	NR			127	/* unreachable state */

extern char ti_statetbl[TE_NOEVENTS][TS_NOSTATES];

/*
 * tidg identifcation id (should not be static should be configurable).
 * Also a Interface ID string should be included so modules
 * may not be pushed with unlike interfaces.
 */
#define	TIDG_ID		2223
#define	TIDU_SIZE	TIDU_DG_SIZE
#define	TIDU_DG_SIZE	1024

#define	TIDG_CNT	12
struct ti_tidg ti_tidg[TIDG_CNT];

struct tidg_addr {
	uint32_t addr;
	int32_t used;
};

struct tidg_addr tidg_addr[MAXADDR] = {
	{0x00010000, 0}, {0x00020000, 0}, {0x00030000, 0},
	{0x00000001, 0}, {0x00000002, 0}, {0x00000003, 0},
	{0x00000100, 0}, {0x00000200, 0}, {0x00000300, 0},
	{0x01000000, 0}, {0x02000000, 0}, {0x03000000, 0},
};

static dev_info_t *tidg_dip;

static int
snd_flushrw(queue_t *q)
{
	mblk_t *mp;

	if ((mp = allocb(1, BPRI_HI)) == NULL)
		return (0);
	mp->b_wptr++;
	mp->b_datap->db_type = M_FLUSH;
	*mp->b_rptr = FLUSHRW;
	if (q->q_flag & QREADR) {
		putnext(q, mp);
	} else
		qreply(q, mp);
	return (1);
}

static void
tidg_finished(queue_t *q)
{
	struct ti_tidg *tiptr;

	tiptr = (struct ti_tidg *)q->q_ptr;
	flushq(q, FLUSHALL);
	tiptr->ti_state = TS_UNBND;
}

static void
tidg_sendfatal(queue_t *q, mblk_t *mp)
{
	struct ti_tidg *tiptr;

	tiptr = (struct ti_tidg *)q->q_ptr;

	tidg_finished(q);
	tiptr->ti_flags |= FATAL;
	mp->b_datap->db_type = M_ERROR;
	*mp->b_datap->db_base = EPROTO;
	mp->b_rptr = mp->b_datap->db_base;
	mp->b_wptr = mp->b_datap->db_base + sizeof (char);
	freemsg(unlinkb(mp));
	if (q->q_flag & QREADR)
		putnext(q, mp);
	else
		qreply(q, mp);
}

static void
tidg_snd_errack(queue_t *q, mblk_t *mp, t_scalar_t tli_error,
	t_scalar_t unix_error)
{
	mblk_t *tmp;
	t_scalar_t type;
	union T_primitives *pptr;

	pptr = (union T_primitives *)mp->b_rptr;
	type = pptr->type;
	/*
	 * is message large enough to send
	 * up a T_ERROR_ACK primitive
	 */
	if ((mp->b_datap->db_lim - mp->b_datap->db_base) <
	    sizeof (struct T_error_ack)) {
		if ((tmp = allocb(sizeof (struct T_error_ack), BPRI_HI)) ==
		    NULL) {
			(void) strlog(TIDG_ID, -1, 0, SL_TRACE|SL_ERROR,
			    "snd_errack: couldn't allocate msg\n");
			tidg_sendfatal(q, mp);
			return;
		}
		freemsg(mp);
		mp = tmp;
	}
	mp->b_rptr = mp->b_datap->db_base;
	mp->b_wptr = mp->b_rptr + sizeof (struct T_error_ack);
	pptr = (union T_primitives *)mp->b_rptr;
	pptr->error_ack.ERROR_prim = type;
	pptr->error_ack.TLI_error = tli_error;
	pptr->error_ack.UNIX_error = unix_error;
	pptr->error_ack.PRIM_type = T_ERROR_ACK;
	mp->b_datap->db_type = M_PCPROTO;
	freemsg(unlinkb(mp));
	if (q->q_flag & QREADR)
		putnext(q, mp);
	else
		qreply(q, mp);
}

static void
tidg_snd_okack(queue_t *q, mblk_t *mp)
{
	mblk_t *tmp;
	t_scalar_t type;
	union T_primitives *pptr;

	pptr = (union T_primitives *)mp->b_rptr;
	type = pptr->type;
	/*
	 * is message large enough to send
	 * up a T_OK_ACK primitive
	 */
	if ((mp->b_datap->db_lim - mp->b_datap->db_base) <
	    sizeof (struct T_ok_ack)) {
		if ((tmp = allocb(sizeof (struct T_ok_ack), BPRI_HI)) == NULL) {
			(void) strlog(TIDG_ID, -1, 0, SL_TRACE|SL_ERROR,
			    "tidg_snd_okack: couldn't allocate ack msg\n");
			tidg_sendfatal(q, mp);
			return;
		}
		freemsg(mp);
		mp = tmp;
	}
	mp->b_rptr = mp->b_datap->db_base;
	mp->b_wptr = mp->b_rptr + sizeof (struct T_ok_ack);
	pptr = (union T_primitives *)mp->b_rptr;
	pptr->ok_ack.CORRECT_prim = type;
	pptr->ok_ack.PRIM_type = T_OK_ACK;
	mp->b_datap->db_type = M_PCPROTO;
	freemsg(unlinkb(mp));
	if (q->q_flag & QREADR)
		putnext(q, mp);
	else
		qreply(q, mp);
}

static void
tidg_bind(queue_t *q, mblk_t *mp)
{
	union T_primitives *pptr;
	struct ti_tidg *tiptr;
	mblk_t *sptr;
	struct stroptions *soptr;

	pptr = (union T_primitives *)mp->b_rptr;
	tiptr = (struct ti_tidg *)q->q_ptr;

	if ((sptr = allocb(sizeof (struct stroptions), BPRI_HI)) == NULL) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_bind: couldn't allocate stropts msg\n");
		tidg_snd_errack(q, mp, TSYSERR, EAGAIN);
		return;
	}
	soptr = (struct stroptions *)sptr->b_rptr;
	sptr->b_wptr = sptr->b_rptr + sizeof (struct stroptions);
	soptr->so_flags = SO_MINPSZ|SO_MAXPSZ|SO_HIWAT|SO_LOWAT;
	soptr->so_readopt = 0;
	soptr->so_wroff = 0;
	soptr->so_minpsz = 0;
	soptr->so_maxpsz = TIDU_SIZE;
	soptr->so_hiwat = 4 * TIDU_SIZE;
	soptr->so_lowat = TIDU_SIZE;
	sptr->b_datap->db_type = M_SETOPTS;
	qreply(q, sptr);

	if (((mp->b_wptr - mp->b_rptr) < (sizeof (struct T_bind_req))) ||
	    ((mp->b_wptr - mp->b_rptr) < (pptr->bind_req.ADDR_offset +
	    pptr->bind_req.ADDR_length))) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_bind: bad control info in request msg\n");
		tidg_snd_errack(q, mp, TSYSERR, EINVAL);
		return;
	}

	tiptr->ti_addr = 0;

	if (pptr->bind_req.ADDR_length == 0) {
		struct tidg_addr *tmp;
		mblk_t *new;

		if ((new = allocb(sizeof (struct T_bind_ack) +
		    sizeof (uint32_t), BPRI_HI)) == NULL) {
			(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0,
			    SL_TRACE|SL_ERROR,
			    "tidg_bind: couldn't allocate ack msg\n");
			tidg_snd_errack(q, mp, TSYSERR, EAGAIN);
			return;
		}

		freemsg(mp);
		mp = new;
		pptr = (union T_primitives *)mp->b_rptr;
		mp->b_wptr = mp->b_rptr + sizeof (struct T_bind_ack) +
		    sizeof (uint32_t);

		pptr->bind_ack.CONIND_number = 0;
		for (tmp = tidg_addr; tmp < &tidg_addr[MAXADDR]; tmp++) {
			if (!tmp->used) {
				tmp->used++;
				*(uint32_t *)(mp->b_rptr +
				    sizeof (struct T_bind_ack)) = tmp->addr;
				tiptr->ti_addr = tmp->addr;
				pptr->bind_ack.ADDR_offset =
				    (t_scalar_t)sizeof (struct T_bind_ack);
				pptr->bind_ack.ADDR_length =
				    (t_scalar_t)sizeof (int32_t);
				break;
			}
		}
		if (tmp > &tidg_addr[MAXADDR-1]) {
			tiptr->ti_state = NEXTSTATE(TE_ERROR_ACK,
			    tiptr->ti_state);
			tidg_snd_errack(q, mp, TNOADDR, 0);
			return;
		}
	} else {
		struct tidg_addr *tmp;
		uint32_t *addr;
		int addrinuse = 0;

		if ((pptr->bind_req.ADDR_length != sizeof (uint32_t)) ||
		    (pptr->bind_req.ADDR_offset < sizeof (struct T_bind_req)) ||
		    ((pptr->bind_req.ADDR_offset + sizeof (uint32_t)) <
		    (mp->b_wptr - mp->b_rptr))) {
			tiptr->ti_state =
			    NEXTSTATE(TE_ERROR_ACK, tiptr->ti_state);
			tidg_snd_errack(q, mp, TBADADDR, 0);
			return;
		}
		addr = (uint32_t *)(mp->b_rptr + pptr->bind_req.ADDR_offset);

		for (tmp = tidg_addr; tmp < &tidg_addr[MAXADDR]; tmp++) {
			if (tmp->addr == *addr) {
				if (tmp->used)
					addrinuse++;
				else
					tmp->used++;
				break;
			}
		}
		if (tmp >= &tidg_addr[MAXADDR-1]) {
			tiptr->ti_state = NEXTSTATE(TE_ERROR_ACK,
			    tiptr->ti_state);
			tidg_snd_errack(q, mp, TBADADDR, 0);
			return;
		}
		if (addrinuse) {
			/*
			 * Address already in use, so assign another one.
			 */
			for (tmp = tidg_addr;
			    tmp < &tidg_addr[MAXADDR]; tmp++) {
				if (!tmp->used) {
					tmp->used++;
					*addr = tmp->addr;
					break;
				}
			}
			if (tmp >= &tidg_addr[MAXADDR]) {
				tiptr->ti_state = NEXTSTATE(T_ERROR_ACK,
				    tiptr->ti_state);
				tidg_snd_errack(q, mp, TNOADDR, 0);
				return;
			}
		}
		tiptr->ti_addr = *addr;
	}
	mp->b_datap->db_type = M_PCPROTO;
	pptr->bind_ack.PRIM_type = T_BIND_ACK;
	tiptr->ti_state = NEXTSTATE(TE_BIND_ACK, tiptr->ti_state);
	qreply(q, mp);
}

static void
tidg_unbind(queue_t *q, mblk_t *mp)
{
	struct ti_tidg *tiptr;
	struct tidg_addr *taddr;

	tiptr = (struct ti_tidg *)q->q_ptr;
	if (!snd_flushrw(q)) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_unbind: couldn't flush queues\n");
		tidg_snd_errack(q, mp, TSYSERR, EPROTO);
		return;
	}
	for (taddr = tidg_addr; taddr < &tidg_addr[MAXADDR]; taddr++)
		if (taddr->addr == tiptr->ti_addr) {
			taddr->used = 0;
			break;
		}
	tiptr->ti_addr = 0;
	tiptr->ti_state = NEXTSTATE(TE_OK_ACK1, tiptr->ti_state);
	tidg_snd_okack(q, mp);
}

static void
tidg_optmgmt(queue_t *q, mblk_t *mp)
{
	union T_primitives *pptr;
	struct ti_tidg *tiptr;

	pptr = (union T_primitives *)mp->b_rptr;
	tiptr = (struct ti_tidg *)q->q_ptr;

	if (((mp->b_wptr - mp->b_rptr) < (sizeof (struct T_optmgmt_req))) ||
	    ((mp->b_wptr - mp->b_rptr) < (pptr->optmgmt_req.OPT_offset +
	    pptr->optmgmt_req.OPT_length))) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_optmgmt: bad control part of msg\n");
		tidg_snd_errack(q, mp, TSYSERR, EINVAL);
		return;
	}

	switch (pptr->optmgmt_req.MGMT_flags) {

	mblk_t *tmp;
	case T_DEFAULT:
		if ((tmp = allocb(sizeof (struct T_optmgmt_ack) +
		    sizeof (int32_t), BPRI_HI)) == NULL) {
			(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0,
			    SL_TRACE|SL_ERROR,
			    "tidg_optmgmt: couldn't allocate "
			    "DEFAULT ack msg\n");
			tidg_snd_errack(q, mp, TSYSERR, EAGAIN);
			return;
		}
		freemsg(mp);
		pptr = (union T_primitives *)tmp->b_rptr;
		tmp->b_wptr = tmp->b_rptr + sizeof (struct T_optmgmt_ack) +
		    sizeof (int32_t);
		tmp->b_datap->db_type = M_PCPROTO;
		pptr->optmgmt_ack.MGMT_flags = T_DEFAULT;
		pptr->optmgmt_ack.PRIM_type = T_OPTMGMT_ACK;
		pptr->optmgmt_ack.OPT_length = (t_scalar_t)sizeof (int32_t);
		pptr->optmgmt_ack.OPT_offset = sizeof (struct T_optmgmt_ack);
		*(int32_t *)(tmp->b_rptr + sizeof (struct T_optmgmt_ack)) =
		    DEFAULTOPT;
		tiptr->ti_state = NEXTSTATE(TE_OPTMGMT_ACK, tiptr->ti_state);
		qreply(q, tmp);
		return;

	case T_CHECK:
		if ((pptr->optmgmt_req.OPT_length != sizeof (int32_t)) ||
		    (pptr->optmgmt_req.OPT_offset <
		    sizeof (struct T_optmgmt_req))) {
			tiptr->ti_state =
			    NEXTSTATE(TE_ERROR_ACK, tiptr->ti_state);
			tidg_snd_errack(q, mp, TBADOPT, 0);
			return;
		}
		pptr->optmgmt_ack.PRIM_type = T_OPTMGMT_ACK;
		if (*(int32_t *)(mp->b_rptr + pptr->optmgmt_req.OPT_offset) !=
		    DEFAULTOPT)
			pptr->optmgmt_ack.MGMT_flags |= T_FAILURE;
		else
			pptr->optmgmt_ack.MGMT_flags |= T_SUCCESS;
		tiptr->ti_state = NEXTSTATE(TE_OPTMGMT_ACK, tiptr->ti_state);
		mp->b_datap->db_type = M_PCPROTO;
		qreply(q, mp);
		return;

	case T_NEGOTIATE:
		if ((pptr->optmgmt_req.OPT_length != sizeof (int32_t)) ||
		    (pptr->optmgmt_req.OPT_offset <
		    sizeof (struct T_optmgmt_req))) {
			tiptr->ti_state =
			    NEXTSTATE(TE_ERROR_ACK, tiptr->ti_state);
			tidg_snd_errack(q, mp, TBADOPT, 0);
			return;
		}
		pptr->optmgmt_ack.PRIM_type = T_OPTMGMT_ACK;
		*(int32_t *)(mp->b_rptr + pptr->optmgmt_req.OPT_offset) =
		    DEFAULTOPT;
		tiptr->ti_state = NEXTSTATE(TE_OPTMGMT_ACK, tiptr->ti_state);
		mp->b_datap->db_type = M_PCPROTO;
		qreply(q, mp);
		return;

	default:
		tiptr->ti_state = NEXTSTATE(TE_ERROR_ACK, tiptr->ti_state);
		tidg_snd_errack(q, mp, TBADFLAG, 0);
		return;
	}
}

static void
tidg_checkmsg(queue_t *q, mblk_t *mp)
{
	union T_primitives *pptr;
	struct ti_tidg *tiptr;

	/*
	 * check to see if the interface is in the correct
	 * state for event
	 */

	tiptr = (struct ti_tidg *)q->q_ptr;
	pptr = (union T_primitives *)mp->b_rptr;

	if (tiptr->ti_flags&FATAL) {
		freemsg(mp);
		return;
	}

	switch (pptr->type) {

	case T_SVR4_OPTMGMT_REQ:
	case T_OPTMGMT_REQ:
		if (NEXTSTATE(TE_OPTMGMT_REQ, tiptr->ti_state) == NR) {
			tidg_snd_errack(q, mp, TOUTSTATE, 0);
			return;
		}
		tiptr->ti_state = NEXTSTATE(TE_OPTMGMT_REQ, tiptr->ti_state);
		tidg_optmgmt(q, mp);
		return;

	case T_UNBIND_REQ:
		if (NEXTSTATE(TE_UNBIND_REQ, tiptr->ti_state) == NR) {
			tidg_snd_errack(q, mp, TOUTSTATE, 0);
			return;
		}
		tiptr->ti_state = NEXTSTATE(TE_UNBIND_REQ, tiptr->ti_state);
		tidg_unbind(q, mp);
		return;

	case O_T_BIND_REQ:
	case T_BIND_REQ:
		if (NEXTSTATE(TE_BIND_REQ, tiptr->ti_state) == NR) {
			tidg_snd_errack(q, mp, TOUTSTATE, 0);
			return;
		}
		tiptr->ti_state = NEXTSTATE(TE_BIND_REQ, tiptr->ti_state);
		tidg_bind(q, mp);
		return;
	}
}

static void
tidg_copy_info(struct T_info_ack *tia, struct ti_tidg *tiptr)
{
	tia->PRIM_type = T_INFO_ACK;
	tia->TSDU_size = TIDU_SIZE;
	tia->ETSDU_size = -2;
	tia->CDATA_size = -2;
	tia->DDATA_size = -2;
	tia->ADDR_size = (t_scalar_t)sizeof (uint32_t);
	tia->OPT_size = (t_scalar_t)sizeof (int32_t);
	tia->TIDU_size = TIDU_SIZE;
	tia->SERV_type = T_CLTS;
	tia->CURRENT_state = tiptr->ti_state;
	tia->PROVIDER_flag = 0;
}

/*
 * This routine responds to T_CAPABILITY_REQ messages.  It is called by
 * tidg_wput.
 */
static void
tidg_capability_req(queue_t *q, mblk_t *mp)
{
	struct ti_tidg		*tiptr;
	mblk_t 			*ack;
	t_uscalar_t		cap_bits1;
	unsigned char		db_type;
	struct T_capability_ack	*tca;

	db_type = mp->b_datap->db_type;
	cap_bits1 = ((struct T_capability_req *)mp->b_rptr)->CAP_bits1;

	tiptr = (struct ti_tidg *)q->q_ptr;
	if ((ack = reallocb(mp, sizeof (struct T_capability_ack), 0)) == NULL) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_capability_req: "
		    "couldn't flush or allocate ack msg\n");
		tidg_sendfatal(q, mp);
		return;
	}

	ack->b_wptr = ack->b_rptr + sizeof (struct T_capability_ack);
	ack->b_datap->db_type = db_type;

	tca = (struct T_capability_ack *)ack->b_rptr;
	tca->PRIM_type = T_CAPABILITY_ACK;
	tca->CAP_bits1 = 0;

	if (cap_bits1 & TC1_INFO) {
		tidg_copy_info(&tca->INFO_ack, tiptr);
		tca->CAP_bits1 |= TC1_INFO;
	}

	qreply(q, ack);
}

static void
tidg_ireq(queue_t *q, mblk_t *mp)
{
	struct ti_tidg *tiptr;
	mblk_t *ack;

	tiptr = (struct ti_tidg *)q->q_ptr;
	if ((ack = allocb(sizeof (struct T_info_ack), BPRI_HI)) == NULL) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidg_ireq: couldn't flush or allocate ack msg\n");
		tidg_sendfatal(q, mp);
		return;
	}
	freemsg(mp);
	ack->b_wptr = ack->b_rptr + sizeof (struct T_info_ack);
	ack->b_datap->db_type = M_PCPROTO;
	tidg_copy_info((struct T_info_ack *)ack->b_rptr, tiptr);
	qreply(q, ack);
}

static void
tidg_snd_uderr(queue_t *q, mblk_t *mp, t_scalar_t error)
{
	union T_primitives *pptr, *tpptr;
	struct ti_tidg *tiptr;
	uint32_t destaddr = 0;
	int options = 0;
	size_t uderr_size;
	mblk_t *tmp;

	tiptr = (struct ti_tidg *)q->q_ptr;
	pptr = (union T_primitives *)mp->b_rptr;
	destaddr = *((uint32_t *)(mp->b_rptr + pptr->unitdata_req.DEST_offset));
	if (pptr->unitdata_req.OPT_length)
		options = *((int32_t *)(mp->b_rptr +
		    pptr->unitdata_req.OPT_offset));
	uderr_size = sizeof (struct T_uderror_ind) + 2 * sizeof (int32_t);

	if ((tmp = allocb(uderr_size, BPRI_HI)) == NULL) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "snd_uderr: couldn't allocate T_UDERROR_IND msg\n");
		tidg_sendfatal(q, mp);
		return;
	}
	tpptr = (union T_primitives *)tmp->b_rptr;
	tmp->b_datap->db_type = M_PROTO;
	tmp->b_wptr = tmp->b_rptr +
	    sizeof (struct T_uderror_ind) + sizeof (int32_t);
	tpptr->uderror_ind.PRIM_type = T_UDERROR_IND;
	tpptr->uderror_ind.ERROR_type = error;
	tpptr->uderror_ind.DEST_length = (t_scalar_t)sizeof (int32_t);
	tpptr->uderror_ind.DEST_offset =
	    (t_scalar_t)sizeof (struct T_uderror_ind);
	*(uint32_t *)(tmp->b_rptr + tpptr->uderror_ind.DEST_offset) = destaddr;
	if (options) {
		tpptr->uderror_ind.OPT_length = (t_scalar_t)sizeof (int32_t);
		tpptr->uderror_ind.OPT_offset =
		    (t_scalar_t)sizeof (struct T_uderror_ind) +
		    (t_scalar_t)sizeof (int32_t);
		*(int32_t *)(tmp->b_rptr + tpptr->uderror_ind.OPT_offset) =
		    options;
		tmp->b_wptr += sizeof (int32_t);
	} else {
		tpptr->uderror_ind.OPT_length = 0;
		tpptr->uderror_ind.OPT_offset = 0;
	}
	tiptr->ti_state = NEXTSTATE(TE_UDERROR_IND, tiptr->ti_state);
	freemsg(mp);
	if (q->q_flag & QREADR)
		putnext(q, tmp);
	else
		qreply(q, tmp);
}

static int
do_udata(queue_t *q, mblk_t *mp)
{
	union T_primitives *pptr;
	struct ti_tidg *tiptr, *rtiptr;
	struct tidg_addr *tmp;
	size_t size;
	int error = 0;

	pptr = (union T_primitives *)mp->b_rptr;
	tiptr = (struct ti_tidg *)q->q_ptr;

	if ((size = mp->b_wptr - mp->b_rptr) < sizeof (struct T_unitdata_req))
		error++;
	if (size < (pptr->unitdata_req.DEST_length +
	    pptr->unitdata_req.DEST_offset))
		error++;
	if (pptr->unitdata_req.OPT_length) {
		if (size < (pptr->unitdata_req.OPT_length +
		    pptr->unitdata_req.OPT_offset))
			error++;
	}

	if (error) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "do_udata: bad control part of T_UNITDATA_REQ\n");
		tidg_sendfatal(q, mp);
		return (0);
	}
	if (msgdsize(mp) > TIDU_SIZE) {
		(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "do_udata: exceeded TIDU size on T_UNITDATA_REQ\n");
		tidg_sendfatal(q, mp);
		return (0);
	}

	if (pptr->unitdata_req.DEST_length != sizeof (uint32_t)) {
		tidg_snd_uderr(q, mp, UDBADADDR);
		return (1);
	}
	for (tmp = tidg_addr; tmp < &tidg_addr[MAXADDR]; tmp++) {
		if (tmp->addr == *(uint32_t *)(mp->b_rptr +
		    pptr->unitdata_req.DEST_offset))
			break;
	}
	if (tmp >= &tidg_addr[MAXADDR]) {
		tidg_snd_uderr(q, mp, UDBADADDR);
		return (1);
	}
	if (pptr->unitdata_req.OPT_length) {
		if ((pptr->unitdata_req.OPT_length != sizeof (int32_t)) ||
		    (*(int32_t *)(mp->b_rptr +
		    pptr->unitdata_req.OPT_offset) != DEFAULTOPT)) {
			tidg_snd_uderr(q, mp, UDBADOPTS);
			return (1);
		}
	}

	for (rtiptr = ti_tidg; rtiptr < &ti_tidg[TIDG_CNT]; rtiptr++)
		if ((rtiptr->ti_addr == tmp->addr) &&
		    (rtiptr->ti_state == TS_IDLE))
			break;

	if (rtiptr >= &ti_tidg[TIDG_CNT]) {
		tidg_snd_uderr(q, mp, UDUNREACHABLE);
		return (1);
	}

	/*
	 * Make sure you can put a message on rd queue, and store if not.
	 * Must do this before you convert T_UNITDATA_REQ into T_UNITDATA_IND.
	 */
	if (!canputnext(rtiptr->ti_rdq)) {
		rtiptr->ti_backwq = q;
		(void) putbq(q, mp);
		return (0);
	}

	/*
	 * Request is valid - create the indication to
	 * send to remote destination, using the control
	 * buffer from the T_UNITDATA_REQ.  It's ok to overlay
	 * the indication structure on the request structure
	 * since fields are same size.  Must insert src addr
	 * but leave options untouched.
	 */
	pptr->type = T_UNITDATA_IND;
	*(uint32_t *)(mp->b_rptr + pptr->unitdata_ind.SRC_offset) =
	    tiptr->ti_addr;
	rtiptr->ti_state = NEXTSTATE(TE_UNITDATA_IND, rtiptr->ti_state);
	putnext(rtiptr->ti_rdq, mp);
	return (1);
}

/* ARGSUSED */
static int
tidg_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (ddi_create_minor_node(devi, "tidg", S_IFCHR,
	    0, DDI_PSEUDO, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	tidg_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
tidg_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = tidg_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = 0;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/* ARGSUSED */
static int
tidgopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	dev_t		dev;
	struct ti_tidg *tiptr;
	klwp_id_t lwp;

	ASSERT(q != NULL);

	/* is it already opened */
	if (q->q_ptr) {
		(void) (STRLOG(TIDG_ID, TI_DG_NUM(q->q_ptr), 0, SL_TRACE,
		    "tidgopen: opened already, open succeeded\n"));
		return (0);
	}

	lwp = ttolwp(curthread);
	if (sflag == CLONEOPEN) {
		for (tiptr = ti_tidg; tiptr < &ti_tidg[TIDG_CNT]; tiptr++)
			if (!(tiptr->ti_flags & USED))
				break;
		if (tiptr > &ti_tidg[TIDG_CNT-1]) {
			(void) (STRLOG(TIDG_ID, -1, 0, SL_TRACE|SL_ERROR,
			    "tidgopen: opened failed: couldn't allocate "
			    "ti_tidg data structure for q = %p\n",
			    (void *)q));
			lwp->lwp_error = ENOSPC;
			return (ENOSPC);
		}
		/*
		 * Return new device number, same major, new minor number.
		 */
		*devp = makedevice(getmajor(*devp), (minor_t)TI_DG_NUM(tiptr));
	} else {
		dev = getminor(*devp);
		if (dev > (TIDG_CNT - 1)) {
			lwp->lwp_error = ENODEV;
			return (ENODEV);
		}

		tiptr = &ti_tidg[dev];
	}

	ASSERT(tiptr <= &ti_tidg[TIDG_CNT-1]);

	/* initialize data structure */
	tiptr->ti_state = TS_UNBND;
	tiptr->ti_flags = USED;
	tiptr->ti_rdq = q;
	tiptr->ti_addr = 0;

	/* assign tiptr to queue private pointers */
	q->q_ptr = (caddr_t)tiptr;
	WR(q)->q_ptr = (caddr_t)tiptr;

	qprocson(q);

	(void) (STRLOG(TIDG_ID, TI_DG_NUM(q->q_ptr), 0, SL_TRACE,
	    "tidgopen: open succeeded\n"));

	return (0);
}

/* ARGSUSED */
static int
tidgclose(queue_t *q, int flag, cred_t *crp)
{
	struct ti_tidg *tiptr;
	struct tidg_addr *tmp;

	qprocsoff(q);

	tiptr = (struct ti_tidg *)q->q_ptr;

	ASSERT(tiptr != NULL);

	if (tiptr->ti_addr) {
		for (tmp = tidg_addr; tmp < &tidg_addr[MAXADDR]; tmp++) {
			if (tmp->addr == tiptr->ti_addr) {
				tmp->used = 0;
				break;
			}
		}
	}

	tidg_finished(q);

	tiptr->ti_state = TS_UNBND;
	tiptr->ti_flags = 0;
	tiptr->ti_rdq = 0;

	(void) (STRLOG(TIDG_ID, TI_DG_NUM(q->q_ptr), 0, SL_TRACE,
	    "tidgclose: close succeeded\n"));

	return (0);
}

static int
tidgwput(queue_t *q, mblk_t *mp)
{
	union T_primitives *pptr;
	struct ti_tidg *tiptr;

	ASSERT(q != NULL);

	tiptr = (struct ti_tidg *)q->q_ptr;

	ASSERT(tiptr != NULL);

	(void) (STRLOG(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE,
	    "tidgwput: STRmsg type %x received mp = %p\n",
	    mp->b_datap->db_type, (void *)mp));

	/* switch on message type */
	switch (mp->b_datap->db_type) {

	case M_DATA:
		(void) (STRLOG(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE|SL_ERROR,
		    "tidgwput: bad STRmsg type = %x\n", mp->b_datap->db_type));
		tidg_sendfatal(q, mp);
		return (0);
	case M_PROTO:
	case M_PCPROTO:
		pptr = (union T_primitives *)mp->b_rptr;

		(void) (STRLOG(TIDG_ID, TI_DG_NUM(tiptr), 0, SL_TRACE,
		    "tidgwput: TImsg type = %d received\n", pptr->type));

		if ((mp->b_wptr - mp->b_rptr) < sizeof (int32_t)) {
			(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0,
			    SL_TRACE|SL_ERROR,
			    "tidgwput: msg wptr < rptr for msg = %p\n",
			    (void *)mp);
			tidg_sendfatal(q, mp);
			return (0);
		}

		/* switch on primitive type */
		switch (pptr->type) {
		case T_CAPABILITY_REQ:
			tidg_capability_req(q, mp);
			break;

		case T_INFO_REQ:
			tidg_ireq(q, mp);
			break;

		case T_UNITDATA_REQ:
			if (tiptr->ti_flags & FATAL) {
				freemsg(mp);
				return (0);
			}
			if (NEXTSTATE(TE_UNITDATA_REQ, tiptr->ti_state) == NR) {
				(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0,
				    SL_TRACE|SL_ERROR,
				    "tidgwput: bad state for TE_UNITDATA_REQ "
				    "in state %d\n", tiptr->ti_state);
				tidg_sendfatal(q, mp);
				return (0);
			}
			(void) putq(q, mp);
			break;

		case T_UNBIND_REQ:
		case O_T_BIND_REQ:
		case T_BIND_REQ:
		case T_SVR4_OPTMGMT_REQ:
		case T_OPTMGMT_REQ:
			tidg_checkmsg(q, mp);
			break;

		default:
			(void) strlog(TIDG_ID, TI_DG_NUM(tiptr), 0,
			    SL_TRACE|SL_ERROR,
			    "tidgwput: bad prim type = %d\n", pptr->type);
			tidg_sendfatal(q, mp);
			break;
		}
		return (0);

	case M_IOCTL:
		mp->b_datap->db_type = M_IOCNAK;
		qreply(q, mp);
		return (0);
	case M_FLUSH:
		if (*mp->b_rptr & FLUSHW)
			flushq(q, FLUSHDATA);
		if (!(*mp->b_rptr & FLUSHR))
			freemsg(mp);
		else {
			*mp->b_rptr &= ~FLUSHW;
			flushq(OTHERQ(q), FLUSHDATA);
			qreply(q, mp);
		}
		return (0);

	default:
		freemsg(mp);
		return (0);
	}
}

static int
tidgwsrv(queue_t *q)
{
	mblk_t *mp;

	while ((mp = getq(q)) != NULL) {
		/*
		 * The only thing on the write queue can be
		 * M_PROTO containing T_UNITDATA_REQ
		 */
		if (!do_udata(q, mp))
			return (0);
	}
	return (0);
}

static int
tidgrsrv(queue_t *q)
{
	struct ti_tidg *tiptr;

	tiptr = (struct ti_tidg *)q->q_ptr;

	ASSERT(tiptr->ti_backwq);
	if (tiptr->ti_state == TS_IDLE)
		qenable(tiptr->ti_backwq);
	return (0);
}

static struct module_info tidg_mod_info = {
	TIDG_ID, "tidg", 0, TIDU_SIZE, 4 * TIDU_SIZE, TIDU_SIZE
};

static struct qinit tidgrinit = {
	NULL, tidgrsrv, tidgopen, tidgclose, nulldev, &tidg_mod_info, NULL
};

static struct qinit tidgwinit = {
	tidgwput, tidgwsrv, tidgopen, tidgclose, nulldev, &tidg_mod_info, NULL
};

struct streamtab tidinfo = {
	&tidgrinit, &tidgwinit, NULL, NULL
};

DDI_DEFINE_STREAM_OPS(tidg_ops, nulldev, nulldev, tidg_attach, nodev, nodev,
	tidg_info, D_NEW | D_MP | D_MTPERMOD, &tidinfo,
	ddi_quiesce_not_supported);

static struct modldrv modldrv = {
	&mod_driverops,
	"SVVS TIDG Driver",
	&tidg_ops
};

static struct modlinkage modlinkage = {
	MODREV_1, &modldrv, NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
