/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Loopback driver
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/tmux.h>
#include <sys/stat.h>

#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

static dev_info_t *tmx_dip;

#define	TMXCNT	4
#define	TMXLCNT	3

struct tmx tmx_tmx[TMXCNT];
struct tmxl tmx_low[TMXLCNT];

/* ARGSUSED */
static int
tmx_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (ddi_create_minor_node(devi, "tmux", S_IFCHR,
	    0, DDI_PSEUDO, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	tmx_dip = devi;
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
tmx_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = tmx_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = 0;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/* ARGSUSED */
static int
tmxopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	dev_t	dev;
	mblk_t *bp;
	struct stroptions *sop;
	struct tmx *tp;

	dev = getminor(*devp);
	if (sflag == CLONEOPEN) {
		for (dev = 0; dev < TMXCNT; dev++)
			if (!(tmx_tmx[dev].tmx_state & TMXOPEN))
				break;
		/*
		 * Return new device number, same major, new minor number.
		 */
		*devp = makedevice(getmajor(*devp), dev);
	}
	if (dev >= TMXCNT)
		return (ENXIO);
	if (!(bp = allocb(sizeof (struct stroptions), BPRI_MED)))
		return (ENXIO);
	tp = &tmx_tmx[dev];
	tp->tmx_state |= TMXOPEN;
	tp->tmx_rdq = q;
	q->q_ptr = (caddr_t)tp;
	WR(q)->q_ptr = (caddr_t)tp;
	qprocson(q);
	bp->b_datap->db_type = M_SETOPTS;
	bp->b_wptr += sizeof (struct stroptions);
	sop = (struct stroptions *)bp->b_rptr;
	sop->so_flags = SO_LOWAT | SO_HIWAT;
	sop->so_lowat = 256;
	sop->so_hiwat = 512;
	putnext(q, bp);
	return (0);
}

/* ARGSUSED */
static int
tmxclose(queue_t *q, int flag, cred_t *crp)
{
	int i;

	qprocsoff(q);
	((struct tmx *)(q->q_ptr))->tmx_state &= ~TMXOPEN;
	((struct tmx *)(q->q_ptr))->tmx_rdq = NULL;
	q->q_ptr = NULL;
	for (i = 0; i < TMXLCNT; i++) {
		if (tmx_low[i].ctlq == WR(q) &&
		    !(tmx_low[i].ltype&TMXPLINK)) {
			tmx_low[i].muxq = tmx_low[i].ctlq = NULL;
			tmx_low[i].muxid = 0;
		}
	}
	return (0);
}

/*
 * Upper side read service procedure.  No messages are ever placed on
 * the read queue here, this just back-enables all of the lower side
 * read service procedures.
 */
/* ARGSUSED */
static int
tmxursrv(queue_t *q)
{
	int i;

	for (i = 0; i < TMXLCNT; i++)
		if (tmx_low[i].muxq)
			qenable(RD(tmx_low[i].muxq));
	return (0);
}

static int
tmxuwsrv(queue_t *q)
{
	mblk_t *mp;
	queue_t *qp;
	struct iocblk *ioc;
	struct linkblk *lp;
	int i;

	while (mp = getq(q)) {
		switch (mp->b_datap->db_type) {
		case M_FLUSH:
			if (*mp->b_rptr & FLUSHW) {
				*mp->b_rptr &= ~FLUSHW;
				flushq(q, FLUSHALL);
			}
			if (*mp->b_rptr & FLUSHR)
				qreply(q, mp);
			else
				freemsg(mp);
			continue;

		case M_DATA:
		case M_PROTO:
		case M_PCPROTO:
			/*
			 * The first byte of the first message block
			 * is the address of the lower stream to put
			 * the message out on.
			 */
			if (MBLKL(mp) < sizeof (char)) {
				merror(q, mp, EIO);
				continue;
			}

			i = *mp->b_rptr;
			if ((i >= TMXLCNT) || !tmx_low[i].muxq) {
				merror(q, mp, EIO);
				continue;
			}
			qp = tmx_low[i].muxq;
			if (!canputnext(qp) && (mp->b_datap->db_type < QPCTL)) {
				(void) putbq(q, mp);
				return (0);
			}
			mp->b_rptr++;
			putnext(qp, mp);
			continue;

		case M_IOCTL:
			ASSERT(MBLKL(mp) == sizeof (struct iocblk));

			ioc = (struct iocblk *)mp->b_rptr;
			switch (ioc->ioc_cmd) {
			case I_PLINK:
			case I_LINK:
				ASSERT(MBLKL(mp->b_cont) ==
				    sizeof (struct linkblk));

				lp = (struct linkblk *)mp->b_cont->b_rptr;
				for (i = 0; i < TMXLCNT; i++)
					if (tmx_low[i].muxq == NULL)
						break;
				if (i >= TMXLCNT) {
					miocnak(q, mp, 0, 0);
					continue;
				}
				if (ioc->ioc_cmd == I_PLINK) {
					tmx_low[i].ltype |= TMXPLINK;
				}
				tmx_low[i].muxq = lp->l_qbot;
				tmx_low[i].muxid = lp->l_index;
				tmx_low[i].ctlq = q;
				miocack(q, mp, 0, 0);
				continue;

			case I_PUNLINK:
			case I_UNLINK:
				ASSERT(MBLKL(mp->b_cont) ==
				    sizeof (struct linkblk));

				lp = (struct linkblk *)mp->b_cont->b_rptr;
				for (i = 0; i < TMXLCNT; i++)
					if (tmx_low[i].muxq == lp->l_qbot)
						break;
				if (i >= TMXLCNT) {
					miocnak(q, mp, 0, 0);
					continue;
				}

				if (ioc->ioc_cmd == I_UNLINK &&
				    (tmx_low[i].ltype & TMXPLINK)) {
					miocnak(q, mp, 0, 0);
					continue;
				}

				if (ioc->ioc_cmd == I_PUNLINK &&
				    !(tmx_low[i].ltype & TMXPLINK)) {
					miocnak(q, mp, 0, 0);
					continue;
				}

				tmx_low[i].muxq = NULL;
				tmx_low[i].ctlq = NULL;
				tmx_low[i].muxid = 0;
				if (ioc->ioc_cmd == I_PUNLINK)
					tmx_low[i].ltype &= ~TMXPLINK;

				miocack(q, mp, 0, 0);
				continue;

			default:
				miocnak(q, mp, 0, 0);
				continue;
			}

		default:
			freemsg(mp);
			continue;
		}
	}
	return (0);
}

static int
tmxlrsrv(queue_t *q)
{
	mblk_t *mp;
	queue_t *qp;
	int i;

	while (mp = getq(q)) {
		switch (mp->b_datap->db_type) {
		case M_FLUSH:
			if (*mp->b_rptr & FLUSHR) {
				*mp->b_rptr &= ~FLUSHR;
				flushq(q, FLUSHALL);
			}
			if (*mp->b_rptr & FLUSHW)
				qreply(q, mp);
			else
				freemsg(mp);
			continue;

		case M_DATA:
		case M_PROTO:
		case M_PCPROTO:
			/*
			 * The first byte of the first message block
			 * is the address of the upper stream to put
			 * the message out on.
			 */
			if (MBLKL(mp) < sizeof (char)) {
				freemsg(mp);
				continue;
			}

			i = *mp->b_rptr;
			if (i >= TMXCNT) {
				freemsg(mp);
				continue;
			}
			qp = tmx_tmx[i].tmx_rdq;
			if (!canputnext(qp) && (mp->b_datap->db_type < QPCTL)) {
				(void) putbq(q, mp);
				return (0);
			}
			mp->b_rptr++;
			putnext(qp, mp);
			continue;

		default:
			freemsg(mp);
			continue;
		}
	}
	return (0);
}

/*
 * Lower side write service procedure.  No messages are ever placed on
 * the write queue here, this just back-enables all of the upper side
 * write service procedures.
 */
/* ARGSUSED */
static int
tmxlwsrv(queue_t *q)
{
	int i;
	for (i = 0; i < TMXCNT; i++)
		if (tmx_tmx[i].tmx_rdq)
			qenable(WR(tmx_tmx[i].tmx_rdq));
	return (0);
}

static struct module_info tmxm_info = { 63, "tmx", 0, 256, 512, 256 };

static struct qinit tmxurinit = {
	NULL, tmxursrv, tmxopen, tmxclose, NULL, &tmxm_info, NULL
};

static struct qinit tmxuwinit = {
	putq, tmxuwsrv, NULL, NULL, NULL, &tmxm_info, NULL
};

static struct qinit tmxlrinit = {
	putq, tmxlrsrv, NULL, NULL, NULL, &tmxm_info, NULL
};

static struct qinit tmxlwinit = {
	NULL, tmxlwsrv, NULL, NULL, NULL, &tmxm_info, NULL
};

struct streamtab tmxinfo = {
	&tmxurinit, &tmxuwinit, &tmxlrinit, &tmxlwinit
};

DDI_DEFINE_STREAM_OPS(tmux_ops, nulldev, nulldev, tmx_attach,
	nodev, nodev, tmx_info, D_MP | D_MTPERMOD, &tmxinfo,
	ddi_quiesce_not_supported);

static struct modldrv modldrv = {
	&mod_driverops,
	"SVVS TMUX Driver",
	&tmux_ops
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
