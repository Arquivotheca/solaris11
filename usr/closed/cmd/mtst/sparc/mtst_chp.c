/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains Cheetah+ (US-III+) specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include "mtst.h"

/*
 * Cheetah+ specific routines located in this file.
 */
void	chp_init(mdata_t *);
int	chp_pre_test(mdata_t *);

/*
 * These Cheetah+ errors are grouped according to the definitions
 * in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t cheetahp_cmds[] = {
	/*
	 * Bus errors.
	 */

	"kddue",		do_k_err,		CHP_KD_DUE,
	MASK(ALL_BITS),		BIT(3)|BIT(2),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a system bus kernel data error.",

	/*
	 * Internal Processor errors.
	 */
	"no_refsh",		do_k_err,		CHP_NO_REFSH,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a system memory starvation error (IERR).",

	"ec_mh",		do_k_err,		CHP_EC_MH,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Cause a ecache multiway tag hit error (IERR).",

	/*
	 * E$ tag errors.
	 *
	 * Note that the masks used by the tags errors are for an 8M cache,
	 * smaller ecaches have MORE target bits but these are masked out.
	 *		8M = tag[43:24]
	 *		4M = tag[43:23]
	 *		1M = tag[43:21]
	 * State bits are not able to be used as targets.
	 */

	"kdetsce",		do_k_err,		CHP_KD_ETSCE,
	MASK(0xfffff000000ull),	BIT(24),
	MASK(0xff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data sw correctable ecache tag error.",

	"kdetscetl1",		do_k_err,		CHP_KD_ETSCETL1,
	MASK(0xfffff000000ull),	BIT(25),
	MASK(0xff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data sw correctable ecache tag error\n"
	"\t\t\tat trap level 1.",

	"kietsce",		do_k_err,		CHP_KI_ETSCE,
	MASK(0xfffff000000ull),	BIT(26),
	MASK(0xff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr sw correctable ecache tag error.",

	"kietscetl1",		do_k_err,		CHP_KI_ETSCETL1,
	MASK(0xfffff000000ull), 	BIT(27),
	MASK(0xff), 		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr sw correctable ecache tag error\n"
	"\t\t\tat trap level 1.",

	"udetsce",		do_u_err,		CHP_UD_ETSCE,
	MASK(0xfffff000000ull),	BIT(28),
	MASK(0xff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw correctable ecache tag error.",

	"uietsce",		do_u_err,		CHP_UI_ETSCE,
	MASK(0xfffff000000ull),	BIT(29),
	MASK(0xff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr sw correctable ecache tag error.",

	"kdetsue",		do_k_err,		CHP_KD_ETSUE,
	MASK(0xfffff000000ull),	BIT(25)|BIT(24),
	MASK(0xff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data sw uncorrectable ecache tag error.",

	"kdetsuetl1",		do_k_err,		CHP_KD_ETSUETL1,
	MASK(0xfffff000000ull),	BIT(26)|BIT(25),
	MASK(0xff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data sw uncorrectable ecache tag error\n"
	"\t\t\tat trap level 1.",

	"kietsue",		do_k_err,		CHP_KI_ETSUE,
	MASK(0xfffff000000ull),	BIT(26)|BIT(25),
	MASK(0xff), 		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr sw uncorrectable ecache tag error.",

	"kietsuetl1",		do_k_err,		CHP_KI_ETSUETL1,
	MASK(0xfffff000000ull),	BIT(27)|BIT(26),
	MASK(0xff), 		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr sw uncorrectable ecache tag error\n"
	"\t\t\tat trap level 1.",

	"udetsue",		do_u_err,		CHP_UD_ETSUE,
	MASK(0xfffff000000ull),	BIT(28)|BIT(27),
	MASK(0xff), 		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user data sw uncorrectable ecache tag error.",

	"uietsue",		do_u_err,		CHP_UI_ETSUE,
	MASK(0xfffff000000ull),	BIT(29)|BIT(28),
	MASK(0xff), 		BIT(6)|BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr sw uncorrectable ecache tag error.",

	"kdthce",		do_k_err,		CHP_KD_ETHCE,
	MASK(0xfffff000000ull),	BIT(30),
	MASK(0xff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw corrected ecache tag error.",

	"kithce",		do_k_err,		CHP_KI_ETHCE,
	MASK(0xfffff000000ull),	BIT(31),
	MASK(0xff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw corrected ecache tag error.",

	"udthce",		do_u_cp_err,		CHP_UD_ETHCE,
	MASK(0xfffff000000ull),	BIT(32),
	MASK(0xff),		BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw corrected ecache tag error.",

	"uithce",		do_u_cp_err,		CHP_UI_ETHCE,
	MASK(0xfffff000000ull),	BIT(33),
	MASK(0xff),		BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw corrected ecache tag error.",

	"kdthue",		do_k_err,		CHP_KD_ETHUE,
	MASK(0xfffff000000ull),	BIT(34)|BIT(33),
	MASK(0xff),		BIT(1)|BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel data hw uncorrectable ecache tag error.",

	"kithue",		do_k_err,		CHP_KI_ETHUE,
	MASK(0xfffff000000ull),	BIT(35)|BIT(34),
	MASK(0xff),		BIT(2)|BIT(1),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel instr hw uncorrectable ecache tag error.",

	"udthue",		do_u_cp_err,		CHP_UD_ETHUE,
	MASK(0xfffff000000ull),	BIT(36)|BIT(35),
	MASK(0xff),		BIT(3)|BIT(2),
	OFFSET(0),		OFFSET(0),
	"Cause a user data hw uncorrectable ecache tag error.",

	"uithue",		do_u_cp_err,		CHP_UI_ETHUE,
	MASK(0xfffff000000ull),	BIT(37)|BIT(36),
	MASK(0xff),		BIT(4)|BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr hw uncorrectable ecache tag error.",

	/*
	 * D$ errors.
	 */

	"kddspel",		do_k_err,		CHP_KD_DDSPEL,
	MASK(ALL_BITS), 	BIT(0),
	MASK(0xff00), 		BIT(8),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable dcache data parity error\n"
	"\t\t\tdue to a load.",

	"kddspeltl1",		do_k_err,		CHP_KD_DDSPELTL1,
	MASK(ALL_BITS), 	BIT(2),
	MASK(0xff00), 		BIT(10),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable dcache data parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"kddspes",		do_notimp,		CHP_KD_DDSPES,
	MASK(ALL_BITS), 	BIT(1),
	MASK(0xff00), 		BIT(9),
	OFFSET(0), 		OFFSET(0),
	"Cause a kernel sw correctable dcache data parity error\n"
	"\t\t\tdue to a store.",

	"uddspel",		do_notimp,		CHP_UD_DDSPEL,
	MASK(ALL_BITS), 	BIT(3),
	MASK(0xff00), 		BIT(11),
	OFFSET(0), 		OFFSET(0),
	"Cause a user sw correctable dcache data parity error\n"
	"\t\t\tdue to a load.",

	"uddspes",		do_notimp,		CHP_UD_DDSPES,
	MASK(ALL_BITS), 	BIT(4),
	MASK(0xff00), 		BIT(12),
	OFFSET(0), 		OFFSET(0),
	"Cause a user sw correctable dcache data parity error\n"
	"\t\t\tdue to a store.",

	"kdtspel",		do_k_err,		CHP_KD_DTSPEL,
	MASK(0x3FFFFFFE), 	BIT(5),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdtspeltl1",		do_k_err,		CHP_KD_DTSPELTL1,
	MASK(0x3FFFFFFE), 	BIT(7),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load at trap level 1.",

	"kdtspes",		do_notimp,		CHP_KD_DTSPES,
	MASK(0x3FFFFFFE), 	BIT(6),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel sw correctable dcache tag parity error\n"
	"\t\t\tdue to a store.",

	"udtspel",		do_notimp,		CHP_UD_DTSPEL,
	MASK(0x3FFFFFFE), 	BIT(8),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"udtspes",		do_notimp,		CHP_UD_DTSPES,
	MASK(0x3FFFFFFE), 	BIT(9),
	MASK(0x40000000), 	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user sw correctable dcache tag parity error\n"
	"\t\t\tdue to a store.",

	"kdthpel",		do_k_err,		CHP_KD_DTHPEL,
	MASK(0x3FFFFFFE),	BIT(10),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel hw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"kdthpes",		do_notimp,		CHP_KD_DTHPES,
	MASK(0x3FFFFFFE),	BIT(11),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a kernel hw correctable dcache tag parity error\n"
	"\t\t\tdue to a store.",

	"udthpel",		do_notimp,		CHP_UD_DTHPEL,
	MASK(0x3FFFFFFE),	BIT(12),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable dcache tag parity error\n"
	"\t\t\tdue to a load.",

	"udthpes",		do_notimp,		CHP_UD_DTHPES,
	MASK(0x3FFFFFFE),	BIT(13),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		OFFSET(0),
	"Cause a user hw correctable dcache tag parity error\n"
	"\t\t\tdue to a store.",

	"dtphys=addr,xor,data",	do_k_err,		G4U_DTPHYS,
	MASK(0x3FFFFFFE), 	BIT(14),
	MASK(0x40000000),	BIT(30),
	OFFSET(0),		NULL,
	"Insert an error into the dcache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.\n",

	/*
	 * I$ errors.
	 */

	"kidspe",		do_k_err,		CHP_KI_IDSPE,
	MASK(0xffffffff),	BIT(0),
	MASK(0x200000000ull),	BIT(33),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable icache data parity error.",

	"kidspetl1",		do_k_err,		CHP_KI_IDSPETL1,
	MASK(0xffffffff),	BIT(1),
	MASK(0x200000000ull),	BIT(33),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable icache data parity error\n"
	"\t\t\tat trap level 1.",

	"kidspepcr",		do_k_err,		CHP_KI_IDSPEPCR,
	MASK(0xfffff800),	BIT(11),
	MASK(0x200000000ull),	BIT(33),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable icache data (pc-rel) parity error.",

	"uidspe",		do_notimp,		CHP_UI_IDSPE,
	MASK(0xffffffff),	BIT(2),
	MASK(0x200000000ull),	BIT(33),
	OFFSET(0),		NULL,
	"Cause a user sw correctable icache data parity error.",

	"kitspe",		do_k_err,		CHP_KI_ITSPE,
	MASK(0x1FFFFFFF00ull),	BIT(8),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable icache tag parity error.",

	"kitspetl1",		do_k_err,		CHP_KI_ITSPETL1,
	MASK(0x1FFFFFFF00ull),	BIT(9),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel sw correctable icache tag parity error\n"
	"\t\t\tat trap level 1.",

	"uitspe",		do_notimp,		CHP_UI_ITSPE,
	MASK(0x1FFFFFFF00ull),	BIT(10),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a user sw correctable icache tag parity error.",

	"kithpe",		do_k_err,		CHP_KI_ITHPE,
	MASK(0x1FFFFFFF00ull),	BIT(11),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a kernel hw correctable icache tag parity error.",

	"uithpe",		do_notimp,		CHP_UI_ITHPE,
	MASK(0x1FFFFFFF00ull),	BIT(12),
	MASK(0x2000000000ull),	BIT(37),
	OFFSET(0),		NULL,
	"Cause a user hw correctable icache tag parity error.",

	"itphys=addr,xor,data",	do_k_err,		G4U_ITPHYS,
	MASK(0x1FFFFFFF00ull), 	BIT(13),
	MASK(0x2000000000ull), 	BIT(37),
	NULL,			NULL,
	"Insert an error into the icache tag at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.",

	/*
	 * End of list.
	 */

	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};


static cmd_t *chp_commands[] = {
	cheetahp_cmds,
	cheetah_cmds,
	us3_generic_cmds,
	sun4u_generic_cmds,
	NULL
};

static  opsvec_t operations = {
	gen_flushall_l2,		/* flush entire L2$ */
	chp_pre_test,			/* pre-test routine */
	NULL				/* no post-test routine */
};

void
chp_init(mdata_t *mdatap)
{
	mdatap->m_opvp = &operations;
	mdatap->m_cmdpp = chp_commands;
}

int
chp_pre_test(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	switch (IOC_COMMAND(iocp)) {
		case CHP_UD_ETHCE:
		case CHP_UI_ETHCE:
		case CHP_UD_ETHUE:
		case CHP_UI_ETHUE:
		case CHP_KD_DTHPEL:
			iocp->ioc_nthreads = 2;
			break;
		default:
			break;
	}

	return (0);
}
