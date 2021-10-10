/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Cheetah (US-III) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_ch.h>
#include "mtst.h"

/*
 * These US3 generic and Cheetah errors are grouped according to the
 * definitions in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t us3_generic_cmds[] = {

	/*
	 * System bus memory errors caused by prefetch.
	 */

	"kdcepr",		do_k_err,		CH_KD_CEPR,
	MASK(0xf),		BIT(0),
	MASK(0xf),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw correctable memory error due to\n"
	"\t\t\ta prefetch.",

	"kduepr",		do_k_err,		CH_KD_UEPR,
	MASK(0xf),		BIT(1)|BIT(0),
	MASK(0xf),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw uncorrectable memory error due to\n"
	"\t\t\ta prefetch.",

	/*
	 * E$ data (UCx) errors
	 */

	"kducu",		do_k_err,		CH_KD_UCU,
	MASK(ALL_BITS),		BIT(18)|BIT(17),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data sw uncorrectable ecache error.",

	"copyinucu",		do_k_err,		CH_KU_UCUCOPYIN,
	MASK(ALL_BITS),		BIT(19)|BIT(18),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a kern/user copyin uncorrectable ecache error.",

	"kducutl1",		do_k_err,		CH_KD_UCUTL1,
	MASK(ALL_BITS),		BIT(20)|BIT(19),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data uncorrectable ecache error at\n"
	"\t\t\ttrap level 1.",

	"kdoucu",		do_notimp,		CH_KD_OUCU,
	MASK(ALL_BITS),		BIT(21)|BIT(20),
	MASK(0x1ff),		BIT(4)|BIT(3),
	NULL,			NULL,
	"Cause a kern data sw uncorrectable ecache error\n"
	"\t\t\tthat is orphaned.",

	"kiucu",		do_k_err,		CH_KI_UCU,
	MASK(ALL_BITS),		BIT(22)|BIT(21),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(32),		NULL,
	"Cause a kern inst sw uncorrectable ecache error.",

	"kiucutl1",		do_k_err,		CH_KI_UCUTL1,
	MASK(ALL_BITS),		BIT(23)|BIT(22),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(32),		NULL,
	"Cause a kern inst uncorrectable ecache error\n"
	"\t\t\tat trap level 1.",

	"kioucu",		do_k_err,		CH_KI_OUCU,
	MASK(ALL_BITS),		BIT(24)|BIT(23),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(32),		OFFSET(0),
	"Cause a kern inst sw uncorrectable ecache error\n"
	"\t\t\tthat is orphaned.",

	"uducu",		do_u_err,		CH_UD_UCU,
	MASK(ALL_BITS),		BIT(25)|BIT(24),
	MASK(0x1ff),		BIT(8)|BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw uncorrectable ecache error.",

	"uiucu",		do_u_err,		CH_UI_UCU,
	MASK(ALL_BITS),		BIT(26)|BIT(25),
	MASK(0x1ff),		BIT(1)|BIT(0),
	OFFSET(0),		NULL,
	"Cause a user inst sw uncorrectable ecache error.",

	"obpducu",		do_k_err,		CH_OBPD_UCU,
	MASK(ALL_BITS),		BIT(27)|BIT(26),
	MASK(0x1ff),		BIT(2)|BIT(1),
	OFFSET(0),		NULL,
	"Cause an OBP data uncorrectable ecache error.",

	"kducc",		do_k_err,		CH_KD_UCC,
	MASK(ALL_BITS),		BIT(27),
	MASK(0x1ff),		BIT(3),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data sw correctable ecache error.",

	"copyinucc",		do_k_err,		CH_KU_UCCCOPYIN,
	MASK(ALL_BITS),		BIT(28),
	MASK(0x1ff),		BIT(4),
	OFFSET(8),		OFFSET(0),
	"Cause a kern/user copyin correctable ecache error.",

	"kducctl1",		do_k_err,		CH_KD_UCCTL1,
	MASK(ALL_BITS), 	BIT(29),
	MASK(0x1ff),		BIT(5),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data sw correctable ecache error at\n"
	"\t\t\ttrap level 1.",

	"kdoucc",		do_notimp,		CH_KD_OUCC,
	MASK(ALL_BITS), 	BIT(30),
	MASK(0x1ff), 		BIT(6),
	NULL,			NULL,
	"Cause a kern data sw correctable ecache error\n"
	"\t\t\tthat is orphaned.",

	"kiucc",		do_k_err,		CH_KI_UCC,
	MASK(ALL_BITS), 	BIT(31),
	MASK(0x1ff), 		BIT(7),
	OFFSET(64),		NULL,
	"Cause a kern inst sw correctable ecache error.",

	"kiucctl1",		do_k_err,		CH_KI_UCCTL1,
	MASK(ALL_BITS), 	BIT(32),
	MASK(0x1ff), 		BIT(8),
	OFFSET(64),		NULL,
	"Cause a kern inst correctable ecache error at\n"
	"\t\t\ttrap level 1.",

	"kioucc",		do_notimp,		CH_KI_OUCC,
	MASK(ALL_BITS),		BIT(33),
	MASK(0x1ff),		BIT(0),
	NULL,			NULL,
	"Cause a kern inst sw correctable ecache error\n"
	"\t\t\tthat is orphaned.",

	"uducc",		do_u_err,		CH_UD_UCC,
	MASK(ALL_BITS),		BIT(34),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw correctable ecache error.",

	"uiucc",		do_u_err,		CH_UI_UCC,
	MASK(ALL_BITS),		BIT(35),
	MASK(0x1ff),		BIT(2),
	OFFSET(64),		NULL,
	"Cause a user inst sw correctable ecache error.",

	"obpducc",		do_k_err,		CH_OBPD_UCC,
	MASK(ALL_BITS),		BIT(36),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		NULL,
	"Cause an OBP data sw correctable ecache error.",

	/*
	 * E$ block-load and store-merge errors.
	 */

	"kdedul",		do_k_err,		CH_KD_EDUL,
	MASK(ALL_BITS),		BIT(37)|BIT(36),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw uncorrectable ecache error due to\n"
	"\t\t\ta block load.",

	"kdedus",		do_k_err,		CH_KD_EDUS,
	MASK(ALL_BITS),		BIT(38)|BIT(37),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw uncorrectable ecache error due to\n"
	"\t\t\ta store merge.",

	"kdedupr",		do_k_err,		CH_KD_EDUPR,
	MASK(ALL_BITS),		BIT(38)|BIT(37),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw uncorrectable ecache error due to\n"
	"\t\t\ta prefetch.",

	"udedul",		do_u_err,		CH_UD_EDUL,
	MASK(ALL_BITS),		BIT(39)|BIT(38),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw uncorrectable ecache error due to a\n"
	"\t\t\tblock load.",

	"udedus",		do_u_err,		CH_UD_EDUS,
	MASK(ALL_BITS),		BIT(40)|BIT(39),
	MASK(0x1ff),		BIT(8)|BIT(7),
	OFFSET(8),		OFFSET(0),
	"Cause a user data hw uncorrectable ecache error due to a\n"
	"\t\t\tstore merge.",

	"kdedcl",		do_k_err,		CH_KD_EDCL,
	MASK(ALL_BITS),		BIT(40),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data hw corrected ecache error due\n"
	"\t\t\tto block load.",

	"kdedcs",		do_k_err,		CH_KD_EDCS,
	MASK(ALL_BITS),		BIT(41),
	MASK(0x1ff),		BIT(1),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw corrected ecache error due\n"
	"\t\t\tto store merge.",

	"kdedcpr",		do_k_err,		CH_KD_EDCPR,
	MASK(ALL_BITS),		BIT(41),
	MASK(0x1ff),		BIT(2),
	OFFSET(8),		OFFSET(0),
	"Cause a kern data hw corrected ecache error due\n"
	"\t\t\tto prefetch.",

	"udedcl",		do_u_err,		CH_UD_EDCL,
	MASK(ALL_BITS),		BIT(42),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw corrected ecache error due to\n"
	"\t\t\ta block load.",

	"udedcs",		do_u_err,		CH_UD_EDCS,
	MASK(ALL_BITS),		BIT(43),
	MASK(0x1ff),		BIT(4),
	OFFSET(8),		OFFSET(0),
	"Cause a user data hw corrected ecache error due to\n"
	"\t\t\tstore merge.",

	/*
	 * E$ write-back errors.
	 */

	"kdwdu",		do_k_err,		CH_KD_WDU,
	MASK(ALL_BITS),		BIT(44)|BIT(43),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern data uncorrectable ecache write-back error.",

	"kiwdu",		do_k_err,		CH_KI_WDU,
	MASK(ALL_BITS),		BIT(45)|BIT(44),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a kern inst uncorrectable ecache write-back error.",

	"udwdu",		do_ud_wb_err,		CH_UD_WDU,
	MASK(ALL_BITS),		BIT(46)|BIT(45),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(0),		NULL,
	"Cause a user data uncorrectable ecache write-back error.",

	"uiwdu",		do_notimp,		CH_UI_WDU,
	MASK(ALL_BITS),		BIT(47)|BIT(46),
	MASK(0x1ff),		BIT(8)|BIT(7),
	NULL,			NULL,
	"Cause a user instr uncorrectable ecache write-back error.",

	"kdwdc",		do_k_err,		CH_KD_WDC,
	MASK(ALL_BITS),		BIT(47),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		NULL,
	"Cause a kern data correctable ecache write-back error.",

	"kiwdc",		do_k_err,		CH_KI_WDC,
	MASK(ALL_BITS),		BIT(48),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		NULL,
	"Cause a kern inst correctable ecache write-back error.",

	"udwdc",		do_ud_wb_err,		CH_UD_WDC,
	MASK(ALL_BITS), 	BIT(49),
	MASK(0x1ff), 		BIT(2),
	OFFSET(0),		NULL,
	"Cause a user data correctable ecache write-back error.",

	"uiwdc",		do_notimp,		CH_UI_WDC,
	MASK(ALL_BITS), 	BIT(50),
	MASK(0x1ff), 		BIT(3),
	NULL,			NULL,
	"Cause a user instr correctable ecache write-back error.",

	/*
	 * E$ copy-back errors.
	 */

	"kdcpu",		do_k_err,		CH_KD_CPU,
	MASK(ALL_BITS),		BIT(51)|BIT(50),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable ecache copy-back error.",

	"kicpu",		do_k_err,		CH_KI_CPU,
	MASK(ALL_BITS),		BIT(52)|BIT(51),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		NULL,
	"Cause a kern inst uncorrectable ecache copy-back error.",

	"udcpu",		do_u_cp_err,		CH_UD_CPU,
	MASK(ALL_BITS),		BIT(53)|BIT(52),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable ecache copy-back error.",

	"uicpu",		do_notimp,		CH_UI_CPU,
	MASK(ALL_BITS),		BIT(54)|BIT(53),
	MASK(0x1ff),		BIT(7)|BIT(6),
	NULL,			NULL,
	"Cause a user inst uncorrectable ecache copy-back error.",

	"kdcpc",		do_k_err,		CH_KD_CPC,
	MASK(ALL_BITS),		BIT(54),
	MASK(0x1ff),		BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable ecache copy-back error.",

	"kicpc",		do_k_err,		CH_KI_CPC,
	MASK(ALL_BITS),		BIT(55),
	MASK(0x1ff), 		BIT(0),
	OFFSET(0),		NULL,
	"Cause a kern inst correctable ecache copy-back error.",

	"udcpc",		do_u_cp_err,		CH_UD_CPC,
	MASK(ALL_BITS), 	BIT(56),
	MASK(0x1ff), 		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data correctable ecache copy-back error.",

	"uicpc",		do_notimp,		CH_UI_CPC,
	MASK(ALL_BITS), 	BIT(57),
	MASK(0x1ff), 		BIT(2),
	NULL,			NULL,
	"Cause a user inst correctable ecache copy-back error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,		NULL,
	NULL,			NULL,		NULL,
	NULL};

cmd_t cheetah_cmds[] = {

	/*
	 * Mtag errors.
	 */

	"kdemu",		do_k_err,		CH_KD_EMU,
	MASK(0x7),		BIT(1)|BIT(0),
	MASK(0xf),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data uncorrectable mtag error.",

	"kdemc",		do_k_err,		CH_KD_EMC,
	MASK(0x7),		BIT(2),
	MASK(0xf),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable mtag error.",

	/*
	 * Safari bus errors.
	 */

	"kdto",			do_k_err,		CH_KD_TO,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data Safari bus timeout error.",

	"kpeekto",		do_k_err,		CH_KD_TOPEEK,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a kern data ddi_peek() Safari bus timeout error.",

	/*
	 * End of list.
	 */

	NULL,			NULL,		NULL,
	NULL,			NULL,		NULL,
	NULL};

static cmd_t *commands[] = {
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,	/* flush entire L2$ */
	NULL
};

void
ch_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = commands;
}
