/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Top Loopback Module (between stream head and bottom module)
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
lmodtopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	qprocson(q);
	return (0);
}

/*ARGSUSED1*/
static int
lmodtclose(queue_t *q, int flag, cred_t *crp)
{
	qprocsoff(q);
	return (0);
}

static int
lmodtput(queue_t *q, mblk_t *bp)
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

static struct module_info lmodtm_info = { 1004, "lmodt", 0, 256, 512, 256 };

static struct qinit lmodtrinit =
	{ lmodtput, NULL, lmodtopen, lmodtclose, NULL, &lmodtm_info, NULL };

static struct qinit lmodtwinit =
	{ lmodtput, NULL, NULL, NULL, NULL, &lmodtm_info, NULL };

struct streamtab lmtinfo = { &lmodtrinit, &lmodtwinit };

static struct fmodsw fsw = {
	"lmodt",
	&lmtinfo,
	D_NEW | D_MP
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "SVVS top loopback mod", &fsw
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
