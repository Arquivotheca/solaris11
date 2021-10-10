/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Loopback driver
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/svvslo.h>
#include <sys/stat.h>

#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

static dev_info_t *lo_dip;		/* private copy of devinfo pointer */

#define	LO_CNT	4

struct lo lo_lo[LO_CNT];

/*ARGSUSED*/
static int
lo_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	int i;
	char name[8];

	for (i = 0; i < LO_CNT; i++) {
		(void) sprintf(name, "svvslo%d", i);
		if (ddi_create_minor_node(devi, name, S_IFCHR,
		    i, DDI_PSEUDO, NULL) == DDI_FAILURE) {
			ddi_remove_minor_node(devi, NULL);
			return (DDI_FAILURE);
		}
	}
	lo_dip = devi;
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
lo_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = lo_dip;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = 0;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
loopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	struct lo *lp;
	mblk_t *bp;
	struct stroptions *sop;
	dev_t	dev;

	if (q->q_ptr)
		return (0);

	dev = getminor(*devp);
	if (sflag == CLONEOPEN) {
		for (dev = 0; dev < LO_CNT; dev++)
			if (lo_lo[dev].lo_rdq == NULL)
				break;
	}
	if (dev >= LO_CNT)
		return (ENXIO);
	lp = &lo_lo[dev];

	if (lp->lo_rdq != NULL)
		return (ENXIO);

	if ((bp = allocb(sizeof (struct stroptions), BPRI_MED)) == NULL)
		return (ENXIO);

	lp->lo_minval = 0;
	lp->lo_rdq = q;
	q->q_ptr = WR(q)->q_ptr = lp;
	qprocson(q);

	/*
	 * Set up the correct stream head flow control parameters
	 */
	bp->b_datap->db_type = M_SETOPTS;
	bp->b_wptr += sizeof (struct stroptions);
	sop = (struct stroptions *)bp->b_rptr;
	sop->so_flags = SO_HIWAT | SO_LOWAT;
	sop->so_hiwat = 512;
	sop->so_lowat = 256;
	putnext(q, bp);
	/*
	 * Return new device number, same major, new minor number.
	 */
	if (sflag == CLONEOPEN)
		*devp = makedevice(getmajor(*devp), dev);

#ifdef	_ILP32
	lp->lo_fdinsertval = (t_uscalar_t)q;
#else
	lp->lo_fdinsertval = (t_uscalar_t)getminor(*devp);
#endif	/* _ILP32 */

	return (0);
}

/*ARGSUSED*/
static int
loclose(queue_t *q, int flag, cred_t *crp)
{
	struct lo *lp = (struct lo *)q->q_ptr;

	qprocsoff(q);
	lp->lo_rdq = NULL;
	q->q_ptr = WR(q)->q_ptr = NULL;

	return (0);
}

static void
loioctl(queue_t *q, mblk_t *bp)
{
	mblk_t *tmp;
	struct lo *lp;
	struct iocblk *iocbp;

	/*
	 * This is the start of the main block of code.  Each particular
	 * ioctl has a special function for testing out the streams
	 * mechanism.
	 */

	iocbp = (struct iocblk *)bp->b_rptr;
	lp = (struct lo *)q->q_ptr;

	switch (iocbp->ioc_cmd) {

	case I_SETRANGE:
		bp->b_datap->db_type = M_IOCACK;
		qreply(q, bp);
		lp->lo_minval = q->q_minpsz;
		(void) strqset(q, QMINPSZ, 0, 64);
		return;

	case I_UNSETRANGE:
		bp->b_datap->db_type = M_IOCACK;
		qreply(q, bp);
		(void) strqset(q, QMINPSZ, 0, lp->lo_minval);
		return;

	case I_QPTR:
		/*
		 * Send queue pointer argument back as return value.
		 */
		iocbp->ioc_rval = (int)lp->lo_fdinsertval;
		iocbp->ioc_count = 0;

		bp->b_datap->db_type = M_IOCACK;
		qreply(q, bp);
		return;

	case I_SETHANG:
		/*
		 * Send ACK followed by M_HANGUP upstream.
		 */
		bp->b_datap->db_type = M_IOCACK;
		qreply(q, bp);
		(void) putnextctl(RD(q), M_HANGUP);
		return;

	case I_SETERR:
		/*
		 * Send ACK followed by M_ERROR upstream - value is
		 * sent in second message block.
		 */
		tmp = unlinkb(bp);
		if (tmp == NULL) {
			miocnak(q, bp, 0, EINVAL);
			return;
		}

		bp->b_datap->db_type = M_IOCACK;
		iocbp->ioc_count = 0;
		qreply(q, bp);

		tmp->b_datap->db_type = M_ERROR;
		qreply(q, tmp);
		return;

	default:
		/*
		 * NAK anything else.
		 */
		miocnak(q, bp, 0, EINVAL);
		return;
	}
}

/*
 * Service routine takes messages off write queue and
 * sends them back up the read queue, processing them
 * along the way.
 */
static int
losrv(queue_t *q)
{
	mblk_t *bp;

	/* if losrv called from read side set q to write side */
	q = ((q)->q_flag & QREADR ? WR(q) : q);

	while ((bp = getq(q)) != NULL) {
		/* if upstream queue full, process only priority messages */
		if ((bp->b_datap->db_type) < QPCTL && !canputnext(RD(q))) {
			(void) putbq(q, bp);
			return (0);
		}

		switch (bp->b_datap->db_type) {

		case M_IOCTL:
			loioctl(q, bp);
			break;

		case M_DATA:
		case M_PROTO:
		case M_PCPROTO:
			qreply(q, bp);
			break;

		case M_CTL:
			freemsg(bp);
			break;

		case M_FLUSH:
			if (*bp->b_rptr & FLUSHW) {
				flushq(q, FLUSHALL);
				*bp->b_rptr &= ~FLUSHW;
			}
			if (*bp->b_rptr & FLUSHR)
				qreply(q, bp);
			else
				freemsg(bp);
			break;

		default:
			freemsg(bp);
			break;
		}
	}
	return (0);
}

static struct module_info lom_info = { 40, "svvslo", 0, 256, 512, 256 };

static struct qinit lorinit =
	{ NULL, losrv, loopen, loclose, NULL, &lom_info, NULL };

static struct qinit lowinit =
	{ putq, losrv, loopen, loclose, NULL, &lom_info, NULL };

struct streamtab loinfo = { &lorinit, &lowinit, NULL, NULL };

DDI_DEFINE_STREAM_OPS(lo_ops, nulldev, nulldev, lo_attach,
	nodev, nodev, lo_info, D_MP | D_MTPERMOD, &loinfo,
	ddi_quiesce_not_supported);

static struct modldrv modldrv = {
	&mod_driverops,
	"SVVS Loopback Driver",
	&lo_ops
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
