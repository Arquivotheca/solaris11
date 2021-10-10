/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


/*
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Bottom loopback module (between driver and top module)
 * This module just returns an error for testing purposes.
 * It returns ENXIO.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/ddi.h>

#include <sys/conf.h>
#include <sys/modctl.h>

/* ARGSUSED */
static int
lmodropen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	return (ENXIO);
}

/* ARGSUSED */
static int
lmodrclose(queue_t *q, int flag, cred_t *crp)
{
	qprocsoff(q);
	return (0);
}

/*
 * Use same put procedure for write and read queues.
 */
static int
lmodrput(queue_t *q, mblk_t *bp)
{
	putnext(q, bp);
	return (0);
}

static struct module_info lmodr_info = { 1003, "lmodr", 0, 256, 512, 256 };

static struct qinit lmodrrinit =
	{ lmodrput, NULL, lmodropen, lmodrclose, NULL, &lmodr_info, NULL };

static struct qinit lmodrwinit =
	{ lmodrput, NULL, NULL, NULL, NULL, &lmodr_info, NULL};

struct streamtab lmrinfo = { &lmodrrinit, &lmodrwinit };

static struct fmodsw fsw = {
	"lmodr",
	&lmrinfo,
	D_NEW | D_MP
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, "SVVS bottom loopback error mod", &fsw
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
