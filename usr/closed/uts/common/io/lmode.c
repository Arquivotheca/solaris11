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
 * It returns its own error return of 255.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/signal.h>
#include <sys/klwp.h>
#include <sys/thread.h>
#include <sys/ddi.h>

#include <sys/conf.h>
#include <sys/modctl.h>

/*ARGSUSED*/
static int
lmodeopen(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *crp)
{
	ttolwp(curthread)->lwp_error = (char)255;
	return (255);
}

/*ARGSUSED*/
static int
lmodeclose(queue_t *q, int flag, cred_t *crp)
{
	qprocsoff(q);
	return (0);
}

/*
 * Use same put procedure for write and read queues.
 */
static int
lmodeput(queue_t *q, mblk_t *bp)
{
	putnext(q, bp);
	return (0);
}

static struct module_info lmode_info = { 1003, "lmode", 0, 256, 512, 256 };

static struct qinit lmoderinit =
	{ lmodeput, NULL, lmodeopen, lmodeclose, NULL, &lmode_info, NULL };

static struct qinit lmodewinit =
	{ lmodeput, NULL, NULL, NULL, NULL, &lmode_info, NULL};

struct streamtab lmeinfo = { &lmoderinit, &lmodewinit };

static struct fmodsw fsw = {
	"lmode",
	&lmeinfo,
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
