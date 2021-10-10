/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Jaguar (US-IV) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_jg.h>
#include "mtst.h"

int	jg_pre_test(mdata_t *);

/*
 * These Jaguar errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t jaguar_cmds[] = {
	/*
	 * Bus errors.
	 */
	"kdppe",		do_k_err,		JG_KD_PPE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a system bus kernel data parity error.",

	"kddpe",		do_k_err,		JG_KD_DPE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a system bus kernel LSB data parity error.",

	"kdsaf",		do_k_err,		JG_KD_SAF,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a system bus address parity kernel data error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

static cmd_t *commands[] = {
	jaguar_cmds,
	cheetahp_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static	opsvec_t operations = {
	gen_flushall_l2,		/* flush entire L2$ */
	jg_pre_test,
};

void
jg_init(mdata_t *mdatap)
{
	mdatap->m_cmdpp = commands;
	mdatap->m_opvp = &operations;
}

int
jg_pre_test(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	switch (IOC_COMMAND(iocp)) {
	/*
	 * All these tests require 2 threads.
	 */
	case CHP_KD_DTHPEL:
	case CHP_UD_ETHCE:
	case CHP_UI_ETHCE:
	case CHP_UD_ETHUE:
	case CHP_UI_ETHUE:
		iocp->ioc_nthreads = 2;
		break;
	}

	return (0);
}
