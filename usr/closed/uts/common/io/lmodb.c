/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Bottom loopback module (between driver and top module)
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/ddi.h>

#include <sys/conf.h>
#include <sys/modctl.h>

/*ARGSUSED1*/
static int
lmodbopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	qprocson(q);
	return (0);
}

/*ARGSUSED1*/
static int
lmodbclose(queue_t *q, int flag, cred_t *crp)
{
	qprocsoff(q);
	return (0);
}

static int
lmodbput(queue_t *q, mblk_t *bp)
{
	switch (bp->b_datap->db_type) {
		case M_FLUSH:
			if (q->q_flag & QREADR) {
				if (*bp->b_rptr & FLUSHR)
					flushq(q, FLUSHDATA);
			} else if (*bp->b_rptr & FLUSHW)
				flushq(q, FLUSHDATA);
			putnext(q, bp);
			break;
		default:
			if (canputnext(q))
				putnext(q, bp);
			else
				freemsg(bp);
			break;
	}
	return (0);
}

static struct module_info lmodb_info = { 1003, "lmodb", 0, 256, 512, 256 };

static struct qinit lmodbrinit =
	{ lmodbput, NULL, lmodbopen, lmodbclose, NULL, &lmodb_info, NULL };

static struct qinit lmodbwinit =
	{ lmodbput, NULL, NULL, NULL, NULL, &lmodb_info, NULL};

struct streamtab lmbinfo = { &lmodbrinit, &lmodbwinit };

static struct fmodsw fsw = {
	"lmodb",
	&lmbinfo,
	D_NEW | D_MP
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "SVVS bottom loopback module", &fsw
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlstrmod, NULL
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
