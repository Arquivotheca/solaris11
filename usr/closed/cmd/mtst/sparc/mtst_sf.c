/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains support for US-I and US-II family of processors.
 * This includes Spitfire (US-I), Blackbird (US-IIs), Sabre (US-IIi),
 * and Hummingbird (US-IIe).
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_sf.h>
#include "mtst.h"

/*
 * These US-II errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t us2_generic_cmds[] = {

	/*
	 * Other system bus errors.
	 */

	"kdbe",			do_k_err,		SF_KD_BE,
	NULL, 			NULL,
	NULL, 			NULL,
	NULL, 			NULL,
	"Cause a kern data bus error.",

	"kpeekbe",		do_k_err,		SF_KD_BEPEEK,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data ddi_peek() bus error.",

	"udbe",			do_notimp,		SF_UD_BE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a user data bus error.",

	"kdto",			do_notimp,		SF_KD_TO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data timeout error.",

	"udto",			do_notimp,		SF_UD_TO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a user data timeout error.",

	"kivu",			do_notimp,		SF_K_IVU,
	MASK(ALL_BITS),		NULL,
	MASK(0x1ff),		NULL,
	NULL,			NULL,
	"Cause a kernel interrupt vector uncorrectable error.",

	"kivc",			do_notimp,		SF_K_IVC,
	MASK(ALL_BITS),		NULL,
	MASK(0x1ff),		NULL,
	NULL,			NULL,
	"Cause a kernel interrupt vector correctable error.",

	/*
	 * E$ data (EDP) errors.
	 */

	"kdedp",		do_k_err,		SF_KD_EDP,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1ff),		BIT(0),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache parity error.",

	"kucopyin",		do_k_err,		SF_KU_EDPCOPYIN,
	MASK(ALL_BITS),		BIT(1),
	MASK(0x1ff), 		BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a kern/user copyin ecache parity error.",

	"copyinedp",		do_k_err,		SF_KU_EDPCOPYIN,
	MASK(ALL_BITS),		BIT(1),
	MASK(0x1ff), 		BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a kern/user copyin ecache parity error.",

	"kdedptl1",		do_k_err,		SF_KD_EDPTL1,
	MASK(ALL_BITS),		BIT(2),
	MASK(0x1ff), 		BIT(2),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache parity error at trap level 1.",

	"kiedp",		do_k_err,		SF_KI_EDP,
	MASK(ALL_BITS),		BIT(4),
	MASK(0x1ff),		BIT(3),
	OFFSET(32),		NULL,
	"Cause a kern instr ecache parity error.",

	"kiedptl1",		do_k_err,		SF_KI_EDPTL1,
	MASK(ALL_BITS),		BIT(5),
	MASK(0x1ff), 		BIT(4),
	OFFSET(32),		NULL,
	"Cause a kern instr ecache parity error at trap level 1.",

	"udedp",		do_u_err,		SF_UD_EDP,
	MASK(ALL_BITS),		BIT(7),
	MASK(0x1ff), 		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user data ecache parity error.",

	"uiedp",		do_u_err,		SF_UI_EDP,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1ff), 		BIT(6),
	OFFSET(0),		NULL,
	"Cause a user instr ecache parity error.",

	"obpdedp",		do_k_err,		SF_OBPD_EDP,
	MASK(ALL_BITS),		BIT(1),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause an OBP data ecache parity error.",

	/*
	 * E$ write-back errors.
	 */

	"kdwp",			do_k_err,		SF_KD_WP,
	MASK(ALL_BITS),		BIT(3),
	MASK(0x1ff), 		BIT(0),
	OFFSET(0),		NULL,
	"Cause a kern data ecache write-back parity error.",

	"kiwp",			do_k_err,		SF_KI_WP,
	MASK(ALL_BITS),		BIT(4),
	MASK(0x1ff), 		BIT(1),
	OFFSET(0),		NULL,
	"Cause a kern instr ecache write-back parity error.",

	"udwp",			do_ud_wb_err,		SF_UD_WP,
	MASK(ALL_BITS),		BIT(5),
	MASK(0x1ff), 		BIT(2),
	OFFSET(0),		NULL,
	"Cause a user data ecache write-back parity error.",

	"uiwp",			do_notimp,		SF_UI_WP,
	MASK(ALL_BITS),		BIT(6),
	MASK(0x1ff), 		BIT(3),
	NULL,			NULL,
	"Cause a user instr ecache write-back parity error.",

	/*
	 * E$ copy-back errors.
	 */

	"kcp",			do_k_err,		SF_KD_CP,
	MASK(ALL_BITS),		BIT(7),
	MASK(0x1ff), 		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data ecache copy-back parity error.",

	"ucp",			do_u_cp_err,		SF_UD_CP,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1ff), 		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user data ecache copy-back parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

/*
 * Becuase the kdetp test needs to set a fault in the parity bit and that
 * each USII chip has their parity bits in a different location the kdetp
 * command structure will need to be different for each chip.
 */
cmd_t spitfire_cmds[] = {
	/*
	 * E$ tag (ETP) errors.
	 */

	"kdetp",		do_k_err,		SF_KD_ETP,
	MASK(0x1ffffff),	BIT(0),
	MASK(0x1e000000), 	BIT(25),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL
};

cmd_t sabre_cmds[] = {
	/*
	 * E$ tag (ETP) errors.
	 */

	"kdetp",		do_k_err,		SF_KD_ETP,
	MASK(0xcfff),		BIT(0),
	MASK(0x30000), 		BIT(16),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL
};

cmd_t hummingbird_cmds[] = {
	/*
	 * E$ tag (ETP) errors.
	 */

	"kdetp",		do_k_err,		SF_KD_ETP,
	MASK(0x3ffff),		BIT(0),
	MASK(0x300000), 	BIT(20),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag parity error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL
};

static cmd_t *sf_commands[] = {
	spitfire_cmds,
	us2_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static cmd_t *sa_commands[] = {
	sabre_cmds,
	us2_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static cmd_t *hb_commands[] = {
	hummingbird_cmds,
	us2_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static	opsvec_t operations = {
	gen_flushall_l2,		/* flush entire L2$ */
};

void
sf_init(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;

	mdatap->m_opvp = &operations;

	switch (CPU_IMPL(cip->c_cpuver)) {
	case SPITFIRE_IMPL:
	case BLACKBIRD_IMPL:
		mdatap->m_cmdpp = sf_commands;
		break;
	case SABRE_IMPL:
		mdatap->m_cmdpp = sa_commands;
		break;
	case HUMMBRD_IMPL:
		mdatap->m_cmdpp = hb_commands;
		break;
	default:
		mdatap->m_cmdpp = sf_commands;
	}
}
