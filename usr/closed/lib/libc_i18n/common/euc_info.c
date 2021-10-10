/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "lint.h"
#include <sys/localedef.h>

/*
 * These two functions are project private functions of CSI project.
 * They should be used cautiously when dealing with CSIed code.
 */

int
_is_euc_fc(void)
{
	return (__lc_charmap->cm_fc_type == _FC_EUC);
}

int
_is_euc_pc(void)
{
	return (__lc_charmap->cm_pc_type == _PC_EUC);
}
