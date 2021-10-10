/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Serrano (US-IIIi+) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_ch.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_ja.h>
#include <sys/memtestio_sr.h>
#include "mtst.h"

/*
 * Serrano specific routines located in this file.
 */
void		sr_init(mdata_t *);

/*
 * Jalapeno routines referenced by this file.
 */
extern	int	ja_pre_test(mdata_t *);

/*
 * These Serrano errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t serrano_cmds[] = {

	/*
	 * E$ tag (ETx) errors.
	 */

	"kdetu",		do_k_err,		SR_KD_ETU,
	MASK(0xe1cffff),	BIT(1)|BIT(0),
	MASK(0x3f0000000ULL),	BIT(29)|BIT(28),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag uncorrectable error.",

	"kietu",		do_k_err,		SR_KI_ETU,
	MASK(0xe1cffff),	BIT(2)|BIT(1),
	MASK(0x3f0000000ULL),	BIT(30)|BIT(29),
	OFFSET(8),		OFFSET(0),
	"Cause a kern instr ecache tag uncorrectable error.",

	"udetu",		do_u_err,		SR_UD_ETU,
	MASK(0xe1cffff),	BIT(3)|BIT(2),
	MASK(0x3f0000000ULL),	BIT(31)|BIT(30),
	OFFSET(8),		OFFSET(0),
	"Cause a user data ecache tag uncorrectable error.",

	"uietu",		do_u_err,		SR_UI_ETU,
	MASK(0xe1cffff),	BIT(4)|BIT(3),
	MASK(0x3f0000000ULL),	BIT(32)|BIT(31),
	OFFSET(8),		OFFSET(0),
	"Cause a user instr ecache tag uncorrectable error.",

	"kdetc",		do_k_err,		SR_KD_ETC,
	MASK(0xe1cffff),	BIT(5),
	MASK(0x3f0000000ULL),	BIT(33),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag correctable error.",

	"kdetctl1",		do_k_err,		SR_KD_ETCTL1,
	MASK(0xe1cffff),	BIT(6),
	MASK(0x3f0000000ULL),	BIT(28),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag correctable error at\n"
	"\t\t\ttrap level 1.",

	"kietc",		do_k_err,		SR_KI_ETC,
	MASK(0xe1cffff),	BIT(7),
	MASK(0x3f0000000ULL),	BIT(29),
	OFFSET(8),		OFFSET(0),
	"Cause a kern instr ecache tag correctable error.",

	"kietctl1",		do_k_err,		SR_KI_ETCTL1,
	MASK(0xe1cffff),	BIT(8),
	MASK(0x3f0000000ULL),	BIT(30),
	OFFSET(8),		OFFSET(0),
	"Cause a kern instr ecache tag correctable error at\n"
	"\t\t\ttrap level 1.",

	"udetc",		do_u_err,		SR_UD_ETC,
	MASK(0xe1cffff),	BIT(9),
	MASK(0x3f0000000ULL),	BIT(31),
	OFFSET(8),		OFFSET(0),
	"Cause a user data ecache tag correctable error.",

	"uietc",		do_u_err,		SR_UI_ETC,
	MASK(0xe1cffff),	BIT(10),
	MASK(0x3f0000000ULL),	BIT(32),
	OFFSET(8),		OFFSET(0),
	"Cause a user instr ecache tag correctable error.",

	"kdeti",		do_k_err,		SR_KD_ETI,
	MASK(0xe1cffff),	BIT(11),
	MASK(0x3f0000000ULL),	BIT(33),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag intermittent error.",

	"kdets",		do_k_err,		SR_KD_ETS,
	MASK(0xe1cffff),	BIT(12),
	MASK(0x3f0000000ULL),	BIT(28),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data ecache tag stuck-at error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

static cmd_t *commands[] = {
	serrano_cmds,
	us3i_generic_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static	opsvec_t operations = {
	gen_flushall_l2,		/* flush entire L2$ */
	ja_pre_test,			/* pre-test routine */
	NULL				/* no post-test routine */
};

void
sr_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}
