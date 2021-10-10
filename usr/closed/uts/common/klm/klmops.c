/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/modctl.h>

static struct modlmisc modlmisc = {
	&mod_miscops, "lock mgr calls"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

char _depends_on[] = "strmod/rpcmod fs/nfs misc/klmmod";

int
_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

#ifdef __lock_lint

/*
 * Stub function for warlock only - this is never compiled or called.
 */
void
klmops_null()
{}

#endif /* __lock_lint */
